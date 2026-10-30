// Two-way sync between the settings controls and the edited settings.

static int SelectedAccountIndex(const SettingsWindowState& state) {
    if (!state.accountList) return -1;
    return ListView_GetNextItem(state.accountList, -1, LVNI_SELECTED);
}

static uint64_t SelectedAccountIdentity(const SettingsWindowState& state) {
    int index = SelectedAccountIndex(state);
    std::lock_guard<std::mutex> lk(g_settingsMutex);
    return index >= 0 && index < (int)g_settings.accounts.size() ?
               AccountIdentityHash(g_settings.accounts[index]) : 0;
}

static void UpdateAccountButtons(SettingsWindowState& state) {
    int index = SelectedAccountIndex(state);
    bool selected = index >= 0;
    AccountConfig account;
    int accountCount = 0;
    int visibleCount = 0;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        accountCount = (int)g_settings.accounts.size();
        for (const auto& candidate : g_settings.accounts) {
            if (!candidate.hidden) visibleCount++;
        }
        if (selected && index < accountCount) account = g_settings.accounts[index];
        else selected = false;
    }
    bool loginInProgress = g_loginInProgress.load();
    EnableWindow(GetDlgItem(state.hWnd, kAccountEdit), selected && !loginInProgress);
    EnableWindow(GetDlgItem(state.hWnd, kAccountRemove), selected && !loginInProgress);
    EnableWindow(GetDlgItem(state.hWnd, kAccountMoveUp), selected && index > 0);
    EnableWindow(GetDlgItem(state.hWnd, kAccountMoveDown),
                 selected && index + 1 < accountCount);
    HWND visibilityButton = GetDlgItem(state.hWnd, kAccountToggleVisible);
    EnableWindow(visibilityButton, selected && (account.hidden || visibleCount > 1));
    SetWindowTextW(visibilityButton, selected && account.hidden ? L"Show" : L"Hide");

    bool oauth = selected && account.provider != L"antigravity";
    bool hasToken = false;
    if (oauth) {
        StoredToken token;
        hasToken = LoadStoredToken(AccountIdentityHash(account), &token);
    }
    bool selectedSigningIn = selected && loginInProgress &&
                             AccountIdentityHash(account) == g_loginAccountIdentity.load();
    SetWindowTextW(GetDlgItem(state.hWnd, kAccountSignIn),
                   selectedSigningIn ? L"Signing in..." :
                   hasToken ? L"Re-sign" : L"Sign in");
    EnableWindow(GetDlgItem(state.hWnd, kAccountSignIn),
                 oauth && !loginInProgress);
    EnableWindow(GetDlgItem(state.hWnd, kAccountSignOut), oauth && hasToken);
}

static void RefreshAccountList(SettingsWindowState& state) {
    std::vector<AccountConfig> accounts;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        accounts = g_settings.accounts;
    }
    uint64_t selectedIdentity = 0;
    int selected = SelectedAccountIndex(state);
    if (selected >= 0) {
        LVITEMW selectedItem{};
        selectedItem.mask = LVIF_PARAM;
        selectedItem.iItem = selected;
        if (ListView_GetItem(state.accountList, &selectedItem)) {
            selectedIdentity = (uint64_t)selectedItem.lParam;
        }
    }
    ListView_DeleteAllItems(state.accountList);
    int restoredSelection = -1;
    bool loginInProgress = g_loginInProgress.load();
    uint64_t loginIdentity = g_loginAccountIdentity.load();
    for (size_t i = 0; i < accounts.size(); i++) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = (int)i;
        item.pszText = const_cast<PWSTR>(accounts[i].label.c_str());
        item.lParam = (LPARAM)AccountIdentityHash(accounts[i]);
        ListView_InsertItem(state.accountList, &item);
        if ((uint64_t)item.lParam == selectedIdentity) restoredSelection = (int)i;
        ListView_SetItemText(state.accountList, (int)i, 1,
                             const_cast<PWSTR>(ProviderDisplayName(accounts[i].provider)));
        std::wstring bars;
        if (accounts[i].showBars[kFiveHourBar]) bars += L"5h";
        if (accounts[i].showBars[kWeeklyBar]) bars += bars.empty() ? L"Week" : L", Week";
        if (accounts[i].showBars[kFableWeeklyBar]) {
            bars += bars.empty() ? L"Fable" : L", Fable";
        }
        if (accounts[i].showBars[kExtraUsageBar]) {
            PCWSTR name = accounts[i].provider == L"openai" ? L"Credits" : L"Extra";
            if (!bars.empty()) bars += L", ";
            bars += name;
        }
        ListView_SetItemText(state.accountList, (int)i, 2, const_cast<PWSTR>(bars.c_str()));
        std::wstring visible = accounts[i].hidden ? L"No" : L"Yes";
        ListView_SetItemText(state.accountList, (int)i, 3, visible.data());
        std::wstring status;
        if (loginInProgress && AccountIdentityHash(accounts[i]) == loginIdentity) {
            status = L"Signing in...";
        } else if (accounts[i].provider == L"antigravity") {
            status = L"Uses local session";
        } else {
            StoredToken token;
            status = LoadStoredToken(AccountIdentityHash(accounts[i]), &token) ?
                         L"Signed in" : L"Not signed in";
        }
        ListView_SetItemText(state.accountList, (int)i, 4, const_cast<PWSTR>(status.c_str()));
    }
    if (restoredSelection >= 0) {
        ListView_SetItemState(state.accountList, restoredSelection, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    }
    UpdateAccountButtons(state);
}

static void SetControlInt(SettingsWindowState& state, int id, int value) {
    wchar_t text[32];
    swprintf(text, ARRAYSIZE(text), L"%d", value);
    HWND control = GetDlgItem(state.hWnd, id);
    SetWindowTextW(control, text);
    for (const auto& pageRows : state.rows) {
        for (const auto& row : pageRows) {
            if (row.control != control) continue;
            if (row.spin) SendMessageW(row.spin, UDM_SETPOS32, 0, value);
            if (row.slider) {
                int maximum = (int)SendMessageW(row.slider, TBM_GETRANGEMAX, 0, 0);
                SendMessageW(row.slider, TBM_SETPOS, TRUE,
                             std::clamp(value, row.minimum, maximum));
            }
            return;
        }
    }
}

static int GetControlInt(HWND hWnd, int id, int fallback) {
    wchar_t text[64] = {};
    GetWindowTextW(GetDlgItem(hWnd, id), text, ARRAYSIZE(text));
    wchar_t* end = nullptr;
    long value = wcstol(text, &end, 10);
    return end && end != text ? (int)value : fallback;
}

static SettingsRow* FindSettingsRow(SettingsWindowState& state, HWND control) {
    for (auto& pageRows : state.rows) {
        for (auto& row : pageRows) {
            if (row.control == control || row.slider == control || row.spin == control) {
                return &row;
            }
        }
    }
    return nullptr;
}

static void RefreshColorPreviews(SettingsWindowState& state, const Settings& settings) {
    if (SettingsRow* row = FindSettingsRow(
            state, GetDlgItem(state.hWnd, kPaceTickColor))) {
        row->previewColor = settings.paceTickColor;
        if (row->preview) InvalidateRect(row->preview, nullptr, TRUE);
    }
    const int ids[] = {kYellowThreshold, kOrangeThreshold, kRedThreshold};
    const COLORREF normalColors[] = {RGB(0xFD, 0xD8, 0x35), RGB(0xFB, 0x8C, 0x00),
                                     RGB(0xE5, 0x39, 0x35)};
    const COLORREF colorblindColors[] = {RGB(0x56, 0xB4, 0xE9), RGB(0xE6, 0x9F, 0x00),
                                         RGB(0xD5, 0x5E, 0x00)};
    for (int i = 0; i < 3; i++) {
        SettingsRow* row = FindSettingsRow(state, GetDlgItem(state.hWnd, ids[i]));
        if (!row || !row->preview) continue;
        row->previewColor = settings.colorblindMode ? colorblindColors[i] : normalColors[i];
        InvalidateRect(row->preview, nullptr, TRUE);
    }
}

static void EnableSettingsRow(SettingsWindowState& state, int id, bool enabled) {
    HWND control = GetDlgItem(state.hWnd, id);
    for (const auto& pageRows : state.rows) {
        for (const auto& row : pageRows) {
            if (row.control != control) continue;
            if (row.label) EnableWindow(row.label, enabled);
            EnableWindow(row.control, enabled);
            if (row.slider) EnableWindow(row.slider, enabled);
            if (row.spin) EnableWindow(row.spin, enabled);
            return;
        }
    }
}

static void UpdateDependentSettingsControls(SettingsWindowState& state) {
    EnableSettingsRow(state, kMonitorNumber,
                      SendDlgItemMessageW(state.hWnd, kMonitorMode,
                                          CB_GETCURSEL, 0, 0) == 2);
    bool labelsVisible = SendDlgItemMessageW(state.hWnd, kLabelPosition,
                                             CB_GETCURSEL, 0, 0) !=
                         (LRESULT)LabelPosition::Hidden;
    EnableSettingsRow(state, kLabelFontSize, labelsVisible);
    EnableSettingsRow(state, kLabelGap, labelsVisible);
    LRESULT percentTextVisibility = SendDlgItemMessageW(
        state.hWnd, kPercentTextVisibility, CB_GETCURSEL, 0, 0);
    bool barTextVisible =
        SendDlgItemMessageW(state.hWnd, kShowBarLabels, BM_GETCHECK, 0, 0) == BST_CHECKED ||
        percentTextVisibility > (LRESULT)PercentTextVisibility::Never;
    EnableSettingsRow(state, kPercentFontSize, barTextVisible);
    EnableSettingsRow(state, kPercentTextAlignment,
                      percentTextVisibility > (LRESULT)PercentTextVisibility::Never);
    EnableSettingsRow(state, kShowExtraBarAmounts,
                      percentTextVisibility > (LRESULT)PercentTextVisibility::Never);
    bool paceTicksVisible = SendDlgItemMessageW(state.hWnd, kShowPaceTicks,
                                                BM_GETCHECK, 0, 0) == BST_CHECKED;
    EnableSettingsRow(state, kPaceTickStyle, paceTicksVisible);
    EnableSettingsRow(state, kPaceTickColor, paceTicksVisible);
    EnableSettingsRow(state, kPollMinutes,
                      SendDlgItemMessageW(state.hWnd, kPollPreset,
                                          CB_GETCURSEL, 0, 0) == 6);
}

static void PopulateMonitorCombo(SettingsWindowState& state, int selectedMonitorNumber) {
    HWND combo = GetDlgItem(state.hWnd, kMonitorNumber);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    auto displays = FindCurrentProcessTaskbarDisplays();
    int selectedIndex = -1;
    for (size_t i = 0; i < displays.size(); i++) {
        const auto& display = displays[i];
        int width = std::abs(display.rect.right - display.rect.left);
        int height = std::abs(display.rect.bottom - display.rect.top);
        wchar_t text[128];
        swprintf(text, ARRAYSIZE(text), L"Display %d - %dx%d%s", (int)i + 1,
                 width, height, display.primary ? L" (Primary)" : L"");
        int item = (int)SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
        SendMessageW(combo, CB_SETITEMDATA, item, (LPARAM)(i + 1));
        if ((int)i + 1 == selectedMonitorNumber) selectedIndex = item;
    }
    if (selectedIndex < 0) {
        wchar_t text[96];
        swprintf(text, ARRAYSIZE(text), L"Display %d - unavailable", selectedMonitorNumber);
        selectedIndex = (int)SendMessageW(combo, CB_ADDSTRING, 0,
                                          reinterpret_cast<LPARAM>(text));
        SendMessageW(combo, CB_SETITEMDATA, selectedIndex, selectedMonitorNumber);
    }
    SendMessageW(combo, CB_SETCURSEL, selectedIndex, 0);
}

static void RefreshSettingsControls(SettingsWindowState& state) {
    Settings s;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        s = g_settings;
    }
    state.updating = true;
    SendDlgItemMessageW(state.hWnd, kMonitorMode, CB_SETCURSEL,
                        s.taskbarMonitorMode == TaskbarMonitorMode::All ? 1 :
                        s.taskbarMonitorMode == TaskbarMonitorMode::Specific ? 2 : 0, 0);
    PopulateMonitorCombo(state, s.taskbarMonitorNumber);
    SendDlgItemMessageW(state.hWnd, kBarLayout, CB_SETCURSEL,
                        s.barLayout == BarLayout::Vertical ? 1 : 0, 0);
    SendDlgItemMessageW(state.hWnd, kBarMode, CB_SETCURSEL,
                        s.barMode == BarMode::Remaining ? 1 : 0, 0);
    SetControlInt(state, kBarLength, s.barLength);
    SetControlInt(state, kBarThickness, s.barThickness);
    SetControlInt(state, kLabelFontSize, s.labelFontSize);
    SetControlInt(state, kPercentFontSize, s.percentFontSize);
    SetControlInt(state, kAccountMargin, s.accountMargin);
    SetControlInt(state, kLabelGap, s.labelGap);
    SetControlInt(state, kBarGap, s.barGap);
    SetControlInt(state, kRightMargin, s.rightMargin);

    auto setCheck = [&](int id, bool checked) {
        SendDlgItemMessageW(state.hWnd, id, BM_SETCHECK,
                            checked ? BST_CHECKED : BST_UNCHECKED, 0);
    };
    bool visualTestMode = g_visualTestMode.load(std::memory_order_acquire);
    setCheck(kVisualTestModeLayout, visualTestMode);
    setCheck(kVisualTestModeDisplay, visualTestMode);
    SendDlgItemMessageW(state.hWnd, kLabelPosition, CB_SETCURSEL,
                        (int)s.labelPosition, 0);
    setCheck(kShowPaceTicks, s.showPaceTicks);
    SendDlgItemMessageW(state.hWnd, kPaceTickStyle, CB_SETCURSEL,
                        (int)s.paceTickStyle, 0);
    setCheck(kShowBarLabels, s.showBarLabels);
    SendDlgItemMessageW(state.hWnd, kPercentTextVisibility, CB_SETCURSEL,
                        (int)s.percentTextVisibility, 0);
    SendDlgItemMessageW(state.hWnd, kPercentTextAlignment, CB_SETCURSEL,
                        (int)s.percentTextAlignment, 0);
    setCheck(kShowExtraBarAmounts, s.showExtraBarAmounts);
    setCheck(kShowOpenAiExtraLimits, s.showOpenAiExtraLimits);
    setCheck(kColorblindMode, s.colorblindMode);
    setCheck(kShowStaleWarning, s.showStaleWarning);
    SetControlInt(state, kYellowThreshold, s.yellowThreshold);
    SetControlInt(state, kOrangeThreshold, s.orangeThreshold);
    SetControlInt(state, kRedThreshold, s.redThreshold);

    SendDlgItemMessageW(state.hWnd, kClickAction, CB_SETCURSEL,
                        s.clickAction == ClickAction::OpenDashboard ? 1 : 0, 0);
    const int pollPresets[] = {2, 5, 10, 15, 30, 60};
    int pollPresetIndex = 6;
    for (int i = 0; i < (int)ARRAYSIZE(pollPresets); i++) {
        if (s.pollMinutes == pollPresets[i]) {
            pollPresetIndex = i;
            break;
        }
    }
    SendDlgItemMessageW(state.hWnd, kPollPreset, CB_SETCURSEL, pollPresetIndex, 0);
    SetControlInt(state, kPollMinutes, s.pollMinutes);
    setCheck(kEnableNotifications, s.enableNotifications);
    RefreshColorPreviews(state, s);
    state.updating = false;
    UpdateDependentSettingsControls(state);
    RefreshAccountList(state);
}

static void CommitScalarSettings(SettingsWindowState& state, bool refreshControls = true) {
    if (state.updating || g_unloading) return;
    if (!GetDlgItem(state.hWnd, kMonitorMode)) return;
    KillTimer(state.hWnd, kSettingsAutosaveTimer);
    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings s;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        s = g_settings;
    }
    auto getBoundedInt = [&](int id, int fallback) {
        int value = GetControlInt(state.hWnd, id, fallback);
        if (SettingsRow* row = FindSettingsRow(state, GetDlgItem(state.hWnd, id))) {
            value = std::clamp(value, row->minimum, row->maximum);
        }
        return value;
    };
    int selection = (int)SendDlgItemMessageW(state.hWnd, kMonitorMode, CB_GETCURSEL, 0, 0);
    s.taskbarMonitorMode = selection == 1 ? TaskbarMonitorMode::All :
                           selection == 2 ? TaskbarMonitorMode::Specific :
                                            TaskbarMonitorMode::Primary;
    int monitorSelection = (int)SendDlgItemMessageW(state.hWnd, kMonitorNumber,
                                                     CB_GETCURSEL, 0, 0);
    LRESULT monitorNumber = monitorSelection >= 0 ?
        SendDlgItemMessageW(state.hWnd, kMonitorNumber, CB_GETITEMDATA,
                            monitorSelection, 0) : CB_ERR;
    if (monitorNumber != CB_ERR) s.taskbarMonitorNumber = (int)monitorNumber;
    s.barLayout = SendDlgItemMessageW(state.hWnd, kBarLayout, CB_GETCURSEL, 0, 0) == 1 ?
                          BarLayout::Vertical : BarLayout::Stacked;
    s.barMode = SendDlgItemMessageW(state.hWnd, kBarMode, CB_GETCURSEL, 0, 0) == 1 ?
                        BarMode::Remaining : BarMode::Used;
    s.barLength = getBoundedInt(kBarLength, s.barLength);
    s.barThickness = getBoundedInt(kBarThickness, s.barThickness);
    s.labelFontSize = getBoundedInt(kLabelFontSize, s.labelFontSize);
    s.percentFontSize = getBoundedInt(kPercentFontSize, s.percentFontSize);
    s.accountMargin = getBoundedInt(kAccountMargin, s.accountMargin);
    s.labelGap = getBoundedInt(kLabelGap, s.labelGap);
    s.barGap = getBoundedInt(kBarGap, s.barGap);
    s.rightMargin = getBoundedInt(kRightMargin, s.rightMargin);
    auto isChecked = [&](int id) {
        return SendDlgItemMessageW(state.hWnd, id, BM_GETCHECK, 0, 0) == BST_CHECKED;
    };
    int labelPosition = (int)SendDlgItemMessageW(state.hWnd, kLabelPosition,
                                                 CB_GETCURSEL, 0, 0);
    s.labelPosition = labelPosition >= 0 && labelPosition <= (int)LabelPosition::Bottom ?
                          (LabelPosition)labelPosition : LabelPosition::Left;
    s.showPaceTicks = isChecked(kShowPaceTicks);
    int paceTickStyle = (int)SendDlgItemMessageW(state.hWnd, kPaceTickStyle,
                                                 CB_GETCURSEL, 0, 0);
    s.paceTickStyle = paceTickStyle >= 0 && paceTickStyle <= (int)PaceTickStyle::Dot ?
                          (PaceTickStyle)paceTickStyle : PaceTickStyle::Caret;
    if (SettingsRow* row = FindSettingsRow(
            state, GetDlgItem(state.hWnd, kPaceTickColor))) {
        s.paceTickColor = row->previewColor;
    }
    s.showBarLabels = isChecked(kShowBarLabels);
    int percentTextVisibility = (int)SendDlgItemMessageW(
        state.hWnd, kPercentTextVisibility, CB_GETCURSEL, 0, 0);
    s.percentTextVisibility =
        percentTextVisibility >= 0 &&
                percentTextVisibility <= (int)PercentTextVisibility::Always ?
            (PercentTextVisibility)percentTextVisibility : PercentTextVisibility::Hover;
    int percentTextAlignment = (int)SendDlgItemMessageW(
        state.hWnd, kPercentTextAlignment, CB_GETCURSEL, 0, 0);
    s.percentTextAlignment =
        percentTextAlignment >= 0 && percentTextAlignment <= (int)PercentTextAlignment::Right ?
            (PercentTextAlignment)percentTextAlignment : PercentTextAlignment::Adaptive;
    s.showExtraBarAmounts = isChecked(kShowExtraBarAmounts);
    s.showOpenAiExtraLimits = isChecked(kShowOpenAiExtraLimits);
    s.colorblindMode = isChecked(kColorblindMode);
    s.showStaleWarning = isChecked(kShowStaleWarning);
    s.yellowThreshold = getBoundedInt(kYellowThreshold, s.yellowThreshold);
    s.orangeThreshold = getBoundedInt(kOrangeThreshold, s.orangeThreshold);
    s.redThreshold = getBoundedInt(kRedThreshold, s.redThreshold);
    s.clickAction = SendDlgItemMessageW(state.hWnd, kClickAction, CB_GETCURSEL, 0, 0) == 1 ?
                            ClickAction::OpenDashboard : ClickAction::Refresh;
    const int pollPresets[] = {2, 5, 10, 15, 30, 60};
    int pollPresetIndex = (int)SendDlgItemMessageW(state.hWnd, kPollPreset,
                                                   CB_GETCURSEL, 0, 0);
    s.pollMinutes = pollPresetIndex >= 0 && pollPresetIndex < (int)ARRAYSIZE(pollPresets) ?
                        pollPresets[pollPresetIndex] :
                        getBoundedInt(kPollMinutes, s.pollMinutes);
    s.enableNotifications = isChecked(kEnableNotifications);
    SettingsApplyResult applyResult = ApplyOwnedSettings(std::move(s));
    configLock.unlock();
    if (applyResult == SettingsApplyResult::Failed) {
        SettingsMessageBoxW(state.hWnd, L"Could not save settings.", L"Taskbar AI Quota Bars",
                    MB_OK | MB_ICONERROR);
    } else if (applyResult == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    if (refreshControls) RefreshSettingsControls(state);
}
