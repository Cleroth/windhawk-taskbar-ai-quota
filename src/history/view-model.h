// History series reduction, render cache, hover hit-testing, statistics, and loading.

// Formatting stays independent of locale for exact values/CSV. Dates on-screen are local.
static std::wstring HistoryNumber(double value) {
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec != std::errc{}) return L"?";
    return std::wstring(buffer, result.ptr);
}

static std::wstring HistoryLocalDate(ULONGLONG time) {
    SYSTEMTIME local{};
    if (!UnixMsToLocalSystemTime(time, &local)) return L"unknown local time";
    wchar_t text[48];
    swprintf(text, ARRAYSIZE(text), L"%04u-%02u-%02u %02u:%02u:%02u",
             local.wYear, local.wMonth, local.wDay, local.wHour, local.wMinute, local.wSecond);
    return text;
}

static std::string HistoryUtcDate(ULONGLONG time) {
    if (!time || time > 1833029933770955ULL) return {};
    ULONGLONG ticks = (time + 11644473600000ULL) * 10000;
    FILETIME file{DWORD(ticks), DWORD(ticks >> 32)};
    SYSTEMTIME utc{};
    if (!FileTimeToSystemTime(&file, &utc)) return {};
    char text[40];
    snprintf(text, sizeof(text), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
             utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond,
             utc.wMilliseconds);
    return text;
}

static std::string HistoryCsvId(const char* prefix, uint64_t id) {
    char text[48];
    snprintf(text, sizeof(text), "%s%016llx", prefix, static_cast<unsigned long long>(id));
    return text;
}

static std::wstring HistoryHours(double ms) {
    wchar_t text[40];
    swprintf(text, ARRAYSIZE(text), L"%.2f h", ms / 3600000.0);
    return text;
}

// Reduction boundary, also callable by isolated structural tests. Each continuous
// segment/pixel keeps its first, minimum, maximum and last raw samples in temporal
// order. A break flushes the bucket even inside the same pixel, so no extrema or
// segment endpoints are lost and no fabricated bridge crosses a reset/failure/gap.
static HistoryRenderSeries HistoryReducePeriod(const HistorySnapshot& snapshot,
                                               const std::vector<size_t>& rows,
                                               int meter, ULONGLONG from,
                                               ULONGLONG to, int width) {
    HistoryRenderSeries result;
    if (width <= 0 || to <= from) return result;
    result.points.reserve(std::min(rows.size(), size_t(width) * 4));
    int bucketX = -1;
    size_t first = 0, minimum = 0, maximum = 0, last = 0;
    bool bucketConnect = false;
    const HistorySample* previous = nullptr;
    auto flush = [&] {
        if (bucketX < 0) return;
        std::array<size_t, 4> positions{first, minimum, maximum, last};
        std::sort(positions.begin(), positions.end());
        auto end = std::unique(positions.begin(), positions.end());
        bool connect = bucketConnect;
        for (auto it = positions.begin(); it != end; ++it) {
            size_t row = rows[*it];
            result.points.push_back({bucketX, snapshot.samples[row].meters[meter].value, row, connect});
            connect = true;
        }
        bucketX = -1;
    };
    // rows follow timestamp order, as established by HistoryLoad. The number of
    // occupied segment/pixel buckets is bounded by plot width + structural breaks.
    for (size_t position = 0; position < rows.size(); ++position) {
        if (g_unloading || g_historyWindowCancelRequested) return {};
        const auto& sample = snapshot.samples[rows[position]];
        if (sample.timeMs < from || sample.timeMs >= to) continue;
        int x = std::clamp(int(double(sample.timeMs - from) / double(to - from) * width), 0, width - 1);
        bool valid = HistoryHasValue(sample, meter);
        HistoryBreak reason = previous ? HistoryConnection(*previous, sample, meter) : HistoryBreak::None;
        bool connect = previous && reason == HistoryBreak::None;
        if (!valid || !connect || x != bucketX) flush();
        if (valid) {
            if (bucketX < 0) {
                bucketX = x;
                first = minimum = maximum = last = position;
                bucketConnect = connect;
            } else {
                double value = sample.meters[meter].value;
                if (value < snapshot.samples[rows[minimum]].meters[meter].value) minimum = position;
                if (value > snapshot.samples[rows[maximum]].meters[meter].value) maximum = position;
                last = position;
            }
        }
        previous = &sample;
    }
    flush();
    return result;
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

static HistoryStatistics HistoryAnalyze(const HistoryWindowState& state,
                                        const HistoryUiAccount& account, int meter,
                                        ULONGLONG from, ULONGLONG to) {
    HistoryStatistics result;
    const HistorySample* previous = nullptr;
    for (size_t row : account.rows) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        const auto& sample = state.snapshot.samples[row];
        if (sample.timeMs < from || sample.timeMs >= to) continue;
        if (HistoryHasValue(sample, meter)) {
            double value = sample.meters[meter].value;
            result.latest = value;
            result.latestTime = sample.timeMs;
            ++result.validSamples;
            if (!result.hasValue || value > result.peak) {
                result.hasValue = true;
                result.peak = value;
                result.peakTime = sample.timeMs;
            }
            if (previous && HistoryConnection(*previous, sample, meter) == HistoryBreak::None) {
                double elapsed = double(sample.timeMs - previous->timeMs);
                result.coverageMs += elapsed;
                if (meter < 6) {
                    double a = previous->meters[meter].value;
                    double b = value;
                    if (a >= state.redThreshold && b >= state.redThreshold) {
                        result.nearLimitMs += elapsed;
                    } else if ((a >= state.redThreshold) != (b >= state.redThreshold)) {
                        // Ratio form avoids overflow when finite endpoints are very far apart.
                        double low = std::min(a, b), high = std::max(a, b);
                        double divisor = std::max({1.0, std::abs(low), std::abs(high)});
                        double fraction = (high / divisor - state.redThreshold / divisor) /
                                          (high / divisor - low / divisor);
                        result.nearLimitMs += elapsed * std::clamp(fraction, 0.0, 1.0);
                    }
                }
            }
        }
        // Failures/missing values remain the predecessor, so a line never bridges them.
        previous = &sample;
    }
    return result;
}

static void HistorySetStatus(HistoryWindowState& state, const std::wstring& message) {
    // Keep operational failures visible; explanatory text lives behind Info.
    std::wstring text = message.empty() ? state.snapshotStatus : message;
    if (!state.snapshot.status.empty()) text += L"\r\n" + state.snapshot.status;
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
    state.axes = {};
    state.axes[0].high = 100;
    // Keep percentages on the left even if only dollar/credit readings are available.
    state.axes[0].visible = true;
    HistoryView view = state.selected < 0 ? state.allAccountsView : state.accountView;
    for (size_t i = 0; i < state.accounts.size(); ++i) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        if (state.selected >= 0 && size_t(state.selected) != i) continue;
        const auto& account = state.accounts[i];
        unsigned meters = account.provider < ARRAYSIZE(kHistoryProviderMeters)
            ? kHistoryProviderMeters[account.provider] & kHistoryViewMeters[size_t(view)] : 0;
        // Only Weekly follows taskbar choices; All meters and removed accounts stay unfiltered.
        if (view == HistoryView::Weekly && account.provider == 1 && account.configured) {
            if (!account.showWeekly) meters &= ~(1U << 1);
            if (!account.showFableWeekly) meters &= ~(1U << 2);
        }
        if (!meters) continue;
        size_t firstSeries = state.visible.size();
        std::wstring status;
        if (account.configured && state.enabled) {
            if (!account.recording) status = L"  [recording off]";
            else if (account.hidden && state.skipHidden) status = L"  [paused]";
        }
        for (int meter = 0; meter < int(kHistoryMeterCount); ++meter) {
            if (g_unloading || g_historyWindowCancelRequested) break;
            if (!(meters & (1U << meter))) continue;
            HistoryUiSeries series;
            series.account = i;
            series.meter = meter;
            series.current = HistoryAnalyze(state, account, meter, state.from, state.to);
            series.previous = state.overlay
                ? HistoryAnalyze(state, account, meter, state.readFrom, state.from) : HistoryStatistics{};
            // No empty per-meter legends. A USD-only account still has real detail data.
            if (!series.current.hasValue && !series.previous.hasValue) continue;
            series.plotted = meter != 6 || view == HistoryView::AllMeters;
            bool scoped = meter == 2 || meter == 4 || meter == 5;
            series.stroke = scoped ? 2 : 3;
            series.color = kHistoryColors[(state.selected < 0 ? i : size_t(meter)) % ARRAYSIZE(kHistoryColors)];
            if (state.selected < 0 && scoped) {
                // Keep scoped quotas in the account's color family; width survives high contrast.
                COLORREF color = series.color;
                series.color = RGB((GetRValue(color) * 3 + 255) / 4,
                                   (GetGValue(color) * 3 + 255) / 4,
                                   (GetBValue(color) * 3 + 255) / 4);
            }
            std::wstring label = kHistoryMeterNames[meter];
            if (account.provider == 3 && meter == 1) label = L"Gemini weekly used (%)";
            auto& axis = state.axes[meter < 6 ? 0 : meter - 5];
            if (series.plotted) axis.visible = true;
            ULONGLONG shortDuration = 0;
            bool hasDuration = false, mixedDuration = false;
            for (size_t row : account.rows) {
                if (g_unloading || g_historyWindowCancelRequested) break;
                const auto& sample = state.snapshot.samples[row];
                if (sample.timeMs < state.readFrom || sample.timeMs >= state.to ||
                    !HistoryHasValue(sample, meter)) continue;
                double value = sample.meters[meter].value;
                if (series.plotted) {
                    axis.low = std::min(axis.low, value);
                    axis.high = std::max(axis.high, value);
                }
                if (meter == 0 || meter == 4) {
                    ULONGLONG duration = sample.meters[meter].durationMs;
                    if (hasDuration && shortDuration != duration) mixedDuration = true;
                    shortDuration = duration;
                    hasDuration = true;
                }
            }
            if (meter == 0 || meter == 4) {
                label = mixedDuration ? L"Short-term (varying windows)" : !shortDuration ? L"Current limit" :
                    shortDuration % 3600000 == 0 ? std::to_wstring(shortDuration / 3600000) + L"h" :
                    HistoryHours(double(shortDuration));
                if (account.provider == 3) {
                    label.front() = wchar_t(towlower(label.front()));
                    label = (meter == 4 ? L"Claude/GPT " : L"Gemini ") + label;
                }
                label += L" used (%)";
            }
            series.hoverTitle = account.label + L" · " + label;
            std::wstring compact = label;
            if (compact.ends_with(L" used (%)")) compact.resize(compact.size() - 9);
            series.legendTitle = (state.selected < 0 ? account.label + L" · " : L"") + compact;
            if (!series.plotted) label += L" — details only";
            series.title = (state.selected < 0 ? account.label + L" · " : L"") + label;
            if (state.visible.size() == firstSeries) series.title += status;
            state.visible.push_back(std::move(series));
        }
        if (state.visible.size() == firstSeries) state.noReadings.push_back(account.title + status);
    }
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
    state.accounts.clear();
    std::map<uint64_t, size_t> index;
    for (const auto& config : configured) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        if (!config.historyId || index.contains(config.historyId)) continue;
        HistoryUiAccount account;
        account.id = config.historyId;
        account.provider = config.provider == L"anthropic" ? 1 : config.provider == L"openai" ? 2 : 3;
        account.configured = true;
        account.recording = config.recordHistory;
        account.hidden = config.hidden;
        account.showWeekly = config.showBars[kWeeklyBar];
        account.showFableWeekly = config.showBars[kFableWeeklyBar];
        account.label = config.label;
        account.title = std::wstring(ProviderDisplayName(config.provider)) + L" — " + config.label;
        index.emplace(account.id, state.accounts.size());
        state.accounts.push_back(std::move(account));
    }
    for (size_t row = 0; row < state.snapshot.samples.size(); ++row) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        const auto& sample = state.snapshot.samples[row];
        if (!sample.accountId || sample.timeMs < state.readFrom || sample.timeMs >= state.to) continue;
        auto [it, inserted] = index.emplace(sample.accountId, state.accounts.size());
        if (inserted) {
            HistoryUiAccount account;
            account.id = sample.accountId;
            account.provider = sample.provider;
            PCWSTR provider = sample.provider == 1 ? L"Anthropic" : sample.provider == 2 ? L"OpenAI" :
                              sample.provider == 3 ? L"Google Antigravity" : L"Unknown provider";
            account.label = L"removed " + Utf8ToWide(HistoryCsvId("account_", sample.accountId));
            account.title = std::wstring(provider) + L" — " + account.label;
            state.accounts.push_back(std::move(account));
        }
        state.accounts[it->second].rows.push_back(row);
    }
    SendMessageW(state.accountCombo, CB_RESETCONTENT, 0, 0);
    SendMessageW(state.accountCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"All accounts"));
    // A requested account that isn't listed keeps the current selection.
    state.selected = -1;
    int requested = -1;
    for (size_t i = 0; i < state.accounts.size(); ++i) {
        SendMessageW(state.accountCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(state.accounts[i].title.c_str()));
        if (state.accounts[i].id == selectedId) state.selected = int(i);
        if (state.accounts[i].id == requestedId) requested = int(i);
    }
    if (requested >= 0) state.selected = requested;
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
