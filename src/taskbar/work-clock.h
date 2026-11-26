// Work days: the working time of a quota window whose pace and projection skip the user's days
// off, as plain values from explicit inputs. Pure: local time comes through its source.

// A time's local date and time on this PC; false when it has none.
using LocalTimeSource = std::function<bool(ULONGLONG unixMs, SYSTEMTIME* local)>;

// The week's work days: every weekday from `first` through `last` (SYSTEMTIME::wDayOfWeek, 0:
// Sunday), wrapping past Saturday. Without `local`, every day is one.
struct WorkDays {
    int first = 1, last = 5;
    LocalTimeSource local;
};

// The settings' work days by `local`'s calendar: every day while the setting is off or its days
// cover the week.
static WorkDays WorkDaysFor(const Settings& settings, const LocalTimeSource& local) {
    int first = std::clamp(settings.firstWorkDay, 0, 6), last = std::clamp(settings.lastWorkDay, 0, 6);
    if (!settings.workDays || (last - first + 7) % 7 == 6) return {};
    return {first, last, local};
}

// A quota window's working time: its span (WorkClockFor) without the days off in it.
struct WorkClock {
    ULONGLONG start = 0, reset = 0;
    // The days off in the window, by local date: in order and apart, each run of them from its
    // first instant to the first of the work day after it, inside the window.
    std::vector<std::pair<ULONGLONG, ULONGLONG>> off;
    ULONGLONG total = 0;  // Working time from start to reset; 0 without a window or a work day in it.

    // Working time from `from` to `to`; outside the window every time counts.
    ULONGLONG Work(ULONGLONG from, ULONGLONG to) const {
        if (to <= from) return 0;
        ULONGLONG work = to - from;
        for (const auto& [begin, end] : off) {
            ULONGLONG first = std::max(begin, from), last = std::min(end, to);
            if (first < last) work -= last - first;
        }
        return work;
    }

    // The share of the window's working time passed at `time`: even use's share of the quota then.
    double Elapsed(ULONGLONG time) const { return total ? double(Work(start, time)) / double(total) : 0; }

    // Where a line from `from` to `to` that is straight in working time turns: both, and between
    // them each day off's start and end.
    std::vector<ULONGLONG> Corners(ULONGLONG from, ULONGLONG to) const {
        std::vector<ULONGLONG> corners{from};
        for (const auto& [begin, end] : off) {
            for (ULONGLONG edge : {begin, end}) {
                if (edge > from && edge < to) corners.push_back(edge);
            }
        }
        corners.push_back(to);
        return corners;
    }
};

// The working time of `window`: all of it, less `days`' days off when it is longer than a day
// (a shorter one runs whatever the day). A day off is every time `days.local` puts on its date:
// the window is cut where a date ends and where the local clock is set forward or back, so a day
// the clock changes on is as long as it was. A source that fails leaves every day a work day.
static WorkClock WorkClockFor(const WorkDays& days, const WindowUsage& window) {
    static constexpr long long kDayMs = 24LL * 60 * 60 * 1000;
    WorkClock clock;
    if (!window.windowDurationMs || window.resetUnixMs <= window.windowDurationMs) return clock;
    clock.start = window.resetUnixMs - window.windowDurationMs;
    clock.reset = window.resetUnixMs;
    clock.total = window.windowDurationMs;
    if (!days.local || window.windowDurationMs <= (ULONGLONG)kDayMs) return clock;

    // A time on the local clock: milliseconds from 1970 by its date (the civil calendar's days,
    // counted in years from March and 400-year eras) and time of day.
    auto localMs = [&](ULONGLONG time) -> std::optional<long long> {
        SYSTEMTIME local;
        if (!days.local(time, &local) || local.wMonth < 1 || local.wMonth > 12) return std::nullopt;
        int year = local.wYear - (local.wMonth < 3), ofEra = year % 400;
        int ofYear = (153 * ((local.wMonth + 9) % 12) + 2) / 5 + local.wDay - 1;
        long long day = year / 400 * 146097LL + ofEra * 365 + ofEra / 4 - ofEra / 100 + ofYear - 719468;
        return day * kDayMs + ((local.wHour * 60 + local.wMinute) * 60 + local.wSecond) * 1000 + local.wMilliseconds;
    };
    std::vector<std::pair<ULONGLONG, ULONGLONG>> off;
    for (ULONGLONG time = clock.start; time < clock.reset;) {
        std::optional<long long> local = localMs(time);
        if (!local || *local < 0) return clock;
        // One stretch on one date: to the date's end as the local clock runs from here, or the
        // reset; when the clock is set before then (its offset differs at the stretch's end), to
        // that change, found between a time before it and one from it on.
        const long long offset = *local - (long long)time;
        ULONGLONG next = time + std::min<ULONGLONG>(kDayMs - *local % kDayMs, clock.reset - time);
        std::optional<long long> last = localMs(next - 1);
        if (!last) return clock;
        if (*last - (long long)(next - 1) != offset) {
            ULONGLONG before = time;
            next--;
            while (next - before > 1) {
                ULONGLONG middle = before + (next - before) / 2;
                std::optional<long long> probe = localMs(middle);
                if (!probe) return clock;
                (*probe - (long long)middle == offset ? before : next) = middle;
            }
        }
        // 1970 started on a Thursday.
        int weekday = int((*local / kDayMs + 4) % 7);
        if ((weekday - days.first + 7) % 7 > (days.last - days.first + 7) % 7) {
            if (!off.empty() && off.back().second == time) off.back().second = next;
            else off.push_back({time, next});
        }
        time = next;
    }
    clock.off = std::move(off);
    clock.total = clock.Work(clock.start, clock.reset);
    return clock;
}
