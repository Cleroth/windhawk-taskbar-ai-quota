// Synthetic journal files and independent expectations. Each case gets its own storage and
// LocalAppData folders under journal-files\ (removed once it passes, kept for a failure), a journal
// as the mod's unload leaves it, the clock at kTestStart, no file faults and no allocation limit.
static int passed = 0, failed = 0, assertions = 0;
static std::filesystem::path g_testRoot;
static constexpr ULONGLONG kTestStart = 1800000000000ULL;
static constexpr ULONGLONG kMinute = 60000, kHour = 3600000;

#define CHECK(condition) do { ++assertions; if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

// Every journal global as Wh_ModUninit leaves it, and the published retention back to forever.
static void ResetJournal() {
    std::lock_guard lock(g_historyMutex);
    g_historyOwnership.close();
    g_historySamples.clear();
    g_historyPending.clear();
    g_historySummary = {};
    g_historyCommittedRecords = 0;
    g_historyHeaderValid = false;
    g_historyCacheFrom = 0;
    g_historyPinMs = ULLONG_MAX;
    g_historyPath.clear();
    g_historyStatus = HistoryJournalError::None;
    g_historyStorageError = false;
    g_historyLoaded = false;
    g_historyWritable = true;
    g_historyCapped = false;
    g_historyMaintenanceMs = 0;
    g_historyWriteRetryMs = 0;
    g_historyThinnedMs = 0;
    g_historyLegacySettled = false;
    g_historyRetentionDays = 0;
}

static void ClearFaults() {
    for (auto& fault : g_testFaults) fault = {};
    for (auto& calls : g_testCalls) calls = 0;
    g_testPartialWrite = false;
    g_testOnRead = {};
    g_testOnWrite = {};
    g_testAllocations = -1;
}

// After `skip` more calls of `api` succeed, `count` fail.
static void Fault(TestFileApi api, long skip, long count = 1) { g_testFaults[api] = {skip, count}; }

static void Run(const std::string& name, const std::function<void()>& body) {
    static int index = 0;
    std::filesystem::path folder = g_testRoot / std::to_string(++index);
    std::filesystem::create_directories(folder / "storage");
    std::filesystem::create_directories(folder / "localappdata");
    g_testStorage = (folder / "storage").wstring();
    g_testLocalAppData = (folder / "localappdata").wstring();
    g_testValues.clear();
    g_testLogs = 0;
    ClearFaults();
    g_unloading = false;
    g_testNow = kTestStart;
    settingsNotifications = 0;
    ResetJournal();
    bool ok = false;
    try {
        body();
        ok = true;
        ++passed;
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception& e) {
        ++failed;
        std::cout << "FAIL " << name << ": " << e.what() << " (files: " << folder.string() << ")\n";
    }
    ClearFaults();
    g_unloading = false;
    ResetJournal();
    std::error_code error;
    if (ok) std::filesystem::remove_all(folder, error);
}

static std::filesystem::path UserFolder() { return std::filesystem::path(g_testStorage) / CurrentUserSid(); }
static std::filesystem::path JournalPath() { return UserFolder() / L"usage-history-v1.bin"; }

// A reading of `account` at `time` (its weekly meter at `value`), checksummed.
static HistorySample Reading(uint64_t account, ULONGLONG time, double value = 10, uint32_t provider = 1,
                             ULONGLONG interval = 10 * kMinute, uint64_t session = 1) {
    HistorySample sample{};
    sample.accountId = account;
    sample.provider = provider;
    sample.session = session;
    sample.timeMs = time;
    sample.intervalMs = interval;
    sample.meters[1] = {value, time + kHistoryWeekMs, kHistoryWeekMs};
    sample.checksum = HistoryChecksum(sample);
    return sample;
}

static bool Same(const std::vector<HistorySample>& a, const std::vector<HistorySample>& b) {
    return a.size() == b.size() && (a.empty() || !memcmp(a.data(), b.data(), a.size() * sizeof(HistorySample)));
}
template <class Samples>
static std::vector<HistorySample> Vector(const Samples& samples) {
    return {samples.begin(), samples.end()};
}

// The journal's file as the format defines it: whether it exists, its bytes, the header's
// validity, its complete records and the stray bytes after them.
struct JournalFile {
    bool exists = false, header = false;
    std::string bytes;
    std::vector<HistorySample> records;
    size_t tail = 0;
};
static JournalFile ReadJournalFile(const std::filesystem::path& path = JournalPath()) {
    JournalFile file;
    FILE* stream = _wfopen(path.c_str(), L"rb");
    if (!stream) return file;
    file.exists = true;
    char buffer[65536];
    for (size_t got; (got = fread(buffer, 1, sizeof(buffer), stream)) > 0;) file.bytes.append(buffer, got);
    fclose(stream);
    HistoryFileHeader header;
    if (file.bytes.size() < sizeof(header)) return file;
    memcpy(&header, file.bytes.data(), sizeof(header));
    file.header = header.magic == HistoryFileHeader{}.magic && header.version == 1 &&
                  header.recordBytes == sizeof(HistorySample);
    size_t count = (file.bytes.size() - sizeof(header)) / sizeof(HistorySample);
    file.records.resize(count);
    if (count) memcpy(file.records.data(), file.bytes.data() + sizeof(header), count * sizeof(HistorySample));
    file.tail = file.bytes.size() - sizeof(header) - count * sizeof(HistorySample);
    return file;
}
// Writes the header, `records`, then `tail` stray bytes.
static void WriteJournalFile(const std::vector<HistorySample>& records, size_t tail = 0,
                             const std::filesystem::path& path = JournalPath()) {
    std::filesystem::create_directories(path.parent_path());
    FILE* stream = _wfopen(path.c_str(), L"wb");
    if (!stream) throw std::runtime_error("cannot write a test journal");
    HistoryFileHeader header;
    fwrite(&header, sizeof(header), 1, stream);
    if (!records.empty()) fwrite(records.data(), sizeof(HistorySample), records.size(), stream);
    for (size_t i = 0; i < tail; ++i) fputc(0x5A, stream);
    fclose(stream);
}

// The journal's memory against its file: the file holds the committed records, valid, and maybe
// stray bytes after them; memory holds every one from the cache's start on, then the pending
// ones; the summary counts them all, per account with its last record.
static void CheckMemory() {
    JournalFile file = ReadJournalFile();
    CHECK(file.records.size() >= g_historyCommittedRecords && (!g_historyCommittedRecords || file.header));
    std::vector<HistorySample> logical(file.records.begin(), file.records.begin() + ptrdiff_t(g_historyCommittedRecords));
    for (const auto& record : logical) CHECK(HistoryRecordValid(record));
    std::vector<HistorySample> cached;
    for (const auto& record : logical) {
        if (record.timeMs >= g_historyCacheFrom) cached.push_back(record);
    }
    logical.insert(logical.end(), g_historyPending.begin(), g_historyPending.end());
    cached.insert(cached.end(), g_historyPending.begin(), g_historyPending.end());
    CHECK(Same(cached, Vector(g_historySamples)));
    CHECK(g_historySummary.records == logical.size());
    std::map<uint64_t, std::pair<uint64_t, HistorySample>> accounts;
    ULONGLONG oldest = ULLONG_MAX;
    for (const auto& record : logical) {
        auto& account = accounts[record.accountId];
        ++account.first;
        account.second = record;
        oldest = std::min(oldest, record.timeMs);
    }
    CHECK(g_historySummary.accounts.size() == accounts.size() && g_historySummary.oldestMs == oldest);
    CHECK(logical.empty() || g_historySummary.lastMs == logical.back().timeMs);
    for (const auto& summary : g_historySummary.accounts) {
        auto account = accounts.find(summary.accountId);
        CHECK(account != accounts.end() && account->second.first == summary.records &&
              !memcmp(&account->second.second, &summary.last, sizeof(HistorySample)));
    }
}

// The journal's calls as its callers make them.
static void Append(const std::vector<HistorySample>& batch) {
    AppendHistory(batch, g_historyEpoch.load(), g_historySettingsGeneration.load());
}
static void Load() {
    std::lock_guard lock(g_historyMutex);
    LoadHistoryLocked();
}
// Maintenance as its hourly cadence next runs it.
static void Maintain() {
    g_historyMaintenanceMs = 0;
    MaintainHistory();
}
static std::vector<HistorySample> Cached() { return Vector(g_historySamples); }
static std::vector<HistorySample> Since(const std::vector<HistorySample>& records, ULONGLONG from) {
    std::vector<HistorySample> result;
    for (const auto& record : records) {
        if (record.timeMs >= from) result.push_back(record);
    }
    return result;
}
static size_t StagingFiles() {
    size_t count = 0;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(UserFolder(), error)) {
        count += entry.path().extension() == L".staging";
    }
    return count;
}
