// Work days (taskbar/work-clock.h) and what they change in the meter model: a window's days off
// by local date, its working time, the pace marks, the projection and the usage graph's lines.
// Synthetic calendars, hand-derived expectations.

static constexpr ULONGLONG kHour = 60 * kMinute, kDay = 24 * kHour;

// A day of November 2023 and an hour of it, UTC: the 14th, a Tuesday, is kNow's day (kNow is
// 22:13:20 that day); the 9th and 16th are Thursdays, the 11th a Saturday, the 12th a Sunday.
static ULONGLONG Nov(int day, double hour = 0) {
    return ULONGLONG(1699920000000LL + (day - 14) * (long long)kDay + std::llround(hour * kHour));
}

// A calendar `offsetHours` from UTC that moves `shiftHours` more from `change` on (0: never).
static LocalTimeSource Zone(int offsetHours, ULONGLONG change = 0, int shiftHours = 0) {
    return [=](ULONGLONG unixMs, SYSTEMTIME* local) {
        long long shifted = (long long)unixMs + (offsetHours + (change && unixMs >= change ? shiftHours : 0)) * (long long)kHour;
        ULONGLONG fileTime = (ULONGLONG(shifted) + 11644473600000ULL) * 10000;
        FILETIME ft{DWORD(fileTime), DWORD(fileTime >> 32)};
        return FileTimeToSystemTime(&ft, local) != 0;
    };
}

using DaysOff = std::vector<std::pair<ULONGLONG, ULONGLONG>>;

// A week that resets at `reset`.
static WindowUsage Week(double pct, ULONGLONG reset) { return {pct, reset, kWeeklyWindowMs}; }

static void RunWorkDayTests() {
    // Monday to Friday, and Monday to Saturday, in UTC.
    static const WorkDays weekdays{1, 5, Zone(0)}, sixDays{1, 6, Zone(0)};
    // The week from Thursday the 9th, 09:00, to Thursday the 16th: 168 hours, 144 without its
    // Sunday.
    static constexpr auto week = [](double pct) { return Week(pct, Nov(16, 9)); };

    Run("work days: the settings' days by a calendar; off, without a calendar or over the whole week, every day", [] {
        const LocalTimeSource utc = Zone(0);
        Settings s;
        CHECK(!s.workDays && s.firstWorkDay == 1 && s.lastWorkDay == 5 && !WorkDaysFor(s, utc).local);
        s.workDays = true;
        WorkDays days = WorkDaysFor(s, utc);
        CHECK(days.local && days.first == 1 && days.last == 5);
        CHECK(!WorkDaysFor(s, {}).local);
        // Saturday to Wednesday wraps; one day is its own first and last.
        s.firstWorkDay = 6;
        s.lastWorkDay = 3;
        days = WorkDaysFor(s, utc);
        CHECK(days.local && days.first == 6 && days.last == 3);
        s.lastWorkDay = 6;
        CHECK(WorkDaysFor(s, utc).local);
        // The whole week, whatever its first day.
        for (auto [first, last] : {std::pair{1, 0}, std::pair{3, 2}, std::pair{0, 6}}) {
            s.firstWorkDay = first;
            s.lastWorkDay = last;
            CHECK(!WorkDaysFor(s, utc).local);
        }
        // Days out of range are the nearest weekday.
        s.firstWorkDay = -2;
        s.lastWorkDay = 9;
        CHECK(!WorkDaysFor(s, utc).local);
        s.lastWorkDay = 4;
        days = WorkDaysFor(s, utc);
        CHECK(days.local && days.first == 0 && days.last == 4);
        // The display carries them, given a calendar.
        CHECK(!Display(s).workDays.local);
        CHECK(MeterDisplayFor(s, I18nLocale(), TestMeasure, kNow, 1, 48, 0, utc).workDays.local);
    });

    Run("work clock: a window's days off by local date, joined and inside it; a window of a day or less works throughout", [] {
        // The weekend, one run: 120 working hours.
        WorkClock clock = WorkClockFor(weekdays, week(10));
        CHECK(clock.start == Nov(9, 9) && clock.reset == Nov(16, 9) && clock.total == 120 * kHour);
        CHECK((clock.off == DaysOff{{Nov(11), Nov(13)}}));
        // Thursday's 15 hours and Friday, then nothing until Monday.
        CHECK(clock.Work(Nov(9, 9), Nov(11)) == 39 * kHour && clock.Work(Nov(9, 9), Nov(12, 12)) == 39 * kHour);
        CHECK(clock.Work(Nov(11, 6), Nov(12, 18)) == 0 && clock.Work(Nov(10, 12), Nov(13, 12)) == 24 * kHour);
        CHECK(clock.Work(Nov(13), Nov(12)) == 0 && clock.Work(Nov(13), Nov(13)) == 0);
        CHECK_NEAR(clock.Elapsed(Nov(9)), 0);
        CHECK_NEAR(clock.Elapsed(Nov(11)), 39.0 / 120);
        CHECK_NEAR(clock.Elapsed(Nov(13)), 39.0 / 120);
        CHECK_NEAR(clock.Elapsed(Nov(16, 9)), 1);
        // A line straight in working time turns where the weekend starts and ends, between its ends.
        CHECK((clock.Corners(Nov(9, 9), Nov(16, 9)) == std::vector<ULONGLONG>{Nov(9, 9), Nov(11), Nov(13), Nov(16, 9)}));
        CHECK((clock.Corners(Nov(11), Nov(13)) == std::vector<ULONGLONG>{Nov(11), Nov(13)}));
        CHECK((clock.Corners(Nov(12), Nov(14)) == std::vector<ULONGLONG>{Nov(12), Nov(13), Nov(14)}));

        // Five hours behind UTC, the local weekend is five hours later.
        CHECK((WorkClockFor({1, 5, Zone(-5)}, week(10)).off == DaysOff{{Nov(11, 5), Nov(13, 5)}}));
        // Monday to Saturday: Sunday alone.
        clock = WorkClockFor(sixDays, week(10));
        CHECK((clock.off == DaysOff{{Nov(12), Nov(13)}}) && clock.total == 144 * kHour);
        // Saturday to Wednesday: the rest of the first Thursday with Friday, and the last Thursday's
        // nine hours.
        clock = WorkClockFor({6, 3, Zone(0)}, week(10));
        CHECK((clock.off == DaysOff{{Nov(9, 9), Nov(11)}, {Nov(16), Nov(16, 9)}}) && clock.total == 120 * kHour);
        // A week from Sunday noon: the rest of that Sunday, then the weekend it ends in.
        clock = WorkClockFor(weekdays, Week(10, Nov(19, 12)));
        CHECK((clock.off == DaysOff{{Nov(12, 12), Nov(13)}, {Nov(18), Nov(19, 12)}}) && clock.total == 120 * kHour);

        // A day or less, a Sunday's included: every hour of it.
        clock = WorkClockFor(weekdays, {10, Nov(12, 18), kDay});
        CHECK(clock.off.empty() && clock.total == kDay && clock.start == Nov(11, 18));
        clock = WorkClockFor(weekdays, {10, Nov(12, 15), kFiveHours});
        CHECK(clock.off.empty() && clock.total == kFiveHours);
        CHECK_NEAR(clock.Elapsed(Nov(12, 12.5)), 0.5);
        // 36 hours inside the weekend: no working time, so no share of it either.
        clock = WorkClockFor(weekdays, {10, Nov(12, 12), 36 * kHour});
        CHECK((clock.off == DaysOff{{Nov(11), Nov(12, 12)}}) && clock.total == 0);
        CHECK_NEAR(clock.Elapsed(Nov(12)), 0);
        // Without a window: nothing.
        CHECK(WorkClockFor(weekdays, {10, 0, 0}).total == 0 && WorkClockFor(weekdays, {10, 5000, kWeeklyWindowMs}).total == 0);
        // Without work days, or with a calendar that fails: the whole window.
        clock = WorkClockFor({}, week(10));
        CHECK(clock.off.empty() && clock.total == kWeeklyWindowMs && clock.start == Nov(9, 9));
        clock = WorkClockFor({1, 5, [](ULONGLONG, SYSTEMTIME*) { return false; }}, week(10));
        CHECK(clock.off.empty() && clock.total == kWeeklyWindowMs);
    });

    Run("work clock: a day the clock changes on is as long as it was, a skipped or repeated midnight included", [] {
        auto sunday = [](const LocalTimeSource& zone) { return WorkClockFor({1, 6, zone}, week(10)); };
        // Five hours behind UTC, Sunday runs from 05:00 UTC to 05:00 UTC.
        CHECK((sunday(Zone(-5)).off == DaysOff{{Nov(12, 5), Nov(13, 5)}}));
        // Forward an hour at 02:00 that Sunday: 23 hours, and 145 of the week's 168 worked.
        WorkClock forward = sunday(Zone(-5, Nov(12, 7), 1));
        CHECK((forward.off == DaysOff{{Nov(12, 5), Nov(13, 4)}}) && forward.total == 145 * kHour);
        // Back an hour at 02:00, four hours behind UTC until then: 25 hours.
        WorkClock back = sunday(Zone(-4, Nov(12, 6), -1));
        CHECK((back.off == DaysOff{{Nov(12, 4), Nov(13, 5)}}) && back.total == 143 * kHour);
        // Forward at midnight, so Sunday starts at 01:00: 23 hours from Saturday's end.
        CHECK((sunday(Zone(-5, Nov(12, 5), 1)).off == DaysOff{{Nov(12, 5), Nov(13, 4)}}));
        // Back at 01:00 to a second midnight: Sunday from its first one, 25 hours.
        CHECK((sunday(Zone(-4, Nov(12, 5), -1)).off == DaysOff{{Nov(12, 4), Nov(13, 5)}}));
        // A change on a work day leaves the days off whole: forward on Friday, Sunday an hour earlier.
        CHECK((sunday(Zone(-5, Nov(10, 7), 1)).off == DaysOff{{Nov(12, 4), Nov(13, 4)}}));
        // Back an hour a minute into Sunday, to Saturday 23:01: that minute is Sunday's, the 59
        // minutes after it Saturday's again, then Sunday whole.
        WorkClock fold = sunday(Zone(-4, Nov(12, 4) + kMinute, -1));
        CHECK((fold.off == DaysOff{{Nov(12, 4), Nov(12, 4) + kMinute}, {Nov(12, 5), Nov(13, 5)}}) &&
              fold.total == 144 * kHour - kMinute);
    });

    // The first slot's meter at `now` for a reading taken then, with `settings` and UTC's calendar.
    static constexpr auto meter = [](ULONGLONG now, WindowUsage reading, const Settings& settings,
                                     BarLayout layout = BarLayout::Stacked, const LocalTimeSource& zone = Zone(0)) {
        MeterDisplay display = MeterDisplayFor(settings, I18nLocale(), TestMeasure, now, 1, 48, 0, zone);
        AccountData data = Fresh();
        data.win5h = reading;
        data.lastSuccessMs = now;
        return AccountMeterState(display, layout, Account(L"anthropic", L"A", {true, false, false, false}), data, 0, 0)
            .quotas[0];
    };

    Run("pace on work days: the share of a long window's working time, level through a day off; short windows as before", [] {
        // A time track's length on a 100 DIP bar is the pace in percent.
        const Settings even = With([](Settings& s) {
            s.barLayout = BarLayout::Stacked;
            s.paceTickStyle = PaceTickStyle::TimeTrack;
            s.lastWorkDay = 6;
        });
        Settings work = even;
        work.workDays = true;
        // Saturday 18:00, 57 hours in: 57 of 144 working hours, or of all 168.
        CHECK(meter(Nov(11, 18), week(10), work).pacePx == 40 && meter(Nov(11, 18), week(10), even).pacePx == 34);
        // Sunday: 63 of 144 all day, while even use goes on (86 of 168 at 23:00).
        CHECK(meter(Nov(12), week(10), work).pacePx == 44 && meter(Nov(12, 23), week(10), work).pacePx == 44 &&
              meter(Nov(13), week(10), work).pacePx == 44);
        CHECK(meter(Nov(12, 23), week(10), even).pacePx == 51);
        // Monday noon: 75 of 144.
        CHECK(meter(Nov(13, 12), week(10), work).pacePx == 52);
        // Remaining mode: the working time left, 87 of 144.
        Settings left = work;
        left.barMode = BarMode::Remaining;
        CHECK(meter(Nov(11, 18), week(10), left).pacePx == 60);
        // Tiles: 39.6% around the ring.
        CHECK(meter(Nov(11, 18), week(10), work, BarLayout::SegmentedRing).paceUnits == 396 &&
              meter(Nov(11, 18), week(10), even, BarLayout::SegmentedRing).paceUnits == 339);
        // Without a calendar the setting changes nothing.
        CHECK(meter(Nov(11, 18), week(10), work, BarLayout::Stacked, {}).pacePx == 34);
        // A 5-hour window on Sunday, half way: even use.
        CHECK(meter(Nov(12, 12.5), {10, Nov(12, 15), kFiveHours}, work).pacePx == 50);
        // 36 hours inside a Monday-to-Friday weekend: no pace; a third of the way with the setting off.
        Settings weekend = work;
        weekend.lastWorkDay = 5;
        const WindowUsage inside{10, Nov(12, 12), 36 * kHour};
        CHECK(!meter(Nov(11, 12), inside, weekend).paceVisible);
        CHECK(meter(Nov(11, 12), inside, even).paceVisible && meter(Nov(11, 12), inside, even).pacePx == 33);
    });

    Run("projection on work days: the rate over working time, level through a day off; the limit where it is first reached", [] {
        const WorkClock six = WorkClockFor(sixDays, week(55));
        const WorkClock all = WorkClockFor({}, week(55));
        auto project = [](const WindowUsage& reading, const WorkClock& clock, ULONGLONG now,
                          std::vector<TileTracePoint> trace = {}, ULONGLONG lookback = kProjectionLookbackMs) {
            std::optional<UsageProjection> p = ProjectUsage(reading, clock, trace, now, lookback);
            CHECK(p && p->fromMs == now);
            return p.value_or(UsageProjection{});
        };
        // Saturday 18:00 at 55%, 40% a day before: 15% a working day. The 45% left take 72 working
        // hours: Saturday's last 6, none on Sunday, 66 from Monday, so Wednesday 18:00, a day later
        // than at that rate every day.
        const std::vector<TileTracePoint> saturday = {{Nov(10, 18), 40, false}, {Nov(11, 18), 55, true}};
        UsageProjection p = project(week(55), six, Nov(11, 18), saturday);
        CHECK(p.sinceMs == Nov(10, 18) && p.limit && p.toMs == Nov(15, 18));
        CHECK_NEAR(p.toUsed, 100);
        // On the way: 58.75% as Sunday starts, the same as it ends, 100% at the limit.
        CHECK_NEAR(p.UsedAt(six, 55, Nov(12)), 58.75);
        CHECK_NEAR(p.UsedAt(six, 55, Nov(13)), 58.75);
        CHECK_NEAR(p.UsedAt(six, 55, Nov(15, 18)), 100);
        UsageProjection daily = project(week(55), all, Nov(11, 18), saturday);
        CHECK(daily.sinceMs == Nov(10, 18) && daily.limit && daily.toMs == Nov(14, 18));

        // Monday 10:00: the last working day reaches back over Sunday to Saturday 10:00. 30% then
        // to 54% is 1% an hour, 46 hours to the limit; every day counted, Sunday 10:00's 42% to
        // 54% is half that, 89.5% at the reset 71 hours on.
        const std::vector<TileTracePoint> monday = {{Nov(11, 10), 30, false}, {Nov(13, 10), 54, true}};
        p = project(week(54), six, Nov(13, 10), monday);
        CHECK(p.sinceMs == Nov(11, 10) && p.limit && p.toMs == Nov(15, 8));
        daily = project(week(54), all, Nov(13, 10), monday);
        CHECK(daily.sinceMs == Nov(12, 10) && !daily.limit && daily.toMs == Nov(16, 9));
        CHECK_NEAR(daily.toUsed, 89.5);
        // A recording gap across that cutoff: from the reading before it, Friday 20:00. 24% in 38
        // working hours (Friday's 4, Saturday, Monday's 10), 71 more to the reset.
        p = project(week(54), six, Nov(13, 10), {{Nov(10, 20), 30, false}, {Nov(13, 9), 50, false}, {Nov(13, 10), 54, true}});
        CHECK(p.sinceMs == Nov(10, 20) && !p.limit && p.toMs == Nov(16, 9));
        CHECK_NEAR(p.toUsed, 54 + 24.0 * 71 / 38);

        // Half the quota in half the working time, Monday 09:00: 100% at the reset, no limit before
        // it.
        p = project(week(50), six, Nov(13, 9), {}, 0);
        CHECK(p.sinceMs == Nov(9, 9) && !p.limit && p.toMs == Nov(16, 9));
        CHECK_NEAR(p.toUsed, 100);
        // The same in a week that resets on Sunday noon: 100% as Saturday ends, 12 hours before the
        // reset.
        const WindowUsage toSunday = Week(50, Nov(12, 12));
        const WorkClock sundayReset = WorkClockFor(sixDays, toSunday);
        CHECK((sundayReset.off == DaysOff{{Nov(5, 12), Nov(6)}, {Nov(12), Nov(12, 12)}}) && sundayReset.total == 144 * kHour);
        p = project(toSunday, sundayReset, Nov(9), {}, 0);
        CHECK(p.sinceMs == Nov(5, 12) && p.limit && p.toMs == Nov(12));
        CHECK_NEAR(p.toUsed, 100);
        // On its last Sunday no working time is left: level at the reading to the reset.
        p = project(toSunday, sundayReset, Nov(12, 6), {}, 0);
        CHECK(!p.limit && p.toMs == Nov(12, 12));
        CHECK_NEAR(p.toUsed, 50);
        CHECK_NEAR(p.UsedAt(sundayReset, 50, Nov(12, 9)), 50);

        // A limit met as the last work day ends is there, not after the weekend: Monday to Friday,
        // 95% Thursday 23:00 to 99.8% Friday 23:00 is 0.2% an hour, so 100% at Saturday 00:00.
        const WorkClock five = WorkClockFor(weekdays, week(99.8));
        p = project(week(99.8), five, Nov(10, 23), {{Nov(9, 23), 95, false}, {Nov(10, 23), 99.8, true}});
        CHECK(p.sinceMs == Nov(9, 23) && p.limit && p.toMs == Nov(11));
        // From 99.7% (4.7% a day) Friday's last hour falls short: the 0.3% left take 5,514,894 ms of
        // working time, 1,914,894 of them from Monday 00:00.
        p = project(week(99.7), five, Nov(10, 23), {{Nov(9, 23), 95, false}, {Nov(10, 23), 99.7, true}});
        CHECK(p.limit && p.toMs == Nov(13) + 1914894);

        // On a day off time alone moves nothing: the last working day is Saturday, 31% as it
        // started (between 19% Friday noon and 43% Saturday noon) to 55%; 45 working hours from
        // Monday reach the limit Tuesday 21:00, at 06:00 as at 18:00.
        const std::vector<TileTracePoint> dayOff = {{Nov(10, 12), 19, false}, {Nov(11, 12), 43, true}, {Nov(11, 22), 55, true}};
        for (double hour : {6.0, 18.0}) {
            p = project(week(55), six, Nov(12, hour), dayOff);
            CHECK(p.sinceMs == Nov(11) && p.limit && p.toMs == Nov(14, 21));
        }

        // From 5% of the working time on: 7.2 of 144 hours, not 8.4 of 168.
        CHECK(!ProjectUsage(week(10), six, {}, Nov(9, 16), 0) && ProjectUsage(week(10), six, {}, Nov(9, 16.5), 0));
        CHECK(!ProjectUsage(week(10), all, {}, Nov(9, 16.5), 0) && ProjectUsage(week(10), all, {}, Nov(9, 17.5), 0));
        // A window without working time has none.
        const WindowUsage inside{30, Nov(12, 12), 36 * kHour};
        CHECK(!ProjectUsage(inside, WorkClockFor(weekdays, inside), {}, Nov(11, 12), 0) &&
              ProjectUsage(inside, WorkClockFor({}, inside), {}, Nov(11, 12), 0));
    });

    Run("usage graph on work days: the even-pace line and the projection level through a day off, in a span inside one too", [] {
        // A 100 x 24 DIP plot: x the week's share in hundredths, y = 22 - used / 5.
        auto graph = [](ULONGLONG now, std::vector<TileTracePoint> trace, SparklineSpan span = SparklineSpan::Window) {
            Settings s;
            s.sparklineWidth = 100;
            s.sparklineHeight = 60;
            s.sparklineSpan = span;
            s.workDays = true;
            s.lastWorkDay = 6;
            MeterDisplay display = MeterDisplayFor(s, I18nLocale(), TestMeasure, now, 1, 48, 0, Zone(0));
            AccountData data = Fresh();
            data.win5h = week(55);
            data.lastSuccessMs = now;
            return AccountMeterState(display, BarLayout::Sparkline, Account(L"anthropic", L"A", {true, false, false, false}),
                                     data, 0, 0, [trace](int, const WindowUsage&) { return trace; }).quotas[0];
        };
        auto path = [](const TileOutline& outline, std::vector<std::pair<double, double>> points) {
            CHECK(outline.size() == 1 && outline[0].segments.size() + 1 == points.size());
            if (outline.size() != 1 || outline[0].segments.size() + 1 != points.size()) return;
            CHECK_NEAR(outline[0].x, points[0].first);
            CHECK_NEAR(outline[0].y, points[0].second);
            for (size_t k = 1; k < points.size(); k++) {
                CHECK_NEAR(outline[0].segments[k - 1].x, points[k].first);
                CHECK_NEAR(outline[0].segments[k - 1].y, points[k].second);
            }
        };
        // Saturday 18:00, 57 of the week's 168 hours in (x 33.93, drawn at 34), at 55% (y 11).
        // Even use: 43.75% (y 13.25) as Sunday starts 63 hours in (x 37.5), the same as it ends 87
        // hours in (51.79, at 51.75), 100% at the reset. The projection: 58.75% (y 10.25) through
        // Sunday, the limit Wednesday 18:00, 153 hours in (91.07, at 91), marked there.
        QuotaMeter q = graph(Nov(11, 18), {{Nov(10, 18), 40, false}, {Nov(11, 18), 55, true}});
        path(q.spark[kSparkPace], {{0, 22}, {37.5, 13.25}, {51.75, 13.25}, {100, 2}});
        path(q.spark[kSparkProjection], {{34, 11}, {37.5, 10.25}, {51.75, 10.25}, {91, 2}});
        CHECK(q.spark[kSparkLimit].size() == 1 && q.spark[kSparkLimit][0].segments.size() == 2);
        CHECK_NEAR(q.spark[kSparkLimit][0].x, 91);
        CHECK_NEAR(q.spark[kSparkLimit][0].y, 2 - 1.75);
        // Sunday noon with 12 hours each side: the span is that Sunday. Both lines level across it,
        // even use at 43.75% and the projection at the reading from now (x 50) on, its limit past
        // the span.
        q = graph(Nov(12, 12), {{Nov(10, 12), 19, false}, {Nov(11, 12), 43, true}, {Nov(11, 22), 55, true}},
                  SparklineSpan::Hours12);
        path(q.spark[kSparkPace], {{0, 13.25}, {100, 13.25}});
        path(q.spark[kSparkProjection], {{50, 11}, {100, 11}});
        CHECK(q.spark[kSparkLimit].empty() && q.spark[kSparkLines].empty());
    });
}
