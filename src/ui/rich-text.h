// Rich text: runs with emphasis, semantic colors and inline links, laid out by DirectWrite. Pure.
// Effect-free layouts (measuring, hit testing) are shared; colored layouts are per target.

// What activating an item or link does. Data only; the window's client interprets it.
// Command: a client-defined target set to a desired value (toggles, dropdown choices, buttons,
// gallery cards, swatches). Number boxes report their native text box: Step (value = delta),
// and the text events with the box's text. Sliders report Slide (value = the dragged value)
// and SlideEnd (released); their keys Step. Pan: a preview strip dragged or wheeled sideways.
// Drag handles report Drag once the pointer moved, then Drop on release or DragCancel (Esc,
// capture loss); Alt+Up/Down on the handle's owner Step it by one.
enum class UiActionKind : uint8_t {
    None, Navigate, OpenUrl, Command, Step, TextChange, TextCommit, TextBlur, TextRevert, Slide,
    SlideEnd, Pan, Drag, Drop, DragCancel
};

struct UiAction {
    UiActionKind kind = UiActionKind::None;
    int value = 0;         // Navigate: page; Command: desired value; Step: delta; Slide: value;
                           // Pan: DIPs the content moves right; Drag: the pointer's content y
    std::wstring text{};   // OpenUrl: the URL; text events: the box's text
    uint32_t target = 0;   // Command, Step and text events: the client's target
    uint64_t key = 0;      // The target's owner (0: global)
};

enum class UiRunStyle : uint8_t { Normal, Strong, Link };

// color: a concrete color the client derived from the theme (a usage color), over the ink;
// high contrast ignores it.
struct UiRun {
    std::wstring text;
    UiRunStyle style = UiRunStyle::Normal;
    UiInk ink = UiInk::Inherit;
    UiAction action{};  // Link runs
    std::optional<D2D1_COLOR_F> color{};
};

static std::wstring UiRunsText(const std::vector<UiRun>& runs) {
    std::wstring text;
    for (const UiRun& run : runs) text += run.text;
    return text;
}

// Link ranges in run order; the index is the link's focus sub-id.
static std::vector<DWRITE_TEXT_RANGE> UiLinkRanges(const std::vector<UiRun>& runs) {
    std::vector<DWRITE_TEXT_RANGE> links;
    UINT32 at = 0;
    for (const UiRun& run : runs) {
        if (run.style == UiRunStyle::Link) links.push_back({at, UINT32(run.text.size())});
        at += UINT32(run.text.size());
    }
    return links;
}

static const UiRun* UiLinkRun(const std::vector<UiRun>& runs, int link) {
    for (const UiRun& run : runs) {
        if (run.style == UiRunStyle::Link && link-- == 0) return &run;
    }
    return nullptr;
}

static std::wstring UiRichKey(const std::vector<UiRun>& runs, UiFont font, float width,
                              DWRITE_TEXT_ALIGNMENT align, bool trim, std::wstring_view locale) {
    std::wstring key = std::wstring(locale) + L'|' + std::to_wstring(int(font)) + L'|' + std::to_wstring(width) +
                       L'|' + std::to_wstring(int(align)) + (trim ? L"t" : L"");
    for (const UiRun& run : runs) {
        key += L'|';
        key += wchar_t(L'0' + int(run.style));
        key += std::to_wstring(run.text.size());
        key += L':';
        key += run.text;
    }
    return key;
}

static winrt::com_ptr<IDWriteTextLayout> UiCreateRichLayout(UiGraphics& g,
                                                            const std::vector<UiRun>& runs,
                                                            UiFont font, float width,
                                                            DWRITE_TEXT_ALIGNMENT align, bool trim) {
    std::wstring text = UiRunsText(runs);
    winrt::com_ptr<IDWriteTextLayout> layout;
    winrt::check_hresult(g.dwrite->CreateTextLayout(text.c_str(), UINT32(text.size()),
                                                    UiFormat(g, UiTextStyleFor(font)),
                                                    std::max(width, 0.0f), 100000.0f,
                                                    layout.put()));
    layout->SetTextAlignment(align);
    if (trim) UiTrimLayout(g, layout.get(), UiTextStyleFor(font));
    UINT32 at = 0;
    for (const UiRun& run : runs) {
        if (run.style == UiRunStyle::Strong) {
            layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, {at, UINT32(run.text.size())});
        }
        at += UINT32(run.text.size());
    }
    return layout;
}

// The shared, effect-free layout: measuring and link hit testing. trim: one line, ellipsized.
static IDWriteTextLayout* UiRichLayout(UiGraphics& g, const std::vector<UiRun>& runs, UiFont font,
                                       float width,
                                       DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                                       bool trim = false) {
    auto& layout = g.layouts[L"rich|" + UiRichKey(runs, font, width, align, trim, g.locale)];
    if (!layout) layout = UiCreateRichLayout(g, runs, font, width, align, trim);
    return layout.get();
}

// The link under a layout-relative point, or -1.
static int UiLinkAt(IDWriteTextLayout* layout, const std::vector<UiRun>& runs, D2D1_POINT_2F point) {
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS hit{};
    if (FAILED(layout->HitTestPoint(point.x, point.y, &trailing, &inside, &hit)) || !inside) {
        return -1;
    }
    std::vector<DWRITE_TEXT_RANGE> links = UiLinkRanges(runs);
    for (size_t i = 0; i < links.size(); i++) {
        if (hit.textPosition >= links[i].startPosition &&
            hit.textPosition < links[i].startPosition + links[i].length) {
            return int(i);
        }
    }
    return -1;
}

// Layout-relative boxes of a link, one per line it spans.
static std::vector<D2D1_RECT_F> UiLinkRects(IDWriteTextLayout* layout, const std::vector<UiRun>& runs,
                                            int link) {
    std::vector<D2D1_RECT_F> rects;
    std::vector<DWRITE_TEXT_RANGE> links = UiLinkRanges(runs);
    if (link < 0 || link >= int(links.size())) return rects;
    UINT32 count = 0;
    layout->HitTestTextRange(links[link].startPosition, links[link].length, 0, 0, nullptr, 0,
                             &count);
    std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
    if (count && SUCCEEDED(layout->HitTestTextRange(links[link].startPosition, links[link].length,
                                                    0, 0, metrics.data(), count, &count))) {
        for (UINT32 i = 0; i < count; i++) {
            const auto& m = metrics[i];
            rects.push_back({m.left, m.top, m.left + m.width, m.top + m.height});
        }
    }
    return rects;
}

// The per-target layout with every run's color as a drawing effect. Link colors follow the
// pointer (hotLink, pressedLink); a hovered link is underlined.
static IDWriteTextLayout* UiInkedLayout(ID2D1RenderTarget* target, UiTargetCache& cache,
                                        UiGraphics& g, const std::vector<UiRun>& runs, UiFont font,
                                        float width, DWRITE_TEXT_ALIGNMENT align,
                                        const UiPalette& palette, UiInk baseInk, int hotLink,
                                        int pressedLink, bool trim = false) {
    auto& layout = cache.layouts[UiRichKey(runs, font, width, align, trim, g.locale)];
    if (!layout) layout = UiCreateRichLayout(g, runs, font, width, align, trim);
    UINT32 at = 0;
    int link = 0;
    for (const UiRun& run : runs) {
        DWRITE_TEXT_RANGE range{at, UINT32(run.text.size())};
        D2D1_COLOR_F color = run.color && palette.kind != UiThemeKind::HighContrast ?
                                 *run.color : UiInkColor(palette, run.ink == UiInk::Inherit ? baseInk : run.ink);
        bool underline = false;
        if (run.style == UiRunStyle::Link) {
            color = link == pressedLink ? palette.linkPressed :
                    link == hotLink     ? palette.linkHover : palette.link;
            underline = link == hotLink;
            link++;
        }
        layout->SetDrawingEffect(UiEffectBrush(target, cache, color), range);
        layout->SetUnderline(underline, range);
        at += range.length;
    }
    return layout.get();
}
