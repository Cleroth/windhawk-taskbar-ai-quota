// The history window's chart (history/chart.h, chart-paint.h) through its controller
// (history/controller.h) on synthetic journals: axes and layout, local time ticks (a fixed zone
// and one with daylight saving), picking, readout rows, emphasis, the legend's filter, the scene
// (draw list) and the reduction cache's key, with hand-derived values. The synthetic journals in
// the mockups' shapes also feed the chart renders (history-render.inc).

// Journals -------------------------------------------------------------------------------------

// One meter's readings for a synthetic journal: per period (current, the week before) segments of
// (hour, value) anchors from the period's start; each segment one quota window (its own reset),
// so lines break between segments.
struct TestSeries {
    uint64_t account = 0;
    uint32_t provider = 1;
    int meter = 1;
    std::vector<std::vector<std::pair<double, double>>> current{}, previous{};
    ULONGLONG duration = kHistoryWeekMs;
};

// Polls `step` hours apart from `from` (the previous week's a week earlier), one reading per
// account and poll holding all its meters; a reading's time a little before its hour (the first
// a minute after the start), so the last hour falls inside [from, from + 7 days). Between anchors
// a value rises in daytime bursts (deterministic, as the mockups' generator; polls under an hour
// apart: steadily, a little uneven), else evenly.
static std::vector<HistorySample> TestPolls(const std::vector<TestSeries>& lines, ULONGLONG from = kWeekFrom,
                                            double step = 1) {
    auto hash = [](uint64_t x) {
        x += 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
        return x ^ (x >> 31);
    };
    ULONGLONG stepMs = ULONGLONG(std::llround(step * double(kHourMs)));
    std::map<std::tuple<uint64_t, int, int64_t>, HistorySample> polls;
    for (const TestSeries& line : lines) {
        for (int period : {0, 1}) {
            ULONGLONG start = from - ULONGLONG(period) * kHistoryWeekMs;
            const auto& segments = period ? line.previous : line.current;
            for (const auto& anchors : segments) {
                ULONGLONG reset = start + ULONGLONG(std::llround((anchors.back().first + 1) * double(kHourMs))) + 300000;
                auto emit = [&](int64_t n, double value) {
                    HistorySample& sample = polls[{line.account, period, n}];
                    if (!sample.accountId) {
                        sample.accountId = line.account;
                        sample.provider = line.provider;
                        uint64_t jitter = hash(line.account * 7919 + uint64_t(n) * 31 + uint64_t(period)) % 60000;
                        sample.timeMs = start + ULONGLONG(n) * stepMs + (n ? jitter - 150000 : 60000);
                        sample.intervalMs = stepMs;
                        sample.session = 1;
                    }
                    sample.meters[size_t(line.meter)] = {value, reset, line.duration};
                };
                emit(std::llround(anchors[0].first / step), anchors[0].second);
                for (size_t a = 1; a < anchors.size(); a++) {
                    int64_t n0 = std::llround(anchors[a - 1].first / step), n1 = std::llround(anchors[a].first / step);
                    std::vector<double> weights;
                    for (int64_t n = n0 + 1; n <= n1; n++) {
                        uint64_t h = hash(line.account * 131 + uint64_t(line.meter) * 17 + uint64_t(n) * 1009 + uint64_t(period));
                        bool night = TestZone()(start + ULONGLONG(n) * stepMs).wHour < 8;
                        weights.push_back(step < 1          ? double(2 + h % 3) :
                                          night || h % 5 < 2 ? 0.0 : double(1 + h % 7 + (h % 6 == 0 ? 6 : 0)));
                    }
                    double total = 0;
                    for (double w : weights) total += w;
                    double sum = 0;
                    for (int64_t n = n0 + 1; n <= n1; n++) {
                        sum += total > 0 ? weights[size_t(n - n0 - 1)] : 1;
                        double share = sum / (total > 0 ? total : double(n1 - n0));
                        emit(n, anchors[a - 1].second + (anchors[a].second - anchors[a - 1].second) * share);
                    }
                }
            }
        }
    }
    std::vector<HistorySample> samples;
    for (auto& [key, sample] : polls) samples.push_back(sample);
    std::stable_sort(samples.begin(), samples.end(),
                     [](const HistorySample& a, const HistorySample& b) { return a.timeMs < b.timeMs; });
    return samples;
}

// A short-term meter's windows over 24 hours (s-short): each (start, end, peak) rising from 0 to
// its peak, idle readings at 0 between them, each run its own window.
static std::vector<std::vector<std::pair<double, double>>> TestWindows(std::vector<std::array<double, 3>> windows) {
    std::vector<std::vector<std::pair<double, double>>> segments;
    double at = 0;
    for (const auto& [start, end, peak] : windows) {
        if (start > at) segments.push_back({{at, 0}, {start - 1.0 / 6, 0}});
        segments.push_back({{start, 0}, {end, peak}});
        at = end + 1.0 / 6;
    }
    return segments;
}

// The approved mockups' accounts: OAI (OpenAI, 0x0A), CC 1 and CC 2 (Anthropic, 0xC1, 0xC2), Go
// (Antigravity, 0x60), polled 5 minutes before kWeekTo (weekly 62, 47, 93 and 30%).
static std::vector<AccountConfig> TestMockupAccounts() {
    return {TestHistoryAccount(L"openai", L"OAI", 0x0A), TestHistoryAccount(L"anthropic", L"CC 1", 0xC1),
            TestHistoryAccount(L"anthropic", L"CC 2", 0xC2), TestHistoryAccount(L"antigravity", L"Go", 0x60)};
}

// s-light and q6a-tooltip (ALL_WEEKLY: the weekly lines in both weeks, six of them through 38%
// at hour 70), with s-account's OAI credits and CC 1 extra usage, CC 2's dollars and five-hour
// windows across the week.
static std::vector<HistorySample> TestMockupWeek() {
    using Segments = std::vector<std::vector<std::pair<double, double>>>;
    Segments five;
    for (int day = 0; day < 7; day++) {
        double h = 18.0 + 24 * day;
        if (h + 5 < 168) five.push_back({{h, 0}, {h + 2, 20.0 + 9 * day}, {h + 5, 35.0 + 8 * day}});
    }
    std::vector<TestSeries> lines = {
        {0x0A, 2, 1, {{{0, 27}, {20, 41}, {40, 68}, {48, 77}}, {{50, 0}, {58, 14}, {70, 38.0}, {96, 47}, {124, 55}, {150, 60}, {168, 62}}},
         {{{0, 8}, {30, 22}, {50, 31}, {70, 37.6}, {92, 55}, {118, 70}, {140, 78}}, {{142, 0}, {168, 12}}}},
        {0x0A, 2, 7, {{{0, 2500}, {30, 2310}, {60, 1980}, {96, 1620}, {123, 1240}}}, {}},
        {0xC1, 1, 1, {{{0, 22}, {26, 38}, {44, 52}, {58, 63}}, {{60, 0}, {80, 8}, {110, 17}, {140, 31}, {168, 47}}},
         {{{0, 30}, {40, 48}, {70, 60}, {90, 66}, {118, 71}}, {{120, 0}, {150, 12}, {168, 20}}}},
        {0xC1, 1, 3, {{{0, 5}, {40, 9}, {80, 14}, {120, 19}, {150, 22}}}, {}},
        {0xC1, 1, 6, {{{0, 2.5}, {40, 4.5}, {80, 7.0}, {120, 9.5}, {150, 11.0}}}, {}},
        {0xC2, 1, 1, {{{0, 25}, {24, 30}, {48, 34}, {70, 38.3}, {96, 60}, {112, 73}, {124, 78}}, {{126, 0}, {140, 22}, {156, 52}, {166, 85}, {168, 93}}},
         {{{0, 15}, {30, 26}, {54, 32}, {70, 38.8}, {98, 66}, {124, 88}, {132, 91}}, {{134, 0}, {168, 25}}}},
        {0xC2, 1, 2, {{{0, 18}, {40, 24}, {70, 27}, {110, 45}, {124, 52}}, {{126, 0}, {150, 30}, {168, 58}}},
         {{{0, 22}, {50, 44}, {70, 54}, {110, 70}, {132, 76}}, {{134, 0}, {168, 18}}}},
        {0xC2, 1, 6, {{{0, 6.0}, {60, 7.2}, {120, 8.8}, {168, 10.0}}}, {}},
        {0xC2, 1, 0, five, {}, 5 * kHourMs},
        {0x60, 3, 1, {{{0, 20}, {40, 29}, {70, 37.4}, {110, 56}, {128, 61}}, {{130, 0}, {150, 14}, {168, 30}}},
         {{{0, 12}, {36, 25}, {70, 38.1}, {104, 52}, {130, 57}}, {{132, 0}, {168, 22}}}},
    };
    return TestPolls(lines);
}

// s-compare: CC 2's weekly lines with a lone reading in each week, a gap and a reset.
static std::vector<HistorySample> TestMockupCompare() {
    return TestPolls({
        {0xC2, 1, 1, {{{0, 55}, {20, 66}, {38, 80}}, {{40, 0}, {70, 31}, {96, 47}}, {{100, 52}}, {{104, 55}, {130, 64}, {150, 79}, {166, 90}, {168, 93}}},
         {{{0, 41}, {30, 60}, {48, 74}}, {{50, 0}, {90, 30}, {130, 62}, {160, 84}, {168, 88}}}},
        {0xC2, 1, 2, {{{0, 41}, {38, 63}}, {{40, 0}, {90, 22}, {130, 44}, {168, 58}}},
         {{{0, 30}, {48, 52}}, {{50, 0}, {58, 6}}, {{64, 11}}, {{70, 14}, {120, 37}, {168, 61}}}},
    });
}

// s-band: the weekly lines with CC 2's above 100% (112% now).
static std::vector<HistorySample> TestMockupBand() {
    std::vector<HistorySample> samples = TestMockupWeek();
    std::vector<HistorySample> band = TestPolls({{0xC2, 1, 1, {{{126, 0}, {140, 22}, {152, 61}, {160, 92}, {164, 104}, {168, 112}}}, {}}});
    for (HistorySample& sample : samples) {
        if (sample.accountId != 0xC2 || sample.timeMs < kWeekFrom + 126 * kHourMs - 200000) continue;
        auto match = std::find_if(band.begin(), band.end(), [&](const HistorySample& b) { return b.timeMs == sample.timeMs; });
        if (match != band.end()) sample.meters[1] = match->meters[1];
    }
    return samples;
}

// s-short: the accounts' short-term windows over the last 24 hours, polled every 10 minutes.
static std::vector<HistorySample> TestMockupShort() {
    ULONGLONG from = kWeekTo - kHistoryDayMs;
    std::vector<TestSeries> lines = {
        {0x0A, 2, 0, TestWindows({{1.5, 6.5, 41}, {9, 14, 64}, {19.5, 24, 34}}), {}, 5 * kHourMs},
        {0xC1, 1, 0, TestWindows({{3, 8, 28}, {12.5, 17.5, 52}, {21, 24, 18}}), {}, 5 * kHourMs},
        {0xC2, 1, 0, TestWindows({{0.5, 5.5, 72}, {8, 13, 88}, {20.5, 24, 12}}), {}, 5 * kHourMs},
        {0x60, 3, 0, TestWindows({{5, 10, 23}, {15, 20, 46}, {22, 24, 8}}), {}, 5 * kHourMs},
    };
    std::vector<HistorySample> samples = TestPolls(lines, from, 1.0 / 6);
    // Go's Claude/GPT short-term windows vary in length: "varying windows".
    std::vector<HistorySample> other = TestPolls({{0x60, 3, 4, TestWindows({{6, 9, 35}, {14, 19, 61}, {21.5, 24, 22}}), {}}},
                                                 from, 1.0 / 6);
    for (HistorySample& sample : samples) {
        if (sample.accountId != 0x60) continue;
        for (const HistorySample& o : other) {
            if (o.timeMs != sample.timeMs) continue;
            sample.meters[4] = o.meters[4];
            double hours = double(sample.timeMs - from) / double(kHourMs);
            sample.meters[4].durationMs = hours < 12 ? 3 * kHourMs : 5 * kHourMs;
        }
    }
    return samples;
}

// s-ticks: two weekly lines over `days` days, resetting weekly, polled every three hours.
static std::vector<HistorySample> TestMockupLong(int days) {
    std::vector<TestSeries> lines = {{0x0A, 2, 1}, {0xC2, 1, 1}};
    for (size_t l = 0; l < lines.size(); l++) {
        for (int week = 0; week * 168 < days * 24; week++) {
            double start = week * 168.0 + (l ? 40 : 110) - 168, end = start + 165;
            double peak = 35 + double((week * 37 + int(l) * 11) % 60);
            start = std::max(start, 0.0);
            end = std::min(end, days * 24.0);
            if (end - start >= 3) lines[l].current.push_back({{start, 0}, {end, peak * (end - start) / 165}});
        }
    }
    return TestPolls(lines, kWeekTo - ULONGLONG(days) * kHistoryDayMs, 3);
}

// A window's controller over a journal: the mockups' accounts (unless given), loaded at kWeekTo,
// then `setup` (its commands).
struct TestChartWindow {
    TestHistoryOps ops;
    HistoryController controller{ops};
};

static std::shared_ptr<TestChartWindow> TestChartOf(std::vector<HistorySample> journal,
                                                    const std::function<void(HistoryController&)>& setup = {},
                                                    std::vector<AccountConfig> accounts = TestMockupAccounts()) {
    auto window = std::make_shared<TestChartWindow>();
    window->ops.settings.settings.accounts = std::move(accounts);
    for (double pct : {62.0, 47.0, 93.0, 30.0}) window->ops.settings.polls.push_back(TestPoll(pct, kWeekTo - 5 * kMinute));
    window->ops.journal = std::move(journal);
    window->controller.Open();
    window->controller.Load();
    if (setup) setup(window->controller);
    return window;
}

// The mockups' chart item: 774 x 300 DIP at (27, 59) in the content.
static constexpr D2D1_RECT_F kTestChartRect{27, 59, 801, 359};

static HistoryChart& TestLaid(TestChartWindow& w, D2D1_RECT_F rect = kTestChartRect, float scale = 1) {
    w.controller.LayChart(TestGraphics(), rect, scale);
    return w.controller.chart;
}

// A reduced point's position, and the hit list's (line, period, point) triples.
static D2D1_POINT_2F TestAt(const HistoryChart& c, size_t line, int period, size_t point) {
    return HistoryChartPoint(c, c.lines[line], c.render[line][size_t(period)].points[point]);
}

static D2D1_POINT_2F TestBetween(D2D1_POINT_2F a, D2D1_POINT_2F b, float fraction) {
    return {a.x + (b.x - a.x) * fraction, a.y + (b.y - a.y) * fraction};
}

// A time as the readout prints it at kWeekTo in the test zone.
static std::wstring TestTime(ULONGLONG ms) {
    return HistoryUiTime(ms, kWeekTo, TestZone());
}

// A time zone one hour east of UTC, two in summer: daylight saving from 01:00 UTC on 29 March to
// 01:00 UTC on 25 October 2026.
static HistoryUiLocalTime TestDstZone() {
    ULONGLONG spring = TestLocalMs(2026, 3, 29, 1, 0, 0), fall = TestLocalMs(2026, 10, 25, 1, 0, 0);
    return [=](ULONGLONG ms) { return TestZone(ms >= spring && ms < fall ? 120 : 60)(ms); };
}

static std::vector<std::wstring> TestLabels(const std::vector<HistoryChartTick>& ticks) {
    std::vector<std::wstring> labels;
    for (const HistoryChartTick& tick : ticks) labels.push_back(tick.label);
    return labels;
}

static std::vector<const HistoryChartPrim*> TestMarks(const HistoryChartScene& scene, HistoryChartMark mark) {
    std::vector<const HistoryChartPrim*> found;
    for (const HistoryChartPrim& prim : scene.prims) {
        if (prim.mark == mark) found.push_back(&prim);
    }
    return found;
}

static bool TestLabeled(const HistoryChartScene& scene, std::wstring_view text) {
    return std::any_of(scene.prims.begin(), scene.prims.end(), [&](const HistoryChartPrim& prim) {
        return prim.mark == HistoryChartMark::Label && prim.text == text;
    });
}

static bool TestSameColor(D2D1_COLOR_F a, D2D1_COLOR_F b) {
    return UiColorKey(a) == UiColorKey(b);
}

static UiPalette TestPalette(UiThemeKind kind) {
    UiThemeInput theme;
    theme.kind = kind;
    theme.systemLight = kind == UiThemeKind::Light;
    return UiPaletteFor(theme);
}

static void RunHistoryChartTests() {
    // Axes and layout -------------------------------------------------------------------------

    Run("history chart: axes over the visible lines: percent always, 0 to 100 or the next 25 above; extra "
        "axes while a line shows, up to 1-8 times a power of ten; labels rounded without trailing zeros", [] {
        auto line = [](int meter, double low, double high, bool hidden = false) {
            HistoryUiLine l;
            l.meter = meter;
            l.low = low;
            l.high = high;
            l.hidden = hidden;
            return l;
        };
        std::array<HistoryChartAxis, 3> axes = HistoryChartAxes({}, false);
        CHECK(axes[0].visible && axes[0].low == 0 && axes[0].high == 100 && !axes[1].visible && !axes[2].visible);
        CHECK((axes[0].values == std::vector<double>{0, 100, 50}));
        // s-band: 112% -> 125; s-account: 2500 credits -> 3000; hidden dollars leave their axis.
        axes = HistoryChartAxes({line(1, 0, 112), line(7, 0, 2500), line(6, 0, 11, true)}, false);
        CHECK(axes[0].high == 125 && (axes[0].values == std::vector<double>{0, 125, 100, 50}));
        CHECK(!axes[1].visible && axes[2].visible && axes[2].high == 3000 &&
              (axes[2].values == std::vector<double>{0, 1500, 3000}));
        // A hidden percent line leaves the axis's range, never the axis.
        axes = HistoryChartAxes({line(1, 0, 112, true), line(6, 0, 11), line(5, -3, 40)}, false);
        CHECK(axes[0].visible && axes[0].high == 100 && axes[0].low == -25 &&
              (axes[0].values == std::vector<double>{0, -25, 100, 50}));
        CHECK(axes[1].visible && axes[1].high == 20 && (axes[1].values == std::vector<double>{0, 10, 20}));
        CHECK(HistoryChartAxes({line(6, 0, 0.3)}, false)[1].high == 1);
        CHECK(HistoryChartAxes({line(7, 0, 4100)}, false)[2].high == 5000);
        CHECK(HistoryChartAxes({line(7, 0, 100)}, false)[2].high == 100);
        CHECK(HistoryChartAxes({line(7, 0, 100.0001)}, false)[2].high == 200);
        CHECK(HistoryChartAxes({line(7, -700, 10)}, false)[2].low == -800);
        CHECK(HistoryChartAxisLabel(0, 50) == L"50%" && HistoryChartAxisLabel(0, -25) == L"-25%");
        CHECK(HistoryChartAxisLabel(1, 0.5) == L"0.5 USD" && HistoryChartAxisLabel(1, 1250) == L"1250 USD");
        CHECK(HistoryChartAxisLabel(1, 0.75) == L"0.75 USD" && HistoryChartAxisLabel(2, 0) == L"0 cr");
        CHECK(HistoryChartAxisLabel(2, 12500) == L"12.5K cr" && HistoryChartAxisLabel(2, 10000) == L"10K cr");
    });

    Run("history chart: the plot in the chart's item (mockup: 44 DIP left, 10 above, 26 below and right, or the "
        "extra axes' label columns), on device pixels; percent gridlines 14 DIP apart", [] {
        UiGraphics& g = TestGraphics();
        auto width = [&](const std::wstring& text) {
            return std::ceil(UiLayoutSize(UiLayout(g, text, kHistoryChartAxisStyle, 10000, false)).width);
        };
        HistoryChart c;
        c.axes = HistoryChartAxes({}, false);
        HistoryChartLayout(g, c, kTestChartRect, 1);
        CHECK(TestSameRect(c.plot, {71, 69, 775, 333}) && c.width == 704 && c.scale == 1);
        HistoryUiLine credits;
        credits.meter = 7;
        credits.high = 2500;
        HistoryUiLine dollars = credits;
        dollars.meter = 6;
        dollars.high = 11;
        c.axes = HistoryChartAxes({credits}, false);
        HistoryChartLayout(g, c, kTestChartRect, 1);
        CHECK(c.columns[2] == width(L"3000 cr") && c.columns[1] == 0);
        CHECK_NEAR(c.plot.right, 801 - 8 - width(L"3000 cr") - 12);
        c.axes = HistoryChartAxes({credits, dollars}, false);
        HistoryChartLayout(g, c, kTestChartRect, 1);
        CHECK(c.columns[1] == width(L"20 USD"));
        CHECK_NEAR(c.plot.right, 801 - 8 - width(L"20 USD") - 8 - width(L"3000 cr") - 12);
        // 150%: every edge on a device pixel; the reduction's width in pixels.
        c.axes = HistoryChartAxes({}, false);
        HistoryChartLayout(g, c, kTestChartRect, 1.5f);
        for (float edge : {c.plot.left, c.plot.top, c.plot.right, c.plot.bottom}) {
            CHECK(std::abs(edge * 1.5 - std::round(edge * 1.5)) < 1e-3);
        }
        CHECK(c.width == 1056 && c.scale == 1.5f);
        // Up to 1000%: 50 is too close to 0 (13.2 DIP in a 264 DIP plot), 100 isn't.
        HistoryUiLine high;
        high.high = 1000;
        c.axes = HistoryChartAxes({high}, false);
        HistoryChartLayout(g, c, kTestChartRect, 1);
        CHECK((c.axes[0].values == std::vector<double>{0, 100, 1000}));
        CHECK(HistoryChartY(c.axes[0], c.plot, 1000) == c.plot.top && HistoryChartY(c.axes[0], c.plot, 0) == c.plot.bottom);
        // A chart narrower than its label columns: no plot width, nothing reduced or picked.
        auto w = TestChartOf(TestMockupWeek());
        HistoryChart& narrow = TestLaid(*w, {27, 59, 87, 359});
        CHECK(narrow.width == 0 && narrow.plot.right == narrow.plot.left && narrow.ticks.empty());
        CHECK(std::all_of(narrow.render.begin(), narrow.render.end(), [](const auto& r) { return r[0].points.empty(); }));
        CHECK(HistoryChartPick(narrow, {narrow.plot.left, (narrow.plot.top + narrow.plot.bottom) / 2}).empty());
        CHECK(TestMarks(HistoryChartBuildScene(narrow, {}, TestPalette(UiThemeKind::Dark)), HistoryChartMark::Run).empty());
    });

    // Ticks ----------------------------------------------------------------------------------

    Run("history chart: time ticks per range at local boundaries (s-ticks): hours, weekdays, dates on Mondays, "
        "months; the smallest step keeping labels apart", [] {
        HistoryUiLocalTime zone = TestZone();
        ULONGLONG to = kWeekTo;
        // 24 hours over 704 DIP: every second hour; midnight shows the day.
        std::vector<HistoryChartTick> ticks = HistoryChartTicks(to - kHistoryDayMs, to, 704, zone);
        CHECK((TestLabels(ticks) == std::vector<std::wstring>{L"16:00", L"18:00", L"20:00", L"22:00", L"Sat 31", L"02:00",
                                                              L"04:00", L"06:00", L"08:00", L"10:00", L"12:00", L"14:00"}));
        CHECK(ticks[0].time == TestLocalMs(2026, 10, 30, 16, 0) && ticks[4].time == TestLocalMs(2026, 10, 31, 0, 0));
        // Every hour when wide enough, every sixth when narrow.
        CHECK(HistoryChartTicks(to - kHistoryDayMs, to, 2000, zone).size() == 24);
        CHECK((TestLabels(HistoryChartTicks(to - kHistoryDayMs, to, 300, zone)) ==
               std::vector<std::wstring>{L"18:00", L"Sat 31", L"06:00", L"12:00"}));
        // 7 days: each local midnight, `Sun 25`.
        ticks = HistoryChartTicks(kWeekFrom, to, 704, zone);
        CHECK((TestLabels(ticks) == std::vector<std::wstring>{L"Sun 25", L"Mon 26", L"Tue 27", L"Wed 28", L"Thu 29",
                                                              L"Fri 30", L"Sat 31"}));
        CHECK(ticks[0].time == TestLocalMs(2026, 10, 25, 0, 0) && ticks[6].time == TestLocalMs(2026, 10, 31, 0, 0));
        // 30 days: Mondays; 90 days: every other Monday (by the weeks since 1970).
        CHECK((TestLabels(HistoryChartTicks(to - 30 * kHistoryDayMs, to, 704, zone)) ==
               std::vector<std::wstring>{L"Oct 5", L"Oct 12", L"Oct 19", L"Oct 26"}));
        CHECK((TestLabels(HistoryChartTicks(to - 90 * kHistoryDayMs, to, 704, zone)) ==
               std::vector<std::wstring>{L"Aug 3", L"Aug 17", L"Aug 31", L"Sep 14", L"Sep 28", L"Oct 12", L"Oct 26"}));
        CHECK(HistoryChartTicks(to - 90 * kHistoryDayMs, to, 704, zone)[0].time == TestLocalMs(2026, 8, 3, 0, 0));
        // All retained, 900 days: quarters; 3650 days over a narrow plot: every other January.
        ticks = HistoryChartTicks(to - 900 * kHistoryDayMs, to, 704, zone);
        CHECK((TestLabels(ticks) == std::vector<std::wstring>{L"Jul 2024", L"Oct 2024", L"Jan 2025", L"Apr 2025",
                                                              L"Jul 2025", L"Oct 2025", L"Jan 2026", L"Apr 2026",
                                                              L"Jul 2026", L"Oct 2026"}));
        CHECK(ticks[2].time == TestLocalMs(2025, 1, 1, 0, 0));
        CHECK((TestLabels(HistoryChartTicks(to - 3650 * kHistoryDayMs, to, 410, zone)) ==
               std::vector<std::wstring>{L"Jan 2018", L"Jan 2020", L"Jan 2022", L"Jan 2024", L"Jan 2026"}));
        CHECK(HistoryChartTicks(to - 3650 * kHistoryDayMs, to, 704, zone).size() == 10);  // Each January, 2017 to 2026
        // No clock, no ticks.
        CHECK(HistoryChartTicks(kWeekFrom, to, 704, [](ULONGLONG) { return SYSTEMTIME{}; }).empty());
    });

    Run("history chart: ticks across daylight saving: the skipped hour gets none, the repeated one one; each "
        "local midnight once, 23 or 25 hours apart", [] {
        HistoryUiLocalTime zone = TestDstZone();
        // Spring forward: 02:00 local never happens on 29 March.
        ULONGLONG from = TestLocalMs(2026, 3, 28, 18, 0, 60);
        std::vector<HistoryChartTick> ticks = HistoryChartTicks(from, from + kHistoryDayMs, 4000, zone);
        std::vector<std::wstring> spring = {L"19:00", L"20:00", L"21:00", L"22:00", L"23:00", L"Sun 29", L"01:00"};
        for (int hour = 3; hour <= 18; hour++) spring.push_back((hour < 10 ? L"0" : L"") + std::to_wstring(hour) + L":00");
        CHECK(TestLabels(ticks) == spring);
        CHECK(ticks[6].time == TestLocalMs(2026, 3, 29, 1, 0, 60) && ticks[7].time == TestLocalMs(2026, 3, 29, 1, 0, 0) &&
              ticks[8].time == TestLocalMs(2026, 3, 29, 4, 0, 120));
        // Fall back: 02:00 local happens twice on 25 October; one tick, at the first.
        from = TestLocalMs(2026, 10, 24, 18, 0, 120);
        ticks = HistoryChartTicks(from, from + 25 * kHourMs, 4000, zone);
        std::vector<std::wstring> fall = {L"19:00", L"20:00", L"21:00", L"22:00", L"23:00", L"Sun 25", L"01:00", L"02:00"};
        for (int hour = 3; hour <= 17; hour++) fall.push_back((hour < 10 ? L"0" : L"") + std::to_wstring(hour) + L":00");
        CHECK(TestLabels(ticks) == fall);
        CHECK(ticks[7].time == TestLocalMs(2026, 10, 25, 2, 0, 120) && ticks[8].time == TestLocalMs(2026, 10, 25, 3, 0, 60) &&
              ticks[8].time - ticks[7].time == 2 * kHourMs);
        // Days across each change: every local midnight once.
        ticks = HistoryChartTicks(TestLocalMs(2026, 3, 26, 12, 0, 60), TestLocalMs(2026, 3, 26, 12, 0, 60) + kHistoryWeekMs,
                                  704, zone);
        CHECK((TestLabels(ticks) == std::vector<std::wstring>{L"Fri 27", L"Sat 28", L"Sun 29", L"Mon 30", L"Tue 31",
                                                              L"Wed 1", L"Thu 2"}));
        CHECK(ticks[2].time == TestLocalMs(2026, 3, 29, 0, 0, 60) && ticks[3].time - ticks[2].time == 23 * kHourMs);
        ticks = HistoryChartTicks(TestLocalMs(2026, 10, 22, 12, 0, 120), TestLocalMs(2026, 10, 22, 12, 0, 120) + kHistoryWeekMs,
                                  704, zone);
        CHECK((TestLabels(ticks) == std::vector<std::wstring>{L"Fri 23", L"Sat 24", L"Sun 25", L"Mon 26", L"Tue 27",
                                                              L"Wed 28", L"Thu 29"}));
        CHECK(ticks[3].time - ticks[2].time == 25 * kHourMs);
        // Months over ten years in this zone: Januaries at local midnight.
        ticks = HistoryChartTicks(kWeekTo - 3650 * kHistoryDayMs, kWeekTo, 704, zone);
        CHECK(ticks.size() == 10 && ticks[0].label == L"Jan 2017" && ticks[0].time == TestLocalMs(2017, 1, 1, 0, 0, 60));
    });

    // Picking and readout -----------------------------------------------------------------------

    Run("history chart: every coincident or nearby line in both periods, at its recorded endpoint; readout rows "
        "by chip name, rounded, compact local times", [] {
        // Three OpenAI accounts at 50, 50 and 53% an hour and five days into each week, comparing.
        std::vector<AccountConfig> accounts;
        std::vector<HistorySample> journal;
        for (uint64_t id : {1, 2, 3}) {
            accounts.push_back(TestHistoryAccount(L"openai", std::to_wstring(id), id));
            for (ULONGLONG start : {kWeekFrom - kHistoryWeekMs, kWeekFrom}) {
                for (ULONGLONG offset : {kHourMs, 5 * kHistoryDayMs}) {
                    HistorySample sample = TestReading(id, 2, start + offset, {{1, id == 3 ? 53.0 : 50.0}});
                    sample.intervalMs = 3 * kHistoryDayMs;
                    journal.push_back(sample);
                }
            }
        }
        auto w = TestChartOf(journal, [](HistoryController& c) { c.Command(HistoryUiCommand::Compare, 1); }, accounts);
        // A 200 DIP chart (a 164 DIP plot): 53% lies 4.9 DIP from 50%, within 5 DIP and half a line;
        // the same in DIPs at 150 and 200%, reduced to their device pixels.
        for (float scale : {1.5f, 2.0f, 1.0f}) {
            HistoryChart& c = TestLaid(*w, {27, 59, 801, 259}, scale);
            CHECK(c.lines.size() == 3 && c.compare && c.width == int(std::lround((c.plot.right - c.plot.left) * scale)));
            std::vector<HistoryChartHit> hits = HistoryChartPick(c, TestBetween(TestAt(c, 0, 0, 0), TestAt(c, 0, 0, 1), 0.25f));
            CHECK(hits.size() == 6);
            for (size_t i = 0; i < hits.size(); i++) {
                CHECK(hits[i].line == i / 2 && hits[i].period == int(i % 2) && hits[i].point == 0);
            }
        }
        HistoryChart& c = w->controller.chart;
        D2D1_POINT_2F at = TestBetween(TestAt(c, 0, 0, 0), TestAt(c, 0, 0, 1), 0.25f);
        UiTooltipContent readout;
        CHECK(w->controller.Hover(at, readout, TestPalette(UiThemeKind::Dark)));
        CHECK((w->controller.picked == std::vector<size_t>{0, 1, 2}) && readout.key && readout.rows.size() == 6);
        std::vector<std::wstring> rows;
        for (const UiReadoutRow& row : readout.rows) rows.push_back(row.title + L" | " + UiRunsText(row.detail));
        CHECK((rows == std::vector<std::wstring>{
                   L"1 \u00B7 Weekly | [Current] 50.0% \u00B7 Sat 24 Oct 15:32",
                   L"1 \u00B7 Weekly | [Previous week] 50.0% \u00B7 Sat 17 Oct 15:32",
                   L"2 \u00B7 Weekly | [Current] 50.0% \u00B7 Sat 24 Oct 15:32",
                   L"2 \u00B7 Weekly | [Previous week] 50.0% \u00B7 Sat 17 Oct 15:32",
                   L"3 \u00B7 Weekly | [Current] 53.0% \u00B7 Sat 24 Oct 15:32",
                   L"3 \u00B7 Weekly | [Previous week] 53.0% \u00B7 Sat 17 Oct 15:32"}));
        CHECK(readout.rows[0].stroke == 3 && readout.more == L" \u2014 hide lines in the legend or select one account");
        // The same point again: no emphasis change, and the shown readout (its key, no rows) stays.
        UiTooltipContent shown;
        shown.key = readout.key;
        CHECK(!w->controller.Hover(at, shown, TestPalette(UiThemeKind::Dark)) && shown.rows.empty());
        // Further along: the far endpoints, raw times five days in.
        at = TestBetween(TestAt(c, 0, 0, 0), TestAt(c, 0, 0, 1), 0.8f);
        CHECK(!w->controller.Hover(at, shown, TestPalette(UiThemeKind::Dark)) && shown.key != readout.key);
        CHECK(UiRunsText(shown.rows[0].detail) == L"[Current] 50.0% \u00B7 Thu 29 Oct 14:32" &&
              UiRunsText(shown.rows[1].detail) == L"[Previous week] 50.0% \u00B7 Thu 22 Oct 14:32");
        // The hover ends: no emphasis, no readout.
        CHECK(w->controller.Hover(std::nullopt, shown, TestPalette(UiThemeKind::Dark)) && !shown.key &&
              w->controller.picked.empty());
        // Once the window closes, or the mod unloads, the pointer picks nothing.
        for (bool unload : {false, true}) {
            auto closing = TestChartOf(journal, [](HistoryController& h) { h.Command(HistoryUiCommand::Compare, 1); }, accounts);
            HistoryChart& laid = TestLaid(*closing, {27, 59, 801, 259});
            if (unload) closing->ops.unloading = true;
            else closing->controller.RequestClose();
            UiTooltipContent none;
            CHECK(!closing->controller.Hover(TestAt(laid, 0, 0, 0), none, TestPalette(UiThemeKind::Dark)) && !none.key &&
                  closing->controller.picked.empty());
        }
    });

    Run("history chart: a sloping segment reports its nearest recorded endpoint, never an interpolated value; "
        "nothing off the line or outside the plot", [] {
        HistorySample a = TestReading(0x0A, 2, kWeekTo - kHistoryDayMs + kHourMs, {{1, 20}});
        HistorySample b = TestReading(0x0A, 2, kWeekTo - kHistoryDayMs + 7 * kHourMs, {{1, 80}});
        a.intervalMs = b.intervalMs = 6 * kHourMs;
        auto w = TestChartOf({a, b}, [](HistoryController& c) { c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day)); });
        HistoryChart& c = TestLaid(*w);
        CHECK(c.lines.size() == 1 && c.render[0][0].points.size() == 2 && c.render[0][0].points[1].connect);
        for (auto [fraction, value, time] : {std::tuple{0.25f, L"20.0%", a.timeMs}, std::tuple{0.75f, L"80.0%", b.timeMs}}) {
            UiTooltipContent readout;
            w->controller.Hover(TestBetween(TestAt(c, 0, 0, 0), TestAt(c, 0, 0, 1), fraction), readout,
                                TestPalette(UiThemeKind::Dark));
            CHECK(readout.rows.size() == 1 &&
                  UiRunsText(readout.rows[0].detail) == std::wstring(L"[Current] ") + value + L" \u00B7 " + TestTime(time));
        }
        D2D1_POINT_2F off = TestBetween(TestAt(c, 0, 0, 0), TestAt(c, 0, 0, 1), 0.5f);
        off.y += 20;
        CHECK(HistoryChartPick(c, off).empty());
        CHECK(HistoryChartPick(c, {c.plot.left - 1, c.plot.top}).empty() &&
              HistoryChartPick(c, {c.plot.right + 1, c.plot.top}).empty() &&
              HistoryChartPick(c, {c.plot.left, c.plot.top - 1}).empty() &&
              HistoryChartPick(c, {c.plot.left, c.plot.bottom + 1}).empty());
    });

    Run("history chart: readings sharing a pixel keep their extremes; a pick reports the recorded sample, not the "
        "pixel's", [] {
        std::vector<HistorySample> journal;
        const double values[] = {20, 30, 5, 100, 40, 60};
        for (size_t i = 0; i < std::size(values); i++) {
            HistorySample sample = TestReading(0x0A, 2, kWeekTo - kHistoryDayMs + kHourMs + i * 1000, {{1, values[i]}});
            sample.intervalMs = 1000;
            journal.push_back(sample);
        }
        auto w = TestChartOf(journal, [](HistoryController& c) { c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day)); });
        HistoryChart& c = TestLaid(*w);
        const std::vector<HistoryRenderPoint>& points = c.render[0][0].points;
        CHECK(points.size() == 4 && points[0].x == points[3].x);
        std::vector<HistoryChartHit> hits = HistoryChartPick(c, TestAt(c, 0, 0, 1));
        CHECK(hits.size() == 1 && hits[0].point == 1);
        const HistorySample& sample = c.snapshot->samples[points[1].sourceRow];
        CHECK(sample.meters[1].value == 5 && sample.timeMs == journal[2].timeMs);
        UiTooltipContent readout;
        w->controller.Hover(TestAt(c, 0, 0, 1), readout, TestPalette(UiThemeKind::Dark));
        CHECK(UiRunsText(readout.rows[0].detail) == L"[Current] 5.0% \u00B7 " + TestTime(journal[2].timeMs));
    });

    Run("history chart: lone readings in both periods pick both; without comparing only the current one; "
        "details-only dollars plot and pick nothing", [] {
        std::vector<HistorySample> journal = {TestReading(0x0A, 2, kWeekFrom - kHistoryWeekMs + kHourMs, {{1, 50}}),
                                              TestReading(0x0A, 2, kWeekFrom + kHourMs, {{1, 50}})};
        auto w = TestChartOf(journal, [](HistoryController& c) { c.Command(HistoryUiCommand::Compare, 1); });
        HistoryChart& c = TestLaid(*w);
        D2D1_POINT_2F at = TestAt(c, 0, 0, 0);
        std::vector<HistoryChartHit> hits = HistoryChartPick(c, at);
        CHECK(hits.size() == 2 && hits[0].period == 0 && hits[1].period == 1);
        CHECK(HistoryChartPick(c, {at.x + 20, at.y}).empty());
        w->controller.Command(HistoryUiCommand::Compare, 0);
        HistoryChart& current = TestLaid(*w);
        hits = HistoryChartPick(current, TestAt(current, 0, 0, 0));
        CHECK(hits.size() == 1 && hits[0].period == 0);
        auto usd = TestChartOf({TestReading(0xC1, 1, kWeekFrom + kHourMs, {{6, 20}})}, [](HistoryController& c) {
            c.Command(HistoryUiCommand::View, int(HistoryView::ExtraUsage));
        });
        HistoryChart& dollars = TestLaid(*usd);
        CHECK(dollars.lines.empty() && usd->controller.view.cards.size() == 1);
        CHECK(HistoryChartPick(dollars, {(dollars.plot.left + dollars.plot.right) / 2, dollars.plot.bottom}).empty());
        CHECK(dollars.message == L"No percentage readings to plot. Dollar spending is shown below.");
    });

    Run("history chart: a break is never a segment: its interior picks nothing, each reading itself", [] {
        for (int kind = 0; kind < 4; kind++) {
            HistorySample a = TestReading(0x0A, 2, kWeekFrom + kHourMs, {{1, 50}});
            HistorySample b = TestReading(0x0A, 2, kWeekFrom + 13 * kHourMs, {{1, 50}});
            a.intervalMs = b.intervalMs = 12 * kHourMs;
            std::vector<HistorySample> journal = {a, b};
            if (kind == 0) journal[1].session++;
            if (kind == 1) journal[1].meters[1].durationMs = kHourMs;
            if (kind == 2) journal[0].meters[1].resetMs = journal[1].meters[1].resetMs = kWeekFrom + 7 * kHourMs;
            if (kind == 3) {
                HistorySample failed = TestReading(0x0A, 2, kWeekFrom + 7 * kHourMs, {{1, 50}});
                failed.intervalMs = 12 * kHourMs;
                failed.failed = 1;
                journal.insert(journal.begin() + 1, failed);
            }
            auto w = TestChartOf(journal);
            HistoryChart& c = TestLaid(*w);
            CHECK(c.render[0][0].points.size() == 2 && !c.render[0][0].points[1].connect);
            CHECK(HistoryChartPick(c, TestBetween(TestAt(c, 0, 0, 0), TestAt(c, 0, 0, 1), 0.5f)).empty());
            for (size_t point : {size_t(0), size_t(1)}) CHECK(HistoryChartPick(c, TestAt(c, 0, 0, point)).size() == 1);
            HistoryChartScene scene = HistoryChartBuildScene(c, {}, TestPalette(UiThemeKind::Dark));
            CHECK(TestMarks(scene, HistoryChartMark::Run).empty() && TestMarks(scene, HistoryChartMark::Dot).size() == 2);
        }
    });

    Run("history chart: unlike units on their own axes, one projection for painting and picking; finite extreme "
        "ranges", [] {
        // All accounts, All meters: 25 to 100%, 250 to 1000 USD, 2500 to 10,000 credits, each a quarter up then at
        // the top of its axis.
        HistorySample anthropic = TestReading(0xC1, 1, kWeekFrom + kHourMs, {{3, 25}, {6, 250}});
        HistorySample anthropic2 = TestReading(0xC1, 1, kWeekFrom + 2 * kHourMs, {{3, 100}, {6, 1000}});
        HistorySample openai = TestReading(0x0A, 2, kWeekFrom + kHourMs, {{7, 2500}});
        HistorySample openai2 = TestReading(0x0A, 2, kWeekFrom + 2 * kHourMs, {{7, 10000}});
        auto w = TestChartOf({anthropic, openai, anthropic2, openai2},
                             [](HistoryController& c) { c.Command(HistoryUiCommand::View, int(HistoryView::AllMeters)); });
        HistoryChart& c = TestLaid(*w);
        CHECK(c.lines.size() == 3 && c.axes[1].high == 1000 && c.axes[2].high == 10000 && c.axes[0].high == 100);
        for (size_t line = 0; line < 3; line++) {
            CHECK_NEAR(TestAt(c, line, 0, 0).y, c.plot.bottom - (c.plot.bottom - c.plot.top) / 4);
            CHECK_NEAR(TestAt(c, line, 0, 1).y, c.plot.top);
        }
        UiTooltipContent readout;
        w->controller.Hover(TestAt(c, 0, 0, 0), readout, TestPalette(UiThemeKind::Dark));
        CHECK(readout.rows.size() == 3);
        std::wstring all;
        for (const UiReadoutRow& row : readout.rows) all += UiRunsText(row.detail) + L"|";
        CHECK(all.find(L" 25.0% ") != std::wstring::npos && all.find(L" USD 250.00 ") != std::wstring::npos &&
              all.find(L" 2500.00 credits ") != std::wstring::npos);
        HistoryChartScene scene = HistoryChartBuildScene(c, {}, TestPalette(UiThemeKind::Dark));
        for (PCWSTR label : {L"100%", L"0 USD", L"500 USD", L"1000 USD", L"0 cr", L"5000 cr", L"10K cr"}) {
            CHECK(TestLabeled(scene, label));
        }
        CHECK(!TestLabeled(scene, L"No readings in this range."));
        // Percent labels end 8 DIP before the plot; the USD column starts 8 DIP after it, credits'
        // 8 DIP after USD's; each axis's labels share their gridline heights' order.
        for (const HistoryChartPrim* label : TestMarks(scene, HistoryChartMark::Label)) {
            if (label->text.ends_with(L"%") && label->text != L"90%") {
                CHECK(label->rect.right == c.plot.left - 8 && label->align == DWRITE_TEXT_ALIGNMENT_TRAILING);
            } else if (label->text.ends_with(L" USD")) {
                CHECK(label->rect.left == c.plot.right + 8 && label->rect.right == c.plot.right + 8 + c.columns[1]);
            } else if (label->text.ends_with(L" cr")) {
                CHECK(label->rect.left == c.plot.right + 8 + c.columns[1] + 8);
            }
        }
        // Extremes: the whole finite range, its middle in the middle; each reading picked.
        for (int meter : {1, 6, 7}) {
            double max = std::numeric_limits<double>::max();
            std::vector<HistorySample> journal;
            double values[] = {-max, 0, max};
            for (int i = 0; i < 3; i++) {
                HistorySample sample = TestReading(meter == 7 ? 0x0A : 0xC1, meter == 7 ? 2 : 1,
                                                   kWeekFrom + ULONGLONG(i + 1) * kHourMs, {{meter, values[i]}});
                journal.push_back(sample);
            }
            auto extreme = TestChartOf(journal, [&](HistoryController& h) {
                h.Command(HistoryUiCommand::Select, 0, meter == 7 ? 0x0A : 0xC1);
                h.Command(HistoryUiCommand::View, int(HistoryView::AllMeters));
            });
            HistoryChart& e = TestLaid(*extreme);
            CHECK(e.lines.size() == 1 && e.render[0][0].points.size() == 3);
            CHECK_NEAR(TestAt(e, 0, 0, 0).y, e.plot.bottom);
            CHECK_NEAR(TestAt(e, 0, 0, 1).y, (e.plot.top + e.plot.bottom) / 2);
            CHECK_NEAR(TestAt(e, 0, 0, 2).y, e.plot.top);
            for (size_t point = 0; point < 3; point++) CHECK(HistoryChartPick(e, TestAt(e, 0, 0, point)).size() == 1);
            HistoryChartAxis percent{true, 0, 100, {}};
            CHECK(HistoryChartY(percent, e.plot, -max) == e.plot.bottom && HistoryChartY(percent, e.plot, max) == e.plot.top);
        }
    });

    Run("history chart: readout rows name removed accounts by their journal IDs and one account's lines without "
        "the account, round values, print a previous year's times without the weekday and the hint by selection; "
        "high contrast swatches", [] {
        HistorySample removed = TestReading(0xDEAD, 1, kWeekFrom + kHourMs, {{1, 20.5}});
        HistorySample go = TestReading(0x60, 3, kWeekFrom + kHourMs, {{1, 19.999999999999996}});
        auto w = TestChartOf({removed, go});
        HistoryChart& c = TestLaid(*w);
        CHECK(c.lines.size() == 2 && c.lines[0].name == L"Go \u00B7 Gemini weekly" &&
              c.lines[1].name == L"account_000000000000dead \u00B7 Weekly" &&
              c.lines[1].title == L"account_000000000000dead \u00B7 Weekly used (%)");
        UiTooltipContent readout;
        w->controller.Hover(TestAt(c, 0, 0, 0), readout, TestPalette(UiThemeKind::HighContrast));
        CHECK(readout.rows.size() == 2 && readout.rows[0].title == L"Go \u00B7 Gemini weekly" &&
              UiRunsText(readout.rows[0].detail) == L"[Current] 20.0% \u00B7 Sat 24 Oct 15:32");
        UiPalette hc = TestPalette(UiThemeKind::HighContrast);
        CHECK(TestSameColor(readout.rows[0].color, hc.text) && TestSameColor(readout.rows[1].color, hc.link));
        // One account: its chip names drop the account, its full titles keep it; the hint drops
        // "select one account".
        w->controller.Command(HistoryUiCommand::Select, 0, 0x60);
        HistoryChart& one = TestLaid(*w);
        CHECK(one.lines.size() == 1 && one.lines[0].name == L"Gemini weekly" &&
              one.lines[0].title == L"Go \u00B7 Gemini weekly used (%)");
        w->controller.Hover(TestAt(one, 0, 0, 0), readout, TestPalette(UiThemeKind::Dark));
        CHECK(readout.rows.size() == 1 && readout.rows[0].title == L"Gemini weekly" &&
              readout.more == L" \u2014 hide lines in the legend");
        CHECK(TestSameColor(readout.rows[0].color, HistoryUiHue(kHistoryColors[1], false)));
        // A reading of last year (All retained over 900 days).
        HistorySample old = TestReading(0x0A, 2, TestLocalMs(2025, 12, 17, 9, 12), {{1, 71}});
        auto year = TestChartOf({old}, [](HistoryController& h) { h.Command(HistoryUiCommand::Range, int(HistoryUiRange::All)); });
        year->ops.settings.settings.historyRetentionDays = 900;
        year->controller.Load();
        HistoryChart& y = TestLaid(*year);
        year->controller.Hover(TestAt(y, 0, 0, 0), readout, TestPalette(UiThemeKind::Dark));
        CHECK(UiRunsText(readout.rows[0].detail) == L"[Current] 71.0% \u00B7 17 Dec 2025 09:12");
    });

    // Legend, emphasis, scene --------------------------------------------------------------------

    Run("history chart: legend chips hide lines for the session (per history ID and meter), across views and "
        "ranges; hidden lines leave the plot, the picker and the axes; cards stay", [] {
        auto w = TestChartOf(TestMockupWeek());
        HistoryController& c = w->controller;
        CHECK(c.view.lines.size() == 5 && c.view.cards.size() == 5);
        const HistoryUiLine cc1 = c.view.lines[1];
        CHECK(cc1.name == L"CC 1 \u00B7 Weekly" && cc1.chip == kHistoryUiChip + 1 * 8 + 1);
        c.Command(HistoryUiCommand::Line, cc1.meter, cc1.historyId);
        CHECK(c.view.lines[1].hidden && c.view.cards.size() == 5);
        HistoryChart& chart = TestLaid(*w);
        CHECK(chart.render[1][0].points.empty() && !chart.render[0][0].points.empty());
        D2D1_POINT_2F onHidden = HistoryChartPoint(chart, chart.lines[1], {100, 30, 0, false});
        for (const HistoryChartHit& hit : HistoryChartPick(chart, onHidden)) CHECK(hit.line != 1);
        // Kept across a view and a range change, and per meter: CC 1's other lines stay.
        c.Command(HistoryUiCommand::View, int(HistoryView::ExtraUsage));
        CHECK(std::all_of(c.view.lines.begin(), c.view.lines.end(), [](const HistoryUiLine& l) { return !l.hidden; }));
        c.Command(HistoryUiCommand::View, int(HistoryView::Weekly));
        c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Month));
        CHECK(c.view.lines[1].hidden && c.view.lines[1].historyId == 0xC1);
        c.Command(HistoryUiCommand::Select, 0, 0xC1);
        CHECK(c.view.lines[0].hidden && c.view.lines[0].meter == 1 && c.view.lines[0].chip == cc1.chip);
        // Shown again.
        c.Command(HistoryUiCommand::Line, cc1.meter, cc1.historyId);
        CHECK(!c.view.lines[0].hidden);
        // The extra axes follow the visible lines; percent stays with every line hidden.
        auto all = TestChartOf(TestMockupWeek(), [](HistoryController& h) { h.Command(HistoryUiCommand::View, int(HistoryView::AllMeters)); });
        HistoryChart& meters = TestLaid(*all);
        CHECK(meters.axes[1].visible && meters.axes[2].visible);
        for (const HistoryUiLine& line : std::vector<HistoryUiLine>(all->controller.view.lines)) {
            if (line.meter == 6) all->controller.Command(HistoryUiCommand::Line, line.meter, line.historyId);
        }
        HistoryChart& noDollars = TestLaid(*all);
        CHECK(!noDollars.axes[1].visible && noDollars.axes[2].visible && noDollars.columns[1] == 0);
        HistoryChartScene scene = HistoryChartBuildScene(noDollars, {}, TestPalette(UiThemeKind::Dark));
        CHECK(std::none_of(scene.prims.begin(), scene.prims.end(),
                           [](const HistoryChartPrim& p) { return p.text.ends_with(L" USD"); }));
        for (const HistoryUiLine& line : std::vector<HistoryUiLine>(all->controller.view.lines)) {
            if (!line.hidden) all->controller.Command(HistoryUiCommand::Line, line.meter, line.historyId);
        }
        HistoryChart& none = TestLaid(*all);
        CHECK(none.axes[0].visible && !none.axes[1].visible && !none.axes[2].visible && none.message == L"All lines are hidden");
        scene = HistoryChartBuildScene(none, {}, TestPalette(UiThemeKind::Dark));
        CHECK(TestMarks(scene, HistoryChartMark::Run).empty() && TestMarks(scene, HistoryChartMark::Fill).empty());
        CHECK(TestLabeled(scene, L"0%") && TestLabeled(scene, L"100%") && !TestLabeled(scene, L"90%"));
        const HistoryChartPrim* message = TestMarks(scene, HistoryChartMark::Label).back();
        CHECK(message->text == L"All lines are hidden" && TestSameColor(message->fill, HistoryChartBackground(TestPalette(UiThemeKind::Dark))));
        CHECK(HistoryChartPick(none, {(none.plot.left + none.plot.right) / 2, (none.plot.top + none.plot.bottom) / 2}).empty());
    });

    Run("history chart: emphasis: the hovered chip's line, else the picked lines, else the focused chip's (keyboard "
        "focus only); never a hidden line", [] {
        std::vector<HistoryUiLine> lines = TestWeeklyLines();
        lines[3].hidden = true;
        UiPaintState state;
        CHECK(HistoryChartEmphasis(lines, {}, state).empty());
        state.hot = {lines[2].chip};
        CHECK((HistoryChartEmphasis(lines, {0, 1}, state) == std::vector<size_t>{2}));
        state.focus = {lines[4].chip};
        state.focusVisible = true;
        CHECK((HistoryChartEmphasis(lines, {}, state) == std::vector<size_t>{2}));  // Hover wins over focus
        state.hot = {};
        CHECK((HistoryChartEmphasis(lines, {0, 1}, state) == std::vector<size_t>{0, 1}));
        CHECK((HistoryChartEmphasis(lines, {}, state) == std::vector<size_t>{4}));
        state.focusVisible = false;
        CHECK(HistoryChartEmphasis(lines, {}, state).empty());
        state.hot = {lines[3].chip};
        state.focus = {lines[3].chip};
        state.focusVisible = true;
        CHECK(HistoryChartEmphasis(lines, {}, state).empty());
        state.hot = {kHistoryUiChart};
        CHECK(HistoryChartEmphasis(lines, {}, state).empty());
    });

    Run("history chart: the scene: band, gridlines and labels, then the lines in order (muted first, previous week "
        "dashed before current), 3 and 2 DIP wide; emphasis mutes the others (high contrast: grey)", [] {
        auto w = TestChartOf(TestMockupWeek());
        HistoryChart& c = TestLaid(*w);
        UiPalette dark = TestPalette(UiThemeKind::Dark);
        HistoryChartScene scene = HistoryChartBuildScene(c, {}, dark);
        CHECK(TestSameRect(scene.clip, UiInflate(c.plot, 3, 3)));
        float height = c.plot.bottom - c.plot.top;
        // The band from the threshold to the top, its dashed line and label; gridlines at 0, 50 and 100%.
        std::vector<const HistoryChartPrim*> fills = TestMarks(scene, HistoryChartMark::Fill);
        CHECK(fills.size() == 1 && scene.prims[0].mark == HistoryChartMark::Fill &&
              TestSameRect(fills[0]->rect, {c.plot.left, c.plot.top, c.plot.right, c.plot.top + height / 10}));
        CHECK(TestSameColor(fills[0]->color, UiRgb(0xE53935, 0.08f)));
        std::vector<const HistoryChartPrim*> rules = TestMarks(scene, HistoryChartMark::Rule);
        CHECK(rules.size() == 4 && rules[3]->dash == 4 && rules[3]->gap == 4 && rules[0]->dash == 0);
        // One device pixel wide, on a pixel row.
        CHECK(rules[0]->width == 1 && rules[0]->points[0].y == c.plot.bottom + 0.5f);
        CHECK(rules[3]->points[0].y == std::round(c.plot.top + height / 10 - 0.5f) + 0.5f);
        for (PCWSTR label : {L"0%", L"50%", L"100%", L"90%", L"Sun 25", L"Sat 31"}) CHECK(TestLabeled(scene, label));
        // Lines in order, nothing dashed; widths per series; runs break at resets.
        std::vector<const HistoryChartPrim*> runs = TestMarks(scene, HistoryChartMark::Run);
        int previousLine = -1;
        for (const HistoryChartPrim* run : runs) {
            CHECK(run->clipped && run->period == 0 && run->dash == 0 && run->line >= previousLine);
            CHECK(run->width == (c.lines[size_t(run->line)].meter == 2 ? 2 : 3));
            previousLine = run->line;
        }
        CHECK(std::count_if(runs.begin(), runs.end(), [](const HistoryChartPrim* r) { return r->line == 0; }) == 2);
        CHECK(TestSameColor(runs[0]->color, HistoryChartColor(c.lines[0], dark)));
        // Emphasis on CC 2's weekly line: drawn last; the others at a quarter of their color.
        scene = HistoryChartBuildScene(c, {2}, dark);
        runs = TestMarks(scene, HistoryChartMark::Run);
        CHECK(runs.back()->line == 2 && runs.front()->line == 0);
        for (const HistoryChartPrim* run : runs) {
            D2D1_COLOR_F base = HistoryChartColor(c.lines[size_t(run->line)], dark);
            CHECK(TestSameColor(run->color, run->line == 2 ? base : UiOver(UiFade(base, 0.25f), HistoryChartBackground(dark))));
        }
        // High contrast: text and link by series, hatched band; emphasis in the selection color, the rest grey.
        UiPalette hc = TestPalette(UiThemeKind::HighContrast);
        scene = HistoryChartBuildScene(c, {}, hc);
        CHECK(TestMarks(scene, HistoryChartMark::Hatch).size() == 1 && TestMarks(scene, HistoryChartMark::Fill).empty());
        for (const HistoryChartPrim* run : TestMarks(scene, HistoryChartMark::Run)) {
            CHECK(TestSameColor(run->color, c.lines[size_t(run->line)].series % 2 ? hc.link : hc.text));
        }
        scene = HistoryChartBuildScene(c, {2}, hc);
        for (const HistoryChartPrim* run : TestMarks(scene, HistoryChartMark::Run)) {
            CHECK(TestSameColor(run->color, run->line == 2 ? hc.highlight : hc.textDisabled));
        }
    });

    Run("history chart: comparing draws each line's previous week dashed before its current one; lone readings are "
        "dots (filled current, hollow previous), a flat run within a pixel a stroke-wide square", [] {
        auto w = TestChartOf(TestMockupCompare(), [](HistoryController& c) {
            c.Command(HistoryUiCommand::Select, 0, 0xC2);
            c.Command(HistoryUiCommand::View, int(HistoryView::Weekly));
            c.Command(HistoryUiCommand::Compare, 1);
        });
        HistoryChart& c = TestLaid(*w);
        UiPalette dark = TestPalette(UiThemeKind::Dark);
        HistoryChartScene scene = HistoryChartBuildScene(c, {}, dark);
        CHECK(c.lines.size() == 2);
        int line = -1, period = 2;
        for (const HistoryChartPrim& prim : scene.prims) {
            if (!prim.clipped) continue;
            if (prim.line != line) {
                CHECK(prim.line > line && prim.period == 1);
                line = prim.line;
            } else {
                CHECK(prim.period <= period);
            }
            period = prim.period;
            if (prim.mark == HistoryChartMark::Run) {
                float w = prim.width;
                CHECK(prim.period ? prim.dash == 3 / w && prim.gap == 6 / w : prim.dash == 0);
            }
        }
        std::vector<const HistoryChartPrim*> dots = TestMarks(scene, HistoryChartMark::Dot);
        CHECK(dots.size() == 2);
        CHECK(dots[0]->line == 0 && dots[0]->period == 0 && TestSameColor(dots[0]->fill, dots[0]->color));
        CHECK(dots[1]->line == 1 && dots[1]->period == 1 && TestSameColor(dots[1]->fill, HistoryChartBackground(dark)));
        CHECK(dots[0]->width == 1.6f);
        // Three equal readings a minute apart share a pixel: one stroke-wide square.
        std::vector<HistorySample> flat;
        for (int i = 0; i < 3; i++) {
            HistorySample sample = TestReading(0x0A, 2, kWeekFrom + kHourMs + ULONGLONG(i) * kMinute, {{1, 40}});
            sample.intervalMs = kMinute;
            flat.push_back(sample);
        }
        auto f = TestChartOf(flat);
        HistoryChart& fc = TestLaid(*f);
        scene = HistoryChartBuildScene(fc, {}, dark);
        std::vector<const HistoryChartPrim*> squares = TestMarks(scene, HistoryChartMark::Square);
        CHECK(squares.size() == 1 && TestMarks(scene, HistoryChartMark::Run).empty() && TestMarks(scene, HistoryChartMark::Dot).empty());
        D2D1_POINT_2F at = TestAt(fc, 0, 0, 0);
        CHECK(TestSameRect(squares[0]->rect, {at.x - 1.5f, at.y - 1.5f, at.x + 1.5f, at.y + 1.5f}));
    });

    Run("history chart: the band shows only while a percent line does, from the threshold to the axis's top above "
        "100%; credits alone have none", [] {
        auto band = TestChartOf(TestMockupBand());
        HistoryChart& c = TestLaid(*band);
        CHECK(c.axes[0].high == 125);
        HistoryChartScene scene = HistoryChartBuildScene(c, {}, TestPalette(UiThemeKind::Light));
        std::vector<const HistoryChartPrim*> fills = TestMarks(scene, HistoryChartMark::Fill);
        float height = c.plot.bottom - c.plot.top;
        CHECK(fills.size() == 1 && TestSameRect(fills[0]->rect, {c.plot.left, c.plot.top, c.plot.right, c.plot.bottom - height * 90 / 125}));
        CHECK(TestSameColor(fills[0]->color, UiRgb(0xC42B1C, 0.07f)));
        CHECK(TestLabeled(scene, L"125%") && TestLabeled(scene, L"100%"));
        auto credits = TestChartOf(TestMockupWeek(), [](HistoryController& h) {
            h.Command(HistoryUiCommand::Select, 0, 0x0A);
            h.Command(HistoryUiCommand::View, int(HistoryView::ExtraUsage));
        });
        HistoryChart& cr = TestLaid(*credits);
        CHECK(cr.lines.size() == 1 && cr.lines[0].meter == 7);
        scene = HistoryChartBuildScene(cr, {}, TestPalette(UiThemeKind::Dark));
        CHECK(TestMarks(scene, HistoryChartMark::Fill).empty() && !TestLabeled(scene, L"90%"));
        CHECK(TestLabeled(scene, L"3000 cr") && TestLabeled(scene, L"0%") && TestMarks(scene, HistoryChartMark::Rule).size() == 3);
        auto empty = TestChartOf({}, [](HistoryController& h) { h.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day)); });
        HistoryChart& none = TestLaid(*empty);
        CHECK(none.message == L"No readings in this range.");
        scene = HistoryChartBuildScene(none, {}, TestPalette(UiThemeKind::Dark));
        CHECK(TestMarks(scene, HistoryChartMark::Fill).empty() && TestLabeled(scene, L"No readings in this range.") &&
              TestLabeled(scene, L"Sat 31"));
    });

    Run("history chart: the Remaining bar mode plots percentages left, below 0% past the limit, the band from what "
        "the threshold leaves down to the axis's bottom; readouts and titles left; dollars and credits as recorded", [] {
        // OAI's weekly quota at 20, 80 and 112% used six hours apart over the last day.
        std::vector<HistorySample> journal;
        for (auto [hour, used] : {std::pair{1, 20.0}, std::pair{7, 80.0}, std::pair{13, 112.0}}) {
            HistorySample sample = TestReading(0x0A, 2, kWeekTo - kHistoryDayMs + ULONGLONG(hour) * kHourMs, {{1, used}});
            sample.intervalMs = 6 * kHourMs;
            journal.push_back(sample);
        }
        auto w = TestChartOf(journal, [](HistoryController& c) { c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day)); });
        CHECK(TestLaid(*w).axes[0].high == 125 && w->controller.chart.lines[0].title == L"OAI \u00B7 Weekly used (%)");
        // The mode switched in Settings: the next load re-lays the chart.
        w->ops.settings.settings.barMode = BarMode::Remaining;
        w->controller.Load();
        HistoryChart& c = TestLaid(*w);
        CHECK(c.remaining && c.lines.size() == 1 && c.lines[0].title == L"OAI \u00B7 Weekly left (%)" &&
              c.lines[0].name == L"OAI \u00B7 Weekly");
        // The axis from -25 to 100%: 80 and 20% left, then 12% below 0 past the limit, so the line keeps the
        // recorded crossing times (as in Used mode, mirrored).
        CHECK(c.axes[0].low == -25 && c.axes[0].high == 100 && c.render[0][0].points.size() == 3);
        CHECK((c.axes[0].values == std::vector<double>{-25, 0, 50, 100}));
        const double shares[] = {80, 20, -12};
        for (size_t i = 0; i < 3; i++) CHECK(TestAt(c, 0, 0, i).y == HistoryChartY(c.axes[0], c.plot, shares[i]));
        for (auto [i, value] : {std::pair{size_t(0), L"80.0%"}, std::pair{size_t(2), L"-12.0%"}}) {
            UiTooltipContent readout;
            w->controller.Hover(TestAt(c, 0, 0, i), readout, TestPalette(UiThemeKind::Dark));
            CHECK(readout.rows.size() == 1 &&
                  UiRunsText(readout.rows[0].detail) == std::wstring(L"[Current] ") + value + L" \u00B7 " + TestTime(journal[i].timeMs));
        }
        // The band from 10% left (the 90% threshold) down to the bottom, its line and label there.
        HistoryChartScene scene = HistoryChartBuildScene(c, {}, TestPalette(UiThemeKind::Dark));
        std::vector<const HistoryChartPrim*> fills = TestMarks(scene, HistoryChartMark::Fill);
        float height = c.plot.bottom - c.plot.top;
        float band = height * 35 / 125;
        CHECK(fills.size() == 1 &&
              TestSameRect(fills[0]->rect, {c.plot.left, c.plot.bottom - band, c.plot.right, c.plot.bottom}));
        std::vector<const HistoryChartPrim*> rules = TestMarks(scene, HistoryChartMark::Rule);
        CHECK(rules.size() == 5 && rules[4]->dash == 4 && rules[4]->points[0].y == std::round(c.plot.bottom - band - 0.5f) + 0.5f);
        CHECK(TestLabeled(scene, L"10%") && TestLabeled(scene, L"-25%") && !TestLabeled(scene, L"90%") &&
              !TestLabeled(scene, L"125%"));
        // Axes alone: a percent line's range turns over (-30 to 112% used is 130 to -12% left); dollars and
        // credits keep theirs.
        HistoryUiLine over, dollars, credits;
        over.meter = 1;
        over.low = -30;
        over.high = 112;
        dollars.meter = 6;
        dollars.high = 11;
        credits.meter = 7;
        credits.high = 2500;
        std::array<HistoryChartAxis, 3> axes = HistoryChartAxes({over, dollars, credits}, true);
        CHECK(axes[0].low == -25 && axes[0].high == 150 && (axes[0].values == std::vector<double>{0, -25, 150, 100, 50}) &&
              axes[1].visible && axes[1].high == 20 && axes[2].visible && axes[2].high == 3000);
        // OAI's credits on their own axis as recorded; no band without a percent line.
        auto balance = TestChartOf(TestMockupWeek(), [](HistoryController& h) {
            h.Command(HistoryUiCommand::Select, 0, 0x0A);
            h.Command(HistoryUiCommand::View, int(HistoryView::ExtraUsage));
        });
        balance->ops.settings.settings.barMode = BarMode::Remaining;
        balance->controller.Load();
        HistoryChart& cr = TestLaid(*balance);
        CHECK(cr.lines.size() == 1 && cr.lines[0].meter == 7 && cr.axes[2].high == 3000);
        CHECK(TestAt(cr, 0, 0, 0).y == HistoryChartY(cr.axes[2], cr.plot, cr.render[0][0].points[0].value));
        CHECK(TestMarks(HistoryChartBuildScene(cr, {}, TestPalette(UiThemeKind::Dark)), HistoryChartMark::Fill).empty());
    });

    Run("history chart: the bar mode switched in Settings takes a live refresh alone: titles, cards, the chart and "
        "its readout follow from the same snapshot, both ways, also with the pointer on the chart", [] {
        // OAI's weekly quota at 20 then 80% used over the last day; the clock stays put.
        std::vector<HistorySample> journal;
        for (auto [hour, used] : {std::pair{1, 20.0}, std::pair{7, 80.0}}) {
            HistorySample sample = TestReading(0x0A, 2, kWeekTo - kHistoryDayMs + ULONGLONG(hour) * kHourMs, {{1, used}});
            sample.intervalMs = 6 * kHourMs;
            journal.push_back(sample);
        }
        auto w = TestChartOf(journal, [](HistoryController& c) { c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day)); });
        HistoryController& c = w->controller;
        TestLaid(*w);
        const std::shared_ptr<const HistorySnapshot> held = c.snapshot;
        const uint64_t generation = c.generation;
        auto check = [&](bool left) {
            HistoryChart& chart = TestLaid(*w);
            CHECK(c.snapshot == held && c.generation == generation && c.view.remaining == left && chart.remaining == left);
            CHECK(c.view.lines.size() == 1 && c.view.lines[0].title == (left ? L"OAI \u00B7 Weekly left (%)" : L"OAI \u00B7 Weekly used (%)"));
            CHECK(c.view.cards.size() == 1 && c.view.cards[0].value == (left ? L"20%" : L"80%") &&
                  c.view.cards[0].lines[0] == (left ? L"Lowest 20% \u00B7 " : L"Peak 80% \u00B7 ") + TestTime(journal[1].timeMs));
            CHECK(TestAt(chart, 0, 0, 0).y == HistoryChartY(chart.axes[0], chart.plot, left ? 80 : 20));
            UiTooltipContent readout;
            c.Hover(TestAt(chart, 0, 0, 1), readout, TestPalette(UiThemeKind::Dark));
            CHECK(readout.rows.size() == 1 && UiRunsText(readout.rows[0].detail) ==
                                                  (left ? L"[Current] 20.0% \u00B7 " : L"[Current] 80.0% \u00B7 ") + TestTime(journal[1].timeMs));
            c.Hover(std::nullopt, readout, TestPalette(UiThemeKind::Dark));
        };
        check(false);
        for (auto [mode, pointer] : {std::pair{BarMode::Remaining, false}, std::pair{BarMode::Used, false},
                                     std::pair{BarMode::Remaining, true}}) {
            w->ops.settings.settings.barMode = mode;
            c.Notify();
            // The window rebuilds; with the pointer on the chart the sample part waits on the deferral timer.
            CHECK(TestDeliver(c, w->ops, false, pointer));
            CHECK(c.samplesDirty == pointer && c.timer == pointer && w->ops.timer == pointer);
            check(mode == BarMode::Remaining);
        }
    });

    Run("history chart: the reduction is kept while its key holds (data generation, plot pixels, period, selection, "
        "view, visible lines) and redone when any part changes", [] {
        auto w = TestChartOf(TestMockupWeek());
        HistoryController& c = w->controller;
        auto mark = [&] { c.chart.render[0][0].points[0].value = 12345; };
        auto kept = [&] { return c.chart.render[0][0].points[0].value == 12345; };
        TestLaid(*w);
        mark();
        TestLaid(*w);
        CHECK(kept());
        TestLaid(*w, {27, 59, 801, 300});  // Shorter: the same pixel width
        CHECK(kept());
        // Each change alone, from the same layout.
        const std::function<void()> changes[] = {
            [&] { TestLaid(*w, {27, 59, 811, 359}); },
            [&] { TestLaid(*w, kTestChartRect, 1.25f); },
            [&] { c.Load(); TestLaid(*w); },
            [&] { c.Command(HistoryUiCommand::Select, 0, 0x0A); TestLaid(*w); },  // All meters: weekly, credits
            [&] { c.Command(HistoryUiCommand::Line, c.view.lines[1].meter, c.view.lines[1].historyId); TestLaid(*w); },
            [&] { c.Command(HistoryUiCommand::View, int(HistoryView::Weekly)); TestLaid(*w); },
            [&] { c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Month)); TestLaid(*w); },
            [&] { c.Command(HistoryUiCommand::Compare, 1); TestLaid(*w); },
        };
        for (const std::function<void()>& change : changes) {
            TestLaid(*w);
            HistoryChartKey before = c.chart.key;
            mark();
            change();
            CHECK(!kept() && !(c.chart.key == before));
        }
        // A reduction cut short by unload keeps no key: the next layout reduces again.
        w->ops.unloading = true;
        TestLaid(*w, {27, 59, 821, 359});
        CHECK(c.chart.key.width == -1);
        w->ops.unloading = false;
        TestLaid(*w, {27, 59, 821, 359});
        CHECK(c.chart.key.width > 0);
        // A build drops the picks (its hover ended).
        UiTooltipContent readout;
        c.Hover(TestAt(c.chart, 0, 0, 0), readout, TestPalette(UiThemeKind::Dark));
        CHECK(!c.picked.empty());
        TestLaid(*w);
        CHECK(c.picked.empty());
    });
}
