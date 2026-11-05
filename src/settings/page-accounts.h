// Settings > Accounts: one expander card per account in taskbar order, or the empty state
// (approved mockups set-accounts.png, accounts-expanded-openai.png, accounts-empty.png). The
// title, its note and the Add account menu (accounts-add.png) are in the shell's pinned header.
// Pure builder.

static constexpr PCWSTR kSettingsEmptyCircle = L"M0,28a28,28 0 1 0 56,0a28,28 0 1 0 -56,0Z";

// Where a dragged card goes: past every other card whose center is above the pointer (content
// DIPs). `centers` are the other cards' centers, in order.
static size_t SettingsReorderTarget(const std::vector<float>& centers, float pointer) {
    return size_t(std::count_if(centers.begin(), centers.end(), [&](float center) { return center < pointer; }));
}

// A drag's live layout of the built page, without rebuilding it: the cards (each its panel, then
// the items after it up to the next panel) restacked in `order` (panel ids) from content y `top`,
// 4 DIP apart as the builder stacks them, the dragged card's place left empty and the card itself
// (built lifted, last) moved to `dragTop`. Returns whether anything moved.
static bool SettingsMoveAccountCards(std::vector<UiItem>& items, const std::vector<uint32_t>& order,
                                     uint32_t dragged, float top, float dragTop) {
    std::vector<size_t> starts;
    for (size_t i = 0; i < items.size(); i++) {
        if (std::find(order.begin(), order.end(), items[i].id) != order.end()) starts.push_back(i);
    }
    bool moved = false;
    for (uint32_t panel : order) {
        auto start = std::find_if(starts.begin(), starts.end(), [&](size_t i) { return items[i].id == panel; });
        if (start == starts.end()) continue;
        size_t end = start + 1 == starts.end() ? items.size() : start[1];
        D2D1_RECT_F card = items[*start].rect;
        float dy = (panel == dragged ? dragTop : top) - card.top;
        top += card.bottom - card.top + 4;
        if (dy == 0) continue;
        moved = true;
        for (size_t i = *start; i < end; i++) {
            items[i].rect.top += dy;
            items[i].rect.bottom += dy;
        }
    }
    return moved;
}

// Lays out the page from content y; returns the bottom of the last card. A dragged card is
// built in its new place, then lifted to view.dragTop and drawn over the others.
static float SettingsUiBuildAccounts(UiGraphics& g, const SettingsView& view, std::vector<UiItem>& items,
                                     uint32_t& id, float x, float y, float width) {
    const Settings& s = view.settings;
    const I18nLocale& locale = view.locale;
    auto textWidth = [&](std::wstring_view text, UiFont font) {
        return std::ceil(UiLayoutSize(UiLayout(g, text, UiTextStyleFor(font), 10000, false)).width);
    };

    // Empty state (mockup .emptycard): an icon in a circle, a title, two centered paragraphs. The
    // first names the header's button by its label.
    if (s.accounts.empty()) {
        constexpr float kPadTop = 44, kPadBottom = 48, kIcon = 56, kGap = 8;
        float top = y + 14, center = x + width / 2;
        float lines = std::clamp(width - 120, 40.0f, 440.0f);
        std::vector<UiRun> first = I18nUiRuns(
            I18nRuns(locale, Tr(L"Select <b>{button}</b> to show an Anthropic, OpenAI, Google Antigravity, GitHub "
                                L"Copilot or OpenCode Go account on the taskbar."),
                     {{L"button", I18nText(locale, TrCtx(L"button", L"Add account"))}}),
            {});
        std::vector<UiRun> second = {{I18nText(locale, Tr(L"Anthropic, OpenAI and GitHub Copilot sign in through "
                                                          L"your browser, OpenCode Go with an API key. Google "
                                                          L"Antigravity uses its signed-in app or CLI, which must "
                                                          L"keep running."))}};
        float firstHeight = UiLayoutSize(UiRichLayout(g, first, UiFont::Note, lines, DWRITE_TEXT_ALIGNMENT_CENTER)).height;
        float secondHeight = UiLayoutSize(UiRichLayout(g, second, UiFont::Note, lines, DWRITE_TEXT_ALIGNMENT_CENTER)).height;
        float bottom = top + 1 + kPadTop + kIcon + 6 + kGap + 26 + kGap + firstHeight + kGap + secondHeight +
                       kPadBottom + 1;
        UiItem card;
        card.id = id++;
        card.kind = UiItemKind::Card;
        card.rect = {x, top, x + width, bottom};
        items.push_back(std::move(card));
        float at = top + 1 + kPadTop;
        UiItem circle;
        circle.id = id++;
        circle.kind = UiItemKind::Shape;
        circle.viewBox = kIcon;
        circle.rect = {center - kIcon / 2, at, center + kIcon / 2, at + kIcon};
        circle.parts.push_back({kSettingsEmptyCircle, {}, UiInk::Primary, 0.06f});
        items.push_back(std::move(circle));
        UiItem glyph;
        glyph.id = id++;
        glyph.kind = UiItemKind::Glyph;
        glyph.text = L"\uE716";
        glyph.iconSize = 26;
        glyph.ink = UiInk::Secondary;
        glyph.rect = {center - 14, at + 14, center + 14, at + 42};
        items.push_back(std::move(glyph));
        at += kIcon + 6 + kGap;
        auto addText = [&](std::vector<UiRun> runs, UiFont font, UiInk ink, float height) {
            UiItem text;
            text.id = id++;
            text.kind = UiItemKind::Text;
            text.font = font;
            text.ink = ink;
            text.align = DWRITE_TEXT_ALIGNMENT_CENTER;
            float span = font == UiFont::Subtitle ? width - 2 : lines;
            text.rect = {center - span / 2, at, center + span / 2, at + height};
            text.runs = std::move(runs);
            items.push_back(std::move(text));
            at += height + kGap;
        };
        addText({{I18nText(locale, Tr(L"No accounts yet"))}}, UiFont::Subtitle, UiInk::Primary, 26);
        addText(std::move(first), UiFont::Note, UiInk::Muted, firstHeight);
        addText(std::move(second), UiFont::Note, UiInk::Muted, secondHeight);
        return bottom;
    }

    int visible = int(std::count_if(s.accounts.begin(), s.accounts.end(),
                                    [](const AccountConfig& account) { return !account.hidden; }));
    std::vector<UiItem> lifted;
    float top = y + 14;
    for (size_t i = 0; i < s.accounts.size() && i < view.accounts.size(); i++) {
        const AccountConfig& account = s.accounts[i];
        const SettingsViewAccount& card = view.accounts[i];
        const SettingsAccountStatus& status = card.status;
        const uint64_t key = card.key;
        auto control = [&](SettingsField field, uint32_t part = 0) {
            return SettingsAccountControlId(card.slot, field, part);
        };
        auto command = [&](SettingsField field, int value = 0) {
            return UiAction{UiActionKind::Command, value, {}, uint32_t(field), key};
        };
        auto draftOf = [&](SettingsField field) -> const SettingsDraft* {
            for (const SettingsDraft& draft : view.drafts) {
                if (draft.key == key && draft.field == field) return &draft;
            }
            return nullptr;
        };
        const bool anthropic = account.provider == L"anthropic", openai = account.provider == L"openai";
        const bool copilot = account.provider == L"copilot", opencode = account.provider == L"opencode";
        const bool signsIn = anthropic || openai || copilot || opencode;
        const bool notPolled = account.hidden && (s.skipHiddenPolling || !s.historyEnabled || !account.recordHistory);
        const bool signInNeeded = signsIn && !status.signingIn && (!status.signedIn || status.needsSignIn);
        const int bars = SettingsAccountValue(account, SettingsField::AccountBars);
        std::vector<UiItem> cardItems;
        auto push = [&](UiItem item) { cardItems.push_back(std::move(item)); };

        // Panel, lifted while dragged; the header (mockup .ahead, 68 DIP) expands it.
        constexpr float kHeader = 68;
        const float cy = top + kHeader / 2;
        UiItem panel;
        panel.id = control(SettingsField::AccountExpand, 1);
        panel.kind = UiItemKind::Card;
        panel.rect = {x, top, x + width, top + kHeader};
        panel.selected = key == view.dragKey;
        push(panel);
        UiItem header;
        header.id = control(SettingsField::AccountExpand);
        header.kind = UiItemKind::Card;
        header.button = UiButtonStyle::Subtle;
        header.rect = {x, top, x + width, top + kHeader};
        header.action = command(SettingsField::AccountExpand, !card.expanded);
        push(header);

        // Grip, logo; then, laid out from the right but in reading (and focus) order: Sign in,
        // quota keys, visibility, chevron.
        UiItem handle;
        handle.id = control(SettingsField::AccountMove);
        handle.kind = UiItemKind::Handle;
        handle.text = L"\uE784";
        handle.iconSize = 14;
        handle.rect = {x + 5, cy - 20, x + 35, cy + 20};
        handle.owner = header.id;
        handle.action = command(SettingsField::AccountMove);
        push(handle);
        UiItem logo;
        logo.id = id++;
        logo.kind = UiItemKind::Shape;
        logo.rect = {x + 41, cy - 11, x + 63, cy + 11};
        logo.parts.push_back({ProviderLogoPath(account.provider), {}, UiInk::Primary});
        if (const LogoBrand* brand = ProviderLogoBrand(s.logoBrands, account.provider)) {
            logo.parts[0].fill = UiPartFill{UiRgb(brand->from & 0xFFFFFF), UiRgb(brand->to & 0xFFFFFF),
                                            {brand->x0, brand->y0}, {brand->x1, brand->y1}};
        }
        push(logo);
        float right = x + width - 17;
        std::vector<UiItem> trailing;
        UiItem chevron;
        chevron.id = id++;
        chevron.kind = UiItemKind::Glyph;
        chevron.text = card.expanded ? L"\uE70E" : L"\uE70D";
        chevron.iconSize = 12;
        chevron.ink = UiInk::Secondary;
        chevron.rect = {right - 16, cy - 6, right, cy + 6};
        trailing.push_back(std::move(chevron));
        right -= 16 + 14;
        // The last visible account stays visible: its switch can't turn off.
        UiAddToggle(trailing, control(SettingsField::AccountHidden), {right - 40, cy - 16, right, cy + 16},
                    !account.hidden, account.hidden || visible > 1,
                    command(SettingsField::AccountHidden, !account.hidden), UiToggleText{});
        right -= 40 + 14;
        for (int bar = kQuotaBarCount - 1; bar >= 0; bar--) {
            if (!((bars >> bar) & 1)) continue;
            std::wstring text = QuotaKey(view.locale, account.provider, bar);
            float chipWidth = textWidth(text, UiFont::Caption) + 18;
            UiItem chip;
            chip.id = id++;
            chip.kind = UiItemKind::Chip;
            chip.font = UiFont::Caption;
            chip.text = text;
            chip.selected = true;
            chip.rect = {right - chipWidth, cy - 10, right, cy + 10};
            trailing.push_back(std::move(chip));
            right -= chipWidth + 4;
        }
        right -= 10;
        if (signInNeeded) {
            std::wstring label = I18nText(locale, TrCtx(L"button", L"Sign in"));
            float buttonWidth = textWidth(label, UiFont::Note) + 24;
            UiItem signIn;
            signIn.id = control(SettingsField::AccountSignIn);
            signIn.kind = UiItemKind::Button;
            signIn.button = UiButtonStyle::Accent;
            signIn.font = UiFont::Note;
            signIn.text = std::move(label);
            signIn.rect = {right - buttonWidth, cy - 14, right, cy + 14};
            signIn.enabled = !view.status.signInRunning;
            signIn.action = command(SettingsField::AccountSignIn);
            trailing.push_back(std::move(signIn));
            right -= buttonWidth + 14;
        }
        for (auto item = trailing.rbegin(); item != trailing.rend(); ++item) push(std::move(*item));

        // Label and "Provider · plan · status", the status in its color.
        std::wstring stateText;
        UiInk stateInk = UiInk::Success;
        // The last poll's error on its own, cut at 60 characters.
        std::wstring error = AccountErrorText(view.locale, status.error, AccountErrorForm::Alone);
        if (error.size() > 60) error = error.substr(0, 59) + L"\u2026";
        if (status.signingIn) {
            stateText = I18nText(locale, TrCtx(L"account-state", L"Signing in\u2026"));
            stateInk = UiInk::Accent;
        } else if (signInNeeded) {
            stateText = I18nText(locale, TrCtx(L"account-state", L"Sign-in needed"));
            stateInk = UiInk::Caution;
        } else if (notPolled) {
            stateText = I18nText(locale, TrCtx(L"account-state", L"Hidden, not polled"));
            stateInk = UiInk::Secondary;
        } else if (!error.empty()) {
            stateText = !signsIn && status.error.code == AccountErrorCode::AntigravityNotRunning ?
                            I18nText(view.locale, TrCtx(L"account-state", L"Local app not running")) : error;
            stateInk = UiInk::Caution;
        } else if (!signsIn) {
            stateText = I18nText(locale, status.polled ? TrCtx(L"account-state", L"Local app running") :
                                                         TrCtx(L"account-state", L"Waiting for the local app"));
            stateInk = status.polled ? UiInk::Success : UiInk::Secondary;
        } else {
            stateText = I18nText(locale, TrCtx(L"account-state", L"Signed in"));
        }
        PCWSTR provider = anthropic ? L"Anthropic" : openai ? L"OpenAI" :  // i18n-en
                          copilot ? L"GitHub Copilot" : opencode ? L"OpenCode Go" : L"Antigravity";  // i18n-en
        std::vector<UiRun> details = SettingsStatusRuns(
            status.plan.empty() ?
                I18nRuns(locale, TrCtx(L"account-details", L"{provider} \u00B7 {state}"),
                         {{L"provider", provider}, {L"state", SettingsStatusArg(stateText)}}) :
                I18nRuns(locale, TrCtx(L"account-details", L"{provider} \u00B7 {plan} \u00B7 {state}"),
                         {{L"provider", provider}, {L"plan", status.plan}, {L"state", SettingsStatusArg(stateText)}}),
            stateInk);
        float textRight = std::max(right, x + 77 + 40);
        UiItem title;
        title.id = id++;
        title.kind = UiItemKind::Text;
        title.rect = {x + 77, cy - 18, textRight, cy + 2};
        title.runs.push_back({account.label});
        push(title);
        UiItem state;
        state.id = id++;
        state.kind = UiItemKind::Text;
        state.font = UiFont::Caption;
        state.ink = UiInk::Muted;
        state.rect = {x + 77, cy + 2, textRight, cy + 18};
        state.runs = std::move(details);
        push(state);

        // Expanded rows (mockup .scard.sub).
        float bottom = top + kHeader;
        auto row = [&](UiCardSpec spec) {
            spec.nested = true;
            spec.minHeight = 60;
            UiCardLayout layout = UiAddCard(g, cardItems, id, x + 1, bottom, width - 2, spec);
            bottom = layout.card.bottom;
            return layout;
        };
        if (card.expanded) {
            {
                const SettingsDraft* draft = draftOf(SettingsField::AccountLabel);
                UiCardSpec spec;
                spec.title = I18nText(locale, Tr(L"Label"));
                spec.description = {{I18nText(locale, Tr(L"Shown in details, and next to bars when Identity is "
                                                         L"Label. Unique per provider; renaming keeps the sign-in "
                                                         L"and history."))}};
                spec.trailing = {200, 32};
                if (draft) spec.error = SettingsDraftErrorText(locale, *draft);
                UiCardLayout layout = row(spec);
                UiItem box;
                box.id = control(SettingsField::AccountLabel);
                box.kind = UiItemKind::TextBox;
                box.rect = layout.trailing;
                box.text = draft ? draft->raw : account.label;
                box.maximum = kSettingsLabelLimit;
                box.action = command(SettingsField::AccountLabel);
                push(box);
            }
            {
                // Chips: the desired selection with this quota flipped; the last one stays.
                struct Quota {
                    int bar;
                    std::wstring name;
                };
                auto name = [&](I18nKey key) { return I18nText(locale, key); };
                auto weekly = [&](PCWSTR model) {
                    return I18nText(locale, TrCtx(L"quota-name", L"{name} weekly"), {{L"name", model}});
                };
                std::vector<Quota> quotas = {
                    {kFiveHourBar,
                     name(signsIn ? TrCtx(L"quota-name", L"5-hour") : TrCtx(L"quota-name", L"Short window"))},
                    {kWeeklyBar, name(TrCtx(L"quota-name", L"Weekly"))}};
                if (anthropic) {
                    quotas.push_back({kFableWeeklyBar, weekly(L"Fable")});  // i18n-en
                    quotas.push_back({kExtraUsageBar, name(TrCtx(L"quota-name", L"Extra usage"))});
                } else if (openai) {
                    quotas.push_back({kExtraUsageBar, name(TrCtx(L"quota-name", L"Credits"))});
                } else if (copilot) {
                    // The monthly allowance is its only quota.
                    quotas = {{kExtraUsageBar, name(TrCtx(L"quota-name", L"AI credits"))}};
                } else if (opencode) {
                    quotas.push_back({kExtraUsageBar, name(TrCtx(L"quota-name", L"Monthly"))});
                } else {
                    quotas.push_back({kFableWeeklyBar, weekly(L"Argon")});  // i18n-en
                }
                float chipsWidth = 0;
                for (const Quota& quota : quotas) chipsWidth += textWidth(quota.name, UiFont::Note) + 26 + 4;
                UiCardSpec spec;
                spec.title = I18nText(locale, Tr(L"Quotas on taskbar"));
                spec.description = {{I18nText(
                    locale, openai && s.barLayout == BarLayout::Sparkline ?
                                Tr(L"Unavailable quotas hide automatically. At least one stays selected. Usage graphs "
                                   L"show Credits as a number on the 7d chart.") :
                                Tr(L"Unavailable quotas hide automatically. At least one stays selected."))}};
                spec.trailing = {chipsWidth - 4, 26};
                if (openai && !account.creditsMax) {
                    spec.reason = I18nText(locale, Tr(L"Set Credits max to show the Credits bar."));
                }
                UiCardLayout layout = row(spec);
                float left = layout.trailing.left;
                for (const Quota& quota : quotas) {
                    bool on = (bars >> quota.bar) & 1;
                    int desired = bars ^ (1 << quota.bar);
                    float chipWidth = textWidth(quota.name, UiFont::Note) + 26;
                    UiItem chip;
                    chip.id = control(SettingsField::AccountBars, uint32_t(quota.bar));
                    chip.kind = UiItemKind::Chip;
                    chip.font = UiFont::Note;
                    chip.text = quota.name;
                    chip.selected = on;
                    chip.enabled = !(openai && quota.bar == kExtraUsageBar && !account.creditsMax);
                    chip.rect = {left, layout.trailing.top, left + chipWidth, layout.trailing.bottom};
                    chip.action = command(SettingsField::AccountBars, desired ? desired : bars);
                    push(chip);
                    left += chipWidth + 4;
                }
            }
            if (openai) {
                const SettingsDraft* draft = draftOf(SettingsField::AccountCreditsMax);
                UiCardSpec spec;
                spec.title = I18nText(locale, Tr(L"Credits max"));
                spec.description = {{I18nText(locale, Tr(L"The balance the Credits bar treats as 100%. OpenAI reports "
                                                         L"only the balance."))}};
                // The unit agrees with the saved max, not text still being typed.
                std::wstring unit =
                    I18nText(locale, TrCtx(L"credits-unit", L"{credits, plural, one{credits} other{credits}}"),
                             {{L"credits", account.creditsMax}});
                spec.trailing = UiNumberBoxSize(g, unit);
                if (draft && draft->text) spec.error = SettingsDraftErrorText(locale, *draft);
                UiCardLayout layout = row(spec);
                UiAddNumberBox(cardItems, control(SettingsField::AccountCreditsMax), layout.trailing,
                               draft && draft->text ? draft->raw :
                                                      SettingsFormatNumber(account.creditsMax, view.status.groupSeparator),
                               std::move(unit), true, command(SettingsField::AccountCreditsMax));
            }
            {
                UiCardSpec spec;
                spec.title = I18nText(locale, Tr(L"Identity"));
                spec.description = {{I18nText(locale, Tr(L"Marks this account beside its bars and on tiles. Label "
                                                         L"shows the label on bars and its short text on tiles. The "
                                                         L"number badge appears when several accounts share a "
                                                         L"provider, hidden ones included."))}};
                spec.trailing = {220, 32};
                if (const SettingsDraft* draft = draftOf(SettingsField::AccountTileIdentity)) {
                    spec.error = SettingsDraftErrorText(locale, *draft);
                }
                UiCardLayout layout = row(spec);
                UiAddDropdown(cardItems, control(SettingsField::AccountTileIdentity), layout.trailing,
                              {{I18nText(locale, TrCtx(L"tile-identity", L"Logo + number badge")),
                                int(TileIdentity::LogoBadge)},
                               {I18nText(locale, TrCtx(L"tile-identity", L"Logo only")), int(TileIdentity::Logo)},
                               {I18nText(locale, TrCtx(L"tile-identity", L"Label")), int(TileIdentity::Text)}},
                              true, command(SettingsField::AccountTileIdentity, int(account.tileIdentity)));
            }
            {
                // Its placeholder is what the tile shows without it: the label without spaces.
                bool used = account.tileIdentity == TileIdentity::Text;
                const SettingsDraft* draft = draftOf(SettingsField::AccountTileText);
                UiCardSpec spec;
                spec.title = I18nText(locale, Tr(L"Short text"));
                spec.description = {{I18nText(locale, Tr(L"The Label identity on tiles, up to 4 characters. Empty uses "
                                                         L"the label without spaces."))}};
                spec.trailing = {96, 32};
                spec.enabled = used;
                if (!used) spec.reason = I18nText(locale, Tr(L"Used on tiles when Identity is Label."));
                if (draft) spec.error = SettingsDraftErrorText(locale, *draft);
                UiCardLayout layout = row(spec);
                UiItem box;
                box.id = control(SettingsField::AccountTileText);
                box.kind = UiItemKind::TextBox;
                box.rect = layout.trailing;
                box.text = draft ? draft->raw : account.tileText;
                std::wstring placeholder;
                std::copy_if(account.label.begin(), account.label.end(), std::back_inserter(placeholder),
                             [](wchar_t ch) { return !iswspace(ch); });
                box.runs.push_back({std::move(placeholder)});
                box.maximum = kSettingsTileTextLimit;
                box.enabled = used;
                box.action = command(SettingsField::AccountTileText);
                push(box);
            }
            {
                // Whether it records: this entry's choice, this Windows user's consent, then the
                // recorder's state.
                struct Recording {
                    PCWSTR mark;
                    I18nKey state;
                    UiInk ink;
                };
                Recording recording =
                    !account.recordHistory ?
                        Recording{L"\u25CB ", TrCtx(L"recording-status", L"Off"), UiInk::Secondary} :
                    !s.historyEnabled ?
                        Recording{L"\u25CB ", TrCtx(L"recording-status", L"Off for this Windows user"),
                                  UiInk::Secondary} :
                    view.status.recording == SettingsRecording::PendingSave ?
                        Recording{L"\u25CF ", TrCtx(L"recording-status", L"Waiting"), UiInk::Caution} :
                    !account.historyId ?
                        Recording{L"\u25CF ", TrCtx(L"recording-status", L"Unavailable"), UiInk::Caution} :
                    account.hidden && s.skipHiddenPolling ?
                        Recording{L"\u25CF ", TrCtx(L"recording-status", L"Paused while hidden"), UiInk::Caution} :
                    view.status.recording == SettingsRecording::StorageError ?
                        Recording{L"\u25CF ", TrCtx(L"recording-status", L"Not saving"), UiInk::Danger} :
                        Recording{L"\u25CF ", TrCtx(L"recording-status", L"Recording"), UiInk::Success};
                UiCardSpec spec;
                spec.title = I18nText(locale, Tr(L"Record history"));
                spec.description = SettingsStatusRuns(
                    I18nRuns(locale, TrCtx(L"recording-detail", L"{status} \u00B7 {detail}"),
                             {{L"status", SettingsStatusArg(recording.mark + I18nText(locale, recording.state))},
                              {L"detail", I18nText(locale, Tr(L"History follows this entry, even after signing in "
                                                              L"again. Use a new entry for a different login."))}}),
                    recording.ink);
                UiToggleText states = SettingsToggleText(view);
                spec.trailing = UiToggleSize(g, states);
                UiCardLayout layout = row(spec);
                UiAddToggle(cardItems, control(SettingsField::AccountRecordHistory), layout.trailing,
                            account.recordHistory, true,
                            command(SettingsField::AccountRecordHistory, !account.recordHistory), states);
            }

            // Actions (mockup .aact): dashboard and sign-in buttons, Remove account at the right.
            // Sign in again is the header's Sign in when no sign-in is needed (its own id).
            UiItem rule;
            rule.id = id++;
            rule.kind = UiItemKind::Rule;
            rule.rect = {x + 1, bottom, x + width - 1, bottom + 1};
            push(rule);
            float buttonTop = bottom + 12, left = x + 56;
            auto addButton = [&](SettingsField field, I18nKey key, PCWSTR icon, UiButtonStyle style, bool enabled,
                                 bool trailing) {
                std::wstring text = I18nText(locale, key);
                float buttonWidth = textWidth(text, UiFont::Body) + 28 + (icon[0] ? 12 + 8 : 0);
                float buttonLeft = trailing ? x + width - 19 - buttonWidth : left;
                UiItem button;
                button.id = control(field, 1);
                button.kind = UiItemKind::Button;
                button.button = style;
                button.text = std::move(text);
                button.icon = icon;
                button.iconSize = 12;
                button.rect = {buttonLeft, buttonTop, buttonLeft + buttonWidth, buttonTop + 32};
                button.enabled = enabled;
                button.action = command(field);
                push(button);
                if (!trailing) left += buttonWidth + 8;
            };
            if (signsIn) {
                addButton(SettingsField::AccountDashboard, TrCtx(L"button", L"Open dashboard"), L"\uE8A7",
                          UiButtonStyle::Standard, true, false);
                if (status.signedIn && !signInNeeded) {
                    addButton(SettingsField::AccountSignIn, TrCtx(L"button", L"Sign in again"), L"",
                              UiButtonStyle::Standard, !view.status.signInRunning, false);
                }
                if (status.signedIn) {
                    addButton(SettingsField::AccountSignOut, TrCtx(L"button", L"Sign out"), L"",
                              UiButtonStyle::Standard, true, false);
                }
            }
            addButton(SettingsField::AccountRemove, TrCtx(L"button", L"Remove account"), L"", UiButtonStyle::Danger,
                      true, true);
            bottom += 12 + 32 + 14;
        }
        cardItems.front().rect.bottom = bottom;

        if (panel.selected) {
            float dy = view.dragTop - top;
            for (UiItem& item : cardItems) {
                item.rect.top += dy;
                item.rect.bottom += dy;
                lifted.push_back(std::move(item));
            }
        } else {
            for (UiItem& item : cardItems) items.push_back(std::move(item));
        }
        top = bottom + 4;
    }
    for (UiItem& item : lifted) items.push_back(std::move(item));
    return top - 4;
}
