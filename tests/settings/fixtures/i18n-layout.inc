// Localized layout: every settings page, account state, popup and preview header built in each
// language (and the pseudo-locale) at the window's default and minimum size, audited
// (layout-audit.inc) for text that doesn't fit its control or slot, text cut without its full text
// reachable, and items drawn over each other or out of their card. Also the review renders' cases
// (fixtures/render.inc).

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

// An account's status; `usage` with when it was read (none: no reading).
static SettingsAccountStatus I18nAccountStatus(PCWSTR plan, AccountError error, bool polled, bool signedIn,
                                               AccountUsageSummary usage = {}, PCWSTR usageTime = L"",
                                               bool needsSignIn = false, bool signingIn = false) {
    return {plan, std::move(error), polled, signedIn, needsSignIn, signingIn, usage, usageTime};
}

// Accounts in every card state: signed in, needing a sign-in, signing in, a long coded error with
// provider text, the local app waiting and not running, hidden (Zen: polled every hour by default);
// every provider. Their usage: fresh (OAI 42% with a reset in 2d 4h, Zen 8%), past 100% (CC 2),
// out of date with the tooltip that says so (Work's month, Laptop read days ago, Team), none (Go),
// and a reading that must not show beside Sign in (CC 1).
static SettingsView I18nAccountsView(std::initializer_list<int> expanded = {}) {
    SettingsView view = TestView(SettingsUiPage::Accounts);
    view.now = kNow;
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
        I18nAccountStatus(L"Plus", {}, true, true, {kWeeklyBar, 42, false, kNow + (2 * 24 + 4) * 60 * kMinute}, L"14:32"),
        I18nAccountStatus(L"Pro", {AccountErrorCode::NotSignedIn}, true, false, {kWeeklyBar, 40, true}, L"14:32", true),
        I18nAccountStatus(L"Ultra", {}, true, true, {kWeeklyBar, 112, false}, L"14:32"),
        I18nAccountStatus(L"", {}, false, false),
        I18nAccountStatus(L"Business", {}, true, false, {kExtraUsageBar, 33, true}, L"14:32", false, true),
        I18nAccountStatus(L"", {}, true, true, {kWeeklyBar, 8, false}, L"13:40"),
        I18nAccountStatus(L"", {AccountErrorCode::AntigravityNotRunning}, true, false, {kWeeklyBar, 64, true},
                          L"11/14/2026"),
        I18nAccountStatus(L"Team", refresh, true, true, {kWeeklyBar, 71, true}, L"Tue 09:05"),
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
    // Accounts with hidden ones polled every hour (the default), every 30 minutes, every 2 hours
    // (the usage as what is left), as often as visible ones, and not polled (the hidden account's
    // card expanded, its recording paused): each with its state text and header note.
    cases.push_back({"accounts", I18nAccountsView()});
    {
        SettingsView view = I18nAccountsView({0});
        view.settings.hiddenPollMinutes = 30;
        cases.push_back({"accounts-openai", view});
        view = I18nAccountsView({2});
        view.settings.hiddenPollMinutes = 120;
        view.settings.barMode = BarMode::Remaining;
        cases.push_back({"accounts-anthropic", view});
        view = I18nAccountsView({3});
        view.settings.hiddenPollMinutes = kHiddenPollAsShown;
        cases.push_back({"accounts-antigravity", view});
        view = I18nAccountsView({4, 5});
        view.settings.hiddenPollMinutes = kHiddenPollNever;
        cases.push_back({"accounts-copilot", view});
    }
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
        // Hidden accounts every hour; both custom boxes enabled with their errors; then its two
        // choices that aren't an interval.
        SettingsView view = TestView(SettingsUiPage::Behavior);
        cases.push_back({"behavior", view});
        view.pollCustom = true;
        view.settings.taskbarMonitorNumber = 3;
        view.settings.hiddenPollMinutes = 90;
        view.drafts.push_back({0, SettingsField::PollMinutes, 1, true, L"7x", 10, false, false, SettingsDraftError::Range});
        view.drafts.push_back({0, SettingsField::HiddenPollMinutes, 2, true, L"9x", 90, false, false,
                               SettingsDraftError::Range});
        cases.push_back({"behavior-custom", view});
        view = TestView(SettingsUiPage::Behavior);
        view.settings.hiddenPollMinutes = kHiddenPollAsShown;
        cases.push_back({"behavior-hidden-as-shown", view});
        view.settings.hiddenPollMinutes = kHiddenPollNever;
        cases.push_back({"behavior-hidden-not-polled", view});
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

// The case built in `tag` at `client` and audited, findings prefixed by where.
static std::vector<std::string> I18nLayoutFindings(UiGraphics& g, const I18nLayoutCase& entry, std::string_view tag,
                                                   D2D1_SIZE_F client) {
    SettingsView view = I18nLayoutLocalized(entry.view, tag);
    g.locale = view.locale.LocaleName();
    UiFrame frame;
    SettingsUiBuildFrame(g, view, client, frame);
    std::vector<std::string> findings = I18nLayoutAudit(g, frame, client, kSettingsNavWidth);
    if (entry.name == "accounts") {
        const UiItem* countdown = CardUsage(frame, 1).countdown;
        if (!countdown || UiRunsText(countdown->runs).empty()) findings.push_back("missing OAI reset countdown");
    }
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
        "ellipsize; the header's tooltip holds the label and the whole line, then the card's reading", [] {
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
        // CC 1 needs a sign-in: its tooltip holds no reading.
        CHECK(UiRunsText(stateLine(frame, 2)->runs) == L"Anthropic \u00B7 Sign-in needed");
        CHECK(UiRunsText(UiFindItem(frame, AccountId(2, SettingsField::AccountExpand))->tooltip) ==
              L"CC 1\nAnthropic \u00B7 " + plan + L" \u00B7 Sign-in needed");
        const UiItem* go = stateLine(frame, 4);
        CHECK(go->runs.size() == 1 && go->runs[0].text == detail && go->runs[0].ink == UiInk::Caution);
        CHECK(UiRunsText(UiFindItem(frame, AccountId(4, SettingsField::AccountExpand))->tooltip) ==
              L"Go\nAntigravity \u00B7 AI Pro \u00B7 " + detail + L"\n7d: 12% used, read 14:32");
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
        // 5 Stunden, Wöchentlich, Fable wöchentlich and Zusatznutzung: more than one line.
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
        // Logo + number badge (220 DIP); es: Logotipo + distintivo numérico, wider than 220 less 41.
        UiFrame frame = I18nLayoutFrame(AccountsView({2}), "en");
        const UiItem* identity = UiFindItem(frame, AccountId(3, SettingsField::AccountTileIdentity));
        CHECK_NEAR(identity->rect.right - identity->rect.left, 220);
        frame = I18nLayoutFrame(AccountsView({2}), "es");
        identity = UiFindItem(frame, AccountId(3, SettingsField::AccountTileIdentity));
        CHECK(identity->text == L"Logotipo + distintivo num\u00E9rico" && identity->tooltip.empty());
        CHECK_NEAR(identity->rect.right - identity->rect.left,
                   I18nLayoutTagWidth(L"Logotipo + distintivo num\u00E9rico", UiFont::Body, "es") + 41);
    });

    Run("accounts header note: as tall as it wraps, the viewport under it", [] {
        // Each of the three notes on one 19 DIP line in English. In German, at 680 DIP: Zum
        // Neuordnen ziehen. ... ausgeblendete Konten werden seltener abgefragt, or nicht abgefragt,
        // on two lines; without that clause, on one.
        SettingsView view = AccountsView();
        for (auto [hidden, lines] : {std::pair{60, 2}, std::pair{kHiddenPollNever, 2}, std::pair{kHiddenPollAsShown, 1}}) {
            view.settings.hiddenPollMinutes = hidden;
            CHECK_NEAR(I18nLayoutFrame(view, "en").viewport.top, 50 + 19);
            UiFrame german = I18nLayoutFrame(view, "de");
            CHECK_NEAR(german.viewport.top, 50 + lines * 19);
            CHECK_NEAR(UiFindItem(german, 101)->rect.bottom, german.viewport.top);
        }
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

    Run("accounts layout cases: readings with their meters at the default width, without at the minimum; a "
        "hidden account polled at each kind of interval, or not; each header note", [] {
        std::vector<I18nLayoutCase> cases = I18nLayoutCases();
        auto frameOf = [&](std::string_view name, D2D1_SIZE_F client = {1000, 820}) {
            auto entry = std::find_if(cases.begin(), cases.end(), [&](const I18nLayoutCase& c) { return c.name == name; });
            if (entry == cases.end()) throw std::runtime_error("no layout case");
            return I18nLayoutFrame(entry->view, "en", client);
        };
        // Each card's percentage, empty where it shows none.
        auto percents = [](const UiFrame& frame) {
            std::vector<std::wstring> texts;
            for (uint32_t slot = 1; slot <= 8; slot++) {
                TestCardUsage usage = CardUsage(frame, slot);
                texts.push_back(usage.percent ? UiRunsText(usage.percent->runs) : L"");
            }
            return texts;
        };
        auto meters = [](const UiFrame& frame) {
            return std::count_if(frame.content.begin(), frame.content.end(),
                                 [](const UiItem& item) { return item.kind == UiItemKind::Meter; });
        };
        auto note = [](const UiFrame& frame) { return UiRunsText(UiFindItem(frame, 101)->runs); };
        const std::wstring drag = L"Drag to reorder. Toggle hides an account from the taskbar";
        // OAI, CC 1 (Sign in), CC 2, Go (no reading), Work, Zen, Laptop, Team.
        UiFrame frame = frameOf("accounts");
        CHECK((percents(frame) ==
               std::vector<std::wstring>{L"42%", L"", L"112%", L"\u2014", L"33%", L"8%", L"64%", L"71%"}));
        CHECK(meters(frame) == 6 && meters(frameOf("accounts", I18nLayoutMinClient())) == 0);
        CHECK(percents(frameOf("accounts", I18nLayoutMinClient())) == percents(frame));
        CHECK(HasText(frame, L"OpenCode Go \u00B7 Hidden, polled every hour") &&
              note(frame) == drag + L"; hidden accounts are polled less often.");
        // Out of date: Laptop's reading from days ago, Work's month.
        CHECK(UiRunsText(UiFindItem(frame, AccountId(7, SettingsField::AccountExpand))->tooltip)
                  .ends_with(L"\n7d: 64% used, read 11/14/2026"));
        CHECK(UiRunsText(UiFindItem(frame, AccountId(5, SettingsField::AccountExpand))->tooltip)
                  .ends_with(L"\nMo: 33% used, read 14:32"));
        CHECK(HasText(frameOf("accounts-openai"), L"OpenCode Go \u00B7 Hidden, polled every 30 minutes"));
        frame = frameOf("accounts-anthropic");
        CHECK(HasText(frame, L"OpenCode Go \u00B7 Hidden, polled every 2 hours"));
        CHECK((percents(frame) ==
               std::vector<std::wstring>{L"58%", L"", L"0%", L"\u2014", L"67%", L"92%", L"36%", L"29%"}));
        frame = frameOf("accounts-antigravity");
        CHECK(HasText(frame, L"OpenCode Go \u00B7 Hidden, polled every 10 minutes") &&
              note(frame) == drag + L".");
        frame = frameOf("accounts-copilot");
        CHECK(HasText(frame, L"OpenCode Go \u00B7 Hidden, not polled") && HasText(frame, L"\u25CF Paused while hidden") &&
              note(frame) == drag + L"; hidden accounts are not polled.");
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
