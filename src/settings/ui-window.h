// The rebuilt settings window: the settings client of the custom-drawn host (ui/host.h). Opened
// by the temporary "Settings (new)" taskbar menu item on the settings thread; never touches XAML.

static constexpr PCWSTR kSettingsUiClass = L"AiQuotaSettingsUi_" WH_MOD_ID;
static constexpr PCWSTR kSettingsUiViewportClass = L"AiQuotaSettingsUiView_" WH_MOD_ID;

struct SettingsUiClient final : UiHostClient {
    SettingsUiPage page = SettingsUiPage::Accounts;

    void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) override {
        SettingsView view;
        view.page = page;
        view.version = WH_MOD_VERSION;
        SettingsUiBuildFrame(*host.graphics, view, client, frame);
    }

    void Invoke(UiHost& host, UiAction action) override {
        if (g_unloading || g_settingsWindowCancelRequested) return;
        if (action.kind == UiActionKind::Navigate) {
            page = SettingsUiPage(std::clamp(action.value, 0, int(SettingsUiPage::Count) - 1));
            UiHostRequestRebuild(host, true);
        } else if (action.kind == UiActionKind::OpenUrl) {
            OpenUrl(action.url.c_str());
        }
    }

    bool Message(UiHost&, UINT message, WPARAM, LPARAM, LRESULT& result) override {
        result = 0;
        switch (message) {
            // Account, sign-in, consent and storage notifications: no built page shows them yet.
            case kSettingsRefreshMessage:
            case kHistoryConsentChangedMessage:
                return true;
            case kExitVisualTestMessage:
                SetVisualTestMode(false);
                return true;
        }
        return false;
    }

    bool Attach(UiHost& host) override {
        g_settingsWindow.store(host.main);
        return true;
    }

    bool CancelModal() override {
        if (!g_settingsMessageBoxContext || !IsWindow(g_settingsMessageBoxContext->hWnd)) {
            return false;
        }
        g_settingsMessageBoxContext->forced = true;
        PostMessageW(g_settingsMessageBoxContext->hWnd, WM_COMMAND,
                     g_settingsMessageBoxContext->forcedResult, 0);
        return true;
    }

    bool Unloading() override { return g_unloading || g_settingsWindowCancelRequested; }

    // No page edits settings yet: nothing to flush.
    bool PrepareClose(UiHost&) override { return true; }

    void Destroying() override { SetVisualTestMode(false); }

    void Destroyed() override { g_settingsWindow.store(nullptr); }
};
