// The custom-drawn usage history window: the history client of the window host (ui/host.h), run
// on the history thread (history/window.h); never touches XAML. It only translates: host
// callbacks into controller events (history/controller.h, on its production ops), the
// controller's view into frames (history/shell.h), rebuilds and focus requests.

static constexpr PCWSTR kHistoryUiClass = L"AiQuotaHistoryUi_" WH_MOD_ID;
static constexpr PCWSTR kHistoryUiViewportClass = L"AiQuotaHistoryUiView_" WH_MOD_ID;
static constexpr PCWSTR kHistoryUiPopupClass = L"AiQuotaHistoryUiPopup_" WH_MOD_ID;

struct HistoryUiClient final : UiHostClient {
    HistoryProductionOps ops;
    HistoryController controller{ops};

    void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) override {
        HistoryUiView view = controller.view;
        view.theme = host.theme.kind;
        HistoryUiBuildFrame(*host.graphics, view, client, frame);
    }

    // A new selection or view scrolls the content back to its top.
    void Invoke(UiHost& host, UiAction action) override {
        if (Unloading() || action.kind != UiActionKind::Command) return;
        uint64_t selected = controller.selectedId;
        HistoryView shown = controller.view.view;
        controller.Command(HistoryUiCommand(action.target), action.value, action.key);
        UiHostRequestRebuild(host, controller.selectedId != selected || controller.view.view != shown);
    }

    // Explicit loads: the first one, the activation request (once the host brought the window
    // back), F5 and Ctrl+R from either surface. A load that changed the selection (its request, or
    // the account no longer listed) moves focus to the selected entry. Esc closes.
    bool Message(UiHost& host, UINT message, WPARAM wParam, LPARAM, LRESULT& result) override {
        result = 0;
        bool load = message == kHistoryLoadMessage || message == kHistoryActivateMessage;
        if (message == WM_KEYDOWN) {
            if (wParam == VK_ESCAPE) {
                PostMessageW(host.main, WM_CLOSE, 0, 0);
                return true;
            }
            load = wParam == VK_F5 || (wParam == 'R' && GetKeyState(VK_CONTROL) < 0);
        }
        if (!load) return false;
        uint64_t selected = controller.selectedId;
        controller.Load();
        bool moved = controller.selectedId != selected;
        if (moved) {
            const HistoryUiView& view = controller.view;
            uint32_t entry = view.selected >= 0 ? kHistoryUiNavAccount + view.accounts[size_t(view.selected)].slot :
                                                  kHistoryUiNavAll;
            UiHostRequestFocus(host, {entry, -1}, false);
        }
        UiHostRequestRebuild(host, moved);
        return true;
    }

    // The window is published before its first load is posted: an open request stored before
    // either is taken by that load, a later one by its activation.
    bool Attach(UiHost& host) override {
        controller.Open();
        g_historyWindow.store(host.main);
        PostMessageW(host.main, kHistoryLoadMessage, 0, 0);
        return true;
    }

    // Every close starts here: the controller's close latch. No modal runs in this window.
    bool CancelModal() override {
        controller.RequestClose();
        return false;
    }

    bool Unloading() override { return ops.Unloading(); }

    bool PrepareClose(UiHost&) override { return true; }

    void Destroying() override { g_historyWindow.store(nullptr); }

    void Destroyed() override {}
};
