// Usage-history samples and the journal coordination state shared with fetch and settings.

// Loaded IDs are not recording identities until the shell owner persists them.
static std::atomic<bool> g_historyIdsPendingSave{false};

// History is a separate domain: sampled levels, never inferred token counts or spend.
// Fixed-width records contain no credentials, account labels, or server response text.
static constexpr size_t kHistoryMeterCount = 8;
static constexpr ULONGLONG kHistoryDayMs = 24ULL * 60 * 60 * 1000;
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
    uint32_t provider = 0;  // 1 Anthropic, 2 OpenAI, 3 Antigravity.
    uint32_t failed = 0;
    // Short, weekly, Fable weekly, extra %, 3p short, 3p weekly, extra $, credits.
    std::array<HistoryValue, kHistoryMeterCount> meters{};
    uint64_t checksum = 0;
};
static_assert(sizeof(HistorySample) == 240);
struct HistorySnapshot {
    std::vector<HistorySample> samples;
    std::wstring status;
};

static std::mutex g_historyMutex;  // Leaf: never acquire settings/UI/auth locks inside it.
static std::atomic<uint64_t> g_historyEpoch{1};  // Clear invalidates in-flight fetch batches.
static std::atomic<ULONGLONG> g_historySettingsGeneration{0};
static std::atomic<bool> g_historyStorageError{false};

static ULONGLONG g_historyContinuityGeneration = 0;  // Protected by g_settingsMutex.
