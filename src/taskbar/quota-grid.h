// Builds the per-taskbar quota grid, including visual-test sample data.

static void BuildVisualTestSnapshot(int yellowThreshold, int orangeThreshold,
                                    int redThreshold, ULONGLONG now,
                                    std::vector<AccountConfig>* accounts,
                                    std::vector<AccountData>* data) {
    static constexpr std::array<const wchar_t*, 4> kLabels = {
        L"OAI", L"CC 1", L"CC 2", L"Go",
    };
    const std::array<double, 4> percentages = {
        yellowThreshold / 2.0,
        50.0,
        orangeThreshold + (redThreshold - orangeThreshold) / 2.0,
        redThreshold + (100 - redThreshold) / 2.0,
    };
    static constexpr std::array<ULONGLONG, kQuotaBarCount> kDurations = {
        kFiveHourWindowMs,
        kWeeklyWindowMs,
        kWeeklyWindowMs,
        30ULL * 24 * 60 * 60 * 1000,
    };
    static constexpr std::array<double, kQuotaBarCount> kRemainingFractions = {
        0.35, 0.5, 0.6, 0.8,
    };

    accounts->clear();
    accounts->reserve(percentages.size());
    if (data) {
        data->clear();
        data->reserve(percentages.size());
    }
    for (size_t i = 0; i < percentages.size(); i++) {
        AccountConfig account;
        account.provider = L"anthropic";
        account.label = kLabels[i];
        account.showBars.fill(false);
        for (size_t w = 0; w <= i; w++) account.showBars[w] = true;
        accounts->push_back(std::move(account));

        if (!data) continue;
        AccountData accountData;
        std::array<WindowUsage*, kQuotaBarCount> usage = {
            &accountData.win5h, &accountData.winWeek,
            &accountData.fableWeek, &accountData.extraUsage,
        };
        for (int w = 0; w < kQuotaBarCount; w++) {
            usage[w]->pct = percentages[i];
            usage[w]->windowDurationMs = kDurations[w];
            usage[w]->resetUnixMs = now +
                (ULONGLONG)std::lround(kDurations[w] * kRemainingFractions[w]);
        }
        accountData.plan = L"Visual test";
        accountData.extraUsedAmount = percentages[i] / 2.0;
        accountData.extraLimitAmount = 50.0;
        accountData.lastSuccessMs = now;
        accountData.stale = false;
        data->push_back(std::move(accountData));
    }
}

static Grid BuildQuotaGrid(QuotaUiInstance& state) {
    try {
        double physicalPixelDip = 1.0 / state.rasterizationScale;
        std::vector<AccountConfig> accounts;
        int barLength, barThickness, labelFontSize, percentFontSize;
        int accountMargin, labelGap, rightMargin;
        int yellowThreshold, orangeThreshold, redThreshold;
        bool showPaceTicks, showBarLabels;
        bool visualTestMode = g_visualTestMode.load(std::memory_order_acquire);
        COLORREF paceTickColor;
        BarLayout barLayout;
        PercentTextVisibility percentTextVisibility;
        PercentTextAlignment percentTextAlignment;
        PaceTickStyle paceTickStyle;
        LabelPosition labelPosition;
        ClickAction clickAction;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            state.buildSettingsGeneration = g_settingsGeneration;
            accounts = g_settings.accounts;
            clickAction = g_settings.clickAction;
            barLayout = g_settings.barLayout;
            barLength = g_settings.barLength;
            barThickness = g_settings.barThickness;
            labelFontSize = g_settings.labelFontSize;
            percentFontSize = g_settings.percentFontSize;
            accountMargin = g_settings.accountMargin;
            labelGap = g_settings.labelGap;
            rightMargin = g_settings.rightMargin;
            labelPosition = g_settings.labelPosition;
            showPaceTicks = g_settings.showPaceTicks;
            paceTickStyle = g_settings.paceTickStyle;
            paceTickColor = g_settings.paceTickColor;
            showBarLabels = g_settings.showBarLabels;
            percentTextVisibility = g_settings.percentTextVisibility;
            percentTextAlignment = g_settings.percentTextAlignment;
            yellowThreshold = g_settings.yellowThreshold;
            orangeThreshold = g_settings.orangeThreshold;
            redThreshold = g_settings.redThreshold;
        }
        state.buildVisualTestMode = visualTestMode;
        if (visualTestMode) {
            BuildVisualTestSnapshot(yellowThreshold, orangeThreshold, redThreshold,
                                    NowUnixMs(), &accounts, nullptr);
        }
        state.accountRefs.clear();
        Grid root;
        root.Name(kRootName);
        root.VerticalAlignment(VerticalAlignment::Center);

        StackPanel panel;
        panel.Orientation(Orientation::Horizontal);
        panel.Margin({4, 0, (double)rightMargin, 0});

        if (accounts.empty()) {
            Border setupTile;
            setupTile.CornerRadius({4, 4, 4, 4});
            setupTile.Padding({8, 3, 8, 3});
            setupTile.Margin({(double)accountMargin, 0, (double)accountMargin, 0});
            setupTile.Background(SolidColorBrush(winrt::Windows::UI::Color{0x30, 0x80, 0x80, 0x80}));

            TextBlock setupText;
            setupText.Text(L"AI +");
            setupText.FontSize(labelFontSize);
            setupText.Opacity(0.9);
            setupTile.Child(setupText);

            UIElement setupElement = setupTile.as<UIElement>();
            auto tappedToken = setupElement.Tapped(
                [](winrt::Windows::Foundation::IInspectable const&,
                   wuxi::TappedRoutedEventArgs const& e) {
                    OpenSettingsWindow();
                    e.Handled(true);
                });
            state.pointerHandlers.push_back({setupElement, tappedToken});
            ToolTipService::SetToolTip(
                setupElement,
                winrt::box_value(g_settingsLoadError.load() ?
                    L"Open AI quota settings. Stored settings could not be read; a backup was kept." :
                    L"Open AI quota settings"));

            MenuFlyout menu;
            MenuFlyoutItem settingsItem;
            settingsItem.Text(L"Settings...");
            auto settingsToken = settingsItem.Click(
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenSettingsWindow();
                });
            state.menuItemClickHandlers.push_back({settingsItem, settingsToken});

            MenuFlyoutItem historyItem;
            historyItem.Text(L"Usage history...");
            auto historyToken = historyItem.Click(
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenHistoryWindow();
                });
            state.menuItemClickHandlers.push_back({historyItem, historyToken});
            menu.Items().Append(historyItem);
            menu.Items().Append(settingsItem);
            setupElement.ContextFlyout(menu);

            panel.Children().Append(setupTile);
            root.Children().Append(panel);
            return root;
        }
        state.accountRefs.reserve(accounts.size());
        bool verticalBars = barLayout == BarLayout::Vertical;
        UINT toolTipDurationSeconds = 5;
        if (!SystemParametersInfoW(SPI_GETMESSAGEDURATION, 0, &toolTipDurationSeconds, 0)) {
            toolTipDurationSeconds = 5;
        }
        auto manualToolTipShowDuration =
            std::chrono::seconds(std::clamp(toolTipDurationSeconds, 1u, 60u));
        UINT toolTipHoverDelayMs = 400;
        if (!SystemParametersInfoW(SPI_GETMOUSEHOVERTIME, 0, &toolTipHoverDelayMs, 0)) {
            toolTipHoverDelayMs = 400;
        }
        auto manualToolTipHoverDelay =
            std::chrono::milliseconds(std::clamp(toolTipHoverDelayMs, 100u, 2000u));
        UINT toolTipMoveThresholdX = 4;
        UINT toolTipMoveThresholdY = 4;
        SystemParametersInfoW(SPI_GETMOUSEHOVERWIDTH, 0, &toolTipMoveThresholdX, 0);
        SystemParametersInfoW(SPI_GETMOUSEHOVERHEIGHT, 0, &toolTipMoveThresholdY, 0);
        toolTipMoveThresholdX = std::clamp(toolTipMoveThresholdX, 2u, 32u);
        toolTipMoveThresholdY = std::clamp(toolTipMoveThresholdY, 2u, 32u);

        wchar_t name[64];
        for (size_t i = 0; i < accounts.size(); i++) {
            bool labelOnSide = labelPosition == LabelPosition::Left ||
                               labelPosition == LabelPosition::Right;
            bool labelBeforeBars = labelPosition == LabelPosition::Left ||
                                   labelPosition == LabelPosition::Top;
            StackPanel col;
            col.Orientation(labelOnSide ? Orientation::Horizontal : Orientation::Vertical);
            col.VerticalAlignment(VerticalAlignment::Center);
            col.Margin({(double)accountMargin, 0, (double)accountMargin, 0});
            col.Background(SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
            swprintf(name, ARRAYSIZE(name), L"AiQuota_Acc_%d", (int)i);
            col.Name(name);
            col.Visibility(accounts[i].hidden ? Visibility::Collapsed : Visibility::Visible);
            AccountUiRefs refs;
            refs.column = col;

            TextBlock label{nullptr};
            if (labelPosition != LabelPosition::Hidden) {
                label = TextBlock{};
                label.Text(accounts[i].label);
                label.FontSize(labelFontSize);
                label.VerticalAlignment(VerticalAlignment::Center);
                label.HorizontalAlignment(labelOnSide ? HorizontalAlignment::Left :
                                                         HorizontalAlignment::Center);
                if (labelPosition == LabelPosition::Left) {
                    label.Margin({0, -2, (double)labelGap, 0});
                } else if (labelPosition == LabelPosition::Right) {
                    label.Margin({(double)labelGap, -2, 0, 0});
                } else if (labelPosition == LabelPosition::Top) {
                    label.Margin({0, 0, 0, (double)labelGap});
                } else {
                    label.Margin({0, (double)labelGap, 0, 0});
                }
                label.Opacity(0.8);
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Label_%d", (int)i);
                label.Name(name);
                refs.label = label;
                if (labelBeforeBars) col.Children().Append(label);
            }

            StackPanel bars;
            bars.Orientation(verticalBars ? Orientation::Horizontal : Orientation::Vertical);
            bars.VerticalAlignment(VerticalAlignment::Center);

            double radius = std::max(1.0, barThickness / 2.0);
            for (int w = 0; w < kQuotaBarCount; w++) {
                Border track;
                track.Width(verticalBars ? barThickness : barLength);
                track.Height(verticalBars ? barLength : barThickness);
                track.CornerRadius({radius, radius, radius, radius});
                track.HorizontalAlignment(HorizontalAlignment::Center);
                track.Background(SolidColorBrush(winrt::Windows::UI::Color{0x46, 0x80, 0x80, 0x80}));
                refs.tracks[w] = track;

                Border fill;
                fill.Height(verticalBars ? 0 : barThickness);
                fill.Width(verticalBars ? barThickness : 0);
                fill.CornerRadius({radius, radius, radius, radius});
                fill.HorizontalAlignment(verticalBars ? HorizontalAlignment::Center : HorizontalAlignment::Left);
                fill.VerticalAlignment(verticalBars ? VerticalAlignment::Bottom : VerticalAlignment::Center);
                fill.Background(SolidColorBrush(winrt::Windows::UI::Color{255, 0x9E, 0x9E, 0x9E}));
                swprintf(name, ARRAYSIZE(name), L"AiQuota_Fill_%d_%d", (int)i, w);
                fill.Name(name);
                refs.fills[w] = fill;

                Grid trackContent;
                trackContent.Children().Append(fill);

                if (showPaceTicks) {
                    double paceCrossSize = barThickness;
                    double paceLength = (paceTickStyle == PaceTickStyle::Caret ? 5 : 4) *
                                        physicalPixelDip;
                    if (paceTickStyle == PaceTickStyle::Notch) {
                        paceCrossSize = std::max(2.0, std::ceil(barThickness * 0.6));
                    } else if (paceTickStyle == PaceTickStyle::Dot) {
                        paceCrossSize = 4 * physicalPixelDip;
                    }

                    Border paceTick;
                    paceTick.Width(verticalBars ? paceCrossSize : paceLength);
                    paceTick.Height(verticalBars ? paceLength : paceCrossSize);
                    paceTick.HorizontalAlignment(
                        verticalBars ? (paceTickStyle == PaceTickStyle::Notch ?
                                            HorizontalAlignment::Left : HorizontalAlignment::Center) :
                                       HorizontalAlignment::Left);
                    paceTick.VerticalAlignment(
                        verticalBars ? VerticalAlignment::Bottom :
                                       (paceTickStyle == PaceTickStyle::Notch ?
                                            VerticalAlignment::Top : VerticalAlignment::Center));
                    paceTick.Visibility(Visibility::Collapsed);
                    paceTick.IsHitTestVisible(false);

                    auto tickColor = winrt::Windows::UI::Color{
                        0xFF, GetRValue(paceTickColor), GetGValue(paceTickColor),
                        GetBValue(paceTickColor)};
                    if (paceTickStyle == PaceTickStyle::Caret) {
                        Grid caretShape;
                        auto haloColor = winrt::Windows::UI::Color{0xC0, 0, 0, 0};
                        for (int layer = 0; layer < 2; layer++) {
                            bool core = layer == 1;
                            double capInset = (core ? 1 : 0) * physicalPixelDip;
                            double stemInset = (core ? 2 : 1) * physicalPixelDip;
                            double capDepth = std::min((core ? 1.0 : 2.0) * physicalPixelDip,
                                                       paceCrossSize / 2.0);
                            wuxs::Polygon part;
                            auto addPoint = [&](double along, double cross) {
                                part.Points().Append(verticalBars ?
                                    winrt::Windows::Foundation::Point{
                                        (float)cross, (float)along} :
                                    winrt::Windows::Foundation::Point{
                                        (float)along, (float)cross});
                            };
                            addPoint(capInset, 0);
                            addPoint(paceLength - capInset, 0);
                            addPoint(paceLength - capInset, capDepth);
                            addPoint(paceLength - stemInset, capDepth);
                            addPoint(paceLength - stemInset, paceCrossSize - capDepth);
                            addPoint(paceLength - capInset, paceCrossSize - capDepth);
                            addPoint(paceLength - capInset, paceCrossSize);
                            addPoint(capInset, paceCrossSize);
                            addPoint(capInset, paceCrossSize - capDepth);
                            addPoint(stemInset, paceCrossSize - capDepth);
                            addPoint(stemInset, capDepth);
                            addPoint(capInset, capDepth);
                            part.Fill(SolidColorBrush(core ? tickColor : haloColor));
                            caretShape.Children().Append(part);
                        }
                        paceTick.Child(caretShape);
                    } else {
                        paceTick.Background(
                            SolidColorBrush(winrt::Windows::UI::Color{0xC0, 0, 0, 0}));
                        if (paceTickStyle == PaceTickStyle::Dot) {
                            double radius = 2 * physicalPixelDip;
                            paceTick.CornerRadius({radius, radius, radius, radius});
                        }

                        Border paceCore;
                        double paceCoreCrossSize =
                            paceTickStyle == PaceTickStyle::Dot ?
                                3 * physicalPixelDip : paceCrossSize;
                        double paceCoreLength = (paceTickStyle == PaceTickStyle::Dot ? 3 : 2) *
                                                physicalPixelDip;
                        paceCore.Width(verticalBars ? paceCoreCrossSize : paceCoreLength);
                        paceCore.Height(verticalBars ? paceCoreLength : paceCoreCrossSize);
                        paceCore.HorizontalAlignment(HorizontalAlignment::Center);
                        paceCore.VerticalAlignment(VerticalAlignment::Center);
                        if (paceTickStyle == PaceTickStyle::Dot) {
                            double radius = 1.5 * physicalPixelDip;
                            paceCore.CornerRadius({radius, radius, radius, radius});
                        }
                        paceCore.Background(SolidColorBrush(tickColor));
                        paceTick.Child(paceCore);
                    }
                    refs.paceTicks[w] = paceTick;
                    trackContent.Children().Append(paceTick);
                }

                track.Child(trackContent);

                Grid barItem;
                double compactLabelFontSize = percentFontSize;
                barItem.Height(verticalBars ? barLength : barThickness);
                barItem.HorizontalAlignment(HorizontalAlignment::Center);

                if (showBarLabels) {
                    static constexpr PCWSTR kBarLabels[kQuotaBarCount] = {
                        L"5h", L"7d", L"Fa", L"Ex"};
                    ColumnDefinition labelColumn;
                    labelColumn.Width({std::ceil(compactLabelFontSize * 1.4) + 3.0,
                                       GridUnitType::Pixel});
                    ColumnDefinition trackColumn;
                    trackColumn.Width({(double)(verticalBars ? barThickness : barLength),
                                       GridUnitType::Pixel});
                    barItem.ColumnDefinitions().Append(labelColumn);
                    barItem.ColumnDefinitions().Append(trackColumn);

                    TextBlock barLabel;
                    barLabel.Text(w == kExtraUsageBar && accounts[i].provider == L"openai" ?
                                      L"Cr" : kBarLabels[w]);
                    barLabel.FontSize(compactLabelFontSize);
                    barLabel.Opacity(0.8);
                    barLabel.IsHitTestVisible(false);
                    barLabel.TextAlignment(TextAlignment::Right);
                    barLabel.VerticalAlignment(VerticalAlignment::Center);
                    barLabel.Margin({0, -1, 3, 0});
                    barItem.Children().Append(barLabel);
                    Grid::SetColumn(track, 1);
                } else {
                    barItem.Width(verticalBars ? barThickness : barLength);
                }

                barItem.Children().Append(track);
                refs.barItems[w] = barItem;

                if (percentTextVisibility != PercentTextVisibility::Never) {
                    TextBlock percent;
                    percent.FontSize(percentFontSize);
                    percent.Width(verticalBars ?
                        std::max((double)barThickness, percentFontSize * 4.0) :
                        (double)barLength);
                    percent.VerticalAlignment(VerticalAlignment::Center);
                    if (percentTextAlignment == PercentTextAlignment::Left) {
                        percent.HorizontalAlignment(HorizontalAlignment::Left);
                        percent.TextAlignment(TextAlignment::Left);
                    } else if (percentTextAlignment == PercentTextAlignment::Center) {
                        percent.HorizontalAlignment(HorizontalAlignment::Center);
                        percent.TextAlignment(TextAlignment::Center);
                    } else {
                        // Adaptive starts on the unfilled right side.
                        percent.HorizontalAlignment(HorizontalAlignment::Right);
                        percent.TextAlignment(TextAlignment::Right);
                    }
                    percent.Foreground(SolidColorBrush(
                        winrt::Windows::UI::Color{255, 255, 255, 255}));
                    percent.Opacity(percentTextVisibility == PercentTextVisibility::Always ?
                                        0.9 : 0.0);
                    percent.IsHitTestVisible(false);
                    TranslateTransform translation;
                    translation.X(percentTextAlignment == PercentTextAlignment::Left ?
                                      4 * physicalPixelDip :
                                  percentTextAlignment == PercentTextAlignment::Center ?
                                      0 : -4 * physicalPixelDip);
                    translation.Y(-1);
                    percent.RenderTransform(translation);
                    swprintf(name, ARRAYSIZE(name), L"AiQuota_Percent_%d_%d", (int)i, w);
                    percent.Name(name);
                    refs.percents[w] = percent;
                    if (showBarLabels) Grid::SetColumn(percent, 1);
                    barItem.Children().Append(percent);
                }

                bars.Children().Append(barItem);
            }

            refs.barArea = bars.as<FrameworkElement>();
            col.Children().Append(bars);
            if (label && !labelBeforeBars) col.Children().Append(label);

            ToolTip toolTip;
            toolTip.Placement(wuxcp::PlacementMode::Top);
            toolTip.VerticalOffset(20);
            toolTip.Padding(Thickness{10, 8, 10, 8});
            toolTip.Background(SolidColorBrush(winrt::Windows::UI::Color{0xF7, 0x1F, 0x1F, 0x1F}));
            toolTip.Foreground(SolidColorBrush(winrt::Windows::UI::Color{255, 0xF3, 0xF3, 0xF3}));
            toolTip.BorderThickness(Thickness{1, 1, 1, 1});
            toolTip.IsHitTestVisible(false);
            UpdateQuotaToolTip(toolTip, L"loading...", false);
            ToolTipService::SetToolTip(col, toolTip);
            refs.toolTip = toolTip;
            ToolTipService::SetPlacement(col, wuxcp::PlacementMode::Top);
            UIElement tappedElement = col.as<UIElement>();
            QuotaUiInstance* statePtr = &state;
            int accountIndex = (int)i;
            uint64_t accountIdentity = AccountIdentityHash(accounts[i]);
            refs.toolTipOpenedToken = toolTip.Opened(
                [hWnd = state.hWnd, accountIndex](winrt::Windows::Foundation::IInspectable const&,
                                                  RoutedEventArgs const&) {
                    if (g_unloading) return;

                    try {
                        auto* uiState = FindUiState(hWnd);
                        if (!uiState || accountIndex >= (int)uiState->accountRefs.size()) return;

                        auto& refs = uiState->accountRefs[accountIndex];
                        refs.hasToolTipOpenCursor = GetCursorPos(&refs.toolTipOpenCursor) != FALSE;
                    } catch (...) {}
                });
            DispatcherTimer manualToolTipTimer;
            manualToolTipTimer.Interval(manualToolTipHoverDelay);
            auto manualToolTipTimerToken = manualToolTipTimer.Tick(
                [hWnd = state.hWnd, accountIndex, manualToolTipShowDuration](
                    winrt::Windows::Foundation::IInspectable const& sender,
                    winrt::Windows::Foundation::IInspectable const&) {
                    try {
                        if (auto timer = sender.try_as<DispatcherTimer>()) timer.Stop();
                        if (g_unloading) return;

                        auto* uiState = FindUiState(hWnd);
                        if (!uiState || !uiState->quotaGrid ||
                            accountIndex >= (int)uiState->accountRefs.size()) return;

                        auto& refs = uiState->accountRefs[accountIndex];
                        if (!refs.toolTip) return;
                        if (refs.manualToolTipOpen) {
                            refs.manualToolTipOpen = false;
                            refs.reopenToolTipOnMove = true;
                            refs.toolTip.IsOpen(false);
                            return;
                        }
                        if (!refs.reopenToolTipOnMove) return;

                        refs.hasToolTipOpenCursor =
                            GetCursorPos(&refs.toolTipOpenCursor) != FALSE;
                        refs.toolTip.IsOpen(true);
                        refs.reopenToolTipOnMove = false;
                        refs.manualToolTipOpen = true;
                        if (refs.manualToolTipTimer) {
                            refs.manualToolTipTimer.Interval(manualToolTipShowDuration);
                            refs.manualToolTipTimer.Start();
                        }
                    } catch (...) {}
                });
            refs.manualToolTipTimer = manualToolTipTimer;
            refs.manualToolTipTimerToken = manualToolTipTimerToken;
            bool hasDashboard = !visualTestMode && accounts[i].provider != L"antigravity";
            auto tappedToken = tappedElement.Tapped(
                [statePtr, accountIndex, accountIdentity, clickAction, hasDashboard,
                  manualToolTipHoverDelay, visualTestMode](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::TappedRoutedEventArgs const& e) {
                    if (g_unloading || !statePtr->quotaGrid) {
                        e.Handled(true);
                        return;
                    }
                    if (visualTestMode) {
                        OpenSettingsWindow();
                        e.Handled(true);
                        return;
                    }

                    bool accountFound = false;
                    bool needsLogin = false;
                    {
                        std::lock_guard<std::mutex> lk(g_settingsMutex);
                        for (size_t i = 0; i < g_settings.accounts.size(); i++) {
                            if (AccountIdentityHash(g_settings.accounts[i]) != accountIdentity) continue;
                            accountFound = true;
                            std::lock_guard<std::mutex> lk2(g_dataMutex);
                            if (i < g_data.size()) needsLogin = g_data[i].needsLogin;
                            break;
                        }
                    }
                    if (!accountFound) {
                        e.Handled(true);
                        return;
                    }
                    if (needsLogin) StartLoginByIdentity(accountIdentity);
                    else if (clickAction == ClickAction::OpenDashboard && hasDashboard) {
                        OpenDashboardForIdentity(accountIdentity);
                    }
                    else {
                        RefreshQuotaByIdentity(accountIdentity);
                        try {
                            if (e.PointerDeviceType() ==
                                    winrt::Windows::Devices::Input::PointerDeviceType::Mouse &&
                                accountIndex < (int)statePtr->accountRefs.size()) {
                                auto& refs = statePtr->accountRefs[accountIndex];
                                if (refs.manualToolTipTimer) refs.manualToolTipTimer.Stop();
                                if (refs.manualToolTipOpen) {
                                    refs.manualToolTipOpen = false;
                                    if (refs.toolTip) refs.toolTip.IsOpen(false);
                                }
                                refs.hasToolTipOpenCursor =
                                    GetCursorPos(&refs.toolTipOpenCursor) != FALSE;
                                refs.reopenToolTipOnMove = true;
                                if (refs.manualToolTipTimer) {
                                    refs.manualToolTipTimer.Interval(manualToolTipHoverDelay);
                                    refs.manualToolTipTimer.Start();
                                }
                            }
                        } catch (...) {}
                    }
                    e.Handled(true);
                });

            auto pointerEnteredToken = tappedElement.PointerEntered(
                [statePtr, accountIndex, percentTextVisibility](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::PointerRoutedEventArgs const&) {
                    if (g_unloading || percentTextVisibility != PercentTextVisibility::Hover ||
                        accountIndex >= (int)statePtr->accountRefs.size()) return;

                    try {
                        for (auto& percent : statePtr->accountRefs[accountIndex].percents) {
                            if (percent) percent.Opacity(0.9);
                        }
                    } catch (...) {}
                });

            // System XAML suppresses the automatic tooltip after a click until pointer re-entry.
            // A short stationary hover reopens it; further movement dismisses it.
            auto pointerMovedToken = tappedElement.PointerMoved(
                [statePtr, accountIndex, manualToolTipHoverDelay,
                 toolTipMoveThresholdX, toolTipMoveThresholdY](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::PointerRoutedEventArgs const& e) {
                    if (g_unloading || !statePtr->quotaGrid ||
                        accountIndex >= (int)statePtr->accountRefs.size()) return;

                    try {
                        if (e.Pointer().PointerDeviceType() !=
                            winrt::Windows::Devices::Input::PointerDeviceType::Mouse) return;

                        auto& refs = statePtr->accountRefs[accountIndex];
                        if (!refs.toolTip) return;

                        bool wasOpen = refs.toolTip.IsOpen();
                        POINT cursor{};
                        bool haveCursor = GetCursorPos(&cursor) != FALSE;
                        bool trackingHover = wasOpen || refs.reopenToolTipOnMove ||
                                             refs.manualToolTipOpen;
                        if (trackingHover && refs.hasToolTipOpenCursor && haveCursor &&
                            std::abs(cursor.x - refs.toolTipOpenCursor.x) < toolTipMoveThresholdX &&
                            std::abs(cursor.y - refs.toolTipOpenCursor.y) < toolTipMoveThresholdY) {
                            return;
                        }
                        if (refs.manualToolTipTimer) refs.manualToolTipTimer.Stop();
                        bool rearmAfterMove = wasOpen || refs.reopenToolTipOnMove ||
                                              refs.manualToolTipOpen;
                        refs.manualToolTipOpen = false;
                        refs.hasToolTipOpenCursor = false;
                        if (wasOpen) {
                            // Automatic and manual tooltips both dismiss on local pointer movement.
                            refs.toolTip.IsOpen(false);
                        }
                        if (!rearmAfterMove) return;

                        refs.reopenToolTipOnMove = true;
                        refs.hasToolTipOpenCursor = haveCursor;
                        if (haveCursor) refs.toolTipOpenCursor = cursor;
                        if (refs.manualToolTipTimer) {
                            refs.manualToolTipTimer.Interval(manualToolTipHoverDelay);
                            refs.manualToolTipTimer.Start();
                        }
                    } catch (...) {}
                });

            auto closeToolTip =
                [statePtr, accountIndex](winrt::Windows::Foundation::IInspectable const&,
                                         wuxi::PointerRoutedEventArgs const&) {
                    if (g_unloading || accountIndex >= (int)statePtr->accountRefs.size()) return;

                    auto& refs = statePtr->accountRefs[accountIndex];
                    refs.hasToolTipOpenCursor = false;
                    refs.reopenToolTipOnMove = false;
                    try {
                        if (refs.manualToolTipTimer) refs.manualToolTipTimer.Stop();
                    } catch (...) {}

                    refs.manualToolTipOpen = false;
                    try {
                        if (refs.toolTip && refs.toolTip.IsOpen()) refs.toolTip.IsOpen(false);
                    } catch (...) {}
                };
            auto pointerExitedToken = tappedElement.PointerExited(
                [closeToolTip, statePtr, accountIndex, percentTextVisibility](
                    winrt::Windows::Foundation::IInspectable const& sender,
                    wuxi::PointerRoutedEventArgs const& e) {
                    closeToolTip(sender, e);
                    if (g_unloading || percentTextVisibility != PercentTextVisibility::Hover ||
                        accountIndex >= (int)statePtr->accountRefs.size()) return;

                    try {
                        for (auto& percent : statePtr->accountRefs[accountIndex].percents) {
                            if (percent) percent.Opacity(0.0);
                        }
                    } catch (...) {}
                });
            auto pointerCaptureLostToken = tappedElement.PointerCaptureLost(closeToolTip);
            auto pointerCanceledToken = tappedElement.PointerCanceled(closeToolTip);
            state.pointerHandlers.push_back({tappedElement, tappedToken, pointerEnteredToken,
                                             pointerMovedToken, pointerExitedToken,
                                             pointerCaptureLostToken, pointerCanceledToken});

            MenuFlyout menu;
            if (visualTestMode) {
                MenuFlyoutItem exitTestItem;
                exitTestItem.Text(L"Exit visual test");
                auto exitTestToken = exitTestItem.Click(
                    [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                        HWND settingsWindow = g_settingsWindow.load();
                        if (settingsWindow &&
                            PostMessageW(settingsWindow, kExitVisualTestMessage, 0, 0)) {
                            return;
                        }

                        // Avoid tearing down this grid while its XAML Click handler is running.
                        if (g_visualTestMode.exchange(false, std::memory_order_acq_rel)) {
                            StartRetryInject(true);
                        }
                    });
                state.menuItemClickHandlers.push_back({exitTestItem, exitTestToken});
                menu.Items().Append(exitTestItem);
            } else {
                MenuFlyoutItem refreshAllItem;
                refreshAllItem.Text(L"Refresh all");
                auto refreshAllToken = refreshAllItem.Click(
                    [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                        QueueRefresh(0);
                    });
                state.menuItemClickHandlers.push_back({refreshAllItem, refreshAllToken});
                menu.Items().Append(refreshAllItem);
            }

            if (hasDashboard) {
                MenuFlyoutItem dashboardItem;
                dashboardItem.Text(L"Open dashboard");
                auto dashboardToken = dashboardItem.Click(
                    [accountIdentity](winrt::Windows::Foundation::IInspectable const&,
                                   RoutedEventArgs const&) {
                        OpenDashboardForIdentity(accountIdentity);
                    });
                state.menuItemClickHandlers.push_back({dashboardItem, dashboardToken});
                menu.Items().Append(dashboardItem);
            }

            if (!visualTestMode) {
                // Per-account show/hide checkboxes (checked = visible). Every column carries the
                // same list; toggling flips global state, persists, and re-syncs all instances.
                menu.Items().Append(MenuFlyoutSeparator{});
                for (size_t k = 0; k < accounts.size(); k++) {
                    ToggleMenuFlyoutItem toggle;
                    toggle.Text(accounts[k].label + L" - " +
                                ProviderDisplayName(accounts[k].provider));
                    toggle.IsChecked(!accounts[k].hidden);
                    int toggleIndex = (int)k;
                    uint64_t toggleIdentity = AccountIdentityHash(accounts[k]);
                    auto toggleToken = toggle.Click(
                        [toggleIdentity](winrt::Windows::Foundation::IInspectable const& sender,
                                         RoutedEventArgs const&) {
                            ToggleAccountVisibility(toggleIdentity, sender);
                        });
                    state.menuItemClickHandlers.push_back({toggle, toggleToken});
                    state.accountToggleItems.push_back({toggleIndex, toggle});
                    menu.Items().Append(toggle);
                }
            }

            // Sign in / Sign out submenus drive the mod's own OAuth per account.
            if (!visualTestMode) {
                MenuFlyoutSubItem signInSub;
                signInSub.Text(L"Sign in");
                MenuFlyoutSubItem signOutSub;
                signOutSub.Text(L"Sign out");
                for (size_t k = 0; k < accounts.size(); k++) {
                    if (accounts[k].provider == L"antigravity") continue;
                    std::wstring name = accounts[k].label + L" - " +
                                        ProviderDisplayName(accounts[k].provider);
                    uint64_t authIdentity = AccountIdentityHash(accounts[k]);

                    MenuFlyoutItem signInItem;
                    signInItem.Text(name);
                    auto signInToken = signInItem.Click(
                        [authIdentity](winrt::Windows::Foundation::IInspectable const&,
                                       RoutedEventArgs const&) {
                            StartLoginByIdentity(authIdentity);
                        });
                    state.menuItemClickHandlers.push_back({signInItem, signInToken});
                    signInSub.Items().Append(signInItem);

                    MenuFlyoutItem signOutItem;
                    signOutItem.Text(name);
                    auto signOutToken = signOutItem.Click(
                        [authIdentity](winrt::Windows::Foundation::IInspectable const&,
                                       RoutedEventArgs const&) {
                            if (!SignOutAccountByIdentity(authIdentity)) OpenSettingsWindow();
                        });
                    state.menuItemClickHandlers.push_back({signOutItem, signOutToken});
                    signOutSub.Items().Append(signOutItem);
                }
                if (signInSub.Items().Size() > 0) {
                    menu.Items().Append(MenuFlyoutSeparator{});
                    menu.Items().Append(signInSub);
                    menu.Items().Append(signOutSub);
                }
            }

            menu.Items().Append(MenuFlyoutSeparator{});
            MenuFlyoutItem settingsItem;
            settingsItem.Text(L"Settings...");
            auto settingsToken = settingsItem.Click(
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenSettingsWindow();
                });
            state.menuItemClickHandlers.push_back({settingsItem, settingsToken});

            MenuFlyoutItem historyItem;
            historyItem.Text(L"Usage history...");
            auto historyToken = historyItem.Click(
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenHistoryWindow();
                });
            state.menuItemClickHandlers.push_back({historyItem, historyToken});
            menu.Items().Append(historyItem);
            menu.Items().Append(settingsItem);

            tappedElement.ContextFlyout(menu);

            panel.Children().Append(col);
            state.accountRefs.push_back(std::move(refs));
        }

        root.Children().Append(panel);
        if (showPaceTicks) {
            DispatcherTimer paceTimer;
            paceTimer.Interval(std::chrono::minutes(1));
            auto paceTimerToken = paceTimer.Tick(
                [hWnd = state.hWnd](winrt::Windows::Foundation::IInspectable const&,
                                     winrt::Windows::Foundation::IInspectable const&) {
                    if (g_unloading) return;
                    try {
                        auto* uiState = FindUiState(hWnd);
                        if (uiState && uiState->quotaGrid) UpdateQuotaUi(*uiState);
                    } catch (...) {}
                });
            state.paceTimer = paceTimer;
            state.paceTimerToken = paceTimerToken;
            paceTimer.Start();
        }
        return root;
    } catch (...) {
        ClearQuotaEventState(state);
        Wh_Log(L"BuildQuotaGrid: exception");
        return nullptr;
    }
}
