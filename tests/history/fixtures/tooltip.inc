// Tooltip protocol, reentrancy, text lifetime and cancellation regressions.
static void RunTooltipTests() {
    Run("unchanged valid cursor suppresses tooltip/tracking/paint calls and synthetic recursion", [] {
        auto state = HoverFixture(); AttachGraph(state); POINT cursor = SegmentCursor(state);
        CHECK(state.hoverPoint.x == -1 && state.hoverPoint.y == -1);
        int callbacks = 0;
        tipCallback = [&](UINT) {
            ++callbacks;
            size_t calls = tipCalls.size();
            HistoryGraphWndProc(state.graph, WM_MOUSEMOVE, 0, MAKELPARAM(cursor.x, cursor.y));
            CHECK(tipCalls.size() == calls);
        };
        HistoryUpdateHover(state, cursor);
        CHECK(state.graphTipActive); CHECK(callbacks == 5);
        CHECK(state.hoverPoint.x == cursor.x && state.hoverPoint.y == cursor.y);
        size_t calls = tipCalls.size(), moves = TipMessages(TTM_TRACKPOSITION), tracking = trackingCalls.size();
        int repaint = invalidations, queries = clientQueries, rectQueries = tipRectQueries;
        for (int i = 0; i < 50; ++i) HistoryGraphWndProc(state.graph, WM_MOUSEMOVE, 0, MAKELPARAM(cursor.x, cursor.y));
        CHECK(tipCalls.size() == calls); CHECK(TipMessages(TTM_TRACKPOSITION) == moves);
        CHECK(trackingCalls.size() == tracking); CHECK(invalidations == repaint); CHECK(callbacks == 5);
        CHECK(tipRectQueries == rectQueries); CHECK(TipMessages(TTM_GETBUBBLESIZE) == 0);
        // The validity guard still intentionally reads the current client rect before deduplication.
        CHECK(clientQueries == queries + 50); CHECK(callbackErrors == 0);
        tipCallback = nullptr;
        HistoryClearHover(state); CHECK(state.hoverPoint.x == -1 && state.hoverPoint.y == -1);
        calls = tipCalls.size(); HistoryClearHover(state); CHECK(tipCalls.size() == calls);
        HistoryUpdateHover(state, cursor); CHECK(state.graphTipActive); CHECK(TipMessages(TTM_TRACKPOSITION) == moves + 2);
    });

    Run("active sample text replacement retains old/new storage through synchronous callback without hide/show", [] {
        auto state = HoverFixture(); POINT first = SegmentCursor(state, 0, .25), second = SegmentCursor(state, 0, .8);
        HistoryUpdateHover(state, first); CHECK(state.graphTipActive);
        const wchar_t* oldPointer = tipTextPointer; std::wstring oldText = tipText;
        size_t activations = TipMessages(TTM_TRACKACTIVATE), moves = TipMessages(TTM_TRACKPOSITION);
        int replacements = pointerReplacements, callbacks = 0;
        tipCallback = [&](UINT message) {
            if (message != TTM_UPDATETIPTEXTW) return;
            ++callbacks;
            CHECK(tipActive); CHECK(state.graphTipActive);
            CHECK(tipTextPointer == state.graphTipText.data()); CHECK(std::wstring(tipTextPointer) == state.graphTipText);
            CHECK(tipText != oldText); CHECK(std::wstring(oldPointer) == oldText);
            CHECK(tipText.find(HistoryLocalDate(state.from + 5*kHistoryDayMs)) != std::wstring::npos);
            size_t calls = tipCalls.size(); HistoryUpdateHover(state, second); CHECK(tipCalls.size() == calls);
            CHECK(std::wstring(oldPointer) == oldText);
        };
        HistoryUpdateHover(state, second); tipCallback = nullptr;
        CHECK(callbacks == 1); CHECK(pointerReplacements == replacements + 1);
        CHECK(TipMessages(TTM_TRACKACTIVATE) == activations); CHECK(TipMessages(TTM_TRACKPOSITION) == moves + 1);
        CHECK(tipActive && state.graphTipActive); CHECK(std::wstring(tipTextPointer) == state.graphTipText);
        CHECK(TipMessages(TTM_UPDATETIPTEXTW) == 2);
        POINT adjacent = second; ++adjacent.x;
        HistoryUpdateHover(state, adjacent); CHECK(TipMessages(TTM_UPDATETIPTEXTW) == 2);
        CHECK(TipMessages(TTM_TRACKACTIVATE) == activations); CHECK(TipMessages(TTM_TRACKPOSITION) == moves + 2);
        size_t beforeClear = tipCalls.size(); HistoryClearHover(state);
        CHECK(tipCalls.size() == beforeClear + 1); CHECK(tipCalls.back().message == TTM_TRACKACTIVATE && !tipCalls.back().value);
        CHECK(state.hoverPoint.x == -1 && state.hoverPoint.y == -1); CHECK(std::wstring(tipTextPointer) == state.graphTipText);
        HistoryClearHover(state); CHECK(tipCalls.size() == beforeClear + 1);
    });

    for (UINT dpi : {96U, 144U, 192U}) for (int edge : {1, 2, 3})
        Run("offscreen prime then real-size single visible move at monitor edge DPI=" + std::to_string(dpi) + "/edge=" + std::to_string(edge), [=] {
            auto state = HoverFixture(dpi); POINT cursor = SegmentCursor(state, 0, .8);
            POINT screen{cursor.x + 80, cursor.y + 40};
            tipSize = {ScaleForDpi(320, dpi), ScaleForDpi(110, dpi)};
            monitorWork = {0, 0, screen.x + (edge & 1 ? 40 : 1000), screen.y + (edge & 2 ? 30 : 1000)};
            int measurementsBeforeMove = 0;
            tipRectCallback = [&] {
                CHECK(tipActive); CHECK(state.graphTipActive); CHECK(TipMessages(TTM_TRACKPOSITION) == 1);
                CHECK(tipPosition.x == -32000 && tipPosition.y == -32000); ++measurementsBeforeMove;
            };
            tipCallback = [&](UINT message) {
                if (message == TTM_TRACKPOSITION && TipMessages(TTM_TRACKPOSITION) == 1) {
                    CHECK(!tipActive); CHECK(measurementsBeforeMove == 0);
                    CHECK(tipPosition.x == -32000 && tipPosition.y == -32000);
                }
                else if (message == TTM_TRACKPOSITION) CHECK(measurementsBeforeMove == 1);
            };
            HistoryUpdateHover(state, cursor); tipCallback = nullptr; tipRectCallback = nullptr;
            const UINT expected[] = {TTM_UPDATETIPTEXTW, TTM_SETMAXTIPWIDTH, TTM_TRACKPOSITION, TTM_TRACKACTIVATE, TTM_TRACKPOSITION};
            CHECK(tipCalls.size() == std::size(expected));
            for (size_t i = 0; i < std::size(expected); ++i) CHECK(tipCalls[i].message == expected[i]);
            CHECK((tipOperations == std::vector<UINT>{TTM_UPDATETIPTEXTW, TTM_SETMAXTIPWIDTH, TTM_TRACKPOSITION, TTM_TRACKACTIVATE, kTipRectQuery, TTM_TRACKPOSITION}));
            CHECK(TipMessages(TTM_TRACKPOSITION) == 2); CHECK(TipMessages(TTM_GETBUBBLESIZE) == 0);
            CHECK(tipRectQueries == 1);
            CHECK(tipPosition.x == (edge & 1 ? monitorWork.right-tipSize.cx : screen.x+ScaleForDpi(12, dpi)));
            CHECK(tipPosition.y == (edge & 2 ? std::max(0L, screen.y-tipSize.cy-ScaleForDpi(12, dpi)) : screen.y+ScaleForDpi(20, dpi)));
            CHECK(tipPosition.x >= monitorWork.left && tipPosition.x+tipSize.cx <= monitorWork.right);
            CHECK(tipPosition.y >= monitorWork.top && tipPosition.y+tipSize.cy <= monitorWork.bottom);
            size_t calls = tipCalls.size(); HistoryUpdateHover(state, cursor); CHECK(tipCalls.size() == calls);
            CHECK(tipRectQueries == 1);

            // Changed active text/size uses its new native rectangle and never re-primes or toggles visibility.
            tipCalls.clear(); tipOperations.clear(); tipSize.cx += 12; tipSize.cy += 10;
            POINT next = SegmentCursor(state, 0, .25), nextScreen{next.x+80, next.y+40};
            HistoryUpdateHover(state, next);
            CHECK((tipOperations == std::vector<UINT>{TTM_UPDATETIPTEXTW, TTM_SETMAXTIPWIDTH, kTipRectQuery, TTM_TRACKPOSITION}));
            CHECK(TipMessages(TTM_TRACKPOSITION) == 1); CHECK(TipMessages(TTM_TRACKACTIVATE) == 0); CHECK(tipRectQueries == 2);
            int expectedY = nextScreen.y+ScaleForDpi(20, dpi);
            if (expectedY+tipSize.cy > monitorWork.bottom) expectedY = nextScreen.y-tipSize.cy-ScaleForDpi(12, dpi);
            CHECK(tipPosition.x == std::clamp(nextScreen.x+ScaleForDpi(12, dpi), 0L, std::max(0L, monitorWork.right-tipSize.cx)));
            CHECK(tipPosition.y == std::clamp(LONG(expectedY), 0L, std::max(0L, monitorWork.bottom-tipSize.cy)));
            CHECK(state.graphTipActive && tipActive);
        });

    Run("V2 TOOLINFO sizes for create, initial activation, active replacement and deactivation", [] {
        auto state = HoverFixture(); state.graphTip = nullptr; AttachGraph(state);
        HistoryGraphWndProc(state.graph, WM_CREATE, 0, 0); CHECK(state.graphTip);
        HistoryUpdateHover(state, SegmentCursor(state));
        HistoryUpdateHover(state, SegmentCursor(state, 0, .8)); HistoryClearHover(state);
        int added = 0, updated = 0, activated = 0, deactivated = 0;
        for (const auto& call : tipCalls) {
            CHECK(call.message != TTM_GETBUBBLESIZE);
            if (call.message == TTM_ADDTOOLW || call.message == TTM_UPDATETIPTEXTW || call.message == TTM_TRACKACTIVATE)
                CHECK(call.toolSize == TTTOOLINFOW_V2_SIZE);
            if (call.message == TTM_ADDTOOLW) ++added;
            if (call.message == TTM_UPDATETIPTEXTW) ++updated;
            if (call.message == TTM_TRACKACTIVATE) { if (call.value) ++activated; else ++deactivated; }
        }
        CHECK(added == 1 && updated == 2 && activated == 1 && deactivated == 1);
        CHECK(callbackErrors == 0); CHECK(tipRectQueries == 2);
        HistoryGraphWndProc(state.graph, WM_DESTROY, 0, 0); CHECK(!state.graphTip);
    });

    for (bool active : {false, true}) for (int failure : {0, 1, 2, 3, 4})
        Run("invalid native tooltip rectangle clears hover/active=" + std::to_string(active) + "/kind=" + std::to_string(failure), [=] {
            auto state = HoverFixture(); POINT cursor = SegmentCursor(state, 0, .8);
            if (active) HistoryUpdateHover(state, SegmentCursor(state));
            tipCalls.clear(); tipOperations.clear();
            failTipRect = failure == 0;
            if (failure == 1) tipSize.cx = 0;
            if (failure == 2) tipSize.cy = 0;
            if (failure == 3) tipSize.cx = -1;
            if (failure == 4) tipSize.cy = -1;
            HistoryUpdateHover(state, cursor);
            CHECK(!state.graphTipActive && !tipActive); CHECK(state.hoverSeries.empty()); CHECK(!state.trackingMouse);
            CHECK(state.hoverPoint.x == -1 && state.hoverPoint.y == -1);
            CHECK(TipMessages(TTM_TRACKPOSITION) == size_t(active ? 0 : 1));
            CHECK(TipMessages(TTM_GETBUBBLESIZE) == 0);
            CHECK(tipOperations[tipOperations.size()-2] == kTipRectQuery);
            CHECK(tipCalls.back().message == TTM_TRACKACTIVATE && !tipCalls.back().value);
            CHECK(tipCalls.back().toolSize == TTTOOLINFOW_V2_SIZE);
            CHECK(trackingCalls.back() == (TME_CANCEL | TME_LEAVE));
            CHECK(tipTextPointer == state.graphTipText.data()); CHECK(std::wstring(tipTextPointer) == state.graphTipText);
            // A failed first measurement must not poison debounce or prevent a fresh offscreen prime.
            failTipRect = false; tipSize = {440, 220}; tipCalls.clear(); tipOperations.clear();
            HistoryUpdateHover(state, cursor); CHECK(state.graphTipActive && tipActive);
            CHECK(TipMessages(TTM_TRACKPOSITION) == 2);
            auto first = std::find_if(tipCalls.begin(), tipCalls.end(), [](const auto& call) { return call.message == TTM_TRACKPOSITION; });
            CHECK(first != tipCalls.end()); CHECK(GET_X_LPARAM(first->data) == -32000 && GET_Y_LPARAM(first->data) == -32000);
        });

    Run("cancel mode delegates to default procedure after clearing native hover", [] {
        auto state = HoverFixture(); AttachGraph(state); HistoryUpdateHover(state, LegendCursor(state));
        defWindowCalls.clear(); defWindowResult = 7341;
        LRESULT result = HistoryGraphWndProc(state.graph, WM_CANCELMODE, 0, 0);
        CHECK(result == defWindowResult); CHECK((defWindowCalls == std::vector<UINT>{WM_CANCELMODE}));
        CHECK(!state.graphTipActive && !tipActive); CHECK(state.hoverSeries.empty()); CHECK(!state.trackingMouse);
        CHECK(state.hoverPoint.x == -1 && state.hoverPoint.y == -1); CHECK(callbackErrors == 0);
    });

    for (bool destroyed : {false, true}) Run(std::string("null graph scroll makes no window calls/") + (destroyed ? "after-teardown" : "fresh"), [=] {
        auto state = HoverFixture();
        if (destroyed) {
            AttachGraph(state); HistoryUpdateHover(state, LegendCursor(state)); HWND graph = state.graph;
            HistoryGraphWndProc(graph, WM_DESTROY, 0, 0); HistoryGraphWndProc(graph, WM_NCDESTROY, 0, 0);
        } else { state.graph = nullptr; }
        CHECK(state.graph == nullptr);
        size_t calls = tipCalls.size(), tracking = trackingCalls.size();
        int repaint = invalidations, queries = clientQueries, scrolls = scrollCalls, measured = measurements, oldScroll = state.scroll;
        for (int position : {0, 20, INT_MAX}) HistoryScroll(state, position);
        CHECK(tipCalls.size() == calls); CHECK(trackingCalls.size() == tracking); CHECK(invalidations == repaint);
        CHECK(clientQueries == queries); CHECK(scrollCalls == scrolls); CHECK(measurements == measured); CHECK(state.scroll == oldScroll);
        CHECK(std::find(invalidationWindows.begin(), invalidationWindows.end(), nullptr) == invalidationWindows.end());
    });

    for (UINT dpi : {96U, 144U, 192U}) Run("single-account legend omits account prefix but keeps full hover title DPI=" + std::to_string(dpi), [=] {
        Surface surface(dpi); auto state = Fixture({1, 2, 3}); state.dpi = dpi; state.font = surface.font; state.graphTip = FakeHandle(90);
        AllLanes(state);
        for (size_t i = 0; i < state.snapshot.samples.size(); ++i) {
            auto& row = state.snapshot.samples[i]; row.timeMs = baseTime+(i%2 ? 5*kHistoryDayMs : hour); row.intervalMs = 3*kHistoryDayMs;
        }
        HistoryRebuildView(state);
        for (const auto& line : state.visible) CHECK(line.legendTitle.starts_with(state.accounts[line.account].title+L" · "));
        for (int account : {0, 1, 2}) {
            SelectAccount(state, account);
            for (const auto& line : state.visible) {
                CHECK(line.legendTitle.find(state.accounts[account].title) == std::wstring::npos);
                CHECK(line.hoverTitle.starts_with(state.accounts[account].title+L" · "));
            }
            HistoryUpdateHover(state, LegendCursor(state)); CHECK(tipText.starts_with(state.visible[0].hoverTitle));
            if (!account) { HistoryPaintGraph(state); surface.Save("single-account-legend", dpi); }
        }
    });

    for (bool unload : {false, true}) {
        Run(std::string("cancel during legend measurement releases DC without publishing/") + (unload ? "unload" : "worker"), [=] {
            auto state = HoverFixture(); HistoryUpdateHover(state, LegendCursor(state)); state.renderWidth = -1;
            int measured = 0, released = dcReleases;
            measurementCallback = [&] { ++measured; if (unload) g_unloading = true; else g_historyWindowCancelRequested = true; };
            HistoryRebuildRenderCache(state); measurementCallback = nullptr;
            CHECK(measured == 1); CHECK(dcReleases == released+1); CHECK(state.layoutWidth == -1); CHECK(state.renderWidth == -1);
            CHECK(state.hoverSeries.empty()); CHECK(!state.graphTipActive); CHECK(state.hoverPoint.x == -1 && state.hoverPoint.y == -1);
            g_unloading = g_historyWindowCancelRequested = false; HistoryRebuildRenderCache(state); CHECK(state.layoutWidth == clientRect.right);
        });
        Run(std::string("cancel during hover legend traversal clears active hover/") + (unload ? "unload" : "worker"), [=] {
            auto state = HoverFixture(); HistoryUpdateHover(state, LegendCursor(state)); int checks = 0;
            pointTestCallback = [&] { if (++checks == 2) { if (unload) g_unloading = true; else g_historyWindowCancelRequested = true; } };
            HistoryUpdateHover(state, SegmentCursor(state)); pointTestCallback = nullptr;
            CHECK(checks == 2); CHECK(!tipActive && !state.graphTipActive); CHECK(state.hoverSeries.empty());
            CHECK(state.hoverPoint.x == -1 && state.hoverPoint.y == -1); CHECK(!state.trackingMouse);
        });
    }
}
