// Settings window control creation, pages, and layout.

static void RecreateSettingsVisuals(SettingsWindowState& state) {
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
    EnumChildWindows(state.hWnd, [](HWND child, LPARAM param) -> BOOL {
        SendMessageW(child, WM_SETFONT, param, TRUE);
        return TRUE;
    }, reinterpret_cast<LPARAM>(state.font));
    if (state.accountList) {
        COLORREF background = state.dark ? RGB(43, 43, 43) : GetSysColor(COLOR_WINDOW);
        ListView_SetBkColor(state.accountList, background);
        ListView_SetTextBkColor(state.accountList, background);
        ListView_SetTextColor(state.accountList,
                              state.dark ? RGB(235, 235, 235) : GetSysColor(COLOR_WINDOWTEXT));
    }
    ApplyNativeWindowTheme(state.hWnd, state.dark);
    RedrawWindow(state.hWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

static LRESULT CALLBACK AccountListSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                LPARAM lParam, UINT_PTR subclassId,
                                                DWORD_PTR referenceData) {
    auto* state = reinterpret_cast<SettingsWindowState*>(referenceData);
    if (message == WM_NOTIFY && state && state->dark) {
        auto* header = reinterpret_cast<NMHDR*>(lParam);
        if (header->hwndFrom == ListView_GetHeader(hWnd) &&
            header->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
            if (draw->dwDrawStage == CDDS_PREPAINT) {
                RECT client{};
                GetClientRect(header->hwndFrom, &client);
                FillRect(draw->hdc, &client, state->inputBrush);
                return CDRF_NOTIFYITEMDRAW;
            }
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                int savedDc = SaveDC(draw->hdc);
                if (draw->uItemState & (CDIS_HOT | CDIS_SELECTED)) {
                    SetDCBrushColor(draw->hdc, RGB(62, 62, 62));
                    FillRect(draw->hdc, &draw->rc,
                             reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                } else {
                    FillRect(draw->hdc, &draw->rc, state->inputBrush);
                }

                wchar_t text[256] = {};
                HDITEMW item{};
                item.mask = HDI_TEXT | HDI_FORMAT;
                item.pszText = text;
                item.cchTextMax = ARRAYSIZE(text);
                Header_GetItem(header->hwndFrom, (int)draw->dwItemSpec, &item);

                SetBkMode(draw->hdc, TRANSPARENT);
                SetTextColor(draw->hdc, RGB(235, 235, 235));
                if (HFONT font = reinterpret_cast<HFONT>(
                        SendMessageW(header->hwndFrom, WM_GETFONT, 0, 0))) {
                    SelectObject(draw->hdc, font);
                }
                RECT textRect = draw->rc;
                int padding = ScaleForDpi(6, state->dpi);
                textRect.left += padding;
                textRect.right -= padding;
                UINT format = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX;
                format |= (item.fmt & HDF_RIGHT) ? DT_RIGHT :
                          (item.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT;
                DrawTextW(draw->hdc, text, -1, &textRect, format);

                SetDCPenColor(draw->hdc, RGB(75, 75, 75));
                SelectObject(draw->hdc, GetStockObject(DC_PEN));
                MoveToEx(draw->hdc, draw->rc.right - 1, draw->rc.top, nullptr);
                LineTo(draw->hdc, draw->rc.right - 1, draw->rc.bottom);
                MoveToEx(draw->hdc, draw->rc.left, draw->rc.bottom - 1, nullptr);
                LineTo(draw->hdc, draw->rc.right, draw->rc.bottom - 1);
                if (savedDc) RestoreDC(draw->hdc, savedDc);
                return CDRF_SKIPDEFAULT;
            }
        }
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, AccountListSubclassProc, subclassId);
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

static LRESULT CALLBACK SettingsNoEraseSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                    LPARAM lParam, UINT_PTR subclassId,
                                                    DWORD_PTR referenceData) {
    if (message == WM_ERASEBKGND) {
        auto* state = reinterpret_cast<SettingsWindowState*>(referenceData);
        if (state && state->dark) {
            // Preserve the previous frame until the native dark paint pass replaces it.
            return 1;
        }
    } else if (message == WM_NCDESTROY) {
        RemoveWindowSubclass(hWnd, SettingsNoEraseSubclassProc, subclassId);
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

static HWND CreateSettingsControl(SettingsWindowState& state, int page, PCWSTR className,
                                  PCWSTR text, DWORD style, DWORD exStyle, int id) {
    HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | style,
                                   0, 0, 1, 1, state.hWnd,
                                   reinterpret_cast<HMENU>((INT_PTR)id),
                                   GetModuleHandleW(nullptr), nullptr);
    if (control) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        state.pageControls[page].push_back(control);
    }
    return control;
}

static HWND AddSettingsRow(SettingsWindowState& state, int page, PCWSTR labelText,
                           PCWSTR className, DWORD style, DWORD exStyle, int id) {
    HWND label = CreateSettingsControl(state, page, L"STATIC", labelText,
                                       WS_VISIBLE | SS_LEFT | SS_NOTIFY, 0, -1);
    HWND control = CreateSettingsControl(state, page, className, L"",
                                         WS_VISIBLE | WS_TABSTOP | style, exStyle, id);
    state.rows[page].push_back({label, control});
    return control;
}

static HWND AddNumericRow(SettingsWindowState& state, int page, PCWSTR labelText, int id,
                          int minimum, int maximum, bool addSlider = false,
                          int sliderMaximum = -1) {
    HWND edit = AddSettingsRow(state, page, labelText, L"EDIT", ES_NUMBER,
                               WS_EX_CLIENTEDGE, id);
    SettingsRow& row = state.rows[page].back();
    row.minimum = minimum;
    row.maximum = maximum;
    row.spin = CreateSettingsControl(
        state, page, UPDOWN_CLASSW, L"",
        WS_VISIBLE | UDS_ARROWKEYS | UDS_SETBUDDYINT | UDS_NOTHOUSANDS, 0, -1);
    if (row.spin &&
        !SetWindowSubclass(row.spin, SettingsNoEraseSubclassProc, 0,
                           reinterpret_cast<DWORD_PTR>(&state))) {
        Wh_Log(L"Could not subclass settings spin control");
    }
    SendMessageW(row.spin, UDM_SETBUDDY, reinterpret_cast<WPARAM>(edit), 0);
    SendMessageW(row.spin, UDM_SETRANGE32, minimum, maximum);
    if (addSlider) {
        row.slider = CreateSettingsControl(
            state, page, TRACKBAR_CLASSW, L"",
            WS_VISIBLE | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS, 0, -1);
        if (row.slider &&
            !SetWindowSubclass(row.slider, SettingsNoEraseSubclassProc, 0,
                               reinterpret_cast<DWORD_PTR>(&state))) {
            Wh_Log(L"Could not subclass settings slider");
        }
        int trackMaximum = sliderMaximum >= minimum ? sliderMaximum : maximum;
        SendMessageW(row.slider, TBM_SETRANGE, TRUE, MAKELPARAM(minimum, trackMaximum));
        SendMessageW(row.slider, TBM_SETPAGESIZE, 0,
                     std::max(1, (trackMaximum - minimum) / 10));
    }
    return edit;
}

static HWND AddThresholdRow(SettingsWindowState& state, PCWSTR labelText, int id) {
    HWND edit = AddNumericRow(state, 2, labelText, id, 0, 100);
    SettingsRow& row = state.rows[2].back();
    row.preview = CreateSettingsControl(state, 2, L"STATIC", L"",
                                        WS_VISIBLE | SS_OWNERDRAW, 0, -1);
    return edit;
}

static HWND AddSettingsCheck(SettingsWindowState& state, int page, PCWSTR text, int id) {
    HWND control = CreateSettingsControl(state, page, L"BUTTON", text,
                                         WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, id);
    state.rows[page].push_back({nullptr, control});
    return control;
}

static void AddComboItems(HWND combo, std::initializer_list<PCWSTR> items) {
    for (PCWSTR item : items) SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
}

static void AddSettingsToolTip(SettingsWindowState& state, int id, PCWSTR text) {
    if (!state.toolTip || !text) return;
    HWND control = GetDlgItem(state.hWnd, id);
    SettingsRow* row = nullptr;
    for (auto& pageRows : state.rows) {
        auto it = std::find_if(pageRows.begin(), pageRows.end(),
                               [control](const SettingsRow& candidate) {
                                   return candidate.control == control;
                               });
        if (it != pageRows.end()) {
            row = &*it;
            break;
        }
    }
    const HWND targets[] = {row ? row->label : nullptr, control,
                            row ? row->slider : nullptr, row ? row->spin : nullptr};
    for (HWND target : targets) {
        if (!target) continue;
        TOOLINFOW info{};
        info.cbSize = sizeof(info);
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = state.hWnd;
        info.uId = reinterpret_cast<UINT_PTR>(target);
        info.lpszText = const_cast<PWSTR>(text);
        SendMessageW(state.toolTip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
}

static void ShowSettingsPage(SettingsWindowState& state, int page) {
    state.currentPage = std::clamp(page, 0, 3);
    state.rowSubpages[state.currentPage] = 0;
    const PCWSTR resetLabels[] = {L"", L"Reset Layout to defaults...",
                                  L"Reset Display to defaults...",
                                  L"Reset Behavior to defaults..."};
    SetWindowTextW(state.resetPageButton, resetLabels[state.currentPage]);
    for (int i = 0; i < 4; i++) {
        SendMessageW(state.pageButtons[i], BM_SETCHECK,
                     i == state.currentPage ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    for (int i = 0; i < 4; i++) {
        for (HWND control : state.pageControls[i]) {
            SetWindowPos(control, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_NOREDRAW |
                             (i == state.currentPage ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
        }
    }
}

static void FitSettingsWindowToContent(SettingsWindowState& state,
                                       bool reposition = true) {
    if (!state.hWnd || IsIconic(state.hWnd) || IsZoomed(state.hWnd)) return;

    // Grow from live row counts; constrained work areas paginate in the layout pass.
    size_t maximumRows = 0;
    for (int page = 1; page < 4; page++) {
        maximumRows = std::max(maximumRows, state.rows[page].size());
    }
    int desiredClientHeight = ScaleForDpi(62, state.dpi) +
                              (int)maximumRows * ScaleForDpi(37, state.dpi) +
                              ScaleForDpi(28, state.dpi) + ScaleForDpi(12, state.dpi);

    RECT windowRect{};
    GetWindowRect(state.hWnd, &windowRect);
    RECT adjustedRect{0, 0, ScaleForDpi(700, state.dpi), desiredClientHeight};
    DWORD style = (DWORD)GetWindowLongPtrW(state.hWnd, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongPtrW(state.hWnd, GWL_EXSTYLE);
    using AdjustWindowRectExForDpi_t = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjustWindowRectExForDpi = reinterpret_cast<AdjustWindowRectExForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    int desiredWindowHeight;
    if (adjustWindowRectExForDpi &&
        adjustWindowRectExForDpi(&adjustedRect, style, FALSE, exStyle, state.dpi)) {
        desiredWindowHeight = adjustedRect.bottom - adjustedRect.top;
    } else {
        RECT client{};
        GetClientRect(state.hWnd, &client);
        desiredWindowHeight = desiredClientHeight +
                              (windowRect.bottom - windowRect.top) - client.bottom;
    }

    HMONITOR monitor = MonitorFromWindow(state.hWnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    GetMonitorInfoW(monitor, &monitorInfo);
    int workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
    int workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
    int width = std::min((int)(windowRect.right - windowRect.left), workWidth);
    int height = std::min(desiredWindowHeight, workHeight);
    int centerX = windowRect.left + (windowRect.right - windowRect.left) / 2;
    int centerY = windowRect.top + (windowRect.bottom - windowRect.top) / 2;
    int x = std::clamp(centerX - width / 2, (int)monitorInfo.rcWork.left,
                       (int)monitorInfo.rcWork.right - width);
    int y = std::clamp(centerY - height / 2, (int)monitorInfo.rcWork.top,
                       (int)monitorInfo.rcWork.bottom - height);
    state.lockedWindowHeight = height;
    if (!reposition && width == windowRect.right - windowRect.left &&
        height == windowRect.bottom - windowRect.top) {
        return;
    }
    SetWindowPos(state.hWnd, nullptr, x, y, width, height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

static void LayoutSettingsWindow(SettingsWindowState& state) {
    RECT client{};
    GetClientRect(state.hWnd, &client);
    int width = client.right;
    int height = client.bottom;
    int margin = ScaleForDpi(12, state.dpi);
    constexpr UINT positionFlags = SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW;
    auto setVisible = [&](HWND control, bool visible) {
        SetWindowPos(control, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | positionFlags |
                         (visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
    };
    int pageButtonWidth = std::max(1, (width - margin * 2) / 4);
    for (int i = 0; i < 4; i++) {
        int x = margin + i * pageButtonWidth;
        int buttonWidth = i == 3 ? std::max(1, width - margin - x) : pageButtonWidth;
        SetWindowPos(state.pageButtons[i], nullptr, x, margin,
                     buttonWidth, ScaleForDpi(32, state.dpi),
                     positionFlags);
    }

    int viewportTop = ScaleForDpi(52, state.dpi);
    int viewportBottom = height - margin;
    int settingsRowHeight = ScaleForDpi(37, state.dpi);
    int resetButtonHeight = ScaleForDpi(28, state.dpi);

    if (state.accountList) {
        int x = ScaleForDpi(26, state.dpi);
        int y = ScaleForDpi(54, state.dpi);
        int buttonHeight = ScaleForDpi(28, state.dpi);
        int gap = ScaleForDpi(6, state.dpi);
        bool compactButtons = width < ScaleForDpi(600, state.dpi);
        int buttonRows = compactButtons ? 2 : 1;
        int buttonY = viewportBottom - buttonHeight * buttonRows - gap * (buttonRows - 1);
        int listTop = std::max(y, viewportTop);
        int listBottom = std::min(buttonY - ScaleForDpi(10, state.dpi), viewportBottom);
        SetWindowPos(state.accountList, nullptr, x, listTop, width - x * 2,
                     std::max(1, listBottom - listTop), positionFlags);
        setVisible(state.accountList,
                   state.currentPage == 0 &&
                       listBottom - listTop >= ScaleForDpi(30, state.dpi));
        const int columnWidths[] = {90, 150, 120, 70, 120};
        int desiredColumnsWidth = ScaleForDpi(550, state.dpi);
        int availableColumnsWidth = width - x * 2 - ScaleForDpi(4, state.dpi);
        double columnScale = desiredColumnsWidth > availableColumnsWidth ?
                                 (double)availableColumnsWidth / desiredColumnsWidth : 1.0;
        for (int i = 0; i < 5; i++) {
            ListView_SetColumnWidth(state.accountList, i,
                                    std::max(ScaleForDpi(48, state.dpi),
                                             (int)(ScaleForDpi(columnWidths[i], state.dpi) *
                                                   columnScale)));
        }
        const int ids[] = {kAccountAdd, kAccountEdit, kAccountRemove,
                           kAccountMoveUp, kAccountMoveDown, kAccountToggleVisible,
                           kAccountSignIn, kAccountSignOut};
        const int widths[] = {60, 60, 70, 42, 50, 72, 68, 68};
        int availableButtonsWidth = width - x * 2 - gap * 7;
        int desiredButtonsWidth = ScaleForDpi(490, state.dpi);
        double buttonScale = desiredButtonsWidth > availableButtonsWidth ?
                                 (double)availableButtonsWidth / desiredButtonsWidth : 1.0;
        int normalButtonX = x;
        for (int i = 0; i < 8; i++) {
            int buttonRow = compactButtons ? i / 4 : 0;
            int buttonColumn = compactButtons ? i % 4 : i;
            int buttonWidth = compactButtons ?
                std::max(1, (width - x * 2 - gap * 3) / 4) :
                std::max(ScaleForDpi(36, state.dpi),
                         (int)(ScaleForDpi(widths[i], state.dpi) * buttonScale));
            int buttonX = compactButtons ? x + buttonColumn * (buttonWidth + gap) :
                                           normalButtonX;
            HWND button = GetDlgItem(state.hWnd, ids[i]);
            int rowY = buttonY + buttonRow * (buttonHeight + gap);
            SetWindowPos(button, nullptr, buttonX, rowY,
                         buttonWidth, buttonHeight, positionFlags);
            setVisible(button, state.currentPage == 0 && rowY >= viewportTop &&
                                   rowY + buttonHeight <= viewportBottom);
            if (!compactButtons) normalButtonX += buttonWidth + gap;
        }
    }

    setVisible(state.resetPageButton, false);
    setVisible(state.previousRowsButton, false);
    setVisible(state.rowsPageLabel, false);
    setVisible(state.nextRowsButton, false);
    for (int page = 1; page < 4; page++) {
        int firstRow = 0;
        int lastRow = (int)state.rows[page].size();
        int subpageCount = 1;
        int rowsPerSubpage = std::max(
            1, (viewportBottom - ScaleForDpi(62, state.dpi) - resetButtonHeight) /
                   settingsRowHeight);
        if (page == state.currentPage) {
            subpageCount = std::max(
                1, ((int)state.rows[page].size() + rowsPerSubpage - 1) / rowsPerSubpage);
            state.rowSubpages[page] = std::clamp(state.rowSubpages[page], 0,
                                                 subpageCount - 1);
            firstRow = state.rowSubpages[page] * rowsPerSubpage;
            lastRow = std::min((int)state.rows[page].size(), firstRow + rowsPerSubpage);
        }
        int y = ScaleForDpi(62, state.dpi);
        int labelX = ScaleForDpi(28, state.dpi);
        int controlX = width < ScaleForDpi(620, state.dpi) ?
                           std::max(labelX + ScaleForDpi(120, state.dpi), width * 44 / 100) :
                           ScaleForDpi(300, state.dpi);
        int controlWidth = std::max(ScaleForDpi(100, state.dpi),
                                    width - controlX - ScaleForDpi(24, state.dpi));
        for (int rowIndex = 0; rowIndex < (int)state.rows[page].size(); rowIndex++) {
            const auto& row = state.rows[page][rowIndex];
            bool rowOnSubpage = page == state.currentPage && rowIndex >= firstRow &&
                                rowIndex < lastRow;
            if (!rowOnSubpage) {
                if (row.label) setVisible(row.label, false);
                setVisible(row.control, false);
                if (row.slider) setVisible(row.slider, false);
                if (row.spin) setVisible(row.spin, false);
                if (row.preview) setVisible(row.preview, false);
                continue;
            }
            bool sliderFits = row.slider && controlWidth >= ScaleForDpi(200, state.dpi);
            bool rowVisible = y >= viewportTop &&
                               y + ScaleForDpi(26, state.dpi) <= viewportBottom;
            if (row.label) {
                int labelRight = controlX - ScaleForDpi(12, state.dpi);
                if (row.preview) labelRight -= ScaleForDpi(28, state.dpi);
                SetWindowPos(row.label, nullptr, labelX, y + ScaleForDpi(4, state.dpi),
                             std::max(1, labelRight - labelX),
                             ScaleForDpi(22, state.dpi), positionFlags);
                if (row.preview) {
                    SetWindowPos(row.preview, nullptr,
                                 controlX - ScaleForDpi(24, state.dpi),
                                 y + ScaleForDpi(4, state.dpi),
                                 ScaleForDpi(18, state.dpi), ScaleForDpi(18, state.dpi),
                                 positionFlags);
                }
                if (row.spin) {
                    int spinWidth = ScaleForDpi(18, state.dpi);
                    int editWidth = ScaleForDpi(72, state.dpi);
                    int editX = controlX;
                    if (sliderFits) {
                        int sliderWidth = std::max(ScaleForDpi(80, state.dpi),
                                                   controlWidth - editWidth - spinWidth -
                                                       ScaleForDpi(12, state.dpi));
                        SetWindowPos(row.slider, nullptr, controlX, y, sliderWidth,
                                     ScaleForDpi(28, state.dpi), positionFlags);
                        editX += sliderWidth + ScaleForDpi(8, state.dpi);
                    }
                    SetWindowPos(row.control, nullptr, editX, y, editWidth,
                                 ScaleForDpi(26, state.dpi), positionFlags);
                    SetWindowPos(row.spin, nullptr, editX + editWidth, y, spinWidth,
                                 ScaleForDpi(26, state.dpi), positionFlags);
                } else {
                    wchar_t className[32] = {};
                    GetClassNameW(row.control, className, ARRAYSIZE(className));
                    int controlHeight = _wcsicmp(className, L"ComboBox") == 0 ?
                                            ScaleForDpi(220, state.dpi) :
                                            ScaleForDpi(26, state.dpi);
                    SetWindowPos(row.control, nullptr, controlX, y, controlWidth, controlHeight,
                                 positionFlags);
                }
                setVisible(row.label, rowVisible);
            } else {
                SetWindowPos(row.control, nullptr, labelX, y, width - labelX * 2,
                             ScaleForDpi(26, state.dpi), positionFlags);
            }
            setVisible(row.control, rowVisible);
            if (row.slider) setVisible(row.slider, rowVisible && sliderFits);
            if (row.spin) setVisible(row.spin, rowVisible);
            if (row.preview) setVisible(row.preview, rowVisible);
            y += settingsRowHeight;
        }
        if (page == state.currentPage) {
            int buttonWidth = std::max(
                1, std::min(width - labelX * 2, ScaleForDpi(200, state.dpi)));
            int buttonY = y;
            bool buttonVisible = buttonY >= viewportTop &&
                                 buttonY + resetButtonHeight <= viewportBottom;
            SetWindowPos(state.resetPageButton, nullptr,
                         width - labelX - buttonWidth, buttonY,
                         buttonWidth, resetButtonHeight, positionFlags);
            setVisible(state.resetPageButton, buttonVisible);
            if (subpageCount > 1) {
                int gap = ScaleForDpi(6, state.dpi);
                int navigationButtonWidth = ScaleForDpi(72, state.dpi);
                int pageLabelWidth = ScaleForDpi(48, state.dpi);
                int previousX = labelX;
                int labelPositionX = previousX + navigationButtonWidth + gap;
                int nextX = labelPositionX + pageLabelWidth + gap;
                SetWindowPos(state.previousRowsButton, nullptr,
                             previousX, buttonY, navigationButtonWidth,
                             resetButtonHeight, positionFlags);
                SetWindowPos(state.rowsPageLabel, nullptr,
                             labelPositionX, buttonY, pageLabelWidth,
                             resetButtonHeight, positionFlags);
                SetWindowPos(state.nextRowsButton, nullptr,
                             nextX, buttonY, navigationButtonWidth,
                             resetButtonHeight, positionFlags);
                wchar_t pageText[32];
                swprintf(pageText, ARRAYSIZE(pageText), L"%d / %d",
                         state.rowSubpages[page] + 1, subpageCount);
                SetWindowTextW(state.rowsPageLabel, pageText);
                bool previousEnabled = state.rowSubpages[page] > 0;
                bool nextEnabled = state.rowSubpages[page] + 1 < subpageCount;
                if (!previousEnabled && GetFocus() == state.previousRowsButton) {
                    SetFocus(state.nextRowsButton);
                } else if (!nextEnabled && GetFocus() == state.nextRowsButton) {
                    SetFocus(state.previousRowsButton);
                }
                EnableWindow(state.previousRowsButton, previousEnabled);
                EnableWindow(state.nextRowsButton, nextEnabled);
                setVisible(state.previousRowsButton, buttonVisible);
                setVisible(state.rowsPageLabel, buttonVisible);
                setVisible(state.nextRowsButton, buttonVisible);
            }
        }
    }
    for (HWND pageButton : state.pageButtons) {
        SetWindowPos(pageButton, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW);
    }
    RedrawWindow(state.hWnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}
