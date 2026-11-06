// The custom-drawn usage history window: the history client of the window host (ui/host.h), run
// on the history thread (history/window.h); never touches XAML. It only translates: host
// callbacks and notifications into controller events (history/controller.h, on its production
// ops), the controller's view into frames (history/shell.h) and its chart and an export's
// progress into paints (history/chart-paint.h) and readouts, rebuilds and focus requests. It
// holds no D2D objects.

static constexpr PCWSTR kHistoryUiClass = L"AiQuotaHistoryUi_" WH_MOD_ID;
static constexpr PCWSTR kHistoryUiViewportClass = L"AiQuotaHistoryUiView_" WH_MOD_ID;
static constexpr PCWSTR kHistoryUiPopupClass = L"AiQuotaHistoryUiPopup_" WH_MOD_ID;
static constexpr PCWSTR kHistoryUiTooltipClass = L"AiQuotaHistoryUiTip_" WH_MOD_ID;

// The window's title in a language.
static std::wstring HistoryWindowTitle(const I18nLocale& locale) {
    return I18nText(locale, TrCtx(L"window-title", L"{app} \u2014 Usage history"), {{L"app", kSettingsProductName}});
}

struct HistoryUiClient final : UiHostClient {
    HistoryProductionOps ops;
    HistoryController controller{ops};
    // The language the title was last set in
    I18nLocale titleLocale;

    // The chart is laid out (and reduced, when its key changed) for the item the frame placed;
    // the window takes the settings' material, and the toolkit and the title the view's language.
    void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) override {
        HistoryUiView view = controller.view;
        host.graphics->locale = view.locale.LocaleName();
        if (std::exchange(titleLocale, view.locale) != view.locale) {
            SetWindowTextW(host.main, HistoryWindowTitle(view.locale).c_str());
        }
        UiHostSetBackdrop(host, MaterialBackdrop(view.material));
        view.theme = host.theme.kind;
        HistoryUiBuildFrame(*host.graphics, view, client, frame);
        if (const UiItem* chart = UiFindItem(frame, kHistoryUiChart)) {
            controller.LayChart(*host.graphics, chart->rect, UiHostScale(host));
        }
    }

    void PaintCustom(UiHost& host, ID2D1RenderTarget* target, UiTargetCache& cache, const UiItem& item,
                     const UiPaintState& state) override {
        HistoryUiPaintCustom(target, *host.graphics, cache, host.palette, controller.chart, controller.picked,
                             controller.view.locale, controller.exported, item, state, UiHostScale(host));
    }

    // Only a change of the picked lines repaints the chart's surface; the readout's rows change
    // with the picked samples. An ended hover (a hide trigger) leaves the inspection on while the
    // pointer is still over the chart.
    bool ChartHover(UiHost& host, uint32_t, std::optional<D2D1_POINT_2F> point, UiTooltipContent& readout) override {
        bool changed = controller.Hover(point, readout, host.palette);
        controller.Inspect(point.has_value() || PointerOnChart(host));
        return changed;
    }

    // A new selection or view scrolls the content back to its top; a successful Enable recording
    // focuses the selected entry, and so does a command with dialogs (Export, the More menu's,
    // Delete history) whose loads changed the selection, as an activation's load does. Those
    // commands return once their dialogs are done; an export's write then runs off this thread,
    // polled by the window's timer.
    void Invoke(UiHost& host, UiAction action) override {
        if (Unloading() || action.kind != UiActionKind::Command) return;
        uint64_t selected = controller.selectedId;
        HistoryView shown = controller.view.view;
        HistoryUiCommand command = HistoryUiCommand(action.target);
        controller.Command(command, action.value, action.key);
        bool waited = command == HistoryUiCommand::Export || command == HistoryUiCommand::Menu ||
                      command == HistoryUiCommand::DeleteHistory;
        if (std::exchange(controller.focusSelected, false) || (waited && controller.selectedId != selected)) {
            UiHostRequestFocus(host, SelectedEntry(), false);
        }
        UiHostRequestRebuild(host, controller.selectedId != selected || controller.view.view != shown);
    }

    // Explicit loads: the first one, the activation request (once the host brought the window
    // back), F5 and Ctrl+R from either surface; during a dialog or an operation they wait for its
    // end. Notifications become one posted refresh, which holds what it must while the popup is
    // open (the deferral timer checks it), the pointer inspects the chart, or a dialog or an
    // operation runs. A load or refresh that changed the selection (its request, or the account no
    // longer listed) moves focus to the selected entry. Esc closes. An export's write poll
    // repaints the progress when its rows moved (the item found again each time, no rebuild) and
    // rebuilds once the write ended. A regional format change composes again like a notification.
    bool Message(UiHost& host, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) override {
        result = 0;
        uint64_t selected = controller.selectedId;
        bool rebuild = true;
        switch (message) {
            case kHistoryLoadMessage:
            case kHistoryActivateMessage:
                controller.RequestLoad();
                break;
            case WM_KEYDOWN:
                if (wParam == VK_ESCAPE) {
                    PostMessageW(host.main, WM_CLOSE, 0, 0);
                    return true;
                }
                if (wParam != VK_F5 && !(wParam == 'R' && GetKeyState(VK_CONTROL) < 0)) return false;
                controller.RequestLoad();
                break;
            case kSettingsRefreshMessage:
            case kHistoryConsentChangedMessage:
                controller.Notify();
                return true;
            case WM_SETTINGCHANGE:
                if (lParam && wcscmp(reinterpret_cast<PCWSTR>(lParam), L"intl") == 0) controller.Notify();
                return false;
            case kHistoryRefreshMessage:
                rebuild = controller.ProcessRefresh(host.popup.open, PointerOnChart(host));
                break;
            case WM_TIMER:
                if (wParam == kHistoryWriteTimer) {
                    size_t rows = controller.exported;
                    if (controller.WriteTick()) break;
                    if (controller.exported != rows) UiHostPaintNow(host, kHistoryUiProgress);
                    return true;
                }
                if (wParam != kHistoryDeferralTimer) return false;
                controller.DeferralTick(host.popup.open, PointerOnChart(host));
                return true;
            default:
                return false;
        }
        bool moved = controller.selectedId != selected;
        if (moved) UiHostRequestFocus(host, SelectedEntry(), false);
        if (rebuild || moved) UiHostRequestRebuild(host, moved);
        return true;
    }

    // The window is published before its first load is posted: an open request stored before
    // either is taken by that load, a later one by its activation. Notifications reach it from
    // then on. Its dialogs publish themselves where unload cancels them (g_historyDialog) and end
    // once the close latch or unload says so.
    bool Attach(UiHost& host) override {
        ops.host = &host;
        ops.dialogs = {&g_historyDialog, [this] { return controller.closeRequested || ops.Unloading(); }};
        controller.Open();
        g_historyWindow.store(host.main);
        PostMessageW(host.main, kHistoryLoadMessage, 0, 0);
        return true;
    }

    // Every close starts here: the controller's close latch first, then the running dialog (and
    // any that opens after it) is cancelled; while one runs the host retries the close once it
    // has unwound.
    bool CancelModal() override {
        controller.RequestClose();
        return HistoryDialogCancel();
    }

    bool Unloading() override { return ops.Unloading(); }

    bool PrepareClose(UiHost&) override { return true; }

    void Destroying() override { g_historyWindow.store(nullptr); }

    void Destroyed() override {}

    // The navigation entry of the selected account (or All accounts).
    UiFocusRef SelectedEntry() const {
        const HistoryUiView& view = controller.view;
        return {view.selected >= 0 ? kHistoryUiNavAccount + view.accounts[size_t(view.selected)].slot : kHistoryUiNavAll,
                -1};
    }

    // Whether the host's last pointer position is over the chart's item (content surface).
    bool PointerOnChart(const UiHost& host) const {
        const UiItem* chart = UiFindItem(host.frame, kHistoryUiChart);
        return chart && host.pointerSurface == UiSurfaceId::Content && UiContains(chart->rect, host.pointerAt);
    }
};
