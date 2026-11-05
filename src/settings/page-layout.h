// Settings > Layout: the tile style gallery and the position, then the chosen style's sizes, identity
// and text, pace markers and spacing (approved mockups layout-v2.png, layout-v2-bars.png). The preview
// card is in the shell's pinned header (settings/preview.h). Pure builder.

// By BarLayout.
static constexpr PCWSTR kSettingsTileStyles[] = {L"Usage graph",    L"Stacked bars",     L"Vertical bars",
                                                 L"Segmented ring", L"Concentric rings", L"Gauge %",
                                                 L"Clock pies",     L"Rounded square",   L"Headline %",
                                                 L"Quota matrix"};
static constexpr COLORREF kSettingsPaceColors[] = {kDefaultPaceTickColor, RGB(0xFF, 0xFF, 0xFF),
                                                   RGB(0x1C, 0x1C, 0x1C), RGB(0x4C, 0xC2, 0xFF),
                                                   RGB(0xFF, 0x6E, 0xC7)};
static constexpr COLORREF kSettingsBadgeColors[] = {kBadgeTaskbarTextColor, RGB(0x4C, 0xC2, 0xFF),
                                                    RGB(0x1C, 0x1C, 0x1C), RGB(0xFF, 0x6E, 0xC7)};

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildLayout(UiGraphics& g, const SettingsView& view, std::vector<UiItem>& items,
                                   uint32_t& id, float x, float y, float width) {
    const Settings& s = view.settings;
    const BarLayout layout = s.barLayout;
    const bool bars = !IsTileLayout(layout);

    // Tile style (mockup .gallery): three cards a row, 8 DIP apart, the chosen one outlined.
    y = UiAddSection(items, id, x, y + 16, width, L"Tile style");
    {
        constexpr float kCardHeight = 1 + 8 + 72 + 8 + 17 + 2 + 8 + 1, kGap = 8;
        constexpr int kRows = int(std::size(kSettingsTileStyles) + 2) / 3;
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
        y += kRows * kCardHeight + (kRows - 1) * kGap;
    }

    // Placement: the position, then the taskbars that show the bars in the tray instead of beside
    // Start (one line each).
    y = UiAddSection(items, id, x, y + 20, width, L"Placement");
    {
        UiCardSpec spec;
        spec.title = L"Position";
        spec.description = {{L"Where the bars sit on the taskbar. Left side and Right of Start are experimental: "
                             L"the taskbar\u2019s buttons move over to make room."}};
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::Placement, 260,
                                    {{L"Before notification icons", int(TaskbarPlacement::BeforeIcons)},
                                     {L"Before network and volume", int(TaskbarPlacement::BeforeNetwork)},
                                     {L"Before the clock", int(TaskbarPlacement::BeforeClock)},
                                     {L"After the clock", int(TaskbarPlacement::AfterClock)},
                                     {L"After Show desktop", int(TaskbarPlacement::AfterShowDesktop)},
                                     {L"Left side", int(TaskbarPlacement::LeftSide)},
                                     {L"Right of Start", int(TaskbarPlacement::RightOfStart)}},
                                    int(s.placement));
    }
    if (IsStartAreaPlacement(s.placement) && !view.status.placementFallbacks.empty()) {
        // Left side needs the taskbar's buttons; Right of Start also a shown Start.
        PCWSTR unavailable = s.placement == TaskbarPlacement::RightOfStart ?
                                 L"its Start button is hidden or wasn\u2019t found." :
                                 L"its taskbar buttons weren\u2019t found.";
        std::wstring text;
        for (const SettingsPlacementFallback& fallback : view.status.placementFallbacks) {
            if (!text.empty()) text += L"\n";
            text += L"Display " + std::to_wstring(fallback.display) + L" shows the bars in the tray: " +
                    (fallback.conflict ? L"another mod controls the space they need. Restart Explorer to retry." :
                                         unavailable);
        }
        y = UiAddInfoBar(g, items, id, SettingsControlId(SettingsField::Placement, 8), x, y + 3, width, {{text}},
                         UiSeverity::Caution);
    }

    // Account identity is per account: a card that goes to Accounts. Fixed ids past the gallery's
    // (its five items' within the field's 16), so focus stays on it when cards above it change.
    auto identityCard = [&](float top) {
        UiCardSpec spec;
        spec.glyph = L"\uE716";
        spec.title = L"Account identity is set per account in Accounts";
        spec.description = {{L"Logo + number badge, logo only, or label."}};
        spec.trailingGlyph = L"\uE76C";
        spec.action = {UiActionKind::Navigate, int(SettingsUiPage::Accounts)};
        spec.minHeight = 62;
        static_assert(std::size(kSettingsTileStyles) <= 10);
        uint32_t cardId = SettingsControlId(SettingsField::BarLayout, 10);
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
                     L"Text size of the number on a logo when several accounts share a provider, in pixels.",
                     SettingsField::BadgeSize);
        {
            UiCardSpec spec;
            spec.title = L"Badge color";
            spec.description = {{L"White, the default, follows the taskbar\u2019s text color. Custom... opens "
                                  L"the color picker."}};
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
            spec.description = {{L"Short key beside each bar: 5h, 7d, Fa, Ar, Ex, Cr, Mo."}};
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
    } else if (IsFaceTile(layout)) {
        // Face tiles share the ring size and thickness; pies are solid.
        const bool gauge = layout == BarLayout::Gauge, pies = layout == BarLayout::ClockPies;
        const bool square = layout == BarLayout::RoundedSquare;
        y = UiAddSection(items, id, x, y + 20, width, gauge ? L"Gauge" : pies ? L"Pies" : square ? L"Square" : L"Ring");
        {
            UiCardSpec spec;
            spec.title = gauge ? L"Gauge size" : pies ? L"Pie area" : square ? L"Square size" : L"Ring size";
            spec.description = {{pies   ? L"Height in pixels; one pie per quota, smaller from the third on." :
                                 square ? L"Side in pixels." :
                                          L"Diameter in pixels."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y, width, spec, SettingsField::RingSize);
        }
        if (!pies) {
            UiCardSpec spec;
            spec.title = gauge ? L"Gauge thickness" : square ? L"Square thickness" : L"Ring thickness";
            spec.description = {{gauge ? L"Stroke width in pixels. Narrows automatically when the arc doesn\u2019t fit "
                                         L"around the number." :
                                         L"Stroke width in pixels. Narrows automatically when the meters don\u2019t fit "
                                         L"around the logo or text."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::RingThickness);
        }
        y = UiAddSection(items, id, x, y + 20, width, L"Identity");
        y = logoCards(identityCard(y) + 3, true);
    } else if (layout == BarLayout::Sparkline) {
        // Usage graphs: each chart's width and height, its text size, the extra usage on the weekly
        // chart, the long windows' span and its swap on hover, then the current position cues and
        // the reading's size.
        y = UiAddSection(items, id, x, y + 20, width, L"Usage graph");
        {
            UiCardSpec spec;
            spec.title = L"Chart width";
            spec.description = {{L"Pixels across each quota\u2019s chart."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y, width, spec, SettingsField::SparklineWidth);
        }
        {
            UiCardSpec spec;
            spec.title = L"Chart height";
            spec.description = {{L"Percent of the tile\u2019s height, from 0% to 100% used, or left in Remaining mode."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineHeight);
        }
        {
            UiCardSpec spec;
            spec.title = L"Text size";
            spec.description = {{L"Quota keys (5h, 7d, ...) and extra usage on the charts, in pixels."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineTextSize);
        }
        {
            UiCardSpec spec;
            spec.title = L"Chart outline";
            spec.description = {{L"A faint border around each chart."}};
            y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineOutline);
        }
        {
            UiCardSpec spec;
            spec.title = L"Extra usage";
            spec.description = {{L"Anthropic extra usage or OpenAI credits in the 7d chart\u2019s top-left corner "
                                 L"(bottom-left in Remaining mode), when selected for the account and reported: spent "
                                 L"in Used mode, left in Remaining mode. Percent shows the amount without a limit. "
                                 L"OpenAI credits never get a chart of their own."}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineExtra, 200,
                                        {{L"Off", int(SparklineExtra::Off)},
                                         {L"Amount", int(SparklineExtra::Amount)},
                                         {L"Percent", int(SparklineExtra::Percent)}},
                                        int(s.sparklineExtra));
        }
        y = UiAddSection(items, id, x, y + 20, width, L"Time span");
        {
            UiCardSpec spec;
            spec.title = L"Long windows";
            spec.description = {{L"Weekly and monthly charts (7d, Fa, Ar, Ex, Mo): the whole window, or 12 to 48 "
                                 L"hours before and after now."}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::SparklineSpan, 200,
                                        {{L"Whole window", int(SparklineSpan::Window)},
                                         {L"12 hours each side", int(SparklineSpan::Hours12)},
                                         {L"24 hours each side", int(SparklineSpan::Hours24)},
                                         {L"36 hours each side", int(SparklineSpan::Hours36)},
                                         {L"48 hours each side", int(SparklineSpan::Hours48)}},
                                        int(s.sparklineSpan));
        }
        {
            UiCardSpec spec;
            spec.title = L"On hover";
            spec.description = {{L"What those charts show while the pointer rests on the account."}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineHoverSpan, 200,
                                        {{L"Off", -1},
                                         {L"Whole window", int(SparklineSpan::Window)},
                                         {L"12 hours each side", int(SparklineSpan::Hours12)},
                                         {L"24 hours each side", int(SparklineSpan::Hours24)},
                                         {L"36 hours each side", int(SparklineSpan::Hours36)},
                                         {L"48 hours each side", int(SparklineSpan::Hours48)}},
                                        SettingsFieldValue(s, SettingsField::SparklineHoverSpan));
        }
        y = UiAddSection(items, id, x, y + 20, width, L"Current position");
        struct Cue {
            PCWSTR title, description;
            SettingsField field;
        };
        static constexpr Cue kCues[] = {
            {L"Elapsed time", L"Lights the part of the quota\u2019s window that has passed; the rest stays dim.",
             SettingsField::SparklineElapsed},
            {L"Now line", L"A thin line across the chart at the current time.", SettingsField::SparklineNowLine},
            {L"Now caret", L"A small triangle on the chart\u2019s bottom edge at the current time.",
             SettingsField::SparklineNowCaret},
            {L"Ringed reading", L"A ring around the current reading, cut out of the lines under it. Off: a plain dot.",
             SettingsField::SparklineRing},
        };
        for (size_t k = 0; k < std::size(kCues); k++) {
            UiCardSpec spec;
            spec.title = kCues[k].title;
            spec.description = {{kCues[k].description}};
            y = SettingsAddToggleCard(g, view, items, id, x, k ? y + 3 : y, width, spec, kCues[k].field);
        }
        {
            UiCardSpec spec;
            spec.title = L"Reading size";
            spec.description = {{L"Percent of the current reading\u2019s standard size: the ring and its dot, or the "
                                 L"plain dot."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineReadingScale);
        }
        y = UiAddSection(items, id, x, y + 20, width, L"Identity");
        y = logoCards(identityCard(y) + 3, true);
    } else if (layout == BarLayout::QuotaMatrix) {
        // Quota matrix: the numbers' size, the headline's.
        y = UiAddSection(items, id, x, y + 20, width, L"Matrix");
        {
            UiCardSpec spec;
            spec.title = L"Number size";
            spec.description = {{L"Text size of each quota\u2019s percentage, in pixels; its key scales with it. Two "
                                 L"rows shrink to fit the taskbar."}};
            y = SettingsAddSliderCard(g, view, items, id, x, y, width, spec, SettingsField::HeadlineFontSize);
        }
        y = UiAddSection(items, id, x, y + 20, width, L"Identity");
        y = logoCards(identityCard(y) + 3, true);
    } else {
        // Headline tiles: the number's size, the countdown, then its bars' sizes and the bars'
        // gap, which the countdown replaces.
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
            spec.title = L"Reset countdown";
            spec.description = {{L"Time until that quota resets under the number, instead of each quota\u2019s bar."}};
            y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HeadlineCountdown);
        }
        const bool segments = !s.headlineCountdown;
        PCWSTR countdownOn = L"Turn off Reset countdown to change this.";
        {
            UiCardSpec spec;
            spec.title = L"Bar length";
            spec.description = {{L"Pixels along each quota\u2019s bar under the number."}};
            spec.enabled = segments;
            if (!segments) spec.reason = countdownOn;
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HeadlineBarLength);
        }
        {
            UiCardSpec spec;
            spec.title = L"Bar thickness";
            spec.description = {{L"Pixels across each bar."}};
            spec.enabled = segments;
            if (!segments) spec.reason = countdownOn;
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HeadlineBarThickness);
        }
        {
            UiCardSpec spec;
            spec.title = L"Bar gap";
            spec.description = {{L"Space between an account\u2019s bars."}};
            spec.enabled = segments;
            if (!segments) spec.reason = countdownOn;
            y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::BarGap, L"px");
        }
        y = UiAddSection(items, id, x, y + 20, width, L"Identity");
        y = logoCards(identityCard(y) + 3, true);
    }

    // Pace markers: none on headline tiles or the quota matrix; the marker style is the bars'.
    // Style and color need the markers on.
    if (layout != BarLayout::LogoHeadline && layout != BarLayout::QuotaMatrix) {
        y = UiAddSection(items, id, x, y + 20, width, L"Pace markers");
        bool on = s.showPaceTicks;
        {
            UiCardSpec spec;
            spec.title = L"Pace markers";
            spec.description = {{layout == BarLayout::Sparkline ?
                                     L"A diagonal marks even use of the quota across its window, to compare with "
                                     L"its usage." :
                                     L"Ticks mark how much of each quota window has elapsed, to compare with its usage."}};
            y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::ShowPaceTicks);
        }
        if (bars) {
            UiCardSpec spec;
            spec.title = L"Marker style";
            spec.description = {{L"Thin caret, full-width line, edge notch, centered dot, or a time track: a small "
                                 L"bar under each bar that fills with the elapsed time."}};
            spec.enabled = on;
            if (!on) spec.reason = L"Turn on Pace markers to change this.";
            y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PaceTickStyle, 200,
                                        {{L"Caret", int(PaceTickStyle::Caret)},
                                         {L"Full line", int(PaceTickStyle::Line)},
                                         {L"Edge notch", int(PaceTickStyle::Notch)},
                                         {L"Dot", int(PaceTickStyle::Dot)},
                                         {L"Time track", int(PaceTickStyle::TimeTrack)}},
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
        spec.title = L"Gap after accounts";
        spec.description = {{L"Space after the last account, before whatever comes next on the taskbar."}};
        y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::RightMargin, L"px");
    }

    UiCardSpec reset;
    reset.title = L"Reset Layout to defaults";
    reset.description = {{L"Tile style, bar style, sizes, identity and text, pace markers, position and spacing. "
                           L"Accounts are not changed."}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset, SettingsField::ResetLayout, L"Reset");
}
