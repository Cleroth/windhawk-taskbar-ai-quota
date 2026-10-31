// Native-host probe DLL: the extracted toolkit host with a stub client, run on its own worker
// thread like the settings thread (per-monitor v2, STA, host scope ended before the apartment),
// optionally on a hidden desktop. host-probe.cpp loads it, drives these exports and unloads it.

static constexpr PCWSTR kProbeClass = L"AiQuotaProbeUi";
static constexpr PCWSTR kProbeViewportClass = L"AiQuotaProbeUiView";
static constexpr PCWSTR kProbePopupClass = L"AiQuotaProbeUiPopup";
static constexpr UINT kProbePing = WM_APP + 70;        // Answered with kProbeMagic.
static constexpr UINT kProbeLoseTarget = WM_APP + 71;  // Device loss through the production path.
static constexpr UINT kProbeModal = WM_APP + 72;       // A tracked message box.
static constexpr UINT kProbeCommand = WM_APP + 73;     // wParam: ProbeCommand, lParam: argument.
static constexpr LRESULT kProbeMagic = 0x51A7E;
static constexpr uint32_t kProbeDropdown = 500, kProbeNumberBox = 600;

enum ProbeCounter { kBuilds, kAttaches, kDestroying, kDestroyed, kCancels, kLossResult, kCounterCount };

// Commands run on the window thread inside the client's message handler, like client code.
enum ProbeCommand {
    kOpenPopup,    // The dropdown's popup through the host; 1 if it has a window
    kPopupState,   // ProbeState bits
    kFieldKey,     // WM_KEYDOWN `argument` to the viewport (the field's surface)
    kPopupMouse,   // 0/1: left down/up on the third row; 2/3: down/up outside the popup
    kSendMain,     // SendMessage(main, LOWORD(argument), HIWORD(argument), 0)
    kAction,       // 0: actions received, 1: last kind, 2: last value, 3: last text as a number
    kFocus,        // Focus item `argument` from the keyboard; 1 if its window has keyboard focus
    kEditState,    // ProbeEdit bits of the number box's text box
    kEditWindow,   // Its window handle's low 32 bits
    kEditChar,     // WM_CHAR `argument` to it
    kEditKey,      // WM_KEYDOWN `argument` to it
    kEditText,     // Its text as a number
    kRebuild,      // Requests a rebuild; returns the builds so far
    kScroll,       // Scrolls the content to `argument` DIPs
    kFocusedItem,  // The host's focused item id
    kDropFocus,    // Keyboard focus to nothing, as when another app activates
};
enum ProbeState { kWindow = 1, kVisible = 2, kCaptured = 4, kNoActivate = 8, kOwned = 16,
                  kNotActive = 32, kOpen = 64, kAnyCapture = 128 };
enum ProbeEdit { kEditExists = 1, kEditFocused = 2, kEditEnabled = 4 };

static std::atomic<int> g_counters[kCounterCount];
static std::atomic<HWND> g_probeWindow{nullptr};
static std::atomic<HWND> g_probeModal{nullptr};
static std::atomic<bool> g_probeUnloading{false};
static bool g_probeFailAttach = false;
static HDESK g_probeDesktop = nullptr;
static HANDLE g_probeThread = nullptr;

static LRESULT CALLBACK ProbeModalHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE && !g_probeModal.load()) {
        wchar_t name[16] = {};
        if (GetClassNameW(HWND(wParam), name, 16) && wcscmp(name, L"#32770") == 0) {
            g_probeModal = HWND(wParam);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// A navigation item; a dropdown and a number box; cards long enough to scroll. Choices, typing,
// reverts and focus loss behave like a settings client's drafts: they change what the next
// build shows, then request a rebuild.
struct ProbeClient final : UiHostClient {
    int choice = 0;
    std::wstring boxText = L"10";
    int actions = 0;
    UiAction last;

    void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) override {
        g_counters[kBuilds]++;
        UiItem nav;
        nav.id = 1;
        nav.kind = UiItemKind::Nav;
        nav.rect = {8, 6, 192, 44};
        nav.text = L"Probe";
        nav.icon = L"\uE946";
        nav.selected = true;
        nav.action = {UiActionKind::Navigate, 0};
        frame.main.push_back(nav);
        frame.viewport = {200, 40, std::max(client.width, 200.0f), std::max(client.height, 40.0f)};
        UiAddDropdown(frame.content, kProbeDropdown, {20, 0, 280, 32},
                      {{L"First", 0}, {L"Second", 1}, {L"Third", 2}}, true,
                      {UiActionKind::Command, choice, {}, 7});
        UiAddNumberBox(frame.content, kProbeNumberBox, {20, 44, 20 + 96 + 10 + 40, 76}, boxText,
                       L"days", true, {UiActionKind::Command, 0, {}, 8});
        uint32_t id = 10;
        float y = 88;
        for (int i = 0; i < 12; i++) {
            UiCardSpec spec;
            spec.glyph = L"\uE774";
            spec.title = L"Card " + std::to_wstring(i);
            spec.description = {{L"Description"}};
            spec.trailingGlyph = L"\uE8A7";
            spec.action = {UiActionKind::OpenUrl, 0, L"https://example.invalid"};
            y = UiAddCard(*host.graphics, frame.content, id, 20, y, 360, spec).card.bottom + 3;
        }
        frame.contentHeight = y + 28;
    }
    void Invoke(UiHost& host, UiAction action) override {
        actions++;
        switch (action.kind) {
            case UiActionKind::Command: choice = action.value; break;
            case UiActionKind::TextChange: boxText = action.text; break;
            case UiActionKind::TextRevert: boxText = L"10"; break;
            default: break;
        }
        last = std::move(action);
        UiHostRequestRebuild(host, false);
    }
    int Command(UiHost& host, int command, int argument) {
        UiPopup& popup = host.popup;
        UiTextBox* box = UiFindTextBox(host.boxes, kProbeNumberBox);
        HWND edit = box ? box->window : nullptr;
        switch (command) {
            case kOpenPopup:
                UiHostOpenPopup(host, kProbeDropdown);
                return popup.window != nullptr;
            case kPopupState: {
                int state = GetCapture() ? kAnyCapture : 0;
                if (!popup.window) return state;
                state |= kWindow;
                if (IsWindowVisible(popup.window)) state |= kVisible;
                if (GetCapture() == popup.window) state |= kCaptured;
                if (GetWindowLongPtrW(popup.window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) state |= kNoActivate;
                if (GetWindow(popup.window, GW_OWNER) == host.main) state |= kOwned;
                if (GetActiveWindow() != popup.window && GetFocus() != popup.window) state |= kNotActive;
                if (popup.open) state |= kOpen;
                return state;
            }
            case kFieldKey:
                SendMessageW(host.viewport, WM_KEYDOWN, WPARAM(argument), 0);
                return 0;
            case kPopupMouse: {
                if (!popup.window || popup.rows.size() < 3) return 0;
                float scale = float(popup.dpi) / 96;
                const D2D1_RECT_F& row = popup.rows[2].rect;
                POINT at = argument < 2 ?
                    POINT{LONG((row.left + row.right) / 2 * scale), LONG((row.top + row.bottom) / 2 * scale)} :
                    POINT{-40, -40};
                SendMessageW(popup.window, argument % 2 ? WM_LBUTTONUP : WM_LBUTTONDOWN,
                             argument % 2 ? 0 : MK_LBUTTON, MAKELPARAM(at.x, at.y));
                return 1;
            }
            case kSendMain:
                SendMessageW(host.main, LOWORD(argument), HIWORD(argument), 0);
                return 0;
            case kAction:
                return argument == 0 ? actions : argument == 1 ? int(last.kind) :
                       argument == 2 ? last.value : _wtoi(last.text.c_str());
            case kFocus:
                UiHostSetFocus(host, {uint32_t(argument), -1}, true);
                return GetFocus() == UiHostFocusWindow(host, {uint32_t(argument), -1});
            case kEditState:
                return !edit ? 0 : kEditExists | (GetFocus() == edit ? kEditFocused : 0) |
                                   (IsWindowEnabled(edit) ? kEditEnabled : 0);
            case kEditWindow:
                return int(reinterpret_cast<intptr_t>(edit));
            case kEditChar:
                if (edit) SendMessageW(edit, WM_CHAR, WPARAM(argument), 0);
                return 0;
            case kEditKey:
                if (edit) SendMessageW(edit, WM_KEYDOWN, WPARAM(argument), 0);
                return 0;
            case kEditText:
                return edit ? _wtoi(UiTextBoxText(edit).c_str()) : -1;
            case kRebuild:
                UiHostRequestRebuild(host, false);
                return g_counters[kBuilds];
            case kScroll:
                UiHostScrollTo(host, float(argument));
                return 0;
            case kFocusedItem:
                return int(host.focus.id);
            case kDropFocus:
                SetFocus(nullptr);
                return 0;
        }
        return -1;
    }
    bool Message(UiHost& host, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) override {
        switch (message) {
            case kProbePing:
                result = kProbeMagic;
                return true;
            case kProbeCommand:
                result = Command(host, int(wParam), int(lParam));
                return true;
            case kProbeLoseTarget: {
                UpdateWindow(host.viewport);
                int before = host.viewSurface.created;
                bool had = host.viewSurface.target != nullptr;
                UiSurfaceDrawResult(host.viewSurface, D2DERR_RECREATE_TARGET);
                bool discarded = !host.viewSurface.target && host.viewSurface.cache.layouts.empty();
                UpdateWindow(host.viewport);
                result = had && discarded && host.viewSurface.target &&
                         host.viewSurface.created == before + 1;
                g_counters[kLossResult] = int(result) + 1;
                return true;
            }
            case kProbeModal: {
                HHOOK hook = SetWindowsHookExW(WH_CBT, ProbeModalHook, nullptr, GetCurrentThreadId());
                MessageBoxW(host.main, L"Native-host probe: closed by the probe.", L"Probe",
                            MB_OKCANCEL);
                if (hook) UnhookWindowsHookEx(hook);
                g_probeModal = nullptr;
                result = 1;
                return true;
            }
        }
        return false;
    }
    bool Attach(UiHost& host) override {
        g_counters[kAttaches]++;
        if (g_probeFailAttach) return false;
        g_probeWindow = host.main;
        return true;
    }
    bool CancelModal() override {
        HWND modal = g_probeModal.load();
        if (!modal || !IsWindow(modal)) return false;
        g_counters[kCancels]++;
        PostMessageW(modal, WM_COMMAND, IDCANCEL, 0);
        return true;
    }
    bool Unloading() override { return g_probeUnloading; }
    bool PrepareClose(UiHost&) override { return true; }
    void Destroying() override { g_counters[kDestroying]++; }
    void Destroyed() override {
        g_counters[kDestroyed]++;
        g_probeWindow = nullptr;
    }
};

static DWORD WINAPI ProbeThread(LPVOID) {
    // A thread joins another desktop only before it owns windows, hooks or a COM apartment.
    if (g_probeDesktop && !SetThreadDesktop(g_probeDesktop)) return 2;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    bool created = false;
    {
        ProbeClient client;
        UiHostConfig config;
        config.mainClass = kProbeClass;
        config.viewportClass = kProbeViewportClass;
        config.popupClass = kProbePopupClass;
        config.title = L"Native-host probe";
        config.size = {640, 480};
        config.activate = false;
        config.position = POINT{-12000, -12000};
        created = UiHostRun(config, client);
    }
    winrt::uninit_apartment();
    return created ? 0 : 1;
}

extern "C" __declspec(dllexport) BOOL ProbeOpen(BOOL failAttach, HDESK desktop) {
    if (g_probeThread) return FALSE;
    for (auto& counter : g_counters) counter = 0;
    g_probeFailAttach = failAttach;
    g_probeDesktop = desktop;
    g_probeUnloading = false;
    g_probeThread = CreateThread(nullptr, 0, ProbeThread, nullptr, 0, nullptr);
    return g_probeThread != nullptr;
}

extern "C" __declspec(dllexport) HWND ProbeWindow() { return g_probeWindow; }
extern "C" __declspec(dllexport) HWND ProbeModalWindow() { return g_probeModal; }
extern "C" __declspec(dllexport) int ProbeCount(int counter) { return g_counters[counter]; }
extern "C" __declspec(dllexport) PCWSTR ProbeClassName() { return kProbeClass; }

// Unload's close: set the cancellation flag first, then close, like Wh_ModUninit.
extern "C" __declspec(dllexport) void ProbeUnload() {
    g_probeUnloading = true;
    if (HWND window = g_probeWindow.load()) PostMessageW(window, WM_CLOSE, 0, 0);
}

// Joins the worker: its exit code (0 = the window was created), or -1 on timeout.
extern "C" __declspec(dllexport) int ProbeJoin(DWORD timeout) {
    if (!g_probeThread) return -2;
    if (WaitForSingleObject(g_probeThread, timeout) != WAIT_OBJECT_0) return -1;
    DWORD code = 0;
    GetExitCodeThread(g_probeThread, &code);
    CloseHandle(g_probeThread);
    g_probeThread = nullptr;
    return int(code);
}
