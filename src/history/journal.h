// Local usage-history journal and its recording consent, in this Windows user's part of the mod's
// Windhawk storage (core/user-storage.h). Takes no UI or auth locks.
//
// The journal is the file's committed records (g_historyCommittedRecords complete records after a
// valid header) followed by the pending ones (admitted, not written yet). Explorer keeps in memory
// its summary and its cache: in journal order, every record from g_historyCacheFrom on and every
// pending one. The cache spans the hot window (the flyout's burn-up windows) and the open history
// window's pinned range; older records are read from the file, by the window's explicit loads, a
// burn-up tail reaching further back, and the rewrite transactions (maintenance, Clear, an
// account's deletion), which stream the file into a staging file that replaces it. Admission never
// lets time go backwards, so the journal is in time order.

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
// The cache's hot window: the longest burn-up window (32 days, history/burn-up.h), its reset
// tolerance and a margin.
static constexpr ULONGLONG kHistoryHotWindowMs = 35 * kHistoryDayMs;
// Records per file read or write.
static constexpr size_t kHistoryChunkRecords = 1024;

// What the whole journal holds, beside the cache: per account its record count and last record
// (an append's failed-marker check), the record count, how many are Antigravity's (thinning's),
// the oldest time (retention pruning) and the last record's time (appends never go back).
struct HistoryAccountSummary {
    uint64_t accountId = 0;
    uint64_t records = 0;
    HistorySample last;
};
struct HistoryJournalSummary {
    std::vector<HistoryAccountSummary> accounts;
    uint64_t records = 0, antigravity = 0;
    ULONGLONG oldestMs = ULLONG_MAX, lastMs = 0;

    const HistoryAccountSummary* Find(uint64_t accountId) const {
        auto it = std::find_if(accounts.begin(), accounts.end(),
                               [&](const HistoryAccountSummary& account) { return account.accountId == accountId; });
        return it == accounts.end() ? nullptr : &*it;
    }
    // The next record in journal order. Throws when out of memory (a new account).
    void Add(const HistorySample& sample) {
        auto it = std::find_if(accounts.begin(), accounts.end(), [&](const HistoryAccountSummary& account) {
            return account.accountId == sample.accountId;
        });
        if (it == accounts.end()) {
            it = accounts.emplace(accounts.end());
            it->accountId = sample.accountId;
        }
        ++it->records;
        it->last = sample;
        ++records;
        antigravity += sample.provider == 3;
        oldestMs = std::min(oldestMs, sample.timeMs);
        lastMs = sample.timeMs;
    }
};

// In-memory journal state, then its storage state.
static std::deque<HistorySample> g_historySamples;  // The cache
static std::vector<HistorySample> g_historyPending;
static HistoryJournalSummary g_historySummary;
// Where the cache starts: the earlier of the hot window's and the pinned range's start, as the last
// load, trim or widening set it (the clock alone never moves it back).
static ULONGLONG g_historyCacheFrom = 0;
// The open history window's range start (ULLONG_MAX: none): its explicit loads set it, a complete
// refresh moves it to its replacement's start, its thread's end releases it.
static ULONGLONG g_historyPinMs = ULLONG_MAX;
// Let incremental readers (burn-up curves, the history window's refresh) mirror
// g_historySamples: they copy only samples appended since their last copy while the revision is
// unchanged. The revision changes when the journal is loaded or rewritten (retention, the size cap,
// thinning, an account's deletion) and when records leave the cache; Clear changes
// g_historyEpoch. Records read into the cache's start (an explicit load's widening) change neither.
static uint64_t g_historyAppendCount = 0;
static uint64_t g_historyRevision = 0;
static HistoryJournalError g_historyStatus = HistoryJournalError::None;
static bool g_historyLoaded = false;
static bool g_historyCapped = false;
static std::wstring g_historyPath;
static bool g_historyWritable = true;
static uint64_t g_historyCommittedRecords = 0;
// Whether the file starts with a valid header; otherwise (none yet, or its write failed) the next
// write puts it first.
static bool g_historyHeaderValid = false;
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

static ULONGLONG HistoryHotStart(ULONGLONG now) {
    return now > kHistoryHotWindowMs ? now - kHistoryHotWindowMs : 0;
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

// File records ----------------------------------------------------------------------------------

static bool HistoryHeaderValid(const HistoryFileHeader& header) {
    return header.magic == HistoryFileHeader{}.magic && header.version == 1 &&
           header.recordBytes == sizeof(HistorySample);
}

// Any nonzero provider code: a later version's providers stay readable after a downgrade.
static bool HistoryRecordValid(const HistorySample& sample) {
    bool valid = sample.checksum == HistoryChecksum(sample) && sample.accountId && sample.timeMs &&
                 sample.timeMs < kHistoryTimestampLimitMs && sample.intervalMs > 0 &&
                 sample.intervalMs <= kHistoryDayMs && sample.provider && sample.failed <= 1;
    for (const auto& meter : sample.meters) {
        valid = valid && std::isfinite(meter.value) && meter.value >= -1 && meter.value <= 1e12 &&
                meter.resetMs < kHistoryTimestampLimitMs && meter.durationMs <= kHistoryMaxWindowMs;
    }
    return valid;
}

// How a pass over the journal's records ended: every record read; unload stopped it; a read (or
// the consumer) failed; a record or the header doesn't validate; the file is gone or shorter than
// its committed records.
enum class HistoryPass : uint8_t { Done, Cancelled, Failed, Damaged, Lost };

// Reads `count` records through `file` from the first, in chunks of kHistoryChunkRecords, each
// validated: `use(record)` for each (false fails the pass). `read` counts the records passed on;
// unload during any read cancels the pass. Throws only what `use` throws, or when out of memory.
static HistoryPass ReadHistoryRecordsLocked(HANDLE file, uint64_t count, uint64_t& read, auto&& use) {
    read = 0;
    if (!count) return HistoryPass::Done;
    LARGE_INTEGER start{};
    start.QuadPart = sizeof(HistoryFileHeader);
    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) return HistoryPass::Failed;
    std::vector<HistorySample> chunk(size_t(std::min<uint64_t>(count, kHistoryChunkRecords)));
    while (read < count) {
        if (g_unloading) return HistoryPass::Cancelled;
        size_t n = size_t(std::min<uint64_t>(count - read, chunk.size()));
        DWORD bytes = DWORD(n * sizeof(HistorySample)), got = 0;
        if (!ReadFile(file, chunk.data(), bytes, &got, nullptr) || got != bytes) return HistoryPass::Failed;
        for (size_t i = 0; i < n; ++i, ++read) {
            if (!HistoryRecordValid(chunk[i])) return HistoryPass::Damaged;
            if (!use(chunk[i])) return HistoryPass::Failed;
        }
    }
    return g_unloading ? HistoryPass::Cancelled : HistoryPass::Done;
}

// Opens the journal to read its committed records, denying other writers and its deletion while
// held (FILE_SHARE_READ only): a plain file (OpenUserStorageFile) holding every committed record
// after a valid header. Without committed records there is nothing to open: an empty handle.
static HistoryPass OpenHistorySourceLocked(winrt::file_handle& file) {
    file.close();
    if (!g_historyCommittedRecords) return HistoryPass::Done;
    file = OpenUserStorageFile(g_historyPath, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING);
    if (!file) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ||
                       error == ERROR_REPARSE_POINT_ENCOUNTERED ?
                   HistoryPass::Lost :
                   HistoryPass::Failed;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file.get(), &size)) return HistoryPass::Failed;
    if (uint64_t(size.QuadPart) < sizeof(HistoryFileHeader) + g_historyCommittedRecords * sizeof(HistorySample)) {
        return HistoryPass::Lost;
    }
    HistoryFileHeader header;
    DWORD read = 0;
    if (!ReadFile(file.get(), &header, sizeof(header), &read, nullptr) || read != sizeof(header)) {
        return HistoryPass::Failed;
    }
    return HistoryHeaderValid(header) ? HistoryPass::Done : HistoryPass::Damaged;
}

// Every record of the journal in order: the committed ones through `source` (none when it is
// empty), then the pending ones. Unload at any point cancels the pass.
static HistoryPass EachHistoryRecordLocked(const winrt::file_handle& source, auto&& use) {
    uint64_t read = 0;
    HistoryPass end = source ? ReadHistoryRecordsLocked(source.get(), g_historyCommittedRecords, read, use) :
                               HistoryPass::Done;
    for (size_t i = 0; end == HistoryPass::Done && i < g_historyPending.size(); ++i) {
        if (g_unloading) end = HistoryPass::Cancelled;
        else if (!use(g_historyPending[i])) end = HistoryPass::Failed;
    }
    return end == HistoryPass::Done && g_unloading ? HistoryPass::Cancelled : end;
}

// A pass that couldn't read the journal. Damage, or a file lost or shortened, means it can't be
// saved to any more (until Clear); otherwise, given the operation's `failure` status, it is retried
// after an hour; unload reports nothing.
static void HistoryPassFailedLocked(HistoryPass end, HistoryJournalError failure) {
    if (end == HistoryPass::Damaged || end == HistoryPass::Lost) {
        Wh_Log(L"The history journal on disk is %s; not saving to it until Clear",
               end == HistoryPass::Damaged ? L"damaged" : L"missing or shortened");
        g_historyWritable = false;
        SetHistoryStatus(end == HistoryPass::Damaged ? HistoryJournalError::DamagedRecord :
                                                       HistoryJournalError::ReadFailed);
    } else if (end == HistoryPass::Failed && failure != HistoryJournalError::None) {
        SetHistoryStatus(failure);
        g_historyWriteRetryMs = NowUnixMs() + 3600000;
    }
}

// The committed records from `fromMs` on that the cache doesn't hold (before g_historyCacheFrom),
// in journal order, read from the file: `use(record)` for each. Nothing to read before a load.
static HistoryPass ReadHistoryRangeLocked(ULONGLONG fromMs, const std::function<void(const HistorySample&)>& use) {
    if (!g_historyLoaded || fromMs >= g_historyCacheFrom) return HistoryPass::Done;
    winrt::file_handle source;
    HistoryPass end = HistoryPass::Failed;
    try {
        end = OpenHistorySourceLocked(source);
        uint64_t read = 0;
        if (end == HistoryPass::Done && source) {
            end = ReadHistoryRecordsLocked(source.get(), g_historyCommittedRecords, read,
                                           [&](const HistorySample& sample) {
                                               if (sample.timeMs >= fromMs && sample.timeMs < g_historyCacheFrom) {
                                                   use(sample);
                                               }
                                               return true;
                                           });
        }
    } catch (...) {
        end = HistoryPass::Failed;
    }
    HistoryPassFailedLocked(end, HistoryJournalError::None);
    return end;
}

// Load and writes --------------------------------------------------------------------------------

// Callers hold only g_historyMutex. An invalid format is preserved until explicit Clear. Reads
// the whole journal whatever the retention (maintenance and appends apply it): every record
// validated into the summary, the cache's in memory.
static void LoadHistoryLocked(bool create = false) {
    if (g_historyLoaded) return;
    g_historyWritable = true;
    SetHistoryStatus({});
    if (!OwnHistoryStorageLocked(create)) return;
    g_historyLoaded = true;
    ++g_historyRevision;
    g_historyThinnedMs = 0;
    g_historySamples.clear();
    g_historyPending.clear();
    g_historySummary = {};
    g_historyCommittedRecords = 0;
    g_historyHeaderValid = false;
    g_historyCacheFrom = std::min(HistoryHotStart(NowUnixMs()), g_historyPinMs);
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
    if (size.QuadPart == 0) return;  // Crash between first file creation and header write.
    if (size.QuadPart < (LONGLONG)sizeof(header) ||
        !ReadFile(file.get(), &header, sizeof(header), &read, nullptr) ||
        read != sizeof(header) || !HistoryHeaderValid(header)) {
        g_historyWritable = false;
        SetHistoryStatus(HistoryJournalError::Unsupported);
        return;
    }
    g_historyHeaderValid = true;
    // A crash may leave a partial final record; the next write cuts it off.
    const uint64_t count = uint64_t(size.QuadPart - sizeof(header)) / sizeof(HistorySample);
    uint64_t loaded = 0;
    HistoryPass end = HistoryPass::Failed;
    try {
        end = ReadHistoryRecordsLocked(file.get(), count, loaded, [](const HistorySample& sample) {
            g_historySummary.Add(sample);
            if (sample.timeMs >= g_historyCacheFrom) g_historySamples.push_back(sample);
            ++g_historyCommittedRecords;
            return true;
        });
    } catch (...) {
        // Out of memory: a partial journal must never be saved over the whole one.
        g_historyWritable = false;
        g_historySamples.clear();
        g_historySummary = {};
        g_historyCommittedRecords = 0;
        SetHistoryStatus(HistoryJournalError::TooLarge);
        return;
    }
    if (end == HistoryPass::Done) return;
    // Stop at corruption rather than connecting graphs across unknown records. A lone torn last
    // record is recoverable (the next write cuts it off); interior corruption needs consent.
    if (end == HistoryPass::Damaged && loaded + 1 == count) return;
    g_historyWritable = false;  // Never save over the part an unload or a failed read stopped at.
    if (end == HistoryPass::Damaged) SetHistoryStatus(HistoryJournalError::DamagedRecord);
    else if (end == HistoryPass::Failed) SetHistoryStatus(HistoryJournalError::ReadFailed);
}

// Saves the pending records after the committed ones: the journal opened as a plain file (created
// with its header when it has none yet), its header checked and its length holding every committed
// record (otherwise nothing is written: never a hole), whatever follows them cut off (a torn record
// or an earlier attempt's part), then the records, flushed when `recovering` or cutting. Once all
// are written they are committed (their storage released), and those then older than the cache
// leave it. Otherwise they stay pending and the write is retried after an hour; unload stops it
// between chunks, reporting nothing.
static bool WriteHistoryPendingLocked(bool recovering) {
    if (g_historyPending.empty()) return true;
    LARGE_INTEGER end{};
    end.QuadPart = g_historyHeaderValid ?
        LONGLONG(sizeof(HistoryFileHeader) + g_historyCommittedRecords * sizeof(HistorySample)) : 0;
    winrt::file_handle file = OpenUserStorageFile(g_historyPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                                                  g_historyHeaderValid ? OPEN_EXISTING : OPEN_ALWAYS);
    LARGE_INTEGER size{};
    bool ok = file && GetFileSizeEx(file.get(), &size);
    if (ok && g_historyHeaderValid) {
        HistoryFileHeader header;
        DWORD read = 0;
        if (size.QuadPart < end.QuadPart || !ReadFile(file.get(), &header, sizeof(header), &read, nullptr) ||
            read != sizeof(header) || !HistoryHeaderValid(header)) {
            Wh_Log(L"The history journal on disk isn't the one loaded; not saving to it");
            SetHistoryStatus(HistoryJournalError::CannotWrite);
            g_historyWriteRetryMs = NowUnixMs() + 3600000;
            return false;
        }
    }
    if (ok && size.QuadPart != end.QuadPart) {
        recovering = true;
        ok = SetFilePointerEx(file.get(), end, nullptr, FILE_BEGIN) && SetEndOfFile(file.get());
    }
    ok = ok && SetFilePointerEx(file.get(), end, nullptr, FILE_BEGIN);
    DWORD written = 0;
    if (ok && !g_historyHeaderValid) {
        HistoryFileHeader header;
        ok = WriteFile(file.get(), &header, sizeof(header), &written, nullptr) && written == sizeof(header);
        g_historyHeaderValid = ok;
    }
    for (size_t i = 0; ok && i < g_historyPending.size(); i += kHistoryChunkRecords) {
        if (g_unloading) return false;
        DWORD bytes = DWORD(std::min(kHistoryChunkRecords, g_historyPending.size() - i) * sizeof(HistorySample));
        ok = WriteFile(file.get(), g_historyPending.data() + i, bytes, &written, nullptr) && written == bytes;
    }
    ok = ok && (!recovering || FlushFileBuffers(file.get()));
    if (!ok) {
        SetHistoryStatus(HistoryJournalError::NotSaved);
        g_historyWriteRetryMs = NowUnixMs() + 3600000;
        return false;
    }
    // The pending records are the cache's last ones.
    auto saved = g_historySamples.end() - ptrdiff_t(g_historyPending.size());
    g_historyCommittedRecords += g_historyPending.size();
    std::vector<HistorySample>().swap(g_historyPending);
    g_historyWriteRetryMs = 0;
    SetHistoryStatus({});
    auto kept = std::remove_if(saved, g_historySamples.end(),
                               [](const HistorySample& sample) { return sample.timeMs < g_historyCacheFrom; });
    if (kept != g_historySamples.end()) {
        g_historySamples.erase(kept, g_historySamples.end());
        ++g_historyRevision;
    }
    return true;
}

// A rewrite transaction's last step: the journal's records in order (the committed ones through
// `source`, none when it is empty, then the pending ones), each that `keep` keeps (as it may change
// it), are written to a new staging file, which replaces the journal (`source` closed first). Then
// the cache (from g_historyCacheFrom), summary and count built meanwhile take over, with every
// record committed (the pending records' storage released). False, the journal as it was:
// damage, any read, write, flush or move failure, running out of memory or unload up to the move;
// the status says why.
static bool ReplaceHistoryLocked(winrt::file_handle& source, auto&& keep) {
    if (g_historyPath.empty() || !g_historyOwnership) return false;
    std::wstring staging;
    winrt::file_handle out;
    HistoryPass end = HistoryPass::Failed;
    HistoryJournalError failure = HistoryJournalError::WriteFailed;
    try {
        out = CreateUserStagingFile(g_historyPath, staging);
        if (!out) failure = HistoryJournalError::CannotWrite;
        std::deque<HistorySample> cache;
        HistoryJournalSummary summary;
        uint64_t records = 0;
        std::vector<HistorySample> buffer;
        buffer.reserve(kHistoryChunkRecords);
        HistoryFileHeader header;
        DWORD written = 0;
        bool ok = out && WriteFile(out.get(), &header, sizeof(header), &written, nullptr) && written == sizeof(header);
        auto flush = [&] {
            DWORD bytes = DWORD(buffer.size() * sizeof(HistorySample));
            ok = ok && (buffer.empty() || (WriteFile(out.get(), buffer.data(), bytes, &written, nullptr) &&
                                           written == bytes));
            buffer.clear();
            return ok;
        };
        if (ok) {
            end = EachHistoryRecordLocked(source, [&](const HistorySample& record) {
                HistorySample sample = record;
                if (!keep(sample)) return true;
                summary.Add(sample);
                if (sample.timeMs >= g_historyCacheFrom) cache.push_back(sample);
                ++records;
                buffer.push_back(sample);
                return buffer.size() < kHistoryChunkRecords || flush();
            });
        }
        ok = ok && end == HistoryPass::Done && flush() && FlushFileBuffers(out.get());
        out.close();
        source.close();
        if (ok && g_unloading) {
            ok = false;
            end = HistoryPass::Cancelled;
        }
        if (ok &&
            MoveFileExW(staging.c_str(), g_historyPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            g_historySamples.swap(cache);
            g_historySummary = std::move(summary);
            g_historyCommittedRecords = records;
            std::vector<HistorySample>().swap(g_historyPending);
            g_historyHeaderValid = true;
            ++g_historyRevision;
            g_historyWriteRetryMs = 0;
            SetHistoryStatus({});
            return true;
        }
        if (end == HistoryPass::Done) end = HistoryPass::Failed;
    } catch (...) {
        end = HistoryPass::Failed;
    }
    out.close();
    source.close();
    if (!staging.empty()) DeleteFileW(staging.c_str());
    HistoryPassFailedLocked(end, failure);
    return false;
}

// Drops the committed records the cache no longer needs: those before both the hot window and the
// pinned range. Pending records stay; the clock alone never moves the cache's start back.
static void TrimHistoryCacheLocked(ULONGLONG now) {
    if (!g_historyLoaded) return;
    ULONGLONG from = std::max(g_historyCacheFrom, std::min(HistoryHotStart(now), g_historyPinMs));
    auto committed = g_historySamples.begin() + ptrdiff_t(g_historySamples.size() - g_historyPending.size());
    auto kept = std::remove_if(g_historySamples.begin(), committed,
                               [from](const HistorySample& sample) { return sample.timeMs < from; });
    g_historyCacheFrom = from;
    if (kept == committed) return;
    g_historySamples.erase(kept, committed);
    ++g_historyRevision;
}

// At most hourly: prunes samples older than the published retention and, for a number of days,
// beyond the size cap, thins older Antigravity readings (at the first maintenance after a load,
// then daily), saves pending records and trims the cache. Its rewrite is one transaction over one
// source, nothing changed unless it all succeeds: with the cap, a first pass counts what the
// retention keeps; with thinning due, a pass feeds what both keep to the thinning machine; then,
// when any of them drops or changes a record, the journal is rewritten with what all three keep.
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
        // A transaction that runs out of memory changes nothing; the trim still runs.
        try {
            if (g_historyLoaded && g_historyWritable && !g_unloading && now >= g_historyWriteRetryMs) {
                int retentionDays = g_historyRetentionDays.load();
                ULONGLONG cutoff = HistoryRetentionStart(retentionDays, now);
                bool pruning = g_historySummary.oldestMs < cutoff;
                bool capping = retentionDays > 0 && g_historySummary.records > kHistoryMaxSamples;
                bool thinning = !g_historyThinnedMs || now >= g_historyThinnedMs + kHistoryDayMs;
                if (thinning && !g_historySummary.antigravity) {
                    g_historyThinnedMs = now;
                    thinning = false;
                }
                if (pruning || capping || thinning) {
                    winrt::file_handle source;
                    HistoryPass end = OpenHistorySourceLocked(source);
                    // The oldest records the cap drops of those the retention keeps.
                    uint64_t evicted = 0;
                    if (end == HistoryPass::Done && capping) {
                        uint64_t retained = 0;
                        end = EachHistoryRecordLocked(source, [&](const HistorySample& sample) {
                            retained += sample.timeMs >= cutoff;
                            return true;
                        });
                        evicted = retained > kHistoryMaxSamples ? retained - kHistoryMaxSamples : 0;
                    }
                    auto kept = [cutoff, evicted, seen = uint64_t(0)](const HistorySample& sample) mutable {
                        return sample.timeMs >= cutoff && ++seen > evicted;
                    };
                    std::optional<HistoryThinning> thinner;
                    if (end == HistoryPass::Done && thinning) {
                        thinner.emplace(now);
                        auto keeps = kept;
                        end = EachHistoryRecordLocked(source, [&](const HistorySample& sample) {
                            if (keeps(sample) && thinner->Eligible(sample)) thinner->Feed(sample);
                            return true;
                        });
                    }
                    bool thins = thinner && thinner->changed;
                    if (end != HistoryPass::Done) {
                        HistoryPassFailedLocked(end, HistoryJournalError::WriteFailed);
                    } else if (pruning || evicted || thins) {
                        size_t ordinal = 0;
                        if (ReplaceHistoryLocked(source, [&, kept](HistorySample& sample) mutable {
                                if (!kept(sample)) return false;
                                if (!thins || !thinner->Eligible(sample)) return true;
                                HistoryThinFate fate = thinner->fates[ordinal++];
                                if (fate == HistoryThinFate::Widened) {
                                    sample.intervalMs = kHistoryThinSpacingMs;
                                    sample.checksum = HistoryChecksum(sample);
                                }
                                return fate != HistoryThinFate::Dropped;
                            })) {
                            g_historyCapped = g_historyCapped || evicted;
                            if (thinning) g_historyThinnedMs = now;
                        }
                    } else {
                        if (thinning) g_historyThinnedMs = now;
                        source.close();
                        WriteHistoryPendingLocked(true);
                    }
                } else {
                    WriteHistoryPendingLocked(true);
                }
            }
        } catch (...) {
            Wh_Log(L"History maintenance failed");
        }
        TrimHistoryCacheLocked(now);
        return now + kHistoryMaintenanceIntervalMs;
    } catch (...) {
        Wh_Log(L"History maintenance failed");
        return NowUnixMs() + kHistoryMaintenanceIntervalMs;
    }
}

// Admits a poll round's samples by the journal's summary (never before the last record's time, one
// failed marker per outage), all or none: they join the cache and the pending records, then are
// written. Past the size cap (a number of days kept) the oldest records go in a rewrite that also
// saves them; while an earlier failure waits for its retry they stay pending, in memory.
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
        HistoryJournalSummary summary = g_historySummary;
        for (auto sample : samples) {
            if (sample.timeMs < cutoff || (summary.records && sample.timeMs < summary.lastMs)) {
                continue;  // Never turn a backwards system-clock adjustment into negative time.
            }
            if (sample.failed) {
                // One marker breaks continuity for an entire outage; failed retries add
                // no measurements and must not fill the journal with an unchanged error.
                const HistoryAccountSummary* previous = summary.Find(sample.accountId);
                if (previous && previous->last.failed && previous->last.session == sample.session) continue;
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
            summary.Add(sample);
            admitted.push_back(sample);
        }
        if (admitted.empty()) return;
        // Every allocation first: out of memory admits nothing.
        const bool retrying = !g_historyPending.empty();
        if (size_t needed = g_historyPending.size() + admitted.size(); needed > g_historyPending.capacity()) {
            g_historyPending.reserve(std::max(needed, 2 * g_historyPending.capacity()));
        }
        size_t cached = 0;
        try {
            for (const auto& sample : admitted) {
                g_historySamples.push_back(sample);
                ++cached;
            }
        } catch (...) {
            while (cached--) g_historySamples.pop_back();
            throw;
        }
        g_historyPending.insert(g_historyPending.end(), admitted.begin(), admitted.end());
        g_historySummary = std::move(summary);
        g_historyAppendCount += admitted.size();
        if (retentionDays > 0 && g_historySummary.records > kHistoryMaxSamples && now >= g_historyWriteRetryMs) {
            // Amortize compaction instead of rewriting a full journal every new sample.
            winrt::file_handle source;
            HistoryPass end = OpenHistorySourceLocked(source);
            uint64_t evicted = g_historySummary.records - kHistoryMaxSamples * 9 / 10;
            if (end != HistoryPass::Done) {
                HistoryPassFailedLocked(end, HistoryJournalError::NotSaved);
            } else if (ReplaceHistoryLocked(source, [evicted, seen = uint64_t(0)](HistorySample&) mutable {
                           return ++seen > evicted;
                       })) {
                g_historyCapped = true;
            }
            return;
        }
        if (!retrying || now >= g_historyWriteRetryMs) WriteHistoryPendingLocked(retrying);
    } catch (...) {
        Wh_Log(L"History append failed");
    }
}

// History window -------------------------------------------------------------------------------

// The history window's explicit load: loads the journal if it isn't (read-only), pins its range at
// `fromMs` (the cache keeps it while the window is open) and widens the cache back to it from the
// file, or trims what a narrower range no longer needs, then its samples from `fromMs` on with the
// status, cap and stamp of that state. Records that couldn't be read leave the snapshot starting
// later (`from`) with a read failure's status.
static HistorySnapshot ReadHistorySnapshot(ULONGLONG fromMs) {
    std::lock_guard lock(g_historyMutex);
    g_historyPinMs = fromMs;
    LoadHistoryLocked();
    HistorySnapshot snapshot;
    snapshot.from = fromMs;
    bool unread = false;
    if (g_historyLoaded && fromMs < g_historyCacheFrom) {
        std::vector<HistorySample> older;
        HistoryPass end = ReadHistoryRangeLocked(fromMs, [&](const HistorySample& sample) { older.push_back(sample); });
        if (end == HistoryPass::Done) {
            try {
                g_historySamples.insert(g_historySamples.begin(), older.begin(), older.end());
                g_historyCacheFrom = fromMs;
            } catch (...) {
                end = HistoryPass::Failed;
            }
        }
        if (end != HistoryPass::Done) {
            snapshot.from = g_historyCacheFrom;
            unread = true;
        }
    }
    TrimHistoryCacheLocked(NowUnixMs());
    snapshot.status = unread && g_historyStatus == HistoryJournalError::None ? HistoryJournalError::ReadFailed :
                                                                               g_historyStatus;
    snapshot.capped = g_historyCapped;
    snapshot.stamp = {g_historyLoaded, g_historyEpoch.load(), g_historyRevision, g_historyAppendCount};
    for (const auto& sample : g_historySamples) {
        if (sample.timeMs >= snapshot.from) snapshot.samples.push_back(sample);
    }
    return snapshot;
}

// The history window's refresh: memory only, never loading, creating, retrying or reading
// storage. In one hold: the stamp, status and cap, and, when `samples` asks for them and the
// journal is loaded:
// - from `fromMs` within the cache, when it differs from a snapshot held with `held` from
//   `heldFrom` (another stamp, or `fromMs` further back): a replacement, every sample from `fromMs`
//   on (possibly none), and the pin moves there, trimming what memory no longer needs (the stamp
//   returned is the one after it, which the replacement matches);
// - from earlier: incomplete. The held range (from `heldFrom`, or the cache's start if later) is
//   replaced when the stamp changed or memory no longer reaches back to it; the pin stays.
static HistoryPeek PeekHistorySnapshot(ULONGLONG fromMs, bool samples, const HistoryJournalStamp& held,
                                       ULONGLONG heldFrom) {
    std::lock_guard lock(g_historyMutex);
    auto stamp = [] {
        return HistoryJournalStamp{g_historyLoaded, g_historyEpoch.load(), g_historyRevision, g_historyAppendCount};
    };
    HistoryPeek peek;
    peek.stamp = stamp();
    peek.status = g_historyStatus;
    peek.capped = g_historyCapped;
    if (!samples || !g_historyLoaded) return peek;
    peek.incomplete = fromMs < g_historyCacheFrom;
    peek.from = peek.incomplete ? std::max(heldFrom, g_historyCacheFrom) : fromMs;
    if (peek.incomplete ? peek.stamp != held || peek.from != heldFrom : peek.stamp != held || fromMs < heldFrom) {
        std::vector<HistorySample>& replacement = peek.samples.emplace();
        for (const auto& sample : g_historySamples) {
            if (sample.timeMs >= peek.from) replacement.push_back(sample);
        }
        if (!peek.incomplete) {
            g_historyPinMs = fromMs;
            TrimHistoryCacheLocked(NowUnixMs());
            peek.stamp = stamp();
        }
    }
    return peek;
}

// The history window's thread ends (every exit): its range no longer holds records in memory.
static void ReleaseHistoryPin() {
    try {
        std::lock_guard lock(g_historyMutex);
        g_historyPinMs = ULLONG_MAX;
        TrimHistoryCacheLocked(NowUnixMs());
    } catch (...) {
        Wh_Log(L"Could not release the history window's range");
    }
}

// Clear and deletion ----------------------------------------------------------------------------

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
    winrt::file_handle nothing;  // Reads no committed record: none is kept.
    if ((!g_historyLoaded && !g_historyWritable) ||
        !ReplaceHistoryLocked(nothing, [](HistorySample&) { return false; })) {
        if (error) {
            *error = g_historyStatus == HistoryJournalError::None ? HistoryJournalError::Unavailable : g_historyStatus;
        }
        return false;
    }
    g_historyWritable = true;
    g_historyCapped = false;
    return true;
}

// Whether the journal may hold samples of a history ID, from memory only (never loading,
// creating or retrying storage): its summary, and unless it is loaded, readable whole and saved,
// it may.
static bool HistoryMayHaveSamples(uint64_t historyId) {
    std::lock_guard lock(g_historyMutex);
    const HistoryAccountSummary* account = g_historySummary.Find(historyId);
    return !g_historyLoaded || !g_historyWritable || !g_historyPending.empty() || (account && account->records);
}

// Deletes a removed account's samples: the journal rewritten without them (the cache and summary
// with it, the revision telling incremental readers), then the windows told. Remove the account
// first: its settings publication fences out batches still carrying its samples and drops its
// burn-up curves. Nothing ever recorded is a success; a journal that couldn't be read (damaged,
// unreadable, in use by another session) stays as it is, with `error` saying why. One with pending
// records is rewritten, saving them, even without the account's samples. Holds only the history
// lock.
static bool DeleteAccountHistory(uint64_t historyId, HistoryJournalError* error) {
    {
        std::lock_guard lock(g_historyMutex);
        LoadHistoryLocked();
        if (!g_historyLoaded && g_historyWritable && !g_historyOwnership && !g_historyPath.empty()) {
            return true;
        }
        const HistoryAccountSummary* account = g_historySummary.Find(historyId);
        bool present = account && account->records;
        // An unload can stop a load part-way: never rewrite what it left. An old copy of the moved
        // journal that can't be deleted yet keeps it too.
        if (!SettleLegacyHistoryLocked()) {
            if (error) *error = HistoryJournalError::InUse;
            return false;
        }
        bool kept = !g_historyLoaded || !g_historyWritable || g_unloading;
        if (!kept && !present && g_historyPending.empty()) return true;
        winrt::file_handle source;
        HistoryPass opened = kept ? HistoryPass::Cancelled : OpenHistorySourceLocked(source);
        HistoryPassFailedLocked(opened, HistoryJournalError::WriteFailed);
        if (opened != HistoryPass::Done ||
            !ReplaceHistoryLocked(source,
                                  [historyId](HistorySample& sample) { return sample.accountId != historyId; })) {
            if (error) {
                *error = g_historyStatus == HistoryJournalError::None ? HistoryJournalError::Unavailable : g_historyStatus;
            }
            return false;
        }
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
