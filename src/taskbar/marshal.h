// Taskbar window discovery, UI instance lookup, and marshaling onto taskbar UI threads.

static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param, DWORD timeoutMs = 2000);
static std::vector<HWND> FindCurrentProcessTaskbarWnds();
static QuotaUiInstance* FindUiState(HWND hWnd);

static void PostUiUpdate() {
    // Never resolve XAML refs on the fetch thread; marshal first.
    if (g_unloading || !g_uiInjected.load(std::memory_order_acquire)) return;

    for (HWND hWnd : FindCurrentProcessTaskbarWnds()) {
        RunFromWindowThread(hWnd, [](void* param) -> bool {
            HWND hWnd = static_cast<HWND>(param);
            auto* state = FindUiState(hWnd);
            if (!g_unloading && state && state->quotaGrid) UpdateQuotaUi(*state);
            return true;
        }, hWnd);
    }
}

static bool RunFromWindowThread(HWND hWnd, WindowThreadProc proc, void* param, DWORD timeoutMs) {
    static const UINT kMsg = RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
    struct Payload {
        WindowThreadProc proc;
        void* param;
        std::atomic<bool> ran{false};
        std::atomic<bool> result{false};

        Payload(WindowThreadProc proc, void* param) : proc(proc), param(param) {}
    };
    using PayloadRef = std::shared_ptr<Payload>;
    static std::mutex pendingPayloadsMutex;
    static std::vector<std::pair<UINT_PTR, PayloadRef>> pendingPayloads;
    static std::atomic<UINT_PTR> nextPayloadId{1};

    DWORD tid = GetWindowThreadProcessId(hWnd, nullptr);
    if (!tid) return false;
    if (tid == GetCurrentThreadId()) {
        return proc(param);
    }

    PayloadRef pay = std::make_shared<Payload>(proc, param);
    UINT_PTR payloadId;
    do {
        payloadId = nextPayloadId.fetch_add(1, std::memory_order_relaxed);
    } while (!payloadId);
    {
        std::lock_guard<std::mutex> lk(pendingPayloadsMutex);
        pendingPayloads.push_back({payloadId, pay});
    }

    HHOOK hook = SetWindowsHookExW(
        WH_CALLWNDPROC,
        [](int code, WPARAM w, LPARAM l) CALLBACK -> LRESULT {
            PayloadRef payload;
            if (code == HC_ACTION) {
                auto* cwp = reinterpret_cast<const CWPSTRUCT*>(l);
                static const UINT kM = RegisterWindowMessage(L"Windhawk_RunFromWindowThread_" WH_MOD_ID);
                if (cwp->message == kM) {
                    {
                        std::lock_guard<std::mutex> lk(pendingPayloadsMutex);
                        auto it = std::find_if(
                            pendingPayloads.begin(), pendingPayloads.end(),
                            [id = (UINT_PTR)cwp->wParam](const auto& entry) {
                                return entry.first == id;
                            });
                        if (it != pendingPayloads.end()) {
                            payload = std::move(it->second);
                            pendingPayloads.erase(it);
                        }
                    }
                    // Multiple concurrent marshals install hooks in the same chain. Only the
                    // first hook that claims this ID may execute and release its payload.
                    if (payload) {
                        payload->result.store(payload->proc(payload->param),
                                              std::memory_order_release);
                    }
                }
            }
            LRESULT result = CallNextHookEx(nullptr, code, w, l);
            if (payload) payload->ran.store(true, std::memory_order_release);
            return result;
        }, nullptr, tid);
    if (!hook) {
        std::lock_guard<std::mutex> lk(pendingPayloadsMutex);
        std::erase_if(pendingPayloads,
                      [payloadId](const auto& entry) { return entry.first == payloadId; });
        return false;
    }

    bool sent = true;
    if (timeoutMs == INFINITE) {
        SendMessageW(hWnd, kMsg, payloadId, 0);
    } else {
        DWORD_PTR ignored = 0;
        // Keep delivering while a responsive target pumps sent messages. A timeout can safely
        // cancel only a payload the target hasn't claimed yet.
        sent = SendMessageTimeoutW(hWnd, kMsg, payloadId, 0,
                                   SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_NOTIMEOUTIFNOTHUNG,
                                   timeoutMs, &ignored) != 0;
    }

    bool ran = pay->ran.load(std::memory_order_acquire);
    bool claimed = false;
    if (!ran) {
        std::lock_guard<std::mutex> lk(pendingPayloadsMutex);
        auto it = std::find_if(
            pendingPayloads.begin(), pendingPayloads.end(),
            [payloadId](const auto& entry) { return entry.first == payloadId; });
        if (it != pendingPayloads.end()) {
            pendingPayloads.erase(it);
        } else {
            // The target claimed the payload before the send timed out. It may still be
            // executing mod code, so don't let the caller proceed until it has returned.
            claimed = true;
        }
    }
    if (claimed) {
        if (!sent) Wh_Log(L"Window-thread marshal timed out after dispatch; waiting");
        while (!pay->ran.load(std::memory_order_acquire)) Sleep(1);
        // The hook tail can still be unwinding. Unload joins every finite-marshal worker,
        // then its synchronous per-taskbar cleanup sends serialize behind that tail.
        ran = true;
    }
    bool result = ran && pay->result.load(std::memory_order_acquire);
    UnhookWindowsHookEx(hook);
    return result;
}

static std::vector<TaskbarDisplayInfo> FindCurrentProcessTaskbarDisplays() {
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR hMonitor, HDC, LPRECT, LPARAM lp) CALLBACK -> BOOL {
            reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(hMonitor);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors));

    struct EnumContext {
        DWORD pid;
        std::vector<HMONITOR>* monitors;
        std::vector<TaskbarDisplayInfo>* windows;
    } ctx{GetCurrentProcessId(), &monitors, nullptr};

    std::vector<TaskbarDisplayInfo> windows;
    ctx.windows = &windows;
    EnumWindows([](HWND hWnd, LPARAM lp) CALLBACK -> BOOL {
        auto* ctx = reinterpret_cast<EnumContext*>(lp);
        DWORD pid = 0;
        wchar_t cls[64] = {};
        if (!GetWindowThreadProcessId(hWnd, &pid) || pid != ctx->pid ||
            !GetClassNameW(hWnd, cls, ARRAYSIZE(cls))) {
            return TRUE;
        }

        bool primary = _wcsicmp(cls, L"Shell_TrayWnd") == 0;
        bool secondary = _wcsicmp(cls, L"Shell_SecondaryTrayWnd") == 0;
        if (!primary && !secondary) return TRUE;

        int monitorNumber = 0;
        HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
        if (hMonitor) {
            for (size_t i = 0; i < ctx->monitors->size(); ++i) {
                if ((*ctx->monitors)[i] == hMonitor) {
                    monitorNumber = (int)i + 1;
                    break;
                }
            }
        }
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        RECT rect{};
        if (hMonitor && GetMonitorInfoW(hMonitor, &monitorInfo)) {
            rect = monitorInfo.rcMonitor;
        }
        ctx->windows->push_back({hWnd, primary, monitorNumber, rect});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));

    std::sort(windows.begin(), windows.end(), [](const auto& a, const auto& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.monitorNumber != b.monitorNumber) {
            if (a.monitorNumber == 0) return false;
            if (b.monitorNumber == 0) return true;
            return a.monitorNumber < b.monitorNumber;
        }
        return reinterpret_cast<UINT_PTR>(a.hWnd) < reinterpret_cast<UINT_PTR>(b.hWnd);
    });
    return windows;
}

static std::vector<HWND> FindCurrentProcessTaskbarWnds() {
    TaskbarMonitorMode mode;
    int targetMonitorNumber;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        mode = g_settings.taskbarMonitorMode;
        targetMonitorNumber = g_settings.taskbarMonitorNumber;
    }
    std::vector<TaskbarDisplayInfo> windows = FindCurrentProcessTaskbarDisplays();

    std::vector<HWND> result;
    result.reserve(windows.size());
    if (mode == TaskbarMonitorMode::Specific) {
        if (targetMonitorNumber >= 1 && targetMonitorNumber <= (int)windows.size()) {
            result.push_back(windows[targetMonitorNumber - 1].hWnd);
        }
        return result;
    }

    for (const auto& window : windows) {
        if (mode != TaskbarMonitorMode::Primary || window.primary) {
            result.push_back(window.hWnd);
        }
    }
    return result;
}

// Registry access is locked, while state contents remain owner-UI-thread-only. Raw pointers stay
// valid after lookup because only that owner thread can erase its state.
static QuotaUiInstance* FindUiState(HWND hWnd) {
    std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
    if (!g_uiInstances) return nullptr;
    for (auto& state : *g_uiInstances) {
        if (state->hWnd == hWnd) return state.get();
    }
    return nullptr;
}

static void EraseUiState(HWND hWnd) {
    std::unique_ptr<QuotaUiInstance> removed;
    {
        std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
        if (!g_uiInstances) return;
        auto it = std::find_if(g_uiInstances->begin(), g_uiInstances->end(),
            [hWnd](const auto& state) { return state->hWnd == hWnd; });
        if (it == g_uiInstances->end()) return;
        if ((*it)->ownerThreadId != GetCurrentThreadId()) {
            Wh_Log(L"Refusing to destroy UI state from a non-owner thread");
            return;
        }

        removed = std::move(*it);
        g_uiInstances->erase(it);
        g_uiInjected.store(!g_unloading && !g_uiInstances->empty(),
                           std::memory_order_release);
    }

    if (removed->windowSubclassed && removed->hWnd && IsWindow(removed->hWnd)) {
        WindhawkUtils::RemoveWindowSubclassFromAnyThread(
            removed->hWnd, TaskbarWindowSubclassProc);
        removed->windowSubclassed = false;
    }
    removed.reset();
}

static void ClearQuotaEventState(QuotaUiInstance& state) {
    // Routed event delegates point into this DLL, so revoke before XAML tears down the subtree.
    // Without an account index, closing leaves the tiles' highlight untouched.
    state.flyout.popup.accountIndex = -1;
    CloseAccountFlyout(state);
    auto releaseTimer = [](DispatcherTimer& timer, winrt::event_token& token) {
        try {
            if (timer) timer.Stop();
        } catch (...) {}
        try {
            if (timer && token) timer.Tick(token);
        } catch (...) {}
        timer = nullptr;
        token = {};
    };
    releaseTimer(state.paceTimer, state.paceTimerToken);
    FlyoutUi& flyout = state.flyout;
    releaseTimer(flyout.dwellTimer, flyout.dwellTimerToken);
    releaseTimer(flyout.pollTimer, flyout.pollTimerToken);
    releaseTimer(flyout.countdownTimer, flyout.countdownTimerToken);
    flyout.hoverIndex = -1;
    flyout.dwellIndex = -1;
    flyout.hoverSuppressedIndex = -1;
    flyout.pressIndex = -1;
    flyout.dismissedIndex = -1;

    for (auto& refs : state.accountRefs) {
        if (!refs.menu || !refs.menuOpeningToken) continue;
        try { refs.menu.Opening(refs.menuOpeningToken); } catch (...) {}
        refs.menuOpeningToken = {};
    }

    for (auto& handler : state.pointerHandlers) {
        if (!handler.element) continue;

        try { handler.element.Tapped(handler.tappedToken); } catch (...) {}
        try { handler.element.PointerEntered(handler.pointerEnteredToken); } catch (...) {}
        try { handler.element.PointerPressed(handler.pointerPressedToken); } catch (...) {}
        try { handler.element.PointerExited(handler.pointerExitedToken); } catch (...) {}
        try { handler.element.PointerCanceled(handler.pointerCanceledToken); } catch (...) {}
        try { handler.element.ContextFlyout(nullptr); } catch (...) {}
        try {
            ToolTipService::SetToolTip(handler.element, winrt::Windows::Foundation::IInspectable{nullptr});
        } catch (...) {}
    }
    state.pointerHandlers.clear();

    for (auto& handler : state.menuItemClickHandlers) {
        if (!handler.item) continue;
        try { handler.item.Click(handler.token); } catch (...) {}
    }
    state.menuItemClickHandlers.clear();
    state.accountToggleItems.clear();
    state.accountRefs.clear();
}
