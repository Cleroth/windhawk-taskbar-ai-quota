// Click and context-menu actions on the taskbar bars.

static void OpenDashboardForIdentity(uint64_t identityHash) {
    std::wstring provider;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        for (const auto& account : g_settings.accounts) {
            if (AccountIdentityHash(account) == identityHash) {
                provider = account.provider;
                break;
            }
        }
    }
    if (provider.empty()) return;

    if (provider == L"antigravity") {
        RefreshQuotaByIdentity(identityHash);
    } else {
        OpenUrl(provider == L"anthropic" ? L"https://claude.ai/settings/usage"
                                         : L"https://chatgpt.com/codex/cloud/settings/analytics#usage");
    }
}

// Right-click menu: show or hide an account (the desired state), keep at least one visible,
// persist the hidden-set to mod storage, then wake the fetch thread and refresh all taskbars,
// which also re-syncs every menu's show/hide toggles. Runs on a taskbar UI thread (menu click).
static void SetAccountVisibility(uint64_t identityHash, bool wantVisible) {
    if (g_unloading) return;
    std::unique_lock<std::mutex> configLock(g_configEditMutex);

    std::wstring hashes;
    Settings settingsSnapshot;
    bool refreshNow = false;
    bool oldHidden = false;
    bool rejectedLastVisible = false;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        int accountIndex = -1;
        for (size_t i = 0; i < g_settings.accounts.size(); i++) {
            if (AccountIdentityHash(g_settings.accounts[i]) == identityHash) {
                accountIndex = (int)i;
                break;
            }
        }
        if (accountIndex < 0) return;
        oldHidden = g_settings.accounts[accountIndex].hidden;

        // Refuse to hide the last visible account: there'd be no bar left to right-click.
        if (!wantVisible && !g_settings.accounts[accountIndex].hidden) {
            int visibleCount = 0;
            for (const auto& a : g_settings.accounts) {
                if (!a.hidden) visibleCount++;
            }
            if (visibleCount <= 1) {
                rejectedLastVisible = true;
            }
        }
        if (!rejectedLastVisible) {
            bool newHidden = !wantVisible;
            settingsSnapshot = g_settings;
            settingsSnapshot.accounts[accountIndex].hidden = newHidden;
            if (newHidden != oldHidden) {
                if (!newHidden) {
                    // Showing: keep the existing (possibly stale) data and only re-query if it has
                    // already gone stale, matching the UI's grey-out threshold. This stops repeated
                    // hide/show from triggering fetches and hitting provider rate limits.
                    ULONGLONG staleIntervalMin =
                        settingsSnapshot.accounts[accountIndex].provider == L"antigravity"
                            ? 1
                            : (ULONGLONG)settingsSnapshot.pollMinutes;
                    ULONGLONG now = NowUnixMs();
                    std::lock_guard<std::mutex> lk2(g_dataMutex);
                    if (accountIndex >= (int)g_data.size()) {
                        refreshNow = true;
                    } else {
                        const AccountData& d = g_data[accountIndex];
                        refreshNow = d.stale || d.lastSuccessMs == 0 ||
                                     now - d.lastSuccessMs > staleIntervalMin * 2 * 60000;
                    }
                }
            }

            wchar_t buf[24];
            for (const auto& a : settingsSnapshot.accounts) {
                if (!a.hidden) continue;
                if (!hashes.empty()) hashes += L";";
                swprintf(buf, ARRAYSIZE(buf), L"%016llx",
                         (unsigned long long)AccountIdentityHash(a));
                hashes += buf;
            }
        }
    }
    if (rejectedLastVisible) {
        configLock.unlock();
        PostUiUpdate();
        return;
    }

    if (!SaveOwnedSettings(settingsSnapshot)) {
        configLock.unlock();
        Wh_Log(L"Could not persist account visibility");
        NotifySettingsWindowChanged();
        PostUiUpdate();
        return;
    }
    PublishSettings(settingsSnapshot);
    Wh_SetStringValue(L"hiddenAccounts", hashes.c_str());
    // RefreshQuota re-queries only this account (and posts the UI); otherwise just repaint so the
    // column collapses/reappears with its existing data without any network request.
    configLock.unlock();
    NotifySettingsWindowChanged();
    if (refreshNow) RefreshQuotaByIdentity(identityHash);
    else PostUiUpdate();
}
