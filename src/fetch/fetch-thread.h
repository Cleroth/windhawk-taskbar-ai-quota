// Per-account usage fetch and the polling loop that publishes results and history.

static void FetchAccount(const AccountConfig& acc, AccountData* d, int* retryAfterSec) {
    d->error.clear();
    d->retryDeadlineMs = 0;
    d->needsLogin = false;

    // Antigravity uses local language server discovery, not OAuth.
    if (acc.provider == L"antigravity") {
        FetchAntigravityAccount(d);
        return;
    }

    uint64_t idHash = AccountIdentityHash(acc);
    ULONGLONG authEpoch = CurrentAuthEpoch(idHash);
    StoredToken tok;
    if (!LoadStoredToken(idHash, &tok) || tok.accessToken.empty()) {
        d->stale = true;
        d->needsLogin = true;
        d->error = L"not signed in - click to sign in";
        return;
    }

    // Refresh just before expiry so a request rarely races the token going stale.
    if (tok.expiresMs && tok.expiresMs < NowUnixMs() + 60000) {
        std::wstring refreshErr;
        int refreshRetryAfter = 0;
        TokenEndpointResult refreshResult =
            RefreshToken(acc.provider, &tok, &refreshErr, &refreshRetryAfter);
        if (refreshResult == TokenEndpointResult::Success) {
            uint64_t savedIdentity = 0;
            TokenSaveResult saved =
                SaveStoredTokenIfCurrent(idHash, authEpoch, tok, &savedIdentity);
            if (saved == TokenSaveResult::Stale) {
                d->stale = true;
                d->needsLogin = true;
                d->error = L"not signed in - click to sign in";
                return;
            }
            if (saved != TokenSaveResult::Saved) {
                d->stale = true;
                d->error = L"could not save refreshed token";
                return;
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
            return;
        }
    }

    auto requestUsage = [&](const StoredToken& t) -> HttpResult {
        if (acc.provider == L"anthropic") {
            std::wstring headers = L"Authorization: Bearer " + t.accessToken +
                                   L"\r\nanthropic-beta: oauth-2025-04-20"
                                   L"\r\nAccept: application/json\r\n";
            return HttpRequest(L"GET", L"api.anthropic.com", L"/api/oauth/usage",
                               L"claude-code/2.1.0", headers);
        }
        std::wstring headers = L"Authorization: Bearer " + t.accessToken +
                               L"\r\nOrigin: https://chatgpt.com"
                               L"\r\nReferer: https://chatgpt.com/"
                               L"\r\nAccept: application/json\r\n";
        if (!t.accountId.empty()) headers += L"ChatGPT-Account-Id: " + t.accountId + L"\r\n";
        return HttpRequest(L"GET", L"chatgpt.com", L"/backend-api/wham/usage",
                           L"taskbar-ai-quota/0.1", headers);
    };

    HttpResult r = requestUsage(tok);
    // Reactive refresh: the access token may have been revoked or expired early.
    if (r.ok && r.status == 401 && !tok.refreshToken.empty()) {
        std::wstring refreshErr;
        int refreshRetryAfter = 0;
        TokenEndpointResult refreshResult =
            RefreshToken(acc.provider, &tok, &refreshErr, &refreshRetryAfter);
        if (refreshResult == TokenEndpointResult::Success) {
            uint64_t savedIdentity = 0;
            TokenSaveResult saved =
                SaveStoredTokenIfCurrent(idHash, authEpoch, tok, &savedIdentity);
            if (saved == TokenSaveResult::Saved) {
                if (savedIdentity != idHash) RefreshQuotaByIdentity(savedIdentity);
                r = requestUsage(tok);
            } else if (saved == TokenSaveResult::Stale) {
                d->stale = true;
                d->needsLogin = true;
                d->error = L"not signed in - click to sign in";
                return;
            } else {
                d->stale = true;
                d->error = L"could not save refreshed token";
                return;
            }
        } else if (refreshResult == TokenEndpointResult::TransientFailure) {
            d->stale = true;
            d->error = L"token refresh failed";
            if (!refreshErr.empty()) d->error += L": " + refreshErr;
            *retryAfterSec = refreshRetryAfter > 0 ? refreshRetryAfter : 120;
            return;
        }
    }

    if (!r.ok) {
        d->stale = true;
        d->error = L"network error";
        return;
    }
    if (r.status == 401) {
        d->stale = true;
        d->needsLogin = true;
        d->error = L"unauthorized - click to sign in";
        return;
    }
    if (r.status == 429) {
        d->stale = true;
        d->error = L"rate limited by API";
        *retryAfterSec = r.retryAfterSec > 0 ? r.retryAfterSec : 120;
        return;
    }
    if (r.status != 200) {
        d->stale = true;
        d->error = L"HTTP " + std::to_wstring(r.status);
        return;
    }

    AccountData fresh;
    std::wstring parseError;
    bool parsed = acc.provider == L"anthropic" ? ParseAnthropicUsage(r.body, &fresh, &parseError)
                                               : ParseOpenAiUsage(r.body, &fresh, &parseError);
    if (!parsed) {
        d->stale = true;
        d->error = parseError.empty() ? L"unexpected response format" : parseError;
        return;
    }

    ApplyCreditsMax(acc, &fresh);
    fresh.stale = false;
    fresh.lastSuccessMs = NowUnixMs();
    *d = std::move(fresh);
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
    // Per-account red-crossing arm state, indexed by QuotaBarIndex:
    // -1 unknown (primes without firing), 0 below/armed, 1 above/already notified.
    std::vector<std::array<int, kQuotaBarCount>> redState;
    ULONGLONG lastLoggedSettingsGeneration = 0;
    const uint64_t historySession = NowUnixMs();
    bool curvesRecording = false;  // Recording was active last round, so curves may be published.
    BurnUpSampleCache curveSamples;  // Journal tails of recorded accounts, kept between rounds.
    while (!g_unloading) {
        const bool historyOwner = IsHistoryOwnerProcess();
        if (historyOwner && g_historyIdsPendingSave) {
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
        uint64_t refreshAccountIdentity;
        bool refreshRequested;
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            refreshGeneration = g_refreshGeneration.load();
            refreshAccountIdentity = g_refreshAccountIdentity.load();
            refreshRequested = g_refreshing.load();
        }
        std::vector<AccountConfig> accounts;
        int intervalMin, redThreshold;
        bool enableNotifications, historyEnabled, skipHiddenPolling;
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
            redThreshold = g_settings.redThreshold;
            enableNotifications = g_settings.enableNotifications;
            historyEnabled = g_settings.historyEnabled && historyOwner && !g_historyIdsPendingSave;
            historyRetentionDays = g_settings.historyRetentionDays;
            skipHiddenPolling = g_settings.skipHiddenPolling;
            settingsGeneration = g_settingsGeneration;
            historyContinuityGeneration = g_historyContinuityGeneration;
            curvePolicyGeneration = g_burnUpPolicyGeneration.load();
        }
        ULONGLONG nextHistoryMaintenanceMs = historyOwner && historyEnabled
            ? MaintainHistory(historyRetentionDays, true) : 0;
        bool settingsChanged = lastLoggedSettingsGeneration != settingsGeneration ||
                               lastLoggedErrorStates.size() != accounts.size();
        if (settingsChanged) {
            lastLoggedErrorStates.assign(accounts.size(), {});

            // Remap provider-directed Retry-After and normal poll deadlines by stable account
            // identity so settings changes don't cause another request for unchanged accounts.
            auto oldIdentityHashes = std::move(retryIdentityHashes);
            auto oldRetryDeadlineMs = std::move(retryDeadlineMs);
            auto oldNextPollDeadlineMs = std::move(nextPollDeadlineMs);
            std::vector<bool> oldDeadlineUsed(oldIdentityHashes.size(), false);
            retryIdentityHashes.resize(accounts.size());
            retryDeadlineMs.assign(accounts.size(), 0);
            nextPollDeadlineMs.assign(accounts.size(), 0);
            for (size_t i = 0; i < accounts.size(); ++i) {
                retryIdentityHashes[i] = AccountIdentityHash(accounts[i]);
                for (size_t j = 0; j < oldIdentityHashes.size() &&
                                    j < oldRetryDeadlineMs.size(); ++j) {
                    if (!oldDeadlineUsed[j] &&
                        oldIdentityHashes[j] == retryIdentityHashes[i]) {
                        retryDeadlineMs[i] = oldRetryDeadlineMs[j];
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

        std::vector<AccountData> results(accounts.size());
        {
            std::lock_guard<std::mutex> lk(g_dataMutex);
            if (g_data.size() == results.size()) results = g_data;
        }
        if (settingsChanged || redState.size() != accounts.size()) {
            redState.assign(accounts.size(),
                            std::array<int, kQuotaBarCount>{-1, -1, -1, -1});
            for (size_t i = 0; i < accounts.size(); i++) {
                const std::array<const WindowUsage*, kQuotaBarCount> usage = {
                    &results[i].win5h, &results[i].winWeek, &results[i].fableWeek,
                    &results[i].extraUsage};
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (usage[w]->pct >= 0) {
                        redState[i][w] = usage[w]->pct >= redThreshold ? 1 : 0;
                    }
                }
            }
        }

        int refreshAccountIndex = -1;
        if (refreshRequested && refreshAccountIdentity) {
            for (size_t i = 0; i < accounts.size(); i++) {
                if (AccountIdentityHash(accounts[i]) == refreshAccountIdentity) {
                    refreshAccountIndex = (int)i;
                    break;
                }
            }
        }
        bool manualRefresh = refreshRequested &&
                             (!refreshAccountIdentity || refreshAccountIndex >= 0);
        bool refreshSingleAccount = manualRefresh && refreshAccountIndex >= 0 &&
                                    refreshAccountIndex < (int)accounts.size();
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
            if (refreshSingleAccount && (int)i != refreshAccountIndex) {
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
            if (!manualRefresh && nextPollDeadlineMs[i] > nowMs) {
                if (nextPollMs == 0 || nextPollDeadlineMs[i] < nextPollMs) {
                    nextPollMs = nextPollDeadlineMs[i];
                }
                continue;
            }

            int retryAfter = 0;
            FetchAccount(accounts[i], &results[i], &retryAfter);
            if (historyEnabled && accounts[i].recordHistory && accounts[i].historyId) {
                HistorySample sample;
                sample.accountId = accounts[i].historyId;
                sample.timeMs = NowUnixMs();
                sample.intervalMs = (accounts[i].provider == L"antigravity" ? 1ULL :
                                     (ULONGLONG)intervalMin) * 60000;
                // A settings pause/resume or poll-rate change must not bridge an interval
                // where recording was disabled, even if it was shorter than two polls.
                sample.session = historySession ^ (historyContinuityGeneration * 0x9e3779b97f4a7c15ULL);
                sample.provider = accounts[i].provider == L"anthropic" ? 1 :
                                  accounts[i].provider == L"openai" ? 2 : 3;
                sample.failed = !results[i].error.empty() || results[i].stale ||
                                results[i].needsLogin;
                if (!sample.failed) {
                    const std::array<const WindowUsage*, 6> windows = {
                        &results[i].win5h, &results[i].winWeek, &results[i].fableWeek,
                        &results[i].extraUsage, &results[i].antigravityThirdParty5h,
                        &results[i].antigravityThirdPartyWeek};
                    for (size_t m = 0; m < windows.size(); ++m) {
                        // OpenAI's user-chosen credit ceiling is not a historical quota.
                        if (m == 3 && sample.provider != 1) continue;
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
        int notificationRedThreshold = redThreshold;
        bool notificationsEnabled = enableNotifications;
        {
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
                notificationRedThreshold = g_settings.redThreshold;
                notificationsEnabled = g_settings.enableNotifications;

                std::lock_guard<std::mutex> lk2(g_dataMutex);
                previousCurrentResults.resize(currentAccounts.size());
                for (size_t i = 0; i < currentAccounts.size() && i < g_data.size(); i++) {
                    previousCurrentResults[i] = g_data[i];
                }
                remappedResults = previousCurrentResults;
                remappedFetchedOk.assign(currentAccounts.size(), false);
                std::vector<bool> oldResultUsed(results.size(), false);
                for (size_t i = 0; i < currentAccounts.size(); i++) {
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
                            break;
                        }
                    }
                }
                g_data = remappedResults;
                published = true;
            }
        }
        // Admission is fenced against settings edits and Clear history. Never store a
        // carried-forward result or let a settings refresh manufacture a sample.
        AppendHistory(historyBatch, historyEpoch, settingsGeneration, historyRetentionDays);
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
            // No longer recording here: consent, history ownership or pending IDs.
            curveSamples = {};
            std::lock_guard<std::mutex> lk(g_dataMutex);
            curvesPublished = !g_burnUpCurves.accounts.empty();
            g_burnUpCurves = {};
        }
        curvesRecording = historyEnabled;
        if (!notificationsEnabled && g_notifyWnd) RemoveNotifyIcon();
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            if (refreshGeneration == g_refreshGeneration.load()) {
                g_refreshing = false;
                g_refreshAccountIdentity = 0;
            }
        }
        if (published) {
            // Fire one toast per upward crossing of the red threshold; re-arm when
            // usage drops back below. A generation-raced result uses current settings and
            // current pre-publication data so settings edits alone cannot cause a toast.
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
                                       previous.pct < notificationRedThreshold &&
                                       wu.pct >= notificationRedThreshold &&
                                       notificationsEnabled;
                    } else {
                        int& st = redState[i][w];
                        if (wu.pct >= notificationRedThreshold) {
                            shouldNotify = st == 0 && notificationsEnabled;
                            st = 1;
                        } else {
                            st = 0;
                        }
                    }
                    if (shouldNotify) {
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
                            }
                        } else if (publishedAccounts[i].provider == L"openai" &&
                                   w == kExtraUsageBar) {
                            quotaName = L"credits";
                        }
                        swprintf(title, ARRAYSIZE(title), L"%s usage at %.0f%%",
                                 quotaName, wu.pct);
                        std::wstring body = providerName;
                        if ((w != kExtraUsageBar && w != kFableWeeklyBar) || wu.resetUnixMs) {
                            body += L" - resets " + FormatReset(wu.resetUnixMs);
                        }
                        FireThresholdNotification(title, body);
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

        HANDLE handles[2] = {g_stopEvent, g_refreshEvent};
        if (WaitForMultipleObjects(2, handles, FALSE, waitMs) == WAIT_OBJECT_0) break;
    }
    RemoveNotifyIcon();
    if (apartmentInitialized) winrt::uninit_apartment();
    return 0;
}
