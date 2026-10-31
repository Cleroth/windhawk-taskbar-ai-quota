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
    {
        UiCardSpec spec;
        spec.title = L"Keep history";
        spec.description = {{L"1 to 3650 days, shared across Windows users. Older samples are pruned "
                             L"hourly; a cap of about 60 MB can shorten this."}};
        spec.enabled = s.historyEnabled;
        if (!s.historyEnabled) spec.reason = L"Turn on Record usage history to change this.";
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
    reset.description = {{L"Recording on for this Windows user, keep 90 days. Saved history stays."}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset,
                                 SettingsField::ResetHistory, L"Reset");
}
