// Tooltip content, layout and placement (pure): a hover readout's rows or a complete-text block,
// laid out inside a region of the work area beside the pointer, never under it, and the items
// the tooltip window paints (ui/tooltip.h). Sizes follow the approved mockups (q6a-tooltip,
// q6-rows, s-tooltips): caption text on 16 DIP lines and 8 DIP padding; a row's 16 DIP swatch
// and 6 DIP gap, 12 DIP before its mandatory part, titles cut no shorter than 64 DIP, 4 DIP
// between rows and 16 between columns; text blocks at most 420 DIP wide with 10 DIP side
// padding, 8 above and 9 below; the pointer kept 8 DIP clear.
static constexpr float kUiTipPad = 8, kUiTipLine = 16, kUiTipSwatch = 16 + 6, kUiTipGap = 12, kUiTipMinTitle = 64,
                       kUiTipRowGap = 4, kUiTipColumnGap = 16, kUiTipTextWidth = 420, kUiTipTextSide = 10,
                       kUiTipTextTop = 8, kUiTipTextBottom = 9, kUiTipMargin = 8;
static constexpr PCWSTR kUiTipSwatchPath = L"M2,8H14";  // 16 DIP view box

// One readout row: a swatch in its line's color and width, the line's title (the only part ever
// cut) and the mandatory part (period, value with its unit, time), never cut.
struct UiReadoutRow {
    std::wstring title;
    std::vector<UiRun> detail;
    D2D1_COLOR_F color{};
    float stroke = 2;  // DIPs
};

// What a tooltip shows: readout rows or a complete-text block. key 0: nothing; an unchanged key:
// the same content. `more`: the client's hint for N rows that don't fit ("+N more — select one
// account"); without one the hint is "+N".
struct UiTooltipContent {
    uint64_t key = 0;
    std::vector<UiReadoutRow> rows;
    std::function<std::wstring(int)> more;
    std::vector<UiRun> text;
};

// A readout's widths, measured once per content: the widest title and the widest mandatory part
// (whole DIPs).
struct UiTooltipMeasure {
    float title = 0, detail = 0;
};

static UiTooltipMeasure UiMeasureReadout(UiGraphics& g, const UiTooltipContent& content) {
    UiTooltipMeasure measure;
    for (const UiReadoutRow& row : content.rows) {
        measure.title = std::max(measure.title,
                                 UiLayoutSize(UiRichLayout(g, {{row.title}}, UiFont::Caption, 100000)).width);
        measure.detail = std::max(measure.detail,
                                  std::ceil(UiLayoutSize(UiRichLayout(g, row.detail, UiFont::Caption, 100000)).width));
    }
    return measure;
}

// A laid-out tooltip in DIPs. A readout: `columns` columns of `rows` row slots, each `column`
// wide with titles `title` wide, of one-line rows or two-line ones (`narrow`: the mandatory part
// under the title); its first `shown` rows, then, when `more` rows don't fit, the "+N more" hint
// (`hint` tall) in the slots after them. A text block: wrapped `column` wide.
struct UiTooltipLayout {
    bool narrow = false;
    int columns = 0, rows = 0, shown = 0, more = 0;
    float column = 0, title = 0, hint = 0, width = 0, height = 0;
    bool operator==(const UiTooltipLayout&) const = default;
};

static std::wstring UiMoreText(const UiTooltipContent& content, int more) {
    return content.more ? content.more(more) : L"+" + std::to_wstring(more);
}

// Most rows, then one-line rows, then the widest titles.
static bool UiBetterReadout(const UiTooltipLayout& a, const UiTooltipLayout& b) {
    if (a.shown != b.shown) return a.shown > b.shown;
    if (a.narrow != b.narrow) return !a.narrow;
    return a.title > b.title + 0.01f;
}

// The readout's best layout inside a region (DIPs): per form and column count, equal columns
// that each hold the mandatory part and a title of at least 64 DIP; its capacity is columns x
// the rows its height holds. All rows when they fit; otherwise the "+N more" hint, measured on
// its own at the column's full width (it can wrap; the columns widen to it), takes the last
// column's last slots before the rows are counted. Of these the best (UiBetterReadout; among
// equals the fewest columns); none when no column fits.
static std::optional<UiTooltipLayout> UiLayoutReadout(UiGraphics& g, const UiTooltipContent& content,
                                                      const UiTooltipMeasure& measure, D2D1_SIZE_F region) {
    int count = int(content.rows.size());
    float innerWidth = region.width - 2 * kUiTipPad, innerHeight = region.height - 2 * kUiTipPad;
    std::optional<UiTooltipLayout> best;
    for (bool narrow : {false, true}) {
        float line = narrow ? 2 * kUiTipLine : kUiTipLine;
        int rows = int(std::floor((innerHeight + kUiTipRowGap) / (line + kUiTipRowGap)));
        if (count < 1 || rows < 1) continue;
        for (int columns = 1;; columns++) {
            float column = (innerWidth - float(columns - 1) * kUiTipColumnGap) / float(columns);
            float room = narrow ? column - kUiTipSwatch : column - kUiTipSwatch - kUiTipGap - measure.detail;
            if (room < kUiTipMinTitle || room < (narrow ? measure.detail : 0)) break;
            UiTooltipLayout layout;
            layout.narrow = narrow;
            layout.title = std::min(measure.title, room);
            layout.column = kUiTipSwatch + (narrow ? std::max(layout.title, measure.detail) :
                                                     layout.title + kUiTipGap + measure.detail);
            int capacity = columns * rows;
            layout.columns = columns;
            layout.rows = rows;
            layout.shown = count;
            if (count > capacity) {
                int slots = 1;
                DWRITE_TEXT_METRICS hint{};
                while (slots <= rows) {
                    layout.more = count - (capacity - slots);
                    winrt::check_hresult(UiLayout(g, UiMoreText(content, layout.more), UiTextStyleFor(UiFont::Caption),
                                                  column - kUiTipSwatch)->GetMetrics(&hint));
                    int need = int(std::ceil((hint.height + kUiTipRowGap) / (line + kUiTipRowGap) - 0.001f));
                    if (need <= slots) break;
                    slots = need;
                }
                if (slots > rows) continue;
                layout.hint = hint.height;
                layout.column = std::max(layout.column, std::min(column, kUiTipSwatch + std::ceil(hint.width)));
                layout.shown = capacity - slots;
            } else {
                // Balanced: as few columns as hold every row, as few rows as fill them.
                layout.columns = (count + rows - 1) / rows;
                layout.rows = (count + layout.columns - 1) / layout.columns;
            }
            layout.width = 2 * kUiTipPad + float(layout.columns) * layout.column +
                           float(layout.columns - 1) * kUiTipColumnGap;
            layout.height = 2 * kUiTipPad + float(layout.rows) * line + float(layout.rows - 1) * kUiTipRowGap;
            if (!best || UiBetterReadout(layout, *best)) best = layout;
            if (count <= capacity) break;  // More columns would only narrow the titles.
        }
    }
    return best;
}

// The pointer a tooltip is placed for, in screen pixels: the cursor's hotspot, the cursor image's
// rectangle (wherever the hotspot lies in it), the work area of its monitor (any origin) and that
// monitor's DPI.
struct UiTooltipKey {
    POINT cursor{};
    RECT cursorRect{}, work{};
    UINT dpi = 96;
};

enum class UiTooltipSide : uint8_t { Below, Right, Above, Left };

struct UiTooltipSpot {
    RECT rect{};  // Screen pixels
    UiTooltipSide side = UiTooltipSide::Below;
    UiTooltipLayout layout;
};

// Where a tooltip goes. The exclusion rectangle is the cursor image (and its hotspot) 8 DIP
// out; the candidate regions are the work area below, right of, above and left of it. A readout
// takes the region whose layout is best (UiBetterReadout; ties in that side order), a text block
// (wrapped at the region's width, at most 420 DIP, at its full height) the first region that
// holds it whole. The rectangle lies against the exclusion rectangle, aligned with the pointer
// along it, and is never moved or clamped afterwards: it never covers the cursor. None when
// nothing fits: no rectangle at all.
static std::optional<UiTooltipSpot> UiTooltipPlacement(UiGraphics& g, const UiTooltipContent& content,
                                                       const UiTooltipMeasure& measure, const UiTooltipKey& at) {
    float scale = float(std::max(at.dpi, 1u)) / 96;
    LONG margin = LONG(std::ceil(kUiTipMargin * scale));
    const RECT& c = at.cursorRect;
    const RECT exclusion{std::min(c.left, at.cursor.x) - margin, std::min(c.top, at.cursor.y) - margin,
                         std::max(c.right, at.cursor.x + 1) + margin, std::max(c.bottom, at.cursor.y + 1) + margin};
    const RECT& w = at.work;
    const std::pair<UiTooltipSide, RECT> regions[] = {
        {UiTooltipSide::Below, {w.left, std::max(exclusion.bottom, w.top), w.right, w.bottom}},
        {UiTooltipSide::Right, {std::max(exclusion.right, w.left), w.top, w.right, w.bottom}},
        {UiTooltipSide::Above, {w.left, w.top, w.right, std::min(exclusion.top, w.bottom)}},
        {UiTooltipSide::Left, {w.left, w.top, std::min(exclusion.left, w.right), w.bottom}},
    };
    std::optional<UiTooltipSpot> best;
    for (const auto& [side, region] : regions) {
        LONG width = region.right - region.left, height = region.bottom - region.top;
        if (width <= 0 || height <= 0) continue;
        D2D1_SIZE_F room{float(width) / scale, float(height) / scale};
        std::optional<UiTooltipLayout> layout;
        if (!content.rows.empty()) {
            layout = UiLayoutReadout(g, content, measure, room);
            if (!layout || (best && !UiBetterReadout(*layout, best->layout))) continue;
        } else {
            float wrap = std::min(kUiTipTextWidth, room.width - 2 * kUiTipTextSide);
            if (content.text.empty() || wrap < 1) continue;
            DWRITE_TEXT_METRICS metrics{};
            winrt::check_hresult(UiRichLayout(g, content.text, UiFont::Caption, wrap)->GetMetrics(&metrics));
            layout = UiTooltipLayout{};
            layout->column = wrap;
            layout->width = std::ceil(metrics.width) + 2 * kUiTipTextSide;
            layout->height = metrics.height + kUiTipTextTop + kUiTipTextBottom;
            if (layout->width > room.width || layout->height > room.height) continue;
        }
        LONG cx = LONG(std::ceil(layout->width * scale - 0.01f)), cy = LONG(std::ceil(layout->height * scale - 0.01f));
        if (cx > width || cy > height) continue;
        LONG x = side == UiTooltipSide::Right ? region.left :
                 side == UiTooltipSide::Left  ? region.right - cx :
                                                std::clamp(at.cursor.x, region.left, region.right - cx);
        LONG y = side == UiTooltipSide::Below ? region.top :
                 side == UiTooltipSide::Above ? region.bottom - cy :
                                                std::clamp(at.cursor.y, region.top, region.bottom - cy);
        best = UiTooltipSpot{{x, y, x + cx, y + cy}, side, *layout};
        if (content.rows.empty()) break;
    }
    return best;
}

// The tooltip's items in its DIPs (ids from 1): per shown readout row a swatch, its title cut at
// the layout's title width and its mandatory part beside it (or under the title); the hint in
// muted text after them; or the text block.
static std::vector<UiItem> UiTooltipItems(const UiTooltipContent& content, const UiTooltipMeasure& measure,
                                          const UiTooltipLayout& layout) {
    std::vector<UiItem> items;
    auto text = [&](D2D1_RECT_F rect, std::vector<UiRun> runs, UiInk ink, bool trim) {
        UiItem item;
        item.id = uint32_t(items.size() + 1);
        item.kind = UiItemKind::Text;
        item.font = UiFont::Caption;
        item.ink = ink;
        item.rect = rect;
        item.runs = std::move(runs);
        item.trim = trim;
        items.push_back(std::move(item));
    };
    if (content.rows.empty()) {
        text({kUiTipTextSide, kUiTipTextTop, kUiTipTextSide + layout.column, layout.height - kUiTipTextBottom},
             content.text, UiInk::Primary, false);
        return items;
    }
    float line = layout.narrow ? 2 * kUiTipLine : kUiTipLine;
    auto slot = [&](int index) {
        int rows = std::max(layout.rows, 1);
        return D2D1_POINT_2F{kUiTipPad + float(index / rows) * (layout.column + kUiTipColumnGap),
                             kUiTipPad + float(index % rows) * (line + kUiTipRowGap)};
    };
    for (int i = 0; i < layout.shown && i < int(content.rows.size()); i++) {
        const UiReadoutRow& row = content.rows[size_t(i)];
        D2D1_POINT_2F at = slot(i);
        UiItem swatch;
        swatch.id = uint32_t(items.size() + 1);
        swatch.kind = UiItemKind::Shape;
        swatch.viewBox = 16;
        swatch.rect = {at.x, at.y, at.x + 16, at.y + 16};
        swatch.parts = {{kUiTipSwatchPath, row.color, UiInk::Inherit, 1, row.stroke}};
        items.push_back(std::move(swatch));
        float left = at.x + kUiTipSwatch;
        text({left, at.y, left + layout.title, at.y + kUiTipLine}, {{row.title}}, UiInk::Primary, true);
        D2D1_POINT_2F detail = layout.narrow ? D2D1_POINT_2F{left, at.y + kUiTipLine} :
                                               D2D1_POINT_2F{left + layout.title + kUiTipGap, at.y};
        text({detail.x, detail.y, detail.x + measure.detail, detail.y + kUiTipLine}, row.detail, UiInk::Primary,
             false);
    }
    if (layout.more) {
        D2D1_POINT_2F at = slot(layout.shown);
        text({at.x + kUiTipSwatch, at.y, at.x + layout.column, at.y + layout.hint},
             {{UiMoreText(content, layout.more)}}, UiInk::Muted, false);
    }
    return items;
}

// What a tooltip shows: its content (measured when its key changes), the pointer it was placed
// for and where. Content and placement are deduped apart.
struct UiTooltipModel {
    UiTooltipContent content;
    UiTooltipMeasure measure;
    UiTooltipKey at;
    std::optional<UiTooltipSpot> spot;
};

// What an update changed: nothing; the rectangle only (the same layout); the layout (new items);
// or the shown tooltip no longer fits anywhere.
enum class UiTooltipChange : uint8_t { None, Move, Layout, Hide };

// Takes `content` when its key differs from the shown one's (measured again), else keeps the
// shown content, and places it for the pointer `at`. The same content at the same pointer,
// cursor image, work area and DPI does nothing.
static UiTooltipChange UiTooltipUpdate(UiGraphics& g, UiTooltipModel& model, UiTooltipContent content,
                                       const UiTooltipKey& at) {
    auto same = [](const RECT& a, const RECT& b) {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    };
    bool shown = model.spot.has_value();
    bool fresh = content.key != model.content.key;
    if (fresh) {
        model.content = std::move(content);
        model.measure = UiMeasureReadout(g, model.content);
    } else if (shown && at.cursor.x == model.at.cursor.x && at.cursor.y == model.at.cursor.y &&
               same(at.cursorRect, model.at.cursorRect) && same(at.work, model.at.work) && at.dpi == model.at.dpi) {
        return UiTooltipChange::None;
    }
    model.at = at;
    std::optional<UiTooltipSpot> spot =
        model.content.key ? UiTooltipPlacement(g, model.content, model.measure, at) : std::nullopt;
    if (!spot) {
        model.spot.reset();
        return shown ? UiTooltipChange::Hide : UiTooltipChange::None;
    }
    bool relaid = !shown || fresh || !(model.spot->layout == spot->layout);
    bool moved = !shown || !same(model.spot->rect, spot->rect);
    model.spot = spot;
    return relaid ? UiTooltipChange::Layout : moved ? UiTooltipChange::Move : UiTooltipChange::None;
}
