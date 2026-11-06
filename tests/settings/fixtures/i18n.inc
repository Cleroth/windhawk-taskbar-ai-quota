// Localization engine (src/i18n/): the grammar and its escapes, plural rules, whole-entry English
// fallback, links, arguments, lists, matching, the pseudo-locale and lookup. Test-only catalogs
// stand in for translations; every expected string is written out by hand.

// Expected runs leave out their defaults.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"

static std::string LocUtf8(std::wstring_view text) {
    std::string out(text.size() * 3, '\0');
    out.resize(size_t(std::max(WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(),
                                                   int(out.size()), nullptr, nullptr), 0)));
    return out;
}

static std::string LocDump(const std::vector<I18nRun>& runs) {
    std::string out;
    for (const I18nRun& run : runs) {
        out += "[" + LocUtf8(run.text) + (run.bold ? "|b" : "") + (run.link.empty() ? "" : "|" + LocUtf8(run.link)) + "]";
    }
    return out;
}

#define CHECK_TEXT(actual, expected) do { ++assertions; std::wstring a_ = (actual), e_ = (expected); \
    if (a_ != e_) throw std::runtime_error("line " + std::to_string(__LINE__) + ": \"" + LocUtf8(a_) + \
    "\", expected \"" + LocUtf8(e_) + "\""); } while (false)
#define CHECK_RUNS(actual, ...) do { ++assertions; std::vector<I18nRun> a_ = (actual), e_ = __VA_ARGS__; \
    if (a_ != e_) throw std::runtime_error("line " + std::to_string(__LINE__) + ": " + LocDump(a_) + \
    ", expected " + LocDump(e_)); } while (false)

// Test catalogs, sorted like production ones. Broken entries name what is wrong with them.
static constexpr I18nEntry kTestCatalogDe[] = {
    {L"", L"<link id=\"open\">Open</link> or <link id=\"copy\">copy</link> the link.",
     L"Den Link <link id=\"copy\">kopieren</link> oder <link id=\"open\">öffnen</link>."},
    {L"", L"Broken {name}", L"Kaputt {name"},  // Syntax error
    {L"", L"Credits: {names}", L"Dank: {names} und {names}"},  // Repeated more than in English
    {L"", L"Delete {name}: {n, plural, one{# file} other{# files}}",  // Missing from other
     L"{n, plural, one{{name} löschen: # Datei} other{Alle löschen}}"},
    {L"", L"Done", L"Fertig {name}"},  // An argument English doesn't have
    {L"", L"Empty", L""},
    {L"", L"Hello {name}", L"Hallo {name}"},
    {L"", L"Open", L"Öffnen"},
    {L"", L"Read <link id=\"guide\">the guide</link>.",  // The link twice
     L"Lies <link id=\"guide\">die Anleitung</link> und <link id=\"guide\">mehr</link>."},
    {L"", L"Signed in as {name}", L"Angemeldet als {user}"},  // Another argument name
    {L"", L"Synced {n, plural, one{# account} other{# accounts}}",  // few: not a German category
     L"{n, plural, one{# Konto} few{# Konten} other{# Konten}} synchronisiert"},
    {L"", L"Thanks to {names}!", L"Danke an {names}!"},
    {L"", L"Thanks to {names}. <link id=\"guide\">Read more</link>.",  // The names moved into the link
     L"<link id=\"guide\">Danke an {names}. Mehr lesen</link>."},
    {L"", L"Use <link id=\"docs\">the docs</link>.", L"Nutze <link id=\"doku\">die Doku</link>."},  // Another link
    {L"", L"Welcome, {name}", L"Willkommen"},  // The argument dropped
    {L"", L"{n, plural, one{# day} other{# days}}", L"{n, plural, one{# Tag} other{# Tage}}"},
    {L"", L"{n, plural, one{{name} has # file} other{{name} has # files}}",
     L"{name} hat {n, plural, one{# Datei} other{# Dateien}}"},
    // Twice for 1, never otherwise: English shows the name once for every number.
    {L"", L"{n, plural, one{{name}} other{}}{n, plural, one{} other{{name}}}", L"{n, plural, one{{name}{name}} other{}}"},
    {L"state", L"Open", L"Offen"},
};
static constexpr I18nEntry kTestCatalogEs[] = {
    {L"", L"{n, plural, one{# day} other{# days}}", L"{n, plural, one{# día} other{# días}}"},
    {L"", L"{n, plural, one{# unit} other{# units}}",
     L"{n, plural, one{# unidad} many{# de unidades} other{# unidades}}"},
};
static constexpr I18nEntry kTestCatalogFr[] = {
    {L"", L"{n, plural, one{# day} other{# days}}", L"{n, plural, one{# jour} other{# jours}}"},
    {L"", L"{n, plural, one{# file} other{# files}}", L"{n, plural, one{# fichier}}"},  // No other
};
static constexpr I18nEntry kTestCatalogJa[] = {
    {L"", L"{n, plural, one{# day} other{# days}}", L"{n, plural, other{#日}}"},
    {L"", L"{n, plural, one{# file} other{# files}}", L"{n, plural, one{#ファイル} other{#ファイル}}"},  // one
    {L"", L"{n, plural, one{# hour} other{# hours}}", L"{n}時間"},  // The plural dropped
};
static constexpr I18nEntry kTestCatalogPtBr[] = {
    {L"", L"{n, plural, one{# day} other{# days}}", L"{n, plural, one{# dia} other{# dias}}"},
};
static constexpr I18nEntry kTestCatalogRu[] = {
    {L"", L"Account {name}: {n, plural, one{# file} other{# files}}",  // other, which a non-number picks, drops the name
     L"{n, plural, one{{name}: # файл} few{{name}: # файла} many{{name}: # файлов} other{}}"},
    {L"", L"{n, plural, one{# <link id=\"x\">account</link>} other{# <link id=\"x\">accounts</link>}}",
     // few lacks the link
     L"{n, plural, one{# <link id=\"x\">аккаунт</link>} few{# аккаунта} many{# <link id=\"x\">аккаунтов</link>} "
     L"other{# <link id=\"x\">аккаунта</link>}}"},
    {L"", L"{n, plural, one{# day} other{# days}}", L"{n, plural, one{# день} few{# дня} many{# дней} other{# дня}}"},
};
static constexpr I18nEntry kTestCatalogZhHans[] = {
    {L"", L"{n, plural, one{# day} other{# days}}", L"{n, plural, other{#天}}"},
};
static_assert(I18nCatalogSorted(kTestCatalogDe) && I18nCatalogSorted(kTestCatalogEs) &&
              I18nCatalogSorted(kTestCatalogFr) && I18nCatalogSorted(kTestCatalogJa) &&
              I18nCatalogSorted(kTestCatalogPtBr) && I18nCatalogSorted(kTestCatalogRu) &&
              I18nCatalogSorted(kTestCatalogZhHans));

static constexpr I18nLanguage kTestDe{L"de", L"", L"de-DE", I18nPluralRule::English, true, kTestCatalogDe, false};
static constexpr I18nLanguage kTestEs{L"es", L"", L"es-ES", I18nPluralRule::Spanish, true, kTestCatalogEs, false};
static constexpr I18nLanguage kTestFr{L"fr", L"", L"fr-FR", I18nPluralRule::French, true, kTestCatalogFr, false};
static constexpr I18nLanguage kTestJa{L"ja", L"", L"ja-JP", I18nPluralRule::OtherOnly, true, kTestCatalogJa, false};
static constexpr I18nLanguage kTestPtBr{L"pt-BR", L"", L"pt-BR", I18nPluralRule::French, true, kTestCatalogPtBr, false};
static constexpr I18nLanguage kTestRu{L"ru", L"", L"ru-RU", I18nPluralRule::Russian, true, kTestCatalogRu, false};
static constexpr I18nLanguage kTestZhHans{L"zh-Hans", L"", L"zh-CN", I18nPluralRule::OtherOnly, true,
                                          kTestCatalogZhHans, false};

static std::wstring LocDays(const I18nLocale& locale, long long n) {
    return I18nText(locale, Tr(L"{n, plural, one{# day} other{# days}}"), {{L"n", n}});
}

static bool LocValid(std::wstring_view text, I18nPluralRule rule = I18nPluralRule::English) {
    return I18nTemplateShape(text, rule).valid;
}

static std::vector<std::vector<I18nRun>> LocItems(std::initializer_list<std::wstring_view> names) {
    std::vector<std::vector<I18nRun>> items;
    for (std::wstring_view name : names) items.push_back({{std::wstring(name)}});
    return items;
}

static const I18nLanguage& LocLanguage(std::wstring_view tag) {
    const I18nLanguage* language = I18nLanguageByTag(tag);
    if (!language) throw std::runtime_error("no language " + LocUtf8(tag));
    return *language;
}

static void RunI18nTests() {
    const I18nLocale en;

    Run("i18n grammar: text, escapes, arguments and malformed templates", [&] {
        CHECK_TEXT(I18nText(en, Tr(L"Plain text")), L"Plain text");
        CHECK_TEXT(I18nText(en, Tr(L"\\{literal\\} \\<b> \\# \\\\ > \"")), L"{literal} <b> # \\ > \"");
        // '#' is literal outside a plural, the number inside one unless escaped.
        CHECK_TEXT(I18nText(en, Tr(L"Issue #4 of {n}"), {{L"n", 9}}), L"Issue #4 of 9");
        CHECK_TEXT(I18nText(en, Tr(L"{n,plural,other{#\\# #}}"), {{L"n", 3}}), L"3# 3");
        // Spaces inside placeholders; repeated arguments.
        CHECK_TEXT(I18nText(en, Tr(L"{ name }/{name}/{name}"), {{L"name", L"x"}}), L"x/x/x");
        CHECK_TEXT(I18nText(en, Tr(L"{n, plural, one {# day} other {# days} } left"), {{L"n", 1}}), L"1 day left");
        for (std::wstring_view bad : {
                 L"{", L"}", L"a}b", L"{}", L"{ }", L"{1x}", L"{a", L"{a b}", L"{a,}", L"{a, plural}",
                 L"{a, plural,}", L"{a, plural, other{x}", L"{a, plural, one{x}}", L"{a, plural, other{x} other{y}}",
                 L"{a, plural, eleven{x} other{y}}", L"{a, plural, other x}", L"{a, select, other{x}}",
                 L"{a, plural, other{{b, plural, other{x}}}}", L"<b>", L"<b></b>", L"<b>x", L"x</b>", L"<i>x</i>",
                 L"<link id=x>y</link>", L"<link id=\"\">y</link>", L"<link id=\"a\" >y</link>",
                 L"<link id=\"a\">y</b>", L"<link id=\"a\"></link>", L"<b><b>x</b></b>",
                 L"<link id=\"a\"><link id=\"b\">x</link></link>", L"<b><link id=\"a\">x</link></b>",
                 L"<link id=\"a\"><b>x</b></link>", L"{n, plural, other{<b><link id=\"a\">x</link></b>}}",
                 L"<b>{a, plural, other{x</b>}}", L"a < b",
                 L"\\x", L"trailing \\"}) {
            CHECK(!LocValid(bad));
        }
        for (std::wstring_view good : {
                 L"", L"a > b # c", L"<b>x</b><link id=\"a\">y</link>",
                 L"{n, plural, one{<b>#</b> day} other{# days}}", L"<b>{n, plural, other{#}}</b>",
                 L"<link id=\"a\">{n, plural, one{# day} other{# days}}</link>",
                 L"{a}{b}{a, plural, other{#}}", L"<link id=\"a_1\">{x}</link>"}) {
            CHECK(LocValid(good));
        }
    });

    Run("i18n grammar: shapes, links once per expansion, plural categories and limits", [] {
        I18nShape shape = I18nTemplateShape(L"{a} {n, plural, one{<link id=\"x\">#</link>} other{<link id=\"x\">#s</link>}} "
                                            L"<link id=\"y\">{b}</link>", I18nPluralRule::English);
        CHECK(shape.valid && shape.args.count == 3 && shape.args.Has(L"a") && shape.args.Has(L"n") &&
              shape.args.Has(L"b"));
        CHECK(shape.plurals.count == 1 && shape.plurals.Has(L"n"));
        CHECK(shape.links.count == 2 && shape.links.Has(L"x") && shape.links.Has(L"y"));
        // Uses per expansion: a once outside the plural and once in other; b twice in one only; the
        // selector's '#' and plural aren't uses.
        shape = I18nTemplateShape(L"{a} {n, plural, one{{b}{b}#} other{{a}}}", I18nPluralRule::English);
        size_t a = shape.args.Find(L"a"), b = shape.args.Find(L"b"), n = shape.args.Find(L"n");
        CHECK(shape.valid && shape.fewest[a] == 1 && shape.most[a] == 2 && shape.fewest[b] == 0 &&
              shape.most[b] == 2 && shape.fewest[n] == 0 && shape.most[n] == 0);
        // Plurals on one selector pick together; a missing category picks other, and so does a
        // selector that isn't a number, Russian's included.
        auto uses = [](std::wstring_view text, I18nPluralRule rule) {
            I18nShape shape = I18nTemplateShape(text, rule);
            size_t i = shape.args.Find(L"a");
            return shape.valid ? std::pair(shape.fewest[i], shape.most[i]) : std::pair(size_t(99), size_t(99));
        };
        using Uses = std::pair<size_t, size_t>;
        CHECK(uses(L"{n, plural, one{{a}} other{}}{n, plural, one{} other{{a}}}", I18nPluralRule::English) == Uses(1, 1));
        CHECK(uses(L"{n, plural, one{{a}} other{}}{m, plural, one{} other{{a}}}", I18nPluralRule::English) == Uses(0, 2));
        CHECK(uses(L"{n, plural, one{{a}{a}} other{}}", I18nPluralRule::English) == Uses(0, 2));
        CHECK(uses(L"{n, plural, other{{a}}}", I18nPluralRule::Russian) == Uses(1, 1));
        CHECK(uses(L"{n, plural, one{} other{{a}}}", I18nPluralRule::English) == Uses(0, 1));
        CHECK(uses(L"{n, plural, one{{a}} few{{a}} many{{a}} other{}}", I18nPluralRule::Russian) == Uses(0, 1));
        CHECK(uses(L"{n, plural, many{{a}} other{}}", I18nPluralRule::French) == Uses(0, 1));
        CHECK(!LocValid(L"<link id=\"x\">a</link><link id=\"x\">b</link>"));
        CHECK(!LocValid(L"{n, plural, one{<link id=\"x\">a</link>} other{b}}"));
        CHECK(!LocValid(L"{n, plural, one{<link id=\"x\">a</link>} other{<link id=\"y\">b</link>}}"));
        CHECK(!LocValid(L"{n, plural, other{<link id=\"x\">a</link><link id=\"x\">b</link>}}"));
        CHECK(!LocValid(L"<link id=\"x\">a</link>{n, plural, other{<link id=\"x\">b</link>}}"));
        CHECK(!LocValid(L"{n, plural, other{<link id=\"x\">a</link>}} {m, plural, other{<link id=\"x\">b</link>}}"));
        CHECK(LocValid(L"{n, plural, one{<link id=\"x\">a</link>} other{<link id=\"x\">b</link>}}"));
        CHECK(LocValid(L"{n, plural, other{<link id=\"x\">a</link>}} {m, plural, other{<link id=\"y\">b</link>}}"));
        // Categories per rule: English, Spanish, French, Russian, OtherOnly.
        constexpr I18nPluralRule kRules[] = {I18nPluralRule::English, I18nPluralRule::Spanish, I18nPluralRule::French,
                                             I18nPluralRule::Russian, I18nPluralRule::OtherOnly};
        const std::pair<std::wstring_view, std::array<bool, 5>> kCategories[] = {
            {L"{n, plural, other{b}}", {true, true, true, true, true}},
            {L"{n, plural, one{a} other{b}}", {true, true, true, true, false}},
            {L"{n, plural, many{a} other{b}}", {false, true, true, true, false}},
            {L"{n, plural, few{a} other{b}}", {false, false, false, true, false}},
            {L"{n, plural, zero{a} other{b}}", {false, false, false, false, false}},
            {L"{n, plural, two{a} other{b}}", {false, false, false, false, false}},
        };
        for (const auto& [text, valid] : kCategories) {
            for (size_t i = 0; i < std::size(kRules); i++) CHECK(LocValid(text, kRules[i]) == valid[i]);
        }
        // At most 16 distinct argument names, 16 link IDs and 4 plural selectors.
        CHECK(LocValid(L"{a, plural, other{}}{b, plural, other{}}{c, plural, other{}}{d, plural, other{}}{a, plural, other{}}"));
        CHECK(!LocValid(L"{a, plural, other{}}{b, plural, other{}}{c, plural, other{}}{d, plural, other{}}{e, plural, other{}}"));
        CHECK(LocValid(L"{a}{b}{c}{d}{e}{f}{g}{h}{i}{j}{k}{l}{m}{n}{o}{p}{a}"));
        CHECK(!LocValid(L"{a}{b}{c}{d}{e}{f}{g}{h}{i}{j}{k}{l}{m}{n}{o}{p}{q}"));
        std::wstring links;
        for (int i = 0; i < 16; i++) links += L"<link id=\"l" + std::to_wstring(i) + L"\">x</link>";
        CHECK(LocValid(links));
        CHECK(!LocValid(links + L"<link id=\"l16\">x</link>"));
    });

    Run("i18n plural rules: integer CLDR categories per language", [] {
        using enum I18nPlural;
        auto categories = [](I18nPluralRule rule, std::initializer_list<unsigned long long> numbers) {
            std::vector<I18nPlural> out;
            for (unsigned long long n : numbers) out.push_back(I18nPluralCategory(rule, n));
            return out;
        };
        using Categories = std::vector<I18nPlural>;
        CHECK(categories(I18nPluralRule::English, {0, 1, 2, 11, 21}) == Categories({Other, One, Other, Other, Other}));
        CHECK(categories(I18nPluralRule::Spanish, {0, 1, 2, 1000000, 2000000, 1000001}) ==
              Categories({Other, One, Other, Many, Many, Other}));
        CHECK(categories(I18nPluralRule::French, {0, 1, 2, 1000000}) == Categories({One, One, Other, Many}));
        CHECK(categories(I18nPluralRule::Russian, {1, 2, 5, 11, 21, 22, 25, 111, 0, 12, 14, 101, 112, 122}) ==
              Categories({One, Few, Many, Many, One, Few, Many, Many, Many, Many, Many, One, Many, Few}));
        CHECK(categories(I18nPluralRule::OtherOnly, {0, 1, 2}) == Categories({Other, Other, Other}));
        const std::pair<std::wstring_view, I18nPluralRule> kRules[] = {
            {L"en", I18nPluralRule::English}, {L"de", I18nPluralRule::English}, {L"es", I18nPluralRule::Spanish},
            {L"fr", I18nPluralRule::French},  {L"pt-BR", I18nPluralRule::French}, {L"ru", I18nPluralRule::Russian},
            {L"ja", I18nPluralRule::OtherOnly}, {L"zh-Hans", I18nPluralRule::OtherOnly},
        };
        for (const auto& [tag, rule] : kRules) CHECK(LocLanguage(tag).plural == rule);
    });

    Run("i18n plurals formatted per language", [&] {
        const I18nLocale de(kTestDe), es(kTestEs), fr(kTestFr), ja(kTestJa), pt(kTestPtBr), ru(kTestRu), zh(kTestZhHans);
        CHECK_TEXT(LocDays(en, 0), L"0 days");
        CHECK_TEXT(LocDays(en, 1), L"1 day");
        CHECK_TEXT(LocDays(en, 2), L"2 days");
        CHECK_TEXT(LocDays(de, 0), L"0 Tage");
        CHECK_TEXT(LocDays(de, 1), L"1 Tag");
        CHECK_TEXT(LocDays(de, 2), L"2 Tage");
        CHECK_TEXT(LocDays(es, 0), L"0 días");
        CHECK_TEXT(LocDays(es, 1), L"1 día");
        CHECK_TEXT(LocDays(es, 2), L"2 días");
        CHECK_TEXT(LocDays(es, 1000000), L"1000000 días");  // No many branch: other
        auto units = [&](long long n) { return I18nText(es, Tr(L"{n, plural, one{# unit} other{# units}}"), {{L"n", n}}); };
        CHECK_TEXT(units(1), L"1 unidad");
        CHECK_TEXT(units(2), L"2 unidades");
        CHECK_TEXT(units(1000000), L"1000000 de unidades");
        CHECK_TEXT(units(1000001), L"1000001 unidades");
        CHECK_TEXT(LocDays(fr, 0), L"0 jour");
        CHECK_TEXT(LocDays(fr, 1), L"1 jour");
        CHECK_TEXT(LocDays(fr, 2), L"2 jours");
        CHECK_TEXT(LocDays(pt, 0), L"0 dia");
        CHECK_TEXT(LocDays(pt, 1), L"1 dia");
        CHECK_TEXT(LocDays(pt, 2), L"2 dias");
        const std::pair<long long, std::wstring_view> kRussian[] = {
            {1, L"1 день"}, {2, L"2 дня"}, {5, L"5 дней"}, {11, L"11 дней"},
            {21, L"21 день"}, {22, L"22 дня"}, {25, L"25 дней"}, {111, L"111 дней"},
        };
        for (const auto& [n, text] : kRussian) CHECK_TEXT(LocDays(ru, n), std::wstring(text));
        CHECK_TEXT(LocDays(ja, 0), L"0日");
        CHECK_TEXT(LocDays(ja, 1), L"1日");
        CHECK_TEXT(LocDays(ja, 2), L"2日");
        CHECK_TEXT(LocDays(zh, 0), L"0天");
        CHECK_TEXT(LocDays(zh, 1), L"1天");
        CHECK_TEXT(LocDays(zh, 2), L"2天");
    });

    Run("i18n fallback: the whole entry in English with English plural rules", [] {
        const I18nLocale de(kTestDe), fr(kTestFr), ja(kTestJa), ru(kTestRu);
        // Missing entries.
        auto nights = [](const I18nLocale& locale, long long n) {
            return I18nText(locale, Tr(L"{n, plural, one{# night} other{# nights}}"), {{L"n", n}});
        };
        CHECK_TEXT(nights(de, 1), L"1 night");
        CHECK_TEXT(nights(de, 2), L"2 nights");
        CHECK_TEXT(nights(ja, 1), L"1 night");
        CHECK_TEXT(nights(fr, 0), L"0 nights");
        // Arguments: renamed, dropped, added; a syntax error; an empty translation.
        CHECK_TEXT(I18nText(de, Tr(L"Signed in as {name}"), {{L"name", L"Alice"}}), L"Signed in as Alice");
        CHECK_TEXT(I18nText(de, Tr(L"Welcome, {name}"), {{L"name", L"Alice"}}), L"Welcome, Alice");
        CHECK_TEXT(I18nText(de, Tr(L"Done"), {{L"name", L"Alice"}}), L"Done");
        CHECK_TEXT(I18nText(de, Tr(L"Broken {name}"), {{L"name", L"X"}}), L"Broken X");
        CHECK_TEXT(I18nText(de, Tr(L"Empty")), L"Empty");
        CHECK_TEXT(I18nText(de, Tr(L"Hello {name}"), {{L"name", L"Alice"}}), L"Hallo Alice");
        // Link sets: another ID, one ID twice.
        CHECK_RUNS(I18nRuns(de, Tr(L"Use <link id=\"docs\">the docs</link>.")),
                   {{L"Use "}, {L"the docs", false, L"docs"}, {L"."}});
        CHECK_RUNS(I18nRuns(de, Tr(L"Read <link id=\"guide\">the guide</link>.")),
                   {{L"Read "}, {L"the guide", false, L"guide"}, {L"."}});
        // A link missing from one plural branch: Russian would read 21 as one.
        auto accounts = [&](long long n) {
            return I18nRuns(ru, Tr(L"{n, plural, one{# <link id=\"x\">account</link>} other{# <link id=\"x\">accounts</link>}}"),
                            {{L"n", n}});
        };
        CHECK_RUNS(accounts(21), {{L"21 "}, {L"accounts", false, L"x"}});
        CHECK_RUNS(accounts(1), {{L"1 "}, {L"account", false, L"x"}});
        CHECK_TEXT(LocDays(ru, 21), L"21 день");
        // A plural without other: French would read 0 as one.
        auto files = [](const I18nLocale& locale, long long n) {
            return I18nText(locale, Tr(L"{n, plural, one{# file} other{# files}}"), {{L"n", n}});
        };
        CHECK_TEXT(files(fr, 0), L"0 files");
        CHECK_TEXT(files(fr, 1), L"1 file");
        CHECK_TEXT(LocDays(fr, 0), L"0 jour");
        // A category the language doesn't have: German few, Japanese one (Japanese would read 1 as
        // other).
        auto synced = [&](long long n) {
            return I18nText(de, Tr(L"Synced {n, plural, one{# account} other{# accounts}}"), {{L"n", n}});
        };
        CHECK_TEXT(synced(1), L"Synced 1 account");
        CHECK_TEXT(synced(3), L"Synced 3 accounts");
        CHECK_TEXT(files(ja, 1), L"1 file");
        CHECK_TEXT(files(ja, 2), L"2 files");
        // An argument English always shows missing from a branch; a linked list repeated. Moving an
        // argument out of the plural keeps it in every expansion.
        CHECK_TEXT(I18nText(de, Tr(L"Delete {name}: {n, plural, one{# file} other{# files}}"),
                            {{L"name", L"X"}, {L"n", 2}}), L"Delete X: 2 files");
        std::vector<I18nRun> names = {{L"Ann", false, L"c0"}};
        CHECK_RUNS(I18nRuns(de, Tr(L"Credits: {names}"), {{L"names", names}}), {{L"Credits: "}, {L"Ann", false, L"c0"}});
        CHECK_TEXT(I18nText(de, Tr(L"{n, plural, one{{name} has # file} other{{name} has # files}}"),
                            {{L"name", L"X"}, {L"n", 2}}), L"X hat 2 Dateien");
        // A linked argument moved into a link would split it; a branch a non-number picks dropping one.
        std::vector<I18nRun> people = {{L"Ann", false, L"c0"}};
        CHECK_RUNS(I18nRuns(de, Tr(L"Thanks to {names}. <link id=\"guide\">Read more</link>."), {{L"names", people}}),
                   {{L"Thanks to "}, {L"Ann", false, L"c0"}, {L". "}, {L"Read more", false, L"guide"}, {L"."}});
        CHECK_TEXT(I18nText(I18nLocale(kTestRu), Tr(L"Account {name}: {n, plural, one{# file} other{# files}}"),
                            {{L"name", L"X"}}), L"Account X: {n} files");
        auto correlated = Tr(L"{n, plural, one{{name}} other{}}{n, plural, one{} other{{name}}}");
        CHECK_TEXT(I18nText(de, correlated, {{L"name", L"X"}, {L"n", 1}}), L"X");
        CHECK_TEXT(I18nText(de, correlated, {{L"name", L"X"}, {L"n", 2}}), L"X");
        // A plural turned into a plain argument.
        auto hours = [&](long long n) { return I18nText(ja, Tr(L"{n, plural, one{# hour} other{# hours}}"), {{L"n", n}}); };
        CHECK_TEXT(hours(1), L"1 hour");
        CHECK_TEXT(hours(3), L"3 hours");
    });

    Run("i18n links: rich runs and reordering", [&] {
        auto key = Tr(L"Read <b>the guide</b> or <link id=\"open\">open it</link>.");
        CHECK_RUNS(I18nRuns(en, key), {{L"Read "}, {L"the guide", true}, {L" or "}, {L"open it", false, L"open"}, {L"."}});
        CHECK_TEXT(I18nText(en, key), L"Read the guide or open it.");
        CHECK_RUNS(I18nRuns(en, Tr(L"<b>a</b> <link id=\"x\">b</link> c")),
                   {{L"a", true}, {L" "}, {L"b", false, L"x"}, {L" c"}});
        CHECK_RUNS(I18nRuns(en, Tr(L"<b>a</b><b>b</b>{x}"), {{L"x", L"c"}}), {{L"ab", true}, {L"c"}});
        auto links = Tr(L"<link id=\"open\">Open</link> or <link id=\"copy\">copy</link> the link.");
        CHECK_RUNS(I18nRuns(en, links), {{L"Open", false, L"open"}, {L" or "}, {L"copy", false, L"copy"}, {L" the link."}});
        CHECK_RUNS(I18nRuns(I18nLocale(kTestDe), links),
                   {{L"Den Link "}, {L"kopieren", false, L"copy"}, {L" oder "}, {L"öffnen", false, L"open"}, {L"."}});
    });

    Run("i18n arguments: literal text, numbers, rich runs and missing ones", [&] {
        const I18nLocale de(kTestDe);
        CHECK_RUNS(I18nRuns(en, Tr(L"Hello {name}"), {{L"name", L"<b>{x}</b> \\# </link>"}}),
                   {{L"Hello <b>{x}</b> \\# </link>"}});
        CHECK_RUNS(I18nRuns(de, Tr(L"Hello {name}"), {{L"name", L"<b>{x}</b>"}}), {{L"Hallo <b>{x}</b>"}});
        CHECK_RUNS(I18nRuns(en, Tr(L"<b>{name}</b>"), {{L"name", L"{y}"}}), {{L"{y}", true}});
        CHECK_TEXT(I18nText(en, Tr(L"{n} items, {m}"), {{L"n", -5}, {L"m", size_t(1234567)}}), L"-5 items, 1234567");
        CHECK_TEXT(I18nText(en, Tr(L"{a} {b}"), {{L"a", std::numeric_limits<unsigned long long>::max()},
                                                 {L"b", std::numeric_limits<long long>::min()}}),
                   L"18446744073709551615 -9223372036854775808");
        // A negative number takes its absolute value's category.
        CHECK_TEXT(LocDays(en, -1), L"-1 day");
        CHECK_TEXT(LocDays(I18nLocale(kTestFr), -1), L"-1 jour");
        // Missing arguments stay visible; a plural on text reads other; extra arguments do nothing.
        CHECK_TEXT(I18nText(en, Tr(L"Hi {name}")), L"Hi {name}");
        CHECK_TEXT(I18nText(en, Tr(L"{n, plural, one{# day} other{# days}}")), L"{n} days");
        CHECK_TEXT(I18nText(en, Tr(L"{n, plural, one{# day} other{# days}}"), {{L"n", L"many"}}), L"many days");
        CHECK_TEXT(I18nText(en, Tr(L"Hi"), {{L"x", 1}}), L"Hi");
        // Trusted runs keep their links and bold, take the template's bold, and its link where
        // they have none.
        std::vector<I18nRun> names = {{L"Ann", false, L"c0"}, {L", "}, {L"Bo", false, L"c1"}};
        CHECK_RUNS(I18nRuns(en, Tr(L"Thanks to {names}!"), {{L"names", names}}),
                   {{L"Thanks to "}, {L"Ann", false, L"c0"}, {L", "}, {L"Bo", false, L"c1"}, {L"!"}});
        CHECK_RUNS(I18nRuns(de, Tr(L"Thanks to {names}!"), {{L"names", names}}),
                   {{L"Danke an "}, {L"Ann", false, L"c0"}, {L", "}, {L"Bo", false, L"c1"}, {L"!"}});
        // Never a bold link: a translation's bold around a linked argument leaves the link plain.
        std::vector<I18nRun> linked = {{L"a", false, L"l"}, {L"b"}};
        CHECK_RUNS(I18nRuns(en, Tr(L"<b>{x}</b>"), {{L"x", linked}}), {{L"a", false, L"l"}, {L"b", true}});
        std::vector<I18nRun> own = {{L"A"}, {L"B", false, L"own"}};
        CHECK_RUNS(I18nRuns(en, Tr(L"<link id=\"t\">{x} more</link>"), {{L"x", own}}),
                   {{L"A", false, L"t"}, {L"B", false, L"own"}, {L" more", false, L"t"}});
        std::vector<I18nRun> strong = {{L"A", true}};
        CHECK_RUNS(I18nRuns(en, Tr(L"{x}!"), {{L"x", strong}}), {{L"A", true}, {L"!"}});
    });

    Run("i18n lists: CLDR patterns per language, keeping runs", [&] {
        auto list = [](const I18nLocale& locale, std::initializer_list<std::wstring_view> names) {
            return I18nRunsText(I18nList(locale, LocItems(names)));
        };
        CHECK(I18nList(en, {}).empty());
        const std::array<std::wstring_view, 4> kEnglish = {L"A", L"A and B", L"A, B and C", L"A, B, C, D and E"};
        const std::pair<std::wstring_view, std::array<std::wstring_view, 4>> kLists[] = {
            {L"en", kEnglish},
            {L"de", {L"A", L"A und B", L"A, B und C", L"A, B, C, D und E"}},
            {L"es", {L"A", L"A y B", L"A, B y C", L"A, B, C, D y E"}},
            {L"fr", {L"A", L"A et B", L"A, B et C", L"A, B, C, D et E"}},
            {L"ja", {L"A", L"A、B", L"A、B、C", L"A、B、C、D、E"}},
            {L"pt-BR", {L"A", L"A e B", L"A, B e C", L"A, B, C, D e E"}},
            {L"ru", {L"A", L"A и B", L"A, B и C", L"A, B, C, D и E"}},
            {L"zh-Hans", {L"A", L"A 和 B", L"A、B 和 C", L"A、B、C、D 和 E"}},
        };
        for (const auto& [tag, expected] : kLists) {
            const I18nLocale locale(LocLanguage(tag));
            CHECK_TEXT(list(locale, {L"A"}), std::wstring(expected[0]));
            CHECK_TEXT(list(locale, {L"A", L"B"}), std::wstring(expected[1]));
            CHECK_TEXT(list(locale, {L"A", L"B", L"C"}), std::wstring(expected[2]));
            CHECK_TEXT(list(locale, {L"A", L"B", L"C", L"D", L"E"}), std::wstring(expected[3]));
        }
        std::vector<std::vector<I18nRun>> people = {{{L"Ann", false, L"c0"}}, {{L"Bo", false, L"c1"}},
                                                    {{L"Cy", false, L"c2"}, {L" Jr", true}}};
        CHECK_RUNS(I18nList(en, people),
                   {{L"Ann", false, L"c0"}, {L", "}, {L"Bo", false, L"c1"}, {L" and "}, {L"Cy", false, L"c2"},
                    {L" Jr", true}});
        CHECK_RUNS(I18nList(en, {people[0]}), {{L"Ann", false, L"c0"}});
        CHECK_TEXT(list(I18nLocale(kI18nPseudoLanguage), {L"A", L"B"}), L"[A åñð B ļ]");
    });

    Run("i18n registry, explicit choices and the localization context", [] {
        struct Expected {
            std::wstring_view tag, name, locale;
            bool catalog;
        };
        const Expected kLanguages[] = {
            {L"en", L"English", L"en-US", false},   {L"de", L"Deutsch", L"de-DE", true},
            {L"es", L"Español", L"es-ES", true},    {L"fr", L"Français", L"fr-FR", true},
            {L"ja", L"日本語", L"ja-JP", true},     {L"pt-BR", L"Português (Brasil)", L"pt-BR", true},
            {L"ru", L"Русский", L"ru-RU", true},    {L"zh-Hans", L"简体中文", L"zh-CN", true},
        };
        CHECK(std::size(kI18nLanguages) == std::size(kLanguages));
        for (size_t i = 0; i < std::size(kLanguages); i++) {
            const I18nLanguage& language = kI18nLanguages[i];
            CHECK(language.tag == kLanguages[i].tag && language.nativeName == kLanguages[i].name &&
                  language.localeName == kLanguages[i].locale && language.catalog.empty() != kLanguages[i].catalog);
            // Every language is finished, so Automatic can pick it; English is the source.
            CHECK(!language.inProgress && !language.pseudo);
        }
        CHECK(kI18nPseudoLanguage.tag == L"qps-ploc" && kI18nPseudoLanguage.pseudo &&
              kI18nPseudoLanguage.plural == I18nPluralRule::English && kI18nPseudoLanguage.catalog.empty());
        // Explicit choices: exact tags, in progress or not, the pseudo-locale included.
        CHECK(I18nLanguageByTag(L"de") == &kI18nLanguages[1] && I18nLanguageByTag(L"pt-br") == &kI18nLanguages[5] &&
              I18nLanguageByTag(L"ZH_hans") == &kI18nLanguages[7] && I18nLanguageByTag(L"en") == &kI18nLanguages[0]);
        CHECK(I18nLanguageByTag(L"qps-ploc") == &kI18nPseudoLanguage && I18nLanguageByTag(L"QPS-PLOC") == &kI18nPseudoLanguage);
        CHECK(!I18nLanguageByTag(L"xx") && !I18nLanguageByTag(L"") && !I18nLanguageByTag(L"de-DE"));
        // The context: English by default, a language's locale name otherwise.
        const I18nLocale english, chinese(kI18nLanguages[7]);
        CHECK(english.Language().tag == L"en" && english.LocaleName() == L"en-US");
        CHECK(chinese.Language().tag == L"zh-Hans" && chinese.LocaleName() == L"zh-CN");
        CHECK(english == I18nLocale(kI18nLanguages[0]) && !(english == chinese));
    });

    Run("i18n matching: Automatic over preferred tag lists; a test registry's languages in progress never match", [] {
        auto match = [](const std::vector<std::wstring_view>& preferred,
                        std::span<const I18nLanguage> languages = kI18nLanguages) {
            return std::wstring(I18nMatchLanguage(preferred, languages).tag);
        };
        const std::pair<std::vector<std::wstring_view>, std::wstring_view> kCases[] = {
            {{}, L"en"},
            {{L"de-DE"}, L"de"},
            {{L"DE-de"}, L"de"},
            {{L"de_AT"}, L"de"},
            {{L"de-CH-1996"}, L"de"},
            {{L"en-GB", L"de-DE"}, L"en"},
            {{L"xx", L"fr-CA"}, L"fr"},
            {{L"pt-PT"}, L"pt-BR"},
            {{L"pt"}, L"pt-BR"},
            {{L"pt-br"}, L"pt-BR"},
            {{L"es-419"}, L"es"},
            {{L"es-MX", L"fr"}, L"es"},
            {{L"ja"}, L"ja"},
            {{L"ja-JP"}, L"ja"},
            {{L"ru-UA"}, L"ru"},
            {{L"zh-CN"}, L"zh-Hans"},
            {{L"zh-SG"}, L"zh-Hans"},
            {{L"zh"}, L"zh-Hans"},
            {{L"zh-Hans"}, L"zh-Hans"},
            {{L"zh-hans-cn"}, L"zh-Hans"},
            {{L"zh_CN"}, L"zh-Hans"},
            {{L"zh-MY"}, L"zh-Hans"},
            {{L"zh-Hans-TW"}, L"zh-Hans"},
            {{L"zh-TW", L"ja-JP"}, L"ja"},
            {{L"zh-HK"}, L"en"},
            {{L"zh-MO", L"zh-Hant"}, L"en"},
            {{L"zh-Hant-CN", L"ru-RU"}, L"ru"},
            {{L"zh-Latn"}, L"en"},
            {{L"zh-cmn-Hant", L"ru"}, L"ru"},
            {{L"zh-cmn-Hans-CN"}, L"zh-Hans"},
            {{L"zh-yue-HK"}, L"en"},
            {{L"zh-x-Hant"}, L"zh-Hans"},
            {{L"zh-TW-x-cn"}, L"en"},
            {{L"zh-CN-u-co-pinyin"}, L"zh-Hans"},
            {{L"zh-Hans-abc"}, L"zh-Hans"},
            {{L"qps-ploc"}, L"en"},
            {{L"qps-ploc", L"fr"}, L"fr"},
            {{L"", L"-", L"de"}, L"de"},
            {{L"nl-NL", L"sv"}, L"en"},
        };
        for (const auto& [tags, expected] : kCases) CHECK_TEXT(match(tags), std::wstring(expected));
        // A test registry with every translation in progress: none matches automatically.
        std::array<I18nLanguage, std::size(kI18nLanguages)> inProgress{};
        std::copy(std::begin(kI18nLanguages), std::end(kI18nLanguages), inProgress.begin());
        for (size_t i = 1; i < inProgress.size(); i++) inProgress[i].inProgress = true;
        CHECK_TEXT(match({L"de-DE"}, inProgress), L"en");
        CHECK_TEXT(match({L"ja-JP", L"zh-CN", L"pt-BR"}, inProgress), L"en");
        // French finished: it matches, its parent's tags too; German stays skipped.
        inProgress[3].inProgress = false;
        CHECK_TEXT(match({L"de-DE", L"fr-FR"}, inProgress), L"fr");
        CHECK_TEXT(match({L"fr-CA"}, inProgress), L"fr");
        CHECK_TEXT(match({L"de-AT"}, inProgress), L"en");
    });

    Run("i18n pseudo-locale: accents, padding and brackets around unchanged syntax", [] {
        const I18nLocale pseudo(kI18nPseudoLanguage);
        CHECK_TEXT(I18nPseudoTemplate(L"{n, plural, one{# day} other{# days}}"), L"[{n, plural, one{# ðåý} other{# ðåýš}} ļöŕ]");
        CHECK_TEXT(I18nPseudoTemplate(L"<b>Open</b> <link id=\"go\">now</link>"),
                   L"[<b>Öþéñ</b> <link id=\"go\">ñöŵ</link> ļö]");
        CHECK_TEXT(I18nPseudoTemplate(L"\\{x\\} \\< #"), L"[\\{ẋ\\} \\< \\# ļö]");
        CHECK_TEXT(I18nText(pseudo, Tr(L"Off")), L"[Öƒƒ ļ]");
        CHECK_TEXT(I18nText(pseudo, Tr(L"Hi {name}"), {{L"name", L"Bob"}}), L"[Ĥî Bob ļ]");
        CHECK_TEXT(LocDays(pseudo, 0), L"[0 ðåýš ļöŕ]");
        CHECK_TEXT(LocDays(pseudo, 1), L"[1 ðåý ļöŕ]");
        CHECK_TEXT(LocDays(pseudo, 2), L"[2 ðåýš ļöŕ]");
        CHECK_RUNS(I18nRuns(pseudo, Tr(L"<b>Open</b> <link id=\"go\">now</link>")),
                   {{L"["}, {L"Öþéñ", true}, {L" "}, {L"ñöŵ", false, L"go"}, {L" ļö]"}});
        CHECK_TEXT(I18nText(pseudo, Tr(L"\\{x\\} \\< #")), L"[{ẋ} < # ļö]");
        // 17 letters, 6 of padding.
        CHECK_TEXT(I18nText(pseudo, Tr(L"Refresh usage now")), L"[Ŕéƒŕéšĥ ûšåĝé ñöŵ ļöŕéɱ]");
    });

    Run("i18n lookup with and without context", [&] {
        const I18nLocale de(kTestDe);
        CHECK(Tr(L"Open").Context().empty() && Tr(L"Open").English() == L"Open");
        CHECK(TrCtx(L"state", L"Open").Context() == L"state" && TrCtx(L"state", L"Open").English() == L"Open");
        CHECK_TEXT(I18nText(de, Tr(L"Open")), L"Öffnen");
        CHECK_TEXT(I18nText(de, TrCtx(L"state", L"Open")), L"Offen");
        CHECK_TEXT(I18nText(de, TrCtx(L"menu", L"Open")), L"Open");
        CHECK_TEXT(I18nText(en, TrCtx(L"state", L"Open")), L"Open");
        // The catalogs' first and last entries.
        CHECK_TEXT(I18nText(de, Tr(L"<link id=\"open\">Open</link> or <link id=\"copy\">copy</link> the link.")),
                   L"Den Link kopieren oder öffnen.");
        CHECK_TEXT(LocDays(de, 5), L"5 Tage");
        // Sorted: context first, then English; duplicates are not.
        constexpr I18nEntry kUnsorted[] = {{L"", L"b", L""}, {L"", L"a", L""}};
        constexpr I18nEntry kDuplicate[] = {{L"", L"a", L"x"}, {L"", L"a", L"y"}};
        constexpr I18nEntry kContextFirst[] = {{L"", L"z", L""}, {L"a", L"a", L""}, {L"a", L"b", L""}};
        CHECK(!I18nCatalogSorted(kUnsorted) && !I18nCatalogSorted(kDuplicate) && I18nCatalogSorted(kContextFirst));
    });
}

#pragma clang diagnostic pop
