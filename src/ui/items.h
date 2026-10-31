// Item model of the custom-drawn windows: flat item lists built from explicit snapshots, ids,
// hit testing, focus order and shared layout builders. Pure. Builders read no globals; paint and
// input work only from the list.

enum class UiItemKind : uint8_t { Nav, Card, Text, Glyph, Shape, Button, Link };
enum class UiButtonStyle : uint8_t { Standard, Accent, Subtle, Danger };

// One filled or stroked path of a Shape item, in the item's view box. A part with an Inherit
// ink keeps its own color (illustrations); others follow the theme.
struct UiShapePart {
    PCWSTR path = nullptr;
    D2D1_COLOR_F color{};
    UiInk ink = UiInk::Inherit;
    float alpha = 1;   // Applied to the ink.
    float stroke = 0;  // View-box units; 0 fills.
};

struct UiItem {
    uint32_t id = 0;
    UiItemKind kind = UiItemKind::Text;
    D2D1_RECT_F rect{};
    UiFont font = UiFont::Body;
    UiInk ink = UiInk::Primary;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    std::wstring text;               // Nav, Button and Link label; Glyph character
    std::wstring icon;               // Nav and Button glyph
    float iconSize = 16;             // Glyph, Nav and Button glyph size
    std::vector<UiRun> runs;         // Text, wrapped to the rectangle's width
    std::vector<UiShapePart> parts;  // Shape
    float viewBox = 24;              // Shape: square view box mapped onto the rectangle
    float opacity = 1;
    UiButtonStyle button = UiButtonStyle::Standard;
    UiAction action;
    bool selected = false;
    bool enabled = true;
};

// A focus or pointer target: an item, or one inline link (link >= 0) of a Text item. id 0 = none.
struct UiFocusRef {
    uint32_t id = 0;
    int link = -1;
    bool operator==(const UiFocusRef&) const = default;
};

enum class UiSurfaceId : uint8_t { Main, Content };

// One built window state, in DIPs: main-surface items (navigation, pinned header), the
// viewport's rectangle in the main client, and the scrolling content at scroll offset 0.
struct UiFrame {
    std::vector<UiItem> main;
    D2D1_RECT_F viewport{};
    std::vector<UiItem> content;
    float contentHeight = 0;
};

struct UiFocusStop {
    UiFocusRef ref;
    UiSurfaceId surface = UiSurfaceId::Main;
    bool nav = false;
};

static bool UiInteractiveKind(UiItemKind kind) {
    return kind == UiItemKind::Nav || kind == UiItemKind::Card || kind == UiItemKind::Button ||
           kind == UiItemKind::Link;
}

static bool UiFocusable(const UiItem& item) {
    return UiInteractiveKind(item.kind) && item.enabled && item.action.kind != UiActionKind::None;
}

static const UiItem* UiFindItem(const UiFrame& frame, uint32_t id, UiSurfaceId* surface = nullptr) {
    if (!id) return nullptr;
    for (const UiItem& item : frame.main) {
        if (item.id != id) continue;
        if (surface) *surface = UiSurfaceId::Main;
        return &item;
    }
    for (const UiItem& item : frame.content) {
        if (item.id != id) continue;
        if (surface) *surface = UiSurfaceId::Content;
        return &item;
    }
    return nullptr;
}

// Item order is focus order: main surface first, then content; inline links in text order.
static std::vector<UiFocusStop> UiFocusStops(const UiFrame& frame) {
    std::vector<UiFocusStop> stops;
    for (UiSurfaceId surface : {UiSurfaceId::Main, UiSurfaceId::Content}) {
        for (const UiItem& item : surface == UiSurfaceId::Main ? frame.main : frame.content) {
            if (item.kind == UiItemKind::Text) {
                int link = 0;
                for (const UiRun& run : item.runs) {
                    if (run.style != UiRunStyle::Link) continue;
                    if (item.enabled && run.action.kind != UiActionKind::None) {
                        stops.push_back({{item.id, link}, surface, false});
                    }
                    link++;
                }
            } else if (UiFocusable(item)) {
                stops.push_back({{item.id, -1}, surface, item.kind == UiItemKind::Nav});
            }
        }
    }
    return stops;
}

static const UiAction* UiActionOf(const UiFrame& frame, UiFocusRef ref) {
    const UiItem* item = UiFindItem(frame, ref.id);
    if (!item || !item->enabled) return nullptr;
    if (ref.link < 0) return UiFocusable(*item) ? &item->action : nullptr;
    const UiRun* run = UiLinkRun(item->runs, ref.link);
    return run && run->action.kind != UiActionKind::None ? &run->action : nullptr;
}

// The interactive element under a point: an inline link, else the topmost interactive item.
// A disabled interactive item blocks the items below it.
static UiFocusRef UiHitTest(UiGraphics& g, const std::vector<UiItem>& items, D2D1_POINT_2F point) {
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        const UiItem& item = *it;
        if (!UiContains(item.rect, point)) continue;
        if (item.kind == UiItemKind::Text) {
            if (!item.enabled || UiLinkRanges(item.runs).empty()) continue;
            int link = UiLinkAt(UiRichLayout(g, item.runs, item.font, item.rect.right - item.rect.left,
                                             item.align),
                                item.runs, {point.x - item.rect.left, point.y - item.rect.top});
            const UiRun* run = link >= 0 ? UiLinkRun(item.runs, link) : nullptr;
            if (run && run->action.kind != UiActionKind::None) return {item.id, link};
            continue;
        }
        if (!UiInteractiveKind(item.kind)) continue;
        return UiFocusable(item) ? UiFocusRef{item.id, -1} : UiFocusRef{};
    }
    return {};
}

// Focus ring boxes: the item, or each line box of an inline link.
static std::vector<D2D1_RECT_F> UiFocusBounds(UiGraphics& g, const UiItem& item, int link) {
    if (link < 0) return {item.rect};
    std::vector<D2D1_RECT_F> rects = UiLinkRects(
        UiRichLayout(g, item.runs, item.font, item.rect.right - item.rect.left, item.align),
        item.runs, link);
    for (D2D1_RECT_F& rect : rects) {
        rect = {rect.left + item.rect.left, rect.top + item.rect.top, rect.right + item.rect.left,
                rect.bottom + item.rect.top};
    }
    return rects;
}

// Focus after a rebuild: kept while it exists, else the next surviving stop in the old order,
// else the previous one, else `fallback` (the selected navigation item).
static UiFocusRef UiResolveFocus(const std::vector<UiFocusStop>& before,
                                 const std::vector<UiFocusStop>& after, UiFocusRef focus,
                                 UiFocusRef fallback) {
    auto exists = [&](UiFocusRef ref) {
        return std::any_of(after.begin(), after.end(),
                           [&](const UiFocusStop& stop) { return stop.ref == ref; });
    };
    if (!focus.id || exists(focus)) return focus;
    auto at = std::find_if(before.begin(), before.end(),
                           [&](const UiFocusStop& stop) { return stop.ref == focus; });
    if (at != before.end()) {
        for (auto next = at + 1; next != before.end(); ++next) {
            if (exists(next->ref)) return next->ref;
        }
        for (auto previous = at; previous != before.begin();) {
            --previous;
            if (exists(previous->ref)) return previous->ref;
        }
    }
    return fallback;
}

// Pinned header rule: the unpinnable part (the Layout and Display preview) joins the scrolling
// content when the viewport would otherwise get less than max(280 DIP, half the client height).
// The title stays. Pages without an unpinnable part don't call it.
[[maybe_unused]] static bool UiUnpinHeader(float clientHeight, float pinnedHeight,
                                           float unpinnableHeight) {
    return unpinnableHeight > 0 &&
           clientHeight - pinnedHeight - unpinnableHeight < std::max(280.0f, clientHeight / 2);
}

// Thin Windows 11 scrollbar at the viewport's right edge: a 2 DIP indicator, expanded to a
// 6 DIP thumb on a 12 DIP track under the pointer or while dragging.
struct UiScrollbar {
    bool visible = false;
    D2D1_RECT_F track{}, thumb{};
};

static UiScrollbar UiScrollbarFor(D2D1_SIZE_F viewport, float offset, float content, bool expanded) {
    UiScrollbar bar;
    if (content <= viewport.height + 0.5f || viewport.height <= 8) return bar;
    bar.visible = true;
    bar.track = {viewport.width - 12, 2, viewport.width, viewport.height - 2};
    float trackHeight = bar.track.bottom - bar.track.top;
    float length = std::clamp(trackHeight * viewport.height / content, 24.0f, trackHeight);
    float range = content - viewport.height;
    float top = bar.track.top + (trackHeight - length) * std::clamp(offset / range, 0.0f, 1.0f);
    float width = expanded ? 6.0f : 2.0f;
    bar.thumb = {viewport.width - 3 - width, top, viewport.width - 3, top + length};
    return bar;
}

// Shared builders. Sizes follow the approved mockups (CSS px = DIP).

// Section header (mockup .shead): BodyStrong text at y; returns where the next card starts.
static float UiAddSection(std::vector<UiItem>& items, uint32_t& id, float x, float y, float width,
                          std::wstring text) {
    UiItem header;
    header.id = id++;
    header.kind = UiItemKind::Text;
    header.font = UiFont::BodyStrong;
    header.rect = {x, y, x + width, y + 20};
    header.runs.push_back({std::move(text)});
    items.push_back(std::move(header));
    return y + 28;
}

// Settings card (mockup .scard): optional leading glyph, a title, a description and a trailing
// slot of `trailing` size (or a trailing glyph), vertically centered. With an action the whole
// card is one interactive item.
struct UiCardSpec {
    std::wstring glyph;
    std::wstring title;
    std::vector<UiRun> description;
    D2D1_SIZE_F trailing{};
    std::wstring trailingGlyph;
    UiAction action;
    float minHeight = 68;
};

struct UiCardLayout {
    D2D1_RECT_F card{}, trailing{};
};

static UiCardLayout UiAddCard(UiGraphics& g, std::vector<UiItem>& items, uint32_t& id, float x,
                              float y, float width, const UiCardSpec& spec) {
    constexpr float kBorder = 1, kPadX = 18, kPadY = 12, kGap = 16, kLeadWidth = 22;
    constexpr float kDescriptionMax = 470;
    D2D1_SIZE_F trailing = spec.trailing;
    if (!spec.trailingGlyph.empty()) trailing = {12, 12};
    float left = x + kBorder + kPadX, right = x + width - kBorder - kPadX;
    float textLeft = left + (spec.glyph.empty() ? 0 : kLeadWidth + kGap);
    float textRight = right - (trailing.width > 0 ? trailing.width + kGap : 0);
    float textWidth = std::max(textRight - textLeft, 40.0f);

    float titleHeight = UiLayoutSize(UiLayout(g, spec.title, UiTextStyleFor(UiFont::Body),
                                              textWidth)).height;
    float descriptionWidth = std::min(textWidth, kDescriptionMax);
    float descriptionHeight =
        spec.description.empty() ? 0 :
        UiLayoutSize(UiRichLayout(g, spec.description, UiFont::Caption, descriptionWidth)).height;
    float textHeight = titleHeight + descriptionHeight;
    float content = std::max({textHeight, trailing.height, spec.glyph.empty() ? 0.0f : 20.0f});
    float height = std::max(spec.minHeight, content + 2 * (kBorder + kPadY));
    float center = y + height / 2;

    UiCardLayout layout;
    layout.card = {x, y, x + width, y + height};
    layout.trailing = {right - trailing.width, center - trailing.height / 2, right,
                       center + trailing.height / 2};

    UiItem card;
    card.id = id++;
    card.kind = UiItemKind::Card;
    card.rect = layout.card;
    card.action = spec.action;
    items.push_back(std::move(card));

    if (!spec.glyph.empty()) {
        UiItem lead;
        lead.id = id++;
        lead.kind = UiItemKind::Glyph;
        lead.text = spec.glyph;
        lead.iconSize = 18;
        lead.rect = {left, center - 10, left + kLeadWidth, center + 10};
        items.push_back(std::move(lead));
    }
    UiItem title;
    title.id = id++;
    title.kind = UiItemKind::Text;
    title.rect = {textLeft, center - textHeight / 2, textLeft + textWidth,
                  center - textHeight / 2 + titleHeight};
    title.runs.push_back({spec.title});
    items.push_back(std::move(title));
    if (!spec.description.empty()) {
        UiItem description;
        description.id = id++;
        description.kind = UiItemKind::Text;
        description.font = UiFont::Caption;
        description.ink = UiInk::Muted;
        description.rect = {textLeft, center - textHeight / 2 + titleHeight,
                            textLeft + descriptionWidth, center + textHeight / 2};
        description.runs = spec.description;
        items.push_back(std::move(description));
    }
    if (!spec.trailingGlyph.empty()) {
        UiItem glyph;
        glyph.id = id++;
        glyph.kind = UiItemKind::Glyph;
        glyph.text = spec.trailingGlyph;
        glyph.iconSize = 12;
        glyph.ink = UiInk::Secondary;
        glyph.rect = layout.trailing;
        items.push_back(std::move(glyph));
    }
    return layout;
}
