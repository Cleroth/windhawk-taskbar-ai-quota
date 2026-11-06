// Native window helpers: the settings window's tracked message box and task dialog, and the DPI
// helpers the sign-in, history and custom-drawn windows share.

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

// A task dialog tracked like the message boxes: owner-close ends it as cancelled (IDCANCEL), as
// do Esc and its close button. TaskDialogIndirect exists only in comctl32 v6, so it's looked up
// in the comctl32 the thread's activation context maps (Explorer's is v6): a direct import would
// keep the whole mod from loading against v5. Returns the button chosen, or 0 when no task dialog
// can be shown.
static int SettingsTaskDialog(TASKDIALOGCONFIG config, BOOL* verification) {
    using TaskDialogIndirect_t = HRESULT(WINAPI*)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
    HMODULE comctl32 = GetModuleHandleW(L"comctl32.dll");
    auto taskDialog = reinterpret_cast<TaskDialogIndirect_t>(
        comctl32 ? GetProcAddress(comctl32, "TaskDialogIndirect") : nullptr);
    if (!taskDialog) return 0;
    if (g_unloading || g_settingsWindowCancelRequested ||
        (config.hwndParent && !IsWindow(config.hwndParent))) {
        return IDCANCEL;
    }
    SettingsMessageBoxContext context;
    context.previous = g_settingsMessageBoxContext;
    config.dwFlags |= TDF_ALLOW_DIALOG_CANCELLATION;
    config.pfCallback = [](HWND hWnd, UINT notification, WPARAM, LPARAM, LONG_PTR data) -> HRESULT {
        if (notification == TDN_CREATED) reinterpret_cast<SettingsMessageBoxContext*>(data)->hWnd = hWnd;
        return S_OK;
    };
    config.lpCallbackData = reinterpret_cast<LONG_PTR>(&context);
    g_settingsMessageBoxContext = &context;
    int button = IDCANCEL;
    HRESULT result = taskDialog(&config, &button, nullptr, verification);
    g_settingsMessageBoxContext = context.previous;
    if (FAILED(result)) {
        Wh_Log(L"Could not show the settings task dialog: 0x%08X", (unsigned)result);
        return 0;
    }
    return context.forced ? context.forcedResult : button;
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
