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

    MeterDisplay display = MeterDisplayFor(settings, NowUnixMs(), state.rasterizationScale,
                                           state.taskbarHeight);
    ULONGLONG now = display.now;
    if (visualTestMode) BuildVisualTestSnapshot(display, &accounts, &data);
    if (data.size() != accounts.size()) return;
    if (state.accountRefs.size() != data.size()) return;
    if (state.applied.size() != data.size()) state.applied.assign(data.size(), {});

    bool refreshing;
    uint64_t refreshAccountIdentity;
    {
        std::lock_guard<std::mutex> refreshLock(g_refreshMutex);
        refreshing = g_refreshing.load();
        refreshAccountIdentity = g_refreshAccountIdentity.load();
    }
    bool verticalBars = settings.barLayout == BarLayout::Vertical;
    // Tiles follow the layout the grid was built with.
    bool tileLayout = IsTileLayout(state.buildBarLayout);
    bool lightTiles = state.tileLightTheme;
    if (tileLayout) {
        // Tile chrome matches the taskbar text color that the tiles' logo and text inherit, like
        // the bars' labels: dark text means a light taskbar. Else the tree's theme.
        try {
            SolidColorBrush text{nullptr};
            for (auto const& refs : state.accountRefs) {
                if (!refs.tile.warn) continue;
                text = refs.tile.warn.Foreground().try_as<SolidColorBrush>();
                break;
            }
            if (text) {
                auto c = text.Color();
                lightTiles = c.R * 299 + c.G * 587 + c.B * 114 < 128 * 1000;
            } else {
                lightTiles = state.quotaGrid.ActualTheme() == ElementTheme::Light;
            }
        } catch (...) {}
        state.tileLightTheme = lightTiles;
    }
    FlyoutPopup& flyout = state.flyout.popup;
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

            AccountMeters meters = AccountMeterState(
                display, state.buildBarLayout, accounts[i], d,
                ui.tile.badge ? (int)ui.tile.badgeText.Text().size() : 0, ui.tile.textWidthPerPt);
            bool accountRefreshing = !visualTestMode && refreshing &&
                (!refreshAccountIdentity ||
                 AccountIdentityHash(accounts[i]) == refreshAccountIdentity);

            int barMask = meters.barMask;
            if (tileLayout) {
                UpdateAccountTile(state, i, meters, display, lightTiles);
                UpdateTileHighlight(state, (int)i);
            } else {
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
                    if (ui.column) {
                        // Preserve a small context-menu target if every selected quota is unavailable.
                        double minimumHitSize =
                            visibleCount ? 0.0 : std::max(display.barThickness, 8);
                        ui.column.MinWidth(minimumHitSize);
                        ui.column.MinHeight(minimumHitSize);
                    }
                    ap.barMask = barMask;
                }

                for (int w = 0; w < kQuotaBarCount; w++) {
                    QuotaMeter& m = meters.quotas[w];
                    if (m.fillPx != ap.fillPx[w] || m.color != ap.fillColor[w]) {
                        if (ui.fills[w]) {
                            if (verticalBars) ui.fills[w].Height(m.fillPx);
                            else ui.fills[w].Width(m.fillPx);
                            ui.fills[w].Background(SolidColorBrush(ArgbColor(m.color)));
                        }
                        ap.fillPx[w] = m.fillPx;
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
                        if (ui.paceTicks[w]) {
                            ui.paceTicks[w].Margin(verticalBars ?
                                Thickness{0, 0, 0, (double)m.pacePx} :
                                Thickness{(double)m.pacePx, 0, 0, 0});
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

            if (flyout.flyout && flyout.accountIndex == (int)i) {
                if (AccountIdentityHash(accounts[i]) != flyout.accountIdentity) {
                    CloseAccountFlyout(state);
                } else {
                    // Burn-up curves per flyout row. The accessor takes the settings and data
                    // locks, so none may be held here.
                    std::array<std::optional<BurnUpCurve>, kFlyoutRowCount> curves;
                    const auto readings = FlyoutRowReadings(d);
                    for (int r = 0; r < kFlyoutRowCount; r++) {
                        const WindowUsage& reading = *readings[r];
                        if (!visualTestMode) {
                            curves[r] = CopyBurnUpCurve(accounts[i].historyId, r, reading);
                            continue;
                        }
                        // Visual test samples: readings from part-way into the window up to now,
                        // ending at the reading, with a recording gap and an isolated reading.
                        // The OpenAI sample has none, so its flyout keeps the no-history bars.
                        if (accounts[i].provider == L"openai" || reading.pct < 0 ||
                            !reading.windowDurationMs ||
                            reading.resetUnixMs <= reading.windowDurationMs ||
                            now <= reading.resetUnixMs - reading.windowDurationMs) {
                            continue;
                        }
                        // Times as fractions of the window's elapsed span from its start, usage
                        // as shares of the reading. Sample windows move with now, so the drawn
                        // geometry stays put between ticks.
                        static constexpr struct {
                            double time, share;
                            bool connect;
                        } kSamples[] = {
                            {0.15, 0.10, false}, {0.22, 0.18, true}, {0.30, 0.22, true},
                            {0.38, 0.36, true}, {0.45, 0.40, true}, {0.55, 0.52, false},
                            {0.68, 0.60, false}, {0.76, 0.66, true}, {0.84, 0.80, true},
                            {0.92, 0.88, true}, {1.00, 1.00, true},
                        };
                        ULONGLONG start = reading.resetUnixMs - reading.windowDurationMs;
                        BurnUpCurve& curve = curves[r].emplace(
                            BurnUpCurve{reading.resetUnixMs, reading.windowDurationMs, {}});
                        for (const auto& sample : kSamples) {
                            curve.points.push_back(
                                {start + (ULONGLONG)std::llround((now - start) * sample.time),
                                 reading.pct * sample.share, sample.connect});
                        }
                    }
                    UpdateAccountFlyout(state, accounts[i], d,
                                        {now, display.barMode, display.yellowThreshold,
                                         display.orangeThreshold, display.redThreshold,
                                         display.colorblindMode, settings.showOpenAiExtraLimits,
                                         meters.stale, meters.warn, accountRefreshing,
                                         visualTestMode},
                                        curves);
                }
            }

            double columnOpacity = accountRefreshing ? 0.65 : 1.0;
            if (columnOpacity != ap.columnOpacity) {
                if (ui.column) ui.column.Opacity(columnOpacity);
                ap.columnOpacity = columnOpacity;
            }

            if (meters.labelOpacity != ap.labelOpacity || meters.labelText != ap.labelText) {
                if (ui.label) {
                    ui.label.Opacity(meters.labelOpacity);
                    ui.label.Text(meters.labelText);
                }
                ap.labelOpacity = meters.labelOpacity;
                ap.labelText = std::move(meters.labelText);
            }
        }

        // Keep every instance's menu checkboxes in sync with the shared hidden state
        // (programmatic IsChecked does not raise Click).
        for (auto& [idx, item] : state.accountToggleItems) {
            if (!item || idx < 0 || idx >= (int)accounts.size()) continue;
            bool checked = !accounts[idx].hidden;
            if (item.IsChecked() != checked) item.IsChecked(checked);
        }
    } catch (...) {
        Wh_Log(L"UpdateQuotaUi: exception");
    }
}
