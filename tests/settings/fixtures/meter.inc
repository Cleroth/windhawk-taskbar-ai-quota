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
            s.showExtraBarAmounts = true; s.showStaleWarning = false; s.barGap = 7; s.barStyle = BarStyle::Rail;
        });
        MeterDisplay d = MeterDisplayFor(settings, kNow, 1.25, 44);
        CHECK(d.now == kNow); CHECK(d.barMode == BarMode::Remaining);
        CHECK(d.barLength == 120); CHECK(d.barThickness == 6);
        CHECK(d.barGap == 7); CHECK(d.barStyle == BarStyle::Rail);
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
            s.barGap = -5; s.barStyle = BarStyle(-1);
        }));
        CHECK(low.barLength == 10); CHECK(low.barThickness == 2);
        CHECK(low.barGap == 0); CHECK(low.barStyle == BarStyle::Track);
        CHECK(low.ringSize == 24); CHECK(low.ringThickness == 1); CHECK(low.pollMinutes == 2);
        CHECK(low.yellowThreshold == 0); CHECK(low.orangeThreshold == 0); CHECK(low.redThreshold == 0);
        MeterDisplay high = Display(With([](Settings& s) {
            s.barLength = 9999; s.barThickness = 51; s.ringSize = 41; s.ringThickness = 7;
            s.pollMinutes = 100000; s.yellowThreshold = 150; s.orangeThreshold = 120;
            s.redThreshold = 101; s.paceTickColor = 0xAB123456; s.barGap = 9999; s.barStyle = BarStyle(9);
        }));
        CHECK(high.barLength == 500); CHECK(high.barThickness == 50);
        CHECK(high.barGap == 500); CHECK(high.barStyle == BarStyle::Track);
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
}

static void RunTileTests() {
    Run("headline segment fills", [] {
        auto segment = [](double pct, const MeterDisplay& d = Display()) {
            return First({pct, 0, 0}, d, BarLayout::LogoHeadline).quotas[0];
        };
        // Any usage fills at least 2 DIPs, as on the bars.
        CHECK(segment(50).fillUnits == 500); CHECK_NEAR(segment(50).fillLength, 4.5);
        CHECK(segment(10).fillUnits == 100); CHECK_NEAR(segment(10).fillLength, 2);
        CHECK_NEAR(segment(25).fillLength, 2.25);
        CHECK(segment(120).fillUnits == 1000); CHECK_NEAR(segment(120).fillLength, 9);
        CHECK(segment(0).fillUnits == 0); CHECK_NEAR(segment(0).fillLength, 0);
        CHECK(segment(0.04).fillUnits == 1); CHECK_NEAR(segment(0.04).fillLength, 2);
        MeterDisplay remaining = Display(With([](Settings& s) { s.barMode = BarMode::Remaining; }));
        CHECK(segment(30, remaining).fillUnits == 700); CHECK_NEAR(segment(30, remaining).fillLength, 6.3);
    });

    Run("headline fills: cut flat until a quarter-thickness corner from the end", [] {
        auto segment = [](double pct) {
            return First({pct, 0, 0}, Display(), BarLayout::LogoHeadline).quotas[0];
        };
        // 9 x 3 headline segments: 0.75 DIP corners; 95% leaves 0.45 DIPs.
        CHECK_NEAR(segment(100).fillEndRadius, 0.75); CHECK_NEAR(segment(95).fillEndRadius, 0.3);
        CHECK_NEAR(segment(50).fillEndRadius, 0); CHECK_NEAR(segment(10).fillEndRadius, 0);
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
        // Logo on bars: the identity carries the mark, so the label drops its "!" in both bar
        // layouts; the account stays warned and dimmed.
        AccountData failed = Fresh({10, -1, -1, -1});
        failed.stale = true;
        failed.error = L"timeout";
        AccountConfig logo = Account();
        logo.barLogo = true;
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical}) {
            AccountMeters marked = AccountMeterState(Display(), layout, logo, failed, 0, 0);
            CHECK(marked.warn && marked.labelText == L"A");
            CHECK_NEAR(marked.labelOpacity, 0.45); CHECK_NEAR(marked.identityOpacity, 0.45);
        }
        AccountMeters plain = AccountMeterState(Display(), BarLayout::Vertical, Account(), failed, 0, 0);
        CHECK(plain.warn && plain.labelText == L"A!");
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
        // Logo on bars: CC 1 (badged) and Go (stale); every identity the default logo and badge.
        CHECK(!accounts[0].barLogo && accounts[1].barLogo && !accounts[2].barLogo && accounts[3].barLogo);
        for (const AccountConfig& account : accounts) CHECK(account.tileIdentity == TileIdentity::LogoBadge);
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
        // Go leads its bars with its logo, which carries the mark: its label has no "!".
        AccountMeters go = AccountMeterState(display, BarLayout::Stacked, accounts[3], data[3], 0, 0);
        CHECK(go.warn && go.labelText == L"Go" && go.quotas[0].color == 0xFF9E9E9E && !go.quotas[0].paceVisible);
        accounts[3].barLogo = false;
        CHECK(AccountMeterState(display, BarLayout::Stacked, accounts[3], data[3], 0, 0).labelText == L"Go!");
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

static void RunMeterTests() {
    RunDisplayTests();
    RunColorTests();
    RunBarTests();
    RunTileTests();
    RunAccountTests();
    RunIdentityTests();
    RunSnapshotTests();
}
