// The sign-in window's controls (the window: auth/login.h): their ids, creation, and their layout
// (auth/login-layout.h) measured in the system message font at the window's DPI. Window and GDI
// calls only, no allocation.

enum {
    kLoginInstructions = 100,
    kLoginCode,
    kLoginCopyLink,
    kLoginOpenLink,
    kLoginStatus,
    kLoginAlternate,
};

// Creates the controls `text` describes in `window`, in tab order; the status line shows `status`.
static void CreateLoginControls(HWND window, const LoginWindowText& text, PCWSTR status) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    auto create = [&](DWORD exStyle, PCWSTR cls, PCWSTR caption, DWORD style, int id) {
        CreateWindowExW(exStyle, cls, caption, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, window,
                        reinterpret_cast<HMENU>((INT_PTR)id), instance, nullptr);
    };
    const LoginWindowLabels& labels = text.labels;
    create(0, L"STATIC", text.instructions.c_str(), SS_LEFT | SS_NOPREFIX, kLoginInstructions);
    if (text.device) {
        // Read-only but selectable, so the code can also be copied by hand. The one button copies
        // the code and opens the verification page.
        create(WS_EX_CLIENTEDGE, L"EDIT", text.userCode.c_str(),
               WS_TABSTOP | ES_READONLY | ES_CENTER | ES_AUTOHSCROLL, kLoginCode);
        create(0, L"STATIC", status, SS_LEFT | SS_NOPREFIX, kLoginStatus);
        create(0, L"BUTTON", labels.copy.c_str(), WS_TABSTOP | BS_DEFPUSHBUTTON, kLoginCopyLink);
    } else {
        create(0, L"BUTTON", labels.open.c_str(), WS_TABSTOP | BS_PUSHBUTTON, kLoginOpenLink);
        create(0, L"BUTTON", labels.copy.c_str(), WS_TABSTOP | BS_PUSHBUTTON, kLoginCopyLink);
    }
    if (!text.alternate.empty()) {
        create(0, L"BUTTON", text.alternate.c_str(), WS_TABSTOP | BS_PUSHBUTTON, kLoginAlternate);
    }
    if (text.paste) {
        create(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | (text.secret ? ES_PASSWORD : 0),
               kLoginCode);
        create(0, L"BUTTON", labels.signIn.c_str(), WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
    }
    create(0, L"BUTTON", labels.cancel.c_str(), WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
}

// Lays the controls out at `dpi` in the message font for it, which replaces `font` on every
// control (the previous one is kept when it can't be created), and sizes the window to fit,
// keeping its position.
static void LayoutLoginControls(HWND window, const LoginWindowText& text, UINT dpi, HFONT& font) {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    LOGFONTW fontDescription{};
    using SystemParametersInfoForDpi_t = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    auto systemParametersInfoForDpi = reinterpret_cast<SystemParametersInfoForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi"));
    if (systemParametersInfoForDpi &&
        systemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, dpi)) {
        fontDescription = metrics.lfMessageFont;
    } else {
        fontDescription.lfHeight = -MulDiv(9, (int)dpi, 72);
        fontDescription.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(fontDescription.lfFaceName, L"Segoe UI");  // i18n-en
    }
    HFONT created = CreateFontIndirectW(&fontDescription);
    HFONT shown = created ? created : font;

    // Measured as the controls draw: a button's label on one line, a static's text wrapped at
    // words, '&' as itself. Without a DC every text measures 0: the English sizes.
    HDC dc = GetDC(window);
    HGDIOBJ previous = dc && shown ? SelectObject(dc, shown) : nullptr;
    TEXTMETRICW textMetrics{};
    if (dc) GetTextMetricsW(dc, &textMetrics);
    auto width = [&](const std::wstring& label) {
        SIZE size{};
        if (dc) GetTextExtentPoint32W(dc, label.c_str(), (int)label.size(), &size);
        return (int)size.cx;
    };
    auto wrappedHeight = [&](const std::wstring& line, int lineWidth) {
        RECT bounds{0, 0, lineWidth, 0};
        if (dc) {
            DrawTextW(dc, line.c_str(), (int)line.size(), &bounds,
                      DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX);
        }
        return (int)bounds.bottom;
    };
    LoginLayout layout = LoginWindowLayout(text, dpi, textMetrics.tmHeight, width, wrappedHeight);
    if (previous) SelectObject(dc, previous);
    if (dc) ReleaseDC(window, dc);

    const std::pair<int, const RECT*> placed[] = {
        {kLoginInstructions, &layout.instructions}, {kLoginCode, &layout.code}, {kLoginStatus, &layout.status},
        {kLoginOpenLink, &layout.open}, {kLoginCopyLink, &layout.copy}, {kLoginAlternate, &layout.alternate},
        {IDOK, &layout.signIn}, {IDCANCEL, &layout.cancel}};
    for (const auto& [id, r] : placed) {
        if (HWND child = GetDlgItem(window, id)) {
            if (created) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(created), TRUE);
            SetWindowPos(child, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    if (created) {
        if (font) DeleteObject(font);
        font = created;
    }
    RECT frame{0, 0, layout.client.cx, layout.client.cy};
    DWORD style = (DWORD)GetWindowLongPtrW(window, GWL_STYLE);
    DWORD exStyle = (DWORD)GetWindowLongPtrW(window, GWL_EXSTYLE);
    using AdjustWindowRectExForDpi_t = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjustWindowRectExForDpi = reinterpret_cast<AdjustWindowRectExForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (!adjustWindowRectExForDpi || !adjustWindowRectExForDpi(&frame, style, FALSE, exStyle, dpi)) {
        AdjustWindowRectEx(&frame, style, FALSE, exStyle);
    }
    SetWindowPos(window, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
