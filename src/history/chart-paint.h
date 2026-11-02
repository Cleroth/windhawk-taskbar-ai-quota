// Usage history chart painting (pure): the history window's client-drawn items, its chart (the
// scene of history/chart.h), the chart's legend chips and an export's progress, on a Direct2D
// target in the items' coordinates. Stroke styles and path geometries are factory-bound
// (UiGraphics); brushes and colored layouts live in the surface's cache.

// Paints the chart item or one of its legend chips, the emphasis from the paint state and the
// chart's picks (HistoryChartEmphasis), or the progress of an export that wrote `exported` rows.
static void HistoryUiPaintCustom(ID2D1RenderTarget* target, UiGraphics& g, UiTargetCache& cache, const UiPalette& p,
                                 const HistoryChart& chart, const std::vector<size_t>& picked, size_t exported,
                                 const UiItem& item, const UiPaintState& state, float scale) {
    // The progress (s-states .upd): its line in the item's font, muted, after a 16 DIP ring and 6
    // DIP gap, at the item's leading or trailing end, centered on it vertically.
    if (item.id == kHistoryUiProgress) {
        const bool hc = p.kind == UiThemeKind::HighContrast;
        UiTextStyle style = UiTextStyleFor(item.font);
        IDWriteTextLayout* layout = UiLayout(g, HistoryUiProgressText(exported), style, 10000, false);
        float width = std::ceil(UiLayoutSize(layout).width), middle = (item.rect.top + item.rect.bottom) / 2;
        float x = item.align == DWRITE_TEXT_ALIGNMENT_TRAILING ? item.rect.right - width - 22 : item.rect.left;
        UiDrawLayout(target, cache, layout, {x + 22, middle - style.lineHeight / 2}, p.textMuted);
        UiItem ring;
        ring.kind = UiItemKind::Shape;
        ring.rect = {x, middle - 8, x + 16, middle + 8};
        ring.viewBox = 16;
        ring.parts = {{L"M8 1.5A6.5 6.5 0 1 1 8 14.5A6.5 6.5 0 1 1 8 1.5", {}, UiInk::Disabled, hc ? 1.0f : 0.45f, 2},
                      {L"M8 1.5A6.5 6.5 0 0 1 14.5 8", {}, UiInk::Accent, 1, 2}};
        UiPaintItems(target, g, cache, p, {ring}, {}, {0, 0}, scale, -1e9f, 1e9f);
        return;
    }
    std::vector<size_t> emphasis = HistoryChartEmphasis(chart.lines, picked, state);

    // The chart: its scene's prims in order, the lines inside the plot's clip.
    if (item.id == kHistoryUiChart) {
        HistoryChartScene scene = HistoryChartBuildScene(chart, emphasis, p);
        bool clipped = false;
        for (const HistoryChartPrim& prim : scene.prims) {
            if (prim.clipped != clipped) {
                if (prim.clipped) target->PushAxisAlignedClip(&scene.clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                else target->PopAxisAlignedClip();
                clipped = prim.clipped;
            }
            switch (prim.mark) {
                case HistoryChartMark::Fill:
                case HistoryChartMark::Square:
                    target->FillRectangle(&prim.rect, UiBrush(target, cache, prim.color));
                    break;
                // Diagonal hairlines 6 DIP apart (mockup: a 45 degree pattern).
                case HistoryChartMark::Hatch: {
                    const D2D1_RECT_F& r = prim.rect;
                    float height = r.bottom - r.top;
                    target->PushAxisAlignedClip(&r, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    for (float x = r.left - height; x < r.right; x += 6) {
                        target->DrawLine({x, r.bottom}, {x + height, r.top}, UiBrush(target, cache, prim.color), 1);
                    }
                    target->PopAxisAlignedClip();
                    break;
                }
                case HistoryChartMark::Rule:
                    target->DrawLine(prim.points[0], prim.points[1], UiBrush(target, cache, prim.color), prim.width,
                                     prim.dash > 0 ? UiStrokeStyle(g, false, prim.dash, prim.gap) : nullptr);
                    break;
                case HistoryChartMark::Run: {
                    winrt::com_ptr<ID2D1PathGeometry> path;
                    winrt::check_hresult(g.d2d->CreatePathGeometry(path.put()));
                    winrt::com_ptr<ID2D1GeometrySink> sink;
                    winrt::check_hresult(path->Open(sink.put()));
                    sink->BeginFigure(prim.points[0], D2D1_FIGURE_BEGIN_HOLLOW);
                    sink->AddLines(prim.points.data() + 1, UINT32(prim.points.size() - 1));
                    sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    winrt::check_hresult(sink->Close());
                    target->DrawGeometry(path.get(), UiBrush(target, cache, prim.color), prim.width,
                                         UiStrokeStyle(g, true, prim.dash, prim.gap));
                    break;
                }
                // A lone reading (mockup: a 3 DIP circle, 1.6 DIP outline).
                case HistoryChartMark::Dot: {
                    D2D1_ELLIPSE dot{prim.points[0], 3, 3};
                    target->FillEllipse(&dot, UiBrush(target, cache, prim.fill));
                    target->DrawEllipse(&dot, UiBrush(target, cache, prim.color), prim.width);
                    break;
                }
                // Text on one line in its box; a backdrop 6 DIP around a message's ink.
                case HistoryChartMark::Label: {
                    IDWriteTextLayout* layout =
                        UiLayout(g, prim.text, prim.style, std::max(prim.rect.right - prim.rect.left, 0.0f), false, prim.align);
                    if (prim.fill.a > 0) {
                        DWRITE_TEXT_METRICS metrics{};
                        winrt::check_hresult(layout->GetMetrics(&metrics));
                        target->FillRectangle(D2D1::RectF(prim.rect.left + metrics.left - 6, prim.rect.top,
                                                          prim.rect.left + metrics.left + metrics.width + 6, prim.rect.bottom),
                                              UiBrush(target, cache, prim.fill));
                    }
                    UiDrawLayout(target, cache, layout, {prim.rect.left, prim.rect.top}, prim.color);
                    break;
                }
            }
        }
        if (clipped) target->PopAxisAlignedClip();
        return;
    }

    // A legend chip (approved q8a-filter, s-hc; mockup .lchip): a 24 DIP pill, its line's swatch
    // (18 x 12 DIP: the line at its width, or solid over dashed while comparing; a hollow outline
    // while hidden) 11 DIP in, its name 7 DIP after it. Shown: a faint fill, stronger under the
    // pointer (high contrast: an outline, the selection colors under the pointer); hidden: an
    // outline (high contrast: dashed) with faint text; muted (emphasis elsewhere): muted text and
    // the line's muted color.
    auto found = std::find_if(chart.lines.begin(), chart.lines.end(),
                              [&](const HistoryUiLine& line) { return line.chip == item.id; });
    if (found == chart.lines.end()) return;
    const HistoryUiLine& line = *found;
    const bool hc = p.kind == UiThemeKind::HighContrast, light = p.kind == UiThemeKind::Light;
    const bool hot = state.hot.id == item.id, off = !item.selected;
    const bool emphasized =
        std::find(emphasis.begin(), emphasis.end(), size_t(found - chart.lines.begin())) != emphasis.end();
    const bool muted = !off && !emphasis.empty() && !emphasized;
    const D2D1_RECT_F& r = item.rect;
    float radius = (r.bottom - r.top) / 2, middle = (r.top + r.bottom) / 2;
    D2D1_COLOR_F base = HistoryChartColor(line, p), fill{}, border{}, ink = p.text, swatch = base;
    if (hc) {
        border = off || muted ? p.textDisabled : hot ? p.highlight : p.text;
        fill = hot && !off ? p.highlight : D2D1_COLOR_F{};
        ink = off || muted ? p.textDisabled : hot ? p.highlightText : p.text;
        swatch = off || muted ? p.textDisabled : hot ? p.highlightText : emphasized ? p.highlight : base;
    } else {
        D2D1_COLOR_F tone = light ? UiRgb(0x000000) : UiRgb(0xFFFFFF);
        fill = off ? D2D1_COLOR_F{} : UiFade(tone, hot ? (light ? 0.08f : 0.09f) : (light ? 0.045f : 0.05f));
        border = off ? UiFade(tone, light ? 0.2f : 0.22f) : D2D1_COLOR_F{};
        ink = off ? UiFade(tone, 0.4f) : muted ? p.textMuted : p.text;
        if (muted) swatch = UiOver(UiFade(base, 0.25f), HistoryChartBackground(p));
    }
    if (!item.enabled) ink = p.textDisabled;
    UiFillRect(target, cache, r, radius, fill, scale);
    if (hc && off) {
        D2D1_RECT_F edge = UiInflate(UiSnapRect(r, scale), -0.5f / scale, -0.5f / scale);
        D2D1_ROUNDED_RECT rounded{edge, radius, radius};
        target->DrawRoundedRectangle(&rounded, UiBrush(target, cache, border), 1 / scale, UiStrokeStyle(g, false, 3, 2));
    } else {
        UiStrokeRect(target, cache, r, radius, border, 1, scale);
    }
    float left = r.left + 11, top = middle - 6;
    auto stroke = [&](float y, float dash) {
        float width = float(line.stroke);
        target->DrawLine({left + 2, top + y}, {left + 16, top + y}, UiBrush(target, cache, swatch), width,
                         UiStrokeStyle(g, true, dash / width, dash ? 2 * dash / width : 0));
    };
    if (off) {
        D2D1_ROUNDED_RECT hollow{{left + 1, top + 3.5f, left + 17, top + 8.5f}, 2.5f, 2.5f};
        target->DrawRoundedRectangle(&hollow, UiBrush(target, cache, hc ? swatch : UiFade(swatch, 0.55f)), 1.2f);
    } else if (chart.compare) {
        stroke(3, 0);
        stroke(9.5f, 2);
    } else {
        stroke(6, 0);
    }
    UiTextStyle label{UiFamily::Text, 12.5f, 16, DWRITE_FONT_WEIGHT_NORMAL};
    float textLeft = left + 18 + 7;
    UiDrawLayout(target, cache,
                 UiLayout(g, line.name, label, std::max(r.right - 11 - textLeft, 0.0f), false,
                          DWRITE_TEXT_ALIGNMENT_LEADING, true),
                 {textLeft, middle - label.lineHeight / 2}, ink);
}
