// Account editor dialog and the add, edit, remove, reorder, and hide account commands.

static void LayoutAccountEditor(HWND hWnd, const AccountEditorState& state) {
    auto sc = [&](int value) { return ScaleForDpi(value, state.dpi); };
    RECT client{};
    GetClientRect(hWnd, &client);
    int width = client.right;
    int height = client.bottom;
    int controlX = width < sc(390) ? sc(100) : sc(130);
    int controlWidth = std::max(sc(120), width - controlX - sc(16));
    SetWindowPos(GetDlgItem(hWnd, kAccountProviderLabel), nullptr,
                 sc(16), sc(18), controlX - sc(28), sc(22), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountProvider), nullptr,
                 controlX, sc(14), controlWidth, sc(220), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountLabelLabel), nullptr,
                 sc(16), sc(58), controlX - sc(28), sc(22), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountLabel), nullptr,
                 controlX, sc(54), controlWidth, sc(26), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountTileIdentityLabel), nullptr,
                 sc(16), sc(98), controlX - sc(28), sc(22), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountTileIdentity), nullptr,
                 controlX, sc(94), controlWidth, sc(220), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountTileTextLabel), nullptr,
                 sc(16), sc(138), controlX - sc(28), sc(22), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountTileText), nullptr,
                 controlX, sc(134), sc(80), sc(26), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountFiveHour), nullptr,
                 sc(16), sc(178), sc(180), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountWeekly), nullptr,
                 sc(16), sc(208), sc(180), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountFableWeekly), nullptr,
                 sc(16), sc(238), width - sc(32), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    // For OpenAI the 4th checkbox reads "Show credits bar, max:" and the max edit follows it.
    bool openai = SendDlgItemMessageW(hWnd, kAccountProvider, CB_GETCURSEL, 0, 0) == 1;
    SetWindowPos(GetDlgItem(hWnd, kAccountExtraUsage), nullptr,
                 sc(16), sc(268), openai ? sc(164) : width - sc(32), sc(24),
                 SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountCreditsMax), nullptr,
                 sc(180), sc(268), sc(80), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountRecordHistory), nullptr,
                 sc(16), sc(298), width - sc(32), sc(24), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, kAccountHistoryIdentityNote), nullptr,
                 sc(16), sc(326), width - sc(32), sc(44), SWP_NOZORDER | SWP_NOACTIVATE);
    int buttonY = std::max(sc(386), height - sc(46));
    SetWindowPos(GetDlgItem(hWnd, IDOK), nullptr,
                 width - sc(182), buttonY, sc(80), sc(30), SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(GetDlgItem(hWnd, IDCANCEL), nullptr,
                 width - sc(96), buttonY, sc(80), sc(30), SWP_NOZORDER | SWP_NOACTIVATE);
}

static void RecreateAccountEditorVisuals(HWND hWnd, AccountEditorState& state) {
    state.dark = IsWindowsDarkMode();
    if (state.font) DeleteObject(state.font);
    if (state.backgroundBrush) DeleteObject(state.backgroundBrush);
    if (state.inputBrush) DeleteObject(state.inputBrush);
    LOGFONTW lf{};
    lf.lfHeight = -MulDiv(9, (int)state.dpi, 72);
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    state.font = CreateFontIndirectW(&lf);
    state.backgroundBrush = CreateSolidBrush(state.dark ? RGB(32, 32, 32) :
                                                          GetSysColor(COLOR_BTNFACE));
    state.inputBrush = CreateSolidBrush(state.dark ? RGB(43, 43, 43) :
                                                     GetSysColor(COLOR_WINDOW));
    EnumChildWindows(hWnd, [](HWND child, LPARAM param) -> BOOL {
        SendMessageW(child, WM_SETFONT, param, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(state.font));
    ApplyNativeWindowTheme(hWnd, state.dark);
    RedrawWindow(hWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

static void UpdateAccountEditorProvider(HWND hWnd) {
    int providerIndex = (int)SendDlgItemMessageW(hWnd, kAccountProvider, CB_GETCURSEL, 0, 0);
    bool anthropic = providerIndex == 0;
    bool openai = providerIndex == 1;
    HWND fableWeekly = GetDlgItem(hWnd, kAccountFableWeekly);
    HWND extraUsage = GetDlgItem(hWnd, kAccountExtraUsage);
    EnableWindow(fableWeekly, anthropic);
    EnableWindow(extraUsage, anthropic || openai);
    SetWindowTextW(extraUsage, openai ? L"Show credits bar, max:" :
                                        L"Show monthly extra-usage bar");
    ShowWindow(GetDlgItem(hWnd, kAccountCreditsMax), openai ? SW_SHOW : SW_HIDE);
}

static void UpdateAccountEditorTileText(HWND hWnd) {
    bool text = SendDlgItemMessageW(hWnd, kAccountTileIdentity, CB_GETCURSEL, 0, 0) ==
                (LRESULT)TileIdentity::Text;
    EnableWindow(GetDlgItem(hWnd, kAccountTileTextLabel), text);
    EnableWindow(GetDlgItem(hWnd, kAccountTileText), text);
}

static bool HasDuplicateAccount(const Settings& settings, uint64_t ignoredIdentity,
                                const AccountConfig& account) {
    for (const auto& existing : settings.accounts) {
        if (ignoredIdentity && AccountIdentityHash(existing) == ignoredIdentity) continue;
        if (existing.provider == account.provider && existing.label == account.label) return true;
    }
    return false;
}

static LRESULT CALLBACK AccountEditorWndProc(HWND hWnd, UINT message,
                                             WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AccountEditorState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<AccountEditorState*>(create->lpCreateParams);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            state->dpi = WindowDpi(hWnd);
            auto sc = [&](int value) { return ScaleForDpi(value, state->dpi); };
            HWND providerLabel = CreateWindowExW(0, L"STATIC", L"Provider", WS_CHILD | WS_VISIBLE,
                                                  sc(16), sc(18), sc(100), sc(22), hWnd,
                                                  reinterpret_cast<HMENU>(kAccountProviderLabel),
                                                  GetModuleHandleW(nullptr), nullptr);
            HWND provider = CreateWindowExW(0, L"COMBOBOX", L"",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                             sc(130), sc(14), sc(240), sc(220), hWnd,
                                             reinterpret_cast<HMENU>(kAccountProvider),
                                             GetModuleHandleW(nullptr), nullptr);
            AddComboItems(provider, {L"Anthropic (Claude)", L"OpenAI (ChatGPT/Codex)",
                                     L"Google Antigravity"});
            int providerIndex = state->account.provider == L"openai" ? 1 :
                                state->account.provider == L"antigravity" ? 2 : 0;
            SendMessageW(provider, CB_SETCURSEL, providerIndex, 0);

            HWND labelLabel = CreateWindowExW(0, L"STATIC", L"Label", WS_CHILD | WS_VISIBLE,
                                               sc(16), sc(58), sc(100), sc(22), hWnd,
                                               reinterpret_cast<HMENU>(kAccountLabelLabel),
                                               GetModuleHandleW(nullptr), nullptr);
            state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", state->account.label.c_str(),
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                           sc(130), sc(54), sc(240), sc(26), hWnd,
                                           reinterpret_cast<HMENU>(kAccountLabel),
                                           GetModuleHandleW(nullptr), nullptr);
            SendMessageW(state->edit, EM_SETLIMITTEXT, 64, 0);

            // Tile identity is used only by the ring and logo layouts.
            CreateWindowExW(0, L"STATIC", L"Tile identity", WS_CHILD | WS_VISIBLE,
                            sc(16), sc(98), sc(100), sc(22), hWnd,
                            reinterpret_cast<HMENU>(kAccountTileIdentityLabel),
                            GetModuleHandleW(nullptr), nullptr);
            HWND tileIdentity = CreateWindowExW(
                0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                sc(130), sc(94), sc(240), sc(220), hWnd,
                reinterpret_cast<HMENU>(kAccountTileIdentity), GetModuleHandleW(nullptr), nullptr);
            AddComboItems(tileIdentity, {L"Logo + number badge", L"Logo only", L"Label text"});
            SendMessageW(tileIdentity, CB_SETCURSEL, (int)state->account.tileIdentity, 0);
            CreateWindowExW(0, L"STATIC", L"Short text", WS_CHILD | WS_VISIBLE,
                            sc(16), sc(138), sc(100), sc(22), hWnd,
                            reinterpret_cast<HMENU>(kAccountTileTextLabel),
                            GetModuleHandleW(nullptr), nullptr);
            HWND tileText = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", state->account.tileText.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                sc(130), sc(134), sc(80), sc(26), hWnd,
                reinterpret_cast<HMENU>(kAccountTileText), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(tileText, EM_SETLIMITTEXT, 4, 0);

            HWND fiveHour = CreateWindowExW(0, L"BUTTON", L"Show 5-hour bar",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                             sc(16), sc(178), sc(180), sc(24), hWnd,
                                             reinterpret_cast<HMENU>(kAccountFiveHour),
                                             GetModuleHandleW(nullptr), nullptr);
            HWND weekly = CreateWindowExW(0, L"BUTTON", L"Show weekly bar",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                           sc(16), sc(208), sc(180), sc(24), hWnd,
                                           reinterpret_cast<HMENU>(kAccountWeekly),
                                           GetModuleHandleW(nullptr), nullptr);
            HWND fable = CreateWindowExW(0, L"BUTTON", L"Show Fable weekly bar",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          sc(16), sc(238), sc(220), sc(24), hWnd,
                                          reinterpret_cast<HMENU>(kAccountFableWeekly),
                                          GetModuleHandleW(nullptr), nullptr);
            HWND extra = CreateWindowExW(0, L"BUTTON", L"Show monthly extra-usage bar",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                          sc(16), sc(268), sc(260), sc(24), hWnd,
                                          reinterpret_cast<HMENU>(kAccountExtraUsage),
                                          GetModuleHandleW(nullptr), nullptr);
            SendMessageW(fiveHour, BM_SETCHECK,
                         state->account.showBars[kFiveHourBar] ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(weekly, BM_SETCHECK,
                         state->account.showBars[kWeeklyBar] ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(fable, BM_SETCHECK,
                         state->account.showBars[kFableWeeklyBar] ? BST_CHECKED : BST_UNCHECKED, 0);
            SendMessageW(extra, BM_SETCHECK,
                         state->account.showBars[kExtraUsageBar] ? BST_CHECKED : BST_UNCHECKED, 0);
            // Shown only for OpenAI; UpdateAccountEditorProvider toggles visibility.
            std::wstring creditsMaxText = state->account.creditsMax > 0 ?
                std::to_wstring(state->account.creditsMax) : L"";
            HWND creditsMax = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", creditsMaxText.c_str(),
                                               WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
                                               sc(180), sc(268), sc(80), sc(24), hWnd,
                                               reinterpret_cast<HMENU>(kAccountCreditsMax),
                                               GetModuleHandleW(nullptr), nullptr);
            SendMessageW(creditsMax, EM_SETLIMITTEXT, 9, 0);

            HWND recordHistory = CreateWindowExW(
                0, L"BUTTON", L"Record history", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                sc(16), sc(298), sc(260), sc(24), hWnd,
                reinterpret_cast<HMENU>(kAccountRecordHistory), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(recordHistory, BM_SETCHECK,
                         state->account.recordHistory ? BST_CHECKED : BST_UNCHECKED, 0);
            CreateWindowExW(0, L"STATIC",
                L"History follows this entry, even after signing in again. "
                L"Use a new entry for a different login.",
                WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hWnd,
                reinterpret_cast<HMENU>(kAccountHistoryIdentityNote),
                GetModuleHandleW(nullptr), nullptr);
            state->toolTip = CreateWindowExW(
                WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (state->toolTip) {
                SendMessageW(state->toolTip, TTM_SETMAXTIPWIDTH, 0, sc(360));
                auto addToolTip = [&](HWND control, PCWSTR text) {
                    if (!control) return;
                    TOOLINFOW info{};
                    info.cbSize = sizeof(info);
                    info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
                    info.hwnd = hWnd;
                    info.uId = reinterpret_cast<UINT_PTR>(control);
                    info.lpszText = const_cast<PWSTR>(text);
                    SendMessageW(state->toolTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
                };
                addToolTip(tileIdentity,
                    L"Applies only to the ring and logo layouts; stacked and vertical bars show "
                    L"the label instead. The number badge appears only when several accounts "
                    L"share this provider.");
                addToolTip(tileText,
                    L"Up to 4 characters shown by Label text in the ring and logo layouts. "
                    L"Leave empty to use the label without spaces.");
                addToolTip(recordHistory,
                    L"This account preference is shared across Windows users. Records only when "
                    L"Enable local history is on for the current Windows user in Behavior. "
                    L"Do not poll hidden accounts overrides this checkbox: hidden accounts are not "
                    L"polled or recorded while that option is on. History follows this configured "
                    L"account, not the signed-in login. Use a new account entry for a different login.");
            }

            HWND ok = CreateWindowExW(0, L"BUTTON", L"OK",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                      sc(204), sc(232), sc(80), sc(30), hWnd,
                                      reinterpret_cast<HMENU>(IDOK), GetModuleHandleW(nullptr), nullptr);
            HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          sc(290), sc(232), sc(80), sc(30), hWnd,
                                          reinterpret_cast<HMENU>(IDCANCEL), GetModuleHandleW(nullptr), nullptr);
            (void)providerLabel;
            (void)labelLabel;
            (void)ok;
            (void)cancel;
            RecreateAccountEditorVisuals(hWnd, *state);
            LayoutAccountEditor(hWnd, *state);
            UpdateAccountEditorProvider(hWnd);
            UpdateAccountEditorTileText(hWnd);
            SetFocus(state->edit);
            return 0;
        }
        case WM_COMMAND:
            if (!state) break;
            if (LOWORD(wParam) == kAccountProvider && HIWORD(wParam) == CBN_SELCHANGE) {
                UpdateAccountEditorProvider(hWnd);
                LayoutAccountEditor(hWnd, *state);
                return 0;
            }
            if (LOWORD(wParam) == kAccountTileIdentity && HIWORD(wParam) == CBN_SELCHANGE) {
                UpdateAccountEditorTileText(hWnd);
                return 0;
            }
            if (LOWORD(wParam) == IDOK) {
                int length = GetWindowTextLengthW(state->edit);
                std::wstring label(length + 1, L'\0');
                GetWindowTextW(state->edit, label.data(), length + 1);
                label.resize(length);
                size_t first = label.find_first_not_of(L" \t\r\n");
                size_t last = label.find_last_not_of(L" \t\r\n");
                label = first == std::wstring::npos ? L"" : label.substr(first, last - first + 1);
                if (label.empty()) {
                    SettingsMessageBoxW(hWnd, L"Enter an account label.", L"Account",
                                MB_OK | MB_ICONWARNING);
                    SetFocus(state->edit);
                    return 0;
                }
                int providerIndex = (int)SendDlgItemMessageW(hWnd, kAccountProvider,
                                                              CB_GETCURSEL, 0, 0);
                state->account.provider = providerIndex == 1 ? L"openai" :
                                          providerIndex == 2 ? L"antigravity" : L"anthropic";
                state->account.label = std::move(label);
                int tileIdentity = (int)SendDlgItemMessageW(hWnd, kAccountTileIdentity,
                                                            CB_GETCURSEL, 0, 0);
                state->account.tileIdentity =
                    tileIdentity >= 0 && tileIdentity <= (int)TileIdentity::Text ?
                        (TileIdentity)tileIdentity : TileIdentity::LogoBadge;
                // Settings normalization trims and caps the text.
                wchar_t tileText[16] = {};
                GetDlgItemTextW(hWnd, kAccountTileText, tileText, ARRAYSIZE(tileText));
                state->account.tileText = tileText;
                state->account.recordHistory =
                    IsDlgButtonChecked(hWnd, kAccountRecordHistory) == BST_CHECKED;
                state->account.showBars[kFiveHourBar] =
                    IsDlgButtonChecked(hWnd, kAccountFiveHour) == BST_CHECKED;
                state->account.showBars[kWeeklyBar] =
                    IsDlgButtonChecked(hWnd, kAccountWeekly) == BST_CHECKED;
                state->account.showBars[kFableWeeklyBar] =
                    state->account.provider == L"anthropic" &&
                    IsDlgButtonChecked(hWnd, kAccountFableWeekly) == BST_CHECKED;
                bool openai = state->account.provider == L"openai";
                state->account.showBars[kExtraUsageBar] =
                    (state->account.provider == L"anthropic" || openai) &&
                    IsDlgButtonChecked(hWnd, kAccountExtraUsage) == BST_CHECKED;
                state->account.creditsMax =
                    openai ? (int)GetDlgItemInt(hWnd, kAccountCreditsMax, nullptr, FALSE) : 0;
                if (openai && state->account.showBars[kExtraUsageBar] &&
                    state->account.creditsMax <= 0) {
                    SettingsMessageBoxW(hWnd, L"Enter the credits max for the credits bar.",
                                        L"Account", MB_OK | MB_ICONWARNING);
                    SetFocus(GetDlgItem(hWnd, kAccountCreditsMax));
                    return 0;
                }
                if (!state->account.showBars[kFiveHourBar] &&
                    !state->account.showBars[kWeeklyBar] &&
                    !state->account.showBars[kFableWeeklyBar] &&
                    !state->account.showBars[kExtraUsageBar]) {
                    SettingsMessageBoxW(hWnd, L"Select at least one quota bar.", L"Account",
                                MB_OK | MB_ICONWARNING);
                    return 0;
                }
                bool duplicate;
                {
                    std::lock_guard<std::mutex> lk(g_settingsMutex);
                    duplicate = HasDuplicateAccount(g_settings, state->originalIdentity,
                                                    state->account);
                }
                if (duplicate) {
                    SettingsMessageBoxW(hWnd,
                                        L"That provider and label are already configured.",
                                        L"Account", MB_OK | MB_ICONWARNING);
                    SetFocus(state->edit);
                    return 0;
                }
                state->accepted = true;
                DestroyWindow(hWnd);
                return 0;
            }
            if (LOWORD(wParam) == IDCANCEL) {
                DestroyWindow(hWnd);
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            if (state && state->dark) {
                SetTextColor(reinterpret_cast<HDC>(wParam), RGB(235, 235, 235));
                SetBkColor(reinterpret_cast<HDC>(wParam), RGB(32, 32, 32));
                return reinterpret_cast<LRESULT>(state->backgroundBrush);
            }
            break;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            if (state && state->dark) {
                SetTextColor(reinterpret_cast<HDC>(wParam), RGB(235, 235, 235));
                SetBkColor(reinterpret_cast<HDC>(wParam), RGB(43, 43, 43));
                return reinterpret_cast<LRESULT>(state->inputBrush);
            }
            break;
        case WM_DPICHANGED:
            if (state) {
                state->dpi = HIWORD(wParam);
                auto* suggested = reinterpret_cast<RECT*>(lParam);
                MONITORINFO monitorInfo{sizeof(monitorInfo)};
                GetMonitorInfoW(MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST),
                                &monitorInfo);
                int width = std::min((int)(suggested->right - suggested->left),
                                     (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left));
                int height = std::min((int)(suggested->bottom - suggested->top),
                                      (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top));
                int x = std::clamp((int)suggested->left, (int)monitorInfo.rcWork.left,
                                   (int)monitorInfo.rcWork.right - width);
                int y = std::clamp((int)suggested->top, (int)monitorInfo.rcWork.top,
                                   (int)monitorInfo.rcWork.bottom - height);
                SetWindowPos(hWnd, nullptr, x, y, width, height,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                RecreateAccountEditorVisuals(hWnd, *state);
                LayoutAccountEditor(hWnd, *state);
            }
            return 0;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            if (state) RecreateAccountEditorVisuals(hWnd, *state);
            return 0;
        case WM_ERASEBKGND:
            if (state && state->backgroundBrush) {
                RECT client{};
                GetClientRect(hWnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                return 1;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            if (state) {
                if (state->font) DeleteObject(state->font);
                if (state->backgroundBrush) DeleteObject(state->backgroundBrush);
                if (state->inputBrush) DeleteObject(state->inputBrush);
                state->font = nullptr;
                state->backgroundBrush = nullptr;
                state->inputBrush = nullptr;
                state->done = true;
            }
            return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

static bool ShowAccountEditor(HWND owner, AccountConfig* account, bool adding) {
    AccountEditorState state;
    state.account = *account;
    state.originalIdentity = adding ? 0 : AccountIdentityHash(*account);
    UINT dpi = WindowDpi(owner);
    int width = ScaleForDpi(404, dpi);
    int height = ScaleForDpi(468, dpi);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &monitorInfo);
    width = std::min(width, (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left));
    height = std::min(height, (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top));
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;
    x = std::clamp(x, (int)monitorInfo.rcWork.left, (int)monitorInfo.rcWork.right - width);
    y = std::clamp(y, (int)monitorInfo.rcWork.top, (int)monitorInfo.rcWork.bottom - height);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, kAccountEditorClass,
                                  adding ? L"Add account" : L"Edit account",
                                  WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  x, y, width, height, owner, nullptr,
                                  GetModuleHandleW(nullptr), &state);
    if (!window) return false;

    EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    MSG message;
    while (!g_unloading && !g_settingsWindowCancelRequested && !state.done) {
        int result = GetMessageW(&message, nullptr, 0, 0);
        if (result == 0) {
            PostQuitMessage((int)message.wParam);
            break;
        }
        if (result < 0) break;
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(window)) DestroyWindow(window);
    if (IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
    if (state.accepted) *account = std::move(state.account);
    return state.accepted;
}

static void AddAccountFromSettingsWindow(SettingsWindowState& state) {
    AccountConfig account{L"anthropic", L"A"};
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        for (int suffix = 1;; suffix++) {
            account.label = suffix == 1 ? L"A" : L"A" + std::to_wstring(suffix);
            if (!HasDuplicateAccount(g_settings, 0, account)) break;
        }
    }
    if (!ShowAccountEditor(state.hWnd, &account, true)) return;
    if (g_unloading || g_settingsWindowCancelRequested || !IsWindow(state.hWnd)) return;
    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    if (HasDuplicateAccount(settings, 0, account)) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"That provider and label are already configured.",
                    L"Account", MB_OK | MB_ICONWARNING);
        return;
    }
    settings.accounts.push_back(std::move(account));
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(settings));
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not save the account.", L"Account",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    RefreshSettingsControls(state);
}

static void EditAccountFromSettingsWindow(SettingsWindowState& state) {
    int index = SelectedAccountIndex(state);
    AccountConfig oldAccount;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        if (index < 0 || index >= (int)g_settings.accounts.size()) return;
        oldAccount = g_settings.accounts[index];
    }
    AccountConfig newAccount = oldAccount;
    if (!ShowAccountEditor(state.hWnd, &newAccount, false)) return;
    if (g_unloading || g_settingsWindowCancelRequested || !IsWindow(state.hWnd)) return;

    bool identityChanged = AccountIdentityHash(oldAccount) != AccountIdentityHash(newAccount);
    // Tile identity is read only while building tiles, so its edits take the rebuild path.
    bool onlyBarSelectionChanged = oldAccount.provider == newAccount.provider &&
                                   oldAccount.label == newAccount.label &&
                                   oldAccount.tileIdentity == newAccount.tileIdentity &&
                                   oldAccount.tileText == newAccount.tileText &&
                                   oldAccount.creditsMax == newAccount.creditsMax &&
                                   oldAccount.recordHistory == newAccount.recordHistory &&
                                   oldAccount.historyId == newAccount.historyId &&
                                   oldAccount.showBars != newAccount.showBars;
    if (identityChanged && g_loginInProgress.load()) {
        SettingsMessageBoxW(state.hWnd, L"Wait for the current sign-in to finish before changing identity.",
                    L"Account", MB_OK | MB_ICONINFORMATION);
        return;
    }
    bool clearOldToken = false;
    if (identityChanged && oldAccount.provider != newAccount.provider &&
        oldAccount.provider != L"antigravity") {
        StoredToken token;
        if (LoadStoredToken(AccountIdentityHash(oldAccount), &token)) {
            int result = SettingsMessageBoxW(
                state.hWnd,
                L"Changing provider creates a new account identity.\n\n"
                L"Yes: delete the old stored sign-in\nNo: keep it for later\nCancel: discard this edit",
                L"Change account provider", MB_YESNOCANCEL | MB_ICONQUESTION);
            if ((result != IDYES && result != IDNO) || g_unloading ||
                !IsWindow(state.hWnd)) return;
            clearOldToken = result == IDYES;
        }
    }

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    if (identityChanged && g_loginInProgress.load()) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"Wait for the current sign-in to finish before changing identity.",
                    L"Account", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    uint64_t oldIdentity = AccountIdentityHash(oldAccount);
    index = -1;
    for (size_t i = 0; i < settings.accounts.size(); i++) {
        if (AccountIdentityHash(settings.accounts[i]) == oldIdentity) {
            index = (int)i;
            break;
        }
    }
    if (index < 0) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"The account was changed from another taskbar window.",
                    L"Account", MB_OK | MB_ICONWARNING);
        RefreshSettingsControls(state);
        return;
    }
    if (HasDuplicateAccount(settings, oldIdentity, newAccount)) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"That provider and label are already configured.",
                    L"Account", MB_OK | MB_ICONWARNING);
        return;
    }

    uint64_t newIdentity = AccountIdentityHash(newAccount);
    bool sameProviderIdentityChange = identityChanged &&
                                      oldAccount.provider == newAccount.provider;
    bool sameProviderRename = sameProviderIdentityChange &&
                              oldAccount.provider != L"antigravity";
    TokenCopyResult tokenCopyResult = TokenCopyResult::SourceMissing;
    bool oldTokenCleared = true;
    ULONGLONG oldAuthEpoch = 0;
    ULONGLONG newAuthEpoch = 0;
    std::unique_lock<std::mutex> authEpochLock(g_authEpochMutex, std::defer_lock);
    if (sameProviderRename) {
        // Block epoch-gated saves only across the local copy/settings/delete transaction.
        authEpochLock.lock();
        oldAuthEpoch = AuthEpochLocked(oldIdentity);
        newAuthEpoch = AuthEpochLocked(newIdentity);
        tokenCopyResult = CopyStoredTokenForRename(oldIdentity, newIdentity);
        if (tokenCopyResult == TokenCopyResult::DestinationOccupied ||
            tokenCopyResult == TokenCopyResult::Failed) {
            authEpochLock.unlock();
            configLock.unlock();
            SettingsMessageBoxW(
                state.hWnd,
                tokenCopyResult == TokenCopyResult::DestinationOccupied ?
                    L"The renamed account identity already has a retained sign-in. To replace it, "
                    L"add an account with that provider and label, then remove it and delete its "
                    L"stored sign-in. The account was not changed." :
                    L"The stored sign-in could not be copied. The account was not changed.",
                L"Account", MB_OK | MB_ICONERROR);
            return;
        }
    }

    // Visibility can be toggled from a taskbar menu while the account editor is open.
    newAccount.hidden = settings.accounts[index].hidden;
    // Renames retain history; switching providers starts a new series during normalization.
    newAccount.historyId = oldAccount.provider == newAccount.provider ?
                               settings.accounts[index].historyId : 0;
    settings.accounts[index] = newAccount;
    SettingsApplyResult applyResult = ApplyOwnedSettings(
        std::move(settings), sameProviderIdentityChange ? oldIdentity : 0,
        sameProviderIdentityChange ? newIdentity : 0);
    if (applyResult == SettingsApplyResult::Failed) {
        bool copiedTokenCleared = tokenCopyResult != TokenCopyResult::Copied ||
                                  ClearStoredToken(newIdentity);
        if (authEpochLock.owns_lock()) authEpochLock.unlock();
        configLock.unlock();
        SettingsMessageBoxW(
            state.hWnd,
            copiedTokenCleared ?
                L"Could not save the account." :
                L"Could not save the account. The copied sign-in remains under the attempted "
                L"new identity. To delete it, add an account with the attempted provider and "
                L"label, then remove it and choose to delete its stored sign-in.",
            L"Account", MB_OK | MB_ICONERROR);
        return;
    }
    if (sameProviderRename) {
        oldTokenCleared = ClearStoredToken(oldIdentity);
        BumpAuthEpochLocked(oldIdentity);
        g_authRenameRedirects.push_back(
            {oldIdentity, oldAuthEpoch, newIdentity, newAuthEpoch});
        authEpochLock.unlock();
    }
    configLock.unlock();
    bool providerTokenCleared = true;
    if (clearOldToken) {
        providerTokenCleared = ClearStoredTokenAndBumpAuthEpoch(oldIdentity);
    }
    if (applyResult == SettingsApplyResult::Changed) {
        // A quota selection change updates in place: bar layouts already hold every bar and
        // only change visibility and spacing; tile layouts regenerate their meter shapes.
        if (onlyBarSelectionChanged) {
            PostUiUpdate();
            if (!g_uiInjected.load(std::memory_order_acquire)) StartRetryInject();
        } else {
            FinishSettingsApply();
        }
    }
    if (!oldTokenCleared) {
        SettingsMessageBoxW(
            state.hWnd,
            L"The account was renamed, but a duplicate stored sign-in remains under the old "
            L"identity. To delete it, re-add an account with the old provider and label, then "
            L"remove it and choose to delete its stored sign-in.",
            L"Account", MB_OK | MB_ICONWARNING);
    } else if (!providerTokenCleared) {
        SettingsMessageBoxW(
            state.hWnd,
            L"The provider was changed, but the old sign-in remains retained. To recover or "
            L"delete it, re-add an account with the old provider and label.",
            L"Account", MB_OK | MB_ICONWARNING);
    }
    NotifySettingsWindowChanged();
    RefreshSettingsControls(state);
}

static void RemoveAccountFromSettingsWindow(SettingsWindowState& state) {
    int index = SelectedAccountIndex(state);
    AccountConfig account;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        if (index < 0 || index >= (int)g_settings.accounts.size()) return;
        account = g_settings.accounts[index];
    }
    if (g_loginInProgress.load()) {
        SettingsMessageBoxW(state.hWnd, L"Wait for the current sign-in to finish before removing accounts.",
                    L"Account", MB_OK | MB_ICONINFORMATION);
        return;
    }
    bool deleteToken = false;
    if (account.provider == L"antigravity") {
        int result = SettingsMessageBoxW(state.hWnd, L"Remove this account?", L"Remove account",
                                 MB_YESNO | MB_ICONQUESTION);
        if (result != IDYES || g_unloading || !IsWindow(state.hWnd)) return;
    } else {
        int result = SettingsMessageBoxW(
            state.hWnd,
            L"Remove this account?\n\n"
            L"Yes: remove it and delete its stored sign-in\n"
            L"No: remove it but retain the sign-in for later\n"
            L"Cancel: do nothing",
            L"Remove account", MB_YESNOCANCEL | MB_ICONQUESTION);
        if ((result != IDYES && result != IDNO) || g_unloading ||
            !IsWindow(state.hWnd)) return;
        deleteToken = result == IDYES;
    }

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    if (g_loginInProgress.load()) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd, L"Wait for the current sign-in to finish before removing accounts.",
                    L"Account", MB_OK | MB_ICONINFORMATION);
        return;
    }
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    uint64_t identity = AccountIdentityHash(account);
    index = -1;
    for (size_t i = 0; i < settings.accounts.size(); i++) {
        if (AccountIdentityHash(settings.accounts[i]) == identity) {
            index = (int)i;
            break;
        }
    }
    if (index < 0) {
        configLock.unlock();
        RefreshSettingsControls(state);
        return;
    }
    if (deleteToken && !ClearStoredTokenAndBumpAuthEpoch(identity)) {
        configLock.unlock();
        SettingsMessageBoxW(state.hWnd,
                    L"The stored sign-in could not be deleted, so the account was not removed.",
                    L"Account", MB_OK | MB_ICONERROR);
        return;
    }
    settings.accounts.erase(settings.accounts.begin() + index);
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(settings));
    if (applyResult == SettingsApplyResult::Failed) {
        configLock.unlock();
        if (deleteToken) {
            RefreshQuotaByIdentity(identity);
            NotifySettingsWindowChanged();
        }
        SettingsMessageBoxW(state.hWnd,
                    deleteToken ?
                        L"The stored sign-in was deleted, but settings could not be saved. The "
                        L"account remains configured but signed out." :
                        L"Could not remove the account.",
                    L"Account",
                    MB_OK | MB_ICONERROR);
        return;
    }
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Changed) FinishSettingsApply();
    RefreshSettingsControls(state);
}

static void MoveAccountFromSettingsWindow(SettingsWindowState& state, int direction) {
    uint64_t identity = SelectedAccountIdentity(state);
    if (!identity || (direction != -1 && direction != 1)) return;

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    int index = -1;
    for (size_t i = 0; i < settings.accounts.size(); i++) {
        if (AccountIdentityHash(settings.accounts[i]) == identity) {
            index = (int)i;
            break;
        }
    }
    int target = index + direction;
    if (index < 0 || target < 0 || target >= (int)settings.accounts.size()) return;
    std::swap(settings.accounts[index], settings.accounts[target]);
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(settings));
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not reorder the account.", L"Account",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    RefreshSettingsControls(state);
}

static void ToggleAccountFromSettingsWindow(SettingsWindowState& state) {
    uint64_t identity = SelectedAccountIdentity(state);
    if (!identity) return;

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    int index = -1;
    int visibleCount = 0;
    for (size_t i = 0; i < settings.accounts.size(); i++) {
        if (!settings.accounts[i].hidden) visibleCount++;
        if (AccountIdentityHash(settings.accounts[i]) == identity) index = (int)i;
    }
    if (index < 0 || (!settings.accounts[index].hidden && visibleCount <= 1)) return;
    settings.accounts[index].hidden = !settings.accounts[index].hidden;
    SettingsApplyResult applyResult = ApplyOwnedSettings(settings);
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not update account visibility.", L"Account",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        std::wstring hashes;
        wchar_t buffer[24];
        for (const auto& account : settings.accounts) {
            if (!account.hidden) continue;
            if (!hashes.empty()) hashes += L";";
            swprintf(buffer, ARRAYSIZE(buffer), L"%016llx",
                     (unsigned long long)AccountIdentityHash(account));
            hashes += buffer;
        }
        Wh_SetStringValue(L"hiddenAccounts", hashes.c_str());
        FinishSettingsApply();
    }
    RefreshSettingsControls(state);
}
