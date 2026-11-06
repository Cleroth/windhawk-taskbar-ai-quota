// Localized text: message keys, arguments, formatting with whole-entry English fallback, and list
// patterns. Call forms and rules: i18n/AGENTS.md.

class I18nKey;
template <size_t N>
static consteval I18nKey Tr(const wchar_t (&english)[N]);
template <size_t C, size_t N>
static consteval I18nKey TrCtx(const wchar_t (&context)[C], const wchar_t (&english)[N]);

// A message key: a literal English template, and the context that tells apart uses of the same
// English (none: empty). Only Tr and TrCtx make keys, so tests/i18n/check.py extracts every one
// from source; both reject a malformed English template at compile time.
class I18nKey {
public:
    constexpr std::wstring_view Context() const { return context_; }
    constexpr std::wstring_view English() const { return english_; }

private:
    constexpr I18nKey(std::wstring_view context, std::wstring_view english)
        : context_(context), english_(english) {}

    std::wstring_view context_;
    std::wstring_view english_;

    template <size_t N>
    friend consteval I18nKey Tr(const wchar_t (&english)[N]);
    template <size_t C, size_t N>
    friend consteval I18nKey TrCtx(const wchar_t (&context)[C], const wchar_t (&english)[N]);
};

template <size_t N>
static consteval I18nKey Tr(const wchar_t (&english)[N]) {
    if (N < 2 || !I18nTemplateShape({english, N - 1}, I18nPluralRule::English).valid) {
        throw "malformed English template";
    }
    return I18nKey({}, {english, N - 1});
}

template <size_t C, size_t N>
static consteval I18nKey TrCtx(const wchar_t (&context)[C], const wchar_t (&english)[N]) {
    if (C < 2) throw "empty context: use Tr";
    if (N < 2 || !I18nTemplateShape({english, N - 1}, I18nPluralRule::English).valid) {
        throw "malformed English template";
    }
    return I18nKey({context, C - 1}, {english, N - 1});
}

// Formatted text: plain, bold, or a link the code binds to an action by ID. Never a bold link: the
// UI's runs are one or the other, so bold around a link is dropped.
struct I18nRun {
    std::wstring text;
    bool bold = false;
    std::wstring link;  // Link ID; empty: not a link

    bool operator==(const I18nRun&) const = default;
};

// A named argument: plain text (inserted as is, never parsed), an integer of any type (a plural's
// selector; decimal digits, '-' when negative, no grouping), an integer shown as given text (a
// plural selects by the number, it and '#' show the text: a regional form, I18nInteger), or
// trusted runs that keep their bold and links. It refers to the caller's value, so build it in the
// call.
struct I18nArg {
    std::wstring_view name;
    std::wstring_view text{};  // A number's: empty shows its digits
    unsigned long long magnitude = 0;
    bool negative = false;
    bool isNumber = false;
    const std::vector<I18nRun>* runs = nullptr;

    I18nArg(std::wstring_view name, std::wstring_view text) : name(name), text(text) {}
    template <class T>
        requires std::is_integral_v<T>
    I18nArg(std::wstring_view name, T number, std::wstring_view shown = {})
        : name(name), text(shown), magnitude((unsigned long long)number), isNumber(true) {
        if constexpr (std::is_signed_v<T>) {
            negative = number < 0;
            if (negative) magnitude = 0 - magnitude;
        }
    }
    I18nArg(std::wstring_view name, const std::vector<I18nRun>& runs) : name(name), runs(&runs) {}
};

// Formats a parsed, valid template. A plural shows its number's category's branch, else other; a
// missing argument shows as "{name}", and a plural on one that isn't a number shows other.
struct I18nFormatSink {
    std::initializer_list<I18nArg> args;
    I18nPluralRule rule = I18nPluralRule::English;
    std::vector<I18nRun> runs{};
    bool bold = false;
    std::wstring_view link{};
    // The plural being read: its selector and each branch's runs, until its end picks one.
    std::wstring_view selector{};
    std::array<std::vector<I18nRun>, std::size(kI18nPluralNames)> branches{};
    unsigned seen = 0;
    std::vector<I18nRun>* out = &runs;

    void Emit(std::wstring_view text, bool strong, std::wstring_view id) {
        if (text.empty()) return;
        strong = strong && id.empty();
        if (!out->empty() && out->back().bold == strong && out->back().link == id) out->back().text += text;
        else out->push_back({std::wstring(text), strong, std::wstring(id)});
    }
    void Text(std::wstring_view text) { Emit(text, bold, link); }
    void Arg(std::wstring_view name) {
        for (const I18nArg& arg : args) {
            if (arg.name != name) continue;
            if (arg.runs) {
                for (const I18nRun& run : *arg.runs) {
                    Emit(run.text, bold || run.bold, run.link.empty() ? link : std::wstring_view(run.link));
                }
            } else if (arg.isNumber && arg.text.empty()) {
                Emit((arg.negative ? L"-" : L"") + std::to_wstring(arg.magnitude), bold, link);
            } else {
                Emit(arg.text, bold, link);
            }
            return;
        }
        Emit(L"{" + std::wstring(name) + L"}", bold, link);
    }
    void Number() { Arg(selector); }
    void PluralBegin(std::wstring_view name) {
        selector = name;
        seen = 0;
    }
    void Branch(I18nPlural category) {
        seen |= 1u << size_t(category);
        out = &branches[size_t(category)];
    }
    void BranchEnd() { out = &runs; }
    void PluralEnd() {
        I18nPlural category = I18nPlural::Other;
        for (const I18nArg& arg : args) {
            if (arg.name == selector && arg.isNumber) category = I18nPluralCategory(rule, arg.magnitude);
        }
        if (!(seen & (1u << size_t(category)))) category = I18nPlural::Other;
        for (const I18nRun& run : branches[size_t(category)]) Emit(run.text, run.bold, run.link);
        branches = {};
    }
    void BoldBegin() { bold = true; }
    void BoldEnd() { bold = false; }
    void LinkBegin(std::wstring_view id) { link = id; }
    void LinkEnd() { link = {}; }
};

// API entry points carry [[maybe_unused]]: a build or test target may use only some of them.

// A key's runs in the locale's language: its translation when usable, else English with English
// plural rules. Usable: it parses under the language's plural rule and has the English template's
// argument names, plural selectors and links; an argument English shows in every expansion shows
// in every one of the translation's, none shows more often than English's most (selectors aside)
// and none sits in a link unless it does in English, so linked runs never repeat, vanish or split
// a link. The pseudo-locale's transform of English is read the same way.
[[maybe_unused]] static std::vector<I18nRun> I18nRuns(const I18nLocale& locale, I18nKey key,
                                                      std::initializer_list<I18nArg> args = {}) {
    const I18nLanguage& language = locale.Language();
    std::wstring pseudo;
    std::wstring_view translation;
    if (language.pseudo) {
        pseudo = I18nPseudoTemplate(key.English());
        translation = pseudo;
    } else {
        I18nEntry probe{key.Context(), key.English(), {}};
        auto entry = std::lower_bound(language.catalog.begin(), language.catalog.end(), probe,
                                      I18nEntryBefore);
        if (entry != language.catalog.end() && !I18nEntryBefore(probe, *entry)) {
            translation = entry->translation;
        }
    }
    I18nShape english = I18nTemplateShape(key.English(), I18nPluralRule::English);
    I18nShape shape = I18nTemplateShape(translation, language.plural);
    bool usable = !translation.empty() && shape.valid && shape.args == english.args &&
                  shape.plurals == english.plurals && shape.links == english.links;
    for (size_t i = 0; usable && i < english.args.count; i++) {
        if (english.plurals.Has(english.args.items[i])) continue;
        size_t index = shape.args.Find(english.args.items[i]);
        usable = (english.fewest[i] == 0 || shape.fewest[index] > 0) && shape.most[index] <= english.most[i];
    }
    for (size_t i = 0; usable && i < shape.linked.count; i++) usable = english.linked.Has(shape.linked.items[i]);
    I18nFormatSink sink;
    sink.args = args;
    sink.rule = usable ? language.plural : I18nPluralRule::English;
    std::wstring_view text = usable ? translation : key.English();
    size_t at = 0;
    I18nParseMessage(text, at, sink, 0);
    return std::move(sink.runs);
}

[[maybe_unused]] static std::wstring I18nRunsText(const std::vector<I18nRun>& runs) {
    std::wstring text;
    for (const I18nRun& run : runs) text += run.text;
    return text;
}

// Plain text: a rich template's runs joined.
[[maybe_unused]] static std::wstring I18nText(const I18nLocale& locale, I18nKey key,
                                              std::initializer_list<I18nArg> args = {}) {
    return I18nRunsText(I18nRuns(locale, key, args));
}

// Items joined by the language's CLDR list patterns, keeping their runs: "A", "A and B",
// "A, B and C". In each pattern {a} is one item and {b} the next one or the joined rest.
[[maybe_unused]] static std::vector<I18nRun> I18nList(const I18nLocale& locale,
                                                      const std::vector<std::vector<I18nRun>>& items) {
    size_t n = items.size();
    if (n < 2) return n ? items[0] : std::vector<I18nRun>{};
    if (n == 2) {
        return I18nRuns(locale, TrCtx(L"list-pair", L"{a} and {b}"), {{L"a", items[0]}, {L"b", items[1]}});
    }
    std::vector<I18nRun> rest =
        I18nRuns(locale, TrCtx(L"list-end", L"{a} and {b}"), {{L"a", items[n - 2]}, {L"b", items[n - 1]}});
    for (size_t i = n - 2; --i > 0;) {
        rest = I18nRuns(locale, TrCtx(L"list-middle", L"{a}, {b}"), {{L"a", items[i]}, {L"b", rest}});
    }
    return I18nRuns(locale, TrCtx(L"list-start", L"{a}, {b}"), {{L"a", items[0]}, {L"b", rest}});
}
