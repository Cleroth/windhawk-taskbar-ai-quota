// Settings window procedure, thread, and activation.

static UINT GetSettingsActivateMessage() {
    static const UINT message =
        RegisterWindowMessageW(L"Windhawk_ActivateSettings_" WH_MOD_ID);
    return message;
}

static void ResetCurrentSettingsPage(SettingsWindowState& state) {
    int page = state.currentPage;
    if (page <= 0 || page >= 4) return;
    if (SettingsMessageBoxW(state.hWnd,
                    L"Reset every setting on this page to its default value?",
                    L"Reset settings", MB_YESNO | MB_ICONQUESTION) != IDYES ||
        g_unloading || !IsWindow(state.hWnd)) {
        return;
    }

    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    Settings defaults;
    if (page == 1) {
        settings.taskbarMonitorMode = defaults.taskbarMonitorMode;
        settings.taskbarMonitorNumber = defaults.taskbarMonitorNumber;
        settings.barLayout = defaults.barLayout;
        settings.barMode = defaults.barMode;
        settings.barLength = defaults.barLength;
        settings.barThickness = defaults.barThickness;
        settings.ringSize = defaults.ringSize;
        settings.ringThickness = defaults.ringThickness;
        settings.labelFontSize = defaults.labelFontSize;
        settings.percentFontSize = defaults.percentFontSize;
        settings.accountMargin = defaults.accountMargin;
        settings.labelGap = defaults.labelGap;
        settings.barGap = defaults.barGap;
        settings.rightMargin = defaults.rightMargin;
    } else if (page == 2) {
        settings.labelPosition = defaults.labelPosition;
        settings.showPaceTicks = defaults.showPaceTicks;
        settings.paceTickStyle = defaults.paceTickStyle;
        settings.paceTickColor = defaults.paceTickColor;
        settings.showBarLabels = defaults.showBarLabels;
        settings.percentTextVisibility = defaults.percentTextVisibility;
        settings.percentTextAlignment = defaults.percentTextAlignment;
        settings.showExtraBarAmounts = defaults.showExtraBarAmounts;
        settings.showOpenAiExtraLimits = defaults.showOpenAiExtraLimits;
        settings.colorblindMode = defaults.colorblindMode;
        settings.showStaleWarning = defaults.showStaleWarning;
        settings.yellowThreshold = defaults.yellowThreshold;
        settings.orangeThreshold = defaults.orangeThreshold;
        settings.redThreshold = defaults.redThreshold;
    } else {
        settings.showDetailsOnHover = defaults.showDetailsOnHover;
        settings.pollMinutes = defaults.pollMinutes;
        settings.enableNotifications = defaults.enableNotifications;
        settings.historyEnabled = defaults.historyEnabled;
        settings.historyRetentionDays = defaults.historyRetentionDays;
        settings.skipHiddenPolling = defaults.skipHiddenPolling;
    }
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(settings));
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not reset settings.", L"Reset settings",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    RefreshSettingsControls(state);
}

static LRESULT CALLBACK SettingsWindowProc(HWND hWnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SettingsWindowState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    if (UINT activateMessage = GetSettingsActivateMessage();
        activateMessage && message == activateMessage) {
        if (!g_unloading) {
            if (IsIconic(hWnd)) ShowWindow(hWnd, SW_RESTORE);
            HWND target = GetLastActivePopup(hWnd);
            SetForegroundWindow(IsWindow(target) ? target : hWnd);
        }
        return 0;
    }
    switch (message) {
        case WM_CREATE: {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = reinterpret_cast<SettingsWindowState*>(create->lpCreateParams);
            state->hWnd = hWnd;
            state->dpi = WindowDpi(hWnd);
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

            const PCWSTR pageNames[] = {L"Accounts", L"Layout", L"Display", L"Behavior"};
            const int pageIds[] = {kSettingsPageAccounts, kSettingsPageLayout,
                                   kSettingsPageDisplay, kSettingsPageBehavior};
            for (int i = 0; i < 4; i++) {
                DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                              BS_AUTORADIOBUTTON | BS_PUSHLIKE;
                if (i == 0) style |= WS_GROUP;
                state->pageButtons[i] = CreateWindowExW(
                    0, L"BUTTON", pageNames[i], style, 0, 0, 1, 1, hWnd,
                    reinterpret_cast<HMENU>((INT_PTR)pageIds[i]),
                    GetModuleHandleW(nullptr), nullptr);
            }
            state->accountList = CreateSettingsControl(
                *state, 0, WC_LISTVIEWW, L"",
                WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                WS_EX_CLIENTEDGE, kAccountList);
            ListView_SetExtendedListViewStyle(state->accountList,
                                              LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            if (!SetWindowSubclass(state->accountList, AccountListSubclassProc, 0,
                                   reinterpret_cast<DWORD_PTR>(state))) {
                Wh_Log(L"Could not subclass settings account list");
            }
            const PCWSTR headers[] = {L"Label", L"Provider", L"Bars", L"Visible", L"Sign-in", L"Recorded"};
            const int widths[] = {80, 135, 110, 55, 125, 100};
            for (int i = 0; i < (int)ARRAYSIZE(headers); i++) {
                LVCOLUMNW column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH;
                column.pszText = const_cast<PWSTR>(headers[i]);
                column.cx = ScaleForDpi(widths[i], state->dpi);
                ListView_InsertColumn(state->accountList, i, &column);
            }
            CreateSettingsControl(*state, 0, L"BUTTON", L"Add...",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountAdd);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Edit...",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountEdit);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Remove",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountRemove);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Up",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountMoveUp);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Down",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountMoveDown);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Hide",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountToggleVisible);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Sign in",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountSignIn);
            CreateSettingsControl(*state, 0, L"BUTTON", L"Sign out",
                                  WS_VISIBLE | WS_TABSTOP, 0, kAccountSignOut);

            AddSettingsCheck(*state, 1, L"Preview test data", kVisualTestModeLayout);
            HWND monitorMode = AddSettingsRow(*state, 1, L"Taskbar monitors",
                                               L"COMBOBOX", CBS_DROPDOWNLIST, 0, kMonitorMode);
            AddComboItems(monitorMode, {L"Primary monitor only", L"All monitors",
                                        L"Specific display"});
            AddSettingsRow(*state, 1, L"Specific display", L"COMBOBOX",
                           CBS_DROPDOWNLIST, 0, kMonitorNumber);
            HWND layout = AddSettingsRow(*state, 1, L"Layout", L"COMBOBOX",
                                         CBS_DROPDOWNLIST, 0, kBarLayout);
            AddComboItems(layout, {L"Stacked bars", L"Vertical bars", L"Segmented ring",
                                   L"Concentric rings", L"Logo + mini bars",
                                   L"Logo + headline %"});
            HWND mode = AddSettingsRow(*state, 1, L"Bar mode", L"COMBOBOX",
                                       CBS_DROPDOWNLIST, 0, kBarMode);
            AddComboItems(mode, {L"Used", L"Remaining"});
            AddNumericRow(*state, 1, L"Bar length (px)", kBarLength,
                          10, 500, true);
            AddNumericRow(*state, 1, L"Bar thickness (px)", kBarThickness,
                          2, 50, true);
            AddNumericRow(*state, 1, L"Ring size (px)", kRingSize,
                          kRingSizeMin, kRingSizeMax, true);
            AddNumericRow(*state, 1, L"Ring thickness (px)", kRingThickness,
                          kRingThicknessMin, kRingThicknessMax, true);
            AddNumericRow(*state, 1, L"Label font size (px)", kLabelFontSize,
                          6, 24, true);
            AddNumericRow(*state, 1, L"Bar text size (px)", kPercentFontSize,
                          6, 24, true);
            AddNumericRow(*state, 1, L"Account margin (px)", kAccountMargin, 0, 500);
            AddNumericRow(*state, 1, L"Label gap (px)", kLabelGap, 0, 500);
            AddNumericRow(*state, 1, L"Bar gap (px)", kBarGap, 0, 500);
            AddNumericRow(*state, 1, L"Right tray gap (px)", kRightMargin, 0, 500);

            AddSettingsCheck(*state, 2, L"Preview test data", kVisualTestModeDisplay);
            HWND labelPosition = AddSettingsRow(*state, 2, L"Label position", L"COMBOBOX",
                                                CBS_DROPDOWNLIST, 0, kLabelPosition);
            AddComboItems(labelPosition, {L"Hidden", L"Left", L"Top", L"Right", L"Bottom"});
            AddSettingsCheck(*state, 2, L"Show quota pace ticks", kShowPaceTicks);
            HWND paceTickStyle = AddSettingsRow(*state, 2, L"Pace tick style", L"COMBOBOX",
                                                CBS_DROPDOWNLIST, 0, kPaceTickStyle);
            AddComboItems(paceTickStyle,
                          {L"Caret", L"Full line", L"Edge notch", L"Dot"});
            HWND paceTickColor = AddSettingsRow(*state, 2, L"Pace tick color", L"BUTTON",
                                                BS_PUSHBUTTON, 0, kPaceTickColor);
            SetWindowTextW(paceTickColor, L"Choose...");
            state->rows[2].back().preview = CreateSettingsControl(
                *state, 2, L"STATIC", L"", WS_VISIBLE | SS_OWNERDRAW, 0, -1);
            AddSettingsCheck(*state, 2, L"Show bar labels (5h, 7d, Fa, Ex)",
                             kShowBarLabels);
            HWND percentTextVisibility = AddSettingsRow(
                *state, 2, L"Percentage text", L"COMBOBOX",
                CBS_DROPDOWNLIST, 0, kPercentTextVisibility);
            AddComboItems(percentTextVisibility,
                          {L"Never show", L"Show on hover", L"Always show"});
            HWND percentTextAlignment = AddSettingsRow(
                *state, 2, L"Percentage text alignment", L"COMBOBOX",
                CBS_DROPDOWNLIST, 0, kPercentTextAlignment);
            AddComboItems(percentTextAlignment, {L"Adaptive", L"Left", L"Center", L"Right"});
            AddSettingsCheck(*state, 2, L"Show amounts on extra/credits bars",
                             kShowExtraBarAmounts);
            AddSettingsCheck(*state, 2, L"Show additional OpenAI rate limits",
                             kShowOpenAiExtraLimits);
            AddSettingsCheck(*state, 2, L"Use colorblind palette", kColorblindMode);
            AddSettingsCheck(*state, 2, L"Mark stale data with !", kShowStaleWarning);
            AddThresholdRow(*state, L"Yellow threshold (%)", kYellowThreshold);
            AddThresholdRow(*state, L"Orange threshold (%)", kOrangeThreshold);
            AddThresholdRow(*state, L"Red threshold (%)", kRedThreshold);

            AddSettingsCheck(*state, 3, L"Show details on hover", kShowDetailsOnHover);
            HWND pollPreset = AddSettingsRow(*state, 3, L"Cloud poll interval",
                                              L"COMBOBOX", CBS_DROPDOWNLIST, 0, kPollPreset);
            AddComboItems(pollPreset, {L"2 minutes", L"5 minutes", L"10 minutes",
                                      L"15 minutes", L"30 minutes", L"60 minutes", L"Custom"});
            AddNumericRow(*state, 3, L"Custom interval (minutes)",
                          kPollMinutes, 2, 1440);
            AddSettingsCheck(*state, 3, L"Show threshold notifications", kEnableNotifications);
            AddSettingsCheck(*state, 3, L"Enable local history", kHistoryEnabled);
            AddNumericRow(*state, 3, L"Retention (days)", kHistoryRetentionDays, 1, 3650);
            AddSettingsCheck(*state, 3, L"Do not poll hidden accounts", kSkipHiddenPolling);
            HWND historyButton = AddSettingsRow(*state, 3, L"Local usage history", L"BUTTON",
                                                 BS_PUSHBUTTON, 0, kOpenHistoryWindow);
            SetWindowTextW(historyButton, L"Usage history...");
            state->resetPageButton = CreateWindowExW(
                0, L"BUTTON", L"", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, hWnd,
                reinterpret_cast<HMENU>((INT_PTR)kResetPage),
                GetModuleHandleW(nullptr), nullptr);
            state->previousRowsButton = CreateWindowExW(
                0, L"BUTTON", L"Previous", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, hWnd,
                reinterpret_cast<HMENU>((INT_PTR)kSettingsRowsPrevious),
                GetModuleHandleW(nullptr), nullptr);
            state->rowsPageLabel = CreateWindowExW(
                0, L"STATIC", L"", WS_CHILD | SS_CENTER | SS_CENTERIMAGE,
                0, 0, 1, 1, hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            state->nextRowsButton = CreateWindowExW(
                0, L"BUTTON", L"Next", WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 1, 1, hWnd,
                reinterpret_cast<HMENU>((INT_PTR)kSettingsRowsNext),
                GetModuleHandleW(nullptr), nullptr);

            state->toolTip = CreateWindowExW(
                WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                hWnd, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (state->toolTip) {
                SendMessageW(state->toolTip, TTM_SETMAXTIPWIDTH, 0,
                             ScaleForDpi(360, state->dpi));
                AddSettingsToolTip(
                    *state, kVisualTestModeLayout,
                    L"Temporarily replaces taskbar accounts with synthetic percentages while this settings window is open.");
                AddSettingsToolTip(
                    *state, kVisualTestModeDisplay,
                    L"Temporarily replaces taskbar accounts with synthetic percentages while this settings window is open.");
                AddSettingsToolTip(
                    *state, kShowPaceTicks,
                    L"Marks elapsed time in each quota reset window so usage can be compared with its expected pace.");
                AddSettingsToolTip(
                    *state, kPaceTickStyle,
                    L"Draws each pace marker as a thin caret, a full-width line, an edge notch, or a centered dot.");
                AddSettingsToolTip(
                    *state, kShowExtraBarAmounts,
                    L"Replaces the percentage on Anthropic extra-usage and OpenAI credits bars with the dollar or credit amount: spent in used mode, left in remaining mode.");
                AddSettingsToolTip(
                    *state, kShowOpenAiExtraLimits,
                    L"Adds every extra OpenAI rate limit (Codex Spark, hidden model lanes such as gpt-reserve) and the Spark plan name to OpenAI account details.");
                AddSettingsToolTip(
                    *state, kShowDetailsOnHover,
                    L"Opens an account's details after the pointer rests on its bars. Clicking the bars always opens them.");
                AddSettingsToolTip(
                    *state, kShowStaleWarning,
                    L"Adds ! when quota data is stale because a refresh failed or is overdue.");
                AddSettingsToolTip(
                    *state, kPollPreset,
                    L"How often Anthropic and OpenAI are polled. Antigravity always polls its local session once per minute.");
                AddSettingsToolTip(
                    *state, kPollMinutes,
                    L"Custom cloud polling interval from 2 to 1440 minutes.");
                AddSettingsToolTip(
                    *state, kHistoryEnabled,
                    L"Opt in for this Windows user only; off by default. Disabling recording keeps saved history. "
                    L"Account recording choices, retention, and hidden-account polling settings are shared "
                    L"across Windows users. Do not poll hidden accounts takes priority for hidden accounts.");
                AddSettingsToolTip(
                    *state, kHistoryRetentionDays,
                    L"Shared across Windows users. Keep local usage history for 1 to 3650 days. Reducing this permanently prunes older "
                    L"samples at hourly maintenance during recording or viewer refresh. The roughly 60 MB cap can shorten retention.");
                AddSettingsToolTip(
                    *state, kSkipHiddenPolling,
                    L"Shared across Windows users. Skip polling hidden accounts, even if Record history is checked. Turn this off "
                    L"to allow hidden accounts to keep recording when local history is enabled.");
                AddSettingsToolTip(
                    *state, kOpenHistoryWindow,
                    L"View saved local usage history, including while recording is disabled.");
                AddSettingsToolTip(
                    *state, kAccountList,
                    L"Recorded shows whether history recording is enabled, not whether a poll succeeded. "
                    L"Storage error means recording is not being saved; open Usage history for details. "
                    L"User off requires this Windows user's Enable local history in Behavior; Paused means the hidden "
                    L"account is skipped by Do not poll hidden accounts. Pending save blocks recording "
                    L"until stable history IDs are saved by the taskbar's Explorer process.");
            }

            RecreateSettingsVisuals(*state);
            RefreshSettingsControls(*state);
            ShowSettingsPage(*state, 0);
            LayoutSettingsWindow(*state);
            return 0;
        }
        case WM_NOTIFY:
            if (!state) break;
            if (reinterpret_cast<NMHDR*>(lParam)->code == UDN_DELTAPOS) {
                auto* delta = reinterpret_cast<NMUPDOWN*>(lParam);
                if (SettingsRow* row = FindSettingsRow(*state, delta->hdr.hwndFrom)) {
                    int id = GetDlgCtrlID(row->control);
                    int64_t proposed = (int64_t)GetControlInt(hWnd, id, delta->iPos) +
                                       delta->iDelta;
                    int value = (int)std::clamp<int64_t>(proposed, row->minimum, row->maximum);
                    state->updating = true;
                    SetControlInt(*state, id, value);
                    state->updating = false;
                    SetTimer(hWnd, kSettingsAutosaveTimer, 250, nullptr);
                    return TRUE;
                }
            }
            if (reinterpret_cast<NMHDR*>(lParam)->hwndFrom == state->accountList) {
                if (reinterpret_cast<NMHDR*>(lParam)->code == LVN_ITEMCHANGED) {
                    UpdateAccountButtons(*state);
                } else if (reinterpret_cast<NMHDR*>(lParam)->code == NM_DBLCLK) {
                    PostMessageW(hWnd, WM_COMMAND,
                                 MAKEWPARAM(kAccountEdit, BN_CLICKED), 0);
                }
            }
            break;
        case WM_DRAWITEM:
            if (state) {
                auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
                for (const auto& pageRows : state->rows) {
                    for (const auto& row : pageRows) {
                        if (!row.preview || draw->hwndItem != row.preview) continue;
                        HBRUSH brush = (HBRUSH)GetStockObject(DC_BRUSH);
                        SetDCBrushColor(draw->hDC,
                                        state->dark ? RGB(105, 105, 105) : RGB(120, 120, 120));
                        FillRect(draw->hDC, &draw->rcItem, brush);
                        RECT inner = draw->rcItem;
                        InflateRect(&inner, -1, -1);
                        SetDCBrushColor(draw->hDC, row.previewColor);
                        FillRect(draw->hDC, &inner, brush);
                        return TRUE;
                    }
                }
            }
            break;
        case WM_HSCROLL:
            if (state && lParam) {
                HWND slider = reinterpret_cast<HWND>(lParam);
                if (SettingsRow* row = FindSettingsRow(*state, slider);
                    row && row->slider == slider) {
                    int id = GetDlgCtrlID(row->control);
                    int value = (int)SendMessageW(slider, TBM_GETPOS, 0, 0);
                    state->updating = true;
                    SetControlInt(*state, id, value);
                    state->updating = false;
                    int action = LOWORD(wParam);
                    if (action == TB_ENDTRACK) {
                        CommitScalarSettings(*state);
                    } else if (action == TB_LINEUP || action == TB_LINEDOWN ||
                               action == TB_PAGEUP || action == TB_PAGEDOWN ||
                               action == TB_TOP || action == TB_BOTTOM ||
                               action == TB_THUMBPOSITION) {
                        SetTimer(hWnd, kSettingsAutosaveTimer, 250, nullptr);
                    }
                    return 0;
                }
            }
            break;
        case WM_COMMAND:
            if (!state) break;
            switch (LOWORD(wParam)) {
                case kSettingsPageAccounts:
                case kSettingsPageLayout:
                case kSettingsPageDisplay:
                case kSettingsPageBehavior:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        ShowSettingsPage(*state, LOWORD(wParam) - kSettingsPageAccounts);
                        LayoutSettingsWindow(*state);
                    }
                    return 0;
                case kAccountAdd:
                    if (HIWORD(wParam) == BN_CLICKED) AddAccountFromSettingsWindow(*state);
                    return 0;
                case kAccountEdit:
                    if (HIWORD(wParam) == BN_CLICKED && IsWindowEnabled(hWnd) &&
                        IsWindowEnabled(GetDlgItem(hWnd, kAccountEdit)) &&
                        !g_loginInProgress.load()) {
                        EditAccountFromSettingsWindow(*state);
                    }
                    return 0;
                case kAccountRemove:
                    if (HIWORD(wParam) == BN_CLICKED) RemoveAccountFromSettingsWindow(*state);
                    return 0;
                case kAccountMoveUp:
                    if (HIWORD(wParam) == BN_CLICKED) MoveAccountFromSettingsWindow(*state, -1);
                    return 0;
                case kAccountMoveDown:
                    if (HIWORD(wParam) == BN_CLICKED) MoveAccountFromSettingsWindow(*state, 1);
                    return 0;
                case kAccountToggleVisible:
                    if (HIWORD(wParam) == BN_CLICKED) ToggleAccountFromSettingsWindow(*state);
                    return 0;
                case kAccountSignIn:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        if (uint64_t identity = SelectedAccountIdentity(*state)) {
                            StartLoginByIdentity(identity);
                        }
                        UpdateAccountButtons(*state);
                    }
                    return 0;
                case kAccountSignOut:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        if (uint64_t identity = SelectedAccountIdentity(*state)) {
                            if (!SignOutAccountByIdentity(identity)) {
                                SettingsMessageBoxW(
                                    state->hWnd,
                                    L"The stored sign-in could not be deleted and remains "
                                    L"retained for this account.",
                                    L"Account", MB_OK | MB_ICONERROR);
                            }
                        }
                    }
                    return 0;
                case kResetPage:
                    if (HIWORD(wParam) == BN_CLICKED) ResetCurrentSettingsPage(*state);
                    return 0;
                case kOpenHistoryWindow:
                    if (HIWORD(wParam) == BN_CLICKED && !g_unloading) {
                        CommitScalarSettings(*state);
                        if (!g_unloading && !g_settingsWindowCancelRequested) OpenHistoryWindow();
                    }
                    return 0;
                case kSettingsRowsPrevious:
                    if (HIWORD(wParam) == BN_CLICKED && state->currentPage > 0) {
                        state->rowSubpages[state->currentPage]--;
                        LayoutSettingsWindow(*state);
                    }
                    return 0;
                case kSettingsRowsNext:
                    if (HIWORD(wParam) == BN_CLICKED && state->currentPage > 0) {
                        state->rowSubpages[state->currentPage]++;
                        LayoutSettingsWindow(*state);
                    }
                    return 0;
                case kVisualTestModeLayout:
                case kVisualTestModeDisplay:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        SetVisualTestMode(
                            SendMessageW(reinterpret_cast<HWND>(lParam), BM_GETCHECK,
                                         0, 0) == BST_CHECKED);
                    }
                    return 0;
                case kPaceTickColor:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        SettingsRow* row = FindSettingsRow(
                            *state, GetDlgItem(hWnd, kPaceTickColor));
                        if (!row) return 0;
                        CHOOSECOLORW chooser{};
                        chooser.lStructSize = sizeof(chooser);
                        chooser.hwndOwner = hWnd;
                        chooser.rgbResult = row->previewColor;
                        chooser.lpCustColors = state->customColors.data();
                        chooser.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ENABLEHOOK;
                        chooser.lCustData = reinterpret_cast<LPARAM>(state);
                        chooser.lpfnHook = SettingsColorHookProc;
                        bool accepted = ChooseColorW(&chooser);
                        state->colorDialog = nullptr;
                        if (accepted) {
                            row->previewColor = chooser.rgbResult;
                            if (row->preview) InvalidateRect(row->preview, nullptr, TRUE);
                            CommitScalarSettings(*state);
                        }
                    }
                    return 0;
                case kPollPreset:
                    if (HIWORD(wParam) == CBN_SELCHANGE &&
                        SendDlgItemMessageW(hWnd, kPollPreset, CB_GETCURSEL, 0, 0) == 6) {
                        UpdateDependentSettingsControls(*state);
                        SetFocus(GetDlgItem(hWnd, kPollMinutes));
                    } else if (HIWORD(wParam) == CBN_SELCHANGE) {
                        CommitScalarSettings(*state);
                    }
                    return 0;
            }
            if ((HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == BN_CLICKED ||
                 HIWORD(wParam) == EN_KILLFOCUS) && LOWORD(wParam) >= kMonitorMode &&
                LOWORD(wParam) <= kSkipHiddenPolling) {
                CommitScalarSettings(*state);
                return 0;
            }
            break;
        case WM_SIZE:
            if (state) LayoutSettingsWindow(*state);
            return 0;
        case WM_TIMER:
            if (state && wParam == kSettingsAutosaveTimer) {
                KillTimer(hWnd, kSettingsAutosaveTimer);
                CommitScalarSettings(*state);
                return 0;
            }
            break;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            UINT dpi = state ? state->dpi : 96;
            MONITORINFO monitorInfo{sizeof(monitorInfo)};
            GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
            int workWidth = (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left);
            int workHeight = (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top);
            info->ptMinTrackSize.x = std::min(ScaleForDpi(500, dpi), workWidth);
            if (state && state->lockedWindowHeight > 0) {
                int lockedHeight = std::min(state->lockedWindowHeight, workHeight);
                info->ptMinTrackSize.y = lockedHeight;
                info->ptMaxTrackSize.y = lockedHeight;
            } else {
                info->ptMinTrackSize.y = std::min(ScaleForDpi(400, dpi), workHeight);
            }
            return 0;
        }
        case WM_ENTERSIZEMOVE:
            if (state) state->inSizeMove = true;
            return 0;
        case WM_DPICHANGED:
            if (state) {
                state->dpi = HIWORD(wParam);
                state->lockedWindowHeight = 0;
                auto* suggested = reinterpret_cast<RECT*>(lParam);
                SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left,
                             suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                RecreateSettingsVisuals(*state);
                if (state->toolTip) {
                    SendMessageW(state->toolTip, TTM_SETMAXTIPWIDTH, 0,
                                 ScaleForDpi(360, state->dpi));
                }
                if (!state->inSizeMove) FitSettingsWindowToContent(*state);
                LayoutSettingsWindow(*state);
            }
            return 0;
        case WM_SETTINGCHANGE:
            if (state) {
                RecreateSettingsVisuals(*state);
                if (wParam == SPI_SETWORKAREA) {
                    FitSettingsWindowToContent(*state);
                    LayoutSettingsWindow(*state);
                }
            }
            return 0;
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            if (state) RecreateSettingsVisuals(*state);
            return 0;
        case WM_DISPLAYCHANGE:
            if (state) {
                int monitorNumber;
                {
                    std::lock_guard<std::mutex> lk(g_settingsMutex);
                    monitorNumber = g_settings.taskbarMonitorNumber;
                }
                state->updating = true;
                PopulateMonitorCombo(*state, monitorNumber);
                state->updating = false;
                UpdateDependentSettingsControls(*state);
                StartRetryInject(true);
                FitSettingsWindowToContent(*state);
                LayoutSettingsWindow(*state);
            }
            return 0;
        case WM_EXITSIZEMOVE:
            if (state) {
                state->inSizeMove = false;
                FitSettingsWindowToContent(*state, false);
                LayoutSettingsWindow(*state);
            }
            return 0;
        case kExitVisualTestMessage:
            SetVisualTestMode(false);
            return 0;
        case kHistoryConsentChangedMessage:
        case kSettingsRefreshMessage:
            if (state) {
                if (message == kHistoryConsentChangedMessage) {
                    bool historyEnabled;
                    {
                        std::lock_guard lock(g_settingsMutex);
                        historyEnabled = g_settings.historyEnabled;
                    }
                    // Only synchronize the externally changed toggle; don't overwrite
                    // unrelated edits in an already-open settings window.
                    bool updating = state->updating;
                    state->updating = true;
                    SendDlgItemMessageW(hWnd, kHistoryEnabled, BM_SETCHECK,
                                        historyEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
                    UpdateDependentSettingsControls(*state);
                    state->updating = updating;
                }
                bool enabled = g_visualTestMode.load(std::memory_order_acquire);
                SendDlgItemMessageW(hWnd, kVisualTestModeLayout, BM_SETCHECK,
                                    enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                SendDlgItemMessageW(hWnd, kVisualTestModeDisplay, BM_SETCHECK,
                                    enabled ? BST_CHECKED : BST_UNCHECKED, 0);
                RefreshAccountList(*state);
            }
            return 0;
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
        case WM_ERASEBKGND:
            if (state && state->backgroundBrush) {
                RECT client{};
                GetClientRect(hWnd, &client);
                FillRect(reinterpret_cast<HDC>(wParam), &client, state->backgroundBrush);
                return 1;
            }
            break;
        case WM_CLOSE:
            if (state && state->colorDialog) {
                PostMessageW(state->colorDialog, WM_COMMAND, IDCANCEL, 0);
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (g_settingsMessageBoxContext &&
                IsWindow(g_settingsMessageBoxContext->hWnd)) {
                g_settingsMessageBoxContext->forced = true;
                PostMessageW(g_settingsMessageBoxContext->hWnd, WM_COMMAND,
                             g_settingsMessageBoxContext->forcedResult, 0);
                PostMessageW(hWnd, WM_CLOSE, 0, 0);
                return 0;
            }
            if (state && !g_unloading) CommitScalarSettings(*state, false);
            SetVisualTestMode(false);
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
                state->hWnd = nullptr;
                state->accountList = nullptr;
            }
            g_settingsWindow.store(nullptr);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

static DWORD WINAPI SettingsWindowThreadProc(LPVOID) {
    using SetThreadDpiAwarenessContext_t = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto setDpi = reinterpret_cast<SetThreadDpiAwarenessContext_t>(
            GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        apartmentInitialized = true;
    } catch (...) {}

    INITCOMMONCONTROLSEX controls{sizeof(controls),
                                  ICC_WIN95_CLASSES | ICC_LISTVIEW_CLASSES |
                                      ICC_BAR_CLASSES | ICC_UPDOWN_CLASS};
    InitCommonControlsEx(&controls);
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW settingsClass{};
    settingsClass.cbSize = sizeof(settingsClass);
    settingsClass.lpfnWndProc = SettingsWindowProc;
    settingsClass.hInstance = instance;
    settingsClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    settingsClass.lpszClassName = kSettingsWindowClass;
    settingsClass.hbrBackground = nullptr;
    WNDCLASSEXW accountClass = settingsClass;
    accountClass.lpfnWndProc = AccountEditorWndProc;
    accountClass.lpszClassName = kAccountEditorClass;
    if (!RegisterClassExW(&settingsClass)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS ||
            !UnregisterClassW(kSettingsWindowClass, instance) ||
            !RegisterClassExW(&settingsClass)) {
            if (apartmentInitialized) winrt::uninit_apartment();
            return 0;
        }
    }
    if (!RegisterClassExW(&accountClass)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS ||
            !UnregisterClassW(kAccountEditorClass, instance) ||
            !RegisterClassExW(&accountClass)) {
            UnregisterClassW(kSettingsWindowClass, instance);
            if (apartmentInitialized) winrt::uninit_apartment();
            return 0;
        }
    }
    if (g_unloading || g_settingsWindowCancelRequested) {
        UnregisterClassW(kAccountEditorClass, instance);
        UnregisterClassW(kSettingsWindowClass, instance);
        if (apartmentInitialized) winrt::uninit_apartment();
        return 0;
    }

    SettingsWindowState state;
    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    UINT dpi = WindowDpi(nullptr);
    HMODULE shcore = GetModuleHandleW(L"shcore.dll");
    if (!shcore) {
        shcore = LoadLibraryExW(L"shcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (shcore) {
        using GetDpiForMonitor_t = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        auto getDpi = reinterpret_cast<GetDpiForMonitor_t>(
            GetProcAddress(shcore, "GetDpiForMonitor"));
        UINT monitorDpiX = dpi;
        UINT monitorDpiY = dpi;
        if (getDpi && SUCCEEDED(getDpi(monitor, 0, &monitorDpiX, &monitorDpiY))) {
            dpi = monitorDpiX;
        }
    }
    int width = ScaleForDpi(700, dpi);
    int height = ScaleForDpi(400, dpi);
    width = std::min(width, (int)(monitorInfo.rcWork.right - monitorInfo.rcWork.left));
    height = std::min(height, (int)(monitorInfo.rcWork.bottom - monitorInfo.rcWork.top));
    int x = monitorInfo.rcWork.left + (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    int y = monitorInfo.rcWork.top + (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    HWND window = CreateWindowExW(WS_EX_APPWINDOW, kSettingsWindowClass,
                                  L"Taskbar AI Quota Bars - Settings",
                                  (WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX) |
                                      WS_CLIPCHILDREN,
                                  x, y, width, height, nullptr, nullptr, instance, &state);
    if (window) {
        FitSettingsWindowToContent(state);
        LayoutSettingsWindow(state);
        g_settingsWindow.store(window);
        if (g_unloading || g_settingsWindowCancelRequested) {
            PostMessageW(window, WM_CLOSE, 0, 0);
        } else {
            ShowWindow(window, SW_SHOW);
            SetForegroundWindow(window);
        }
        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(window, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (IsWindow(window)) DestroyWindow(window);
    }
    SetVisualTestMode(false);
    g_settingsWindow.store(nullptr);
    UnregisterClassW(kAccountEditorClass, instance);
    UnregisterClassW(kSettingsWindowClass, instance);
    if (apartmentInitialized) winrt::uninit_apartment();
    return 0;
}

static void OpenSettingsWindow() {
    if (g_unloading) return;
    std::lock_guard<std::mutex> lock(g_settingsWindowMutex);
    if (HWND window = g_settingsWindow.load(); window && IsWindow(window)) {
        if (UINT message = GetSettingsActivateMessage()) {
            PostMessageW(window, message, 0, 0);
        }
        return;
    }
    if (g_settingsWindowThread) {
        if (WaitForSingleObject(g_settingsWindowThread, 0) != WAIT_OBJECT_0) return;
        CloseHandle(g_settingsWindowThread);
        g_settingsWindowThread = nullptr;
    }
    if (g_unloading) return;
    g_settingsWindowCancelRequested = false;
    g_settingsWindowThread = CreateThread(nullptr, 0, SettingsWindowThreadProc,
                                          nullptr, 0, nullptr);
    if (!g_settingsWindowThread) Wh_Log(L"Could not create settings window thread");
}
