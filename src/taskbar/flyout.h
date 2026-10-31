// Account details flyout host: open/close, hover quick look and click-to-pin.

static constexpr int kFlyoutHoverDwellMs = 350;
static constexpr int kFlyoutHoverGraceMs = 300;
static constexpr int kFlyoutPollMs = 100;
// Refreshes countdowns while a flyout is open.
static constexpr int kFlyoutCountdownSeconds = 30;
static constexpr float kFlyoutTaskbarGap = 12.f;

static void ShowAccountFlyout(QuotaUiInstance& state, int index, bool pinned);
static void OnTilePointerExited(QuotaUiInstance& state, int index);

// Taskbars whose UI this thread owns; their flyout state may be used directly. Pointers stay
// valid while this thread doesn't erase states.
static std::vector<QuotaUiInstance*> CurrentThreadUiInstances() {
    std::vector<QuotaUiInstance*> instances;
    std::lock_guard<std::mutex> lk(g_uiInstancesMutex);
    if (!g_uiInstances) return instances;
    for (auto& instance : *g_uiInstances) {
        if (instance->ownerThreadId == GetCurrentThreadId()) instances.push_back(instance.get());
    }
    return instances;
}

// Stops popup timers, revokes every popup delegate, hides the popup, then drops its references.
// Safe after partial construction, after the popup closed itself, and when nothing is open.
static void CloseAccountFlyout(QuotaUiInstance& state) {
    FlyoutUi& fo = state.flyout;
    FlyoutPopup& popup = fo.popup;
    try {
        if (fo.countdownTimer) fo.countdownTimer.Stop();
    } catch (...) {}
    try {
        // A hovered tile keeps the poll running to catch a missed pointer exit.
        if (fo.pollTimer && !IsTileHovered(state)) fo.pollTimer.Stop();
    } catch (...) {}
    fo.pointerOutsideTick = 0;
    if (popup.flyout && popup.closedToken) {
        try { popup.flyout.Closed(popup.closedToken); } catch (...) {}
    }
    for (auto& handler : popup.buttonHandlers) {
        if (!handler.button) continue;
        try { handler.button.Click(handler.token); } catch (...) {}
    }
    popup.buttonHandlers.clear();
    if (popup.flyout) {
        try { popup.flyout.Hide(); } catch (...) {}
    }
    int closedIndex = popup.accountIndex;
    popup = {};
    UpdateTileHighlight(state, closedIndex);
}

static void EnsureFlyoutTimers(QuotaUiInstance& state) {
    FlyoutUi& fo = state.flyout;
    HWND hWnd = state.hWnd;
    if (!fo.dwellTimer) {
        DispatcherTimer timer;
        timer.Interval(std::chrono::milliseconds(kFlyoutHoverDwellMs));
        fo.dwellTimerToken = timer.Tick(
            [hWnd](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Windows::Foundation::IInspectable const&) {
                if (g_unloading) return;
                try {
                    auto* uiState = FindUiState(hWnd);
                    if (!uiState || !uiState->quotaGrid) return;
                    FlyoutUi& fo = uiState->flyout;
                    if (fo.dwellTimer) fo.dwellTimer.Stop();
                    int index = fo.dwellIndex;
                    fo.dwellIndex = -1;
                    if (index < 0 || fo.hoverIndex != index || fo.hoverSuppressedIndex == index) {
                        return;
                    }
                    // While pinned, hover does nothing, including from another taskbar.
                    if (fo.popup.flyout && (fo.popup.pinned || fo.popup.accountIndex == index)) {
                        return;
                    }
                    for (auto* instance : CurrentThreadUiInstances()) {
                        if (instance->flyout.popup.pinned) return;
                    }
                    ShowAccountFlyout(*uiState, index, false);
                } catch (...) {}
            });
        fo.dwellTimer = timer;
    }
    // XAML pointer events miss the pointer leaving the taskbar or the popup for another window,
    // and clicks in other apps never reach the taskbar's light dismiss, so open popups poll the
    // real pointer and foreground window.
    if (!fo.pollTimer) {
        DispatcherTimer timer;
        timer.Interval(std::chrono::milliseconds(kFlyoutPollMs));
        fo.pollTimerToken = timer.Tick(
            [hWnd](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Windows::Foundation::IInspectable const&) {
                if (g_unloading) return;
                try {
                    auto* uiState = FindUiState(hWnd);
                    if (!uiState) return;
                    FlyoutUi& fo = uiState->flyout;
                    FlyoutPopup& popup = fo.popup;
                    if (!popup.flyout) {
                        // Without a popup it runs only while a tile is hovered: XAML can miss
                        // the pointer leaving the taskbar for another window, so the hover ends
                        // once the real cursor is off this taskbar. Exits within the taskbar are
                        // left to XAML's own events.
                        if (IsTileHovered(*uiState)) {
                            int index = fo.hoverIndex;
                            POINT pt{};
                            RECT window{};
                            bool inside = GetCursorPos(&pt) &&
                                          GetWindowRect(uiState->hWnd, &window) &&
                                          PtInRect(&window, pt);
                            if (!inside) {
                                OnTilePointerExited(*uiState, index);
                                UpdateTileHighlight(*uiState, index);
                            }
                        }
                        if (fo.pollTimer && !IsTileHovered(*uiState)) fo.pollTimer.Stop();
                        return;
                    }

                    // Pinned: close when another thread's window becomes the foreground.
                    if (popup.pinned) {
                        HWND foreground = GetForegroundWindow();
                        if (foreground == popup.lastForeground) return;
                        popup.lastForeground = foreground;
                        if (foreground && GetWindowThreadProcessId(foreground, nullptr) !=
                                              uiState->ownerThreadId) {
                            CloseAccountFlyout(*uiState);
                        }
                        return;
                    }

                    // Hover: inside while over its tile (taskbar pointer events track that) or
                    // over a window this thread showed after the popup opened.
                    POINT pt{};
                    if (!GetCursorPos(&pt)) return;
                    bool overTaskbar = false;
                    for (auto* instance : CurrentThreadUiInstances()) {
                        RECT rect{};
                        if (GetWindowRect(instance->hWnd, &rect) && PtInRect(&rect, pt)) {
                            overTaskbar = true;
                            break;
                        }
                    }
                    bool inside = fo.hoverIndex == popup.accountIndex;
                    if (!overTaskbar) {
                        struct PopupSearch {
                            POINT pt;
                            const std::vector<HWND>* before;
                            HWND found;
                        } search{pt, &popup.windowsBeforeOpen, nullptr};
                        EnumThreadWindows(GetCurrentThreadId(),
                            [](HWND candidate, LPARAM param) CALLBACK -> BOOL {
                                auto& s = *reinterpret_cast<PopupSearch*>(param);
                                RECT rect{};
                                if (!IsWindowVisible(candidate) ||
                                    !GetWindowRect(candidate, &rect) || !PtInRect(&rect, s.pt) ||
                                    std::find(s.before->begin(), s.before->end(), candidate) !=
                                        s.before->end()) {
                                    return TRUE;
                                }
                                s.found = candidate;
                                return FALSE;
                            }, reinterpret_cast<LPARAM>(&search));
                        inside = search.found != nullptr;
                        static bool loggedPopupClass = false;
                        if (inside && !loggedPopupClass) {
                            loggedPopupClass = true;
                            wchar_t className[128] = {};
                            GetClassNameW(search.found, className, ARRAYSIZE(className));
                            Wh_Log(L"Flyout popup window class: %s", className);
                        }
                    }
                    if (inside) {
                        fo.pointerOutsideTick = 0;
                        return;
                    }
                    ULONGLONG now = GetTickCount64();
                    if (!fo.pointerOutsideTick) fo.pointerOutsideTick = now;
                    // On another tile, its dwell switches the account instead.
                    if (fo.hoverIndex >= 0 && fo.dwellIndex == fo.hoverIndex) return;
                    if (now - fo.pointerOutsideTick >= kFlyoutHoverGraceMs) {
                        CloseAccountFlyout(*uiState);
                    }
                } catch (...) {}
            });
        fo.pollTimer = timer;
    }
    if (!fo.countdownTimer) {
        DispatcherTimer timer;
        timer.Interval(std::chrono::seconds(kFlyoutCountdownSeconds));
        fo.countdownTimerToken = timer.Tick(
            [hWnd](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Windows::Foundation::IInspectable const&) {
                if (g_unloading) return;
                try {
                    auto* uiState = FindUiState(hWnd);
                    if (uiState && uiState->quotaGrid && uiState->flyout.popup.flyout) {
                        UpdateQuotaUi(*uiState);
                    }
                } catch (...) {}
            });
        fo.countdownTimer = timer;
    }
}

// Opens the account's flyout above its column, replacing any open one: transient (no focus)
// for hover, standard (focusable, light dismiss) when pinned by a click.
static void ShowAccountFlyout(QuotaUiInstance& state, int index, bool pinned) {
    if (g_unloading || !state.quotaGrid || index < 0 ||
        index >= (int)state.accountRefs.size()) {
        return;
    }
    StackPanel column = state.accountRefs[index].column;
    std::wstring provider = state.accountRefs[index].provider;
    uint64_t identity = state.accountRefs[index].identity;
    // One flyout per taskbar thread, including the other taskbars this thread owns.
    CloseAccountFlyout(state);
    for (auto* instance : CurrentThreadUiInstances()) CloseAccountFlyout(*instance);

    FlyoutUi& fo = state.flyout;
    FlyoutPopup& popup = fo.popup;
    try {
        if (!column || column.Visibility() != Visibility::Visible) return;
        auto xamlRoot = column.XamlRoot();
        auto rootContent = xamlRoot ? xamlRoot.Content().try_as<FrameworkElement>() : nullptr;
        if (!rootContent) return;
        EnsureFlyoutTimers(state);

        popup.generation = ++fo.openGeneration;
        popup.accountIndex = index;
        popup.accountIdentity = identity;
        popup.pinned = pinned;
        popup.theme = LoadFlyoutTheme();
        Border card = BuildFlyoutCard(state, provider, state.buildVisualTestMode);
        popup.card = card;

        Flyout flyout;
        popup.flyout = flyout;
        flyout.Content(card);
        // The card draws its own surface; the presenter only hosts it.
        Style presenterStyle(winrt::xaml_typename<FlyoutPresenter>());
        Brush transparent = FlyoutBrush(winrt::Windows::UI::Colors::Transparent());
        auto setters = presenterStyle.Setters();
        setters.Append(Setter(Control::BackgroundProperty(), transparent));
        setters.Append(Setter(Control::BorderBrushProperty(), transparent));
        setters.Append(Setter(Control::BorderThicknessProperty(), winrt::box_value(Thickness{})));
        setters.Append(Setter(Control::PaddingProperty(), winrt::box_value(Thickness{})));
        setters.Append(Setter(Control::CornerRadiusProperty(),
                              winrt::box_value(CornerRadius{8, 8, 8, 8})));
        setters.Append(Setter(Control::FontFamilyProperty(),
                              FontFamily(L"Segoe UI Variable Text, Segoe UI")));
        setters.Append(Setter(FrameworkElement::MarginProperty(), winrt::box_value(Thickness{})));
        setters.Append(Setter(FrameworkElement::MinWidthProperty(), winrt::box_value(0.0)));
        setters.Append(Setter(FrameworkElement::MaxWidthProperty(), winrt::box_value(10000.0)));
        setters.Append(Setter(FrameworkElement::MaxHeightProperty(), winrt::box_value(10000.0)));
        flyout.FlyoutPresenterStyle(presenterStyle);
        flyout.ShouldConstrainToRootBounds(false);
        // Taskbar input stays live under the overlay so tiles keep hover, click and menu.
        flyout.OverlayInputPassThroughElement(rootContent);

        HWND hWnd = state.hWnd;
        uint64_t generation = popup.generation;
        // Closed by the system (light dismiss, Escape). Cleanup never depends on this event.
        popup.closedToken = flyout.Closed(
            [hWnd, generation](winrt::Windows::Foundation::IInspectable const&,
                               winrt::Windows::Foundation::IInspectable const&) {
                if (g_unloading) return;
                try {
                    auto* uiState = FindUiState(hWnd);
                    if (!uiState) return;
                    FlyoutUi& fo = uiState->flyout;
                    if (!fo.popup.flyout || fo.popup.generation != generation) return;
                    fo.dismissedIndex = fo.popup.accountIndex;
                    fo.dismissedPinned = fo.popup.pinned;
                    fo.dismissedTick = GetTickCount64();
                    if (fo.hoverIndex == fo.popup.accountIndex) {
                        fo.hoverSuppressedIndex = fo.hoverIndex;
                    }
                    CloseAccountFlyout(*uiState);
                } catch (...) {}
            });
        UpdateTileHighlight(state, index);
        UpdateQuotaUi(state);
        if (!popup.flyout) return;

        // Centered on the column, just above the taskbar's top edge.
        auto origin = column.TransformToVisual(rootContent).TransformPoint({0.f, 0.f});
        wuxcp::FlyoutShowOptions options;
        options.Placement(wuxcp::FlyoutPlacementMode::Top);
        options.ShowMode(pinned ? wuxcp::FlyoutShowMode::Standard :
                                  wuxcp::FlyoutShowMode::Transient);
        options.Position(winrt::Windows::Foundation::Point{
            (float)column.ActualWidth() / 2.f, -origin.Y - kFlyoutTaskbarGap});
        EnumThreadWindows(GetCurrentThreadId(),
            [](HWND candidate, LPARAM param) CALLBACK -> BOOL {
                if (IsWindowVisible(candidate)) {
                    reinterpret_cast<std::vector<HWND>*>(param)->push_back(candidate);
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&popup.windowsBeforeOpen));
        popup.lastForeground = GetForegroundWindow();
        fo.pointerOutsideTick = 0;
        flyout.ShowAt(column, options);
        if (fo.countdownTimer) fo.countdownTimer.Start();
        if (fo.pollTimer) fo.pollTimer.Start();
    } catch (...) {
        Wh_Log(L"ShowAccountFlyout: exception");
        CloseAccountFlyout(state);
    }
}

// Click: open pinned; pinned on this account closes; otherwise switch to this account. Accounts
// that need a sign-in start it instead.
static void OnTileTapped(QuotaUiInstance& state, int index, uint64_t identity,
                         bool visualTestMode) {
    FlyoutUi& fo = state.flyout;
    if (fo.dwellTimer) fo.dwellTimer.Stop();
    fo.dwellIndex = -1;
    if (!visualTestMode) {
        bool accountFound = false;
        bool needsLogin = false;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            for (size_t i = 0; i < g_settings.accounts.size(); i++) {
                if (AccountIdentityHash(g_settings.accounts[i]) != identity) continue;
                accountFound = true;
                std::lock_guard<std::mutex> lk2(g_dataMutex);
                if (i < g_data.size()) needsLogin = g_data[i].needsLogin;
                break;
            }
        }
        if (!accountFound) return;
        if (needsLogin) {
            for (auto* instance : CurrentThreadUiInstances()) CloseAccountFlyout(*instance);
            StartLoginByIdentity(identity);
            return;
        }
    }

    // Pressing this tile can light-dismiss its pinned flyout before the tap arrives.
    bool dismissedByThisPress = fo.dismissedIndex == index && fo.pressIndex == index &&
                                fo.dismissedTick + 50 >= fo.pressTick;
    bool dismissedPinned = fo.dismissedPinned;
    fo.dismissedIndex = -1;
    if (fo.popup.flyout) {
        bool closeOnly = fo.popup.pinned && fo.popup.accountIndex == index;
        CloseAccountFlyout(state);
        if (closeOnly) {
            if (fo.hoverIndex == index) fo.hoverSuppressedIndex = index;
            return;
        }
    } else if (dismissedByThisPress && dismissedPinned) {
        return;
    }
    ShowAccountFlyout(state, index, true);
}

static void OnTilePointerEntered(QuotaUiInstance& state, int index, bool showDetailsOnHover) {
    FlyoutUi& fo = state.flyout;
    const FlyoutPopup& popup = fo.popup;
    fo.hoverIndex = index;
    // Watches for the pointer leaving a hovered tile without a PointerExited.
    if (IsTileHovered(state)) {
        EnsureFlyoutTimers(state);
        if (!fo.pollTimer.IsEnabled()) fo.pollTimer.Start();
    }
    if (!showDetailsOnHover || fo.hoverSuppressedIndex == index ||
        (popup.flyout && (popup.pinned || popup.accountIndex == index))) {
        if (fo.dwellTimer) fo.dwellTimer.Stop();
        fo.dwellIndex = -1;
        return;
    }
    EnsureFlyoutTimers(state);
    fo.dwellIndex = index;
    fo.dwellTimer.Stop();
    fo.dwellTimer.Start();
}

static void OnTilePointerExited(QuotaUiInstance& state, int index) {
    FlyoutUi& fo = state.flyout;
    if (fo.hoverIndex == index) fo.hoverIndex = -1;
    if (fo.hoverSuppressedIndex == index) fo.hoverSuppressedIndex = -1;
    if (fo.dwellIndex == index) {
        if (fo.dwellTimer) fo.dwellTimer.Stop();
        fo.dwellIndex = -1;
    }
}
