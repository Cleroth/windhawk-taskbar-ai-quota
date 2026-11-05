// Languages: the registry, the pseudo-locale, explicit choices by tag, Automatic's match over the
// user's preferred languages, and the localization context pure code receives.

struct I18nLanguage {
    std::wstring_view tag;         // BCP-47; what an explicit choice stores
    std::wstring_view nativeName;  // The language list's name for it
    std::wstring_view localeName;  // DirectWrite's locale and XAML's Language
    I18nPluralRule plural;
    bool inProgress;               // Automatic skips it; an explicit choice doesn't
    std::span<const I18nEntry> catalog;
    bool pseudo;                   // English transformed at lookup
};

// English, the source language, first.
static constexpr I18nLanguage kI18nLanguages[] = {
    {L"en", L"English", L"en-US", I18nPluralRule::English, false, {}, false},
    {L"de", L"Deutsch", L"de-DE", I18nPluralRule::English, true, kI18nCatalogDe, false},
    {L"es", L"Español", L"es-ES", I18nPluralRule::Spanish, true, kI18nCatalogEs, false},
    {L"fr", L"Français", L"fr-FR", I18nPluralRule::French, true, kI18nCatalogFr, false},
    {L"ja", L"日本語", L"ja-JP", I18nPluralRule::OtherOnly, true, kI18nCatalogJa, false},
    {L"pt-BR", L"Português (Brasil)", L"pt-BR", I18nPluralRule::French, true, kI18nCatalogPtBr, false},
    {L"ru", L"Русский", L"ru-RU", I18nPluralRule::Russian, true, kI18nCatalogRu, false},
    {L"zh-Hans", L"简体中文", L"zh-CN", I18nPluralRule::OtherOnly, true, kI18nCatalogZhHans, false},
};

// The pseudo-locale (i18n/pseudo.h): not among the languages, so only an explicit choice reaches it.
static constexpr I18nLanguage kI18nPseudoLanguage{L"qps-ploc", L"Pseudo-locale", L"en-US", I18nPluralRule::English,
                                                  true, {}, true};

// Tags compare ASCII case-insensitively, reading '_' as '-'.
static constexpr bool I18nTagEquals(std::wstring_view a, std::wstring_view b) {
    auto fold = [](wchar_t c) { return c == L'_' ? L'-' : c >= L'A' && c <= L'Z' ? wchar_t(c - L'A' + L'a') : c; };
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++) {
        if (fold(a[i]) != fold(b[i])) return false;
    }
    return true;
}

// API entry points carry [[maybe_unused]]: a build or test target may use only some of them.

// An explicit choice by its stored tag, in progress or the pseudo-locale; null when unknown.
[[maybe_unused]] static const I18nLanguage* I18nLanguageByTag(std::wstring_view tag) {
    for (const I18nLanguage& language : kI18nLanguages) {
        if (I18nTagEquals(language.tag, tag)) return &language;
    }
    return I18nTagEquals(kI18nPseudoLanguage.tag, tag) ? &kI18nPseudoLanguage : nullptr;
}

// Automatic: for each preferred tag in order, the first finished language it names, else the next
// tag; English (the registry's first entry) when none does. A tag names its exact language, else
// its primary language's (de-AT: de, pt-PT: pt-BR). Chinese goes by script, explicit or implied by
// the region: Hans (zh-Hans, zh-CN, zh-SG, bare zh) or Hant (zh-Hant, zh-TW, zh-HK, zh-MO), which
// no catalog serves; an explicit script beats the region. languages: the registry; tests pass theirs.
[[maybe_unused]] static const I18nLanguage& I18nMatchLanguage(
    std::span<const std::wstring_view> preferred, std::span<const I18nLanguage> languages = kI18nLanguages) {
    auto primary = [](std::wstring_view tag) { return tag.substr(0, tag.find_first_of(L"-_")); };
    for (std::wstring_view tag : preferred) {
        std::wstring_view wanted = tag;
        if (I18nTagEquals(primary(tag), L"zh")) {
            // BCP-47 positions: extended languages (3 letters), then a script (4 letters), then a
            // region (2 letters or 3 digits); variants and singletons (extensions, private use) end it.
            auto all = [](std::wstring_view s, bool letters) {
                return std::all_of(s.begin(), s.end(), [letters](wchar_t c) {
                    return letters ? (c | 0x20) >= L'a' && (c | 0x20) <= L'z' : c >= L'0' && c <= L'9';
                });
            };
            std::wstring_view script, region;
            for (std::wstring_view rest = tag.substr(std::min<size_t>(tag.size(), 3)); !rest.empty();) {
                size_t cut = rest.find_first_of(L"-_");
                std::wstring_view subtag = rest.substr(0, cut);
                rest = cut == std::wstring_view::npos ? std::wstring_view{} : rest.substr(cut + 1);
                if (script.empty() && subtag.size() == 3 && all(subtag, true)) continue;
                if (script.empty() && subtag.size() == 4 && all(subtag, true)) {
                    script = subtag;
                    continue;
                }
                if ((subtag.size() == 2 && all(subtag, true)) || (subtag.size() == 3 && all(subtag, false))) {
                    region = subtag;
                }
                break;
            }
            bool hans = !script.empty() ? I18nTagEquals(script, L"Hans") :
                        !I18nTagEquals(region, L"TW") && !I18nTagEquals(region, L"HK") && !I18nTagEquals(region, L"MO");
            if (!hans) continue;
            wanted = L"zh-Hans";
        }
        const I18nLanguage* parent = nullptr;
        for (const I18nLanguage& language : languages) {
            if (language.inProgress) continue;
            if (I18nTagEquals(language.tag, wanted)) return language;
            if (!parent && I18nTagEquals(primary(language.tag), primary(wanted))) parent = &language;
        }
        if (parent) return *parent;
    }
    return languages.front();
}

// The localization context: one immutable value per frame, notification or dialog that pure code
// receives through its inputs, never from a global. Regional formats join it later. Default:
// English.
class I18nLocale {
public:
    constexpr I18nLocale() = default;
    constexpr explicit I18nLocale(const I18nLanguage& language) : language_(&language) {}

    constexpr const I18nLanguage& Language() const { return *language_; }
    // DirectWrite's locale and XAML's Language.
    constexpr std::wstring_view LocaleName() const { return language_->localeName; }

    constexpr bool operator==(const I18nLocale&) const = default;

private:
    const I18nLanguage* language_ = &kI18nLanguages[0];
};
