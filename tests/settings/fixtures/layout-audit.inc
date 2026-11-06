// Localized layout audit shared by the settings window's (i18n-layout.inc) and the usage history
// window's (i18n-history-layout.inc) fixtures: what a built frame shows wrong, measured with the
// production fonts in the frame's locale.

static std::string I18nLayoutNarrow(std::wstring_view text) {
    std::string narrow;
    for (wchar_t c : text) narrow += c >= 0x20 && c < 0x7F ? char(c) : '?';
    return narrow.size() > 48 ? narrow.substr(0, 45) + "..." : narrow;
}

static float I18nLayoutWidth(UiGraphics& g, std::wstring_view text, UiFont font) {
    return UiLayoutSize(UiLayout(g, text, UiTextStyleFor(font), 10000, false)).width;
}

// The widest word of `text` wrapped at `width`: past it, DirectWrite breaks the word.
static float I18nLayoutWidestWord(UiGraphics& g, std::wstring_view text, UiFont font, float width) {
    winrt::com_ptr<IDWriteTextLayout> layout;
    winrt::check_hresult(g.dwrite->CreateTextLayout(text.data(), UINT32(text.size()), UiFormat(g, UiTextStyleFor(font)),
                                                    std::max(width, 0.0f), 100000.0f, layout.put()));
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WHOLE_WORD);
    DWRITE_TEXT_METRICS m{};
    winrt::check_hresult(layout->GetMetrics(&m));
    return m.width;
}

// Where a Text item's text lies: its layout's line boxes placed at the item.
static D2D1_RECT_F I18nLayoutTextBox(UiGraphics& g, const UiItem& item) {
    DWRITE_TEXT_METRICS m{};
    winrt::check_hresult(UiRichLayout(g, item.runs, item.font, item.rect.right - item.rect.left, item.align,
                                      item.trim)->GetMetrics(&m));
    return {item.rect.left + m.left, item.rect.top + m.top, item.rect.left + m.left + m.width,
            item.rect.top + m.top + m.height};
}

// What a built frame shows wrong, one line each: a label wider than its control (clipped), a label
// ellipsized without a tooltip holding it whole (cut), wrapped text taller than its slot, two items
// over each other, an item out of its card or column. Popups: rows wider than the popup.
// Navigation entries stay left of `navRight`.
static std::vector<std::string> I18nLayoutAudit(UiGraphics& g, const UiFrame& frame, D2D1_SIZE_F client,
                                                float navRight) {
    std::vector<std::string> findings;
    auto report = [&](const char* surface, const UiItem& item, const std::string& what) {
        findings.push_back(std::string(surface) + " #" + std::to_string(item.id) + " " + what);
    };
    for (UiSurfaceId surface : {UiSurfaceId::Nav, UiSurfaceId::Main, UiSurfaceId::Content}) {
        const char* name = surface == UiSurfaceId::Main ? "main" : surface == UiSurfaceId::Nav ? "nav" : "content";
        const std::vector<UiItem>& items = UiSurfaceItems(frame, surface);
        // The cards (not subtle headers) and the column they span.
        float columnLeft = 1e9f, columnRight = -1e9f;
        for (const UiItem& item : items) {
            if (item.kind == UiItemKind::Card && item.button != UiButtonStyle::Subtle) {
                columnLeft = std::min(columnLeft, item.rect.left);
                columnRight = std::max(columnRight, item.rect.right);
            }
        }
        struct Box {
            const UiItem* item;
            D2D1_RECT_F rect;
        };
        std::vector<Box> boxes;
        for (const UiItem& item : items) {
            const D2D1_RECT_F& r = item.rect;
            float width = r.right - r.left;
            auto fits = [&](std::wstring_view text, UiFont font, float room, const char* what, bool cut = false) {
                float need = I18nLayoutWidth(g, text, font);
                if (need > room + 0.5f && !(cut && !item.tooltip.empty())) {
                    report(name, item, std::string(cut ? "cut " : "clipped ") + what + " \"" + I18nLayoutNarrow(text) +
                                           "\" needs " + std::to_string(int(std::ceil(need))) + " of " +
                                           std::to_string(int(room)));
                }
            };
            switch (item.kind) {
                case UiItemKind::Nav:
                    fits(item.text, UiFont::Body, UiNavLabelWidth(g, item), "nav label", true);
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Button: {
                    float icon = item.icon.empty() ? 0 : item.iconSize + (item.text.empty() ? 0 : 8);
                    fits(item.text, item.font, width - icon - 16, "button label");
                    boxes.push_back({&item, r});
                    break;
                }
                case UiItemKind::Dropdown:
                    fits(item.text, UiFont::Body, UiDropdownTextWidth(item), "dropdown value", true);
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Chip:
                    fits(item.text, item.font, width - 12, "chip", true);
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Pill:
                    fits(item.text, item.font, width - 28, "pill");
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Gallery: {
                    D2D1_RECT_F thumb = UiGalleryThumbRect(item);
                    float room = UiGalleryCaptionWidth(item);
                    float height = UiLayoutSize(UiLayout(g, item.text, UiTextStyleFor(UiFont::Note), room)).height;
                    if (thumb.bottom + 8 + height > r.bottom - 9 + 0.5f) {
                        report(name, item, "gallery caption \"" + I18nLayoutNarrow(item.text) + "\" taller than its card");
                    }
                    if (I18nLayoutWidestWord(g, item.text, UiFont::Note, room) > room + 0.5f) {
                        report(name, item, "gallery caption \"" + I18nLayoutNarrow(item.text) + "\" breaks a word");
                    }
                    boxes.push_back({&item, r});
                    break;
                }
                case UiItemKind::Toggle:
                    if (item.align == DWRITE_TEXT_ALIGNMENT_TRAILING) {
                        fits(item.text, UiFont::Note, width - 48, "toggle label", true);
                    } else if (!item.text.empty()) {
                        fits(item.text, UiFont::Body, width - 52, "toggle state");
                    }
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Link:
                    fits(item.text, item.font, width - 22, "link");
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Text: {
                    if (item.runs.empty() || UiRunsText(item.runs).empty()) break;
                    D2D1_RECT_F box = I18nLayoutTextBox(g, item);
                    if (item.trim) {
                        // Cut: what the pointer over it hits must hold it whole in its tooltip.
                        DWRITE_TEXT_METRICS whole{};
                        winrt::check_hresult(UiRichLayout(g, item.runs, item.font, 10000, DWRITE_TEXT_ALIGNMENT_LEADING)
                                                 ->GetMetrics(&whole));
                        const UiItem* target =
                            UiFindItem(frame, UiHitTest(g, items, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}).id);
                        if (whole.width > width + 0.5f &&
                            (!target || UiRunsText(target->tooltip).find(UiRunsText(item.runs)) == std::wstring::npos)) {
                            report(name, item, "cut text \"" + I18nLayoutNarrow(UiRunsText(item.runs)) + "\"");
                        }
                    }
                    if (!item.trim && I18nLayoutWidestWord(g, UiRunsText(item.runs), item.font, width) > width + 0.5f) {
                        report(name, item, "text \"" + I18nLayoutNarrow(UiRunsText(item.runs)) + "\" breaks a word in " +
                                               std::to_string(int(width)));
                    }
                    if (box.bottom > r.bottom + 0.5f || box.right > r.right + 0.5f || box.left < r.left - 0.5f) {
                        report(name, item, "text \"" + I18nLayoutNarrow(UiRunsText(item.runs)) + "\" overflows its slot (" +
                                               std::to_string(int(box.right - box.left)) + "x" +
                                               std::to_string(int(box.bottom - box.top)) + " in " +
                                               std::to_string(int(width)) + "x" + std::to_string(int(r.bottom - r.top)) +
                                               ")");
                    }
                    boxes.push_back({&item, box});
                    break;
                }
                case UiItemKind::Slider:
                    boxes.push_back({&item, UiInflate(r, 10, 0)});
                    break;
                case UiItemKind::NumberBox:
                case UiItemKind::TextBox:
                case UiItemKind::Swatch:
                case UiItemKind::Glyph:
                case UiItemKind::Shape:
                case UiItemKind::Handle:
                case UiItemKind::Preview:
                case UiItemKind::Segmented:
                case UiItemKind::Chart:
                case UiItemKind::Custom:
                    boxes.push_back({&item, r});
                    break;
                default:
                    break;
            }
            if ((item.kind == UiItemKind::Dropdown || item.kind == UiItemKind::Button) && !item.options.empty()) {
                std::vector<UiItem> rows;
                UiLayoutOptions(g, item.options, item.action, width, rows);
                for (const UiItem& row : rows) {
                    float room = row.rect.right - row.rect.left - (row.runs.empty() ? (row.icon.empty() ? 12 : 40) : 44) - 12;
                    float need = std::max(I18nLayoutWidth(g, row.text, UiFont::Body),
                                          row.runs.empty() ? 0 : I18nLayoutWidth(g, UiRunsText(row.runs), UiFont::Caption));
                    if (need > room + 0.5f) {
                        report(name, item, "popup row \"" + I18nLayoutNarrow(row.text) + "\" needs " +
                                               std::to_string(int(std::ceil(need))) + " of " + std::to_string(int(room)));
                    }
                }
            }
        }

        // Overlaps: two boxes crossing, unless one holds the other as its part (a toggle's label, a
        // shape's glyph).
        for (size_t i = 0; i < boxes.size(); i++) {
            for (size_t j = i + 1; j < boxes.size(); j++) {
                const D2D1_RECT_F &a = boxes[i].rect, &b = boxes[j].rect;
                float ix = std::min(a.right, b.right) - std::max(a.left, b.left);
                float iy = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
                if (ix <= 0.5f || iy <= 0.5f) continue;
                auto holds = [](const Box& outer, const Box& inner) {
                    const D2D1_RECT_F &o = outer.rect, &n = inner.rect;
                    bool inside = n.left >= o.left - 0.5f && n.right <= o.right + 0.5f && n.top >= o.top - 0.5f &&
                                  n.bottom <= o.bottom + 0.5f;
                    return inside && (outer.item->kind == UiItemKind::Toggle || outer.item->kind == UiItemKind::Shape ||
                                      outer.item->kind == UiItemKind::Preview);
                };
                if (holds(boxes[i], boxes[j]) || holds(boxes[j], boxes[i])) continue;
                // A glyph drawn over another as one icon (an info bar's severity mark).
                if (boxes[i].item->kind == UiItemKind::Glyph && boxes[j].item->kind == UiItemKind::Glyph &&
                    std::memcmp(&a, &b, sizeof(a)) == 0) {
                    continue;
                }
                findings.push_back(std::string(name) + " #" + std::to_string(boxes[i].item->id) + " and #" +
                                   std::to_string(boxes[j].item->id) + " overlap (" + std::to_string(int(ix)) + "x" +
                                   std::to_string(int(iy)) + ")");
            }
        }

        // Out of its card (the smallest card holding its top-left), else out of the column (content)
        // or the client (main). The navigation pane's items stay in the pane.
        for (const Box& box : boxes) {
            const D2D1_RECT_F& r = box.rect;
            const UiItem* card = nullptr;
            for (const UiItem& item : items) {
                if (item.kind != UiItemKind::Card || item.button == UiButtonStyle::Subtle) continue;
                const D2D1_RECT_F& c = item.rect;
                if (r.left >= c.left - 0.5f && r.left < c.right && r.top >= c.top - 0.5f && r.top < c.bottom &&
                    (!card || (c.right - c.left) * (c.bottom - c.top) <
                                  (card->rect.right - card->rect.left) * (card->rect.bottom - card->rect.top))) {
                    card = &item;
                }
            }
            float left = card ? card->rect.left : surface == UiSurfaceId::Content ? columnLeft : 0;
            float right = card ? card->rect.right : surface == UiSurfaceId::Content ? columnRight : client.width;
            if (box.item->kind == UiItemKind::Nav) right = navRight;
            float bottom = card ? card->rect.bottom : 1e9f;
            if (r.left < left - 0.5f || r.right > right + 0.5f || r.bottom > bottom + 0.5f) {
                findings.push_back(std::string(name) + " #" + std::to_string(box.item->id) + " out of its " +
                                   (card ? "card" : "column") + " (" + std::to_string(int(r.left)) + ".." +
                                   std::to_string(int(r.right)) + " in " + std::to_string(int(left)) + ".." +
                                   std::to_string(int(right)) + ")");
            }
        }
    }
    return findings;
}
