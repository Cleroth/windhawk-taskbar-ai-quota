// Settings > Layout: the tile style gallery and the position, then the chosen style's sizes, identity
// and text, pace markers and spacing (approved mockups layout-v2.png, layout-v2-bars.png). The preview
// card is in the shell's pinned header (settings/preview.h). Pure builder.

// By BarLayout; also the Display page's name for the current style.
static constexpr I18nKey kSettingsTileStyles[] = {
    TrCtx(L"tile-style", L"Usage graph"),      TrCtx(L"tile-style", L"Stacked bars"),
    TrCtx(L"tile-style", L"Vertical bars"),    TrCtx(L"tile-style", L"Segmented ring"),
    TrCtx(L"tile-style", L"Concentric rings"), TrCtx(L"tile-style", L"Gauge %"),
    TrCtx(L"tile-style", L"Clock pies"),       TrCtx(L"tile-style", L"Rounded square"),
    TrCtx(L"tile-style", L"Headline %"),       TrCtx(L"tile-style", L"Quota matrix")};
static constexpr COLORREF kSettingsPaceColors[] = {kDefaultPaceTickColor, RGB(0xFF, 0xFF, 0xFF),
                                                   RGB(0x1C, 0x1C, 0x1C), RGB(0x4C, 0xC2, 0xFF),
                                                   RGB(0xFF, 0x6E, 0xC7)};
static constexpr COLORREF kSettingsBadgeColors[] = {kBadgeTaskbarTextColor, RGB(0x4C, 0xC2, 0xFF),
                                                    RGB(0x1C, 0x1C, 0x1C), RGB(0xFF, 0x6E, 0xC7)};

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildLayout(UiGraphics& g, const SettingsView& view, std::vector<UiItem>& items,
                                   uint32_t& id, float x, float y, float width) {
    const Settings& s = view.settings;
    const I18nLocale& locale = view.locale;
    const BarLayout layout = s.barLayout;
    const bool bars = !IsTileLayout(layout);
    auto text = [&](I18nKey k) { return I18nText(locale, k); };
    auto section = [&](float top, I18nKey k) { return UiAddSection(items, id, x, top, width, text(k)); };
    // The quota keys the descriptions name, as the taskbar shows them (kQuotaKeys' order).
    auto key = [&](size_t index) { return text(kQuotaKeys[index]); };
    const std::wstring pixels = text(TrCtx(L"pixel-unit", L"px"));

    // Tile style (mockup .gallery): three cards a row, 8 DIP apart, the chosen one outlined; every
    // card as tall as the longest name wraps.
    y = section(y + 16, TrCtx(L"settings-section", L"Tile style"));
    {
        constexpr float kGap = 8;
        constexpr int kRows = int(std::size(kSettingsTileStyles) + 2) / 3;
        float cardWidth = std::max((width - 2 * kGap) / 3, 1.0f);
        std::vector<UiItem> cards;
        float captionHeight = 0;
        for (int i = 0; i < int(std::size(kSettingsTileStyles)); i++) {
            float left = x + float(i % 3) * (cardWidth + kGap);
            UiItem card;
            card.id = SettingsControlId(SettingsField::BarLayout, uint32_t(i));
            card.kind = UiItemKind::Gallery;
            card.rect = {left, 0, left + cardWidth, 0};
            card.text = text(kSettingsTileStyles[i]);
            card.selected = i == int(layout);
            card.action = SettingsCommand(SettingsField::BarLayout, i);
            captionHeight = std::max(captionHeight, UiLayoutSize(UiLayout(g, card.text, UiTextStyleFor(UiFont::Note),
                                                                          UiGalleryCaptionWidth(card))).height);
            cards.push_back(std::move(card));
        }
        const float cardHeight = 1 + 8 + 72 + 8 + captionHeight + 8 + 1;
        for (int i = 0; i < int(cards.size()); i++) {
            cards[size_t(i)].rect.top = y + float(i / 3) * (cardHeight + kGap);
            cards[size_t(i)].rect.bottom = cards[size_t(i)].rect.top + cardHeight;
            items.push_back(std::move(cards[size_t(i)]));
        }
        y += kRows * cardHeight + (kRows - 1) * kGap;
    }

    // Placement: the position, then the taskbars that show the bars in the tray instead of beside
    // Start (one line each).
    y = section(y + 20, TrCtx(L"settings-section", L"Placement"));
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Position"));
        spec.description = {{text(Tr(L"Where the bars sit on the taskbar. Left side and Right of Start are "
                                     L"experimental: the taskbar\u2019s buttons move over to make room."))}};
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::Placement, 260,
                                    {{text(TrCtx(L"placement", L"Before notification icons")),
                                      int(TaskbarPlacement::BeforeIcons)},
                                     {text(TrCtx(L"placement", L"Before network and volume")),
                                      int(TaskbarPlacement::BeforeNetwork)},
                                     {text(TrCtx(L"placement", L"Before the clock")), int(TaskbarPlacement::BeforeClock)},
                                     {text(TrCtx(L"placement", L"After the clock")), int(TaskbarPlacement::AfterClock)},
                                     {text(TrCtx(L"placement", L"After Show desktop")),
                                      int(TaskbarPlacement::AfterShowDesktop)},
                                     {text(TrCtx(L"placement", L"Left side")), int(TaskbarPlacement::LeftSide)},
                                     {text(TrCtx(L"placement", L"Right of Start")), int(TaskbarPlacement::RightOfStart)}},
                                    int(s.placement));
    }
    if (IsStartAreaPlacement(s.placement)) {
        // Left side needs the taskbar's buttons; Right of Start also a shown Start. A side taskbar
        // whose tray takes no bars shows none: the note below says so instead.
        const I18nKey unavailable =
            s.placement == TaskbarPlacement::RightOfStart ?
                Tr(L"Display {number} shows the bars in the tray: its Start button is hidden or wasn\u2019t found.") :
                Tr(L"Display {number} shows the bars in the tray: its taskbar buttons weren\u2019t found.");
        std::wstring note;
        for (const SettingsPlacementFallback& fallback : view.status.placementFallbacks) {
            const auto& sides = view.status.sideTaskbars;
            if (std::any_of(sides.begin(), sides.end(), [&](const SettingsSideTaskbar& taskbar) {
                    return taskbar.display == fallback.display && taskbar.unsupported;
                })) {
                continue;
            }
            if (!note.empty()) note += L"\n";
            note += I18nText(locale,
                             fallback.side ?
                                 Tr(L"Display {number} shows the bars in the tray: its taskbar is on a side of the "
                                    L"screen.") :
                             fallback.conflict ?
                                 Tr(L"Display {number} shows the bars in the tray: another mod controls the space they "
                                    L"need. Restart Explorer to retry.") :
                                 unavailable,
                             {{L"number", fallback.display}});
        }
        if (!note.empty()) {
            y = UiAddInfoBar(g, items, id, SettingsControlId(SettingsField::Placement, 8), x, y + 3, width, {{note}},
                             UiSeverity::Caution);
        }
    }
    // Taskbars on a left or right edge: what changes there, a line per display.
    if (!view.status.sideTaskbars.empty()) {
        std::wstring note;
        bool unsupported = false;
        for (const SettingsSideTaskbar& taskbar : view.status.sideTaskbars) {
            if (!note.empty()) note += L"\n";
            note += I18nText(locale,
                             taskbar.unsupported ?
                                 Tr(L"Display {number}\u2019s taskbar is on a side of the screen: its tray takes no "
                                    L"bars on this Windows build. Update Windows to show them.") :
                                 Tr(L"Display {number}\u2019s taskbar is on a side of the screen: accounts stack down "
                                    L"it, each style rearranged to fit its width. The preview shows them across a "
                                    L"bottom taskbar."),
                             {{L"number", taskbar.display}});
            unsupported = unsupported || taskbar.unsupported;
        }
        y = UiAddInfoBar(g, items, id, SettingsControlId(SettingsField::Placement, 9), x, y + 3, width, {{note}},
                         unsupported ? UiSeverity::Caution : UiSeverity::Info);
    }

    // Account identity is per account: a card that goes to Accounts. Fixed ids past the gallery's
    // (its five items' within the field's 16), so focus stays on it when cards above it change.
    auto identityCard = [&](float top) {
        UiCardSpec spec;
        spec.glyph = L"\uE716";
        spec.title = text(Tr(L"Account identity is set per account in Accounts"));
        spec.description = {{text(Tr(L"Logo + number badge, logo only, or label."))}};
        spec.trailingGlyph = L"\uE76C";
        spec.action = {UiActionKind::Navigate, int(SettingsUiPage::Accounts)};
        spec.minHeight = 62;
        static_assert(std::size(kSettingsTileStyles) <= 10);
        uint32_t cardId = SettingsControlId(SettingsField::BarLayout, 10);
        return UiAddCard(g, items, cardId, x, top, width, spec).card.bottom;
    };
    const std::wstring unplaced = text(Tr(L"Choose an identity position to change this."));

    // Logo identities in every style: the logo size and the colors of the logos that have brand
    // colors, the badge's size and color, then its offsets. Bars need an identity position, except
    // for the logo colors, which the flyout and the windows also show.
    auto logoCards = [&](float top, bool placed) {
        auto slider = [&](float at, I18nKey title, I18nKey description, SettingsField field) {
            UiCardSpec spec;
            spec.title = text(title);
            spec.description = {{text(description)}};
            spec.enabled = placed;
            if (!placed) spec.reason = unplaced;
            return SettingsAddSliderCard(g, view, items, id, x, at, width, spec, field);
        };
        auto brand = [&](float at, I18nKey title, I18nKey description, SettingsField field, I18nKey colors) {
            UiCardSpec spec;
            spec.title = text(title);
            spec.description = {{text(description)}};
            return SettingsAddDropdownCard(g, view, items, id, x, at, width, spec, field, 200,
                                           {{text(TrCtx(L"logo-color", L"Taskbar text color")), 0}, {text(colors), 1}},
                                           SettingsFieldValue(s, field));
        };
        top = slider(top, Tr(L"Logo size"), Tr(L"Percent of the standard size; rings keep it inside the hole."),
                     SettingsField::LogoScale);
        top = brand(top + 3, Tr(L"Claude logo"),
                    Tr(L"The taskbar\u2019s text color, or Claude\u2019s orange. Details, Accounts and usage history "
                       L"match."),
                    SettingsField::ClaudeLogoBrand, TrCtx(L"logo-color", L"Orange"));
        top = brand(top + 3, Tr(L"Antigravity logo"),
                    Tr(L"The Gemini sparkle in the taskbar\u2019s text color, or its purple-to-blue gradient."),
                    SettingsField::GeminiLogoBrand, TrCtx(L"logo-color", L"Gemini gradient"));
        top = slider(top + 3, Tr(L"Badge size"),
                     Tr(L"Text size of the number on a logo when several accounts share a provider, in pixels."),
                     SettingsField::BadgeSize);
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Badge color"));
            spec.description = {{text(Tr(L"White, the default, follows the taskbar\u2019s text color. Custom... "
                                         L"opens the color picker."))}};
            spec.enabled = placed;
            if (!placed) spec.reason = unplaced;
            top = SettingsAddColorCard(g, view, items, id, x, top + 3, width, spec, SettingsField::BadgeColor,
                                       SettingsField::BadgeColorCustom,
                                       {std::begin(kSettingsBadgeColors), std::end(kSettingsBadgeColors)},
                                       s.badgeColor & 0x00FFFFFF);
        }
        top = slider(top + 3, Tr(L"Badge horizontal offset"),
                     Tr(L"Pixels right, or left when negative, from the logo\u2019s lower-right corner (inside the "
                        L"hole on rings)."),
                     SettingsField::BadgeOffsetX);
        return slider(top + 3, Tr(L"Badge vertical offset"), Tr(L"Pixels down, or up when negative."),
                      SettingsField::BadgeOffsetY);
    };

    if (bars) {
        y = section(y + 20, TrCtx(L"settings-section", L"Bars"));
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Bar style"));
            spec.description = {{text(Tr(L"A full track behind each fill, or a thin rail under it."))}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::BarStyle, 200,
                                        {{text(TrCtx(L"bar-style", L"Full track")), int(BarStyle::Track)},
                                         {text(TrCtx(L"bar-style", L"Thin rail")), int(BarStyle::Rail)}},
                                        int(s.barStyle));
        }
        // Stacked and vertical bars each keep their own length.
        if (layout == BarLayout::Vertical) {
            UiCardSpec spec;
            spec.title = text(Tr(L"Vertical bar length"));
            spec.description = {{text(Tr(L"Pixels up each bar."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::VerticalBarLength);
        } else {
            UiCardSpec spec;
            spec.title = text(Tr(L"Bar length"));
            spec.description = {{text(Tr(L"Pixels along each bar."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::BarLength);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Bar thickness"));
            spec.description = {{text(Tr(L"Pixels across each bar."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::BarThickness);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Bar gap"));
            spec.description = {{text(Tr(L"Space between an account\u2019s bars."))}};
            y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::BarGap, pixels);
        }

        // Identity and text: the link to each account's identity, where it sits; the Label form's
        // size, the gap and the logo cards need a position. Then quota keys, and the bar text size
        // for them or percentage text.
        y = section(y + 20, TrCtx(L"settings-section", L"Identity and text"));
        bool placed = s.labelPosition != LabelPosition::Hidden;
        y = identityCard(y);
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Identity position"));
            spec.description = {{text(Tr(L"Where each account\u2019s identity sits next to its bars."))}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::LabelPosition, 200,
                                        {{text(TrCtx(L"identity-position", L"Left")), int(LabelPosition::Left)},
                                         {text(TrCtx(L"identity-position", L"Right")), int(LabelPosition::Right)},
                                         {text(TrCtx(L"identity-position", L"Top")), int(LabelPosition::Top)},
                                         {text(TrCtx(L"identity-position", L"Bottom")), int(LabelPosition::Bottom)},
                                         {text(TrCtx(L"identity-position", L"Hidden")), int(LabelPosition::Hidden)}},
                                        int(s.labelPosition));
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Label font size"));
            spec.description = {{text(Tr(L"Text of accounts whose identity is Label, in pixels."))}};
            spec.enabled = placed;
            if (!placed) spec.reason = unplaced;
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::LabelFontSize);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Identity gap"));
            spec.description = {{text(Tr(L"Space between the identity and the bars."))}};
            spec.enabled = placed;
            if (!placed) spec.reason = unplaced;
            y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::LabelGap, pixels);
        }
        y = logoCards(y + 3, placed);
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Quota keys"));
            spec.description = {{I18nText(locale, Tr(L"Short key beside each bar: {key5h}, {key7d}, {keyFa}, {keyAr}, "
                                                     L"{keyEx}, {keyCr}, {keyMo}."),
                                          {{L"key5h", key(0)}, {L"key7d", key(1)}, {L"keyFa", key(2)}, {L"keyAr", key(4)},
                                           {L"keyEx", key(3)}, {L"keyCr", key(5)}, {L"keyMo", key(6)}})}};
            y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec, SettingsField::ShowBarLabels);
        }
        {
            bool shown = s.showBarLabels || s.percentTextVisibility != PercentTextVisibility::Never;
            UiCardSpec spec;
            spec.title = text(Tr(L"Bar text size"));
            spec.description = {{text(Tr(L"Percentage text and quota keys. Percentage text is set in Display."))}};
            spec.enabled = shown;
            if (!shown) spec.reason = text(Tr(L"Turn on Quota keys, or percentage text in Display, to change this."));
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PercentFontSize);
        }
    } else if (IsFaceTile(layout)) {
        // Face tiles share the ring size and thickness; pies are solid.
        const bool gauge = layout == BarLayout::Gauge, pies = layout == BarLayout::ClockPies;
        const bool square = layout == BarLayout::RoundedSquare;
        y = section(y + 20, gauge  ? TrCtx(L"settings-section", L"Gauge") :
                            pies   ? TrCtx(L"settings-section", L"Pies") :
                            square ? TrCtx(L"settings-section", L"Square") :
                                     TrCtx(L"settings-section", L"Ring"));
        {
            UiCardSpec spec;
            spec.title = text(gauge ? Tr(L"Gauge size") : pies ? Tr(L"Pie area") : square ? Tr(L"Square size") :
                                                                                            Tr(L"Ring size"));
            spec.description = {{text(pies   ? Tr(L"Height in pixels; one pie per quota, smaller from the third on.") :
                                      square ? Tr(L"Side in pixels.") :
                                               Tr(L"Diameter in pixels."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y, width, spec, SettingsField::RingSize);
        }
        if (!pies) {
            UiCardSpec spec;
            spec.title = text(gauge ? Tr(L"Gauge thickness") : square ? Tr(L"Square thickness") : Tr(L"Ring thickness"));
            spec.description = {{text(gauge ? Tr(L"Stroke width in pixels. Narrows automatically when the arc "
                                                 L"doesn\u2019t fit around the number.") :
                                              Tr(L"Stroke width in pixels. Narrows automatically when the meters "
                                                 L"don\u2019t fit around the logo or text."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::RingThickness);
        }
        y = section(y + 20, TrCtx(L"settings-section", L"Identity"));
        y = logoCards(identityCard(y) + 3, true);
    } else if (layout == BarLayout::Sparkline) {
        // Usage graphs: each chart's width and height, its text size, the extra usage on the weekly
        // chart, the long windows' span and its swap on hover, then the current position cues and
        // the reading's size.
        y = section(y + 20, kSettingsTileStyles[int(BarLayout::Sparkline)]);
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Chart width"));
            spec.description = {{text(Tr(L"Pixels across each quota\u2019s chart."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y, width, spec, SettingsField::SparklineWidth);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Chart height"));
            spec.description = {{text(Tr(L"Percent of the tile\u2019s height, from 0% to 100% used, or left in "
                                         L"Remaining mode."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineHeight);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Text size"));
            spec.description = {{I18nText(locale, Tr(L"Quota keys ({key5h}, {key7d}, ...) and extra usage on the "
                                                     L"charts, in pixels."),
                                          {{L"key5h", key(0)}, {L"key7d", key(1)}})}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineTextSize);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Chart outline"));
            spec.description = {{text(Tr(L"A faint border around each chart."))}};
            y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineOutline);
        }
        {
            UiCardSpec spec;
            spec.title = text(TrCtx(L"quota-name", L"Extra usage"));
            spec.description = {{I18nText(locale,
                                          Tr(L"Anthropic extra usage or OpenAI credits in the {key7d} chart\u2019s "
                                             L"top-left corner (bottom-left in Remaining mode), when selected for the "
                                             L"account and reported: spent in Used mode, left in Remaining mode. "
                                             L"Percent shows the amount without a limit. OpenAI credits never get a "
                                             L"chart of their own."),
                                          {{L"key7d", key(1)}})}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineExtra, 200,
                                        {{text(TrCtx(L"sparkline-extra", L"Off")), int(SparklineExtra::Off)},
                                         {text(TrCtx(L"sparkline-extra", L"Amount")), int(SparklineExtra::Amount)},
                                         {text(TrCtx(L"sparkline-extra", L"Percent")), int(SparklineExtra::Percent)}},
                                        int(s.sparklineExtra));
        }
        // The long windows' spans, after Off on hover.
        auto spans = [&](bool off) {
            std::vector<UiOption> options;
            if (off) options.push_back({text(TrCtx(L"sparkline-span", L"Off")), -1});
            options.push_back({text(TrCtx(L"sparkline-span", L"Whole window")), int(SparklineSpan::Window)});
            const std::pair<int, SparklineSpan> hours[] = {{12, SparklineSpan::Hours12},
                                                           {24, SparklineSpan::Hours24},
                                                           {36, SparklineSpan::Hours36},
                                                           {48, SparklineSpan::Hours48}};
            for (auto [count, span] : hours) {
                options.push_back(
                    {I18nText(locale,
                              TrCtx(L"sparkline-span", L"{hours, plural, one{# hour each side} other{# hours each side}}"),
                              {{L"hours", count}}),
                     int(span)});
            }
            return options;
        };
        y = section(y + 20, TrCtx(L"settings-section", L"Time span"));
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Long windows"));
            spec.description = {{I18nText(locale,
                                          Tr(L"Weekly and monthly charts ({key7d}, {keyFa}, {keyAr}, {keyEx}, {keyMo}): "
                                             L"the whole window, or 12 to 48 hours before and after now."),
                                          {{L"key7d", key(1)}, {L"keyFa", key(2)}, {L"keyAr", key(4)},
                                           {L"keyEx", key(3)}, {L"keyMo", key(6)}})}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::SparklineSpan, 200,
                                        spans(false), int(s.sparklineSpan));
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"On hover"));
            spec.description = {{text(Tr(L"What those charts show while the pointer rests on the account."))}};
            y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineHoverSpan, 200,
                                        spans(true), SettingsFieldValue(s, SettingsField::SparklineHoverSpan));
        }
        y = section(y + 20, TrCtx(L"settings-section", L"Current position"));
        struct Cue {
            I18nKey title, description;
            SettingsField field;
        };
        static constexpr Cue kCues[] = {
            {Tr(L"Elapsed time"), Tr(L"Lights the part of the quota\u2019s window that has passed; the rest stays dim."),
             SettingsField::SparklineElapsed},
            {Tr(L"Now line"), Tr(L"A thin line across the chart at the current time."), SettingsField::SparklineNowLine},
            {Tr(L"Now caret"), Tr(L"A small triangle on the chart\u2019s bottom edge at the current time."),
             SettingsField::SparklineNowCaret},
            {Tr(L"Ringed reading"),
             Tr(L"A ring around the current reading, cut out of the lines under it. Off: a plain dot."),
             SettingsField::SparklineRing},
        };
        for (size_t k = 0; k < std::size(kCues); k++) {
            UiCardSpec spec;
            spec.title = text(kCues[k].title);
            spec.description = {{text(kCues[k].description)}};
            y = SettingsAddToggleCard(g, view, items, id, x, k ? y + 3 : y, width, spec, kCues[k].field);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Reading size"));
            spec.description = {{text(Tr(L"Percent of the current reading\u2019s standard size: the ring and its dot, "
                                         L"or the plain dot."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::SparklineReadingScale);
        }
        y = section(y + 20, TrCtx(L"settings-section", L"Identity"));
        y = logoCards(identityCard(y) + 3, true);
    } else if (layout == BarLayout::QuotaMatrix) {
        // Quota matrix: the numbers' size, the headline's.
        y = section(y + 20, TrCtx(L"settings-section", L"Matrix"));
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Number size"));
            spec.description = {{text(Tr(L"Text size of each quota\u2019s percentage, in pixels; its key scales with "
                                         L"it. Two rows shrink to fit the taskbar."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y, width, spec, SettingsField::HeadlineFontSize);
        }
        y = section(y + 20, TrCtx(L"settings-section", L"Identity"));
        y = logoCards(identityCard(y) + 3, true);
    } else {
        // Headline tiles: the number's size, the countdown, then its bars' sizes and the bars'
        // gap, which the countdown replaces.
        y = section(y + 20, TrCtx(L"settings-section", L"Headline"));
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Number size"));
            spec.description = {{text(Tr(L"Text size of the most-used quota\u2019s percentage, in pixels; its quota "
                                         L"key scales with it."))}};
            y = SettingsAddSliderCard(g, view, items, id, x, y, width, spec, SettingsField::HeadlineFontSize);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Reset countdown"));
            spec.description = {{text(Tr(L"Time until that quota resets under the number, instead of each "
                                         L"quota\u2019s bar."))}};
            y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HeadlineCountdown);
        }
        const bool segments = !s.headlineCountdown;
        const std::wstring countdownOn = text(Tr(L"Turn off Reset countdown to change this."));
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Bar length"));
            spec.description = {{text(Tr(L"Pixels along each quota\u2019s bar under the number."))}};
            spec.enabled = segments;
            if (!segments) spec.reason = countdownOn;
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HeadlineBarLength);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Bar thickness"));
            spec.description = {{text(Tr(L"Pixels across each bar."))}};
            spec.enabled = segments;
            if (!segments) spec.reason = countdownOn;
            y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HeadlineBarThickness);
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Bar gap"));
            spec.description = {{text(Tr(L"Space between an account\u2019s bars."))}};
            spec.enabled = segments;
            if (!segments) spec.reason = countdownOn;
            y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::BarGap, pixels);
        }
        y = section(y + 20, TrCtx(L"settings-section", L"Identity"));
        y = logoCards(identityCard(y) + 3, true);
    }

    // Pace markers: none on headline tiles or the quota matrix; the marker style is the bars'.
    // Style and color need the markers on.
    if (layout != BarLayout::LogoHeadline && layout != BarLayout::QuotaMatrix) {
        y = section(y + 20, TrCtx(L"settings-section", L"Pace markers"));
        bool on = s.showPaceTicks;
        const std::wstring off = text(Tr(L"Turn on Pace markers to change this."));
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Pace markers"));
            spec.description = {{text(layout == BarLayout::Sparkline ?
                                          Tr(L"A diagonal marks even use of the quota across its window, to compare "
                                             L"with its usage.") :
                                          Tr(L"Ticks mark how much of each quota window has elapsed, to compare with "
                                             L"its usage."))}};
            y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::ShowPaceTicks);
        }
        if (bars) {
            UiCardSpec spec;
            spec.title = text(Tr(L"Marker style"));
            spec.description = {{text(Tr(L"Thin caret, full-width line, edge notch, centered dot, or a time track: a "
                                         L"small bar under each bar that fills with the elapsed time."))}};
            spec.enabled = on;
            if (!on) spec.reason = off;
            y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PaceTickStyle, 200,
                                        {{text(TrCtx(L"pace-marker", L"Caret")), int(PaceTickStyle::Caret)},
                                         {text(TrCtx(L"pace-marker", L"Full line")), int(PaceTickStyle::Line)},
                                         {text(TrCtx(L"pace-marker", L"Edge notch")), int(PaceTickStyle::Notch)},
                                         {text(TrCtx(L"pace-marker", L"Dot")), int(PaceTickStyle::Dot)},
                                         {text(TrCtx(L"pace-marker", L"Time track")), int(PaceTickStyle::TimeTrack)}},
                                        int(s.paceTickStyle));
        }
        {
            UiCardSpec spec;
            spec.title = text(Tr(L"Marker color"));
            spec.description = {{text(Tr(L"Light grey by default, dark on a light taskbar. Custom... opens the color "
                                         L"picker."))}};
            spec.enabled = on;
            if (!on) spec.reason = off;
            y = SettingsAddColorCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PaceTickColor,
                                     SettingsField::PaceTickColorCustom,
                                     {std::begin(kSettingsPaceColors), std::end(kSettingsPaceColors)},
                                     s.paceTickColor & 0x00FFFFFF);
        }
    }

    y = section(y + 20, TrCtx(L"settings-section", L"Spacing"));
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Account margin"));
        spec.description = {{text(Tr(L"Space on each side of every account."))}};
        y = SettingsAddNumberCard(g, view, items, id, x, y, width, spec, SettingsField::AccountMargin, pixels);
    }
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Gap after accounts"));
        spec.description = {{text(Tr(L"Space after the last account, before whatever comes next on the taskbar."))}};
        y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::RightMargin, pixels);
    }

    UiCardSpec reset;
    reset.title = text(Tr(L"Reset Layout to defaults"));
    reset.description = {{text(Tr(L"Tile style, bar style, sizes, identity and text, pace markers, position and "
                                  L"spacing. Accounts are not changed."))}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset, SettingsField::ResetLayout,
                                 text(TrCtx(L"button", L"Reset")));
}
