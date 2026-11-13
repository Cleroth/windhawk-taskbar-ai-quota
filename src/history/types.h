// Usage-history sample and snapshot types.

// History is a separate domain: sampled levels, never inferred token counts or spend.
// Fixed-width records contain no credentials, account labels, or server response text.
static constexpr size_t kHistoryMeterCount = 8;
static constexpr ULONGLONG kHistoryDayMs = 24ULL * 60 * 60 * 1000;
static constexpr ULONGLONG kHistoryWeekMs = 7 * kHistoryDayMs;
// The oldest time `retentionDays` keeps at `now`; 0 (forever) keeps every time.
static ULONGLONG HistoryRetentionStart(int retentionDays, ULONGLONG now) {
    ULONGLONG age = ULONGLONG(std::clamp(retentionDays, 0, 3650)) * kHistoryDayMs;
    return age && now > age ? now - age : 0;
}
// The calendar month (UTC) holding `unixMs`, a time of the clock's or the journal's (the calendar
// ends with the year 32767): when it ends and how long it is. Anthropic's extra usage runs over
// it; its usage response names neither.
struct UtcMonth {
    ULONGLONG endMs = 0;
    ULONGLONG durationMs = 0;
};
static UtcMonth UtcMonthOf(ULONGLONG unixMs) {
    using namespace std::chrono;
    const year_month_day day{floor<days>(sys_time<milliseconds>{milliseconds{(long long)unixMs}})};
    auto startMs = [](year_month month) {
        return (ULONGLONG)duration_cast<milliseconds>(sys_days{month / 1}.time_since_epoch()).count();
    };
    const year_month month = day.year() / day.month();
    const ULONGLONG endMs = startMs(month + months{1});
    return {endMs, endMs - startMs(month)};
}
struct HistoryValue {
    double value = -1;
    ULONGLONG resetMs = 0;
    ULONGLONG durationMs = 0;
};
struct HistorySample {
    uint64_t accountId = 0;
    ULONGLONG timeMs = 0;
    ULONGLONG intervalMs = 0;
    uint64_t session = 0;
    uint32_t provider = 0;  // HistoryProviderCode.
    uint32_t failed = 0;
    // Short, weekly, Fable weekly (Antigravity: Argon weekly), extra % (OpenCode Go: monthly),
    // 3p short, 3p weekly, extra $, credits.
    std::array<HistoryValue, kHistoryMeterCount> meters{};
    uint64_t checksum = 0;
};
static_assert(sizeof(HistorySample) == 240);
// A provider's journal code: 1 Anthropic, 2 OpenAI, 3 Antigravity, 4 GitHub Copilot, 5 OpenCode
// Go; 0 unknown.
static uint32_t HistoryProviderCode(const std::wstring& provider) {
    return provider == L"anthropic" ? 1 : provider == L"openai" ? 2 : provider == L"antigravity" ? 3 :
           provider == L"copilot" ? 4 : provider == L"opencode" ? 5 : 0;
}
// The journal's state when it was read: loaded (a damaged or unreadable journal stays loaded with
// an error status), then its epoch (Clear), revision (load, any rewrite, records leaving memory)
// and append count. Equal stamps hold the same samples.
struct HistoryJournalStamp {
    bool loaded = false;
    uint64_t epoch = 0, revision = 0, appended = 0;
    bool operator==(const HistoryJournalStamp&) const = default;
};
// Why the journal can't be read or saved (its status), or why an operation on it didn't run; its
// text, in the reader's language: HistoryJournalErrorText (history/journal-error.h).
enum class HistoryJournalError : uint8_t {
    None,
    Unavailable,
    CannotCreateDirectory,
    InUse,
    CannotRead,
    CannotReadSize,
    Unsupported,
    ReadFailed,
    DamagedRecord,
    TooLarge,
    CannotWrite,
    WriteFailed,
    NotSaved,
    AccountConfigured,  // A deletion's refusal: an account in the settings holds the history ID
};
struct HistorySnapshot {
    std::vector<HistorySample> samples;
    HistoryJournalError status = HistoryJournalError::None;  // The journal's error status; None while healthy
    bool capped = false;  // The storage cap pruned the oldest samples
    HistoryJournalStamp stamp;
    // Every sample from here on: the asked start, or later when the older ones couldn't be read
    ULONGLONG from = 0;
};
// A memory-only look at the journal: its stamp, status and cap, and a replacement for a held
// snapshot (every sample from `from` on, possibly none) or nullopt: no replacement. Incomplete:
// memory doesn't reach back to the asked start, so `from` is the held snapshot's start (or later,
// where memory begins); only an explicit load reads further back.
struct HistoryPeek {
    HistoryJournalStamp stamp;
    HistoryJournalError status = HistoryJournalError::None;
    bool capped = false;
    std::optional<std::vector<HistorySample>> samples;
    ULONGLONG from = 0;
    bool incomplete = false;
};
