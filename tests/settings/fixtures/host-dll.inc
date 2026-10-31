// Native-host probe DLL: the extracted toolkit host with a stub client, run on its own worker
// thread like the settings thread (per-monitor v2, STA, host scope ended before the apartment).
// host-probe.cpp loads it, drives these exports and unloads it.

static constexpr PCWSTR kProbeClass = L"AiQuotaProbeUi";
static constexpr PCWSTR kProbeViewportClass = L"AiQuotaProbeUiView";
static constexpr UINT kProbePing = WM_APP + 70;        // Answered with kProbeMagic.
static constexpr UINT kProbeLoseTarget = WM_APP + 71;  // Device loss through the production path.
static constexpr UINT kProbeModal = WM_APP + 72;       // A tracked message box.
static constexpr LRESULT kProbeMagic = 0x51A7E;

enum ProbeCounter { kBuilds, kAttaches, kDestroying, kDestroyed, kCancels, kLossResult, kCounterCount };

static std::atomic<int> g_counters[kCounterCount];
static std::atomic<HWND> g_probeWindow{nullptr};
static std::atomic<HWND> g_probeModal{nullptr};
static std::atomic<bool> g_probeUnloading{false};
static bool g_probeFailAttach = false;
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

// Content long enough to scroll; a navigation item; cards with link actions.
struct ProbeClient final : UiHostClient {
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
        uint32_t id = 10;
        float y = 0;
        for (int i = 0; i < 12; i++) {
            UiCardSpec spec;
            spec.glyph = L"\uE774";
            spec.title = L"Card " + std::to_wstring(i);
            spec.description = {{L"Description"}};
            spec.trailingGlyph = L"\uE8A7";
            y = UiAddCard(*host.graphics, frame.content, id, 20, y, 360, spec).card.bottom + 3;
        }
        frame.contentHeight = y + 28;
    }
    void Invoke(UiHost&, UiAction) override {}
    bool Message(UiHost& host, UINT message, WPARAM, LPARAM, LRESULT& result) override {
        switch (message) {
            case kProbePing:
                result = kProbeMagic;
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
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    bool created = false;
    {
        ProbeClient client;
        UiHostConfig config;
        config.mainClass = kProbeClass;
        config.viewportClass = kProbeViewportClass;
        config.title = L"Native-host probe";
        config.size = {640, 480};
        config.activate = false;
        config.position = POINT{-12000, -12000};
        created = UiHostRun(config, client);
    }
    winrt::uninit_apartment();
    return created ? 0 : 1;
}

extern "C" __declspec(dllexport) BOOL ProbeOpen(BOOL failAttach) {
    if (g_probeThread) return FALSE;
    for (auto& counter : g_counters) counter = 0;
    g_probeFailAttach = failAttach;
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
