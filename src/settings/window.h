// Settings window thread and opener: one window at a time, on its own STA thread.

// The app icon on a window the toolkit doesn't host (the sign-in window): drawn at `dpi` in the
// app theme's palette, `icons` kept and destroyed by the caller as UiSetWindowIcons describes.
static void SetAppWindowIcons(HWND window, UINT dpi, std::array<HICON, 2>& icons) {
    try {
        UiGraphics graphics;
        UiCreateGraphics(graphics);
        UiThemeReader themes;
        UiSetWindowIcons(window, graphics, UiPaletteFor(UiReadTheme(themes)), SettingsAppIcon({}), dpi, icons);
    } catch (...) {
        Wh_Log(L"Could not draw the window icons");
    }
}

static UINT GetSettingsActivateMessage() {
    static const UINT message =
        RegisterWindowMessageW(L"Windhawk_ActivateSettings_" WH_MOD_ID);
    return message;
}

// The window keeps its graphics and WinRT objects inside UiHostRun, which returns before the
// apartment is uninitialized.
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

    if (apartmentInitialized && !g_unloading && !g_settingsWindowCancelRequested) {
        SettingsUiClient client;
        UiHostConfig config;
        config.mainClass = kSettingsUiClass;
        config.viewportClass = kSettingsUiViewportClass;
        config.popupClass = kSettingsUiPopupClass;
        config.title = L"Taskbar AI Quota Bars \u2014 Settings";
        config.activateMessage = GetSettingsActivateMessage();
        config.icon = SettingsAppIcon({});
        config.appId = kWindowAppId;
        if (!UiHostRun(config, client)) Wh_Log(L"Could not open the settings window");
    }
    SetVisualTestMode(false);
    g_settingsWindow.store(nullptr);
    if (apartmentInitialized) winrt::uninit_apartment();
    return 0;
}

// Opens the settings window, or activates the one already open.
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
    g_settingsWindowThread = CreateThread(nullptr, 0, SettingsWindowThreadProc, nullptr, 0, nullptr);
    if (!g_settingsWindowThread) Wh_Log(L"Could not create settings window thread");
}
