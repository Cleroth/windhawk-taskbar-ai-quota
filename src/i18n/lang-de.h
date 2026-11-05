// German (de): {context, English, translation}, sorted by context, then English.
// Adding, changing and translating entries: i18n/AGENTS.md.
static constexpr I18nEntry kI18nCatalogDe[] = {
    {L"list-end", L"{a} and {b}", L"{a} und {b}"},
    {L"list-middle", L"{a}, {b}", L"{a}, {b}"},
    {L"list-pair", L"{a} and {b}", L"{a} und {b}"},
    {L"list-start", L"{a}, {b}", L"{a}, {b}"},
};
static_assert(I18nCatalogSorted(kI18nCatalogDe), "sort by (context, English), no duplicates");
