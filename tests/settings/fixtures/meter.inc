// Meter model: display entry clamps, bars, tiles, account state, identity and visual test data.
// Expected values are derived by hand from the documented behavior.

static void RunDisplayTests() {
    Run("display: in-range settings pass through", [] {
        Settings settings = With([](Settings& s) {
            s.barMode = BarMode::Remaining; s.barLength = 120; s.barThickness = 6;
            s.ringSize = 28; s.ringThickness = 4; s.yellowThreshold = 40;
            s.orangeThreshold = 60; s.redThreshold = 80; s.pollMinutes = 15;
            s.colorblindMode = true; s.showPaceTicks = false; s.paceTickStyle = PaceTickStyle::Dot;
            s.paceTickColor = RGB(1, 2, 3); s.percentTextAlignment = PercentTextAlignment::Center;
            s.showExtraBarAmounts = true; s.showStaleWarning = false;
        });
        MeterDisplay d = MeterDisplayFor(settings, kNow, 1.25, 44);
        CHECK(d.now == kNow); CHECK(d.barMode == BarMode::Remaining);
        CHECK(d.barLength == 120); CHECK(d.barThickness == 6);
        CHECK(d.ringSize == 28); CHECK(d.ringThickness == 4);
        CHECK(d.yellowThreshold == 40); CHECK(d.orangeThreshold == 60); CHECK(d.redThreshold == 80);
        CHECK(d.pollMinutes == 15); CHECK(d.colorblindMode); CHECK(!d.showPaceTicks);
        CHECK(d.paceTickStyle == PaceTickStyle::Dot); CHECK(d.paceTickColor == RGB(1, 2, 3));
        CHECK(d.percentTextAlignment == PercentTextAlignment::Center);
        CHECK(d.showExtraBarAmounts); CHECK(!d.showStaleWarning);
        CHECK_NEAR(d.rasterizationScale, 1.25); CHECK_NEAR(d.tileHeight, 36);
    });

    Run("display: out-of-range settings clamp at the entry", [] {
        MeterDisplay low = Display(With([](Settings& s) {
            s.barLength = -5; s.barThickness = 0; s.ringSize = 0; s.ringThickness = -1;
            s.pollMinutes = 0; s.yellowThreshold = -10; s.orangeThreshold = -20; s.redThreshold = -30;
        }));
        CHECK(low.barLength == 10); CHECK(low.barThickness == 2);
        CHECK(low.ringSize == 24); CHECK(low.ringThickness == 1); CHECK(low.pollMinutes == 2);
        CHECK(low.yellowThreshold == 0); CHECK(low.orangeThreshold == 0); CHECK(low.redThreshold == 0);
        MeterDisplay high = Display(With([](Settings& s) {
            s.barLength = 9999; s.barThickness = 51; s.ringSize = 41; s.ringThickness = 7;
            s.pollMinutes = 100000; s.yellowThreshold = 150; s.orangeThreshold = 120;
            s.redThreshold = 101; s.paceTickColor = 0xAB123456;
        }));
        CHECK(high.barLength == 500); CHECK(high.barThickness == 50);
        CHECK(high.ringSize == 40); CHECK(high.ringThickness == 6); CHECK(high.pollMinutes == 1440);
        CHECK(high.yellowThreshold == 100); CHECK(high.orangeThreshold == 100);
        CHECK(high.redThreshold == 100); CHECK(high.paceTickColor == 0x123456);
    });

    Run("display: unordered thresholds order upward", [] {
        MeterDisplay a = Display(With([](Settings& s) {
            s.yellowThreshold = 80; s.orangeThreshold = 60; s.redThreshold = 70;
        }));
        CHECK(a.yellowThreshold == 80); CHECK(a.orangeThreshold == 80); CHECK(a.redThreshold == 80);
        MeterDisplay b = Display(With([](Settings& s) {
            s.yellowThreshold = 30; s.orangeThreshold = 90; s.redThreshold = 50;
        }));
        CHECK(b.yellowThreshold == 30); CHECK(b.orangeThreshold == 90); CHECK(b.redThreshold == 90);
    });

    Run("display: scale and taskbar height fall back", [] {
        double nan = std::numeric_limits<double>::quiet_NaN();
        double inf = std::numeric_limits<double>::infinity();
        for (double scale : {0.0, -1.0, nan, inf, 0.5}) CHECK_NEAR(Display({}, scale).rasterizationScale, 1);
        CHECK_NEAR(Display({}, 1.5).rasterizationScale, 1.5);
        CHECK_NEAR(Display({}, 2).rasterizationScale, 2);
        for (double taskbar : {0.0, -3.0, nan, inf, 100.0}) CHECK_NEAR(Display({}, 1, taskbar).tileHeight, 40);
        CHECK_NEAR(Display({}, 1, 20).tileHeight, 24);
        CHECK_NEAR(Display({}, 1, 47.9).tileHeight, 39);
    });

    Run("tile height for 32, 40 and 48 DIP taskbars", [] {
        CHECK_NEAR(Display({}, 1, 32).tileHeight, 24);
        CHECK_NEAR(Display({}, 1, 40).tileHeight, 32);
        CHECK_NEAR(Display({}, 1, 48).tileHeight, 40);
    });

    Run("out-of-range sizes reach the model clamped", [] {
        MeterDisplay shortBar = Display(With([](Settings& s) { s.barLength = 0; s.barThickness = 0; }));
        CHECK(Bar(50, shortBar).fillPx == 5);
        CHECK(Bar(3, shortBar).fillPx == 2);
        CHECK(Bar(100, shortBar).fillPx == 10);
        // Caret 5 DIPs on a 10 DIP bar: centre kept 2.5 DIPs from either end.
        CHECK(First(Window(10, kFiveHours), shortBar).quotas[0].pacePx == 0);
        CHECK(First(Window(10, 0), shortBar).quotas[0].pacePx == 5);
        BarGeometry zeroScale = BarGeometryFor(Display({}, 0));
        CHECK_NEAR(zeroScale.paceLength, 5); CHECK_NEAR(zeroScale.textInset, 4);
        MeterDisplay ring = Display(With([](Settings& s) { s.ringSize = 99; s.ringThickness = 0; }));
        AccountMeters meters = First({10, 0, 0}, ring, BarLayout::SegmentedRing);
        CHECK_NEAR(meters.ring.size, 40); CHECK_NEAR(meters.ring.stroke, 1);
        MeterDisplay unordered = Display(With([](Settings& s) {
            s.yellowThreshold = 80; s.orangeThreshold = 60; s.redThreshold = 70;
        }));
        CHECK(Bar(75, unordered).color == 0xFF43A047);
        CHECK(Bar(80, unordered).color == 0xFFE53935);
        MeterDisplay noPoll = Display(With([](Settings& s) { s.pollMinutes = 0; }));
        AccountData data = Fresh({10, -1, -1, -1});
        data.lastSuccessMs = kNow - 4 * kMinute;
        CHECK(!AccountMeterState(noPoll, BarLayout::Stacked, Account(), data, 0, 0).stale);
        data.lastSuccessMs -= 1;
        CHECK(AccountMeterState(noPoll, BarLayout::Stacked, Account(), data, 0, 0).stale);
    });
}

static void RunColorTests() {
    Run("usage colors by band, colorblind and stale", [] {
        auto color = [](double pct, bool stale = false, bool colorblind = false) {
            return UsageColor(pct, stale, 50, 75, 90, colorblind);
        };
        CHECK(color(0) == 0xFF43A047); CHECK(color(49.99) == 0xFF43A047);
        CHECK(color(50) == 0xFFFDD835); CHECK(color(74.99) == 0xFFFDD835);
        CHECK(color(75) == 0xFFFB8C00); CHECK(color(89.99) == 0xFFFB8C00);
        CHECK(color(90) == 0xFFE53935); CHECK(color(250) == 0xFFE53935);
        CHECK(color(-1) == 0xFF9E9E9E); CHECK(color(60, true) == 0xFF9E9E9E);
        CHECK(color(10, false, true) == 0xFF0072B2); CHECK(color(60, false, true) == 0xFF56B4E9);
        CHECK(color(80, false, true) == 0xFFE69F00); CHECK(color(95, false, true) == 0xFFD55E00);
        // The fill follows the bar mode, its color the used share.
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK(Bar(95, remaining).color == 0xFFE53935);
        CHECK(Bar(30, remaining).color == 0xFF43A047);
    });

    Run("tile palette per taskbar theme", [] {
        TilePalette light = TilePaletteFor(true), dark = TilePaletteFor(false);
        CHECK(light.track == 0x24000000); CHECK(light.hover == 0x0A000000);
        CHECK(light.open == 0xB3FFFFFF); CHECK(light.badge == 0xFFD8DBE2);
        CHECK(light.badgeOutline == 0xFFEEF0F4);
        CHECK(dark.track == 0x2BFFFFFF); CHECK(dark.hover == 0x0FFFFFFF);
        CHECK(dark.open == 0x1AFFFFFF); CHECK(dark.badge == 0xFF3A3D44);
        CHECK(dark.badgeOutline == 0xFF1F2126);
    });
}

static void RunBarTests() {
    Run("bar fill: 2 px minimum, rounding, clamp to length", [] {
        CHECK(Bar(0).fillPx == 0); CHECK(Bar(0.4).fillPx == 2); CHECK(Bar(1.6).fillPx == 2);
        CHECK(Bar(2.4).fillPx == 2); CHECK(Bar(2.6).fillPx == 3); CHECK(Bar(37.4).fillPx == 37);
        CHECK(Bar(99.6).fillPx == 100); CHECK(Bar(150).fillPx == 100); CHECK(Bar(-1).fillPx == 0);
        MeterDisplay ten = Display(With([](Settings& s) { s.barLength = 10; }));
        CHECK(Bar(14, ten).fillPx == 2); CHECK(Bar(26, ten).fillPx == 3); CHECK(Bar(100, ten).fillPx == 10);
    });

    Run("bar text: used mode is unclamped above 100%", [] {
        CHECK(Bar(0).text == L"0%"); CHECK(Bar(0.4).text == L"0%"); CHECK(Bar(37.4).text == L"37%");
        CHECK(Bar(99.6).text == L"100%"); CHECK(Bar(123.4).text == L"123%");
        CHECK(Bar(250).text == L"250%"); CHECK(Bar(-1).text.empty());
    });

    Run("bar remaining mode clamps and keeps unavailable quotas", [] {
        MeterDisplay d = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK(Bar(30, d).fillPx == 70); CHECK(Bar(30, d).text == L"70%");
        CHECK(Bar(0, d).fillPx == 100); CHECK(Bar(0, d).text == L"100%");
        CHECK(Bar(123.4, d).fillPx == 0); CHECK(Bar(123.4, d).text == L"0%");
        CHECK(Bar(99.7, d).fillPx == 2); CHECK(Bar(99.7, d).text == L"0%");
        CHECK(Bar(-1, d).fillPx == 0); CHECK(Bar(-1, d).text.empty());
    });

    Run("bar alignment: adaptive from shown %, dark text from the used band", [] {
        using enum PercentTextAlignment;
        auto check = [](QuotaMeter m, PercentTextAlignment alignment, bool dark) {
            CHECK(m.textAlignment == alignment); CHECK(m.darkText == dark);
        };
        check(Bar(40), Right, false); check(Bar(50), Left, true); check(Bar(74.9), Left, true);
        check(Bar(80), Left, true); check(Bar(90), Left, false); check(Bar(130), Left, false);
        check(Bar(-1), Right, false);
        AccountData stale = Fresh({60, -1, -1, -1});
        stale.stale = true;
        check(AccountMeterState(Display(), BarLayout::Stacked, Account(), stale, 0, 0).quotas[0],
              Left, false);
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        check(Bar(30, remaining), Left, false);
        check(Bar(60, remaining), Right, false);
        check(Bar(50, remaining), Left, true);
    });

    Run("bar alignment: fixed alignments and offsets per scale", [] {
        using enum PercentTextAlignment;
        auto fixed = [](PercentTextAlignment alignment, double scale = 1) {
            return Display(With([&](Settings& s) { s.percentTextAlignment = alignment; }), scale);
        };
        QuotaMeter center = Bar(60, fixed(Center));
        CHECK(center.textAlignment == Center); CHECK_NEAR(center.textOffset, 0); CHECK(!center.darkText);
        QuotaMeter left = Bar(30, fixed(Left));
        CHECK(left.textAlignment == Left); CHECK_NEAR(left.textOffset, 4); CHECK(!left.darkText);
        CHECK(Bar(60, fixed(Left)).darkText);
        QuotaMeter right = Bar(60, fixed(Right));
        CHECK(right.textAlignment == Right); CHECK_NEAR(right.textOffset, -4); CHECK(!right.darkText);
        CHECK_NEAR(Bar(60, Display({}, 1.5)).textOffset, 4 / 1.5);
        CHECK_NEAR(Bar(40, Display({}, 2)).textOffset, -2);
    });

    Run("bar amounts: dollars, credits, remaining clamp, fallbacks", [] {
        auto amount = [](std::wstring provider, double pct, double used, double limit,
                         BarMode mode = BarMode::Used, bool amounts = true) {
            AccountData data = Fresh({25, -1, -1, pct});
            data.extraUsedAmount = used;
            data.extraLimitAmount = limit;
            MeterDisplay d = Display(With([&](Settings& s) {
                s.barMode = mode; s.showExtraBarAmounts = amounts;
            }));
            return AccountMeterState(d, BarLayout::Stacked, Account(provider), data, 0, 0);
        };
        CHECK(amount(L"anthropic", 25, 12.5, 50).quotas[3].text == L"$12.50");
        CHECK(amount(L"anthropic", 25, 12.5, 50, BarMode::Remaining).quotas[3].text == L"$37.50");
        CHECK(amount(L"anthropic", 100, 60, 50).quotas[3].text == L"$60.00");
        CHECK(amount(L"anthropic", 100, 60, 50, BarMode::Remaining).quotas[3].text == L"$0.00");
        CHECK(amount(L"openai", 25, 250, 1000).quotas[3].text == L"250");
        CHECK(amount(L"openai", 25, 250, 1000, BarMode::Remaining).quotas[3].text == L"750");
        CHECK(amount(L"anthropic", 25, 12.5, -1).quotas[3].text == L"25%");
        CHECK(amount(L"anthropic", 25, 12.5, -1, BarMode::Remaining).quotas[3].text == L"75%");
        CHECK(amount(L"anthropic", 25, 12.5, 50, BarMode::Used, false).quotas[3].text == L"25%");
        CHECK(amount(L"anthropic", 25, 12.5, 50).quotas[0].text == L"25%");
        CHECK(amount(L"anthropic", -1, 12.5, 50).quotas[3].text.empty());
    });

    Run("bar pace: window start, end and middle in both modes", [] {
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        auto pace = [](long long resetIn, const MeterDisplay& d = Display()) {
            QuotaMeter m = First(Window(10, resetIn), d).quotas[0];
            CHECK(m.paceVisible);
            return m.pacePx;
        };
        // Caret 5 DIPs on a 100 DIP bar 8 thick: centre kept 4 DIPs from either end.
        CHECK(pace(kFiveHours) == 2); CHECK(pace(kFiveHours, remaining) == 94);
        CHECK(pace(0) == 94); CHECK(pace(0, remaining) == 2);
        CHECK(pace(13500000) == 23); CHECK(pace(13500000, remaining) == 73);
        CHECK(pace(-1000) == 94);
    });

    Run("bar pace: clamped by tick half-size and bar radius", [] {
        auto pace = [](long long resetIn, const MeterDisplay& d) {
            return First(Window(10, resetIn), d).quotas[0].pacePx;
        };
        MeterDisplay thin = Display(With([](Settings& s) { s.barThickness = 2; }));
        CHECK(pace(kFiveHours, thin) == 0); CHECK(pace(0, thin) == 95);
        MeterDisplay thick = Display(With([](Settings& s) { s.barThickness = 20; }));
        CHECK(pace(kFiveHours, thick) == 8); CHECK(pace(0, thick) == 88);
        MeterDisplay line = Display(With([](Settings& s) { s.paceTickStyle = PaceTickStyle::Line; }));
        CHECK(pace(kFiveHours, line) == 2); CHECK(pace(0, line) == 94);
        MeterDisplay hiDpi = Display({}, 2);
        CHECK(pace(kFiveHours, hiDpi) == 3); CHECK(pace(0, hiDpi) == 95);
    });

    Run("bar pace: hidden without a usable window", [] {
        CHECK(!First(Window(10, kFiveHours + 1000)).quotas[0].paceVisible);
        CHECK(!First({10, kNow + 1000, 0}).quotas[0].paceVisible);
        CHECK(!First({10, 5000, kFiveHours}).quotas[0].paceVisible);
        CHECK(!First(Window(-1, 1000)).quotas[0].paceVisible);
        MeterDisplay off = Display(With([](Settings& s) { s.showPaceTicks = false; }));
        CHECK(!First(Window(10, 1000), off).quotas[0].paceVisible);
        CHECK(!First(Window(10, 1000), Display(), BarLayout::LogoHeadline).quotas[0].paceVisible);
        AccountData stale = Fresh();
        stale.win5h = Window(10, 1000);
        stale.stale = true;
        CHECK(!AccountMeterState(Display(), BarLayout::Stacked, Account(), stale, 0, 0)
                   .quotas[0].paceVisible);
    });

    Run("bar geometry: caret polygon and tick sizes per style and scale", [] {
        using Point = BarGeometry::Point;
        auto geometry = [](PaceTickStyle style, int thickness = 8, double scale = 1) {
            return BarGeometryFor(Display(With([&](Settings& s) {
                s.paceTickStyle = style; s.barThickness = thickness;
            }), scale));
        };
        auto same = [](const std::array<Point, 12>& actual, const std::array<Point, 12>& expected) {
            for (size_t i = 0; i < actual.size(); i++) {
                CHECK_NEAR(actual[i].along, expected[i].along);
                CHECK_NEAR(actual[i].cross, expected[i].cross);
            }
        };
        BarGeometry caret = geometry(PaceTickStyle::Caret);
        CHECK_NEAR(caret.paceLength, 5); CHECK_NEAR(caret.paceCrossSize, 8); CHECK_NEAR(caret.textInset, 4);
        same(caret.caret[0], {{{0, 0}, {5, 0}, {5, 2}, {4, 2}, {4, 6}, {5, 6}, {5, 8}, {0, 8},
                               {0, 6}, {1, 6}, {1, 2}, {0, 2}}});
        same(caret.caret[1], {{{1, 0}, {4, 0}, {4, 1}, {3, 1}, {3, 7}, {4, 7}, {4, 8}, {1, 8},
                               {1, 7}, {2, 7}, {2, 1}, {1, 1}}});
        BarGeometry thinCaret = geometry(PaceTickStyle::Caret, 2);
        CHECK_NEAR(thinCaret.caret[0][2].cross, 1); CHECK_NEAR(thinCaret.caret[1][2].cross, 1);
        BarGeometry line = geometry(PaceTickStyle::Line);
        CHECK_NEAR(line.paceLength, 4); CHECK_NEAR(line.paceCrossSize, 8);
        CHECK_NEAR(line.coreLength, 2); CHECK_NEAR(line.coreCrossSize, 8);
        CHECK_NEAR(line.paceRadius, 0); CHECK_NEAR(line.coreRadius, 0);
        BarGeometry notch = geometry(PaceTickStyle::Notch);
        CHECK_NEAR(notch.paceLength, 4); CHECK_NEAR(notch.paceCrossSize, 5);
        CHECK_NEAR(notch.coreCrossSize, 5); CHECK_NEAR(notch.coreLength, 2);
        CHECK_NEAR(geometry(PaceTickStyle::Notch, 2).paceCrossSize, 2);
        CHECK_NEAR(geometry(PaceTickStyle::Notch, 50).paceCrossSize, 30);
        BarGeometry dot = geometry(PaceTickStyle::Dot, 8, 2);
        CHECK_NEAR(dot.paceLength, 2); CHECK_NEAR(dot.paceCrossSize, 2); CHECK_NEAR(dot.paceRadius, 1);
        CHECK_NEAR(dot.coreLength, 1.5); CHECK_NEAR(dot.coreCrossSize, 1.5);
        CHECK_NEAR(dot.coreRadius, 0.75); CHECK_NEAR(dot.textInset, 2);
    });
}

static void RunTileTests() {
    Run("mini bar and headline segment fills", [] {
        auto mini = [](double pct, const MeterDisplay& d = Display()) {
            return First({pct, 0, 0}, d, BarLayout::LogoBars).quotas[0];
        };
        CHECK(mini(50).fillUnits == 500); CHECK_NEAR(mini(50).fillLength, 19);
        CHECK(mini(1).fillUnits == 10); CHECK_NEAR(mini(1).fillLength, 4);
        CHECK(mini(0).fillUnits == 0); CHECK_NEAR(mini(0).fillLength, 0);
        CHECK(mini(120).fillUnits == 1000); CHECK_NEAR(mini(120).fillLength, 38);
        CHECK(mini(0.04).fillUnits == 1); CHECK_NEAR(mini(0.04).fillLength, 4);
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK(mini(30, remaining).fillUnits == 700); CHECK_NEAR(mini(30, remaining).fillLength, 26.6);
        auto segment = [](double pct) {
            return First({pct, 0, 0}, Display(), BarLayout::LogoHeadline).quotas[0].fillLength;
        };
        CHECK_NEAR(segment(50), 4.5); CHECK_NEAR(segment(10), 3);
        CHECK_NEAR(segment(100), 9); CHECK_NEAR(segment(0), 0);
    });

    Run("ring fills keep a 5-degree minimum per arc sweep", [] {
        auto single = [](double pct, BarLayout layout = BarLayout::SegmentedRing,
                         const MeterDisplay& d = Display()) {
            return First({pct, 0, 0}, d, layout).quotas[0];
        };
        CHECK(single(0.5).fillUnits == 14); CHECK_NEAR(single(0.5).fillSweep, 5);
        CHECK(single(50).fillUnits == 500); CHECK_NEAR(single(50).fillSweep, 180);
        CHECK(single(0).fillUnits == 0);
        CHECK(single(0.5, BarLayout::ConcentricRings).fillUnits == 14);
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK(single(99.9, BarLayout::SegmentedRing, remaining).fillUnits == 14);
        AccountMeters two = AccountMeterState(Display(), BarLayout::SegmentedRing,
            Account(L"anthropic", L"A", {true, true, false, false}), Fresh({1, 1, -1, -1}), 0, 0);
        CHECK(two.quotas[0].fillUnits == 33); CHECK_NEAR(two.quotas[0].fillSweep, 5);
        CHECK(two.quotas[1].fillUnits == 33); CHECK_NEAR(two.quotas[1].fillSweep, -5);
    });

    Run("ring arcs follow mask positions; concentric radii step inward", [] {
        auto arcs = [](BarLayout layout, std::array<bool, kQuotaBarCount> bars) {
            return AccountMeterState(Display(), layout, Account(L"anthropic", L"A", bars),
                                     Fresh({10, 20, 30, 40}), 0, 0);
        };
        auto arc = [](const QuotaMeter& m, double start, double sweep) {
            CHECK_NEAR(m.arc.start, start); CHECK_NEAR(m.arc.sweep, sweep);
        };
        AccountMeters gap = arcs(BarLayout::SegmentedRing, {true, false, false, true});
        arc(gap.quotas[0], 195, 150); arc(gap.quotas[3], 165, -150);
        AccountMeters three = arcs(BarLayout::SegmentedRing, {true, true, true, false});
        arc(three.quotas[0], 165, 90); arc(three.quotas[1], 135, -90); arc(three.quotas[2], 285, 90);
        AccountMeters four = arcs(BarLayout::SegmentedRing, {true, true, true, true});
        arc(four.quotas[0], 190, 70); arc(four.quotas[1], 280, 70);
        arc(four.quotas[2], 10, 70); arc(four.quotas[3], 100, 70);
        CHECK_NEAR(arcs(BarLayout::SegmentedRing, {true, false, false, false}).quotas[0].radius, 14);
        AccountMeters rings = arcs(BarLayout::ConcentricRings, {true, true, false, false});
        arc(rings.quotas[0], 270, 360); arc(rings.quotas[1], 270, 360);
        CHECK_NEAR(rings.quotas[0].radius, 14.195); CHECK_NEAR(rings.quotas[1].radius, 10.295);
    });

    Run("tile pace: thousandths, then offsets and angles from them", [] {
        auto mini = [](long long resetIn, const MeterDisplay& d = Display()) {
            QuotaMeter m = First(Window(10, resetIn), d, BarLayout::LogoBars).quotas[0];
            CHECK(m.paceVisible);
            return m;
        };
        CHECK(mini(13500000).paceUnits == 250); CHECK_NEAR(mini(13500000).paceOffset, 7.75);
        CHECK(mini(kFiveHours).paceUnits == 0); CHECK_NEAR(mini(kFiveHours).paceOffset, -1.75);
        CHECK(mini(0).paceUnits == 1000); CHECK_NEAR(mini(0).paceOffset, 36.25);
        CHECK(mini(12000000).paceUnits == 333); CHECK_NEAR(mini(12000000).paceOffset, 10.904);
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK(mini(13500000, remaining).paceUnits == 750);
        CHECK_NEAR(mini(13500000, remaining).paceOffset, 26.75);
        CHECK_NEAR(First(Window(10, 13500000), Display(), BarLayout::SegmentedRing)
                       .quotas[0].paceAngle, 360);
        AccountData data = Fresh();
        data.win5h = Window(10, 13500000);
        data.winWeek = Window(10, 13500000);
        AccountMeters two = AccountMeterState(Display(), BarLayout::SegmentedRing,
            Account(L"anthropic", L"A", {true, true, false, false}), data, 0, 0);
        CHECK_NEAR(two.quotas[1].paceAngle, 127.5);
        CHECK(!First(Window(10, 1000), Display(), BarLayout::LogoHeadline).quotas[0].paceVisible);
    });

    Run("ring capped by tile height; warning mark margin", [] {
        auto ring = [](int size, double taskbar) {
            MeterDisplay d = Display(With([&](Settings& s) { s.ringSize = size; }), 1, taskbar);
            return First({10, 0, 0}, d, BarLayout::SegmentedRing);
        };
        CHECK_NEAR(ring(40, 40).ring.size, 32); CHECK_NEAR(ring(40, 40).ringWarnTop, 1);
        CHECK_NEAR(ring(40, 32).ring.size, 24); CHECK_NEAR(ring(40, 32).ringWarnTop, 1);
        CHECK_NEAR(ring(40, 48).ring.size, 40); CHECK_NEAR(ring(40, 48).ringWarnTop, 1);
        CHECK_NEAR(ring(28, 48).ring.size, 28); CHECK_NEAR(ring(28, 48).ringWarnTop, -5);
        CHECK_NEAR(ring(32, 48).ring.size, 32); CHECK_NEAR(ring(32, 48).ringWarnTop, -3);
    });

    Run("ring geometry, badge and identity text fit", [] {
        auto ring = [](BarLayout layout, std::array<bool, kQuotaBarCount> bars, int badge = 0,
                       double textWidth = 0) {
            return AccountMeterState(Display(), layout, Account(L"anthropic", L"A", bars),
                                     Fresh({10, 20, 30, 40}), badge, textWidth);
        };
        AccountMeters single = ring(BarLayout::SegmentedRing, {true, false, false, false});
        CHECK_NEAR(single.ring.stroke, 3); CHECK_NEAR(single.ring.gap, 0);
        CHECK_NEAR(single.ring.radius, 14); CHECK_NEAR(single.ring.hole, 12.5);
        CHECK_NEAR(single.ring.dot, 1.71); CHECK_NEAR(single.ring.logo, 14.08);
        AccountMeters pair = ring(BarLayout::SegmentedRing, {true, true, false, false});
        CHECK_NEAR(pair.ring.stroke, 3); CHECK_NEAR(pair.ring.gap, 4.330383);
        AccountMeters concentric = ring(BarLayout::ConcentricRings, {true, true, false, false});
        CHECK_NEAR(concentric.ring.stroke, 2.61); CHECK_NEAR(concentric.ring.gap, 1.29);
        CHECK_NEAR(concentric.ring.hole, 8.99);
        auto fit = [&](double textWidth) {
            return ring(BarLayout::SegmentedRing, {true, false, false, false}, 0, textWidth);
        };
        CHECK_NEAR(fit(1).ringTextFont, 10.56); CHECK_NEAR(fit(1).ringTextWidth, 22.130326);
        CHECK_NEAR(fit(3).ringTextFont, 7.56); CHECK_NEAR(fit(3).ringTextWidth, 22.758110);
        CHECK_NEAR(fit(10).ringTextFont, 7); CHECK_NEAR(fit(10).ringTextWidth, 22.850786);
        CHECK_NEAR(fit(0).ringTextFont, 10.56);
        CHECK_NEAR(ring(BarLayout::SegmentedRing, {true, false, false, false}, 1).ring.badgeWidth, 10.88);
        CHECK_NEAR(ring(BarLayout::SegmentedRing, {true, false, false, false}, 2).ring.badgeWidth, 15.27808);
    });
}

static void RunAccountTests() {
    Run("stale rules, including Antigravity", [] {
        auto stale = [](std::wstring provider, ULONGLONG age, const MeterDisplay& d = Display()) {
            AccountData data = Fresh({10, -1, -1, -1});
            data.lastSuccessMs = kNow - age;
            return AccountMeterState(d, BarLayout::Stacked, Account(provider), data, 0, 0).stale;
        };
        CHECK(!stale(L"anthropic", 20 * kMinute)); CHECK(stale(L"anthropic", 20 * kMinute + 1));
        CHECK(!stale(L"openai", 20 * kMinute));
        CHECK(!stale(L"antigravity", 2 * kMinute)); CHECK(stale(L"antigravity", 2 * kMinute + 1));
        CHECK(stale(L"antigravity", 3 * kMinute)); CHECK(!stale(L"anthropic", 3 * kMinute));
        MeterDisplay fast = Display(With([](Settings& s) { s.pollMinutes = 2; }));
        CHECK(!stale(L"anthropic", 4 * kMinute, fast)); CHECK(stale(L"anthropic", 4 * kMinute + 1, fast));
        AccountData flagged = Fresh({10, -1, -1, -1});
        flagged.stale = true;
        CHECK(AccountMeterState(Display(), BarLayout::Stacked, Account(), flagged, 0, 0).stale);
        AccountData never = Fresh({10, -1, -1, -1});
        never.lastSuccessMs = 0;
        CHECK(AccountMeterState(Display(), BarLayout::Stacked, Account(), never, 0, 0).stale);
    });

    Run("warning mark, label and identity dimming", [] {
        auto meters = [](bool stale, std::wstring error, bool warning = true) {
            AccountData data = Fresh({10, -1, -1, -1});
            data.stale = stale;
            data.error = error;
            MeterDisplay d = Display(With([&](Settings& s) { s.showStaleWarning = warning; }));
            return AccountMeterState(d, BarLayout::Stacked, Account(), data, 0, 0);
        };
        AccountMeters warn = meters(true, L"timeout");
        CHECK(warn.warn); CHECK(warn.labelText == L"A!");
        CHECK_NEAR(warn.labelOpacity, 0.45); CHECK_NEAR(warn.identityOpacity, 0.45);
        CHECK(warn.quotas[0].color == 0xFF9E9E9E);
        AccountMeters quiet = meters(true, L"");
        CHECK(!quiet.warn); CHECK(quiet.labelText == L"A"); CHECK_NEAR(quiet.labelOpacity, 0.45);
        CHECK(!meters(true, L"timeout", false).warn);
        AccountMeters fresh = meters(false, L"timeout");
        CHECK(!fresh.warn); CHECK(fresh.labelText == L"A");
        CHECK_NEAR(fresh.labelOpacity, 0.8); CHECK_NEAR(fresh.identityOpacity, 1);
    });

    Run("quota mask before and after the first success", [] {
        AccountConfig account = Account(L"anthropic", L"A", {true, true, false, true});
        AccountData before = Fresh({10, -1, -1, -1});
        before.lastSuccessMs = 0;
        CHECK(AccountMeterState(Display(), BarLayout::Stacked, account, before, 0, 0).barMask == 0b1011);
        CHECK(AccountMeterState(Display(), BarLayout::Stacked, account, Fresh({10, -1, -1, -1}), 0, 0)
                  .barMask == 0b0001);
        CHECK(AccountMeterState(Display(), BarLayout::Stacked, account, Fresh({10, 20, 50, -1}), 0, 0)
                  .barMask == 0b0011);
        // Rings lay out every selected quota until then.
        AccountMeters ring = AccountMeterState(Display(), BarLayout::SegmentedRing, account, before, 0, 0);
        CHECK_NEAR(ring.quotas[3].arc.start, 285); CHECK(ring.quotas[3].fillUnits == 0);
    });

    Run("headline: most-used quota, its key and color", [] {
        auto headline = [](std::array<double, kQuotaBarCount> pct, std::wstring provider = L"anthropic",
                           std::array<bool, kQuotaBarCount> bars = {true, true, true, true},
                           BarMode mode = BarMode::Used) {
            MeterDisplay d = Display(With([&](Settings& s) { s.barMode = mode; }));
            return AccountMeterState(d, BarLayout::LogoHeadline, Account(provider, L"A", bars),
                                     Fresh(pct), 0, 0);
        };
        AccountMeters tie = headline({40, 80, 80, 20});
        CHECK(tie.headline == L"80%"); CHECK(tie.headlineKey == L"7d"); CHECK(tie.headlineColor == 0xFFFB8C00);
        AccountMeters credits = headline({10, 20, -1, 95}, L"openai", {true, true, false, true});
        CHECK(credits.headline == L"95%"); CHECK(credits.headlineKey == L"Cr");
        CHECK(credits.headlineColor == 0xFFE53935);
        CHECK(headline({10, 20, -1, 95}, L"anthropic", {true, true, false, true}).headlineKey == L"Ex");
        AccountMeters left = headline({40, 80, 80, 20}, L"anthropic", {true, true, true, true},
                                      BarMode::Remaining);
        CHECK(left.headline == L"20%"); CHECK(left.headlineKey == L"7d"); CHECK(left.headlineColor == 0xFFFB8C00);
        CHECK(headline({130, -1, -1, -1}).headline == L"100%");
        CHECK(headline({130, -1, -1, -1}, L"anthropic", {true, true, true, true}, BarMode::Remaining)
                  .headline == L"0%");
        AccountMeters none = headline({-1, -1, -1, -1});
        CHECK(none.headline == L"\u2013"); CHECK(none.headlineKey.empty()); CHECK(none.headlineColor == 0xFF9E9E9E);
        AccountData before = Fresh();
        before.lastSuccessMs = 0;
        CHECK(AccountMeterState(Display(), BarLayout::LogoHeadline, Account(), before, 0, 0).headline ==
              L"\u2013");
        AccountMeters selected = headline({10, 90, -1, -1}, L"anthropic", {true, false, false, false});
        CHECK(selected.headline == L"10%"); CHECK(selected.headlineKey == L"5h");
        AccountData stale = Fresh({40, 80, -1, -1});
        stale.stale = true;
        AccountMeters gray = AccountMeterState(Display(), BarLayout::LogoHeadline, Account(), stale, 0, 0);
        CHECK(gray.headline == L"80%"); CHECK(gray.headlineColor == 0xFF9E9E9E);
    });
}

static void RunIdentityTests() {
    Run("tile identity: number badges", [] {
        std::vector<AccountConfig> mixed = {Account(L"anthropic", L"CC 1"), Account(L"anthropic", L"CC 2"),
                                            Account(L"openai", L"OAI"), Account(L"antigravity", L"Go")};
        CHECK(TileIdentityFor(mixed, 0).badge); CHECK(TileIdentityFor(mixed, 0).badgeText == L"1");
        CHECK(TileIdentityFor(mixed, 1).badgeText == L"2");
        CHECK(!TileIdentityFor(mixed, 2).badge); CHECK(!TileIdentityFor(mixed, 3).badge);
        auto badges = [](std::wstring a, std::wstring b) {
            std::vector<AccountConfig> pair = {Account(L"anthropic", a), Account(L"anthropic", b)};
            return std::pair{TileIdentityFor(pair, 0).badgeText, TileIdentityFor(pair, 1).badgeText};
        };
        CHECK((badges(L"Work", L"home") == std::pair<std::wstring, std::wstring>{L"W", L"H"}));
        CHECK((badges(L"acct 123", L"x") == std::pair<std::wstring, std::wstring>{L"23", L"X"}));
        CHECK((badges(L"#!", L"--") == std::pair<std::wstring, std::wstring>{L"#", L"-"}));
        CHECK((badges(L"", L"") == std::pair<std::wstring, std::wstring>{L"1", L"2"}));
        std::vector<AccountConfig> plain = {Account(L"anthropic", L"A"), Account(L"anthropic", L"B")};
        plain[0].tileIdentity = TileIdentity::Logo;
        plain[1].tileIdentity = TileIdentity::Text;
        CHECK(!TileIdentityFor(plain, 0).badge); CHECK(!TileIdentityFor(plain, 1).badge);
    });

    Run("tile identity: text", [] {
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"Long label"),
                                               Account(L"openai", L"C C 1"), Account(L"openai", L"   ")};
        accounts[0].tileText = L"AB";
        CHECK(TileIdentityFor(accounts, 0).text == L"AB");
        CHECK(TileIdentityFor(accounts, 1).text == L"CC1");
        CHECK(TileIdentityFor(accounts, 2).text == L"3");
    });
}

static void RunSnapshotTests() {
    Run("visual test snapshot from the display thresholds", [] {
        std::vector<AccountConfig> accounts;
        std::vector<AccountData> data;
        BuildVisualTestSnapshot(Display(), &accounts, &data);
        CHECK(accounts.size() == 4); CHECK(data.size() == 4);
        CHECK(accounts[0].provider == L"openai"); CHECK(accounts[0].label == L"OAI");
        CHECK(accounts[1].label == L"CC 1"); CHECK(accounts[2].label == L"CC 2");
        CHECK(accounts[3].provider == L"antigravity");
        CHECK((accounts[0].showBars == std::array<bool, kQuotaBarCount>{true, true, false, true}));
        CHECK(accounts[0].creditsMax == 1000);
        CHECK_NEAR(data[0].win5h.pct, 25); CHECK_NEAR(data[0].winWeek.pct, 62.5);
        CHECK_NEAR(data[0].extraUsage.pct, 12.5); CHECK_NEAR(data[0].extraUsedAmount, 125);
        CHECK_NEAR(data[0].extraLimitAmount, 1000); CHECK_NEAR(data[0].creditsBalance, 875);
        CHECK(data[0].win5h.resetUnixMs == kNow + 6300000); CHECK(data[0].win5h.windowDurationMs == kFiveHours);
        CHECK_NEAR(data[1].win5h.pct, 82.5); CHECK_NEAR(data[1].winWeek.pct, 25);
        CHECK_NEAR(data[2].winWeek.pct, 95); CHECK_NEAR(data[2].fableWeek.pct, 62.5);
        CHECK_NEAR(data[2].extraUsedAmount, 12.5); CHECK_NEAR(data[2].extraLimitAmount, 50);
        CHECK(data[2].winWeek.resetUnixMs == kNow + 241920000);
        CHECK_NEAR(data[3].antigravityThirdParty5h.pct, 62.5);
        for (const AccountData& d : data) {
            CHECK(d.lastSuccessMs == kNow); CHECK(!d.stale); CHECK(d.plan == L"Visual test");
        }
        std::vector<AccountConfig> only;
        BuildVisualTestSnapshot(Display(), &only, nullptr);
        CHECK(only.size() == 4);
        BuildVisualTestSnapshot(Display(With([](Settings& s) {
            s.yellowThreshold = 80; s.orangeThreshold = 60; s.redThreshold = 70;
        })), &accounts, &data);
        CHECK_NEAR(data[0].win5h.pct, 40); CHECK_NEAR(data[0].winWeek.pct, 80);
        CHECK_NEAR(data[2].winWeek.pct, 90);
    });
}

static void RunMeterTests() {
    RunDisplayTests();
    RunColorTests();
    RunBarTests();
    RunTileTests();
    RunAccountTests();
    RunIdentityTests();
    RunSnapshotTests();
}
