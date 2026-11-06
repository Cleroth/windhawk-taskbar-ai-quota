// Local usage-history journal and its recording consent, in this Windows user's part of the mod's
// Windhawk storage (core/user-storage.h). Takes no UI or auth locks.

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
// retention or thinning or deleted with their account; Clear changes g_historyEpoch.
static uint64_t g_historyAppendCount = 0;
static uint64_t g_historyRevision = 0;
static HistoryJournalError g_historyStatus = HistoryJournalError::None;
static bool g_historyLoaded = false;
static bool g_historyCapped = false;
static std::wstring g_historyPath;
static bool g_historyWritable = true;
static bool g_historyDirty = false;
static ULONGLONG g_historyMaintenanceMs = 0;
static ULONGLONG g_historyWriteRetryMs = 0;
// When Antigravity thinning (history/thinning.h) last ran since the load; 0: not yet.
static ULONGLONG g_historyThinnedMs = 0;
// Whether this process settled what older versions left in LocalAppData (moved, cleaned up or
// kept), so later loads skip it.
static bool g_historyLegacySettled = false;
// The journal folder's history.lock, held exclusively from the first load (or the fetch thread's
// start, PrepareHistoryStorage) until unload.
static winrt::file_handle g_historyOwnership;

// Each change of the status notifies the settings and history windows (a posted message).
static void SetHistoryStatus(HistoryJournalError status) {
    bool changed = status != g_historyStatus;
    g_historyStatus = status;
    g_historyStorageError = g_historyStatus != HistoryJournalError::None;
    if (changed) NotifySettingsWindowChanged();
}

static uint64_t HistoryChecksum(const HistorySample& sample) {
    uint64_t hash = 1469598103934665603ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&sample);
    for (size_t i = 0; i < offsetof(HistorySample, checksum); ++i) {
        hash = (hash ^ bytes[i]) * 1099511628211ULL;
    }
    return hash;
}

// What older versions left in `legacy` (LocalAppData\TaskbarAIQuota\<mod ID>: the journal, its
// lock, the notifications' icon), moved into this user's folder or cleaned up. Caller holds
// g_historyMutex and the journal's ownership. Under the old lock, taken exclusively as older
// versions take it:
// - Without a journal here, the old one is copied to a staging file beside g_historyPath, flushed
//   and moved into place (never over a file), its provenance (size, last write time, contents'
//   hash) stored in Windhawk storage, then the old journal deleted.
// - With one here (a plain file, held open meanwhile), the old journal goes only when it matches
//   that provenance or this journal byte for byte. Any other (a downgrade's own journal) is kept
//   and never merged.
// Once the old journal is gone: the old lock, then the icon and the emptied folders, then the
// provenance. Only not-found counts as absent: an inspection error keeps the old files and never
// starts a fresh journal, nor does an old lock held by an older version. Returns what stops the
// load (None: load g_historyPath). A failure before the move leaves the old files as they were,
// and later loads retry it, as they do the cleanup after it. `create` (recording's loads) retries a
// failed copy only hourly.
static HistoryJournalError MoveLegacyHistoryLocked(const std::wstring& legacy, bool create) {
    using enum HistoryJournalError;
    auto absent = [](DWORD error) { return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND; };
    DWORD error = GetFileAttributesW(g_historyPath.c_str()) == INVALID_FILE_ATTRIBUTES ? GetLastError() : 0;
    if (error && !absent(error)) return None;  // The load reports it.
    const bool fresh = error != 0;  // No journal here yet.
    // Loaded samples are the journal: one moved in now wouldn't be in memory.
    if (fresh && g_historyLoaded) return None;
    if (legacy.empty()) return fresh ? Unavailable : None;  // Unknown, not absent: never settled.
    const std::wstring sid = CurrentUserSid();
    const std::wstring provenanceName = L"historyMigratedFrom_" + sid;
    struct Provenance {
        uint64_t size, written, hash;
    };
    auto clearProvenance = [&] {
        BYTE stored[64];
        if (!sid.empty() && Wh_GetBinaryValue(provenanceName.c_str(), stored, sizeof(stored))) {
            Wh_DeleteValue(provenanceName.c_str());
        }
    };
    if (GetFileAttributesW(legacy.c_str()) == INVALID_FILE_ATTRIBUTES) {
        error = GetLastError();
        if (!absent(error)) {
            Wh_Log(L"Could not inspect the earlier history folder: %lu", error);
            return fresh ? CannotRead : None;
        }
        clearProvenance();
        g_historyLegacySettled = true;
        return None;
    }
    const std::wstring journal = legacy + L"\\usage-history-v1.bin";
    winrt::file_handle lock{CreateFileW((legacy + L"\\history.lock").c_str(), GENERIC_READ | GENERIC_WRITE | DELETE, 0,
                                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!lock) {
        // An older version holds it (another session, or a downgrade still running), possibly
        // before it wrote its journal: no journal may start here meanwhile.
        Wh_Log(L"The earlier history folder is in use (%lu); retrying later", GetLastError());
        return fresh ? InUse : None;
    }
    // Reads `file` from its start in chunks, passing each to `use` (false stops); whether it read to
    // its end.
    std::vector<BYTE> buffer(1 << 20);
    auto eachChunk = [&](HANDLE file, auto&& use) {
        LARGE_INTEGER start{};
        if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return false;
        for (;;) {
            DWORD read = 0;
            if (g_unloading || !ReadFile(file, buffer.data(), DWORD(buffer.size()), &read, nullptr)) return false;
            if (!read) return true;
            if (!use(buffer.data(), read)) return false;
        }
    };
    auto fnv = [](uint64_t hash, const BYTE* bytes, DWORD count) {
        for (DWORD i = 0; i < count; ++i) hash = (hash ^ bytes[i]) * 1099511628211ULL;
        return hash;
    };
    winrt::file_handle source{CreateFileW(journal.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                          FILE_ATTRIBUTE_NORMAL, nullptr)};
    if (!source && !absent(error = GetLastError())) {
        Wh_Log(L"Could not open the earlier history journal: %lu", error);
        return fresh ? CannotRead : None;
    }
    if (source) {
        // The journal here, once checked to be a file of its own, held until the old one is gone.
        winrt::file_handle current;
        BY_HANDLE_FILE_INFORMATION info{};
        if (!GetFileInformationByHandle(source.get(), &info)) {
            Wh_Log(L"Could not inspect the earlier history journal: %lu", GetLastError());
            return fresh ? CannotRead : None;
        }
        Provenance found{(uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow,
                         (uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime,
                         1469598103934665603ULL};
        if (fresh) {
            if (create && NowUnixMs() < g_historyWriteRetryMs) return CannotWrite;
            std::wstring staging;
            winrt::file_handle copy = CreateUserStagingFile(g_historyPath, staging);
            uint64_t copied = 0;
            bool ok = copy && eachChunk(source.get(), [&](const BYTE* bytes, DWORD count) {
                found.hash = fnv(found.hash, bytes, count);
                copied += count;
                DWORD written = 0;
                return WriteFile(copy.get(), bytes, count, &written, nullptr) && written == count;
            }) && copied == found.size && FlushFileBuffers(copy.get());
            copy.close();
            ok = ok && MoveFileExW(staging.c_str(), g_historyPath.c_str(), MOVEFILE_WRITE_THROUGH);
            if (!ok) {
                error = GetLastError();
                if (!staging.empty()) DeleteFileW(staging.c_str());
                Wh_Log(L"Could not move the earlier history journal: %lu", error);
                g_historyWriteRetryMs = NowUnixMs() + 3600000;
                return CannotWrite;
            }
            g_historyWriteRetryMs = 0;
            Wh_Log(L"Moved the history journal into the mod's storage");
            if (sid.empty() || !Wh_SetBinaryValue(provenanceName.c_str(), &found, sizeof(found))) {
                Wh_Log(L"Could not store the moved journal's provenance");
            }
        } else {
            // A link here to the old journal would match it: only a plain file counts.
            current = OpenUserStorageFile(g_historyPath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
            if (!current) {
                Wh_Log(L"Could not inspect the history journal (%lu); kept the earlier one", GetLastError());
                return None;
            }
            // Read: every comparison ran to its answer, so a mismatch is a difference, not an error.
            Provenance stored{};
            bool read = true, matches = false;
            if (!sid.empty() && Wh_GetBinaryValue(provenanceName.c_str(), &stored, sizeof(stored)) == sizeof(stored) &&
                stored.size == found.size && stored.written == found.written) {
                read = eachChunk(source.get(), [&](const BYTE* bytes, DWORD count) {
                    found.hash = fnv(found.hash, bytes, count);
                    return true;
                });
                matches = read && found.hash == stored.hash;
            }
            LARGE_INTEGER size{};
            if (!matches && read) read = GetFileSizeEx(current.get(), &size);
            if (!matches && read && uint64_t(size.QuadPart) == found.size) {
                std::vector<BYTE> other(buffer.size());
                bool differs = false;
                matches = eachChunk(source.get(), [&](const BYTE* bytes, DWORD count) {
                    DWORD got = 0;
                    if (!ReadFile(current.get(), other.data(), count, &got, nullptr) || got != count) return false;
                    differs = memcmp(bytes, other.data(), count) != 0;
                    return !differs;
                });
                read = matches || differs;
            }
            if (!matches) {
                if (read) {
                    Wh_Log(L"Kept the earlier history journal %s: it differs from the current one, which doesn't "
                           L"merge it", journal.c_str());
                    g_historyLegacySettled = true;
                } else {
                    Wh_Log(L"Could not compare the earlier history journal; kept it");
                }
                return None;
            }
        }
        source.close();
        if (!DeleteFileW(journal.c_str())) {
            Wh_Log(L"Could not delete the earlier history journal (%lu); retrying later", GetLastError());
            return None;
        }
    }
    // The old journal is gone: its leftover rewrite, the old lock, the icon and the emptied
    // folders, then the provenance.
    DeleteFileW((journal + L".tmp").c_str());
    FILE_DISPOSITION_INFO dispose{TRUE};
    SetFileInformationByHandle(lock.get(), FileDispositionInfo, &dispose, sizeof(dispose));
    lock.close();
    for (PCWSTR name : {L"\\notification-icon.png", L"\\notification-icon.png.tmp"}) {
        DeleteFileW((legacy + name).c_str());
    }
    RemoveDirectoryW(legacy.c_str());
    RemoveDirectoryW(legacy.substr(0, legacy.rfind(L'\\')).c_str());
    clearProvenance();
    g_historyLegacySettled = true;
    return None;
}

// Takes this user's journal folder for this process: g_historyPath, its ownership (history.lock,
// held exclusively until unload, so separate Explorer processes, reloads or sessions never compact
// each other's journal; the OS releases it even after a shell crash), then what older versions
// left (MoveLegacyHistoryLocked). Without `create`, nothing is created while nothing was recorded,
// here or by an older version. Returns whether the journal can be loaded; otherwise the status
// says why (none: nothing recorded yet) and g_historyWritable is false. Caller holds
// g_historyMutex.
static bool OwnHistoryStorageLocked(bool create) {
    std::wstring legacy;
    if (!g_historyLegacySettled) {
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr, &localAppData))) {
            legacy = std::wstring(localAppData) + L"\\TaskbarAIQuota\\" WH_MOD_ID;
        }
        CoTaskMemFree(localAppData);
    }
    auto fail = [](HistoryJournalError status) {
        g_historyWritable = false;
        SetHistoryStatus(status);
        return false;
    };
    if (!g_historyOwnership) {
        const std::wstring folder = UserStorageFolder();
        if (folder.empty()) return fail(HistoryJournalError::Unavailable);
        g_historyPath = folder + L"\\usage-history-v1.bin";
        auto missing = [](const std::wstring& path) {
            if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
            DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
        };
        if (!create && missing(g_historyPath) && (g_historyLegacySettled || (!legacy.empty() && missing(legacy)))) {
            return false;
        }
        if (DWORD error = PrepareUserStorageFolder(folder)) {
            return fail(error == ERROR_REPARSE_POINT_ENCOUNTERED ? HistoryJournalError::Unavailable :
                                                                   HistoryJournalError::CannotCreateDirectory);
        }
        g_historyOwnership = OpenUserStorageFile(folder + L"\\history.lock", GENERIC_READ | GENERIC_WRITE, 0, OPEN_ALWAYS);
        if (!g_historyOwnership) {
            // In use: retried once the overlapping Explorer session exits.
            return fail(GetLastError() == ERROR_REPARSE_POINT_ENCOUNTERED ? HistoryJournalError::Unavailable :
                                                                            HistoryJournalError::InUse);
        }
        DeleteStaleStagingFiles(g_historyPath);
    }
    if (g_historyLegacySettled) return true;
    HistoryJournalError status = HistoryJournalError::None;
    try {
        status = MoveLegacyHistoryLocked(legacy, create);
    } catch (...) {
        Wh_Log(L"Could not check the earlier history folder");
        status = GetFileAttributesW(g_historyPath.c_str()) == INVALID_FILE_ATTRIBUTES ? HistoryJournalError::CannotRead :
                                                                                       HistoryJournalError::None;
    }
    return status == HistoryJournalError::None || fail(status);
}

// Before Clear or an account's deletion of a loaded journal: settles what older versions left
// (OwnHistoryStorageLocked, which never moves a journal in once loaded), so a moved journal's old
// copy can't come back later, as after the mod's removal and a reinstall. Whether it is settled.
static bool SettleLegacyHistoryLocked() {
    if (g_historyLoaded && !g_historyLegacySettled) OwnHistoryStorageLocked(false);
    return !g_historyLoaded || g_historyLegacySettled;
}

// Once from the taskbar's fetch thread at start, recording or not: takes the journal's storage as
// a load does (OwnHistoryStorageLocked: what older versions left moves in or is cleaned up),
// reading no samples. A read-only take: nothing recorded anywhere creates nothing.
static void PrepareHistoryStorage() {
    try {
        std::lock_guard lock(g_historyMutex);
        if (!g_historyLoaded && !g_unloading) OwnHistoryStorageLocked(false);
    } catch (...) {
        Wh_Log(L"Could not prepare history storage");
    }
}

// Callers hold only g_historyMutex. An invalid format is preserved until explicit Clear. Reads
// the whole journal whatever the retention: maintenance and appends apply it.
static void LoadHistoryLocked(bool create = false) {
    if (g_historyLoaded) return;
    g_historyWritable = true;
    SetHistoryStatus({});
    if (!OwnHistoryStorageLocked(create)) return;
    g_historyLoaded = true;
    ++g_historyRevision;
    g_historyThinnedMs = 0;
    // A link in the journal's place is refused, never read as missing or someone else's journal.
    winrt::file_handle file = OpenUserStorageFile(g_historyPath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    if (!file) {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND) {
            g_historyWritable = false;
            SetHistoryStatus(error == ERROR_REPARSE_POINT_ENCOUNTERED ? HistoryJournalError::Unavailable :
                                                                        HistoryJournalError::CannotRead);
        }
        return;
    }
    HistoryFileHeader header;
    LARGE_INTEGER size{};
    DWORD read = 0;
    if (!GetFileSizeEx(file.get(), &size)) {
        g_historyWritable = false;
        SetHistoryStatus(HistoryJournalError::CannotReadSize);
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
        SetHistoryStatus(HistoryJournalError::Unsupported);
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
            SetHistoryStatus(HistoryJournalError::ReadFailed);
            return;
        }
        for (size_t i = 0; i < n; ++i) {
            const auto& sample = batch[i];
            // Any nonzero provider code: a later version's providers stay readable after a
            // downgrade.
            bool valid = sample.checksum == HistoryChecksum(sample) && sample.accountId &&
                         sample.timeMs && sample.timeMs < kHistoryTimestampLimitMs &&
                         sample.intervalMs > 0 && sample.intervalMs <= kHistoryDayMs &&
                         sample.provider && sample.failed <= 1;
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
                SetHistoryStatus(HistoryJournalError::DamagedRecord);
                return;
            }
            try {
                g_historySamples.push_back(sample);
            } catch (...) {
                // Out of memory: a partial journal must never be saved over the whole one.
                g_historyWritable = false;
                g_historySamples.clear();
                SetHistoryStatus(HistoryJournalError::TooLarge);
                return;
            }
        }
        count -= n;
    }
    if (count) g_historyWritable = false;  // Unload stopped it part-way: never save that part.
}

// Atomically replaces the journal with `samples`, except those of `skippedAccount` (0: none):
// written to a new staging file, then moved over it.
static bool RewriteHistoryLocked(const std::deque<HistorySample>& samples, uint64_t skippedAccount = 0) {
    if (g_historyPath.empty() || !g_historyOwnership) return false;
    std::wstring temporary;
    winrt::file_handle file = CreateUserStagingFile(g_historyPath, temporary);
    if (!file) {
        SetHistoryStatus(HistoryJournalError::CannotWrite);
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
        SetHistoryStatus(HistoryJournalError::WriteFailed);
        g_historyWriteRetryMs = NowUnixMs() + 3600000;
        return false;
    }
    g_historyDirty = false;
    g_historyWriteRetryMs = 0;
    SetHistoryStatus({});
    return true;
}

// At most hourly: prunes samples older than the published retention and, for a number of days,
// beyond the size cap, thins older Antigravity readings (ThinHistorySamples: at the first
// maintenance after a load, then daily), then saves an unsaved journal.
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
        // Built aside and swapped in: running out of memory leaves the journal as it was. Only a
        // whole, saveable journal is thinned.
        if (g_historyLoaded && g_historyWritable && !g_unloading &&
            (!g_historyThinnedMs || now >= g_historyThinnedMs + kHistoryDayMs)) {
            g_historyThinnedMs = now;
            if (auto thinned = ThinHistorySamples(g_historySamples, now)) {
                g_historySamples.swap(*thinned);
                g_historyDirty = true;
                ++g_historyRevision;
            }
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
        winrt::file_handle file = OpenUserStorageFile(g_historyPath, FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_ALWAYS);
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
            SetHistoryStatus(HistoryJournalError::NotSaved);
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

static bool ClearHistory(HistoryJournalError* error) {
    std::lock_guard lock(g_historyMutex);
    LoadHistoryLocked();
    ++g_historyEpoch;
    if (!g_historyLoaded && g_historyWritable && !g_historyOwnership && !g_historyPath.empty()) {
        g_historySamples.clear();
        return true;  // Nothing has ever been recorded; Clear is an idempotent no-op.
    }
    // Storage that couldn't be taken (in use, unavailable, an earlier version's journal not moved
    // yet), or an old copy of the moved journal that can't be deleted yet, stays as it is.
    if (!SettleLegacyHistoryLocked()) {
        if (error) *error = HistoryJournalError::InUse;
        return false;
    }
    if ((!g_historyLoaded && !g_historyWritable) || !RewriteHistoryLocked({})) {
        if (error) {
            *error = g_historyStatus == HistoryJournalError::None ? HistoryJournalError::Unavailable : g_historyStatus;
        }
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
static bool DeleteAccountHistory(uint64_t historyId, HistoryJournalError* error) {
    {
        std::lock_guard lock(g_historyMutex);
        LoadHistoryLocked();
        if (!g_historyLoaded && g_historyWritable && !g_historyOwnership && !g_historyPath.empty()) {
            return true;
        }
        bool present = std::any_of(g_historySamples.begin(), g_historySamples.end(),
                                   [&](const HistorySample& sample) { return sample.accountId == historyId; });
        // An unload can stop a load part-way: never rewrite what it left. An old copy of the moved
        // journal that can't be deleted yet keeps it too.
        if (!SettleLegacyHistoryLocked()) {
            if (error) *error = HistoryJournalError::InUse;
            return false;
        }
        bool kept = !g_historyLoaded || !g_historyWritable || g_unloading;
        if (!kept && !present && !g_historyDirty) return true;
        if (kept || !RewriteHistoryLocked(g_historySamples, historyId)) {
            if (error) {
                *error = g_historyStatus == HistoryJournalError::None ? HistoryJournalError::Unavailable : g_historyStatus;
            }
            return false;
        }
        EraseHistorySamplesLocked(historyId);
        if (!present) return true;
    }
    NotifySettingsWindowChanged();
    return true;
}

// Consent is per Windows user, a value in Windhawk storage named after the user's SID
// (core/user-storage.h); settings_v1 is machine-shared. Recording is on until this user turns it
// off, which stores 0. Without the Windhawk value, the HKCU one older versions wrote, rereading the
// Windhawk value once after finding that absent: the taskbar's process may have just moved it.
static bool ReadHistoryConsent() {
    const std::wstring sid = CurrentUserSid();
    const std::wstring name = kConsentValuePrefix + sid;
    auto stored = [&] { return sid.empty() ? -1 : Wh_GetIntValue(name.c_str(), -1); };
    if (int value = stored(); value != -1) return value == 1;
    DWORD value = 0;
    DWORD size = sizeof(value);
    LSTATUS result = RegGetValueW(HKEY_CURRENT_USER, kLegacyUserRegistryKey, kLegacyConsentValue, RRF_RT_REG_DWORD,
                                  nullptr, &value, &size);
    if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
        int moved = stored();
        return moved == -1 || moved == 1;
    }
    return result == ERROR_SUCCESS && size == sizeof(value) && value == 1;
}

// Stores consent under UserPreferencesLock; fails, writing nothing, when the lock can't be had.
static bool WriteHistoryConsent(bool enabled) {
    const std::wstring sid = CurrentUserSid();
    if (sid.empty()) return false;
    UserPreferencesLock lock(sid);
    return lock.held && Wh_SetIntValue((kConsentValuePrefix + sid).c_str(), enabled ? 1 : 0);
}
