// Published burn-up curves: fetch-thread rebuilds, invalidation and the flyout's accessor.

// Guarded by g_dataMutex. The fetch thread publishes curves; settings publication and Clear
// history only reset the store.
static BurnUpStore g_burnUpCurves;
// Bumped with g_settingsMutex and g_dataMutex held when consent, retention or a history ID's
// presence, provider or recording changes. Hiding and quota selection keep valid curves.
static std::atomic<ULONGLONG> g_burnUpPolicyGeneration{0};
// Asks the fetch thread to rebuild every recorded account from local history only.
static std::atomic<bool> g_burnUpRebuildRequested{false};

// Drops published curves and requests a full local rebuild. Caller holds g_dataMutex, then
// updates the UI and wakes the fetch thread once it holds no locks.
static void ResetBurnUpCurvesLocked() {
    g_burnUpCurves = {};
    g_burnUpRebuildRequested = true;
}

// Fetch thread only, after AppendHistory while this shell records. Under g_historyMutex the
// flagged recorded accounts' tails in `cache` copy only what was appended since the last round
// (a full scan of their current windows after an invalidation or journal change; never loading
// the journal). Curves are built outside the lock and published unless Clear or a policy change
// superseded the copy or `policyGeneration`'s settings snapshot. Returns whether the store was
// published.
static bool RebuildBurnUpCurves(BurnUpSampleCache& cache, const std::vector<AccountConfig>& accounts,
                                const std::vector<AccountData>& readings,
                                const std::vector<bool>& rebuild, bool replaceAll,
                                int retentionDays, ULONGLONG policyGeneration) {
    try {
        std::vector<BurnUpRequest> targets;
        ULONGLONG now = NowUnixMs();
        ULONGLONG retained = HistoryRetentionStart(retentionDays, now);
        for (size_t i = 0; i < accounts.size() && i < readings.size() && i < rebuild.size(); i++) {
            const AccountConfig& account = accounts[i];
            if (!rebuild[i] || !account.recordHistory || !account.historyId) continue;
            BurnUpRequest& target = targets.emplace_back();
            target.historyId = account.historyId;
            target.provider = HistoryProviderCode(account.provider);
            // Back to the earliest start among the displayed windows, within retention.
            const AccountData& data = readings[i];
            for (const WindowUsage* usage : {&data.win5h, &data.winWeek, &data.fableWeek,
                                             &data.extraUsage, &data.antigravityThirdParty5h,
                                             &data.antigravityThirdPartyWeek}) {
                ULONGLONG duration = usage->windowDurationMs;
                if (usage->pct < 0 || !duration || duration > kBurnUpMaxWindowMs ||
                    usage->resetUnixMs <= duration) continue;
                ULONGLONG start = usage->resetUnixMs - duration;
                target.from = std::min(target.from, start > kHistoryResetToleranceMs ?
                                                        start - kHistoryResetToleranceMs : 0);
            }
            if (target.from != ULLONG_MAX) target.from = std::max(target.from, retained);
        }

        uint64_t epoch;
        {
            std::lock_guard lock(g_historyMutex);
            epoch = g_historyEpoch.load();
            UpdateBurnUpTails(cache, g_historySamples, epoch, g_historyRevision,
                              g_historyAppendCount, targets, replaceAll);
        }
        if (g_unloading) {
            cache = {};  // A refill may have stopped part-way.
            return false;
        }

        std::vector<BurnUpAccountCurves> built;
        for (const auto& target : targets) {
            if (g_unloading) return false;
            auto tail = std::find_if(cache.accounts.begin(), cache.accounts.end(),
                [&](const BurnUpAccountTail& candidate) { return candidate.historyId == target.historyId; });
            BurnUpAccountCurves& curves = built.emplace_back();
            curves.historyId = target.historyId;
            curves.provider = target.provider;
            for (int meter = 0; meter < kBurnUpMeterCount && tail != cache.accounts.end(); meter++) {
                curves.meters[meter] = BuildBurnUpCurve(tail->samples, meter);
            }
        }
        std::lock_guard<std::mutex> lock(g_dataMutex);
        return !g_unloading &&
               PublishBurnUpCurves(g_burnUpCurves, std::move(built), replaceAll, epoch,
                                   policyGeneration, g_historyEpoch.load(),
                                   g_burnUpPolicyGeneration.load());
    } catch (...) {
        Wh_Log(L"Burn-up curve rebuild failed");
        cache = {};
        return false;
    }
}

// After Clear history (which changes the history epoch): drops published curves, updates the
// UI and asks the fetch thread for a local rebuild, without a provider poll or deadline change.
// Call holding no locks.
static void InvalidateBurnUpCurves() {
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        ResetBurnUpCurvesLocked();
    }
    PostUiUpdate();
    if (g_refreshEvent) SetEvent(g_refreshEvent);
}

// UI thread: copies the current-window curve for a flyout row's displayed reading. Nothing when
// the row has no chart: no window (credits, unknown duration), an account this shell doesn't
// actively record, curves from before Clear or a recording change, or another window than the
// reading's. Takes g_settingsMutex then g_dataMutex; call it holding neither.
static std::optional<BurnUpCurve> CopyBurnUpCurve(uint64_t historyId, int flyoutRow,
                                                  const WindowUsage& reading) {
    // The extra row's curve is Anthropic extra usage or GitHub Copilot's AI credits; OpenAI
    // credits are not a windowed meter.
    int meter = flyoutRow == kFlyoutFiveHourRow ? 0 : flyoutRow == kFlyoutWeeklyRow ? 1 :
                flyoutRow == kFlyoutFableRow ? 2 : flyoutRow == kFlyoutExtraRow ? 3 :
                flyoutRow == kFlyoutPoolShortRow ? 4 : flyoutRow == kFlyoutPoolWeeklyRow ? 5 : -1;
    if (meter < 0 || !historyId || reading.pct < 0) return std::nullopt;
    std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
    auto account = std::find_if(g_settings.accounts.begin(), g_settings.accounts.end(),
        [&](const AccountConfig& candidate) { return candidate.historyId == historyId; });
    if (!g_settings.historyEnabled || g_historyIdsPendingSave || account == g_settings.accounts.end() ||
        !account->recordHistory || (account->hidden && g_settings.skipHiddenPolling)) {
        return std::nullopt;
    }
    uint32_t provider = HistoryProviderCode(account->provider);
    int retentionDays = g_settings.historyRetentionDays;
    std::lock_guard<std::mutex> dataLock(g_dataMutex);
    const BurnUpCurve* curve = FindBurnUpCurve(g_burnUpCurves, g_historyEpoch.load(),
                                               g_burnUpPolicyGeneration.load(), historyId, provider,
                                               meter, reading.resetUnixMs, reading.windowDurationMs);
    if (!curve) return std::nullopt;
    // Retention maintenance can prune readings between rebuilds; never chart them.
    BurnUpCurve copy = *curve;
    ULONGLONG retained = HistoryRetentionStart(retentionDays, NowUnixMs());
    std::erase_if(copy.points, [&](const BurnUpPoint& point) { return point.timeMs < retained; });
    if (copy.points.empty()) return std::nullopt;
    copy.points.front().connect = false;
    return copy;
}
