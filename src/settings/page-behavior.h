// Settings > Behavior: this user's language, where the bars show, hover details, polling and
// notifications (approved mockup behavior.png). Pure builder.

static constexpr int kSettingsPollPresets[] = {2, 5, 10, 15, 30, 60};

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildBehavior(UiGraphics& g, const SettingsView& view,
                                     std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                     float width) {
    const Settings& s = view.settings;
    const I18nLocale& locale = view.locale;

    // Language: Automatic with what it shows, then every language by its own name; the
    // pseudo-locale only while it is the stored choice.
    y = UiAddSection(items, id, x, y + 16, width, I18nText(locale, TrCtx(L"settings-section", L"Language")));
    {
        std::vector<UiOption> options{
            {I18nText(locale, TrCtx(L"language-choice", L"Automatic ({language})"),
                      {{L"language", view.status.automaticLanguage->nativeName}}),
             0}};
        for (size_t i = 0; i < std::size(kI18nLanguages); i++) {
            options.push_back({std::wstring(kI18nLanguages[i].nativeName), int(i) + 1});
        }
        int language = SettingsFieldValue(s, SettingsField::Language);
        if (language == kSettingsPseudoLanguage) {
            options.push_back({std::wstring(kI18nPseudoLanguage.nativeName), kSettingsPseudoLanguage});
        }
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Interface language"));
        spec.description = {{I18nText(locale, Tr(L"The settings and usage history windows and the taskbar. Automatic "
                                                 L"follows your Windows display language. Applies to this Windows user "
                                                 L"only."))}};
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::Language, 200,
                                    std::move(options), language);
    }

    // Taskbar: primary, all, each taskbar's display, and a chosen display that is gone.
    y = UiAddSection(items, id, x, y + 20, width, I18nText(locale, TrCtx(L"settings-section", L"Taskbar")));
    {
        std::vector<UiOption> options{{I18nText(locale, Tr(L"Primary display only")), 0},
                                      {I18nText(locale, Tr(L"All displays")), -1}};
        for (const SettingsDisplay& display : view.status.displays) {
            options.push_back(
                {display.primary ?
                     I18nText(locale, Tr(L"Display {number} - {width}x{height} (primary)"),
                              {{L"number", display.number}, {L"width", display.width}, {L"height", display.height}}) :
                     I18nText(locale, Tr(L"Display {number} - {width}x{height}"),
                              {{L"number", display.number}, {L"width", display.width}, {L"height", display.height}}),
                 display.number});
        }
        int showOn = SettingsFieldValue(s, SettingsField::ShowOn);
        if (std::none_of(options.begin(), options.end(),
                         [&](const UiOption& option) { return option.value == showOn; })) {
            options.push_back({I18nText(locale, Tr(L"Display {number} - unavailable"), {{L"number", showOn}}), showOn});
        }
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Show on"));
        spec.description = {{I18nText(locale, Tr(L"Primary display, all displays, or one display."))}};
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::ShowOn, 260,
                                    std::move(options), showOn);
    }

    // Details
    y = UiAddSection(items, id, x, y + 20, width, I18nText(locale, TrCtx(L"settings-section", L"Details")));
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Show details on hover"));
        spec.description = {{I18nText(locale, Tr(L"Opens an account\u2019s details after the pointer rests on it."))}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec,
                                  SettingsField::ShowDetailsOnHover);
    }

    // Updates: a preset interval, or Custom with its own number. Text typed into the custom box
    // keeps Custom even while a partial number matches a preset.
    y = UiAddSection(items, id, x, y + 20, width, I18nText(locale, TrCtx(L"settings-section", L"Updates")));
    bool preset = std::find(std::begin(kSettingsPollPresets), std::end(kSettingsPollPresets),
                            s.pollMinutes) != std::end(kSettingsPollPresets);
    const SettingsDraft* typed = SettingsViewDraft(view, SettingsField::PollMinutes);
    bool custom = view.pollCustom || !preset || (typed && typed->text);
    {
        std::vector<UiOption> options;
        for (int minutes : kSettingsPollPresets) {
            options.push_back({I18nText(locale, TrCtx(L"poll-interval", L"{minutes, plural, one{# minute} other{# minutes}}"),
                                        {{L"minutes", minutes}}),
                               minutes});
        }
        options.push_back({I18nText(locale, TrCtx(L"poll-interval", L"Custom")), 0});
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Cloud poll interval"));
        spec.description = {{I18nText(locale, Tr(L"How often Anthropic, OpenAI, GitHub Copilot and OpenCode Go are "
                                                 L"polled. Antigravity polls its local session every minute."))}};
        // It edits the same field as the custom box: a distinct control id.
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::PollMinutes,
                                    200, std::move(options), custom ? 0 : s.pollMinutes, 8);
    }
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Custom interval"));
        spec.description = {{I18nText(locale, Tr(L"2 to 1440 minutes."))}};
        spec.enabled = custom;
        if (!custom) spec.reason = I18nText(locale, Tr(L"Choose Custom in Cloud poll interval to edit."));
        // The unit agrees with the saved interval, not text still being typed.
        y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PollMinutes,
                                  I18nText(locale, TrCtx(L"poll-interval-unit", L"{minutes, plural, one{minute} other{minutes}}"),
                                           {{L"minutes", s.pollMinutes}}));
    }
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Do not poll hidden accounts"));
        spec.description = {{I18nText(locale, Tr(L"Hidden accounts are neither polled nor recorded, even with Record "
                                                 L"history on. Turn off to keep recording them. Shared across Windows "
                                                 L"users."))}};
        y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec,
                                  SettingsField::SkipHiddenPolling);
    }

    // Alerts: each level with its threshold from Display.
    y = UiAddSection(items, id, x, y + 20, width, I18nText(locale, TrCtx(L"settings-section", L"Alerts")));
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Threshold notifications"));
        spec.description = {{I18nText(locale, Tr(L"Notifies when a taskbar quota first reaches the chosen threshold. "
                                                 L"Re-arms after it drops below."))}};
        y = SettingsAddDropdownCard(
            g, view, items, id, x, y, width, spec, SettingsField::Notifications, 200,
            {{I18nText(locale, TrCtx(L"notification-level", L"Off")), int(NotificationLevel::Off)},
             {I18nText(locale, TrCtx(L"notification-level", L"Yellow ({threshold}%)"), {{L"threshold", s.yellowThreshold}}),
              int(NotificationLevel::Yellow)},
             {I18nText(locale, TrCtx(L"notification-level", L"Orange ({threshold}%)"), {{L"threshold", s.orangeThreshold}}),
              int(NotificationLevel::Orange)},
             {I18nText(locale, TrCtx(L"notification-level", L"Red ({threshold}%)"), {{L"threshold", s.redThreshold}}),
              int(NotificationLevel::Red)}},
            int(s.notificationLevel));
    }

    UiCardSpec reset;
    reset.title = I18nText(locale, Tr(L"Reset Behavior to defaults"));
    reset.description = {{I18nText(locale, Tr(L"Language, displays, hover details, polling and notifications."))}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset,
                                 SettingsField::ResetBehavior, I18nText(locale, TrCtx(L"button", L"Reset")));
}
