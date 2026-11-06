// Thinning of older Antigravity readings: it is polled every minute whatever the poll setting, so
// its readings older than kHistoryThinAgeMs are kept about kHistoryThinSpacingMs apart (a thinned
// reading stores that as its interval), with every line the history window draws through them
// still connected, and every break still a break. Pure.

static constexpr ULONGLONG kHistoryThinAgeMs = 15 * kHistoryDayMs;
static constexpr ULONGLONG kHistoryThinSpacingMs = 600000;

// The journal with each Antigravity account's readings older than kHistoryThinAgeMs at `nowMs`
// thinned, every other sample as it was, all in their order; nullopt when nothing changes. Per
// account, over all those readings (thinned ones included), with their stored intervals:
// - Kept: the first and the last, failed ones, and both readings of each break between
//   consecutive ones: a polling gap (HistoryPollingGap), a meter gaining or losing its value, the
//   failed flag changing, or a reset (HistoryWindowConnection) of a meter valued in both.
// - Any other is dropped only when it comes less than kHistoryThinSpacingMs after the last kept
//   reading and, on every meter its readings connect from that one to the next, the last kept one
//   (at its interval once widened) connects straight to the next (at its stored interval).
// - Kept readings are widened to kHistoryThinSpacingMs, their checksum recomputed, except the last,
//   failed ones and those beside a polling gap.
static std::optional<std::deque<HistorySample>> ThinHistorySamples(const std::deque<HistorySample>& samples,
                                                                   ULONGLONG nowMs) {
    const ULONGLONG cutoff = nowMs > kHistoryThinAgeMs ? nowMs - kHistoryThinAgeMs : 0;
    std::map<uint64_t, std::vector<size_t>> runs;  // Each account's readings to thin, by index
    for (size_t i = 0; i < samples.size(); ++i) {
        if (samples[i].provider == 3 && samples[i].timeMs < cutoff) runs[samples[i].accountId].push_back(i);
    }
    enum class Fate : uint8_t { Kept, Dropped, Widened };
    std::vector<Fate> fates(samples.size());
    bool changed = false;
    for (const auto& [account, run] : runs) {
        const size_t n = run.size();
        auto at = [&](size_t k) -> const HistorySample& { return samples[run[k]]; };
        // Between readings k - 1 and k: a polling gap, or any break.
        std::vector<bool> gap(n + 1), split(n + 1);
        for (size_t k = 1; k < n; ++k) {
            const HistorySample& a = at(k - 1);
            const HistorySample& b = at(k);
            gap[k] = HistoryPollingGap(a, b);
            split[k] = gap[k] || a.failed != b.failed;
            for (int m = 0; m < int(kHistoryMeterCount) && !split[k]; ++m) {
                bool valued = HistoryHasValue(a, m);
                split[k] = valued != HistoryHasValue(b, m) ||
                           (valued && HistoryWindowConnection(a, b, m) == HistoryBreak::Reset);
            }
        }
        auto widens = [&](size_t k) { return k + 1 < n && !gap[k] && !gap[k + 1] && !at(k).failed; };
        size_t last = 0;
        // Per meter: whether the readings connect from the last kept one to the current one.
        std::array<bool, kHistoryMeterCount> chain;
        chain.fill(true);
        for (size_t k = 1; k + 1 < n; ++k) {
            const HistorySample& reading = at(k);
            const HistorySample& next = at(k + 1);
            for (int m = 0; m < int(kHistoryMeterCount); ++m) {
                chain[m] = chain[m] && HistoryConnection(at(k - 1), reading, m) == HistoryBreak::None;
            }
            bool drop = !reading.failed && !split[k] && !split[k + 1] &&
                        reading.timeMs < at(last).timeMs + kHistoryThinSpacingMs;
            if (drop) {
                HistorySample kept = at(last);
                if (widens(last)) kept.intervalMs = std::max(kept.intervalMs, kHistoryThinSpacingMs);
                for (int m = 0; m < int(kHistoryMeterCount) && drop; ++m) {
                    drop = !chain[m] || HistoryConnection(reading, next, m) != HistoryBreak::None ||
                           HistoryConnection(kept, next, m) == HistoryBreak::None;
                }
            }
            if (drop) {
                fates[run[k]] = Fate::Dropped;
                changed = true;
            } else {
                last = k;
                chain.fill(true);
            }
        }
        for (size_t k = 0; k < n; ++k) {
            if (fates[run[k]] == Fate::Kept && widens(k) && at(k).intervalMs < kHistoryThinSpacingMs) {
                fates[run[k]] = Fate::Widened;
                changed = true;
            }
        }
    }
    if (!changed) return std::nullopt;
    std::deque<HistorySample> thinned;
    for (size_t i = 0; i < samples.size(); ++i) {
        if (fates[i] == Fate::Dropped) continue;
        HistorySample& sample = thinned.emplace_back(samples[i]);
        if (fates[i] == Fate::Widened) {
            sample.intervalMs = kHistoryThinSpacingMs;
            sample.checksum = HistoryChecksum(sample);
        }
    }
    return thinned;
}
