// Meter model: display entry clamps, bars (time tracks included), tiles (gauges, pies, rounded
// squares, sparklines and the quota matrix included), account state, the headline countdown,
// identity and visual test data.
// Expected values are derived by hand from the documented behavior.

static void RunDisplayTests() {
    Run("display: in-range settings pass through", [] {
        Settings settings = With([](Settings& s) {
            s.barMode = BarMode::Remaining; s.barLength = 120; s.barThickness = 6;
            s.ringSize = 28; s.ringThickness = 4; s.yellowThreshold = 40;
            s.orangeThreshold = 60; s.redThreshold = 80; s.pollMinutes = 15;
            s.colorblindMode = true; s.showPaceTicks = false; s.paceTickStyle = PaceTickStyle::Dot;
            s.paceTickColor = RGB(1, 2, 3); s.percentTextAlignment = PercentTextAlignment::Center;
            s.showExtraBarAmounts = true; s.showStaleWarning = false; s.barGap = 7; s.barStyle = BarStyle::Rail;
            s.headlineFontSize = 18; s.headlineBarLength = 24; s.headlineBarThickness = 6;
        });
        MeterDisplay d = MeterDisplayFor(settings, kNow, 1.25, 44);
        CHECK(d.now == kNow); CHECK(d.barMode == BarMode::Remaining);
        CHECK(d.barLength == 120); CHECK(d.barThickness == 6);
        CHECK(d.barGap == 7); CHECK(d.barStyle == BarStyle::Rail);
        CHECK(d.ringSize == 28); CHECK(d.ringThickness == 4);
        CHECK(d.headlineFontSize == 18); CHECK(d.headlineBarLength == 24); CHECK(d.headlineBarThickness == 6);
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
            s.barGap = -5; s.barStyle = BarStyle(-1);
            s.headlineFontSize = 0; s.headlineBarLength = -1; s.headlineBarThickness = 1;
        }));
        CHECK(low.barLength == 10); CHECK(low.barThickness == 2);
        CHECK(low.barGap == 0); CHECK(low.barStyle == BarStyle::Track);
        CHECK(low.ringSize == 24); CHECK(low.ringThickness == 1); CHECK(low.pollMinutes == 2);
        CHECK(low.headlineFontSize == 10); CHECK(low.headlineBarLength == 4); CHECK(low.headlineBarThickness == 2);
        CHECK(low.yellowThreshold == 0); CHECK(low.orangeThreshold == 0); CHECK(low.redThreshold == 0);
        MeterDisplay high = Display(With([](Settings& s) {
            s.barLength = 9999; s.barThickness = 51; s.ringSize = 41; s.ringThickness = 7;
            s.pollMinutes = 100000; s.yellowThreshold = 150; s.orangeThreshold = 120;
            s.redThreshold = 101; s.paceTickColor = 0xAB123456; s.barGap = 9999; s.barStyle = BarStyle(9);
            s.headlineFontSize = 25; s.headlineBarLength = 61; s.headlineBarThickness = 11;
        }));
        CHECK(high.barLength == 500); CHECK(high.barThickness == 50);
        CHECK(high.barGap == 500); CHECK(high.barStyle == BarStyle::Track);
        CHECK(high.ringSize == 40); CHECK(high.ringThickness == 6); CHECK(high.pollMinutes == 1440);
        CHECK(high.headlineFontSize == 24); CHECK(high.headlineBarLength == 60); CHECK(high.headlineBarThickness == 10);
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
        CHECK(light.open == 0xB3FFFFFF); CHECK(light.badgeHalo == 0xFFF3F3F3);
        CHECK(dark.track == 0x2BFFFFFF); CHECK(dark.hover == 0x0FFFFFFF);
        CHECK(dark.open == 0x1AFFFFFF); CHECK(dark.badgeHalo == 0xFF1C1C1C);
        // Sparklines: the plot (darker than the taskbar), its lit part and outline, the now line, and
        // the caret and ring.
        CHECK(light.plot == 0x14000000); CHECK(dark.plot == 0x59000000);
        CHECK(light.lit == 0x12000000); CHECK(dark.lit == 0x1FFFFFFF);
        CHECK(light.outline == 0x24000000); CHECK(dark.outline == 0x2EFFFFFF);
        CHECK(light.nowLine == 0x80000000); CHECK(dark.nowLine == 0x80FFFFFF);
        CHECK(light.nowMark == 0xF0000000); CHECK(dark.nowMark == 0xF0FFFFFF);
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
        // From 1,000 on: three significant digits with k or M; amounts that round into the next
        // suffix read as 1 of it.
        auto spent = [&](std::wstring provider, double used) {
            return amount(provider, 25, used, 1e10).quotas[3].text;
        };
        CHECK(spent(L"openai", 999.4) == L"999"); CHECK(spent(L"openai", 999.6) == L"1k");
        CHECK(spent(L"openai", 1234) == L"1.23k"); CHECK(spent(L"openai", 62500) == L"62.5k");
        CHECK(spent(L"openai", 110616) == L"111k"); CHECK(spent(L"openai", 999400) == L"999k");
        CHECK(spent(L"openai", 999600) == L"1M"); CHECK(spent(L"openai", 1250000) == L"1.25M");
        CHECK(spent(L"openai", 2.5e9) == L"2500M");
        CHECK(spent(L"anthropic", 999.99) == L"$999.99"); CHECK(spent(L"anthropic", 1500) == L"$1.5k");
        CHECK(spent(L"anthropic", 999.996) == L"$1k");
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
        // Caret 5 DIPs on a 100 DIP bar 8 thick (corners 2 DIPs): centre kept 2.5 DIPs from
        // either end.
        CHECK(pace(kFiveHours) == 0); CHECK(pace(kFiveHours, remaining) == 95);
        CHECK(pace(0) == 95); CHECK(pace(0, remaining) == 0);
        CHECK(pace(13500000) == 23); CHECK(pace(13500000, remaining) == 73);
        CHECK(pace(-1000) == 95);
    });

    Run("bar pace: clamped by tick half-size and bar radius", [] {
        auto pace = [](long long resetIn, const MeterDisplay& d) {
            return First(Window(10, resetIn), d).quotas[0].pacePx;
        };
        // Corners are a quarter of the thickness; the centre keeps the larger of that and half
        // the tick from either end.
        MeterDisplay thin = Display(With([](Settings& s) { s.barThickness = 2; }));
        CHECK(pace(kFiveHours, thin) == 0); CHECK(pace(0, thin) == 95);
        // 20 thick: centre 5 from either end, 2.5 and 92.5 rounded away from zero.
        MeterDisplay thick = Display(With([](Settings& s) { s.barThickness = 20; }));
        CHECK(pace(kFiveHours, thick) == 3); CHECK(pace(0, thick) == 93);
        MeterDisplay line = Display(With([](Settings& s) { s.paceTickStyle = PaceTickStyle::Line; }));
        CHECK(pace(kFiveHours, line) == 0); CHECK(pace(0, line) == 96);
        // 200%: a 2.5 DIP caret centred 2 DIPs from either end, 0.75 and 96.75 rounded.
        MeterDisplay hiDpi = Display({}, 2);
        CHECK(pace(kFiveHours, hiDpi) == 1); CHECK(pace(0, hiDpi) == 97);
        // The thin rail's fill has the same corners.
        MeterDisplay rail = Display(With([](Settings& s) { s.barStyle = BarStyle::Rail; s.barThickness = 20; }));
        CHECK(pace(kFiveHours, rail) == 3); CHECK(pace(0, rail) == 93);
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
        auto geometry = [](PaceTickStyle style, int thickness = 8, double scale = 1,
                           BarStyle bars = BarStyle::Track) {
            return BarGeometryFor(Display(With([&](Settings& s) {
                s.paceTickStyle = style; s.barThickness = thickness; s.barStyle = bars;
            }), scale));
        };
        auto same = [](const std::array<Point, 12>& actual, const std::array<Point, 12>& expected) {
            for (size_t i = 0; i < actual.size(); i++) {
                CHECK_NEAR(actual[i].along, expected[i].along);
                CHECK_NEAR(actual[i].cross, expected[i].cross);
            }
        };
        // Over a full track with the default 2 DIP gap the caret reaches a DIP past each side:
        // 8 + 2 across, its caps in the overhang.
        BarGeometry caret = geometry(PaceTickStyle::Caret);
        CHECK_NEAR(caret.overhang, 1);
        CHECK_NEAR(caret.paceLength, 5); CHECK_NEAR(caret.paceCrossSize, 10); CHECK_NEAR(caret.textInset, 4);
        same(caret.caret[0], {{{0, 0}, {5, 0}, {5, 2}, {4, 2}, {4, 8}, {5, 8}, {5, 10}, {0, 10},
                               {0, 8}, {1, 8}, {1, 2}, {0, 2}}});
        same(caret.caret[1], {{{1, 0}, {4, 0}, {4, 1}, {3, 1}, {3, 9}, {4, 9}, {4, 10}, {1, 10},
                               {1, 9}, {2, 9}, {2, 1}, {1, 1}}});
        // Over a thin rail it spans the bar only.
        BarGeometry railCaret = geometry(PaceTickStyle::Caret, 8, 1, BarStyle::Rail);
        CHECK_NEAR(railCaret.overhang, 0); CHECK_NEAR(railCaret.paceCrossSize, 8);
        same(railCaret.caret[0], {{{0, 0}, {5, 0}, {5, 2}, {4, 2}, {4, 6}, {5, 6}, {5, 8}, {0, 8},
                                   {0, 6}, {1, 6}, {1, 2}, {0, 2}}});
        // Caps no deeper than half the caret: 2 across on a 2 DIP rail bar, 4 on a track's.
        BarGeometry thinCaret = geometry(PaceTickStyle::Caret, 2, 1, BarStyle::Rail);
        CHECK_NEAR(thinCaret.caret[0][2].cross, 1); CHECK_NEAR(thinCaret.caret[1][2].cross, 1);
        BarGeometry thinTrackCaret = geometry(PaceTickStyle::Caret, 2);
        CHECK_NEAR(thinTrackCaret.paceCrossSize, 4);
        CHECK_NEAR(thinTrackCaret.caret[0][2].cross, 2); CHECK_NEAR(thinTrackCaret.caret[1][2].cross, 1);
        CHECK_NEAR(caret.coreCrossOffset, 0);
        // Line: a 4 px halo across the bar and its overhang (8 + 2), the 2 px core a pixel short
        // of it at both ends; square over a full track.
        BarGeometry line = geometry(PaceTickStyle::Line);
        CHECK_NEAR(line.paceLength, 4); CHECK_NEAR(line.paceCrossSize, 10);
        CHECK_NEAR(line.coreLength, 2); CHECK_NEAR(line.coreCrossSize, 8); CHECK_NEAR(line.coreCrossOffset, 1);
        CHECK_NEAR(line.paceRadius, 0); CHECK_NEAR(line.coreRadius, 0);
        // Over a thin rail: the bar's 8 across, 1 px halo corners and 0.5 px core corners.
        BarGeometry railLine = geometry(PaceTickStyle::Line, 8, 1, BarStyle::Rail);
        CHECK_NEAR(railLine.paceCrossSize, 8); CHECK_NEAR(railLine.coreCrossSize, 6);
        CHECK_NEAR(railLine.coreCrossOffset, 1);
        CHECK_NEAR(railLine.paceRadius, 1); CHECK_NEAR(railLine.coreRadius, 0.5);
        // At 200% a pixel is half a DIP.
        BarGeometry railLine2x = geometry(PaceTickStyle::Line, 8, 2, BarStyle::Rail);
        CHECK_NEAR(railLine2x.coreCrossSize, 7); CHECK_NEAR(railLine2x.coreCrossOffset, 0.5);
        CHECK_NEAR(railLine2x.paceRadius, 0.5); CHECK_NEAR(railLine2x.coreRadius, 0.25);
        // A 2 DIP rail bar at 100% leaves no room for the core.
        CHECK_NEAR(geometry(PaceTickStyle::Line, 2, 1, BarStyle::Rail).coreCrossSize, 0);
        // A notch's core starts at the overhang's outer edge and keeps its depth into the bar,
        // max(2, ceil(0.6 x thickness)); its halo reaches a pixel deeper.
        BarGeometry notch = geometry(PaceTickStyle::Notch);
        CHECK_NEAR(notch.overhang, 1);
        CHECK_NEAR(notch.paceLength, 4); CHECK_NEAR(notch.paceCrossSize, 1 + 5 + 1);
        CHECK_NEAR(notch.coreCrossSize, 1 + 5); CHECK_NEAR(notch.coreLength, 2);
        CHECK_NEAR(notch.coreCrossOffset, 0); CHECK_NEAR(notch.paceRadius, 0); CHECK_NEAR(notch.coreRadius, 0);
        CHECK_NEAR(geometry(PaceTickStyle::Notch, 2).paceCrossSize, 1 + 2 + 1);
        CHECK_NEAR(geometry(PaceTickStyle::Notch, 50).coreCrossSize, 1 + 30);
        CHECK_NEAR(geometry(PaceTickStyle::Notch, 50).paceCrossSize, 1 + 30 + 1);
        BarGeometry railNotch = geometry(PaceTickStyle::Notch, 8, 1, BarStyle::Rail);
        CHECK_NEAR(railNotch.coreCrossSize, 5); CHECK_NEAR(railNotch.paceCrossSize, 6);
        // 200% with the default gap: a one DIP (2 px) overhang, a half-DIP extra halo.
        BarGeometry notch2x = geometry(PaceTickStyle::Notch, 8, 2);
        CHECK_NEAR(notch2x.coreCrossSize, 1 + 5); CHECK_NEAR(notch2x.paceCrossSize, 1 + 5 + 0.5);
        // Dots never overhang; the 3 px core sits half a pixel in.
        BarGeometry dot = geometry(PaceTickStyle::Dot, 8, 2);
        CHECK_NEAR(dot.overhang, 0);
        CHECK_NEAR(dot.paceLength, 2); CHECK_NEAR(dot.paceCrossSize, 2); CHECK_NEAR(dot.paceRadius, 1);
        CHECK_NEAR(dot.coreLength, 1.5); CHECK_NEAR(dot.coreCrossSize, 1.5); CHECK_NEAR(dot.coreCrossOffset, 0.25);
        CHECK_NEAR(dot.coreRadius, 0.75); CHECK_NEAR(dot.textInset, 2);
        CHECK_NEAR(geometry(PaceTickStyle::Dot, 8, 2, BarStyle::Rail).paceRadius, 1);
    });

    Run("bar geometry: tick overhang in whole device pixels, at most half the gap", [] {
        auto pixels = [](int gap, double scale, BarStyle bars = BarStyle::Track,
                         PaceTickStyle style = PaceTickStyle::Line) {
            BarGeometry g = BarGeometryFor(Display(With([&](Settings& s) {
                s.barGap = gap; s.barStyle = bars; s.paceTickStyle = style;
            }), scale));
            return g.overhang * scale;
        };
        // min(round(scale), floor(round(gap x scale) / 2)) device pixels.
        // 100%: one pixel from a 2 DIP gap; a 1 DIP gap has no half pixel to give.
        CHECK_NEAR(pixels(0, 1), 0); CHECK_NEAR(pixels(1, 1), 0);
        CHECK_NEAR(pixels(2, 1), 1); CHECK_NEAR(pixels(4, 1), 1);
        // 125%: gaps of 1.25, 2.5 and 5 pixels round to 1, 3 and 5; a DIP rounds to one pixel.
        CHECK_NEAR(pixels(0, 1.25), 0); CHECK_NEAR(pixels(1, 1.25), 0);
        CHECK_NEAR(pixels(2, 1.25), 1); CHECK_NEAR(pixels(4, 1.25), 1);
        // 150%: gaps of 1.5, 3 and 6 pixels round to 2, 3 and 6; a DIP rounds to two pixels.
        CHECK_NEAR(pixels(0, 1.5), 0); CHECK_NEAR(pixels(1, 1.5), 1);
        CHECK_NEAR(pixels(2, 1.5), 1); CHECK_NEAR(pixels(4, 1.5), 2);
        // In DIPs at 150%: 2 / 1.5.
        CHECK_NEAR(BarGeometryFor(Display(With([](Settings& s) { s.barGap = 4; }), 1.5)).overhang, 2 / 1.5);
        // None over a thin rail, none for dots.
        for (double scale : {1.0, 1.25, 1.5}) {
            CHECK_NEAR(pixels(4, scale, BarStyle::Rail), 0);
            CHECK_NEAR(pixels(4, scale, BarStyle::Track, PaceTickStyle::Dot), 0);
            CHECK_NEAR(pixels(4, scale, BarStyle::Track, PaceTickStyle::Caret), pixels(4, scale));
            CHECK_NEAR(pixels(4, scale, BarStyle::Track, PaceTickStyle::Notch), pixels(4, scale));
        }
    });

    Run("bar geometry: corners a quarter of the thickness; the thin rail's thickness", [] {
        auto geometry = [](int thickness, BarStyle bars) {
            return BarGeometryFor(Display(With([&](Settings& s) { s.barThickness = thickness; s.barStyle = bars; })));
        };
        CHECK_NEAR(geometry(8, BarStyle::Track).radius, 2); CHECK_NEAR(geometry(2, BarStyle::Track).radius, 0.5);
        CHECK_NEAR(geometry(50, BarStyle::Rail).radius, 12.5);
        // max(1, round(thickness / 4)) DIPs; no rail over a full track.
        CHECK_NEAR(geometry(2, BarStyle::Rail).rail, 1); CHECK_NEAR(geometry(4, BarStyle::Rail).rail, 1);
        CHECK_NEAR(geometry(8, BarStyle::Rail).rail, 2); CHECK_NEAR(geometry(12, BarStyle::Rail).rail, 3);
        CHECK_NEAR(geometry(8, BarStyle::Track).rail, 0);
    });

    Run("bar fill: end corners cut flat until the track's rounded end; round over a rail", [] {
        // 100 DIP bar, 8 thick: 2 DIP corners. The end's radius is what's left of the track's
        // corner past the fill.
        CHECK_NEAR(Bar(100).fillEndRadius, 2); CHECK_NEAR(Bar(150).fillEndRadius, 2);
        CHECK_NEAR(Bar(99).fillEndRadius, 1); CHECK_NEAR(Bar(98).fillEndRadius, 0);
        CHECK_NEAR(Bar(50).fillEndRadius, 0); CHECK_NEAR(Bar(0.4).fillEndRadius, 0);
        MeterDisplay thick = Display(With([](Settings& s) { s.barThickness = 20; s.barLength = 60; }));
        CHECK_NEAR(Bar(95, thick).fillEndRadius, 5 - 3);  // 57 of 60 DIPs, 5 DIP corners
        MeterDisplay rail = Display(With([](Settings& s) { s.barStyle = BarStyle::Rail; }));
        CHECK_NEAR(Bar(50, rail).fillEndRadius, 2); CHECK_NEAR(Bar(100, rail).fillEndRadius, 2);
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK_NEAR(Bar(1, remaining).fillEndRadius, 1);  // 99 DIPs left
    });

    Run("bar fill highlight: OKLCH lightness + 0.06 for every fill color, alpha kept", [] {
        // Hand-picked expectations per usage color (normal, stale, colorblind), within one step
        // per channel.
        const std::pair<uint32_t, uint32_t> colors[] = {
            {0xFF43A047, 0xFF57B359}, {0xFFFDD835, 0xFFFFEEAE}, {0xFFFB8C00, 0xFFFFA960},
            {0xFFE53935, 0xFFFB5048}, {0xFF9E9E9E, 0xFFB1B1B1}, {0xFF0072B2, 0xFF2484C6},
            {0xFF56B4E9, 0xFF6AC7FD}, {0xFFE69F00, 0xFFFAB22F}, {0xFFD55E00, 0xFFEA7126},
        };
        for (auto [color, expected] : colors) {
            uint32_t actual = UsageHighlight(color);
            CHECK(actual >> 24 == expected >> 24);
            for (int shift : {16, 8, 0}) {
                CHECK(std::abs(int(actual >> shift & 0xFF) - int(expected >> shift & 0xFF)) <= 1);
            }
        }
        CHECK(UsageHighlight(0x80E53935) >> 24 == 0x80);
        CHECK((UsageHighlight(0x80E53935) & 0xFFFFFF) == (UsageHighlight(0xFFE53935) & 0xFFFFFF));
        // Grey stays grey: no chroma to keep.
        uint32_t grey = UsageHighlight(0xFF9E9E9E);
        CHECK((grey >> 16 & 0xFF) == (grey >> 8 & 0xFF) && (grey >> 8 & 0xFF) == (grey & 0xFF));
        // White can't get lighter: still in range.
        CHECK(UsageHighlight(0xFFFFFFFF) == 0xFFFFFFFF);
    });

    Run("bar pace colors: the default turns dark with a light halo on a light taskbar", [] {
        PaceMarkColors dark = PaceMarkColorsFor(kDefaultPaceTickColor, false);
        CHECK(dark.core == 0xFFDEDEDE && dark.halo == 0xC0000000);
        PaceMarkColors light = PaceMarkColorsFor(kDefaultPaceTickColor, true);
        CHECK(light.core == 0xFF1A1A1A && light.halo == 0x80FFFFFF);
        // Stray high bits still name the default.
        CHECK(PaceMarkColorsFor(0xAB000000 | kDefaultPaceTickColor, true).core == 0xFF1A1A1A);
        // A chosen color stays, with the dark halo, on either taskbar.
        for (bool theme : {false, true}) {
            PaceMarkColors pink = PaceMarkColorsFor(RGB(0xFF, 0x6E, 0xC7), theme);
            CHECK(pink.core == 0xFFFF6EC7 && pink.halo == 0xC0000000);
        }
    });

    Run("badge colors: white inherits the taskbar's text; any other color gets a contrasting outline", [] {
        // White: no text color (inherited), outlined in the taskbar's own color.
        BadgeColors dark = BadgeColorsFor(RGB(0xFF, 0xFF, 0xFF), false);
        CHECK(dark.text == 0 && dark.halo == 0xFF1C1C1C);
        BadgeColors light = BadgeColorsFor(RGB(0xFF, 0xFF, 0xFF), true);
        CHECK(light.text == 0 && light.halo == 0xFFF3F3F3);
        CHECK(BadgeColorsFor(0xAB000000 | RGB(0xFF, 0xFF, 0xFF), true).text == 0);
        // Any other color stays on either taskbar: light ones outlined dark, dark ones light.
        for (bool theme : {false, true}) {
            BadgeColors blue = BadgeColorsFor(RGB(0x4C, 0xC2, 0xFF), theme);
            CHECK(blue.text == 0xFF4CC2FF && blue.halo == 0xFF1C1C1C);
            BadgeColors black = BadgeColorsFor(RGB(0x1C, 0x1C, 0x1C), theme);
            CHECK(black.text == 0xFF1C1C1C && black.halo == 0xFFF3F3F3);
        }
        // The outline flips below mid luma (0.299 R + 0.587 G + 0.114 B < 128).
        CHECK(BadgeColorsFor(RGB(0x80, 0x80, 0x80), true).halo == 0xFF1C1C1C);
        CHECK(BadgeColorsFor(RGB(0x7F, 0x7F, 0x7F), false).halo == 0xFFF3F3F3);
    });
}

static void RunTimeTrackTests() {
    Run("time track: a small bar a DIP's pixels from the bar, a quarter of its thickness in whole pixels", [] {
        auto geometry = [](int thickness, double scale = 1, BarStyle bars = BarStyle::Track, int gap = 2,
                           PaceTickStyle style = PaceTickStyle::TimeTrack, bool on = true) {
            return BarGeometryFor(Display(With([&](Settings& s) {
                s.paceTickStyle = style; s.barThickness = thickness; s.barStyle = bars; s.barGap = gap;
                s.showPaceTicks = on;
            }), scale));
        };
        // 8 thick at 100%: 1 DIP from the bar, 2 px across; no tick overhang.
        BarGeometry g = geometry(8);
        CHECK_NEAR(g.overhang, 0); CHECK_NEAR(g.timeGap, 1); CHECK_NEAR(g.timeThickness, 2);
        // A 4 DIP gap gives a line a pixel of overhang, the time track none; a thin rail the same sizes.
        CHECK_NEAR(geometry(8, 1, BarStyle::Track, 4).overhang, 0);
        BarGeometry rail = geometry(8, 1, BarStyle::Rail);
        CHECK_NEAR(rail.overhang, 0); CHECK_NEAR(rail.timeGap, 1); CHECK_NEAR(rail.timeThickness, 2);
        // 2, 9, 10 and 50 thick: 0.5, 2.25, 2.5 and 12.5 px round to 1, 2, 3 and 13.
        CHECK_NEAR(geometry(2).timeThickness, 1); CHECK_NEAR(geometry(9).timeThickness, 2);
        CHECK_NEAR(geometry(10).timeThickness, 3); CHECK_NEAR(geometry(50).timeThickness, 13);
        CHECK_NEAR(geometry(50).timeGap, 1);
        // 150%: 1.5 px of gap round to 2 (4/3 DIP), 3 px across are 2 DIPs. 125%: one pixel of gap
        // (0.8 DIP), 2.5 px round to 3 (2.4 DIPs). 200%, 2 thick: two pixels of gap, one across.
        BarGeometry wide = geometry(8, 1.5);
        CHECK_NEAR(wide.timeGap, 2 / 1.5); CHECK_NEAR(wide.timeThickness, 2);
        BarGeometry odd = geometry(8, 1.25);
        CHECK_NEAR(odd.timeGap, 0.8); CHECK_NEAR(odd.timeThickness, 2.4);
        BarGeometry fine = geometry(2, 2);
        CHECK_NEAR(fine.timeGap, 1); CHECK_NEAR(fine.timeThickness, 0.5);
        // No small bar with the markers off or another style.
        BarGeometry off = geometry(8, 1, BarStyle::Track, 2, PaceTickStyle::TimeTrack, false);
        CHECK_NEAR(off.timeGap, 0); CHECK_NEAR(off.timeThickness, 0);
        BarGeometry line = geometry(8, 1, BarStyle::Track, 2, PaceTickStyle::Line);
        CHECK_NEAR(line.timeGap, 0); CHECK_NEAR(line.timeThickness, 0);
    });

    Run("time track: the rail's length from the bar's start in both modes, hidden below a DIP", [] {
        MeterDisplay used = Display(With([](Settings& s) { s.paceTickStyle = PaceTickStyle::TimeTrack; }));
        MeterDisplay left = Display(With([](Settings& s) {
            s.paceTickStyle = PaceTickStyle::TimeTrack; s.barMode = BarMode::Remaining;
        }));
        auto rail = [](long long resetIn, const MeterDisplay& d, BarLayout layout = BarLayout::Stacked) {
            return First(Window(10, resetIn), d, layout).quotas[0];
        };
        // A quarter of the 5-hour window elapsed: 25 of 100 DIPs used, 75 left.
        CHECK(rail(13500000, used).paceVisible && rail(13500000, used).pacePx == 25);
        CHECK(rail(13500000, left).paceVisible && rail(13500000, left).pacePx == 75);
        // Unlike ticks, not kept off the bar's rounded ends: the whole bar.
        CHECK(rail(0, used).paceVisible && rail(0, used).pacePx == 100);
        CHECK(rail(kFiveHours, left).paceVisible && rail(kFiveHours, left).pacePx == 100);
        // Nothing elapsed (used) or nothing left (remaining): no rail.
        CHECK(!rail(kFiveHours, used).paceVisible && rail(kFiveHours, used).pacePx == 0);
        CHECK(!rail(0, left).paceVisible);
        // 0.4% of the bar rounds to nothing, 0.6% to a DIP.
        CHECK(!rail(17928000, used).paceVisible);
        CHECK(rail(17892000, used).paceVisible && rail(17892000, used).pacePx == 1);
        // Vertical bars and thin rails: the same length. A 10 DIP bar: 2.5 rounds to 3.
        CHECK(rail(13500000, used, BarLayout::Vertical).pacePx == 25);
        MeterDisplay thinRail = Display(With([](Settings& s) {
            s.paceTickStyle = PaceTickStyle::TimeTrack; s.barStyle = BarStyle::Rail;
        }));
        CHECK(rail(13500000, thinRail).pacePx == 25);
        MeterDisplay shortBar = Display(With([](Settings& s) { s.paceTickStyle = PaceTickStyle::TimeTrack; s.barLength = 10; }));
        CHECK(rail(13500000, shortBar).pacePx == 3);
        // Markers off: none.
        MeterDisplay off = Display(With([](Settings& s) { s.paceTickStyle = PaceTickStyle::TimeTrack; s.showPaceTicks = false; }));
        CHECK(!rail(13500000, off).paceVisible);
        // Tiles keep their own ticks.
        QuotaMeter ring = rail(13500000, used, BarLayout::SegmentedRing);
        CHECK(ring.paceVisible && ring.paceUnits == 250 && !ring.paceHalo.empty());
    });

    Run("time track: the fill's far end cut flat until it comes within half the small bar of its end", [] {
        auto rail = [](long long resetIn, int thickness = 8, BarMode mode = BarMode::Used) {
            MeterDisplay d = Display(With([&](Settings& s) {
                s.paceTickStyle = PaceTickStyle::TimeTrack; s.barThickness = thickness; s.barMode = mode;
            }));
            return First(Window(10, resetIn), d).quotas[0];
        };
        // 8 thick: a 2 DIP small bar, 1 DIP corners. Mid-bar (25%) and 99% square, 100% round.
        CHECK(rail(13500000).pacePx == 25); CHECK_NEAR(rail(13500000).paceEndRadius, 0);
        CHECK(rail(180000).pacePx == 99); CHECK_NEAR(rail(180000).paceEndRadius, 0);
        CHECK(rail(0).pacePx == 100); CHECK_NEAR(rail(0).paceEndRadius, 1);
        // Remaining mode at the window's start is a full bar too.
        CHECK_NEAR(rail(kFiveHours, 8, BarMode::Remaining).paceEndRadius, 1);
        // 50 thick: a 13 DIP small bar, 6.5 DIP corners: 99% leaves 5.5, 95% 1.5, 90% none.
        CHECK_NEAR(rail(180000, 50).paceEndRadius, 5.5);
        CHECK_NEAR(rail(900000, 50).paceEndRadius, 1.5);
        CHECK_NEAR(rail(1800000, 50).paceEndRadius, 0);
    });
}

static void RunTileTests() {
    Run("headline segment fills", [] {
        auto segment = [](double pct, const MeterDisplay& d = Display()) {
            return First({pct, 0, 0}, d, BarLayout::LogoHeadline).quotas[0];
        };
        // 16 DIP segments by default. Any usage fills at least 2 DIPs, as on the bars.
        CHECK(segment(50).fillUnits == 500); CHECK_NEAR(segment(50).fillLength, 8);
        CHECK(segment(10).fillUnits == 100); CHECK_NEAR(segment(10).fillLength, 2);
        CHECK_NEAR(segment(25).fillLength, 4);
        CHECK(segment(120).fillUnits == 1000); CHECK_NEAR(segment(120).fillLength, 16);
        CHECK(segment(0).fillUnits == 0); CHECK_NEAR(segment(0).fillLength, 0);
        CHECK(segment(0.04).fillUnits == 1); CHECK_NEAR(segment(0.04).fillLength, 2);
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK(segment(30, remaining).fillUnits == 700); CHECK_NEAR(segment(30, remaining).fillLength, 11.2);
        // The headline bar length scales them; the shortest keeps the 2 DIP minimum.
        MeterDisplay longer = Display(With([](Settings& s) { s.headlineBarLength = 40; }));
        CHECK_NEAR(segment(25, longer).fillLength, 10); CHECK_NEAR(segment(100, longer).fillLength, 40);
        MeterDisplay shortest = Display(With([](Settings& s) { s.headlineBarLength = 4; }));
        CHECK_NEAR(segment(25, shortest).fillLength, 2); CHECK_NEAR(segment(75, shortest).fillLength, 3);
    });

    Run("headline fills: cut flat until a quarter-thickness corner from the end", [] {
        auto segment = [](double pct, const MeterDisplay& d = Display()) {
            return First({pct, 0, 0}, d, BarLayout::LogoHeadline).quotas[0];
        };
        // 16 x 4 headline segments: 1 DIP corners; 95% leaves 0.8 DIPs.
        CHECK_NEAR(segment(100).fillEndRadius, 1); CHECK_NEAR(segment(95).fillEndRadius, 0.2);
        CHECK_NEAR(segment(50).fillEndRadius, 0); CHECK_NEAR(segment(10).fillEndRadius, 0);
        // 10 DIPs thick: 2.5 DIP corners; 90% of 16 leaves 1.6 DIPs.
        MeterDisplay thick = Display(With([](Settings& s) { s.headlineBarThickness = 10; }));
        CHECK_NEAR(segment(100, thick).fillEndRadius, 2.5); CHECK_NEAR(segment(90, thick).fillEndRadius, 0.9);
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
        // Two arcs of 150 degrees, each grown by 2 x 6.14 (below): 5 of 162.28 degrees.
        AccountMeters two = AccountMeterState(Display(), BarLayout::SegmentedRing,
            Account(L"anthropic", L"A", {true, true, false, false}), Fresh({1, 1, -1, -1}), 0, 0);
        CHECK(two.quotas[0].fillUnits == 31); CHECK_NEAR(two.quotas[0].fillSweep, 5);
        CHECK(two.quotas[1].fillUnits == 31); CHECK_NEAR(two.quotas[1].fillSweep, -5);
    });

    Run("ring arcs follow mask positions, segmented ones grown half a stroke at each end", [] {
        auto arcs = [](BarLayout layout, std::array<bool, kQuotaBarCount> bars) {
            return AccountMeterState(Display(), layout, Account(L"anthropic", L"A", bars),
                                     Fresh({10, 20, 30, 40}), 0, 0);
        };
        // Slots grow at both ends by half the 3 DIP stroke along the 14 DIP centreline.
        const double grow = 1.5 / 14 * 180 / std::numbers::pi;
        auto arc = [&](const QuotaMeter& m, double start, double sweep) {
            double direction = sweep < 0 ? -1 : 1;
            CHECK_NEAR(m.arc.start, start - direction * grow);
            CHECK_NEAR(m.arc.sweep, sweep + 2 * direction * grow);
        };
        AccountMeters gap = arcs(BarLayout::SegmentedRing, {true, false, false, true});
        arc(gap.quotas[0], 195, 150); arc(gap.quotas[3], 165, -150);
        AccountMeters three = arcs(BarLayout::SegmentedRing, {true, true, true, false});
        arc(three.quotas[0], 165, 90); arc(three.quotas[1], 135, -90); arc(three.quotas[2], 285, 90);
        AccountMeters four = arcs(BarLayout::SegmentedRing, {true, true, true, true});
        arc(four.quotas[0], 190, 70); arc(four.quotas[1], 280, 70);
        arc(four.quotas[2], 10, 70); arc(four.quotas[3], 100, 70);
        // Full rings don't grow.
        AccountMeters single = arcs(BarLayout::SegmentedRing, {true, false, false, false});
        CHECK_NEAR(single.quotas[0].radius, 14);
        CHECK_NEAR(single.quotas[0].arc.start, 270); CHECK_NEAR(single.quotas[0].arc.sweep, 360);
        AccountMeters rings = arcs(BarLayout::ConcentricRings, {true, true, false, false});
        CHECK_NEAR(rings.quotas[0].arc.start, 270); CHECK_NEAR(rings.quotas[0].arc.sweep, 360);
        CHECK_NEAR(rings.quotas[1].arc.start, 270); CHECK_NEAR(rings.quotas[1].arc.sweep, 360);
        CHECK_NEAR(rings.quotas[0].radius, 14.195); CHECK_NEAR(rings.quotas[1].radius, 10.295);
    });

    Run("segmented arcs keep the ring's gap between them along the centreline", [] {
        auto pair = [](int thickness) {
            MeterDisplay d = Display(With([&](Settings& s) { s.ringThickness = thickness; }));
            return AccountMeterState(d, BarLayout::SegmentedRing,
                                     Account(L"anthropic", L"A", {true, true, false, false}),
                                     Fresh({10, 20, -1, -1}), 0, 0);
        };
        for (int thickness : {1, 3, 6}) {
            AccountMeters meters = pair(thickness);
            const QuotaMeter& first = meters.quotas[0];   // Clockwise from the left.
            const QuotaMeter& second = meters.quotas[1];  // Counterclockwise from the left.
            // Degrees left between the first arc's end and the second's, at the top and bottom.
            double top = (second.arc.start + second.arc.sweep + 360) - (first.arc.start + first.arc.sweep);
            double bottom = first.arc.start - second.arc.start;
            CHECK_NEAR(top * std::numbers::pi / 180 * first.radius, meters.ring.gap);
            CHECK_NEAR(bottom * std::numbers::pi / 180 * first.radius, meters.ring.gap);
        }
        // 6 DIPs would close the 30 degree gaps: the stroke stops where a 1 DIP gap stays.
        CHECK_NEAR(pair(6).ring.gap, 1);
    });

    Run("ring outline: annular sectors, square and rounded corners, full annuli", [] {
        auto point = [](const auto& p, double x, double y) { CHECK_NEAR(p.x, x); CHECK_NEAR(p.y, y); };
        auto arc = [](const TileOutlineSegment& s, double radius, bool clockwise) {
            CHECK_NEAR(s.radius, radius); CHECK(s.clockwise == clockwise);
        };
        const double half = 1 / std::sqrt(2.0);
        // Radii 10 and 6 around the origin, 0 to 90 degrees, square: the outer edge clockwise in
        // two halves, the radial cut inward, the inner edge back in two halves.
        TileOutline square = TileSectorOutline(0, 10, 6, 0, 90, 0, 0);
        CHECK(square.size() == 1 && square[0].segments.size() == 5);
        const std::vector<TileOutlineSegment>& s = square[0].segments;
        point(square[0], 10, 0);
        point(s[0], 10 * half, 10 * half); arc(s[0], 10, true);
        point(s[1], 0, 10); arc(s[1], 10, true);
        point(s[2], 0, 6); arc(s[2], 0, true);
        point(s[3], 6 * half, 6 * half); arc(s[3], 6, false);
        point(s[4], 6, 0); arc(s[4], 6, false);
        // 1 DIP corners, each a clockwise arc tangent to both edges. Outer corners' circles sit 9
        // out from the centre: they touch the radial edge sqrt(9^2 - 1) out and the outer edge
        // 10 / 9 off the radial line. Inner ones sit 7 out: sqrt(7^2 - 1) and 6 / 7.
        TileOutline round = TileSectorOutline(0, 10, 6, 0, 90, 1, 1);
        CHECK(round.size() == 1 && round[0].segments.size() == 9);
        const std::vector<TileOutlineSegment>& r = round[0].segments;
        const double outerTouch = std::sqrt(100 - 100.0 / 81), innerTouch = std::sqrt(36 - 36.0 / 49);
        point(round[0], std::sqrt(80.0), 0);
        point(r[0], outerTouch, 10.0 / 9); arc(r[0], 1, true);
        point(r[1], 10 * half, 10 * half); arc(r[1], 10, true);
        point(r[2], 10.0 / 9, outerTouch); arc(r[2], 10, true);
        point(r[3], 0, std::sqrt(80.0)); arc(r[3], 1, true);
        point(r[4], 0, std::sqrt(48.0)); arc(r[4], 0, true);
        point(r[5], 6.0 / 7, innerTouch); arc(r[5], 1, true);
        point(r[6], 6 * half, 6 * half); arc(r[6], 6, false);
        point(r[7], innerTouch, 6.0 / 7); arc(r[7], 6, false);
        point(r[8], std::sqrt(48.0), 0); arc(r[8], 1, true);
        // Offset by the centre; a rounded start with a square end has 7 segments.
        TileOutline mixed = TileSectorOutline(16, 10, 6, 0, 90, 1, 0);
        CHECK(mixed[0].segments.size() == 7);
        point(mixed[0], 16 + std::sqrt(80.0), 16);
        point(mixed[0].segments[2], 16, 26); point(mixed[0].segments[3], 16, 22);
        // From 360 degrees on: an annulus, two circles each in two halves, corners ignored.
        TileOutline annulus = TileSectorOutline(16, 15, 12, 270, 630, 1, 1);
        CHECK(annulus.size() == 2);
        for (int i = 0; i < 2; i++) {
            double radius = i ? 12 : 15;
            CHECK(annulus[i].segments.size() == 2);
            point(annulus[i], 16 + radius, 16);
            point(annulus[i].segments[0], 16 - radius, 16); arc(annulus[i].segments[0], radius, true);
            point(annulus[i].segments[1], 16 + radius, 16); arc(annulus[i].segments[1], radius, true);
        }
    });

    Run("ring outline: corners too wide for a short span shrink together", [] {
        // 5 degrees: two corners of radius k reach 2.5 degrees each along the 6 DIP inner edge,
        // k / (6 + k) = sin 2.5; one alone reaches all 5.
        auto fit = [](double degrees) {
            double s = std::sin(degrees * std::numbers::pi / 180);
            return 6 * s / (1 - s);
        };
        TileOutline both = TileSectorOutline(0, 10, 6, 0, 5, 1, 1);
        CHECK(both[0].segments.size() == 9);
        CHECK_NEAR(both[0].segments[0].radius, fit(2.5)); CHECK_NEAR(both[0].segments[3].radius, fit(2.5));
        TileOutline one = TileSectorOutline(0, 10, 6, 0, 5, 1, 0);
        CHECK(one[0].segments.size() == 7);
        CHECK_NEAR(one[0].segments[0].radius, fit(5)); CHECK_NEAR(one[0].segments[6].radius, fit(5));
        // Corners that fit stay.
        CHECK_NEAR(TileSectorOutline(0, 10, 6, 0, 20, 1, 1)[0].segments[0].radius, 1);
    });

    Run("ring fill: track corners at its start, a radial cut at its value, rounded only near the end", [] {
        auto pair = [](double first, BarLayout layout = BarLayout::SegmentedRing,
                       std::array<bool, kQuotaBarCount> bars = {true, true, false, false}) {
            return AccountMeterState(Display(), layout, Account(L"anthropic", L"A", bars),
                                     Fresh({first, 20, -1, -1}), 0, 0).quotas[0];
        };
        // Stroke 3 on a 14 DIP centreline: 0.75 DIP corners; the 150 degree slot grown by half
        // the stroke at each end is 14 x 5 pi / 6 + 3 = 35 pi / 3 + 3 DIPs long.
        const double length = 35 * std::numbers::pi / 3 + 3;
        CHECK_NEAR(pair(100).fillEndRadius, 0.75);
        CHECK_NEAR(pair(99).fillEndRadius, 0.75 - 0.01 * length);
        CHECK_NEAR(pair(98).fillEndRadius, 0);
        CHECK_NEAR(pair(50).fillEndRadius, 0);
        // Half full: the cut is radial at the top (270 degrees), square, from the 15.5 DIP outer
        // edge to the 12.5 DIP inner one; the start keeps the track's corners.
        QuotaMeter half = pair(50);
        CHECK_NEAR(half.arc.start + half.fillSweep, 270);
        const std::vector<TileOutlineSegment>& cut = half.fill[0].segments;
        CHECK(half.fill.size() == 1 && cut.size() == 7);
        CHECK_NEAR(cut[0].radius, 0.75);
        CHECK_NEAR(cut[2].x, 16); CHECK_NEAR(cut[2].y, 0.5);
        CHECK_NEAR(cut[3].x, 16); CHECK_NEAR(cut[3].y, 3.5); CHECK_NEAR(cut[3].radius, 0);
        CHECK_NEAR(cut[6].radius, 0.75);
        // The track: rounded at both ends.
        CHECK(half.track.size() == 1 && half.track[0].segments.size() == 9);
        CHECK(pair(0).fill.empty() && pair(0).fillUnits == 0);
        // Full rings: square ends at any value, a whole annulus at 100%; tracks are annuli.
        for (BarLayout layout : {BarLayout::SegmentedRing, BarLayout::ConcentricRings}) {
            QuotaMeter almost = pair(99, layout, {true, false, false, false});
            CHECK_NEAR(almost.fillEndRadius, 0);
            CHECK(almost.fill.size() == 1 && almost.fill[0].segments.size() == 5);
            CHECK(pair(100, layout, {true, false, false, false}).fill.size() == 2);
            CHECK(almost.track.size() == 2);
        }
    });

    Run("tile pace: thousandths, then angles from them", [] {
        auto ring = [](long long resetIn, const MeterDisplay& d = Display()) {
            QuotaMeter m = First(Window(10, resetIn), d, BarLayout::SegmentedRing).quotas[0];
            CHECK(m.paceVisible);
            return m;
        };
        // A quarter, none, all and a third of the 5-hour window elapsed; the full ring's tick
        // turns clockwise from the top.
        CHECK(ring(13500000).paceUnits == 250); CHECK_NEAR(ring(13500000).paceAngle, 270 + 90);
        CHECK(ring(kFiveHours).paceUnits == 0); CHECK_NEAR(ring(kFiveHours).paceAngle, 270);
        CHECK(ring(0).paceUnits == 1000); CHECK_NEAR(ring(0).paceAngle, 270 + 360);
        CHECK(ring(12000000).paceUnits == 333); CHECK_NEAR(ring(12000000).paceAngle, 270 + 0.333 * 360);
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK(ring(13500000, remaining).paceUnits == 750);
        AccountData data = Fresh();
        data.win5h = Window(10, 13500000);
        data.winWeek = Window(10, 13500000);
        AccountMeters two = AccountMeterState(Display(), BarLayout::SegmentedRing,
            Account(L"anthropic", L"A", {true, true, false, false}), data, 0, 0);
        // A quarter along the grown arc from 165 degrees counterclockwise: 165 + 6.14 - 162.28 / 4.
        const double grow = 1.5 / 14 * 180 / std::numbers::pi;
        CHECK_NEAR(two.quotas[1].paceAngle, 165 + grow - (150 + 2 * grow) / 4);
        CHECK(!First(Window(10, 1000), Display(), BarLayout::LogoHeadline).quotas[0].paceVisible);
    });

    Run("ring pace tick: clamped clear of a partial arc's corners; free on full rings", [] {
        auto two = [](long long resetIn) {
            AccountData data = Fresh();
            data.win5h = Window(10, resetIn);
            data.winWeek = Window(10, resetIn);
            return AccountMeterState(Display(), BarLayout::SegmentedRing,
                                     Account(L"anthropic", L"A", {true, true, false, false}), data, 0, 0);
        };
        // Grown arcs: 195 - g to 345 + g clockwise, and 165 + g to 15 - g counterclockwise. The
        // tick's centre keeps a 0.75 DIP corner plus half the 3.5 DIP halo from either end, along
        // the 14 DIP centreline.
        const double degrees = 180 / std::numbers::pi;
        const double grow = 1.5 / 14 * degrees, pad = (0.75 + 1.75) / 14 * degrees;
        AccountMeters start = two(kFiveHours), end = two(0), middle = two(9000000);
        CHECK_NEAR(start.quotas[0].paceAngle, 195 - grow + pad);
        CHECK_NEAR(end.quotas[0].paceAngle, 345 + grow - pad);
        CHECK_NEAR(start.quotas[1].paceAngle, 165 + grow - pad);
        CHECK_NEAR(end.quotas[1].paceAngle, 15 - grow + pad);
        CHECK_NEAR(middle.quotas[0].paceAngle, 270); CHECK_NEAR(middle.quotas[1].paceAngle, 90);
        // A full ring has no ends to keep clear of.
        CHECK_NEAR(First(Window(10, kFiveHours), Display(), BarLayout::SegmentedRing).quotas[0].paceAngle, 270);
        CHECK_NEAR(First(Window(10, 0), Display(), BarLayout::ConcentricRings).quotas[0].paceAngle, 630);
        // Hidden: no tick shapes.
        AccountData stale = Fresh();
        stale.win5h = Window(10, 9000000);
        stale.stale = true;
        QuotaMeter hidden = AccountMeterState(Display(), BarLayout::SegmentedRing,
                                              Account(L"anthropic", L"A", {true, false, false, false}), stale, 0, 0)
                                .quotas[0];
        CHECK(!hidden.paceVisible && hidden.paceHalo.empty() && hidden.paceCore.empty());
    });

    Run("ring pace tick: radial, 1.5 DIPs past the ring, half the gap toward a neighbouring ring", [] {
        // The tick's extent along its angle from the centre, and its width across.
        struct Extent {
            double from, to, width;
        };
        auto extent = [](const TileOutline& outline, double center, double degrees) {
            double radians = degrees * std::numbers::pi / 180, ax = std::cos(radians), ay = std::sin(radians);
            Extent e{1e9, -1e9, 0};
            auto add = [&](double x, double y) {
                double along = (x - center) * ax + (y - center) * ay;
                double across = (y - center) * ax - (x - center) * ay;
                e.from = std::min(e.from, along);
                e.to = std::max(e.to, along);
                e.width = std::max(e.width, 2 * std::abs(across));
            };
            CHECK(outline.size() == 1 && outline[0].segments.size() == 3);
            add(outline[0].x, outline[0].y);
            for (const TileOutlineSegment& s : outline[0].segments) {
                CHECK_NEAR(s.radius, 0);
                add(s.x, s.y);
            }
            return e;
        };
        auto check = [&](const QuotaMeter& m, double center, double from, double to) {
            Extent halo = extent(m.paceHalo, center, m.paceAngle), core = extent(m.paceCore, center, m.paceAngle);
            CHECK_NEAR(halo.from, from); CHECK_NEAR(halo.to, to); CHECK_NEAR(halo.width, 3.5);
            CHECK_NEAR(core.from, from + 0.5); CHECK_NEAR(core.to, to - 0.5); CHECK_NEAR(core.width, 2);
        };
        auto rings = [](BarLayout layout, int count, int thickness, int size) {
            AccountData data = Fresh();
            for (WindowUsage* u : {&data.win5h, &data.winWeek, &data.fableWeek}) *u = Window(10, 13500000);
            MeterDisplay d = Display(With([&](Settings& s) { s.ringThickness = thickness; s.ringSize = size; }));
            return AccountMeterState(d, layout, Account(L"anthropic", L"A", {count > 0, count > 1, count > 2, false}),
                                     data, 0, 0);
        };
        // One 3 DIP ring 32 across: edges 12.5 and 15.5 from the centre.
        check(rings(BarLayout::SegmentedRing, 1, 3, 32).quotas[0], 16, 11, 17);
        // Segmented arcs: past both edges by 1.5.
        AccountMeters pair = rings(BarLayout::SegmentedRing, 2, 3, 32);
        check(pair.quotas[0], 16, 11, 17); check(pair.quotas[1], 16, 11, 17);
        // Concentric, thickness 1: 0.87 DIP strokes 0.8 apart. The outer ring spans 14.63-15.5,
        // the inner one 12.96-13.83; facing sides reach 0.4, to the gap's middle.
        AccountMeters thin = rings(BarLayout::ConcentricRings, 2, 1, 32);
        check(thin.quotas[0], 16, 14.63 - 0.4, 15.5 + 1.5);
        check(thin.quotas[1], 16, 12.96 - 1.5, 13.83 + 0.4);
        // Three rings, thickness 1: 0.75 DIP strokes 0.8 apart; the middle one reaches 0.4 both ways.
        AccountMeters three = rings(BarLayout::ConcentricRings, 3, 1, 32);
        check(three.quotas[0], 16, 14.75 - 0.4, 15.5 + 1.5);
        check(three.quotas[1], 16, 13.2 - 0.4, 13.95 + 0.4);
        check(three.quotas[2], 16, 11.65 - 1.5, 12.4 + 0.4);
        // Thickness 5 on a 40 DIP ring: 4.35 DIP strokes 2 apart (19.5 out to 15.15, 13.15 to
        // 8.8); facing sides reach 1.
        AccountMeters wide = rings(BarLayout::ConcentricRings, 2, 5, 40);
        check(wide.quotas[0], 20, 15.15 - 1, 19.5 + 1.5);
        check(wide.quotas[1], 20, 8.8 - 1.5, 13.15 + 1);
    });

    Run("ring shapes: arcs inside the ring area's 0.5 DIP margin, ticks within kTileRingOverflow", [] {
        // Every size, thickness and quota count at pace positions around the ring.
        for (BarLayout layout : {BarLayout::SegmentedRing, BarLayout::ConcentricRings}) {
            for (int size : {24, 32, 40}) {
                for (int thickness : {1, 3, 6}) {
                    for (int count = 1; count <= 4; count++) {
                        for (long long resetIn : {0LL, 2250000LL, 4500000LL, 6750000LL, 9000000LL, 13500000LL,
                                                  (long long)kFiveHours}) {
                            AccountData data = Fresh();
                            for (WindowUsage* u : {&data.win5h, &data.winWeek, &data.fableWeek, &data.extraUsage}) {
                                *u = Window(100, resetIn);
                            }
                            MeterDisplay d = Display(With([&](Settings& s) { s.ringSize = size; s.ringThickness = thickness; }));
                            AccountMeters meters = AccountMeterState(
                                d, layout, Account(L"anthropic", L"A", {count > 0, count > 1, count > 2, count > 3}), data, 0, 0);
                            double center = meters.ring.size / 2;
                            auto points = [](const TileOutline& outline, auto&& visit) {
                                for (const TileOutlineFigure& figure : outline) {
                                    visit(figure.x, figure.y);
                                    for (const TileOutlineSegment& s : figure.segments) visit(s.x, s.y);
                                }
                            };
                            for (int w = 0; w < count; w++) {
                                const QuotaMeter& m = meters.quotas[w];
                                CHECK(!m.track.empty() && !m.fill.empty() && !m.paceHalo.empty());
                                // Arc outlines lie on circles no larger than the outer edge.
                                for (const TileOutline* outline : {&m.track, &m.fill}) {
                                    points(*outline, [&](double x, double y) {
                                        CHECK(std::hypot(x - center, y - center) <= center - 0.5 + 1e-9);
                                    });
                                }
                                points(m.paceHalo, [&](double x, double y) {
                                    CHECK(x >= -kTileRingOverflow && x <= meters.ring.size + kTileRingOverflow);
                                    CHECK(y >= -kTileRingOverflow && y <= meters.ring.size + kTileRingOverflow);
                                });
                            }
                        }
                    }
                }
            }
        }
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
        CHECK_NEAR(single.ring.logo, 14.08);
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
        // No badge: an empty box.
        CHECK_NEAR(single.ringBadge.width, 0);
    });

    Run("logo size and the number badge's box", [] {
        // The box: per character 0.6 of the badge size, the cap height 0.7 of it, and the 1 DIP
        // outline around both: at 9 DIPs, 7.4 x 8.3 DIPs for one character, 12.8 wide for two.
        MeterDisplay d = Display();
        CHECK(d.badgeSize == 13 && d.badgeOffsetX == 0 && d.badgeOffsetY == 0); CHECK_NEAR(d.logoScale, 1);
        CHECK(d.badgeColor == RGB(0xFF, 0xFF, 0xFF));
        CHECK(Display(With([](Settings& s) { s.badgeColor = 0xAB123456; })).badgeColor == 0x123456);
        // Logos in the text color by default; the brand choices pass through.
        CHECK(d.logoBrands == LogoBrands{});
        CHECK((Display(With([](Settings& s) { s.logoBrands.gemini = true; })).logoBrands == LogoBrands{false, true}));
        MeterDisplay nine = Display(With([](Settings& s) { s.badgeSize = 9; }));
        BadgeBox one = LogoBadgeBox(nine, 15, 1);
        CHECK_NEAR(one.width, 7.4); CHECK_NEAR(one.height, 8.3);
        // Beside bars: centred on the 15 DIP logo's lower-right corner.
        CHECK_NEAR(one.left, 15 - 3.7); CHECK_NEAR(one.top, 15 - 4.15);
        CHECK_NEAR(LogoBadgeBox(nine, 15, 2).width, 12.8); CHECK_NEAR(LogoBadgeBox(nine, 15, 2).left, 15 - 6.4);
        // The offsets move it; the size grows it about the same centre.
        MeterDisplay moved = Display(With([](Settings& s) { s.badgeOffsetX = -6; s.badgeOffsetY = 4; s.badgeSize = 12; }));
        BadgeBox big = LogoBadgeBox(moved, 15, 1);
        CHECK_NEAR(big.width, 12 * 0.6 + 2); CHECK_NEAR(big.height, 12 * 0.7 + 2);
        CHECK_NEAR(big.left + big.width / 2, 15 - 6); CHECK_NEAR(big.top + big.height / 2, 15 + 4);
        // Sizes and offsets clamp at the entry.
        MeterDisplay wild = Display(With([](Settings& s) {
            s.logoScale = 900; s.badgeSize = 0; s.badgeOffsetX = -99; s.badgeOffsetY = 99;
        }));
        CHECK_NEAR(wild.logoScale, 2); CHECK(wild.badgeSize == 6 && wild.badgeOffsetX == -20 && wild.badgeOffsetY == 20);
        CHECK_NEAR(Display(With([](Settings& s) { s.logoScale = 10; s.badgeSize = 40; })).logoScale, 0.5);
        CHECK(Display(With([](Settings& s) { s.badgeSize = 40; })).badgeSize == 20);

        // Rings: the logo is 0.44 of the 32 DIP ring at 100%, scaled, capped 1.2 DIPs inside the
        // 12.5 DIP hole (22.6 DIPs).
        auto ring = [](const MeterDisplay& display, int badge, BarLayout layout = BarLayout::SegmentedRing,
                       std::array<bool, kQuotaBarCount> bars = {true, false, false, false}) {
            return AccountMeterState(display, layout, Account(L"anthropic", L"A", bars), Fresh({10, 20, 30, 40}), badge, 0);
        };
        auto scaled = [](int percent) {
            return Display(With([&](Settings& s) { s.logoScale = percent; s.badgeSize = 9; }));
        };
        CHECK_NEAR(ring(scaled(100), 0).ring.logo, 14.08);
        CHECK_NEAR(ring(scaled(150), 0).ring.logo, 21.12);
        CHECK_NEAR(ring(scaled(200), 0).ring.logo, 22.6);
        CHECK_NEAR(ring(scaled(50), 0).ring.logo, 7.04);
        // The ring's badge sits on the diagonal through the centre (16, 16), pulled in until its
        // far corner is half a DIP inside the hole.
        BadgeBox inside = ring(nine, 1).ringBadge;
        CHECK_NEAR(inside.width, 7.4); CHECK_NEAR(inside.height, 8.3);
        CHECK_NEAR(inside.left + 3.7 - 16, inside.top + 4.15 - 16);
        CHECK_NEAR(std::hypot(inside.left + inside.width - 16, inside.top + inside.height - 16), 12);
        CHECK(inside.left + 3.7 < 16 + 7.04);
        // A small logo keeps it on the logo's corner; offsets move it from there.
        BadgeBox corner = ring(scaled(50), 1).ringBadge;
        CHECK_NEAR(corner.left + 3.7, 16 + 3.52); CHECK_NEAR(corner.top + 4.15, 16 + 3.52);
        MeterDisplay nudged = Display(With([](Settings& s) {
            s.logoScale = 50; s.badgeSize = 9; s.badgeOffsetX = 3; s.badgeOffsetY = -2;
        }));
        CHECK_NEAR(ring(nudged, 1).ringBadge.left, corner.left + 3);
        CHECK_NEAR(ring(nudged, 1).ringBadge.top, corner.top - 2);
        // A badge too large for the hole centres on the logo.
        MeterDisplay large = Display(With([](Settings& s) { s.badgeSize = 16; }));
        BadgeBox centred = ring(large, 1, BarLayout::ConcentricRings, {true, true, true, true}).ringBadge;
        CHECK_NEAR(centred.left + centred.width / 2, 16); CHECK_NEAR(centred.top + centred.height / 2, 16);
    });
}

// Gauges, clock pies and rounded squares.
static void RunFaceTileTests() {
    static constexpr auto point = [](const auto& p, double x, double y) { CHECK_NEAR(p.x, x); CHECK_NEAR(p.y, y); };

    Run("tile kinds: ring tiles hold the identity inside, face tiles draw meter shapes in a face", [] {
        using enum BarLayout;
        struct Kind {
            BarLayout layout;
            bool tile, ring, face;
        };
        for (Kind k : {Kind{Stacked, false, false, false}, Kind{Vertical, false, false, false},
                       Kind{LogoHeadline, true, false, false}, Kind{SegmentedRing, true, true, true},
                       Kind{ConcentricRings, true, true, true}, Kind{Gauge, true, false, true},
                       Kind{ClockPies, true, false, true}, Kind{RoundedSquare, true, true, true},
                       Kind{Sparkline, true, false, false}, Kind{QuotaMatrix, true, false, false}}) {
            CHECK(IsTileLayout(k.layout) == k.tile && IsRingTile(k.layout) == k.ring && IsFaceTile(k.layout) == k.face);
        }
        CHECK(kLastBarLayout == QuotaMatrix);
    });

    Run("gauge: only the most-used tile quota; until a reading, the first selected one", [] {
        auto gauge = [](std::array<double, kQuotaBarCount> pct, std::array<bool, kQuotaBarCount> bars = {true, true, true, true}) {
            return AccountMeterState(Display(), BarLayout::Gauge, Account(L"anthropic", L"A", bars), Fresh(pct), 0, 0);
        };
        CHECK(gauge({10, 80, 30, 40}).barMask == 0b0010);
        CHECK(gauge({90, 80, -1, 95}).barMask == 0b1000);
        // A tie goes to the earlier slot.
        CHECK(gauge({40, 80, 80, 20}).barMask == 0b0010);
        // Only selected quotas count: the 5-hour one though the weekly one is higher.
        CHECK(gauge({10, 90, -1, -1}, {true, false, false, false}).barMask == 0b0001);
        // The other slots get no shapes.
        AccountMeters weekly = gauge({10, 80, 30, 40});
        CHECK(!weekly.quotas[1].track.empty() && !weekly.quotas[1].fill.empty());
        for (int w : {0, 2, 3}) CHECK(weekly.quotas[w].track.empty() && weekly.quotas[w].fill.empty());
        // Before the first success: the first selected quota, empty.
        AccountData before = Fresh();
        before.lastSuccessMs = 0;
        AccountMeters first = AccountMeterState(Display(), BarLayout::Gauge,
                                                Account(L"anthropic", L"A", {false, true, false, true}), before, 0, 0);
        CHECK(first.barMask == 0b0010 && !first.quotas[1].track.empty() && first.quotas[1].fillUnits == 0);
        CHECK(first.quotas[3].track.empty());
        CHECK(first.headline == L"\u2013" && first.headlineKey.empty());
        // Nothing reported after a success: no meter.
        AccountMeters none = gauge({-1, -1, -1, -1});
        CHECK(none.barMask == 0 && none.headline == L"\u2013" && none.headlineKey.empty());
        for (const QuotaMeter& q : none.quotas) CHECK(q.track.empty());
    });

    Run("gauge: its number without %, the window key and color, per bar mode; no countdown", [] {
        auto gauge = [](std::array<double, kQuotaBarCount> pct, std::wstring provider = L"anthropic",
                        BarMode mode = BarMode::Used, bool countdown = false) {
            MeterDisplay d = Display(With([&](Settings& s) { s.barMode = mode; s.headlineCountdown = countdown; }));
            AccountData data = Fresh(pct);
            data.winWeek.resetUnixMs = kNow + 45 * kMinute;
            return AccountMeterState(d, BarLayout::Gauge, Account(provider, L"A", {true, true, false, true}), data, 0, 0);
        };
        AccountMeters weekly = gauge({10, 80, -1, 40});
        CHECK(weekly.headline == L"80" && weekly.headlineKey == L"7d");
        CHECK(weekly.headlineColor == 0xFFFB8C00 && weekly.quotas[1].color == 0xFFFB8C00);
        CHECK(weekly.quotas[1].fillUnits == 800); CHECK_NEAR(weekly.quotas[1].fillSweep, 216);
        CHECK(gauge({10, 20, -1, 95}, L"openai").headlineKey == L"Cr");
        CHECK(gauge({10, 20, -1, 95}).headlineKey == L"Ex");
        CHECK(gauge({130, -1, -1, -1}).headline == L"100");
        // Remaining: the most-used quota still, 20 left of it.
        AccountMeters left = gauge({10, 80, -1, 40}, L"anthropic", BarMode::Remaining);
        CHECK(left.barMask == 0b0010 && left.headline == L"20" && left.headlineColor == 0xFFFB8C00);
        CHECK(left.quotas[1].fillUnits == 200); CHECK_NEAR(left.quotas[1].fillSweep, 54);
        CHECK(gauge({130, -1, -1, -1}, L"anthropic", BarMode::Remaining).headline == L"0");
        CHECK(gauge({130, -1, -1, -1}, L"anthropic", BarMode::Remaining).quotas[0].fill.empty());
        // The countdown is the headline tiles'.
        CHECK(gauge({10, 80, -1, 40}, L"anthropic", BarMode::Used, true).headlineCountdown.empty());
    });

    Run("gauge: a 270-degree arc open at the bottom, its ends rounded, the fill cut at its value", [] {
        auto gauge = [](double pct, int size = 32, int thickness = 3) {
            MeterDisplay d = Display(With([&](Settings& s) { s.ringSize = size; s.ringThickness = thickness; }));
            return First({pct, 0, 0}, d, BarLayout::Gauge);
        };
        // 32 DIP face, 3 DIP stroke: centreline 14, edges 15.5 and 12.5 from the centre (16, 16).
        AccountMeters half = gauge(50);
        const QuotaMeter& q = half.quotas[0];
        CHECK_NEAR(half.faceWidth, 32); CHECK_NEAR(half.faceHeight, 32);
        CHECK_NEAR(half.ring.stroke, 3); CHECK_NEAR(q.radius, 14);
        // From the lower left clockwise to the lower right, not grown past the slot.
        CHECK_NEAR(q.arc.start, 135); CHECK_NEAR(q.arc.sweep, 270);
        // The track: 0.75 DIP (stroke / 4) corners at both ends. It starts where the start corner
        // meets the radial edge, sqrt(14.75^2 - 0.75^2) = sqrt(217) out at 135 degrees; the outer
        // edge's middle is at the top; the end's radial edge at 45 degrees runs in to the inner
        // corner, sqrt(13.25^2 - 0.75^2) = sqrt(175) out; the inner edge's middle is at the top.
        const TileOutline& track = q.track;
        CHECK(track.size() == 1 && track[0].segments.size() == 9);
        point(track[0], 16 - std::sqrt(217 / 2.0), 16 + std::sqrt(217 / 2.0));
        CHECK_NEAR(track[0].segments[0].radius, 0.75);
        point(track[0].segments[1], 16, 0.5); CHECK_NEAR(track[0].segments[1].radius, 15.5);
        point(track[0].segments[3], 16 + std::sqrt(217 / 2.0), 16 + std::sqrt(217 / 2.0));
        point(track[0].segments[4], 16 + std::sqrt(175 / 2.0), 16 + std::sqrt(175 / 2.0));
        point(track[0].segments[6], 16, 3.5); CHECK_NEAR(track[0].segments[6].radius, 12.5);
        CHECK(!track[0].segments[6].clockwise);
        // Half full: the start's corners, then a square radial cut at the top.
        CHECK(q.fillUnits == 500); CHECK_NEAR(q.fillSweep, 135); CHECK_NEAR(q.fillEndRadius, 0);
        CHECK(q.fill.size() == 1 && q.fill[0].segments.size() == 7);
        const std::vector<TileOutlineSegment>& cut = q.fill[0].segments;
        CHECK_NEAR(cut[0].radius, 0.75);
        point(cut[2], 16, 0.5); point(cut[3], 16, 3.5); CHECK_NEAR(cut[3].radius, 0);
        CHECK_NEAR(cut[6].radius, 0.75);
        // Full: the track's shape, rounded at both ends. Any usage: at least 5 of 270 degrees.
        QuotaMeter full = gauge(100).quotas[0];
        CHECK(full.fillUnits == 1000); CHECK_NEAR(full.fillSweep, 270); CHECK_NEAR(full.fillEndRadius, 0.75);
        CHECK(full.fill.size() == 1 && full.fill[0].segments.size() == 9);
        QuotaMeter tiny = gauge(0.5).quotas[0];
        CHECK(tiny.fillUnits == 19); CHECK_NEAR(tiny.fillSweep, 5);
        CHECK(gauge(0).quotas[0].fill.empty() && gauge(0).quotas[0].fillUnits == 0);
        // Any size and thickness: the same arc. 40 DIPs, 6 thick: centreline 16.5.
        QuotaMeter thick = gauge(50, 40, 6).quotas[0];
        CHECK_NEAR(thick.arc.start, 135); CHECK_NEAR(thick.arc.sweep, 270); CHECK_NEAR(thick.radius, 16.5);
    });

    Run("gauge: number and key fonts and cap centres from the face", [] {
        auto face = [](int size, int thickness, double taskbar = 48) {
            MeterDisplay d = Display(With([&](Settings& s) { s.ringSize = size; s.ringThickness = thickness; }), 1, taskbar);
            return First({50, 0, 0}, d, BarLayout::Gauge);
        };
        // 32 DIPs, 3 thick (hole 12.5): 0.375 of the size, 12; the hole would allow (25 - 2) / 1.75.
        // The key at 9/14 of it; the number half a DIP above the centre, the key 0.75 of the 14 DIP
        // centreline below it.
        AccountMeters m = face(32, 3);
        CHECK_NEAR(m.faceFont, 12); CHECK_NEAR(m.faceKeyFont, 12 * 9 / 14.0);
        CHECK_NEAR(m.faceNumberCenter, 15.5); CHECK_NEAR(m.faceKeyCenter, 16 + 10.5);
        // 40 DIPs: 15; centreline 18.
        m = face(40, 3);
        CHECK_NEAR(m.faceFont, 15); CHECK_NEAR(m.faceKeyFont, 15 * 9 / 14.0);
        CHECK_NEAR(m.faceNumberCenter, 19.5); CHECK_NEAR(m.faceKeyCenter, 20 + 13.5);
        // 40 DIPs, 6 thick: the 13.5 DIP hole holds (27 - 2) / 1.75; centreline 16.5.
        m = face(40, 6);
        CHECK_NEAR(m.faceFont, 25 / 1.75); CHECK_NEAR(m.faceKeyFont, 25 / 1.75 * 9 / 14);
        CHECK_NEAR(m.faceKeyCenter, 20 + 16.5 * 0.75);
        // 24 DIPs: the 8.5 DIP hole holds (17 - 2) / 1.75; the key at its 6 DIP floor.
        m = face(24, 3);
        CHECK_NEAR(m.faceFont, 15 / 1.75); CHECK_NEAR(m.faceKeyFont, 6);
        CHECK_NEAR(m.faceNumberCenter, 11.5); CHECK_NEAR(m.faceKeyCenter, 12 + 7.5);
        // 24 DIPs, 6 thick: a 4.5 DIP stroke leaves a 7 DIP hole, (14 - 2) / 1.75 = 6.86: the 7 DIP floor.
        m = face(24, 6);
        CHECK_NEAR(m.ring.stroke, 4.5); CHECK_NEAR(m.faceFont, 7); CHECK_NEAR(m.faceKeyFont, 6);
        CHECK_NEAR(m.faceKeyCenter, 12 + 9.25 * 0.75);
        // Capped by the tile: 40 DIPs on a 40 DIP taskbar are 32.
        m = face(40, 3, 40);
        CHECK_NEAR(m.faceWidth, 32); CHECK_NEAR(m.faceFont, 12);
    });

    Run("gauge pace tick: radial across the arc, kept clear of its rounded ends", [] {
        auto gauge = [](long long resetIn) { return First(Window(10, resetIn), Display(), BarLayout::Gauge).quotas[0]; };
        // Half elapsed: straight up, from 11 to 17 DIPs out (1.5 past both edges), 3.5 wide; the
        // core half a DIP shorter at each end, 2 wide.
        QuotaMeter half = gauge(9000000);
        CHECK(half.paceVisible && half.paceUnits == 500); CHECK_NEAR(half.paceAngle, 270);
        CHECK(half.paceHalo.size() == 1 && half.paceHalo[0].segments.size() == 3);
        point(half.paceHalo[0], 17.75, 5);
        point(half.paceHalo[0].segments[0], 17.75, -1);
        point(half.paceHalo[0].segments[1], 14.25, -1);
        point(half.paceHalo[0].segments[2], 14.25, 5);
        point(half.paceCore[0], 17, 4.5);
        point(half.paceCore[0].segments[1], 15, -0.5);
        // The ends: the tick's centre a 0.75 DIP corner plus half the halo inside, along the 14 DIP centreline.
        const double pad = (0.75 + 1.75) / 14 * 180 / std::numbers::pi;
        CHECK_NEAR(gauge(kFiveHours).paceAngle, 135 + pad);
        CHECK_NEAR(gauge(0).paceAngle, 405 - pad);
        CHECK(gauge(kFiveHours + 1000).paceHalo.empty());
    });

    Run("pies: face sizes and pie centres per quota count, down the first column from the third", [] {
        auto pies = [](std::array<bool, kQuotaBarCount> bars, int size = 32, double taskbar = 48) {
            MeterDisplay d = Display(With([&](Settings& s) { s.ringSize = size; }), 1, taskbar);
            return AccountMeterState(d, BarLayout::ClockPies, Account(L"anthropic", L"A", bars), Fresh({10, 20, 30, 40}), 0, 0);
        };
        // A pie's centre and radius, from its track's top point.
        auto pie = [&](const QuotaMeter& m, double x, double y, double r) {
            CHECK_NEAR(m.radius, r);
            CHECK(m.track.size() == 1);
            point(m.track[0], x, y - r);
        };
        // One: 0.75 of the 32 DIP face.
        AccountMeters one = pies({true, false, false, false});
        CHECK_NEAR(one.faceWidth, 24); CHECK_NEAR(one.faceHeight, 24);
        pie(one.quotas[0], 12, 12, 12);
        // Two, side by side: 0.5625 of it (18 DIPs), 3 apart.
        AccountMeters two = pies({false, true, false, true});
        CHECK_NEAR(two.faceWidth, 39); CHECK_NEAR(two.faceHeight, 18);
        pie(two.quotas[1], 9, 9, 9); pie(two.quotas[3], 30, 9, 9);
        // Three and four: a grid of (32 - 2) / 2 = 15 DIP pies 2 apart, by slot down each column.
        AccountMeters three = pies({true, false, true, true});
        CHECK_NEAR(three.faceWidth, 32); CHECK_NEAR(three.faceHeight, 32);
        pie(three.quotas[0], 7.5, 7.5, 7.5); pie(three.quotas[2], 7.5, 24.5, 7.5); pie(three.quotas[3], 24.5, 7.5, 7.5);
        AccountMeters four = pies({true, true, true, true});
        CHECK_NEAR(four.faceWidth, 32); CHECK_NEAR(four.faceHeight, 32);
        pie(four.quotas[0], 7.5, 7.5, 7.5); pie(four.quotas[1], 7.5, 24.5, 7.5);
        pie(four.quotas[2], 24.5, 7.5, 7.5); pie(four.quotas[3], 24.5, 24.5, 7.5);
        // The ring size sets the face, capped by the tile.
        AccountMeters small = pies({true, true, false, false}, 24);
        CHECK_NEAR(small.faceWidth, 2 * 13.5 + 3); CHECK_NEAR(small.faceHeight, 13.5);
        CHECK_NEAR(pies({true, true, true, false}, 24).faceWidth, 2 * 11 + 2);
        AccountMeters capped = pies({true, false, false, false}, 40, 32);
        CHECK_NEAR(capped.faceWidth, 18); pie(capped.quotas[0], 9, 9, 9);
        // Nothing reported: an empty face.
        AccountMeters none = AccountMeterState(Display(), BarLayout::ClockPies, Account(), Fresh(), 0, 0);
        CHECK(none.barMask == 0); CHECK_NEAR(none.faceWidth, 0); CHECK_NEAR(none.faceHeight, 0);
    });

    Run("pies: a disc track, the fill a wedge clockwise from the top, at least 5 degrees", [] {
        auto pie = [](double pct, BarMode mode = BarMode::Used) {
            MeterDisplay d = Display(With([&](Settings& s) { s.barMode = mode; }));
            return First({pct, 0, 0}, d, BarLayout::ClockPies).quotas[0];
        };
        auto arc = [](const TileOutlineSegment& s, double radius) { CHECK_NEAR(s.radius, radius); CHECK(s.clockwise); };
        const double h = 12 / std::sqrt(2.0);
        // One 24 DIP pie at (12, 12). The track: a disc from the top, in two clockwise halves.
        QuotaMeter quarter = pie(25);
        CHECK(quarter.track.size() == 1 && quarter.track[0].segments.size() == 2);
        point(quarter.track[0], 12, 0);
        point(quarter.track[0].segments[0], 12, 24); arc(quarter.track[0].segments[0], 12);
        point(quarter.track[0].segments[1], 12, 0); arc(quarter.track[0].segments[1], 12);
        // A quarter: from the centre up to the rim, then 45 and 90 degrees round.
        CHECK(quarter.fillUnits == 250); CHECK_NEAR(quarter.fillSweep, 90);
        CHECK(quarter.fill.size() == 1 && quarter.fill[0].segments.size() == 3);
        point(quarter.fill[0], 12, 12);
        point(quarter.fill[0].segments[0], 12, 0); CHECK_NEAR(quarter.fill[0].segments[0].radius, 0);
        point(quarter.fill[0].segments[1], 12 + h, 12 - h); arc(quarter.fill[0].segments[1], 12);
        point(quarter.fill[0].segments[2], 24, 12); arc(quarter.fill[0].segments[2], 12);
        // Three quarters: halves of 135 degrees, ending at the left.
        QuotaMeter most = pie(75);
        CHECK_NEAR(most.fillSweep, 270);
        point(most.fill[0].segments[1], 12 + h, 12 + h); point(most.fill[0].segments[2], 0, 12);
        // Full: a disc like the track.
        QuotaMeter full = pie(100);
        CHECK(full.fillUnits == 1000 && full.fill.size() == 1 && full.fill[0].segments.size() == 2);
        point(full.fill[0], 12, 0); point(full.fill[0].segments[0], 12, 24);
        // Any usage: 5 degrees; none: no wedge, the disc stays. Remaining: 30 used shows 70%.
        QuotaMeter tiny = pie(0.5);
        CHECK(tiny.fillUnits == 14); CHECK_NEAR(tiny.fillSweep, 5);
        CHECK(pie(0).fill.empty() && pie(0).fillUnits == 0 && pie(0).track.size() == 1);
        CHECK_NEAR(pie(30, BarMode::Remaining).fillSweep, 252);
    });

    Run("pies: the pace hand from the centre past the rim, turning clockwise from the top", [] {
        auto hand = [](long long resetIn, const MeterDisplay& d = Display()) {
            return First(Window(10, resetIn), d, BarLayout::ClockPies).quotas[0];
        };
        // A quarter elapsed: pointing right from (12, 12); the halo 3 wide to a DIP past the 12 DIP
        // rim, the core 1.5 wide to half a DIP past it.
        QuotaMeter q = hand(13500000);
        CHECK(q.paceVisible && q.paceUnits == 250); CHECK_NEAR(q.paceAngle, 360);
        CHECK(q.paceHalo.size() == 1 && q.paceHalo[0].segments.size() == 3);
        point(q.paceHalo[0], 12, 13.5);
        point(q.paceHalo[0].segments[0], 25, 13.5);
        point(q.paceHalo[0].segments[1], 25, 10.5);
        point(q.paceHalo[0].segments[2], 12, 10.5);
        point(q.paceCore[0], 12, 12.75);
        point(q.paceCore[0].segments[0], 24.5, 12.75);
        point(q.paceCore[0].segments[1], 24.5, 11.25);
        // None elapsed: straight up; all elapsed: round to the top again. Never clamped.
        QuotaMeter top = hand(kFiveHours);
        CHECK_NEAR(top.paceAngle, 270);
        point(top.paceHalo[0], 13.5, 12); point(top.paceHalo[0].segments[0], 13.5, -1);
        point(top.paceHalo[0].segments[1], 10.5, -1);
        CHECK_NEAR(hand(0).paceAngle, 630);
        // Remaining mode: the time left, three quarters.
        MeterDisplay left = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK(hand(13500000, left).paceUnits == 750); CHECK_NEAR(hand(13500000, left).paceAngle, 270 + 270);
        // Markers off: no hand.
        MeterDisplay off = Display(With([](Settings& s) { s.showPaceTicks = false; }));
        CHECK(!hand(13500000, off).paceVisible && hand(13500000, off).paceHalo.empty());
    });

    Run("rounded square: points and outward normals along the centreline, wrapping both ways", [] {
        // Centre (20, 20), half-side 10, 4 DIP corners: 6 + 2 pi + 6 DIPs from one edge's middle to
        // the next one's, 48 + 8 pi round.
        const double pi = std::numbers::pi, side = 12 + 2 * pi, r2 = std::sqrt(2.0) / 2;
        auto check = [](double along, double offset, double x, double y, double nx, double ny) {
            TileSquarePoint p = TileSquareAt(20, 10, 4, along, offset);
            CHECK_NEAR(p.x, x); CHECK_NEAR(p.y, y); CHECK_NEAR(p.normalX, nx); CHECK_NEAR(p.normalY, ny);
        };
        check(0, 0, 20, 10, 0, -1);                                   // Top middle
        check(3, 0, 23, 10, 0, -1);                                   // Clockwise: to the right
        check(6, 0, 26, 10, 0, -1);                                   // The corner's start
        check(6 + pi, 0, 26 + 4 * r2, 14 - 4 * r2, r2, -r2);           // Its middle, around (26, 14)
        check(6 + 2 * pi, 0, 30, 14, 1, 0);                           // Its end
        check(side, 0, 30, 20, 1, 0);                                 // Right middle
        check(side + 3, 0, 30, 23, 1, 0);                             // Downward
        check(2 * side, 0, 20, 30, 0, 1);                             // Bottom middle
        check(2 * side + 6 + pi, 0, 14 - 4 * r2, 26 + 4 * r2, -r2, r2); // Bottom-left corner
        check(3 * side, 0, 10, 20, -1, 0);                            // Left middle
        check(3 * side + 6 + pi, 0, 14 - 4 * r2, 14 - 4 * r2, -r2, -r2); // Top-left corner
        // Lengths wrap both ways.
        check(4 * side, 0, 20, 10, 0, -1);
        check(4 * side + 3, 0, 23, 10, 0, -1);
        check(-3, 0, 17, 10, 0, -1);
        check(-side, 0, 10, 20, -1, 0);
        check(-9 * side - 3, 0, 10, 23, -1, 0);
        // Offsets: outward along the normal, inward when negative.
        check(0, 2, 20, 8, 0, -1);
        check(0, -2, 20, 12, 0, -1);
        check(6 + pi, 1.5, 26 + 5.5 * r2, 14 - 5.5 * r2, r2, -r2);
        check(side, -1, 29, 20, 1, 0);
    });

    Run("rounded square band: a whole loop as two figures, a stretch cut square at both ends", [] {
        const double pi = std::numbers::pi, side = 12 + 2 * pi, r2 = std::sqrt(2.0) / 2;
        // Signed distance from the centreline (half-side 10, 4 DIP corners around (20, 20)), outward positive.
        auto distance = [](double x, double y) {
            double qx = std::abs(x - 20) - 6, qy = std::abs(y - 20) - 6;
            return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - 4;
        };
        auto line = [&](const TileOutlineSegment& s, double x, double y) { point(s, x, y); CHECK_NEAR(s.radius, 0); };
        auto arc = [&](const TileOutlineSegment& s, double x, double y, double radius, bool clockwise) {
            point(s, x, y); CHECK_NEAR(s.radius, radius); CHECK(s.clockwise == clockwise);
        };
        // A 2 DIP band all round: its outer and inner edges a DIP out and in, each back at its
        // start; lines along the sides, a quarter arc of radius 5 or 3 round each corner.
        for (double from : {0.0, 5.0}) {
            TileOutline whole = TileSquareBandOutline(20, 10, 4, 2, from, from + 4 * side + (from ? 1 : 0));
            CHECK(whole.size() == 2);
            for (int i = 0; i < 2; i++) {
                const double offset = i ? -1 : 1;
                const TileOutlineFigure& f = whole[i];
                TileSquarePoint start = TileSquareAt(20, 10, 4, from, offset);
                point(f, start.x, start.y);
                point(f.segments.back(), start.x, start.y);
                CHECK_NEAR(distance(f.x, f.y), offset);
                int arcs = 0;
                for (const TileOutlineSegment& s : f.segments) {
                    CHECK_NEAR(distance(s.x, s.y), offset);
                    CHECK(s.clockwise);
                    if (s.radius > 0) {
                        arcs++;
                        CHECK_NEAR(s.radius, 4 + offset);
                    }
                }
                CHECK(arcs == 4);
            }
        }
        point(TileSquareBandOutline(20, 10, 4, 2, 0, 4 * side)[0], 20, 9);
        point(TileSquareBandOutline(20, 10, 4, 2, 0, 4 * side)[1], 20, 11);
        // A quarter, top middle to right middle: out along the top, round the corner, down; a square
        // cut; back along the inner edge, counterclockwise round the corner.
        TileOutline quarter = TileSquareBandOutline(20, 10, 4, 2, 0, side);
        CHECK(quarter.size() == 1 && quarter[0].segments.size() == 7);
        const std::vector<TileOutlineSegment>& q = quarter[0].segments;
        point(quarter[0], 20, 9);
        line(q[0], 26, 9);
        arc(q[1], 31, 14, 5, true);
        line(q[2], 31, 20);
        line(q[3], 29, 20);
        line(q[4], 29, 14);
        arc(q[5], 26, 11, 3, false);
        line(q[6], 20, 11);
        // Across the top middle from a negative length: lines only, cut at both ends.
        TileOutline top = TileSquareBandOutline(20, 10, 4, 2, -3, 3);
        CHECK(top.size() == 1);
        point(top[0], 17, 9);
        point(top[0].segments.back(), 17, 11);
        for (const TileOutlineSegment& s : top[0].segments) {
            CHECK_NEAR(s.radius, 0);
            CHECK_NEAR(std::abs(distance(s.x, s.y)), 1);
        }
        CHECK(std::any_of(top[0].segments.begin(), top[0].segments.end(), [](const TileOutlineSegment& s) {
            return std::abs(s.x - 23) < 1e-9 && std::abs(s.y - 9) < 1e-9;
        }));
        // Ending inside a corner: cut across the band along the corner's normal there.
        TileOutline corner = TileSquareBandOutline(20, 10, 4, 2, 0, 6 + pi);
        CHECK(corner.size() == 1 && corner[0].segments.size() == 5);
        const std::vector<TileOutlineSegment>& c = corner[0].segments;
        line(c[0], 26, 9);
        arc(c[1], 26 + 5 * r2, 14 - 5 * r2, 5, true);
        line(c[2], 26 + 3 * r2, 14 - 3 * r2);
        arc(c[3], 26, 11, 3, false);
        line(c[4], 20, 11);
    });

    Run("rounded square tile: the segmented ring's slots along a rounded square, bands cut square", [] {
        const double pi = std::numbers::pi;
        // 32 DIP face, 3 DIP stroke: a centreline of half-side 14 around (16, 16) with 0.22 x 32 =
        // 7.04 DIP corners, 8 x 6.96 + 2 pi x 7.04 DIPs round.
        const double corner = 7.04, perimeter = 8 * (14 - corner) + 2 * pi * corner;
        auto square = [](std::array<bool, kQuotaBarCount> bars, std::array<double, kQuotaBarCount> pct, int badge = 0,
                         double textWidth = 0, BarLayout layout = BarLayout::RoundedSquare) {
            return AccountMeterState(Display(), layout, Account(L"anthropic", L"A", bars), Fresh(pct), badge, textWidth);
        };
        auto distance = [&](double x, double y) {
            double qx = std::abs(x - 16) - (14 - corner), qy = std::abs(y - 16) - (14 - corner);
            return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - corner;
        };
        // Every point on the band's edges, 1.5 DIPs either side of the centreline; arcs round its corners.
        auto onBand = [&](const TileOutline& outline) {
            CHECK(!outline.empty());
            for (const TileOutlineFigure& f : outline) {
                CHECK_NEAR(std::abs(distance(f.x, f.y)), 1.5);
                for (const TileOutlineSegment& s : f.segments) {
                    CHECK_NEAR(std::abs(distance(s.x, s.y)), 1.5);
                    if (s.radius > 0) CHECK(std::abs(s.radius - (corner + 1.5)) < 1e-6 || std::abs(s.radius - (corner - 1.5)) < 1e-6);
                }
            }
        };
        // One quota: the whole loop from the top middle, as two figures. Geometry and identity fit
        // as a single segmented ring's.
        AccountMeters one = square({true, false, false, false}, {50, -1, -1, -1}, 0, 1);
        AccountMeters ring = square({true, false, false, false}, {50, -1, -1, -1}, 0, 1, BarLayout::SegmentedRing);
        CHECK_NEAR(one.faceWidth, 32); CHECK_NEAR(one.faceHeight, 32);
        CHECK_NEAR(one.ring.stroke, 3); CHECK_NEAR(one.ring.radius, 14); CHECK_NEAR(one.ring.hole, 12.5);
        CHECK_NEAR(one.ring.logo, ring.ring.logo); CHECK_NEAR(one.ringTextFont, ring.ringTextFont);
        CHECK_NEAR(one.ringTextWidth, ring.ringTextWidth); CHECK_NEAR(one.ringWarnTop, ring.ringWarnTop);
        BadgeBox badge = square({true, false, false, false}, {50, -1, -1, -1}, 1).ringBadge;
        BadgeBox ringBadge = square({true, false, false, false}, {50, -1, -1, -1}, 1, 0, BarLayout::SegmentedRing).ringBadge;
        CHECK(badge.width > 0); CHECK_NEAR(badge.left, ringBadge.left); CHECK_NEAR(badge.top, ringBadge.top);
        const QuotaMeter& q = one.quotas[0];
        CHECK_NEAR(q.arc.start, 270); CHECK_NEAR(q.arc.sweep, 360);
        CHECK(q.track.size() == 2);
        point(q.track[0], 16, 0.5); point(q.track[1], 16, 3.5);
        onBand(q.track);
        // Half full: clockwise to the bottom middle, cut across there; round two corners each way.
        CHECK(q.fillUnits == 500); CHECK_NEAR(q.fillSweep, 180);
        CHECK(q.fill.size() == 1);
        onBand(q.fill);
        const std::vector<TileOutlineSegment>& f = q.fill[0].segments;
        point(q.fill[0], 16, 0.5);
        point(f.back(), 16, 3.5);
        auto at = [](const TileOutlineSegment& s, double x, double y) {
            return std::abs(s.x - x) < 1e-6 && std::abs(s.y - y) < 1e-6;
        };
        bool cut = false;
        for (size_t i = 0; i + 1 < f.size(); i++) cut |= at(f[i], 16, 31.5) && at(f[i + 1], 16, 28.5) && f[i + 1].radius == 0;
        CHECK(cut);
        CHECK(std::count_if(f.begin(), f.end(), [](const TileOutlineSegment& s) { return s.radius > 0; }) == 4);
        // Any usage: 5 degrees of perimeter.
        QuotaMeter tiny = square({true, false, false, false}, {0.5, -1, -1, -1}).quotas[0];
        CHECK(tiny.fillUnits == 14); CHECK_NEAR(tiny.fillSweep, 5);

        // Two quotas: the segmented ring's 150 degree slots, each grown by half the stroke (1.5
        // DIPs) along the perimeter at both ends.
        AccountMeters two = square({true, true, false, false}, {10, 20, -1, -1});
        const QuotaMeter &a = two.quotas[0], &b = two.quotas[1];
        CHECK_NEAR((195 - a.arc.start) / 360 * perimeter, 1.5); CHECK_NEAR((a.arc.sweep - 150) / 360 * perimeter, 3);
        CHECK_NEAR((b.arc.start - 165) / 360 * perimeter, 1.5); CHECK_NEAR((-150 - b.arc.sweep) / 360 * perimeter, 3);
        for (const QuotaMeter* m : {&a, &b}) {
            CHECK(m->track.size() == 1 && m->fill.size() == 1);
            onBand(m->track);
            onBand(m->fill);
        }
        // The first's track starts 75 degrees of perimeter and 1.5 DIPs before the top middle, on the
        // outer edge; its fill there too, the second's where its own fill ends, counterclockwise.
        TileSquarePoint start = TileSquareAt(16, 14, corner, -(75.0 / 360 * perimeter + 1.5), 1.5);
        point(a.track[0], start.x, start.y);
        point(a.fill[0], start.x, start.y);
        CHECK_NEAR(b.fillSweep, -(150 + 3 / perimeter * 360) * 0.2);
    });

    Run("rounded square pace tick: across the band along its normal, clear of a partial arc's ends", [] {
        const double pi = std::numbers::pi, r2 = std::sqrt(2.0) / 2;
        const double corner = 7.04, perimeter = 8 * (14 - corner) + 2 * pi * corner;
        auto loop = [](long long resetIn) { return First(Window(10, resetIn), Display(), BarLayout::RoundedSquare).quotas[0]; };
        // A quarter elapsed on the whole loop: the right edge's middle (30, 16), pointing out. The
        // halo from 3 DIPs in to 3 out (half the stroke plus 1.5), 3.5 wide; the core half a DIP
        // shorter at each end, 2 wide.
        QuotaMeter right = loop(13500000);
        CHECK(right.paceVisible); CHECK_NEAR(right.paceAngle, 360);
        CHECK(right.paceHalo.size() == 1 && right.paceHalo[0].segments.size() == 3);
        point(right.paceHalo[0], 27, 17.75);
        point(right.paceHalo[0].segments[0], 33, 17.75);
        point(right.paceHalo[0].segments[1], 33, 14.25);
        point(right.paceHalo[0].segments[2], 27, 14.25);
        point(right.paceCore[0], 27.5, 17);
        point(right.paceCore[0].segments[1], 32.5, 15);
        // An eighth: the top-right corner's middle, along its diagonal.
        QuotaMeter diagonal = loop(15750000);
        CHECK_NEAR(diagonal.paceAngle, 315);
        const TileOutlineFigure& halo = diagonal.paceHalo[0];
        const double px = 16 + 6.96 + corner * r2, py = 16 - 6.96 - corner * r2;
        CHECK_NEAR((halo.x + halo.segments[0].x + halo.segments[1].x + halo.segments[2].x) / 4, px);
        CHECK_NEAR((halo.y + halo.segments[0].y + halo.segments[1].y + halo.segments[2].y) / 4, py);
        CHECK_NEAR(halo.segments[0].x - halo.x, 6 * r2); CHECK_NEAR(halo.segments[0].y - halo.y, -6 * r2);
        CHECK_NEAR(halo.segments[1].x - halo.segments[0].x, -3.5 * r2);
        CHECK_NEAR(halo.segments[1].y - halo.segments[0].y, -3.5 * r2);
        // A whole loop has no ends: none elapsed is straight up across the top middle (16, 2).
        CHECK_NEAR(loop(kFiveHours).paceAngle, 270);
        point(loop(kFiveHours).paceHalo[0], 16 + 1.75, 2 + 3);
        // Two quotas: the tick's centre half the halo (1.75 DIPs) inside the grown arc's end, 0.25 DIPs
        // inside the slot; no corner to keep clear of, the band is cut square.
        auto two = [](long long resetIn) {
            AccountData data = Fresh();
            data.win5h = Window(10, resetIn);
            data.winWeek = Window(10, resetIn);
            return AccountMeterState(Display(), BarLayout::RoundedSquare,
                                     Account(L"anthropic", L"A", {true, true, false, false}), data, 0, 0);
        };
        AccountMeters start = two(kFiveHours), end = two(0), middle = two(9000000);
        CHECK_NEAR((start.quotas[0].paceAngle - 195) / 360 * perimeter, 0.25);
        CHECK_NEAR((345 - end.quotas[0].paceAngle) / 360 * perimeter, 0.25);
        CHECK_NEAR((165 - start.quotas[1].paceAngle) / 360 * perimeter, 0.25);
        CHECK_NEAR((end.quotas[1].paceAngle - 15) / 360 * perimeter, 0.25);
        // Mid-window: the top and bottom middles; the bottom's tick across (16, 30), pointing down.
        CHECK_NEAR(middle.quotas[0].paceAngle, 270); CHECK_NEAR(middle.quotas[1].paceAngle, 90);
        point(middle.quotas[1].paceHalo[0], 16 - 1.75, 30 - 3);
    });
}

static void RunCountdownTests() {
    Run("headline countdown: days and hours, hours and minutes, minutes, rounded up; now, no reset, no readings", [] {
        const ULONGLONG hour = 60 * kMinute, day = 24 * hour;
        auto countdown = [](WindowUsage first, bool on = true, BarLayout layout = BarLayout::LogoHeadline) {
            MeterDisplay d = Display(With([&](Settings& s) { s.headlineCountdown = on; }));
            return First(first, d, layout).headlineCountdown;
        };
        auto in = [&](long long resetIn) { return countdown({50, ULONGLONG((long long)kNow + resetIn), kWeeklyWindowMs}); };
        CHECK(std::wstring(kHeadlineCountdownArrow) == L"\u21BB ");
        CHECK(in(3 * day + 12 * hour) == L"\u21BB 3d 12h");
        CHECK(in(3 * day) == L"\u21BB 3d");
        // Days show whole hours.
        CHECK(in(2 * day + 5 * hour + 59 * kMinute) == L"\u21BB 2d 5h");
        CHECK(in(hour + 30 * kMinute) == L"\u21BB 1h 30m");
        CHECK(in(hour) == L"\u21BB 1h");
        CHECK(in(45 * kMinute) == L"\u21BB 45m");
        // Rounded up to the minute, carrying into hours and days.
        CHECK(in(44 * kMinute + 1) == L"\u21BB 45m");
        CHECK(in(1) == L"\u21BB 1m");
        CHECK(in(59 * kMinute + 30000) == L"\u21BB 1h");
        CHECK(in(day - kMinute + 1000) == L"\u21BB 1d");
        // Due or past: now. No reset time: says so. No reading: the dash.
        CHECK(in(0) == L"\u21BB now");
        CHECK(in(-(long long)kMinute) == L"\u21BB now");
        CHECK(countdown({50, 0, 0}) == L"\u21BB no reset");
        CHECK(countdown({-1, kNow + hour, kWeeklyWindowMs}) == L"\u21BB \u2013");
        AccountData before = Fresh();
        before.lastSuccessMs = 0;
        MeterDisplay on = Display(With([](Settings& s) { s.headlineCountdown = true; }));
        CHECK(AccountMeterState(on, BarLayout::LogoHeadline, Account(), before, 0, 0).headlineCountdown == L"\u21BB \u2013");
        // Off, or another tile style: none.
        CHECK(countdown({50, kNow + hour, kWeeklyWindowMs}, false).empty());
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical, BarLayout::SegmentedRing,
                                 BarLayout::ConcentricRings, BarLayout::Gauge, BarLayout::ClockPies,
                                 BarLayout::RoundedSquare, BarLayout::Sparkline, BarLayout::QuotaMatrix}) {
            CHECK(countdown({50, kNow + hour, kWeeklyWindowMs}, true, layout).empty());
        }
    });

    Run("headline countdown: the most-used quota's reset, in either bar mode", [] {
        auto meters = [](BarMode mode) {
            AccountData data = Fresh();
            data.win5h = {30, kNow + 45 * kMinute, kFiveHourWindowMs};
            data.winWeek = {80, kNow + (3 * 24 + 12) * 60 * kMinute, kWeeklyWindowMs};
            MeterDisplay d = Display(With([&](Settings& s) { s.headlineCountdown = true; s.barMode = mode; }));
            return AccountMeterState(d, BarLayout::LogoHeadline, Account(L"anthropic", L"A", {true, true, false, false}),
                                     data, 0, 0);
        };
        AccountMeters used = meters(BarMode::Used);
        CHECK(used.headline == L"80%" && used.headlineKey == L"7d" && used.headlineCountdown == L"\u21BB 3d 12h");
        AccountMeters left = meters(BarMode::Remaining);
        CHECK(left.headline == L"20%" && left.headlineCountdown == L"\u21BB 3d 12h");
        // The segments are still modeled; the tile draws the countdown instead.
        CHECK(used.barMask == 0b0011 && used.quotas[0].fillUnits == 300);
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
        // Every identity form, in both bar layouts: the Label form shows the label with its "!",
        // a logo carries the mark; either way warned and dimmed.
        AccountData failed = Fresh({10, -1, -1, -1});
        failed.stale = true;
        failed.error = L"timeout";
        for (TileIdentity identity : {TileIdentity::LogoBadge, TileIdentity::Logo, TileIdentity::Text}) {
            AccountConfig account = Account();
            account.tileIdentity = identity;
            for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical}) {
                AccountMeters marked = AccountMeterState(Display(), layout, account, failed, 0, 0);
                CHECK(marked.warn && marked.labelText == L"A!");
                CHECK_NEAR(marked.labelOpacity, 0.45); CHECK_NEAR(marked.identityOpacity, 0.45);
            }
        }
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
        // The third arc's slot starts at 285 degrees; it grows by half its 3 DIP stroke.
        CHECK_NEAR(ring.quotas[3].arc.start, 285 - 1.5 / 14 * 180 / std::numbers::pi);
        CHECK(ring.quotas[3].fillUnits == 0);
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
        CHECK(TileIdentityFor(mixed, 0).badgeText == L"1");
        CHECK(TileIdentityFor(mixed, 1).badgeText == L"2");
        CHECK(TileIdentityFor(mixed, 2).badgeText.empty()); CHECK(TileIdentityFor(mixed, 3).badgeText.empty());
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
        CHECK(TileIdentityFor(plain, 0).badgeText.empty()); CHECK(TileIdentityFor(plain, 1).badgeText.empty());
        // Hidden accounts count: the only visible account of a shared provider keeps its badge.
        std::vector<AccountConfig> shared = {Account(L"anthropic", L"CC 1"), Account(L"anthropic", L"CC 2"),
                                             Account(L"anthropic", L"CC 3"), Account(L"openai", L"OAI")};
        shared[1].hidden = true;
        shared[2].hidden = true;
        CHECK(TileIdentityFor(shared, 0).badgeText == L"1");
        CHECK(TileIdentityFor(shared, 1).badgeText == L"2" && TileIdentityFor(shared, 2).badgeText == L"3");
        CHECK(TileIdentityFor(shared, 3).badgeText.empty());
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
        // Mixed identities: OAI's label, the Anthropic pair's logos (badged "1" and "2"), Go's logo
        // alone.
        CHECK(accounts[0].tileIdentity == TileIdentity::Text && accounts[1].tileIdentity == TileIdentity::LogoBadge &&
              accounts[2].tileIdentity == TileIdentity::LogoBadge && accounts[3].tileIdentity == TileIdentity::Logo);
        CHECK(TileIdentityFor(accounts, 1).badgeText == L"1" && TileIdentityFor(accounts, 2).badgeText == L"2");
        CHECK(TileIdentityFor(accounts, 0).badgeText.empty() && TileIdentityFor(accounts, 3).badgeText.empty());
        for (const AccountConfig& account : accounts) CHECK(account.tileText.empty());
        CHECK((accounts[0].showBars == std::array<bool, kQuotaBarCount>{true, true, false, true}));
        CHECK(accounts[0].creditsMax == 1000);
        CHECK_NEAR(data[0].win5h.pct, 25); CHECK_NEAR(data[0].winWeek.pct, 62.5);
        CHECK_NEAR(data[0].extraUsage.pct, 12.5); CHECK_NEAR(data[0].extraUsedAmount, 125);
        CHECK_NEAR(data[0].extraLimitAmount, 1000); CHECK_NEAR(data[0].creditsBalance, 875);
        CHECK(data[0].win5h.resetUnixMs == kNow + 6300000); CHECK(data[0].win5h.windowDurationMs == kFiveHours);
        CHECK_NEAR(data[1].win5h.pct, 82.5); CHECK_NEAR(data[1].winWeek.pct, 25);
        CHECK_NEAR(data[2].winWeek.pct, 110); CHECK_NEAR(data[2].fableWeek.pct, 62.5);
        CHECK_NEAR(data[2].extraUsedAmount, 12.5); CHECK_NEAR(data[2].extraLimitAmount, 50);
        CHECK(data[2].winWeek.resetUnixMs == kNow + 241920000);
        CHECK_NEAR(data[3].antigravityThirdParty5h.pct, 62.5);
        for (size_t i = 0; i < data.size(); i++) {
            CHECK(data[i].plan == L"Visual test");
            CHECK(data[i].stale == (i == 3));
            CHECK(data[i].error.empty() == (i != 3));
            CHECK(data[i].lastSuccessMs == (i == 3 ? kNow - 10 * kMinute : kNow));
        }
        CHECK(data[3].error == L"Antigravity not running");
        std::vector<AccountConfig> only;
        BuildVisualTestSnapshot(Display(), &only, nullptr);
        CHECK(only.size() == 4);
        BuildVisualTestSnapshot(Display(With([](Settings& s) {
            s.yellowThreshold = 80; s.orangeThreshold = 60; s.redThreshold = 70;
        })), &accounts, &data);
        CHECK_NEAR(data[0].win5h.pct, 40); CHECK_NEAR(data[0].winWeek.pct, 80);
        CHECK_NEAR(data[2].winWeek.pct, 110);
    });

    Run("visual test snapshot: one stale account with its mark, one quota past 100%, at any time", [] {
        // Built at the time it's drawn: a day or a month later the same accounts are stale.
        for (ULONGLONG now : {kNow, kNow + 24 * 60 * kMinute, kNow + 30ULL * 24 * 60 * kMinute}) {
            for (int poll : {2, 1440}) {
                MeterDisplay display = MeterDisplayFor(With([&](Settings& s) { s.pollMinutes = poll; }), now, 1, 48);
                std::vector<AccountConfig> accounts;
                std::vector<AccountData> data;
                BuildVisualTestSnapshot(display, &accounts, &data);
                for (size_t i = 0; i < data.size(); i++) {
                    AccountMeters meters = AccountMeterState(display, BarLayout::Stacked, accounts[i], data[i], 0, 0);
                    CHECK(meters.stale == (i == 3) && meters.warn == (i == 3));
                }
            }
        }
        MeterDisplay display = Display();
        std::vector<AccountConfig> accounts;
        std::vector<AccountData> data;
        BuildVisualTestSnapshot(display, &accounts, &data);
        // Go is marked: its logo shows the mark, a Label identity its "!".
        AccountMeters go = AccountMeterState(display, BarLayout::Stacked, accounts[3], data[3], 0, 0);
        CHECK(go.warn && go.labelText == L"Go!" && go.quotas[0].color == 0xFF9E9E9E && !go.quotas[0].paceVisible);
        CHECK_NEAR(go.identityOpacity, 0.45);
        MeterDisplay unmarked = Display(With([](Settings& s) { s.showStaleWarning = false; }));
        go = AccountMeterState(unmarked, BarLayout::Stacked, accounts[3], data[3], 0, 0);
        CHECK(go.stale && !go.warn && go.labelText == L"Go");
        // CC 2's weekly quota: red past 100% when used, an empty 0% when remaining.
        AccountMeters cc2 = AccountMeterState(display, BarLayout::Stacked, accounts[2], data[2], 0, 0);
        CHECK(cc2.quotas[1].text == L"110%" && cc2.quotas[1].fillPx == 100 && cc2.quotas[1].color == 0xFFE53935);
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        cc2 = AccountMeterState(remaining, BarLayout::Stacked, accounts[2], data[2], 0, 0);
        CHECK(cc2.quotas[1].text == L"0%" && cc2.quotas[1].fillPx == 0 && cc2.quotas[1].color == 0xFFE53935);
    });
}

static void RunSparklineTests() {
    static constexpr auto point = [](const auto& p, double x, double y) { CHECK_NEAR(p.x, x); CHECK_NEAR(p.y, y); };
    // A disc: from its top through its bottom and back, clockwise; counterclockwise it cuts a hole
    // in a nonzero fill.
    static constexpr auto disc = [](const TileOutlineFigure& figure, double x, double y, double r, bool clockwise = true) {
        CHECK(figure.segments.size() == 2);
        point(figure, x, y - r);
        point(figure.segments[0], x, y + r);
        point(figure.segments[1], x, y - r);
        for (const TileOutlineSegment& s : figure.segments) {
            CHECK_NEAR(s.radius, r);
            CHECK(s.clockwise == clockwise);
        }
    };
    static constexpr auto circle = [](const TileOutline& outline, double x, double y, double r) {
        CHECK(outline.size() == 1);
        disc(outline[0], x, y, r);
    };
    // One figure from (x, y) through each segment's end point; arcs (a radius) clockwise.
    struct Seg {
        double x, y, radius = 0;
    };
    static constexpr auto path = [](const TileOutline& outline, double x, double y, const std::vector<Seg>& segments) {
        CHECK(outline.size() == 1 && outline[0].segments.size() == segments.size());
        point(outline[0], x, y);
        for (size_t k = 0; k < segments.size(); k++) {
            point(outline[0].segments[k], segments[k].x, segments[k].y);
            CHECK_NEAR(outline[0].segments[k].radius, segments[k].radius);
            CHECK(outline[0].segments[k].clockwise);
        }
    };
    static constexpr auto line = [](const TileOutline& outline, double x0, double y0, double x1, double y1) {
        path(outline, x0, y0, {{x1, y1}});
    };
    // The current reading at (x, y): the 4.1 DIP cut-out, the ring from 3 to 4.1 DIPs (its inner
    // circle counterclockwise: the hole) and the 2.2 DIP dot.
    static constexpr auto reading = [](const QuotaMeter& q, double x, double y) {
        circle(q.spark[kSparkNowCut], x, y, 4.1);
        CHECK(q.spark[kSparkNowRing].size() == 2);
        disc(q.spark[kSparkNowRing][0], x, y, 4.1);
        disc(q.spark[kSparkNowRing][1], x, y, 3, false);
        circle(q.spark[kSparkNow], x, y, 2.2);
    };
    // The plain current reading (the ring off, its default): a 2 DIP dot, no cut-out or ring.
    static constexpr auto dot = [](const QuotaMeter& q, double x, double y) {
        CHECK(q.spark[kSparkNowCut].empty() && q.spark[kSparkNowRing].empty());
        circle(q.spark[kSparkNow], x, y, 2);
    };
    // The ringed reading and the caret on, both off by default.
    static constexpr auto cues = [](Settings& s) {
        s.sparklineRing = true;
        s.sparklineNowCaret = true;
    };
    static constexpr auto unmarked = [](const QuotaMeter& q) {
        return q.spark[kSparkNowCut].empty() && q.spark[kSparkNowRing].empty() && q.spark[kSparkNow].empty();
    };
    static constexpr auto bare = [](const QuotaMeter& q) {
        return std::all_of(q.spark.begin(), q.spark.end(), [](const TileOutline& shape) { return shape.empty(); });
    };
    // Channels within one step of hand-picked expectations (OKLCH lifts).
    static constexpr auto nearColor = [](uint32_t actual, uint32_t expected) {
        CHECK(actual >> 24 == expected >> 24);
        for (int shift : {16, 8, 0}) CHECK(std::abs(int(actual >> shift & 0xFF) - int(expected >> shift & 0xFF)) <= 1);
    };
    // A 100 x 24 DIP plot (60% of the 40 DIP tile): x is the window's share in hundredths,
    // y = 22 - used / 5 (2 DIPs free at the top and bottom).
    static constexpr auto plot = [](const std::function<void(Settings&)>& edit = {}) {
        return Display(With([&](Settings& s) {
            s.sparklineWidth = 100;
            s.sparklineHeight = 60;
            if (edit) edit(s);
        }));
    };
    // A time `share` of the way through a five-hour window that resets 2.5 hours after kNow
    // (Window(pct, 9000000)), so kNow is half way.
    static constexpr auto at = [](double share) {
        return ULONGLONG((long long)kNow - 9000000 + std::llround(share * kFiveHours));
    };
    // A sparkline of the first slot's `first` reading, `trace` its recorded usage, on that plot
    // unless given another display.
    static constexpr auto spark = [](WindowUsage first, std::vector<TileTracePoint> trace = {},
                                     std::optional<MeterDisplay> d = std::nullopt) {
        AccountData data = Fresh();
        data.win5h = first;
        return AccountMeterState(d ? *d : plot(), BarLayout::Sparkline, Account(L"anthropic", L"A", {true, false, false, false}),
                                 data, 0, 0, [trace](int, const WindowUsage&) { return trace; });
    };

    Run("sparkline: a chart per shown quota with a window, side by side in slot order; else the most-used one's", [] {
        // The extra usage corner off here: extra usage is a quota like the others.
        const MeterDisplay off = Display(With([](Settings& s) { s.sparklineExtra = SparklineExtra::Off; }));
        auto charts = [&](const AccountData& data, std::array<bool, kQuotaBarCount> bars = {true, true, true, true},
                          const MeterDisplay* d = nullptr) {
            return AccountMeterState(d ? *d : off, BarLayout::Sparkline, Account(L"anthropic", L"A", bars), data, 0, 0);
        };
        auto lefts = [](const AccountMeters& m, std::initializer_list<std::pair<int, double>> expected) {
            for (auto [slot, left] : expected) CHECK_NEAR(m.quotas[slot].sparkLeft, left);
        };
        // Windows on the 5-hour and weekly quotas; Fable's 60% and extra usage's 20% have none: two
        // 48 x 32 DIP charts 4 DIPs apart, 100 DIPs across, the weekly one 52 DIPs in.
        AccountData data = Fresh({90, 40, 60, 20});
        data.win5h = Window(90, 9000000);
        data.winWeek = {40, kNow + kMinute, kWeeklyWindowMs};
        AccountMeters two = charts(data);
        CHECK(two.barMask == 0b0011);
        CHECK_NEAR(two.faceWidth, 100); CHECK_NEAR(two.faceHeight, 32);
        lefts(two, {{0, 0}, {1, 52}});
        CHECK(!bare(two.quotas[0]) && !bare(two.quotas[1]) && bare(two.quotas[2]) && bare(two.quotas[3]));
        // Each its own reading at its own window's place: the 5-hour quota half way at 90%, the
        // weekly one at its end (a minute left) at 40%.
        dot(two.quotas[0], 24, 4.75);
        dot(two.quotas[1], 48, 18.75);
        // Windows on the 5-hour, Fable and extra usage quotas, not the weekly one: three charts,
        // Fable's second, extra usage's third.
        data.winWeek = {40, 0, 0};
        data.fableWeek = {60, kNow + 2 * kMinute, kWeeklyWindowMs};
        data.extraUsage = {20, kNow + 3 * kMinute, kFiveHours};
        AccountMeters three = charts(data);
        CHECK(three.barMask == 0b1101 && bare(three.quotas[1]));
        CHECK_NEAR(three.faceWidth, 3 * 48 + 2 * 4);
        lefts(three, {{0, 0}, {2, 52}, {3, 104}});
        // Shown quotas only: the 5-hour one hidden, Fable's chart first.
        AccountMeters hidden = charts(data, {false, true, true, true});
        CHECK(hidden.barMask == 0b1100 && bare(hidden.quotas[0]));
        CHECK_NEAR(hidden.faceWidth, 100);
        lefts(hidden, {{2, 0}, {3, 52}});
        // The chart width sets each chart: 24 DIPs, 28 apart; 120, 124 apart.
        MeterDisplay narrow = Display(With([](Settings& s) { s.sparklineExtra = SparklineExtra::Off; s.sparklineWidth = 24; }));
        AccountMeters small = charts(data, {true, true, true, true}, &narrow);
        CHECK_NEAR(small.faceWidth, 80);
        lefts(small, {{0, 0}, {2, 28}, {3, 56}});
        MeterDisplay wide = Display(With([](Settings& s) { s.sparklineExtra = SparklineExtra::Off; s.sparklineWidth = 120; }));
        AccountMeters large = charts(data, {true, true, true, true}, &wide);
        CHECK_NEAR(large.faceWidth, 368);
        lefts(large, {{0, 0}, {2, 124}, {3, 248}});

        // Before the first success: a chart per shown quota not read yet (the plot lit whole, no
        // reading); the 5-hour quota's reading without a window has none.
        AccountData before = Fresh({42, -1, -1, -1});
        before.lastSuccessMs = 0;
        AccountMeters waiting = charts(before);
        CHECK(waiting.barMask == 0b1110 && bare(waiting.quotas[0]));
        CHECK_NEAR(waiting.faceWidth, 152);
        lefts(waiting, {{1, 0}, {2, 52}, {3, 104}});
        CHECK(!waiting.quotas[1].spark[kSparkLit].empty() && unmarked(waiting.quotas[1]));
        before.win5h.pct = -1;
        AccountMeters first = charts(before, {false, true, false, true});
        CHECK(first.barMask == 0b1010);
        CHECK_NEAR(first.faceWidth, 100);
        lefts(first, {{1, 0}, {3, 52}});

        // Without a window: the most-used quota's chart alone; a tie to the earlier slot; shown
        // quotas only.
        AccountMeters top = charts(Fresh({30, 70, -1, 50}));
        CHECK(top.barMask == 0b0010);
        CHECK_NEAR(top.faceWidth, 48); CHECK_NEAR(top.quotas[1].sparkLeft, 0);
        CHECK(charts(Fresh({70, 70, -1, -1})).barMask == 0b0001);
        CHECK(charts(Fresh({30, 70, -1, 50}), {true, false, true, true}).barMask == 0b1000);
        // A reset not past its duration, or no duration, isn't a window.
        AccountData odd = Fresh({95, 70, -1, 99});
        odd.win5h = {95, kFiveHours, kFiveHours};
        odd.extraUsage = {99, kNow + kMinute, 0};
        CHECK(charts(odd).barMask == 0b1000);
        odd.winWeek = {70, kNow + kMinute, kWeeklyWindowMs};
        CHECK(charts(odd).barMask == 0b0010);
        // Nothing reported after a success: no chart, no width.
        AccountMeters none = charts(Fresh());
        CHECK(none.barMask == 0);
        CHECK_NEAR(none.faceWidth, 0);
        for (const QuotaMeter& q : none.quotas) CHECK(bare(q));
    });

    Run("sparkline: the charts' height a percentage of the tile, 50 to 100", [] {
        auto chart = [](int percent, double taskbar = 48) {
            MeterDisplay d = Display(With([&](Settings& s) { s.sparklineHeight = percent; }), 1, taskbar);
            return First(Window(50, 9000000), d, BarLayout::Sparkline);
        };
        auto check = [](const AccountMeters& m, double height) {
            CHECK_NEAR(m.faceWidth, 48); CHECK_NEAR(m.faceHeight, height);
            // The text keeps its size whatever the height.
            CHECK_NEAR(m.sparkKeyFont, 8);
        };
        // A 48 DIP taskbar's 40 DIP tile: 32 DIPs at 80% (the default), 30 at 75%, 40 at 100%, 20
        // at 50%.
        check(First(Window(50, 9000000), Display(), BarLayout::Sparkline), 32);
        check(chart(80), 32);
        check(chart(75), 30);
        check(chart(100), 40);
        check(chart(50), 20);
        // A 32 DIP taskbar's 24 DIP tile: 80% is 19.2, 19 DIPs; 90% 21.6, 22; 24 and 12 at the
        // ends. A 40 DIP taskbar's 32 DIP tile: 25.6, 26.
        check(chart(80, 32), 19);
        check(chart(90, 32), 22);
        check(chart(100, 32), 24);
        check(chart(50, 32), 12);
        check(chart(80, 40), 26);
        // The shapes follow: on the 19 DIP chart the diagonal from (0, 17) to (48, 2), 50% half way
        // at (24, 9.5).
        AccountMeters low = chart(80, 32);
        line(low.quotas[0].spark[kSparkPace], 0, 17, 48, 2);
        dot(low.quotas[0], 24, 9.5);
        // The height clamps at the display entry: 80 by default, 50 to 100.
        CHECK(Display().sparklineHeight == 80);
        CHECK(Display(With([](Settings& s) { s.sparklineHeight = 65; })).sparklineHeight == 65);
        CHECK(Display(With([](Settings& s) { s.sparklineHeight = 10; })).sparklineHeight == 50);
        CHECK(Display(With([](Settings& s) { s.sparklineHeight = 999; })).sparklineHeight == 100);
        check(chart(-5), 20);
        check(chart(1 << 30), 40);
        // The chart width likewise: 48 by default, 24 to 120.
        CHECK(Display().sparklineWidth == 48);
        CHECK(Display(With([](Settings& s) { s.sparklineWidth = 64; })).sparklineWidth == 64);
        CHECK(Display(With([](Settings& s) { s.sparklineWidth = 10; })).sparklineWidth == 24);
        CHECK(Display(With([](Settings& s) { s.sparklineWidth = 999; })).sparklineWidth == 120);
        MeterDisplay narrow = Display(With([](Settings& s) { s.sparklineWidth = -5; }));
        CHECK_NEAR(First(Window(50, 9000000), narrow, BarLayout::Sparkline).faceWidth, 24);
    });

    Run("sparkline: the text size sets the keys' and the extra usage's font and their placement boxes", [] {
        auto sized = [](int size, const std::function<void(Settings&)>& edit = {}) {
            return Display(With([&](Settings& s) {
                s.sparklineTextSize = size;
                if (edit) edit(s);
            }));
        };
        // 8 DIPs by default, 6 to 16; the font whatever the chart's height.
        CHECK(Display().sparklineTextSize == 8);
        CHECK(sized(12).sparklineTextSize == 12);
        CHECK(sized(2).sparklineTextSize == 6 && sized(99).sparklineTextSize == 16);
        CHECK_NEAR(First(Window(50, 9000000), sized(12), BarLayout::Sparkline).sparkKeyFont, 12);
        MeterDisplay shortChart = Display(With([](Settings& s) {
            s.sparklineHeight = 50;
            s.sparklineTextSize = 14;
        }), 1, 32);
        AccountMeters tiny = First(Window(50, 9000000), shortChart, BarLayout::Sparkline);
        CHECK_NEAR(tiny.faceHeight, 12); CHECK_NEAR(tiny.sparkKeyFont, 14);
        // The key's box on the 48 x 32 chart: 8 DIPs: x 35.4 to 45, at the bottom y 23.4 to 29; 12
        // DIPs (0.6 of it per character, a 0.7 cap height): x 30.6 to 45, y 20.6 to 29, at the top
        // 3 to 11.4. 20% at 60% of the window, the dot at (28.75, 24.5) reaching x 30.75: clear of
        // the 8 DIP key (kept), on the 12 DIP one (moved to the top).
        auto first = [](double pct, double share, const MeterDisplay& d) {
            return spark(Window(pct, std::llround((1 - share) * kFiveHours)), {}, d).quotas[0];
        };
        dot(first(20, 0.6, sized(8)), 28.75, 24.5);
        CHECK(!first(20, 0.6, sized(8)).sparkKeyTop);
        CHECK(first(20, 0.6, sized(12)).sparkKeyTop);
        // The now line stops a DIP short of the larger box: 90% at 80% of the window, x 38.5, the dot
        // clear of the key at the bottom: down to 19.6 at 12 DIPs (22.4 at 8).
        QuotaMeter large = first(90, 0.8, sized(12));
        CHECK(!large.sparkKeyTop);
        path(large.spark[kSparkNowLine], 38, 1, {{39, 1}, {39, 19.6}, {38, 19.6}});
        path(first(90, 0.8, sized(8)).spark[kSparkNowLine], 38, 1, {{39, 1}, {39, 22.4}, {38, 22.4}});
        // The extra usage's box: "$20.00" from x 3 to 3 + 6 x 0.6 of the size: 24.6 at 6 DIPs, 31.8
        // at 8, 46.2 at 12 (across the 12 DIP key's box: the two never share an edge). The weekly
        // dot at 110% at 60% of the week, (28.75, 2), from x 26.75: clear of it at 6 DIPs (kept);
        // on it at 8 (moved to the bottom); at 12 on it, and with both moved on the key at the top:
        // one hit either way (kept).
        auto weekly = [](const MeterDisplay& d) {
            AccountData data = Fresh();
            data.winWeek = {110, kNow + (ULONGLONG)std::llround(0.4 * kWeeklyWindowMs), kWeeklyWindowMs};
            data.extraUsage = {40, kNow + 10 * 24 * 60 * kMinute, 30ULL * 24 * 60 * kMinute};
            data.extraUsedAmount = 20;
            data.extraLimitAmount = 50;
            return AccountMeterState(d, BarLayout::Sparkline, Account(L"anthropic", L"A", {false, true, false, true}), data,
                                     0, 0);
        };
        AccountMeters six = weekly(sized(6)), eight = weekly(sized(8)), twelve = weekly(sized(12));
        CHECK(six.sparkExtraText == L"$20.00" && !six.sparkExtraBottom && !six.quotas[1].sparkKeyTop);
        CHECK(eight.sparkExtraBottom && !eight.quotas[1].sparkKeyTop);
        CHECK(!twelve.sparkExtraBottom && !twelve.quotas[1].sparkKeyTop);
    });

    Run("sparkline: the even-use diagonal across each window, with pace markers on", [] {
        // From 0% at the window's start to 100% at its reset, 2 DIPs inside the plot's bottom and top.
        line(spark(Window(30, 9000000)).quotas[0].spark[kSparkPace], 0, 22, 100, 2);
        line(First(Window(30, 9000000), Display(), BarLayout::Sparkline).quotas[0].spark[kSparkPace], 0, 30, 48, 2);
        // Remaining mode plots what is left: from 100% at the window's start down to 0% at its reset.
        AccountMeters left = spark(Window(30, 9000000), {}, plot([](Settings& s) { s.barMode = BarMode::Remaining; }));
        line(left.quotas[0].spark[kSparkPace], 0, 2, 100, 22);
        // Each chart its own, from its own corner.
        AccountData both = Fresh();
        both.win5h = Window(30, 9000000);
        both.winWeek = {50, kNow + kMinute, kWeeklyWindowMs};
        AccountMeters two = AccountMeterState(Display(), BarLayout::Sparkline, Account(L"anthropic", L"A", {true, true, false, false}),
                                              both, 0, 0);
        line(two.quotas[0].spark[kSparkPace], 0, 30, 48, 2);
        line(two.quotas[1].spark[kSparkPace], 0, 30, 48, 2);
        // Markers off, or no window: none.
        CHECK(spark(Window(30, 9000000), {}, plot([](Settings& s) { s.showPaceTicks = false; })).quotas[0].spark[kSparkPace].empty());
        CHECK(spark({30, 0, 0}).quotas[0].spark[kSparkPace].empty());
        CHECK(spark({30, kFiveHours, kFiveHours}).quotas[0].spark[kSparkPace].empty());
    });

    Run("sparkline: recorded runs with an area under each, lone readings as dots, clamped to the plot", [] {
        // A reading from before the window (drawn at its start), a run, a gap, a lone reading, then
        // a run ending past 100% (at the top inset).
        AccountMeters m = spark(Window(45, 9000000), {{at(-0.1), 10, false}, {at(0.1), 20, true}, {at(0.2), 30, true},
                                                      {at(0.3), 35, false}, {at(0.4), 40, false}, {at(0.45), 120, true}});
        const auto& shapes = m.quotas[0].spark;
        CHECK(shapes[kSparkLines].size() == 2 && shapes[kSparkArea].size() == 2 && shapes[kSparkDots].size() == 1);
        const TileOutlineFigure &run = shapes[kSparkLines][0], &last = shapes[kSparkLines][1];
        CHECK(run.segments.size() == 2 && last.segments.size() == 1);
        point(run, 0, 20); point(run.segments[0], 10, 18); point(run.segments[1], 20, 16);
        point(last, 40, 14); point(last.segments[0], 45, 2);
        for (const TileOutlineFigure& figure : shapes[kSparkLines]) {
            for (const TileOutlineSegment& s : figure.segments) CHECK_NEAR(s.radius, 0);
        }
        // Each area up from the plot's bottom under its run's start, along the run and back down.
        const TileOutlineFigure &under = shapes[kSparkArea][0], &underLast = shapes[kSparkArea][1];
        CHECK(under.segments.size() == 4 && underLast.segments.size() == 3);
        point(under, 0, 24); point(under.segments[0], 0, 20); point(under.segments[1], 10, 18);
        point(under.segments[2], 20, 16); point(under.segments[3], 20, 24);
        point(underLast, 40, 24); point(underLast.segments[0], 40, 14); point(underLast.segments[1], 45, 2);
        point(underLast.segments[2], 45, 24);
        // The lone reading: a 1.5 DIP dot.
        circle(shapes[kSparkDots], 30, 15, 1.5);
        // Quarter DIPs: on the 48 x 32 plot a tenth of the window at 10% is (4.8, 27.2), drawn at
        // (4.75, 27.25); a fifth at 20%, (9.6, 24.4), at (9.5, 24.5).
        AccountMeters snapped = spark(Window(45, 9000000), {{at(0.1), 10, false}, {at(0.2), 20, true}}, Display());
        CHECK(snapped.quotas[0].spark[kSparkLines].size() == 1 && snapped.quotas[0].spark[kSparkDots].empty());
        point(snapped.quotas[0].spark[kSparkLines][0], 4.75, 27.25);
        point(snapped.quotas[0].spark[kSparkLines][0].segments[0], 9.5, 24.5);
        // No history: no runs, areas or dots; the current reading stays.
        AccountMeters none = spark(Window(45, 9000000));
        CHECK(none.quotas[0].spark[kSparkLines].empty() && none.quotas[0].spark[kSparkArea].empty() &&
              none.quotas[0].spark[kSparkDots].empty() && !unmarked(none.quotas[0]));
    });

    Run("sparkline: the recorded usage asked for each charted quota with a window, with its reading", [] {
        struct Ask {
            int slot;
            WindowUsage reading;
        };
        std::vector<Ask> asked;
        TileTraceSource source = [&](int slot, const WindowUsage& reading) {
            asked.push_back({slot, reading});
            return std::vector<TileTracePoint>{};
        };
        // Windows on the weekly, Fable and extra usage quotas ($10 of $50 spent); the 5-hour one
        // has none.
        AccountData data = Fresh({90, 40, 60, 20});
        data.winWeek = {40, kNow + kMinute, kWeeklyWindowMs};
        data.fableWeek = {60, kNow + 2 * kMinute, kWeeklyWindowMs};
        data.extraUsage = {20, kNow + 3 * kMinute, kFiveHours};
        data.extraUsedAmount = 10;
        data.extraLimitAmount = 50;
        // In slot order; extra usage in the weekly chart's corner is never asked.
        AccountMeterState(Display(), BarLayout::Sparkline, Account(), data, 0, 0, source);
        CHECK(asked.size() == 2 && asked[0].slot == 1 && asked[0].reading.pct == 40 &&
              asked[0].reading.resetUnixMs == kNow + kMinute && asked[1].slot == 2 && asked[1].reading.pct == 60 &&
              asked[1].reading.resetUnixMs == kNow + 2 * kMinute && asked[1].reading.windowDurationMs == kWeeklyWindowMs);
        // With the corner off, its chart asks too.
        asked.clear();
        MeterDisplay off = Display(With([](Settings& s) { s.sparklineExtra = SparklineExtra::Off; }));
        AccountMeterState(off, BarLayout::Sparkline, Account(), data, 0, 0, source);
        CHECK(asked.size() == 3 && asked[2].slot == 3 && asked[2].reading.pct == 20 &&
              asked[2].reading.windowDurationMs == kFiveHours);
        // Without a window: the most-used quota, never asked.
        asked.clear();
        AccountMeterState(Display(), BarLayout::Sparkline, Account(), Fresh({90, 40, 60, 20}), 0, 0, source);
        CHECK(asked.empty());
        // No other style asks, the bars included.
        for (int layout = 0; layout <= int(kLastBarLayout); layout++) {
            if (BarLayout(layout) != BarLayout::Sparkline) {
                AccountMeterState(Display(), BarLayout(layout), Account(), data, 0, 0, source);
            }
        }
        CHECK(asked.empty());
        // No source: charted without history.
        AccountMeters none = AccountMeterState(Display(), BarLayout::Sparkline, Account(), data, 0, 0);
        CHECK(none.barMask == 0b0110 && none.quotas[1].spark[kSparkLines].empty() && !unmarked(none.quotas[1]) &&
              !unmarked(none.quotas[2]));
    });

    Run("sparkline: the current reading at now, after the newest reading, or at the right edge without a window", [] {
        // With the ring (and the caret) on.
        const MeterDisplay ringed = plot(cues);
        // Half way through the window at 45%: (50, 13).
        reading(spark(Window(45, 9000000), {}, ringed).quotas[0], 50, 13);
        // A history ending at the poll: the same place, on its last reading.
        AccountMeters recorded = spark(Window(45, 9000000), {{at(0.2), 20, false}, {at(0.5), 45, true}}, ringed);
        reading(recorded.quotas[0], 50, 13);
        point(recorded.quotas[0].spark[kSparkLines][0].segments[0], 50, 13);
        // A clock rolled back behind the newest reading: the reading and the time stay at that
        // reading's (the lit part, the now line and the caret at x 60), and the projection starts
        // there at the rate up to it: 45% in 60% of the window is 75% at the reset.
        AccountMeters rolled = spark(Window(45, 9000000), {{at(0.2), 20, false}, {at(0.6), 45, true}}, ringed);
        reading(rolled.quotas[0], 60, 13);
        line(rolled.quotas[0].spark[kSparkProjection], 60, 13, 100, 7);
        point(rolled.quotas[0].spark[kSparkLit][0].segments[0], 60, 0);
        point(rolled.quotas[0].spark[kSparkNowLine][0], 59.5, 1);
        point(rolled.quotas[0].spark[kSparkCaret][0].segments[0], 60, 20.8);
        // Over pace, it still runs forward: 80% in 70% reaches the limit at 87.5%.
        AccountMeters ahead = spark(Window(80, 9000000), {{at(0.2), 20, false}, {at(0.7), 80, true}}, ringed);
        line(ahead.quotas[0].spark[kSparkProjection], 70, 6, 87.5, 2);
        circle(ahead.quotas[0].spark[kSparkLimit], 87.5, 2, 1.75);
        // Before the window starts: at its start; past the reset: at its end.
        reading(spark(Window(45, kFiveHours + 3600000), {}, ringed).quotas[0], 0, 13);
        reading(spark(Window(45, -(long long)kMinute), {}, ringed).quotas[0], 100, 13);
        // At the top past 100%. Remaining mode plots what is left (y = 2 + used / 5): 55% at y 11,
        // at the bottom past 100% used; without a window too.
        MeterDisplay left = plot([](Settings& s) {
            cues(s);
            s.barMode = BarMode::Remaining;
        });
        reading(spark(Window(130, 9000000), {}, ringed).quotas[0], 50, 2);
        reading(spark(Window(45, 9000000), {}, left).quotas[0], 50, 11);
        reading(spark(Window(130, 9000000), {}, left).quotas[0], 50, 22);
        reading(spark({45, 0, 0}, {}, left).quotas[0], 95.9, 11);
        // Without a window: alone, the ring's radius in from the plot's right edge; no diagonal,
        // time, projection or limit.
        AccountMeters alone = spark({45, 0, 0}, {}, ringed);
        const auto& shapes = alone.quotas[0].spark;
        reading(alone.quotas[0], 95.9, 13);
        CHECK(shapes[kSparkPace].empty() && shapes[kSparkNowLine].empty() && shapes[kSparkCaret].empty() &&
              shapes[kSparkProjection].empty() && shapes[kSparkLimit].empty() && !shapes[kSparkLit].empty());
        reading(First({45, 0, 0}, Display(With(cues)), BarLayout::Sparkline).quotas[0], 43.9, 17.5);
        // A window without a reading yet (before the first success): the diagonal and the time, no
        // reading, projection or limit.
        AccountData before = Fresh();
        before.lastSuccessMs = 0;
        before.win5h = {-1, kNow + 9000000, kFiveHours};
        AccountMeters waiting = AccountMeterState(ringed, BarLayout::Sparkline,
                                                  Account(L"anthropic", L"A", {true, false, false, false}), before, 0, 0);
        const auto& empty = waiting.quotas[0].spark;
        CHECK(waiting.barMask == 0b0001 && empty[kSparkPace].size() == 1 && !empty[kSparkLit].empty() &&
              !empty[kSparkNowLine].empty() && !empty[kSparkCaret].empty());
        CHECK(unmarked(waiting.quotas[0]) && empty[kSparkProjection].empty() && empty[kSparkLimit].empty());

        // The ring off, the default: a plain 2 DIP dot at the same places, without a window its own
        // radius in from the right edge: (98, 13) on the plot, (46, 17.5) on the 48 x 32 chart.
        dot(spark(Window(45, 9000000)).quotas[0], 50, 13);
        dot(spark(Window(45, 9000000), {{at(0.2), 20, false}, {at(0.6), 45, true}}).quotas[0], 60, 13);
        dot(spark(Window(130, 9000000)).quotas[0], 50, 2);
        dot(spark({45, 0, 0}).quotas[0], 98, 13);
        dot(First({45, 0, 0}, Display(), BarLayout::Sparkline).quotas[0], 46, 17.5);
        // None before a reading.
        AccountMeters plainWaiting = AccountMeterState(plot(), BarLayout::Sparkline,
                                                       Account(L"anthropic", L"A", {true, false, false, false}), before, 0, 0);
        CHECK(unmarked(plainWaiting.quotas[0]));
    });

    Run("sparkline: the plot lit up to now, clockwise from its top edge, cut through its rounded corners", [] {
        // Where x 1 DIP in from a side meets a 3 DIP corner's arc: 3 - sqrt(3^2 - 2^2) from the edge.
        const double k = 3 - std::sqrt(5.0);
        // Before the window starts, or at its start: nothing lit.
        CHECK(spark(Window(45, kFiveHours + 3600000)).quotas[0].spark[kSparkLit].empty());
        CHECK(spark(Window(45, kFiveHours)).quotas[0].spark[kSparkLit].empty());
        // Half way: along the top edge to x 50, down, back along the bottom, around the bottom-left
        // corner, up the left side and around the top-left corner.
        path(spark(Window(45, 9000000)).quotas[0].spark[kSparkLit], 3, 0,
             {{50, 0}, {50, 24}, {3, 24}, {0, 21, 3}, {0, 3}, {3, 0, 3}});
        // 1% in (x 1), inside the left corners: from the top-left arc down to the bottom-left one,
        // around it, up the left side and around the top-left arc back.
        path(spark(Window(45, 17820000)).quotas[0].spark[kSparkLit], 1, k, {{1, 24 - k}, {0, 21, 3}, {0, 3}, {1, k, 3}});
        // 99% in (x 99), inside the right corners: the top edge, the top-right arc to x 99, down to the
        // bottom-right arc and around it, then as above.
        path(spark(Window(45, 180000)).quotas[0].spark[kSparkLit], 3, 0,
             {{97, 0}, {99, k, 3}, {99, 24 - k}, {97, 24, 3}, {3, 24}, {0, 21, 3}, {0, 3}, {3, 0, 3}});
        // 97% in, where the right corners start: a straight cut.
        path(spark(Window(45, 540000)).quotas[0].spark[kSparkLit], 3, 0,
             {{97, 0}, {97, 24}, {3, 24}, {0, 21, 3}, {0, 3}, {3, 0, 3}});
        // Past the reset, and without a window (with a reading or not yet): the whole plot.
        const std::vector<Seg> whole = {{97, 0}, {100, 3, 3}, {100, 21}, {97, 24, 3}, {3, 24}, {0, 21, 3}, {0, 3}, {3, 0, 3}};
        path(spark(Window(45, -(long long)kMinute)).quotas[0].spark[kSparkLit], 3, 0, whole);
        path(spark({45, 0, 0}).quotas[0].spark[kSparkLit], 3, 0, whole);
        AccountData before = Fresh();
        before.lastSuccessMs = 0;
        AccountMeters waiting = AccountMeterState(plot(), BarLayout::Sparkline,
                                                  Account(L"anthropic", L"A", {true, false, false, false}), before, 0, 0);
        path(waiting.quotas[0].spark[kSparkLit], 3, 0, whole);
        // Elapsed time off: the whole plot with a window too, half way or before it starts.
        const MeterDisplay unlit = plot([](Settings& s) { s.sparklineElapsed = false; });
        path(spark(Window(45, 9000000), {}, unlit).quotas[0].spark[kSparkLit], 3, 0, whole);
        path(spark(Window(45, kFiveHours + 3600000), {}, unlit).quotas[0].spark[kSparkLit], 3, 0, whole);
        // The default 48 x 32 chart half way, and stale: to x 24.
        AccountData stale = Fresh();
        stale.win5h = Window(45, 9000000);
        stale.stale = true;
        AccountMeters grey = AccountMeterState(Display(), BarLayout::Sparkline,
                                               Account(L"anthropic", L"A", {true, false, false, false}), stale, 0, 0);
        path(grey.quotas[0].spark[kSparkLit], 3, 0, {{24, 0}, {24, 32}, {3, 32}, {0, 29, 3}, {0, 3}, {3, 0, 3}});
    });

    Run("sparkline: the plot's outline half a DIP in, clockwise from its top edge; off with its toggle", [] {
        // The 1 DIP stroke's centre half a DIP in from the plot's edges, its corners 2.5 DIPs round
        // (the plot's 3 less half the stroke). On the 48 x 32 chart: from (3, 0.5) along the top to
        // x 45, around to the right side at y 3, down to 29, around to the bottom at x 45, along to 3,
        // around to the left side at y 29, up to 3 and around to the start.
        const std::vector<Seg> standard = {{45, 0.5}, {47.5, 3, 2.5}, {47.5, 29}, {45, 31.5, 2.5},
                                           {3, 31.5}, {0.5, 29, 2.5}, {0.5, 3}, {3, 0.5, 2.5}};
        path(First(Window(45, 9000000), Display(), BarLayout::Sparkline).quotas[0].spark[kSparkOutline], 3, 0.5, standard);
        // The 100 x 24 plot: the right side at x 99.5, the bottom at y 23.5.
        const std::vector<Seg> wide = {{97, 0.5}, {99.5, 3, 2.5}, {99.5, 21}, {97, 23.5, 2.5},
                                       {3, 23.5}, {0.5, 21, 2.5}, {0.5, 3}, {3, 0.5, 2.5}};
        path(spark(Window(45, 9000000)).quotas[0].spark[kSparkOutline], 3, 0.5, wide);
        // A 24 x 12 chart (50% of a 32 DIP taskbar's tile): x 23.5, y 11.5.
        MeterDisplay small = Display(With([](Settings& s) {
            s.sparklineWidth = 24;
            s.sparklineHeight = 50;
        }), 1, 32);
        path(First(Window(45, 9000000), small, BarLayout::Sparkline).quotas[0].spark[kSparkOutline], 3, 0.5,
             {{21, 0.5}, {23.5, 3, 2.5}, {23.5, 9}, {21, 11.5, 2.5}, {3, 11.5}, {0.5, 9, 2.5}, {0.5, 3}, {3, 0.5, 2.5}});
        // Whatever the chart shows: without a window, before the first reading, the cues off.
        path(spark({45, 0, 0}).quotas[0].spark[kSparkOutline], 3, 0.5, wide);
        AccountData before = Fresh();
        before.lastSuccessMs = 0;
        AccountMeters waiting = AccountMeterState(plot(), BarLayout::Sparkline,
                                                  Account(L"anthropic", L"A", {true, false, false, false}), before, 0, 0);
        path(waiting.quotas[0].spark[kSparkOutline], 3, 0.5, wide);
        MeterDisplay noCues = plot([](Settings& s) { s.sparklineElapsed = s.sparklineNowLine = false; });
        path(spark(Window(45, 9000000), {}, noCues).quotas[0].spark[kSparkOutline], 3, 0.5, wide);
        // Each chart its own, from its own corner.
        AccountData both = Fresh();
        both.win5h = Window(30, 9000000);
        both.winWeek = {50, kNow + kMinute, kWeeklyWindowMs};
        AccountMeters two = AccountMeterState(Display(), BarLayout::Sparkline, Account(L"anthropic", L"A", {true, true, false, false}),
                                              both, 0, 0);
        path(two.quotas[0].spark[kSparkOutline], 3, 0.5, standard);
        path(two.quotas[1].spark[kSparkOutline], 3, 0.5, standard);
        // The toggle off: none, on any chart; the other shapes stay.
        MeterDisplay off = Display(With([](Settings& s) { s.sparklineOutline = false; }));
        CHECK(Display().sparklineOutline && !off.sparklineOutline);
        AccountMeters plain = AccountMeterState(off, BarLayout::Sparkline, Account(L"anthropic", L"A", {true, true, false, false}),
                                                both, 0, 0);
        CHECK(plain.quotas[0].spark[kSparkOutline].empty() && plain.quotas[1].spark[kSparkOutline].empty());
        CHECK(!plain.quotas[0].spark[kSparkLit].empty() && !plain.quotas[0].spark[kSparkNow].empty());
        CHECK(spark({45, 0, 0}, {}, plot([](Settings& s) { s.sparklineOutline = false; })).quotas[0].spark[kSparkOutline].empty());
    });

    Run("sparkline: the current time's line across the plot, inside its edges, and its caret on the bottom edge", [] {
        // The caret on (off by default).
        const MeterDisplay shown = plot([](Settings& s) { s.sparklineNowCaret = true; });
        // Half way: 1 DIP wide around x 50, from 1 DIP below the top to 1 above the bottom; the caret
        // 4.8 DIPs wide on the bottom edge, its tip 3.2 DIPs up.
        AccountMeters half = spark(Window(45, 9000000), {}, shown);
        path(half.quotas[0].spark[kSparkNowLine], 49.5, 1, {{50.5, 1}, {50.5, 23}, {49.5, 23}});
        path(half.quotas[0].spark[kSparkCaret], 47.6, 24, {{50, 20.8}, {52.4, 24}});
        // Before the window starts (x 0) and a quarter DIP in: the line kept inside the left edge;
        // the caret around x itself.
        AccountMeters before = spark(Window(45, kFiveHours + 3600000), {}, shown);
        path(before.quotas[0].spark[kSparkNowLine], 0, 1, {{1, 1}, {1, 23}, {0, 23}});
        path(before.quotas[0].spark[kSparkCaret], -2.4, 24, {{0, 20.8}, {2.4, 24}});
        AccountMeters early = spark(Window(45, 17955000), {}, shown);
        path(early.quotas[0].spark[kSparkNowLine], 0, 1, {{1, 1}, {1, 23}, {0, 23}});
        path(early.quotas[0].spark[kSparkCaret], -2.15, 24, {{0.25, 20.8}, {2.65, 24}});
        // Past the reset (x 100): inside the right edge, the line clear of the key (x 87.4 to 97).
        AccountMeters after = spark(Window(45, -(long long)kMinute), {}, shown);
        path(after.quotas[0].spark[kSparkNowLine], 99, 1, {{100, 1}, {100, 23}, {99, 23}});
        path(after.quotas[0].spark[kSparkCaret], 97.6, 24, {{100, 20.8}, {102.4, 24}});
        // The same place in remaining mode; on the default 48 x 32 chart from y 1 to 31.
        AccountMeters left = spark(Window(45, 9000000), {}, plot([](Settings& s) {
                                       s.sparklineNowCaret = true;
                                       s.barMode = BarMode::Remaining;
                                   }));
        path(left.quotas[0].spark[kSparkNowLine], 49.5, 1, {{50.5, 1}, {50.5, 23}, {49.5, 23}});
        AccountMeters standard =
            First(Window(45, 9000000), Display(With([](Settings& s) { s.sparklineNowCaret = true; })), BarLayout::Sparkline);
        path(standard.quotas[0].spark[kSparkNowLine], 23.5, 1, {{24.5, 1}, {24.5, 31}, {23.5, 31}});
        path(standard.quotas[0].spark[kSparkCaret], 21.6, 32, {{24, 28.8}, {26.4, 32}});
        // Without a window: neither.
        AccountMeters alone = spark({45, 0, 0}, {}, shown);
        CHECK(alone.quotas[0].spark[kSparkNowLine].empty() && alone.quotas[0].spark[kSparkCaret].empty());

        // Each on its own toggle: by default the line without the caret; the line off with the caret
        // on, the caret alone; both off, neither. The reading and the lit part stay.
        AccountMeters plain = spark(Window(45, 9000000));
        path(plain.quotas[0].spark[kSparkNowLine], 49.5, 1, {{50.5, 1}, {50.5, 23}, {49.5, 23}});
        CHECK(plain.quotas[0].spark[kSparkCaret].empty());
        AccountMeters caretOnly = spark(Window(45, 9000000), {}, plot([](Settings& s) {
                                            s.sparklineNowLine = false;
                                            s.sparklineNowCaret = true;
                                        }));
        CHECK(caretOnly.quotas[0].spark[kSparkNowLine].empty());
        path(caretOnly.quotas[0].spark[kSparkCaret], 47.6, 24, {{50, 20.8}, {52.4, 24}});
        AccountMeters neither = spark(Window(45, 9000000), {}, plot([](Settings& s) { s.sparklineNowLine = false; }));
        CHECK(neither.quotas[0].spark[kSparkNowLine].empty() && neither.quotas[0].spark[kSparkCaret].empty());
        dot(neither.quotas[0], 50, 13);
        CHECK(!neither.quotas[0].spark[kSparkLit].empty());
    });

    Run("sparkline: the now line stops a DIP short of the text it crosses; the caret yields to text at the bottom", [] {
        // Line pieces, top first: each 1 DIP wide from `left` to `right`, from y `from` to `to`.
        auto pieces = [](const TileOutline& outline, double left, double right,
                         const std::vector<std::pair<double, double>>& spans) {
            CHECK(outline.size() == spans.size());
            for (size_t k = 0; k < spans.size(); k++) {
                auto [from, to] = spans[k];
                path({outline[k]}, left, from, {{right, from}, {right, to}, {left, to}});
            }
        };
        // With the ring and the caret on.
        const MeterDisplay ringed = Display(With(cues));
        auto first = [&](double pct, double share) {
            return spark(Window(pct, std::llround((1 - share) * kFiveHours)), {}, ringed).quotas[0];
        };
        // On the 48 x 32 chart (8 DIP font, 5.6 cap height) the key from x 35.4 to 45. The line runs
        // from 1 to 31; text on an edge it crosses takes it from that edge to a DIP past the text: at
        // the top (y 3 to 8.6) down to 9.6, at the bottom (23.4 to 29) up from 22.4.
        // Clear of the text, half way (x 24): one piece; the caret.
        QuotaMeter clear = first(45, 0.5);
        pieces(clear.spark[kSparkNowLine], 23.5, 24.5, {{1, 31}});
        CHECK(clear.spark[kSparkCaret].size() == 1);
        // Through the key at the bottom: 90% at 80% of the window, (38.5, 4.75), its ring clear of the
        // key: from 1 to 22.4; the caret, under the key, dropped.
        QuotaMeter under = first(90, 0.8);
        CHECK(!under.sparkKeyTop);
        pieces(under.spark[kSparkNowLine], 38, 39, {{1, 22.4}});
        CHECK(under.spark[kSparkCaret].empty());
        // Through the key moved to the top: 10% at 80%, (38.5, 27.25): from 9.6 to 31; the caret kept.
        QuotaMeter over = first(10, 0.8);
        CHECK(over.sparkKeyTop);
        pieces(over.spark[kSparkNowLine], 38, 39, {{9.6, 31}});
        path(over.spark[kSparkCaret], 36.1, 32, {{38.5, 28.8}, {40.9, 32}});
        // The caret beside the key at the bottom: 90% at x 33, its right corner on the key's left
        // edge (35.4): kept; a quarter DIP later, at 33.25, under it: dropped. The line clear of it.
        QuotaMeter beside = first(90, 0.6875);
        CHECK(!beside.sparkKeyTop && beside.spark[kSparkCaret].size() == 1);
        pieces(beside.spark[kSparkNowLine], 32.5, 33.5, {{1, 31}});
        QuotaMeter past = first(90, 0.6927);
        CHECK(!past.sparkKeyTop && past.spark[kSparkCaret].empty());
        pieces(past.spark[kSparkNowLine], 32.75, 33.75, {{1, 31}});

        // The weekly chart alone, its corner per `data`, at `pct` `share` of the way through the week.
        auto weekly = [&](AccountData data, std::wstring provider, double pct, double share,
                          const MeterDisplay* d = nullptr) {
            data.winWeek = {pct, kNow + (ULONGLONG)std::llround((1 - share) * kWeeklyWindowMs), kWeeklyWindowMs};
            return AccountMeterState(d ? *d : ringed, BarLayout::Sparkline, Account(provider, L"A", {false, true, false, true}),
                                     data, 0, 0);
        };
        AccountData dollars = Fresh();
        dollars.extraUsage = {40, kNow + 10 * 24 * 60 * kMinute, 30ULL * 24 * 60 * kMinute};
        dollars.extraUsedAmount = 20;
        dollars.extraLimitAmount = 50;
        // Text on both edges: "Cr" and a 12345 credit balance ("12.3k") at the top, x 3 to 38.6, over
        // the key at the bottom; 50% at 75% of the week, (36, 16), clear of both: between them, from
        // 9.6 to 22.4; the caret, under the key, dropped.
        AccountData balance = Fresh();
        balance.creditsBalance = 12345;
        AccountMeters both = weekly(balance, L"openai", 50, 0.75);
        CHECK(both.sparkExtraText == L"12.3k" && !both.sparkExtraBottom && !both.quotas[1].sparkKeyTop);
        pieces(both.quotas[1].spark[kSparkNowLine], 35.5, 36.5, {{9.6, 22.4}});
        CHECK(both.quotas[1].spark[kSparkCaret].empty());
        // "$20.00" (x 3 to 31.8) at the top, half way at 40%: the line below it, the caret kept.
        AccountMeters top = weekly(dollars, L"anthropic", 40, 0.5);
        CHECK(!top.sparkExtraBottom);
        pieces(top.quotas[1].spark[kSparkNowLine], 23.5, 24.5, {{9.6, 31}});
        CHECK(top.quotas[1].spark[kSparkCaret].size() == 1);
        // Moved to the bottom by 110% half way, (24, 2): the line above it, the caret dropped.
        AccountMeters bottom = weekly(dollars, L"anthropic", 110, 0.5);
        CHECK(bottom.sparkExtraBottom);
        pieces(bottom.quotas[1].spark[kSparkNowLine], 23.5, 24.5, {{1, 22.4}});
        CHECK(bottom.quotas[1].spark[kSparkCaret].empty());
        // A 24 x 12 chart (7 DIP text, 4.9 cap height; the line from 1 to 11): "$20.00" at the top (x 3
        // to 28.2, y 3 to 7.9) over the key at the bottom (x 12.6 to 21, y 4.1 to 9); 50% at 62.5% of
        // the week, (15, 6), hitting both wherever they go. The top text takes the line down to 8.9,
        // the bottom text up from 3.1: no line left; the caret dropped.
        const MeterDisplay tiny = Display(With([](Settings& s) {
            cues(s);
            s.sparklineWidth = 24;
            s.sparklineHeight = 50;
            s.sparklineTextSize = 7;
        }), 1, 32);
        AccountMeters cramped = weekly(dollars, L"anthropic", 50, 0.625, &tiny);
        CHECK(!cramped.sparkExtraBottom && !cramped.quotas[1].sparkKeyTop);
        CHECK(cramped.quotas[1].spark[kSparkNowLine].empty());
        CHECK(cramped.quotas[1].spark[kSparkCaret].empty());

        // Remaining mode's text corners: the key at the top takes the line down from 9.6 and leaves the
        // caret (90% used at 80%, (38.5, 27.25), clear of it); "$30.00" left at the bottom (40% used
        // half way, (24, 13.25)) takes it up from 22.4 and drops the caret.
        const MeterDisplay falling = Display(With([](Settings& s) {
            cues(s);
            s.barMode = BarMode::Remaining;
        }));
        QuotaMeter top5h = spark(Window(90, std::llround(0.2 * kFiveHours)), {}, falling).quotas[0];
        CHECK(top5h.sparkKeyTop);
        pieces(top5h.spark[kSparkNowLine], 38, 39, {{9.6, 31}});
        path(top5h.spark[kSparkCaret], 36.1, 32, {{38.5, 28.8}, {40.9, 32}});
        AccountMeters leftDollars = weekly(dollars, L"anthropic", 40, 0.5, &falling);
        CHECK(leftDollars.sparkExtraText == L"$30.00" && leftDollars.sparkExtraBottom && leftDollars.quotas[1].sparkKeyTop);
        pieces(leftDollars.quotas[1].spark[kSparkNowLine], 23.5, 24.5, {{1, 22.4}});
        CHECK(leftDollars.quotas[1].spark[kSparkCaret].empty());
    });

    Run("sparkline: the projection at the window's average rate, to the reset or to the limit", [] {
        // Under pace: 45% half way reaches 90% at the reset.
        AccountMeters under = spark(Window(45, 9000000));
        line(under.quotas[0].spark[kSparkProjection], 50, 13, 100, 4);
        CHECK(under.quotas[0].spark[kSparkLimit].empty());
        // Even use reaches the limit just at the reset: no mark.
        AccountMeters even = spark(Window(50, 9000000));
        line(even.quotas[0].spark[kSparkProjection], 50, 12, 100, 2);
        CHECK(even.quotas[0].spark[kSparkLimit].empty());
        // Over pace: 80% half way reaches 100% at 62.5% of the window, marked there in the 100%
        // color (red), whatever the quota's own (orange).
        AccountMeters over = spark(Window(80, 9000000));
        line(over.quotas[0].spark[kSparkProjection], 50, 6, 62.5, 2);
        circle(over.quotas[0].spark[kSparkLimit], 62.5, 2, 1.75);
        CHECK(over.sparkLimitColor == 0xFFE53935 && over.quotas[0].color == 0xFFFB8C00);
        // 60% half way reaches the limit at 83.33, drawn at 83.25.
        line(spark(Window(60, 9000000)).quotas[0].spark[kSparkProjection], 50, 10, 83.25, 2);
        CHECK(spark(Window(80, 9000000), {}, plot([](Settings& s) { s.colorblindMode = true; })).sparkLimitColor == 0xFFD55E00);
        // From 5% of the window on: 1% there reaches 20%.
        line(spark(Window(1, 17100000)).quotas[0].spark[kSparkProjection], 5, 21.75, 100, 18);
        // None before 5% of the window, at 0% or from 100%, at the reset, or before the window
        // starts; the reading stays.
        for (WindowUsage r : {Window(45, 17280000), Window(0, 9000000), Window(100, 9000000), Window(110, 9000000),
                              Window(45, 0), Window(45, kFiveHours + 1000)}) {
            AccountMeters m = spark(r);
            CHECK(m.quotas[0].spark[kSparkProjection].empty() && m.quotas[0].spark[kSparkLimit].empty() && !unmarked(m.quotas[0]));
        }
        // Stale: none either; the quota greys, the limit color stays.
        AccountData stale = Fresh();
        stale.win5h = Window(80, 9000000);
        stale.stale = true;
        AccountMeters grey = AccountMeterState(plot(), BarLayout::Sparkline,
                                               Account(L"anthropic", L"A", {true, false, false, false}), stale, 0, 0);
        CHECK(grey.quotas[0].spark[kSparkProjection].empty() && grey.quotas[0].spark[kSparkLimit].empty() &&
              !unmarked(grey.quotas[0]));
        CHECK(grey.quotas[0].color == 0xFF9E9E9E && grey.sparkLimitColor == 0xFFE53935);
    });

    Run("sparkline: remaining mode plots what is left, its runs over areas from the bottom, down to the limit", [] {
        // 100% left at the top inset, 0% at the bottom one: y = 2 + used / 5 on the 100 x 24 plot.
        const MeterDisplay left = plot([](Settings& s) { s.barMode = BarMode::Remaining; });
        // A run (90% and 70% left, then none past 100% used) and a lone reading (55% left); the area
        // still rises from the plot's bottom under the run.
        AccountMeters m = spark(Window(45, 9000000), {{at(0.1), 10, false}, {at(0.2), 30, true}, {at(0.4), 120, true},
                                                      {at(0.45), 45, false}}, left);
        const auto& shapes = m.quotas[0].spark;
        CHECK(shapes[kSparkLines].size() == 1 && shapes[kSparkArea].size() == 1);
        const TileOutlineFigure& run = shapes[kSparkLines][0];
        CHECK(run.segments.size() == 2);
        point(run, 10, 4); point(run.segments[0], 20, 8); point(run.segments[1], 40, 22);
        const TileOutlineFigure& under = shapes[kSparkArea][0];
        CHECK(under.segments.size() == 4);
        point(under, 10, 24); point(under.segments[0], 10, 4); point(under.segments[1], 20, 8);
        point(under.segments[2], 40, 22); point(under.segments[3], 40, 24);
        circle(shapes[kSparkDots], 45, 11, 1.5);
        dot(m.quotas[0], 50, 11);
        // The projection at the average rate: 45% used half way leaves 10% at the reset, no mark.
        line(shapes[kSparkProjection], 50, 11, 100, 20);
        CHECK(shapes[kSparkLimit].empty());
        // Over pace, 80% used half way runs out at 62.5% of the window: marked on the bottom edge in
        // the 100% color.
        AccountMeters over = spark(Window(80, 9000000), {}, left);
        line(over.quotas[0].spark[kSparkProjection], 50, 18, 62.5, 22);
        circle(over.quotas[0].spark[kSparkLimit], 62.5, 22, 1.75);
        CHECK(over.sparkLimitColor == 0xFFE53935 && over.quotas[0].color == 0xFFFB8C00);
        // The lit part keeps to the time: to x 50 half way.
        path(shapes[kSparkLit], 3, 0, {{50, 0}, {50, 24}, {3, 24}, {0, 21, 3}, {0, 3}, {3, 0, 3}});
    });

    Run("sparkline: extra usage in the weekly chart's top-left corner instead of a chart of its own", [] {
        const ULONGLONG kMonth = 30ULL * 24 * 60 * kMinute;
        // An Anthropic account's 5-hour and weekly quotas and its monthly extra usage, each with a
        // window: `used` of `limit` dollars spent (no limit: unlimited, no percentage).
        auto anthropic = [&](double used, double limit) {
            AccountData data = Fresh();
            data.win5h = Window(30, 9000000);
            data.winWeek = {40, kNow + kMinute, kWeeklyWindowMs};
            data.extraUsage = {limit > 0 ? used * 100 / limit : -1, kNow + 10 * 24 * 60 * kMinute, kMonth};
            data.extraUsedAmount = used;
            data.extraLimitAmount = limit;
            return data;
        };
        auto corner = [](const AccountData& data, SparklineExtra extra = SparklineExtra::Amount, BarMode mode = BarMode::Used,
                         std::wstring provider = L"anthropic", std::array<bool, kQuotaBarCount> bars = {true, true, false, true}) {
            MeterDisplay d = Display(With([&](Settings& s) { s.sparklineExtra = extra; s.barMode = mode; }));
            return AccountMeterState(d, BarLayout::Sparkline, Account(provider, L"A", bars), data, 0, 0);
        };
        auto shows = [](const AccountMeters& m, std::wstring_view key, std::wstring_view text, uint32_t color) {
            CHECK(m.sparkExtraKey == key && m.sparkExtraText == text && m.sparkExtraColor == color);
        };
        using enum SparklineExtra;
        constexpr BarMode kUsed = BarMode::Used, kLeft = BarMode::Remaining;
        constexpr uint32_t green = 0xFF43A047, yellow = 0xFFFDD835, orange = 0xFFFB8C00, red = 0xFFE53935,
                           grey = 0xFF9E9E9E;

        // Amount, the default: $20 of $50 spent, without a key, in extra usage's usage color (40%:
        // green); extra usage gets no chart, the other two 100 DIPs.
        AccountMeters spent = corner(anthropic(20, 50));
        shows(spent, L"", L"$20.00", green);
        CHECK(spent.barMask == 0b0011 && bare(spent.quotas[3]));
        CHECK_NEAR(spent.faceWidth, 100);
        // Remaining mode: what's left of the limit; past it, none.
        shows(corner(anthropic(20, 50), Amount, kLeft), L"", L"$30.00", green);
        shows(corner(anthropic(60, 50)), L"", L"$60.00", red);
        shows(corner(anthropic(60, 50), Amount, kLeft), L"", L"$0.00", red);
        // Unlimited: the amount spent in either mode, in the taskbar's text color (0).
        shows(corner(anthropic(12.5, -1)), L"", L"$12.50", 0);
        shows(corner(anthropic(12.5, -1), Amount, kLeft), L"", L"$12.50", 0);
        // Percent: of the limit, keyed "Ex"; in remaining mode the share left, at least 0%.
        shows(corner(anthropic(20, 50), Percent), L"Ex", L"40%", green);
        shows(corner(anthropic(20, 50), Percent, kLeft), L"Ex", L"60%", green);
        shows(corner(anthropic(60, 50), Percent), L"Ex", L"120%", red);
        shows(corner(anthropic(60, 50), Percent, kLeft), L"Ex", L"0%", red);
        // Without a limit Percent shows the amount.
        shows(corner(anthropic(12.5, -1), Percent), L"", L"$12.50", 0);
        shows(corner(anthropic(12.5, -1), Percent, kLeft), L"", L"$12.50", 0);

        // OpenAI credits against a 1000 credit max with 250 left (75% used: orange), keyed "Cr":
        // 750 spent, 250 left; 75%, 25%. Credits have no window: never a chart of their own.
        AccountData credits = Fresh();
        credits.win5h = Window(30, 9000000);
        credits.winWeek = {40, kNow + kMinute, kWeeklyWindowMs};
        credits.creditsBalance = 250;
        credits.extraUsage.pct = 75;
        credits.extraUsedAmount = 750;
        credits.extraLimitAmount = 1000;
        AccountMeters used = corner(credits, Amount, kUsed, L"openai");
        shows(used, L"Cr", L"750", orange);
        CHECK(used.barMask == 0b0011);
        shows(corner(credits, Amount, kLeft, L"openai"), L"Cr", L"250", orange);
        shows(corner(credits, Percent, kUsed, L"openai"), L"Cr", L"75%", orange);
        shows(corner(credits, Percent, kLeft, L"openai"), L"Cr", L"25%", orange);
        // A balance without a max: the balance, in either mode and for Percent, in the text color.
        AccountData balance = credits;
        balance.creditsBalance = 420;
        balance.extraUsage.pct = balance.extraUsedAmount = balance.extraLimitAmount = -1;
        for (SparklineExtra extra : {Amount, Percent}) {
            for (BarMode mode : {kUsed, kLeft}) shows(corner(balance, extra, mode, L"openai"), L"Cr", L"420", 0);
        }
        // A percentage with neither amounts nor a balance: shown as such in Amount mode too, keyed,
        // in either bar mode.
        AccountData share = anthropic(-1, -1);
        share.extraUsage.pct = 40;
        shows(corner(share), L"Ex", L"40%", green);
        shows(corner(share, Amount, kLeft), L"Ex", L"60%", green);
        shows(corner(share, Percent), L"Ex", L"40%", green);
        AccountData creditShare = balance;
        creditShare.extraUsage.pct = 75;
        creditShare.creditsBalance = -1;
        shows(corner(creditShare, Amount, kUsed, L"openai"), L"Cr", L"75%", orange);
        // With a balance (no amounts) Amount shows the balance, Percent the percentage.
        creditShare.creditsBalance = 250;
        shows(corner(creditShare, Amount, kUsed, L"openai"), L"Cr", L"250", orange);
        shows(corner(creditShare, Percent, kUsed, L"openai"), L"Cr", L"75%", orange);
        // A balance counts for OpenAI only.
        AccountData stray = anthropic(-1, -1);
        stray.creditsBalance = 420;
        AccountMeters unreported = corner(stray);
        shows(unreported, L"", L"", 0);
        CHECK(unreported.barMask == 0b0011);
        // Stale: grey, with a percentage or without.
        AccountData stale = anthropic(20, 50);
        stale.stale = true;
        shows(corner(stale), L"", L"$20.00", grey);
        stale = anthropic(12.5, -1);
        stale.stale = true;
        shows(corner(stale), L"", L"$12.50", grey);

        // Off: no corner; extra usage charted third with its window, 104 DIPs in.
        AccountMeters off = corner(anthropic(20, 50), Off);
        shows(off, L"", L"", 0);
        CHECK(off.barMask == 0b1011 && !bare(off.quotas[3]));
        CHECK_NEAR(off.faceWidth, 152); CHECK_NEAR(off.quotas[3].sparkLeft, 104);
        // The weekly quota not charted (hidden, or without a window): no corner; extra usage charted
        // second.
        for (AccountMeters m : {corner(anthropic(20, 50), Amount, kUsed, L"anthropic", {true, false, false, true}),
                                corner([&] {
                                    AccountData flat = anthropic(20, 50);
                                    flat.winWeek = {40, 0, 0};
                                    return flat;
                                }())}) {
            shows(m, L"", L"", 0);
            CHECK(m.barMask == 0b1001);
            CHECK_NEAR(m.quotas[3].sparkLeft, 52);
        }
        // Nor as the most-used quota's chart without any window.
        AccountData flat = Fresh({30, 70, -1, 40});
        flat.extraUsedAmount = 20;
        flat.extraLimitAmount = 50;
        AccountMeters fallback = corner(flat);
        shows(fallback, L"", L"", 0);
        CHECK(fallback.barMask == 0b0010);
        // Extra usage or credits not selected for the account: no corner, and no chart.
        AccountMeters unselected = corner(anthropic(20, 50), Amount, kUsed, L"anthropic", {true, true, false, false});
        shows(unselected, L"", L"", 0);
        CHECK(unselected.barMask == 0b0011);
        shows(corner(credits, Amount, kUsed, L"openai", {true, true, false, false}), L"", L"", 0);
        shows(corner(balance, Amount, kUsed, L"openai", {true, true, false, false}), L"", L"", 0);
        // Amounts from 1,000 on: three significant digits with k or M. 110616 of a 250000 credit max
        // left (55.8% used: yellow): 139384 spent, 111k left; a 62500 balance without a max; $1500
        // spent past a $1200 limit.
        AccountData large = credits;
        large.creditsBalance = 110616;
        large.extraUsage.pct = 55.7536;
        large.extraUsedAmount = 139384;
        large.extraLimitAmount = 250000;
        shows(corner(large, Amount, kUsed, L"openai"), L"Cr", L"139k", yellow);
        shows(corner(large, Amount, kLeft, L"openai"), L"Cr", L"111k", yellow);
        AccountData largeBalance = balance;
        largeBalance.creditsBalance = 62500;
        shows(corner(largeBalance, Amount, kUsed, L"openai"), L"Cr", L"62.5k", 0);
        shows(corner(anthropic(1500, 1200)), L"", L"$1.5k", red);
        // The setting clamps at the display entry: Amount by default.
        CHECK(Display().sparklineExtra == Amount);
        CHECK(Display(With([](Settings& s) { s.sparklineExtra = SparklineExtra(7); })).sparklineExtra == Percent);
        CHECK(Display(With([](Settings& s) { s.sparklineExtra = SparklineExtra(-3); })).sparklineExtra == Off);
    });

    Run("sparkline: the key and the extra usage move to the plot's other edge, clear of the current reading", [] {
        // Text boxes 3 DIPs in from the plot's edges, a cap height (0.7 of the key's font) tall and
        // 0.6 of the font wide per character (plus 2 DIPs after the extra usage's key); the
        // reading's box its ring's, 4.1 DIPs around it (the ring on, as below unless noted; a plain
        // dot's 2 DIPs). On the 48 x 32 chart (8 DIP text, 5.6 cap height): the 2-letter key from x
        // 35.4 to 45, at the bottom from y 23.4 to 29, at the top from 3 to 8.6.
        const MeterDisplay ringed = Display(With(cues));
        auto chart = [&](double pct, double share, const MeterDisplay* d = nullptr) {
            return spark(Window(pct, std::llround((1 - share) * kFiveHours)), {}, d ? *d : ringed).quotas[0];
        };
        // 10% at 80% of the window, (38.5, 27.25): in the bottom box, clear of the top: moved.
        CHECK(chart(10, 0.8).sparkKeyTop);
        // Clear of the bottom box: half way (24, 27.25), or at 90% (38.5, 4.75): kept.
        CHECK(!chart(10, 0.5).sparkKeyTop);
        CHECK(!chart(90, 0.8).sparkKeyTop);
        // Without a window, 10% at (43.9, 27.25): moved.
        CHECK(spark({10, 0, 0}, {}, ringed).quotas[0].sparkKeyTop);
        // A 12 DIP chart (50% of a 32 DIP taskbar's tile; 7 DIP text, 4.9 cap height): both boxes,
        // y 4.1 to 9 and 3 to 7.9, under the ring at (38.5, 9.25): kept.
        const MeterDisplay low = Display(With([](Settings& s) {
            cues(s);
            s.sparklineHeight = 50;
            s.sparklineTextSize = 7;
        }), 1, 32);
        CHECK(!chart(10, 0.8, &low).sparkKeyTop);
        // Touching counts as clear: on a 24 x 28 chart (7 DIP text) the key from x 12.6 to 21, at the
        // bottom from y 20.1 to 25; 17% at (8.5, 22) has its ring end at x 12.6: kept; a quarter DIP
        // later, at 8.75: moved.
        const MeterDisplay narrow = Display(With([](Settings& s) {
            cues(s);
            s.sparklineWidth = 24;
            s.sparklineHeight = 70;
            s.sparklineTextSize = 7;
        }));
        CHECK(!chart(17, 0.355, &narrow).sparkKeyTop);
        CHECK(chart(17, 0.365, &narrow).sparkKeyTop);
        // No reading (a window before the first success): kept.
        AccountData before = Fresh();
        before.lastSuccessMs = 0;
        before.win5h = {-1, kNow + 3600000, kFiveHours};
        AccountMeters waiting = AccountMeterState(ringed, BarLayout::Sparkline,
                                                  Account(L"anthropic", L"A", {true, false, false, false}), before, 0, 0);
        CHECK(waiting.barMask == 0b0001 && unmarked(waiting.quotas[0]) && !waiting.quotas[0].sparkKeyTop);

        // The weekly chart places its key and the extra usage together, trying in order: the key at
        // the bottom and the extra usage at the top, the key moved, the extra usage moved, both moved;
        // skipping placements with both on one edge where their boxes overlap across, and keeping the
        // first the ring overlaps the fewest boxes in. With $20 of $50 extra usage: "$20.00" (6
        // characters) from x 3 to 31.8, at the top from y 3 to 8.6, at the bottom from 23.4 to 29.
        auto weekly = [](double pct, double share, SparklineExtra extra = SparklineExtra::Amount,
                         const MeterDisplay* d = nullptr) {
            AccountData data = Fresh();
            data.win5h = Window(110, 9000000);
            data.winWeek = {pct, kNow + (ULONGLONG)std::llround((1 - share) * kWeeklyWindowMs), kWeeklyWindowMs};
            data.extraUsage = {40, kNow + 10 * 24 * 60 * kMinute, 30ULL * 24 * 60 * kMinute};
            data.extraUsedAmount = 20;
            data.extraLimitAmount = 50;
            MeterDisplay display = d ? *d : Display(With(cues));
            display.sparklineExtra = extra;
            return AccountMeterState(display, BarLayout::Sparkline, Account(L"anthropic", L"A", {true, true, false, true}),
                                     data, 0, 0);
        };
        // 110% half way, (24, 2): only the extra usage's top box hit; it moves, the key stays. The
        // 5-hour chart's 110% reading at its top-left moves nothing here.
        AccountMeters high = weekly(110, 0.5);
        CHECK(high.sparkExtraText == L"$20.00" && high.sparkExtraBottom && !high.quotas[1].sparkKeyTop);
        // 40% half way, (24, 18.75): no box hit: nothing moves.
        AccountMeters middle = weekly(40, 0.5);
        CHECK(!middle.sparkExtraBottom && !middle.quotas[1].sparkKeyTop);
        // 10% at 80%, (38.5, 27.25): only the key's bottom box hit; the key moves, and the extra
        // usage stays at the top (moving both is as clear, no improvement).
        AccountMeters late = weekly(10, 0.8);
        CHECK(late.quotas[1].sparkKeyTop && !late.sparkExtraBottom);
        // The box's width per its text: at (33.25, 2) the ring starts at x 29.15, inside "$20.00"
        // (moved) but past "Ex" and "40%" (5 characters and the 2 DIP gap: x 3 to 29; kept); at
        // (31.5, 2), from 27.4, inside that too (moved).
        CHECK(weekly(110, 0.6925).sparkExtraBottom);
        AccountMeters keyed = weekly(110, 0.6925, SparklineExtra::Percent);
        CHECK(keyed.sparkExtraKey == L"Ex" && keyed.sparkExtraText == L"40%" && !keyed.sparkExtraBottom);
        CHECK(weekly(110, 0.656, SparklineExtra::Percent).sparkExtraBottom);
        // The 12 DIP chart (key from x 36.6 to 45, "$20.00" from 3 to 28.2): the ring at (24, 2) hits
        // the extra usage on either edge and the key on neither; one hit in every placement, the first
        // kept.
        AccountMeters crowded = weekly(110, 0.5, SparklineExtra::Amount, &low);
        CHECK(!crowded.sparkExtraBottom && !crowded.quotas[1].sparkKeyTop);

        // The weekly chart alone, its corner per `data`, on `d`.
        auto placed = [&](const AccountData& data, std::wstring provider, const MeterDisplay* d = nullptr) {
            return AccountMeterState(d ? *d : ringed, BarLayout::Sparkline, Account(provider, L"A", {false, true, false, true}),
                                     data, 0, 0);
        };
        auto weekReading = [](double pct, double share) {
            return WindowUsage{pct, kNow + (ULONGLONG)std::llround((1 - share) * kWeeklyWindowMs), kWeeklyWindowMs};
        };
        // A 12345 credit balance: "Cr" and "12.3k" (7 characters and the gap) from x 3 to 38.6, past
        // the key's 35.4: the two never share an edge.
        AccountData balance = Fresh();
        balance.creditsBalance = 12345;
        // 10% at 80%, (38.5, 27.25): the key's bottom box hit; moved alone it would sit on the extra
        // usage, and moving both puts the extra usage under the ring: one hit either way, neither
        // moves.
        balance.winWeek = weekReading(10, 0.8);
        AccountMeters stuck = placed(balance, L"openai");
        CHECK(stuck.barMask == 0b0010 && stuck.sparkExtraKey == L"Cr" && stuck.sparkExtraText == L"12.3k");
        CHECK(!stuck.sparkExtraBottom && !stuck.quotas[1].sparkKeyTop);
        // 110% a quarter of the way, (12, 2): the extra usage's top box hit; moved alone it would sit
        // on the key: both move, swapping edges, clear of the ring.
        balance.winWeek = weekReading(110, 0.25);
        AccountMeters swapped = placed(balance, L"openai");
        CHECK(swapped.sparkExtraBottom && swapped.quotas[1].sparkKeyTop);

        // 24 DIP charts: the key's box overlaps any corner text across, so they never share an edge.
        // OpenAI credits, 875 of a 1000 credit max left: "Cr" and "125" (5 characters and the gap:
        // x 3 to 26), on a 24 x 20 chart (50% of a 40 DIP tile; 7 DIP text, 4.9 cap height: the key
        // from x 12.6 to 21; boxes y 3 to 7.9 and 12.1 to 17). 62.5% half way, (12, 8): the ring hits
        // the extra usage at the top, and with both moved the key at the top (it ends at y 12.1,
        // touching the bottom boxes): one hit either way, neither moves.
        AccountData credits = Fresh();
        credits.winWeek = weekReading(62.5, 0.5);
        credits.creditsBalance = 875;
        credits.extraUsage.pct = 12.5;
        credits.extraUsedAmount = 125;
        credits.extraLimitAmount = 1000;
        const MeterDisplay short24 = Display(With([](Settings& s) {
            cues(s);
            s.sparklineWidth = 24;
            s.sparklineHeight = 50;
            s.sparklineTextSize = 7;
        }));
        AccountMeters oai = placed(credits, L"openai", &short24);
        CHECK(oai.sparkExtraKey == L"Cr" && oai.sparkExtraText == L"125");
        CHECK(!oai.sparkExtraBottom && !oai.quotas[1].sparkKeyTop);
        // $12.50 of $50 on a 24 x 40 chart (100%; 9 DIP text, 6.3 cap height: the key from x 10.2 to
        // 21, "$12.50" from 3 to 35.4; boxes y 3 to 9.3 and 30.7 to 37). 110% at 60% of the window,
        // (14.5, 2): the extra usage's top box hit, or with both moved the key's: neither moves.
        AccountData dollars = Fresh();
        dollars.winWeek = weekReading(110, 0.6);
        dollars.extraUsage = {25, kNow + 10 * 24 * 60 * kMinute, 30ULL * 24 * 60 * kMinute};
        dollars.extraUsedAmount = 12.5;
        dollars.extraLimitAmount = 50;
        const MeterDisplay tall24 = Display(With([](Settings& s) {
            cues(s);
            s.sparklineWidth = 24;
            s.sparklineHeight = 100;
            s.sparklineTextSize = 9;
        }));
        AccountMeters cc = placed(dollars, L"anthropic", &tall24);
        CHECK(cc.sparkExtraText == L"$12.50" && cc.sparkExtraKey.empty());
        CHECK(!cc.sparkExtraBottom && !cc.quotas[1].sparkKeyTop);
        // The same reading on the 24 x 20 chart ("$12.50" from 3 to 28.2): likewise.
        AccountMeters ccShort = placed(dollars, L"anthropic", &short24);
        CHECK(!ccShort.sparkExtraBottom && !ccShort.quotas[1].sparkKeyTop);
        // Without corner text the weekly key moves as any other: on the 24 x 20 chart 10% at 80% of the
        // way, (19.25, 16.5), hits the key's bottom box and clears its top one: moved.
        AccountData plain = Fresh();
        plain.winWeek = weekReading(10, 0.8);
        CHECK(placed(plain, L"anthropic", &short24).quotas[1].sparkKeyTop);

        // The ring off, the default: the dot's box, 2 DIPs around it. 35% at 80% of the window,
        // (38.5, 20.25): the ring reaches y 24.35, into the key's bottom box (moved); the dot only
        // 22.25 (kept). Without a window the dot at (46, 27.25), in that box: moved.
        const MeterDisplay plainDot = Display();
        CHECK(chart(35, 0.8).sparkKeyTop && !chart(35, 0.8, &plainDot).sparkKeyTop);
        CHECK(spark({10, 0, 0}, {}, plainDot).quotas[0].sparkKeyTop);
        // 110% at (34, 2): the ring from x 29.9, into "$20.00" (moved); the dot from 32, past its
        // 31.8 (kept).
        CHECK(weekly(110, 0.7083).sparkExtraBottom);
        AccountMeters plainHigh = weekly(110, 0.7083, SparklineExtra::Amount, &plainDot);
        CHECK(!plainHigh.sparkExtraBottom && !plainHigh.quotas[1].sparkKeyTop);
        // No reading (the weekly quota not read yet, before the first success): kept.
        AccountData unread = Fresh();
        unread.lastSuccessMs = 0;
        unread.winWeek = {-1, kNow + kWeeklyWindowMs / 2, kWeeklyWindowMs};
        unread.extraUsage = {40, kNow + 10 * 24 * 60 * kMinute, 30ULL * 24 * 60 * kMinute};
        unread.extraUsedAmount = 20;
        unread.extraLimitAmount = 50;
        AccountMeters blank = AccountMeterState(Display(), BarLayout::Sparkline,
                                                Account(L"anthropic", L"A", {false, true, false, true}), unread, 0, 0);
        CHECK(blank.barMask == 0b0010 && blank.sparkExtraText == L"$20.00" && unmarked(blank.quotas[1]) &&
              !blank.sparkExtraBottom && !blank.quotas[1].sparkKeyTop);

        // Remaining mode, whose line falls from the top-left: the key starts at the top-right and the
        // extra usage at the bottom-left, each moving to the other edge as above. Readings sit at
        // y = 2 + 0.28 x used on the 48 x 32 chart.
        const MeterDisplay falling = Display(With([](Settings& s) {
            cues(s);
            s.barMode = BarMode::Remaining;
        }));
        // 10% used at 80% of the window, (38.5, 4.75), on the key's top box: moved down. 90% there,
        // (38.5, 27.25), or 10% half way, (24, 4.75): kept at the top.
        CHECK(!chart(10, 0.8, &falling).sparkKeyTop);
        CHECK(chart(90, 0.8, &falling).sparkKeyTop);
        CHECK(chart(10, 0.5, &falling).sparkKeyTop);
        // The weekly chart, "$30.00" left: 40% used half way, (24, 13.25), hits neither: as placed. Past
        // 100% half way, (24, 30), on the extra usage: it moves to the top, the key stays. 10% at 80%,
        // (38.5, 4.75), on the key: it moves to the bottom, beside the extra usage.
        AccountMeters even = weekly(40, 0.5, SparklineExtra::Amount, &falling);
        CHECK(even.sparkExtraText == L"$30.00" && even.sparkExtraBottom && even.quotas[1].sparkKeyTop);
        AccountMeters spent = weekly(110, 0.5, SparklineExtra::Amount, &falling);
        CHECK(!spent.sparkExtraBottom && spent.quotas[1].sparkKeyTop);
        AccountMeters light = weekly(10, 0.8, SparklineExtra::Amount, &falling);
        CHECK(light.sparkExtraBottom && !light.quotas[1].sparkKeyTop);
        // No reading: as placed.
        MeterDisplay unreadLeft = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        AccountMeters blankLeft = AccountMeterState(unreadLeft, BarLayout::Sparkline,
                                                    Account(L"anthropic", L"A", {false, true, false, true}), unread, 0, 0);
        CHECK(unmarked(blankLeft.quotas[1]) && blankLeft.sparkExtraBottom && blankLeft.quotas[1].sparkKeyTop);
    });

    Run("sparkline: shape colors per taskbar theme, each chart in its quota's; strokes and drawing order", [] {
        // 80% half way (orange), over pace, with a run and a lone reading, the ring and the caret on:
        // every shape.
        const MeterDisplay d = plot(cues);
        AccountMeters m = spark(Window(80, 9000000), {{at(0.1), 10, false}, {at(0.2), 20, true}, {at(0.3), 30, false}}, d);
        for (int k = 0; k < kSparkShapeCount; k++) CHECK(!m.quotas[0].spark[k].empty());
        // Dark taskbar: the lit part, the outline, the now line, the caret and the ring white at their
        // alphas, the cut-out in the taskbar's color, the diagonal the pace color at half alpha; the
        // area (at 0x47), runs, dots and projection in the usage color; the dot 0.03 lighter in OKLCH
        // lightness, keeping its hue; the limit mark the 100% color. Light: the same in black, the
        // light pace color.
        const std::pair<int, uint32_t> dark[] = {
            {kSparkLit, 0x1FFFFFFF},     {kSparkOutline, 0x2EFFFFFF},     {kSparkPace, 0x80DEDEDE},
            {kSparkArea, 0x47FB8C00},
            {kSparkLines, 0xFFFB8C00},   {kSparkDots, 0xFFFB8C00},        {kSparkNowLine, 0x80FFFFFF},
            {kSparkProjection, 0xFFFB8C00}, {kSparkCaret, 0xF0FFFFFF},    {kSparkNowCut, 0xFF1C1C1C},
            {kSparkNowRing, 0xF0FFFFFF}, {kSparkLimit, 0xFFE53935}};
        const std::pair<int, uint32_t> light[] = {
            {kSparkLit, 0x12000000},     {kSparkOutline, 0x24000000},     {kSparkPace, 0x801A1A1A},
            {kSparkArea, 0x47FB8C00},
            {kSparkLines, 0xFFFB8C00},   {kSparkDots, 0xFFFB8C00},        {kSparkNowLine, 0x80000000},
            {kSparkProjection, 0xFFFB8C00}, {kSparkCaret, 0xF0000000},    {kSparkNowCut, 0xFFF3F3F3},
            {kSparkNowRing, 0xF0000000}, {kSparkLimit, 0xFFE53935}};
        for (auto [shape, color] : dark) CHECK(SparkShapeColor(shape, m, 0, d, false) == color);
        for (auto [shape, color] : light) CHECK(SparkShapeColor(shape, m, 0, d, true) == color);
        for (bool theme : {false, true}) nearColor(SparkShapeColor(kSparkNow, m, 0, d, theme), 0xFFFF9A3A);
        // A chosen pace color, at half alpha on either taskbar.
        MeterDisplay pink = plot([](Settings& s) { s.paceTickColor = RGB(0xFF, 0x6E, 0xC7); });
        for (bool theme : {false, true}) CHECK(SparkShapeColor(kSparkPace, m, 0, pink, theme) == 0x80FF6EC7);
        // Each chart in its own quota's color: the 5-hour quota's 90% red, the weekly 60% yellow, Fable's
        // 40% green, each dot a shade lighter.
        AccountData three = Fresh({90, 60, 40, -1});
        three.win5h = Window(90, 9000000);
        three.winWeek = {60, kNow + kMinute, kWeeklyWindowMs};
        three.fableWeek = {40, kNow + kMinute, kWeeklyWindowMs};
        const MeterDisplay ringed = Display(With(cues));
        AccountMeters each = AccountMeterState(ringed, BarLayout::Sparkline, Account(), three, 0, 0);
        CHECK(each.barMask == 0b0111);
        const std::tuple<int, uint32_t, uint32_t> slots[] = {
            {0, 0xFFE53935, 0xFFF0453E}, {1, 0xFFFDD835, 0xFFFFE377}, {2, 0xFF43A047, 0xFF4DA950}};
        for (auto [slot, usage, lifted] : slots) {
            CHECK(SparkShapeColor(kSparkLines, each, slot, ringed, false) == usage);
            CHECK(SparkShapeColor(kSparkArea, each, slot, ringed, true) == ((usage & 0xFFFFFF) | 0x47000000));
            nearColor(SparkShapeColor(kSparkNow, each, slot, ringed, false), lifted);
            // The ring off: the dot in the plain usage color.
            CHECK(SparkShapeColor(kSparkNow, each, slot, Display(), false) == usage);
        }
        // The ring off, the default: no cut-out or ring, no caret, the dot unlifted on either taskbar.
        const MeterDisplay plain = plot();
        AccountMeters flat = spark(Window(80, 9000000), {}, plain);
        CHECK(flat.quotas[0].spark[kSparkNowCut].empty() && flat.quotas[0].spark[kSparkNowRing].empty() &&
              flat.quotas[0].spark[kSparkCaret].empty() && !flat.quotas[0].spark[kSparkNow].empty());
        for (bool theme : {false, true}) CHECK(SparkShapeColor(kSparkNow, flat, 0, plain, theme) == 0xFFFB8C00);
        // Stale: grey, the dot a lighter grey; the limit color stays red.
        AccountData staleData = Fresh();
        staleData.win5h = Window(80, 9000000);
        staleData.stale = true;
        AccountMeters stale = AccountMeterState(d, BarLayout::Sparkline, Account(L"anthropic", L"A", {true, false, false, false}),
                                                staleData, 0, 0);
        CHECK(SparkShapeColor(kSparkLines, stale, 0, d, false) == 0xFF9E9E9E);
        nearColor(SparkShapeColor(kSparkNow, stale, 0, d, false), 0xFFA7A7A7);
        CHECK(SparkShapeColor(kSparkLimit, stale, 0, d, false) == 0xFFE53935);
        // Colorblind: 80% is the orange band, the limit vermillion.
        MeterDisplay colorblind = plot([](Settings& s) {
            cues(s);
            s.colorblindMode = true;
        });
        AccountMeters cb = spark(Window(80, 9000000), {}, colorblind);
        CHECK(SparkShapeColor(kSparkLines, cb, 0, colorblind, false) == 0xFFE69F00);
        nearColor(SparkShapeColor(kSparkNow, cb, 0, colorblind, false), 0xFFF0A91E);
        CHECK(SparkShapeColor(kSparkLimit, cb, 0, colorblind, false) == 0xFFD55E00);
        // Strokes: the outline 1 DIP solid, the diagonal 1 DIP dashed 2-2, the runs 1.3 solid, the
        // projection 1.2 dashed 1-1.5 (in stroke widths); every other shape filled.
        for (int k = 0; k < kSparkShapeCount; k++) {
            const SparkShapeStyle& style = kSparkShapeStyles[k];
            const std::array<double, 3> expected = k == kSparkOutline ? std::array<double, 3>{1, 0, 0} :
                                                   k == kSparkPace ? std::array<double, 3>{1, 2, 2} :
                                                   k == kSparkLines ? std::array<double, 3>{1.3, 0, 0} :
                                                   k == kSparkProjection ? std::array<double, 3>{1.2, 1, 1.5} :
                                                                           std::array<double, 3>{0, 0, 0};
            CHECK_NEAR(style.stroke, expected[0]);
            CHECK_NEAR(style.dashes[0], expected[1]);
            CHECK_NEAR(style.dashes[1], expected[2]);
        }
        // Drawing order: the lit part first, then the outline, the limit mark last.
        CHECK(kSparkLit == 0 && kSparkOutline == 1 && kSparkPace == 2 && kSparkArea == 3 && kSparkLines == 4 &&
              kSparkDots == 5 && kSparkNowLine == 6 && kSparkProjection == 7 && kSparkCaret == 8 &&
              kSparkNowCut == 9 && kSparkNowRing == 10 && kSparkNow == 11 && kSparkLimit == 12 &&
              kSparkShapeCount == 13);
    });

    Run("visual test trace: readings from part-way into the window up to now, two runs and a lone reading", [] {
        const AccountConfig claude = Account(L"anthropic", L"CC 1");
        const WindowUsage reading60 = Window(60, 9000000);
        const ULONGLONG start = kNow - 9000000;
        std::vector<TileTracePoint> trace = VisualTestTrace(claude, reading60, kNow);
        CHECK(!trace.empty() && !trace[0].connect);
        // Oldest first, inside the window's elapsed part, rising to the reading at now.
        for (size_t k = 0; k < trace.size(); k++) {
            CHECK(trace[k].timeMs > start && trace[k].timeMs <= kNow);
            CHECK(trace[k].used > 0 && trace[k].used <= 60);
            if (k) CHECK(trace[k].timeMs > trace[k - 1].timeMs && trace[k].used >= trace[k - 1].used);
        }
        CHECK(trace.back().timeMs == kNow && trace.back().used == 60 && trace.back().connect);
        // Connected runs: two of several readings, and one reading alone.
        std::vector<size_t> runs;
        for (const TileTracePoint& p : trace) {
            if (!p.connect) runs.push_back(0);
            runs.back()++;
        }
        CHECK(std::count(runs.begin(), runs.end(), size_t(1)) == 1 &&
              std::count_if(runs.begin(), runs.end(), [](size_t n) { return n > 1; }) == 2);
        // Charted on the 48 x 32 plot: two runs and their areas, a dot, and the current reading on
        // the last one, half way at 60% (13.2, drawn at 13.25).
        AccountData data = Fresh();
        data.win5h = reading60;
        AccountMeters m = AccountMeterState(Display(), BarLayout::Sparkline, Account(L"anthropic", L"CC 1", {true, false, false, false}),
                                            data, 0, 0, [&](int, const WindowUsage& r) { return VisualTestTrace(claude, r, kNow); });
        const auto& shapes = m.quotas[0].spark;
        CHECK(shapes[kSparkLines].size() == 2 && shapes[kSparkArea].size() == 2 && shapes[kSparkDots].size() == 1);
        point(shapes[kSparkLines][1].segments.back(), 24, 13.25);
        dot(m.quotas[0], 24, 13.25);
        // A tick later, its window moved with it: the same readings, a tick later.
        std::vector<TileTracePoint> later =
            VisualTestTrace(claude, {60, reading60.resetUnixMs + kMinute, kFiveHours}, kNow + kMinute);
        CHECK(later.size() == trace.size());
        for (size_t k = 0; k < later.size(); k++) {
            CHECK(later[k].timeMs == trace[k].timeMs + kMinute && later[k].used == trace[k].used &&
                  later[k].connect == trace[k].connect);
        }
        // None for the OpenAI sample, without a reading or a window, or before the window starts.
        CHECK(VisualTestTrace(Account(L"openai", L"OAI"), reading60, kNow).empty());
        CHECK(VisualTestTrace(claude, {-1, reading60.resetUnixMs, kFiveHours}, kNow).empty());
        CHECK(VisualTestTrace(claude, {60, 0, 0}, kNow).empty());
        CHECK(VisualTestTrace(claude, {60, kFiveHours, kFiveHours}, kNow).empty());
        CHECK(VisualTestTrace(claude, Window(60, kFiveHours), kNow).empty());
    });
}

static void RunMatrixTests() {
    Run("quota matrix: cells down the first column, then the second, in slot order; no pace marks", [] {
        auto matrix = [](std::array<bool, kQuotaBarCount> bars, std::array<double, kQuotaBarCount> pct = {10, 20, 30, 40}) {
            return AccountMeterState(Display(), BarLayout::QuotaMatrix, Account(L"anthropic", L"A", bars), Fresh(pct), 0, 0);
        };
        auto cell = [](const QuotaMeter& q, int row, int column) { CHECK(q.matrixRow == row && q.matrixColumn == column); };
        AccountMeters one = matrix({false, false, true, false});
        CHECK(one.barMask == 0b0100);
        cell(one.quotas[2], 0, 0);
        AccountMeters two = matrix({false, true, false, true});
        CHECK(two.barMask == 0b1010);
        cell(two.quotas[1], 0, 0); cell(two.quotas[3], 1, 0);
        AccountMeters three = matrix({true, false, true, true});
        cell(three.quotas[0], 0, 0); cell(three.quotas[2], 1, 0); cell(three.quotas[3], 0, 1);
        AccountMeters four = matrix({true, true, true, true});
        cell(four.quotas[0], 0, 0); cell(four.quotas[1], 1, 0); cell(four.quotas[2], 0, 1); cell(four.quotas[3], 1, 1);
        // After the first success an unreported quota takes no cell; the next one moves up.
        AccountMeters gap = matrix({true, true, true, true}, {10, -1, 30, 40});
        CHECK(gap.barMask == 0b1101);
        cell(gap.quotas[0], 0, 0); cell(gap.quotas[2], 1, 0); cell(gap.quotas[3], 0, 1);
        // No pace marks, with markers on and a window running.
        AccountData timed = Fresh();
        timed.win5h = Window(10, 9000000);
        AccountMeters paced = AccountMeterState(Display(), BarLayout::QuotaMatrix,
                                                Account(L"anthropic", L"A", {true, false, false, false}), timed, 0, 0);
        CHECK(!paced.quotas[0].paceVisible && paced.quotas[0].paceUnits == 0 && paced.quotas[0].paceHalo.empty());
    });

    Run("quota matrix: numbers per bar mode, a dash without a reading; keys on every slot in every style", [] {
        auto matrix = [](const AccountData& data, BarMode mode = BarMode::Used) {
            MeterDisplay d = Display(With([&](Settings& s) { s.barMode = mode; }));
            return AccountMeterState(d, BarLayout::QuotaMatrix, Account(), data, 0, 0);
        };
        // Used unclamped; remaining clamped to 0-100%.
        AccountMeters used = matrix(Fresh({12, 110, 0, 99.4}));
        CHECK(used.quotas[0].text == L"12%" && used.quotas[1].text == L"110%" && used.quotas[2].text == L"0%" &&
              used.quotas[3].text == L"99%");
        AccountMeters left = matrix(Fresh({12, 110, 0, 99.4}), BarMode::Remaining);
        CHECK(left.quotas[0].text == L"88%" && left.quotas[1].text == L"0%" && left.quotas[2].text == L"100%" &&
              left.quotas[3].text == L"1%");
        // Colors follow the used share in either mode.
        CHECK(used.quotas[1].color == 0xFFE53935 && left.quotas[1].color == 0xFFE53935 && left.quotas[0].color == 0xFF43A047);
        // Before the first success every selected quota has a cell: a dash until it reads.
        AccountData before = Fresh({42, -1, -1, -1});
        before.lastSuccessMs = 0;
        AccountMeters waiting = matrix(before);
        CHECK(waiting.barMask == 0b1111 && waiting.quotas[0].text == L"42%" && waiting.quotas[1].text == L"\u2013" &&
              waiting.quotas[3].text == L"\u2013");
        CHECK(matrix(before, BarMode::Remaining).quotas[2].text == L"\u2013");
        // Keys on every slot in every style, selected or not; OpenAI's fourth is its credits.
        for (int layout = 0; layout <= int(kLastBarLayout); layout++) {
            for (std::wstring provider : {L"anthropic", L"openai", L"antigravity"}) {
                AccountMeters m = AccountMeterState(Display(), BarLayout(layout), Account(provider, L"A", {true, false, false, false}),
                                                    Fresh({10, -1, -1, -1}), 0, 0);
                CHECK(m.quotas[0].key == L"5h" && m.quotas[1].key == L"7d" && m.quotas[2].key == L"Fa");
                CHECK(m.quotas[3].key == (provider == L"openai" ? L"Cr" : L"Ex"));
            }
        }
    });

    Run("quota matrix: the number size, two rows shrunk to fit the tile, at least 7 DIPs; keys at 9/14 of it", [] {
        auto fonts = [](int quotas, int size, double taskbar = 48) {
            MeterDisplay d = Display(With([&](Settings& s) { s.headlineFontSize = size; }), 1, taskbar);
            std::array<bool, kQuotaBarCount> bars{};
            for (int k = 0; k < quotas; k++) bars[k] = true;
            return AccountMeterState(d, BarLayout::QuotaMatrix, Account(L"anthropic", L"A", bars), Fresh({10, 20, 30, 40}), 0, 0);
        };
        auto check = [](const AccountMeters& m, double font) {
            CHECK_NEAR(m.matrixFont, font);
            CHECK_NEAR(m.matrixKeyFont, font * 9 / 14);
        };
        // 40 DIP tiles: two rows' 0.7 cap heights and their 5 DIP gap fit 2 DIPs clear of both edges
        // up to (40 - 4 - 5) / 1.4 = 22.14 DIPs; one row takes the number size as it is.
        check(fonts(1, 14), 14);
        check(fonts(2, 14), 14);
        check(fonts(4, 24), 31 / 1.4);
        check(fonts(1, 24), 24);
        // 24 DIP tiles (a 32 DIP taskbar): (24 - 9) / 1.4 = 10.71.
        check(fonts(2, 14, 32), 15 / 1.4);
        check(fonts(3, 10, 32), 10);
        check(fonts(1, 14, 32), 14);
        // Never under 7 DIPs: a 16 DIP tile would allow 5.
        MeterDisplay tiny = Display();
        tiny.tileHeight = 16;
        check(AccountMeterState(tiny, BarLayout::QuotaMatrix, Account(), Fresh({10, 20, 30, 40}), 0, 0), 7);
        // Before the first success every selected quota counts: two rows.
        AccountData before = Fresh();
        before.lastSuccessMs = 0;
        check(AccountMeterState(Display({}, 1, 32), BarLayout::QuotaMatrix, Account(L"anthropic", L"A", {true, true, false, false}),
                                before, 0, 0),
              15 / 1.4);
    });
}

static void RunMeterTests() {
    RunDisplayTests();
    RunColorTests();
    RunBarTests();
    RunTimeTrackTests();
    RunTileTests();
    RunFaceTileTests();
    RunSparklineTests();
    RunMatrixTests();
    RunAccountTests();
    RunCountdownTests();
    RunIdentityTests();
    RunSnapshotTests();
}
