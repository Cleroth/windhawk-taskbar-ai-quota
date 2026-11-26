// Settings > Behavior: this user's language, where the bars show, hover details, the work days of
// pace and projections, polling (hidden accounts' too) and notifications (approved mockup
// behavior.png, the work days aside). Pure builder.

static constexpr int kSettingsPollPresets[] = {2, 5, 10, 15, 30, 60};
// Hidden accounts' intervals, in minutes, and the dropdown's value for Custom.
static constexpr int kSettingsHiddenPollPresets[] = {30, 60, 120, 240, 720, 1440};
static constexpr int kSettingsHiddenPollCustom = -2;

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

    // Pace and projection: the week's work days, Monday first. The days need the switch on.
    y = UiAddSection(items, id, x, y + 20, width, I18nText(locale, TrCtx(L"settings-section", L"Pace and projection")));
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Work days only"));
        spec.description = {{I18nText(locale, Tr(L"Pace and projections count only your work days and stay level "
                                                 L"through your days off, for quotas whose window is longer than a "
                                                 L"day. Usage on a day off still counts, and reset times stay as "
                                                 L"they are."))}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::WorkDays);
    }
    const std::vector<UiOption> weekdays = {{I18nText(locale, TrCtx(L"weekday", L"Monday")), 1},
                                            {I18nText(locale, TrCtx(L"weekday", L"Tuesday")), 2},
                                            {I18nText(locale, TrCtx(L"weekday", L"Wednesday")), 3},
                                            {I18nText(locale, TrCtx(L"weekday", L"Thursday")), 4},
                                            {I18nText(locale, TrCtx(L"weekday", L"Friday")), 5},
                                            {I18nText(locale, TrCtx(L"weekday", L"Saturday")), 6},
                                            {I18nText(locale, TrCtx(L"weekday", L"Sunday")), 0}};
    for (SettingsField field : {SettingsField::FirstWorkDay, SettingsField::LastWorkDay}) {
        UiCardSpec spec;
        if (field == SettingsField::FirstWorkDay) {
            spec.title = I18nText(locale, Tr(L"First work day"));
            spec.description = {{I18nText(locale, Tr(L"Your work days run from this day through the last one."))}};
        } else {
            spec.title = I18nText(locale, Tr(L"Last work day"));
        }
        spec.enabled = s.workDays;
        if (!s.workDays) spec.reason = I18nText(locale, Tr(L"Turn on Work days only to change this."));
        y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, field, 200, weekdays,
                                    SettingsFieldValue(s, field));
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
    // Hidden accounts: not polled, polled as the others, or at a slower interval: a preset, or
    // Custom with its own number, as the cloud interval's.
    bool hiddenPreset = std::find(std::begin(kSettingsHiddenPollPresets), std::end(kSettingsHiddenPollPresets),
                                  s.hiddenPollMinutes) != std::end(kSettingsHiddenPollPresets);
    const SettingsDraft* hiddenTyped = SettingsViewDraft(view, SettingsField::HiddenPollMinutes);
    bool hiddenCustom = s.hiddenPollMinutes > 0 &&
                        (view.hiddenPollCustom || !hiddenPreset || (hiddenTyped && hiddenTyped->text));
    {
        std::vector<UiOption> options{
            {I18nText(locale, TrCtx(L"hidden-polling", L"Not polled")), kHiddenPollNever},
            {I18nText(locale, TrCtx(L"hidden-polling", L"Same as visible accounts")), kHiddenPollAsShown}};
        for (int minutes : kSettingsHiddenPollPresets) {
            options.push_back(
                {minutes % 60 ?
                     I18nText(locale, TrCtx(L"poll-interval", L"{minutes, plural, one{# minute} other{# minutes}}"),
                              {{L"minutes", minutes}}) :
                     I18nText(locale, TrCtx(L"poll-interval", L"{hours, plural, one{# hour} other{# hours}}"),
                              {{L"hours", minutes / 60}}),
                 minutes});
        }
        options.push_back({I18nText(locale, TrCtx(L"poll-interval", L"Custom")), kSettingsHiddenPollCustom});
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Hidden accounts"));
        spec.description = {{I18nText(locale, Tr(L"How often accounts hidden from the taskbar are polled, for "
                                                 L"Accounts and usage history: never more often than visible ones. "
                                                 L"Shared across Windows users."))}};
        y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HiddenPolling, 200,
                                    std::move(options), hiddenCustom ? kSettingsHiddenPollCustom : s.hiddenPollMinutes);
    }
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Custom hidden interval"));
        spec.description = {{I18nText(locale, Tr(L"2 to 1440 minutes."))}};
        spec.enabled = hiddenCustom;
        if (!hiddenCustom) spec.reason = I18nText(locale, Tr(L"Choose Custom in Hidden accounts to edit."));
        // The unit agrees with the saved interval, not text still being typed.
        y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::HiddenPollMinutes,
                                  I18nText(locale, TrCtx(L"poll-interval-unit", L"{minutes, plural, one{minute} other{minutes}}"),
                                           {{L"minutes", SettingsFieldValue(s, SettingsField::HiddenPollMinutes)}}));
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
    reset.description = {
        {I18nText(locale, Tr(L"Language, displays, hover details, work days, polling and notifications."))}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset,
                                 SettingsField::ResetBehavior, I18nText(locale, TrCtx(L"button", L"Reset")));
}
