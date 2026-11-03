// Journal coordination state shared by the history journal, fetch and settings.

// Loaded IDs are not recording identities until the shell owner persists them.
static std::atomic<bool> g_historyIdsPendingSave{false};

static std::mutex g_historyMutex;  // Leaf: never acquire settings/UI/auth locks inside it.
static std::atomic<uint64_t> g_historyEpoch{1};  // Clear invalidates in-flight fetch batches.
static std::atomic<ULONGLONG> g_historySettingsGeneration{0};
// The published retention in days (0: forever, no size cap). Journal operations read it under the
// history lock once loaded, so none prunes or caps by a setting superseded before it ran.
static std::atomic<int> g_historyRetentionDays{0};
static std::atomic<bool> g_historyStorageError{false};

static ULONGLONG g_historyContinuityGeneration = 0;  // Protected by g_settingsMutex.
