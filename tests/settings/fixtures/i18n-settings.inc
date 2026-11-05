// Localization in the settings window and the toolkit: localized runs bound to actions
// (i18n/ui-runs.h) and the History page's two-link sentence with its links reordered by a test
// catalog, the language field and its edits, the Behavior page in other languages and with the
// pseudo-locale stored, the toggles' state text, and the locale in the toolkit's text caches.
// Expected text is written out by hand from the production catalogs.

// A test language whose History page sentence names the hidden accounts' link first.
static constexpr I18nEntry kTestCatalogLinks[] = {
    {L"", L"Each account chooses whether it records: <link id=\"accounts\">Accounts</link> > Record history. Hidden "
          L"accounts are not recorded while <link id=\"hidden\">Do not poll hidden accounts</link> is on in Behavior.",
     L"While <link id=\"hidden\">hidden accounts go unpolled</link> they aren't recorded; each account's own choice "
     L"is under <link id=\"accounts\">its card</link>."},
};
static_assert(I18nCatalogSorted(kTestCatalogLinks));
static constexpr I18nLanguage kTestLinks{L"x-links", L"", L"en-US", I18nPluralRule::English, true, kTestCatalogLinks,
                                         false};

// A production language by its tag.
static I18nLocale TestLocale(std::wstring_view tag) {
    const I18nLanguage* language = I18nLanguageByTag(tag);
    if (!language) throw std::runtime_error("no language");
    return I18nLocale(*language);
}

// The Behavior page in `locale` (TestView's displays and settings), Automatic showing `automatic`.
static UiFrame TestBehaviorFrame(const I18nLocale& locale, std::wstring_view automatic = L"en",
                                 const std::function<void(SettingsView&)>& edit = {}) {
    SettingsView view = TestView(SettingsUiPage::Behavior);
    view.locale = locale;
    view.status.automaticLanguage = &TestLocale(automatic).Language();
    if (edit) edit(view);
    UiFrame frame;
    SettingsUiBuildFrame(TestGraphics(), view, {1000, 820}, frame);
    return frame;
}

static std::vector<std::wstring> TestOptionTexts(const UiItem& field) {
    std::vector<std::wstring> texts;
    for (const UiOption& option : field.options) texts.push_back(option.text);
    return texts;
}

static void RunI18nSettingsTests() {
    Run("localized runs: bold strong, each link bound by its ID in the translation's order, an unbound one plain", [] {
        std::vector<I18nRun> runs = {{L"Go ", false, L""}, {L"here", true, L""}, {L"later", false, L"second"},
                                     {L" or ", false, L""}, {L"now", false, L"first"}, {L"!", false, L"other"}};
        std::vector<UiRun> ui = I18nUiRuns(runs, {{L"first", {UiActionKind::Navigate, 1}},
                                                  {L"second", {UiActionKind::OpenUrl, 0, L"https://example.com"}}});
        CHECK(ui.size() == 6 && UiRunsText(ui) == L"Go herelater or now!");
        CHECK(ui[0].style == UiRunStyle::Normal && ui[1].style == UiRunStyle::Strong);
        CHECK(ui[2].style == UiRunStyle::Link && ui[2].action.kind == UiActionKind::OpenUrl &&
              ui[2].action.text == L"https://example.com");
        CHECK(ui[4].style == UiRunStyle::Link && ui[4].action.kind == UiActionKind::Navigate && ui[4].action.value == 1);
        CHECK(ui[5].style == UiRunStyle::Normal && ui[5].action.kind == UiActionKind::None);
        CHECK(UiLinkRanges(ui).size() == 2);
    });

    Run("history page: the two-link sentence as before in English; a translation reordering its links moves "
        "their actions and focus order with them", [] {
        UiGraphics& g = TestGraphics();
        uint32_t textId = SettingsControlId(SettingsField::RetentionDays, 8);
        auto stops = [&](const UiFrame& frame) {
            std::vector<std::pair<std::wstring, int>> links;
            for (const UiFocusStop& stop : UiFocusStops(frame)) {
                if (stop.ref.id != textId) continue;
                const UiAction* action = UiActionOf(frame, stop.ref);
                links.push_back({UiLinkRun(UiFindItem(frame, textId)->runs, stop.ref.link)->text, action->value});
            }
            return links;
        };
        SettingsView view = TestView(SettingsUiPage::History);
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        const std::vector<UiRun>& runs = UiFindItem(frame, textId)->runs;
        CHECK(runs.size() == 5 && runs[0].text == L"Each account chooses whether it records: " &&
              runs[1].text == L"Accounts" && runs[1].style == UiRunStyle::Link &&
              runs[2].text == L" > Record history. Hidden accounts are not recorded while " &&
              runs[3].text == L"Do not poll hidden accounts" && runs[3].style == UiRunStyle::Link &&
              runs[4].text == L" is on in Behavior.");
        CHECK((stops(frame) == std::vector<std::pair<std::wstring, int>>{
                   {L"Accounts", int(SettingsUiPage::Accounts)},
                   {L"Do not poll hidden accounts", int(SettingsUiPage::Behavior)}}));
        view.locale = I18nLocale(kTestLinks);
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(UiRunsText(UiFindItem(frame, textId)->runs) ==
              L"While hidden accounts go unpolled they aren't recorded; each account's own choice is under its card.");
        CHECK((stops(frame) == std::vector<std::pair<std::wstring, int>>{
                   {L"hidden accounts go unpolled", int(SettingsUiPage::Behavior)},
                   {L"its card", int(SettingsUiPage::Accounts)}}));
    });

    Run("language field: Automatic (empty) 0, each language 1 + its registry index, the pseudo-locale last; "
        "choices clamp; the Behavior reset restores Automatic, the History reset leaves it", [] {
        Settings s;
        CHECK(SettingsFieldValue(s, SettingsField::Language) == 0);
        const std::pair<PCWSTR, int> values[] = {{L"en", 1},    {L"de", 2},    {L"es", 3},      {L"fr", 4},
                                                 {L"ja", 5},    {L"pt-BR", 6}, {L"ru", 7},      {L"zh-Hans", 8},
                                                 {L"qps-ploc", 9}};
        for (auto [tag, value] : values) {
            s.language = tag;
            CHECK(SettingsFieldValue(s, SettingsField::Language) == value);
            Settings assigned;
            SettingsAssign(assigned, SettingsField::Language, value);
            CHECK(assigned.language == tag);
        }
        CHECK(kSettingsPseudoLanguage == 9);
        SettingsAssign(s, SettingsField::Language, 0);
        CHECK(s.language.empty());
        SettingsAssign(s, SettingsField::Language, 99);
        CHECK(s.language == L"qps-ploc");
        SettingsAssign(s, SettingsField::Language, -4);
        CHECK(s.language.empty());
        s.language = L"ru";
        SettingsAssign(s, SettingsField::ResetHistory, 0);
        CHECK(s.language == L"ru");
        SettingsAssign(s, SettingsField::ResetBehavior, 0);
        CHECK(s.language.empty());
        std::vector<SettingsField> behavior = SettingsGroupFields(SettingsField::ResetBehavior);
        CHECK(std::count(behavior.begin(), behavior.end(), SettingsField::Language) == 1);
    });

    Run("language edits: the dropdown commits its choice at once; a failed save keeps the stored language with "
        "the field's error; the Behavior reset returns to Automatic", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        CHECK(edits.Commit(0, SettingsField::Language, 7) == 0);
        CHECK(store.settings.language == L"ru" && edits.drafts.empty());
        store.failNext = 1;
        CHECK(edits.Commit(0, SettingsField::Language, 5) == 1);
        CHECK(store.settings.language == L"ru" && edits.drafts.size() == 1 && edits.drafts[0].failed &&
              edits.drafts[0].error == L"This change couldn't be saved.");
        // The failed choice shows with its error until the next change.
        CHECK(edits.Projection().language == L"ja");
        CHECK(edits.Reset(SettingsField::ResetBehavior) == 0);
        CHECK(store.settings.language.empty() && edits.drafts.empty());
    });

    Run("behavior page: the Language section first, Automatic naming what it shows, then English and every "
        "language by its own name; the field fits its widest option", [] {
        UiGraphics& g = TestGraphics();
        UiFrame frame = TestBehaviorFrame({});
        CHECK(frame.content[0].kind == UiItemKind::Text && UiRunsText(frame.content[0].runs) == L"Language");
        const UiItem& field = Control(frame, SettingsField::Language);
        CHECK(field.kind == UiItemKind::Dropdown && field.text == L"Automatic (English)" && field.action.value == 0);
        CHECK((TestOptionTexts(field) ==
               std::vector<std::wstring>{L"Automatic (English)", L"English", L"Deutsch", L"Espa\u00F1ol", L"Fran\u00E7ais",
                                         L"\u65E5\u672C\u8A9E", L"Portugu\u00EAs (Brasil)", L"\u0420\u0443\u0441\u0441\u043A\u0438\u0439",
                                         L"\u7B80\u4F53\u4E2D\u6587"}));
        for (size_t i = 0; i < field.options.size(); i++) CHECK(field.options[i].value == int(i));
        CHECK(HasText(frame, L"Interface language"));
        CHECK(HasText(frame, L"Automatic follows your Windows display language."));
        float widest = 0;
        for (const UiOption& option : field.options) {
            widest = std::max(widest, std::ceil(UiLayoutSize(UiLayout(g, option.text, UiTextStyleFor(UiFont::Body),
                                                                      10000, false)).width));
        }
        CHECK_NEAR(field.rect.right - field.rect.left, std::max(200.0f, widest + 41));
        // A stored explicit choice; the pseudo-locale only while it is the stored one.
        frame = TestBehaviorFrame({}, L"de", [](SettingsView& view) { view.settings.language = L"pt-BR"; });
        const UiItem& chosen = Control(frame, SettingsField::Language);
        CHECK(chosen.text == L"Portugu\u00EAs (Brasil)" && chosen.action.value == 6 &&
              chosen.options[0].text == L"Automatic (Deutsch)" && chosen.options.size() == 9);
        frame = TestBehaviorFrame({}, L"en", [](SettingsView& view) { view.settings.language = L"qps-ploc"; });
        const UiItem& pseudo = Control(frame, SettingsField::Language);
        CHECK(pseudo.options.size() == 10 && pseudo.options.back().text == L"Pseudo-locale" &&
              pseudo.options.back().value == kSettingsPseudoLanguage && pseudo.text == L"Pseudo-locale");
        // The reset card names the language too.
        CHECK(HasText(TestBehaviorFrame({}), L"Language, displays, hover details, polling and notifications."));
    });

    Run("behavior page in German: navigation, title, sections, cards, options, toggles and the reset; wider "
        "options widen their field, never past half the card", [] {
        UiGraphics& g = TestGraphics();
        UiFrame frame = TestBehaviorFrame(TestLocale(L"de"), L"de");
        std::vector<std::wstring> nav;
        for (const UiItem& item : frame.main) {
            if (item.kind == UiItemKind::Nav) nav.push_back(item.text);
        }
        CHECK((nav == std::vector<std::wstring>{L"Konten", L"Layout", L"Darstellung", L"Verhalten", L"Verlauf", L"Info"}));
        CHECK(UiRunsText(UiFindItem(frame, 100)->runs) == L"Verhalten");
        std::vector<std::wstring> sections;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Text && item.font == UiFont::BodyStrong) sections.push_back(UiRunsText(item.runs));
        }
        CHECK((sections == std::vector<std::wstring>{L"Sprache", L"Taskleiste", L"Details", L"Aktualisierungen",
                                                     L"Warnungen"}));
        CHECK(Control(frame, SettingsField::Language).text == L"Automatisch (Deutsch)");
        const UiItem& showOn = Control(frame, SettingsField::ShowOn);
        CHECK((TestOptionTexts(showOn) == std::vector<std::wstring>{L"Nur Hauptbildschirm", L"Alle Bildschirme",
                                                                     L"Bildschirm 1 \u2013 2560x1440 (Hauptbildschirm)",
                                                                     L"Bildschirm 2 \u2013 1920x1080"}));
        float widest = std::ceil(UiLayoutSize(UiLayout(g, showOn.options[2].text, UiTextStyleFor(UiFont::Body), 10000,
                                                       false)).width);
        float card = 0;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Card && UiContains(item.rect, {showOn.rect.left, showOn.rect.top})) {
                card = item.rect.right - item.rect.left;
            }
        }
        CHECK(widest + 41 > 260 && card > 0);
        CHECK_NEAR(showOn.rect.right - showOn.rect.left, std::min(widest + 41, card / 2));
        CHECK((TestOptionTexts(Control(frame, kPoll, 8)) ==
               std::vector<std::wstring>{L"2 Minuten", L"5 Minuten", L"10 Minuten", L"15 Minuten", L"30 Minuten",
                                         L"60 Minuten", L"Benutzerdefiniert"}));
        CHECK(Control(frame, kPoll, 3).runs[0].text == L"Minuten");
        CHECK(Control(frame, SettingsField::ShowDetailsOnHover).text == L"Ein");
        CHECK((TestOptionTexts(Control(frame, SettingsField::Notifications)) ==
               std::vector<std::wstring>{L"Aus", L"Gelb (50%)", L"Orange (75%)", L"Rot (90%)"}));
        CHECK(Control(frame, SettingsField::ResetBehavior).text == L"Zur\u00FCcksetzen");
        CHECK(HasText(frame, L"W\u00E4hlen Sie \u201EBenutzerdefiniert\u201C unter \u201ECloud-Abfrageintervall\u201C, um "
                             L"dies zu bearbeiten."));
        // The toggle's slot fits either state's text.
        UiToggleText states{L"Ein", L"Aus"};
        float state = std::max(UiLayoutSize(UiLayout(g, states.on, UiTextStyleFor(UiFont::Body), 1000, false)).width,
                               UiLayoutSize(UiLayout(g, states.off, UiTextStyleFor(UiFont::Body), 1000, false)).width);
        const UiItem& toggle = Control(frame, SettingsField::SkipHiddenPolling);
        CHECK_NEAR(toggle.rect.right - toggle.rect.left, std::ceil(state) + 12 + 40);
        CHECK_NEAR(UiToggleSize(g, states).width, std::ceil(state) + 12 + 40);
    });

    Run("behavior page plurals: the poll intervals and the custom box's unit agree with their numbers (Russian, "
        "Japanese)", [] {
        UiFrame ru = TestBehaviorFrame(TestLocale(L"ru"), L"ru",
                                       [](SettingsView& view) { view.settings.pollMinutes = 21; });
        CHECK((TestOptionTexts(Control(ru, kPoll, 8)) ==
               std::vector<std::wstring>{L"2 \u043C\u0438\u043D\u0443\u0442\u044B", L"5 \u043C\u0438\u043D\u0443\u0442",
                                         L"10 \u043C\u0438\u043D\u0443\u0442", L"15 \u043C\u0438\u043D\u0443\u0442",
                                         L"30 \u043C\u0438\u043D\u0443\u0442", L"60 \u043C\u0438\u043D\u0443\u0442",
                                         L"\u0414\u0440\u0443\u0433\u043E\u0439"}));
        CHECK(Control(ru, kPoll, 8).text == L"\u0414\u0440\u0443\u0433\u043E\u0439");
        CHECK(Control(ru, kPoll, 3).runs[0].text == L"\u043C\u0438\u043D\u0443\u0442\u0430");  // 21 минута
        ru = TestBehaviorFrame(TestLocale(L"ru"), L"ru", [](SettingsView& view) { view.settings.pollMinutes = 2; });
        CHECK(Control(ru, kPoll, 3).runs[0].text == L"\u043C\u0438\u043D\u0443\u0442\u044B");  // 2 минуты
        UiFrame ja = TestBehaviorFrame(TestLocale(L"ja"), L"ja");
        CHECK(Control(ja, kPoll, 8).options[0].text == L"2 \u5206" && Control(ja, kPoll, 3).runs[0].text == L"\u5206");
        CHECK(Control(ja, SettingsField::Language).text == L"\u81EA\u52D5 (\u65E5\u672C\u8A9E)");
    });

    Run("behavior page in the pseudo-locale: every text of the page transformed, none left English", [] {
        const I18nLanguage& pseudo = *I18nLanguageByTag(L"qps-ploc");
        UiFrame frame = TestBehaviorFrame(I18nLocale(pseudo), L"en");
        CHECK(UiRunsText(UiFindItem(frame, 100)->runs) == L"[\u0181\u00E9\u0125\u00E5\u1E7D\u00EE\u00F6\u0155 \u013C\u00F6]");
        for (const UiItem& item : frame.main) {
            if (item.kind == UiItemKind::Nav) CHECK(item.text.starts_with(L"[") && item.text.ends_with(L"]"));
        }
        for (const UiItem& item : frame.content) {
            std::wstring text = item.kind == UiItemKind::Text ? UiRunsText(item.runs) : item.text;
            bool word = std::any_of(text.begin(), text.end(), [](wchar_t c) { return c >= L'a' && c <= L'z'; });
            if (item.kind != UiItemKind::Glyph && word) CHECK(text.find(L'[') != std::wstring::npos);
        }
    });

    Run("toolkit locale: text formats take the graphics' locale; styles, plain and rich layouts are keyed by "
        "it, so another locale's never come back", [] {
        UiGraphics g;
        UiCreateGraphics(g);
        UiTextStyle body = UiTextStyleFor(UiFont::Body);
        CHECK(UiStyleKey(body, L"en-us") != UiStyleKey(body, L"zh-CN"));
        CHECK(UiRichKey({{L"a"}}, UiFont::Body, 100, DWRITE_TEXT_ALIGNMENT_LEADING, false, L"en-us") !=
              UiRichKey({{L"a"}}, UiFont::Body, 100, DWRITE_TEXT_ALIGNMENT_LEADING, false, L"zh-CN"));
        IDWriteTextFormat* english = UiFormat(g, body);
        IDWriteTextLayout* plain = UiLayout(g, L"\u8FD9\u4E2A", body, 100);
        IDWriteTextLayout* rich = UiRichLayout(g, {{L"\u8FD9\u4E2A"}}, UiFont::Body, 100);
        g.locale = L"zh-CN";
        IDWriteTextFormat* chinese = UiFormat(g, body);
        // DirectWrite may change the name's case.
        wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
        auto named = [&](IDWriteTextFormat* format, PCWSTR expected) {
            return SUCCEEDED(format->GetLocaleName(name, LOCALE_NAME_MAX_LENGTH)) &&
                   CompareStringOrdinal(name, -1, expected, -1, TRUE) == CSTR_EQUAL;
        };
        CHECK(named(chinese, L"zh-CN") && named(english, L"en-us"));
        CHECK(chinese != english && g.formats.size() == 2);
        CHECK(UiLayout(g, L"\u8FD9\u4E2A", body, 100) != plain);
        CHECK(UiRichLayout(g, {{L"\u8FD9\u4E2A"}}, UiFont::Body, 100) != rich);
        g.locale = L"en-US";
        CHECK(UiFormat(g, body) == english && UiLayout(g, L"\u8FD9\u4E2A", body, 100) == plain &&
              UiRichLayout(g, {{L"\u8FD9\u4E2A"}}, UiFont::Body, 100) == rich);
    });
}
