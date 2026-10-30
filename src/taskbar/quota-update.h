// Applies settings and fetched data to an existing quota grid.

static void UpdateQuotaUi(QuotaUiInstance& state) {
    if (!state.quotaGrid) return;

    bool visualTestMode = g_visualTestMode.load(std::memory_order_acquire);
    if (state.buildVisualTestMode != visualTestMode) return;

    std::vector<AccountConfig> accounts;
    std::vector<AccountData> data;
    int intervalMin, barLength, barThickness, barGap, yellowThreshold, orangeThreshold, redThreshold;
    bool showPaceTicks, showExtraBarAmounts, showOpenAiExtraLimits, colorblindMode,
         showStaleWarning;
    BarLayout barLayout;
    BarMode barMode;
    PercentTextVisibility percentTextVisibility;
    PercentTextAlignment percentTextAlignment;
    ClickAction clickAction;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        accounts = g_settings.accounts;
        intervalMin = g_settings.pollMinutes;
        clickAction = g_settings.clickAction;
        barLayout = g_settings.barLayout;
        barMode = g_settings.barMode;
        barLength = g_settings.barLength;
        barThickness = g_settings.barThickness;
        barGap = g_settings.barGap;
        yellowThreshold = g_settings.yellowThreshold;
        orangeThreshold = g_settings.orangeThreshold;
        redThreshold = g_settings.redThreshold;
        showPaceTicks = g_settings.showPaceTicks;
        percentTextVisibility = g_settings.percentTextVisibility;
        percentTextAlignment = g_settings.percentTextAlignment;
        showExtraBarAmounts = g_settings.showExtraBarAmounts;
        showOpenAiExtraLimits = g_settings.showOpenAiExtraLimits;
        colorblindMode = g_settings.colorblindMode;
        showStaleWarning = g_settings.showStaleWarning;
        if (!visualTestMode) {
            std::lock_guard<std::mutex> dataLock(g_dataMutex);
            data = g_data;
        }
    }

    ULONGLONG now = NowUnixMs();
    if (visualTestMode) {
        BuildVisualTestSnapshot(yellowThreshold, orangeThreshold, redThreshold,
                                now, &accounts, &data);
    }
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
    bool verticalBars = barLayout == BarLayout::Vertical;
    double physicalPixelDip = 1.0 / state.rasterizationScale;
    // Remaining mode shows the quota left (100 - used); n/a (pct < 0) stays unchanged.
    auto displayPct = [&](double pct) {
        return barMode == BarMode::Remaining && pct >= 0 ? std::clamp(100.0 - pct, 0.0, 100.0) : pct;
    };
    const wchar_t* remainingSuffix = barMode == BarMode::Remaining ? L" remaining" : L"";
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
            if (!visible) continue;

            ULONGLONG staleAfterMs = (accounts[i].provider == L"antigravity" ? 1ULL :
                                      (ULONGLONG)intervalMin) * 2 * 60000;
            bool stale = d.stale || d.lastSuccessMs == 0 || now - d.lastSuccessMs > staleAfterMs;
            bool warn = showStaleWarning && stale && !d.error.empty();
            bool accountRefreshing = !visualTestMode && refreshing &&
                (!refreshAccountIdentity ||
                 AccountIdentityHash(accounts[i]) == refreshAccountIdentity);

            const std::array<const WindowUsage*, kQuotaBarCount> usage = {
                &d.win5h, &d.winWeek, &d.fableWeek, &d.extraUsage};
            int barMask = 0;
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (accounts[i].showBars[w] && (d.lastSuccessMs == 0 || usage[w]->pct >= 0)) {
                    barMask |= 1 << w;
                }
            }
            if (barMask != ap.barMask) {
                int visibleCount = 0;
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (barMask & (1 << w)) visibleCount++;
                }

                int visiblePosition = 0;
                double halfBarGap = barGap / 2.0;
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
                    double minimumHitSize = visibleCount ? 0.0 : std::max(barThickness, 8);
                    ui.column.MinWidth(minimumHitSize);
                    ui.column.MinHeight(minimumHitSize);
                }
                ap.barMask = barMask;
            }

            for (int w = 0; w < kQuotaBarCount; w++) {
                const WindowUsage& wu = *usage[w];
                double dispPct = displayPct(wu.pct);
                int px = dispPct > 0 ? std::clamp((int)std::lround(barLength * dispPct / 100.0), 2, barLength) : 0;
                // Color stays keyed to actual usage so depleting quota still reds out.
                auto c = UsageColor(wu.pct, stale, yellowThreshold, orangeThreshold, redThreshold,
                                    colorblindMode);
                uint32_t cv = ((uint32_t)c.A << 24) | ((uint32_t)c.R << 16) |
                              ((uint32_t)c.G << 8) | c.B;
                if (px != ap.fillPx[w] || cv != ap.fillColor[w]) {
                    if (ui.fills[w]) {
                        if (verticalBars) ui.fills[w].Height(px);
                        else ui.fills[w].Width(px);
                        ui.fills[w].Background(SolidColorBrush(c));
                    }
                    ap.fillPx[w] = px;
                    ap.fillColor[w] = cv;
                }

                bool paceVisible = showPaceTicks && ui.paceTicks[w] &&
                                   !stale && wu.pct >= 0 &&
                                   wu.resetUnixMs > wu.windowDurationMs &&
                                   wu.windowDurationMs > 0 &&
                                   now >= wu.resetUnixMs - wu.windowDurationMs;
                int pacePx = 0;
                if (paceVisible) {
                    double remainingFraction =
                        ((double)wu.resetUnixMs - (double)now) / wu.windowDurationMs;
                    double pacePct = barMode == BarMode::Remaining ? remainingFraction * 100.0 :
                                                                     (1.0 - remainingFraction) * 100.0;
                    pacePct = std::clamp(pacePct, 0.0, 100.0);

                    double paceThickness = verticalBars ? ui.paceTicks[w].Height() :
                                                          ui.paceTicks[w].Width();
                    double radius = std::clamp(std::max(1.0, barThickness / 2.0),
                                               paceThickness / 2.0,
                                               (barLength - paceThickness) / 2.0);
                    double center = std::clamp(barLength * pacePct / 100.0,
                                               radius, barLength - radius);
                    pacePx = (int)std::lround(center - paceThickness / 2.0);
                }

                int paceVisibleInt = paceVisible ? 1 : 0;
                if (paceVisibleInt != ap.paceVisible[w]) {
                    if (ui.paceTicks[w]) {
                        ui.paceTicks[w].Visibility(paceVisible ? Visibility::Visible :
                                                                Visibility::Collapsed);
                    }
                    ap.paceVisible[w] = paceVisibleInt;
                }
                if (paceVisible && pacePx != ap.pacePx[w]) {
                    if (ui.paceTicks[w]) {
                        ui.paceTicks[w].Margin(verticalBars ? Thickness{0, 0, 0, (double)pacePx} :
                                                               Thickness{(double)pacePx, 0, 0, 0});
                    }
                    ap.pacePx[w] = pacePx;
                }

                if (percentTextVisibility != PercentTextVisibility::Never) {
                    PercentTextAlignment appliedAlignment =
                        percentTextAlignment == PercentTextAlignment::Adaptive ?
                            (dispPct >= yellowThreshold ? PercentTextAlignment::Left :
                                                         PercentTextAlignment::Right) :
                            percentTextAlignment;
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
                                translation.X(appliedAlignment == PercentTextAlignment::Left ?
                                                  4 * physicalPixelDip :
                                              appliedAlignment == PercentTextAlignment::Center ?
                                                  0 : -4 * physicalPixelDip);
                            }
                        }
                        ap.percentAlignments[w] = (int)appliedAlignment;
                    }

                    bool yellowOrOrange = !stale && wu.pct >= 0 &&
                        ((wu.pct >= yellowThreshold && wu.pct < orangeThreshold) ||
                         (wu.pct >= orangeThreshold && wu.pct < redThreshold));
                    int darkText = appliedAlignment == PercentTextAlignment::Left &&
                                   yellowOrOrange ? 1 : 0;
                    if (darkText != ap.percentDark[w]) {
                        if (ui.percents[w]) {
                            ui.percents[w].Foreground(SolidColorBrush(darkText ?
                                winrt::Windows::UI::Color{255, 0, 0, 0} :
                                winrt::Windows::UI::Color{255, 255, 255, 255}));
                        }
                        ap.percentDark[w] = darkText;
                    }

                    std::wstring percentText;
                    if (wu.pct >= 0) {
                        wchar_t text[24];
                        if (w == kExtraUsageBar && showExtraBarAmounts &&
                            d.extraLimitAmount >= 0) {
                            // Amount follows the bar mode: spent in used mode, left in
                            // remaining mode. Dollars for Anthropic, credits for OpenAI.
                            double amount = barMode == BarMode::Remaining ?
                                d.extraLimitAmount - d.extraUsedAmount : d.extraUsedAmount;
                            amount = std::max(amount, 0.0);
                            swprintf(text, ARRAYSIZE(text),
                                     accounts[i].provider == L"openai" ? L"%.0f" : L"$%.2f",
                                     amount);
                        } else {
                            swprintf(text, ARRAYSIZE(text), L"%.0f%%", dispPct);
                        }
                        percentText = text;
                    }
                    if (percentText != ap.percentTexts[w]) {
                        if (ui.percents[w]) ui.percents[w].Text(percentText);
                        ap.percentTexts[w] = std::move(percentText);
                    }
                }
            }

            std::wstring tip = (warn ? L"! " : L"") + accounts[i].label + L" - " +
                               ProviderDisplayName(accounts[i].provider);
            bool planIsSpark = d.plan.find(L"Spark") != std::wstring::npos ||
                               d.plan.find(L"spark") != std::wstring::npos;
            bool hideSparkPlan = accounts[i].provider == L"openai" && planIsSpark &&
                                 !showOpenAiExtraLimits;
            if (!d.plan.empty() && !hideSparkPlan) {
                tip += L" (" + d.plan + L")";
            }
            wchar_t line[160];
            if (d.win5h.pct >= 0) {
                PCWSTR label = L"5h";
                if (accounts[i].provider == L"antigravity") {
                    label = d.win5h.windowDurationMs == 3ULL * 60 * 60 * 1000 ? L"Gemini 3h" :
                            d.win5h.windowDurationMs == 60ULL * 60 * 1000 ? L"Gemini 1h" :
                            d.win5h.windowDurationMs == 0 ?
                                L"Gemini current limit" : L"Gemini 5h";
                }
                swprintf(line, ARRAYSIZE(line), L"\n%s: %.0f%%%s | resets %s", label,
                         displayPct(d.win5h.pct),
                         remainingSuffix, FormatReset(d.win5h.resetUnixMs).c_str());
                tip += line;
            }
            if (d.winWeek.pct >= 0) {
                PCWSTR label = accounts[i].provider == L"antigravity" ? L"Gemini week" : L"week";
                swprintf(line, ARRAYSIZE(line), L"\n%s: %.0f%%%s | resets %s", label,
                         displayPct(d.winWeek.pct),
                         remainingSuffix, FormatReset(d.winWeek.resetUnixMs).c_str());
                tip += line;
            }
            if (accounts[i].provider == L"antigravity" &&
                d.antigravityThirdParty5h.pct >= 0) {
                PCWSTR label =
                    d.antigravityThirdParty5h.windowDurationMs == 3ULL * 60 * 60 * 1000 ?
                        L"Claude/GPT 3h" :
                    d.antigravityThirdParty5h.windowDurationMs == 60ULL * 60 * 1000 ?
                        L"Claude/GPT 1h" :
                    d.antigravityThirdParty5h.windowDurationMs == 0 ?
                        L"Claude/GPT current limit" : L"Claude/GPT 5h";
                swprintf(line, ARRAYSIZE(line), L"\n%s: %.0f%%%s | resets %s", label,
                         displayPct(d.antigravityThirdParty5h.pct), remainingSuffix,
                         FormatReset(d.antigravityThirdParty5h.resetUnixMs).c_str());
                tip += line;
            }
            if (accounts[i].provider == L"antigravity" &&
                d.antigravityThirdPartyWeek.pct >= 0) {
                swprintf(line, ARRAYSIZE(line),
                         L"\nClaude/GPT week: %.0f%%%s | resets %s",
                         displayPct(d.antigravityThirdPartyWeek.pct), remainingSuffix,
                         FormatReset(d.antigravityThirdPartyWeek.resetUnixMs).c_str());
                tip += line;
            }
            if (d.fableWeek.pct >= 0) {
                swprintf(line, ARRAYSIZE(line), L"\nFable week: %.0f%%%s",
                         displayPct(d.fableWeek.pct), remainingSuffix);
                tip += line;
                if (d.fableWeek.resetUnixMs) {
                    tip += L" | resets " + FormatReset(d.fableWeek.resetUnixMs);
                }
            }
            bool openAiAccount = accounts[i].provider == L"openai";
            // The credits percentage is only meaningful relative to the user's max, so with
            // the credits bar unchecked the tooltip falls back to the plain balance.
            if (d.extraUsage.pct >= 0 &&
                (!openAiAccount || accounts[i].showBars[kExtraUsageBar])) {
                // The slot holds Anthropic monthly extra usage or OpenAI credits vs. the
                // user's max.
                PCWSTR label = openAiAccount ? L"credits" : L"extra usage";
                PCWSTR suffix = barMode == BarMode::Remaining ?
                    (openAiAccount ? L" remaining" : L" remaining this month") :
                    (openAiAccount ? L" used" : L" monthly");
                swprintf(line, ARRAYSIZE(line), L"\n%s: %.1f%%%s", label,
                         displayPct(d.extraUsage.pct), suffix);
                tip += line;
                if (d.extraLimitAmount >= 0) {
                    // Credits state the balance explicitly so the number reads the same in
                    // used and remaining modes.
                    if (openAiAccount) {
                        swprintf(line, ARRAYSIZE(line), L" (%.0f left of %.0f)",
                                 d.extraLimitAmount - d.extraUsedAmount, d.extraLimitAmount);
                    } else {
                        swprintf(line, ARRAYSIZE(line), L" ($%.2f / $%.2f spent)",
                                 d.extraUsedAmount, d.extraLimitAmount);
                    }
                    tip += line;
                }
                if (d.extraUsage.resetUnixMs) {
                    tip += L" | resets " + FormatReset(d.extraUsage.resetUnixMs);
                }
            } else if (openAiAccount && d.creditsUnlimited) {
                tip += L"\ncredits: unlimited";
            } else if (openAiAccount && d.creditsBalance >= 0) {
                swprintf(line, ARRAYSIZE(line), L"\ncredits: %.0f", d.creditsBalance);
                tip += line;
            } else if (openAiAccount && d.hasCredits) {
                tip += L"\ncredits: available";
            }
            if (showOpenAiExtraLimits && accounts[i].provider == L"openai" && !d.openAiExtraLimitLines.empty()) {
                tip += L"\n" + d.openAiExtraLimitLines;
            }
            if (!d.extraLines.empty()) tip += L"\n" + d.extraLines;
            if (!d.error.empty()) {
                tip += L"\nerror: " + d.error;
                if (d.retryDeadlineMs > now) {
                    ULONGLONG retrySec = (d.retryDeadlineMs - now + 999) / 1000;
                    ULONGLONG days = retrySec / (24 * 60 * 60);
                    ULONGLONG hours = (retrySec / (60 * 60)) % 24;
                    ULONGLONG mins = (retrySec / 60) % 60;
                    ULONGLONG secs = retrySec % 60;
                    wchar_t retry[64];
                    if (days > 0) {
                        if (hours > 0) swprintf(retry, ARRAYSIZE(retry), L"%llud %lluh", days, hours);
                        else swprintf(retry, ARRAYSIZE(retry), L"%llud", days);
                    } else if (hours > 0) {
                        if (mins > 0) swprintf(retry, ARRAYSIZE(retry), L"%lluh %llum", hours, mins);
                        else swprintf(retry, ARRAYSIZE(retry), L"%lluh", hours);
                    } else if (mins > 0) {
                        if (secs > 0) swprintf(retry, ARRAYSIZE(retry), L"%llum %llus", mins, secs);
                        else swprintf(retry, ARRAYSIZE(retry), L"%llum", mins);
                    } else {
                        swprintf(retry, ARRAYSIZE(retry), L"%llus", secs);
                    }
                    tip += L" - retry in ";
                    tip += retry;
                }
            }
            tip += L"\n" + FormatUpdated(d.lastSuccessMs, stale);
            tip += visualTestMode ? L" - visual test; click to open settings" :
                   accountRefreshing ? L" - refreshing..." :
                   d.needsLogin ? L" - click to sign in" :
                   clickAction == ClickAction::OpenDashboard && accounts[i].provider != L"antigravity"
                       ? L" - click to open dashboard" :
                   L" - click to refresh";

            if (tip != ap.tip) {
                // Keep the attached ToolTip object alive so an in-place refresh doesn't reset
                // ToolTipService's pointer-over state and require a leave/re-enter cycle.
                if (ui.toolTip) UpdateQuotaToolTip(ui.toolTip, tip, !d.error.empty());
                ap.tip = tip;
            }

            double columnOpacity = accountRefreshing ? 0.65 : 1.0;
            if (columnOpacity != ap.columnOpacity) {
                if (ui.column) ui.column.Opacity(columnOpacity);
                ap.columnOpacity = columnOpacity;
            }

            double labelOpacity = stale ? 0.45 : 0.8;
            std::wstring labelText = warn ? accounts[i].label + L"!" : accounts[i].label;
            if (labelOpacity != ap.labelOpacity || labelText != ap.labelText) {
                if (ui.label) {
                    ui.label.Opacity(labelOpacity);
                    ui.label.Text(labelText);
                }
                ap.labelOpacity = labelOpacity;
                ap.labelText = std::move(labelText);
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
