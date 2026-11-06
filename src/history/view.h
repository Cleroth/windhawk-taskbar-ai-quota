// Usage history window view: the explicit snapshot its frame builders read (history/shell.h,
// content.h), the ids and commands of its controls, the compact display formats and the summary
// cards built from a series set (history/series.h). Pure.

// Range segments, by HistoryUiRange (core/model.h).
static constexpr I18nKey kHistoryUiRangeNames[] = {TrCtx(L"history-range", L"24h"), TrCtx(L"history-range", L"7d"),
                                                   TrCtx(L"history-range", L"30d"), TrCtx(L"history-range", L"90d"),
                                                   TrCtx(L"history-range", L"All")};

// The header's status pill: Recording (with the retention), Waiting (recording on while the
// taskbar hasn't saved account IDs yet or no sample is in range), Not saving (a storage error),
// Off (this user's recording is off). Before the first load it follows the settings alone.
enum class HistoryUiPill : uint8_t { Recording, Waiting, NotSaving, Off };

// An account as the window lists it, parallel to the collected accounts (HistoryCollectAccounts):
// its navigation entry, header and recording state, and its latest poll.
struct HistoryUiViewAccount {
    uint64_t historyId = 0;
    uint32_t slot = 0;          // Session-stable per history ID (below 0xFF000): its entry's item id
    uint32_t provider = 0;      // HistoryProviderCode; else unknown
    std::wstring label;         // Removed accounts: account_<16 hex digits>
    bool removed = false;
    bool recordingOff = false;  // Its Record history is off
    bool paused = false;        // Hidden while hidden accounts aren't polled
    double weeklyPct = -1;      // The latest poll's weekly (GitHub Copilot: AI credits) used %; < 0 unknown
    bool stale = false;         // That poll is stale: the value shows muted, cards never say "now"
    ULONGLONG lastPollMs = 0;   // The latest successful poll; 0 none
    bool operator==(const HistoryUiViewAccount&) const = default;
};

// A summary card (approved Q10 a): a series' chip name and line, a headline with the current
// period's latest value ("now" when it is the account's latest fresh poll, else its time), then
// its statistics lines. Details-only dollar spending has no line.
struct HistoryUiCard {
    std::wstring title;
    std::wstring status;        // After the title, muted: the account's recording status
    COLORREF color = 0;         // The series' line color (history/series.h palette)
    bool detailsOnly = false;
    std::wstring value, when;   // Empty without a current reading
    std::vector<std::wstring> lines;
    bool operator==(const HistoryUiCard&) const = default;
};

// An account without readings in range: its logo, label and provider (with its status).
struct HistoryUiNoReadings {
    uint32_t provider = 0;
    std::wstring label, detail;
    bool operator==(const HistoryUiNoReadings&) const = default;
};

// A plotted series as the chart, its legend chip and the readout show it (approved q8a-filter):
// the chip's item id (per history ID and meter, stable for the session), the chip's name (the
// readout's too) and the full title, the series' color and width, the range of its readings in
// both periods (with 0), and whether the legend hides it.
struct HistoryUiLine {
    uint32_t chip = 0;
    uint64_t historyId = 0;
    int meter = 0;
    size_t series = 0, account = 0;  // Its series (and card) in the set; its collected account
    std::wstring name, title;
    COLORREF color = 0;
    int stroke = 3;
    double low = 0, high = 0;
    bool hidden = false;
    bool operator==(const HistoryUiLine&) const = default;
};

// An operation's result shown inline (approved Q3 a, s-states): failures as errors, successes as
// information, until the next operation or a selection change. A cancelled operation shows none.
struct HistoryUiNotice {
    std::wstring text;
    UiSeverity severity = UiSeverity::Info;
    bool operator==(const HistoryUiNotice&) const = default;
};

struct HistoryUiView {
    I18nLocale locale;                       // The language and regional number format its text is in
    UiThemeKind theme = UiThemeKind::Dark;   // Built content follows it: value inks, card lines

    // Accounts and controls; the view is the current mode's (All accounts or one account)
    std::vector<HistoryUiViewAccount> accounts;
    int selected = -1;                       // Account index; -1: All accounts
    HistoryView view = HistoryView::Weekly;
    HistoryUiRange range = HistoryUiRange::Week;
    bool compare = false;                    // Shows the range as 7 days, disabled

    // Settings: recording (and whether the banner's Enable recording can run; else it shows
    // disabled), retention, the usage colors of navigation values, the logos' brand colors and
    // the window's material
    bool recordingEnabled = true;
    bool canEnableRecording = false;
    int retentionDays = 90;                  // 0: forever
    int yellowThreshold = 50, orangeThreshold = 75, redThreshold = 90;
    std::array<uint32_t, 4> usageColors = UsagePaletteColors(UsagePalette::Default, kDefaultUsageColors);
    bool remaining = false;                  // Values in the Remaining bar mode
    LogoBrands logoBrands;
    Material material = Settings{}.material;

    // Status: the pill, the header's status text ("Updated Sat 31 Oct 14:32"; empty before the
    // first load), the journal's state (its status without the cap note, the cap) and an
    // operation's notice (none: empty text)
    HistoryUiPill pill = HistoryUiPill::Recording;
    std::wstring status;
    std::wstring storageError;
    bool storageCapped = false;
    HistoryUiNotice notice;

    // Export: whether it can start (once loaded), and whether its write runs (s-states: its
    // progress in the status slot, every control but the legend's chips disabled)
    bool canExport = false;
    bool exporting = false;

    // Content: the loading state before the first load, then cards (HistoryUiSetCards) and the
    // chart's lines
    bool loading = true;
    std::vector<HistoryUiCard> cards;
    std::vector<HistoryUiNoReadings> noReadings;
    std::vector<HistoryUiLine> lines;
    bool operator==(const HistoryUiView&) const = default;
};

// Interactive items (and the export's progress, painted by id) keep these ids across rebuilds, so
// focus follows them; an account's entry takes kHistoryUiNavAccount + its slot, a legend chip
// kHistoryUiChip + 8 x its account's slot + its meter. Other items take running ids from
// kHistoryUiFirstRunning.
enum HistoryUiControl : uint32_t {
    kHistoryUiNavAll = 1, kHistoryUiRefresh, kHistoryUiExport, kHistoryUiMore, kHistoryUiViews,
    kHistoryUiRanges, kHistoryUiCompare, kHistoryUiEnableRecording, kHistoryUiChart, kHistoryUiProgress,
    kHistoryUiDeleteHistory,
    kHistoryUiNavAccount = 0x1000,
    kHistoryUiFirstRunning = 0x100000,
    kHistoryUiChip = 0x800000,
};

// What an item's action asks for (UiAction::target): Select (key = history ID, 0: All accounts),
// View (value = HistoryView), Range (value = HistoryUiRange), Compare (value = the desired state),
// Refresh, Export, Menu (value = HistoryUiMenuItem), EnableRecording, Line (a legend chip: key =
// history ID, value = meter; shows or hides that line), DeleteHistory (key = a removed account's
// history ID).
enum class HistoryUiCommand : uint32_t {
    None, Select, View, Range, Compare, Refresh, Export, Menu, EnableRecording, Line, DeleteHistory
};
// The More menu (approved Q9 a, s-menu).
enum class HistoryUiMenuItem : int { Settings, Folder, About, Clear };

static UiAction HistoryUiAction(HistoryUiCommand command, int value = 0, uint64_t key = 0) {
    return {UiActionKind::Command, value, {}, uint32_t(command), key};
}

// A provider's brand name (English in every language), or an unknown one's description.
static std::wstring HistoryUiProviderName(const I18nLocale& locale, uint32_t provider) {
    if (provider < 1 || provider > 5) return I18nText(locale, TrCtx(L"history-provider", L"Unknown provider"));
    return provider == 1 ? L"Anthropic" : provider == 2 ? L"OpenAI" :              // i18n-en
           provider == 3 ? L"Google Antigravity" : provider == 4 ? L"GitHub Copilot" :  // i18n-en
                           L"OpenCode Go";                                          // i18n-en
}

// A provider's logo (taskbar/logo-paths.h, 24 DIP view box) in the primary ink, or in its brand
// colors when chosen and `brand`; none for an unknown provider.
static std::vector<UiShapePart> HistoryUiLogo(const HistoryUiView& view, uint32_t provider, bool brand) {
    PCWSTR path = provider == 1 ? kClaudeLogoPath : provider == 2 ? kOpenAiLogoPath :
                  provider == 3 ? kGeminiLogoPath : provider == 4 ? kCopilotLogoPath :
                  provider == 5 ? kOpenCodeLogoPath : nullptr;
    if (!path) return {};
    UiShapePart part{path, {}, UiInk::Primary};
    const LogoBrand* colors =
        brand ? ProviderLogoBrand(view.logoBrands, provider == 1 ? L"anthropic" : provider == 3 ? L"antigravity" : L"")
              : nullptr;
    if (colors) {
        part.fill = UiPartFill{UiRgb(colors->from & 0xFFFFFF), UiRgb(colors->to & 0xFFFFFF),
                               {colors->x0, colors->y0}, {colors->x1, colors->y1}};
    }
    return {part};
}

// Compact display formats (approved with the phase-3 mockups) in the locale's language: each
// language's own order and abbreviated names (the catalog's), always 24-hour, independent of
// Windows' regional date and time formats like the CSV; numbers below take the locale's regional
// number format. Local times read `Sat 31 Oct 14:31` in English; outside
// the current year the year takes the weekday's place, `25 May 2026 23:59` (the readout's
// mandatory part keeps its width bound); `localTime` converts a Unix ms time with the user's
// clock and zone. Months come in a date's form, and alone with a year (Russian: мая, май).
using HistoryUiLocalTime = std::function<SYSTEMTIME(ULONGLONG)>;
static constexpr I18nKey kHistoryUiDays[] = {
    TrCtx(L"date-weekday", L"Sun"), TrCtx(L"date-weekday", L"Mon"), TrCtx(L"date-weekday", L"Tue"),
    TrCtx(L"date-weekday", L"Wed"), TrCtx(L"date-weekday", L"Thu"), TrCtx(L"date-weekday", L"Fri"),
    TrCtx(L"date-weekday", L"Sat")};
static constexpr I18nKey kHistoryUiMonths[] = {
    TrCtx(L"date-month", L"Jan"), TrCtx(L"date-month", L"Feb"), TrCtx(L"date-month", L"Mar"),
    TrCtx(L"date-month", L"Apr"), TrCtx(L"date-month", L"May"), TrCtx(L"date-month", L"Jun"),
    TrCtx(L"date-month", L"Jul"), TrCtx(L"date-month", L"Aug"), TrCtx(L"date-month", L"Sep"),
    TrCtx(L"date-month", L"Oct"), TrCtx(L"date-month", L"Nov"), TrCtx(L"date-month", L"Dec")};
static constexpr I18nKey kHistoryUiMonthsAlone[] = {
    TrCtx(L"date-month-alone", L"Jan"), TrCtx(L"date-month-alone", L"Feb"), TrCtx(L"date-month-alone", L"Mar"),
    TrCtx(L"date-month-alone", L"Apr"), TrCtx(L"date-month-alone", L"May"), TrCtx(L"date-month-alone", L"Jun"),
    TrCtx(L"date-month-alone", L"Jul"), TrCtx(L"date-month-alone", L"Aug"), TrCtx(L"date-month-alone", L"Sep"),
    TrCtx(L"date-month-alone", L"Oct"), TrCtx(L"date-month-alone", L"Nov"), TrCtx(L"date-month-alone", L"Dec")};

// A clock time, 24-hour: `14:31`.
static std::wstring HistoryUiClock(const SYSTEMTIME& t) {
    wchar_t clock[8];
    swprintf(clock, ARRAYSIZE(clock), L"%02u:%02u", unsigned(t.wHour % 24), unsigned(t.wMinute % 60));
    return clock;
}

static std::wstring HistoryUiTime(const I18nLocale& locale, ULONGLONG ms, ULONGLONG now,
                                  const HistoryUiLocalTime& localTime) {
    static constexpr int kMonthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    SYSTEMTIME t = localTime(ms);
    if (t.wMonth < 1 || t.wMonth > 12 || t.wDay < 1 || t.wDay > 31) return L"\u2014";
    std::wstring month = I18nText(locale, kHistoryUiMonths[t.wMonth - 1]), clock = HistoryUiClock(t);
    if (t.wYear != localTime(now).wYear) {
        return I18nText(locale, TrCtx(L"date-time-year", L"{day} {month} {year} {time}"),
                        {{L"day", t.wDay}, {L"month", month}, {L"year", t.wYear}, {L"time", clock}});
    }
    // The weekday from the date (Sakamoto's method), whatever the conversion filled in.
    int year = t.wYear - (t.wMonth < 3);
    int weekday = (year + year / 4 - year / 100 + year / 400 + kMonthOffsets[t.wMonth - 1] + t.wDay) % 7;
    return I18nText(locale, TrCtx(L"date-time", L"{weekday} {day} {month} {time}"),
                    {{L"weekday", I18nText(locale, kHistoryUiDays[weekday])}, {L"day", t.wDay}, {L"month", month},
                     {L"time", clock}});
}

// `decimals` places below 10,000 after rounding; past that one decimal with a suffix (12.3K,
// 1.2M). Rounding carries into the next magnitude: 9999.996 at two places prints 10.0K, 999950
// prints 1.0M. Journal values stay within 1e12 (1.0T). `trim` drops trailing decimal zeros (10K,
// 0.5). Never grouped; de-DE 9999,99, 12,3K.
static std::wstring HistoryUiNumber(const I18nLocale& locale, double value, int decimals, bool trim = false) {
    static constexpr PCWSTR kSuffixes[] = {L"", L"K", L"M", L"G", L"T"};
    if (!std::isfinite(value)) return L"\u2014";
    char text[64];
    for (size_t level = 0;; level++) {
        auto [end, error] = std::to_chars(text, text + sizeof(text), value / std::pow(1000.0, double(level)),
                                          std::chars_format::fixed, level ? 1 : decimals);
        if (error != std::errc{}) return L"?";
        std::string_view printed(text, end);
        size_t point = printed.find('.');
        size_t digits = std::min(point, printed.size()) - (printed.front() == '-');
        if (digits <= (level ? 3u : 4u) || level + 1 == std::size(kSuffixes)) {
            if (trim && point != std::string_view::npos) {
                printed.remove_suffix(printed.size() - 1 - printed.find_last_not_of('0'));
                if (printed.ends_with('.')) printed.remove_suffix(1);
            }
            return I18nDigits(locale, printed, false) + kSuffixes[level];
        }
    }
}

// An export's progress line: `Exporting 12,345 rows…`, the count in the regional digit groups.
// Digits are tabular: within a plural form, a count's width bounds every smaller one's.
static std::wstring HistoryUiProgressText(const I18nLocale& locale, size_t rows) {
    return I18nText(locale, TrCtx(L"history-export", L"{rows, plural, one{Exporting # rows\u2026} "
                                                     L"other{Exporting # rows\u2026}}"),
                    {{L"rows", rows, I18nInteger(locale, rows)}});
}

// Hours to a tenth without a trailing zero: `166 h`, `0.4 h`.
static std::wstring HistoryUiHours(const I18nLocale& locale, double ms) {
    return I18nText(locale, TrCtx(L"hours-value", L"{hours} h"), {{L"hours", I18nTrimmed(locale, ms / 3600000.0, 1)}});
}

// A recorded value as the window shows it: in the Remaining bar mode a percentage's share left
// (100 - used, below 0% past the limit, as used readings go past 100%); dollars, credits and used
// percentages as recorded.
static double HistoryUiShown(int meter, double value, bool remaining) {
    return remaining && meter < 6 ? 100 - value : value;
}

// A meter's value with its unit: percentages with `percentDecimals` places (cards whole, the
// readout and shares one), dollars and credits with two. Dollars keep their currency code and
// percentages their sign in every language and format. An abbreviated amount of credits (12.3K)
// takes its own template: it reads as thousands or more, which some languages word differently
// from a decimal amount (Russian: кредитов, not кредита).
static std::wstring HistoryUiValue(const I18nLocale& locale, int meter, double value, int percentDecimals) {
    if (meter == 6) return L"USD " + HistoryUiNumber(locale, value, 2);
    if (meter == 7) {
        std::wstring amount = HistoryUiNumber(locale, value, 2);
        bool abbreviated = !amount.empty() && std::wstring_view(L"KMGT").find(amount.back()) != std::wstring_view::npos;
        return I18nText(locale, abbreviated ? TrCtx(L"credits-amount-abbreviated", L"{amount} credits") :
                                              TrCtx(L"credits-amount", L"{amount} credits"),
                        {{L"amount", amount}});
    }
    return HistoryUiNumber(locale, value, percentDecimals) + L"%";
}

// The summary cards of a series set over [from, to), one per series in its order, and its
// accounts without readings, from the view's accounts (parallel to the set's), selection,
// comparison, bar mode, consent and language: every peak and its time, coverage with its share, time at or
// above the red threshold the set was analyzed with for percentages, dollar spending's last and
// peak readings, and an account's recording status once (in All accounts, on its first card).
// The Remaining bar mode shows percentages left: the peak is the lowest share left, the time at
// or below what the threshold leaves.
static void HistoryUiSetCards(HistoryUiView& view, const HistorySeriesSet& set, ULONGLONG from, ULONGLONG to,
                              ULONGLONG now, const HistoryUiLocalTime& localTime) {
    view.cards.clear();
    view.noReadings.clear();
    const I18nLocale& locale = view.locale;
    auto status = [&](size_t account) -> std::wstring {
        const HistoryUiViewAccount& a = view.accounts[account];
        if (a.removed || !view.recordingEnabled) return {};
        return a.recordingOff ? I18nText(locale, TrCtx(L"history-account-status", L"recording off")) :
               a.paused       ? I18nText(locale, TrCtx(L"history-account-status", L"paused")) : std::wstring();
    };
    double duration = double(to > from ? to - from : 0);
    size_t previousAccount = SIZE_MAX;
    for (const HistorySeries& series : set.series) {
        if (series.account >= view.accounts.size()) continue;
        const HistoryUiViewAccount& account = view.accounts[series.account];
        HistoryUiCard card;
        card.title = series.plotted ? series.legendTitle :
                                      I18nText(locale, TrCtx(L"history-card", L"{title} \u2014 details only"),
                                               {{L"title", series.legendTitle}});
        if (view.selected < 0 && series.account != previousAccount) card.status = status(series.account);
        previousAccount = series.account;
        card.color = series.color;
        card.detailsOnly = !series.plotted;
        const bool left = view.remaining && series.meter < 6;
        auto shown = [&](double value) {
            return HistoryUiValue(locale, series.meter, HistoryUiShown(series.meter, value, left), 0);
        };
        auto reading = [&](double value, ULONGLONG time) {
            return I18nText(locale, TrCtx(L"history-reading", L"{value} \u00B7 {time}"),
                            {{L"value", shown(value)}, {L"time", HistoryUiTime(locale, time, now, localTime)}});
        };
        auto peak = [&](const std::wstring& text) {
            return I18nText(locale, left ? TrCtx(L"history-card", L"Lowest {reading}") :
                                           TrCtx(L"history-card", L"Peak {reading}"),
                            {{L"reading", text}});
        };
        const HistoryStatistics& current = series.current;
        if (current.hasValue) {
            card.value = shown(current.latest);
            bool latestPoll = !account.stale && account.lastPollMs && current.latestTime >= account.lastPollMs;
            card.when = latestPoll ? I18nText(locale, TrCtx(L"history-card", L"now")) :
                                     HistoryUiTime(locale, current.latestTime, now, localTime);
        }
        // Per period (comparing: current, then previous, each first line naming it). The headline
        // holds the current period's last dollar reading.
        for (int period = 0; period < (view.compare ? 2 : 1); period++) {
            const HistoryStatistics& stats = period ? series.previous : series.current;
            bool named = !view.compare;
            auto line = [&](std::wstring text) {
                if (!std::exchange(named, true)) {
                    text = I18nText(locale, period ? TrCtx(L"history-card", L"Previous: {line}") :
                                                     TrCtx(L"history-card", L"Current: {line}"),
                                    {{L"line", text}});
                }
                card.lines.push_back(std::move(text));
            };
            if (!stats.hasValue) {
                line(series.plotted ? peak(L"\u2014") :
                                      I18nText(locale, TrCtx(L"history-card", L"No dollar readings in this period.")));
                continue;
            }
            if (!series.plotted && period) {
                line(I18nText(locale, TrCtx(L"history-card", L"Last recorded spend: {reading}"),
                              {{L"reading", reading(stats.latest, stats.latestTime)}}));
            }
            line(peak(reading(stats.peak, stats.peakTime)));
            if (!series.plotted) continue;
            line(I18nText(locale, TrCtx(L"history-card", L"Coverage {hours} ({percent}%)"),
                          {{L"hours", HistoryUiHours(locale, stats.coverageMs)},
                           {L"percent",
                            HistoryUiNumber(locale, duration > 0 ? 100 * stats.coverageMs / duration : 0, 1)}}));
            if (series.meter < 6) {
                std::wstring hours = stats.coverageMs ? HistoryUiHours(locale, stats.nearLimitMs) : L"\u2014";
                line(left ? I18nText(locale, TrCtx(L"history-card", L"Est. \u2264{threshold}%: {hours}"),
                                     {{L"threshold", 100 - set.redThreshold}, {L"hours", hours}}) :
                            I18nText(locale, TrCtx(L"history-card", L"Est. \u2265{threshold}%: {hours}"),
                                     {{L"threshold", set.redThreshold}, {L"hours", hours}}));
            }
        }
        view.cards.push_back(std::move(card));
    }

    // Accounts without readings, in account order.
    for (size_t i : set.noReadingAccounts) {
        if (i >= view.accounts.size()) continue;
        std::wstring provider = HistoryUiProviderName(locale, view.accounts[i].provider), recording = status(i);
        std::wstring detail = recording.empty() ? provider :
                              I18nText(locale, TrCtx(L"history-account", L"{provider} \u00B7 {recording}"),
                                       {{L"provider", provider}, {L"recording", recording}});
        view.noReadings.push_back({view.accounts[i].provider, view.accounts[i].label, std::move(detail)});
    }
}
