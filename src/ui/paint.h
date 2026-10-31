// Per-kind paint of item lists, the focus ring and the scrollbar. Pure: explicit target, caches,
// palette and interaction state. High contrast draws only palette (system) colors and borders on
// every control.

// open: the dropdown whose popup is showing; editing: the number box whose native text box has
// keyboard focus; pointer: where the pointer is over this surface, in item coordinates (far
// away when it isn't), for client-drawn hover.
struct UiPaintState {
    UiFocusRef hot, pressed, focus;
    bool focusVisible = false;
    uint32_t open = 0, editing = 0;
    D2D1_POINT_2F pointer{-1e9f, -1e9f};
};

// Draws a client-drawn item (a Preview, a Gallery card's thumbnail) with the target's transform
// in item coordinates.
using UiCustomPaint = std::function<void(const UiItem&)>;

static D2D1_COLOR_F UiFade(D2D1_COLOR_F color, float opacity) {
    color.a *= opacity;
    return color;
}

// Paints `items` with their DIP coordinates shifted by `offset` (pixel-aligned by the caller),
// skipping items entirely outside the vertical range [top, bottom) of item coordinates.
static void UiPaintItems(ID2D1RenderTarget* target, UiGraphics& g, UiTargetCache& cache,
                         const UiPalette& p, const std::vector<UiItem>& items,
                         const UiPaintState& state, D2D1_POINT_2F offset, float scale, float top,
                         float bottom, const UiCustomPaint* custom = nullptr) {
    D2D1_MATRIX_3X2_F base{};
    target->GetTransform(&base);
    D2D1::Matrix3x2F shifted = D2D1::Matrix3x2F::Translation(offset.x, offset.y) *
                               *D2D1::Matrix3x2F::ReinterpretBaseType(&base);
    target->SetTransform(&shifted);
    const bool hc = p.kind == UiThemeKind::HighContrast;

    for (const UiItem& item : items) {
        if (item.rect.bottom < top || item.rect.top >= bottom) continue;
        const D2D1_RECT_F& r = item.rect;
        const float width = r.right - r.left, centerY = (r.top + r.bottom) / 2;
        const bool hot = state.hot == UiFocusRef{item.id, -1};
        const bool pressed = hot && state.pressed == UiFocusRef{item.id, -1};
        switch (item.kind) {
            // Navigation item and popup option: accent pill when selected, glyph, label.
            case UiItemKind::Nav:
            case UiItemKind::Option: {
                D2D1_COLOR_F fill{};
                if (hc) {
                    if (item.selected) fill = p.highlight;
                } else if (item.selected) {
                    fill = hot ? p.subtlePressed : p.subtleHover;
                } else if (pressed || hot) {
                    fill = pressed ? p.subtlePressed : p.subtleHover;
                }
                UiFillRect(target, cache, r, 4, fill, scale);
                if (hc && hot && !item.selected) UiStrokeRect(target, cache, r, 4, p.highlight, 1, scale);
                if (item.selected) {
                    UiFillRect(target, cache, {r.left, centerY - 8, r.left + 3, centerY + 8}, 1.5f,
                               hc ? p.highlightText : p.accent, scale);
                }
                D2D1_COLOR_F ink = !item.enabled ? p.textDisabled :
                                   hc && item.selected ? p.highlightText : p.text;
                float labelLeft = 12;
                if (!item.icon.empty()) {
                    UiDrawLayout(target, cache,
                                 UiLayout(g, item.icon, UiIconStyle(item.iconSize), 16, false,
                                          DWRITE_TEXT_ALIGNMENT_CENTER),
                                 {r.left + 14, centerY - item.iconSize / 2}, ink);
                    labelLeft = 44;
                }
                UiTextStyle label = UiTextStyleFor(UiFont::Body);
                UiDrawLayout(target, cache, UiLayout(g, item.text, label, width - labelLeft, false),
                             {r.left + labelLeft, centerY - label.lineHeight / 2}, ink);
                break;
            }
            // Card: a raised panel; interactive cards react to the pointer as a whole.
            case UiItemKind::Card: {
                bool interactive = UiFocusable(item);
                D2D1_COLOR_F fill = interactive && pressed ? p.cardPressed :
                                    interactive && hot     ? p.cardHover : p.card;
                UiFillRect(target, cache, r, 6, fill, scale);
                bool hcHot = hc && interactive && hot;
                UiStrokeRect(target, cache, r, 6, hcHot ? p.highlight : p.cardBorder,
                             hcHot ? 2.0f : 1.0f, scale);
                break;
            }
            // Rich text wrapped to the item width, inline links colored by pointer state.
            case UiItemKind::Text: {
                int hotLink = state.hot.id == item.id ? state.hot.link : -1;
                int pressedLink =
                    state.pressed.id == item.id && state.pressed == state.hot ? state.pressed.link : -1;
                UiInk ink = item.enabled ? item.ink : UiInk::Disabled;
                IDWriteTextLayout* layout =
                    UiInkedLayout(target, cache, g, item.runs, item.font, width, item.align, p, ink,
                                  hotLink, pressedLink);
                UiDrawLayout(target, cache, layout, {r.left, r.top},
                             UiFade(UiInkColor(p, ink), item.opacity));
                break;
            }
            case UiItemKind::Glyph:
                UiDrawLayout(target, cache,
                             UiLayout(g, item.text, UiIconStyle(item.iconSize), width, false,
                                      DWRITE_TEXT_ALIGNMENT_CENTER),
                             {r.left, centerY - item.iconSize / 2},
                             UiFade(UiInkColor(p, item.enabled ? item.ink : UiInk::Disabled),
                                    item.opacity));
                break;
            // Path parts mapped from the view box onto the rectangle.
            case UiItemKind::Shape: {
                float s = width / item.viewBox;
                D2D1::Matrix3x2F local = D2D1::Matrix3x2F::Scale(s, s) *
                                         D2D1::Matrix3x2F::Translation(r.left, r.top) * shifted;
                target->SetTransform(&local);
                for (const UiShapePart& part : item.parts) {
                    D2D1_COLOR_F color =
                        part.ink == UiInk::Inherit ? part.color :
                                                     UiFade(UiInkColor(p, part.ink), part.alpha);
                    color = UiFade(color, item.opacity);
                    ID2D1PathGeometry* geometry = UiPathGeometry(g, part.path);
                    if (part.stroke > 0) {
                        target->DrawGeometry(geometry, UiBrush(target, cache, color), part.stroke,
                                             g.roundCaps.get());
                    } else {
                        target->FillGeometry(geometry, UiBrush(target, cache, color), nullptr);
                    }
                }
                target->SetTransform(&shifted);
                break;
            }
            // Button: standard, accent, subtle or danger; optional glyph before the label.
            case UiItemKind::Button: {
                D2D1_COLOR_F fill{}, border{}, ink{};
                bool active = item.enabled && (hot || pressed);
                switch (item.button) {
                    case UiButtonStyle::Accent:
                        fill = !item.enabled ? p.accentDisabled : pressed ? p.accentPressed :
                               hot ? p.accentHover : p.accent;
                        border = hc ? p.controlBorder : D2D1_COLOR_F{};
                        ink = item.enabled ? p.textOnAccent : p.textDisabled;
                        break;
                    case UiButtonStyle::Subtle:
                        fill = !item.enabled ? D2D1_COLOR_F{} : pressed ? p.subtlePressed :
                               hot ? p.subtleHover : D2D1_COLOR_F{};
                        border = hc && active ? p.highlight : D2D1_COLOR_F{};
                        ink = item.enabled ? p.text : p.textDisabled;
                        break;
                    default:
                        fill = !item.enabled ? p.controlDisabled : pressed ? p.controlPressed :
                               hot ? p.controlHover : p.control;
                        border = p.controlBorder;
                        ink = !item.enabled                          ? p.textDisabled :
                              hc && active                           ? p.highlightText :
                              item.button == UiButtonStyle::Danger   ? p.danger :
                              pressed                                ? p.textSecondary : p.text;
                        break;
                }
                UiFillRect(target, cache, r, 4, fill, scale);
                UiStrokeRect(target, cache, r, 4, border, 1, scale);
                UiTextStyle label = UiTextStyleFor(UiFont::Body);
                IDWriteTextLayout* text = UiLayout(g, item.text, label, width, false);
                float textWidth = UiLayoutSize(text).width;
                float iconWidth = item.icon.empty() ? 0 : item.iconSize + 8;
                float x = r.left + (width - textWidth - iconWidth) / 2;
                if (!item.icon.empty()) {
                    UiDrawLayout(target, cache,
                                 UiLayout(g, item.icon, UiIconStyle(item.iconSize), item.iconSize,
                                          false, DWRITE_TEXT_ALIGNMENT_CENTER),
                                 {x, centerY - item.iconSize / 2}, ink);
                }
                UiDrawLayout(target, cache, text, {x + iconWidth, centerY - label.lineHeight / 2}, ink);
                break;
            }
            // Hyperlink button: accent text; a subtle fill under the pointer.
            case UiItemKind::Link: {
                if (!hc && item.enabled) {
                    UiFillRect(target, cache, r, 4,
                               pressed ? p.subtlePressed : hot ? p.subtleHover : D2D1_COLOR_F{}, scale);
                }
                if (hc && item.enabled && hot) UiStrokeRect(target, cache, r, 4, p.highlight, 1, scale);
                UiTextStyle label = UiTextStyleFor(item.font);
                UiDrawLayout(target, cache,
                             UiLayout(g, item.text, label, width, false, DWRITE_TEXT_ALIGNMENT_CENTER),
                             {r.left, centerY - label.lineHeight / 2},
                             !item.enabled ? p.textDisabled : pressed ? p.linkPressed :
                             hot ? p.linkHover : p.link);
                break;
            }
            // Toggle switch at the right end, "On"/"Off" before it. On: accent track, knob in
            // the text-on-accent color; off: strong outline. The knob grows under the pointer.
            case UiItemKind::Toggle: {
                bool on = item.selected, active = item.enabled && hot;
                D2D1_RECT_F track{r.right - 40, centerY - 10, r.right, centerY + 10};
                D2D1_COLOR_F fill{}, border{}, knob{};
                if (!item.enabled) {
                    fill = on ? p.accentDisabled : D2D1_COLOR_F{};
                    border = on ? D2D1_COLOR_F{} : p.textDisabled;
                    knob = on ? p.background : p.textDisabled;
                } else if (on) {
                    fill = pressed ? p.accentPressed : hot ? p.accentHover : p.accent;
                    border = hc ? p.controlBorder : D2D1_COLOR_F{};
                    knob = p.textOnAccent;
                } else {
                    fill = pressed ? p.subtlePressed : hot ? p.subtleHover : D2D1_COLOR_F{};
                    border = hc && active ? p.highlight : p.controlStrong;
                    knob = p.textSecondary;
                }
                UiFillRect(target, cache, track, 10, fill, scale);
                UiStrokeRect(target, cache, track, 10, border, 1, scale);
                float size = on ? (active ? 14.0f : 12.0f) : (active ? 12.0f : 10.0f);
                float knobX = on ? track.right - 10 : track.left + 10;
                UiFillRect(target, cache,
                           {knobX - size / 2, centerY - size / 2, knobX + size / 2, centerY + size / 2},
                           size / 2, knob, scale);
                UiTextStyle label = UiTextStyleFor(UiFont::Body);
                UiDrawLayout(target, cache,
                             UiLayout(g, item.text, label, std::max(track.left - 12 - r.left, 0.0f),
                                      false, DWRITE_TEXT_ALIGNMENT_TRAILING),
                             {r.left, centerY - label.lineHeight / 2},
                             item.enabled ? p.text : p.textDisabled);
                break;
            }
            // Dropdown field: a standard control fill (pressed while its popup shows), the
            // chosen label clipped before the chevron.
            case UiItemKind::Dropdown: {
                bool down = item.enabled && (pressed || state.open == item.id);
                bool active = item.enabled && (hot || down);
                D2D1_COLOR_F fill = !item.enabled ? p.controlDisabled : down ? p.controlPressed :
                                    hot ? p.controlHover : p.control;
                D2D1_COLOR_F ink = !item.enabled ? p.textDisabled : hc && active ? p.highlightText : p.text;
                UiFillRect(target, cache, r, 4, fill, scale);
                UiStrokeRect(target, cache, r, 4, p.controlBorder, 1, scale);
                UiTextStyle label = UiTextStyleFor(UiFont::Body);
                D2D1_RECT_F clip{r.left + 11, r.top, r.right - 30, r.bottom};
                target->PushAxisAlignedClip(&clip, D2D1_ANTIALIAS_MODE_ALIASED);
                UiDrawLayout(target, cache, UiLayout(g, item.text, label, 10000, false),
                             {clip.left, centerY - label.lineHeight / 2}, ink);
                target->PopAxisAlignedClip();
                UiDrawLayout(target, cache,
                             UiLayout(g, L"\uE70D", UiIconStyle(10), 12, false,
                                      DWRITE_TEXT_ALIGNMENT_CENTER),
                             {r.right - 22, centerY - 5},
                             !item.enabled ? p.textDisabled : hc && active ? p.highlightText :
                                                              p.textSecondary);
                break;
            }
            // Number box frame. Its native text box draws the text on screen; the text drawn
            // here sits under it (and shows in offscreen renders). Editing: solid fill and a
            // 2 DIP accent bottom edge.
            case UiItemKind::NumberBox: {
                bool editing = item.enabled && state.editing == item.id;
                D2D1_COLOR_F fill = !item.enabled ? p.controlDisabled : editing ? p.inputActive :
                                                                         p.control;
                UiFillRect(target, cache, r, 4, fill, scale);
                UiStrokeRect(target, cache, r, 4, hc && editing ? p.highlight : p.controlBorder, 1,
                             scale);
                if (editing) {
                    UiFillRect(target, cache, {r.left + 2, r.bottom - 2, r.right - 2, r.bottom}, 1,
                               p.accent, scale);
                }
                D2D1_RECT_F clip = UiNumberBoxTextRect(item);
                UiTextStyle label = UiTextStyleFor(UiFont::Body);
                target->PushAxisAlignedClip(&clip, D2D1_ANTIALIAS_MODE_ALIASED);
                UiDrawLayout(target, cache,
                             UiLayout(g, item.text, label, clip.right - clip.left, false, item.align),
                             {clip.left, centerY - label.lineHeight / 2},
                             item.enabled ? p.text : p.textDisabled);
                target->PopAxisAlignedClip();
                break;
            }
            // Spin button: a subtle button with a small chevron.
            case UiItemKind::SpinButton: {
                if (item.enabled && !hc) {
                    UiFillRect(target, cache, r, 3,
                               pressed ? p.subtlePressed : hot ? p.subtleHover : D2D1_COLOR_F{},
                               scale);
                }
                if (hc && item.enabled && hot) UiStrokeRect(target, cache, r, 3, p.highlight, 1, scale);
                UiDrawLayout(target, cache,
                             UiLayout(g, item.text, UiIconStyle(10), width, false,
                                      DWRITE_TEXT_ALIGNMENT_CENTER),
                             {r.left, centerY - 5}, item.enabled ? p.textSecondary : p.textDisabled);
                break;
            }
            // Slider (WinUI): a 4 DIP track with the accent fill up to the value; a 20 DIP thumb
            // whose accent core grows under the pointer and shrinks while dragged.
            case UiItemKind::Slider: {
                float thumb = UiSliderThumbX(item);
                bool dragging = item.enabled && state.pressed == UiFocusRef{item.id, -1};
                D2D1_COLOR_F accent = !item.enabled ? p.accentDisabled : hc ? p.highlight : p.accent;
                UiFillRect(target, cache, {r.left, centerY - 2, r.right, centerY + 2}, 2,
                           item.enabled ? p.controlStrong : p.textDisabled, scale);
                UiFillRect(target, cache, {r.left, centerY - 2, thumb, centerY + 2}, 2, accent, scale);
                D2D1_RECT_F knob{thumb - 10, centerY - 10, thumb + 10, centerY + 10};
                UiFillRect(target, cache, knob, 10, p.controlSolid, scale);
                UiStrokeRect(target, cache, knob, 10, hc && (hot || dragging) ? p.highlight : p.controlBorder,
                             1, scale);
                float core = dragging ? 4.0f : item.enabled && hot ? 7.0f : 5.0f;
                UiFillRect(target, cache, {thumb - core, centerY - core, thumb + core, centerY + core},
                           core, accent, scale);
                break;
            }
            // Swatch: a color circle with a thin edge; the current color gets an accent ring
            // 2 DIP out, a hovered one a thin ring there.
            case UiItemKind::Swatch: {
                COLORREF rgb = COLORREF(item.action.value);
                D2D1_COLOR_F color{float(GetRValue(rgb)) / 255, float(GetGValue(rgb)) / 255,
                                   float(GetBValue(rgb)) / 255, item.enabled ? 1.0f : 0.4f};
                float radius = width / 2;
                UiFillRect(target, cache, r, radius, color, scale);
                UiStrokeRect(target, cache, r, radius, hc ? p.text : UiFade(p.text, 0.28f), 1, scale);
                if (item.selected) {
                    UiStrokeRect(target, cache, UiInflate(r, 4, 4), radius + 4, hc ? p.highlight : p.accent,
                                 std::max(1.0f, std::round(2 * scale)), scale);
                } else if (item.enabled && hot) {
                    UiStrokeRect(target, cache, UiInflate(r, 4, 4), radius + 4,
                                 hc ? p.highlight : p.textSecondary, 1, scale);
                }
                break;
            }
            // Gallery card: reacts to the pointer as a whole; the chosen one has a 2 DIP accent
            // border. The client draws the thumbnail; the name sits under it.
            case UiItemKind::Gallery: {
                bool active = UiActive(item);
                D2D1_COLOR_F fill = active && pressed ? p.cardPressed : active && hot ? p.cardHover : p.card;
                UiFillRect(target, cache, r, 6, fill, scale);
                if (item.selected) {
                    UiStrokeRect(target, cache, r, 6, hc ? p.highlight : p.accent,
                                 std::max(1.0f, std::round(2 * scale)), scale);
                } else {
                    UiStrokeRect(target, cache, r, 6, hc && hot ? p.highlight : p.cardBorder, 1, scale);
                }
                D2D1_RECT_F thumb = UiGalleryThumbRect(item);
                if (custom && *custom) {
                    (*custom)(item);
                    target->SetTransform(&shifted);
                } else {
                    UiFillRect(target, cache, thumb, 4, p.control, scale);
                }
                UiDrawLayout(target, cache,
                             UiLayout(g, item.text, UiTextStyleFor(UiFont::Note),
                                      std::max(thumb.right - thumb.left - 4, 0.0f), false),
                             {thumb.left + 2, thumb.bottom + 8}, item.enabled ? p.text : p.textDisabled);
                break;
            }
            case UiItemKind::Preview:
                if (custom && *custom) {
                    (*custom)(item);
                    target->SetTransform(&shifted);
                } else {
                    UiFillRect(target, cache, r, 4, p.control, scale);
                }
                break;
        }
    }

    // Focus ring (keyboard focus only): 2 DIP outer and 1 DIP inner band, 3 DIP outside the
    // element; 2 DIP around inline links, between their neighbors; inside for navigation items,
    // which touch the pane edges; outside a swatch's selection ring. A number box shows focus
    // with its editing style instead.
    if (state.focusVisible && state.focus.id) {
        auto focused = std::find_if(items.begin(), items.end(),
                                    [&](const UiItem& item) { return item.id == state.focus.id; });
        if (focused != items.end() && focused->kind != UiItemKind::NumberBox) {
            float radius = state.focus.link >= 0                ? 2 :
                           focused->kind == UiItemKind::Swatch  ? (focused->rect.right - focused->rect.left) / 2 :
                           focused->kind == UiItemKind::Card ||
                               focused->kind == UiItemKind::Gallery ? 6 : 4;
            float outside = state.focus.link >= 0             ? 2 :
                            focused->kind == UiItemKind::Nav    ? 0 :
                            focused->kind == UiItemKind::Swatch ? 6 : 3;
            for (D2D1_RECT_F rect : UiFocusBounds(g, *focused, state.focus.link)) {
                // A slider's ring takes in its thumb at either end.
                if (focused->kind == UiItemKind::Slider) rect = UiInflate(rect, 10, -2);
                UiStrokeRect(target, cache, UiInflate(rect, outside, outside), radius + outside,
                             p.focusOuter, std::max(1.0f, std::round(2 * scale)), scale);
                UiStrokeRect(target, cache, UiInflate(rect, outside - 2, outside - 2),
                             radius + outside - 2, p.focusInner, std::max(1.0f, std::round(scale)),
                             scale);
            }
        }
    }
    target->SetTransform(&base);
}

static void UiPaintScrollbar(ID2D1RenderTarget* target, UiTargetCache& cache, const UiPalette& p,
                             const UiScrollbar& bar, bool expanded, float scale) {
    if (!bar.visible) return;
    if (expanded) UiFillRect(target, cache, bar.track, 6, p.scrollTrack, scale);
    UiFillRect(target, cache, bar.thumb, (bar.thumb.right - bar.thumb.left) / 2, p.scrollThumb, scale);
}
