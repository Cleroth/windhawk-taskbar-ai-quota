// History graph painting.

// Paint resources are scoped so allocation/formatting exceptions cannot leak a DC or pen.
struct HistoryPaint {
    HWND window;
    PAINTSTRUCT paint{};
    HDC dc = nullptr;
    int saved = 0;
    HPEN solid = nullptr, dashed = nullptr;
    explicit HistoryPaint(HWND owner) : window(owner), dc(BeginPaint(owner, &paint)) {
        if (dc) saved = SaveDC(dc);
    }
    ~HistoryPaint() {
        if (saved) RestoreDC(dc, saved);
        else if (dc) { SelectObject(dc, GetStockObject(BLACK_PEN)); SelectObject(dc, GetStockObject(SYSTEM_FONT)); }
        if (solid) DeleteObject(solid);
        if (dashed) DeleteObject(dashed);
        if (dc) EndPaint(window, &paint);
    }
};

static void HistorySetLinePens(HistoryPaint& paint, COLORREF color, int width, bool overlay) {
    SelectObject(paint.dc, GetStockObject(DC_PEN));
    SetDCPenColor(paint.dc, color);
    if (paint.solid) DeleteObject(paint.solid);
    if (paint.dashed) DeleteObject(paint.dashed);
    LOGBRUSH brush{BS_SOLID, color, 0};
    constexpr DWORD style = PS_GEOMETRIC | PS_ENDCAP_ROUND | PS_JOIN_ROUND;
    paint.solid = ExtCreatePen(style | PS_SOLID, width, &brush, 0, nullptr);
    // CreatePen silently turns wide dashed pens solid; geometric pens retain the period key.
    paint.dashed = overlay ? ExtCreatePen(style | PS_DASH, width, &brush, 0, nullptr) : nullptr;
}

static void HistoryDrawText(HDC dc, const std::wstring& text, RECT rect, UINT flags = DT_LEFT) {
    DrawTextW(dc, text.c_str(), int(text.size()), &rect, flags | DT_NOPREFIX);
}

static void HistoryPaintGraph(HistoryWindowState& state) {
    HistoryPaint paint(state.graph);
    if (!paint.dc) return;
    HDC dc = paint.dc;
    RECT client{};
    GetClientRect(state.graph, &client);
    FillRect(dc, &paint.paint.rcPaint, GetSysColorBrush(COLOR_WINDOW));
    SelectObject(dc, state.font ? state.font : GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    COLORREF textColor = GetSysColor(COLOR_WINDOWTEXT);
    SetTextColor(dc, textColor);
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    bool highContrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
                        (contrast.dwFlags & HCF_HIGHCONTRASTON);
    int pad = ScaleForDpi(12, state.dpi);
    int graphHeight = state.graphHeight;
    int seriesHeight = std::max(1, ScaleForDpi(kHistorySeriesHeight + (state.overlay ? 44 : 0), state.dpi));
    if (state.visible.empty() && state.noReadings.empty()) {
        RECT rect{pad, pad, client.right - pad, client.bottom - pad};
        HistoryDrawText(dc, state.accounts.empty() ? L"No accounts to display." : L"No accounts support this view.",
                        rect, DT_WORDBREAK);
        return;
    }
    // Synchronous resize paints may precede the matching cache; never lay out in WM_PAINT.
    if (state.layoutWidth != client.right || state.renderWidth < 0) return;
    // All lines share a timeline; unlike units have separate, explicitly labeled axes.
    int top = -state.scroll;
    RECT plot = state.plot;
    OffsetRect(&plot, 0, top);
    if (top + graphHeight >= paint.paint.rcPaint.top && top <= paint.paint.rcPaint.bottom) {
        RECT title{pad, top + ScaleForDpi(6, state.dpi), client.right - pad, top + ScaleForDpi(30, state.dpi)};
        HistoryView view = state.selected < 0 ? state.allAccountsView : state.accountView;
        std::wstring heading = state.selected < 0 ? L"All accounts" : state.accounts.at(state.selected).title;
        HistoryDrawText(dc, heading + L" · " + kHistoryViewNames[size_t(view)],
                        title, DT_SINGLELINE | DT_END_ELLIPSIS);
        for (size_t index = 0; index < state.visible.size(); ++index) {
            if (g_unloading || g_historyWindowCancelRequested) break;
            const auto& series = state.visible[index];
            RECT rect = series.legendRect;
            OffsetRect(&rect, 0, top);
            if (!series.plotted || rect.right <= rect.left || rect.bottom < paint.paint.rcPaint.top ||
                rect.top > paint.paint.rcPaint.bottom) continue;
            COLORREF color = HistoryLineColor(state, index, highContrast);
            int strokeWidth = std::max(1, ScaleForDpi(series.stroke, state.dpi));
            HistorySetLinePens(paint, color, strokeWidth, state.overlay);
            if (rect.right - rect.left >= ScaleForDpi(34, state.dpi)) {
                for (int period = 0; period < (state.overlay ? 2 : 1); ++period) {
                    if (!(period ? series.previous : series.current).hasValue) continue;
                    SelectObject(dc, period && paint.dashed ? paint.dashed :
                                     paint.solid ? paint.solid : GetStockObject(DC_PEN));
                    int y = rect.top + ScaleForDpi(state.overlay ? 6 + period * 10 : 10, state.dpi);
                    MoveToEx(dc, rect.left + ScaleForDpi(2, state.dpi), y, nullptr);
                    LineTo(dc, rect.left + ScaleForDpi(26, state.dpi), y);
                }
                rect.left += ScaleForDpi(34, state.dpi);
            }
            SetTextColor(dc, state.hoverSeries.empty() ||
                std::find(state.hoverSeries.begin(), state.hoverSeries.end(), index) != state.hoverSeries.end()
                ? textColor : GetSysColor(COLOR_GRAYTEXT));
            HistoryDrawText(dc, series.legendTitle, rect, DT_WORDBREAK | DT_END_ELLIPSIS);
        }
        SetTextColor(dc, textColor);
        if (plot.right <= plot.left) {
            RECT narrow{pad, plot.top, client.right - pad, plot.bottom};
            HistoryDrawText(dc, L"Widen the window to show the plot.", narrow, DT_WORDBREAK);
        }
    }
    if (plot.bottom + ScaleForDpi(30, state.dpi) >= paint.paint.rcPaint.top && plot.top <= paint.paint.rcPaint.bottom &&
        plot.right > plot.left && plot.bottom > plot.top) {
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, GetSysColor(COLOR_GRAYTEXT));
        for (int tick = 0; tick < 3; ++tick) {
            int y = plot.bottom - (plot.bottom - plot.top) * tick / 2;
            MoveToEx(dc, plot.left, y, nullptr);
            LineTo(dc, plot.right, y);
            static constexpr PCWSTR units[] = {L"%", L" USD", L" cr"};
            int axisPosition = 0;
            for (size_t i = 0; i < state.axes.size(); ++i) {
                const auto& axis = state.axes[i];
                if (!axis.visible) continue;
                double value = tick == 0 ? axis.low : tick == 2 ? axis.high : axis.low / 2 + axis.high / 2;
                wchar_t label[64];
                swprintf(label, ARRAYSIZE(label), L"%.6g%ls", value, units[i]);
                RECT labelRect{pad, y - ScaleForDpi(10, state.dpi), plot.left - ScaleForDpi(6, state.dpi), y + ScaleForDpi(12, state.dpi)};
                if (axisPosition) {
                    labelRect.left = plot.right + ScaleForDpi(6 + (axisPosition - 1) * 110, state.dpi);
                    labelRect.right = labelRect.left + ScaleForDpi(104, state.dpi);
                }
                HistoryDrawText(dc, label, labelRect, (axisPosition ? DT_LEFT : DT_RIGHT) | DT_SINGLELINE);
                ++axisPosition;
            }
        }
        bool hasValue = false, hasDetails = false;
        int lineClip = SaveDC(dc);
        if (lineClip) IntersectClipRect(dc, plot.left, plot.top, plot.right + 1, plot.bottom + 1);
        // Muted lines go first so a selected coincident series is never hidden beneath them.
        for (size_t draw = 0; lineClip && draw < state.visible.size() * (state.hoverSeries.empty() ? 1 : 2); ++draw) {
            if (g_unloading || g_historyWindowCancelRequested) break;
            size_t index = draw % state.visible.size();
            const auto& line = state.visible[index];
            if (!line.plotted) { hasDetails = true; continue; }
            bool highlighted = std::find(state.hoverSeries.begin(), state.hoverSeries.end(), index) != state.hoverSeries.end();
            if (!state.hoverSeries.empty() && highlighted != (draw >= state.visible.size())) continue;
            hasValue |= line.current.hasValue || (state.overlay && line.previous.hasValue);
            COLORREF color = HistoryLineColor(state, index, highContrast);
            int strokeWidth = std::max(1, ScaleForDpi(line.stroke, state.dpi));
            // Keep line colors in both periods; dashes/hollow points identify the prior week.
            HistorySetLinePens(paint, color, strokeWidth, state.overlay);
            // Width mismatches can occur during synchronous resize paints. Do not rebuild
            // here: size/selection handlers publish the matching cache before invalidating.
            for (int period = state.overlay ? 1 : 0; period >= 0 && state.renderWidth == plot.right - plot.left; --period) {
                const auto& series = line.render[period];
                int radius = std::max(1, ScaleForDpi(2, state.dpi));
                std::vector<POINT> segment;
                segment.reserve(series.points.size());
                size_t segmentSamples = 0;
                // One stroke per continuous run preserves dash phase and rounded joins.
                // Coincident vertices must not fill the gaps in a dashed stroke.
                auto drawSegment = [&] {
                    if (segment.empty() || g_unloading || g_historyWindowCancelRequested) return;
                    SelectObject(dc, period && paint.dashed ? paint.dashed :
                                     paint.solid ? paint.solid : GetStockObject(DC_PEN));
                    SetDCPenColor(dc, color);
                    if (segment.size() > 1) {
                        Polyline(dc, segment.data(), int(segment.size()));
                    } else {
                        int x = segment.front().x, y = segment.front().y;
                        if (segmentSamples > 1) {
                            // Compressed flat runs still use the stroke's width, not a stray pixel.
                            RECT mark{x - strokeWidth / 2, y - strokeWidth / 2,
                                      x - strokeWidth / 2 + strokeWidth, y - strokeWidth / 2 + strokeWidth};
                            SetDCBrushColor(dc, color);
                            FillRect(dc, &mark, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                        } else {
                            // Keep lone readings visible across gaps without dotting connected curves.
                            SelectObject(dc, GetStockObject(DC_PEN));
                            SetDCPenColor(dc, color);
                            SelectObject(dc, period ? GetStockObject(HOLLOW_BRUSH) : GetStockObject(DC_BRUSH));
                            SetDCBrushColor(dc, color);
                            Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
                        }
                    }
                    segment.clear();
                    segmentSamples = 0;
                };
                for (const auto& point : series.points) {
                    if (g_unloading || g_historyWindowCancelRequested) break;
                    if (!point.connect) drawSegment();
                    POINT position = HistoryPlotPoint(state, line, point);
                    position.y -= state.scroll;
                    if (segment.empty() || position.x != segment.back().x || position.y != segment.back().y)
                        segment.push_back(position);
                    ++segmentSamples;
                }
                drawSegment();
            }
        }
        if (lineClip) RestoreDC(dc, lineClip);
        if (!hasValue) {
            RECT empty{plot.left + pad, plot.top + pad, plot.right - pad, plot.bottom - pad};
            HistoryDrawText(dc, hasDetails ? L"No percentage readings to plot. Dollar spending is shown below."
                                          : L"No readings in this range.",
                            empty, DT_WORDBREAK);
        }
        for (int tick = 0; tick < 3; ++tick) {
            ULONGLONG time = state.from + (state.to - state.from) / 2 * tick;
            int width = std::min(ScaleForDpi(190, state.dpi), int(plot.right - plot.left) / 3);
            int x = tick == 0 ? plot.left : tick == 2 ? plot.right - width : (plot.left + plot.right - width) / 2;
            RECT rect{x, plot.bottom + ScaleForDpi(7, state.dpi), x + width, plot.bottom + ScaleForDpi(29, state.dpi)};
            HistoryDrawText(dc, HistoryLocalDate(time), rect, DT_SINGLELINE | DT_END_ELLIPSIS | (tick == 2 ? DT_RIGHT : tick == 1 ? DT_CENTER : DT_LEFT));
        }
    }

    // Cached titles/styles match the plotted lines; USD details never claim a swatch or axis.
    size_t firstView = size_t(std::max(0LL, static_cast<long long>(state.scroll) + paint.paint.rcPaint.top - graphHeight)) / seriesHeight;
    size_t endView = std::min(state.visible.size(),
        size_t(std::max(0LL, static_cast<long long>(state.scroll) + paint.paint.rcPaint.bottom - graphHeight)) / seriesHeight + 1);
    for (size_t view = firstView; view < endView; ++view) {
        if (g_unloading || g_historyWindowCancelRequested) break;
        long long top64 = graphHeight + static_cast<long long>(view) * seriesHeight - state.scroll;
        if (top64 + seriesHeight < paint.paint.rcPaint.top || top64 > paint.paint.rcPaint.bottom) continue;
        int seriesTop = int(top64);
        const auto& series = state.visible[view];
        int meter = series.meter;
        COLORREF color = highContrast ? textColor : series.color;
        int strokeWidth = std::max(1, ScaleForDpi(series.stroke, state.dpi));
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, color);
        int swatchY = seriesTop + ScaleForDpi(16, state.dpi);
        RECT swatch{pad, swatchY - strokeWidth / 2, pad + ScaleForDpi(26, state.dpi),
                    swatchY - strokeWidth / 2 + strokeWidth};
        SetDCBrushColor(dc, color);
        if (series.plotted) FillRect(dc, &swatch, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        RECT title{pad + (series.plotted ? ScaleForDpi(34, state.dpi) : 0), seriesTop + ScaleForDpi(6, state.dpi),
                    client.right - pad, seriesTop + ScaleForDpi(30, state.dpi)};
        HistoryDrawText(dc, series.title, title, DT_SINGLELINE | DT_END_ELLIPSIS);
        for (int period = 0; period < (state.overlay ? 2 : 1); ++period) {
            const auto& stats = period ? series.previous : series.current;
            std::wstring prefix = !state.overlay ? L"" : period ? L"Previous: " : L"Current: ";
            std::wstring level = (meter == 6 ? L"USD " : L"") + HistoryNumber(stats.peak) +
                (meter < 6 ? L"%" : meter == 7 ? L" credits" : L"");
            std::wstring peak = prefix + (stats.hasValue
                ? L"Peak " + level + L"  ·  " + HistoryLocalDate(stats.peakTime)
                : L"Peak —");
            int y = seriesTop + ScaleForDpi(34 + period * 44, state.dpi);
            RECT line{pad, y, client.right - pad, y + ScaleForDpi(21, state.dpi)};
            if (!series.plotted) {
                std::wstring latest = prefix + (stats.hasValue
                    ? L"Last recorded spend: USD " + HistoryNumber(stats.latest) + L"  ·  " + HistoryLocalDate(stats.latestTime)
                    : L"No dollar readings in this period.");
                HistoryDrawText(dc, latest, line, DT_SINGLELINE | DT_END_ELLIPSIS);
                line.top += ScaleForDpi(21, state.dpi);
                line.bottom += ScaleForDpi(21, state.dpi);
                if (stats.hasValue) HistoryDrawText(dc, peak, line, DT_SINGLELINE | DT_END_ELLIPSIS);
                continue;
            }
            HistoryDrawText(dc, peak, line, DT_SINGLELINE | DT_END_ELLIPSIS);
            double duration = double(state.to - state.from);
            wchar_t coverage[32];
            swprintf(coverage, ARRAYSIZE(coverage), L" (%.1f%%)", duration ? 100 * stats.coverageMs / duration : 0);
            std::wstring summary = L"Coverage " + HistoryHours(stats.coverageMs) + coverage;
            if (meter < 6) {
                summary += L"  ·  Est. ≥" + std::to_wstring(state.redThreshold) + L"%: " +
                    (stats.coverageMs ? HistoryHours(stats.nearLimitMs) : L"—");
            }
            line.top += ScaleForDpi(21, state.dpi);
            line.bottom += ScaleForDpi(21, state.dpi);
            HistoryDrawText(dc, summary, line, DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        SelectObject(dc, GetStockObject(DC_PEN));
        SetDCPenColor(dc, GetSysColor(COLOR_GRAYTEXT));
        MoveToEx(dc, pad, seriesTop + seriesHeight - 1, nullptr);
        LineTo(dc, client.right - pad, seriesTop + seriesHeight - 1);
    }
    // Accounts without readings get one compact entry, not an empty block per quota.
    if (!state.noReadings.empty()) {
        int rowHeight = ScaleForDpi(24, state.dpi);
        long long listTop = graphHeight + static_cast<long long>(state.visible.size()) * seriesHeight - state.scroll;
        for (size_t row = 0; row <= state.noReadings.size(); ++row) {
            if (g_unloading || g_historyWindowCancelRequested) break;
            long long y = listTop + static_cast<long long>(row) * rowHeight;
            if (y + rowHeight < paint.paint.rcPaint.top) continue;
            if (y > paint.paint.rcPaint.bottom) break;
            RECT rect{pad, int(y), client.right - pad, int(y + rowHeight)};
            HistoryDrawText(dc, row ? state.noReadings[row - 1] : L"No readings in range:",
                            rect, DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }
    if (GetFocus() == state.graph) {
        InflateRect(&client, -1, -1);
        DrawFocusRect(dc, &client);
    }
}
