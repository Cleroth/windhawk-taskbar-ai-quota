// Settings > Display: the material of the details flyout and the windows, what the meters show,
// their usage colors and thresholds, the stale and outage marks and the OpenAI details (approved mockup
// display.png, Appearance aside). The preview card is in the shell's pinned header
// (settings/preview.h). Pure builder.

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildDisplay(UiGraphics& g, const SettingsView& view, std::vector<UiItem>& items,
                                    uint32_t& id, float x, float y, float width) {
    const Settings& s = view.settings;
    const I18nLocale& locale = view.locale;
    const int style = std::clamp(int(s.barLayout), 0, int(std::size(kSettingsTileStyles)) - 1);
    auto text = [&](I18nKey k) { return I18nText(locale, k); };
    auto section = [&](float top, I18nKey k) { return UiAddSection(items, id, x, top, width, text(k)); };

    // Appearance: the material stays choosable without transparency effects; its notes say how
    // it shows then, and what the windows use for Tinted wallpaper, each on its own line.
    y = section(y + 16, TrCtx(L"settings-section", L"Appearance"));
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Material"));
        spec.description = {{text(Tr(L"Background of the details flyout and the settings and usage history windows."))}};
        if (!view.transparencyEffects) {
            spec.description[0].text += L"\n" + text(Tr(L"Windows transparency effects are off, so it shows as Solid."));
        }
        if (s.material == Material::TintedWallpaper) {
            spec.description[0].text += L"\n" + text(Tr(L"The settings and usage history windows use Mica for it."));
        }
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::Material, 200,
                                    {{text(TrCtx(L"material", L"Acrylic")), int(Material::Acrylic)},
                                     {text(TrCtx(L"material", L"Mica")), int(Material::Mica)},
                                     {text(TrCtx(L"material", L"Mica Alt")), int(Material::MicaAlt)},
                                     {text(TrCtx(L"material", L"Tinted wallpaper")), int(Material::TintedWallpaper)},
                                     {text(TrCtx(L"material", L"Solid")), int(Material::Solid)}},
                                    int(s.material));
    }

    // Values: the bar mode applies to every style. Percentage text is the bars' own; its
    // alignment and the amounts that replace it need it shown.
    y = section(y + 20, TrCtx(L"settings-section", L"Values"));
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Bar mode"));
        spec.description = {{text(Tr(L"Used fills as quota is consumed; Remaining fills with what is left. Colors "
                                     L"always follow usage."))}};
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::BarMode, 200,
                                    {{text(TrCtx(L"bar-mode", L"Used")), int(BarMode::Used)},
                                     {text(TrCtx(L"bar-mode", L"Remaining")), int(BarMode::Remaining)}},
                                    int(s.barMode));
    }
    const bool bars = !IsTileLayout(BarLayout(style));
    const bool shown = bars && s.percentTextVisibility != PercentTextVisibility::Never;
    const std::wstring barsOnly = I18nText(locale, Tr(L"Stacked and Vertical bars only. Current tile style: {style}."),
                                           {{L"style", text(kSettingsTileStyles[style])}});
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Percentage text"));
        spec.description = {{text(Tr(L"Percentage on each bar: never, on hover, or always."))}};
        spec.enabled = bars;
        if (!bars) spec.reason = barsOnly;
        y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PercentTextVisibility,
                                    200,
                                    {{text(TrCtx(L"percent-text", L"Never show")), int(PercentTextVisibility::Never)},
                                     {text(TrCtx(L"percent-text", L"Show on hover")), int(PercentTextVisibility::Hover)},
                                     {text(TrCtx(L"percent-text", L"Always show")), int(PercentTextVisibility::Always)}},
                                    int(s.percentTextVisibility));
    }
    const std::wstring textOff = text(Tr(L"Show percentage text on hover or always to change this."));
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Percentage alignment"));
        spec.description = {{text(Tr(L"Adaptive: right side, moving left once the bar reaches the yellow threshold."))}};
        spec.enabled = shown;
        if (!shown) spec.reason = bars ? textOff : barsOnly;
        y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PercentTextAlignment,
                                    200,
                                    {{text(TrCtx(L"percent-alignment", L"Adaptive")), int(PercentTextAlignment::Adaptive)},
                                     {text(TrCtx(L"percent-alignment", L"Left")), int(PercentTextAlignment::Left)},
                                     {text(TrCtx(L"percent-alignment", L"Center")), int(PercentTextAlignment::Center)},
                                     {text(TrCtx(L"percent-alignment", L"Right")), int(PercentTextAlignment::Right)}},
                                    int(s.percentTextAlignment));
    }
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Amounts on extra usage and credits"));
        spec.description = {{text(Tr(L"$ or credits instead of % on Anthropic extra usage, OpenAI credits and GitHub "
                                     L"Copilot AI credits: spent in Used mode, left in Remaining mode."))}};
        spec.enabled = shown;
        if (!shown) spec.reason = bars ? textOff : barsOnly;
        y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec, SettingsField::ShowExtraBarAmounts);
    }

    // Usage colors: the palette, and Custom's color per band, each offering the band's default and
    // colorblind colors. Each threshold's dot is its band's usage color (taskbar/meter-model.h) in
    // the chosen palette, taken from thresholds 1, 2 and 3 so equal thresholds still show their
    // own. The level threshold notifications use (Behavior) says so in its card's own text.
    y = section(y + 20, TrCtx(L"settings-section", L"Usage colors"));
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Palette"));
        spec.description = {{text(Tr(L"Default: green, yellow, orange and red. Colorblind: blue to vermilion. Custom: "
                                     L"a color you pick for each threshold."))}};
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::UsagePalette, 200,
                                    {{text(TrCtx(L"palette", L"Default")), int(UsagePalette::Default)},
                                     {text(TrCtx(L"palette", L"Colorblind")), int(UsagePalette::Colorblind)},
                                     {text(TrCtx(L"palette", L"Custom")), int(UsagePalette::Custom)}},
                                    int(s.usagePalette));
    }
    if (s.usagePalette == UsagePalette::Custom) {
        const I18nKey bands[] = {TrCtx(L"usage-band", L"Below yellow"), TrCtx(L"usage-band", L"From yellow"),
                                 TrCtx(L"usage-band", L"From orange"), TrCtx(L"usage-band", L"From red")};
        for (int i = 0; i < 4; i++) {
            UiCardSpec spec;
            spec.title = text(bands[i]);
            y = SettingsAddColorCard(g, view, items, id, x, y + 3, width, spec,
                                     SettingsField(int(SettingsField::UsageColorBelowYellow) + i),
                                     SettingsField::UsageColorCustom, {kDefaultUsageColors[i], kColorblindUsageColors[i]},
                                     s.customUsageColors[i] & 0x00FFFFFF, i);
        }
    }
    // Each threshold's text alone, and as a whole with the notifications' level.
    struct Threshold {
        SettingsField field;
        I18nKey title;
        std::optional<I18nKey> description;
        I18nKey notifying;
    };
    const Threshold yellow =
        s.usagePalette == UsagePalette::Colorblind ?
            Threshold{SettingsField::YellowThreshold, Tr(L"Yellow from"), Tr(L"Usage below this is blue."),
                      Tr(L"Usage below this is blue. Also the level for threshold notifications.")} :
        s.usagePalette == UsagePalette::Custom ?
            Threshold{SettingsField::YellowThreshold, Tr(L"Yellow from"),
                      Tr(L"Usage below this takes the Below yellow color."),
                      Tr(L"Usage below this takes the Below yellow color. Also the level for threshold notifications.")} :
            Threshold{SettingsField::YellowThreshold, Tr(L"Yellow from"), Tr(L"Usage below this is green."),
                      Tr(L"Usage below this is green. Also the level for threshold notifications.")};
    const Threshold thresholds[] = {
        yellow,
        {SettingsField::OrangeThreshold, Tr(L"Orange from"), Tr(L"Thresholds stay in order: yellow, orange, red."),
         Tr(L"Thresholds stay in order: yellow, orange, red. Also the level for threshold notifications.")},
        {SettingsField::RedThreshold, Tr(L"Red from"), std::nullopt, Tr(L"Also the level for threshold notifications.")},
    };
    const std::array<uint32_t, 4> colors = UsagePaletteColors(s.usagePalette, s.customUsageColors);
    for (int i = 0; i < 3; i++) {
        const Threshold& threshold = thresholds[i];
        uint32_t color = UsageColor(i + 1, false, 1, 2, 3, colors);
        UiCardSpec spec;
        spec.dot = RGB(color >> 16 & 0xFF, color >> 8 & 0xFF, color & 0xFF);
        spec.title = text(threshold.title);
        if (int(s.notificationLevel) == i + 1) spec.description = {{text(threshold.notifying)}};
        else if (threshold.description) spec.description = {{text(*threshold.description)}};
        y = SettingsAddSliderCard(g, view, items, id, x, y + 3, width, spec, threshold.field);
    }

    y = section(y + 20, TrCtx(L"settings-section", L"Status"));
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Mark stale data"));
        spec.description = {{text(Tr(L"Adds ! when a refresh failed or is overdue."))}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::ShowStaleWarning);
    }
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Show provider outages"));
        spec.description = {{text(Tr(L"A warning triangle on an account while its provider's status page reports a "
                                     L"problem with a service it uses: in the orange usage color for degraded "
                                     L"service, a partial outage or maintenance, the red one for a major outage. Off: "
                                     L"no status pages are read."))}};
        y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec, SettingsField::ShowProviderOutages);
    }

    y = section(y + 20, TrCtx(L"settings-section", L"Details flyout"));
    {
        UiCardSpec spec;
        spec.title = text(Tr(L"Additional OpenAI rate limits"));
        spec.description = {{text(Tr(L"Codex Spark, hidden model lanes such as gpt-reserve, and the Spark plan name in "
                                     L"OpenAI details."))}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::ShowOpenAiExtraLimits);
    }

    UiCardSpec reset;
    reset.title = text(Tr(L"Reset Display to defaults"));
    reset.description = {{text(Tr(L"Material, values, usage colors, status marks and flyout details."))}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset, SettingsField::ResetDisplay,
                                 text(TrCtx(L"button", L"Reset")));
}
