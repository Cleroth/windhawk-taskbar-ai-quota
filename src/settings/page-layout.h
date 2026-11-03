// Settings > Layout: the tile style gallery, then the chosen style's sizes, identity and text, pace
// markers and spacing (approved mockups layout-v2.png, layout-v2-bars.png). The preview card is in the
// shell's pinned header (settings/preview.h). Pure builder.

static constexpr PCWSTR kSettingsTileStyles[] = {L"Stacked bars", L"Vertical bars", L"Headline %",
                                                 L"Segmented ring", L"Concentric rings"};
static constexpr COLORREF kSettingsPaceColors[] = {kDefaultPaceTickColor, RGB(0xFF, 0xFF, 0xFF),
                                                   RGB(0x1C, 0x1C, 0x1C), RGB(0x4C, 0xC2, 0xFF),
                                                   RGB(0xFF, 0x6E, 0xC7)};
static constexpr COLORREF kSettingsBadgeColors[] = {kDefaultBadgeColor, kBadgeTaskbarTextColor,
                                                    RGB(0x1C, 0x1C, 0x1C), RGB(0xFF, 0x6E, 0xC7)};

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildLayout(UiGraphics& g, const SettingsView& view, std::vector<UiItem>& items,
                                   uint32_t& id, float x, float y, float width) {
    const Settings& s = view.settings;
    const BarLayout layout = s.barLayout;
    const bool bars = !IsTileLayout(layout);
    const bool ring = layout == BarLayout::SegmentedRing || layout == BarLayout::ConcentricRings;

    // Tile style (mockup .gallery): three cards a row, 8 DIP apart, the chosen one outlined.
    y = UiAddSection(items, id, x, y + 16, width, L"Tile style");
    {
        constexpr float kCardHeight = 1 + 8 + 72 + 8 + 17 + 2 + 8 + 1, kGap = 8;
        float cardWidth = std::max((width - 2 * kGap) / 3, 1.0f);
        for (int i = 0; i < int(std::size(kSettingsTileStyles)); i++) {
            float left = x + float(i % 3) * (cardWidth + kGap), top = y + float(i / 3) * (kCardHeight + kGap);
            UiItem card;
            card.id = SettingsControlId(SettingsField::BarLayout, uint32_t(i));
            card.kind = UiItemKind::Gallery;
            card.rect = {left, top, left + cardWidth, top + kCardHeight};
            card.text = kSettingsTileStyles[i];
            card.selected = i == int(layout);
            card.action = SettingsCommand(SettingsField::BarLayout, i);
            items.push_back(std::move(card));
        }
        y += 2 * kCardHeight + kGap;
    }

    // Account identity is per account: a card that goes to Accounts. Fixed ids, so focus stays on
    // it when cards above it change.
    auto identityCard = [&](float top) {
        UiCardSpec spec;
        spec.glyph = L"\uE716";
        spec.title = L"Account identity is set per account in Accounts";
        spec.description = {{L"Logo + number badge, logo only, or label."}};
        spec.trailingGlyph = L"\uE76C";
        spec.action = {UiActionKind::Navigate, int(SettingsUiPage::Accounts)};
        spec.minHeight = 62;
        uint32_t cardId = SettingsControlId(SettingsField::BarLayout, 8);
        return UiAddCard(g, items, cardId, x, top, width, spec).card.bottom;
    };

    // A color card's swatches (mockup .swrow): 24 DIP circles 10 DIP apart, Custom... (`custom`)
    // 16 DIP after them. A color picked in Custom... shows as one more swatch.
    auto colorCard = [&](float top, UiCardSpec spec, SettingsField field, SettingsField custom,
                         std::vector<COLORREF> colors, COLORREF current) {
        if (std::find(colors.begin(), colors.end(), current) == colors.end()) colors.push_back(current);
        PCWSTR customText = L"Custom...";
        float buttonWidth =
            std::ceil(UiLayoutSize(UiLayout(g, customText, UiTextStyleFor(UiFont::Body), 1000, false)).width) + 28;
        float swatches = float(colors.size()) * 24 + float(colors.size() - 1) * 10;
        spec.trailing = {swatches + 16 + buttonWidth, 32};
        if (const SettingsDraft* draft = SettingsViewDraft(view, field)) spec.error = draft->error;
        UiCardLayout card = UiAddCard(g, items, id, x, top, width, spec);
        float center = (card.trailing.top + card.trailing.bottom) / 2;
        for (size_t i = 0; i < colors.size(); i++) {
            UiItem swatch;
            swatch.id = SettingsControlId(field, uint32_t(1 + i));
            swatch.kind = UiItemKind::Swatch;
            float left = card.trailing.left + float(i) * 34;
            swatch.rect = {left, center - 12, left + 24, center + 12};
            swatch.selected = colors[i] == current;
            swatch.enabled = spec.enabled;
            swatch.action = SettingsCommand(field, int(colors[i]));
            items.push_back(std::move(swatch));
        }
        UiItem button;
        button.id = SettingsControlId(custom);
        button.kind = UiItemKind::Button;
        button.rect = {card.trailing.right - buttonWidth, card.trailing.top, card.trailing.right,
                       card.trailing.bottom};
        button.text = customText;
        button.enabled = spec.enabled;
        button.action = SettingsCommand(custom);
        items.push_back(std::move(button));
        return card.card.bottom;
    };

    // Logo identities in every style: the logo size and the colors of the logos that have brand
    // colors, the badge's size and color, then its offsets. Bars need an identity position, except
    // for the logo colors, which the flyout and the windows also show.
    auto logoCards = [&](float top, bool placed) {
        auto slider = [&](float at, PCWSTR title, PCWSTR description, SettingsField field) {
            UiCardSpec spec;
            spec.title = title;
            spec.description = {{description}};
            spec.enabled = placed;
            if (!placed) spec.reason = L"Choose an identity position to change this.";
            return SettingsAddSliderCard(g, view, items, id, x, at, width, spec, field);
        };
        auto brand = [&](float at, PCWSTR title, PCWSTR description, SettingsField field, PCWSTR colors) {
            UiCardSpec spec;
            spec.title = title;
            spec.description = {{description}};
            return SettingsAddDropdownCard(g, view, items, id, x, at, width, spec, field, 200,
                                           {{L"Taskbar text color", 0}, {colors, 1}},
                                           SettingsFieldValue(s, field));
        };
        top = slider(top, L"Logo size", L"Percent of the standard size; rings keep it inside the hole.",
                     SettingsField::LogoScale);
        top = brand(top + 3, L"Claude logo",
                    L"The taskbar\u2019s text color, or Claude\u2019s orange. Details, Accounts and usage history "
                    L"match.",
                    SettingsField::ClaudeLogoBrand, L"Orange");
        top = brand(top + 3, L"Antigravity logo",
                    L"The Gemini sparkle in the taskbar\u2019s text color, or its purple-to-blue gradient.",
                    SettingsField::GeminiLogoBrand, L"Gemini gradient");
        top = slider(top + 3, L"Badge size",
                     L"Text size of the number on a logo when several visible accounts share a provider, in pixels.",
                     SettingsField::BadgeSize);
        {
            UiCardSpec spec;
            spec.title = L"Badge color";
            spec.description = {{L"Light blue by default; white follows the taskbar\u2019s text color. Custom... "
                                  L"opens the color picker."}};
            spec.enabled = placed;
            if (!placed) spec.reason = L"Choose an identity position to change this.";
            top = colorCard(top + 3, spec, SettingsField::BadgeColor, SettingsField::BadgeColorCustom,
                            {std::begin(kSettingsBadgeColors), std::end(kSettingsBadgeColors)},
                            s.badgeColor & 0x00FFFFFF);
        }
        top = slider(top + 3, L"Badge horizontal offset",
                     L"Pixels right, or left when negative, from the logo\u2019s lower-right corner (inside the "
                     L"hole on rings).",
                     SettingsField::BadgeOffsetX);
        return slider(top + 3, L"Badge vertical offset", L"Pixels down, or up when negative.",
                      SettingsField::BadgeOffsetY);
    };

    if (bars) {
        y = UiAddSection(items, id, x, y + 20, width, L"Bars");
        {
            UiCardSpec spec;
            spec.title = L"Bar style";
            spec.description = {{L"A full track behind each fill, or a thin rail under it."}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::BarStyle, 200,
                                        {{L"Full track", int(BarStyle::Track)}, {L"Thin rail", int(BarStyle::Rail)}},
                                        int(s.barStyle));
        }
        {
            UiCardSpec spec;
            spec.title = L"Bar length";
            spec.description = {{L"Pixels along each bar."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::BarLength);
        }
        {
            UiCardSpec spec;
            spec.title = L"Bar thickness";
            spec.description = {{L"Pixels across each bar."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::BarThickness);
        }
        {
            UiCardSpec spec;
            spec.title = L"Bar gap";
            spec.description = {{L"Space between an account\u2019s bars."}};
            y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::BarGap, L"px");
        }

        // Identity and text: the link to each account's identity, where it sits; the Label form's
        // size, the gap and the logo cards need a position. Then quota keys, and the bar text size
        // for them or percentage text.
        y = UiAddSection(items, id, x, y + 20, width, L"Identity and text");
        bool placed = s.labelPosition != LabelPosition::Hidden;
        y = identityCard(y);
        {
            UiCardSpec spec;
            spec.title = L"Identity position";
            spec.description = {{L"Where each account\u2019s identity sits next to its bars."}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::LabelPosition, 200,
                                        {{L"Left", int(LabelPosition::Left)},
                                         {L"Right", int(LabelPosition::Right)},
                                         {L"Top", int(LabelPosition::Top)},
                                         {L"Bottom", int(LabelPosition::Bottom)},
                                         {L"Hidden", int(LabelPosition::Hidden)}},
                                        int(s.labelPosition));
        }
        {
            UiCardSpec spec;
            spec.title = L"Label font size";
            spec.description = {{L"Text of accounts whose identity is Label, in pixels."}};
            spec.enabled = placed;
            if (!placed) spec.reason = L"Choose an identity position to change this.";
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::LabelFontSize);
        }
        {
            UiCardSpec spec;
            spec.title = L"Identity gap";
            spec.description = {{L"Space between the identity and the bars."}};
            spec.enabled = placed;
            if (!placed) spec.reason = L"Choose an identity position to change this.";
            y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::LabelGap, L"px");
        }
        y = logoCards(y + 3, placed);
        {
            UiCardSpec spec;
            spec.title = L"Quota keys";
            spec.description = {{L"Short key beside each bar: 5h, 7d, Fa, Ex, Cr."}};
            y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec, SettingsField::ShowBarLabels);
        }
        {
            bool text = s.showBarLabels || s.percentTextVisibility != PercentTextVisibility::Never;
            UiCardSpec spec;
            spec.title = L"Bar text size";
            spec.description = {{L"Percentage text and quota keys. Percentage text is set in Display."}};
            spec.enabled = text;
            if (!text) spec.reason = L"Turn on Quota keys, or percentage text in Display, to change this.";
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PercentFontSize);
        }
    } else if (ring) {
        y = UiAddSection(items, id, x, y + 20, width, L"Ring");
        {
            UiCardSpec spec;
            spec.title = L"Ring size";
            spec.description = {{L"Diameter in pixels."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y, width, spec, SettingsField::RingSize);
        }
        {
            UiCardSpec spec;
            spec.title = L"Ring thickness";
            spec.description = {{L"Stroke width in pixels. Narrows automatically when the meters don\u2019t fit "
                                 L"around the logo or text."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::RingThickness);
        }
        y = UiAddSection(items, id, x, y + 20, width, L"Identity");
        y = logoCards(identityCard(y) + 3, true);
    } else {
        // Headline tiles: the number's size, then its bars' sizes and the bars' gap.
        y = UiAddSection(items, id, x, y + 20, width, L"Headline");
        {
            UiCardSpec spec;
            spec.title = L"Number size";
            spec.description = {{L"Text size of the most-used quota\u2019s percentage, in pixels; its quota key "
                                 L"scales with it."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y, width, spec, SettingsField::HeadlineFontSize);
        }
        {
            UiCardSpec spec;
            spec.title = L"Bar length";
            spec.description = {{L"Pixels along each quota\u2019s bar under the number."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HeadlineBarLength);
        }
        {
            UiCardSpec spec;
            spec.title = L"Bar thickness";
            spec.description = {{L"Pixels across each bar."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HeadlineBarThickness);
        }
        {
            UiCardSpec spec;
            spec.title = L"Bar gap";
            spec.description = {{L"Space between an account\u2019s bars."}};
            y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::BarGap, L"px");
        }
        y = UiAddSection(items, id, x, y + 20, width, L"Identity");
        y = logoCards(identityCard(y) + 3, true);
    }

    // Pace markers: none on headline tiles; the marker style is the bars'. Style and color
    // need the markers on.
    if (layout != BarLayout::LogoHeadline) {
        y = UiAddSection(items, id, x, y + 20, width, L"Pace markers");
        bool on = s.showPaceTicks;
        {
            UiCardSpec spec;
            spec.title = L"Pace markers";
            spec.description = {{L"Ticks mark how much of each quota window has elapsed, to compare with its usage."}};
            y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::ShowPaceTicks);
        }
        if (bars) {
            UiCardSpec spec;
            spec.title = L"Marker style";
            spec.description = {{L"Thin caret, full-width line, edge notch, or centered dot."}};
            spec.enabled = on;
            if (!on) spec.reason = L"Turn on Pace markers to change this.";
            y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PaceTickStyle, 200,
                                        {{L"Caret", int(PaceTickStyle::Caret)},
                                         {L"Full line", int(PaceTickStyle::Line)},
                                         {L"Edge notch", int(PaceTickStyle::Notch)},
                                         {L"Dot", int(PaceTickStyle::Dot)}},
                                        int(s.paceTickStyle));
        }
        {
            UiCardSpec spec;
            spec.title = L"Marker color";
            spec.description = {{L"Light grey by default, dark on a light taskbar. Custom... opens the color picker."}};
            spec.enabled = on;
            if (!on) spec.reason = L"Turn on Pace markers to change this.";
            y = colorCard(y + 3, spec, SettingsField::PaceTickColor, SettingsField::PaceTickColorCustom,
                          {std::begin(kSettingsPaceColors), std::end(kSettingsPaceColors)},
                          s.paceTickColor & 0x00FFFFFF);
        }
    }

    y = UiAddSection(items, id, x, y + 20, width, L"Spacing");
    {
        UiCardSpec spec;
        spec.title = L"Account margin";
        spec.description = {{L"Space on each side of every account."}};
        y = SettingsAddNumberCard(g, view, items, id, x, y, width, spec, SettingsField::AccountMargin, L"px");
    }
    {
        UiCardSpec spec;
        spec.title = L"Tray gap";
        spec.description = {{L"Space between the last account and the system tray."}};
        y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::RightMargin, L"px");
    }

    UiCardSpec reset;
    reset.title = L"Reset Layout to defaults";
    reset.description = {{L"Tile style, bar style, sizes, identity and text, pace markers and spacing. Accounts are "
                           L"not changed."}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset, SettingsField::ResetLayout, L"Reset");
}
