// Thinning of older Antigravity readings: it is polled every minute whatever the poll setting, so
// its readings older than kHistoryThinAgeMs are kept about kHistoryThinSpacingMs apart (a thinned
// reading stores that as its interval), with every line the history window draws through them
// still connected, and every break still a break. Pure: a machine fed the journal's readings, one
// pass, deciding each reading once the next one of its account arrives.

static constexpr ULONGLONG kHistoryThinAgeMs = 15 * kHistoryDayMs;
static constexpr ULONGLONG kHistoryThinSpacingMs = 600000;

// What thinning does with a reading: keeps it as it was, drops it, or keeps it with its interval
// widened to kHistoryThinSpacingMs (its checksum then recomputed by the writer).
enum class HistoryThinFate : uint8_t { Kept, Dropped, Widened };

// Per account, over its readings older than kHistoryThinAgeMs at `nowMs` (thinned ones included),
// with their stored intervals:
// - Kept: the first and the last, failed ones, and both readings of each break between
//   consecutive ones: a polling gap (HistoryPollingGap), a meter gaining or losing its value, the
//   failed flag changing, or a reset (HistoryWindowConnection) of a meter valued in both.
// - Any other is dropped only when it comes less than kHistoryThinSpacingMs after the last kept
//   reading and, on every meter its readings connect from that one to the next, the last kept one
//   (at its interval once widened) connects straight to the next (at its stored interval).
// - Kept readings are widened to kHistoryThinSpacingMs, except the last, failed ones and those
//   beside a polling gap.
// Feed it the eligible readings (Eligible) in journal order; a reading's fate is final once the
// next reading of its account was fed, and each account's last one stays Kept.
struct HistoryThinning {
    explicit HistoryThinning(ULONGLONG nowMs) : cutoff(nowMs > kHistoryThinAgeMs ? nowMs - kHistoryThinAgeMs : 0) {}

    // Readings it decides and their fates
    ULONGLONG cutoff;
    // Each fed reading's fate, in the order fed (its eligible ordinal); whether any isn't Kept
    std::vector<HistoryThinFate> fates;
    bool changed = false;

    bool Eligible(const HistorySample& sample) const { return sample.provider == 3 && sample.timeMs < cutoff; }

    // The next eligible reading; decides the previous one of its account. Throws when out of
    // memory (the caller drops the machine).
    void Feed(const HistorySample& reading) {
        auto run = std::find_if(runs.begin(), runs.end(), [&](const Run& r) { return r.account == reading.accountId; });
        if (run == runs.end()) {
            run = runs.emplace(runs.end());
            run->account = reading.accountId;
        }
        fates.push_back(HistoryThinFate::Kept);
        const size_t ordinal = fates.size() - 1;
        if (run->seen++) {
            const HistorySample& current = run->current;
            bool gap = HistoryPollingGap(current, reading);
            bool split = gap || current.failed != reading.failed;
            for (int m = 0; m < int(kHistoryMeterCount) && !split; ++m) {
                bool valued = HistoryHasValue(current, m);
                split = valued != HistoryHasValue(reading, m) ||
                        (valued && HistoryWindowConnection(current, reading, m) == HistoryBreak::Reset);
            }
            bool drop = false;
            if (run->seen > 2) {
                for (int m = 0; m < int(kHistoryMeterCount); ++m) {
                    run->chain[m] = run->chain[m] && HistoryConnection(run->previous, current, m) == HistoryBreak::None;
                }
                drop = !current.failed && !run->splitBefore && !split &&
                       current.timeMs < run->kept.timeMs + kHistoryThinSpacingMs;
                if (drop) {
                    HistorySample kept = run->kept;
                    if (run->keptWidens) kept.intervalMs = std::max(kept.intervalMs, kHistoryThinSpacingMs);
                    for (int m = 0; m < int(kHistoryMeterCount) && drop; ++m) {
                        drop = !run->chain[m] || HistoryConnection(current, reading, m) != HistoryBreak::None ||
                               HistoryConnection(kept, reading, m) == HistoryBreak::None;
                    }
                }
            }
            if (drop) {
                fates[run->ordinal] = HistoryThinFate::Dropped;
                changed = true;
            } else {
                run->kept = current;
                run->keptWidens = !run->gapBefore && !gap && !current.failed;
                run->chain.fill(true);
                if (run->keptWidens && current.intervalMs < kHistoryThinSpacingMs) {
                    fates[run->ordinal] = HistoryThinFate::Widened;
                    changed = true;
                }
            }
            run->previous = current;
            run->gapBefore = gap;
            run->splitBefore = split;
        }
        run->current = reading;
        run->ordinal = ordinal;
    }

private:
    // One account's readings so far: the undecided newest one (`current`, with its ordinal), the
    // one before it, what lies between them, the last kept one (widened or not) and, per meter,
    // whether the readings connect from it to `current`.
    struct Run {
        uint64_t account = 0;
        size_t seen = 0, ordinal = 0;
        HistorySample previous, current, kept;
        bool gapBefore = false, splitBefore = false, keptWidens = false;
        std::array<bool, kHistoryMeterCount> chain{};
    };
    std::vector<Run> runs;
};
