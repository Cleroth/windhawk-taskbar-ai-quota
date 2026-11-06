# Localization

Pure fragments (no globals, state, WinRT, registry or `Wh_Log`): `template.h` (grammar, plural
rules), `catalog.h` (entry order), `lang-<tag>.h` (one catalog per language), `languages.h`
(registry, pseudo-locale, explicit choice, Automatic match, `I18nNumberFormat`, `I18nLocale`),
`pseudo.h`, `format.h` (keys, arguments, runs, fallback, lists), `numbers.h` (regional number
formats), `ui-runs.h` (runs as the toolkit's, after `ui/`). English is the source and has no
catalog. The process's current language, the per-user preference and the user's regional format
are native: `core/language.h` (rules: [../AGENTS.md](../AGENTS.md) Concurrency).

## Call forms

- Keys: `Tr(L"English")`, or `TrCtx(L"context", L"English")` when the same English can differ in
  role or meaning. Only these make an `I18nKey`, and only from wide string literals; adjacent
  literals join, with nothing but whitespace between them. A malformed or empty English template,
  or an empty context, fails the build.
- `I18nText(locale, key, {{L"name", value}, ...})`: plain text. `I18nRuns(...)`: runs, each plain,
  bold or a link ID (never a bold link: bold around a link is dropped); `I18nUiRuns(runs,
  {{L"id", action}, ...})` makes them `UiRun`s, each link bound to the action its ID names (an
  unbound one plain), in the translation's order, which is the links' focus order;
  `I18nRunsText` joins them. `I18nList(locale,
  items)`: items' runs joined by the catalog's list patterns (contexts `list-pair`, `list-start`,
  `list-middle`, `list-end`).
- Arguments: text (inserted as is, never parsed), integers of any type (ASCII digits, `-` when
  negative, no grouping), an integer with its shown text (`{L"rows", rows, I18nInteger(locale,
  rows)}`: a plural selects by the number, `#` and `{rows}` show the text), or trusted runs (keep
  their bold and links, take the template's bold, and its link where they have none). Build them
  in the call: they refer to the caller's values. A missing one shows as `{name}`.
- Pure code gets an `I18nLocale` (language, locale name, regional number format) through its
  inputs, never a global:
  `SettingsView::locale`, `HistoryUiView::locale` (the controller takes it from
  `HistoryOps::Locale`, and passes it on in `HistorySeriesQuery::locale` and
  `HistoryChart::locale`), `MeterDisplay::locale` (`MeterDisplayFor`'s), and the history formats'
  `locale` parameter. The `ui/` toolkit gets only translated strings (toggle states, the
  readout's hint) and `UiGraphics::locale`.
- Account errors the mod authors are `AccountError` codes (`core/model.h`), with arguments in
  fields (a cause among them) and provider text in `detail`; `AccountErrorText`
  (`core/account-error.h`) formats them for presentation in the reader's language, inline or alone
  (sentence case), and for logs and their dedupe in English. Compare codes, never text.
  `AccountErrorCode::Message` holds provider text without a code of its own. The history journal's
  errors are `HistoryJournalError` codes the same way (`history/journal-error.h`), and so are the
  settings window's notices and draft errors (`SettingsNoticeCode`, `SettingsDraftError` in
  `settings/edits.h`) and the history window's operation notices (`HistoryNoticeCode`,
  `history/controller.h`), formatted when a message box opens or a frame is built.
- Native code snapshots the context with `CurrentLocale()` (`core/language.h`): the current
  language and the user's regional format, read anew each time.
- `I18nLanguageByTag` (explicit choice) reaches any language and `qps-ploc`;
  `I18nMatchLanguage` (Automatic) skips `inProgress` ones and never picks `qps-ploc`. Every
  shipped language is finished; a new one stays `inProgress` until its catalog is reviewed.
- Intentional English literals (IDs, CSV, protocol text) carry `// i18n-en` on their line;
  `Wh_Log` arguments need none.
- API entry points carry `[[maybe_unused]]`: a build or test target may use only some.

## Regional formats

Numbers follow the user's Windows regional format, words the UI language. `I18nLocale::Numbers()`
holds the decimal separator, digit group separator and grouping (`LOCALE_SGROUPING`; default
en-US: `.`, `,`, `3;0`). `numbers.h`: `I18nFixed` (fixed decimals), `I18nTrimmed` (trailing
decimal zeros dropped), both never grouped; `I18nInteger` (the region's digit groups);
`I18nDigits` (ASCII as `to_chars` writes it, for formats those don't cover). Digits stay ASCII
and `-`; `%`, `$`, `USD`, `k`/`M` and `K`/`M`/`G`/`T` stay the callers'.

- Group only what English grouped before (counts such as export rows, number boxes); amounts,
  values, minutes and day counts stay ungrouped, so en-US English stays as it was.
- Never for text that is read back: CSV, logs, storage, protocol, copied version info.
- Settings number boxes parse what `I18nInteger` shows (a space separator takes any space), in
  the format the box's text was set in as typing starts (the frame's, unless an IME composition
  kept older text), kept while the draft is text.
- Times: the taskbar, flyout and notifications use Windows' regional time formats; the usage
  history window the language's compact 24-hour form.

## Grammar

- Escapes: `\{ \} \< \# \\` (in C++: `L"\\{"`); any other backslash is an error.
- `{name}`: argument, `[A-Za-z_][A-Za-z0-9_]*`, spaces allowed inside the braces; repeatable.
- `{n, plural, one{...} other{...}}`: categories `zero one two few many other`, each once,
  `other` required. In a branch `#` is n; elsewhere `#` is literal. n's category picks the branch,
  else `other`.
- `<b>...</b>`, `<link id="name">...</link>`: exactly these forms, never empty. Any other `<` is
  an error; `>` is literal.
- Nesting: plurals never nest, tags never nest; a tag may hold a plural or sit in a branch. At
  most 16 distinct argument names, 16 link IDs and 4 plural selectors.
- Plural rules (integer CLDR): en, de one (1); es one (1), many (non-zero multiples of 1,000,000);
  fr, pt-BR one (0, 1), many (as es); ru one, few, many; ja, zh-Hans other only.
- A translation is used only when it parses, uses only its language's categories, has each link
  once outside plurals or in every branch of one plural, and has the English template's argument
  names, plural selectors and link IDs. Per expansion (one category per selector: plurals on one
  selector pick together, a missing category picks `other`, and so does a selector that isn't a
  number, Russian's included), an argument English always shows must always show, and none more
  often than English's most (plural selectors aside: `#` may replace them). An argument sits in a
  link only where English has it in one (a linked argument would split the link). Otherwise, or
  when empty, the whole entry shows in English with English plural rules.

## Catalogs

`lang-<tag>.h`: `static constexpr I18nEntry kI18nCatalog<Tag>[] = {{L"context", L"English",
L"translation"}, ...};` and its `static_assert(I18nCatalogSorted(...))`. Sorted by context (empty
first), then English, by UTF-16 code units; no duplicates. The English column equals the key
(after C++ escapes): a changed English key loses its translation until the entries change too. A
new language needs its file, an entry include before `languages.h`, a registry entry and a place
in `tests/settings/extract.py`'s `I18N`.

## Adding or changing a string

- Owner rule: a change that adds or edits English UI text updates all seven catalogs in the same
  change.
- Template, never concatenation; translated templates never go through `swprintf`.
- Add or replace the entry in every `lang-*.h` at its sorted position, then run the checker.

## Translator notes

- Translations are machine-translated by agents and open to community fixes.
- Keep argument names, plural selectors and link IDs exactly; reorder them freely. Write every
  category your language uses, and `other`.
- The context names the use; read the call site when it isn't enough.
- Compact text (taskbar keys and labels, tile text, segmented controls, chart axes, buttons) has
  fixed room: stay as short as the English and abbreviate rather than overflow. Nothing changes
  case automatically: write the case shown.
- `quota-unit-order` orders the details flyout's number and its unit; only the arguments' order
  shows, their spacing is the layout's.

## Checker

`python tests/i18n/check.py` (`--src DIR`, `--json FILE`); self-test
`python tests/i18n/test_check.py`. Read-only except `--json`'s file. It extracts the keys of
`src/`, parses the catalogs and the registry, and reports: unextractable key calls, malformed
English keys, catalogs outside the registry, and per language missing, unused or stale, duplicate
and unsorted entries and argument, link and plural mismatches (the runtime's rules); then wide
literals outside `src/i18n/` that look user-facing and are neither wrapped, `Wh_Log` arguments
nor marked (a heuristic, not an inventory: single lowercase words such as `left` slip through).
Any other use of `Tr`/`TrCtx` than a literal call or an `I18nKey` declaration is reported. Exit 0 with a report whatever it finds; 1 when it can't read the sources or parse a
catalog or the registry. A complete string change shows none of the catalog findings for its
keys.
