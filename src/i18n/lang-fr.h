// French (fr): {context, English, translation}, sorted by context, then English.
// Adding, changing and translating entries: i18n/AGENTS.md.
static constexpr I18nEntry kI18nCatalogFr[] = {
    {L"list-end", L"{a} and {b}", L"{a} et {b}"},
    {L"list-middle", L"{a}, {b}", L"{a}, {b}"},
    {L"list-pair", L"{a} and {b}", L"{a} et {b}"},
    {L"list-start", L"{a}, {b}", L"{a}, {b}"},
};
static_assert(I18nCatalogSorted(kI18nCatalogFr), "sort by (context, English), no duplicates");
