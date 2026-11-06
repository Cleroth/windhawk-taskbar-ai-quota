// Shared runtime state, lifecycle signals, and cross-thread requests to the fetch thread, the
// settings window and the usage history window.

static Settings g_settings;
static std::mutex g_settingsMutex;
static std::mutex g_configEditMutex;
static ULONGLONG g_settingsGeneration = 0;
static std::vector<AccountData> g_data;
static std::mutex g_dataMutex;

static std::atomic<bool> g_unloading{false};
// Manual refreshes the fetch thread hasn't served, under g_refreshMutex: an account identity (0:
// every account) with the g_refreshGeneration that last asked for it. A round serves the requests
// it started with; one made meanwhile waits for the next round.
static std::vector<std::pair<uint64_t, ULONGLONG>> g_refreshRequests;
static ULONGLONG g_refreshGeneration = 0;
static std::mutex g_refreshMutex;
static std::atomic<bool> g_uiInjected{false};
static std::atomic<bool> g_visualTestMode{false};
// The settings load's failure (settings/storage.h), until a save replaces the stored settings:
// whether a backup of them was kept (settings_v1_invalid), and whether a save may replace them:
// read whole and backed up, or once the settings window's user confirmed it. Each load resets them.
static std::atomic<bool> g_settingsLoadError{false};
static std::atomic<bool> g_settingsLoadBackup{false};
static std::atomic<bool> g_settingsReplaceAllowed{false};
static std::atomic<bool> g_fetchThreadStarted{false};
static HANDLE g_stopEvent = nullptr;
static HANDLE g_refreshEvent = nullptr;

static std::atomic<HWND> g_settingsWindow{nullptr};
// The usage history window from its publication until its destruction (history/ui-window.h).
static std::atomic<HWND> g_historyWindow{nullptr};

static constexpr UINT kSettingsRefreshMessage = WM_APP + 20;
static constexpr UINT kExitVisualTestMessage = WM_APP + 21;
static constexpr UINT kHistoryConsentChangedMessage = WM_APP + 22;

// The settings, usage history and sign-in windows' shared taskbar identity (ui/window-icon.h).
static constexpr PCWSTR kWindowAppId = L"Windhawk.TaskbarAIQuota";

// Settings, sign-in, visibility, history ID, storage status, consent, poll and journal changes
// (an account's history deleted): posted to the settings window and the usage history window,
// never waited on.
static void NotifySettingsWindowChanged(bool historyConsentChanged = false) {
    UINT message = historyConsentChanged ? kHistoryConsentChangedMessage : kSettingsRefreshMessage;
    for (HWND window : {g_settingsWindow.load(), g_historyWindow.load()}) {
        if (window) PostMessageW(window, message, 0, 0);
    }
}

static void QueueRefresh(uint64_t identityHash) {
    if (g_unloading) return;

    if (!g_fetchThreadStarted.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            g_refreshRequests.clear();
        }
        PostUiUpdate();
        return;
    }

    {
        std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
        ULONGLONG generation = ++g_refreshGeneration;
        auto request = std::find_if(g_refreshRequests.begin(), g_refreshRequests.end(),
                                    [&](const auto& pending) { return pending.first == identityHash; });
        if (request != g_refreshRequests.end()) {
            request->second = generation;
        } else {
            g_refreshRequests.push_back({identityHash, generation});
        }
    }
    PostUiUpdate();
    if (g_refreshEvent) SetEvent(g_refreshEvent);
}

static void RefreshQuotaByIdentity(uint64_t identityHash) {
    bool found = false;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        for (const auto& account : g_settings.accounts) {
            if (AccountIdentityHash(account) == identityHash) {
                found = true;
                break;
            }
        }
    }
    if (found) QueueRefresh(identityHash);
}
