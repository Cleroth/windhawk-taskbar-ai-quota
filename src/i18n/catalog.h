// Catalog entries: one language's translation of an English key in its context. Each language's
// catalog (i18n/lang-*.h) is sorted by (context, English) with no duplicates, for binary search.

struct I18nEntry {
    std::wstring_view context;
    std::wstring_view english;
    std::wstring_view translation;
};

// The catalogs' order: context, then English, by UTF-16 code units.
static constexpr bool I18nEntryBefore(const I18nEntry& a, const I18nEntry& b) {
    return a.context != b.context ? a.context < b.context : a.english < b.english;
}

static constexpr bool I18nCatalogSorted(std::span<const I18nEntry> catalog) {
    return std::adjacent_find(catalog.begin(), catalog.end(), [](const I18nEntry& a, const I18nEntry& b) {
               return !I18nEntryBefore(a, b);
           }) == catalog.end();
}
