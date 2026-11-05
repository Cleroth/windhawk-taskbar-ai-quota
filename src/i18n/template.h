// Message templates: the grammar of English keys and their translations (i18n/AGENTS.md) and
// integer CLDR plural rules. Pure and constexpr: Tr and TrCtx check English keys at compile time,
// lookups check translations against them.

// CLDR cardinal categories, in kI18nPluralNames' order.
enum class I18nPlural : uint8_t { Zero, One, Two, Few, Many, Other };
static constexpr std::wstring_view kI18nPluralNames[] = {L"zero", L"one", L"two", L"few", L"many", L"other"};

// Integer CLDR cardinal rules, named after a language that uses them.
enum class I18nPluralRule : uint8_t {
    English,    // en, de: one (1), other
    Spanish,    // es: one (1), many (non-zero multiples of a million), other
    French,     // fr, pt-BR: one (0 and 1), many (as Spanish), other
    Russian,    // ru: one (1, 21, 101), few (2-4, 22-24), many (0, 5-20, 25-30, 111)
    OtherOnly,  // ja, zh-Hans
};

// The category of an integer's absolute value.
static constexpr I18nPlural I18nPluralCategory(I18nPluralRule rule, unsigned long long i) {
    bool millions = i != 0 && i % 1000000 == 0;
    switch (rule) {
    case I18nPluralRule::English:
        return i == 1 ? I18nPlural::One : I18nPlural::Other;
    case I18nPluralRule::Spanish:
        return i == 1 ? I18nPlural::One : millions ? I18nPlural::Many : I18nPlural::Other;
    case I18nPluralRule::French:
        return i <= 1 ? I18nPlural::One : millions ? I18nPlural::Many : I18nPlural::Other;
    case I18nPluralRule::Russian:
        if (i % 10 == 1 && i % 100 != 11) return I18nPlural::One;
        if (i % 10 >= 2 && i % 10 <= 4 && (i % 100 < 12 || i % 100 > 14)) return I18nPlural::Few;
        return I18nPlural::Many;
    case I18nPluralRule::OtherOnly:
        return I18nPlural::Other;
    }
    return I18nPlural::Other;
}

// A rule's categories as bits (1 << category). Russian's other holds fractions, never an integer,
// but a selector that isn't a number shows it.
static constexpr unsigned I18nPluralMask(I18nPluralRule rule) {
    auto bit = [](I18nPlural category) { return 1u << unsigned(category); };
    switch (rule) {
    case I18nPluralRule::English:
        return bit(I18nPlural::One) | bit(I18nPlural::Other);
    case I18nPluralRule::Spanish:
    case I18nPluralRule::French:
        return bit(I18nPlural::One) | bit(I18nPlural::Many) | bit(I18nPlural::Other);
    case I18nPluralRule::Russian:
        return bit(I18nPlural::One) | bit(I18nPlural::Few) | bit(I18nPlural::Many) | bit(I18nPlural::Other);
    case I18nPluralRule::OtherOnly:
        return bit(I18nPlural::Other);
    }
    return bit(I18nPlural::Other);
}

// Characters a backslash escapes; a backslash before anything else is a syntax error.
static constexpr std::wstring_view kI18nEscapable = L"{}<#\\";

// Parse scopes: what an element is inside. Plurals and tags never nest in their own kind; a tag
// may hold a plural or sit in a branch. Tags stay flat as the UI's runs are: bold or a link.
enum : unsigned { kI18nInBranch = 1, kI18nInTag = 2 };

// Reads elements up to the end or a closer ("}", "</b>", "</link>"), which it leaves for the caller
// to match, and reports them to the sink in order: Text (an escape arrives as its character
// alone), Arg, Number ('#' in a branch), PluralBegin, Branch, BranchEnd, PluralEnd, BoldBegin,
// BoldEnd, LinkBegin, LinkEnd. False on a syntax error, after reporting what came before it.
template <class Sink>
static constexpr bool I18nParseMessage(std::wstring_view s, size_t& at, Sink& sink, unsigned scope) {
    size_t literal = at;
    auto flush = [&] {
        if (at > literal) sink.Text(s.substr(literal, at - literal));
    };
    auto spaces = [&] {
        while (at < s.size() && s[at] == L' ') at++;
    };
    auto next = [&](wchar_t c) { return at < s.size() && s[at] == c; };
    auto name = [&] {
        size_t start = at;
        while (at < s.size() && ((s[at] >= L'a' && s[at] <= L'z') || (s[at] >= L'A' && s[at] <= L'Z') ||
                                 s[at] == L'_' || (at > start && s[at] >= L'0' && s[at] <= L'9'))) {
            at++;
        }
        return s.substr(start, at - start);
    };
    while (at < s.size()) {
        std::wstring_view rest = s.substr(at);
        if (rest[0] == L'}' || rest.starts_with(L"</b>") || rest.starts_with(L"</link>")) break;
        if (rest[0] == L'\\') {
            if (rest.size() < 2 || kI18nEscapable.find(rest[1]) == std::wstring_view::npos) return false;
            flush();
            sink.Text(rest.substr(1, 1));
            at += 2;
        } else if (rest[0] == L'#' && (scope & kI18nInBranch)) {
            flush();
            sink.Number();
            at++;
        } else if (rest[0] == L'{') {
            flush();
            at++;
            spaces();
            std::wstring_view arg = name();
            spaces();
            if (arg.empty()) return false;
            if (next(L'}')) {
                sink.Arg(arg);
                at++;
            } else {
                // {arg, plural, category{message} ... other{message}}
                if ((scope & kI18nInBranch) || !next(L',')) return false;
                at++;
                spaces();
                if (name() != L"plural") return false;
                spaces();
                if (!next(L',')) return false;
                at++;
                sink.PluralBegin(arg);
                unsigned seen = 0;
                for (spaces(); !next(L'}'); spaces()) {
                    std::wstring_view keyword = name();
                    size_t category = size_t(std::find(std::begin(kI18nPluralNames), std::end(kI18nPluralNames),
                                                       keyword) - std::begin(kI18nPluralNames));
                    if (keyword.empty() || category == std::size(kI18nPluralNames) || (seen & (1u << category))) {
                        return false;
                    }
                    seen |= 1u << category;
                    spaces();
                    if (!next(L'{')) return false;
                    at++;
                    sink.Branch(I18nPlural(category));
                    if (!I18nParseMessage(s, at, sink, scope | kI18nInBranch) || !next(L'}')) return false;
                    at++;
                    sink.BranchEnd();
                }
                if (!(seen & (1u << size_t(I18nPlural::Other)))) return false;
                at++;
                sink.PluralEnd();
            }
        } else if (rest[0] == L'<') {
            flush();
            bool bold = rest.starts_with(L"<b>");
            if (scope & kI18nInTag) return false;
            if (bold) {
                at += 3;
                sink.BoldBegin();
            } else if (rest.starts_with(L"<link id=\"")) {
                at += 10;
                std::wstring_view id = name();
                if (id.empty() || !s.substr(at).starts_with(L"\">")) return false;
                at += 2;
                sink.LinkBegin(id);
            } else {
                return false;
            }
            size_t content = at;
            std::wstring_view closer = bold ? L"</b>" : L"</link>";
            if (!I18nParseMessage(s, at, sink, scope | kI18nInTag) || at == content ||
                !s.substr(at).starts_with(closer)) {
                return false;
            }
            at += closer.size();
            if (bold) sink.BoldEnd();
            else sink.LinkEnd();
        } else {
            at++;
            continue;
        }
        literal = at;
    }
    flush();
    return true;
}

// Distinct argument names and link IDs one template may use, each.
static constexpr size_t kI18nMaxNames = 16;

// A set of argument names or link IDs.
struct I18nNames {
    std::array<std::wstring_view, kI18nMaxNames> items{};
    size_t count = 0;

    // Its index; count when absent.
    constexpr size_t Find(std::wstring_view name) const {
        size_t i = 0;
        while (i < count && items[i] != name) i++;
        return i;
    }
    constexpr bool Has(std::wstring_view name) const { return Find(name) < count; }
    // False when full.
    constexpr bool Add(std::wstring_view name) {
        if (Has(name)) return true;
        if (count == items.size()) return false;
        items[count++] = name;
        return true;
    }
    constexpr bool operator==(const I18nNames& other) const {
        if (count != other.count) return false;
        for (size_t i = 0; i < count; i++) {
            if (!other.Has(items[i])) return false;
        }
        return true;
    }
};

// What a translation must share with its English key. valid: the template parses, uses only its
// language's plural categories, and has each link exactly once in every expansion (each branch of
// a plural holding links holds the same ones). An expansion is the text one category per selector
// shows: plurals on the same selector pick together, and a missing category picks other.
struct I18nShape {
    bool valid = false;
    I18nNames args;     // Plural selectors included
    I18nNames plurals;  // Plural selectors
    I18nNames links;
    I18nNames linked;   // Arguments inside a link
    // Per args entry: its fewest and most {name} uses in an expansion ('#' and plurals aside).
    std::array<size_t, kI18nMaxNames> fewest{};
    std::array<size_t, kI18nMaxNames> most{};
};

// Distinct plural selectors one template may use.
static constexpr size_t kI18nMaxSelectors = 4;

// Collects I18nTemplateShape's shape.
struct I18nShapeSink {
    using Uses = std::array<size_t, kI18nMaxNames>;
    static constexpr size_t kCategories = std::size(kI18nPluralNames);

    I18nPluralRule rule = I18nPluralRule::English;
    I18nShape shape{};
    bool ok = true;
    bool inLink = false;
    // The plural being read: its selector's index, the categories it has, the branch being read
    // and its links, the first branch's links (the others match them) and each branch's uses.
    bool inPlural = false;
    size_t selector = 0;
    unsigned present = 0;
    size_t branch = 0;
    size_t branches = 0;
    I18nNames branchLinks{};
    I18nNames pluralLinks{};
    std::array<Uses, kCategories> branchUses{};
    // Per selector and category: the uses in the branches that category picks.
    std::array<std::array<Uses, kCategories>, kI18nMaxSelectors> picked{};

    constexpr void Text(std::wstring_view) {}
    constexpr void Number() {}
    constexpr void Arg(std::wstring_view name) {
        ok = shape.args.Add(name) && (!inLink || shape.linked.Add(name)) && ok;
        size_t i = shape.args.Find(name);
        if (i == shape.args.count) return;
        if (inPlural) {
            branchUses[branch][i]++;
        } else {
            shape.fewest[i]++;
            shape.most[i]++;
        }
    }
    constexpr void PluralBegin(std::wstring_view name) {
        ok = shape.args.Add(name) && shape.plurals.Add(name) && ok;
        selector = shape.plurals.Find(name);
        ok = ok && selector < kI18nMaxSelectors;
        inPlural = true;
        present = 0;
        branches = 0;
        branchUses = {};
    }
    constexpr void Branch(I18nPlural category) {
        branch = size_t(category);
        present |= 1u << branch;
        ok = ok && (I18nPluralMask(rule) & (1u << branch));
        branchLinks = {};
    }
    constexpr void BranchEnd() {
        if (branches++ == 0) pluralLinks = branchLinks;
        else ok = ok && branchLinks == pluralLinks;
    }
    constexpr void PluralEnd() {
        for (size_t i = 0; i < pluralLinks.count; i++) {
            ok = ok && !shape.links.Has(pluralLinks.items[i]) && shape.links.Add(pluralLinks.items[i]);
        }
        for (size_t c = 0; selector < kI18nMaxSelectors && c < kCategories; c++) {
            if (!(I18nPluralMask(rule) & (1u << c))) continue;
            const Uses& uses = branchUses[present & (1u << c) ? c : size_t(I18nPlural::Other)];
            for (size_t i = 0; i < kI18nMaxNames; i++) picked[selector][c][i] += uses[i];
        }
        inPlural = false;
    }
    constexpr void BoldBegin() {}
    constexpr void BoldEnd() {}
    constexpr void LinkBegin(std::wstring_view id) {
        I18nNames& links = inPlural ? branchLinks : shape.links;
        ok = ok && !links.Has(id) && links.Add(id);
        inLink = true;
    }
    constexpr void LinkEnd() { inLink = false; }
};

// A template's shape under a plural rule (English keys: English).
static constexpr I18nShape I18nTemplateShape(std::wstring_view text, I18nPluralRule rule) {
    I18nShapeSink sink;
    sink.rule = rule;
    size_t at = 0;
    bool parsed = I18nParseMessage(text, at, sink, 0) && at == text.size();
    sink.shape.valid = parsed && sink.ok;
    // Each selector's plurals add their fewest and most uses over the categories it can select.
    for (size_t s = 0; s < std::min(sink.shape.plurals.count, kI18nMaxSelectors); s++) {
        for (size_t i = 0; i < kI18nMaxNames; i++) {
            size_t fewest = std::numeric_limits<size_t>::max(), most = 0;
            for (size_t c = 0; c < I18nShapeSink::kCategories; c++) {
                if (!(I18nPluralMask(rule) & (1u << c))) continue;
                fewest = std::min(fewest, sink.picked[s][c][i]);
                most = std::max(most, sink.picked[s][c][i]);
            }
            sink.shape.fewest[i] += fewest;
            sink.shape.most[i] += most;
        }
    }
    return sink.shape;
}
