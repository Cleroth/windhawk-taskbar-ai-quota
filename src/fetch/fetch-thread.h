// Per-account usage fetch and the polling loop that publishes results and history.

// Optional account details the fetch thread keeps between polls, read through the sign-in of
// `signInGeneration`: the Anthropic plan (OAuth profile) and the next expiry of the Codex free
// resets, read for `resetsCount` of them. A failed read waits for its own retry time and never
// fails the usage read.
struct AccountDetailsCache {
    uint64_t signInGeneration = 0;
    std::wstring plan;
    ULONGLONG planReadMs = 0;
    ULONGLONG planRetryMs = 0;
    int resetsCount = 0;
    ULONGLONG resetsExpiryMs = 0;
    ULONGLONG resetsReadMs = 0;
    ULONGLONG resetsRetryMs = 0;
};
static constexpr ULONGLONG kPlanCacheMs = 6ULL * 60 * 60 * 1000;
static constexpr ULONGLONG kResetsExpiryCacheMs = 3ULL * 60 * 60 * 1000;
static constexpr ULONGLONG kDetailsRetryMs = 2ULL * 60 * 60 * 1000;  // Unless a 429 says when.

// Returns the sign-in generation the result was read through. Once that sign-in is replaced,
// removed or moved, the result is that login's, whatever it says (a rejected refresh included):
// the fetch thread polls again rather than publish it. A refreshed token's stale save means just
// that.
static uint64_t FetchAccount(const AccountConfig& acc, AccountData* d, int* retryAfterSec,
                             AccountDetailsCache* details) {
    d->error.clear();
    d->retryDeadlineMs = 0;
    d->needsLogin = false;
    uint64_t idHash = AccountIdentityHash(acc);

    // Antigravity uses local language server discovery, not OAuth.
    if (acc.provider == L"antigravity") {
        FetchAntigravityAccount(d);
        return CurrentSignInGeneration(idHash);
    }

    bool anthropic = acc.provider == L"anthropic";
    bool openAi = acc.provider == L"openai";
    bool copilot = acc.provider == L"copilot";
    bool opencode = acc.provider == L"opencode";  // An API key: never expires, never refreshes.
    ULONGLONG authEpoch = 0;
    uint64_t signInGeneration = 0;
    StoredToken tok;
    bool signedIn = false;
    {
        // The token belongs to the epoch and sign-in generation read with it. An account renamed
        // or removed since the round began reads none: a rename's failed delete leaves a copy of
        // the moved sign-in, whose single-use refresh token must not be spent.
        std::lock_guard<std::mutex> lk(g_authEpochMutex);
        authEpoch = AuthEpochLocked(idHash);
        signInGeneration = SignInGenerationLocked(idHash);
        bool configured;
        {
            std::lock_guard<std::mutex> settingsLock(g_settingsMutex);
            configured = std::any_of(g_settings.accounts.begin(), g_settings.accounts.end(),
                [&](const AccountConfig& account) { return AccountIdentityHash(account) == idHash; });
        }
        signedIn = configured && LoadStoredToken(idHash, &tok) && !tok.accessToken.empty();
    }
    if (!signedIn) {
        d->stale = true;
        d->needsLogin = true;
        d->error = L"not signed in - click to sign in";
        return signInGeneration;
    }
    // GitHub refreshes through its own form-encoded endpoint.
    auto refresh = [&](std::wstring* err, int* retryAfterSec) {
        return copilot ? RefreshCopilotToken(&tok, err, retryAfterSec) :
                         RefreshToken(acc.provider, &tok, err, retryAfterSec);
    };

    // Refresh just before expiry so a request rarely races the token going stale.
    if (tok.expiresMs && tok.expiresMs < NowUnixMs() + 60000) {
        std::wstring refreshErr;
        int refreshRetryAfter = 0;
        TokenEndpointResult refreshResult = refresh(&refreshErr, &refreshRetryAfter);
        if (refreshResult == TokenEndpointResult::Success) {
            uint64_t savedIdentity = 0;
            TokenSaveResult saved =
                SaveStoredTokenIfCurrent(idHash, authEpoch, tok, &savedIdentity);
            if (saved != TokenSaveResult::Saved) {
                d->stale = true;
                d->error = saved == TokenSaveResult::Stale ? L"token refresh failed" :
                                                             L"could not save refreshed token";
                return signInGeneration;
            }
            if (savedIdentity != idHash) RefreshQuotaByIdentity(savedIdentity);
        } else {
            d->stale = true;
            if (refreshResult == TokenEndpointResult::Rejected) {
                d->needsLogin = true;
                d->error = L"session expired - click to sign in";
            } else {
                d->error = L"token refresh failed";
                if (!refreshErr.empty()) d->error += L": " + refreshErr;
                *retryAfterSec = refreshRetryAfter > 0 ? refreshRetryAfter : 120;
            }
            return signInGeneration;
        }
    }

    // Shared by the usage request and the account details requests.
    auto anthropicHeaders = [](const StoredToken& t) {
        return L"Authorization: Bearer " + t.accessToken +
               L"\r\nanthropic-beta: oauth-2025-04-20"
               L"\r\nAccept: application/json\r\n";
    };
    auto openAiHeaders = [](const StoredToken& t) {
        std::wstring headers = L"Authorization: Bearer " + t.accessToken +
                               L"\r\nOrigin: https://chatgpt.com"
                               L"\r\nReferer: https://chatgpt.com/"
                               L"\r\nAccept: application/json\r\n";
        if (!t.accountId.empty()) headers += L"ChatGPT-Account-Id: " + t.accountId + L"\r\n";
        return headers;
    };
    auto requestUsage = [&](const StoredToken& t) -> HttpResult {
        if (anthropic) {
            // cedar_ember=1 adds the free resets.
            return HttpRequest(L"GET", L"api.anthropic.com", L"/api/oauth/usage?cedar_ember=1",
                               kClaudeCliUserAgent, anthropicHeaders(t));
        }
        if (copilot) {
            std::wstring headers = L"Authorization: token " + t.accessToken +
                                   L"\r\nAccept: application/json"
                                   L"\r\nX-GitHub-Api-Version: 2025-04-01\r\n";
            return HttpRequest(L"GET", L"api.github.com", L"/copilot_internal/user", kOAuthUserAgent,
                               headers);
        }
        if (opencode) {
            std::wstring headers = L"Authorization: Bearer " + t.accessToken +
                                   L"\r\nAccept: application/json\r\n";
            return HttpRequest(L"GET", L"opencode.ai", L"/zen/go/v1/usage", kOAuthUserAgent, headers);
        }
        return HttpRequest(L"GET", L"chatgpt.com", L"/backend-api/wham/usage",
                           L"taskbar-ai-quota/0.1", openAiHeaders(t));
    };

    HttpResult r = requestUsage(tok);
    // Reactive refresh: the access token may have been revoked or expired early.
    if (r.ok && r.status == 401 && !tok.refreshToken.empty()) {
        std::wstring refreshErr;
        int refreshRetryAfter = 0;
        TokenEndpointResult refreshResult = refresh(&refreshErr, &refreshRetryAfter);
        if (refreshResult == TokenEndpointResult::Success) {
            uint64_t savedIdentity = 0;
            TokenSaveResult saved =
                SaveStoredTokenIfCurrent(idHash, authEpoch, tok, &savedIdentity);
            if (saved != TokenSaveResult::Saved) {
                d->stale = true;
                d->error = saved == TokenSaveResult::Stale ? L"token refresh failed" :
                                                             L"could not save refreshed token";
                return signInGeneration;
            }
            if (savedIdentity != idHash) RefreshQuotaByIdentity(savedIdentity);
            r = requestUsage(tok);
        } else if (refreshResult == TokenEndpointResult::TransientFailure) {
            d->stale = true;
            d->error = L"token refresh failed";
            if (!refreshErr.empty()) d->error += L": " + refreshErr;
            *retryAfterSec = refreshRetryAfter > 0 ? refreshRetryAfter : 120;
            return signInGeneration;
        }
    }

    if (!r.ok) {
        d->stale = true;
        d->error = L"network error";
        return signInGeneration;
    }
    if (r.status == 401) {
        d->stale = true;
        d->needsLogin = true;
        d->error = L"unauthorized - click to sign in";
        return signInGeneration;
    }
    // GitHub's rate limits answer 403 with Retry-After or a message naming the rate limit; any
    // other 403 means the signed-in GitHub account has no Copilot seat or plan.
    bool githubRateLimited = false;
    if (copilot && r.status == 403) {
        std::string message = r.body;
        std::transform(message.begin(), message.end(), message.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        githubRateLimited = r.retryAfterSec > 0 || message.find("rate limit") != std::string::npos;
    }
    if (r.status == 429 || githubRateLimited) {
        d->stale = true;
        d->error = L"rate limited by API";
        *retryAfterSec = r.retryAfterSec > 0 ? r.retryAfterSec : 120;
        return signInGeneration;
    }
    if (copilot && r.status == 403) {
        d->stale = true;
        d->error = L"no Copilot access on this GitHub account";
        return signInGeneration;
    }
    // OpenCode's EntitlementError: the key's workspace has no Go subscription. Other 403s (an edge
    // block, say) stay plain HTTP errors.
    if (opencode && r.status == 403) {
        bool entitlement = false;
        try {
            entitlement = GetStr(GetObj(JsonObject::Parse(Utf8ToWide(r.body)), L"error"), L"type") ==
                          L"EntitlementError";
        } catch (...) {}
        if (entitlement) {
            d->stale = true;
            d->error = L"no OpenCode Go subscription on this key";
            return signInGeneration;
        }
    }
    if (r.status != 200) {
        d->stale = true;
        d->error = L"HTTP " + std::to_wstring(r.status);
        return signInGeneration;
    }

    AccountData fresh;
    std::wstring parseError;
    bool parsed = anthropic ? ParseAnthropicUsage(r.body, &fresh, &parseError) :
                  copilot ? ParseCopilotUsage(r.body, &fresh, &parseError) :
                  opencode ? ParseOpenCodeUsage(r.body, &fresh, &parseError) :
                             ParseOpenAiUsage(r.body, &fresh, &parseError);
    if (!parsed) {
        d->stale = true;
        d->error = parseError.empty() ? L"unexpected response format" : parseError;
        return signInGeneration;
    }

    // Account details through the same token: the Anthropic plan, re-read after kPlanCacheMs,
    // and while Codex has free resets their next expiry, re-read when their count changes or
    // after kResetsExpiryCacheMs.
    AccountDetailsCache next;
    if (details->signInGeneration == signInGeneration) next = *details;
    next.signInGeneration = signInGeneration;
    ULONGLONG nowMs = NowUnixMs();
    auto retryAt = [nowMs](const HttpResult& failed) {
        return nowMs + (failed.ok && failed.status == 429 && failed.retryAfterSec > 0 ?
                            (ULONGLONG)failed.retryAfterSec * 1000 : kDetailsRetryMs);
    };
    if (anthropic && !g_unloading && nowMs >= next.planRetryMs &&
        (!next.planReadMs || nowMs - next.planReadMs >= kPlanCacheMs)) {
        HttpResult profile = HttpRequest(L"GET", L"api.anthropic.com", L"/api/oauth/profile",
                                         kClaudeCliUserAgent, anthropicHeaders(tok));
        std::optional<std::wstring> plan;
        if (profile.ok && profile.status == 200) plan = ParseAnthropicPlan(profile.body);
        if (plan) {
            next.plan = std::move(*plan);
            next.planReadMs = nowMs;
            next.planRetryMs = 0;
        } else {
            next.planRetryMs = retryAt(profile);
        }
    }
    if (openAi && fresh.freeResets <= 0) next.resetsCount = 0;  // Any later ones are new.
    if (openAi && fresh.freeResets > 0 && !g_unloading && nowMs >= next.resetsRetryMs &&
        (next.resetsCount != fresh.freeResets || nowMs - next.resetsReadMs >= kResetsExpiryCacheMs)) {
        HttpResult credits = HttpRequest(L"GET", L"chatgpt.com",
                                         L"/backend-api/wham/rate-limit-reset-credits",
                                         L"taskbar-ai-quota/0.1", openAiHeaders(tok));
        std::optional<ULONGLONG> expiryMs;
        if (credits.ok && credits.status == 200) expiryMs = ParseOpenAiResetsExpiry(credits.body);
        if (expiryMs) {
            next.resetsCount = fresh.freeResets;
            next.resetsExpiryMs = *expiryMs;
            next.resetsReadMs = nowMs;
            next.resetsRetryMs = 0;
        } else {
            next.resetsRetryMs = retryAt(credits);
        }
    }
    // After a sign-in change meanwhile, what was read may belong to either login.
    if (CurrentSignInGeneration(idHash) == signInGeneration) {
        if (anthropic) fresh.plan = next.plan;
        if (openAi && next.resetsCount == fresh.freeResets) {
            fresh.freeResetsExpiryMs = next.resetsExpiryMs;
        }
        *details = std::move(next);
    }

    ApplyCreditsMax(acc, &fresh);
    fresh.stale = false;
    fresh.lastSuccessMs = NowUnixMs();
    *d = std::move(fresh);
    return signInGeneration;
}

static DWORD WINAPI FetchThreadProc(LPVOID) {
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartmentInitialized = true;
    } catch (...) {}

    std::vector<std::wstring> lastLoggedErrorStates;
    std::vector<uint64_t> retryIdentityHashes;
    std::vector<ULONGLONG> retryDeadlineMs;
    std::vector<ULONGLONG> nextPollDeadlineMs;
    // The sign-in generation each account's last poll read its token with.
    std::vector<uint64_t> pollSignInGeneration;
    std::vector<AccountDetailsCache> accountDetails;
    // Per-account notification arm state at the chosen threshold, indexed by QuotaBarIndex:
    // -1 unknown (primes without firing), 0 below/armed, 1 above/already notified.
    std::vector<std::array<int, kQuotaBarCount>> armState;
    // The chosen level's threshold; Off has none to reach, its value only primes.
    auto notificationThresholdOf = [](const Settings& s) {
        return s.notificationLevel == NotificationLevel::Yellow ? s.yellowThreshold :
               s.notificationLevel == NotificationLevel::Orange ? s.orangeThreshold : s.redThreshold;
    };
    // The toasts' app identity may be registered, by this shell or an earlier one that ended
    // without unloading. The taskbar's process removes it while notifications are off and at
    // unload; a failed removal retries.
    bool toastAppIdMayExist = true;
    ULONGLONG lastLoggedSettingsGeneration = 0;
    const uint64_t historySession = NowUnixMs();
    bool curvesRecording = false;  // Recording was active last round, so curves may be published.
    BurnUpSampleCache curveSamples;  // Journal tails of recorded accounts, kept between rounds.
    ProviderStatusPoll providerStatus;  // Status page reads, on their own cadence.
    ULONGLONG fastRecheckUntilMs = GetTickCount64() + 60000;
    bool waitingForTaskbar = false;
    while (!g_unloading) {
        // Another Explorer process would duplicate requests and notifications and race the
        // shell over single-use refresh tokens: it makes no requests until it owns the taskbar.
        // Init can precede the shell's taskbar, and a starting taskbar wakes this thread before
        // its window may exist, so recheck often for a while after either.
        if (!IsTaskbarOwnerProcess()) {
            if (!waitingForTaskbar) Wh_Log(L"This process doesn't own the taskbar; not polling");
            waitingForTaskbar = true;
            DWORD recheckMs = GetTickCount64() < fastRecheckUntilMs ? 500 : 10000;
            HANDLE handles[2] = {g_stopEvent, g_refreshEvent};
            DWORD wait = WaitForMultipleObjects(2, handles, FALSE, recheckMs);
            if (wait == WAIT_OBJECT_0) break;
            if (wait == WAIT_OBJECT_0 + 1) fastRecheckUntilMs = GetTickCount64() + 60000;
            continue;
        }
        if (waitingForTaskbar) Wh_Log(L"This process owns the taskbar; polling");
        waitingForTaskbar = false;
        if (g_historyIdsPendingSave) {
            // Init can run before the shell creates its taskbar. Persist the exact IDs
            // under the normal edit lock before any sample uses them, without history IO.
            std::lock_guard configLock(g_configEditMutex);
            Settings pending;
            {
                std::lock_guard settingsLock(g_settingsMutex);
                pending = g_settings;
            }
            if (SaveOwnedSettings(pending)) {
                g_historyIdsPendingSave = false;
                NotifySettingsWindowChanged();
            }
        }
        ULONGLONG refreshGeneration;
        std::vector<std::pair<uint64_t, ULONGLONG>> refreshRequests;
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            refreshGeneration = g_refreshGeneration;
            refreshRequests = g_refreshRequests;
        }
        std::vector<AccountConfig> accounts;
        std::vector<AccountData> results;
        int intervalMin, notificationThreshold;
        bool notificationsOn, historyEnabled, skipHiddenPolling;
        int historyRetentionDays;
        ULONGLONG settingsGeneration, historyContinuityGeneration, curvePolicyGeneration;
        const uint64_t historyEpoch = g_historyEpoch.load();
        // Taken before the snapshot: a later invalidation requests another rebuild and makes
        // this round's curves stale.
        bool rebuildAllCurves = g_burnUpRebuildRequested.exchange(false);
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            accounts = g_settings.accounts;
            intervalMin = g_settings.pollMinutes;
            notificationThreshold = notificationThresholdOf(g_settings);
            notificationsOn = g_settings.notificationLevel != NotificationLevel::Off;
            historyEnabled = g_settings.historyEnabled && !g_historyIdsPendingSave;
            historyRetentionDays = g_settings.historyRetentionDays;
            skipHiddenPolling = g_settings.skipHiddenPolling;
            settingsGeneration = g_settingsGeneration;
            historyContinuityGeneration = g_historyContinuityGeneration;
            curvePolicyGeneration = g_burnUpPolicyGeneration.load();
            // The published data, of these accounts.
            std::lock_guard<std::mutex> dataLock(g_dataMutex);
            results = g_data.size() == accounts.size() ? g_data :
                                                         std::vector<AccountData>(accounts.size());
        }
        if (!notificationsOn) {
            if (g_notifyWnd) RemoveNotifyIcon();
            if (toastAppIdMayExist) toastAppIdMayExist = !RemoveToastAppId();
        }
        ULONGLONG nextHistoryMaintenanceMs = historyEnabled ? MaintainHistory(true) : 0;
        bool settingsChanged = lastLoggedSettingsGeneration != settingsGeneration ||
                               lastLoggedErrorStates.size() != accounts.size();
        if (settingsChanged) {
            lastLoggedErrorStates.assign(accounts.size(), {});

            // Remap provider-directed Retry-After, normal poll deadlines, the sign-in they were
            // read through and account details by stable account identity so settings changes
            // don't cause another request for unchanged accounts.
            auto oldIdentityHashes = std::move(retryIdentityHashes);
            auto oldRetryDeadlineMs = std::move(retryDeadlineMs);
            auto oldNextPollDeadlineMs = std::move(nextPollDeadlineMs);
            auto oldPollSignInGeneration = std::move(pollSignInGeneration);
            auto oldAccountDetails = std::move(accountDetails);
            std::vector<bool> oldDeadlineUsed(oldIdentityHashes.size(), false);
            retryIdentityHashes.resize(accounts.size());
            retryDeadlineMs.assign(accounts.size(), 0);
            nextPollDeadlineMs.assign(accounts.size(), 0);
            pollSignInGeneration.assign(accounts.size(), 0);
            accountDetails.assign(accounts.size(), {});
            for (size_t i = 0; i < accounts.size(); ++i) {
                retryIdentityHashes[i] = AccountIdentityHash(accounts[i]);
                for (size_t j = 0; j < oldIdentityHashes.size() &&
                                    j < oldRetryDeadlineMs.size(); ++j) {
                    if (!oldDeadlineUsed[j] &&
                        oldIdentityHashes[j] == retryIdentityHashes[i]) {
                        retryDeadlineMs[i] = oldRetryDeadlineMs[j];
                        if (j < oldPollSignInGeneration.size()) {
                            pollSignInGeneration[i] = oldPollSignInGeneration[j];
                        }
                        if (j < oldAccountDetails.size()) {
                            accountDetails[i] = std::move(oldAccountDetails[j]);
                        }
                        if (j < oldNextPollDeadlineMs.size()) {
                            nextPollDeadlineMs[i] = oldNextPollDeadlineMs[j];
                            ULONGLONG latestDeadline = NowUnixMs() +
                                (accounts[i].provider == L"antigravity" ? 1ULL :
                                 (ULONGLONG)intervalMin) * 60000;
                            if (nextPollDeadlineMs[i] > latestDeadline) {
                                nextPollDeadlineMs[i] = latestDeadline;
                            }
                        }
                        oldDeadlineUsed[j] = true;
                        break;
                    }
                }
            }
            lastLoggedSettingsGeneration = settingsGeneration;
        }

        if (settingsChanged || armState.size() != accounts.size()) {
            armState.assign(accounts.size(),
                            std::array<int, kQuotaBarCount>{-1, -1, -1, -1});
            for (size_t i = 0; i < accounts.size(); i++) {
                const std::array<const WindowUsage*, kQuotaBarCount> usage = {
                    &results[i].win5h, &results[i].winWeek, &results[i].fableWeek,
                    &results[i].extraUsage};
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (usage[w]->pct >= 0) {
                        armState[i][w] = usage[w]->pct >= notificationThreshold ? 1 : 0;
                    }
                }
            }
        }

        // Manual refreshes poll every account, or only the requested ones while the others wait.
        bool refreshAll = std::any_of(refreshRequests.begin(), refreshRequests.end(),
                                      [](const auto& request) { return !request.first; });
        std::vector<bool> refreshRequested(accounts.size(), refreshAll);
        for (size_t i = 0; i < accounts.size(); i++) {
            for (const auto& request : refreshRequests) {
                if (request.first == retryIdentityHashes[i]) refreshRequested[i] = true;
            }
        }
        bool refreshSelected = !refreshAll && std::find(refreshRequested.begin(),
                                                        refreshRequested.end(), true) !=
                                                  refreshRequested.end();
        // What the round started with, for polls whose sign-in changed (see FetchAccount).
        const std::vector<AccountData> startResults = results;
        std::vector<bool> polled(accounts.size(), false);
        std::vector<bool> fetchedOk(accounts.size(), false);
        std::vector<HistorySample> historyBatch;
        ULONGLONG nextRetryMs = 0;
        ULONGLONG nextPollMs = 0;
        bool anyError = false;
        for (size_t i = 0; i < accounts.size() && !g_unloading; i++) {
            ULONGLONG nowMs = NowUnixMs();
            // Hiding still stops polling by default. The explicit history exception never
            // polls an unrecorded hidden account, including during Refresh all.
            if (accounts[i].hidden && (skipHiddenPolling || !historyEnabled ||
                                        !accounts[i].recordHistory)) continue;
            // A poll through an earlier sign-in (replaced, removed or moved since) set that
            // login's deadlines: the current one polls now.
            if (pollSignInGeneration[i] != CurrentSignInGeneration(retryIdentityHashes[i])) {
                retryDeadlineMs[i] = 0;
                nextPollDeadlineMs[i] = 0;
            }
            if (refreshSelected && !refreshRequested[i]) {
                if (retryDeadlineMs[i] > nowMs &&
                    (nextRetryMs == 0 || retryDeadlineMs[i] < nextRetryMs)) {
                    nextRetryMs = retryDeadlineMs[i];
                } else if (nextPollDeadlineMs[i] > nowMs &&
                           (nextPollMs == 0 || nextPollDeadlineMs[i] < nextPollMs)) {
                    nextPollMs = nextPollDeadlineMs[i];
                } else if (nextPollMs == 0 || nowMs < nextPollMs) {
                    nextPollMs = nowMs;
                }
                continue;
            }

            if (retryDeadlineMs[i] > nowMs) {
                if (nextRetryMs == 0 || retryDeadlineMs[i] < nextRetryMs) {
                    nextRetryMs = retryDeadlineMs[i];
                }
                continue;
            }
            if (!refreshRequested[i] && nextPollDeadlineMs[i] > nowMs) {
                if (nextPollMs == 0 || nextPollDeadlineMs[i] < nextPollMs) {
                    nextPollMs = nextPollDeadlineMs[i];
                }
                continue;
            }

            // Ownership can end mid-round (the shell exiting as another starts): request nothing
            // more and recheck at once.
            if (!IsTaskbarOwnerProcess()) {
                nextPollMs = nowMs;
                break;
            }
            int retryAfter = 0;
            polled[i] = true;
            pollSignInGeneration[i] =
                FetchAccount(accounts[i], &results[i], &retryAfter, &accountDetails[i]);
            // Its sign-in changed meanwhile: poll once more with the current one (none after a
            // sign-out). Should it change again, publication drops the result.
            if (pollSignInGeneration[i] != CurrentSignInGeneration(retryIdentityHashes[i])) {
                results[i] = startResults[i];
                retryAfter = 0;
                pollSignInGeneration[i] =
                    FetchAccount(accounts[i], &results[i], &retryAfter, &accountDetails[i]);
            }
            // The journal rejects samples without a known provider code.
            uint32_t providerCode = HistoryProviderCode(accounts[i].provider);
            if (historyEnabled && accounts[i].recordHistory && accounts[i].historyId && providerCode) {
                HistorySample sample;
                sample.accountId = accounts[i].historyId;
                sample.timeMs = NowUnixMs();
                sample.intervalMs = (accounts[i].provider == L"antigravity" ? 1ULL :
                                     (ULONGLONG)intervalMin) * 60000;
                // A settings pause/resume or poll-rate change must not bridge an interval
                // where recording was disabled, even if it was shorter than two polls.
                sample.session = historySession ^ (historyContinuityGeneration * 0x9e3779b97f4a7c15ULL);
                sample.provider = providerCode;
                sample.failed = !results[i].error.empty() || results[i].stale ||
                                results[i].needsLogin;
                if (!sample.failed) {
                    const std::array<const WindowUsage*, 6> windows = {
                        &results[i].win5h, &results[i].winWeek, &results[i].fableWeek,
                        &results[i].extraUsage, &results[i].antigravityThirdParty5h,
                        &results[i].antigravityThirdPartyWeek};
                    for (size_t m = 0; m < windows.size(); ++m) {
                        // Anthropic's extra budget, Copilot's AI credits and OpenCode Go's
                        // monthly; OpenAI's user-chosen credit ceiling is not a historical quota.
                        if (m == 3 && sample.provider != 1 && sample.provider != 4 && sample.provider != 5) {
                            continue;
                        }
                        sample.meters[m] = {windows[m]->pct, windows[m]->resetUnixMs,
                                            windows[m]->windowDurationMs};
                    }
                    if (sample.provider == 1) {
                        sample.meters[6] = {results[i].extraUsedAmount,
                                           results[i].extraUsage.resetUnixMs, 0};
                    } else if (sample.provider == 2 && !results[i].creditsUnlimited) {
                        sample.meters[7].value = results[i].creditsBalance;
                    }
                }
                historyBatch.push_back(sample);
            }
            if (retryAfter > 0) {
                nextPollDeadlineMs[i] = 0;
                retryDeadlineMs[i] = NowUnixMs() + (ULONGLONG)retryAfter * 1000;
                results[i].retryDeadlineMs = retryDeadlineMs[i];
                if (nextRetryMs == 0 || retryDeadlineMs[i] < nextRetryMs) {
                    nextRetryMs = retryDeadlineMs[i];
                }
            } else {
                retryDeadlineMs[i] = 0;
                results[i].retryDeadlineMs = 0;
                ULONGLONG pollDelayMs = accounts[i].provider == L"antigravity"
                                            ? 60000
                                            : (ULONGLONG)intervalMin * 60000;
                if (!results[i].error.empty() && !results[i].needsLogin &&
                    accounts[i].provider != L"antigravity") {
                    pollDelayMs = std::min<ULONGLONG>(pollDelayMs, 120000);
                }
                nextPollDeadlineMs[i] = NowUnixMs() + pollDelayMs;
                if (nextPollMs == 0 || nextPollDeadlineMs[i] < nextPollMs) {
                    nextPollMs = nextPollDeadlineMs[i];
                }
            }

            if (!results[i].error.empty()) {
                if (retryAfter <= 0 && !results[i].needsLogin &&
                    accounts[i].provider != L"antigravity") {
                    anyError = true;
                }
                std::wstring errorState = accounts[i].provider + L"\n" + accounts[i].label +
                                          L"\n" + results[i].error;
                if (errorState != lastLoggedErrorStates[i]) {
                    Wh_Log(L"Fetch [%d] %s (%s): %s", (int)i, accounts[i].label.c_str(),
                           accounts[i].provider.c_str(), results[i].error.c_str());
                    lastLoggedErrorStates[i] = std::move(errorState);
                }
            } else {
                lastLoggedErrorStates[i].clear();
                fetchedOk[i] = true;
            }
        }
        bool published = false;
        bool generationRaced = false;
        std::vector<AccountConfig> currentAccounts;
        std::vector<AccountData> previousCurrentResults;
        std::vector<AccountData> remappedResults;
        std::vector<bool> remappedFetchedOk;
        int publishedThreshold = notificationThreshold;
        bool notificationsEnabled = notificationsOn;
        {
            // What this round read through a sign-in replaced, removed or moved since isn't
            // published, recorded or notified: the account polls again at once. Nor are account
            // details read so (this round or an earlier one, the usage request failing since). The
            // lock keeps a sign-in change from falling between these checks and the publication.
            std::lock_guard<std::mutex> authLock(g_authEpochMutex);
            for (size_t i = 0; i < accounts.size() && i < results.size() &&
                               i < accountDetails.size(); i++) {
                uint64_t signInGeneration = SignInGenerationLocked(AccountIdentityHash(accounts[i]));
                if (polled[i] && pollSignInGeneration[i] != signInGeneration) {
                    results[i] = startResults[i];
                    fetchedOk[i] = false;
                    std::erase_if(historyBatch, [&](const HistorySample& sample) {
                        return sample.accountId == accounts[i].historyId;
                    });
                    nextPollMs = NowUnixMs();
                }
                if (accountDetails[i].signInGeneration == signInGeneration) continue;
                if (accounts[i].provider == L"anthropic") results[i].plan.clear();
                if (accounts[i].provider == L"openai") results[i].freeResetsExpiryMs = 0;
            }
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            if (settingsGeneration == g_settingsGeneration) {
                std::lock_guard<std::mutex> lk2(g_dataMutex);
                if (g_data.size() == results.size()) {
                    g_data = results;
                    published = true;
                }
            } else {
                generationRaced = true;
                currentAccounts = g_settings.accounts;
                publishedThreshold = notificationThresholdOf(g_settings);
                notificationsEnabled = g_settings.notificationLevel != NotificationLevel::Off;

                std::lock_guard<std::mutex> lk2(g_dataMutex);
                previousCurrentResults.resize(currentAccounts.size());
                for (size_t i = 0; i < currentAccounts.size() && i < g_data.size(); i++) {
                    previousCurrentResults[i] = g_data[i];
                }
                remappedResults = previousCurrentResults;
                remappedFetchedOk.assign(currentAccounts.size(), false);
                std::vector<bool> oldResultUsed(results.size(), false);
                for (size_t i = 0; i < currentAccounts.size(); i++) {
                    bool matched = false;
                    for (size_t j = 0; j < accounts.size() && j < results.size(); j++) {
                        if (!oldResultUsed[j] &&
                            AccountIdentityHash(accounts[j]) ==
                                AccountIdentityHash(currentAccounts[i])) {
                            remappedResults[i] = results[j];
                            // The fetch used the old settings; a changed credits max must
                            // not publish a stale percentage.
                            ApplyCreditsMax(currentAccounts[i], &remappedResults[i]);
                            remappedFetchedOk[i] = fetchedOk[j];
                            oldResultUsed[j] = true;
                            matched = true;
                            break;
                        }
                    }
                    // A renamed account's data, carried over, has details read through its old
                    // identity's sign-in.
                    if (!matched && currentAccounts[i].provider == L"anthropic") {
                        remappedResults[i].plan.clear();
                    } else if (!matched && currentAccounts[i].provider == L"openai") {
                        remappedResults[i].freeResetsExpiryMs = 0;
                    }
                }
                g_data = remappedResults;
                published = true;
            }
        }
        // Admission is fenced against settings edits and Clear history. Never store a
        // carried-forward result or let a settings refresh manufacture a sample.
        AppendHistory(historyBatch, historyEpoch, settingsGeneration);
        // Flyout burn-up curves from local history: the accounts recorded this round, or every
        // recorded account after an invalidation or once recording starts here.
        bool curvesPublished = false;
        if (historyEnabled) {
            bool rebuildAll = rebuildAllCurves || !curvesRecording;
            std::vector<bool> rebuild(accounts.size(), rebuildAll);
            for (const auto& sample : historyBatch) {
                for (size_t i = 0; i < accounts.size(); i++) {
                    if (accounts[i].historyId == sample.accountId) rebuild[i] = true;
                }
            }
            if (std::find(rebuild.begin(), rebuild.end(), true) != rebuild.end()) {
                curvesPublished = RebuildBurnUpCurves(curveSamples, accounts, results, rebuild,
                                                      rebuildAll, historyRetentionDays,
                                                      curvePolicyGeneration);
                if (rebuildAll && !curvesPublished) g_burnUpRebuildRequested = true;
            }
        } else if (curvesRecording) {
            // No longer recording here: consent or pending IDs.
            curveSamples = {};
            std::lock_guard<std::mutex> lk(g_dataMutex);
            curvesPublished = !g_burnUpCurves.accounts.empty();
            g_burnUpCurves = {};
        }
        curvesRecording = historyEnabled;
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            std::erase_if(g_refreshRequests, [&](const auto& request) {
                return request.second <= refreshGeneration;
            });
        }
        if (published) {
            // Notify once per upward crossing of the chosen threshold; re-arm when usage
            // drops back below. A generation-raced result uses current settings and current
            // pre-publication data so settings edits alone cannot cause a notification.
            const auto& publishedAccounts = generationRaced ? currentAccounts : accounts;
            const auto& publishedResults = generationRaced ? remappedResults : results;
            const auto& publishedFetchedOk = generationRaced ? remappedFetchedOk : fetchedOk;
            for (size_t i = 0; i < publishedAccounts.size(); i++) {
                if (!publishedFetchedOk[i] || publishedAccounts[i].hidden) continue;
                const std::array<const WindowUsage*, kQuotaBarCount> usage = {
                    &publishedResults[i].win5h, &publishedResults[i].winWeek,
                    &publishedResults[i].fableWeek, &publishedResults[i].extraUsage};
                const std::array<const WindowUsage*, kQuotaBarCount> previousUsage = {
                    generationRaced ? &previousCurrentResults[i].win5h : nullptr,
                    generationRaced ? &previousCurrentResults[i].winWeek : nullptr,
                    generationRaced ? &previousCurrentResults[i].fableWeek : nullptr,
                    generationRaced ? &previousCurrentResults[i].extraUsage : nullptr};
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (!publishedAccounts[i].showBars[w]) continue;
                    const WindowUsage& wu = *usage[w];
                    if (wu.pct < 0) continue;
                    bool shouldNotify = false;
                    if (generationRaced) {
                        const WindowUsage& previous = *previousUsage[w];
                        shouldNotify = previous.pct >= 0 &&
                                       previous.pct < publishedThreshold &&
                                       wu.pct >= publishedThreshold &&
                                       notificationsEnabled;
                    } else {
                        int& st = armState[i][w];
                        if (wu.pct >= publishedThreshold) {
                            shouldNotify = st == 0 && notificationsEnabled;
                            st = 1;
                        } else {
                            st = 0;
                        }
                    }
                    // Turned off since publication (history and earlier notifications take time):
                    // nothing more shows.
                    if (shouldNotify) {
                        std::lock_guard<std::mutex> lk(g_settingsMutex);
                        shouldNotify = g_settings.notificationLevel != NotificationLevel::Off;
                    }
                    if (shouldNotify && !g_unloading) {
                        std::wstring providerName =
                            ProviderDisplayName(publishedAccounts[i].provider);
                        wchar_t title[96];
                        PCWSTR quotaName =
                            w == kFiveHourBar ? L"5h" :
                            w == kWeeklyBar ? L"weekly" :
                            w == kFableWeeklyBar ? L"Fable weekly" : L"monthly extra";
                        if (publishedAccounts[i].provider == L"antigravity") {
                            if (w == kFiveHourBar) {
                                ULONGLONG duration = publishedResults[i].win5h.windowDurationMs;
                                quotaName = duration == 3ULL * 60 * 60 * 1000 ? L"Gemini 3h" :
                                            duration == 60ULL * 60 * 1000 ? L"Gemini 1h" :
                                            duration == 0 ? L"Gemini current limit" :
                                                            L"Gemini 5h";
                            } else if (w == kWeeklyBar) {
                                quotaName = L"Gemini weekly";
                            } else if (w == kFableWeeklyBar) {
                                quotaName = L"Argon weekly";
                            }
                        } else if (publishedAccounts[i].provider == L"openai" &&
                                   w == kExtraUsageBar) {
                            quotaName = L"credits";
                        } else if (publishedAccounts[i].provider == L"copilot") {
                            quotaName = CopilotUnitName(publishedResults[i].copilotUnit);
                        } else if (publishedAccounts[i].provider == L"opencode" &&
                                   w == kExtraUsageBar) {
                            quotaName = L"monthly";
                        }
                        swprintf(title, ARRAYSIZE(title), L"%s usage at %.0f%%",
                                 quotaName, wu.pct);
                        std::wstring body = providerName;
                        if ((w != kExtraUsageBar && w != kFableWeeklyBar) || wu.resetUnixMs) {
                            body += L" - resets " + FormatReset(wu.resetUnixMs);
                        }
                        // The flyout's projection to the limit; none for an OpenCode Go window at
                        // its limit, as there.
                        std::wstring projection = publishedResults[i].opencodeLimited[w] ? L"" :
                            NotificationProjectionText(publishedAccounts[i].historyId, w, wu);
                        FireThresholdNotification(title, body, projection,
                                                  ProviderDashboardUrl(publishedAccounts[i].provider),
                                                  AccountIdentityHash(publishedAccounts[i]), w);
                        toastAppIdMayExist = true;
                    }
                }
            }
            PostUiUpdate();
            // The settings window's account statuses follow each publication (a posted message;
            // no lock is held).
            NotifySettingsWindowChanged();
        } else if (curvesPublished) {
            PostUiUpdate();
        }
        // Status pages after the round's polls and notifications, so they never hold them up.
        ULONGLONG statusDueTick = PollProviderStatuses(providerStatus);

        DWORD waitMs = (DWORD)intervalMin * 60000;
        if (anyError) waitMs = std::min(waitMs, (DWORD)120000);
        ULONGLONG nowMs = NowUnixMs();
        if (nextRetryMs > nowMs) {
            waitMs = (DWORD)std::min<ULONGLONG>(waitMs, nextRetryMs - nowMs);
        }
        if (nextPollMs <= nowMs && nextPollMs != 0) {
            waitMs = 0;
        } else if (nextPollMs > nowMs) {
            waitMs = (DWORD)std::min<ULONGLONG>(waitMs, nextPollMs - nowMs);
        }
        // Local retention must wake independently of slow provider polling. Existing
        // per-account deadlines still prevent this wake from issuing early requests.
        if (nextHistoryMaintenanceMs) {
            waitMs = (DWORD)std::min<ULONGLONG>(waitMs,
                nextHistoryMaintenanceMs > nowMs ? nextHistoryMaintenanceMs - nowMs : 0);
        }
        if (statusDueTick) {
            ULONGLONG tick = GetTickCount64();
            waitMs = (DWORD)std::min<ULONGLONG>(waitMs, statusDueTick > tick ? statusDueTick - tick : 0);
        }

        HANDLE handles[2] = {g_stopEvent, g_refreshEvent};
        if (WaitForMultipleObjects(2, handles, FALSE, waitMs) == WAIT_OBJECT_0) break;
    }
    RemoveNotifyIcon();
    if (toastAppIdMayExist && IsTaskbarOwnerProcess()) RemoveToastAppId();
    if (apartmentInitialized) winrt::uninit_apartment();
    return 0;
}
