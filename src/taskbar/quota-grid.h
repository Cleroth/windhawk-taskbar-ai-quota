// Builds the per-taskbar quota grid, including visual-test sample data.

static void BuildVisualTestSnapshot(int yellowThreshold, int orangeThreshold,
                                    int redThreshold, ULONGLONG now,
                                    std::vector<AccountConfig>* accounts,
                                    std::vector<AccountData>* data) {
    // One value per usage color band.
    const double green = yellowThreshold / 2.0;
    const double yellow = (yellowThreshold + orangeThreshold) / 2.0;
    const double orange = (orangeThreshold + redThreshold) / 2.0;
    const double red = (redThreshold + 100) / 2.0;
    // One to four taskbar quotas, each from slots its provider reports; two Anthropic accounts
    // share a provider so tiles show number badges. A negative value is a quota not reported.
    struct Sample {
        PCWSTR provider;
        PCWSTR label;
        std::array<double, kQuotaBarCount> pct;
        std::array<bool, kQuotaBarCount> showBars;
    };
    const std::array<Sample, 4> samples = {{
        {L"openai", L"OAI", {green, yellow, -1, green / 2}, {true, true, false, true}},
        {L"anthropic", L"CC 1", {orange, green, -1, -1}, {true, true, false, false}},
        {L"anthropic", L"CC 2", {green / 2, red, yellow, green}, {true, true, true, true}},
        {L"antigravity", L"Go", {green, green * 1.5, -1, -1}, {true, false, false, false}},
    }};
    static constexpr std::array<ULONGLONG, kQuotaBarCount> kDurations = {
        kFiveHourWindowMs,
        kWeeklyWindowMs,
        kWeeklyWindowMs,
        30ULL * 24 * 60 * 60 * 1000,
    };
    static constexpr std::array<double, kQuotaBarCount> kRemainingFractions = {
        0.35, 0.5, 0.6, 0.8,
    };
    static constexpr double kCreditsMax = 1000;

    accounts->clear();
    accounts->reserve(samples.size());
    if (data) {
        data->clear();
        data->reserve(samples.size());
    }
    for (size_t i = 0; i < samples.size(); i++) {
        const Sample& sample = samples[i];
        AccountConfig account;
        account.provider = sample.provider;
        account.label = sample.label;
        account.showBars = sample.showBars;
        bool openAi = account.provider == L"openai";
        if (openAi) account.creditsMax = (int)kCreditsMax;
        accounts->push_back(account);

        if (!data) continue;
        AccountData accountData;
        std::array<WindowUsage*, kQuotaBarCount> usage = {
            &accountData.win5h, &accountData.winWeek,
            &accountData.fableWeek, &accountData.extraUsage,
        };
        for (int w = 0; w < kQuotaBarCount; w++) {
            if (sample.pct[w] < 0) continue;
            usage[w]->pct = sample.pct[w];
            usage[w]->windowDurationMs = kDurations[w];
            usage[w]->resetUnixMs = now +
                (ULONGLONG)std::lround(kDurations[w] * (kRemainingFractions[w] - 0.05 * i));
        }
        if (openAi) {
            // Credits are a balance against the user's max, without a window.
            accountData.hasCredits = true;
            accountData.creditsBalance = kCreditsMax * (1 - sample.pct[kExtraUsageBar] / 100);
            ApplyCreditsMax(account, &accountData);
        } else if (sample.pct[kExtraUsageBar] >= 0) {
            accountData.extraUsedAmount = sample.pct[kExtraUsageBar] / 2.0;
            accountData.extraLimitAmount = 50.0;
        }
        if (account.provider == L"antigravity") {
            // Claude/GPT pools appear only in the details flyout.
            accountData.antigravityThirdParty5h = {
                yellow, now + (ULONGLONG)std::lround(kFiveHourWindowMs * 0.4), kFiveHourWindowMs};
            accountData.antigravityThirdPartyWeek = {
                green, now + (ULONGLONG)std::lround(kWeeklyWindowMs * 0.6), kWeeklyWindowMs};
        }
        accountData.plan = L"Visual test";
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
        bool showPaceTicks, showBarLabels, showDetailsOnHover;
        bool visualTestMode = g_visualTestMode.load(std::memory_order_acquire);
        COLORREF paceTickColor;
        BarLayout barLayout;
        PercentTextVisibility percentTextVisibility;
        PercentTextAlignment percentTextAlignment;
        PaceTickStyle paceTickStyle;
        LabelPosition labelPosition;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            state.buildSettingsGeneration = g_settingsGeneration;
            accounts = g_settings.accounts;
            showDetailsOnHover = g_settings.showDetailsOnHover;
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
        state.buildBarLayout = barLayout;
        bool tileLayout = IsTileLayout(barLayout);
        // Tiles are kTileHeight tall, less on a taskbar too short for that.
        state.tileHeight = kTileHeight;
        if (tileLayout && state.injectionParent) {
            try {
                if (auto xamlRoot = state.injectionParent.XamlRoot()) {
                    double taskbarHeight = xamlRoot.Size().Height;
                    if (taskbarHeight > 0) {
                        state.tileHeight =
                            std::clamp(std::floor(taskbarHeight) - 8, 24.0, kTileHeight);
                    }
                }
            } catch (...) {}
        }
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

        // Context menu items with a Fluent icon; ClearQuotaEventState revokes their Click handlers.
        FontFamily iconFont(L"Segoe Fluent Icons, Segoe MDL2 Assets");
        auto menuIcon = [&](PCWSTR glyph) {
            FontIcon icon;
            icon.FontFamily(iconFont);
            icon.Glyph(glyph);
            return icon;
        };
        auto menuItem = [&](PCWSTR text, PCWSTR glyph, auto const& onClick) {
            MenuFlyoutItem item;
            item.Text(text);
            item.Icon(menuIcon(glyph));
            auto token = item.Click(onClick);
            state.menuItemClickHandlers.push_back({item, token});
            return item;
        };

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
            menu.Items().Append(menuItem(L"Usage history", L"\uE81C",
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenHistoryWindow();
                }));
            menu.Items().Append(menuItem(L"Settings", L"\uE713",
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenSettingsWindow();
                }));
            setupElement.ContextFlyout(menu);

            panel.Children().Append(setupTile);
            root.Children().Append(panel);
            return root;
        }
        state.accountRefs.reserve(accounts.size());
        bool verticalBars = barLayout == BarLayout::Vertical;

        // Context menu account header: icon, bold label, then provider and plan. Unlike the
        // standard item template, it neither highlights nor dims while disabled; its margin,
        // padding and icon column match the standard (WinUI 2) items. Without it, headers fall
        // back to dimmed standard items.
        ControlTemplate menuHeaderTemplate{nullptr};
        if (!visualTestMode) {
            try {
                menuHeaderTemplate = winrt::Windows::UI::Xaml::Markup::XamlReader::Load(
                    L"<ControlTemplate "
                    L"xmlns=\"http://schemas.microsoft.com/winfx/2006/xaml/presentation\" "
                    L"TargetType=\"MenuFlyoutItem\">"
                    L"<Grid Margin=\"4,2,4,2\" Padding=\"11,4,11,5\" Background=\"Transparent\">"
                    L"<Grid.ColumnDefinitions><ColumnDefinition Width=\"28\"/>"
                    L"<ColumnDefinition Width=\"Auto\"/><ColumnDefinition Width=\"*\"/>"
                    L"</Grid.ColumnDefinitions>"
                    L"<Viewbox Width=\"16\" Height=\"16\" HorizontalAlignment=\"Left\" "
                    L"VerticalAlignment=\"Center\"><ContentPresenter "
                    L"Content=\"{TemplateBinding Icon}\" Foreground=\"{TemplateBinding Foreground}\"/>"
                    L"</Viewbox>"
                    L"<TextBlock Grid.Column=\"1\" Text=\"{TemplateBinding Text}\" "
                    L"Foreground=\"{TemplateBinding Foreground}\" FontWeight=\"SemiBold\" "
                    L"VerticalAlignment=\"Center\"/>"
                    L"<TextBlock Grid.Column=\"2\" "
                    L"Text=\"{TemplateBinding KeyboardAcceleratorTextOverride}\" "
                    L"Foreground=\"{TemplateBinding Foreground}\" Opacity=\"0.6\" Margin=\"10,0,0,0\" "
                    L"TextTrimming=\"CharacterEllipsis\" VerticalAlignment=\"Center\"/>"
                    L"</Grid></ControlTemplate>").as<ControlTemplate>();
            } catch (...) {
                Wh_Log(L"Menu header template failed to load");
            }
        }

        wchar_t name[64];
        for (size_t i = 0; i < accounts.size(); i++) {
            AccountUiRefs refs;
            refs.provider = accounts[i].provider;
            refs.identity = AccountIdentityHash(accounts[i]);
            refs.historyId = accounts[i].historyId;
            StackPanel col{nullptr};
            if (tileLayout) {
                col = BuildAccountTile(accounts, i, barLayout, state.tileHeight, refs);
            } else {
                bool labelOnSide = labelPosition == LabelPosition::Left ||
                                   labelPosition == LabelPosition::Right;
                bool labelBeforeBars = labelPosition == LabelPosition::Left ||
                                       labelPosition == LabelPosition::Top;
                col = StackPanel{};
                col.Orientation(labelOnSide ? Orientation::Horizontal : Orientation::Vertical);
                col.VerticalAlignment(VerticalAlignment::Center);

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
                                          L"Cr" : kQuotaKeys[w]);
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
            }

            col.Margin({(double)accountMargin, 0, (double)accountMargin, 0});
            col.Background(SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
            swprintf(name, ARRAYSIZE(name), L"AiQuota_Acc_%d", (int)i);
            col.Name(name);
            col.Visibility(accounts[i].hidden ? Visibility::Collapsed : Visibility::Visible);
            refs.column = col;

            UIElement tappedElement = col.as<UIElement>();
            QuotaUiInstance* statePtr = &state;
            int accountIndex = (int)i;
            uint64_t accountIdentity = refs.identity;
            auto tappedToken = tappedElement.Tapped(
                [statePtr, accountIndex, accountIdentity, visualTestMode](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::TappedRoutedEventArgs const& e) {
                    e.Handled(true);
                    if (g_unloading || !statePtr->quotaGrid) return;
                    try {
                        OnTileTapped(*statePtr, accountIndex, accountIdentity, visualTestMode);
                    } catch (...) {}
                });

            auto pointerEnteredToken = tappedElement.PointerEntered(
                [statePtr, accountIndex, percentTextVisibility, showDetailsOnHover](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::PointerRoutedEventArgs const& e) {
                    if (g_unloading || !statePtr->quotaGrid ||
                        accountIndex >= (int)statePtr->accountRefs.size()) return;

                    try {
                        if (percentTextVisibility == PercentTextVisibility::Hover) {
                            for (auto& percent : statePtr->accountRefs[accountIndex].percents) {
                                if (percent) percent.Opacity(0.9);
                            }
                        }
                        if (e.Pointer().PointerDeviceType() ==
                            winrt::Windows::Devices::Input::PointerDeviceType::Mouse) {
                            int previousHover = statePtr->flyout.hoverIndex;
                            OnTilePointerEntered(*statePtr, accountIndex, showDetailsOnHover);
                            if (previousHover != accountIndex) {
                                UpdateTileHighlight(*statePtr, previousHover);
                            }
                            UpdateTileHighlight(*statePtr, accountIndex);
                        }
                    } catch (...) {}
                });

            // Lets a click recognize that its own press light-dismissed this tile's flyout.
            auto pointerPressedToken = tappedElement.PointerPressed(
                [statePtr, accountIndex](winrt::Windows::Foundation::IInspectable const&,
                                         wuxi::PointerRoutedEventArgs const&) {
                    statePtr->flyout.pressIndex = accountIndex;
                    statePtr->flyout.pressTick = GetTickCount64();
                });

            auto pointerExited =
                [statePtr, accountIndex, percentTextVisibility](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::PointerRoutedEventArgs const&) {
                    if (g_unloading || accountIndex >= (int)statePtr->accountRefs.size()) return;

                    try {
                        if (percentTextVisibility == PercentTextVisibility::Hover) {
                            for (auto& percent : statePtr->accountRefs[accountIndex].percents) {
                                if (percent) percent.Opacity(0.0);
                            }
                        }
                        OnTilePointerExited(*statePtr, accountIndex);
                        UpdateTileHighlight(*statePtr, accountIndex);
                    } catch (...) {}
                };
            auto pointerExitedToken = tappedElement.PointerExited(pointerExited);
            auto pointerCanceledToken = tappedElement.PointerCanceled(pointerExited);
            state.pointerHandlers.push_back({tappedElement, tappedToken, pointerEnteredToken,
                                             pointerPressedToken, pointerExitedToken,
                                             pointerCanceledToken});

            // Context menu: this account's header and actions, then every account's, then
            // Settings. Visual tests offer only their exit and Settings.
            bool antigravity = accounts[i].provider == L"antigravity";
            MenuFlyout menu;
            MenuFlyoutItem menuHeader{nullptr};
            MenuFlyoutItem signItem{nullptr};
            if (visualTestMode) {
                menu.Items().Append(menuItem(L"Exit visual test", L"\uE711",
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
                    }));
            } else {
                // Non-interactive; the menu's Opening handler adds the plan to the provider.
                menuHeader = MenuFlyoutItem{};
                menuHeader.Text(accounts[i].label);
                menuHeader.KeyboardAcceleratorTextOverride(
                    ProviderDisplayName(accounts[i].provider));
                menuHeader.IsEnabled(false);
                if (menuHeaderTemplate) menuHeader.Template(menuHeaderTemplate);
                try {
                    menuHeader.Icon(LoadProviderLogoShape(accounts[i].provider, nullptr)
                                        .as<IconElement>());
                } catch (...) {
                    Wh_Log(L"Menu header logo failed to load");
                }
                menu.Items().Append(menuHeader);

                menu.Items().Append(menuItem(L"Refresh", L"\uE72C",
                    [accountIdentity](winrt::Windows::Foundation::IInspectable const&,
                                      RoutedEventArgs const&) {
                        RefreshQuotaByIdentity(accountIdentity);
                    }));
                if (!antigravity) {
                    menu.Items().Append(menuItem(L"Open dashboard", L"\uE8A7",
                        [accountIdentity](winrt::Windows::Foundation::IInspectable const&,
                                          RoutedEventArgs const&) {
                            OpenDashboardForIdentity(accountIdentity);
                        }));
                }
                uint64_t historyId = accounts[i].historyId;
                menu.Items().Append(menuItem(L"Usage history", L"\uE81C",
                    [historyId](winrt::Windows::Foundation::IInspectable const&,
                                RoutedEventArgs const&) {
                        OpenHistoryWindow(historyId);
                    }));
                // Sign in or out with the mod's own OAuth. The Opening handler picks the label,
                // which decides the action.
                if (!antigravity) {
                    signItem = menuItem(L"Sign out", L"\uF3B1",
                        [accountIdentity](winrt::Windows::Foundation::IInspectable const& sender,
                                          RoutedEventArgs const&) {
                            if (g_unloading) return;
                            bool signIn = false;
                            try {
                                auto item = sender.try_as<MenuFlyoutItem>();
                                if (!item) return;
                                signIn = item.Text() == L"Sign in";
                            } catch (...) {
                                return;
                            }
                            if (signIn) StartLoginByIdentity(accountIdentity);
                            else if (!SignOutAccountByIdentity(accountIdentity)) OpenSettingsWindow();
                        });
                    menu.Items().Append(signItem);
                }

                menu.Items().Append(MenuFlyoutSeparator{});
                menu.Items().Append(menuItem(L"Refresh all", L"\uE895",
                    [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                        QueueRefresh(0);
                    }));

                // Per-account show/hide checkboxes (checked = visible). Every column carries the
                // same list; toggling flips global state, persists, and re-syncs all instances.
                MenuFlyoutSubItem showSub;
                showSub.Text(L"Show on taskbar");
                showSub.Icon(menuIcon(L"\uE890"));
                for (size_t k = 0; k < accounts.size(); k++) {
                    ToggleMenuFlyoutItem toggle;
                    toggle.Text(accounts[k].label + L" · " +
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
                    showSub.Items().Append(toggle);
                }
                menu.Items().Append(showSub);
            }

            menu.Items().Append(MenuFlyoutSeparator{});
            menu.Items().Append(menuItem(L"Settings", L"\uE713",
                [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                    OpenSettingsWindow();
                }));

            tappedElement.ContextFlyout(menu);

            panel.Children().Append(col);
            refs.menu = menu;
            state.accountRefs.push_back(std::move(refs));
            // Right-click closes any details flyout before the menu shows. The header's plan and
            // the sign-in choice then follow the account's current state.
            state.accountRefs.back().menuOpeningToken = menu.Opening(
                [statePtr, accountIdentity, provider = accounts[i].provider, menuHeader, signItem](
                    winrt::Windows::Foundation::IInspectable const&,
                    winrt::Windows::Foundation::IInspectable const&) {
                    if (g_unloading) return;
                    try {
                        FlyoutUi& fo = statePtr->flyout;
                        if (fo.dwellTimer) fo.dwellTimer.Stop();
                        fo.dwellIndex = -1;
                        fo.hoverSuppressedIndex = fo.hoverIndex;
                        for (auto* instance : CurrentThreadUiInstances()) {
                            CloseAccountFlyout(*instance);
                        }
                    } catch (...) {}
                    if (!menuHeader) return;
                    try {
                        std::wstring plan;
                        bool showSparkPlan = false;
                        bool needsLogin = false;
                        {
                            std::lock_guard<std::mutex> lk(g_settingsMutex);
                            showSparkPlan = g_settings.showOpenAiExtraLimits;
                            for (size_t k = 0; k < g_settings.accounts.size(); k++) {
                                if (AccountIdentityHash(g_settings.accounts[k]) != accountIdentity) {
                                    continue;
                                }
                                std::lock_guard<std::mutex> lk2(g_dataMutex);
                                if (k < g_data.size()) {
                                    plan = g_data[k].plan;
                                    needsLogin = g_data[k].needsLogin;
                                }
                                break;
                            }
                        }
                        // As in the details flyout, OpenAI's Spark plan name needs its extra
                        // limits shown.
                        if (provider == L"openai" && !showSparkPlan &&
                            (plan.find(L"Spark") != std::wstring::npos ||
                             plan.find(L"spark") != std::wstring::npos)) {
                            plan.clear();
                        }
                        std::wstring details = ProviderDisplayName(provider);
                        if (!plan.empty()) details += L" · " + plan;
                        menuHeader.KeyboardAcceleratorTextOverride(details);
                        if (signItem) {
                            signItem.Text(needsLogin ? L"Sign in" : L"Sign out");
                            if (auto icon = signItem.Icon().try_as<FontIcon>()) {
                                icon.Glyph(needsLogin ? L"\uE77B" : L"\uF3B1");
                            }
                        }
                    } catch (...) {}
                });
        }

        root.Children().Append(panel);
        // Moves pace marks each minute; tiles also pick up a missed light/dark switch with it.
        if (showPaceTicks || tileLayout) {
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
