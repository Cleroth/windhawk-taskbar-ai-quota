// Usage history window view: the explicit snapshot its frame builders read (history/shell.h,
// content.h), the ids and commands of its controls, the compact display formats and the summary
// cards built from a series set (history/series.h). Pure.

// Range segments, by HistoryUiRange (core/model.h).
static constexpr PCWSTR kHistoryUiRangeNames[] = {L"24h", L"7d", L"30d", L"90d", L"All"};

// The header's status pill: Recording (with the retention), Waiting (recording on while the
// taskbar hasn't saved account IDs yet or no sample is in range), Not saving (a storage error),
// Off (this user's recording is off). Before the first load it follows the settings alone.
enum class HistoryUiPill : uint8_t { Recording, Waiting, NotSaving, Off };

// An account as the window lists it, parallel to the collected accounts (HistoryCollectAccounts):
// its navigation entry, header and recording state, and its latest poll.
struct HistoryUiViewAccount {
    uint64_t historyId = 0;
    uint32_t slot = 0;          // Session-stable per history ID (below 0xFF000): its entry's item id
    uint32_t provider = 0;      // 1 Anthropic, 2 OpenAI, 3 Antigravity; else unknown
    std::wstring label;         // Removed accounts: account_<16 hex digits>
    bool removed = false;
    bool recordingOff = false;  // Its Record history is off
    bool paused = false;        // Hidden while hidden accounts aren't polled
    double weeklyPct = -1;      // The latest poll's weekly used %; < 0 unknown
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
    UiThemeKind theme = UiThemeKind::Dark;   // Built content follows it: value inks, card lines

    // Accounts and controls; the view is the current mode's (All accounts or one account)
    std::vector<HistoryUiViewAccount> accounts;
    int selected = -1;                       // Account index; -1: All accounts
    HistoryView view = HistoryView::Weekly;
    HistoryUiRange range = HistoryUiRange::Week;
    bool compare = false;                    // Shows the range as 7 days, disabled

    // Settings: recording (and whether the banner's Enable recording can run; else it shows
    // disabled), retention and the usage colors of navigation values
    bool recordingEnabled = true;
    bool canEnableRecording = false;
    int retentionDays = 90;                  // 0: forever
    int yellowThreshold = 50, orangeThreshold = 75, redThreshold = 90;
    bool colorblind = false, remaining = false;  // remaining: values in the Remaining bar mode

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

static PCWSTR HistoryUiProviderName(uint32_t provider) {
    return provider == 1 ? L"Anthropic" : provider == 2 ? L"OpenAI" :
           provider == 3 ? L"Google Antigravity" : L"Unknown provider";
}

// A provider's logo path (taskbar/logo-paths.h, 24 DIP view box), or nullptr.
static PCWSTR HistoryUiLogo(uint32_t provider) {
    return provider == 1 ? kClaudeLogoPath : provider == 2 ? kOpenAiLogoPath :
           provider == 3 ? kGeminiLogoPath : nullptr;
}

// Compact display formats (approved with the phase-3 mockups), English and independent of the
// locale like the CSV. Local times read `Sat 31 Oct 14:31`; outside the current year the year
// takes the weekday's place, `25 May 2026 23:59` (the readout's mandatory part keeps its width
// bound); `localTime` converts a Unix ms time with the user's clock and zone.
using HistoryUiLocalTime = std::function<SYSTEMTIME(ULONGLONG)>;
static constexpr PCWSTR kHistoryUiDays[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
static constexpr PCWSTR kHistoryUiMonths[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                              L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};

static std::wstring HistoryUiTime(ULONGLONG ms, ULONGLONG now, const HistoryUiLocalTime& localTime) {
    static constexpr int kMonthOffsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    SYSTEMTIME t = localTime(ms);
    if (t.wMonth < 1 || t.wMonth > 12 || t.wDay < 1 || t.wDay > 31) return L"\u2014";
    wchar_t clock[8];
    swprintf(clock, ARRAYSIZE(clock), L"%02u:%02u", unsigned(t.wHour % 24), unsigned(t.wMinute % 60));
    std::wstring date = std::to_wstring(t.wDay) + L' ' + kHistoryUiMonths[t.wMonth - 1];
    if (t.wYear != localTime(now).wYear) return date + L' ' + std::to_wstring(t.wYear) + L' ' + clock;
    // The weekday from the date (Sakamoto's method), whatever the conversion filled in.
    int year = t.wYear - (t.wMonth < 3);
    int weekday = (year + year / 4 - year / 100 + year / 400 + kMonthOffsets[t.wMonth - 1] + t.wDay) % 7;
    return std::wstring(kHistoryUiDays[weekday]) + L' ' + date + L' ' + clock;
}

// `decimals` places below 10,000 after rounding; past that one decimal with a suffix (12.3K,
// 1.2M). Rounding carries into the next magnitude: 9999.996 at two places prints 10.0K, 999950
// prints 1.0M. Journal values stay within 1e12 (1.0T).
static std::wstring HistoryUiNumber(double value, int decimals) {
    static constexpr PCWSTR kSuffixes[] = {L"", L"K", L"M", L"G", L"T"};
    if (!std::isfinite(value)) return L"\u2014";
    char text[64];
    for (size_t level = 0;; level++) {
        auto [end, error] = std::to_chars(text, text + sizeof(text), value / std::pow(1000.0, double(level)),
                                          std::chars_format::fixed, level ? 1 : decimals);
        if (error != std::errc{}) return L"?";
        std::string_view printed(text, end);
        size_t digits = std::min(printed.find('.'), printed.size()) - (printed.front() == '-');
        if (digits <= (level ? 3u : 4u) || level + 1 == std::size(kSuffixes)) {
            return std::wstring(printed.begin(), printed.end()) + kSuffixes[level];
        }
    }
}

// A count with thousands separators: `12,345`.
static std::wstring HistoryUiCount(size_t count) {
    std::wstring text = std::to_wstring(count);
    for (size_t at = text.size(); at > 3; at -= 3) text.insert(at - 3, 1, L',');
    return text;
}

// An export's progress line: `Exporting 12,345 rows…`. Digits are tabular: a count's width
// bounds every smaller one's.
static std::wstring HistoryUiProgressText(size_t rows) {
    return L"Exporting " + HistoryUiCount(rows) + L" rows\u2026";
}

// Hours to a tenth without a trailing zero: `166 h`, `0.4 h`.
static std::wstring HistoryUiHours(double ms) {
    char text[32];
    auto [end, error] = std::to_chars(text, text + sizeof(text), ms / 3600000.0, std::chars_format::fixed, 1);
    if (error != std::errc{}) return L"? h";
    std::string_view printed(text, end);
    if (printed.ends_with(".0")) printed.remove_suffix(2);
    return std::wstring(printed.begin(), printed.end()) + L" h";
}

// A meter's value with its unit: percentages with `percentDecimals` places (cards whole, the
// readout and shares one), dollars and credits with two.
static std::wstring HistoryUiValue(int meter, double value, int percentDecimals) {
    if (meter == 6) return L"USD " + HistoryUiNumber(value, 2);
    if (meter == 7) return HistoryUiNumber(value, 2) + L" credits";
    return HistoryUiNumber(value, percentDecimals) + L"%";
}

// The summary cards of a series set over [from, to), one per series in its order, and its
// accounts without readings, from the view's accounts (parallel to the set's), selection,
// comparison and consent: every peak and its time, coverage with its share, time at or above the
// red threshold the set was analyzed with for percentages, dollar spending's last and peak
// readings, and an account's recording status once (in All accounts, on its first card).
static void HistoryUiSetCards(HistoryUiView& view, const HistorySeriesSet& set, ULONGLONG from, ULONGLONG to,
                              ULONGLONG now, const HistoryUiLocalTime& localTime) {
    view.cards.clear();
    view.noReadings.clear();
    auto status = [&](size_t account) -> std::wstring {
        const HistoryUiViewAccount& a = view.accounts[account];
        if (a.removed || !view.recordingEnabled) return {};
        return a.recordingOff ? L"recording off" : a.paused ? L"paused" : L"";
    };
    double duration = double(to > from ? to - from : 0);
    size_t previousAccount = SIZE_MAX;
    for (const HistorySeries& series : set.series) {
        if (series.account >= view.accounts.size()) continue;
        const HistoryUiViewAccount& account = view.accounts[series.account];
        HistoryUiCard card;
        card.title = series.legendTitle;
        if (!series.plotted) card.title += L" \u2014 details only";
        if (view.selected < 0 && series.account != previousAccount) card.status = status(series.account);
        previousAccount = series.account;
        card.color = series.color;
        card.detailsOnly = !series.plotted;
        auto reading = [&](double value, ULONGLONG time) {
            return HistoryUiValue(series.meter, value, 0) + L" \u00B7 " + HistoryUiTime(time, now, localTime);
        };
        const HistoryStatistics& current = series.current;
        if (current.hasValue) {
            card.value = HistoryUiValue(series.meter, current.latest, 0);
            bool latestPoll = !account.stale && account.lastPollMs && current.latestTime >= account.lastPollMs;
            card.when = latestPoll ? L"now" : HistoryUiTime(current.latestTime, now, localTime);
        }
        // Per period (comparing: current, then previous, each first line prefixed). The headline
        // holds the current period's last dollar reading.
        for (int period = 0; period < (view.compare ? 2 : 1); period++) {
            const HistoryStatistics& stats = period ? series.previous : series.current;
            std::wstring prefix = !view.compare ? L"" : period ? L"Previous: " : L"Current: ";
            auto line = [&](std::wstring text) { card.lines.push_back(std::exchange(prefix, {}) + text); };
            if (!stats.hasValue) {
                line(series.plotted ? L"Peak \u2014" : L"No dollar readings in this period.");
                continue;
            }
            if (!series.plotted && period) line(L"Last recorded spend: " + reading(stats.latest, stats.latestTime));
            line(L"Peak " + reading(stats.peak, stats.peakTime));
            if (!series.plotted) continue;
            line(L"Coverage " + HistoryUiHours(stats.coverageMs) + L" (" +
                 HistoryUiNumber(duration > 0 ? 100 * stats.coverageMs / duration : 0, 1) + L"%)");
            if (series.meter < 6) {
                line(L"Est. \u2265" + std::to_wstring(set.redThreshold) + L"%: " +
                     (stats.coverageMs ? HistoryUiHours(stats.nearLimitMs) : L"\u2014"));
            }
        }
        view.cards.push_back(std::move(card));
    }

    // Accounts without readings, in account order.
    for (size_t i : set.noReadingAccounts) {
        if (i >= view.accounts.size()) continue;
        std::wstring detail = HistoryUiProviderName(view.accounts[i].provider);
        if (std::wstring s = status(i); !s.empty()) detail += L" \u00B7 " + s;
        view.noReadings.push_back({view.accounts[i].provider, view.accounts[i].label, std::move(detail)});
    }
}
