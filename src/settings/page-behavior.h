// Settings > Behavior: where the bars show, hover details, polling and notifications (approved
// mockup behavior.png). Pure builder.

static constexpr int kSettingsPollPresets[] = {2, 5, 10, 15, 30, 60};

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildBehavior(UiGraphics& g, const SettingsView& view,
                                     std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                     float width) {
    const Settings& s = view.settings;

    // Taskbar: primary, all, each taskbar's display, and a chosen display that is gone.
    y = UiAddSection(items, id, x, y + 16, width, L"Taskbar");
    {
        std::vector<UiOption> options{{L"Primary display only", 0}, {L"All displays", -1}};
        for (const SettingsDisplay& display : view.status.displays) {
            options.push_back({L"Display " + std::to_wstring(display.number) + L" - " +
                                   std::to_wstring(display.width) + L"x" +
                                   std::to_wstring(display.height) +
                                   (display.primary ? L" (primary)" : L""),
                               display.number});
        }
        int showOn = SettingsFieldValue(s, SettingsField::ShowOn);
        if (std::none_of(options.begin(), options.end(),
                         [&](const UiOption& option) { return option.value == showOn; })) {
            options.push_back({L"Display " + std::to_wstring(showOn) + L" - unavailable", showOn});
        }
        UiCardSpec spec;
        spec.title = L"Show on";
        spec.description = {{L"Primary display, all displays, or one display."}};
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::ShowOn, 260,
                                    std::move(options), showOn);
    }

    // Details
    y = UiAddSection(items, id, x, y + 20, width, L"Details");
    {
        UiCardSpec spec;
        spec.title = L"Show details on hover";
        spec.description = {{L"Opens an account\u2019s details after the pointer rests on it."}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec,
                                  SettingsField::ShowDetailsOnHover);
    }

    // Updates: a preset interval, or Custom with its own number. Text typed into the custom box
    // keeps Custom even while a partial number matches a preset.
    y = UiAddSection(items, id, x, y + 20, width, L"Updates");
    bool preset = std::find(std::begin(kSettingsPollPresets), std::end(kSettingsPollPresets),
                            s.pollMinutes) != std::end(kSettingsPollPresets);
    const SettingsDraft* typed = SettingsViewDraft(view, SettingsField::PollMinutes);
    bool custom = view.pollCustom || !preset || (typed && typed->text);
    {
        std::vector<UiOption> options;
        for (int minutes : kSettingsPollPresets) {
            options.push_back({std::to_wstring(minutes) + L" minutes", minutes});
        }
        options.push_back({L"Custom", 0});
        UiCardSpec spec;
        spec.title = L"Cloud poll interval";
        spec.description = {{L"How often Anthropic, OpenAI, GitHub Copilot and OpenCode Go are polled. "
                             L"Antigravity polls its local session every minute."}};
        // It edits the same field as the custom box: a distinct control id.
        y = SettingsAddDropdownCard(g, view, items, id, x, y, width, spec, SettingsField::PollMinutes,
                                    200, std::move(options), custom ? 0 : s.pollMinutes, 8);
    }
    {
        UiCardSpec spec;
        spec.title = L"Custom interval";
        spec.description = {{L"2 to 1440 minutes."}};
        spec.enabled = custom;
        if (!custom) spec.reason = L"Choose Custom in Cloud poll interval to edit.";
        y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::PollMinutes,
                                  L"minutes");
    }
    {
        UiCardSpec spec;
        spec.title = L"Do not poll hidden accounts";
        spec.description = {{L"Hidden accounts are neither polled nor recorded, even with Record "
                             L"history on. Turn off to keep recording them. Shared across Windows "
                             L"users."}};
        y = SettingsAddToggleCard(g, view, items, id, x, y + 3, width, spec,
                                  SettingsField::SkipHiddenPolling);
    }

    // Alerts
    y = UiAddSection(items, id, x, y + 20, width, L"Alerts");
    {
        UiCardSpec spec;
        spec.title = L"Threshold notifications";
        spec.description = {{L"Notifies when a taskbar quota first reaches the red threshold (" +
                             std::to_wstring(s.redThreshold) + L"%). Re-arms after it drops below."}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::Notifications);
    }

    UiCardSpec reset;
    reset.title = L"Reset Behavior to defaults";
    reset.description = {{L"Displays, hover details, polling and notifications."}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset,
                                 SettingsField::ResetBehavior, L"Reset");
}
