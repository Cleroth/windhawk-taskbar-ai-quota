// Settings window view: the explicit snapshot page builders read (the render projection, draft
// text and errors, statuses, window flags), the ids of interactive controls, and the settings
// cards pages share. Pure.

enum class SettingsUiPage : int { Accounts, Layout, Display, Behavior, History, About, Count };

// An account card's state, beside its projected settings: its key (settings/edits.h), its control
// slot, what is known about it and whether it is expanded.
struct SettingsViewAccount {
    uint64_t key = 0;
    uint32_t slot = 0;
    SettingsAccountStatus status;
    bool expanded = false;
};

struct SettingsView {
    I18nLocale locale;                  // The frame's language and regional number format
    SettingsUiPage page = SettingsUiPage::Accounts;
    std::wstring version;
    Settings settings;                  // Render projection (SettingsEdits::Projection)
    SettingsStatus status;
    std::vector<SettingsDraft> drafts;  // Text box text and errors
    std::vector<SettingsViewAccount> accounts;  // Parallel to settings.accounts
    uint64_t dragKey = 0;               // The account card being dragged, drawn lifted at dragTop
    float dragTop = 0;                  // Content DIPs
    bool pollCustom = false;            // Custom chosen in Cloud poll interval
    bool retentionCustom = false;       // Custom chosen in Keep history
    bool transparencyEffects = true;    // Windows' transparency effects setting; the window fills it
    float scale = 1;                    // Window DPI / 96: the preview's device pixels
    float previewScroll = 0;            // The preview strip's scroll right of its home, DIPs
                                        // (settings/preview.h)
    ULONGLONG now = 0;                  // The preview's sample time
    std::optional<bool> versionCopied;  // Copy version info's feedback for a moment: whether it copied
};

// Interactive controls keep their ids across rebuilds, so focus and text boxes follow them.
// Other items take running ids below 0x10000.
static uint32_t SettingsControlId(SettingsField field, uint32_t part = 0) {
    return 0x10000u + uint32_t(field) * 16 + part;
}

// An account's controls: by its slot, so they follow the account across reorders.
static uint32_t SettingsAccountControlId(uint32_t slot, SettingsField field, uint32_t part = 0) {
    return 0x100000u + slot * 0x1000u + uint32_t(field) * 16 + part;
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

// A toggle's state text in the view's language.
static UiToggleText SettingsToggleText(const SettingsView& view) {
    return {I18nText(view.locale, TrCtx(L"toggle", L"On")), I18nText(view.locale, TrCtx(L"toggle", L"Off"))};
}

// A status shown in its color inside localized text: the argument SettingsStatusArg makes, whose
// link ID SettingsStatusRuns turns into the ink (these templates have no links of their own).
static std::vector<I18nRun> SettingsStatusArg(std::wstring text) {
    return {{std::move(text), false, L"status"}};
}

static std::vector<UiRun> SettingsStatusRuns(const std::vector<I18nRun>& runs, UiInk ink) {
    std::vector<UiRun> out;
    for (const I18nRun& run : runs) {
        out.push_back(run.link == L"status" ? UiRun{run.text, UiRunStyle::Normal, ink} : UiRun{run.text});
    }
    return out;
}

// Shared cards; each returns its bottom.

static float SettingsAddToggleCard(UiGraphics& g, const SettingsView& view,
                                   std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                   float width, UiCardSpec spec, SettingsField field) {
    bool on = SettingsFieldValue(view.settings, field) != 0;
    UiToggleText states = SettingsToggleText(view);
    spec.trailing = UiToggleSize(g, states);
    if (const SettingsDraft* draft = SettingsViewDraft(view, field)) {
        spec.error = SettingsDraftErrorText(view.locale, *draft);
    }
    UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
    UiAddToggle(items, SettingsControlId(field), card.trailing, on, spec.enabled,
                SettingsCommand(field, !on), states);
    return card.card.bottom;
}

// The field is at least `fieldWidth` wide, wider when its widest option needs it, growing to at
// most half the card; a label cut there shows whole in its tooltip and the popup.
static float SettingsAddDropdownCard(UiGraphics& g, const SettingsView& view,
                                     std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                     float width, UiCardSpec spec, SettingsField field,
                                     float fieldWidth, std::vector<UiOption> options, int value,
                                     uint32_t part = 0) {
    fieldWidth = std::max(fieldWidth, std::min(UiDropdownWidth(g, options), width / 2));
    spec.trailing = {fieldWidth, 32};
    if (const SettingsDraft* draft = SettingsViewDraft(view, field); draft && !draft->text) {
        spec.error = SettingsDraftErrorText(view.locale, *draft);
    }
    UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
    UiAddDropdown(g, items, SettingsControlId(field, part), card.trailing, std::move(options),
                  spec.enabled, SettingsCommand(field, value));
    return card.card.bottom;
}

// A number box shows its draft's text while there is one, else the projected value in the
// regional digit groups.
static float SettingsAddNumberCard(UiGraphics& g, const SettingsView& view,
                                   std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                   float width, UiCardSpec spec, SettingsField field,
                                   std::wstring unit) {
    const SettingsDraft* draft = SettingsViewDraft(view, field);
    std::wstring text = draft && draft->text ? draft->raw :
                                               I18nInteger(view.locale, SettingsFieldValue(view.settings, field));
    spec.trailing = UiNumberBoxSize(g, unit);
    if (draft && draft->text) spec.error = SettingsDraftErrorText(view.locale, *draft);
    UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
    UiAddNumberBox(items, SettingsControlId(field), card.trailing, std::move(text), std::move(unit),
                   spec.enabled, SettingsCommand(field));
    return card.card.bottom;
}

// A slider with a centered number box before it (mockup .sldwrap: 60 DIP box, 14 DIP gap,
// 220 DIP slider). On a narrow card the slider gives up to 100 DIP so the text keeps 200. The box
// shows its draft's text while there is one, else the projected value; the slider shows the
// projected value.
static float SettingsAddSliderCard(UiGraphics& g, const SettingsView& view,
                                   std::vector<UiItem>& items, uint32_t& id, float x, float y,
                                   float width, UiCardSpec spec, SettingsField field) {
    SettingsNumberRange range = SettingsFieldRange(field).value_or(SettingsNumberRange{});
    int value = SettingsFieldValue(view.settings, field);
    const SettingsDraft* draft = SettingsViewDraft(view, field);
    spec.trailing = {60 + 14 + std::clamp(UiCardRoom(spec, width) - 16 - 60 - 14 - 200, 120.0f, 220.0f), 32};
    if (draft) spec.error = SettingsDraftErrorText(view.locale, *draft);
    UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
    const D2D1_RECT_F& slot = card.trailing;
    UiItem box;
    box.id = SettingsControlId(field);
    box.kind = UiItemKind::NumberBox;
    box.rect = {slot.left, slot.top, slot.left + 60, slot.bottom};
    box.text = draft && draft->text ? draft->raw : I18nInteger(view.locale, value);
    box.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    box.enabled = spec.enabled;
    box.action = SettingsCommand(field);
    items.push_back(std::move(box));
    UiAddSlider(items, SettingsControlId(field, 1), {slot.left + 74, slot.top, slot.right, slot.bottom},
                value, range.minimum, range.maximum, spec.enabled, SettingsCommand(field));
    return card.card.bottom;
}

// A color card's swatches (mockup .swrow): 24 DIP circles 10 DIP apart, Custom... (the `custom`
// command with `customValue`, also its control part) 16 DIP after them, or 8 DIP under them on a
// card too narrow to leave the text 200 DIP beside both. A color picked in Custom... shows as one
// more swatch.
static float SettingsAddColorCard(UiGraphics& g, const SettingsView& view, std::vector<UiItem>& items,
                                  uint32_t& id, float x, float y, float width, UiCardSpec spec,
                                  SettingsField field, SettingsField custom, std::vector<COLORREF> colors,
                                  COLORREF current, int customValue = 0) {
    if (std::find(colors.begin(), colors.end(), current) == colors.end()) colors.push_back(current);
    std::wstring customText = I18nText(view.locale, TrCtx(L"button", L"Custom..."));
    float buttonWidth =
        std::ceil(UiLayoutSize(UiLayout(g, customText, UiTextStyleFor(UiFont::Body), 1000, false)).width) + 28;
    float swatches = float(colors.size()) * 24 + float(colors.size() - 1) * 10;
    const bool stacked = UiCardRoom(spec, width) - 16 - (swatches + 16 + buttonWidth) < 200;
    spec.trailing = stacked ? D2D1_SIZE_F{std::max(swatches, buttonWidth), 32 + 8 + 32} :
                              D2D1_SIZE_F{swatches + 16 + buttonWidth, 32};
    if (const SettingsDraft* draft = SettingsViewDraft(view, field)) {
        spec.error = SettingsDraftErrorText(view.locale, *draft);
    }
    UiCardLayout card = UiAddCard(g, items, id, x, y, width, spec);
    float center = card.trailing.top + 16, swatchLeft = stacked ? card.trailing.right - swatches : card.trailing.left;
    for (size_t i = 0; i < colors.size(); i++) {
        UiItem swatch;
        swatch.id = SettingsControlId(field, uint32_t(1 + i));
        swatch.kind = UiItemKind::Swatch;
        float left = swatchLeft + float(i) * 34;
        swatch.rect = {left, center - 12, left + 24, center + 12};
        swatch.selected = colors[i] == current;
        swatch.enabled = spec.enabled;
        swatch.action = SettingsCommand(field, int(colors[i]));
        items.push_back(std::move(swatch));
    }
    UiItem button;
    button.id = SettingsControlId(custom, uint32_t(customValue));
    button.kind = UiItemKind::Button;
    button.rect = {card.trailing.right - buttonWidth, card.trailing.bottom - 32, card.trailing.right, card.trailing.bottom};
    button.text = customText;
    button.enabled = spec.enabled;
    button.action = SettingsCommand(custom, customValue);
    items.push_back(std::move(button));
    return card.card.bottom;
}

// A card with a standard button (mockup .btn: 14 DIP padding, optional 12 DIP glyph), as wide as
// its label or the widest of the labels it can switch to (`states`), so it keeps its width.
static float SettingsAddButtonCard(UiGraphics& g, std::vector<UiItem>& items, uint32_t& id,
                                   float x, float y, float width, UiCardSpec spec,
                                   SettingsField command, std::wstring label, std::wstring icon = {},
                                   const std::vector<std::wstring>& states = {}) {
    float labelWidth = UiLayoutSize(UiLayout(g, label, UiTextStyleFor(UiFont::Body), 1000, false)).width;
    for (const std::wstring& state : states) {
        labelWidth = std::max(labelWidth, UiLayoutSize(UiLayout(g, state, UiTextStyleFor(UiFont::Body), 1000, false)).width);
    }
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
