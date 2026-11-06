// Localized layout: every settings page, account state, popup and preview header built in each
// language (and the pseudo-locale) at the window's default and minimum size, audited for text that
// doesn't fit its control or slot, text cut without its full text reachable, and items drawn over
// each other or out of their card. Also the review renders' cases (fixtures/render.inc).

// A case: the view (in English; the sweep sets each language) and a name for findings and renders.
struct I18nLayoutCase {
    std::string name;
    SettingsView view;
};

// The settings window's smallest client area: its minimum outer size (settings/window.h) less the
// standard frame (8 DIP borders, a 23 DIP caption) at 96 DPI.
static D2D1_SIZE_F I18nLayoutMinClient() {
    return {kSettingsWindowMinSize.width - 16, kSettingsWindowMinSize.height - 39};
}

static SettingsAccountStatus I18nAccountStatus(PCWSTR plan, AccountError error, bool polled, bool signedIn,
                                               bool needsSignIn = false, bool signingIn = false) {
    return {plan, std::move(error), polled, signedIn, needsSignIn, signingIn};
}

// Accounts in every card state: signed in, needing a sign-in, signing in, a long coded error with
// provider text, the local app waiting and not running, hidden and not polled; every provider.
static SettingsView I18nAccountsView(std::initializer_list<int> expanded = {}) {
    SettingsView view = TestView(SettingsUiPage::Accounts);
    view.settings.accounts = {TestAccount(L"openai", L"OAI", 11, {true, true, false, true}),
                              TestAccount(L"anthropic", L"CC 1", 12),
                              TestAccount(L"anthropic", L"CC 2", 13, {true, true, true, true}),
                              TestAccount(L"antigravity", L"Go", 14, {true, true, true, false}),
                              TestAccount(L"copilot", L"Work", 15, {false, false, false, true}),
                              TestAccount(L"opencode", L"Zen", 16, {true, true, false, true}),
                              TestAccount(L"antigravity", L"Laptop", 17),
                              TestAccount(L"anthropic", L"Team", 18)};
    view.settings.accounts[0].creditsMax = 2500;
    view.settings.accounts[5].hidden = true;
    AccountError refresh{AccountErrorCode::TokenRefreshFailed};
    refresh.cause = AccountErrorCode::Message;
    refresh.detail = L"invalid_grant: The refresh token was already used or revoked by the user.";  // i18n-en
    const SettingsAccountStatus statuses[] = {
        I18nAccountStatus(L"Plus", {}, true, true),
        I18nAccountStatus(L"Pro", {AccountErrorCode::NotSignedIn}, false, false, true),
        I18nAccountStatus(L"Max 20x", {}, true, true),
        I18nAccountStatus(L"", {}, false, false),
        I18nAccountStatus(L"Business", {}, false, false, false, true),
        I18nAccountStatus(L"", {}, false, true),
        I18nAccountStatus(L"", {AccountErrorCode::AntigravityNotRunning}, false, false),
        I18nAccountStatus(L"Team", refresh, false, true),
    };
    for (size_t i = 0; i < view.settings.accounts.size(); i++) {
        SettingsViewAccount account;
        account.key = view.settings.accounts[i].historyId;
        account.slot = uint32_t(i + 1);
        account.status = statuses[i];
        account.expanded = std::find(expanded.begin(), expanded.end(), int(i)) != expanded.end();
        view.accounts.push_back(account);
    }
    return view;
}

static SettingsView I18nLayoutView(BarLayout layout) {
    SettingsView view = TestView(SettingsUiPage::Layout);
    view.settings.barLayout = layout;
    view.status.taskbarHeight = 48;
    view.now = kNow;
    return view;
}

static std::vector<I18nLayoutCase> I18nLayoutCases() {
    std::vector<I18nLayoutCase> cases;
    cases.push_back({"accounts", I18nAccountsView()});
    cases.push_back({"accounts-openai", I18nAccountsView({0})});
    cases.push_back({"accounts-anthropic", I18nAccountsView({2})});
    cases.push_back({"accounts-antigravity", I18nAccountsView({3})});
    cases.push_back({"accounts-copilot", I18nAccountsView({4, 5})});
    cases.push_back({"accounts-empty", TestView(SettingsUiPage::Accounts)});
    static constexpr const char* kStyles[] = {"spark",  "stacked", "vertical", "ring",     "concentric",
                                              "gauge",  "pies",    "square",   "headline", "matrix"};
    for (int style = 0; style < int(std::size(kStyles)); style++) {
        cases.push_back({std::string("layout-") + kStyles[style], I18nLayoutView(BarLayout(style))});
    }
    {
        SettingsView view = I18nLayoutView(BarLayout::Stacked);
        view.settings.placement = TaskbarPlacement::RightOfStart;
        view.settings.labelPosition = LabelPosition::Hidden;
        view.settings.showPaceTicks = false;
        view.status.placementFallbacks = {{1, false}, {2, true}, {3, false, true}};
        view.status.sideTaskbars = {{3, false}, {4, true}};
        cases.push_back({"layout-placement", view});
    }
    {
        SettingsView view = I18nLayoutView(BarLayout::LogoHeadline);
        view.settings.headlineCountdown = true;
        view.status.visualTest = true;
        view.settings.barLength = 180;
        cases.push_back({"layout-headline-countdown", view});
    }
    {
        SettingsView view = I18nLayoutView(BarLayout::Stacked);
        view.page = SettingsUiPage::Display;
        cases.push_back({"display", view});
        view.settings.usagePalette = UsagePalette::Custom;
        view.settings.material = Material::TintedWallpaper;
        view.settings.notificationLevel = NotificationLevel::Orange;
        view.transparencyEffects = false;
        cases.push_back({"display-custom", view});
        view = I18nLayoutView(BarLayout::SegmentedRing);
        view.page = SettingsUiPage::Display;
        view.settings.usagePalette = UsagePalette::Colorblind;
        cases.push_back({"display-ring", view});
    }
    {
        SettingsView view = TestView(SettingsUiPage::Behavior);
        cases.push_back({"behavior", view});
        view.pollCustom = true;
        view.settings.taskbarMonitorNumber = 3;
        view.drafts.push_back({0, SettingsField::PollMinutes, 1, true, L"7x", 10, false, false, SettingsDraftError::Range});
        cases.push_back({"behavior-custom", view});
    }
    {
        SettingsView view = TestView(SettingsUiPage::History);
        cases.push_back({"history", view});
        view.retentionCustom = true;
        view.settings.historyRetentionDays = 45;
        view.status.recording = SettingsRecording::StorageError;
        cases.push_back({"history-custom", view});
        view = TestView(SettingsUiPage::History);
        view.settings.historyEnabled = false;
        cases.push_back({"history-off", view});
        view = TestView(SettingsUiPage::History);
        view.status.recording = SettingsRecording::PendingSave;
        cases.push_back({"history-waiting", view});
    }
    {
        SettingsView view = TestView(SettingsUiPage::About);
        view.version = L"2.0.1";
        cases.push_back({"about", view});
        view.versionCopied = true;
        cases.push_back({"about-copied", view});
        view.versionCopied = false;
        cases.push_back({"about-failed", view});
    }
    return cases;
}

// The view in `tag`'s language ("en": English), as the settings client sets it.
static SettingsView I18nLayoutLocalized(SettingsView view, std::string_view tag) {
    if (tag == "en") return view;
    std::wstring wide(tag.begin(), tag.end());
    view.locale = I18nLocale(*I18nLanguageByTag(wide));
    view.settings.language = wide;
    return view;
}

static constexpr std::string_view kI18nLayoutTags[] = {"en", "de", "es", "fr", "ja", "pt-BR", "ru", "zh-Hans",
                                                       "qps-ploc"};

// Audit ------------------------------------------------------------------------------------------

static std::string I18nLayoutNarrow(std::wstring_view text) {
    std::string narrow;
    for (wchar_t c : text) narrow += c >= 0x20 && c < 0x7F ? char(c) : '?';
    return narrow.size() > 48 ? narrow.substr(0, 45) + "..." : narrow;
}

static float I18nLayoutWidth(UiGraphics& g, std::wstring_view text, UiFont font) {
    return UiLayoutSize(UiLayout(g, text, UiTextStyleFor(font), 10000, false)).width;
}

// The widest word of `text` wrapped at `width`: past it, DirectWrite breaks the word.
static float I18nLayoutWidestWord(UiGraphics& g, std::wstring_view text, UiFont font, float width) {
    winrt::com_ptr<IDWriteTextLayout> layout;
    winrt::check_hresult(g.dwrite->CreateTextLayout(text.data(), UINT32(text.size()), UiFormat(g, UiTextStyleFor(font)),
                                                    std::max(width, 0.0f), 100000.0f, layout.put()));
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WHOLE_WORD);
    DWRITE_TEXT_METRICS m{};
    winrt::check_hresult(layout->GetMetrics(&m));
    return m.width;
}

// Where a Text item's text lies: its layout's line boxes placed at the item.
static D2D1_RECT_F I18nLayoutTextBox(UiGraphics& g, const UiItem& item) {
    DWRITE_TEXT_METRICS m{};
    winrt::check_hresult(UiRichLayout(g, item.runs, item.font, item.rect.right - item.rect.left, item.align,
                                      item.trim)->GetMetrics(&m));
    return {item.rect.left + m.left, item.rect.top + m.top, item.rect.left + m.left + m.width,
            item.rect.top + m.top + m.height};
}

// What a built frame shows wrong, one line each: a label wider than its control (clipped), a label
// ellipsized without a tooltip holding it whole (cut), wrapped text taller than its slot, two items
// over each other, an item out of its card or column. Popups: rows wider than the popup.
static std::vector<std::string> I18nLayoutAudit(UiGraphics& g, const UiFrame& frame, D2D1_SIZE_F client) {
    std::vector<std::string> findings;
    auto report = [&](const char* surface, const UiItem& item, const std::string& what) {
        findings.push_back(std::string(surface) + " #" + std::to_string(item.id) + " " + what);
    };
    for (UiSurfaceId surface : {UiSurfaceId::Main, UiSurfaceId::Content}) {
        const char* name = surface == UiSurfaceId::Main ? "main" : "content";
        const std::vector<UiItem>& items = UiSurfaceItems(frame, surface);
        // The cards (not subtle headers) and the column they span.
        float columnLeft = 1e9f, columnRight = -1e9f;
        for (const UiItem& item : items) {
            if (item.kind == UiItemKind::Card && item.button != UiButtonStyle::Subtle) {
                columnLeft = std::min(columnLeft, item.rect.left);
                columnRight = std::max(columnRight, item.rect.right);
            }
        }
        struct Box {
            const UiItem* item;
            D2D1_RECT_F rect;
        };
        std::vector<Box> boxes;
        for (const UiItem& item : items) {
            const D2D1_RECT_F& r = item.rect;
            float width = r.right - r.left;
            auto fits = [&](std::wstring_view text, UiFont font, float room, const char* what, bool cut = false) {
                float need = I18nLayoutWidth(g, text, font);
                if (need > room + 0.5f && !(cut && !item.tooltip.empty())) {
                    report(name, item, std::string(cut ? "cut " : "clipped ") + what + " \"" + I18nLayoutNarrow(text) +
                                           "\" needs " + std::to_string(int(std::ceil(need))) + " of " +
                                           std::to_string(int(room)));
                }
            };
            switch (item.kind) {
                case UiItemKind::Nav:
                    fits(item.text, UiFont::Body, r.right - 14 - (r.left + (item.icon.empty() ? 12 : 44)), "nav label",
                         true);
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Button: {
                    float icon = item.icon.empty() ? 0 : item.iconSize + (item.text.empty() ? 0 : 8);
                    fits(item.text, item.font, width - icon - 16, "button label");
                    boxes.push_back({&item, r});
                    break;
                }
                case UiItemKind::Dropdown:
                    fits(item.text, UiFont::Body, UiDropdownTextWidth(item), "dropdown value", true);
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Chip:
                    fits(item.text, item.font, width - 12, "chip", true);
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Gallery: {
                    D2D1_RECT_F thumb = UiGalleryThumbRect(item);
                    float room = UiGalleryCaptionWidth(item);
                    float height = UiLayoutSize(UiLayout(g, item.text, UiTextStyleFor(UiFont::Note), room)).height;
                    if (thumb.bottom + 8 + height > r.bottom - 9 + 0.5f) {
                        report(name, item, "gallery caption \"" + I18nLayoutNarrow(item.text) + "\" taller than its card");
                    }
                    if (I18nLayoutWidestWord(g, item.text, UiFont::Note, room) > room + 0.5f) {
                        report(name, item, "gallery caption \"" + I18nLayoutNarrow(item.text) + "\" breaks a word");
                    }
                    boxes.push_back({&item, r});
                    break;
                }
                case UiItemKind::Toggle:
                    if (item.align == DWRITE_TEXT_ALIGNMENT_TRAILING) {
                        fits(item.text, UiFont::Note, width - 48, "toggle label", true);
                    } else if (!item.text.empty()) {
                        fits(item.text, UiFont::Body, width - 52, "toggle state");
                    }
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Link:
                    fits(item.text, item.font, width - 22, "link");
                    boxes.push_back({&item, r});
                    break;
                case UiItemKind::Text: {
                    if (item.runs.empty() || UiRunsText(item.runs).empty()) break;
                    D2D1_RECT_F box = I18nLayoutTextBox(g, item);
                    if (item.trim) {
                        // Cut: what the pointer over it hits must hold it whole in its tooltip.
                        DWRITE_TEXT_METRICS whole{};
                        winrt::check_hresult(UiRichLayout(g, item.runs, item.font, 10000, DWRITE_TEXT_ALIGNMENT_LEADING)
                                                 ->GetMetrics(&whole));
                        const UiItem* target =
                            UiFindItem(frame, UiHitTest(g, items, {(box.left + box.right) / 2, (box.top + box.bottom) / 2}).id);
                        if (whole.width > width + 0.5f &&
                            (!target || UiRunsText(target->tooltip).find(UiRunsText(item.runs)) == std::wstring::npos)) {
                            report(name, item, "cut text \"" + I18nLayoutNarrow(UiRunsText(item.runs)) + "\"");
                        }
                    }
                    if (!item.trim && I18nLayoutWidestWord(g, UiRunsText(item.runs), item.font, width) > width + 0.5f) {
                        report(name, item, "text \"" + I18nLayoutNarrow(UiRunsText(item.runs)) + "\" breaks a word in " +
                                               std::to_string(int(width)));
                    }
                    if (box.bottom > r.bottom + 0.5f || box.right > r.right + 0.5f || box.left < r.left - 0.5f) {
                        report(name, item, "text \"" + I18nLayoutNarrow(UiRunsText(item.runs)) + "\" overflows its slot (" +
                                               std::to_string(int(box.right - box.left)) + "x" +
                                               std::to_string(int(box.bottom - box.top)) + " in " +
                                               std::to_string(int(width)) + "x" + std::to_string(int(r.bottom - r.top)) +
                                               ")");
                    }
                    boxes.push_back({&item, box});
                    break;
                }
                case UiItemKind::Slider:
                    boxes.push_back({&item, UiInflate(r, 10, 0)});
                    break;
                case UiItemKind::NumberBox:
                case UiItemKind::TextBox:
                case UiItemKind::Swatch:
                case UiItemKind::Glyph:
                case UiItemKind::Shape:
                case UiItemKind::Handle:
                case UiItemKind::Preview:
                    boxes.push_back({&item, r});
                    break;
                default:
                    break;
            }
            if ((item.kind == UiItemKind::Dropdown || item.kind == UiItemKind::Button) && !item.options.empty()) {
                std::vector<UiItem> rows;
                UiLayoutOptions(g, item.options, item.action, width, rows);
                for (const UiItem& row : rows) {
                    float room = row.rect.right - row.rect.left - (row.runs.empty() ? (row.icon.empty() ? 12 : 40) : 44) - 12;
                    float need = std::max(I18nLayoutWidth(g, row.text, UiFont::Body),
                                          row.runs.empty() ? 0 : I18nLayoutWidth(g, UiRunsText(row.runs), UiFont::Caption));
                    if (need > room + 0.5f) {
                        report(name, item, "popup row \"" + I18nLayoutNarrow(row.text) + "\" needs " +
                                               std::to_string(int(std::ceil(need))) + " of " + std::to_string(int(room)));
                    }
                }
            }
        }

        // Overlaps: two boxes crossing, unless one holds the other as its part (a toggle's label, a
        // shape's glyph).
        for (size_t i = 0; i < boxes.size(); i++) {
            for (size_t j = i + 1; j < boxes.size(); j++) {
                const D2D1_RECT_F &a = boxes[i].rect, &b = boxes[j].rect;
                float ix = std::min(a.right, b.right) - std::max(a.left, b.left);
                float iy = std::min(a.bottom, b.bottom) - std::max(a.top, b.top);
                if (ix <= 0.5f || iy <= 0.5f) continue;
                auto holds = [](const Box& outer, const Box& inner) {
                    const D2D1_RECT_F &o = outer.rect, &n = inner.rect;
                    bool inside = n.left >= o.left - 0.5f && n.right <= o.right + 0.5f && n.top >= o.top - 0.5f &&
                                  n.bottom <= o.bottom + 0.5f;
                    return inside && (outer.item->kind == UiItemKind::Toggle || outer.item->kind == UiItemKind::Shape ||
                                      outer.item->kind == UiItemKind::Preview);
                };
                if (holds(boxes[i], boxes[j]) || holds(boxes[j], boxes[i])) continue;
                // A glyph drawn over another as one icon (an info bar's severity mark).
                if (boxes[i].item->kind == UiItemKind::Glyph && boxes[j].item->kind == UiItemKind::Glyph &&
                    std::memcmp(&a, &b, sizeof(a)) == 0) {
                    continue;
                }
                findings.push_back(std::string(name) + " #" + std::to_string(boxes[i].item->id) + " and #" +
                                   std::to_string(boxes[j].item->id) + " overlap (" + std::to_string(int(ix)) + "x" +
                                   std::to_string(int(iy)) + ")");
            }
        }

        // Out of its card (the smallest card holding its top-left), else out of the column (content)
        // or the client (main). The navigation pane's items stay in the pane.
        for (const Box& box : boxes) {
            const D2D1_RECT_F& r = box.rect;
            const UiItem* card = nullptr;
            for (const UiItem& item : items) {
                if (item.kind != UiItemKind::Card || item.button == UiButtonStyle::Subtle) continue;
                const D2D1_RECT_F& c = item.rect;
                if (r.left >= c.left - 0.5f && r.left < c.right && r.top >= c.top - 0.5f && r.top < c.bottom &&
                    (!card || (c.right - c.left) * (c.bottom - c.top) <
                                  (card->rect.right - card->rect.left) * (card->rect.bottom - card->rect.top))) {
                    card = &item;
                }
            }
            float left = card ? card->rect.left : surface == UiSurfaceId::Content ? columnLeft : 0;
            float right = card ? card->rect.right : surface == UiSurfaceId::Content ? columnRight : client.width;
            if (box.item->kind == UiItemKind::Nav) right = kSettingsNavWidth;
            float bottom = card ? card->rect.bottom : 1e9f;
            if (r.left < left - 0.5f || r.right > right + 0.5f || r.bottom > bottom + 0.5f) {
                findings.push_back(std::string(name) + " #" + std::to_string(box.item->id) + " out of its " +
                                   (card ? "card" : "column") + " (" + std::to_string(int(r.left)) + ".." +
                                   std::to_string(int(r.right)) + " in " + std::to_string(int(left)) + ".." +
                                   std::to_string(int(right)) + ")");
            }
        }
    }
    return findings;
}

// The case built in `tag` at `client` and audited, findings prefixed by where.
static std::vector<std::string> I18nLayoutFindings(UiGraphics& g, const I18nLayoutCase& entry, std::string_view tag,
                                                   D2D1_SIZE_F client) {
    SettingsView view = I18nLayoutLocalized(entry.view, tag);
    g.locale = view.locale.LocaleName();
    UiFrame frame;
    SettingsUiBuildFrame(g, view, client, frame);
    std::vector<std::string> findings = I18nLayoutAudit(g, frame, client);
    g.locale = L"en-US";
    std::string where = entry.name + " " + std::string(tag) + " " + std::to_string(int(client.width)) + "x" +
                        std::to_string(int(client.height)) + ": ";
    for (std::string& finding : findings) finding = where + finding;
    return findings;
}

static UiFrame I18nLayoutFrame(const SettingsView& entry, std::string_view tag, D2D1_SIZE_F client = {1000, 820}) {
    UiGraphics& g = TestGraphics();
    SettingsView view = I18nLayoutLocalized(entry, tag);
    g.locale = view.locale.LocaleName();
    UiFrame frame;
    SettingsUiBuildFrame(g, view, client, frame);
    g.locale = L"en-US";
    return frame;
}

static float I18nLayoutTagWidth(std::wstring_view text, UiFont font, std::string_view tag) {
    UiGraphics& g = TestGraphics();
    g.locale = I18nLocale(*I18nLanguageByTag(std::wstring(tag.begin(), tag.end()))).LocaleName();
    float width = std::ceil(I18nLayoutWidth(g, text, font));
    g.locale = L"en-US";
    return width;
}

static void RunI18nLayoutTests() {
    Run("settings window minimum: 800 x 590 DIP outer, a 784 x 551 DIP client; the page column 464 DIP wide", [] {
        D2D1_SIZE_F client = I18nLayoutMinClient();
        CHECK_NEAR(client.width, 784);
        CHECK_NEAR(client.height, 551);
        UiFrame frame = I18nLayoutFrame(TestView(SettingsUiPage::Behavior), "en", client);
        const UiItem& reset = Control(frame, SettingsField::ResetBehavior);
        // The column: 20 DIP after the navigation pane, 40 before the edge: 784 - 260 - 60.
        CHECK_NEAR(reset.rect.right, 20 + 464 - 19);
    });

    Run("account state lines: one line each; details that don't fit drop the plan, then the provider, then "
        "ellipsize; the header's tooltip holds the label and the whole line", [] {
        auto stateLine = [](const UiFrame& frame, uint32_t slot) -> const UiItem* {
            const UiItem* header = UiFindItem(frame, AccountId(slot, SettingsField::AccountExpand));
            for (const UiItem& item : frame.content) {
                if (item.kind == UiItemKind::Text && item.font == UiFont::Caption && item.trim && header &&
                    item.rect.top > header->rect.top && item.rect.bottom <= header->rect.bottom) {
                    return &item;
                }
            }
            return nullptr;
        };
        SettingsView view = AccountsView();
        UiFrame frame = I18nLayoutFrame(view, "en");
        for (uint32_t slot = 1; slot <= 4; slot++) {
            CHECK(UiFindItem(frame, AccountId(slot, SettingsField::AccountExpand))->tooltip.empty());
        }
        CHECK(UiRunsText(stateLine(frame, 2)->runs) == L"Anthropic \u00B7 Pro \u00B7 Sign-in needed");

        // CC 1's plan too long for the line: the provider and state stay.
        const std::wstring plan = L"Claude Enterprise annual seat for the research and platform teams";
        view.accounts[1].status.plan = plan;
        // Go's error too long even alone: only the state, ellipsized, in its color.
        const std::wstring detail = L"The language server answered with something this window can't show in "
                                    L"full on one line beside the account's controls";
        view.accounts[3].status.error = {AccountErrorCode::Message, detail};
        frame = I18nLayoutFrame(view, "en");
        CHECK(UiRunsText(stateLine(frame, 2)->runs) == L"Anthropic \u00B7 Sign-in needed");
        CHECK(UiRunsText(UiFindItem(frame, AccountId(2, SettingsField::AccountExpand))->tooltip) ==
              L"CC 1\nAnthropic \u00B7 " + plan + L" \u00B7 Sign-in needed");
        const UiItem* go = stateLine(frame, 4);
        CHECK(go->runs.size() == 1 && go->runs[0].text == detail && go->runs[0].ink == UiInk::Caution);
        CHECK(UiRunsText(UiFindItem(frame, AccountId(4, SettingsField::AccountExpand))->tooltip) ==
              L"Go\nAntigravity \u00B7 AI Pro \u00B7 " + detail);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountExpand))->tooltip.empty());
    });

    Run("ellipsized text never splits a surrogate pair: each pair is one cluster, trimmed or shown whole", [] {
        UiGraphics& g = TestGraphics();
        std::wstring text = L"Plan ";
        for (int i = 0; i < 40; i++) text += L"\U00020000";  // CJK Extension B, a surrogate pair each
        IDWriteTextLayout* cut = UiRichLayout(g, {{text}}, UiFont::Caption, 120, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        bool shown = false, trimmed = false;
        for (size_t at = 5; at < text.size(); at += 2) {
            CHECK(TestTrimmed(cut, at + 1) == TestTrimmed(cut, at + 2));
            (TestTrimmed(cut, at + 1) ? trimmed : shown) = true;
        }
        CHECK(shown && trimmed);
    });

    Run("account actions: a row from the left with Remove account at the right; a button that would reach "
        "the edge, or Remove account meeting the row, starts a row 8 DIP below", [] {
        auto button = [](const UiFrame& frame, SettingsField field) {
            const UiItem* item = UiFindItem(frame, AccountId(3, field, 1));
            if (!item) throw std::runtime_error("missing account button");
            return item->rect;
        };
        // CC 2 signed in: Open dashboard, Sign in again, Sign out, Remove account.
        UiFrame frame = I18nLayoutFrame(AccountsView({2}), "en");
        D2D1_RECT_F dashboard = button(frame, SettingsField::AccountDashboard),
                    again = button(frame, SettingsField::AccountSignIn), out = button(frame, SettingsField::AccountSignOut),
                    remove = button(frame, SettingsField::AccountRemove);
        CHECK_NEAR(dashboard.left, 20 + 56);
        CHECK_NEAR(again.left, dashboard.right + 8);
        CHECK_NEAR(out.left, again.right + 8);
        CHECK_NEAR(remove.top, dashboard.top);
        CHECK_NEAR(remove.right, 20 + 680 - 19);
        CHECK(out.right + 8 <= remove.left);
        // The minimum width: the three fit beside each other, Remove account no longer beside them.
        frame = I18nLayoutFrame(AccountsView({2}), "en", I18nLayoutMinClient());
        dashboard = button(frame, SettingsField::AccountDashboard);
        out = button(frame, SettingsField::AccountSignOut);
        remove = button(frame, SettingsField::AccountRemove);
        CHECK(out.top == dashboard.top && out.right <= 20 + 464 - 19);
        CHECK(out.right + 8 + (remove.right - remove.left) > 20 + 464 - 19);
        CHECK_NEAR(remove.top, dashboard.top + 32 + 8);
        CHECK_NEAR(remove.right, 20 + 464 - 19);
        CHECK_NEAR(UiFindItem(frame, AccountId(3, SettingsField::AccountExpand, 1))->rect.bottom, remove.bottom + 14);
    });

    Run("account rows: quota chips on one line while the description keeps 200 DIP, else wrapped; the label "
        "box narrower for it on a narrow card; the identity field grows to its widest option", [] {
        auto chips = [](const UiFrame& frame, uint32_t slot = 3) {
            std::vector<D2D1_RECT_F> rects;
            for (int bar = 0; bar < kQuotaBarCount; bar++) {
                if (const UiItem* chip = UiFindItem(frame, AccountId(slot, SettingsField::AccountBars, uint32_t(bar)))) {
                    rects.push_back(chip->rect);
                }
            }
            return rects;
        };
        // 680 DIP cards: 680 - 2 - 55 - 18 - 16 = 589 DIP for the text and chips, lines of 389.
        std::vector<D2D1_RECT_F> english = chips(I18nLayoutFrame(AccountsView({2}), "en"));
        CHECK(english.size() == 4);
        for (const D2D1_RECT_F& chip : english) CHECK_NEAR(chip.top, english[0].top);
        // 5 Stunden, Wöchentlich, Fable wöchentlich and Zusätzliche Nutzung: more than one line.
        std::vector<D2D1_RECT_F> german = chips(I18nLayoutFrame(AccountsView({2}), "de"));
        CHECK(german.size() == 4 && german[3].top > german[0].top);
        for (const D2D1_RECT_F& chip : german) {
            CHECK(chip.right - german[0].left <= 589 - 200 + 0.01f);
            CHECK(std::fmod(chip.top - german[0].top, 30.0f) == 0);
        }
        // OAI's 5-hour, Weekly and Credits at the minimum width: 464 - 91 - 200 = 173 DIP lines.
        std::vector<D2D1_RECT_F> narrow = chips(I18nLayoutFrame(AccountsView({0}), "en", I18nLayoutMinClient()), 1);
        CHECK(narrow.size() == 3 && narrow[2].top > narrow[0].top);
        for (const D2D1_RECT_F& chip : narrow) CHECK(chip.right - narrow[0].left <= 173.01f);
        // The label box: 200 DIP, there 173.
        auto labelBox = [](const UiFrame& frame) {
            const D2D1_RECT_F& r = UiFindItem(frame, AccountId(1, SettingsField::AccountLabel))->rect;
            return r.right - r.left;
        };
        CHECK_NEAR(labelBox(I18nLayoutFrame(AccountsView({0}), "en")), 200);
        CHECK_NEAR(labelBox(I18nLayoutFrame(AccountsView({0}), "en", I18nLayoutMinClient())), 173);
        // Logo + number badge (220 DIP); es: Logotipo + insignia numérica, wider than 220 less 41.
        UiFrame frame = I18nLayoutFrame(AccountsView({2}), "en");
        const UiItem* identity = UiFindItem(frame, AccountId(3, SettingsField::AccountTileIdentity));
        CHECK_NEAR(identity->rect.right - identity->rect.left, 220);
        frame = I18nLayoutFrame(AccountsView({2}), "es");
        identity = UiFindItem(frame, AccountId(3, SettingsField::AccountTileIdentity));
        CHECK(identity->text == L"Logotipo + insignia num\u00E9rica" && identity->tooltip.empty());
        CHECK_NEAR(identity->rect.right - identity->rect.left,
                   I18nLayoutTagWidth(L"Logotipo + insignia num\u00E9rica", UiFont::Body, "es") + 41);
    });

    Run("accounts header note: as tall as it wraps, the viewport under it", [] {
        CHECK_NEAR(I18nLayoutFrame(AccountsView(), "en").viewport.top, 50 + 19);
        // Zum Neuordnen ziehen. ... werden nicht abgefragt: two 19 DIP lines at 680 DIP.
        UiFrame german = I18nLayoutFrame(AccountsView(), "de");
        CHECK_NEAR(german.viewport.top, 50 + 2 * 19);
        CHECK_NEAR(UiFindItem(german, 101)->rect.bottom, german.viewport.top);
    });

    Run("tile style gallery: cards as tall as the longest name wraps, one line in English", [] {
        auto heights = [](const UiFrame& frame) {
            std::vector<float> values;
            for (uint32_t i = 0; i < 10; i++) {
                const UiItem& card = Control(frame, SettingsField::BarLayout, i);
                values.push_back(card.rect.bottom - card.rect.top);
            }
            return values;
        };
        // 1 + 8 + 72 + 8, then the 19 DIP caption lines, 8 + 1.
        for (float height : heights(I18nLayoutFrame(I18nLayoutView(BarLayout::Stacked), "en"))) CHECK_NEAR(height, 117);
        // At the minimum width a card is (464 - 16) / 3 wide, its name 127.33 DIP: Abgerundetes Quadrat wraps.
        UiFrame german = I18nLayoutFrame(I18nLayoutView(BarLayout::Stacked), "de", I18nLayoutMinClient());
        CHECK(Control(german, SettingsField::BarLayout, 7).text == L"Abgerundetes Quadrat");
        CHECK(I18nLayoutTagWidth(L"Abgerundetes Quadrat", UiFont::Note, "de") > (464.0f - 16) / 3 - 22);
        for (float height : heights(german)) CHECK_NEAR(height, 117 + 19);
        CHECK_NEAR(Control(german, SettingsField::BarLayout, 3).rect.top,
                   Control(german, SettingsField::BarLayout, 0).rect.bottom + 8);
    });

    Run("Copy version info: as wide as its widest state in every state", [] {
        SettingsView view = TestView(SettingsUiPage::About);
        float widest = std::max({TestTextWidth(L"Copy", UiFont::Body), TestTextWidth(L"Copied", UiFont::Body),
                                 TestTextWidth(L"Copy failed", UiFont::Body)});
        for (std::optional<bool> copied : {std::optional<bool>{}, std::optional<bool>{true}, std::optional<bool>{false}}) {
            view.versionCopied = copied;
            UiFrame frame = I18nLayoutFrame(view, "en");
            const UiItem& copy = Control(frame, SettingsField::CopyVersionInfo);
            CHECK_NEAR(copy.rect.right - copy.rect.left, widest + 28);
        }
    });

    Run("narrow cards: a slider gives up to 100 DIP so its text keeps 200; Custom... goes under the swatches "
        "where both beside them would leave the text less", [] {
        auto width = [](const UiItem& item) { return item.rect.right - item.rect.left; };
        SettingsView layout = I18nLayoutView(BarLayout::Stacked);
        UiFrame wide = I18nLayoutFrame(layout, "en");
        UiFrame narrow = I18nLayoutFrame(layout, "en", I18nLayoutMinClient());
        CHECK_NEAR(width(Control(wide, SettingsField::BarLength, 1)), 220);
        // 464 - 38 = 426 DIP of room: 426 - 16 - 74 - 200.
        CHECK_NEAR(width(Control(narrow, SettingsField::BarLength, 1)), 136);
        // A threshold's dot takes 30 more: 106, at least 120.
        SettingsView display = TestView(SettingsUiPage::Display);
        CHECK_NEAR(width(Control(I18nLayoutFrame(display, "en"), SettingsField::YellowThreshold, 1)), 220);
        CHECK_NEAR(width(Control(I18nLayoutFrame(display, "en", I18nLayoutMinClient()), SettingsField::YellowThreshold, 1)),
                   120);
        // Marker color: five swatches (160 DIP), 16, Custom...; beside them at 680 DIP, under them at 464.
        const UiItem* swatch = &Control(wide, SettingsField::PaceTickColor, 1);
        const UiItem* custom = &Control(wide, SettingsField::PaceTickColorCustom);
        CHECK_NEAR(custom->rect.top, swatch->rect.top - 4);
        CHECK_NEAR(custom->rect.left, Control(wide, SettingsField::PaceTickColor, 5).rect.right + 16);
        swatch = &Control(narrow, SettingsField::PaceTickColor, 1);
        custom = &Control(narrow, SettingsField::PaceTickColorCustom);
        CHECK_NEAR(custom->rect.top, swatch->rect.top - 4 + 32 + 8);
        CHECK_NEAR(custom->rect.right, Control(narrow, SettingsField::PaceTickColor, 5).rect.right);
    });

    Run("localized layout: every page, state and popup fits in every language at the default and minimum size", [] {
        UiGraphics& g = TestGraphics();
        std::vector<std::string> findings;
        for (const I18nLayoutCase& entry : I18nLayoutCases()) {
            for (std::string_view tag : kI18nLayoutTags) {
                for (D2D1_SIZE_F client : {D2D1_SIZE_F{1000, 820}, I18nLayoutMinClient()}) {
                    std::vector<std::string> found = I18nLayoutFindings(g, entry, tag, client);
                    findings.insert(findings.end(), found.begin(), found.end());
                }
            }
        }
        for (const std::string& finding : findings) std::cout << "FINDING " << finding << '\n';
        CHECK(findings.empty());
    });
}
