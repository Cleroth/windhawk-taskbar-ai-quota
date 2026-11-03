// Injects and removes quota grids, retries injection, and hooks taskbar startup.

static HANDLE g_injectEvent = nullptr;
static HANDLE g_retryThread = nullptr;
static std::mutex g_retryThreadMutex;
static std::atomic<bool> g_rebuildQuotaUiBeforeInject{false};  // Remove after the topology settles.
static std::atomic<bool> g_removeQuotaUiBeforeInject{false};   // Remove before the next attempt.
static std::atomic<ULONGLONG> g_nextInjectFailureLogMs{0};

static UINT GetQuotaCleanupMessage() {
    static const UINT message = RegisterWindowMessageW(L"Windhawk_CleanupQuotaUi_" WH_MOD_ID);
    return message;
}

static void RemoveQuotaChildren(Panel const& targetPanel, QuotaUiInstance& state) {
    if (!targetPanel) return;
    ClearQuotaEventState(state);

    for (int i = (int)targetPanel.Children().Size() - 1; i >= 0; --i) {
        auto fe = targetPanel.Children().GetAt(i).try_as<FrameworkElement>();
        if (fe && fe.Name() == kRootName) {
            try { targetPanel.Children().RemoveAt(i); } catch (...) {}
        }
    }
}

static void RemoveQuotaGridFromState(QuotaUiInstance& state) {
    if (state.ownerThreadId && state.ownerThreadId != GetCurrentThreadId()) {
        Wh_Log(L"Refusing XAML cleanup from a non-owner thread");
        return;
    }

    if (!state.injectionParent) {
        ClearQuotaEventState(state);
        state.quotaGrid = nullptr;
        state.quotaColumnDefinition = nullptr;
        state.applied.clear();
        return;
    }

    try {
        RemoveQuotaChildren(state.injectionParent, state);
        auto targetGrid = state.injectionParent.try_as<Grid>();
        int ownedCol = -1;
        if (targetGrid && state.quotaColumnDefinition) {
            auto definitions = targetGrid.ColumnDefinitions();
            for (uint32_t i = 0; i < definitions.Size(); ++i) {
                auto definition = definitions.GetAt(i);
                if (winrt::get_abi(definition) == winrt::get_abi(state.quotaColumnDefinition)) {
                    ownedCol = (int)i;
                    break;
                }
            }
        }

        // If another component already removed our definition, it also owns the resulting
        // child shifts. Never substitute a numeric column belonging to somebody else.
        if (targetGrid && ownedCol >= 0 && ownedCol < (int)targetGrid.ColumnDefinitions().Size()) {
            targetGrid.ColumnDefinitions().RemoveAt(ownedCol);
            for (uint32_t i = 0; i < targetGrid.Children().Size(); ++i) {
                auto child = targetGrid.Children().GetAt(i).try_as<FrameworkElement>();
                if (child) {
                    int childCol = Grid::GetColumn(child);
                    int childSpan = Grid::GetColumnSpan(child);
                    if (childCol > ownedCol) {
                        Grid::SetColumn(child, childCol - 1);
                    } else if (childCol < ownedCol && childCol + childSpan > ownedCol) {
                        Grid::SetColumnSpan(child, childSpan - 1);
                    }
                }
            }
        }
    } catch (...) {
        Wh_Log(L"RemoveQuotaGrid: exception");
    }

    state.quotaGrid = nullptr;
    state.injectionParent = nullptr;
    state.quotaColumnDefinition = nullptr;
    state.applied.clear();
}

static bool InjectQuotaGrid(HWND hWnd) {
    if (!hWnd) return false;
    DWORD ownerThreadId = GetWindowThreadProcessId(hWnd, nullptr);
    if (!ownerThreadId || ownerThreadId != GetCurrentThreadId()) return false;
    if (auto* existingState = FindUiState(hWnd); existingState) {
        if (existingState->ownerThreadId != ownerThreadId) {
            Wh_Log(L"Taskbar HWND was reused by a different UI thread");
            return false;
        }
        ULONGLONG settingsGeneration;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            settingsGeneration = g_settingsGeneration;
        }
        if (existingState->quotaGrid &&
            existingState->buildSettingsGeneration == settingsGeneration &&
            existingState->buildVisualTestMode ==
                g_visualTestMode.load(std::memory_order_acquire)) {
            return true;
        }
    }

    auto fail = [&](PCWSTR reason) {
        ULONGLONG now = NowUnixMs();
        ULONGLONG nextLogMs = g_nextInjectFailureLogMs.load(std::memory_order_acquire);
        if (now >= nextLogMs &&
            g_nextInjectFailureLogMs.compare_exchange_strong(nextLogMs, now + 5000,
                                                            std::memory_order_acq_rel)) {
            wchar_t cls[64] = {};
            GetClassNameW(hWnd, cls, ARRAYSIZE(cls));
            Wh_Log(L"InjectQuotaGrid failed: hwnd=%p class=%s reason=%s", hWnd, cls, reason);
        }
        return false;
    };

    QuotaUiInstance* state = nullptr;
    try {
        auto xamlRoot = GetTaskbarXamlRoot(hWnd);
        if (!xamlRoot) return fail(L"no XamlRoot");
        auto root = xamlRoot.Content().try_as<FrameworkElement>();
        if (!root) return fail(L"no XamlRoot content");
        auto trayFrame = FindChildByName(root, L"SystemTrayFrameGrid");
        // On a cold start the XamlRoot is ready before the system tray contents are realized
        // in the visual tree, so SystemTrayFrameGrid may be missing for the first attempts.
        // Bail and let the retry loop poll until it appears; never inject elsewhere, which
        // would render the bars on top of the clock/tray.
        if (!trayFrame) return fail(L"no SystemTrayFrameGrid");
        auto trayPanel = trayFrame.try_as<Panel>();
        auto trayGrid = trayFrame.try_as<Grid>();
        // Newer Windows taskbars keep the name but use a StackPanel. Don't assume other
        // panel types share its child-order layout semantics.
        if (!trayPanel || (!trayGrid && !trayFrame.try_as<StackPanel>())) {
            std::wstring reason = L"unsupported SystemTrayFrameGrid type: ";
            reason += winrt::get_class_name(trayFrame);
            return fail(reason.c_str());
        }

        state = FindUiState(hWnd);
        if (!state) {
            auto newState = std::make_unique<QuotaUiInstance>();
            newState->hWnd = hWnd;
            newState->ownerThreadId = ownerThreadId;
            {
                std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
                if (g_uiInstances) {
                    g_uiInstances->push_back(std::move(newState));
                    state = g_uiInstances->back().get();
                }
            }
            if (!state) return fail(L"UI state registry unavailable");
            if (!WindhawkUtils::SetWindowSubclassFromAnyThread(
                    hWnd, TaskbarWindowSubclassProc, 0)) {
                EraseUiState(hWnd);
                state = nullptr;
                return fail(L"window subclass failed");
            }
            state->windowSubclassed = true;
            // Remove a visual left by an earlier failed teardown, but don't guess which column
            // definition it owned.
            RemoveQuotaChildren(trayPanel, *state);
        } else {
            RemoveQuotaGridFromState(*state);
        }
        state->injectionParent = trayPanel;
        double rasterizationScale = xamlRoot.RasterizationScale();
        state->rasterizationScale = rasterizationScale > 0 ? rasterizationScale : 1.0;
        Grid quota = BuildQuotaGrid(*state);
        if (!quota) {
            RemoveQuotaGridFromState(*state);
            EraseUiState(hWnd);
            return fail(L"BuildQuotaGrid failed");
        }

        if (trayGrid) {
            ColumnDefinition newCol;
            newCol.Width({1.0, GridUnitType::Auto});
            state->quotaColumnDefinition = newCol;
            trayGrid.ColumnDefinitions().InsertAt(0, newCol);
            // Inserting at the left edge means no existing child can span across the new column.
            for (uint32_t i = 0; i < trayGrid.Children().Size(); ++i) {
                auto child = trayGrid.Children().GetAt(i).try_as<FrameworkElement>();
                if (child) Grid::SetColumn(child, Grid::GetColumn(child) + 1);
            }
            Grid::SetColumn(quota, 0);
            trayGrid.Children().Append(quota);
        } else {
            // StackPanel layout follows child order; no column is created or owned.
            trayPanel.Children().InsertAt(0, quota);
        }

        state->quotaGrid = quota;
        g_uiInjected.store(true, std::memory_order_release);
        state->applied.clear();
        UpdateQuotaUi(*state);
        Wh_Log(L"Injected quota bars into %s", winrt::get_class_name(trayPanel).c_str());
        return true;
    } catch (...) {
        if (state) {
            RemoveQuotaGridFromState(*state);
            EraseUiState(hWnd);
        }
        Wh_Log(L"InjectQuotaGrid: exception");
        return false;
    }
}

static void RemoveQuotaGrid(HWND hWnd) {
    auto* state = FindUiState(hWnd);
    if (!state) return;
    if (state->ownerThreadId != GetCurrentThreadId()) {
        Wh_Log(L"Refusing UI removal from a non-owner thread");
        return;
    }

    RemoveQuotaGridFromState(*state);
    EraseUiState(hWnd);
}

static void ReleaseQuotaUiState(HWND hWnd) {
    auto* state = FindUiState(hWnd);
    if (!state) return;
    if (state->ownerThreadId != GetCurrentThreadId()) {
        Wh_Log(L"Refusing UI state release from a non-owner thread");
        return;
    }

    ClearQuotaEventState(*state);
    state->quotaGrid = nullptr;
    state->injectionParent = nullptr;
    state->quotaColumnDefinition = nullptr;
    state->applied.clear();
    EraseUiState(hWnd);
}

static LRESULT CALLBACK TaskbarWindowSubclassProc(HWND hWnd, UINT message, WPARAM wParam,
                                                  LPARAM lParam, DWORD_PTR) {
    if (message == WM_NCDESTROY) {
        // Windhawk's wrapper has already removed the subclass. The XAML tree is dying, so only
        // revoke callbacks and release references; don't mutate its columns.
        if (auto* state = FindUiState(hWnd)) state->windowSubclassed = false;
        ReleaseQuotaUiState(hWnd);
    } else if (message == GetQuotaCleanupMessage()) {
        // Fallback when installing a temporary marshal hook fails during unload.
        if (auto* state = FindUiState(hWnd)) {
            if (state->windowSubclassed) {
                WindhawkUtils::RemoveWindowSubclassFromAnyThread(
                    hWnd, TaskbarWindowSubclassProc);
                state->windowSubclassed = false;
            }
            RemoveQuotaGrid(hWnd);
        }
    } else if (message == WM_DISPLAYCHANGE && !g_unloading) {
        StartRetryInject(true);
    } else if (message == WM_SETTINGCHANGE && wParam == SPI_SETHIGHCONTRAST && !g_unloading) {
        // Brand-colored logos give way to the text color in high contrast: rebuild them.
        StartRetryInject(true);
    } else if (message == WM_SETTINGCHANGE && lParam && !g_unloading &&
               wcscmp(reinterpret_cast<PCWSTR>(lParam), L"ImmersiveColorSet") == 0) {
        // Light/dark switch: once the taskbar has handled it, repaint the tiles' theme colors.
        // Window procedures run on the window's thread, which owns this taskbar's XAML.
        LRESULT result = DefSubclassProc(hWnd, message, wParam, lParam);
        if (!g_unloading) {
            try {
                auto* state = FindUiState(hWnd);
                if (state && state->quotaGrid && state->ownerThreadId == GetCurrentThreadId()) {
                    UpdateQuotaUi(*state);
                }
            } catch (...) {}
        }
        return result;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

static void RemoveAllQuotaGrids(bool waitForCompletion = false) {
    std::vector<std::pair<HWND, DWORD>> windows;
    {
        std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
        if (!g_uiInstances) return;
        windows.reserve(g_uiInstances->size());
        for (auto& state : *g_uiInstances) {
            windows.push_back({state->hWnd, state->ownerThreadId});
        }
    }

    for (auto [hWnd, ownerThreadId] : windows) {
        if (!hWnd) continue;
        if (ownerThreadId == GetCurrentThreadId()) {
            if (IsWindow(hWnd)) RemoveQuotaGrid(hWnd);
            else ReleaseQuotaUiState(hWnd);
            continue;
        }

        DWORD liveThreadId = GetWindowThreadProcessId(hWnd, nullptr);
        bool removed = false;
        if (liveThreadId == ownerThreadId) {
            removed = RunFromWindowThread(hWnd, [](void* param) -> bool {
                RemoveQuotaGrid(static_cast<HWND>(param));
                return true;
            }, hWnd, waitForCompletion ? INFINITE : 2000);
            if (!removed && IsWindow(hWnd)) {
                if (waitForCompletion) {
                    SendMessageW(hWnd, GetQuotaCleanupMessage(), 0, 0);
                } else {
                    DWORD_PTR ignored = 0;
                    SendMessageTimeoutW(
                        hWnd, GetQuotaCleanupMessage(), 0, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_NOTIMEOUTIFNOTHUNG,
                        2000, &ignored);
                }
                removed = !FindUiState(hWnd);
            }
        } else {
            HWND ownerWindow = nullptr;
            EnumThreadWindows(ownerThreadId,
                [](HWND candidate, LPARAM param) CALLBACK -> BOOL {
                    *reinterpret_cast<HWND*>(param) = candidate;
                    return FALSE;
                }, reinterpret_cast<LPARAM>(&ownerWindow));
            if (ownerWindow) {
                removed = RunFromWindowThread(ownerWindow, [](void* param) -> bool {
                    ReleaseQuotaUiState(static_cast<HWND>(param));
                    return true;
                }, hWnd, waitForCompletion ? INFINITE : 2000);
            }
        }
        if (!removed || FindUiState(hWnd)) {
            Wh_Log(L"RemoveQuotaGrid marshal failed");
        }
    }
}

static LRESULT CALLBACK TopologyWindowProc(HWND hWnd, UINT message,
                                           WPARAM wParam, LPARAM lParam) {
    if (message == WM_DISPLAYCHANGE && !g_unloading) {
        g_rebuildQuotaUiBeforeInject = true;
        if (g_injectEvent) SetEvent(g_injectEvent);
        return 0;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

static DWORD WINAPI RetryInjectThreadProc(LPVOID) {
    static PCWSTR kTopologyWindowClass = L"AiQuotaTopology_" WH_MOD_ID;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = TopologyWindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kTopologyWindowClass;
    bool classRegistered = RegisterClassExW(&windowClass) != 0;
    if (!classRegistered && GetLastError() == ERROR_CLASS_ALREADY_EXISTS &&
        UnregisterClassW(kTopologyWindowClass, instance)) {
        classRegistered = RegisterClassExW(&windowClass) != 0;
    }
    HWND topologyWindow = classRegistered ?
        CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kTopologyWindowClass, L"",
                        WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, nullptr) : nullptr;

    HANDLE startHandles[2] = {g_stopEvent, g_injectEvent};
    auto waitWithMessages = [&](DWORD timeout) {
        DWORD result = MsgWaitForMultipleObjects(2, startHandles, FALSE, timeout, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0 + 2) {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return result;
    };
    bool stopping = false;
    while (!g_unloading) {
        DWORD startWait = waitWithMessages(INFINITE);
        if (startWait == WAIT_OBJECT_0 || g_unloading) break;
        if (startWait == WAIT_OBJECT_0 + 2) continue;
        if (startWait != WAIT_OBJECT_0 + 1) continue;

        ULONGLONG settleUntil = 0;
        for (int attempt = 0; attempt < 600 && !g_unloading; attempt++) {
            if (g_removeQuotaUiBeforeInject.exchange(false)) RemoveAllQuotaGrids();
            if (g_rebuildQuotaUiBeforeInject.exchange(false)) {
                settleUntil = GetTickCount64() + 2000;
            }
            if (settleUntil) {
                ULONGLONG now = GetTickCount64();
                if (now < settleUntil) {
                    DWORD settleWait = waitWithMessages(
                        (DWORD)std::min<ULONGLONG>(100, settleUntil - now));
                    if (settleWait == WAIT_OBJECT_0 || g_unloading) {
                        stopping = true;
                        break;
                    }
                    if (settleWait == WAIT_OBJECT_0 + 1) attempt = -1;
                    else attempt--;  // The absolute settle deadline bounds these wakes.
                    continue;
                }
                RemoveAllQuotaGrids();
                settleUntil = 0;
            }
            auto hWnds = FindCurrentProcessTaskbarWnds();
            TaskbarMonitorMode mode;
            int targetMonitorNumber;
            {
                std::lock_guard<std::mutex> lk(g_settingsMutex);
                mode = g_settings.taskbarMonitorMode;
                targetMonitorNumber = g_settings.taskbarMonitorNumber;
            }
            if (hWnds.empty() || (mode == TaskbarMonitorMode::All && hWnds.size() < 2)) {
                ULONGLONG now = NowUnixMs();
                ULONGLONG nextLogMs = g_nextInjectFailureLogMs.load(std::memory_order_acquire);
                if (now >= nextLogMs &&
                    g_nextInjectFailureLogMs.compare_exchange_strong(nextLogMs, now + 5000,
                                                                    std::memory_order_acq_rel)) {
                    Wh_Log(L"Taskbar discovery: mode=%d target=%d eligible=%zu",
                           (int)mode, targetMonitorNumber, hWnds.size());
                }
            }
            bool allInjected = !hWnds.empty();
            for (HWND hWnd : hWnds) {
                bool injected = RunFromWindowThread(hWnd, [](void* param) -> bool {
                    return !g_unloading && InjectQuotaGrid(static_cast<HWND>(param));
                }, hWnd);
                allInjected = allInjected && injected;
            }

            DWORD waitMs = allInjected ? 0 : 100;
            DWORD retryWait = waitWithMessages(waitMs);
            if (retryWait == WAIT_OBJECT_0 || g_unloading) {
                stopping = true;
                break;
            }
            if (retryWait == WAIT_OBJECT_0 + 1) {
                attempt = -1;  // A newer rebuild request supersedes this attempt series.
                continue;
            }
            if (allInjected) break;
        }
        if (stopping) break;
    }
    if (topologyWindow) DestroyWindow(topologyWindow);
    if (classRegistered) UnregisterClassW(kTopologyWindowClass, instance);
    return 0;
}

// Requests removal/injection from the retry worker, which unload joins; callers never marshal
// them themselves. If the worker can't start, the UI stays as it is and pending requests wait for
// the next successful start.
static void StartRetryInject(bool removeExisting) {
    if (removeExisting) g_rebuildQuotaUiBeforeInject = true;
    // The long-lived worker consumes every rebuild event. Keeping it alive closes the race
    // where an autosave removes the UI just as an older one-shot injection thread exits.
    std::lock_guard<std::mutex> lk(g_retryThreadMutex);
    if (g_unloading) return;

    if (g_retryThread && WaitForSingleObject(g_retryThread, 0) == WAIT_OBJECT_0) {
        CloseHandle(g_retryThread);
        g_retryThread = nullptr;
    }
    if (!g_retryThread) {
        g_retryThread = CreateThread(nullptr, 0, RetryInjectThreadProc, nullptr, 0, nullptr);
        if (!g_retryThread) {
            Wh_Log(L"CreateThread RetryInjectThreadProc failed: %lu", GetLastError());
            return;
        }
    }
    if (g_injectEvent) SetEvent(g_injectEvent);
}

using TrayUI_StartTaskbar_t = void(WINAPI*)(void*);
static TrayUI_StartTaskbar_t TrayUI_StartTaskbar_Original = nullptr;

static void WINAPI TrayUI_StartTaskbar_Hook(void* pThis) {
    TrayUI_StartTaskbar_Original(pThis);
    if (g_unloading) return;

    // Drop UI bound to the previous taskbar windows before injecting into the new ones.
    g_removeQuotaUiBeforeInject = true;
    StartRetryInject();
}

static bool HookTaskbarDllSymbols() {
    HMODULE h = LoadLibraryExW(L"taskbar.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!h) return false;

    WindhawkUtils::SYMBOL_HOOK taskbarDllHooks[] = {
        {{LR"(const CTaskBand::`vftable'{for `ITaskListWndSite'})"}, &CTaskBand_ITaskListWndSite_vftable},
        {{LR"(const CSecondaryTaskBand::`vftable'{for `ITaskListWndSite'})"}, &CSecondaryTaskBand_ITaskListWndSite_vftable},
        {{LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CTaskBand::GetTaskbarHost(void)const )"}, &CTaskBand_GetTaskbarHost_Original},
        {{LR"(public: virtual class std::shared_ptr<class TaskbarHost> __cdecl CSecondaryTaskBand::GetTaskbarHost(void)const )"}, &CSecondaryTaskBand_GetTaskbarHost_Original},
        {{LR"(public: int __cdecl TaskbarHost::FrameHeight(void)const )"}, &TaskbarHost_FrameHeight_Original},
        {{LR"(public: void __cdecl std::_Ref_count_base::_Decref(void))"}, &Std_Ref_Decref_Original},
        {{LR"(public: virtual void __cdecl TrayUI::StartTaskbar(void))"}, &TrayUI_StartTaskbar_Original, TrayUI_StartTaskbar_Hook},
    };
    return WindhawkUtils::HookSymbols(h, taskbarDllHooks, ARRAYSIZE(taskbarDllHooks));
}
