// Meter model: what every quota meter shows (fill, usage color, pace mark, text) and the tile,
// ring, bar and pace tick geometry and colors, as plain values from explicit inputs. Pure: no XAML, WinRT,
// window, registry or global state; colors are ARGB. Renderers own measuring, theme detection,
// fonts and applied-state caching.
//
// MeterDisplayFor is the entry. It clamps the display settings to their ranges, and every other
// function here relies on them (clamp bounds, ring and pace tick fit).

// Tile sizes, in DIPs.
// Nominal; shorter taskbars get shorter tiles. Bar columns are at least as tall.
static constexpr double kTileHeight = 40;
static constexpr double kHeadlineSegmentLength = 9;
static constexpr double kHeadlineSegmentThickness = 3;
// Quota window keys on bar labels and headline tiles; OpenAI credits show "Cr" instead of "Ex".
static constexpr PCWSTR kQuotaKeys[kQuotaBarCount] = {L"5h", L"7d", L"Fa", L"Ex"};

static bool IsTileLayout(BarLayout layout) {
    return layout != BarLayout::Stacked && layout != BarLayout::Vertical;
}

static uint32_t UsageColor(double pct, bool stale, int yellowThreshold, int orangeThreshold,
                           int redThreshold, bool colorblindMode) {
    if (stale || pct < 0) return 0xFF9E9E9E;

    if (colorblindMode) {
        if (pct >= redThreshold) return 0xFFD55E00;
        if (pct >= orangeThreshold) return 0xFFE69F00;
        if (pct >= yellowThreshold) return 0xFF56B4E9;
        return 0xFF0072B2;
    }

    if (pct >= redThreshold) return 0xFFE53935;
    if (pct >= orangeThreshold) return 0xFFFB8C00;
    if (pct >= yellowThreshold) return 0xFFFDD835;
    return 0xFF43A047;
}

// A bar fill's highlight: the usage color 0.06 lighter in OKLCH lightness with the same hue and
// chroma, the chroma reduced only as far as sRGB requires. Fills shade from it at their top edge
// (vertical bars: left) to the usage color 60% across. Alpha is kept.
static uint32_t UsageHighlight(uint32_t argb) {
    auto linear = [](uint32_t channel) {
        double c = channel / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    double r = linear(argb >> 16 & 0xFF), g = linear(argb >> 8 & 0xFF), b = linear(argb & 0xFF);
    double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    double lightness = std::min(0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s + 0.06, 0.999);
    double okA = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    double okB = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    // Linear sRGB of the lifted color with its chroma scaled by `k`.
    auto lifted = [&](double k) {
        double l3 = lightness + 0.3963377774 * okA * k + 0.2158037573 * okB * k;
        double m3 = lightness - 0.1055613458 * okA * k - 0.0638541728 * okB * k;
        double s3 = lightness - 0.0894841775 * okA * k - 1.2914855480 * okB * k;
        l3 = l3 * l3 * l3;
        m3 = m3 * m3 * m3;
        s3 = s3 * s3 * s3;
        return std::array<double, 3>{4.0767416621 * l3 - 3.3077115913 * m3 + 0.2309699292 * s3,
                                     -1.2684380046 * l3 + 2.6097574011 * m3 - 0.3413193965 * s3,
                                     -0.0041960863 * l3 - 0.7034186147 * m3 + 1.7076147010 * s3};
    };
    auto inside = [&](double k) {
        auto rgb = lifted(k);
        return std::all_of(rgb.begin(), rgb.end(), [](double c) { return c >= -1e-4 && c <= 1 + 1e-4; });
    };
    double k = 1;
    if (!inside(1)) {
        double outside = 1;
        k = 0;
        for (int i = 0; i < 30; i++) {
            double mid = (k + outside) / 2;
            (inside(mid) ? k : outside) = mid;
        }
    }
    auto encode = [](double c) {
        c = std::clamp(c, 0.0, 1.0);
        c = c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1 / 2.4) - 0.055;
        return (uint32_t)std::clamp(std::lround(c * 255), 0L, 255L);
    };
    auto rgb = lifted(k);
    return (argb & 0xFF000000) | encode(rgb[0]) << 16 | encode(rgb[1]) << 8 | encode(rgb[2]);
}

// Bar layouts' unfilled track, or their thin rail.
static constexpr uint32_t kBarTrackColor = 0x46808080;
static constexpr uint32_t kBarRailColor = 0x66808080;

// Bar layouts' pace tick: a core over a halo. The default color turns dark on a light taskbar; a
// chosen color stays.
struct PaceMarkColors {
    uint32_t core, halo;
};

static PaceMarkColors PaceMarkColorsFor(COLORREF color, bool light) {
    color &= 0x00FFFFFF;
    if (light && color == kDefaultPaceTickColor) return {0xFF1A1A1A, 0x80FFFFFF};
    return {0xFF000000 | uint32_t(GetRValue(color)) << 16 | uint32_t(GetGValue(color)) << 8 |
                GetBValue(color),
            0xC0000000};
}

// Ring geometry. The centre keeps at least kTileRingHoleMin of radius so the identity always
// fits; strokes and gaps shrink instead, down to kTileRingStrokeMin.
static constexpr double kTileRingHoleMin = 7.0;
static constexpr double kTileRingStrokeMin = 0.75;
// Space between neighbouring segmented arcs along their centreline.
static constexpr double kTileArcGapMin = 1.0;
// Room for ring shapes past the ring area: a pace tick reaches 1.5 DIPs past the outer edge,
// which lies 0.5 DIPs inside the area, and its square corners a little further.
static constexpr double kTileRingOverflow = 2.0;

struct TileArc {
    double start;  // Degrees: 0 = right, positive = clockwise.
    double sweep;
};

// Segmented ring arcs by tile quota count. Concentric rings each fill clockwise from the top.
static constexpr TileArc kSegmentedArcs[kQuotaBarCount][kQuotaBarCount] = {
    {{270, 360}},
    {{195, 150}, {165, -150}},
    {{165, 90}, {135, -90}, {285, 90}},
    {{190, 70}, {280, 70}, {10, 70}, {100, 70}},
};
static constexpr TileArc kConcentricArc{270, 360};

struct TileRingGeometry {
    double size = 0;         // Ring diameter.
    double stroke = 0;       // Effective stroke of each arc or ring.
    double gap = 0;          // Concentric: space between rings. Segmented: space between arcs.
    double radius = 0;       // Centreline radius of the outermost arc or ring.
    double hole = 0;         // Free centre radius, inside the innermost stroke.
    double logo = 0;         // Logo side without a badge.
    double badge = 0;        // Badge height.
    double badgeWidth = 0;
    double badgeFont = 0;
    double badgeLeft = 0;    // From the ring area's top-left corner.
    double badgeTop = 0;
};

// `badgeChars`: length of the badge text, 0 without a badge.
static TileRingGeometry TileRingLayout(bool concentric, double size, int thickness, int count,
                                       int badgeChars) {
    TileRingGeometry g;
    g.size = size;
    double edge = size / 2 - 0.5;  // Outer edge of the outermost stroke.
    double band = std::max(edge - kTileRingHoleMin, kTileRingStrokeMin);
    double stroke = std::clamp<double>(thickness, kTileRingStrokeMin, band);
    double used = 0;
    if (count > 0 && concentric) {
        // Nominal strokes and gaps thin out as rings are added, then shrink to keep the hole.
        static constexpr double kStrokeShare[kQuotaBarCount] = {1.0, 0.87, 0.73, 0.63};
        stroke = std::max(thickness * kStrokeShare[count - 1], kTileRingStrokeMin);
        g.gap = count > 1 ? std::clamp(thickness * 0.43, 0.8, 2.0) : 0.0;
        double need = count * stroke + (count - 1) * g.gap;
        if (need > band) {
            stroke = std::max(stroke * band / need, kTileRingStrokeMin);
            g.gap = count > 1 ? std::max((band - count * stroke) / (count - 1), 0.0) : 0.0;
        }
        used = count * stroke + (count - 1) * g.gap;
    } else if (count > 0) {
        // Arcs reach half a stroke into the angular gaps between them at either end.
        double gapAngle = (count == 1 ? 0.0 : count == 4 ? 20.0 : 30.0) * std::numbers::pi / 180;
        if (gapAngle > 0) {
            stroke = std::max(std::min(stroke, (gapAngle * edge - kTileArcGapMin) /
                                                   (1 + gapAngle / 2)),
                              kTileRingStrokeMin);
            g.gap = gapAngle * (edge - stroke / 2) - stroke;
        }
        used = stroke;
    }
    g.stroke = stroke;
    g.radius = edge - stroke / 2;
    g.hole = edge - used;
    g.logo = std::min(size * 0.44, 2 * (g.hole - 1.2));
    // Badge: a circle, or a pill for two characters, centred just inside the ring's lower-right
    // edge and kept inside the ring area.
    g.badge = std::clamp(size * 0.34, 10.0, 13.0);
    g.badgeFont = g.badge * 0.68;
    g.badgeWidth = std::max(g.badge, badgeChars * g.badgeFont * 0.6 + 6.4);
    double badgeCenter = size / 2 + (edge + 1 - g.badge / 2) / std::numbers::sqrt2;
    g.badgeLeft = std::min(badgeCenter - g.badgeWidth / 2, size - g.badgeWidth);
    g.badgeTop = std::min(badgeCenter - g.badge / 2, size - g.badge);
    return g;
}

// A ring tile shape: closed figures of lines and circular arcs, filled even-odd, in DIPs from
// the ring area's top-left corner. No arc turns more than 180 degrees.
struct TileOutlineSegment {
    double x = 0, y = 0;    // End point.
    double radius = 0;      // Arc radius; 0 for a line.
    bool clockwise = true;  // On screen, y down.
};

struct TileOutlineFigure {
    double x = 0, y = 0;    // Start point.
    std::vector<TileOutlineSegment> segments;
};

using TileOutline = std::vector<TileOutlineFigure>;

// The band between radii `inner` and `outer` around (center, center), clockwise from `from` to
// `to` degrees (0 = right), its corners at either end rounded by `fromCorner` and `toCorner`; a
// full annulus from 360 degrees on. Corners too wide for the span shrink together.
static TileOutline TileSectorOutline(double center, double outer, double inner, double from,
                                     double to, double fromCorner, double toCorner) {
    constexpr double kDegrees = 180 / std::numbers::pi;
    auto at = [&](double radius, double degrees, double arc = 0, bool clockwise = true) {
        return TileOutlineSegment{center + radius * std::cos(degrees / kDegrees),
                                  center + radius * std::sin(degrees / kDegrees), arc, clockwise};
    };
    // Edges in two halves, so none turns more than 180 degrees.
    auto edge = [&](std::vector<TileOutlineSegment>& segments, double radius, double start,
                    double end) {
        segments.push_back(at(radius, (start + end) / 2, radius, end > start));
        segments.push_back(at(radius, end, radius, end > start));
    };
    TileOutline outline;
    if (to - from >= 360) {
        for (double radius : {outer, inner}) {
            TileOutlineSegment start = at(radius, 0);
            outline.push_back({start.x, start.y, {}});
            edge(outline.back().segments, radius, 0, 360);
        }
        return outline;
    }

    // A corner of radius r touches its circular edge this many degrees from its end, and its
    // radial edge this far from the centre. Corners reach furthest along the inner edge.
    auto outerReach = [&](double r) { return std::asin(r / (outer - r)) * kDegrees; };
    auto innerReach = [&](double r) { return std::asin(r / (inner + r)) * kDegrees; };
    auto outerRadial = [&](double r) { return std::sqrt((outer - r) * (outer - r) - r * r); };
    auto innerRadial = [&](double r) { return std::sqrt((inner + r) * (inner + r) - r * r); };
    if (innerReach(fromCorner) + innerReach(toCorner) > to - from) {
        double fits = 0, wide = 1;
        for (int i = 0; i < 30; i++) {
            double mid = (fits + wide) / 2;
            bool fit = innerReach(fromCorner * mid) + innerReach(toCorner * mid) <= to - from;
            (fit ? fits : wide) = mid;
        }
        fromCorner *= fits;
        toCorner *= fits;
    }
    TileOutlineSegment start = at(outerRadial(fromCorner), from);
    TileOutlineFigure figure{start.x, start.y, {}};
    std::vector<TileOutlineSegment>& segments = figure.segments;
    auto corner = [&](double r, double radius, double degrees) {
        if (r > 0) segments.push_back(at(radius, degrees, r));
    };
    double outerFrom = from + outerReach(fromCorner), outerTo = to - outerReach(toCorner);
    double innerFrom = from + innerReach(fromCorner), innerTo = to - innerReach(toCorner);
    corner(fromCorner, outer, outerFrom);
    edge(segments, outer, outerFrom, outerTo);
    corner(toCorner, outerRadial(toCorner), to);
    segments.push_back(at(innerRadial(toCorner), to));
    corner(toCorner, inner, innerTo);
    edge(segments, inner, innerTo, innerFrom);
    corner(fromCorner, innerRadial(fromCorner), from);
    outline.push_back(std::move(figure));
    return outline;
}

// Tile chrome for the taskbar theme. Logo and text inherit the taskbar's text color instead.
struct TilePalette {
    uint32_t track;
    uint32_t hover;
    uint32_t open;  // Tile whose flyout is open.
    uint32_t badge;
    uint32_t badgeOutline;
};

static TilePalette TilePaletteFor(bool light) {
    if (light) return {0x24000000, 0x0A000000, 0xB3FFFFFF, 0xFFD8DBE2, 0xFFEEF0F4};
    return {0x2BFFFFFF, 0x0FFFFFFF, 0x1AFFFFFF, 0xFF3A3D44, 0xFF1F2126};
}

// Display inputs of every meter. Build it with MeterDisplayFor only; the ranges below hold.
struct MeterDisplay {
    ULONGLONG now;
    BarMode barMode;
    int barLength;            // 10-500 DIPs.
    int barThickness;         // 2-50 DIPs.
    int barGap;               // 0-500 DIPs.
    BarStyle barStyle;
    int ringSize;             // kRingSizeMin-kRingSizeMax DIPs.
    int ringThickness;        // kRingThicknessMin-kRingThicknessMax DIPs.
    int yellowThreshold;      // 0-100, yellow <= orange <= red.
    int orangeThreshold;
    int redThreshold;
    int pollMinutes;          // 2-1440.
    bool colorblindMode;
    bool showPaceTicks;
    PaceTickStyle paceTickStyle;
    COLORREF paceTickColor;   // RGB only.
    PercentTextAlignment percentTextAlignment;
    bool showExtraBarAmounts;
    bool showStaleWarning;
    // Physical pixels per DIP, at least 1 (100% scaling), so a pace tick fits twice into the
    // shortest bar.
    double rasterizationScale;
    double tileHeight;        // 24-kTileHeight DIPs.
};

// `taskbarHeight`: the taskbar's height in DIPs, 0 if unknown; tiles leave 4 DIPs above and
// below them.
static MeterDisplay MeterDisplayFor(const Settings& settings, ULONGLONG now,
                                    double rasterizationScale, double taskbarHeight) {
    int yellow = std::clamp(settings.yellowThreshold, 0, 100);
    int orange = std::clamp(settings.orangeThreshold, yellow, 100);
    return {
        .now = now,
        .barMode = settings.barMode,
        .barLength = std::clamp(settings.barLength, 10, 500),
        .barThickness = std::clamp(settings.barThickness, 2, 50),
        .barGap = std::clamp(settings.barGap, 0, 500),
        .barStyle = settings.barStyle == BarStyle::Rail ? BarStyle::Rail : BarStyle::Track,
        .ringSize = std::clamp(settings.ringSize, kRingSizeMin, kRingSizeMax),
        .ringThickness = std::clamp(settings.ringThickness, kRingThicknessMin, kRingThicknessMax),
        .yellowThreshold = yellow,
        .orangeThreshold = orange,
        .redThreshold = std::clamp(settings.redThreshold, orange, 100),
        .pollMinutes = std::clamp(settings.pollMinutes, 2, 24 * 60),
        .colorblindMode = settings.colorblindMode,
        .showPaceTicks = settings.showPaceTicks,
        .paceTickStyle = settings.paceTickStyle,
        .paceTickColor = settings.paceTickColor & 0x00FFFFFF,
        .percentTextAlignment = settings.percentTextAlignment,
        .showExtraBarAmounts = settings.showExtraBarAmounts,
        .showStaleWarning = settings.showStaleWarning,
        .rasterizationScale =
            std::isfinite(rasterizationScale) ? std::max(rasterizationScale, 1.0) : 1.0,
        .tileHeight = taskbarHeight > 0 ?
            std::clamp(std::floor(taskbarHeight) - 8, 24.0, kTileHeight) : kTileHeight,
    };
}

// Visual test accounts with sample data at display.now. `data` may be null.
static void BuildVisualTestSnapshot(const MeterDisplay& display,
                                    std::vector<AccountConfig>* accounts,
                                    std::vector<AccountData>* data) {
    // One value per usage color band. Red is past the quota whatever the thresholds: used text
    // above 100%, remaining mode clamped to 0.
    const double green = display.yellowThreshold / 2.0;
    const double yellow = (display.yellowThreshold + display.orangeThreshold) / 2.0;
    const double orange = (display.orangeThreshold + display.redThreshold) / 2.0;
    const double red = 110;
    const ULONGLONG now = display.now;
    // One to four taskbar quotas, each from slots its provider reports; two Anthropic accounts
    // share a provider so tiles show number badges. A negative value is a quota not reported.
    // Antigravity is stale, with an error. CC 1 (badged) and Go (marked) lead their bars with
    // their identity.
    struct Sample {
        PCWSTR provider;
        PCWSTR label;
        std::array<double, kQuotaBarCount> pct;
        std::array<bool, kQuotaBarCount> showBars;
        bool barLogo;
    };
    const std::array<Sample, 4> samples = {{
        {L"openai", L"OAI", {green, yellow, -1, green / 2}, {true, true, false, true}, false},
        {L"anthropic", L"CC 1", {orange, green, -1, -1}, {true, true, false, false}, true},
        {L"anthropic", L"CC 2", {green / 2, red, yellow, green}, {true, true, true, true}, false},
        {L"antigravity", L"Go", {green, green * 1.5, -1, -1}, {true, false, false, false}, true},
    }};
    static constexpr std::array<ULONGLONG, kQuotaBarCount> kDurations = {
        kFiveHourWindowMs,
        kWeeklyWindowMs,
        kWeeklyWindowMs,
        30ULL * 24 * 60 * 60 * 1000,
    };
    static constexpr std::array<double, kQuotaBarCount> kRemainingFractions = {
        0.35, 0.5, 0.6, 0.8,
    };
    static constexpr double kCreditsMax = 1000;

    accounts->clear();
    accounts->reserve(samples.size());
    if (data) {
        data->clear();
        data->reserve(samples.size());
    }
    for (size_t i = 0; i < samples.size(); i++) {
        const Sample& sample = samples[i];
        AccountConfig account;
        account.provider = sample.provider;
        account.label = sample.label;
        account.showBars = sample.showBars;
        account.barLogo = sample.barLogo;
        bool openAi = account.provider == L"openai";
        if (openAi) account.creditsMax = (int)kCreditsMax;
        accounts->push_back(account);

        if (!data) continue;
        AccountData accountData;
        std::array<WindowUsage*, kQuotaBarCount> usage = {
            &accountData.win5h, &accountData.winWeek,
            &accountData.fableWeek, &accountData.extraUsage,
        };
        for (int w = 0; w < kQuotaBarCount; w++) {
            if (sample.pct[w] < 0) continue;
            usage[w]->pct = sample.pct[w];
            usage[w]->windowDurationMs = kDurations[w];
            usage[w]->resetUnixMs = now +
                (ULONGLONG)std::lround(kDurations[w] * (kRemainingFractions[w] - 0.05 * i));
        }
        if (openAi) {
            // Credits are a balance against the user's max, without a window.
            accountData.hasCredits = true;
            accountData.creditsBalance = kCreditsMax * (1 - sample.pct[kExtraUsageBar] / 100);
            ApplyCreditsMax(account, &accountData);
        } else if (sample.pct[kExtraUsageBar] >= 0) {
            accountData.extraUsedAmount = sample.pct[kExtraUsageBar] / 2.0;
            accountData.extraLimitAmount = 50.0;
        }
        accountData.plan = L"Visual test";
        accountData.lastSuccessMs = now;
        accountData.stale = false;
        if (account.provider == L"antigravity") {
            // Claude/GPT pools appear only in the details flyout. A failed refresh keeps the
            // last success, as the fetch thread does; ten minutes before now, it stays stale.
            accountData.antigravityThirdParty5h = {
                yellow, now + (ULONGLONG)std::lround(kFiveHourWindowMs * 0.4), kFiveHourWindowMs};
            accountData.antigravityThirdPartyWeek = {
                green, now + (ULONGLONG)std::lround(kWeeklyWindowMs * 0.6), kWeeklyWindowMs};
            accountData.lastSuccessMs = now - 10 * 60000;
            accountData.stale = true;
            accountData.error = L"Antigravity not running";
        }
        data->push_back(std::move(accountData));
    }
}

// Bar layouts' corners, thin rail, pace tick and percent text placement, in DIPs along and across
// the bar. Pixel details scale with 1 / rasterizationScale.
struct BarGeometry {
    struct Point {
        double along, cross;
    };
    double radius;         // Track and fill corners.
    double rail;           // Thin rail thickness, centred across the bar; 0 with a full track.
    // How far the pace tick reaches past the bar across it: on both sides, or a notch's near side.
    double overhang;
    double paceLength;     // Pace tick outline (the halo).
    double paceCrossSize;  // Overhang included.
    double paceRadius;     // Corner radius: dots, and lines over a thin rail.
    // Line, notch and dot core: centred along the outline, `coreCrossOffset` from its top
    // (vertical bars: left) edge. A notch's core is flush with its outer end.
    double coreLength;
    double coreCrossSize;
    double coreCrossOffset;
    double coreRadius;
    std::array<std::array<Point, 12>, 2> caret;  // Caret outline, then core polygon.
    double textInset;      // Percent text shift away from its aligned edge.
};

static BarGeometry BarGeometryFor(const MeterDisplay& display) {
    double scale = display.rasterizationScale;
    double pixel = 1.0 / scale;
    PaceTickStyle style = display.paceTickStyle;
    bool track = display.barStyle == BarStyle::Track;
    BarGeometry g{};
    g.radius = display.barThickness / 4.0;
    g.rail = track ? 0 : std::max(1.0, (double)std::lround(display.barThickness / 4.0));
    // Over a full track, ticks other than dots reach past the bar: a DIP's worth of whole device
    // pixels, at most half the bar gap.
    if (track && style != PaceTickStyle::Dot) {
        g.overhang = std::min(std::lround(scale), std::lround(display.barGap * scale) / 2) * pixel;
    }
    g.paceLength = (style == PaceTickStyle::Caret ? 5 : 4) * pixel;
    g.coreLength = (style == PaceTickStyle::Dot ? 3 : 2) * pixel;
    switch (style) {
        case PaceTickStyle::Line:
            // The halo wraps the core: a pixel beyond it at both ends.
            g.paceCrossSize = display.barThickness + 2 * g.overhang;
            g.coreCrossSize = std::max(g.paceCrossSize - 2 * pixel, 0.0);
            g.coreCrossOffset = pixel;
            g.paceRadius = track ? 0 : pixel;
            g.coreRadius = track ? 0 : 0.5 * pixel;
            break;
        case PaceTickStyle::Notch:
            // From the overhang's outer edge into the bar; the halo a pixel deeper than the core.
            g.coreCrossSize = g.overhang + std::max(2.0, std::ceil(display.barThickness * 0.6));
            g.paceCrossSize = g.coreCrossSize + pixel;
            break;
        case PaceTickStyle::Dot:
            g.paceCrossSize = 4 * pixel;
            g.coreCrossSize = 3 * pixel;
            g.coreCrossOffset = 0.5 * pixel;
            g.paceRadius = 2 * pixel;
            g.coreRadius = 1.5 * pixel;
            break;
        case PaceTickStyle::Caret:
        default:
            g.paceCrossSize = display.barThickness + 2 * g.overhang;
            g.coreCrossSize = g.paceCrossSize;
            break;
    }
    // Caret: a bracket of two caps joined by a stem, a dark halo around the core.
    double length = g.paceLength;
    double cross = g.paceCrossSize;
    for (int layer = 0; layer < 2; layer++) {
        bool core = layer == 1;
        double capInset = (core ? 1 : 0) * pixel;
        double stemInset = (core ? 2 : 1) * pixel;
        double capDepth = std::min((core ? 1.0 : 2.0) * pixel, cross / 2.0);
        g.caret[layer] = {{
            {capInset, 0},
            {length - capInset, 0},
            {length - capInset, capDepth},
            {length - stemInset, capDepth},
            {length - stemInset, cross - capDepth},
            {length - capInset, cross - capDepth},
            {length - capInset, cross},
            {capInset, cross},
            {capInset, cross - capDepth},
            {stemInset, cross - capDepth},
            {stemInset, capDepth},
            {capInset, capDepth},
        }};
    }
    g.textInset = 4 * pixel;
    return g;
}

// The identity text of a tile, or of a bar column with barLogo. Logo identities show a number badge when several configured accounts
// share the provider; text identities, and logos that fail to load, show `text`.
struct TileIdentityText {
    bool badge = false;
    std::wstring badgeText;
    std::wstring text;
};

static TileIdentityText TileIdentityFor(const std::vector<AccountConfig>& accounts,
                                        size_t index) {
    const AccountConfig& account = accounts[index];
    TileIdentityText identity;
    identity.badge = account.tileIdentity == TileIdentity::LogoBadge &&
        std::count_if(accounts.begin(), accounts.end(), [&](const AccountConfig& other) {
            return other.provider == account.provider;
        }) > 1;
    if (identity.badge) {
        // The label's trailing digits (at most two), else its first letter, else its first
        // non-space character.
        const std::wstring& label = account.label;
        size_t digits = label.size();
        while (digits > 0 && iswdigit(label[digits - 1])) digits--;
        auto letter = std::find_if(label.begin(), label.end(),
                                   [](wchar_t ch) { return iswalpha(ch); });
        if (letter == label.end()) {
            letter = std::find_if(label.begin(), label.end(),
                                  [](wchar_t ch) { return !iswspace(ch); });
        }
        if (digits < label.size()) {
            identity.badgeText =
                label.substr(label.size() - std::min<size_t>(label.size() - digits, 2));
        } else if (letter != label.end()) {
            identity.badgeText.assign(1, (wchar_t)towupper(*letter));
        } else {
            identity.badgeText = std::to_wstring(index + 1);
        }
    }
    // Short text, else the label without spaces, else the account's position.
    identity.text = account.tileText;
    if (identity.text.empty()) {
        std::copy_if(account.label.begin(), account.label.end(),
                     std::back_inserter(identity.text), [](wchar_t ch) { return !iswspace(ch); });
    }
    if (identity.text.empty()) identity.text = std::to_wstring(index + 1);
    return identity;
}

// One quota slot's meter. Bar fields cover every slot of a bar layout; tile fields cover a tile
// layout's slots in the mask.
struct QuotaMeter {
    uint32_t color = 0;        // Usage color: follows used % in either bar mode.
    bool paceVisible = false;  // Pace mark shown, in layouts that draw one.

    // The fill's corner radius toward 100%, in bars, headline segments and ring arcs;
    // its start corners are the track's. Over a full track the fill ends in a straight cut until
    // it comes within a corner's radius of the track's end.
    double fillEndRadius = 0;

    // Bars, in DIPs along the bar.
    int fillPx = 0;
    int pacePx = 0;            // Pace tick's start.
    PercentTextAlignment textAlignment = PercentTextAlignment::Right;  // Never Adaptive.
    double textOffset = 0;     // Percent text shift.
    bool darkText = false;     // Black percent text over a yellow or orange fill.
    std::wstring text;         // Percent or amount; empty while unavailable.

    // Tiles. Fill and pace in thousandths of the meter are the update keys; the geometry
    // below follows them.
    int fillUnits = 0;
    int paceUnits = 0;
    double fillLength = 0;     // Headline segment fill.
    // Rings: the drawn arc and its centreline radius. Partial arcs reach half a stroke past
    // their slot's ends along the centreline.
    TileArc arc{};
    double radius = 0;
    double fillSweep = 0;      // Ring fill, in degrees from the arc's start.
    double paceAngle = 0;      // Ring pace tick, in degrees; clear of a partial arc's corners.
    TileOutline track;
    TileOutline fill;          // Empty without usage.
    TileOutline paceHalo;      // Empty while the pace mark is hidden.
    TileOutline paceCore;
};

struct AccountMeters {
    bool stale = false;
    bool warn = false;         // Stale with an error: the label's "!" or the identity's mark.
    int barMask = 0;           // Quota slots shown.
    std::array<QuotaMeter, kQuotaBarCount> quotas;
    std::wstring labelText;    // Bars' label.
    double labelOpacity = 0;
    double identityOpacity = 0;  // Tiles' identity, and bars' with barLogo.
    // Ring tiles: the identity text's fitted font size and width, and the warning mark's top
    // margin, into the tile's padding.
    TileRingGeometry ring;
    double ringTextFont = 0;
    double ringTextWidth = 0;
    double ringWarnTop = 0;
    // Logo + headline tiles: the most-used quota's number, window key and color.
    std::wstring headline;
    std::wstring headlineKey;
    uint32_t headlineColor = 0;
};

// `layout`: the layout the meters are drawn in. `badgeChars`: the tile badge's text length, 0
// without a badge. `textWidthPerPt`: the tile identity text's width per DIP of font size, 0
// without text.
static AccountMeters AccountMeterState(const MeterDisplay& display, BarLayout layout,
                                       const AccountConfig& account, const AccountData& data,
                                       int badgeChars, double textWidthPerPt) {
    AccountMeters meters;
    const std::array<const WindowUsage*, kQuotaBarCount> usage = {
        &data.win5h, &data.winWeek, &data.fableWeek, &data.extraUsage};
    bool remainingMode = display.barMode == BarMode::Remaining;
    int yellow = display.yellowThreshold;
    int orange = display.orangeThreshold;
    int red = display.redThreshold;

    // Account state. Stale after two missed polls; Antigravity after two minutes.
    ULONGLONG staleAfterMs = (account.provider == L"antigravity" ? 1ULL :
                              (ULONGLONG)display.pollMinutes) * 2 * 60000;
    meters.stale = data.stale || data.lastSuccessMs == 0 ||
                   display.now - data.lastSuccessMs > staleAfterMs;
    meters.warn = display.showStaleWarning && meters.stale && !data.error.empty();
    // With barLogo the identity carries the mark instead.
    meters.labelText = meters.warn && !account.barLogo ? account.label + L"!" : account.label;
    meters.labelOpacity = meters.stale ? 0.45 : 0.8;
    meters.identityOpacity = meters.stale ? 0.45 : 1.0;
    // Every selected quota until the first success, then the reported ones.
    int count = 0;
    for (int w = 0; w < kQuotaBarCount; w++) {
        if (account.showBars[w] && (data.lastSuccessMs == 0 || usage[w]->pct >= 0)) {
            meters.barMask |= 1 << w;
            count++;
        }
    }

    bool tiles = IsTileLayout(layout);
    bool concentric = layout == BarLayout::ConcentricRings;
    bool ring = concentric || layout == BarLayout::SegmentedRing;
    bool paceMarks = display.showPaceTicks && layout != BarLayout::LogoHeadline;
    if (ring) {
        meters.ring = TileRingLayout(concentric,
                                     std::min<double>(display.ringSize, display.tileHeight),
                                     display.ringThickness, count, badgeChars);
        // The largest font, from min(12, size / 3) down to 7, whose text fits across the hole at
        // half its cap height; clipped beyond that.
        double radius = meters.ring.hole - 0.8;
        auto chord = [&](double font) {
            double half = font * 0.36;
            return 2 * std::sqrt(std::max(radius * radius - half * half, 0.0));
        };
        double font = std::max(std::min(12.0, meters.ring.size * 0.33), 7.0);
        while (font > 7 && textWidthPerPt * font > chord(font)) font -= 0.25;
        font = std::max(font, 7.0);
        meters.ringTextFont = font;
        meters.ringTextWidth = chord(font);
        meters.ringWarnTop = 1 - (display.tileHeight - meters.ring.size) / 2;
    }
    // Tiles fill per the bar mode, clamped to the meter; unavailable quotas stay empty.
    auto tileShown = [&](double pct) {
        if (pct < 0) return 0.0;
        return std::clamp(remainingMode ? 100.0 - pct : pct, 0.0, 100.0);
    };

    BarGeometry bar = BarGeometryFor(display);
    int position = 0;
    for (int w = 0; w < kQuotaBarCount; w++) {
        const WindowUsage& wu = *usage[w];
        QuotaMeter& m = meters.quotas[w];
        // Color stays keyed to actual usage so depleting quota still reds out.
        m.color = UsageColor(wu.pct, meters.stale, yellow, orange, red, display.colorblindMode);
        m.paceVisible = paceMarks && !meters.stale && wu.pct >= 0 &&
                        wu.resetUnixMs > wu.windowDurationMs && wu.windowDurationMs > 0 &&
                        display.now >= wu.resetUnixMs - wu.windowDurationMs;
        double remainingFraction = m.paceVisible ?
            ((double)wu.resetUnixMs - (double)display.now) / wu.windowDurationMs : 0.0;

        if (!tiles) {
            // Remaining mode shows the quota left (100 - used); n/a (pct < 0) stays unchanged
            // and used mode stays unclamped, so text can exceed 100%.
            double displayed = remainingMode && wu.pct >= 0 ?
                                   std::clamp(100.0 - wu.pct, 0.0, 100.0) : wu.pct;
            m.fillPx = displayed > 0 ?
                std::clamp((int)std::lround(display.barLength * displayed / 100.0), 2,
                           display.barLength) : 0;
            m.fillEndRadius = display.barStyle == BarStyle::Rail ? bar.radius :
                std::max(0.0, bar.radius - (display.barLength - m.fillPx));
            if (m.paceVisible) {
                // Centred on the pace, kept inside the bar's rounded ends.
                double pacePct = remainingMode ? remainingFraction * 100.0 :
                                                 (1.0 - remainingFraction) * 100.0;
                pacePct = std::clamp(pacePct, 0.0, 100.0);
                double radius = std::clamp(bar.radius, bar.paceLength / 2.0,
                                           (display.barLength - bar.paceLength) / 2.0);
                double center = std::clamp(display.barLength * pacePct / 100.0,
                                           radius, display.barLength - radius);
                m.pacePx = (int)std::lround(center - bar.paceLength / 2.0);
            }
            // Adaptive text sits over the fill from the yellow threshold on, else over the track.
            m.textAlignment = display.percentTextAlignment == PercentTextAlignment::Adaptive ?
                (displayed >= yellow ? PercentTextAlignment::Left : PercentTextAlignment::Right) :
                display.percentTextAlignment;
            m.textOffset = m.textAlignment == PercentTextAlignment::Left ? bar.textInset :
                           m.textAlignment == PercentTextAlignment::Center ? 0 : -bar.textInset;
            bool yellowOrOrange = !meters.stale && wu.pct >= 0 &&
                ((wu.pct >= yellow && wu.pct < orange) || (wu.pct >= orange && wu.pct < red));
            m.darkText = m.textAlignment == PercentTextAlignment::Left && yellowOrOrange;
            if (wu.pct >= 0) {
                wchar_t text[24];
                if (w == kExtraUsageBar && display.showExtraBarAmounts &&
                    data.extraLimitAmount >= 0) {
                    // Amount follows the bar mode: spent in used mode, left in remaining mode.
                    // Dollars for Anthropic, credits for OpenAI.
                    double amount = remainingMode ? data.extraLimitAmount - data.extraUsedAmount :
                                                    data.extraUsedAmount;
                    amount = std::max(amount, 0.0);
                    swprintf(text, ARRAYSIZE(text),
                             account.provider == L"openai" ? L"%.0f" : L"$%.2f", amount);
                } else {
                    swprintf(text, ARRAYSIZE(text), L"%.0f%%", displayed);
                }
                m.text = text;
            }
            continue;
        }

        if (!(meters.barMask & (1 << w))) continue;
        double shown = tileShown(wu.pct);
        double fill = shown / 100;
        const TileRingGeometry& geometry = meters.ring;
        bool fullRing = false;
        if (ring) {
            TileArc slot = concentric ? kConcentricArc : kSegmentedArcs[count - 1][position];
            m.radius = geometry.radius -
                       (concentric ? position * (geometry.stroke + geometry.gap) : 0.0);
            // Partial arcs reach half a stroke past their slot at both ends, along the
            // centreline.
            fullRing = std::abs(slot.sweep) >= 360;
            double grow = fullRing ? 0.0 :
                geometry.stroke / 2 / m.radius * 180 / std::numbers::pi;
            double direction = slot.sweep < 0 ? -1 : 1;
            m.arc = {slot.start - direction * grow, slot.sweep + 2 * direction * grow};
            // Any usage fills at least 5 degrees.
            if (shown > 0) fill = std::max(fill, 5.0 / std::abs(m.arc.sweep));
        }
        int slotPosition = position++;
        m.fillUnits = shown > 0 ? std::max(1, (int)std::lround(fill * 1000)) : 0;
        if (m.paceVisible) {
            double pace = remainingMode ? remainingFraction : 1.0 - remainingFraction;
            m.paceUnits = (int)std::lround(std::clamp(pace, 0.0, 1.0) * 1000);
        }
        double pace = m.paceUnits / 1000.0;
        if (ring) {
            // Annular sectors with stroke / 4 corners at a partial arc's ends; full rings have
            // none. Fills start with the track's corners and end in a radial cut at their value.
            double center = geometry.size / 2;
            double outer = m.radius + geometry.stroke / 2, inner = m.radius - geometry.stroke / 2;
            double corner = fullRing ? 0.0 : geometry.stroke / 4;
            double end = m.arc.start + m.arc.sweep;
            double low = std::min(m.arc.start, end), high = std::max(m.arc.start, end);
            m.track = TileSectorOutline(center, outer, inner, low, high, corner, corner);
            m.fillSweep = m.arc.sweep * fill;
            m.fillEndRadius = std::max(0.0, corner - std::abs(m.arc.sweep - m.fillSweep) *
                                                         std::numbers::pi / 180 * m.radius);
            if (m.fillUnits) {
                double fillEnd = m.arc.start + m.fillSweep;
                m.fill = m.arc.sweep > 0 ?
                    TileSectorOutline(center, outer, inner, m.arc.start, fillEnd, corner,
                                      m.fillEndRadius) :
                    TileSectorOutline(center, outer, inner, fillEnd, m.arc.start, m.fillEndRadius,
                                      corner);
            }
            m.paceAngle = m.arc.start + m.arc.sweep * pace;
            if (m.paceVisible) {
                // A radial tick: a 2 DIP core over a 3.5 DIP halo, square ended, kept clear of a
                // partial arc's corners. It reaches 1.5 DIPs past the ring, at most half the gap
                // toward a neighbouring ring, and the core stops half a DIP short of the halo.
                static constexpr double kHalo = 3.5, kCore = 2, kOverhang = 1.5, kCoreInset = 0.5;
                if (!fullRing) {
                    double pad = (corner + kHalo / 2) / m.radius * 180 / std::numbers::pi;
                    m.paceAngle = std::max(low + pad, std::min(m.paceAngle, high - pad));
                }
                double between = std::min(kOverhang, geometry.gap / 2);
                double outside = concentric && slotPosition > 0 ? between : kOverhang;
                double inside = concentric && slotPosition < count - 1 ? between : kOverhang;
                double radians = m.paceAngle * std::numbers::pi / 180;
                double alongX = std::cos(radians), alongY = std::sin(radians);
                // From `from` to `to` DIPs out from the centre, `width` across.
                auto tick = [&](double from, double to, double width) {
                    double acrossX = -alongY * width / 2, acrossY = alongX * width / 2;
                    auto point = [&](double distance, double side) {
                        return TileOutlineSegment{center + alongX * distance + side * acrossX,
                                                  center + alongY * distance + side * acrossY};
                    };
                    TileOutlineSegment start = point(from, 1);
                    return TileOutline{
                        {start.x, start.y, {point(to, 1), point(to, -1), point(from, -1)}}};
                };
                m.paceHalo = tick(inner - inside, outer + outside, kHalo);
                m.paceCore = tick(inner - inside + kCoreInset, outer + outside - kCoreInset, kCore);
            }
        } else {
            // Headline segments: corners a quarter of the thickness; the fill cut flat until the
            // track's end. Any usage fills at least 2 DIPs, as on the bars.
            double length = kHeadlineSegmentLength;
            m.fillLength = m.fillUnits ? std::clamp(length * fill, 2.0, length) : 0;
            m.fillEndRadius = std::max(0.0, kHeadlineSegmentThickness / 4 - (length - m.fillLength));
        }
    }

    // Headline: the most-used tile quota, shown per the bar mode in its usage color.
    if (layout == BarLayout::LogoHeadline) {
        int top = -1;
        for (int w = 0; w < kQuotaBarCount; w++) {
            if ((meters.barMask & (1 << w)) && usage[w]->pct >= 0 &&
                (top < 0 || usage[w]->pct > usage[top]->pct)) {
                top = w;
            }
        }
        meters.headline = L"\u2013";
        if (top >= 0) {
            wchar_t text[16];
            swprintf(text, ARRAYSIZE(text), L"%.0f%%", tileShown(usage[top]->pct));
            meters.headline = text;
            meters.headlineKey = top == kExtraUsageBar && account.provider == L"openai" ?
                                     L"Cr" : kQuotaKeys[top];
        }
        meters.headlineColor = UsageColor(top >= 0 ? usage[top]->pct : -1, meters.stale, yellow,
                                          orange, red, display.colorblindMode);
    }
    return meters;
}
