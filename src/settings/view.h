// Settings window view: the explicit snapshot page builders read (the render projection, draft
// text and errors, statuses, window flags), the ids of interactive controls, and the settings
// cards pages share. Pure.

enum class SettingsUiPage : int { Accounts, Layout, Display, Behavior, History, About, Count };

struct SettingsView {
    SettingsUiPage page = SettingsUiPage::Accounts;
    std::wstring version;
    Settings settings;                  // Render projection (SettingsEdits::Projection)
    SettingsStatus status;
    std::vector<SettingsDraft> drafts;  // Number box text and errors
    bool pollCustom = false;            // Custom chosen in Cloud poll interval
};

// Interactive controls keep their ids across rebuilds, so focus and text boxes follow them.
// Other items take running ids below 0x10000.
static uint32_t SettingsControlId(SettingsField field, uint32_t part = 0) {
    return 0x10000u + uint32_t(field) * 16 + part;
}

static const SettingsDraft* SettingsViewDraft(const SettingsView& view, SettingsField field) {
    for (const SettingsDraft& draft : view.drafts) {
        if (!draft.key && draft.field == field) return &draft;
    }
    return nullptr;
}

static UiAction SettingsCommand(SettingsField field, int value = 0) {
    return {UiActionKind::Command, value, {}, uint32_t(field)};
}

// Shared cards; each returns its bottom.

static float SettingsAddToggleCard(UiGraphics& g, const SettingsView& view,
                                   std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                   float width, UiCardSpec spec, SettingsField field) {
    bool on = SettingsFieldValue(view.settings, field) != 0;
    spec.trailing = UiToggleSize(g);
    if (const SettingsDraft* draft = SettingsViewDraft(view, field)) spec.error = draft->error;
    UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
    UiAddToggle(items, SettingsControlId(field), card.trailing, on, spec.enabled,
                SettingsCommand(field, !on));
    return card.card.bottom;
}

static float SettingsAddDropdownCard(UiGraphics& g, const SettingsView& view,
                                     std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                     float width, UiCardSpec spec, SettingsField field,
                                     float fieldWidth, std::vector<UiOption> options, int value,
                                     uint32_t part = 0) {
    spec.trailing = {fieldWidth, 32};
    if (const SettingsDraft* draft = SettingsViewDraft(view, field); draft && !draft->text) {
        spec.error = draft->error;
    }
    UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
    UiAddDropdown(items, SettingsControlId(field, part), card.trailing, std::move(options),
                  spec.enabled, SettingsCommand(field, value));
    return card.card.bottom;
}

// A number box shows its draft's text while there is one, else the projected value grouped.
static float SettingsAddNumberCard(UiGraphics& g, const SettingsView& view,
                                   std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                   float width, UiCardSpec spec, SettingsField field,
                                   std::wstring unit) {
    const SettingsDraft* draft = SettingsViewDraft(view, field);
    std::wstring text = draft && draft->text ?
        draft->raw : SettingsFormatNumber(SettingsFieldValue(view.settings, field),
                                          view.status.groupSeparator);
    spec.trailing = UiNumberBoxSize(g, unit);
    if (draft && draft->text) spec.error = draft->error;
    UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
    UiAddNumberBox(items, SettingsControlId(field), card.trailing, std::move(text), std::move(unit),
                   spec.enabled, SettingsCommand(field));
    return card.card.bottom;
}

// A card with a standard button (mockup .btn: 14 DIP padding, optional 12 DIP glyph).
static float SettingsAddButtonCard(UiGraphics& g, std::vector<UiItem>& items, uint32_t& id,
                                   float x, float y, float width, UiCardSpec spec,
                                   SettingsField command, std::wstring label, std::wstring icon = {}) {
    float labelWidth = UiLayoutSize(UiLayout(g, label, UiTextStyleFor(UiFont::Body), 1000, false)).width;
    spec.trailing = {std::ceil(labelWidth) + 28 + (icon.empty() ? 0 : 12 + 8), 32};
    UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
    UiItem button;
    button.id = SettingsControlId(command);
    button.kind = UiItemKind::Button;
    button.rect = card.trailing;
    button.text = std::move(label);
    button.icon = std::move(icon);
    button.iconSize = 12;
    button.action = SettingsCommand(command);
    items.push_back(std::move(button));
    return card.card.bottom;
}
