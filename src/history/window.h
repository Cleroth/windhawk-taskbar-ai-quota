// History window layout, procedures, thread, and open/close.

static void HistoryLayout(HistoryWindowState& state, bool newFont) {
    HistoryClearHover(state);
    RECT client{};
    GetClientRect(state.window, &client);
    if (newFont) {
        state.renderWidth = state.layoutWidth = -1;
        HFONT font = CreateFontW(-MulDiv(9, state.dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (font) {
            EnumChildWindows(state.window, [](HWND child, LPARAM value) -> BOOL {
                SendMessageW(child, WM_SETFONT, WPARAM(value), TRUE);
                return TRUE;
            }, reinterpret_cast<LPARAM>(font));
            if (state.graphTip) SendMessageW(state.graphTip, WM_SETFONT, WPARAM(font), FALSE);
            if (state.font) DeleteObject(state.font);
            state.font = font;
        }
    }
    // Layout table uses logical pixels; combo heights include their drop-down lists.
    int width = MulDiv(client.right, 96, state.dpi);
    int height = MulDiv(client.bottom, 96, state.dpi);
    bool showBanner = state.to && !state.enabled;
    int bannerHeight = showBanner ? 54 : 0;
    struct Placement { HWND control; int x, y, w, h; };
    const Placement positions[] = {
        {state.recordingBanner, 12, 10, width - 24, 42},
        {state.enableButton, width - 184, 17, 160, 28},
        {state.accountLabel, 12, 15, 80, 22}, {state.accountCombo, 96, 10, width - 108, 320},
        {state.meterLabel, 12, 51, 80, 22}, {state.meterCombo, 96, 46, width - 328, 310},
        {state.rangeLabel, width - 220, 51, 64, 22}, {state.rangeCombo, width - 154, 46, 142, 180},
        {state.overlayCheck, 12, 80, 280, 26}, {state.legend, 312, 84, width - 324, 22},
        {state.refreshButton, 12, 113, 105, 28}, {state.exportButton, 127, 113, 145, 28},
        {state.clearButton, 282, 113, 135, 28}, {state.settingsButton, 427, 113, 105, 28},
        {state.infoButton, 542, 113, 80, 28},
        {state.graph, 12, 153 + bannerHeight, width - 24, std::max(0, height - 225 - bannerHeight)},
        {state.status, 12, std::max(153 + bannerHeight, height - 64), width - 24, 60},
    };
    for (const auto& p : positions) {
        int y = p.y;
        if (p.control != state.recordingBanner && p.control != state.enableButton &&
            p.control != state.graph && p.control != state.status) y += bannerHeight;
        MoveWindow(p.control, ScaleForDpi(p.x, state.dpi), ScaleForDpi(y, state.dpi),
                    ScaleForDpi(std::max(0, p.w), state.dpi), ScaleForDpi(std::max(0, p.h), state.dpi), TRUE);
    }
    ShowWindow(state.recordingBanner, showBanner ? SW_SHOW : SW_HIDE);
    ShowWindow(state.enableButton, showBanner ? SW_SHOW : SW_HIDE);
    EnableWindow(state.enableButton, showBanner && !state.loading);
    SetWindowPos(state.enableButton, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    HistoryScroll(state, state.scroll);
}

static LRESULT CALLBACK HistoryGraphWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<HistoryWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    try {
        if (message == WM_NCCREATE) {
            state = static_cast<HistoryWindowState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            state->graph = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
        if (!state) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
            case WM_CREATE: {
                state->graphTip = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TRANSPARENT, TOOLTIPS_CLASSW, nullptr,
                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                    CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, GetModuleHandleW(nullptr), nullptr);
                if (state->graphTip) {
                    TOOLINFOW tool{};
                    // Only V2 fields are used; the v6-only size is rejected by comctl32 v5.
                    tool.cbSize = TTTOOLINFOW_V2_SIZE;
                    tool.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_TRANSPARENT;
                    tool.hwnd = window;
                    tool.uId = 1;
                    tool.lpszText = const_cast<PWSTR>(L"");
                    if (!SendMessageW(state->graphTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool))) {
                        DestroyWindow(state->graphTip);
                        state->graphTip = nullptr;
                    }
                }
                return 0;
            }
            case WM_PAINT: HistoryPaintGraph(*state); return 0;
            case WM_ERASEBKGND: return 1;
            case WM_SIZE:
                HistoryScroll(*state, state->scroll);
                return 0;
            case WM_MOUSEMOVE:
                HistoryUpdateHover(*state, {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))});
                return 0;
            case WM_MOUSELEAVE:
                HistoryClearHover(*state);
                return 0;
            case WM_CANCELMODE:
                HistoryClearHover(*state);
                break;  // DefWindowProc must release capture and cancel scrollbar thumb tracking.
            case WM_LBUTTONDOWN: SetFocus(window); return 0;
            case WM_KILLFOCUS: HistoryClearHover(*state); InvalidateRect(window, nullptr, FALSE); return 0;
            case WM_SETFOCUS: InvalidateRect(window, nullptr, FALSE); return 0;
            case WM_GETDLGCODE: {
                const auto* key = reinterpret_cast<const MSG*>(lParam);
                if (key && key->message == WM_KEYDOWN && (key->wParam == VK_PRIOR || key->wParam == VK_NEXT ||
                    key->wParam == VK_HOME || key->wParam == VK_END)) return DLGC_WANTMESSAGE;
                return DLGC_WANTARROWS;
            }
            case WM_MOUSEWHEEL: {
                UINT lines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                state->wheelRemainder += GET_WHEEL_DELTA_WPARAM(wParam);
                int notches = state->wheelRemainder / WHEEL_DELTA;
                state->wheelRemainder %= WHEEL_DELTA;
                RECT client{};
                GetClientRect(window, &client);
                int step = lines == WHEEL_PAGESCROLL ? client.bottom : ScaleForDpi(20, state->dpi) * int(std::min(lines, 100U));
                long long position = static_cast<long long>(state->scroll) - static_cast<long long>(notches) * step;
                HistoryScroll(*state, int(std::clamp(position, 0LL, static_cast<long long>(INT_MAX))));
                return 0;
            }
            case WM_KEYDOWN: case WM_VSCROLL: {
                SCROLLINFO info{};
                info.cbSize = sizeof(info);
                info.fMask = SIF_ALL;
                GetScrollInfo(window, SB_VERT, &info);
                int code = LOWORD(wParam);
                if (message == WM_KEYDOWN) {
                    switch (wParam) {
                        case VK_UP: code = SB_LINEUP; break;
                        case VK_DOWN: code = SB_LINEDOWN; break;
                        case VK_PRIOR: code = SB_PAGEUP; break;
                        case VK_NEXT: code = SB_PAGEDOWN; break;
                        case VK_HOME: code = SB_TOP; break;
                        case VK_END: code = SB_BOTTOM; break;
                        default: return DefWindowProcW(window, message, wParam, lParam);
                    }
                }
                long long position = state->scroll;
                switch (code) {
                    case SB_LINEUP: position -= ScaleForDpi(32, state->dpi); break;
                    case SB_LINEDOWN: position += ScaleForDpi(32, state->dpi); break;
                    case SB_PAGEUP: position -= info.nPage; break;
                    case SB_PAGEDOWN: position += info.nPage; break;
                    case SB_TOP: position = 0; break;
                    case SB_BOTTOM: position = info.nMax; break;
                    case SB_THUMBTRACK: case SB_THUMBPOSITION: position = info.nTrackPos; break;
                    default: return 0;
                }
                HistoryScroll(*state, int(std::clamp(position, 0LL, static_cast<long long>(INT_MAX))));
                return 0;
            }
            case WM_DESTROY:
                HistoryClearHover(*state);
                if (state->graphTip) DestroyWindow(state->graphTip);
                state->graphTip = nullptr;
                return 0;
            case WM_NCDESTROY:
                state->graph = nullptr;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    } catch (...) {
        Wh_Log(L"History graph callback failed");
        if (state && state->window) {
            SetWindowTextW(state->status, L"ERROR: history rendering failed; closing the window.");
            PostMessageW(state->window, WM_CLOSE, 0, 0);
        }
        return message == WM_NCCREATE ? FALSE : 0;
    }
}

static LRESULT CALLBACK HistoryWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<HistoryWindowState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    try {
        if (message == WM_NCCREATE) {
            state = static_cast<HistoryWindowState*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        }
        if (!state) return DefWindowProcW(window, message, wParam, lParam);
        switch (message) {
            case WM_CREATE: {
                state->dpi = WindowDpi(window);
                HINSTANCE instance = GetModuleHandleW(nullptr);
                struct Control { HWND* slot; PCWSTR kind; PCWSTR text; DWORD style; int id; };
                const Control controls[] = {
                    // Z-order alone doesn't clip sibling painting: the banner must not erase its overlaid button.
                    {&state->recordingBanner, L"STATIC", L"   History recording is off.", SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX | WS_BORDER | WS_CLIPSIBLINGS, 0},
                    {&state->enableButton, L"BUTTON", L"E&nable recording", WS_TABSTOP | BS_PUSHBUTTON, kHistoryEnableButton},
                    {&state->accountLabel, L"STATIC", L"&Account:", SS_LEFT, 0},
                    {&state->accountCombo, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kHistoryAccountCombo},
                    {&state->meterLabel, L"STATIC", L"&View:", SS_LEFT, 0},
                    {&state->meterCombo, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kHistoryMeterCombo},
                    {&state->rangeLabel, L"STATIC", L"&Range:", SS_LEFT, 0},
                    {&state->rangeCombo, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, kHistoryRangeCombo},
                    {&state->overlayCheck, L"BUTTON", L"&Compare previous week", WS_TABSTOP | BS_AUTOCHECKBOX, kHistoryOverlayCheck},
                    {&state->refreshButton, L"BUTTON", L"&Refresh (F5)", WS_TABSTOP | BS_PUSHBUTTON, kHistoryRefreshButton},
                    {&state->exportButton, L"BUTTON", L"&Export CSV...", WS_TABSTOP | BS_PUSHBUTTON, kHistoryExportButton},
                    {&state->clearButton, L"BUTTON", L"&Clear all history...", WS_TABSTOP | BS_PUSHBUTTON, kHistoryClearButton},
                    {&state->settingsButton, L"BUTTON", L"&Settings...", WS_TABSTOP | BS_PUSHBUTTON, kHistorySettingsButton},
                    {&state->infoButton, L"BUTTON", L"&Info...", WS_TABSTOP | BS_PUSHBUTTON, kHistoryInfoButton},
                    {&state->legend, L"STATIC", L"Solid: current", SS_LEFT | SS_NOPREFIX, 0},
                    {&state->graph, kHistoryGraphClass, L"Usage history graph and legend; arrows, Page Up/Down, Home/End scroll", WS_TABSTOP | WS_VSCROLL | WS_BORDER, kHistoryGraphControl},
                    {&state->status, L"STATIC", L"Loading local history...", SS_LEFT | SS_NOPREFIX, 0},
                };
                for (const auto& control : controls) {
                    *control.slot = CreateWindowExW(0, control.kind, control.text, WS_CHILD | WS_VISIBLE | control.style,
                        0, 0, 0, 0, window, reinterpret_cast<HMENU>(INT_PTR(control.id)), instance,
                        control.slot == &state->graph ? state : nullptr);
                    if (!*control.slot) return -1;
                }
                state->exportTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                    WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, CW_USEDEFAULT, CW_USEDEFAULT,
                    CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
                if (state->exportTip) {
                    TOOLINFOW tip{};
                    tip.cbSize = sizeof(tip);
                    tip.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                    tip.hwnd = window;
                    tip.uId = reinterpret_cast<UINT_PTR>(state->exportButton);
                    tip.lpszText = const_cast<PWSTR>(L"Export all raw meters for the selected account, or every account under All accounts. Uses the selected date range, plus the preceding 7 days in week comparison. UTC timestamps; text IDs; no labels.");
                    SendMessageW(state->exportTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tip));
                    SendMessageW(state->exportTip, TTM_SETMAXTIPWIDTH, 0, ScaleForDpi(420, state->dpi));
                }
                for (int view = 0; view < 3; ++view)
                    SendMessageW(state->meterCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(kHistoryViewNames[view]));
                for (auto range : {L"24 hours", L"7 days", L"30 days", L"90 days", L"All retained"})
                    SendMessageW(state->rangeCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(range));
                SendMessageW(state->meterCombo, CB_SETCURSEL, WPARAM(state->allAccountsView), 0);
                SendMessageW(state->rangeCombo, CB_SETCURSEL, state->range, 0);
                EnableWindow(state->exportButton, FALSE);
                HistoryLayout(*state, true);
                PostMessageW(window, kHistoryLoadMessage, 0, 0);
                return 0;
            }
            case kHistoryActivateMessage:
                if (state->closing || g_unloading || g_historyWindowCancelRequested) return 0;
                ShowWindow(window, IsIconic(window) ? SW_RESTORE : SW_SHOW);
                SetForegroundWindow(g_historyDialog.load() ? g_historyDialog.load() : window);
                HistoryLoad(*state);
                return 0;
            case kHistoryLoadMessage: HistoryLoad(*state); return 0;
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE) HistoryClearHover(*state);
                break;
            case WM_ENABLE:
                if (!wParam) HistoryClearHover(*state);
                break;
            case WM_SIZE: if (state->graph) HistoryLayout(*state, false); return 0;
            case WM_DPICHANGED: {
                state->dpi = HIWORD(wParam);
                const auto* rect = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left,
                             rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
                HistoryLayout(*state, true);
                if (state->exportTip) SendMessageW(state->exportTip, TTM_SETMAXTIPWIDTH, 0, ScaleForDpi(420, state->dpi));
                return 0;
            }
            case WM_GETMINMAXINFO: {
                auto* size = reinterpret_cast<MINMAXINFO*>(lParam);
                size->ptMinTrackSize = {ScaleForDpi(800, state->dpi), ScaleForDpi(590, state->dpi)};
                return 0;
            }
            case WM_SETFOCUS: if (state->accountCombo) SetFocus(state->accountCombo); return 0;
            case WM_MOUSEWHEEL: return SendMessageW(state->graph, message, wParam, lParam);
            case WM_CTLCOLORSTATIC:
                if (reinterpret_cast<HWND>(lParam) == state->recordingBanner) {
                    SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_INFOTEXT));
                    SetBkColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_INFOBK));
                    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_INFOBK));
                }
                break;
            case WM_COMMAND: {
                int id = LOWORD(wParam), event = HIWORD(wParam);
                if (id == IDCANCEL) { PostMessageW(window, WM_CLOSE, 0, 0); return 0; }
                if (state->closing || state->modal || state->loading || g_unloading || g_historyWindowCancelRequested) return 0;
                if (event == CBN_SELCHANGE) {
                    if (id == kHistoryAccountCombo) {
                        state->selected = int(SendMessageW(state->accountCombo, CB_GETCURSEL, 0, 0)) - 1;
                        HistoryRebuildView(*state);
                    } else if (id == kHistoryMeterCombo) {
                        int choice = int(SendMessageW(state->meterCombo, CB_GETCURSEL, 0, 0));
                        if (choice != CB_ERR) {
                            auto& view = state->selected < 0 ? state->allAccountsView : state->accountView;
                            view = HistoryView(std::clamp(choice, 0, int(HistoryView::AllMeters)));
                            HistoryRebuildView(*state);
                        }
                    } else if (id == kHistoryRangeCombo) {
                        state->range = std::clamp(int(SendMessageW(state->rangeCombo, CB_GETCURSEL, 0, 0)), 0, 4);
                        HistoryLoad(*state);
                    }
                } else if (event == BN_CLICKED) {
                    switch (id) {
                        case kHistoryOverlayCheck:
                            state->overlay = SendMessageW(state->overlayCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                            // Show the actual comparison range rather than a disabled,
                            // misleading old selection. Restore the user's range afterward.
                            SendMessageW(state->rangeCombo, CB_SETCURSEL, state->overlay ? 1 : state->range, 0);
                            EnableWindow(state->rangeCombo, !state->overlay);
                            HistoryLoad(*state);
                            break;
                        case kHistoryRefreshButton: HistoryLoad(*state); break;
                        case kHistoryExportButton: HistoryExport(*state); break;
                        case kHistorySettingsButton: OpenSettingsWindow(); break;
                        case kHistoryEnableButton: {
                            SettingsApplyResult applied;
                            {
                                std::lock_guard configLock(g_configEditMutex);
                                if (g_unloading) break;
                                Settings settings;
                                {
                                    std::lock_guard settingsLock(g_settingsMutex);
                                    settings = g_settings;
                                }
                                settings.historyEnabled = true;
                                applied = ApplyOwnedSettings(std::move(settings));
                            }
                            // The shared edit path persists this user's consent without
                            // changing account selections. Never marshal with the edit lock held.
                            if (applied == SettingsApplyResult::Changed) FinishSettingsApply();
                            NotifySettingsWindowChanged(true);
                            HistoryLoad(*state);
                            if (applied == SettingsApplyResult::Failed) {
                                HistorySetStatus(*state, L"Could not save recording consent. Recording is still off.");
                            } else {
                                SetFocus(state->accountCombo);
                            }
                            break;
                        }
                        case kHistoryInfoButton: {
                            std::wstring info =
                                L"Reading the charts\r\n"
                                L"These are sampled quota levels, not tokens or daily consumption. Choose Short-term, Weekly, Extra usage, or All meters. "
                                L"Weekly follows each Anthropic account's taskbar weekly selections; removed accounts keep both weeklies. "
                                L"Refresh/F5 picks up changed selections. Antigravity Gemini and Claude/GPT quotas share the matching short/weekly views. "
                                L"Each quota keeps its own line and resets; overlapping limits are never added or merged. "
                                L"Short-window labels use recorded durations (or current limit when unknown). Antigravity shows limiting pools, which can change. "
                                L"All accounts starts on Weekly. A single account starts on All meters. Both modes offer all four views; "
                                L"All meters includes weeklies hidden on the taskbar. "
                                L"Each mode remembers its view until the window closes.\r\n\r\n"
                                L"Legend and hover\r\n"
                                L"The wrapping legend above the plot identifies each plotted account/quota with its line color and width. "
                                L"Hover a legend entry to emphasize that quota and dim the others; both comparison periods stay visible. "
                                L"Hover the plot to identify every nearby or overlapping line, including current and previous weeks. "
                                L"Each line reports a real recorded endpoint nearest its closest drawn segment, not an interpolated reading. "
                                L"Previous-week dates remain the original sample dates. Gaps cannot be hovered as connected lines. "
                                L"Details-only dollar spending and empty statuses have no line legend.\r\n\r\n"
                                L"Extra usage and empty entries\r\n"
                                L"Extra usage shows Anthropic budget used (%) on the left and OpenAI credits remaining on the right. "
                                L"Usage rises with spending; remaining credits fall. Dollars appear in details, including accounts without percentage readings. "
                                L"All meters also plots Anthropic dollar spending on its own axis, separate from percentages and credits. "
                                L"Unsupported quotas are omitted; accounts without readings get a compact status entry. "
                                L"Dollar details report sampled last/peak spending, not total spending across the selected range.\r\n\r\n"
                                L"Lines and markers\r\n"
                                L"All accounts uses an account color family: primary quotas are thicker, scoped quotas thinner and tinted. "
                                L"Single-account colors stay tied to each meter. Solid lines: current. Dashed lines: previous. Dots mark only isolated samples "
                                L"(filled: current, hollow: previous). Resets, missing or failed readings, gaps, restarts, "
                                L"and recording changes break lines without extra markers. "
                                L"Lines are reduced for drawing; statistics and CSV use the raw samples.\r\n\r\n"
                                L"Statistics and time\r\n"
                                L"Peak is the highest recorded value. Coverage counts connected intervals. Time above the red threshold is "
                                L"estimated within those intervals, never across gaps or beyond the last sample. Dates are local. "
                                L"Week comparison uses two elapsed seven-day periods; daylight saving can shift local clock times.\r\n\r\n"
                                L"Recording and export\r\n"
                                L"Recording is on by default. When it is off, enable it using the banner or Settings; the choice applies to "
                                L"the current Windows user only, and per-account selections and hidden-account rules still apply. "
                                L"Recording starts with the next poll. "
                                L"Pending recording waits for account history IDs to be saved. "
                                L"No data can also mean an unsupported meter, failed polls, or expired history. Nothing is backfilled. "
                                L"Refresh / F5 reads local history only. CSV includes all supported meters for the selected accounts and range "
                                L"(both weeks when comparing), with UTC times and IDs instead of labels. Credits are not dollars or a spending ledger.\r\n\r\n"
                                L"Retention: " + std::to_wstring(state->retention) + L" days, also limited to 250,000 records / about 60 MB. "
                                L"Turning recording off keeps saved history. Removed accounts remain under their IDs until expiry or Clear.";
                            if (!state->snapshot.samples.empty()) {
                                info += L"\r\nOldest loaded sample: " + HistoryLocalDate(state->snapshot.samples.front().timeMs);
                            }
                            HistoryModalScope modal(*state);
                            if (modal.hook) MessageBoxW(window, info.c_str(), L"Usage history — Info", MB_OK | MB_ICONINFORMATION);
                            break;
                        }
                        case kHistoryClearButton: {
                            int answer = IDCANCEL;
                            {
                                HistoryModalScope modal(*state);
                                if (!modal.hook) break;
                                answer = MessageBoxW(window,
                                    L"Permanently clear ALL local usage history for this Windows user, including deleted accounts?\r\n\r\nThis cannot be undone. Settings and sign-ins are not changed. If recording stays enabled, future polls will record new history.",
                                    L"Clear all usage history", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2);
                            }
                            if (state->closing || g_unloading || g_historyWindowCancelRequested) break;
                            if (answer != IDOK) { HistorySetStatus(*state, L"Clear history cancelled."); break; }
                            std::wstring error;
                            bool cleared = ClearHistory(&error);
                            // Clear changed the history epoch even if the rewrite failed.
                            InvalidateBurnUpCurves();
                            if (cleared) {
                                HistoryLoad(*state);
                                HistorySetStatus(*state, L"All local history cleared. Future polls can start recording again.");
                            } else HistorySetStatus(*state, L"ERROR: could not clear all history. " + error);
                            break;
                        }
                    }
                }
                return 0;
            }
            case WM_CLOSE:
                state->closing = true;
                HistoryClearHover(*state);
                if (state->modal) {
                    if (HWND dialog = g_historyDialog.load()) {
                        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
                        PostMessageW(dialog, WM_CLOSE, 0, 0);
                    }
                    return 0;
                }
                DestroyWindow(window);
                return 0;
            case WM_DESTROY:
                g_historyWindow.store(nullptr);
                PostQuitMessage(0);
                return 0;
            case WM_NCDESTROY:
                // Children are gone; no control can still use the font or stack state.
                if (state->font) { DeleteObject(state->font); state->font = nullptr; }
                state->window = nullptr;
                state->graph = nullptr;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                break;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    } catch (...) {
        Wh_Log(L"History window callback failed");
        if (message == WM_NCCREATE) return FALSE;
        if (message == WM_CREATE) return -1;
        if (state && state->status) SetWindowTextW(state->status, L"ERROR: history operation failed; closing the window.");
        PostMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    }
}

static DWORD WINAPI HistoryWindowThreadProc(LPVOID) {
    // COM is only for the native file dialog. No XAML or WinRT references live here.
    HINSTANCE instance = GetModuleHandleW(nullptr);
    HistoryWindowState state;  // Outlives all callbacks, including DestroyWindow below.
    bool registeredWindow = false, registeredGraph = false;
    HRESULT apartment = E_FAIL;
    try {
        using SetThreadDpi_t = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
            auto setDpi = reinterpret_cast<SetThreadDpi_t>(GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
            if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
        apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&controls);
        if (g_unloading || g_historyWindowCancelRequested) {
            if (SUCCEEDED(apartment)) CoUninitialize();
            return 0;
        }
        WNDCLASSEXW cls{};
        cls.cbSize = sizeof(cls);
        cls.hInstance = instance;
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
        cls.lpszClassName = kHistoryWindowClass;
        cls.lpfnWndProc = HistoryWindowProc;
        registeredWindow = RegisterClassExW(&cls) != 0;
        if (!registeredWindow && GetLastError() == ERROR_CLASS_ALREADY_EXISTS &&
            UnregisterClassW(kHistoryWindowClass, instance)) {
            registeredWindow = RegisterClassExW(&cls) != 0;
        }
        cls.hbrBackground = nullptr;
        cls.lpszClassName = kHistoryGraphClass;
        cls.lpfnWndProc = HistoryGraphWndProc;
        if (registeredWindow) {
            registeredGraph = RegisterClassExW(&cls) != 0;
            if (!registeredGraph && GetLastError() == ERROR_CLASS_ALREADY_EXISTS &&
                UnregisterClassW(kHistoryGraphClass, instance)) {
                registeredGraph = RegisterClassExW(&cls) != 0;
            }
        }
        if (registeredWindow && registeredGraph && !g_unloading && !g_historyWindowCancelRequested) {
            POINT cursor{};
            GetCursorPos(&cursor);
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
            UINT dpi = WindowDpi(nullptr);
            int width = std::min(ScaleForDpi(1100, dpi), int(monitor.rcWork.right - monitor.rcWork.left));
            int height = std::min(ScaleForDpi(830, dpi), int(monitor.rcWork.bottom - monitor.rcWork.top));
            int x = monitor.rcWork.left + (monitor.rcWork.right - monitor.rcWork.left - width) / 2;
            int y = monitor.rcWork.top + (monitor.rcWork.bottom - monitor.rcWork.top - height) / 2;
            HWND window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_CONTROLPARENT, kHistoryWindowClass,
                L"Taskbar AI Quota Bars — Usage history", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                x, y, width, height, nullptr, nullptr, instance, &state);
            if (window) {
                g_historyWindow.store(window);
                if (g_unloading || g_historyWindowCancelRequested) PostMessageW(window, WM_CLOSE, 0, 0);
                else { ShowWindow(window, SW_SHOW); SetForegroundWindow(window); }
                MSG message{};
                int result;
                while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                    if (message.message == WM_KEYDOWN &&
                        (message.wParam == VK_F5 || (message.wParam == 'R' && GetKeyState(VK_CONTROL) < 0))) {
                        if (!state.modal) PostMessageW(window, kHistoryLoadMessage, 0, 0);
                        continue;
                    }
                    if (!IsDialogMessageW(window, &message)) {
                        TranslateMessage(&message);
                        DispatchMessageW(&message);
                    }
                }
                if (result < 0) Wh_Log(L"History message loop failed: %lu", GetLastError());
            } else Wh_Log(L"Could not create history window: %lu", GetLastError());
        } else if (!g_unloading && !g_historyWindowCancelRequested) {
            Wh_Log(L"Could not register history window classes: %lu", GetLastError());
        }
    } catch (...) {
        Wh_Log(L"History worker failed");
    }
    // Native modal APIs have unwound and their hooks are removed before this point.
    if (state.window && IsWindow(state.window)) DestroyWindow(state.window);
    if (state.font) DeleteObject(state.font);
    g_historyDialog.store(nullptr);
    g_historyWindow.store(nullptr);
    if (registeredGraph) UnregisterClassW(kHistoryGraphClass, instance);
    if (registeredWindow) UnregisterClassW(kHistoryWindowClass, instance);
    if (SUCCEEDED(apartment)) CoUninitialize();
    return 0;
}

// Opens or activates the window, selecting the account with `historyId` if it is listed; 0 keeps
// the current selection.
static void OpenHistoryWindow(uint64_t historyId) {
    if (g_unloading) return;
    std::lock_guard lock(g_historyWindowMutex);
    // Recheck under the same lock as close/thread publication; never spawn during unload.
    if (g_unloading || g_historyWindowCancelRequested) return;
    // The latest request wins. Stored before the window check: a window published after it (its
    // thread still starting) runs its first load later and takes the request then.
    g_historyRequestedAccount.store(historyId);
    if (HWND window = g_historyWindow.load(); window && IsWindow(window)) {
        // Without the activation, no prompt load would take the request.
        if (!PostMessageW(window, kHistoryActivateMessage, 0, 0)) g_historyRequestedAccount.store(0);
        return;
    }
    if (g_historyWindowThread) {
        if (WaitForSingleObject(g_historyWindowThread, 0) != WAIT_OBJECT_0) return;
        CloseHandle(g_historyWindowThread);
        g_historyWindowThread = nullptr;
    }
    g_historyWindowThread = CreateThread(nullptr, 0, HistoryWindowThreadProc, nullptr, 0, nullptr);
    if (!g_historyWindowThread) Wh_Log(L"Could not create history window thread: %lu", GetLastError());
}

// Lifecycle API: call from the unload thread, after g_unloading=true. Never from this
// window's thread and never while holding settings/history/auth locks.
static void CloseHistoryWindow() {
    HANDLE thread = nullptr;
    {
        std::lock_guard lock(g_historyWindowMutex);
        g_historyWindowCancelRequested = true;
        if (HWND dialog = g_historyDialog.load()) {
            PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            PostMessageW(dialog, WM_CLOSE, 0, 0);
        }
        if (HWND window = g_historyWindow.load()) PostMessageW(window, WM_CLOSE, 0, 0);
        thread = g_historyWindowThread;
        g_historyWindowThread = nullptr;
    }
    if (thread) {
        // A finite wait could unload the DLL while a dialog/hook still runs mod code.
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
    g_historyDialog.store(nullptr);
    g_historyWindow.store(nullptr);
}
