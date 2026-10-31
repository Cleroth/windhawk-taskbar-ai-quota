// Settings > Display: what the meters show, their usage colors and thresholds, the stale marker
// and the OpenAI details (approved mockup display.png). The preview card is in the shell's
// pinned header (settings/preview.h). Pure builder.

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildDisplay(UiGraphics& g, const SettingsView& view, std::vector<UiItem>& items,
                                    uint32_t& id, float x, float y, float width) {
    const Settings& s = view.settings;
    const int style = std::clamp(int(s.barLayout), 0, int(std::size(kSettingsTileStyles)) - 1);

    // Values: the bar mode applies to every style. Percentage text is the bars' own; its
    // alignment and the amounts that replace it need it shown.
    y = UiAddSection(items, id, x, y + 16, width, L"Values");
    {
        UiCardSpec spec;
        spec.title = L"Bar mode";
        spec.description = {{L"Used fills as quota is consumed; Remaining fills with what is left. Colors "
                             L"always follow usage."}};
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::BarMode, 200,
                                    {{L"Used", int(BarMode::Used)}, {L"Remaining", int(BarMode::Remaining)}},
                                    int(s.barMode));
    }
    const bool bars = !IsTileLayout(BarLayout(style));
    const bool text = bars && s.percentTextVisibility != PercentTextVisibility::Never;
    const std::wstring barsOnly =
        L"Stacked and Vertical bars only. Current tile style: " + std::wstring(kSettingsTileStyles[style]) + L".";
    {
        UiCardSpec spec;
        spec.title = L"Percentage text";
        spec.description = {{L"Percentage on each bar: never, on hover, or always."}};
        spec.enabled = bars;
        if (!bars) spec.reason = barsOnly;
        y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PercentTextVisibility,
                                    200,
                                    {{L"Never show", int(PercentTextVisibility::Never)},
                                     {L"Show on hover", int(PercentTextVisibility::Hover)},
                                     {L"Always show", int(PercentTextVisibility::Always)}},
                                    int(s.percentTextVisibility));
    }
    PCWSTR textOff = L"Show percentage text on hover or always to change this.";
    {
        UiCardSpec spec;
        spec.title = L"Percentage alignment";
        spec.description = {{L"Adaptive: right side, moving left once the bar reaches yellow."}};
        spec.enabled = text;
        if (!text) spec.reason = bars ? textOff : barsOnly;
        y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PercentTextAlignment,
                                    200,
                                    {{L"Adaptive", int(PercentTextAlignment::Adaptive)},
                                     {L"Left", int(PercentTextAlignment::Left)},
                                     {L"Center", int(PercentTextAlignment::Center)},
                                     {L"Right", int(PercentTextAlignment::Right)}},
                                    int(s.percentTextAlignment));
    }
    {
        UiCardSpec spec;
        spec.title = L"Amounts on extra usage and credits";
        spec.description = {{L"$ or credits instead of % on Anthropic extra usage and OpenAI credits: spent in "
                             L"Used mode, left in Remaining mode."}};
        spec.enabled = text;
        if (!text) spec.reason = bars ? textOff : barsOnly;
        y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec, SettingsField::ShowExtraBarAmounts);
    }

    // Usage colors. Each threshold's dot is its band's usage color (taskbar/meter-model.h) in the
    // chosen palette, taken from thresholds 1, 2 and 3 so equal thresholds still show their own.
    y = UiAddSection(items, id, x, y + 20, width, L"Usage colors");
    {
        UiCardSpec spec;
        spec.title = L"Colorblind palette";
        spec.description = {{L"Blue to vermilion instead of green to red."}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::ColorblindMode);
    }
    const std::tuple<SettingsField, PCWSTR, PCWSTR> thresholds[] = {
        {SettingsField::YellowThreshold, L"Yellow from",
         s.colorblindMode ? L"Usage below this is blue." : L"Usage below this is green."},
        {SettingsField::OrangeThreshold, L"Orange from", L"Thresholds stay in order: yellow, orange, red."},
        {SettingsField::RedThreshold, L"Red from", L"Also the level for threshold notifications."},
    };
    for (int i = 0; i < 3; i++) {
        auto [field, title, description] = thresholds[i];
        uint32_t color = UsageColor(i + 1, false, 1, 2, 3, s.colorblindMode);
        UiCardSpec spec;
        spec.dot = RGB(color >> 16 & 0xFF, color >> 8 & 0xFF, color & 0xFF);
        spec.title = title;
        spec.description = {{description}};
        y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, field);
    }

    y = UiAddSection(items, id, x, y + 20, width, L"Status");
    {
        UiCardSpec spec;
        spec.title = L"Mark stale data";
        spec.description = {{L"Adds ! when a refresh failed or is overdue."}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::ShowStaleWarning);
    }

    y = UiAddSection(items, id, x, y + 20, width, L"Details flyout");
    {
        UiCardSpec spec;
        spec.title = L"Additional OpenAI rate limits";
        spec.description = {{L"Codex Spark, hidden model lanes such as gpt-reserve, and the Spark plan name in "
                             L"OpenAI details."}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::ShowOpenAiExtraLimits);
    }

    UiCardSpec reset;
    reset.title = L"Reset Display to defaults";
    reset.description = {{L"Values, usage colors, stale marker and flyout details."}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset, SettingsField::ResetDisplay, L"Reset");
}
