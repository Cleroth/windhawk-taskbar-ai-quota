// The usage history window's thread and its open and close (one history thread at a time).

// The window (history/ui-window.h). Its graphics and WinRT objects live inside UiHostRun, which
// returns before the apartment is uninitialized.
static DWORD WINAPI HistoryUiThreadProc(LPVOID) {
    using SetThreadDpi_t = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto setDpi = reinterpret_cast<SetThreadDpi_t>(GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        apartmentInitialized = true;
    } catch (...) {}
    if (apartmentInitialized && !g_unloading && !g_historyWindowCancelRequested) {
        try {
            HistoryUiClient client;
            UiHostConfig config;
            config.mainClass = kHistoryUiClass;
            config.viewportClass = kHistoryUiViewportClass;
            config.popupClass = kHistoryUiPopupClass;
            config.tooltipClass = kHistoryUiTooltipClass;
            config.title = L"Taskbar AI Quota Bars \u2014 Usage history";
            config.activateMessage = kHistoryActivateMessage;
            config.size = {1100, 830};
            config.minSize = {800, 590};
            config.icon = SettingsAppIcon({});
            config.appId = kWindowAppId;
            if (!UiHostRun(config, client)) Wh_Log(L"Could not open the usage history window");
        } catch (...) {
            Wh_Log(L"History worker failed");
        }
    }
    g_historyDialog.store(nullptr);
    g_historyWindow.store(nullptr);
    if (apartmentInitialized) winrt::uninit_apartment();
    return 0;
}

// Opens the window, or activates the open one, selecting the account with `historyId` if it is
// listed; 0 keeps the current selection.
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
    g_historyWindowThread = CreateThread(nullptr, 0, HistoryUiThreadProc, nullptr, 0, nullptr);
    if (!g_historyWindowThread) Wh_Log(L"Could not create history window thread: %lu", GetLastError());
}

// Lifecycle API: call from the unload thread, after g_unloading=true. Never from the window's
// thread and never while holding settings/history/auth locks.
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
