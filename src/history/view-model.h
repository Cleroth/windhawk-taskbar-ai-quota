// Classic history window over the shared model (history/series.h): dates, render cache, hover
// hit-testing, and loading.

// Dates on-screen are local; CSV dates are UTC.
static std::wstring HistoryLocalDate(ULONGLONG time) {
    SYSTEMTIME local{};
    if (!UnixMsToLocalSystemTime(time, &local)) return L"unknown local time";
    wchar_t text[48];
    swprintf(text, ARRAYSIZE(text), L"%04u-%02u-%02u %02u:%02u:%02u",
             local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
    return text;
}

// The render cache's reduction, stopped by unload or window close.
static HistoryRenderSeries HistoryReducePeriod(const HistorySnapshot& snapshot,
                                               const std::vector<size_t>& rows,
                                               int meter, ULONGLONG from,
                                               ULONGLONG to, int width) {
    return HistoryReduceRows(snapshot.samples, rows, meter, from, to, width,
                             [] { return g_unloading || g_historyWindowCancelRequested; });
}

// Cache reduction and painting reserve identical space for additional unit axes.
static int HistoryPlotRightPadding(const HistoryWindowState& state) {
    int axes = 0;
    for (const auto& axis : state.axes) axes += axis.visible;
    return ScaleForDpi(26 + std::max(0, axes - 1) * 110, state.dpi);
}

static void HistoryClearHover(HistoryWindowState& state) {
    if (state.graphTip && state.graphTipActive) {
        TOOLINFOW tool{};
        tool.cbSize = TTTOOLINFOW_V2_SIZE;
        tool.hwnd = state.graph;
        tool.uId = 1;
        SendMessageW(state.graphTip, TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    }
    state.graphTipActive = false;
    state.hoverPoint = {-1, -1};
    // The native tool can retain lpszText even while hidden; do not discard its backing string.
    if (!state.hoverSeries.empty()) {
        state.hoverSeries.clear();
        if (state.graph) InvalidateRect(state.graph, nullptr, FALSE);
    }
    if (state.trackingMouse && state.graph) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_CANCEL | TME_LEAVE, state.graph, 0};
        TrackMouseEvent(&tracking);
    }
    state.trackingMouse = false;
}

// One projection for drawing and picking, including large finite values and unlike units.
static POINT HistoryPlotPoint(const HistoryWindowState& state, const HistoryUiSeries& series,
                              const HistoryRenderPoint& point) {
    const auto& axis = state.axes[series.meter < 6 ? 0 : series.meter - 5];
    double magnitude = std::max({1.0, std::abs(axis.low), std::abs(axis.high)});
    double low = axis.low / magnitude, span = axis.high / magnitude - low;
    double fraction = span > 0 ? (point.value / magnitude - low) / span : 0;
    return {state.plot.left + point.x,
            state.plot.bottom - int(std::clamp(fraction, 0.0, 1.0) * (state.plot.bottom - state.plot.top))};
}

static COLORREF HistoryLineColor(const HistoryWindowState& state, size_t index, bool highContrast) {
    COLORREF color = highContrast ? GetSysColor(COLOR_WINDOWTEXT) : state.visible[index].color;
    if (state.hoverSeries.empty() ||
        std::find(state.hoverSeries.begin(), state.hoverSeries.end(), index) != state.hoverSeries.end()) return color;
    if (highContrast) return GetSysColor(COLOR_GRAYTEXT);
    COLORREF background = GetSysColor(COLOR_WINDOW);
    return RGB((GetRValue(color) + 3 * GetRValue(background)) / 4,
               (GetGValue(color) + 3 * GetGValue(background)) / 4,
               (GetBValue(color) + 3 * GetBValue(background)) / 4);
}

// Only selection/snapshot/size handlers build caches; WM_PAINT never scans raw rows.
// Selection rebuilds invalidate renderWidth even if the physical plot width is unchanged.
static void HistoryRebuildRenderCache(HistoryWindowState& state) {
    RECT client{};
    if (!state.graph || !GetClientRect(state.graph, &client)) return;
    int width = std::max(0, int(client.right) - ScaleForDpi(110, state.dpi) - HistoryPlotRightPadding(state));
    if (width == state.renderWidth && state.layoutWidth == client.right) return;
    HistoryClearHover(state);
    state.layoutWidth = -1;
    HDC dc = GetDC(state.graph);
    if (!dc) return;
    HGDIOBJ oldFont = SelectObject(dc, state.font ? state.font : GetStockObject(DEFAULT_GUI_FONT));
    int pad = ScaleForDpi(12, state.dpi), swatch = ScaleForDpi(34, state.dpi);
    int right = std::max(pad, int(client.right) - pad);
    int x = pad, y = ScaleForDpi(36, state.dpi), rowHeight = 0;
    for (auto& series : state.visible) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        series.legendRect = {};
        if (!series.plotted || right <= pad) continue;
        RECT text{0, 0, std::max(1, right - pad - swatch), 0};
        DrawTextW(dc, series.legendTitle.c_str(), int(series.legendTitle.size()), &text,
                  DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        int itemWidth = std::min(right - pad, int(text.right) + swatch);
        int itemHeight = std::max(ScaleForDpi(22, state.dpi), int(text.bottom));
        if (x > pad && x + itemWidth > right) {
            x = pad;
            y += rowHeight + ScaleForDpi(2, state.dpi);
            rowHeight = 0;
        }
        series.legendRect = {x, y, x + itemWidth, y + itemHeight};
        x += itemWidth + pad;
        rowHeight = std::max(rowHeight, itemHeight);
    }
    SelectObject(dc, oldFont);
    ReleaseDC(state.graph, dc);
    if (g_unloading || g_historyWindowCancelRequested) return;
    int plotTop = rowHeight ? y + rowHeight + ScaleForDpi(10, state.dpi) : ScaleForDpi(42, state.dpi);
    int left = std::min(int(client.right), ScaleForDpi(110, state.dpi));
    state.plot = {left, plotTop, left + width, plotTop + ScaleForDpi(166, state.dpi)};
    state.graphHeight = state.plot.bottom + ScaleForDpi(44, state.dpi);
    for (auto& series : state.visible) {
        if (g_unloading || g_historyWindowCancelRequested) return;
        if (!series.plotted) continue;
        const auto& account = state.accounts[series.account];
        series.render[0] = HistoryReducePeriod(state.snapshot, account.rows, series.meter, state.from, state.to, width);
        series.render[1] = state.overlay
            ? HistoryReducePeriod(state.snapshot, account.rows, series.meter, state.readFrom, state.from, width)
            : HistoryRenderSeries{};
    }
    state.renderWidth = width;
    state.layoutWidth = client.right;
}

struct HistoryHoverHit {
    size_t series = 0;
    int period = 0;
    size_t point = 0;
};

// Picking boundary: return every nearby series/period, not just the last painted line.
// Only real connected segments participate; the reported endpoint is always a recorded sample.
static std::vector<HistoryHoverHit> HistoryHitTest(const HistoryWindowState& state, POINT cursor) {
    std::vector<HistoryHoverHit> hits;
    if (state.renderWidth <= 0 || state.renderWidth != state.plot.right - state.plot.left ||
        cursor.x < state.plot.left || cursor.x > state.plot.right ||
        cursor.y < state.plot.top || cursor.y > state.plot.bottom) return hits;
    for (size_t index = 0; index < state.visible.size(); ++index) {
        if (g_unloading || g_historyWindowCancelRequested) return {};
        const auto& line = state.visible[index];
        if (!line.plotted) continue;
        int tolerance = ScaleForDpi(5, state.dpi) + std::max(1, ScaleForDpi(line.stroke, state.dpi)) / 2;
        for (int period = 0; period < (state.overlay ? 2 : 1); ++period) {
            const auto& points = line.render[period].points;
            int minX = cursor.x - state.plot.left - tolerance;
            int maxX = cursor.x - state.plot.left + tolerance;
            auto first = std::lower_bound(points.begin(), points.end(), minX,
                [](const HistoryRenderPoint& point, int x) { return point.x < x; });
            double best = double(tolerance) * tolerance;
            size_t nearest = SIZE_MAX;
            for (size_t i = size_t(first - points.begin()); i < points.size(); ++i) {
                if (g_unloading || g_historyWindowCancelRequested) return {};
                POINT b = HistoryPlotPoint(state, line, points[i]);
                double bx = double(cursor.x) - b.x, by = double(cursor.y) - b.y;
                double distance = bx * bx + by * by;
                size_t endpoint = i;
                if (i && points[i].connect) {
                    POINT a = HistoryPlotPoint(state, line, points[i - 1]);
                    double dx = double(b.x) - a.x, dy = double(b.y) - a.y;
                    double ax = double(cursor.x) - a.x, ay = double(cursor.y) - a.y;
                    double length = dx * dx + dy * dy;
                    double t = length > 0 ? std::clamp((ax * dx + ay * dy) / length, 0.0, 1.0) : 0;
                    double ex = ax - t * dx, ey = ay - t * dy;
                    distance = ex * ex + ey * ey;
                    if (ax * ax + ay * ay < bx * bx + by * by) endpoint = i - 1;
                }
                if (distance <= best) {
                    best = distance;
                    nearest = endpoint;
                }
                // Include one endpoint beyond the search band: its incoming segment can cross it.
                if (points[i].x > maxX) break;
            }
            if (nearest != SIZE_MAX) hits.push_back({index, period, nearest});
        }
    }
    return hits;
}

// Native tracking tool and its text belong exclusively to the graph's history thread.
static void HistoryUpdateHover(HistoryWindowState& state, POINT cursor) {
    RECT client{};
    GetClientRect(state.graph, &client);
    if (state.closing || state.loading || state.modal || g_unloading || g_historyWindowCancelRequested ||
        state.renderWidth < 0 || state.layoutWidth != client.right || !PtInRect(&client, cursor)) {
        HistoryClearHover(state);
        return;
    }
    if (state.hoverPoint.x == cursor.x && state.hoverPoint.y == cursor.y) return;
    state.hoverPoint = cursor;
    POINT content{cursor.x, cursor.y + state.scroll};
    std::vector<size_t> highlighted;
    std::wstring text;
    for (size_t index = 0; index < state.visible.size(); ++index) {
        if (g_unloading || g_historyWindowCancelRequested) { HistoryClearHover(state); return; }
        const auto& series = state.visible[index];
        if (!series.plotted || !PtInRect(&series.legendRect, content)) continue;
        highlighted.push_back(index);
        text = series.hoverTitle + (state.overlay
            ? L"\r\nSolid / filled: current week. Dashed / hollow: previous week."
            : L"\r\nSolid / filled: current range.");
        break;
    }
    if (highlighted.empty()) {
        auto hits = HistoryHitTest(state, content);
        for (const auto& hit : hits) {
            if (g_unloading || g_historyWindowCancelRequested) { HistoryClearHover(state); return; }
            const auto& series = state.visible[hit.series];
            const auto& point = series.render[hit.period].points[hit.point];
            if (point.sourceRow >= state.snapshot.samples.size()) continue;
            if (highlighted.empty() || highlighted.back() != hit.series) highlighted.push_back(hit.series);
            if (!text.empty()) text += L"\r\n";
            text += series.hoverTitle + (hit.period ? L" [Previous week]: " : L" [Current]: ") +
                (series.meter == 6 ? L"USD " : L"") + HistoryNumber(point.value) +
                (series.meter < 6 ? L"%" : series.meter == 7 ? L" credits" : L"") + L" · " +
                HistoryLocalDate(state.snapshot.samples[point.sourceRow].timeMs);
        }
    }
    if (highlighted.empty()) {
        HistoryClearHover(state);
        return;
    }
    if (!state.trackingMouse) {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, state.graph, 0};
        state.trackingMouse = TrackMouseEvent(&tracking) != FALSE;
    }
    if (state.hoverSeries != highlighted) {
        state.hoverSeries = std::move(highlighted);
        InvalidateRect(state.graph, nullptr, FALSE);
    }
    if (!state.graphTip) return;
    TOOLINFOW tool{};
    tool.cbSize = TTTOOLINFOW_V2_SIZE;
    tool.hwnd = state.graph;
    tool.uId = 1;
    if (state.graphTipText != text) {
        // Keep the old native text pointer alive until its replacement has been processed.
        state.graphTipText.swap(text);
        tool.lpszText = state.graphTipText.data();
        SendMessageW(state.graphTip, TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    POINT screen = cursor;
    ClientToScreen(state.graph, &screen);
    MONITORINFO monitor{sizeof(monitor)};
    if (!GetMonitorInfoW(MonitorFromPoint(screen, MONITOR_DEFAULTTONEAREST), &monitor))
        monitor.rcWork = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    int pad = ScaleForDpi(12, state.dpi);
    SendMessageW(state.graphTip, TTM_SETMAXTIPWIDTH, 0,
                  std::max(1L, std::min(LONG(ScaleForDpi(900, state.dpi)), monitor.rcWork.right - monitor.rcWork.left - 2 * pad)));
    // Native tooltip sizing is unreliable (and can fault in v5) before first activation.
    // Prime it offscreen once; active text updates resize synchronously without hide/show.
    if (!state.graphTipActive) {
        SendMessageW(state.graphTip, TTM_TRACKPOSITION, 0, MAKELPARAM(-32000, -32000));
        SendMessageW(state.graphTip, TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
        state.graphTipActive = true;
    }
    RECT tip{};
    if (!GetWindowRect(state.graphTip, &tip) || tip.right <= tip.left || tip.bottom <= tip.top) {
        HistoryClearHover(state);
        return;
    }
    int tipWidth = tip.right - tip.left, tipHeight = tip.bottom - tip.top;
    int x = screen.x + pad, y = screen.y + ScaleForDpi(20, state.dpi);
    if (y + tipHeight > monitor.rcWork.bottom) y = screen.y - tipHeight - pad;
    x = std::clamp(x, int(monitor.rcWork.left), std::max(int(monitor.rcWork.left), int(monitor.rcWork.right) - tipWidth));
    y = std::clamp(y, int(monitor.rcWork.top), std::max(int(monitor.rcWork.top), int(monitor.rcWork.bottom) - tipHeight));
    SendMessageW(state.graphTip, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
}

static void HistorySetStatus(HistoryWindowState& state, const std::wstring& message) {
    // Keep operational failures visible; explanatory text lives behind Info.
    std::wstring text = message.empty() ? state.snapshotStatus : message;
    std::wstring journal = state.snapshot.status;
    if (state.snapshot.capped) journal += L" Storage cap reached; oldest samples were pruned.";
    if (!journal.empty()) text += L"\r\n" + journal;
    SetWindowTextW(state.status, text.c_str());
}

static void HistoryScroll(HistoryWindowState& state, int position) {
    if (!state.graph) return;
    HistoryClearHover(state);
    HistoryRebuildRenderCache(state);
    RECT client{};
    GetClientRect(state.graph, &client);
    int seriesHeight = std::max(1, ScaleForDpi(kHistorySeriesHeight + (state.overlay ? 44 : 0), state.dpi));
    size_t height = state.visible.empty() && state.noReadings.empty() ? 0 :
        size_t(state.graphHeight) + state.visible.size() * size_t(seriesHeight);
    if (!state.noReadings.empty())
        height += (state.noReadings.size() + 1) * size_t(ScaleForDpi(24, state.dpi));
    int total = int(std::min(height, size_t(INT_MAX)));
    SCROLLINFO scroll{};
    scroll.cbSize = sizeof(scroll);
    scroll.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    scroll.nMax = std::max(0, total - 1);
    scroll.nPage = UINT(std::max(0L, client.bottom));
    scroll.nPos = std::clamp(position, 0, std::max(0, total - int(scroll.nPage)));
    SetScrollInfo(state.graph, SB_VERT, &scroll, TRUE);
    state.scroll = scroll.nPos;
    InvalidateRect(state.graph, nullptr, FALSE);
}

// Runs only on the history thread. Selection changes reuse the loaded file snapshot.
static void HistoryRebuildView(HistoryWindowState& state) {
    HistoryClearHover(state);
    state.visible.clear();
    state.noReadings.clear();
    state.renderWidth = -1;
    HistoryView view = state.selected < 0 ? state.allAccountsView : state.accountView;
    auto built = HistoryBuildSeries(state.snapshot.samples, state.accounts,
        {.selected = state.selected, .view = view, .from = state.from, .to = state.to,
         .readFrom = state.readFrom, .overlay = state.overlay, .enabled = state.enabled,
         .skipHidden = state.skipHidden, .redThreshold = state.redThreshold},
        [] { return g_unloading || g_historyWindowCancelRequested; });
    state.visible.reserve(built.series.size());
    for (auto& series : built.series) state.visible.push_back({std::move(series), {}, {}});
    state.noReadings = std::move(built.noReadings);
    state.axes = built.axes;
    // Each account mode keeps its own view selection; single accounts start on All meters.
    SendMessageW(state.meterCombo, CB_RESETCONTENT, 0, 0);
    for (PCWSTR name : kHistoryViewNames)
        SendMessageW(state.meterCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
    SendMessageW(state.meterCombo, CB_SETCURSEL, WPARAM(view), 0);
    EnableWindow(state.exportButton, !state.loading);
    SetWindowTextW(state.exportButton, state.selected < 0 ? L"&Export all CSV..." : L"&Export CSV...");
    HistoryScroll(state, 0);
}

static void HistoryLoad(HistoryWindowState& state) {
    // An open request applies to this load only, even one that bails (e.g. under a modal
    // dialog), so it never switches the selection later.
    uint64_t requestedId = g_historyRequestedAccount.exchange(0);
    if (state.loading || state.modal || state.closing || g_unloading || g_historyWindowCancelRequested) return;
    HistoryClearHover(state);
    state.loading = true;
    EnableWindow(state.refreshButton, FALSE);
    uint64_t selectedId = state.selected >= 0 && size_t(state.selected) < state.accounts.size()
                              ? state.accounts[state.selected].id : 0;
    std::vector<AccountConfig> configured;
    {
        std::lock_guard lock(g_settingsMutex);
        configured = g_settings.accounts;
        state.enabled = g_settings.historyEnabled;
        state.idsPendingSave = g_historyIdsPendingSave.load();
        state.retention = g_settings.historyRetentionDays;
        state.skipHidden = g_settings.skipHiddenPolling;
        state.redThreshold = g_settings.redThreshold;
    }
    static constexpr int days[] = {1, 7, 30, 90};
    state.to = NowUnixMs();
    ULONGLONG rangeDays = state.overlay ? 7 : state.range == 4
        ? std::max(1, state.retention) : days[state.range];
    ULONGLONG range = rangeDays * kHistoryDayMs;
    state.from = state.to > range ? state.to - range : 0;
    state.readFrom = state.overlay && state.from > kHistoryWeekMs
                        ? state.from - kHistoryWeekMs : state.from;
    // Explicit viewer refresh also prunes retained history while recording is off.
    // No settings lock is held across disk I/O or the history leaf lock.
    MaintainHistory(state.retention);
    state.snapshot = ReadHistorySnapshot(state.readFrom);
    // A close can arrive while the snapshot API is doing I/O. Drop old row indices
    // before returning so even a queued paint cannot index the replacement snapshot.
    state.visible.clear();
    state.noReadings.clear();
    state.renderWidth = -1;
    if (g_unloading || g_historyWindowCancelRequested) {
        state.loading = false;
        PostMessageW(state.window, WM_CLOSE, 0, 0);
        return;
    }
    std::stable_sort(state.snapshot.samples.begin(), state.snapshot.samples.end(),
                     [](const HistorySample& a, const HistorySample& b) {
                         return a.timeMs < b.timeMs;
                     });
    // Old rows index the replaced snapshot: drop them even if collection throws.
    state.accounts.clear();
    state.accounts = HistoryCollectAccounts(configured, state.snapshot.samples, state.readFrom, state.to,
                                            [] { return g_unloading || g_historyWindowCancelRequested; });
    SendMessageW(state.accountCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(state.accountCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All accounts"));
    for (const auto& account : state.accounts)
        SendMessageW(state.accountCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(account.title.c_str()));
    state.selected = HistoryResolveSelection(state.accounts, selectedId, requestedId);
    SendMessageW(state.accountCombo, CB_SETCURSEL, state.selected + 1, 0);
    state.snapshotStatus = std::wstring(!state.enabled ? L"Recording off" : state.idsPendingSave
        ? L"Recording pending" : L"Recording on") + L"  ·  Updated " + HistoryLocalDate(state.to);
    if (state.enabled && !state.idsPendingSave && state.snapshot.samples.empty()) {
        state.snapshotStatus = L"Recording on  ·  Waiting for the next poll";
    }
    SetWindowTextW(state.legend, state.overlay ? L"Solid: current    Dashed: previous" : L"Solid: current");
    state.loading = false;
    EnableWindow(state.refreshButton, TRUE);
    HistorySetStatus(state, {});
    HistoryRebuildView(state);
    HistoryLayout(state, false);
}
