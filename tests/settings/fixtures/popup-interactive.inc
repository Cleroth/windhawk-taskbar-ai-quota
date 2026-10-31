// Interactive part of the popup probe (included by popup-probe.inc): the steps a user performs
// with real input on their desktop, the hotkeys that end them and the step log.

struct ProbeStep {
    ProbeCapture capture;
    const char* id;
    const char* action;
};

// Interactive steps: the planned design (capture on the popup) first, then capture on the
// dropdown field's surface (the viewport) and no capture, for the click-routing comparison.
static constexpr ProbeStep kSteps[] = {
    {ProbeCapture::Popup, "click-inside-popup", "Click an option inside the popup."},
    {ProbeCapture::Popup, "click-own-window", "Click empty space in the probe window, outside the popup."},
    {ProbeCapture::Popup, "click-own-caption", "Click the probe window's title bar."},
    {ProbeCapture::Popup, "click-other-app", "Click another app's window (for example the console)."},
    {ProbeCapture::Popup, "right-click-other-app", "Right-click another app's window."},
    {ProbeCapture::Popup, "click-taskbar", "Click an empty area of the taskbar."},
    {ProbeCapture::Popup, "alt-tab", "Press Alt+Tab and switch to another window."},
    {ProbeCapture::Popup, "win-key", "Press the Windows key, then Esc to close Start."},
    {ProbeCapture::Popup, "win-down",
     "Press Win+Down to minimize the probe window, then restore it from its taskbar button."},
    {ProbeCapture::Popup, "wheel",
     "Scroll the mouse wheel over the popup, then over the probe window outside it, then over "
     "another app's window."},
    {ProbeCapture::Popup, "keys", "Press Down, then Esc."},
    {ProbeCapture::Popup, "alt-space", "Press Alt+Space (window menu), then Esc."},
    {ProbeCapture::Popup, "reactivate",
     "Press Alt+Tab to another window, then press F10 (asks the probe window to come back, like the "
     "taskbar menu's Settings item while Settings is open)."},
    {ProbeCapture::Owner, "click-inside-popup", "Click an option inside the popup."},
    {ProbeCapture::Owner, "click-own-window", "Click empty space in the probe window, outside the popup."},
    {ProbeCapture::Owner, "click-other-app", "Click another app's window (for example the console)."},
    {ProbeCapture::Owner, "click-taskbar", "Click an empty area of the taskbar."},
    {ProbeCapture::None, "click-inside-popup", "Click an option inside the popup."},
    {ProbeCapture::None, "click-own-window", "Click empty space in the probe window, outside the popup."},
    {ProbeCapture::None, "click-other-app", "Click another app's window (for example the console)."},
    {ProbeCapture::None, "click-taskbar", "Click an empty area of the taskbar."},
    {ProbeCapture::None, "click-text-box", "Click the text box under \"Probe\" in the left column."},
};
static constexpr size_t kStepCount = std::size(kSteps);

static void Log(const std::string& text) {
    std::printf("%s", text.c_str());
    std::fflush(stdout);
    if (g_probe.log) {
        std::fputs(text.c_str(), g_probe.log);
        std::fflush(g_probe.log);
    }
}

static void ShowStep() {
    const ProbeStep& step = kSteps[g_probe.step];
    g_probe.recording = false;
    g_probe.signals.clear();
    g_probe.opened = 0;
    std::printf("\nStep %u of %u (capture on %s): %s\n  Click \"Open dropdown\" in the probe "
                "window, do this, then press %s (%s skips).\n",
                unsigned(g_probe.step + 1), unsigned(kStepCount), CaptureName(step.capture),
                step.action, g_probe.keys[kHotkeyDone].c_str(), g_probe.keys[kHotkeySkip].c_str());
    std::fflush(stdout);
    // The host rebuilds (new instructions) when it finishes dispatching a message.
    UiHostRequestRebuild(*g_probe.host, false);
    PostMessageW(g_probe.main, kProbeRefresh, 0, 0);
}

static void FinishInteractive(const char* how) {
    Log(std::string("\n") + how + "\n");
    if (g_probe.log) {
        std::fclose(g_probe.log);
        g_probe.log = nullptr;
    }
}

static void EndStep(bool skipped) {
    const ProbeStep& step = kSteps[g_probe.step];
    g_probe.recording = false;
    ProbeSnapshot end = Snap();
    char header[160];
    std::snprintf(header, sizeof(header), "\n[step %u/%u] capture=%s %s: %s\n",
                  unsigned(g_probe.step + 1), unsigned(kStepCount), CaptureName(step.capture),
                  step.id, skipped ? "SKIPPED" : "done");
    std::string block = header;
    block += std::string("action: ") + step.action + "\n";
    block += "popup opened " + std::to_string(g_probe.opened) + " time(s)\n";
    for (const ProbeSignal& signal : g_probe.signals) {
        char time[24];
        std::snprintf(time, sizeof(time), "  +%.3fs ", signal.time / 1000.0);
        block += time + signal.text + "\n";
    }
    block += "end: " + Describe(end, true) + "\n";
    block += "fired: " + Fired(g_probe.signals, step.capture) + "\n";
    Log(block);
    ClosePopup();
    if (HWND capture = GetCapture(); capture == g_probe.main || capture == g_probe.viewport) {
        ReleaseCapture();
    }
    if (++g_probe.step == kStepCount) {
        FinishInteractive("All steps done. Send the log to the agent.");
        PostMessageW(g_probe.main, WM_CLOSE, 0, 0);
    } else {
        ShowStep();
    }
}

static LRESULT CALLBACK ProbeControlProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message != WM_HOTKEY || !g_probe.main || g_probe.step >= kStepCount) {
        return DefWindowProcW(window, message, wParam, lParam);
    }
    try {
        if (wParam == kHotkeyActivate) {
            ProbeSnapshot before = Snap();
            AddNote("F10 activate request: last_active_popup=" + before.lastActive +
                    " activate_target=" + before.target);
            SendMessageW(g_probe.main, g_probe.activateMessage, 0, 0);
            AddNote("after F10: foreground=" + Role(GetForegroundWindow()) +
                    " active=" + Role(GetActiveWindow()) + " capture=" + Role(GetCapture()));
        } else {
            EndStep(wParam == kHotkeySkip);
        }
    } catch (...) {
        Wh_Log(L"Hotkey handling failed");
    }
    return 0;
}

static void StartInteractive() {
    struct Key {
        int id;
        UINT vk;
        const char* name;
    };
    // Global hotkeys: they reach the probe whichever window is in the foreground.
    for (Key key : {Key{kHotkeyDone, VK_F9, "F9"}, Key{kHotkeySkip, VK_F8, "F8"},
                    Key{kHotkeyActivate, VK_F10, "F10"}}) {
        std::string& chord = g_probe.keys[key.id];
        chord = key.name;
        if (!RegisterHotKey(g_probe.control, key.id, MOD_NOREPEAT, key.vk)) {
            chord = std::string("Ctrl+Alt+") + key.name;
            if (!RegisterHotKey(g_probe.control, key.id, MOD_NOREPEAT | MOD_CONTROL | MOD_ALT, key.vk)) {
                throw std::runtime_error("cannot register hotkey " + chord);
            }
        }
    }
    wchar_t build[32] = {}, version[32] = {};
    DWORD size = sizeof(build);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                 L"CurrentBuild", RRF_RT_REG_SZ, nullptr, build, &size);
    size = sizeof(version);
    RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                 L"DisplayVersion", RRF_RT_REG_SZ, nullptr, version, &size);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char header[640];
    std::snprintf(header, sizeof(header),
                  "Popup capture probe, interactive. %04u-%02u-%02u %02u:%02u. Windows %s build %s, "
                  "window DPI %u.\nStandalone process (not Explorer): taskbar and Alt+Tab activation "
                  "inside Explorer can differ.\nKeys: %s ends a step, %s skips it, %s sends the "
                  "window's activate request (the step that asks for it).\nThe probe popup never "
                  "closes itself; each step lists what the windows received.\n",
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                  Utf8(version).c_str(), Utf8(build).c_str(), g_probe.host->dpi,
                  g_probe.keys[kHotkeyDone].c_str(), g_probe.keys[kHotkeySkip].c_str(),
                  g_probe.keys[kHotkeyActivate].c_str());
    Log(header);
    ShowStep();
}
