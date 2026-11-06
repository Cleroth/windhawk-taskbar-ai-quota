// Current-window usage curves for the details flyout's burn-up chart: built from journal samples
// with the history continuity rules and reduced for drawing. Pure; the published store and its
// fetch/UI protocol are in burn-up-store.h.

static constexpr int kBurnUpMeterCount = 6;  // History meters 0-5: percentages with windows.
// Drawing resolution: one reduction bin per flyout chart column (DIP) across the window.
static constexpr int kBurnUpColumns = 250;
static constexpr size_t kBurnUpMaxPoints = 4 * kBurnUpColumns;
// Longest charted window; it bounds the fetch thread's journal scan. Monthly windows (Anthropic
// extra usage, GitHub Copilot's AI credits, OpenCode Go's monthly) are the longest provider
// windows.
static constexpr ULONGLONG kBurnUpMaxWindowMs = 32 * kHistoryDayMs;

struct BurnUpPoint {
    ULONGLONG timeMs = 0;  // A recorded reading's time, never interpolated.
    double used = 0;       // Used percentage as recorded; can exceed 100.
    bool connect = false;  // Joined to the previous point; false starts a segment.
};

// One meter's recorded readings in the window of its newest reading, oldest first.
struct BurnUpCurve {
    ULONGLONG resetMs = 0;
    ULONGLONG durationMs = 0;
    std::vector<BurnUpPoint> points;  // Empty: no chart.
};

struct BurnUpAccountCurves {
    uint64_t historyId = 0;
    uint32_t provider = 0;  // As in HistorySample.
    std::array<BurnUpCurve, kBurnUpMeterCount> meters;  // By history meter.
};

// Published curves and what they were built from.
struct BurnUpStore {
    uint64_t epoch = 0;        // g_historyEpoch when the journal was copied.
    ULONGLONG generation = 0;  // g_burnUpPolicyGeneration of the settings snapshot.
    std::vector<BurnUpAccountCurves> accounts;
};

// An account to rebuild and how far back its current windows reach (within retention).
struct BurnUpRequest {
    uint64_t historyId = 0;
    uint32_t provider = 0;
    ULONGLONG from = ULLONG_MAX;  // No window: nothing to copy.
};

// The fetch thread's copy of one account's journal samples from `from` on, in journal order.
struct BurnUpAccountTail {
    uint64_t historyId = 0;
    ULONGLONG from = ULLONG_MAX;
    std::deque<HistorySample> samples;
};

// Tails kept between rounds, and the journal state they mirror.
struct BurnUpSampleCache {
    uint64_t epoch = 0;     // g_historyEpoch,
    uint64_t revision = 0;  // g_historyRevision
    uint64_t appended = 0;  // and g_historyAppendCount at the last copy.
    std::vector<BurnUpAccountTail> accounts;
};

// Reads the journal's samples older than its cache, from `from` on, from the file
// (ReadHistoryRangeLocked): `use(sample)` for each in journal order; whether every one was read.
using BurnUpArchiveReader =
    std::function<bool(ULONGLONG from, const std::function<void(const HistorySample&)>& use)>;

// Copy step, run under g_historyMutex with the journal's cache (`journal`: in journal order, every
// sample from `cachedFrom` on), epoch, revision and append count. A requested tail gets only the
// samples appended since the last copy; it is refilled from its scan start when it is new or must
// reach further back, on `rescan`, or when the journal changed other than by appends (Clear, load,
// rewrites, samples leaving the cache), which also drops unrequested tails: they refill once
// requested again. A refill reaching back before the cache reads the older part through `older`
// first, one pass for all; each refill is built aside and taken only whole, and one whose older
// part couldn't be read drops its tail, refilled when next requested. Each requested tail is then
// trimmed to its scan start.
static void UpdateBurnUpTails(BurnUpSampleCache& cache, const std::deque<HistorySample>& journal, ULONGLONG cachedFrom,
                              uint64_t epoch, uint64_t revision, uint64_t appended,
                              const std::vector<BurnUpRequest>& requests, bool rescan,
                              const BurnUpArchiveReader& older) {
    uint64_t added = appended - cache.appended;
    if (rescan || cache.epoch != epoch || cache.revision != revision ||
        appended < cache.appended || added > journal.size()) {
        cache.accounts.clear();
        added = 0;
    }
    cache.epoch = epoch;
    cache.revision = revision;
    cache.appended = appended;
    auto tailOf = [&](uint64_t historyId) {
        return std::find_if(cache.accounts.begin(), cache.accounts.end(),
                            [&](const BurnUpAccountTail& tail) { return tail.historyId == historyId; });
    };
    std::vector<BurnUpAccountTail> refills;
    ULONGLONG oldest = ULLONG_MAX;
    for (const auto& request : requests) {
        auto tail = tailOf(request.historyId);
        if (tail != cache.accounts.end() && request.from >= tail->from) continue;
        BurnUpAccountTail& refill = refills.emplace_back();
        refill.historyId = request.historyId;
        refill.from = request.from;
        oldest = std::min(oldest, request.from);
    }
    auto refillOf = [&](uint64_t historyId) {
        return std::find_if(refills.begin(), refills.end(),
                            [&](const BurnUpAccountTail& refill) { return refill.historyId == historyId; });
    };
    for (size_t i = journal.size() - added; i < journal.size(); ++i) {
        auto tail = tailOf(journal[i].accountId);
        if (tail != cache.accounts.end() && refillOf(tail->historyId) == refills.end()) {
            tail->samples.push_back(journal[i]);
        }
    }
    auto take = [&](const HistorySample& sample) {
        auto refill = refillOf(sample.accountId);
        if (refill != refills.end() && sample.timeMs >= refill->from) refill->samples.push_back(sample);
    };
    bool archived = oldest >= cachedFrom || older(oldest, take);
    size_t first = journal.size();
    while (first > 0 && journal[first - 1].timeMs >= oldest) --first;
    for (size_t i = first; i < journal.size() && !g_unloading; ++i) take(journal[i]);
    for (auto& refill : refills) {
        auto tail = tailOf(refill.historyId);
        if (g_unloading || (!archived && refill.from < cachedFrom)) {
            if (tail != cache.accounts.end()) cache.accounts.erase(tail);
            continue;
        }
        if (tail == cache.accounts.end()) {
            tail = cache.accounts.insert(cache.accounts.end(), BurnUpAccountTail{refill.historyId});
        }
        tail->from = refill.from;
        tail->samples = std::move(refill.samples);
    }
    for (const auto& request : requests) {
        auto tail = tailOf(request.historyId);
        if (tail == cache.accounts.end()) continue;
        tail->from = request.from;
        while (!tail->samples.empty() && tail->samples.front().timeMs < tail->from) tail->samples.pop_front();
    }
}

// `samples` are one account's journal entries in journal order. The newest valid reading sets
// the window; earlier readings join it under the history continuity rules, and across a
// failure, gap or restart stay in it as a new segment only while the window is unchanged. A
// reset, a reading before the window start or a clock rollback ends the curve. Nothing is
// fabricated: the curve starts at the window's first recorded reading, not at a zero origin.
static BurnUpCurve BuildBurnUpCurve(const std::deque<HistorySample>& samples, int meter) {
    BurnUpCurve curve;
    size_t newest = samples.size();
    while (newest && !HistoryHasValue(samples[newest - 1], meter)) --newest;
    if (!newest) return curve;
    const HistorySample* later = &samples[--newest];
    const HistoryValue& window = later->meters[meter];
    if (!window.resetMs || !window.durationMs || window.durationMs > kBurnUpMaxWindowMs ||
        window.durationMs > window.resetMs) return curve;
    ULONGLONG start = window.resetMs - window.durationMs;
    ULONGLONG earliest = start > kHistoryResetToleranceMs ? start - kHistoryResetToleranceMs : 0;
    if (later->timeMs < earliest) return curve;
    curve.resetMs = window.resetMs;
    curve.durationMs = window.durationMs;
    auto& points = curve.points;
    points.push_back({later->timeMs, window.value, false});

    // Walk back from the newest reading. `visited` keeps journal times strictly decreasing.
    bool adjacent = true;
    ULONGLONG visited = later->timeMs;
    for (size_t i = newest; i-- > 0;) {
        const HistorySample& sample = samples[i];
        if (sample.timeMs >= visited || sample.timeMs < earliest) break;
        visited = sample.timeMs;
        if (!HistoryHasValue(sample, meter)) {
            adjacent = false;
            continue;
        }
        HistoryBreak link = adjacent ? HistoryConnection(sample, *later, meter) : HistoryBreak::Gap;
        if (link == HistoryBreak::Reset ||
            (link != HistoryBreak::None &&
             HistoryWindowConnection(sample, *later, meter) != HistoryBreak::None)) break;
        points.back().connect = link == HistoryBreak::None;
        points.push_back({sample.timeMs, sample.meters[meter].value, false});
        later = &sample;
        adjacent = true;
    }
    std::reverse(points.begin(), points.end());

    // Reduce for drawing: per chart column of the window and segment keep the first, lowest,
    // highest and last readings, so every segment keeps its endpoints and joins between columns
    // survive. Only past the cap do columns span segments: kept readings of different segments
    // are never joined, dropped readings stay within their column, and short runs inside one
    // column can become dots or vanish.
    if (points.size() > kBurnUpMaxPoints) {
        std::vector<size_t> segment(points.size(), 0);
        for (size_t i = 1; i < points.size(); ++i) segment[i] = segment[i - 1] + !points[i].connect;
        auto columnOf = [&](size_t i) {
            // Readings within the reset tolerance before the start go to the first column.
            ULONGLONG time = std::clamp(points[i].timeMs, start, curve.resetMs);
            return std::min<ULONGLONG>((time - start) * kBurnUpColumns / curve.durationMs,
                                       kBurnUpColumns - 1);
        };
        auto reduce = [&](bool acrossSegments) {
            std::vector<BurnUpPoint> reduced;
            size_t previous = SIZE_MAX;
            for (size_t begin = 0, end = 0; begin < points.size(); begin = end) {
                size_t low = begin, high = begin;
                for (ULONGLONG column = columnOf(begin);
                     end < points.size() && columnOf(end) == column &&
                     (acrossSegments || segment[end] == segment[begin]); ++end) {
                    if (points[end].used < points[low].used) low = end;
                    if (points[end].used > points[high].used) high = end;
                }
                std::array<size_t, 4> kept{begin, low, high, end - 1};
                std::sort(kept.begin(), kept.end());
                for (auto it = kept.begin(), last = std::unique(kept.begin(), kept.end()); it != last; ++it) {
                    reduced.push_back({points[*it].timeMs, points[*it].used,
                                       previous != SIZE_MAX && segment[*it] == segment[previous]});
                    previous = *it;
                }
            }
            return reduced;
        };
        std::vector<BurnUpPoint> reduced = reduce(false);
        if (reduced.size() > kBurnUpMaxPoints) reduced = reduce(true);
        points = std::move(reduced);
    }
    return curve;
}

// The curve for a displayed reading: built under the current tags, for this account and
// provider, in the reading's window (same duration, reset within the jitter tolerance).
static const BurnUpCurve* FindBurnUpCurve(const BurnUpStore& store, uint64_t epoch,
                                          ULONGLONG generation, uint64_t historyId,
                                          uint32_t provider, int meter, ULONGLONG resetMs,
                                          ULONGLONG durationMs) {
    if (store.epoch != epoch || store.generation != generation || meter < 0 ||
        meter >= kBurnUpMeterCount || !resetMs || !durationMs) return nullptr;
    for (const auto& account : store.accounts) {
        if (account.historyId != historyId) continue;
        const BurnUpCurve& curve = account.meters[meter];
        ULONGLONG shift = curve.resetMs > resetMs ? curve.resetMs - resetMs : resetMs - curve.resetMs;
        if (account.provider != provider || curve.points.empty() || curve.durationMs != durationMs ||
            shift > kHistoryResetToleranceMs) return nullptr;
        return &curve;
    }
    return nullptr;
}

// Publication step, run under g_dataMutex. Rejects curves whose journal copy or settings
// snapshot was superseded by Clear or a curve policy change. A partial rebuild replaces only its
// accounts; accounts left without any curve are dropped.
static bool PublishBurnUpCurves(BurnUpStore& store, std::vector<BurnUpAccountCurves> built,
                                bool replaceAll, uint64_t epoch, ULONGLONG generation,
                                uint64_t currentEpoch, ULONGLONG currentGeneration) {
    if (epoch != currentEpoch || generation != currentGeneration) return false;
    if (replaceAll || store.epoch != epoch || store.generation != generation) store.accounts.clear();
    store.epoch = epoch;
    store.generation = generation;
    for (auto& account : built) {
        std::erase_if(store.accounts, [&](const BurnUpAccountCurves& existing) {
            return existing.historyId == account.historyId;
        });
        if (std::any_of(account.meters.begin(), account.meters.end(),
                        [](const BurnUpCurve& curve) { return !curve.points.empty(); })) {
            store.accounts.push_back(std::move(account));
        }
    }
    return true;
}
