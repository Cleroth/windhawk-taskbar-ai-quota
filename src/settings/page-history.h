// Settings > History: this user's recording with its live status, retention, the viewer and the
// storage folder (approved mockup history.png). Pure builder.

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildHistory(UiGraphics& g, const SettingsView& view,
                                    std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                    float width) {
    const Settings& s = view.settings;

    // Recording: the status follows consent, then the recorder's state.
    y = UiAddSection(items, id, x, y + 16, width, L"Recording");
    {
        struct Status {
            PCWSTR mark;
            UiInk ink;
            PCWSTR detail;
        };
        Status status = !s.historyEnabled ?
            Status{L"\u25CB Off", UiInk::Secondary, L" \u00B7 Nothing new is recorded."} :
            view.status.recording == SettingsRecording::PendingSave ?
            Status{L"\u25CF Waiting", UiInk::Caution,
                   L" \u00B7 Recording starts once the taskbar saves its account IDs."} :
            view.status.recording == SettingsRecording::StorageError ?
            Status{L"\u25CF Not saving", UiInk::Danger,
                   L" \u00B7 History storage failed; Open usage history shows why."} :
            Status{L"\u25CF Recording", UiInk::Success, L" \u00B7 New samples arrive with each poll."};
        UiCardSpec spec;
        spec.title = L"Record usage history";
        spec.description = {{status.mark, UiRunStyle::Normal, status.ink},
                            {std::wstring(status.detail) + L"\n"},
                            {L"Applies to this Windows user only. Turning it off keeps saved history."}};
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::RecordHistory);
    }
    // Retention: a preset, Forever (0) or Custom (-1) with its own number. Text typed into the
    // custom box keeps Custom even while a partial number matches a preset.
    std::vector<UiOption> options = {{L"30 days", 30}, {L"90 days", 90}, {L"1 year", 365},
                                     {L"Forever", 0},  {L"Custom", -1}};
    bool preset = std::any_of(options.begin(), options.end(),
                              [&](const UiOption& option) { return option.value == s.historyRetentionDays; });
    const SettingsDraft* typed = SettingsViewDraft(view, SettingsField::RetentionDays);
    bool custom = s.historyRetentionDays > 0 && (view.retentionCustom || !preset || (typed && typed->text));
    {
        UiCardSpec spec;
        spec.title = L"Keep history";
        spec.description = {{L"Shared across Windows users. Forever deletes nothing and has no size limit, "
                             L"so history keeps growing in Explorer's memory and on disk. Day limits prune "
                             L"hourly, with a cap of about 60 MB."}};
        spec.enabled = s.historyEnabled;
        if (!s.historyEnabled) spec.reason = L"Turn on Record usage history to change this.";
        y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::KeepHistory,
                                    200, std::move(options), custom ? -1 : s.historyRetentionDays);
    }
    {
        UiCardSpec spec;
        spec.title = L"Custom retention";
        spec.description = {{L"1 to 3650 days."}};
        spec.enabled = s.historyEnabled && custom;
        if (!s.historyEnabled) spec.reason = L"Turn on Record usage history to change this.";
        else if (!custom) spec.reason = L"Choose Custom in Keep history to edit.";
        y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec,
                                  SettingsField::RetentionDays, L"days");
    }
    y = UiAddInfoBar(g, items, id, SettingsControlId(SettingsField::RetentionDays, 8), x, y + 3, width,
                     {{L"Each account chooses whether it records: "},
                      {L"Accounts", UiRunStyle::Link, UiInk::Inherit,
                       {UiActionKind::Navigate, int(SettingsUiPage::Accounts)}},
                      {L" > Record history. Hidden accounts are not recorded while "},
                      {L"Do not poll hidden accounts", UiRunStyle::Link, UiInk::Inherit,
                       {UiActionKind::Navigate, int(SettingsUiPage::Behavior)}},
                      {L" is on in Behavior."}});

    // Viewer
    y = UiAddSection(items, id, x, y + 20, width, L"Viewer");
    {
        UiCardSpec spec;
        spec.title = L"Usage history";
        spec.description = {{L"Charts, statistics and CSV export from local samples, also while "
                             L"recording is off."}};
        y = SettingsAddButtonCard(g, items, id, x, y, width, spec, SettingsField::OpenHistory,
                                  L"Open usage history", L"\uE81C");
    }

    // Storage
    y = UiAddSection(items, id, x, y + 20, width, L"Storage");
    {
        UiCardSpec spec;
        spec.title = L"Storage location";
        spec.description = {{view.status.historyFolder + L"\n", UiRunStyle::Normal, UiInk::Secondary},
                            {L"Not encrypted. Readings and opaque IDs, no tokens or labels. "
                             L"Survives mod removal."}};
        y = SettingsAddButtonCard(g, items, id, x, y, width, spec, SettingsField::OpenHistoryFolder,
                                  L"Open folder", L"\uE838");
    }

    UiCardSpec reset;
    reset.title = L"Reset History to defaults";
    reset.description = {{L"Recording on for this Windows user, keep history forever. Saved history stays."}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset,
                                 SettingsField::ResetHistory, L"Reset");
}
