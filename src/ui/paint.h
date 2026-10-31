// Per-kind paint of item lists, the focus ring and the scrollbar. Pure: explicit target, caches,
// palette and interaction state. High contrast draws only palette (system) colors and borders on
// every control.

struct UiPaintState {
    UiFocusRef hot, pressed, focus;
    bool focusVisible = false;
};

static D2D1_COLOR_F UiFade(D2D1_COLOR_F color, float opacity) {
    color.a *= opacity;
    return color;
}

// Paints `items` with their DIP coordinates shifted by `offset` (pixel-aligned by the caller),
// skipping items entirely outside the vertical range [top, bottom) of item coordinates.
static void UiPaintItems(ID2D1RenderTarget* target, UiGraphics& g, UiTargetCache& cache,
                         const UiPalette& p, const std::vector<UiItem>& items,
                         const UiPaintState& state, D2D1_POINT_2F offset, float scale, float top,
                         float bottom) {
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
            // Navigation item: accent pill when selected, glyph, label.
            case UiItemKind::Nav: {
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
                UiDrawLayout(target, cache,
                             UiLayout(g, item.icon, UiIconStyle(item.iconSize), 16, false,
                                      DWRITE_TEXT_ALIGNMENT_CENTER),
                             {r.left + 14, centerY - item.iconSize / 2}, ink);
                UiTextStyle label = UiTextStyleFor(UiFont::Body);
                UiDrawLayout(target, cache, UiLayout(g, item.text, label, width - 44, false),
                             {r.left + 44, centerY - label.lineHeight / 2}, ink);
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
        }
    }

    // Focus ring (keyboard focus only): 2 DIP outer and 1 DIP inner band, 3 DIP outside the
    // element; 2 DIP around inline links, between their neighbors; inside for navigation items,
    // which touch the pane edges.
    if (state.focusVisible && state.focus.id) {
        auto focused = std::find_if(items.begin(), items.end(),
                                    [&](const UiItem& item) { return item.id == state.focus.id; });
        if (focused != items.end()) {
            float radius = state.focus.link >= 0           ? 2 :
                           focused->kind == UiItemKind::Card ? 6 : 4;
            float outside = state.focus.link >= 0          ? 2 :
                            focused->kind == UiItemKind::Nav ? 0 : 3;
            for (D2D1_RECT_F rect : UiFocusBounds(g, *focused, state.focus.link)) {
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
