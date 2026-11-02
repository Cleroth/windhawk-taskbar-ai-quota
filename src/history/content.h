// Usage history window content (approved mockups s-light, s-account, s-compare, s-states,
// s-empty, s-legend, q8a-filter): info bars, the chart's card (legend chips, key line, the chart
// item, drawn by history/chart-paint.h) or an empty state, the summary cards and the No readings
// card, from the view snapshot (history/view.h). Pure builder.

// Lays out the content from y in a column of `width`; the viewport's height sizes the chart's
// card. Returns the bottom of the last card.
static float HistoryUiBuildContent(UiGraphics& g, const HistoryUiView& view, std::vector<UiItem>& items,
                                   uint32_t& id, float x, float y, float width, float viewHeight) {
    const bool hc = view.theme == UiThemeKind::HighContrast, light = view.theme == UiThemeKind::Light;
    auto text = [&](D2D1_RECT_F rect, std::vector<UiRun> runs, UiFont font, UiInk ink, bool trim) {
        UiItem item;
        item.id = id++;
        item.kind = UiItemKind::Text;
        item.rect = rect;
        item.font = font;
        item.ink = ink;
        item.runs = std::move(runs);
        item.trim = trim;
        items.push_back(std::move(item));
    };
    auto shape = [&](D2D1_RECT_F rect, float viewBox, std::vector<UiShapePart> parts) {
        UiItem item;
        item.id = id++;
        item.kind = UiItemKind::Shape;
        item.rect = rect;
        item.viewBox = viewBox;
        item.parts = std::move(parts);
        items.push_back(std::move(item));
    };
    auto panel = [&](D2D1_RECT_F rect) {
        UiItem card;
        card.id = id++;
        card.kind = UiItemKind::Card;
        card.rect = rect;
        items.push_back(std::move(card));
    };

    // Info bars (mockup .ibar), 10 DIP apart: this user's recording off (caution, with Enable
    // recording), a storage error, the storage cap (caution), then the selected account's own
    // recording off or pause (info).
    if (!view.recordingEnabled) {
        y = UiAddInfoBar(g, items, id, id++, x, y, width,
                         {{L"History recording is off. Saved history stays; nothing new is recorded."}},
                         UiSeverity::Caution,
                         UiInfoButton{kHistoryUiEnableRecording, L"Enable recording",
                                      HistoryUiAction(HistoryUiCommand::EnableRecording),
                                      view.canEnableRecording}) + 10;
    }
    if (!view.storageError.empty()) {
        y = UiAddInfoBar(g, items, id, id++, x, y, width, {{view.storageError}}, UiSeverity::Error) + 10;
    }
    if (view.storageCapped) {
        y = UiAddInfoBar(g, items, id, id++, x, y, width, {{L"Storage cap reached; oldest samples were pruned."}},
                         UiSeverity::Caution) + 10;
    }
    if (view.selected >= 0 && size_t(view.selected) < view.accounts.size() && view.recordingEnabled) {
        const HistoryUiViewAccount& account = view.accounts[size_t(view.selected)];
        if (!account.removed && (account.recordingOff || account.paused)) {
            y = UiAddInfoBar(g, items, id, id++, x, y, width,
                             {{account.recordingOff ?
                                   L"Recording is off for this account. Its saved history stays; turn on Record "
                                   L"history in Settings > Accounts to record new polls." :
                                   L"Recording is paused for this account: it is hidden from the taskbar, and hidden "
                                   L"accounts are not polled."}}) + 10;
        }
    }

    // The chart's card (mockups .gcardh, .legendrow, .lchip, .keyline): inside 10 DIP (top), 14
    // (sides) and 8 (bottom) padding, the legend chips (24 DIP pills 6 DIP apart, wrapping), the
    // key line 4 DIP under them, the chart 4 DIP under that, its area between 200 and 300 DIP tall
    // by the viewport's height. Without a plotted line the chart holds a message and the card has
    // no legend. Without a chart, a message instead (a shorter card): the first load, no accounts,
    // a view no account supports.
    float plot = std::clamp(viewHeight - 212, 200.0f, 300.0f), height = plot + 40;
    PCWSTR message = view.loading                                     ? L"Loading local history\u2026" :
                     view.accounts.empty()                            ? L"No accounts to display." :
                     view.cards.empty() && view.noReadings.empty() ?
                         (view.selected >= 0 ? L"This account has no meters in this view." :
                                               L"No accounts support this view.") : nullptr;
    size_t graphCard = items.size();
    panel({x, y, x + width, y + height});
    if (!message) {
        // A chip: 1 DIP border, 10 DIP padding, its 18 DIP swatch and 7 DIP gap, the 12.5 DIP name.
        float left = x + 15, right = x + width - 15, top = y + 11;
        UiTextStyle name{UiFamily::Text, 12.5f, 16, DWRITE_FONT_WEIGHT_NORMAL};
        float cx = left;
        for (const HistoryUiLine& line : view.lines) {
            float chipWidth = std::min(std::ceil(UiLayoutSize(UiLayout(g, line.name, name, 100000, false)).width) + 47,
                                       right - left);
            if (cx > left && cx + chipWidth > right) {
                cx = left;
                top += 24 + 6;
            }
            UiItem chip;
            chip.id = line.chip;
            chip.kind = UiItemKind::Custom;
            chip.rect = {cx, top, cx + chipWidth, top + 24};
            chip.text = line.name;
            chip.selected = !line.hidden;
            chip.action = HistoryUiAction(HistoryUiCommand::Line, line.meter, line.historyId);
            chip.tooltip = {{line.title, UiRunStyle::Strong},
                            {view.compare ? L"\nSolid / filled: current week. Dashed / hollow: previous week. Click to "
                                            L"hide or show this line." :
                                            L"\nSolid / filled: current range. Click to hide or show this line.",
                             UiRunStyle::Normal, UiInk::Secondary}};
            items.push_back(std::move(chip));
            cx += chipWidth + 6;
        }
        if (!view.lines.empty()) {
            top += 24 + 4;
            text({left + 2, top, right, top + 16},
                 {{view.compare ? L"Solid: current \u00B7 Dashed: previous" : L"Solid: current"}}, UiFont::Caption,
                 UiInk::Muted, true);
            top += 16 + 4;
        }
        UiItem chart;
        chart.id = kHistoryUiChart;
        chart.kind = UiItemKind::Chart;
        chart.rect = {left, top, right, top + plot};
        items.push_back(std::move(chart));
        height = top + plot + 9 - y;
        items[graphCard].rect.bottom = y + height;
    } else {
        // Centered (mockup .pmsg), the first load's progress ring 8 DIP before it.
        float textWidth = std::ceil(UiLayoutSize(UiLayout(g, message, UiTextStyleFor(UiFont::Note), 10000, false)).width);
        float ring = view.loading ? 16 + 8 : 0;
        float left = x + std::max((width - textWidth - ring) / 2, 0.0f), center = y + height / 2;
        if (view.loading) {
            shape({left, center - 8, left + 16, center + 8}, 16,
                  {{L"M8 1.5A6.5 6.5 0 1 1 8 14.5A6.5 6.5 0 1 1 8 1.5", {}, UiInk::Disabled, hc ? 1.0f : 0.45f, 2},
                   {L"M8 1.5A6.5 6.5 0 0 1 14.5 8", {}, UiInk::Accent, 1, 2}});
        }
        text({left + ring, center - 9.5f, left + ring + textWidth + 1, center + 9.5f}, {{message}}, UiFont::Note,
             UiInk::Secondary, false);
    }
    y += height + 10;

    // Summary cards (mockup .stats, .stcard): up to three columns at least 240 DIP wide (two while
    // comparing, whose cards hold both periods), 8 DIP apart, each row as tall as its tallest card;
    // the No readings card last. A card: 11 DIP (border and padding) around a 16 DIP head, the
    // headline row (4 DIP above it, 30, 2 below) and 16 DIP lines; No readings: 24 DIP rows 4 DIP
    // under its head.
    std::vector<float> heights;
    for (const HistoryUiCard& card : view.cards) {
        heights.push_back(11 + 16 + (card.value.empty() ? 4 : 36) + 16 * float(card.lines.size()) + 11);
    }
    if (!view.noReadings.empty()) heights.push_back(11 + 16 + 4 + 24 * float(view.noReadings.size()) + 11);
    if (heights.empty()) return y - 10;
    size_t columns = size_t(std::clamp(int((width + 8) / 248), 1, view.compare ? 2 : 3));
    float cardWidth = (width - 8 * float(columns - 1)) / float(columns);
    // A card's line (mockup .sthead svg: 16 x 8 DIP, 3 DIP wide whatever the series' width) in its
    // chart line's color (history/chart.h; high contrast: text and link alternating as inks).
    for (size_t first = 0; first < heights.size(); first += columns) {
        size_t last = std::min(first + columns, heights.size());
        float rowHeight = *std::max_element(heights.begin() + ptrdiff_t(first), heights.begin() + ptrdiff_t(last));
        for (size_t i = first; i < last; i++) {
            float cx = x + (cardWidth + 8) * float(i - first), left = cx + 13, right = cx + cardWidth - 13;
            panel({cx, y, cx + cardWidth, y + rowHeight});
            if (i == view.cards.size()) {
                text({left, y + 11, right, y + 27}, {{L"No readings in range"}}, UiFont::Caption, UiInk::Primary, true);
                float row = y + 31;
                for (const HistoryUiNoReadings& entry : view.noReadings) {
                    if (PCWSTR logo = HistoryUiLogo(entry.provider)) {
                        shape({left, row + 4, left + 16, row + 20}, 24, {{logo, {}, UiInk::Primary}});
                    }
                    float labelLeft = left + 24;
                    float labelWidth = std::min(
                        std::ceil(UiLayoutSize(UiLayout(g, entry.label, UiTextStyleFor(UiFont::Note), 10000, false)).width),
                        std::max(right - labelLeft, 0.0f));
                    text({labelLeft, row + 2.5f, labelLeft + labelWidth, row + 21.5f}, {{entry.label}}, UiFont::Note,
                         UiInk::Primary, true);
                    if (right - (labelLeft + labelWidth + 8) >= 16) {
                        text({labelLeft + labelWidth + 8, row + 4, right, row + 20}, {{entry.detail}}, UiFont::Caption,
                             UiInk::Muted, true);
                    }
                    row += 24;
                }
                continue;
            }
            const HistoryUiCard& card = view.cards[i];
            float titleLeft = left;
            if (!card.detailsOnly) {
                UiShapePart line{L"M1 4H15", {}, UiInk::Inherit, 1, 3};
                if (hc) line.ink = i % 2 ? UiInk::Link : UiInk::Primary;
                else line.color = HistoryUiHue(card.color, light);
                shape({left, y + 15, left + 16, y + 23}, 16, {line});
                titleLeft = left + 24;
            }
            std::vector<UiRun> title{{card.title}};
            if (!card.status.empty()) title.push_back({L" \u00B7 " + card.status, UiRunStyle::Normal, UiInk::Muted});
            text({titleLeft, y + 11, right, y + 27}, std::move(title), UiFont::Caption, UiInk::Primary, true);
            float lineTop = y + 31;
            if (!card.value.empty()) {
                // Headline (mockup .stnum, .stunit): the value in display type, then its time in
                // caption type 5 DIP after it, on the value's baseline.
                UiTextStyle number = UiTextStyleFor(UiFont::Subtitle), caption = UiTextStyleFor(UiFont::Caption);
                UiFontMetrics a = UiFontMetricsFor(g, number), b = UiFontMetricsFor(g, caption);
                float numberTop = y + 33;
                float whenTop = numberTop + (number.lineHeight - a.ascent - a.descent) / 2 + a.ascent -
                                (caption.lineHeight - b.ascent - b.descent) / 2 - b.ascent;
                float numberRight = std::min(
                    left + std::ceil(UiLayoutSize(UiLayout(g, card.value, number, 10000, false)).width) + 1, right);
                text({left, numberTop, numberRight, numberTop + number.lineHeight}, {{card.value}}, UiFont::Subtitle,
                     UiInk::Primary, true);
                if (right - (numberRight + 5) >= 16) {
                    text({numberRight + 5, whenTop, right, whenTop + caption.lineHeight}, {{card.when}}, UiFont::Caption,
                         UiInk::Muted, true);
                }
                lineTop = y + 63;
            }
            for (const std::wstring& line : card.lines) {
                text({left, lineTop, right, lineTop + 16}, {{line}}, UiFont::Caption, UiInk::Muted, true);
                lineTop += 16;
            }
        }
        y += rowHeight + 8;
    }
    return y - 8;
}
