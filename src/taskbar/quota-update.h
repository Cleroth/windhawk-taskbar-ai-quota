// Applies settings and fetched data to an existing quota grid.

static void UpdateQuotaUi(QuotaUiInstance& state) {
    if (!state.quotaGrid) return;

    bool visualTestMode = g_visualTestMode.load(std::memory_order_acquire);
    if (state.buildVisualTestMode != visualTestMode) return;

    Settings settings;
    std::vector<AccountData> data;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
        if (!visualTestMode) {
            std::lock_guard<std::mutex> dataLock(g_dataMutex);
            data = g_data;
        }
    }
    std::vector<AccountConfig>& accounts = settings.accounts;

    // In the grid's language, which its reserved widths and labels were built in, with the
    // current regional number format: no width measured at build holds a separator, so a change
    // shows from this update on (the flyout's too).
    state.buildLocale = I18nLocale(state.buildLocale.Language(), ReadNumberFormat());
    MeterDisplay display = MeterDisplayFor(settings, state.buildLocale, XamlTileTextMeasure(state.buildLocale),
                                           NowUnixMs(), state.rasterizationScale, state.taskbarHeight,
                                           state.sideWidth);
    ULONGLONG now = display.now;
    if (visualTestMode) BuildVisualTestSnapshot(display, &accounts, &data);
    if (data.size() != accounts.size()) return;
    if (state.accountRefs.size() != data.size()) return;
    if (state.applied.size() != data.size()) state.applied.assign(data.size(), {});

    std::vector<std::pair<uint64_t, ULONGLONG>> refreshRequests;
    {
        std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
        refreshRequests = g_refreshRequests;
    }
    bool verticalBars = settings.barLayout == BarLayout::Vertical;
    // Tiles follow the layout the grid was built with.
    bool tileLayout = IsTileLayout(state.buildBarLayout);
    // Tile chrome and the bars' default pace tick match the taskbar text color that the
    // identities' logo and text and the bars' labels inherit: dark text means a light taskbar.
    // Else the tree's theme.
    bool light = state.lightTheme;
    try {
        SolidColorBrush text{nullptr};
        for (auto const& refs : state.accountRefs) {
            TextBlock const& inherits = refs.label ? refs.label : refs.tile.warn;
            if (!inherits) continue;
            text = inherits.Foreground().try_as<SolidColorBrush>();
            break;
        }
        if (text) {
            auto c = text.Color();
            light = c.R * 299 + c.G * 587 + c.B * 114 < 128 * 1000;
        } else {
            light = state.quotaGrid.ActualTheme() == ElementTheme::Light;
        }
    } catch (...) {}
    state.lightTheme = light;
    BarGeometry bar = BarGeometryFor(display);
    FlyoutPopup& flyout = state.flyout.popup;
    // Usage graphs with a span on hover: the hovered account's long windows in it.
    MeterDisplay swappedDisplay = display;
    if (settings.sparklineHoverSpan) swappedDisplay.sparklineAroundMs = SparklineAroundMs(*settings.sparklineHoverSpan);
    state.sparkSwapIndex = state.buildSparkHoverSwap ? state.flyout.hoverIndex : -1;
    // What each account's provider status page shows while outage marks are on: the visual test
    // sample, or the status reads' (CurrentProviderOutage takes only its leaf lock).
    auto outageOf = [&](const std::wstring& provider) -> ProviderOutage {
        if (!settings.showProviderOutages) return {};
        return visualTestMode ? VisualTestOutage(provider, now) : CurrentProviderOutage(provider);
    };
    try {
        for (size_t i = 0; i < data.size(); i++) {
            const AccountData& d = data[i];
            AppliedState& ap = state.applied[i];
            const AccountUiRefs& ui = state.accountRefs[i];

            // Hidden accounts collapse their column (no space, not right-clickable) and skip
            // visual work; data stays in g_data and repaints on un-hide (which posts an update).
            int visible = accounts[i].hidden ? 0 : 1;
            if (visible != ap.visible) {
                if (ui.column) {
                    ui.column.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
                }
                ap.visible = visible;
            }
            if (!visible) {
                if (flyout.flyout && flyout.accountIndex == (int)i) CloseAccountFlyout(state);
                continue;
            }

            // A flyout row's recorded usage of its reading: the row's burn-up curve (the accessor
            // takes the settings and data locks, so none may be held here), or the visual test
            // samples. Sparklines ask for the row showing their quota.
            auto rowTrace = [&](int row, const WindowUsage& reading) {
                if (visualTestMode) return VisualTestTrace(accounts[i], reading, now);
                std::vector<TileTracePoint> trace;
                if (std::optional<BurnUpCurve> curve = CopyBurnUpCurve(accounts[i].historyId, row, reading)) {
                    for (const BurnUpPoint& point : curve->points) trace.push_back({point.timeMs, point.used, point.connect});
                }
                return trace;
            };
            TileTraceSource traceOf = [&](int slot, const WindowUsage& reading) {
                static constexpr int kRows[kQuotaBarCount] = {kFlyoutFiveHourRow, kFlyoutWeeklyRow,
                                                              kFlyoutFableRow, kFlyoutExtraRow};
                return rowTrace(kRows[slot], reading);
            };
            ProviderOutage outage = outageOf(accounts[i].provider);
            AccountMeters meters = AccountMeterState(
                (int)i == state.sparkSwapIndex ? swappedDisplay : display, state.buildBarLayout, accounts[i], d,
                ui.tile.badge ? (int)ui.tile.badgeText.Text().size() : 0, ui.tile.textWidthPerPt, traceOf,
                outage.level);
            bool accountRefreshing = !visualTestMode &&
                std::any_of(refreshRequests.begin(), refreshRequests.end(), [&](const auto& request) {
                    return !request.first || request.first == AccountIdentityHash(accounts[i]);
                });

            int barMask = meters.barMask;
            if (tileLayout) {
                UpdateAccountTile(state, i, meters, display, light);
            } else {
                UpdateTileIdentity(ui.tile, ap, meters, display, light);
                // The identity's margins: its box, then the identity gap toward the bars while
                // any shows.
                FrameworkElement identity{nullptr};
                if (ui.label) {
                    identity = ui.label;
                } else if (ui.tile.identity) {
                    identity = ui.tile.identity;
                }
                if (identity) {
                    double side = kBarLogoSide * display.logoScale;
                    BadgeBox badge = ui.tile.badge ?
                        LogoBadgeBox(display, side, (int)ui.tile.badgeText.Text().size()) : BadgeBox{};
                    BarIdentityMargin m = BarIdentityMarginFor(
                        BarIdentityPositionFor(settings.labelPosition, display), ui.label != nullptr, side,
                        badge, meters.mark, barMask ? settings.labelGap : 0, state.labelAboveCap,
                        state.labelBelowBaseline);
                    Thickness margin{m.left, m.top, m.right, m.bottom};
                    if (margin != ap.identityMargin) {
                        identity.Margin(margin);
                        ap.identityMargin = margin;
                    }
                }
                int paceLight = light ? 1 : 0;
                if (paceLight != ap.paceLight) {
                    if (ui.paceCore && ui.paceHalo) {
                        PaceMarkColors colors = PaceMarkColorsFor(display.paceTickColor, light);
                        ui.paceCore.Color(ArgbColor(colors.core));
                        ui.paceHalo.Color(ArgbColor(colors.halo));
                    }
                    ap.paceLight = paceLight;
                }

                if (barMask != ap.barMask) {
                    int visibleCount = 0;
                    for (int w = 0; w < kQuotaBarCount; w++) {
                        if (barMask & (1 << w)) visibleCount++;
                    }

                    int visiblePosition = 0;
                    double halfBarGap = settings.barGap / 2.0;
                    for (int w = 0; w < kQuotaBarCount; w++) {
                        bool barVisible = (barMask & (1 << w)) != 0;
                        if (ui.barItems[w]) {
                            ui.barItems[w].Visibility(barVisible ? Visibility::Visible :
                                                                  Visibility::Collapsed);
                            if (barVisible) {
                                bool first = visiblePosition == 0;
                                bool last = visiblePosition == visibleCount - 1;
                                ui.barItems[w].Margin(verticalBars ?
                                    Thickness{first ? 0.0 : halfBarGap, 0,
                                              last ? 0.0 : halfBarGap, 0} :
                                    Thickness{0, first ? 1.0 : halfBarGap, 0,
                                              last ? 1.0 : halfBarGap});
                            }
                        }
                        if (barVisible) visiblePosition++;
                    }
                    if (ui.barArea) {
                        ui.barArea.Visibility(visibleCount ? Visibility::Visible :
                                                             Visibility::Collapsed);
                    }
                    if (ui.content) {
                        // Without a quota or an identity shown, a small context-menu target stays.
                        double minimumHitSize =
                            visibleCount || identity ? 0.0 : std::max(display.barThickness, 8);
                        ui.content.MinWidth(minimumHitSize);
                        ui.content.MinHeight(minimumHitSize);
                    }
                    ap.barMask = barMask;
                }

                for (int w = 0; w < kQuotaBarCount; w++) {
                    QuotaMeter& m = meters.quotas[w];
                    if (m.fillPx != ap.fillPx[w]) {
                        if (ui.fills[w]) {
                            // Start corners round with the track; vertical bars fill upward.
                            double start = bar.radius, end = m.fillEndRadius;
                            if (verticalBars) {
                                ui.fills[w].Height(m.fillPx);
                                ui.fills[w].CornerRadius({end, end, start, start});
                            } else {
                                ui.fills[w].Width(m.fillPx);
                                ui.fills[w].CornerRadius({start, end, end, start});
                            }
                        }
                        ap.fillPx[w] = m.fillPx;
                    }
                    if (m.color != ap.fillColor[w]) {
                        if (ui.fills[w]) {
                            ui.fills[w].Background(UsageFillBrush(m.color, verticalBars));
                        }
                        ap.fillColor[w] = m.color;
                    }

                    bool paceVisible = m.paceVisible && ui.paceTicks[w];
                    int paceVisibleInt = paceVisible ? 1 : 0;
                    if (paceVisibleInt != ap.paceVisible[w]) {
                        if (ui.paceTicks[w]) {
                            ui.paceTicks[w].Visibility(paceVisible ? Visibility::Visible :
                                                                    Visibility::Collapsed);
                        }
                        ap.paceVisible[w] = paceVisibleInt;
                    }
                    if (paceVisible && m.pacePx != ap.pacePx[w]) {
                        if (ui.paceTicks[w] && bar.timeThickness > 0) {
                            // The time track's fill from the bar's start: start corners round
                            // with its track, the far end as the meter's.
                            double start = bar.timeThickness / 2, end = m.paceEndRadius;
                            if (verticalBars) {
                                ui.paceTicks[w].Height(m.pacePx);
                                ui.paceTicks[w].CornerRadius({end, end, start, start});
                            } else {
                                ui.paceTicks[w].Width(m.pacePx);
                                ui.paceTicks[w].CornerRadius({start, end, end, start});
                            }
                        } else if (ui.paceTicks[w]) {
                            // Centred ticks reach past both sides by the overhang. A notch hangs
                            // from the top (vertical: left) one; its far margin keeps its layout
                            // size within the bar, so layout never clips its halo.
                            double top = -bar.overhang;
                            double bottom = display.paceTickStyle == PaceTickStyle::Notch ?
                                std::min(0.0, display.barThickness - bar.paceCrossSize - top) : top;
                            ui.paceTicks[w].Margin(verticalBars ?
                                Thickness{top, 0, bottom, (double)m.pacePx} :
                                Thickness{(double)m.pacePx, top, 0, bottom});
                        }
                        ap.pacePx[w] = m.pacePx;
                    }

                    if (settings.percentTextVisibility != PercentTextVisibility::Never) {
                        PercentTextAlignment appliedAlignment = m.textAlignment;
                        if ((int)appliedAlignment != ap.percentAlignments[w]) {
                            if (ui.percents[w]) {
                                if (appliedAlignment == PercentTextAlignment::Left) {
                                    ui.percents[w].HorizontalAlignment(HorizontalAlignment::Left);
                                    ui.percents[w].TextAlignment(TextAlignment::Left);
                                } else if (appliedAlignment == PercentTextAlignment::Center) {
                                    ui.percents[w].HorizontalAlignment(HorizontalAlignment::Center);
                                    ui.percents[w].TextAlignment(TextAlignment::Center);
                                } else {
                                    ui.percents[w].HorizontalAlignment(HorizontalAlignment::Right);
                                    ui.percents[w].TextAlignment(TextAlignment::Right);
                                }
                                if (auto translation = ui.percents[w].RenderTransform()
                                                           .try_as<TranslateTransform>()) {
                                    translation.X(m.textOffset);
                                }
                            }
                            ap.percentAlignments[w] = (int)appliedAlignment;
                        }

                        int darkText = m.darkText ? 1 : 0;
                        if (darkText != ap.percentDark[w]) {
                            if (ui.percents[w]) {
                                ui.percents[w].Foreground(SolidColorBrush(darkText ?
                                    winrt::Windows::UI::Color{255, 0, 0, 0} :
                                    winrt::Windows::UI::Color{255, 255, 255, 255}));
                            }
                            ap.percentDark[w] = darkText;
                        }

                        if (m.text != ap.percentTexts[w]) {
                            if (ui.percents[w]) ui.percents[w].Text(m.text);
                            ap.percentTexts[w] = std::move(m.text);
                        }
                    }
                }
            }
            UpdateTileHighlight(state, (int)i);

            if (flyout.flyout && flyout.accountIndex == (int)i) {
                if (AccountIdentityHash(accounts[i]) != flyout.accountIdentity) {
                    CloseAccountFlyout(state);
                } else {
                    std::array<std::vector<TileTracePoint>, kFlyoutRowCount> traces;
                    const auto readings = FlyoutRowReadings(d);
                    for (int r = 0; r < kFlyoutRowCount; r++) traces[r] = rowTrace(r, *readings[r]);
                    FlyoutDisplay flyoutDisplay{now, display.barMode, display.yellowThreshold,
                                                display.orangeThreshold, display.redThreshold,
                                                display.usageColors, settings.showOpenAiExtraLimits,
                                                meters.stale, meters.warn, accountRefreshing,
                                                visualTestMode};
                    flyoutDisplay.outage = outage;
                    flyoutDisplay.outageColor = meters.mark == IdentityMark::Outage ? meters.markColor : 0;
                    flyoutDisplay.locale = display.locale;
                    UpdateAccountFlyout(state, accounts[i], d, flyoutDisplay, traces);
                }
            }

            double columnOpacity = accountRefreshing ? 0.65 : 1.0;
            if (columnOpacity != ap.columnOpacity) {
                if (ui.column) ui.column.Opacity(columnOpacity);
                ap.columnOpacity = columnOpacity;
            }

            // The Label identity: its text (with the stale "!"), or the text and the outage glyph in
            // its color as a run in the icon font at the label's size, on its baseline (the font's
            // ascent is shorter than the label's, so the line keeps its height). Text() replaces the
            // runs.
            uint32_t labelMarkColor = meters.mark == IdentityMark::Outage ? meters.markColor : 0;
            if (meters.labelOpacity != ap.labelOpacity || meters.labelText != ap.labelText ||
                labelMarkColor != ap.labelMarkColor) {
                if (ui.label) {
                    ui.label.Opacity(meters.labelOpacity);
                    if (labelMarkColor) {
                        namespace wuxd = winrt::Windows::UI::Xaml::Documents;
                        auto inlines = ui.label.Inlines();
                        inlines.Clear();
                        wuxd::Run text;
                        text.Text(meters.labelText);
                        inlines.Append(text);
                        wuxd::Run glyph;
                        glyph.Text(kOutageGlyph);
                        glyph.FontFamily(FontFamily(kTaskbarIconFont));
                        glyph.Foreground(SolidColorBrush(ArgbColor(labelMarkColor)));
                        inlines.Append(glyph);
                    } else {
                        ui.label.Text(meters.labelText);
                    }
                }
                ap.labelOpacity = meters.labelOpacity;
                ap.labelText = std::move(meters.labelText);
                ap.labelMarkColor = labelMarkColor;
            }
        }

        // Keep every instance's menu checkboxes and Hide items in sync with the shared hidden
        // state, live while a menu is open (programmatic IsChecked does not raise Click). The
        // last visible account can't be hidden, so its checkbox and Hide item are disabled.
        int visibleCount = (int)std::count_if(accounts.begin(), accounts.end(),
            [](const AccountConfig& account) { return !account.hidden; });
        for (auto& [idx, item] : state.accountToggleItems) {
            if (!item || idx < 0 || idx >= (int)accounts.size()) continue;
            bool checked = !accounts[idx].hidden;
            if (item.IsChecked() != checked) item.IsChecked(checked);
            bool enabled = !checked || visibleCount > 1;
            if (item.IsEnabled() != enabled) item.IsEnabled(enabled);
        }
        for (size_t i = 0; i < state.accountRefs.size(); i++) {
            MenuFlyoutItem const& hideItem = state.accountRefs[i].hideItem;
            bool enabled = accounts[i].hidden || visibleCount > 1;
            if (hideItem && hideItem.IsEnabled() != enabled) hideItem.IsEnabled(enabled);
        }
    } catch (...) {
        Wh_Log(L"UpdateQuotaUi: exception");
    }
}
