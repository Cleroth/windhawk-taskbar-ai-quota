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

// A test language that thanks the contributors last to first, its list patterns reversing them.
static constexpr I18nEntry kTestCatalogThanks[] = {
    {L"", L"Thanks to {names}.", L"{names}: thank you!"},
    {L"list-end", L"{a} and {b}", L"{b} with {a}"},
    {L"list-middle", L"{a}, {b}", L"{b}; {a}"},
    {L"list-start", L"{a}, {b}", L"{b}; {a}"},
};
static_assert(I18nCatalogSorted(kTestCatalogThanks));
static constexpr I18nLanguage kTestThanks{L"x-thanks", L"", L"en-US", I18nPluralRule::English, true,
                                          kTestCatalogThanks, false};

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
              ErrorText(edits.drafts[0]) == L"This change couldn't be saved.");
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

    Run("about page: the Contributors sentence in the language's word order and list patterns; each name links to "
        "its profile by its login, in the translation's focus order", [] {
        UiGraphics& g = TestGraphics();
        struct Shown {
            std::wstring text;
            std::vector<std::pair<std::wstring, std::wstring>> links;  // Focus order: name, URL
        };
        auto shown = [&](const I18nLocale& locale) {
            SettingsView view;
            view.page = SettingsUiPage::About;
            view.version = L"9.8.7";
            view.locale = locale;
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            Shown result;
            for (const UiItem& item : frame.content) {
                if (std::count_if(item.runs.begin(), item.runs.end(),
                                  [](const UiRun& run) { return run.style == UiRunStyle::Link; }) != 6) {
                    continue;
                }
                result.text = UiRunsText(item.runs);
                for (const UiFocusStop& stop : UiFocusStops(frame)) {
                    if (stop.ref.id != item.id) continue;
                    const UiAction* action = UiActionOf(frame, stop.ref);
                    CHECK(action && action->kind == UiActionKind::OpenUrl);
                    result.links.push_back({UiLinkRun(item.runs, stop.ref.link)->text, action ? action->text : L""});
                }
            }
            return result;
        };
        auto profile = [](PCWSTR name, PCWSTR login) {
            return std::pair<std::wstring, std::wstring>{name, std::wstring(L"https://github.com/") + login};
        };
        const std::vector<std::pair<std::wstring, std::wstring>> alphabetical = {
            profile(L"amirf147", L"amirf147"), profile(L"Hugh Macdonald", L"HughMacdonald"),
            profile(L"incconutwo", L"incconutwo"), profile(L"smnio", L"smnio"), profile(L"Sof2245", L"Sof2245"),
            profile(L"yalov", L"yalov")};
        Shown english = shown({});
        CHECK(english.text == L"Thanks to amirf147, Hugh Macdonald, incconutwo, smnio, Sof2245 and yalov.");
        CHECK(english.links == alphabetical);
        // Japanese: the names first, joined by its enumeration comma.
        Shown japanese = shown(TestLocale(L"ja"));
        CHECK(japanese.text == L"amirf147\u3001Hugh Macdonald\u3001incconutwo\u3001smnio\u3001Sof2245\u3001yalov "
                               L"\u306B\u611F\u8B1D\u3057\u307E\u3059\u3002");
        CHECK(japanese.links == alphabetical);
        // List patterns that reverse their items move each link, and its focus stop, with its name.
        Shown reversed = shown(I18nLocale(kTestThanks));
        CHECK(reversed.text == L"yalov with Sof2245; smnio; incconutwo; Hugh Macdonald; amirf147: thank you!");
        CHECK((reversed.links == std::vector<std::pair<std::wstring, std::wstring>>{
                   alphabetical[5], alphabetical[4], alphabetical[3], alphabetical[2], alphabetical[1],
                   alphabetical[0]}));
    });

    Run("history page in Russian: retention and time span options agree with their numbers, the custom box's unit "
        "with the saved days; the status keeps its color inside the translated line", [] {
        UiGraphics& g = TestGraphics();
        auto frameFor = [&](int days) {
            SettingsView view = TestView(SettingsUiPage::History);
            view.locale = TestLocale(L"ru");
            view.settings.historyRetentionDays = days;
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            return frame;
        };
        UiFrame frame = frameFor(21);
        const UiItem& keep = Control(frame, kKeep);
        CHECK((TestOptionTexts(keep) ==
               std::vector<std::wstring>{L"30 \u0434\u043D\u0435\u0439", L"90 \u0434\u043D\u0435\u0439",
                                         L"1 \u0433\u043E\u0434", L"\u0411\u0435\u0441\u0441\u0440\u043E\u0447\u043D\u043E",
                                         L"\u0414\u0440\u0443\u0433\u043E\u0439"}));
        CHECK(keep.text == L"\u0414\u0440\u0443\u0433\u043E\u0439");  // 21 days: Другой
        CHECK((TestOptionTexts(Control(frame, SettingsField::HistoryRange)) ==
               std::vector<std::wstring>{L"24 \u0447\u0430\u0441\u0430", L"7 \u0434\u043D\u0435\u0439",
                                         L"30 \u0434\u043D\u0435\u0439", L"90 \u0434\u043D\u0435\u0439",
                                         L"\u0412\u0441\u0451 \u0441\u043E\u0445\u0440\u0430\u043D\u0451\u043D\u043D\u043E\u0435"}));
        CHECK(Control(frame, kRetention, 3).runs[0].text == L"\u0434\u0435\u043D\u044C");  // 21 день
        CHECK(Control(frameFor(2), kRetention, 3).runs[0].text == L"\u0434\u043D\u044F");  // 2 дня
        CHECK(Control(frameFor(5), kRetention, 3).runs[0].text == L"\u0434\u043D\u0435\u0439");  // 5 дней
        CHECK(Control(frameFor(0), kRetention, 3).runs[0].text == L"\u0434\u043D\u0435\u0439");  // forever: 3650 дней
        // ● Запись · Новые замеры поступают при каждом опросе.
        const UiItem* status = nullptr;
        for (const UiItem& item : frame.content) {
            if (!item.runs.empty() && item.runs[0].text == L"\u25CF \u0417\u0430\u043F\u0438\u0441\u044C") status = &item;
        }
        CHECK(status && status->runs.size() == 3 && status->runs[0].ink == UiInk::Success &&
              status->runs[1].text == L" \u00B7 \u041D\u043E\u0432\u044B\u0435 \u0437\u0430\u043C\u0435\u0440\u044B "
                                      L"\u043F\u043E\u0441\u0442\u0443\u043F\u0430\u044E\u0442 \u043F\u0440\u0438 "
                                      L"\u043A\u0430\u0436\u0434\u043E\u043C \u043E\u043F\u0440\u043E\u0441\u0435.\n" &&
              status->runs[1].ink == UiInk::Inherit);
    });

    Run("settings notices: each code's text in English as before, read in the language shown when its box opens; "
        "a kept history's reason in the same language, inside the whole sentence", [] {
        using enum SettingsNoticeCode;
        const std::pair<SettingsNoticeCode, PCWSTR> english[] = {
            {AccountsDropped, L"An account is no longer configured; its unsaved changes were dropped."},
            {AccountDropped, L"This account is no longer configured; its unsaved changes were dropped."},
            {AccountNotSaved, L"Could not save the account. The change is kept here with its error; change it again "
                              L"to retry."},
            {AccountExists, L"That provider and label are already configured."},
            {AccountSaveFailed, L"Could not save the account."},
            {ChangedElsewhere, L"The account was changed from another taskbar window."},
            {IdentitySignInRunning, L"Wait for the current sign-in to finish before changing identity."},
            {RenameTargetSignedIn, L"The renamed account identity already has a retained sign-in. To replace it, add "
                                   L"an account with that provider and label, then remove it. The account was not "
                                   L"changed."},
            {RenameCopyFailed, L"The stored sign-in could not be copied. The account was not changed."},
            {RenameCopyLeft, L"Could not save the account. The copied sign-in remains under the attempted new "
                             L"identity. To delete it, add an account with the attempted provider and label, then "
                             L"remove it."},
            {RenameOldSignInLeft, L"The account was renamed, but a duplicate stored sign-in remains under the old "
                                  L"identity. To delete it, re-add an account with the old provider and label, then "
                                  L"remove it."},
            {RemoveSignInRunning, L"Wait for the current sign-in to finish before removing accounts."},
            {RemoveSignInKept, L"The stored sign-in could not be deleted, so the account was not removed."},
            {RemoveSignedOut, L"The stored sign-in was deleted, but settings could not be saved. The account remains "
                              L"configured but signed out."},
            {RemoveFailed, L"Could not remove the account."},
            {ReorderFailed, L"Could not reorder the account."},
            {VisibilityFailed, L"Could not update account visibility."},
            {SignOutFailed, L"The stored sign-in could not be deleted and remains retained for this account."},
        };
        for (auto [code, text] : english) CHECK_TEXT(SettingsNoticeText({}, code), text);
        CHECK(SettingsNoticeText({}, None).empty());
        // A removal whose history stays: the controller keeps the code and the journal's error.
        TestStore store;
        AddTestAccounts(store.settings);
        store.historyKept = true;
        SettingsEdits edits(store);
        edits.Reload();
        CHECK(edits.AccountAction(11, SettingsField::AccountRemove, 1) == SettingsActionResult::Done);
        CHECK(edits.notices.size() == 1 && edits.notices[0].code == HistoryKept &&
              edits.notices[0].reason == HistoryJournalError::CannotWrite);
        const SettingsNotice& kept = edits.notices[0];
        CHECK_TEXT(SettingsNoticeText({}, kept.code, kept.reason),
                   L"The account was removed, but its usage history could not be deleted and was kept.\n\nCannot "
                   L"write history. Check disk space and storage permissions.");
        CHECK_TEXT(SettingsNoticeText(TestLocale(L"de"), kept.code, kept.reason),
                   L"Das Konto wurde entfernt, aber sein Nutzungsverlauf konnte nicht gel\u00F6scht werden und wurde "
                   L"behalten.\n\nDer Verlauf kann nicht geschrieben werden. Pr\u00FCfen Sie Speicherplatz und "
                   L"Speicherberechtigungen.");
        CHECK_TEXT(SettingsNoticeText(TestLocale(L"ru"), RemoveSignInRunning),
                   L"\u0414\u043E\u0436\u0434\u0438\u0442\u0435\u0441\u044C \u0437\u0430\u0432\u0435\u0440\u0448\u0435"
                   L"\u043D\u0438\u044F \u0442\u0435\u043A\u0443\u0449\u0435\u0433\u043E \u0432\u0445\u043E\u0434\u0430, "
                   L"\u043F\u0440\u0435\u0436\u0434\u0435 \u0447\u0435\u043C \u0443\u0434\u0430\u043B\u044F\u0442\u044C "
                   L"\u0430\u043A\u043A\u0430\u0443\u043D\u0442\u044B.");
    });

    Run("validation: a draft keeps its error as a code, so each frame shows it in its own language and a language "
        "switch re-reads it; English as before", [] {
        UiGraphics& g = TestGraphics();
        TestStore store;
        AddTestAccounts(store.settings);
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kPoll, L"12a");
        CHECK(!edits.RequestCommit(0, kPoll) && edits.drafts[0].error == SettingsDraftError::Range);
        edits.SetText(12, SettingsField::AccountLabel, L" ");
        CHECK(!edits.RequestCommit(12, SettingsField::AccountLabel) &&
              edits.drafts[1].error == SettingsDraftError::LabelEmpty);
        auto frameFor = [&](SettingsUiPage page, const I18nLocale& locale) {
            SettingsView view = TestView(page);
            view.locale = locale;
            view.settings = edits.Projection();
            view.drafts = edits.drafts;
            for (size_t i = 0; i < edits.keys.size(); i++) {
                view.accounts.push_back({edits.keys[i], edits.SlotOf(edits.keys[i]), {}, edits.keys[i] == 12});
            }
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            return frame;
        };
        CHECK(HasText(frameFor(SettingsUiPage::Behavior, {}), L"Enter a whole number from 2 to 1440."));
        UiFrame german = frameFor(SettingsUiPage::Behavior, TestLocale(L"de"));
        CHECK(HasText(german, L"Geben Sie eine ganze Zahl von 2 bis 1440 ein.") &&
              !HasText(german, L"Enter a whole number"));
        CHECK(HasText(frameFor(SettingsUiPage::Behavior, TestLocale(L"ru")),
                      L"\u0412\u0432\u0435\u0434\u0438\u0442\u0435 \u0446\u0435\u043B\u043E\u0435 \u0447\u0438\u0441"
                      L"\u043B\u043E \u043E\u0442 2 \u0434\u043E 1440."));
        CHECK(HasText(frameFor(SettingsUiPage::Behavior, {}), L"Enter a whole number from 2 to 1440."));
        // The label's error on its expanded card, the same way.
        CHECK(HasText(frameFor(SettingsUiPage::Accounts, {}), L"Enter a label."));
        UiFrame russian = frameFor(SettingsUiPage::Accounts, TestLocale(L"ru"));
        CHECK(HasText(russian, L"\u0412\u0432\u0435\u0434\u0438\u0442\u0435 \u043C\u0435\u0442\u043A\u0443.") &&
              !HasText(russian, L"Enter a label."));
        CHECK(HasText(frameFor(SettingsUiPage::Accounts, TestLocale(L"de")), L"Geben Sie eine Bezeichnung ein."));
    });
}
