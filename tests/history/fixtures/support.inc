// Synthetic fixtures and independent expectations, never a second grouping implementation.
static int passed = 0, failed = 0, assertions = 0;
static constexpr ULONGLONG baseTime = 1700000000000ULL;
static constexpr ULONGLONG hour = 3600000;

#define CHECK(condition) do { ++assertions; if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

static void Run(const std::string& name, const std::function<void()>& body) {
    g_unloading = false;
    try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}

// Explicit inputs of the history model (history/series.h): a series query plus the samples and
// accounts it reads.
struct ModelFixture : HistorySeriesQuery {
    HistorySnapshot snapshot;
    std::vector<HistoryUiAccount> accounts;
};

static ModelFixture Model(std::vector<uint32_t> providers = {1, 2, 3}) {
    ModelFixture state;
    state.from = state.readFrom = baseTime;
    state.to = baseTime + kHistoryWeekMs;
    for (auto provider : providers) {
        HistoryUiAccount account;
        account.id = 100 + state.accounts.size(); account.provider = provider;
        account.label = L"account-" + std::to_wstring(account.id);
        account.configured = true; account.recording = true;
        state.accounts.push_back(std::move(account));
    }
    return state;
}

static HistorySample& AddRow(ModelFixture& state, size_t account, ULONGLONG time,
                             std::initializer_list<std::pair<int, double>> values) {
    HistorySample sample;
    sample.accountId = state.accounts.at(account).id;
    sample.provider = state.accounts.at(account).provider;
    sample.session = 41; sample.timeMs = time; sample.intervalMs = 60000;
    for (auto [meter, value] : values) {
        sample.meters[meter].value = value;
        sample.meters[meter].durationMs = meter == 0 || meter == 4 ? 5 * hour :
            meter == 1 || meter == 2 || meter == 5 ? kHistoryWeekMs : 0;
    }
    state.accounts[account].rows.push_back(state.snapshot.samples.size());
    state.snapshot.samples.push_back(sample);
    return state.snapshot.samples.back();
}

static void AllLanes(ModelFixture& state, bool previousOnly = false) {
    for (size_t account = 0; account < state.accounts.size(); ++account) {
        ULONGLONG start = previousOnly ? state.readFrom : state.from;
        AddRow(state, account, start + 1000, {{0, 10}, {1, 20}, {2, 30}, {3, 40}, {4, 50}, {5, 60}, {6, 12.5}, {7, 800}});
        AddRow(state, account, start + 61000, {{0, 15}, {1, 25}, {2, 35}, {3, 45}, {4, 55}, {5, 65}, {6, 15.5}, {7, 750}});
    }
}

static bool Never() { return false; }

static HistorySeriesSet Build(const ModelFixture& state) {
    return HistoryBuildSeries(state.snapshot.samples, state.accounts, state, Never);
}

static const HistorySeries& Series(const HistorySeriesSet& built, size_t account, int meter) {
    auto it = std::find_if(built.series.begin(), built.series.end(), [&](const auto& line) {
        return line.account == account && line.meter == meter;
    });
    if (it == built.series.end()) throw std::runtime_error("Missing account/meter " + std::to_string(account) + "/" + std::to_string(meter));
    return *it;
}

static std::set<int> Meters(const HistorySeriesSet& built, size_t account = 0) {
    std::set<int> result;
    for (const auto& line : built.series) if (line.account == account) result.insert(line.meter);
    return result;
}

// A built line's raw samples in its current [from, to) or previous [readFrom, from) period,
// reduced at a fixed plot width.
static HistoryRenderSeries Reduce(const ModelFixture& state, const HistorySeries& line, int period) {
    const auto& rows = state.accounts.at(line.account).rows;
    return period ? HistoryReduceRows(state.snapshot.samples, rows, line.meter, state.readFrom, state.from, 1000, Never)
                  : HistoryReduceRows(state.snapshot.samples, rows, line.meter, state.from, state.to, 1000, Never);
}
