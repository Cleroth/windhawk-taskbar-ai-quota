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
    auto textWidth = [&](std::wstring_view text, UiFont font) {
        return std::ceil(UiLayoutSize(UiLayout(g, text, UiTextStyleFor(font), 10000, false)).width);
    };

    // Empty state (mockup .emptycard): an icon in a circle, a title, two centered paragraphs.
    if (s.accounts.empty()) {
        constexpr float kPadTop = 44, kPadBottom = 48, kIcon = 56, kGap = 8;
        float top = y + 14, center = x + width / 2;
        float lines = std::clamp(width - 120, 40.0f, 440.0f);
        std::vector<UiRun> first = {{L"Select "}, {L"Add account", UiRunStyle::Strong},
                                    {L" to show an Anthropic, OpenAI, Google Antigravity, GitHub Copilot or "
                                     L"OpenCode Go account on the taskbar."}};
        std::vector<UiRun> second = {{L"Anthropic, OpenAI and GitHub Copilot sign in through your browser, "
                                      L"OpenCode Go with an API key. Google Antigravity uses its signed-in app or "
                                      L"CLI, which must keep running."}};
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
        addText({{L"No accounts yet"}}, UiFont::Subtitle, UiInk::Primary, 26);
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
                    command(SettingsField::AccountHidden, !account.hidden));
        trailing.back().text.clear();
        right -= 40 + 14;
        for (int bar = kQuotaBarCount - 1; bar >= 0; bar--) {
            if (!((bars >> bar) & 1)) continue;
            PCWSTR text = QuotaKey(account.provider, bar);
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
            float buttonWidth = textWidth(L"Sign in", UiFont::Note) + 24;
            UiItem signIn;
            signIn.id = control(SettingsField::AccountSignIn);
            signIn.kind = UiItemKind::Button;
            signIn.button = UiButtonStyle::Accent;
            signIn.font = UiFont::Note;
            signIn.text = L"Sign in";
            signIn.rect = {right - buttonWidth, cy - 14, right, cy + 14};
            signIn.enabled = !view.status.signInRunning;
            signIn.action = command(SettingsField::AccountSignIn);
            trailing.push_back(std::move(signIn));
            right -= buttonWidth + 14;
        }
        for (auto item = trailing.rbegin(); item != trailing.rend(); ++item) push(std::move(*item));

        // Label and "Provider · plan · status".
        std::wstring stateText;
        UiInk stateInk = UiInk::Success;
        std::wstring error = status.error.size() > 60 ? status.error.substr(0, 59) + L"\u2026" : status.error;
        if (!error.empty()) error[0] = wchar_t(towupper(error[0]));
        if (status.signingIn) {
            stateText = L"Signing in\u2026";
            stateInk = UiInk::Accent;
        } else if (signInNeeded) {
            stateText = L"Sign-in needed";
            stateInk = UiInk::Caution;
        } else if (notPolled) {
            stateText = L"Hidden, not polled";
            stateInk = UiInk::Secondary;
        } else if (!error.empty()) {
            stateText = !signsIn && status.error == L"Antigravity not running" ? L"Local app not running" : error;
            stateInk = UiInk::Caution;
        } else if (!signsIn) {
            stateText = status.polled ? L"Local app running" : L"Waiting for the local app";
            stateInk = status.polled ? UiInk::Success : UiInk::Secondary;
        } else {
            stateText = L"Signed in";
        }
        std::wstring details = anthropic ? L"Anthropic \u00B7 " : openai ? L"OpenAI \u00B7 " :
                               copilot ? L"GitHub Copilot \u00B7 " : opencode ? L"OpenCode Go \u00B7 " :
                                         L"Antigravity \u00B7 ";
        if (!status.plan.empty()) details += status.plan + L" \u00B7 ";
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
        state.runs = {{details}, {stateText, UiRunStyle::Normal, stateInk}};
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
                spec.title = L"Label";
                spec.description = {{L"Shown in details, and next to bars when Identity is Label. Unique per "
                                     L"provider; renaming keeps the sign-in and history."}};
                spec.trailing = {200, 32};
                if (draft) spec.error = draft->error;
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
                    PCWSTR name;
                };
                std::vector<Quota> quotas = {{kFiveHourBar, signsIn ? L"5-hour" : L"Short window"},
                                             {kWeeklyBar, L"Weekly"}};
                if (anthropic) {
                    quotas.push_back({kFableWeeklyBar, L"Fable weekly"});
                    quotas.push_back({kExtraUsageBar, L"Extra usage"});
                } else if (openai) {
                    quotas.push_back({kExtraUsageBar, L"Credits"});
                } else if (copilot) {
                    // The monthly allowance is its only quota.
                    quotas = {{kExtraUsageBar, L"AI credits"}};
                } else if (opencode) {
                    quotas.push_back({kExtraUsageBar, L"Monthly"});
                } else {
                    quotas.push_back({kFableWeeklyBar, L"Argon weekly"});
                }
                float chipsWidth = 0;
                for (const Quota& quota : quotas) chipsWidth += textWidth(quota.name, UiFont::Note) + 26 + 4;
                UiCardSpec spec;
                spec.title = L"Quotas on taskbar";
                spec.description = {{L"Unavailable quotas hide automatically. At least one stays selected."}};
                if (openai && s.barLayout == BarLayout::Sparkline) {
                    spec.description[0].text += L" Usage graphs show Credits as a number on the 7d chart.";
                }
                spec.trailing = {chipsWidth - 4, 26};
                if (openai && !account.creditsMax) spec.reason = L"Set Credits max to show the Credits bar.";
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
                spec.title = L"Credits max";
                spec.description = {{L"The balance the Credits bar treats as 100%. OpenAI reports only the "
                                     L"balance."}};
                spec.trailing = UiNumberBoxSize(g, L"credits");
                if (draft && draft->text) spec.error = draft->error;
                UiCardLayout layout = row(spec);
                UiAddNumberBox(cardItems, control(SettingsField::AccountCreditsMax), layout.trailing,
                               draft && draft->text ? draft->raw :
                                                      SettingsFormatNumber(account.creditsMax, view.status.groupSeparator),
                               L"credits", true, command(SettingsField::AccountCreditsMax));
            }
            {
                UiCardSpec spec;
                spec.title = L"Identity";
                spec.description = {{L"Marks this account beside its bars and on tiles. Label "
                                     L"shows the label on bars and its short text on tiles. The number badge "
                                     L"appears when several accounts share a provider, hidden ones included."}};
                spec.trailing = {220, 32};
                if (const SettingsDraft* draft = draftOf(SettingsField::AccountTileIdentity)) spec.error = draft->error;
                UiCardLayout layout = row(spec);
                UiAddDropdown(cardItems, control(SettingsField::AccountTileIdentity), layout.trailing,
                              {{L"Logo + number badge", int(TileIdentity::LogoBadge)},
                               {L"Logo only", int(TileIdentity::Logo)},
                               {L"Label", int(TileIdentity::Text)}},
                              true, command(SettingsField::AccountTileIdentity, int(account.tileIdentity)));
            }
            {
                // Its placeholder is what the tile shows without it: the label without spaces.
                bool used = account.tileIdentity == TileIdentity::Text;
                const SettingsDraft* draft = draftOf(SettingsField::AccountTileText);
                UiCardSpec spec;
                spec.title = L"Short text";
                spec.description = {{L"The Label identity on tiles, up to 4 characters. Empty "
                                     L"uses the label without spaces."}};
                spec.trailing = {96, 32};
                spec.enabled = used;
                if (!used) spec.reason = L"Used on tiles when Identity is Label.";
                if (draft) spec.error = draft->error;
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
                    UiInk ink;
                };
                Recording recording =
                    !account.recordHistory ? Recording{L"\u25CB Off", UiInk::Secondary} :
                    !s.historyEnabled ? Recording{L"\u25CB Off for this Windows user", UiInk::Secondary} :
                    view.status.recording == SettingsRecording::PendingSave ? Recording{L"\u25CF Waiting", UiInk::Caution} :
                    !account.historyId ? Recording{L"\u25CF Unavailable", UiInk::Caution} :
                    account.hidden && s.skipHiddenPolling ? Recording{L"\u25CF Paused while hidden", UiInk::Caution} :
                    view.status.recording == SettingsRecording::StorageError ? Recording{L"\u25CF Not saving", UiInk::Danger} :
                    Recording{L"\u25CF Recording", UiInk::Success};
                UiCardSpec spec;
                spec.title = L"Record history";
                spec.description = {{recording.mark, UiRunStyle::Normal, recording.ink},
                                    {L" \u00B7 History follows this entry, even after signing in again. Use a new "
                                     L"entry for a different login."}};
                spec.trailing = UiToggleSize(g);
                UiCardLayout layout = row(spec);
                UiAddToggle(cardItems, control(SettingsField::AccountRecordHistory), layout.trailing,
                            account.recordHistory, true,
                            command(SettingsField::AccountRecordHistory, !account.recordHistory));
            }

            // Actions (mockup .aact): dashboard and sign-in buttons, Remove account at the right.
            // Sign in again is the header's Sign in when no sign-in is needed (its own id).
            UiItem rule;
            rule.id = id++;
            rule.kind = UiItemKind::Rule;
            rule.rect = {x + 1, bottom, x + width - 1, bottom + 1};
            push(rule);
            float buttonTop = bottom + 12, left = x + 56;
            auto addButton = [&](SettingsField field, PCWSTR text, PCWSTR icon, UiButtonStyle style, bool enabled,
                                 bool trailing) {
                float buttonWidth = textWidth(text, UiFont::Body) + 28 + (icon[0] ? 12 + 8 : 0);
                float buttonLeft = trailing ? x + width - 19 - buttonWidth : left;
                UiItem button;
                button.id = control(field, 1);
                button.kind = UiItemKind::Button;
                button.button = style;
                button.text = text;
                button.icon = icon;
                button.iconSize = 12;
                button.rect = {buttonLeft, buttonTop, buttonLeft + buttonWidth, buttonTop + 32};
                button.enabled = enabled;
                button.action = command(field);
                push(button);
                if (!trailing) left += buttonWidth + 8;
            };
            if (signsIn) {
                addButton(SettingsField::AccountDashboard, L"Open dashboard", L"\uE8A7", UiButtonStyle::Standard, true,
                          false);
                if (status.signedIn && !signInNeeded) {
                    addButton(SettingsField::AccountSignIn, L"Sign in again", L"", UiButtonStyle::Standard,
                              !view.status.signInRunning, false);
                }
                if (status.signedIn) {
                    addButton(SettingsField::AccountSignOut, L"Sign out", L"", UiButtonStyle::Standard, true, false);
                }
            }
            addButton(SettingsField::AccountRemove, L"Remove account", L"", UiButtonStyle::Danger, true, true);
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
