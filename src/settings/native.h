// Native window helpers shared with the history window: themed message boxes and color
// dialogs, DPI scaling, and dark mode.

struct SettingsMessageBoxContext {
    SettingsMessageBoxContext* previous = nullptr;
    HWND hWnd = nullptr;
    int forcedResult = IDCANCEL;
    bool forced = false;
};

static thread_local SettingsMessageBoxContext* g_settingsMessageBoxContext = nullptr;

static LRESULT CALLBACK SettingsMessageBoxCbtProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE && g_settingsMessageBoxContext &&
        !g_settingsMessageBoxContext->hWnd) {
        HWND hWnd = reinterpret_cast<HWND>(wParam);
        wchar_t className[16] = {};
        if (GetClassNameW(hWnd, className, ARRAYSIZE(className)) &&
            wcscmp(className, L"#32770") == 0) {
            g_settingsMessageBoxContext->hWnd = hWnd;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// Only settings-thread callers participate in owner-close handling.
static int SettingsMessageBoxW(HWND hWnd, LPCWSTR text, LPCWSTR caption, UINT type) {
    SettingsMessageBoxContext context;
    context.previous = g_settingsMessageBoxContext;
    switch (type & MB_TYPEMASK) {
        case MB_OK:
            context.forcedResult = IDOK;
            break;
        case MB_ABORTRETRYIGNORE:
            context.forcedResult = IDABORT;
            break;
        case MB_YESNO:
            context.forcedResult = IDNO;
            break;
        case MB_OKCANCEL:
        case MB_YESNOCANCEL:
        case MB_RETRYCANCEL:
        case MB_CANCELTRYCONTINUE:
            context.forcedResult = IDCANCEL;
            break;
    }
    if (g_unloading || g_settingsWindowCancelRequested || (hWnd && !IsWindow(hWnd))) {
        return context.forcedResult;
    }

    g_settingsMessageBoxContext = &context;
    HHOOK hook = SetWindowsHookExW(WH_CBT, SettingsMessageBoxCbtProc, nullptr,
                                   GetCurrentThreadId());
    if (!hook) {
        Wh_Log(L"Could not install settings MessageBox hook: %lu", GetLastError());
        g_settingsMessageBoxContext = context.previous;
        return context.forcedResult;
    }

    int result = MessageBoxW(hWnd, text, caption, type);
    if (hook) {
        if (!UnhookWindowsHookEx(hook)) {
            Wh_Log(L"Could not remove settings MessageBox hook: %lu", GetLastError());
        }
    }
    g_settingsMessageBoxContext = context.previous;
    return context.forced ? context.forcedResult : result;
}

static UINT_PTR CALLBACK SettingsColorHookProc(HWND hWnd, UINT message,
                                               WPARAM, LPARAM lParam) {
    if (message == WM_INITDIALOG) {
        auto* chooser = reinterpret_cast<CHOOSECOLORW*>(lParam);
        auto* state = reinterpret_cast<SettingsWindowState*>(chooser->lCustData);
        if (state) state->colorDialog = hWnd;
    }
    return FALSE;
}

static int ScaleForDpi(int value, UINT dpi) {
    return MulDiv(value, (int)dpi, 96);
}

static UINT WindowDpi(HWND hWnd) {
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using GetDpiForWindow_t = UINT(WINAPI*)(HWND);
        auto getDpi = reinterpret_cast<GetDpiForWindow_t>(
            GetProcAddress(user32, "GetDpiForWindow"));
        if (getDpi && hWnd) return getDpi(hWnd);
    }
    HDC dc = GetDC(hWnd);
    UINT dpi = dc ? (UINT)GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc) ReleaseDC(hWnd, dc);
    return dpi ? dpi : 96;
}

static bool IsWindowsDarkMode() {
    HIGHCONTRASTW highContrast{sizeof(highContrast)};
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(highContrast),
                              &highContrast, 0) &&
        (highContrast.dwFlags & HCF_HIGHCONTRASTON)) {
        return false;
    }

    DWORD light = 1;
    DWORD size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
                 L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

static void ApplyNativeWindowTheme(HWND hWnd, bool dark) {
    HMODULE uxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (!uxTheme) {
        uxTheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (uxTheme) {
        using SetWindowTheme_t = HRESULT(WINAPI*)(HWND, LPCWSTR, LPCWSTR);
        auto setWindowTheme = reinterpret_cast<SetWindowTheme_t>(
            GetProcAddress(uxTheme, "SetWindowTheme"));
        if (setWindowTheme) {
            struct ThemeContext {
                SetWindowTheme_t setWindowTheme;
                bool dark;
            } context{setWindowTheme, dark};
            EnumChildWindows(hWnd, [](HWND child, LPARAM param) -> BOOL {
                auto* context = reinterpret_cast<ThemeContext*>(param);
                wchar_t className[32] = {};
                GetClassNameW(child, className, ARRAYSIZE(className));
                PCWSTR theme = context->dark ? L"DarkMode_Explorer" : L"Explorer";
                if (context->dark && _wcsicmp(className, L"ComboBox") == 0) {
                    theme = L"DarkMode_CFD";
                } else if (context->dark && _wcsicmp(className, WC_HEADERW) == 0) {
                    theme = L"DarkMode_ItemsView";
                }
                context->setWindowTheme(child, theme, nullptr);
                return TRUE;
            }, reinterpret_cast<LPARAM>(&context));
        }
    }

    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm) {
        dwm = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (dwm) {
        using DwmSetWindowAttribute_t = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
        auto setAttribute = reinterpret_cast<DwmSetWindowAttribute_t>(
            GetProcAddress(dwm, "DwmSetWindowAttribute"));
        if (setAttribute) {
            BOOL enabled = dark;
            setAttribute(hWnd, 20, &enabled, sizeof(enabled));
        }
    }
}
