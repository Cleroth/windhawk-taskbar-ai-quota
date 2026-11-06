"""Self-test of the localization checker on synthetic sources (no files written)."""
import sys
import unittest

sys.dont_write_bytecode = True
import check

REGISTRY = '''
static constexpr I18nLanguage kI18nLanguages[] = {
    {L"en", L"English", L"en-US", I18nPluralRule::English, false, {}, false},
    {L"de", L"Deutsch", L"de-DE", I18nPluralRule::English, true, kI18nCatalogDe, false},
    {L"ru", L"Русский", L"ru-RU", I18nPluralRule::Russian, true, kI18nCatalogRu, false},
};
'''


def keys(text):
    code, _ = check.tokenize(text, "x.h")
    return check.extract_keys(code, "x.h")


def literals(text):
    code, marked = check.tokenize(text, "x.h")
    return [item["text"] for item in check.unwrapped_literals(code, marked, "x.h")]


class GrammarTests(unittest.TestCase):
    def valid(self, text, rule="English"):
        return check.shape(text, rule)[0] is not None

    def test_runtime_grammar(self):
        for good in ('', 'a > b # c', '\\{x\\} \\< \\# \\\\', '<b>x</b><link id="a">y</link>',
                     '{n, plural, one{<b>#</b> day} other{# days}}', '<b>{n, plural, other{#}}</b>',
                     '{n, plural, one {x} other {y} }'):
            self.assertTrue(self.valid(good), good)
        for bad in ('{', '}', '{}', '{1x}', '{a b}', '{a, plural,}', '{a, plural, one{x}}',
                    '{a, plural, other{x} other{y}}', '{a, select, other{x}}', '{a, plural, other{{b, plural, other{x}}}}',
                    '<b></b>', '<b>x', 'x</b>', '<i>x</i>', '<link id="">y</link>', '<link id="a">y</b>',
                    '<b><b>x</b></b>', '<b><link id="a">x</link></b>', '<link id="a"><b>x</b></link>', 'a < b',
                    '\\x', 'trailing \\', '<b>{a, plural, other{x</b>}}'):
            self.assertFalse(self.valid(bad), bad)

    def test_links_once_per_expansion_and_categories(self):
        self.assertFalse(self.valid('<link id="x">a</link><link id="x">b</link>'))
        self.assertFalse(self.valid('{n, plural, one{<link id="x">a</link>} other{b}}'))
        self.assertTrue(self.valid('{n, plural, one{<link id="x">a</link>} other{<link id="x">b</link>}}'))
        self.assertTrue(self.valid('{n, plural, few{a} other{b}}', "Russian"))
        self.assertFalse(self.valid('{n, plural, few{a} other{b}}', "English"))
        self.assertTrue(self.valid('{n, plural, many{a} other{b}}', "French"))
        self.assertFalse(self.valid('{n, plural, one{a} other{b}}', "OtherOnly"))
        self.assertTrue(self.valid("{a, plural, other{}}{b, plural, other{}}{c, plural, other{}}{d, plural, other{}}"))
        self.assertFalse(self.valid("{a, plural, other{}}{b, plural, other{}}{c, plural, other{}}{d, plural, other{}}"
                                    "{e, plural, other{}}"))
        self.assertTrue(self.valid("".join(f"{{a{i}}}" for i in range(16))))
        self.assertFalse(self.valid("".join(f"{{a{i}}}" for i in range(17))))

    def test_uses_per_expansion(self):
        for text, rule, uses in (
                ("{a} {n, plural, one{{b}{b}#} other{{a}}}", "English", {"a": (1, 2), "b": (0, 2), "n": (0, 0)}),
                ("{n, plural, one{{a}} other{}}{n, plural, one{} other{{a}}}", "English", {"a": (1, 1), "n": (0, 0)}),
                ("{n, plural, one{{a}} other{}}{m, plural, one{} other{{a}}}", "English",
                 {"a": (0, 2), "n": (0, 0), "m": (0, 0)}),
                ("{n, plural, other{{a}}}", "Russian", {"a": (1, 1), "n": (0, 0)}),
                ("{n, plural, one{{a}} few{{a}} many{{a}} other{}}", "Russian", {"a": (0, 1), "n": (0, 0)}),
                ("{n, plural, many{{a}} other{}}", "French", {"a": (0, 1), "n": (0, 0)})):
            with self.subTest(text=text):
                self.assertEqual(check.shape(text, rule)[0].uses, uses)

    def test_translation_problems(self):
        days = "{n, plural, one{# day} other{# days}}"
        self.assertIsNone(check.translation_problem("{n, plural, one{# день} few{# дня} many{# дней} other{# дня}}",
                                                    "Russian", days))
        # Moved out of the plural, or the selector dropped as a plain argument: still usable.
        self.assertIsNone(check.translation_problem("{name} hat {n, plural, one{# Datei} other{# Dateien}}", "English",
                                                    "{n, plural, one{{name} has # file} other{{name} has # files}}"))
        self.assertIsNone(check.translation_problem("{n, plural, other{#件}}", "OtherOnly",
                                                    "{n} of {n, plural, one{# item} other{# items}}"))
        for translation, rule, english, problem in (
                ("", "English", "Open", "empty"),
                ("Hallo {user}", "English", "Hello {name}", "argument names"),
                ("{n}日", "OtherOnly", days, "plural selectors"),
                ('<link id="b">x</link>', "English", '<link id="a">x</link>', "link IDs"),
                ("{n, plural, one{# Tag} few{# Tage} other{# Tage}}", "English", days, "few"),
                ("{n, plural, one{# jour}}", "French", days, "without other"),
                ("{n, plural, one{{name}: # Datei} other{alle}}", "English", "{name}: " + days, "missing from"),
                ("{names} und {names}", "English", "Thanks to {names}", "at most 1"),
                # English shows the name once for every number; this twice for 1, never otherwise.
                ("{n, plural, one{{name}{name}} other{}}", "English",
                 "{n, plural, one{{name}} other{}}{n, plural, one{} other{{name}}}", "missing from"),
                ('<link id="g">Danke {names}. Mehr</link>', "English", 'Thanks {names}. <link id="g">More</link>',
                 "inside a link"),
                ("{n, plural, one{{name}: # файл} few{{name}: # файла} many{{name}: # файлов} other{}}", "Russian",
                 "Account {name}: " + days, "missing from")):
            with self.subTest(translation=translation):
                self.assertIn(problem, check.translation_problem(translation, rule, english))


class SourceScanTests(unittest.TestCase):
    def test_keys_join_literals_decode_escapes_and_skip_comments(self):
        found, problems = keys('// Tr(L"commented")\n/* TrCtx(L"x", L"y") */\n'
                               'auto a = Tr(L"Hello, "\n           L"{name}");\n'
                               'auto b = TrCtx(L"menu", L"Open \\u00e9\\\\{x\\\\}");\n'
                               'auto c = "Tr(L\\"in a string\\")";\n'
                               'auto d = on ? Tr(L"On") : Tr(L"Off"); auto e = ::Tr(L"Global");\n')
        self.assertEqual(problems, [])
        self.assertEqual([(k[0], k[1]) for k in found], [("", "Hello, {name}"), ("menu", "Open é\\{x\\}"),
                                                         ("", "On"), ("", "Off"), ("", "Global")])
        self.assertEqual(found[0][2], "x.h:3")

    def test_declarations_skipped_and_other_calls_reported(self):
        found, problems = keys('template <size_t N> static consteval I18nKey Tr(const wchar_t (&e)[N]);\n'
                               'Tr(kName); Tr("narrow"); TrCtx(L"only one"); Tr(L"a", L"b"); Tr<8>(L"Another");\n'
                               'x.Tr(L"member"); auto f = &Tr<2>;\n')
        self.assertEqual(found[0][:2], ("", "member"))
        self.assertEqual([p["where"] for p in problems], ["x.h:2"] * 5 + ["x.h:3"])

    def test_unwrapped_literals_honor_wrappers_logs_and_marker(self):
        found = literals('Tr(L"Wrapped text");\nWh_Log(L"Log %s line", x);\n'
                         'f(L"Visible text");\nf(L"Settings");\nf(L"Custom...");\nf(L"%s ago");\n'
                         'f(L"Segoe UI");  // i18n-en: font family\n'
                         'f(L"accountOrder"); f(L"RecordHistory"); f(L"https://x.y/a b"); f(L"a\\\\b c");\n'
                         'f(L"\\uE8A7"); f(L"%d%%"); f(L"Iv1.b507a08c"); f("Narrow text");\n'
                         'f(L"Two "\n  L"lines");\n')
        self.assertEqual(found, ["Visible text", "Settings", "Custom...", "%s ago", "Two lines"])


class CatalogTests(unittest.TestCase):
    SOURCES = {
        check.REGISTRY: REGISTRY,
        "src/i18n/format.h": 'Tr(L"Hello {name}"); TrCtx(L"state", L"Open"); Tr(L"Open"); Tr(L"Missing");\n'
                             'Tr(L"{n, plural, one{# day} other{# days}}");',
        "src/i18n/lang-de.h": '''
static constexpr I18nEntry kI18nCatalogDe[] = {
    {L"", L"Hello {name}", L"Hallo {user}"},
    {L"", L"Open", L"Öffnen"},
    {L"", L"Gone", L"Weg"},
    {L"", L"Open", L"Offen"},
    {L"", L"{n, plural, one{# day} other{# days}}", L"{n, plural, one{# Tag} other{# Tage}}"},
    {L"state", L"Open",
     L"Of" L"fen"},
};
''',
        "src/i18n/lang-ru.h": 'static constexpr I18nEntry kI18nCatalogRu[] = {\n};\n',
        "src/i18n/lang-xx.h": 'static constexpr I18nEntry kI18nCatalogXx[] = {};\n',
        "src/ui/page.h": 'f(L"Unwrapped label");\n',
    }

    def test_findings(self):
        findings = check.check(self.SOURCES)
        self.assertEqual(len(findings["keys"]), 5)
        self.assertEqual(findings["unregistered_catalogs"], ["src/i18n/lang-xx.h: kI18nCatalogXx"])
        de, ru = findings["languages"]
        self.assertEqual((de["tag"], de["entries"], ru["tag"], ru["entries"]), ("de", 6, "ru", 0))
        self.assertEqual(de["missing"], [["", "Missing"]])
        self.assertEqual([i["key"] for i in de["stale"]], [["", "Gone"]])
        self.assertEqual([i["key"] for i in de["duplicates"]], [["", "Open"]])
        # Each entry against the one before it: "Gone" after "Open".
        self.assertEqual([i["key"] for i in de["unsorted"]], [["", "Gone"]])
        self.assertEqual([i["key"] for i in de["invalid"]], [["", "Hello {name}"]])
        self.assertEqual(len(ru["missing"]), 5)
        self.assertEqual([i["text"] for i in findings["unwrapped_literals"]], ["Unwrapped label"])
        report = check.report(findings, "src")
        self.assertIn("Missing entries: 6", report)
        self.assertIn("Unwrapped user-facing literals: 1 in 1 file(s)", report)

    def test_special_spaces_written_as_characters(self):
        sources = dict(self.SOURCES)
        sources["src/i18n/notes.h"] = ('// a\u00a0b\n'
                                       'static constexpr auto kEscaped = L"x\\u00A0y\\u202Fz";\n'
                                       'static constexpr auto kLiteral = L"a\u202f:\u00a0b\u200b";\n')
        findings = check.check(sources)
        self.assertEqual(findings["special_spaces"], [
            {"where": "src/i18n/notes.h:1", "chars": ["U+00A0"]},
            {"where": "src/i18n/notes.h:3", "chars": ["U+00A0", "U+200B", "U+202F"]}])
        self.assertIn("written as characters (write \\u escapes): 2", check.report(findings, "src"))

    def test_sort_order_is_utf16_code_units(self):
        # U+FF5B sorts after U+1F600 by code point, before it by UTF-16 code unit (a surrogate pair).
        self.assertLess(check.sort_key("", "\U0001F600"), check.sort_key("", "\uFF5B"))
        self.assertLess(check.sort_key("", "z"), check.sort_key("a", ""))

    def test_unparseable_catalogs_and_registry_fail_the_checker(self):
        for path, text in (("src/i18n/lang-de.h", 'static constexpr I18nEntry kI18nCatalogDe[] = {\n    {L"", L"x"},\n};'),
                           ("src/i18n/lang-de.h", 'static constexpr I18nEntry kI18nCatalogDe[] = {\n    {L"", L"x", kText},\n};'),
                           ("src/i18n/lang-de.h", ''),  # The registry's catalog is nowhere
                           (check.REGISTRY, 'static constexpr I18nLanguage kI18nLanguages[] = {\n    {L"de"},\n};')):
            sources = dict(self.SOURCES)
            sources[path] = text
            with self.subTest(text=text), self.assertRaises(check.CheckError):
                check.check(sources)


if __name__ == "__main__":
    unittest.main(verbosity=2)
