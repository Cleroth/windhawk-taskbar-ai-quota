// Provider status reads on the fetch thread, and what the taskbar shows of them. A read is an
// unauthenticated GET of a public status page (fetch/status-feeds.h), made only by the taskbar's
// process, only while Show provider outages is on and only for a provider with a visible account.
// Nothing is notified.

// Bumped by settings publication when Show provider outages changes: what was read before shows
// nothing, and the next poll starts over, as if it had seen the setting off.
static std::atomic<uint64_t> g_providerStatusPolicy{0};

// What each provider's accounts show, indexed like kProviderStatusSources: the shown state, the
// read it comes from (GetTickCount64; 0: none) and the policy generation it was read under.
// g_providerStatusMutex is a leaf: copy in or out, never acquire another lock while holding it.
struct ProviderStatusEntry {
    ProviderOutage shown;
    ULONGLONG readTick = 0;
    uint64_t policy = 0;
};
static std::mutex g_providerStatusMutex;
static std::array<ProviderStatusEntry, std::size(kProviderStatusSources)> g_providerStatus;

static constexpr DWORD kStatusRequestTimeoutMs = 3000;
static constexpr ULONGLONG kStatusRetryAfterCapMs = 60ULL * 60 * 1000;

// Taskbar UI threads: what `provider`'s accounts show now; nothing once its reading is older than
// two of its intervals, or from before the setting last changed. Takes only the leaf lock.
static ProviderOutage CurrentProviderOutage(std::wstring_view provider) {
    const ProviderStatusSource* source = FindProviderStatusSource(provider);
    if (!source) return {};
    ProviderStatusEntry entry;
    {
        std::lock_guard<std::mutex> lock(g_providerStatusMutex);
        entry = g_providerStatus[size_t(source - kProviderStatusSources)];
    }
    bool current = entry.policy == g_providerStatusPolicy.load() &&
                   ProviderStatusCurrent(*source, entry.readTick, GetTickCount64());
    return current ? entry.shown : ProviderOutage{};
}

// The fetch thread's state of every provider's reads: their records, when each is due
// (GetTickCount64; 0: now), the problem last logged, and the policy generation they belong to.
struct ProviderStatusPoll {
    std::array<ProviderStatusTrack, std::size(kProviderStatusSources)> tracks;
    std::array<ULONGLONG, std::size(kProviderStatusSources)> dueTick{};
    std::array<std::wstring, std::size(kProviderStatusSources)> logged;
    uint64_t policy = 0;
};

// Fetch thread, once per round after the quota polls: reads each eligible provider's page that is
// due, publishes what its accounts show, and updates the taskbars when that changed. A provider that
// isn't eligible shows nothing and is read as soon as it is again. A pending manual refresh ends the
// round's reads early: they resume right after it. Returns when the next read is due
// (GetTickCount64; 0: none).
static ULONGLONG PollProviderStatuses(ProviderStatusPoll& poll) {
    constexpr size_t kCount = std::size(kProviderStatusSources);
    // On, and a visible account of the provider, from the current settings.
    auto eligible = [](const ProviderStatusSource& source) {
        std::lock_guard<std::mutex> lock(g_settingsMutex);
        return g_settings.showProviderOutages &&
               std::any_of(g_settings.accounts.begin(), g_settings.accounts.end(), [&](const AccountConfig& account) {
                   return !account.hidden && account.provider == source.provider;
               });
    };
    bool changed = false;
    const uint64_t policy = g_providerStatusPolicy.load();
    auto publish = [&](size_t i, const ProviderOutage& shown, ULONGLONG readTick) {
        const ProviderStatusSource& source = kProviderStatusSources[i];
        std::lock_guard<std::mutex> lock(g_providerStatusMutex);
        ProviderStatusEntry& entry = g_providerStatus[i];
        bool shownBefore = entry.policy == policy && ProviderStatusCurrent(source, entry.readTick, GetTickCount64());
        if ((shownBefore ? entry.shown : ProviderOutage{}) != shown) changed = true;
        entry = {shown, readTick, policy};
    };
    auto log = [&](size_t i, const std::wstring& problem) {
        if (problem == poll.logged[i]) return;
        if (problem.empty()) {
            Wh_Log(L"Provider status %s: readable again", kProviderStatusSources[i].provider);
        } else {
            Wh_Log(L"Provider status %s: %s", kProviderStatusSources[i].provider, problem.c_str());
        }
        poll.logged[i] = problem;
    };
    auto reset = [&](size_t i) {
        publish(i, {}, 0);
        poll.tracks[i] = {};
        poll.dueTick[i] = 0;
    };

    if (policy != poll.policy) {
        for (size_t i = 0; i < kCount; i++) reset(i);
        poll.policy = policy;
    }
    ULONGLONG next = 0;
    auto due = [&](ULONGLONG tick) {
        if (!next || tick < next) next = tick;
    };
    for (size_t i = 0; i < kCount && !g_unloading; i++) {
        const ProviderStatusSource& source = kProviderStatusSources[i];
        if (!eligible(source)) {
            reset(i);
            continue;
        }
        ULONGLONG now = GetTickCount64();
        if (poll.dueTick[i] > now) {
            due(poll.dueTick[i]);
            continue;
        }
        // A pending manual refresh, a changed setting or a lost taskbar ends the round's reads; the
        // next round goes on, starting over after a setting change.
        bool refreshPending;
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            refreshPending = !g_refreshRequests.empty();
        }
        if (refreshPending || policy != g_providerStatusPolicy.load() || !IsTaskbarOwnerProcess()) {
            due(now);
            break;
        }

        HttpResult response = HttpRequest(L"GET", source.host, source.path, L"taskbar-ai-quota",
                                          L"Accept: application/json\r\n", {}, kStatusRequestTimeoutMs);
        if (g_unloading) break;
        std::wstring problem;
        std::optional<ProviderOutage> read;
        if (!response.ok) {
            problem = L"network error";
        } else if (response.status != 200) {
            problem = L"HTTP " + std::to_wstring(response.status);
        } else {
            read = ParseProviderStatus(source, response.body, &problem);
        }
        // Turned off, or its last visible account hidden, while the page was read: discarded. A
        // change after this check stamps the reading with an older policy, which shows nothing.
        if (policy != g_providerStatusPolicy.load()) {
            due(GetTickCount64());
            break;
        }
        if (!eligible(source)) {
            reset(i);
            continue;
        }
        ULONGLONG tick = GetTickCount64();
        ProviderStatusTrack& track = poll.tracks[i];
        publish(i, ShownAfterRead(source, track, read, tick), read ? tick : 0);
        log(i, problem);
        ULONGLONG delay = track.delayMs;
        if (response.ok && (response.status == 429 || response.status == 503) && response.retryAfterSec > 0) {
            delay = std::max(delay, std::min((ULONGLONG)response.retryAfterSec * 1000, kStatusRetryAfterCapMs));
        }
        poll.dueTick[i] = tick + delay;
        due(poll.dueTick[i]);
    }
    if (changed) PostUiUpdate();
    return next;
}
