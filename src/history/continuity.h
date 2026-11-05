// Reading continuity rules shared by the history window and the flyout's burn-up curves.

// Reset endpoints jitter by up to this much between readings of one window.
static constexpr ULONGLONG kHistoryResetToleranceMs = 120000;

static bool HistoryHasValue(const HistorySample& sample, int meter) {
    return !sample.failed && sample.meters[meter].value != -1 &&
           std::isfinite(sample.meters[meter].value);
}

enum class HistoryBreak { None, Gap, Reset, Unavailable };

// Whether two valid readings of a meter, b later than a, belong to one quota window,
// regardless of how they were sampled.
static HistoryBreak HistoryWindowConnection(const HistorySample& a, const HistorySample& b,
                                            int meter) {
    ULONGLONG gap = b.timeMs - a.timeMs;
    const auto& av = a.meters[meter];
    const auto& bv = b.meters[meter];
    if (av.durationMs != bv.durationMs) return HistoryBreak::Reset;
    if (!!av.resetMs != !!bv.resetMs) return HistoryBreak::Reset;
    if (av.resetMs) {
        // Reset endpoints can jitter. Never require exact equality; reject a boundary
        // within two minutes of this interval or a material shift of the reset epoch.
        constexpr ULONGLONG tolerance = kHistoryResetToleranceMs;
        ULONGLONG shift = av.resetMs > bv.resetMs ? av.resetMs - bv.resetMs : bv.resetMs - av.resetMs;
        ULONGLONG lower = a.timeMs > tolerance ? a.timeMs - tolerance : 0;
        ULONGLONG upper = b.timeMs > ULLONG_MAX - tolerance ? ULLONG_MAX : b.timeMs + tolerance;
        if ((av.resetMs >= lower && av.resetMs <= upper) ||
            (bv.resetMs >= lower && bv.resetMs <= upper)) return HistoryBreak::Reset;
        if (shift > tolerance) {
            // OpenAI's unused windows and OpenCode Go's unused 5-hour window roll a full duration
            // ahead until first use anchors them. This is not a reset: the old deadline is still
            // beyond this sampled interval. Keep real boundaries, other providers and lanes,
            // backward shifts, and partial windows conservative.
            if ((a.provider == 2 || (a.provider == 5 && meter == 0)) && av.value == 0 &&
                bv.value >= 0 && av.durationMs &&
                av.resetMs > upper && bv.resetMs > upper && bv.resetMs > av.resetMs &&
                (shift <= gap || shift - gap <= tolerance)) {
                ULONGLONG remaining = av.resetMs - a.timeMs;
                ULONGLONG error = remaining > av.durationMs ? remaining - av.durationMs : av.durationMs - remaining;
                if (error <= tolerance) return HistoryBreak::None;
            }
            return HistoryBreak::Reset;
        }
    }
    return HistoryBreak::None;
}

// Shared by rendering and integration: no extrapolation, including at range boundaries.
static HistoryBreak HistoryConnection(const HistorySample& a, const HistorySample& b,
                                      int meter) {
    if (!HistoryHasValue(a, meter) || !HistoryHasValue(b, meter))
        return HistoryBreak::Unavailable;
    if (a.accountId != b.accountId || a.provider != b.provider || a.session != b.session ||
        b.timeMs <= a.timeMs || !a.intervalMs || !b.intervalMs)
        return HistoryBreak::Gap;
    ULONGLONG gap = b.timeMs - a.timeMs;
    ULONGLONG polling = std::max(a.intervalMs, b.intervalMs);
    // ceil(gap / 2) > polling is overflow-safe gap > 2 * polling.
    if (gap / 2 + gap % 2 > polling) return HistoryBreak::Gap;
    return HistoryWindowConnection(a, b, meter);
}
