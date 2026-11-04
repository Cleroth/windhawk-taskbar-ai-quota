// Builds the per-taskbar quota grid.

// How long the pointer rests on another menu item before an open submenu closes. System XAML
// waits MenuShowDelay (400 ms by default), which feels sluggish next to the instant open; a short
// delay still lets a diagonal move cross an item on its way into the submenu.
static constexpr int kSubmenuCloseDelayMs = 150;

// Opens or closes a menu's submenu at once through its automation peer's expand pattern. System
// XAML opens one only after the MenuShowDelay hover timeout, and not on a mouse click. Expanding
// while the menu is closed would leave an orphan submenu that nothing closes.
static void SetMenuSubItemExpanded(wuxcp::FlyoutBase const& menu, MenuFlyoutSubItem const& item,
                                   bool expanded) {
    using namespace winrt::Windows::UI::Xaml::Automation;
    try {
        if (!menu || !item || !menu.IsOpen()) return;
        auto peer = Peers::FrameworkElementAutomationPeer::CreatePeerForElement(item);
        auto expander = peer ? peer.GetPattern(Peers::PatternInterface::ExpandCollapse)
                                   .try_as<Provider::IExpandCollapseProvider>() : nullptr;
        if (!expander) return;
        ExpandCollapseState current = expander.ExpandCollapseState();
        if (expanded && current == ExpandCollapseState::Collapsed) expander.Expand();
        if (!expanded && current == ExpandCollapseState::Expanded) expander.Collapse();
    } catch (...) {}
}

static Grid BuildQuotaGrid(QuotaUiInstance& state) {
    try {
        bool visualTestMode = g_visualTestMode.load(std::memory_order_acquire);
        Settings settings;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            state.buildSettingsGeneration = g_settingsGeneration;
            settings = g_settings;
        }
        std::vector<AccountConfig>& accounts = settings.accounts;
        BarLayout barLayout = settings.barLayout;
        state.buildVisualTestMode = visualTestMode;
        state.buildBarLayout = barLayout;
        bool tileLayout = IsTileLayout(barLayout);
        // Tiles are kTileHeight tall, less on a taskbar too short for that; bar columns at least.
        state.taskbarHeight = 0;
        if (state.injectionParent) {
            try {
                if (auto xamlRoot = state.injectionParent.XamlRoot()) {
                    state.taskbarHeight = xamlRoot.Size().Height;
                }
            } catch (...) {}
        }
        MeterDisplay display = MeterDisplayFor(settings, NowUnixMs(), state.rasterizationScale,
                                               state.taskbarHeight);
        if (visualTestMode) BuildVisualTestSnapshot(display, &accounts, nullptr);
        BarGeometry barGeometry = BarGeometryFor(display);
        // Bar sizes and pace ticks as the meter model clamps them.
        int barLength = display.barLength, barThickness = display.barThickness;
        bool showPaceTicks = display.showPaceTicks;
        PaceTickStyle paceTickStyle = display.paceTickStyle;
        COLORREF paceTickColor = display.paceTickColor;
        int labelFontSize = settings.labelFontSize, percentFontSize = settings.percentFontSize;
        int accountMargin = settings.accountMargin;
        int rightMargin = settings.rightMargin;
        bool showBarLabels = settings.showBarLabels;
        bool showDetailsOnHover = settings.showDetailsOnHover;
        PercentTextVisibility percentTextVisibility = settings.percentTextVisibility;
        PercentTextAlignment percentTextAlignment = settings.percentTextAlignment;
        LabelPosition labelPosition = settings.labelPosition;
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
        // The label line's space over its cap height and under its baseline, as the labels lay
        // out: the line box less each trimmed one, out of the tree and unrounded.
        state.labelAboveCap = state.labelBelowBaseline = 0;
        if (!tileLayout) {
            try {
                TextBlock line;
                line.Text(L"A");
                line.FontSize(labelFontSize);
                line.UseLayoutRounding(false);
                auto lineHeight = [&](TextLineBounds bounds) {
                    line.TextLineBounds(bounds);
                    line.Measure({INFINITY, INFINITY});
                    return (double)line.DesiredSize().Height;
                };
                double full = lineHeight(TextLineBounds::Full);
                double belowCap = lineHeight(TextLineBounds::TrimToCapHeight);
                double aboveBaseline = lineHeight(TextLineBounds::TrimToBaseline);
                state.labelAboveCap = std::max(0.0, full - belowCap);
                state.labelBelowBaseline = std::max(0.0, full - aboveBaseline);
            } catch (...) {
                Wh_Log(L"Label line metrics failed");
            }
        }
        // Tiles, or a bar column with a logo identity.
        bool identities = tileLayout;

        // Drops a show/hide toggle's keep-open request on the next dispatcher pass if no close
        // request followed its click.
        if (!visualTestMode) {
            DispatcherTimer keepOpenTimer;
            keepOpenTimer.Interval(std::chrono::milliseconds(0));
            state.keepMenuOpenTimerToken = keepOpenTimer.Tick(
                [hWnd = state.hWnd](winrt::Windows::Foundation::IInspectable const&,
                                     winrt::Windows::Foundation::IInspectable const&) {
                    if (g_unloading) return;
                    try {
                        auto* uiState = FindUiState(hWnd);
                        if (!uiState) return;
                        if (uiState->keepMenuOpenTimer) uiState->keepMenuOpenTimer.Stop();
                        uiState->keepMenuOpenToggle = nullptr;
                    } catch (...) {}
                });
            state.keepMenuOpenTimer = keepOpenTimer;

            DispatcherTimer submenuCloseTimer;
            submenuCloseTimer.Interval(std::chrono::milliseconds(kSubmenuCloseDelayMs));
            state.submenuCloseTimerToken = submenuCloseTimer.Tick(
                [hWnd = state.hWnd](winrt::Windows::Foundation::IInspectable const&,
                                     winrt::Windows::Foundation::IInspectable const&) {
                    if (g_unloading) return;
                    try {
                        auto* uiState = FindUiState(hWnd);
                        if (!uiState) return;
                        if (uiState->submenuCloseTimer) uiState->submenuCloseTimer.Stop();
                        MenuFlyout menu = std::exchange(uiState->submenuCloseMenu, nullptr);
                        MenuFlyoutSubItem item = std::exchange(uiState->submenuCloseItem, nullptr);
                        SetMenuSubItemExpanded(menu, item, false);
                    } catch (...) {}
                });
            state.submenuCloseTimer = submenuCloseTimer;
        }

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
                col = BuildAccountTile(accounts, i, barLayout, display, refs);
            } else {
                // The tiles' box: at least the tile height, up to 7 DIPs inside each side, holding
                // the content centred across it: the identity beside, above or below the bars at
                // the identity position, centred on them, or the bars alone while it is Hidden.
                // UpdateQuotaUi sets the identity's margins.
                bool identityAcross = labelPosition == LabelPosition::Top ||
                                      labelPosition == LabelPosition::Bottom;
                bool identityBeforeBars = labelPosition == LabelPosition::Left ||
                                          labelPosition == LabelPosition::Top;
                col = StackPanel{};
                col.Orientation(Orientation::Horizontal);
                col.VerticalAlignment(VerticalAlignment::Center);
                col.MinHeight(display.tileHeight);
                col.Padding({7, 0, 7, 0});
                col.CornerRadius({4, 4, 4, 4});
                StackPanel content;
                content.Orientation(identityAcross ? Orientation::Vertical : Orientation::Horizontal);
                content.VerticalAlignment(VerticalAlignment::Center);
                refs.content = content;

                // The identity: the account label, or the logo with its badge and stale mark.
                FrameworkElement identity{nullptr};
                if (labelPosition != LabelPosition::Hidden) {
                    if (accounts[i].tileIdentity == TileIdentity::Text) {
                        TextBlock label;
                        label.Text(accounts[i].label);
                        label.FontSize(labelFontSize);
                        label.Opacity(0.8);
                        swprintf(name, ARRAYSIZE(name), L"AiQuota_Label_%d", (int)i);
                        label.Name(name);
                        refs.label = label;
                        identity = label;
                    } else {
                        identity = BuildTileIdentity(accounts, i, barLayout, display, refs.tile);
                        identities = true;
                    }
                    identity.VerticalAlignment(VerticalAlignment::Center);
                    identity.HorizontalAlignment(identityAcross ? HorizontalAlignment::Center :
                                                                  HorizontalAlignment::Left);
                    if (identityBeforeBars) content.Children().Append(identity);
                }

                StackPanel bars;
                bars.Orientation(verticalBars ? Orientation::Horizontal : Orientation::Vertical);
                bars.VerticalAlignment(VerticalAlignment::Center);
                bars.HorizontalAlignment(HorizontalAlignment::Center);

                // Each bar's cell holds its track (or thin rail), fill and pace tick, centred across
                // it. Ticks reach past the bar through negative margins, which keep them unclipped.
                // A time track's small bar takes a row under it (vertical bars: a column right of
                // it).
                double radius = barGeometry.radius;
                double rail = barGeometry.rail;
                double timeSize = barGeometry.timeGap + barGeometry.timeThickness;
                bool timeTrack = timeSize > 0;
                if (showPaceTicks) {
                    PaceMarkColors colors = PaceMarkColorsFor(paceTickColor, state.lightTheme);
                    refs.paceCore = SolidColorBrush(ArgbColor(colors.core));
                    refs.paceHalo = SolidColorBrush(ArgbColor(colors.halo));
                }
                for (int w = 0; w < kQuotaBarCount; w++) {
                    Grid barItem;
                    double compactLabelFontSize = percentFontSize;
                    barItem.Height(verticalBars ? barLength : barThickness + timeSize);
                    barItem.HorizontalAlignment(HorizontalAlignment::Center);
                    int barColumn = 0;
                    if (timeTrack && !verticalBars) {
                        for (double height : {(double)barThickness, timeSize}) {
                            RowDefinition row;
                            row.Height({height, GridUnitType::Pixel});
                            barItem.RowDefinitions().Append(row);
                        }
                    }

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
                        barLabel.Text(QuotaKey(accounts[i].provider, w));
                        barLabel.FontSize(compactLabelFontSize);
                        barLabel.Opacity(0.8);
                        barLabel.IsHitTestVisible(false);
                        barLabel.TextAlignment(TextAlignment::Right);
                        barLabel.VerticalAlignment(VerticalAlignment::Center);
                        barLabel.Margin({0, -1, 3, 0});
                        barItem.Children().Append(barLabel);
                        barColumn = 1;
                    } else if (timeTrack && verticalBars) {
                        ColumnDefinition trackColumn;
                        trackColumn.Width({(double)barThickness, GridUnitType::Pixel});
                        barItem.ColumnDefinitions().Append(trackColumn);
                    } else {
                        barItem.Width(verticalBars ? barThickness : barLength);
                    }
                    if (timeTrack && verticalBars) {
                        ColumnDefinition timeColumn;
                        timeColumn.Width({timeSize, GridUnitType::Pixel});
                        barItem.ColumnDefinitions().Append(timeColumn);
                    }

                    Border track;
                    double trackThickness = rail > 0 ? rail : barThickness;
                    double trackRadius = rail > 0 ? rail / 2 : radius;
                    track.Width(verticalBars ? trackThickness : barLength);
                    track.Height(verticalBars ? barLength : trackThickness);
                    track.CornerRadius({trackRadius, trackRadius, trackRadius, trackRadius});
                    track.HorizontalAlignment(HorizontalAlignment::Center);
                    track.VerticalAlignment(VerticalAlignment::Center);
                    track.Background(SolidColorBrush(ArgbColor(rail > 0 ? kBarRailColor : kBarTrackColor)));
                    Grid::SetColumn(track, barColumn);
                    barItem.Children().Append(track);
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
                    Grid::SetColumn(fill, barColumn);
                    barItem.Children().Append(fill);
                    refs.fills[w] = fill;

                    if (showPaceTicks) {
                        double paceCrossSize = barGeometry.paceCrossSize;
                        double paceLength = barGeometry.paceLength;
                        bool notch = paceTickStyle == PaceTickStyle::Notch;
                        Border paceTick;
                        paceTick.Width(verticalBars ? paceCrossSize : paceLength);
                        paceTick.Height(verticalBars ? paceLength : paceCrossSize);
                        paceTick.HorizontalAlignment(
                            verticalBars && !notch ? HorizontalAlignment::Center : HorizontalAlignment::Left);
                        paceTick.VerticalAlignment(
                            verticalBars ? VerticalAlignment::Bottom :
                                           (notch ? VerticalAlignment::Top : VerticalAlignment::Center));
                        paceTick.Visibility(Visibility::Collapsed);
                        paceTick.IsHitTestVisible(false);

                        if (timeTrack) {
                            // The small bar at its cell's far edge: a track as long as the bar,
                            // then the tick as its fill from the bar's start, in the pace color;
                            // UpdateQuotaUi sizes the fill and rounds its far end.
                            double thickness = barGeometry.timeThickness, end = thickness / 2;
                            Border timeTrackBar;
                            timeTrackBar.Width(verticalBars ? thickness : barLength);
                            timeTrackBar.Height(verticalBars ? barLength : thickness);
                            timeTrackBar.HorizontalAlignment(verticalBars ? HorizontalAlignment::Right :
                                                                            HorizontalAlignment::Left);
                            timeTrackBar.VerticalAlignment(VerticalAlignment::Bottom);
                            timeTrackBar.CornerRadius({end, end, end, end});
                            timeTrackBar.Background(SolidColorBrush(ArgbColor(kBarTrackColor)));
                            timeTrackBar.IsHitTestVisible(false);
                            Grid::SetColumn(timeTrackBar, verticalBars ? barColumn + 1 : barColumn);
                            Grid::SetRow(timeTrackBar, verticalBars ? 0 : 1);
                            barItem.Children().Append(timeTrackBar);
                            paceTick.Width(verticalBars ? thickness : 0);
                            paceTick.Height(verticalBars ? 0 : thickness);
                            paceTick.HorizontalAlignment(verticalBars ? HorizontalAlignment::Right :
                                                                        HorizontalAlignment::Left);
                            paceTick.VerticalAlignment(VerticalAlignment::Bottom);
                            paceTick.Background(refs.paceCore);
                        } else if (paceTickStyle == PaceTickStyle::Caret) {
                            Grid caretShape;
                            for (int layer = 0; layer < 2; layer++) {
                                wuxs::Polygon part;
                                for (auto [along, cross] : barGeometry.caret[layer]) {
                                    part.Points().Append(verticalBars ?
                                        winrt::Windows::Foundation::Point{
                                            (float)cross, (float)along} :
                                        winrt::Windows::Foundation::Point{
                                            (float)along, (float)cross});
                                }
                                part.Fill(layer == 1 ? refs.paceCore : refs.paceHalo);
                                caretShape.Children().Append(part);
                            }
                            paceTick.Child(caretShape);
                        } else {
                            paceTick.Background(refs.paceHalo);
                            double paceRadius = barGeometry.paceRadius;
                            paceTick.CornerRadius({paceRadius, paceRadius, paceRadius, paceRadius});

                            // Centred along the tick, at its offset across it.
                            Border paceCore;
                            double paceCoreCrossSize = barGeometry.coreCrossSize;
                            double paceCoreLength = barGeometry.coreLength;
                            double paceCoreOffset = barGeometry.coreCrossOffset;
                            paceCore.Width(verticalBars ? paceCoreCrossSize : paceCoreLength);
                            paceCore.Height(verticalBars ? paceCoreLength : paceCoreCrossSize);
                            paceCore.HorizontalAlignment(verticalBars ? HorizontalAlignment::Left :
                                                                        HorizontalAlignment::Center);
                            paceCore.VerticalAlignment(verticalBars ? VerticalAlignment::Center :
                                                                      VerticalAlignment::Top);
                            paceCore.Margin(verticalBars ? Thickness{paceCoreOffset, 0, 0, 0} :
                                                           Thickness{0, paceCoreOffset, 0, 0});
                            double coreRadius = barGeometry.coreRadius;
                            paceCore.CornerRadius({coreRadius, coreRadius, coreRadius, coreRadius});
                            paceCore.Background(refs.paceCore);
                            paceTick.Child(paceCore);
                        }
                        Grid::SetColumn(paceTick, timeTrack && verticalBars ? barColumn + 1 : barColumn);
                        Grid::SetRow(paceTick, timeTrack && !verticalBars ? 1 : 0);
                        barItem.Children().Append(paceTick);
                        refs.paceTicks[w] = paceTick;
                    }

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
                                          barGeometry.textInset :
                                      percentTextAlignment == PercentTextAlignment::Center ?
                                          0 : -barGeometry.textInset);
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
                content.Children().Append(bars);
                if (identity && !identityBeforeBars) content.Children().Append(identity);
                col.Children().Append(content);
            }

            // The box's side padding comes out of the account margin, so accounts' contents sit
            // twice the margin apart; at small margins the padding shrinks with it.
            Thickness padding = col.Padding();
            padding.Left = std::min(padding.Left, (double)accountMargin);
            padding.Right = std::min(padding.Right, (double)accountMargin);
            col.Padding(padding);
            col.Margin({accountMargin - padding.Left, 0, accountMargin - padding.Right, 0});
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
                [statePtr, accountIdentity, visualTestMode](
                    winrt::Windows::Foundation::IInspectable const&,
                    wuxi::TappedRoutedEventArgs const& e) {
                    e.Handled(true);
                    if (g_unloading || !statePtr->quotaGrid) return;
                    try {
                        OnTileTapped(*statePtr, accountIdentity, visualTestMode);
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
                                             pointerExitedToken, pointerCanceledToken});

            // Context menu: this account's header and actions, then every account's, then
            // Settings. Visual tests offer only their exit and Settings.
            bool antigravity = accounts[i].provider == L"antigravity";
            MenuFlyout menu;
            MenuFlyoutItem menuHeader{nullptr};
            MenuFlyoutItem signInItem{nullptr};
            MenuFlyoutSubItem showSub{nullptr};
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
                // Sign in with the mod's own OAuth; the Opening handler shows it only while the
                // account needs a sign-in.
                if (!antigravity) {
                    signInItem = menuItem(L"Sign in", L"\uE77B",
                        [accountIdentity](winrt::Windows::Foundation::IInspectable const&,
                                          RoutedEventArgs const&) {
                            StartLoginByIdentity(accountIdentity);
                        });
                    signInItem.Visibility(Visibility::Collapsed);
                    menu.Items().Append(signInItem);
                }
                // UpdateQuotaUi disables it while this is the last visible account.
                refs.hideItem = menuItem(L"Hide", L"\uED1A",
                    [accountIdentity](winrt::Windows::Foundation::IInspectable const&,
                                      RoutedEventArgs const&) {
                        SetAccountVisibility(accountIdentity, false);
                    });
                menu.Items().Append(refs.hideItem);

                menu.Items().Append(MenuFlyoutSeparator{});
                menu.Items().Append(menuItem(L"Refresh all", L"\uE895",
                    [](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                        QueueRefresh(0);
                    }));

                // Per-account show/hide checkboxes (checked = visible). Every column carries the
                // same list; toggling sets global state, persists, and re-syncs all instances.
                // The menu stays open for more toggles, even when one hides this column.
                showSub = MenuFlyoutSubItem{};
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
                        [statePtr, toggleIdentity](
                            winrt::Windows::Foundation::IInspectable const& sender,
                            RoutedEventArgs const&) {
                            if (g_unloading) return;
                            bool visible = true;
                            try {
                                auto item = sender.try_as<ToggleMenuFlyoutItem>();
                                if (!item) return;
                                visible = item.IsChecked();
                                statePtr->keepMenuOpenToggle = item;
                                if (statePtr->keepMenuOpenTimer) statePtr->keepMenuOpenTimer.Start();
                            } catch (...) {
                                return;
                            }
                            SetAccountVisibility(toggleIdentity, visible);
                        });
                    state.menuItemClickHandlers.push_back({toggle, toggleToken});
                    state.accountToggleItems.push_back({toggleIndex, toggle});
                    showSub.Items().Append(toggle);
                }
                // Opens the submenu as soon as the pointer enters it or clicks it, even when the
                // item handles the event itself.
                auto expandHandler = winrt::box_value(wuxi::PointerEventHandler(
                    [weakMenu = winrt::make_weak(menu)](
                        winrt::Windows::Foundation::IInspectable const& sender,
                        wuxi::PointerRoutedEventArgs const&) {
                        if (g_unloading) return;
                        auto item = sender.try_as<MenuFlyoutSubItem>();
                        if (item) SetMenuSubItemExpanded(weakMenu.get(), item, true);
                    }));
                for (RoutedEvent routedEvent :
                     {UIElement::PointerEnteredEvent(), UIElement::PointerReleasedEvent()}) {
                    showSub.AddHandler(routedEvent, expandHandler, true);
                    state.routedHandlers.push_back({showSub, routedEvent, expandHandler});
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
            // whether Sign in shows then follow the account's current state.
            state.accountRefs.back().menuOpeningToken = menu.Opening(
                [statePtr, accountIdentity, provider = accounts[i].provider, menuHeader, signInItem](
                    winrt::Windows::Foundation::IInspectable const&,
                    winrt::Windows::Foundation::IInspectable const&) {
                    if (g_unloading) return;
                    statePtr->keepMenuOpenToggle = nullptr;
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
                        if (signInItem) {
                            signInItem.Visibility(needsLogin ? Visibility::Visible :
                                                               Visibility::Collapsed);
                        }
                    } catch (...) {}
                });
            if (showSub) {
                // The pointer resting on another item for kSubmenuCloseDelayMs closes the
                // submenu; coming back to the submenu or its item first keeps it.
                auto closeSubmenu = winrt::box_value(wuxi::PointerEventHandler(
                    [statePtr, weakMenu = winrt::make_weak(menu), weakSub = winrt::make_weak(showSub)](
                        winrt::Windows::Foundation::IInspectable const&,
                        wuxi::PointerRoutedEventArgs const&) {
                        if (g_unloading || !statePtr->submenuCloseTimer) return;
                        try {
                            statePtr->submenuCloseMenu = weakMenu.get();
                            statePtr->submenuCloseItem = weakSub.get();
                            statePtr->submenuCloseTimer.Stop();
                            statePtr->submenuCloseTimer.Start();
                        } catch (...) {}
                    }));
                auto keepSubmenu = winrt::box_value(wuxi::PointerEventHandler(
                    [statePtr](winrt::Windows::Foundation::IInspectable const&,
                               wuxi::PointerRoutedEventArgs const&) {
                        try {
                            if (statePtr->submenuCloseTimer) statePtr->submenuCloseTimer.Stop();
                        } catch (...) {}
                    }));
                auto addPointerEntered = [&](UIElement const& element,
                                             winrt::Windows::Foundation::IInspectable const& handler) {
                    element.AddHandler(UIElement::PointerEnteredEvent(), handler, true);
                    state.routedHandlers.push_back({element, UIElement::PointerEnteredEvent(), handler});
                };
                // Root items other than the submenu's own; separators don't count.
                for (auto const& item : menu.Items()) {
                    if (item.try_as<MenuFlyoutItem>()) addPointerEntered(item, closeSubmenu);
                }
                addPointerEntered(showSub, keepSubmenu);
                for (auto const& toggle : showSub.Items()) addPointerEntered(toggle, keepSubmenu);

                // A show/hide toggle's click closes the submenu, then asks the menu to close; the
                // menu stays, and the submenu reopens in the same call (no blank frame) with the
                // toggle focused. Other closes (light dismiss, Escape, other items) pass.
                state.accountRefs.back().menuClosingToken = menu.Closing(
                    [statePtr, showSub](wuxcp::FlyoutBase const& sender,
                                        wuxcp::FlyoutBaseClosingEventArgs const& e) {
                        if (g_unloading || !statePtr->keepMenuOpenToggle) return;
                        ToggleMenuFlyoutItem toggle = std::move(statePtr->keepMenuOpenToggle);
                        statePtr->keepMenuOpenToggle = nullptr;
                        try {
                            e.Cancel(true);
                        } catch (...) {
                            return;
                        }
                        SetMenuSubItemExpanded(sender, showSub, true);
                        try {
                            toggle.Focus(FocusState::Programmatic);
                        } catch (...) {}
                    });
            }
        }

        root.Children().Append(panel);
        // Moves pace marks each minute; identities also pick up a missed light/dark switch with it.
        if (showPaceTicks || identities) {
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
