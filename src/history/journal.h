// Local usage-history journal and its recording consent. Takes no UI or auth locks.

struct HistoryFileHeader {
    uint64_t magic = 0x3154534948514941ULL;  // AIQHIST1
    uint32_t version = 1;
    uint32_t recordBytes = sizeof(HistorySample);
};
// ~60 MB, independent of the days kept; forever retention has no cap.
static constexpr size_t kHistoryMaxSamples = 250000;
static constexpr ULONGLONG kHistoryTimestampLimitMs = 100000000000000ULL;
static constexpr ULONGLONG kHistoryMaxWindowMs = 3650 * kHistoryDayMs;
static constexpr ULONGLONG kHistoryMaintenanceIntervalMs = 3600000;
// In-memory journal state, then its storage state.
static std::deque<HistorySample> g_historySamples;
// Let incremental readers (burn-up curves, the history window's refresh) mirror
// g_historySamples: they copy only samples appended since their last copy while the revision is
// unchanged. The revision changes when samples are loaded, evicted by the size cap, pruned by
// retention or deleted with their account; Clear changes g_historyEpoch.
static uint64_t g_historyAppendCount = 0;
static uint64_t g_historyRevision = 0;
static std::wstring g_historyStatus;
static bool g_historyLoaded = false;
static bool g_historyCapped = false;
static std::wstring g_historyPath;
static bool g_historyWritable = true;
static bool g_historyDirty = false;
static ULONGLONG g_historyMaintenanceMs = 0;
static ULONGLONG g_historyWriteRetryMs = 0;
static winrt::file_handle g_historyOwnership;

// Each change of the status text notifies the settings and history windows (a posted message).
static void SetHistoryStatus(std::wstring message) {
    bool changed = message != g_historyStatus;
    g_historyStatus = std::move(message);
    g_historyStorageError = !g_historyStatus.empty();
    if (changed) NotifySettingsWindowChanged();
}

static bool IsHistoryOwnerProcess() {
    DWORD shellProcessId = 0;
    if (HWND taskbar = FindWindowW(L"Shell_TrayWnd", nullptr)) {
        GetWindowThreadProcessId(taskbar, &shellProcessId);
    }
    return shellProcessId && shellProcessId == GetCurrentProcessId();
}

static uint64_t HistoryChecksum(const HistorySample& sample) {
    uint64_t hash = 1469598103934665603ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&sample);
    for (size_t i = 0; i < offsetof(HistorySample, checksum); ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ULL;
    }
    return hash;
}

// Callers hold only g_historyMutex. An invalid format is preserved until explicit Clear. Reads
// the whole journal whatever the retention: maintenance and appends apply it.
static void LoadHistoryLocked(bool create = false) {
    if (g_historyLoaded) return;
    g_historyLoaded = true;
    ++g_historyRevision;
    g_historyWritable = true;
    SetHistoryStatus({});
    PWSTR localAppData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &localAppData))) {
        g_historyWritable = false;
        SetHistoryStatus(L"History storage is unavailable.");
        return;
    }
    std::wstring directory = localAppData;
    CoTaskMemFree(localAppData);
    // Windhawk's writable mod directory is shared across Windows users. Work-pattern
    // history belongs under the current user's profile, with its inherited private ACL.
    directory += L"\\TaskbarAIQuota";
    g_historyPath = directory + L"\\" WH_MOD_ID L"\\usage-history-v1.bin";
    if (!create && GetFileAttributesW(g_historyPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            g_historyLoaded = false;
            return;  // A read-only visit before opt-in must not create a directory or lock.
        }
    }
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        g_historyWritable = false;
        SetHistoryStatus(L"Cannot create the local history directory.");
        return;
    }
    directory += L"\\" WH_MOD_ID;
    if (!CreateDirectoryW(directory.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
        g_historyWritable = false;
        SetHistoryStatus(L"Cannot create the local history directory.");
        return;
    }
    g_historyPath = directory;
    g_historyPath += L"\\usage-history-v1.bin";
    // Separate Explorer processes, reloads, or sessions must never compact each other's
    // journal. The OS releases this exclusive ownership handle even after a shell crash.
    std::wstring lockPath = directory + L"\\history.lock";
    g_historyOwnership.attach(CreateFileW(lockPath.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!g_historyOwnership) {
        g_historyLoaded = false;  // Retry after an overlapping Explorer session exits.
        g_historyWritable = false;
        SetHistoryStatus(L"History storage is in use by another Explorer session, or is inaccessible.");
        return;
    }
    winrt::file_handle file{CreateFileW(g_historyPath.c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND) {
            g_historyWritable = false;
            SetHistoryStatus(L"Cannot read history. Check storage permissions, then reopen the mod.");
        }
        return;
    }
    HistoryFileHeader header;
    LARGE_INTEGER size{};
    DWORD read = 0;
    if (!GetFileSizeEx(file.get(), &size)) {
        g_historyWritable = false;
        SetHistoryStatus(L"Cannot read the history file size.");
        return;
    }
    if (size.QuadPart == 0) {
        g_historyDirty = true;  // Crash between first file creation and header write.
        return;
    }
    if (size.QuadPart < (LONGLONG)sizeof(header) ||
        !ReadFile(file.get(), &header, sizeof(header), &read, nullptr) ||
        read != sizeof(header) || header.magic != HistoryFileHeader{}.magic ||
        header.version != 1 || header.recordBytes != sizeof(HistorySample)) {
        g_historyWritable = false;
        SetHistoryStatus(L"Unsupported or damaged history file. Clear history to start again.");
        return;
    }
    size_t count = (size_t)((size.QuadPart - sizeof(header)) / sizeof(HistorySample));
    // A crash may leave only a partial final record. Retain complete checksummed samples.
    g_historyDirty = (size.QuadPart - sizeof(header)) % sizeof(HistorySample) != 0;
    std::array<HistorySample, 256> batch;
    while (count && !g_unloading) {
        size_t n = std::min(count, batch.size());
        DWORD bytes = (DWORD)(n * sizeof(HistorySample));
        if (!ReadFile(file.get(), batch.data(), bytes, &read, nullptr) || read != bytes) {
            g_historyWritable = false;
            SetHistoryStatus(L"History read failed. Loaded samples remain available for export.");
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            const auto& sample = batch[i];
            bool valid = sample.checksum == HistoryChecksum(sample) && sample.accountId &&
                         sample.timeMs && sample.timeMs < kHistoryTimestampLimitMs &&
                         sample.intervalMs > 0 && sample.intervalMs <= kHistoryDayMs &&
                         sample.provider >= 1 && sample.provider <= 3 && sample.failed <= 1;
            for (const auto& meter : sample.meters) {
                valid = valid && std::isfinite(meter.value) && meter.value >= -1 &&
                        meter.value <= 1e12 && meter.resetMs < kHistoryTimestampLimitMs &&
                        meter.durationMs <= kHistoryMaxWindowMs;
            }
            if (!valid) {
                // Stop at corruption rather than connecting graphs across unknown records.
                // A lone torn last record is recoverable; interior corruption needs consent.
                if (count == n && i + 1 == n) {
                    g_historyDirty = true;
                    return;
                }
                g_historyWritable = false;
                SetHistoryStatus(L"Damaged history record. Export readable samples or clear history.");
                return;
            }
            try {
                g_historySamples.push_back(sample);
            } catch (...) {
                // Out of memory: a partial journal must never be saved over the whole one.
                g_historyWritable = false;
                g_historySamples.clear();
                SetHistoryStatus(L"History is too large to load. Clear history to start again.");
                return;
            }
        }
        count -= n;
    }
    if (count) g_historyWritable = false;  // Unload stopped it part-way: never save that part.
}

// Atomically replaces the journal with `samples`, except those of `skippedAccount` (0: none).
static bool RewriteHistoryLocked(const std::deque<HistorySample>& samples, uint64_t skippedAccount = 0) {
    if (g_historyPath.empty() || !g_historyOwnership) return false;
    std::wstring temporary = g_historyPath + L".tmp";
    winrt::file_handle file{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!file) {
        SetHistoryStatus(L"Cannot write history. Check disk space and storage permissions.");
        g_historyWriteRetryMs = NowUnixMs() + 3600000;
        return false;
    }
    HistoryFileHeader header;
    DWORD written = 0;
    bool ok = WriteFile(file.get(), &header, sizeof(header), &written, nullptr) &&
              written == sizeof(header);
    std::array<HistorySample, 256> batch;
    auto it = samples.begin();
    while (ok && it != samples.end()) {
        size_t n = 0;
        for (; n < batch.size() && it != samples.end(); ++it) {
            if (!skippedAccount || it->accountId != skippedAccount) batch[n++] = *it;
        }
        DWORD bytes = (DWORD)(n * sizeof(HistorySample));
        ok = WriteFile(file.get(), batch.data(), bytes, &written, nullptr) && written == bytes;
    }
    ok = ok && FlushFileBuffers(file.get());
    file.close();
    if (ok) {
        ok = MoveFileExW(temporary.c_str(), g_historyPath.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (!ok) {
        DeleteFileW(temporary.c_str());
        SetHistoryStatus(L"History write failed; the previous journal was kept.");
        g_historyWriteRetryMs = NowUnixMs() + 3600000;
        return false;
    }
    g_historyDirty = false;
    g_historyWriteRetryMs = 0;
    SetHistoryStatus({});
    return true;
}

// At most hourly: prunes samples older than the published retention and, for a number of days,
// beyond the size cap; then saves an unsaved journal.
static ULONGLONG MaintainHistory(bool create = false) {
    try {
        std::lock_guard lock(g_historyMutex);
        LoadHistoryLocked(create);
        ULONGLONG now = NowUnixMs();
        // Retention edits use the regular hourly cadence too: autosave must not prune
        // immediately while someone is still adjusting a numeric field.
        if (g_historyMaintenanceMs && g_historyMaintenanceMs <= now &&
            now - g_historyMaintenanceMs < kHistoryMaintenanceIntervalMs) {
            return g_historyMaintenanceMs + kHistoryMaintenanceIntervalMs;
        }
        g_historyMaintenanceMs = now;
        int retentionDays = g_historyRetentionDays.load();
        ULONGLONG cutoff = HistoryRetentionStart(retentionDays, now);
        auto previousSize = g_historySamples.size();
        std::erase_if(g_historySamples, [cutoff](const auto& sample) {
            return sample.timeMs < cutoff;
        });
        if (retentionDays > 0 && g_historySamples.size() > kHistoryMaxSamples) {
            g_historySamples.erase(g_historySamples.begin(), g_historySamples.end() - kHistoryMaxSamples);
            g_historyCapped = true;
        }
        if (g_historySamples.size() != previousSize) {
            g_historyDirty = true;
            ++g_historyRevision;
        }
        if (g_historyDirty && g_historyWritable && !g_unloading && now >= g_historyWriteRetryMs) {
            RewriteHistoryLocked(g_historySamples);
        }
        return now + kHistoryMaintenanceIntervalMs;
    } catch (...) {
        Wh_Log(L"History maintenance failed");
        return NowUnixMs() + kHistoryMaintenanceIntervalMs;
    }
}

static void AppendHistory(const std::vector<HistorySample>& samples, uint64_t epoch, ULONGLONG generation) {
    if (samples.empty() || g_unloading) return;
    try {
        std::lock_guard lock(g_historyMutex);
        // This is the admission point. A later settings change doesn't retract samples
        // already admitted; Clear holds the same lock and invalidates pre-clear fetches.
        if (epoch != g_historyEpoch.load() || generation != g_historySettingsGeneration.load()) return;
        LoadHistoryLocked(true);
        if (!g_historyWritable || g_historyPath.empty()) return;
        ULONGLONG now = NowUnixMs();
        int retentionDays = g_historyRetentionDays.load();
        ULONGLONG cutoff = HistoryRetentionStart(retentionDays, now);
        std::vector<HistorySample> admitted;
        for (auto sample : samples) {
            if (sample.timeMs < cutoff ||
                (!g_historySamples.empty() && sample.timeMs < g_historySamples.back().timeMs)) {
                continue;  // Never turn a backwards system-clock adjustment into negative time.
            }
            if (sample.failed) {
                auto previous = std::find_if(g_historySamples.rbegin(), g_historySamples.rend(),
                    [&](const HistorySample& entry) { return entry.accountId == sample.accountId; });
                // One marker breaks continuity for an entire outage; failed retries add
                // no measurements and must not fill the journal with an unchanged error.
                if (previous != g_historySamples.rend() && previous->failed &&
                    previous->session == sample.session) continue;
            }
            for (auto& meter : sample.meters) {
                if (!std::isfinite(meter.value) || meter.value < 0 || meter.value > 1e12) {
                    meter.value = -1;
                }
                // Persist only metadata the reader accepts; bad provider fields mean
                // unknown timing, not a checksummed record that poisons the journal.
                if (meter.resetMs >= kHistoryTimestampLimitMs) meter.resetMs = 0;
                if (meter.durationMs > kHistoryMaxWindowMs) meter.durationMs = 0;
            }
            sample.checksum = HistoryChecksum(sample);
            g_historySamples.push_back(sample);
            ++g_historyAppendCount;
            admitted.push_back(sample);
        }
        if (admitted.empty()) return;
        if (retentionDays > 0 && g_historySamples.size() > kHistoryMaxSamples) {
            // Amortize compaction instead of rewriting a full journal every new sample.
            while (g_historySamples.size() > kHistoryMaxSamples * 9 / 10) {
                g_historySamples.pop_front();
            }
            ++g_historyRevision;
            g_historyDirty = g_historyCapped = true;
        }
        if (g_historyDirty) {
            if (now >= g_historyWriteRetryMs) RewriteHistoryLocked(g_historySamples);
            return;
        }
        winrt::file_handle file{CreateFileW(g_historyPath.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
        LARGE_INTEGER size{};
        DWORD written = 0;
        bool ok = file && GetFileSizeEx(file.get(), &size);
        if (ok && size.QuadPart == 0) {
            HistoryFileHeader header;
            ok = WriteFile(file.get(), &header, sizeof(header), &written, nullptr) &&
                 written == sizeof(header);
        }
        DWORD bytes = (DWORD)(admitted.size() * sizeof(HistorySample));
        if (ok) ok = WriteFile(file.get(), admitted.data(), bytes, &written, nullptr) && written == bytes;
        if (!ok) {
            // Retain admitted samples in memory; next append repairs a possible torn tail.
            g_historyDirty = true;
            g_historyWriteRetryMs = now + 3600000;
            SetHistoryStatus(L"History is not being saved to disk. Check disk space and permissions.");
        } else {
            SetHistoryStatus({});
        }
    } catch (...) {
        Wh_Log(L"History append failed");
    }
}

// Loads the journal if it isn't (read-only), then its samples from `fromMs` on with the status,
// cap and stamp of that state.
static HistorySnapshot ReadHistorySnapshot(ULONGLONG fromMs) {
    std::lock_guard lock(g_historyMutex);
    LoadHistoryLocked();
    HistorySnapshot snapshot;
    snapshot.status = g_historyStatus;
    snapshot.capped = g_historyCapped;
    snapshot.stamp = {g_historyLoaded, g_historyEpoch.load(), g_historyRevision, g_historyAppendCount};
    for (const auto& sample : g_historySamples) {
        if (sample.timeMs >= fromMs) snapshot.samples.push_back(sample);
    }
    return snapshot;
}

// The history window's refresh: memory only, never loading, creating or retrying storage. In one
// hold: the stamp, status and cap, and, when `samples` asks for them, the journal is loaded and it
// differs from a snapshot held with `held` from `heldFrom` (another stamp, or `fromMs` further
// back), a replacement: every sample from `fromMs` on, possibly none.
static HistoryPeek PeekHistorySnapshot(ULONGLONG fromMs, bool samples, const HistoryJournalStamp& held,
                                       ULONGLONG heldFrom) {
    std::lock_guard lock(g_historyMutex);
    HistoryPeek peek;
    peek.stamp = {g_historyLoaded, g_historyEpoch.load(), g_historyRevision, g_historyAppendCount};
    peek.status = g_historyStatus;
    peek.capped = g_historyCapped;
    if (samples && g_historyLoaded && (peek.stamp != held || fromMs < heldFrom)) {
        std::vector<HistorySample>& replacement = peek.samples.emplace();
        for (const auto& sample : g_historySamples) {
            if (sample.timeMs >= fromMs) replacement.push_back(sample);
        }
    }
    return peek;
}

static bool ClearHistory(std::wstring* error) {
    std::lock_guard lock(g_historyMutex);
    LoadHistoryLocked();
    ++g_historyEpoch;
    if (!g_historyLoaded && g_historyWritable && !g_historyOwnership && !g_historyPath.empty()) {
        g_historySamples.clear();
        return true;  // Nothing has ever been recorded; Clear is an idempotent no-op.
    }
    if (!RewriteHistoryLocked({})) {
        if (error) *error = g_historyStatus.empty() ? L"History storage is unavailable." : g_historyStatus;
        return false;
    }
    g_historySamples.clear();
    g_historyWritable = true;
    g_historyCapped = false;
    return true;
}

// Whether the journal may hold samples of a history ID, from memory only (never loading,
// creating or retrying storage): unless it is loaded, readable whole and saved, it may.
static bool HistoryMayHaveSamples(uint64_t historyId) {
    std::lock_guard lock(g_historyMutex);
    return !g_historyLoaded || !g_historyWritable || g_historyDirty ||
           std::any_of(g_historySamples.begin(), g_historySamples.end(),
                       [&](const HistorySample& sample) { return sample.accountId == historyId; });
}

// The memory half of deleting an account's history, once the journal was rewritten without its
// samples: they go, and the revision tells incremental readers. Caller holds g_historyMutex.
static void EraseHistorySamplesLocked(uint64_t historyId) {
    if (std::erase_if(g_historySamples, [&](const HistorySample& sample) { return sample.accountId == historyId; })) {
        ++g_historyRevision;
    }
}

// Deletes a removed account's samples: the journal rewritten without them, then memory, then the
// windows told. Remove the account first: its settings publication fences out batches still
// carrying its samples and drops its burn-up curves. Nothing ever recorded is a success; a
// journal that couldn't be read (damaged, unreadable, in use by another session) stays as it is,
// with `error` saying why. An unsaved one (a torn tail, or past the size cap, whose eviction this
// then saves) is rewritten even without the account's samples. Holds only the history lock.
static bool DeleteAccountHistory(uint64_t historyId, std::wstring* error) {
    {
        std::lock_guard lock(g_historyMutex);
        LoadHistoryLocked();
        if (!g_historyLoaded && g_historyWritable && !g_historyOwnership && !g_historyPath.empty()) {
            return true;
        }
        bool present = std::any_of(g_historySamples.begin(), g_historySamples.end(),
                                   [&](const HistorySample& sample) { return sample.accountId == historyId; });
        // An unload can stop a load part-way: never rewrite what it left.
        bool kept = !g_historyLoaded || !g_historyWritable || g_unloading;
        if (!kept && !present && !g_historyDirty) return true;
        if (kept || !RewriteHistoryLocked(g_historySamples, historyId)) {
            if (error) *error = g_historyStatus.empty() ? L"History storage is unavailable." : g_historyStatus;
            return false;
        }
        EraseHistorySamplesLocked(historyId);
        if (!present) return true;
    }
    NotifySettingsWindowChanged();
    return true;
}

static constexpr PCWSTR kHistoryConsentRegistryKey =
    L"Software\\Windhawk\\TaskbarAIQuota\\" WH_MOD_ID;

// Consent is per Windows user; Windhawk's settings_v1 storage is machine-shared. Recording is on
// until this user turns it off, which stores 0.
static bool ReadHistoryConsent() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, kHistoryConsentRegistryKey, L"RecordHistory",
                                  RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) return true;
    return result == ERROR_SUCCESS && size == sizeof(value) && value == 1;
}

static bool WriteHistoryConsent(bool enabled) {
    HKEY key = nullptr;
    LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, kHistoryConsentRegistryKey, 0, nullptr, 0,
                                     KEY_SET_VALUE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS) return false;
    DWORD value = enabled ? 1 : 0;
    result = RegSetValueExW(key, L"RecordHistory", 0, REG_DWORD,
                          reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
