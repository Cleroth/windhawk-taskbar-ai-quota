// Production grouping, geometry, picking, hover, paint and graph callback are extracted verbatim.
// Fixtures below set raw inputs and independent expectations; OS window/tooltip endpoints are fake.
static HistoryWindowState HoverFixture(UINT dpi = 96, HFONT font = nullptr) {
    auto state = Fixture({2, 2, 2});
    state.dpi = dpi; state.font = font; state.graphTip = FakeHandle(90);
    state.overlay = true; state.readFrom -= kHistoryWeekMs;
    for (size_t account = 0; account < 3; ++account) {
        state.accounts[account].title = L"OpenAI — " + std::to_wstring(account + 1);
        state.accounts[account].label = std::to_wstring(account + 1);
        for (int period : {1, 0}) for (ULONGLONG offset : {hour, 5 * kHistoryDayMs}) {
            auto& row = AddRow(state, account, (period ? state.readFrom : state.from) + offset,
                               {{1, account == 2 ? 53.0 : 50.0}});
            row.intervalMs = 3 * kHistoryDayMs;
        }
    }
    HistoryRebuildView(state);
    return state;
}

static POINT LegendCursor(const HistoryWindowState& state, size_t index = 0) {
    const auto& rect = state.visible.at(index).legendRect;
    return {(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2 - state.scroll};
}

static POINT SegmentCursor(const HistoryWindowState& state, size_t index = 0, double fraction = .25) {
    const auto& line = state.visible.at(index);
    const auto& points = line.render[0].points;
    POINT a = HistoryPlotPoint(state, line, points.front()), b = HistoryPlotPoint(state, line, points.back());
    return {LONG(a.x + (b.x - a.x) * fraction), LONG(a.y + (b.y - a.y) * fraction) - state.scroll};
}

static void AttachGraph(HistoryWindowState& state) {
    CREATESTRUCTW create{}; create.lpCreateParams = &state;
    HistoryGraphWndProc(state.graph, WM_NCCREATE, 0, reinterpret_cast<LPARAM>(&create));
}

static size_t TipMessages(UINT message) {
    return std::count_if(tipCalls.begin(), tipCalls.end(), [=](const auto& call) { return call.message == message; });
}

static void RunInteractionTests() {
    for (UINT dpi : {96U, 144U, 192U}) {
        Run("all coincident/nearby lines in both periods, raw tooltip times DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi); auto state = HoverFixture(dpi, surface.font);
            POINT cursor = SegmentCursor(state);
            auto hits = HistoryHitTest(state, cursor);
            CHECK(hits.size() == 6);
            for (size_t i = 0; i < hits.size(); ++i) {
                CHECK(hits[i].series == i / 2); CHECK(hits[i].period == int(i % 2)); CHECK(hits[i].point == 0);
                const auto& point = state.visible[hits[i].series].render[hits[i].period].points[hits[i].point];
                CHECK(state.snapshot.samples[point.sourceRow].timeMs == (hits[i].period ? state.readFrom : state.from) + hour);
            }
            HistoryUpdateHover(state, cursor);
            CHECK((state.hoverSeries == std::vector<size_t>{0, 1, 2})); CHECK(state.trackingMouse);
            CHECK(state.graphTipActive); CHECK(tipActive); CHECK(tipText == state.graphTipText);
            CHECK(tipTextPointer == state.graphTipText.data()); CHECK(std::wstring(tipTextPointer) == tipText);
            CHECK(Occurrences(tipText, L"[Previous week]") == 3); CHECK(Occurrences(tipText, L"[Current]") == 3);
            CHECK(Occurrences(tipText, HistoryLocalDate(state.readFrom + hour)) == 3);
            CHECK(Occurrences(tipText, HistoryLocalDate(state.from + hour)) == 3);
            CHECK(tipText.find(L"Nearby lines") == std::wstring::npos);
            CHECK(tipText.starts_with(L"1 · Weekly used (%) [Current]: "));
            CHECK(tipText.find(L"OpenAI") == std::wstring::npos);
            for (const auto& line : state.visible) {
                CHECK(line.legendTitle.starts_with(state.accounts[line.account].label + L" · "));
                CHECK(line.title.starts_with(state.accounts[line.account].label + L" · "));
                CHECK(line.legendTitle.find(L"OpenAI") == std::wstring::npos);
                CHECK(line.title.find(L"OpenAI") == std::wstring::npos);
            }
            CHECK(Occurrences(tipText, L"50%") == 4); CHECK(Occurrences(tipText, L"53%") == 2);
            size_t updates = TipMessages(TTM_UPDATETIPTEXTW), activations = TipMessages(TTM_TRACKACTIVATE);
            const wchar_t* retained = tipTextPointer;
            int repaint = invalidations;
            HistoryUpdateHover(state, cursor);
            CHECK(TipMessages(TTM_UPDATETIPTEXTW) == updates); CHECK(TipMessages(TTM_TRACKACTIVATE) == activations);
            CHECK(tipTextPointer == retained); CHECK(invalidations == repaint); CHECK(trackingCalls.size() == 1);
            HistoryUpdateHover(state, SegmentCursor(state, 0, .8));
            CHECK(Occurrences(tipText, HistoryLocalDate(state.from + 5 * kHistoryDayMs)) == 3);
            CHECK(Occurrences(tipText, HistoryLocalDate(state.readFrom + 5 * kHistoryDayMs)) == 3);
            CHECK(TipMessages(TTM_UPDATETIPTEXTW) == updates + 1);
        });

        Run("legend hover compact/full titles, plot color and width consistency DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi); auto state = Fixture({1, 3});
            state.dpi = dpi; state.font = surface.font; state.graphTip = FakeHandle(90);
            state.overlay = true; state.readFrom -= kHistoryWeekMs;
            AllLanes(state, true); AllLanes(state);
            for (size_t i = 0; i < state.snapshot.samples.size(); ++i) {
                auto& row = state.snapshot.samples[i];
                if (row.timeMs < state.from) for (auto& meter : row.meters) meter.value -= 10;
                row.timeMs = (row.timeMs < state.from ? state.readFrom : state.from) + (i % 2 ? 5*kHistoryDayMs : hour);
                row.intervalMs = 3*kHistoryDayMs;
            }
            HistoryRebuildView(state);
            for (const auto& line : state.visible) {
                CHECK(line.legendTitle.find(L" used (%)") == std::wstring::npos);
                CHECK(line.hoverTitle.find(L" used (%)") != std::wstring::npos);
                CHECK(line.legendRect.bottom < state.plot.top);
            }
            HistoryPaintGraph(state); surface.Save("weekly-legend-normal", dpi);
            HistoryUpdateHover(state, LegendCursor(state, 1));
            CHECK((state.hoverSeries == std::vector<size_t>{1}));
            CHECK(tipText.starts_with(state.visible[1].hoverTitle));
            CHECK(tipText.find(L"Solid / filled: current week. Dashed / hollow: previous week.") != std::wstring::npos);
            CHECK(tipText.find(L"[Current]") == std::wstring::npos);
            for (bool highContrast : {false, true}) {
                forceHighContrast = highContrast;
                penCalls.clear(); strokes.clear(); lines.clear(); textCalls.clear();
                HistoryPaintGraph(state);
                CHECK(penCalls.size() == state.visible.size() * 4); CHECK(strokes.size() == state.visible.size() * 2);
                for (size_t i = 0; i < state.visible.size(); ++i) {
                    const auto& line = state.visible[i];
                    COLORREF expected = line.color;
                    if (highContrast) expected = GetSysColor(i == 1 ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT);
                    else if (i != 1) {
                        auto bg = GetSysColor(COLOR_WINDOW);
                        expected = RGB((GetRValue(expected) + 3*GetRValue(bg))/4,
                            (GetGValue(expected) + 3*GetGValue(bg))/4, (GetBValue(expected) + 3*GetBValue(bg))/4);
                    }
                    CHECK(HistoryLineColor(state, i, highContrast) == expected);
                    CHECK(penCalls[2*i].color == expected); CHECK(penCalls[2*i+1].color == expected);
                    CHECK(penCalls[2*i].width == DWORD(ScaleForDpi(line.stroke, dpi)));
                    size_t plotIndex = i == 1 ? state.visible.size()-1 : i > 1 ? i-1 : i;
                    for (size_t period = 0; period < 2; ++period) {
                        const auto& stroke = strokes[2*plotIndex + period];
                        CHECK(stroke.pen.color == expected); CHECK(stroke.pen.width == penCalls[2*i].width);
                        CHECK((stroke.pen.style & PS_STYLE_MASK) == (period ? PS_SOLID : PS_DASH));
                        CHECK(stroke.points.size() == 2);
                        auto actual = HistoryPlotPoint(state, line, line.render[1-period].points[0]);
                        CHECK(stroke.points[0].x == actual.x); CHECK(stroke.points[0].y == actual.y);
                    }
                    auto label = std::find_if(textCalls.begin(), textCalls.end(), [&](const auto& t) { return t.text == line.legendTitle; });
                    CHECK(label != textCalls.end()); CHECK(label->color == GetSysColor(i == 1 ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT));
                    CHECK(lines[2*i].pen.width == penCalls[2*i].width); CHECK(lines[2*i].pen.color == expected);
                    CHECK(lines[2*i+1].pen.color == expected); CHECK((lines[2*i+1].pen.style & PS_STYLE_MASK) == PS_DASH);
                }
                surface.Save(highContrast ? "weekly-legend-hover-highcontrast" : "weekly-legend-hover", dpi);
            }
        });

        Run("all units independent projection and extra axes DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi); auto state = Fixture({1, 2}); state.dpi = dpi; state.font = surface.font;
            state.graphTip = FakeHandle(90);
            for (size_t account = 0; account < 2; ++account) for (int i = 0; i < 2; ++i) {
                auto& row = AddRow(state, account, baseTime + hour + i*hour,
                    {{3, i ? 100 : 25}, {6, i ? 1000 : 250}, {7, i ? 10000 : 2500}});
                row.intervalMs = hour;
            }
            state.allAccountsView = HistoryView::AllMeters;
            HistoryRebuildView(state);
            CHECK(state.visible.size() == 3); CHECK(state.axes[0].high == 100); CHECK(state.axes[1].high == 1000); CHECK(state.axes[2].high == 10000);
            CHECK(HistoryPlotRightPadding(state) == ScaleForDpi(246, dpi));
            for (const auto& line : state.visible) {
                POINT a = HistoryPlotPoint(state, line, line.render[0].points[0]);
                POINT b = HistoryPlotPoint(state, line, line.render[0].points[1]);
                CHECK(a.y == state.plot.bottom - (state.plot.bottom - state.plot.top)/4); CHECK(b.y == state.plot.top);
            }
            HistoryUpdateHover(state, SegmentCursor(state, 0, 0));
            CHECK(state.hoverSeries.size() == 3); CHECK(tipText.find(L"25%") != std::wstring::npos);
            CHECK(tipText.find(L"USD 250") != std::wstring::npos); CHECK(tipText.find(L"2500 credits") != std::wstring::npos);
            textCalls.clear(); HistoryPaintGraph(state);
            CHECK(Painted(L"100%")); CHECK(Painted(L"1000 USD")); CHECK(Painted(L"10000 cr"));
            CHECK(!Painted(L"No readings in this range."));
            // Capture a UI-reachable Extra view: percent + credits, dollars are details only.
            for (size_t i = 0; i < state.snapshot.samples.size(); ++i) {
                auto& row = state.snapshot.samples[i];
                row.timeMs = baseTime + (i % 2 ? 5*kHistoryDayMs : hour); row.intervalMs = 3*kHistoryDayMs;
                row.meters[7].value = i % 2 ? 5000 : 9000;
            }
            SelectView(state, HistoryView::ExtraUsage); HistoryPaintGraph(state); surface.Save("extra-axes", dpi);
        });

        for (int width : {1000, 480, 180, 40}) Run("cached wrapping layout/skinny DPI=" + std::to_string(dpi) + "/width=" + std::to_string(width), [=] {
            Surface surface(dpi, width, 640); auto state = Fixture({1, 2, 3, 1}); state.dpi = dpi; state.font = surface.font;
            for (size_t i = 0; i < state.accounts.size(); ++i)
                state.accounts[i].label = state.accounts[i].title = L"Long account title with spaces and & literal ampersand — team " + std::to_wstring(i);
            AllLanes(state);
            for (size_t i = 0; i < state.snapshot.samples.size(); ++i) {
                auto& row = state.snapshot.samples[i];
                row.timeMs = baseTime + (i % 2 ? 5*kHistoryDayMs : hour); row.intervalMs = 3*kHistoryDayMs;
            }
            HistoryRebuildView(state);
            CHECK(state.layoutWidth == clientRect.right); CHECK(state.renderWidth == std::max(0, int(clientRect.right) - ScaleForDpi(136, dpi)));
            CHECK(state.plot.right - state.plot.left == state.renderWidth); CHECK(state.graphHeight == state.plot.bottom + ScaleForDpi(44, dpi));
            CHECK(lastScroll.nMax == state.graphHeight + int(state.visible.size())*ScaleForDpi(kHistorySeriesHeight, dpi) - 1);
            int bottom = 0; std::set<LONG> rows;
            for (size_t i = 0; i < state.visible.size(); ++i) {
                const auto& rect = state.visible[i].legendRect;
                CHECK(rect.left >= ScaleForDpi(12, dpi)); CHECK(rect.right <= clientRect.right - ScaleForDpi(12, dpi));
                CHECK(rect.bottom > rect.top); CHECK(rect.bottom < state.plot.top);
                bottom = std::max(bottom, int(rect.bottom)); rows.insert(rect.top);
                for (size_t j = 0; j < i; ++j) { RECT intersection{}; CHECK(!IntersectRect(&intersection, &rect, &state.visible[j].legendRect)); }
            }
            CHECK(rows.size() > 1); CHECK(state.plot.top == bottom + ScaleForDpi(10, dpi));
            int measured = measurements;
            HistoryRebuildRenderCache(state); CHECK(measurements == measured);
            auto* points = state.visible[0].render[0].points.data();
            HistoryScroll(state, 20); CHECK(measurements == measured); CHECK(points == state.visible[0].render[0].points.data());
            textCalls.clear(); HistoryPaintGraph(state); CHECK(measurements == measured);
            for (const auto& text : textCalls) CHECK(!(text.flags & DT_CALCRECT));
            if (width == 480) { HistoryScroll(state, 0); HistoryPaintGraph(state); surface.Save("long-wrap", dpi); }
            if (width == 40) { CHECK(state.renderWidth == 0); CHECK(HistoryHitTest(state, {state.plot.left, state.plot.top}).empty()); }
            // Width change, including zero-width plots, must invalidate layout and not only reduction.
            clientRect.right += ScaleForDpi(8, dpi); HistoryScroll(state, 0);
            CHECK(measurements == measured + 1); CHECK(state.layoutWidth == clientRect.right);
        });

        Run("scroll aligns legend/graph picking and painting DPI=" + std::to_string(dpi), [=] {
            Surface surface(dpi, 850, 330); auto state = HoverFixture(dpi, surface.font);
            HistoryScroll(state, ScaleForDpi(25, dpi)); CHECK(state.scroll == ScaleForDpi(25, dpi));
            POINT cursor = LegendCursor(state, 1); HistoryUpdateHover(state, cursor); CHECK((state.hoverSeries == std::vector<size_t>{1}));
            HistoryPaintGraph(state); surface.Save("scrolled-legend", dpi);
            HistoryScroll(state, ScaleForDpi(70, dpi)); CHECK(state.scroll == ScaleForDpi(70, dpi));
            cursor = SegmentCursor(state); HistoryUpdateHover(state, cursor); CHECK(state.hoverSeries.size() == 3);
            strokes.clear(); textCalls.clear(); HistoryPaintGraph(state); CHECK(strokes.size() == 6);
            CHECK(strokes[0].points[0].y == HistoryPlotPoint(state, state.visible[0], state.visible[0].render[1].points[0]).y - state.scroll);
            surface.Save("scrolled-graph", dpi);
        });
    }

    for (int kind = 0; kind < 10; ++kind) Run("gap interior not pickable/reset glyph absent kind=" + std::to_string(kind), [=] {
        Surface surface(96); auto state = Fixture({2}); state.graphTip = FakeHandle(90); state.to = baseTime + 600000;
        AddRow(state, 0, baseTime + 60000, {{1, 50}});
        AddRow(state, 0, baseTime + 300000, {{1, 50}});
        state.snapshot.samples[0].intervalMs = state.snapshot.samples[1].intervalMs = 300000;
        if (kind == 0) state.snapshot.samples[1].session++;
        if (kind == 1) state.snapshot.samples[1].meters[1].durationMs += hour;
        if (kind == 2) { state.snapshot.samples[0].meters[1].resetMs = baseTime + hour; state.snapshot.samples[1].meters[1].resetMs = baseTime + 2*hour; }
        if (kind == 3) state.snapshot.samples[0].meters[1].resetMs = state.snapshot.samples[1].meters[1].resetMs = baseTime + 180000;
        if (kind == 4) state.snapshot.samples[1].intervalMs = 0;
        if (kind == 5) state.snapshot.samples[0].intervalMs = state.snapshot.samples[1].intervalMs = 60000;
        if (kind == 6) state.snapshot.samples[1].provider = 3;
        if (kind == 7) state.snapshot.samples[1].accountId++;
        if (kind == 8 || kind == 9) {
            auto& missing = AddRow(state, 0, baseTime + 180000, {{1, -1}});
            missing.intervalMs = 300000;
            if (kind == 9) { missing.meters[1].value = 50; missing.failed = 1; }
            state.accounts[0].rows = {0, 2, 1};
        }
        HistoryRebuildView(state); CHECK(state.visible[0].render[0].points.size() == 2);
        CHECK(!state.visible[0].render[0].points[1].connect);
        CHECK(HistoryHitTest(state, SegmentCursor(state, 0, .5)).empty());
        for (const auto& point : state.visible[0].render[0].points) {
            auto hits = HistoryHitTest(state, HistoryPlotPoint(state, state.visible[0], point)); CHECK(hits.size() == 1);
        }
        strokes.clear(); lines.clear(); ellipses.clear(); HistoryPaintGraph(state);
        CHECK(strokes.empty()); CHECK(ellipses.size() == 2);
        // Current-only legend has one horizontal key, plot three grid lines, details one separator.
        CHECK(lines.size() == 5);
        for (const auto& line : lines) CHECK(line.a.y == line.b.y); // No vertical reset glyph stems.
        CHECK(state.visible[0].current.coverageMs == 0);
    });

    Run("sloping segment nearest actual retained endpoint, never interpolation", [] {
        auto state = Fixture({2}); state.graphTip = FakeHandle(90); state.to = baseTime + 600000;
        AddRow(state, 0, baseTime + 60000, {{1, 20}}).intervalMs = 300000;
        AddRow(state, 0, baseTime + 360000, {{1, 80}}).intervalMs = 300000;
        HistoryRebuildView(state);
        for (auto [fraction, endpoint] : {std::pair{.25, size_t(0)}, std::pair{.75, size_t(1)}}) {
            POINT cursor = SegmentCursor(state, 0, fraction);
            auto hits = HistoryHitTest(state, cursor); CHECK(hits.size() == 1); CHECK(hits[0].point == endpoint);
            HistoryUpdateHover(state, cursor);
            CHECK(tipText.find(endpoint ? L"80%" : L"20%") != std::wstring::npos);
            CHECK(tipText.find(HistoryLocalDate(baseTime + (endpoint ? 360000 : 60000))) != std::wstring::npos);
        }
        POINT miss = SegmentCursor(state, 0, .5); miss.y += 20; CHECK(HistoryHitTest(state, miss).empty());
        CHECK(HistoryHitTest(state, {state.plot.left-1, state.plot.top}).empty());
        CHECK(HistoryHitTest(state, {state.plot.right+1, state.plot.top}).empty());
        CHECK(HistoryHitTest(state, {state.plot.left, state.plot.top-1}).empty());
        CHECK(HistoryHitTest(state, {state.plot.left, state.plot.bottom+1}).empty());
    });

    Run("reduced same-pixel extrema report retained raw sample not bucket time", [] {
        Surface surface(96, 137, 400); auto state = Fixture({2}); state.graphTip = FakeHandle(90);
        const double values[] = {20, 30, 5, 100, 40, 60};
        for (size_t i = 0; i < std::size(values); ++i) AddRow(state, 0, baseTime + 1000 + i*60000, {{1, values[i]}});
        HistoryRebuildView(state); CHECK(state.renderWidth == 1); CHECK(state.visible[0].render[0].points.size() == 4);
        auto cursor = HistoryPlotPoint(state, state.visible[0], state.visible[0].render[0].points[1]);
        auto hits = HistoryHitTest(state, cursor); CHECK(hits.size() == 1);
        const auto& point = state.visible[0].render[0].points[hits[0].point]; CHECK(point.sourceRow == 2); CHECK(point.value == 5);
        HistoryUpdateHover(state, cursor); CHECK(tipText.find(L"5%") != std::wstring::npos);
        CHECK(tipText.find(HistoryLocalDate(baseTime + 121000)) != std::wstring::npos);
    });

    Run("lone readings both periods, absent overlay and detail-only not hit", [] {
        auto state = Fixture({2}); state.graphTip = FakeHandle(90); state.overlay = true; state.readFrom -= kHistoryWeekMs;
        AddRow(state, 0, state.readFrom + hour, {{1, 50}}); AddRow(state, 0, state.from + hour, {{1, 50}});
        HistoryRebuildView(state);
        POINT cursor = HistoryPlotPoint(state, state.visible[0], state.visible[0].render[0].points[0]);
        auto hits = HistoryHitTest(state, cursor); CHECK(hits.size() == 2); CHECK(hits[0].period == 0); CHECK(hits[1].period == 1);
        cursor.x += 20; CHECK(HistoryHitTest(state, cursor).empty()); cursor.x -= 20;
        state.overlay = false; hits = HistoryHitTest(state, cursor); CHECK(hits.size() == 1); CHECK(hits[0].period == 0);
        state.visible[0].plotted = false; CHECK(HistoryHitTest(state, cursor).empty());
        state.visible[0].plotted = true; ++state.renderWidth; CHECK(HistoryHitTest(state, cursor).empty());
        auto usd = Fixture({1}); AddRow(usd, 0, baseTime+hour, {{6, 20}}); SelectView(usd, HistoryView::ExtraUsage);
        CHECK(usd.visible[0].legendRect.right == 0); CHECK(HistoryHitTest(usd, {usd.plot.left, usd.plot.bottom}).empty());
    });

    for (int meter : {1, 6, 7}) Run("projection finite extreme range unit=" + std::to_string(meter), [=] {
        auto state = Fixture({meter == 7 ? 2U : 1U}); state.selected = 0;
        double max = std::numeric_limits<double>::max();
        AddRow(state, 0, baseTime+hour, {{meter, -max}}).intervalMs = hour;
        AddRow(state, 0, baseTime+2*hour, {{meter, 0}}).intervalMs = hour;
        AddRow(state, 0, baseTime+3*hour, {{meter, max}}).intervalMs = hour;
        HistoryRebuildView(state); CHECK(state.visible.size() == 1);
        const auto& line = state.visible[0]; CHECK(line.render[0].points.size() == 3);
        CHECK(HistoryPlotPoint(state, line, line.render[0].points[0]).y == state.plot.bottom);
        CHECK(HistoryPlotPoint(state, line, line.render[0].points[1]).y == state.plot.bottom - (state.plot.bottom-state.plot.top)/2);
        CHECK(HistoryPlotPoint(state, line, line.render[0].points[2]).y == state.plot.top);
        for (const auto& point : line.render[0].points) CHECK(HistoryHitTest(state, HistoryPlotPoint(state, line, point)).size() == 1);
        CHECK(std::isfinite(line.current.coverageMs)); CHECK(std::isfinite(line.current.nearLimitMs));
        auto clamped = line.render[0].points[0]; state.axes[meter < 6 ? 0 : meter-5] = {0, 100, true};
        CHECK(HistoryPlotPoint(state, line, clamped).y == state.plot.bottom); clamped.value = max;
        CHECK(HistoryPlotPoint(state, line, clamped).y == state.plot.top);
    });

    for (std::string mode : {"view", "account", "rebuild", "data", "width", "height", "scroll", "leave", "cancel", "focus", "destroy", "loading", "modal", "closing", "unload", "cancel-worker", "outside", "stale-width", "stale-render"})
        Run("hover invalidation and retained tooltip buffer/" + mode, [=] {
            auto state = HoverFixture(); AttachGraph(state);
            HistoryUpdateHover(state, LegendCursor(state)); CHECK(state.graphTipActive); CHECK(state.trackingMouse);
            std::wstring text = state.graphTipText; const wchar_t* retained = tipTextPointer;
            size_t tracks = trackingCalls.size(); int repaint = invalidations;
            if (mode == "view") SelectView(state, HistoryView::ShortTerm);
            if (mode == "account") SelectAccount(state, 0);
            if (mode == "rebuild") { state.renderWidth = -1; HistoryRebuildRenderCache(state); }
            if (mode == "data") { state.snapshot.samples[0].meters[1].value = 10; HistoryRebuildView(state); }
            if (mode == "width") { --clientRect.right; HistoryGraphWndProc(state.graph, WM_SIZE, 0, 0); }
            if (mode == "height") { clientRect.bottom = 400; HistoryGraphWndProc(state.graph, WM_SIZE, 0, 0); }
            if (mode == "scroll") HistoryScroll(state, 20);
            if (mode == "leave") HistoryGraphWndProc(state.graph, WM_MOUSELEAVE, 0, 0);
            if (mode == "cancel") HistoryGraphWndProc(state.graph, WM_CANCELMODE, 0, 0);
            if (mode == "focus") HistoryGraphWndProc(state.graph, WM_KILLFOCUS, 0, 0);
            if (mode == "destroy") HistoryGraphWndProc(state.graph, WM_DESTROY, 0, 0);
            if (mode == "loading") state.loading = true;
            if (mode == "modal") state.modal = true;
            if (mode == "closing") state.closing = true;
            if (mode == "unload") g_unloading = true;
            if (mode == "cancel-worker") g_historyWindowCancelRequested = true;
            if (mode == "stale-width") ++clientRect.right;
            if (mode == "stale-render") state.renderWidth = -1;
            if (mode == "outside") HistoryUpdateHover(state, {-1, -1});
            else if (!state.hoverSeries.empty()) HistoryUpdateHover(state, LegendCursor(state));
            CHECK(state.hoverSeries.empty()); CHECK(!state.graphTipActive); CHECK(!tipActive); CHECK(!state.trackingMouse);
            CHECK(trackingCalls.size() == tracks + 1); CHECK(trackingCalls.back() == (TME_CANCEL | TME_LEAVE));
            CHECK(invalidations > repaint); CHECK(state.graphTipText == text); CHECK(state.graphTipText.data() == retained);
            if (mode == "destroy") { CHECK(state.graphTip == nullptr); CHECK(destroyedWindows.size() == 1); CHECK(tipTextPointer == nullptr); }
            else { CHECK(tipTextPointer == retained); CHECK(std::wstring(tipTextPointer) == text); }
            CHECK(callbackErrors == 0);
        });

    Run("DPI layout recreates font and cache, clears hover before resize", [] {
        auto state = HoverFixture(); state.window = FakeHandle(10);
        for (UINT dpi : {96U, 144U, 192U, 96U}) {
            HistoryUpdateHover(state, LegendCursor(state)); CHECK(state.hoverSeries.size() == 1);
            state.dpi = dpi; windowRects[state.window] = {0, 0, ScaleForDpi(1024, dpi), ScaleForDpi(800, dpi)};
            int measured = measurements; HistoryLayout(state, true);
            CHECK(measurements == measured+1); CHECK(state.hoverSeries.empty()); CHECK(state.font != nullptr);
            CHECK(state.layoutWidth == ScaleForDpi(1000, dpi)); CHECK(state.plot.bottom-state.plot.top == ScaleForDpi(166, dpi));
            CHECK(state.graphHeight == state.plot.bottom+ScaleForDpi(44, dpi));
        }
        DeleteObject(state.font); state.font = nullptr;
    });

    Run("cache DC failure remains unpublished and retry recovers", [] {
        auto state = HoverFixture(); HistoryUpdateHover(state, LegendCursor(state));
        --clientRect.right; failGetDc = true; HistoryRebuildRenderCache(state);
        CHECK(state.layoutWidth == -1); CHECK(state.hoverSeries.empty()); CHECK(!state.graphTipActive);
        failGetDc = false; HistoryRebuildRenderCache(state); CHECK(state.layoutWidth == clientRect.right);
        HistoryUpdateHover(state, LegendCursor(state)); CHECK(state.graphTipActive);
    });

    for (int failure : {0, 1, 2}) Run("fake tooltip create/add/teardown seam failure=" + std::to_string(failure), [=] {
        auto state = HoverFixture(); state.graphTip = nullptr; AttachGraph(state);
        failTipCreate = failure == 1; failTipAdd = failure == 2;
        HistoryGraphWndProc(state.graph, WM_CREATE, 0, 0);
        CHECK((state.graphTip != nullptr) == (failure == 0));
        if (failure == 2) CHECK(destroyedWindows.size() == 1);
        HistoryUpdateHover(state, LegendCursor(state)); CHECK(state.hoverSeries.size() == 1);
        CHECK(state.graphTipActive == (failure == 0));
        if (!failure) CHECK(tipTextPointer == state.graphTipText.data());
        HWND graph = state.graph;
        HistoryGraphWndProc(graph, WM_DESTROY, 0, 0); CHECK(!state.graphTip); CHECK(state.hoverSeries.empty()); CHECK(!tipTextPointer);
        HistoryGraphWndProc(graph, WM_NCDESTROY, 0, 0); CHECK(!state.graph); CHECK(windowData[graph] == 0);
        CHECK(callbackErrors == 0);
    });

    Run("tooltip bounded placement and absent tracking support", [] {
        auto state = HoverFixture(); monitorWork = {0, 0, 700, 300}; tipSize = {440, 220};
        failTracking = true; HistoryUpdateHover(state, SegmentCursor(state, 0, .8));
        CHECK(!state.trackingMouse); CHECK(state.graphTipActive); CHECK(!state.hoverSeries.empty());
        CHECK(tipPosition.x >= monitorWork.left); CHECK(tipPosition.y >= monitorWork.top);
        CHECK(tipPosition.x+tipSize.cx <= monitorWork.right); CHECK(tipPosition.y+tipSize.cy <= monitorWork.bottom);
        monitorWork = {0, 0, 1920, 1080};
    });

    Run("blank states and keyboard focus retained with cached paints", [] {
        Surface surface(96, 900, 400); auto state = HoverFixture(96, surface.font); AttachGraph(state);
        HistoryGraphWndProc(state.graph, WM_LBUTTONDOWN, 0, 0); CHECK(focusWindow == state.graph);
        HistoryPaintGraph(state); CHECK(focusRects == 1);
        int measured = measurements; clientRect.right -= 1; textCalls.clear();
        HistoryPaintGraph(state); CHECK(textCalls.empty()); CHECK(measurements == measured); // Intentional transient stale-cache blank.
        HistoryGraphWndProc(state.graph, WM_SIZE, 0, 0); HistoryPaintGraph(state);
        CHECK(!textCalls.empty()); CHECK(measurements == measured+1); CHECK(focusRects == 2);
        MSG key{}; key.message = WM_KEYDOWN;
        for (WPARAM code : {VK_PRIOR, VK_NEXT, VK_HOME, VK_END}) { key.wParam = code; CHECK(HistoryGraphWndProc(state.graph, WM_GETDLGCODE, 0, LPARAM(&key)) == DLGC_WANTMESSAGE); }
        key.wParam = VK_TAB; CHECK(HistoryGraphWndProc(state.graph, WM_GETDLGCODE, 0, LPARAM(&key)) == DLGC_WANTARROWS);
        HistoryGraphWndProc(state.graph, WM_KEYDOWN, VK_END, 0); CHECK(state.scroll > 0);
        HistoryGraphWndProc(state.graph, WM_KEYDOWN, VK_HOME, 0); CHECK(state.scroll == 0);
        HistoryGraphWndProc(state.graph, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), 0); CHECK(state.scroll == 60);
        CHECK(HistoryGraphWndProc(state.graph, WM_ERASEBKGND, 0, 0) == 1); CHECK(callbackErrors == 0);
    });

    Run("unchanged empty-state early return and supported-empty focus", [] {
        Surface surface(96, 900, 400); auto state = Fixture({}); state.font = surface.font;
        focusWindow = state.graph; HistoryRebuildView(state); textCalls.clear(); HistoryPaintGraph(state);
        CHECK(textCalls.size() == 1); CHECK(Painted(L"No accounts to display.")); CHECK(focusRects == 0);
        state = Fixture({3}); state.font = surface.font; SelectView(state, HistoryView::ExtraUsage);
        textCalls.clear(); HistoryPaintGraph(state); CHECK(textCalls.size() == 1); CHECK(Painted(L"No accounts support this view.")); CHECK(focusRects == 0);
        SelectView(state, HistoryView::Weekly); textCalls.clear(); HistoryPaintGraph(state);
        CHECK(Painted(L"No readings in range:")); CHECK(focusRects == 1);
    });

    Run("OpenAI rolling idle and first-use anchor only, shared statistics/reduction", [] {
        for (double endValue : {0.0, 10.0}) {
            auto state = Fixture({2});
            auto& a = AddRow(state, 0, baseTime+hour, {{1, 0}}); a.intervalMs = 300000; a.meters[1].resetMs = a.timeMs+kHistoryWeekMs;
            auto& b = AddRow(state, 0, baseTime+hour+300000, {{1, endValue}}); b.intervalMs = 300000; b.meters[1].resetMs = b.timeMs+kHistoryWeekMs;
            CHECK(HistoryConnection(state.snapshot.samples[0], state.snapshot.samples[1], 1) == HistoryBreak::None);
            HistoryRebuildView(state); CHECK(state.visible[0].render[0].points[1].connect); CHECK(state.visible[0].current.coverageMs == 300000);
            for (int kind = 0; kind < 5; ++kind) {
                auto first = state.snapshot.samples[0], second = state.snapshot.samples[1];
                if (kind == 0) first.provider = second.provider = 1;
                if (kind == 1) first.meters[1].value = 1;
                if (kind == 2) std::swap(first.meters[1].resetMs, second.meters[1].resetMs);
                if (kind == 3) first.meters[1].durationMs -= hour;
                if (kind == 4) second.session++;
                CHECK(HistoryConnection(first, second, 1) != HistoryBreak::None);
            }
        }
    });

    Run("real GDI saved-DC pen lifetime across repeated paints/highlight toggles", [] {
        Surface surface(96); auto state = HoverFixture(96, surface.font);
        HistoryPaintGraph(state); GdiFlush();
        DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        CHECK(before > 0); CHECK(deleteFailures == 0); int failuresBefore = deleteFailures;
        constexpr int paintCount = 120;
        for (int i = 0; i < paintCount; ++i) {
            if (i % 3) HistoryUpdateHover(state, LegendCursor(state, i % state.visible.size()));
            else HistoryClearHover(state);
            forceHighContrast = (i % 4) == 0;
            textCalls.clear(); penCalls.clear(); strokes.clear(); lines.clear(); ellipses.clear();
            HistoryPaintGraph(state); GdiFlush();
            CHECK(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == before);
        }
        DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        std::cout << "GDI_RESOURCES before=" << before << " after=" << after << " paints=" << paintCount << " delete_failures=" << deleteFailures-failuresBefore << '\n';
        CHECK(after == before); CHECK(deleteFailures == failuresBefore);
    });
}
