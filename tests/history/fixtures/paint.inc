// Real offscreen GDI output at three DPIs.
    for (UINT dpi : {96U, 144U, 192U}) {
        Run("All accounts AllMeters has distinct percent/USD/credits axes DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi); auto state = Fixture({1, 2}); state.dpi = dpi; state.font = surface.font;
            state.accounts[0].showWeekly = state.accounts[0].showFableWeekly = false;
            AddRow(state, 0, baseTime + hour, {{1, 25}, {6, 250}});
            AddRow(state, 0, baseTime + 5*kHistoryDayMs, {{1, 75}, {6, 1000}});
            AddRow(state, 1, baseTime + hour, {{7, 10000}});
            AddRow(state, 1, baseTime + 5*kHistoryDayMs, {{7, 5000}});
            for (auto& sample : state.snapshot.samples) sample.intervalMs = 3*kHistoryDayMs;
            SelectView(state, HistoryView::AllMeters);
            CHECK(state.selected == -1); CHECK(state.allAccountsView == HistoryView::AllMeters);
            CHECK(controls[state.meterCombo].items.size() == 4); CHECK(controls[state.meterCombo].selection == 3);
            CHECK(state.visible.size() == 3); CHECK(state.noReadings.empty());
            for (const auto& axis : state.axes) { CHECK(axis.visible); CHECK(axis.low == 0); }
            CHECK(state.axes[0].high == 100); CHECK(state.axes[1].high == 1000); CHECK(state.axes[2].high == 10000);
            CHECK(HistoryPlotRightPadding(state) == ScaleForDpi(246, dpi));
            for (const auto& line : state.visible) { CHECK(line.plotted); CHECK(line.render[0].points.size() == 2); }
            textCalls.clear(); penCalls.clear(); polylineCalls = 0; HistoryPaintGraph(state);
            CHECK(Painted(L"All accounts · All meters")); CHECK(!Painted(L"details only"));
            CHECK(penCalls.size() == 6); CHECK(polylineCalls == 3);
            const std::array<std::array<std::wstring, 3>, 3> labels{{
                {{L"0%", L"50%", L"100%"}}, {{L"0 USD", L"500 USD", L"1000 USD"}},
                {{L"0 cr", L"5000 cr", L"10000 cr"}}}};
            std::array<std::array<RECT, 3>, 3> ticks{};
            for (size_t axis = 0; axis < labels.size(); ++axis) for (size_t tick = 0; tick < 3; ++tick) {
                int matches = 0;
                for (const auto& call : textCalls) if (call.text == labels[axis][tick]) {
                    ++matches; ticks[axis][tick] = call.rect;
                    CHECK((call.flags & DT_SINGLELINE) != 0);
                    CHECK(bool(call.flags & DT_RIGHT) == (axis == 0));
                    CHECK(call.rect.left >= 0); CHECK(call.rect.right <= clientRect.right);
                    CHECK(call.rect.top >= 0); CHECK(call.rect.bottom <= clientRect.bottom);
                    CHECK(call.rect.right > call.rect.left); CHECK(call.rect.bottom > call.rect.top);
                }
                CHECK(matches == 1);
            }
            for (size_t tick = 0; tick < 3; ++tick) {
                CHECK(ticks[0][tick].right < state.plot.left);
                CHECK(ticks[1][tick].left > state.plot.right);
                CHECK(ticks[1][tick].right < ticks[2][tick].left);
                CHECK(ticks[0][tick].top == ticks[1][tick].top);
                CHECK(ticks[1][tick].top == ticks[2][tick].top);
            }
            surface.Save("all-accounts-all-meters", dpi);
        });
        Run("credits-only axes/paint DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi); auto state = Fixture({2}); state.dpi = dpi; state.font = surface.font;
            AddRow(state, 0, baseTime + 1000, {{7, 900}}); AddRow(state, 0, baseTime + 61000, {{7, 850}});
            SelectView(state, HistoryView::ExtraUsage);
            CHECK(state.axes[0].visible); CHECK(state.axes[0].high == 100);
            CHECK(!state.axes[1].visible); CHECK(state.axes[2].visible);
            CHECK(HistoryPlotRightPadding(state) == ScaleForDpi(136, dpi));
            textCalls.clear(); penCalls.clear(); HistoryPaintGraph(state);
            int percentTicks = 0, creditTicks = 0;
            int left = ScaleForDpi(110, dpi), right = clientRect.right - HistoryPlotRightPadding(state);
            for (const auto& call : textCalls) {
                if (call.rect.top >= state.plot.top - ScaleForDpi(10, dpi) && call.rect.bottom <= state.plot.bottom + ScaleForDpi(12, dpi)) {
                    if (call.text.ends_with(L"%")) { ++percentTicks; CHECK(call.rect.right < left); CHECK((call.flags & DT_RIGHT) != 0); }
                    if (call.text.ends_with(L" cr")) { ++creditTicks; CHECK(call.rect.left > right); }
                    CHECK(!call.text.ends_with(L" USD"));
                }
            }
            CHECK(percentTicks == 3); CHECK(creditTicks == 3); CHECK(penCalls.size() == 2);
            surface.Save("credits-only", dpi);
        });
        Run("USD-only detail text/paint DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi, 1000, 390); auto state = Fixture({1}); state.dpi = dpi; state.font = surface.font;
            AddRow(state, 0, baseTime + 1000, {{6, 30}}); AddRow(state, 0, baseTime + 61000, {{6, 5}});
            SelectView(state, HistoryView::ExtraUsage);
            textCalls.clear(); penCalls.clear(); HistoryPaintGraph(state);
            CHECK(Painted(L"Dollar spending is shown below.")); CHECK(Painted(L"Last recorded spend: USD 5"));
            CHECK(Painted(L"Peak USD 30")); CHECK(!Painted(L"No accounts support this view."));
            CHECK(penCalls.empty()); CHECK(!state.axes[1].visible);
            surface.Save("usd-details", dpi);
            SelectAccount(state, 0); textCalls.clear(); penCalls.clear(); HistoryPaintGraph(state);
            CHECK(state.axes[1].visible); CHECK(!penCalls.empty()); CHECK(!Painted(L"Dollar spending is shown below."));
        });
        Run("Weekly GDI overlay, stroke widths and compact status DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi); auto state = Fixture({1, 2, 3, 1}); state.dpi = dpi; state.font = surface.font;
            state.overlay = true; state.readFrom -= kHistoryWeekMs; state.enabled = true;
            AllLanes(state, true); AllLanes(state); state.accounts[3].rows.clear(); state.accounts[3].recording = false;
            // Spread fixture readings across the week so captured strokes/dashes are visible.
            for (size_t row = 0; row < state.snapshot.samples.size(); ++row) {
                auto& sample = state.snapshot.samples[row];
                bool previous = sample.timeMs < state.from;
                sample.timeMs = (previous ? state.readFrom : state.from) + (row % 2 ? 5*kHistoryDayMs : hour);
                sample.intervalMs = 3*kHistoryDayMs;
                if (previous) for (auto& meter : sample.meters) meter.value -= 10;
            }
            HistoryRebuildView(state);
            textCalls.clear(); penCalls.clear(); polylineCalls = 0; HistoryPaintGraph(state);
            CHECK(Painted(L"Fable weekly")); CHECK(Painted(L"Gemini weekly")); CHECK(Painted(L"Claude/GPT weekly"));
            CHECK(penCalls.size() == 20); CHECK(polylineCalls == 10);
            for (size_t i = 0; i < state.visible.size(); ++i) {
                CHECK(penCalls[2*i].width == DWORD(ScaleForDpi(state.visible[i].stroke, dpi)));
                CHECK(penCalls[2*i+1].width == penCalls[2*i].width);
                CHECK((penCalls[2*i+1].style & PS_STYLE_MASK) == PS_DASH);
                CHECK((penCalls[2*i+1].style & PS_TYPE_MASK) == PS_GEOMETRIC);
            }
            surface.Save("weekly-overlay", dpi);
            HistoryScroll(state, INT_MAX); textCalls.clear(); HistoryPaintGraph(state);
            CHECK(Painted(L"No readings in range:")); CHECK(Painted(L"account-103  [recording off]"));
            HistoryScroll(state, 0);
            forceHighContrast = true; penCalls.clear(); HistoryPaintGraph(state);
            for (const auto& call : penCalls) CHECK(call.color == GetSysColor(COLOR_WINDOWTEXT));
        });
        Run("zero-account empty paint DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi, 1000, 200); auto state = Fixture({}); state.dpi = dpi; state.font = surface.font;
            for (auto view : {HistoryView::ShortTerm, HistoryView::Weekly, HistoryView::ExtraUsage, HistoryView::AllMeters}) {
                SelectView(state, view); textCalls.clear(); HistoryPaintGraph(state);
                CHECK(state.accounts.empty()); CHECK(state.visible.empty()); CHECK(state.noReadings.empty());
                CHECK(textCalls.size() == 1); CHECK(textCalls[0].text == L"No accounts to display.");
                CHECK(!Painted(L"No accounts support this view.")); CHECK(!Painted(L"No readings in range:"));
            }
            surface.Save("zero-accounts", dpi);
        });
        Run("unsupported versus supported-empty paint DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi, 1000, 400); auto state = Fixture({3}); state.dpi = dpi; state.font = surface.font;
            SelectView(state, HistoryView::ExtraUsage); textCalls.clear(); HistoryPaintGraph(state);
            CHECK(Painted(L"No accounts support this view.")); CHECK(!Painted(L"No readings in range:"));
            CHECK(!state.accounts.empty()); CHECK(state.visible.empty()); CHECK(state.noReadings.empty());
            CHECK(textCalls.size() == 1); CHECK(textCalls[0].text == L"No accounts support this view.");
            CHECK(!Painted(L"No accounts to display."));
            surface.Save("unsupported-view", dpi);
            SelectView(state, HistoryView::Weekly); textCalls.clear(); HistoryPaintGraph(state);
            CHECK(!Painted(L"No accounts support this view.")); CHECK(Painted(L"No readings in range:"));
            CHECK(!Painted(L"No accounts to display."));
            size_t accountEntries = 0;
            for (const auto& call : textCalls) if (call.text == state.accounts[0].title) ++accountEntries;
            CHECK(accountEntries == 1);
        });
    }
