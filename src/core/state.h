// Shared runtime state, lifecycle signals, and cross-thread requests to the fetch thread, the
// settings window and the custom-drawn history window.

static Settings g_settings;
static std::mutex g_settingsMutex;
static std::mutex g_configEditMutex;
static ULONGLONG g_settingsGeneration = 0;
static std::vector<AccountData> g_data;
static std::mutex g_dataMutex;

static std::atomic<bool> g_unloading{false};
static std::atomic<bool> g_refreshing{false};
static std::atomic<uint64_t> g_refreshAccountIdentity{0};
static std::atomic<ULONGLONG> g_refreshGeneration{0};
static std::mutex g_refreshMutex;
static std::atomic<bool> g_uiInjected{false};
static std::atomic<bool> g_visualTestMode{false};
static std::atomic<bool> g_settingsLoadError{false};
static std::atomic<bool> g_fetchThreadStarted{false};
static HANDLE g_stopEvent = nullptr;
static HANDLE g_refreshEvent = nullptr;

static std::atomic<HWND> g_settingsWindow{nullptr};
// The custom-drawn usage history window while it takes notifications (history/ui-window.h); the
// classic window never gets them.
static std::atomic<HWND> g_historyUiWindow{nullptr};

static constexpr UINT kSettingsRefreshMessage = WM_APP + 20;
static constexpr UINT kExitVisualTestMessage = WM_APP + 21;
static constexpr UINT kHistoryConsentChangedMessage = WM_APP + 22;

// Settings, sign-in, visibility, history ID, storage status, consent and poll changes: posted to
// the settings window and the custom-drawn history window, never waited on.
static void NotifySettingsWindowChanged(bool historyConsentChanged = false) {
    UINT message = historyConsentChanged ? kHistoryConsentChangedMessage : kSettingsRefreshMessage;
    for (HWND window : {g_settingsWindow.load(), g_historyUiWindow.load()}) {
        if (window) PostMessageW(window, message, 0, 0);
    }
}

static void QueueRefresh(uint64_t identityHash) {
    if (g_unloading) return;

    if (!g_fetchThreadStarted.load(std::memory_order_acquire)) {
        {
            std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
            g_refreshing = false;
            g_refreshAccountIdentity = 0;
        }
        PostUiUpdate();
        return;
    }

    {
        std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
        g_refreshing = true;
        g_refreshAccountIdentity = identityHash;
        g_refreshGeneration++;
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
