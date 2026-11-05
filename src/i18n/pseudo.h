// The pseudo-locale (qps-ploc): English with accented letters, about 35% padding and brackets, so
// untranslated, cut or concatenated text stands out. Arguments, plurals, markup and link IDs stay.

// Re-emits a template with its text transformed.
struct I18nPseudoSink {
    std::wstring out = L"[";
    size_t letters = 0;  // Text characters, which the padding follows

    void Text(std::wstring_view text) {
        static constexpr std::wstring_view kUpper = L"ÅƁÇÐÉƑĜĤÎĴĶĹṀÑÖƤǪŔŠŦÛṼŴẊÝŽ";
        static constexpr std::wstring_view kLower = L"åƀçðéƒĝĥîĵķļɱñöþǫŕšţûṽŵẋýž";
        for (wchar_t c : text) {
            letters++;
            if (kI18nEscapable.find(c) != std::wstring_view::npos) out += L'\\';
            out += c >= L'A' && c <= L'Z' ? kUpper[c - L'A'] : c >= L'a' && c <= L'z' ? kLower[c - L'a'] : c;
        }
    }
    void Arg(std::wstring_view name) { out.append(L"{").append(name).append(L"}"); }
    void Number() { out += L'#'; }
    void PluralBegin(std::wstring_view name) { out.append(L"{").append(name).append(L", plural,"); }
    void Branch(I18nPlural category) { out.append(L" ").append(kI18nPluralNames[size_t(category)]).append(L"{"); }
    void BranchEnd() { out += L'}'; }
    void PluralEnd() { out += L'}'; }
    void BoldBegin() { out += L"<b>"; }
    void BoldEnd() { out += L"</b>"; }
    void LinkBegin(std::wstring_view id) { out.append(L"<link id=\"").append(id).append(L"\">"); }
    void LinkEnd() { out += L"</link>"; }
};

// The pseudo-locale's template for an English one; a testing boundary as well as the lookup's.
static std::wstring I18nPseudoTemplate(std::wstring_view english) {
    static constexpr std::wstring_view kPadding = L" ļöŕéɱ îþšûɱ ðöļöŕ šîţ åɱéţ";
    I18nPseudoSink sink;
    size_t at = 0;
    I18nParseMessage(english, at, sink, 0);
    for (size_t i = 0, pad = (sink.letters * 35 + 99) / 100; i < pad; i++) sink.out += kPadding[i % kPadding.size()];
    return sink.out + L']';
}
