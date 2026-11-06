// Settings > History: this user's recording with its live status, retention, the viewer with the
// range it opens on, and the storage folder (approved mockup history.png). Pure builder.

// Lays out the page from content y; returns the bottom of the last card.
static float SettingsUiBuildHistory(UiGraphics& g, const SettingsView& view,
                                    std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                    float width) {
    const Settings& s = view.settings;
    const I18nLocale& locale = view.locale;

    // Recording: the status follows consent, then the recorder's state.
    y = UiAddSection(items, id, x, y + 16, width, I18nText(locale, TrCtx(L"settings-section", L"Recording")));
    {
        struct Status {
            PCWSTR mark;
            I18nKey state;
            UiInk ink;
            I18nKey detail;
        };
        Status status = !s.historyEnabled ?
            Status{L"\u25CB ", TrCtx(L"recording-status", L"Off"), UiInk::Secondary, Tr(L"Nothing new is recorded.")} :
            view.status.recording == SettingsRecording::PendingSave ?
            Status{L"\u25CF ", TrCtx(L"recording-status", L"Waiting"), UiInk::Caution,
                   Tr(L"Recording starts once the taskbar saves its account IDs.")} :
            view.status.recording == SettingsRecording::StorageError ?
            Status{L"\u25CF ", TrCtx(L"recording-status", L"Not saving"), UiInk::Danger,
                   Tr(L"History storage failed; Open usage history shows why.")} :
            Status{L"\u25CF ", TrCtx(L"recording-status", L"Recording"), UiInk::Success,
                   Tr(L"New samples arrive with each poll.")};
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Record usage history"));
        spec.description = SettingsStatusRuns(
            I18nRuns(locale, TrCtx(L"recording-detail", L"{status} \u00B7 {detail}"),
                     {{L"status", SettingsStatusArg(status.mark + I18nText(locale, status.state))},
                      {L"detail", I18nText(locale, status.detail)}}),
            status.ink);
        spec.description.back().text += L"\n";
        spec.description.push_back(
            {I18nText(locale, Tr(L"Applies to this Windows user only. Turning it off keeps saved history."))});
        y = SettingsAddToggleCard(g, view, items, id, x, y, width, spec, SettingsField::RecordHistory);
    }
    // Retention: a preset, Forever (0) or Custom (-1) with its own number. Text typed into the
    // custom box keeps Custom even while a partial number matches a preset.
    auto days = [&](int count) {
        return I18nText(locale, TrCtx(L"history-span", L"{days, plural, one{# day} other{# days}}"),
                        {{L"days", count}});
    };
    std::vector<UiOption> options = {
        {days(30), 30},
        {days(90), 90},
        {I18nText(locale, TrCtx(L"history-span", L"{years, plural, one{# year} other{# years}}"), {{L"years", 1}}),
         365},
        {I18nText(locale, TrCtx(L"retention", L"Forever")), 0},
        {I18nText(locale, TrCtx(L"retention", L"Custom")), -1}};
    bool preset = std::any_of(options.begin(), options.end(),
                              [&](const UiOption& option) { return option.value == s.historyRetentionDays; });
    const SettingsDraft* typed = SettingsViewDraft(view, SettingsField::RetentionDays);
    bool custom = s.historyRetentionDays > 0 && (view.retentionCustom || !preset || (typed && typed->text));
    std::wstring recordingOff = I18nText(locale, Tr(L"Turn on Record usage history to change this."));
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Keep history"));
        spec.description = {{I18nText(locale, Tr(L"Shared across Windows users. Forever has no size limit, so history "
                                                 L"keeps growing on disk; Antigravity readings older than 15 days are "
                                                 L"kept about 10 minutes apart. Day limits prune hourly, with a cap of "
                                                 L"about 60 MB."))}};
        spec.enabled = s.historyEnabled;
        if (!s.historyEnabled) spec.reason = recordingOff;
        y = SettingsAddDropdownCard(g, view, items, id, x, y + 3, width, spec, SettingsField::KeepHistory,
                                    200, std::move(options), custom ? -1 : s.historyRetentionDays);
    }
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Custom retention"));
        spec.description = {{I18nText(locale, Tr(L"1 to 3650 days."))}};
        spec.enabled = s.historyEnabled && custom;
        if (!s.historyEnabled) spec.reason = recordingOff;
        else if (!custom) spec.reason = I18nText(locale, Tr(L"Choose Custom in Keep history to edit."));
        // The unit agrees with the saved retention, not text still being typed.
        y = SettingsAddNumberCard(g, view, items, id, x, y + 3, width, spec, SettingsField::RetentionDays,
                                  I18nText(locale, TrCtx(L"retention-unit", L"{days, plural, one{days} other{days}}"),
                                           {{L"days", SettingsFieldValue(s, SettingsField::RetentionDays)}}));
    }
    y = UiAddInfoBar(g, items, id, SettingsControlId(SettingsField::RetentionDays, 8), x, y + 3, width,
                     I18nUiRuns(I18nRuns(locale, Tr(L"Each account chooses whether it records: <link "
                                                    L"id=\"accounts\">Accounts</link> > Record history. Hidden "
                                                    L"accounts are not recorded while <link id=\"hidden\">Do not "
                                                    L"poll hidden accounts</link> is on in Behavior.")),
                                {{L"accounts", {UiActionKind::Navigate, int(SettingsUiPage::Accounts)}},
                                 {L"hidden", {UiActionKind::Navigate, int(SettingsUiPage::Behavior)}}}));

    // Viewer
    y = UiAddSection(items, id, x, y + 20, width, I18nText(locale, TrCtx(L"settings-section", L"Viewer")));
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Usage history"));
        spec.description = {{I18nText(locale, Tr(L"Charts, statistics and CSV export from local samples, also while "
                                                 L"recording is off."))}};
        y = SettingsAddButtonCard(g, items, id, x, y, width, spec, SettingsField::OpenHistory,
                                  I18nText(locale, TrCtx(L"button", L"Open usage history")), L"\uE81C");
    }
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Default time span"));
        spec.description = {{I18nText(locale, Tr(L"The range the usage history window opens on. Its range buttons "
                                                 L"still switch it."))}};
        y = SettingsAddDropdownCard(
            g, view, items, id, x, y + 3, width, spec, SettingsField::HistoryRange, 200,
            {{I18nText(locale, TrCtx(L"history-span", L"{hours, plural, one{# hour} other{# hours}}"),
                       {{L"hours", 24}}),
              int(HistoryUiRange::Day)},
             {days(7), int(HistoryUiRange::Week)},
             {days(30), int(HistoryUiRange::Month)},
             {days(90), int(HistoryUiRange::Quarter)},
             {I18nText(locale, TrCtx(L"history-span", L"All retained")), int(HistoryUiRange::All)}},
            int(s.historyDefaultRange));
    }

    // Storage
    y = UiAddSection(items, id, x, y + 20, width, I18nText(locale, TrCtx(L"settings-section", L"Storage")));
    {
        UiCardSpec spec;
        spec.title = I18nText(locale, Tr(L"Storage location"));
        spec.description = {{view.status.historyFolder + L"\n", UiRunStyle::Normal, UiInk::Secondary},
                            {I18nText(locale, Tr(L"Not encrypted. Readings and opaque IDs, no tokens or labels. In "
                                                 L"the mod's Windhawk storage, which Windhawk deletes, as far as it "
                                                 L"can, when it removes the mod."))}};
        y = SettingsAddButtonCard(g, items, id, x, y, width, spec, SettingsField::OpenHistoryFolder,
                                  I18nText(locale, TrCtx(L"button", L"Open folder")), L"\uE838");
    }

    UiCardSpec reset;
    reset.title = I18nText(locale, Tr(L"Reset History to defaults"));
    reset.description = {{I18nText(locale, Tr(L"Recording on for this Windows user, keep history forever, open on 7 "
                                              L"days. Saved history stays."))}};
    return SettingsAddButtonCard(g, items, id, x, y + 3 + 22, width, reset,
                                 SettingsField::ResetHistory, I18nText(locale, TrCtx(L"button", L"Reset")));
}
