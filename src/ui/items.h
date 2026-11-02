// Item model of the custom-drawn windows: flat item lists built from explicit snapshots, ids,
// hit testing, focus order and shared layout builders. Pure. Builders read no globals; paint and
// input work only from the list.

// Nav: a navigation entry: a glyph (`icon`) or a shape (`parts`, a 24 DIP view box drawn at 16
// DIP), its label `text` in `ink` (ellipsized) and a trailing value (`runs`: semibold caption in
// its first run's color or ink). Card: a raised panel; a subtle one (button = Subtle) only shows
// the pointer over it, and a selected one is lifted (an accent border); a Caution or Danger ink
// tints it as an info bar of that severity. Text: rich text wrapped to the rectangle's width,
// or one line ellipsized at it (`trim`). Button: an optional glyph (`icon`) before its label,
// centered alone without one; with options, a menu button whose activation opens them in the
// popup. Toggle: selected = on; its label ("On"/"Off", or `text`) leads the
// switch, or follows a leading switch when align is trailing. Dropdown: text = the chosen
// option's label, action.value = its value; activating it opens the popup (ui/popup.h).
// NumberBox: the drawn frame of a native text box (ui/text-box.h) showing `text`, centered
// without spin buttons when align is center; SpinButton: its up/down buttons (not tab stops).
// TextBox: the frame of a native text box for free text, `runs` its placeholder while empty,
// `maximum` its character limit. Option: one row of the popup; with runs, a two-line menu row
// (title `text`, detail `runs`, an icon from `parts`); with a glyph (`icon`), a one-line menu row,
// a separator line above it when `separator` is set. Slider: action.value within [minimum,
// maximum]. Swatch: a color choice, action.value = 0xBBGGRR, selected = current; without an
// action kind, a color dot. Gallery: a choice card named `text` with a client-drawn thumbnail.
// Preview: a client-drawn strip that pans sideways (not a tab stop). Chip: a pill named `text`,
// selected = on; without an action it only shows. Handle: a drag handle (glyph `text`, not a
// tab stop) for its `owner`, which holds keyboard focus for it. Rule: a 1 px separator along the
// rectangle's top edge. Segmented: one tab stop choosing among `options` (action.value = the
// selected one's value); the pointer targets each segment (UiFocusRef::link = its index).
// Pill: a status chip tinted with its `ink`, a dot (hollow unless selected) before `text`; never
// interactive. Chart: client-drawn, a tab stop while enabled (no action needed, never activated):
// the pointer over it reports to the client, which may answer with a readout (ui/host.h), and
// its arrows scroll the content. Custom: client-drawn whole; a decoration, or with an action a
// control like a button (hot, pressed, a tab stop, activated, its tooltip) whose focus ring is
// rounded at its ends.
enum class UiItemKind : uint8_t {
    Nav, Card, Text, Glyph, Shape, Button, Link, Toggle, Dropdown, NumberBox, SpinButton, Option,
    Slider, Swatch, Gallery, Preview, Chip, TextBox, Handle, Rule, Segmented, Pill, Chart, Custom
};
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

// A dropdown or menu choice. Menus give each a detail line and an icon (two-line rows), or a glyph
// (one-line rows), optionally after a separator.
struct UiOption {
    std::wstring text;
    int value = 0;
    std::wstring detail;
    std::vector<UiShapePart> parts;  // 24 DIP view box
    std::wstring icon;               // Glyph of a one-line menu row
    bool separator = false;          // A separator line above it (one-line menus)
};

struct UiItem {
    uint32_t id = 0;
    UiItemKind kind = UiItemKind::Text;
    D2D1_RECT_F rect{};
    UiFont font = UiFont::Body;      // Text; Button and Chip labels
    UiInk ink = UiInk::Primary;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    std::wstring text;               // Label (Nav, Button, Link, Toggle, Dropdown, Option, Chip,
                                     // Pill); Glyph, SpinButton and Handle character; text box text
    std::wstring icon;               // Nav, Button and one-line menu row glyph
    float iconSize = 16;             // Glyph, Nav, Button and Handle glyph size
    std::vector<UiRun> runs;         // Text, wrapped to the rectangle's width; TextBox
                                     // placeholder; Option detail; Nav value
    std::vector<UiShapePart> parts;  // Shape; Option and Nav icon
    std::vector<UiOption> options;   // Dropdown; menu Button; Segmented
    std::vector<UiRun> tooltip;      // Complete text shown after the hover delay (icon buttons, chips)
    float viewBox = 24;              // Shape: square view box mapped onto the rectangle
    float opacity = 1;
    UiButtonStyle button = UiButtonStyle::Standard;  // Button; Card (Subtle)
    UiAction action;
    uint32_t owner = 0;              // SpinButton: its NumberBox; Handle: its focus owner
    int minimum = 0, maximum = 0;    // Slider range; Preview: its sideways scroll range;
                                     // TextBox: maximum = character limit
    bool selected = false;
    bool enabled = true;
    bool trim = false;               // Text: one line, ellipsized
    bool separator = false;          // Option: a menu separator line above it
};

// A focus or pointer target: an item, or one inline link (link >= 0) of a Text item. id 0 = none.
struct UiFocusRef {
    uint32_t id = 0;
    int link = -1;
    bool operator==(const UiFocusRef&) const = default;
};

// Nav: the main surface's scrolling navigation pane (UiFrame::nav); Content: the viewport's.
enum class UiSurfaceId : uint8_t { Main, Content, Nav };

// One built window state, in DIPs: main-surface items (navigation, pinned header), the
// viewport's rectangle in the main client, and the scrolling content at scroll offset 0.
// Optionally a scrolling navigation pane on the main surface: `nav` items in main coordinates at
// scroll offset 0, clipped to `navPane`, whose content is `navHeight` tall from the pane's top.
struct UiFrame {
    std::vector<UiItem> main;
    D2D1_RECT_F viewport{};
    std::vector<UiItem> content;
    float contentHeight = 0;
    std::vector<UiItem> nav;
    D2D1_RECT_F navPane{};
    float navHeight = 0;
};

static const std::vector<UiItem>& UiSurfaceItems(const UiFrame& frame, UiSurfaceId surface) {
    return surface == UiSurfaceId::Main ? frame.main : surface == UiSurfaceId::Nav ? frame.nav : frame.content;
}

struct UiFocusStop {
    UiFocusRef ref;
    UiSurfaceId surface = UiSurfaceId::Main;
    bool nav = false;
    bool chart = false;  // Its arrows scroll the content
};

static bool UiInteractiveKind(UiItemKind kind) {
    return kind != UiItemKind::Text && kind != UiItemKind::Glyph && kind != UiItemKind::Shape &&
           kind != UiItemKind::Rule && kind != UiItemKind::Pill;
}

// Reacts to the pointer and can be activated (a chart only reacts).
static bool UiActive(const UiItem& item) {
    return UiInteractiveKind(item.kind) && item.enabled &&
           (item.action.kind != UiActionKind::None || item.kind == UiItemKind::Chart);
}

// A tab stop. Spin buttons belong to their number box; options to the dropdown field; a
// preview strip only pans; a drag handle's keys go to its owner.
static bool UiFocusable(const UiItem& item) {
    return UiActive(item) && item.kind != UiItemKind::SpinButton && item.kind != UiItemKind::Option &&
           item.kind != UiItemKind::Preview && item.kind != UiItemKind::Handle;
}

static const UiItem* UiFindItem(const UiFrame& frame, uint32_t id, UiSurfaceId* surface = nullptr) {
    if (!id) return nullptr;
    for (UiSurfaceId in : {UiSurfaceId::Nav, UiSurfaceId::Main, UiSurfaceId::Content}) {
        for (const UiItem& item : UiSurfaceItems(frame, in)) {
            if (item.id != id) continue;
            if (surface) *surface = in;
            return &item;
        }
    }
    return nullptr;
}

// Item order is focus order: the navigation pane, the main surface, then content; inline links
// in text order.
static std::vector<UiFocusStop> UiFocusStops(const UiFrame& frame) {
    std::vector<UiFocusStop> stops;
    for (UiSurfaceId surface : {UiSurfaceId::Nav, UiSurfaceId::Main, UiSurfaceId::Content}) {
        for (const UiItem& item : UiSurfaceItems(frame, surface)) {
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
                stops.push_back({{item.id, -1}, surface, item.kind == UiItemKind::Nav, item.kind == UiItemKind::Chart});
            }
        }
    }
    return stops;
}

static const UiAction* UiActionOf(const UiFrame& frame, UiFocusRef ref) {
    const UiItem* item = UiFindItem(frame, ref.id);
    if (!item || !item->enabled) return nullptr;
    if (ref.link < 0) return UiActive(*item) ? &item->action : nullptr;
    const UiRun* run = UiLinkRun(item->runs, ref.link);
    return run && run->action.kind != UiActionKind::None ? &run->action : nullptr;
}

// A segmented control's segments (mockup .segctl, .segi): 13 DIP labels with 12 DIP padding,
// side by side inside its 1 DIP border and 2 DIP padding. Paint, hit testing and focus share it.
static std::vector<D2D1_RECT_F> UiSegmentRects(UiGraphics& g, const UiItem& item) {
    std::vector<D2D1_RECT_F> rects;
    float x = item.rect.left + 3;
    for (const UiOption& option : item.options) {
        float width = std::ceil(UiLayoutSize(UiLayout(g, option.text, UiTextStyleFor(UiFont::Note), 10000,
                                                      false)).width) + 24;
        rects.push_back({x, item.rect.top + 3, x + width, item.rect.bottom - 3});
        x += width;
    }
    return rects;
}

// The selected segment's index (its value is action.value), or -1.
static int UiSelectedSegment(const UiItem& item) {
    for (size_t i = 0; i < item.options.size(); i++) {
        if (item.options[i].value == item.action.value) return int(i);
    }
    return -1;
}

// The interactive element under a point: an inline link or a segment, else the topmost
// interactive item. A disabled interactive item blocks the items below it; an enabled inactive
// one (a plain card, a chip that only shows) doesn't. A slider takes in its thumb, which
// overhangs the track's ends by its 10 DIP radius.
static UiFocusRef UiHitTest(UiGraphics& g, const std::vector<UiItem>& items, D2D1_POINT_2F point) {
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        const UiItem& item = *it;
        if (!UiContains(item.kind == UiItemKind::Slider ? UiInflate(item.rect, 10, 0) : item.rect, point)) {
            continue;
        }
        if (item.kind == UiItemKind::Text) {
            if (!item.enabled || UiLinkRanges(item.runs).empty()) continue;
            int link = UiLinkAt(UiRichLayout(g, item.runs, item.font, item.rect.right - item.rect.left,
                                             item.align, item.trim),
                                item.runs, {point.x - item.rect.left, point.y - item.rect.top});
            const UiRun* run = link >= 0 ? UiLinkRun(item.runs, link) : nullptr;
            if (run && run->action.kind != UiActionKind::None) return {item.id, link};
            continue;
        }
        if (!UiInteractiveKind(item.kind) || (item.enabled && !UiActive(item))) continue;
        if (!UiActive(item)) return {};
        if (item.kind == UiItemKind::Segmented) {
            std::vector<D2D1_RECT_F> segments = UiSegmentRects(g, item);
            for (size_t i = 0; i < segments.size(); i++) {
                if (UiContains(segments[i], point)) return {item.id, int(i)};
            }
        }
        return {item.id, -1};
    }
    return {};
}

// Focus ring boxes: the item, its selected segment, or each line box of an inline link.
static std::vector<D2D1_RECT_F> UiFocusBounds(UiGraphics& g, const UiItem& item, int link) {
    if (item.kind == UiItemKind::Segmented) {
        int selected = UiSelectedSegment(item);
        return {selected >= 0 ? UiSegmentRects(g, item)[size_t(selected)] : item.rect};
    }
    if (link < 0) return {item.rect};
    std::vector<D2D1_RECT_F> rects = UiLinkRects(
        UiRichLayout(g, item.runs, item.font, item.rect.right - item.rect.left, item.align, item.trim),
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

// The same scrollbar at the navigation pane's right edge (mockup .nbar), in main DIPs.
static UiScrollbar UiNavScrollbar(const UiFrame& frame, float offset, bool expanded) {
    const D2D1_RECT_F& pane = frame.navPane;
    UiScrollbar bar = UiScrollbarFor({pane.right - pane.left, pane.bottom - pane.top}, offset,
                                     frame.navHeight, expanded);
    for (D2D1_RECT_F* rect : {&bar.track, &bar.thumb}) {
        *rect = {rect->left + pane.left, rect->top + pane.top, rect->right + pane.left, rect->bottom + pane.top};
    }
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

// Settings card (mockup .scard): optional leading glyph or color dot, a title, a description and
// a trailing slot of `trailing` size (or a trailing glyph), vertically centered. With an action
// the whole card is one interactive item. A disabled card dims its title and may say why (mockup
// .reason); an error line reports an invalid or unsaved value. A nested card is a row of an
// expanded card (mockup .scard.sub): no panel, a rule along its top, text 56 DIP in from the
// panel's edge (x and width are the panel's inside its border).
struct UiCardSpec {
    std::wstring glyph;
    std::optional<COLORREF> dot;  // Mockup .dotsw: a 14 DIP circle, 0xBBGGRR
    std::wstring title;
    std::vector<UiRun> description;
    D2D1_SIZE_F trailing{};
    std::wstring trailingGlyph;
    UiAction action;
    float minHeight = 68;
    bool enabled = true;
    bool nested = false;
    std::wstring reason, error;
};

struct UiCardLayout {
    D2D1_RECT_F card{}, trailing{};
};

static UiCardLayout UiAddCard(UiGraphics& g, std::vector<UiItem>& items, uint32_t& id, float x,
                              float y, float width, const UiCardSpec& spec) {
    constexpr float kBorder = 1, kPadX = 18, kPadY = 12, kGap = 16, kLeadWidth = 22, kDot = 14;
    constexpr float kDescriptionMax = 470, kNestedIndent = 56 - kBorder;
    D2D1_SIZE_F trailing = spec.trailing;
    if (!spec.trailingGlyph.empty()) trailing = {12, 12};
    float left = x + (spec.nested ? kNestedIndent : kBorder + kPadX);
    float right = x + width - (spec.nested ? kPadX : kBorder + kPadX);
    float lead = !spec.glyph.empty() ? kLeadWidth : spec.dot ? kDot : 0;
    float textLeft = left + (lead > 0 ? lead + kGap : 0);
    float textRight = right - (trailing.width > 0 ? trailing.width + kGap : 0);
    float textWidth = std::max(textRight - textLeft, 40.0f);

    float titleHeight = UiLayoutSize(UiLayout(g, spec.title, UiTextStyleFor(UiFont::Body),
                                              textWidth)).height;
    float descriptionWidth = std::min(textWidth, kDescriptionMax);
    float descriptionHeight =
        spec.description.empty() ? 0 :
        UiLayoutSize(UiRichLayout(g, spec.description, UiFont::Caption, descriptionWidth)).height;
    // Reason and error lines: a 12 DIP glyph, 6 DIP gap, caption text; 3 DIP above each.
    struct Note {
        const std::wstring* text;
        PCWSTR glyph;
        UiInk ink;
        float height;
    };
    std::vector<Note> notes;
    for (Note note : {Note{&spec.reason, L"\uE946", UiInk::Secondary, 0},
                      Note{&spec.error, L"\uE783", UiInk::Danger, 0}}) {
        if (note.text->empty()) continue;
        note.height = 3 + UiLayoutSize(UiLayout(g, *note.text, UiTextStyleFor(UiFont::Caption),
                                                descriptionWidth - 18)).height;
        notes.push_back(note);
    }
    float textHeight = titleHeight + descriptionHeight;
    for (const Note& note : notes) textHeight += note.height;
    float content = std::max({textHeight, trailing.height, spec.glyph.empty() ? 0.0f : 20.0f});
    float height = std::max(spec.minHeight, content + 2 * (kBorder + kPadY));
    float center = y + height / 2;

    UiCardLayout layout;
    layout.card = {x, y, x + width, y + height};
    layout.trailing = {right - trailing.width, center - trailing.height / 2, right,
                       center + trailing.height / 2};

    UiItem card;
    card.id = id++;
    card.kind = spec.nested ? UiItemKind::Rule : UiItemKind::Card;
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
    } else if (spec.dot) {
        // A swatch without an action: drawn, never a target.
        UiItem dot;
        dot.id = id++;
        dot.kind = UiItemKind::Swatch;
        dot.rect = {left, center - kDot / 2, left + kDot, center + kDot / 2};
        dot.action.value = int(*spec.dot);
        dot.enabled = spec.enabled;
        items.push_back(std::move(dot));
    }
    UiItem title;
    title.id = id++;
    title.kind = UiItemKind::Text;
    title.rect = {textLeft, center - textHeight / 2, textLeft + textWidth,
                  center - textHeight / 2 + titleHeight};
    title.runs.push_back({spec.title});
    title.enabled = spec.enabled;
    items.push_back(std::move(title));
    float lineTop = center - textHeight / 2 + titleHeight;
    if (!spec.description.empty()) {
        UiItem description;
        description.id = id++;
        description.kind = UiItemKind::Text;
        description.font = UiFont::Caption;
        description.ink = UiInk::Muted;
        description.rect = {textLeft, lineTop, textLeft + descriptionWidth,
                            lineTop + descriptionHeight};
        description.runs = spec.description;
        items.push_back(std::move(description));
        lineTop += descriptionHeight;
    }
    for (const Note& note : notes) {
        UiItem glyph;
        glyph.id = id++;
        glyph.kind = UiItemKind::Glyph;
        glyph.text = note.glyph;
        glyph.iconSize = 12;
        glyph.ink = note.ink;
        glyph.rect = {textLeft, lineTop + 3 + 2, textLeft + 12, lineTop + 3 + 14};
        items.push_back(std::move(glyph));
        UiItem text;
        text.id = id++;
        text.kind = UiItemKind::Text;
        text.font = UiFont::Caption;
        text.ink = note.ink;
        text.rect = {textLeft + 18, lineTop + 3, textLeft + descriptionWidth, lineTop + note.height};
        text.runs.push_back({*note.text});
        items.push_back(std::move(text));
        lineTop += note.height;
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

// Info bar (mockup .infobar; severities and the button: history mockup .ibar): a severity icon
// and wrapped rich text on a card, tinted for caution and error, with an optional trailing
// button (28 DIP, 10 DIP from the bar's edges; the text centered against it). The text item
// takes `textId`, the button its own id; both stay the same across rebuilds so focus stays.
enum class UiSeverity : uint8_t { Info, Caution, Error };

struct UiInfoButton {
    uint32_t id = 0;
    std::wstring text;
    UiAction action;
    bool enabled = true;
};

static float UiAddInfoBar(UiGraphics& g, std::vector<UiItem>& items, uint32_t& id, uint32_t textId,
                          float x, float y, float width, std::vector<UiRun> runs,
                          UiSeverity severity = UiSeverity::Info,
                          const std::optional<UiInfoButton>& button = std::nullopt) {
    constexpr float kInset = 1 + 13, kPadX = 1 + 18, kIcon = 16, kGap = 12;
    constexpr float kButton = 28, kButtonInset = 1 + 9, kButtonRight = 1 + 12;
    UiTextStyle note = UiTextStyleFor(UiFont::Note);
    float buttonWidth = button ? std::ceil(UiLayoutSize(UiLayout(g, button->text, note, 1000, false)).width) + 24 : 0;
    float right = button ? x + width - kButtonRight - buttonWidth - kGap : x + width - kPadX;
    float textLeft = x + kPadX + kIcon + kGap;
    float textWidth = std::max(right - textLeft, 40.0f);
    float textHeight = UiLayoutSize(UiRichLayout(g, runs, UiFont::Note, textWidth)).height;
    float height = std::max(textHeight + 2 * kInset, button ? kButton + 2 * kButtonInset : 0.0f);
    float textTop = y + (height - textHeight) / 2;
    UiItem card;
    card.id = id++;
    card.kind = UiItemKind::Card;
    card.rect = {x, y, x + width, y + height};
    card.ink = severity == UiSeverity::Caution ? UiInk::Caution :
               severity == UiSeverity::Error   ? UiInk::Danger : UiInk::Primary;
    items.push_back(card);
    // Info: InfoSolid in the accent; caution and error (WinUI InfoBar): a filled circle in the
    // severity's color under its mark.
    D2D1_RECT_F iconRect{x + kPadX, textTop + 1.5f, x + kPadX + kIcon, textTop + 1.5f + kIcon};
    auto glyph = [&](PCWSTR text, UiInk ink) {
        UiItem icon;
        icon.id = id++;
        icon.kind = UiItemKind::Glyph;
        icon.text = text;
        icon.ink = ink;
        icon.rect = iconRect;
        items.push_back(std::move(icon));
    };
    if (severity == UiSeverity::Info) {
        glyph(L"\uF167", UiInk::Accent);
    } else {
        glyph(L"\uF136", card.ink);
        glyph(severity == UiSeverity::Caution ? L"\uF13C" : L"\uF13D", UiInk::OnAccent);
    }
    UiItem text;
    text.id = textId;
    text.kind = UiItemKind::Text;
    text.font = UiFont::Note;
    text.ink = UiInk::Secondary;
    text.rect = {textLeft, textTop, textLeft + textWidth, textTop + textHeight};
    text.runs = std::move(runs);
    items.push_back(std::move(text));
    if (button) {
        UiItem action;
        action.id = button->id;
        action.kind = UiItemKind::Button;
        action.font = UiFont::Note;
        action.text = button->text;
        float center = y + height / 2, buttonRight = x + width - kButtonRight;
        action.rect = {buttonRight - buttonWidth, center - kButton / 2, buttonRight, center + kButton / 2};
        action.enabled = button->enabled;
        action.action = button->action;
        items.push_back(std::move(action));
    }
    return card.rect.bottom;
}

// Controls placed in a card's trailing slot (mockup .ctl). Interactive controls take explicit
// ids that stay the same across rebuilds: focus and native text boxes follow them.

// Toggle switch (mockup .tglwrap): "On"/"Off", 12 DIP gap, a 40 x 20 DIP switch. With a label
// (history mockup .cmpt): the switch first, 8 DIP gap, the 13 DIP label; the label toggles too.
static D2D1_SIZE_F UiToggleSize(UiGraphics& g, std::wstring_view label = {}) {
    if (!label.empty()) {
        return {40 + 8 + std::ceil(UiLayoutSize(UiLayout(g, label, UiTextStyleFor(UiFont::Note), 1000, false)).width),
                32};
    }
    UiTextStyle body = UiTextStyleFor(UiFont::Body);
    float state = std::max(UiLayoutSize(UiLayout(g, L"On", body, 1000, false)).width,
                           UiLayoutSize(UiLayout(g, L"Off", body, 1000, false)).width);
    return {std::ceil(state) + 12 + 40, 32};
}

static void UiAddToggle(std::vector<UiItem>& items, uint32_t id, D2D1_RECT_F slot, bool on,
                        bool enabled, UiAction action, std::wstring label = {}) {
    UiItem toggle;
    toggle.id = id;
    toggle.kind = UiItemKind::Toggle;
    toggle.rect = slot;
    if (label.empty()) {
        toggle.text = on ? L"On" : L"Off";
    } else {
        toggle.text = std::move(label);
        toggle.align = DWRITE_TEXT_ALIGNMENT_TRAILING;
    }
    toggle.selected = on;
    toggle.enabled = enabled;
    toggle.action = std::move(action);
    items.push_back(std::move(toggle));
}

// Segmented control (history mockup .segctl): 32 DIP tall, as wide as its segments.
[[maybe_unused]] static D2D1_SIZE_F UiSegmentedSize(UiGraphics& g, const std::vector<UiOption>& options) {
    UiItem control;
    control.options = options;
    std::vector<D2D1_RECT_F> segments = UiSegmentRects(g, control);
    return {(segments.empty() ? 3 : segments.back().right) + 3, 32};
}

// The selected option is the one whose value is action.value.
[[maybe_unused]] static void UiAddSegmented(std::vector<UiItem>& items, uint32_t id, D2D1_RECT_F slot,
                           std::vector<UiOption> options, bool enabled, UiAction action) {
    UiItem control;
    control.id = id;
    control.kind = UiItemKind::Segmented;
    control.rect = slot;
    control.options = std::move(options);
    control.enabled = enabled;
    control.action = std::move(action);
    items.push_back(std::move(control));
}

// Status pill (history mockup .pill): 20 DIP tall, 9 DIP padding, a 6 DIP dot and 4 DIP gap
// before caption text.
[[maybe_unused]] static D2D1_SIZE_F UiPillSize(UiGraphics& g, std::wstring_view text) {
    float width = UiLayoutSize(UiLayout(g, text, UiTextStyleFor(UiFont::Caption), 1000, false)).width;
    return {9 + 6 + 4 + std::ceil(width) + 9, 20};
}

// ink: Success, Caution or Danger for a state, Secondary for off; on: a filled dot.
[[maybe_unused]] static void UiAddPill(std::vector<UiItem>& items, uint32_t id, D2D1_RECT_F slot, std::wstring text,
                      UiInk ink, bool on) {
    UiItem pill;
    pill.id = id;
    pill.kind = UiItemKind::Pill;
    pill.rect = slot;
    pill.font = UiFont::Caption;
    pill.text = std::move(text);
    pill.ink = ink;
    pill.selected = on;
    items.push_back(std::move(pill));
}

// Dropdown field (mockup .combo): the option whose value is action.value.
static void UiAddDropdown(std::vector<UiItem>& items, uint32_t id, D2D1_RECT_F slot,
                          std::vector<UiOption> options, bool enabled, UiAction action) {
    UiItem field;
    field.id = id;
    field.kind = UiItemKind::Dropdown;
    field.rect = slot;
    for (const UiOption& option : options) {
        if (option.value == action.value) field.text = option.text;
    }
    field.options = std::move(options);
    field.enabled = enabled;
    field.action = std::move(action);
    items.push_back(std::move(field));
}

// Spin number box (mockup .numwrap): a 96 x 32 DIP box with up/down buttons at its right end,
// then a unit 10 DIP after it.
static constexpr float kUiNumberBoxWidth = 96;

static D2D1_SIZE_F UiNumberBoxSize(UiGraphics& g, std::wstring_view unit) {
    float label = UiLayoutSize(UiLayout(g, unit, UiTextStyleFor(UiFont::Body), 1000, false)).width;
    return {kUiNumberBoxWidth + 10 + std::max(std::ceil(label), 22.0f), 32};
}

// Where a box's text sits: after the 11 DIP padding, before a number box's spin buttons; a
// centered box (no spin buttons) keeps 6 DIP on each side, a text box 11 DIP.
static D2D1_RECT_F UiNumberBoxTextRect(const UiItem& box) {
    const D2D1_RECT_F& r = box.rect;
    if (box.kind == UiItemKind::TextBox) return {r.left + 11, r.top + 1, r.right - 11, r.bottom - 1};
    if (box.align == DWRITE_TEXT_ALIGNMENT_CENTER) return {r.left + 6, r.top + 1, r.right - 6, r.bottom - 1};
    return {r.left + 11, r.top + 1, r.right - 49, r.bottom - 1};
}

// The box takes `id`, its up and down buttons id + 1 and id + 2. The action names the target;
// the buttons step it by 1.
static void UiAddNumberBox(std::vector<UiItem>& items, uint32_t id, D2D1_RECT_F slot,
                           std::wstring text, std::wstring unit, bool enabled, UiAction action) {
    UiItem box;
    box.id = id;
    box.kind = UiItemKind::NumberBox;
    box.rect = {slot.left, slot.top, slot.left + kUiNumberBoxWidth, slot.bottom};
    box.text = std::move(text);
    box.enabled = enabled;
    box.action = action;
    const D2D1_RECT_F r = box.rect;
    items.push_back(std::move(box));
    for (int i = 0; i < 2; i++) {
        UiItem spin;
        spin.id = id + 1 + uint32_t(i);
        spin.kind = UiItemKind::SpinButton;
        float right = r.right - 3 - 22 * float(1 - i);
        spin.rect = {right - 22, r.top + 4, right, r.bottom - 4};
        spin.text = i == 0 ? L"\uE70E" : L"\uE70D";
        spin.owner = id;
        spin.enabled = enabled;
        spin.action = action;
        spin.action.kind = UiActionKind::Step;
        spin.action.value = i == 0 ? 1 : -1;
        items.push_back(std::move(spin));
    }
    UiItem label;
    label.id = id + 3;
    label.kind = UiItemKind::Text;
    label.ink = UiInk::Secondary;
    label.rect = {r.right + 10, (r.top + r.bottom) / 2 - 10, slot.right, (r.top + r.bottom) / 2 + 10};
    label.runs.push_back({std::move(unit)});
    label.enabled = enabled;
    items.push_back(std::move(label));
}

// Slider (mockup .sld): a track across the slot, the thumb centered on the value (overhanging
// the ends by its radius). The action names the target; drags report their value.
static void UiAddSlider(std::vector<UiItem>& items, uint32_t id, D2D1_RECT_F slot, int value,
                        int minimum, int maximum, bool enabled, UiAction action) {
    UiItem slider;
    slider.id = id;
    slider.kind = UiItemKind::Slider;
    slider.rect = slot;
    slider.minimum = minimum;
    slider.maximum = std::max(maximum, minimum);
    slider.enabled = enabled;
    slider.action = std::move(action);
    slider.action.value = std::clamp(value, slider.minimum, slider.maximum);
    items.push_back(std::move(slider));
}

// The thumb's center for the slider's value.
static float UiSliderThumbX(const UiItem& slider) {
    int range = slider.maximum - slider.minimum;
    float t = range > 0 ? float(slider.action.value - slider.minimum) / float(range) : 0;
    return slider.rect.left + std::clamp(t, 0.0f, 1.0f) * (slider.rect.right - slider.rect.left);
}

// A gallery card's thumbnail (mockup .gcard .thumb): 72 DIP tall inside its 1 DIP border and
// 8 DIP padding; the name sits 8 DIP below it.
static D2D1_RECT_F UiGalleryThumbRect(const UiItem& card) {
    return {card.rect.left + 9, card.rect.top + 9, card.rect.right - 9, card.rect.top + 9 + 72};
}

// The popup's rows. A dropdown's (WinUI combo box items): 32 DIP rows on a 36 DIP pitch, 5 DIP
// side insets, 2 DIP top and bottom padding; the current value's row selected. A menu's, when
// its options have details (mockup .dmenu .dmi): 53 DIP rows inside 4 DIP padding, an 18 DIP
// icon, the title and its detail line; at least 310 DIP wide. A one-line menu's, when its
// options have glyphs (history mockup .pmenu.small .pmi): dropdown rows with a 16 DIP glyph 12
// DIP in and the label 12 DIP after it, 28 DIP after the widest label; a separator adds 5 DIP
// above its row (a line 4 DIP under the previous row). Each row's action is the field's, with the
// row's value. Returns the popup size in DIPs, at least `minWidth` wide.
static D2D1_SIZE_F UiLayoutOptions(UiGraphics& g, const std::vector<UiOption>& options,
                                   const UiAction& action, float minWidth,
                                   std::vector<UiItem>& rows) {
    bool menu = std::any_of(options.begin(), options.end(),
                            [](const UiOption& option) { return !option.detail.empty(); });
    bool glyphs = !menu && std::any_of(options.begin(), options.end(),
                                       [](const UiOption& option) { return !option.icon.empty(); });
    UiTextStyle body = UiTextStyleFor(UiFont::Body), caption = UiTextStyleFor(UiFont::Caption);
    float width = menu ? std::max(minWidth, 310.0f) : minWidth;
    for (const UiOption& option : options) {
        float text = UiLayoutSize(UiLayout(g, option.text, body, 10000, false)).width;
        if (menu) {
            text = std::max(text, UiLayoutSize(UiLayout(g, option.detail, caption, 10000, false)).width);
            width = std::max(width, std::ceil(text) + 4 + 12 + 18 + 14 + 12 + 4);
        } else {
            width = std::max(width, std::ceil(text) + 5 + 12 + (glyphs ? 16 + 12 + 28 : 12) + 5);
        }
    }
    float pitch = menu ? 53 : 36, inset = menu ? 4 : 5, row = menu ? 53 : 32, y = 4;
    rows.clear();
    for (size_t i = 0; i < options.size(); i++) {
        UiItem item;
        item.id = uint32_t(i + 1);
        item.kind = UiItemKind::Option;
        item.separator = glyphs && i > 0 && options[i].separator;
        if (item.separator) y += 5;
        item.rect = {inset, y, width - inset, y + row};
        item.text = options[i].text;
        if (menu) {
            item.runs.push_back({options[i].detail});
            item.parts = options[i].parts;
        } else if (glyphs) {
            item.icon = options[i].icon;
        }
        item.selected = options[i].value == action.value;
        item.action = action;
        item.action.value = options[i].value;
        rows.push_back(std::move(item));
        y += pitch;
    }
    return {width, y + (menu ? 4 : 0)};
}

// Where the popup goes, in pixels: below the field, else above it, else on the side with more
// room; aligned with the field's left (a menu's: right) edge and kept inside the work area.
static POINT UiPopupPlacement(RECT field, SIZE size, RECT work, int gap, bool alignRight = false) {
    LONG x = std::clamp(alignRight ? field.right - size.cx : field.left, work.left,
                        std::max(work.left, work.right - size.cx));
    LONG below = field.bottom + gap, above = field.top - gap - size.cy;
    if (below + size.cy <= work.bottom) return {x, below};
    if (above >= work.top) return {x, above};
    bool roomBelow = work.bottom - field.bottom >= field.top - work.top;
    return {x, roomBelow ? std::max(work.top, work.bottom - size.cy) : work.top};
}
