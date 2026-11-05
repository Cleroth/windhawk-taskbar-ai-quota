// Localized runs (i18n/format.h) as the toolkit's (ui/rich-text.h), each link bound by its ID to
// the action the code defines for it. The runs keep the translation's order, so links take focus
// in its reading order. Pure.

struct I18nLinkAction {
    std::wstring_view id;
    UiAction action;
};

// Bold runs are strong; a link whose ID `links` doesn't bind shows as plain text.
[[maybe_unused]] static std::vector<UiRun> I18nUiRuns(const std::vector<I18nRun>& runs,
                                                       std::initializer_list<I18nLinkAction> links) {
    std::vector<UiRun> out;
    for (const I18nRun& run : runs) {
        UiRun& ui = out.emplace_back();
        ui.text = run.text;
        ui.style = run.bold ? UiRunStyle::Strong : UiRunStyle::Normal;
        for (const I18nLinkAction& link : links) {
            if (run.link.empty() || link.id != run.link) continue;
            ui.style = UiRunStyle::Link;
            ui.action = link.action;
        }
    }
    return out;
}
