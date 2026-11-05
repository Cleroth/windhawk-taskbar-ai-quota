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
// On a left or right taskbar, accounts keep this much of its width free on each side.
static constexpr double kSideInset = 4;
// Logo sides at 100% logo size beside bars and on headline tiles; rings size theirs to the hole.
static constexpr double kBarLogoSide = 15;
static constexpr double kHeadlineLogoSide = 16;
// A headline tile's window key text, in proportion to its number's (9 DIPs beside 14).
static constexpr double kHeadlineKeyScale = 9.0 / 14;
// A headline countdown follows a reset arrow, at the key's size; tiles reserve the width of
// kHeadlineCountdownReserve for it.
static constexpr PCWSTR kHeadlineCountdownArrow = L"\u21BB ";
static constexpr PCWSTR kHeadlineCountdownReserve = L"\u21BB 00h 00m";
// Quota window keys on bar labels and tiles; QuotaKey gives the per-provider ones.
static constexpr PCWSTR kQuotaKeys[kQuotaBarCount] = {L"5h", L"7d", L"Fa", L"Ex"};

// A slot's key: Antigravity's Gemini Argon weekly shows "Ar" instead of "Fa", OpenAI credits "Cr"
// instead of "Ex", GitHub Copilot's monthly AI credits and OpenCode Go's monthly "Mo".
static PCWSTR QuotaKey(std::wstring_view provider, int slot) {
    if (slot == kFableWeeklyBar) return provider == L"antigravity" ? L"Ar" : kQuotaKeys[slot];
    if (slot != kExtraUsageBar) return kQuotaKeys[slot];
    return provider == L"openai" ? L"Cr" : provider == L"copilot" || provider == L"opencode" ? L"Mo" :
                                                                                              kQuotaKeys[slot];
}
// Tile text's cap height, as a share of its font size (Segoe UI Variable).
static constexpr double kTileCapShare = 0.7;
// Quota matrix cells: the key, kMatrixKeyGap DIPs, then the number; columns and rows apart by
// their gaps.
static constexpr double kMatrixKeyGap = 3, kMatrixColumnGap = 8, kMatrixRowGap = 5;
// Sparkline strokes and marks, in DIPs. Dash patterns are in stroke widths, as XAML's and
// Direct2D's dash arrays take them. The plot keeps kSparkInset DIPs free at its top and bottom;
// an account's charts sit kSparkChartGap apart.
static constexpr double kSparkLineStroke = 1.3;
static constexpr double kSparkGuideStroke = 1;
static constexpr std::array<double, 2> kSparkGuideDashes = {2, 2};
static constexpr double kSparkProjectionStroke = 1.2;
static constexpr std::array<double, 2> kSparkProjectionDashes = {1, 1.5};
static constexpr double kSparkDotRadius = 1.5, kSparkLimitRadius = 1.75;
// The current reading: a dot (its usage color lifted by kSparkNowLift) inside a ring, cut out of
// the marks under them in the taskbar's color; without the ring, a plain dot; both at a 100%
// reading size. The current time: a line across the plot, and a caret on its bottom edge.
static constexpr double kSparkNowRadius = 1.9, kSparkRingInner = 2.6, kSparkRingOuter = 3.6, kSparkNowLift = 0.03;
static constexpr double kSparkPlainNowRadius = 2;
static constexpr double kSparkNowLineWidth = 1, kSparkCaretHalfWidth = 2.4, kSparkCaretHeight = 3.2;
static constexpr double kSparkInset = 2, kSparkPlotRadius = 3, kSparkChartGap = 4, kSparkOutlineStroke = 1;
// Room for sparkline shapes past the plot: the largest ring around a point on its edge.
static constexpr double kSparkOverflow = kSparkRingOuter * kSparklineReadingScaleMax / 100 + 0.4;
// How far before and after now a long window's chart spans (MeterDisplay::sparklineAroundMs); 0:
// the whole window.
static ULONGLONG SparklineAroundMs(SparklineSpan span) {
    static constexpr ULONGLONG kHourMs = 60 * 60 * 1000;
    switch (span) {
        case SparklineSpan::Hours12: return 12 * kHourMs;
        case SparklineSpan::Hours24: return 24 * kHourMs;
        case SparklineSpan::Hours36: return 36 * kHourMs;
        case SparklineSpan::Hours48: return 48 * kHourMs;
        default: return 0;
    }
}
// Chart text, 3 DIPs in from the plot's edges: the weekly chart's extra usage, its key, then the
// number, kSparkExtraKeyGap DIPs apart. Text placement estimates widths at kSparkTextWidthShare
// of the font size per character, generously.
static constexpr double kSparkExtraKeyGap = 2, kSparkTextInset = 3, kSparkTextWidthShare = 0.6;
// Sparkline fills' and guides' alpha over their usage or pace color.
static constexpr uint32_t kSparkAreaAlpha = 0x47000000, kSparkGuideAlpha = 0x80000000;

// A sparkline chart's shapes, in drawing order, from its plot's top-left: the lit part of the plot
// (the window's elapsed part), the plot's outline, the even-pace diagonal, the recorded usage (an area under each
// connected run, the runs, isolated readings as dots), the current time's line, the projection,
// the current time's caret, the current reading's cut-out, ring and dot, and the limit mark.
enum SparkShape {
    kSparkLit,
    kSparkOutline,
    kSparkPace,
    kSparkArea,
    kSparkLines,
    kSparkDots,
    kSparkNowLine,
    kSparkProjection,
    kSparkCaret,
    kSparkNowCut,
    kSparkNowRing,
    kSparkNow,
    kSparkLimit,
    kSparkShapeCount,
};

// Each shape's stroke: open figures stroked `stroke` DIPs wide, solid with round caps and joins,
// or dashed (flat caps); 0: closed figures filled nonzero, so overlapping dots merge and a ring's
// reversed inner circle cuts its hole.
struct SparkShapeStyle {
    double stroke = 0;
    std::array<double, 2> dashes{};  // In stroke widths; zeros: solid.
};

static constexpr SparkShapeStyle kSparkShapeStyles[kSparkShapeCount] = {
    {}, {kSparkOutlineStroke}, {kSparkGuideStroke, kSparkGuideDashes}, {}, {kSparkLineStroke}, {}, {},
    {kSparkProjectionStroke, kSparkProjectionDashes}, {}, {}, {}, {}, {},
};

static bool IsTileLayout(BarLayout layout) {
    return layout != BarLayout::Stacked && layout != BarLayout::Vertical;
}

// Tiles whose identity sits inside their meters, sized to the hole: the rings and the rounded
// square.
static bool IsRingTile(BarLayout layout) {
    return layout == BarLayout::SegmentedRing || layout == BarLayout::ConcentricRings ||
           layout == BarLayout::RoundedSquare;
}

// Tiles whose meters are outline shapes in a face (TileOutline): ring tiles, the gauge and the
// pies. Gauges and pies show the identity beside the face, as headline tiles do. Sparklines draw
// outlines in their plots too, a chart per quota rather than a meter.
static bool IsFaceTile(BarLayout layout) {
    return IsRingTile(layout) || layout == BarLayout::Gauge || layout == BarLayout::ClockPies;
}

// A palette's usage colors by band (core/model.h), opaque 0xAARRGGBB.
static std::array<uint32_t, 4> UsagePaletteColors(UsagePalette palette, const std::array<COLORREF, 4>& custom) {
    const std::array<COLORREF, 4>& colors = palette == UsagePalette::Colorblind ? kColorblindUsageColors :
                                            palette == UsagePalette::Custom     ? custom :
                                                                                  kDefaultUsageColors;
    std::array<uint32_t, 4> argb;
    for (size_t i = 0; i < argb.size(); i++) {
        argb[i] = 0xFF000000 | uint32_t(GetRValue(colors[i])) << 16 | uint32_t(GetGValue(colors[i])) << 8 |
                  GetBValue(colors[i]);
    }
    return argb;
}

// `colors`: UsagePaletteColors. Stale and unknown readings are grey in every palette.
static uint32_t UsageColor(double pct, bool stale, int yellowThreshold, int orangeThreshold,
                           int redThreshold, const std::array<uint32_t, 4>& colors) {
    if (stale || pct < 0) return 0xFF9E9E9E;
    return colors[pct >= redThreshold ? 3 : pct >= orangeThreshold ? 2 : pct >= yellowThreshold ? 1 : 0];
}

// WCAG relative luminance of an 0xAARRGGBB color, 0 (black) to 1 (white); alpha is ignored.
static double RelativeLuminance(uint32_t argb) {
    auto linear = [](uint32_t channel) {
        double c = channel / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(argb >> 16 & 0xFF) + 0.7152 * linear(argb >> 8 & 0xFF) + 0.0722 * linear(argb & 0xFF);
}

// An extra usage or credits amount on the taskbar: whole credits or dollars to the cent below
// 1,000, then three significant digits with k or M (1.23k, 62.5k, 111k, $1.5k).
static std::wstring TaskbarAmountText(double amount, bool dollars) {
    wchar_t text[32];
    PCWSTR sign = dollars ? L"$" : L"";
    if (amount < (dollars ? 999.995 : 999.5)) {
        swprintf(text, ARRAYSIZE(text), dollars ? L"$%.2f" : L"%.0f", amount);
    } else {
        // Amounts that round up into the next suffix read as 1 of it.
        bool millions = amount >= 999.5e3;
        double scaled = std::max(amount / (millions ? 1e6 : 1e3), 1.0);
        swprintf(text, ARRAYSIZE(text), scaled < 999.5 ? L"%s%.3g%s" : L"%s%.0f%s", sign, scaled,
                 millions ? L"M" : L"k");
    }
    return text;
}

// A bar fill's highlight: the usage color `lift` lighter in OKLCH lightness with the same hue and
// chroma, the chroma reduced only as far as sRGB requires. Fills shade from it at their top edge
// (vertical bars: left) to the usage color 60% across. Alpha is kept.
static uint32_t UsageHighlight(uint32_t argb, double lift = 0.06) {
    auto linear = [](uint32_t channel) {
        double c = channel / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    double r = linear(argb >> 16 & 0xFF), g = linear(argb >> 8 & 0xFF), b = linear(argb & 0xFF);
    double l = std::cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
    double m = std::cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
    double s = std::cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
    double lightness = std::min(0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s + lift, 0.999);
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
// A gauge's arc: open at the bottom, filling clockwise from its lower left.
static constexpr TileArc kGaugeArc{135, 270};
// A rounded square's centreline corner radius, as a share of its size.
static constexpr double kTileSquareCornerShare = 0.22;
// Ring, square and pie pace ticks: a core over a halo, square ended, reaching past the meter.
static constexpr double kTilePaceHalo = 3.5, kTilePaceCore = 2, kTilePaceOverhang = 1.5,
                        kTilePaceCoreInset = 0.5;

struct TileRingGeometry {
    double size = 0;         // Ring diameter.
    double stroke = 0;       // Effective stroke of each arc or ring.
    double gap = 0;          // Concentric: space between rings. Segmented: space between arcs.
    double radius = 0;       // Centreline radius of the outermost arc or ring.
    double hole = 0;         // Free centre radius, inside the innermost stroke.
    double logo = 0;         // Logo side, centred in the ring area.
};

// `logoScale`: the logo size as a fraction of its standard size.
static TileRingGeometry TileRingLayout(bool concentric, double size, int thickness, int count,
                                       double logoScale) {
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
    g.logo = std::min(size * 0.44 * logoScale, 2 * (g.hole - 1.2));
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

// A rounded square tile's centreline: a square of half-side `half` with corners of radius
// `corner` around (center, center), measured in DIPs clockwise from its top edge's middle; lengths
// wrap. TileSquareAt is the point `offset` DIPs outward of it (inward when negative) at `along`,
// with the outward normal there.
struct TileSquarePoint {
    double x = 0, y = 0;
    double normalX = 0, normalY = 0;
};

static TileSquarePoint TileSquareAt(double center, double half, double corner, double along, double offset) {
    // Four sides, each from an edge's middle around a corner to the next edge's middle; the first
    // one's frame, turned a quarter clockwise per side.
    double straight = half - corner, quarter = std::numbers::pi * corner / 2, side = 2 * straight + quarter;
    along -= std::floor(along / (4 * side)) * 4 * side;
    int turns = std::min(int(along / side), 3);
    double u = along - turns * side;
    double x, y, nx, ny;
    if (u < straight) {
        x = u, y = -half, nx = 0, ny = -1;
    } else if (u < straight + quarter) {
        double angle = (-90 + 90 * (u - straight) / quarter) * std::numbers::pi / 180;
        nx = std::cos(angle), ny = std::sin(angle);
        x = straight + corner * nx, y = -straight + corner * ny;
    } else {
        x = half, y = -straight + (u - straight - quarter), nx = 1, ny = 0;
    }
    x += offset * nx;
    y += offset * ny;
    for (int k = 0; k < turns; k++) {
        std::tie(x, y) = std::pair{-y, x};
        std::tie(nx, ny) = std::pair{-ny, nx};
    }
    return {center + x, center + y, nx, ny};
}

// The band `width` DIPs wide centred on a rounded square's centreline (TileSquareAt), clockwise
// from `from` to `to` DIPs along it and cut square at both ends; the whole band, as two figures,
// from a full perimeter on. `corner` is at least half the width.
static TileOutline TileSquareBandOutline(double center, double half, double corner, double width,
                                         double from, double to) {
    double straight = half - corner, quarter = std::numbers::pi * corner / 2, side = 2 * straight + quarter;
    // One edge `offset` from the centreline, from `start` to `end` (backward when lower): lines
    // along the sides and quarter arcs around the corners.
    auto edge = [&](std::vector<TileOutlineSegment>& segments, double offset, double start, double end) {
        double low = std::min(start, end), high = std::max(start, end);
        std::vector<double> stops;
        for (double base = std::floor(low / side) * side; base < high; base += side) {
            for (double stop : {base, base + straight, base + straight + quarter}) {
                if (stop > low + 1e-9 && stop < high - 1e-9 && (stops.empty() || stop > stops.back() + 1e-9)) {
                    stops.push_back(stop);
                }
            }
        }
        if (end < start) std::reverse(stops.begin(), stops.end());
        stops.push_back(end);
        double at = start;
        for (double stop : stops) {
            double middle = (at + stop) / 2;
            middle -= std::floor(middle / side) * side;
            bool arc = middle > straight && middle < straight + quarter && corner + offset > 1e-6;
            TileSquarePoint p = TileSquareAt(center, half, corner, stop, offset);
            segments.push_back({p.x, p.y, arc ? corner + offset : 0, end > start});
            at = stop;
        }
    };
    TileOutline outline;
    if (to - from >= 4 * side) {
        for (double offset : {width / 2, -width / 2}) {
            TileSquarePoint start = TileSquareAt(center, half, corner, from, offset);
            outline.push_back({start.x, start.y, {}});
            edge(outline.back().segments, offset, from, from + 4 * side);
        }
        return outline;
    }
    TileSquarePoint start = TileSquareAt(center, half, corner, from, width / 2);
    TileOutlineFigure figure{start.x, start.y, {}};
    edge(figure.segments, width / 2, from, to);
    TileSquarePoint cut = TileSquareAt(center, half, corner, to, -width / 2);
    figure.segments.push_back({cut.x, cut.y});
    edge(figure.segments, -width / 2, to, from);
    outline.push_back(std::move(figure));
    return outline;
}

// Tile chrome for the taskbar theme. Logo and text inherit the taskbar's text color instead.
struct TilePalette {
    uint32_t track;
    uint32_t hover;
    uint32_t open;       // Tile whose flyout is open.
    // The taskbar's own color: the number badge's outline, the cut-out around a sparkline's current
    // reading, and the outage mark's halo on a logo, text or ring identity.
    uint32_t badgeHalo;
    uint32_t plot;     // A sparkline chart's background, darker than the taskbar,
    uint32_t lit;      // over it the window's elapsed part,
    uint32_t outline;  // and the plot's outline.
    uint32_t nowLine;  // A sparkline's current time line,
    uint32_t nowMark;  // and its caret and the current reading's ring.
};

static TilePalette TilePaletteFor(bool light) {
    if (light) {
        return {0x24000000, 0x0A000000, 0xB3FFFFFF, 0xFFF3F3F3, 0x14000000, 0x12000000, 0x24000000, 0x80000000,
                0xF0000000};
    }
    return {0x2BFFFFFF, 0x0FFFFFFF, 0x1AFFFFFF, 0xFF1C1C1C, 0x59000000, 0x1FFFFFFF, 0x2EFFFFFF, 0x80FFFFFF,
            0xF0FFFFFF};
}

// The number badge's text (0: the taskbar's text color, inherited) and outline. White is the
// taskbar's text, outlined in the taskbar's color; any other color stays on either taskbar,
// outlined in a light taskbar's color when it's dark, else a dark one's.
struct BadgeColors {
    uint32_t text, halo;
};

static BadgeColors BadgeColorsFor(COLORREF color, bool light) {
    color &= 0x00FFFFFF;
    if (color == kBadgeTaskbarTextColor) return {0, TilePaletteFor(light).badgeHalo};
    bool dark = GetRValue(color) * 299 + GetGValue(color) * 587 + GetBValue(color) * 114 < 128 * 1000;
    return {0xFF000000 | uint32_t(GetRValue(color)) << 16 | uint32_t(GetGValue(color)) << 8 | GetBValue(color),
            TilePaletteFor(dark).badgeHalo};
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
    int headlineFontSize;     // kHeadlineFontSizeMin-kHeadlineFontSizeMax DIPs.
    int headlineBarLength;    // kHeadlineBarLengthMin-kHeadlineBarLengthMax DIPs.
    int headlineBarThickness; // kHeadlineBarThicknessMin-kHeadlineBarThicknessMax DIPs.
    bool headlineCountdown;
    int sparklineWidth;       // kSparklineWidthMin-kSparklineWidthMax DIPs.
    int sparklineHeight;      // kSparklineHeightMin-kSparklineHeightMax percent of tileHeight.
    int sparklineTextSize;    // kSparklineTextSizeMin-kSparklineTextSizeMax DIPs.
    bool sparklineOutline;
    SparklineExtra sparklineExtra;
    bool sparklineElapsed;
    bool sparklineNowLine;
    bool sparklineNowCaret;
    bool sparklineRing;
    double sparklineReadingScale;  // kSparklineReadingScaleMin-kSparklineReadingScaleMax percent, as a fraction.
    // Long windows' charts span this before and after now (AccountMeters); 0: the whole window.
    ULONGLONG sparklineAroundMs;
    double logoScale;         // kLogoScaleMin-kLogoScaleMax percent, as a fraction.
    LogoBrands logoBrands;
    int badgeSize;            // kBadgeSizeMin-kBadgeSizeMax DIPs.
    COLORREF badgeColor;      // RGB only.
    int badgeOffsetX;         // Up to kBadgeOffsetMax DIPs either way.
    int badgeOffsetY;
    int yellowThreshold;      // 0-100, yellow <= orange <= red.
    int orangeThreshold;
    int redThreshold;
    int pollMinutes;          // 2-1440.
    std::array<uint32_t, 4> usageColors;  // UsagePaletteColors of the chosen palette.
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
    // A left or right taskbar's width in DIPs; 0 across the screen. Accounts stack down it, each
    // tile's identity sits above its meters and its quotas run down it; meter lengths across it
    // (stacked bars, chart widths, ring sizes) fit inside kSideInset of each edge.
    double sideWidth;
};

// A bar label column's width beside stacked or vertical bars, at the percent font size.
static double BarLabelColumnWidth(int percentFontSize) {
    return std::ceil(percentFontSize * 1.4) + 3.0;
}

// `taskbarHeight`: the taskbar's height in DIPs, 0 if unknown (or on a side); tiles leave 4 DIPs
// above and below them. `sideWidth`: MeterDisplay::sideWidth.
static MeterDisplay MeterDisplayFor(const Settings& settings, ULONGLONG now,
                                    double rasterizationScale, double taskbarHeight,
                                    double sideWidth = 0) {
    int yellow = std::clamp(settings.yellowThreshold, 0, 100);
    int orange = std::clamp(settings.orangeThreshold, yellow, 100);
    sideWidth = std::isfinite(sideWidth) ? std::max(sideWidth, 0.0) : 0.0;
    // A side taskbar's room across, and what fits in it, never under each range's minimum.
    int across = (int)std::floor(sideWidth - 2 * kSideInset);
    auto fit = [&](int value, int minimum, double reserved = 0) {
        return sideWidth > 0 ? std::max(minimum, std::min(value, across - (int)std::ceil(reserved))) : value;
    };
    bool stacked = settings.barLayout == BarLayout::Stacked;
    double readingScale =
        std::clamp(settings.sparklineReadingScale, kSparklineReadingScaleMin, kSparklineReadingScaleMax) / 100.0;
    // A chart's current reading reaches past its sides by its mark (as kSparkOverflow); beyond
    // kSideInset that takes room from the chart.
    double readingReach = (settings.sparklineRing ? kSparkRingOuter : kSparkPlainNowRadius) * readingScale + 0.4;
    return {
        .now = now,
        .barMode = settings.barMode,
        .barLength = stacked ? fit(std::clamp(settings.barLength, 10, 500), 10,
                                   settings.showBarLabels ? BarLabelColumnWidth(settings.percentFontSize) : 0) :
                               std::clamp(settings.barLength, 10, 500),
        .barThickness = std::clamp(settings.barThickness, 2, 50),
        .barGap = std::clamp(settings.barGap, 0, 500),
        .barStyle = settings.barStyle == BarStyle::Rail ? BarStyle::Rail : BarStyle::Track,
        .ringSize = fit(std::clamp(settings.ringSize, kRingSizeMin, kRingSizeMax), kRingSizeMin),
        .ringThickness = std::clamp(settings.ringThickness, kRingThicknessMin, kRingThicknessMax),
        .headlineFontSize = std::clamp(settings.headlineFontSize, kHeadlineFontSizeMin, kHeadlineFontSizeMax),
        .headlineBarLength = std::clamp(settings.headlineBarLength, kHeadlineBarLengthMin, kHeadlineBarLengthMax),
        .headlineBarThickness =
            std::clamp(settings.headlineBarThickness, kHeadlineBarThicknessMin, kHeadlineBarThicknessMax),
        .headlineCountdown = settings.headlineCountdown,
        .sparklineWidth = fit(std::clamp(settings.sparklineWidth, kSparklineWidthMin, kSparklineWidthMax),
                              kSparklineWidthMin, 2 * std::max(readingReach - kSideInset, 0.0)),
        .sparklineHeight = std::clamp(settings.sparklineHeight, kSparklineHeightMin, kSparklineHeightMax),
        .sparklineTextSize = std::clamp(settings.sparklineTextSize, kSparklineTextSizeMin, kSparklineTextSizeMax),
        .sparklineOutline = settings.sparklineOutline,
        .sparklineExtra = SparklineExtra(std::clamp(int(settings.sparklineExtra), int(SparklineExtra::Off),
                                                    int(SparklineExtra::Percent))),
        .sparklineElapsed = settings.sparklineElapsed,
        .sparklineNowLine = settings.sparklineNowLine,
        .sparklineNowCaret = settings.sparklineNowCaret,
        .sparklineRing = settings.sparklineRing,
        .sparklineReadingScale = readingScale,
        .sparklineAroundMs = SparklineAroundMs(settings.sparklineSpan),
        .logoScale = std::clamp(settings.logoScale, kLogoScaleMin, kLogoScaleMax) / 100.0,
        .logoBrands = settings.logoBrands,
        .badgeSize = std::clamp(settings.badgeSize, kBadgeSizeMin, kBadgeSizeMax),
        .badgeColor = settings.badgeColor & 0x00FFFFFF,
        .badgeOffsetX = std::clamp(settings.badgeOffsetX, -kBadgeOffsetMax, kBadgeOffsetMax),
        .badgeOffsetY = std::clamp(settings.badgeOffsetY, -kBadgeOffsetMax, kBadgeOffsetMax),
        .yellowThreshold = yellow,
        .orangeThreshold = orange,
        .redThreshold = std::clamp(settings.redThreshold, orange, 100),
        .pollMinutes = std::clamp(settings.pollMinutes, 2, 24 * 60),
        .usageColors = UsagePaletteColors(settings.usagePalette, settings.customUsageColors),
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
        .sideWidth = sideWidth,
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
    // Antigravity is stale, with an error. Identities mix: OAI shows its label (with VisualTestOutage's
    // mark), the Anthropic pair their logo and number badge, Go its logo alone, with the stale mark.
    struct Sample {
        PCWSTR provider;
        PCWSTR label;
        std::array<double, kQuotaBarCount> pct;
        std::array<bool, kQuotaBarCount> showBars;
        TileIdentity identity;
    };
    const std::array<Sample, 4> samples = {{
        {L"openai", L"OAI", {green, yellow, -1, green / 2}, {true, true, false, true}, TileIdentity::Text},
        {L"anthropic", L"CC 1", {orange, green, -1, -1}, {true, true, false, false}, TileIdentity::LogoBadge},
        {L"anthropic", L"CC 2", {green / 2, red, yellow, green}, {true, true, true, true}, TileIdentity::LogoBadge},
        {L"antigravity", L"Go", {green, green * 1.5, orange, -1}, {true, false, false, false}, TileIdentity::Logo},
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
        account.tileIdentity = sample.identity;
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
        // Extra quotas (OpenAI's with their Display setting) and free resets: details flyout only.
        if (account.label == L"CC 2") {
            accountData.extraQuotas = {
                {L"Sonnet weekly",
                 {yellow, now + (ULONGLONG)std::lround(kWeeklyWindowMs * 0.45), kWeeklyWindowMs}},
                {L"Opus weekly", {0, 0, kWeeklyWindowMs, true}},
            };
            accountData.freeResets = 2;
            accountData.freeResetsExpiryMs = now + 3 * 24 * 60 * 60000ULL;
        } else if (openAi) {
            accountData.extraQuotas = {
                {L"Spark 5-hour",
                 {orange, now + (ULONGLONG)std::lround(kFiveHourWindowMs * 0.3), kFiveHourWindowMs}},
                {L"Spark weekly",
                 {green, now + (ULONGLONG)std::lround(kWeeklyWindowMs * 0.7), kWeeklyWindowMs}},
            };
            accountData.freeResets = 1;
        }
        if (account.provider == L"antigravity") {
            // Claude/GPT pools, like its unselected weekly and Argon weekly, appear only in the
            // details flyout. A failed refresh keeps the last success, as the fetch thread does;
            // ten minutes before now, it stays stale.
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

// The visual test accounts' provider status, while outage marks are on: OpenAI's (the OAI sample's
// provider) degraded since 25 minutes before `now`; the others operational.
static ProviderOutage VisualTestOutage(std::wstring_view provider, ULONGLONG now) {
    if (provider != L"openai") return {};
    return {OutageLevel::Degraded, L"Codex API", 0, L"Elevated error rates", now - 25 * 60000};
}

// A recorded reading of a quota in its current window (the burn-up curve's), oldest first.
struct TileTracePoint {
    ULONGLONG timeMs = 0;
    double used = 0;       // Used percentage as recorded; can exceed 100.
    bool connect = false;  // Joined to the previous point; false starts a segment.
};

// Visual test samples' recorded usage of `reading`, for the details flyout and sparklines: from
// part-way into its window up to `now`, ending at the reading, with a recording gap and an
// isolated reading. Times are shares of the window's elapsed span, so the drawn geometry stays
// put between ticks. None for the OpenAI sample, which keeps the views without history, or for a
// reading without a window or before its window starts.
static std::vector<TileTracePoint> VisualTestTrace(const AccountConfig& account, const WindowUsage& reading,
                                                   ULONGLONG now) {
    if (account.provider == L"openai" || reading.pct < 0 || !reading.windowDurationMs ||
        reading.resetUnixMs <= reading.windowDurationMs || now <= reading.resetUnixMs - reading.windowDurationMs) {
        return {};
    }
    static constexpr struct {
        double time, share;
        bool connect;
    } kSamples[] = {
        {0.15, 0.10, false}, {0.22, 0.18, true}, {0.30, 0.22, true}, {0.38, 0.36, true},
        {0.45, 0.40, true},  {0.55, 0.52, false}, {0.68, 0.60, false}, {0.76, 0.66, true},
        {0.84, 0.80, true},  {0.92, 0.88, true}, {1.00, 1.00, true},
    };
    ULONGLONG start = reading.resetUnixMs - reading.windowDurationMs;
    std::vector<TileTracePoint> trace;
    for (const auto& sample : kSamples) {
        trace.push_back({start + (ULONGLONG)std::llround((now - start) * sample.time), reading.pct * sample.share,
                         sample.connect});
    }
    return trace;
}

// How far back a quota's projection measures its rate: a day, so it follows a start late in a
// weekly window while sleep and breaks stay in the average. 5-hour windows (and a window's first
// day) get their window's average; monthly extra usage passes 0 for it, since its percentage
// moves with its budget.
static constexpr ULONGLONG kProjectionLookbackMs = 24ULL * 60 * 60 * 1000;

// A quota's projection from its current reading, to the reset or, reaching 100% before it, to
// the limit.
struct UsageProjection {
    ULONGLONG fromMs = 0;   // The current reading's time: now, or the newest recorded reading if later.
    // The rate's span start: the window's start (its average), the lookback's cutoff, or the
    // reading before a recording gap across that cutoff.
    ULONGLONG sinceMs = 0;
    ULONGLONG toMs = 0;     // The reset, or the limit's time.
    double toUsed = 0;      // Used % at toMs; 100 at the limit.
    bool limit = false;
};

// Projects `reading` (fresh: the caller's check) above 0% and short of 100%, from 5% of its window
// on and before its reset, at the average rate since its window started at 0%, or with a
// `lookbackMs` ending after that, since the cutoff it gives. `trace` (oldest first) places the
// cutoff's usage: interpolated between connected readings, across a recording gap only between
// equal ones, else the span starts at the reading before the gap. Any decrease from there to the
// current reading (a reset, a changed budget) falls back to the window's average.
static std::optional<UsageProjection> ProjectUsage(const WindowUsage& reading,
                                                   const std::vector<TileTracePoint>& trace,
                                                   ULONGLONG now, ULONGLONG lookbackMs) {
    double pct = reading.pct;
    if (pct <= 0 || pct >= 100 || !reading.windowDurationMs || reading.resetUnixMs <= reading.windowDurationMs) {
        return std::nullopt;
    }
    ULONGLONG start = reading.resetUnixMs - reading.windowDurationMs;
    ULONGLONG from = trace.empty() ? now : std::max(now, trace.back().timeMs);
    double elapsed = ((double)from - (double)start) / reading.windowDurationMs;
    if (elapsed < 0.05 || from >= reading.resetUnixMs) return std::nullopt;

    TileTracePoint base{start, 0, false};
    if (lookbackMs && from - start > lookbackMs) {
        // The readings around the cutoff: the window's start before the first, the current
        // reading after the last.
        ULONGLONG cutoff = from - lookbackMs;
        auto next = std::upper_bound(trace.begin(), trace.end(), cutoff,
                                     [](ULONGLONG time, const TileTracePoint& point) { return time < point.timeMs; });
        TileTracePoint before = next == trace.begin() ? base : next[-1];
        TileTracePoint after = next == trace.end() ? TileTracePoint{from, pct, false} : *next;
        TileTracePoint cut = before;
        if (next != trace.begin() && after.connect) {
            cut = {cutoff, before.used + (after.used - before.used) * double(cutoff - before.timeMs) /
                                             double(after.timeMs - before.timeMs)};
        } else if (after.used == before.used) {
            cut = {cutoff, before.used};
        }
        bool rising = true;
        double previous = before.used;
        for (auto point = next; point != trace.end() && rising; ++point) {
            rising = point->used >= previous;
            previous = point->used;
        }
        if (rising && pct >= previous) base = cut;
    }

    // Products before quotients keep exact cases exact (even use reaching 100% at the reset).
    double rise = pct - base.used;
    double span = double(from - base.timeMs);
    UsageProjection projection{from, base.timeMs, reading.resetUnixMs,
                               pct + rise * double(reading.resetUnixMs - from) / span, false};
    if (projection.toUsed > 100) {
        projection.toMs = from + (ULONGLONG)std::llround((100 - pct) * span / rise);
        projection.toUsed = 100;
        projection.limit = true;
    }
    return projection;
}

// Bar layouts' corners, thin rail, pace tick and percent text placement, in DIPs along and across
// the bar. Pixel details scale with 1 / rasterizationScale.
struct BarGeometry {
    struct Point {
        double along, cross;
    };
    double radius;         // Track and fill corners.
    double rail;           // Thin rail thickness, centred across the bar; 0 with a full track.
    // The time track's small bar, while pace markers show in that style (else 0): `timeGap` past
    // the bar's bottom (vertical bars: right) edge, `timeThickness` across, as long as the bar.
    // Its track and fill corners are half its thickness; the fill fills like the bars' and ends
    // at the meter's pacePx. It adds both to the bar's cell.
    double timeGap;
    double timeThickness;
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
    if (track && style != PaceTickStyle::Dot && style != PaceTickStyle::TimeTrack) {
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
        case PaceTickStyle::TimeTrack:
            // A DIP's worth of whole device pixels from the bar; a quarter of its thickness, at
            // least a pixel.
            if (display.showPaceTicks) {
                g.timeGap = std::max(1L, std::lround(scale)) * pixel;
                g.timeThickness = std::max(1L, std::lround(display.barThickness * scale / 4)) * pixel;
            }
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

// The identity text of a tile, or of a bar column's logo identity. A logo identity has a number
// badge when another configured account, hidden or not, shares the provider; text identities on
// tiles, and logos that fail to load, show `text`.
struct TileIdentityText {
    std::wstring badgeText;  // Empty without a badge.
    std::wstring text;
};

static TileIdentityText TileIdentityFor(const std::vector<AccountConfig>& accounts,
                                        size_t index) {
    const AccountConfig& account = accounts[index];
    TileIdentityText identity;
    if (account.tileIdentity == TileIdentity::LogoBadge &&
        std::count_if(accounts.begin(), accounts.end(), [&](const AccountConfig& other) {
            return other.provider == account.provider;
        }) > 1) {
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

// The number badge: its text in bold at the badge size, outlined kBadgeOutline DIPs in the
// palette's halo color. Its box (text and outline) is estimated from the text's length;
// renderers centre the text on the box's centre.
static constexpr double kBadgeOutline = 1;

struct BadgeBox {
    double left = 0, top = 0, width = 0, height = 0;  // Empty without a badge.
};

// The badge's box from the top-left of a `side` DIP logo, for `chars` characters: centred on the
// logo's lower-right corner, then moved by the badge offsets. A `hole` radius around the logo's
// centre (rings) pulls it in along the diagonal until the box fits inside it, at most to the
// centre.
static BadgeBox LogoBadgeBox(const MeterDisplay& display, double side, int chars, double hole = 0) {
    double width = std::max(chars, 1) * display.badgeSize * 0.6 + 2 * kBadgeOutline;
    double height = display.badgeSize * 0.7 + 2 * kBadgeOutline;
    double along = side / 2;  // From the logo's centre, on both axes.
    if (hole > 0) {
        // The far corner (along + width / 2, along + height / 2) at `hole` from the centre.
        double a = (width + height) / 2;
        double disc = a * a - 2 * (width * width / 4 + height * height / 4 - hole * hole);
        along = std::clamp(disc >= 0 ? (std::sqrt(disc) - a) / 2 : 0.0, 0.0, along);
    }
    double x = side / 2 + along + display.badgeOffsetX, y = side / 2 + along + display.badgeOffsetY;
    return {x - width / 2, y - height / 2, width, height};
}

// The mark beside an account's identity (a logo's or text's top-right corner, a ring tile's
// corner, after a Label identity): the stale "!", or a provider outage's filled warning triangle
// (kOutageGlyph in Segoe Fluent Icons), which takes its place. The outage mark is
// kOutageMarkShare of a logo's side (a text identity's: its logo's), clamped to kOutageMarkMin-
// kOutageMarkMax DIPs, its box kOutageMarkRight of its size past the identity's right edge and
// kOutageMarkTop above its top; on ring tiles kOutageRingMarkShare of the ring, clamped likewise,
// where the "!" goes (AccountMeters::markSize). Under it, the same glyph kOutageHaloShare of its size
// larger on each side, in the taskbar's color (TilePalette::badgeHalo), so it stands clear of a
// logo. After a label and a thin space: the glyph at the label's font size.
enum class IdentityMark : uint8_t {
    None,
    Stale,
    Outage,
};
static constexpr PCWSTR kOutageGlyph = L"\uE814";
static constexpr double kOutageMarkShare = 0.75, kOutageMarkMin = 7, kOutageMarkMax = 14;
static constexpr double kOutageRingMarkShare = 1.0 / 3, kOutageRingMarkMin = 8, kOutageRingMarkMax = 13;
static constexpr double kOutageMarkRight = 4.0 / 9, kOutageMarkTop = 3.0 / 9, kOutageHaloShare = 1.0 / 9;

// The outage mark's size beside a `side` DIP logo.
static double OutageMarkSize(double side) {
    return std::clamp(side * kOutageMarkShare, kOutageMarkMin, kOutageMarkMax);
}

// A bar column's identity margins in DIPs, which make its box: the identity gap (`gap`; 0
// without bars) on the side facing the bars. A Label identity beside them sits 2 DIPs high. A
// `side` DIP logo's box takes the overhangs of its badge (`badge`, from the logo's top-left) and
// its mark (the stale "!" 4 DIPs right and 3 above, the outage mark its own) while they show, and
// above or below the bars the label line's space beyond its text at the label font size
// (`aboveCap`, over its cap height; `belowBaseline`), so the logo keeps a label's distance from
// them. Across the bars (vertically
// beside them, horizontally above or below them) an overhang counts on both sides, so the logo
// stays centred on the bars wherever its badge sits.
struct BarIdentityMargin {
    double left = 0, top = 0, right = 0, bottom = 0;
};

// Where bars show their identity: the identity position, except on a side taskbar, where Left and
// Right show it at the top.
static LabelPosition BarIdentityPositionFor(LabelPosition position, const MeterDisplay& display) {
    bool beside = position == LabelPosition::Left || position == LabelPosition::Right;
    return display.sideWidth > 0 && beside ? LabelPosition::Top : position;
}

static BarIdentityMargin BarIdentityMarginFor(LabelPosition position, bool label, double side, BadgeBox badge,
                                              IdentityMark mark, double gap, double aboveCap, double belowBaseline) {
    BarIdentityMargin m;
    bool across = position == LabelPosition::Top || position == LabelPosition::Bottom;
    if (label) {
        m.top = across ? 0 : -2;
    } else {
        bool shown = badge.width > 0;
        bool outage = mark == IdentityMark::Outage;
        double markTop = outage ? kOutageMarkTop * OutageMarkSize(side) : mark == IdentityMark::Stale ? 3 : 0;
        double markRight = outage ? kOutageMarkRight * OutageMarkSize(side) : mark == IdentityMark::Stale ? 4 : 0;
        m.left = shown ? std::max(-badge.left, 0.0) : 0;
        m.top = std::max({markTop, shown ? -badge.top : 0.0});
        m.right = std::max({markRight, shown ? badge.left + badge.width - side : 0.0});
        m.bottom = shown ? std::max(badge.top + badge.height - side, 0.0) : 0;
        if (across) {
            m.left = m.right = std::max(m.left, m.right);
            m.top += aboveCap;
            m.bottom += belowBaseline;
        } else {
            m.top = m.bottom = std::max(m.top, m.bottom);
        }
    }
    if (position == LabelPosition::Left) {
        m.right += gap;
    } else if (position == LabelPosition::Right) {
        m.left += gap;
    } else if (position == LabelPosition::Top) {
        m.bottom += gap;
    } else if (position == LabelPosition::Bottom) {
        m.top += gap;
    }
    return m;
}

// One quota slot's meter. Bar fields cover every slot of a bar layout; tile fields cover a tile
// layout's slots in the mask.
struct QuotaMeter {
    uint32_t color = 0;        // Usage color: follows used % in either bar mode.
    bool paceVisible = false;  // Pace mark shown, in layouts that draw one.
    std::wstring key;          // Window key (QuotaKey).

    // The fill's corner radius toward 100%, in bars, headline segments and ring arcs;
    // its start corners are the track's. Over a full track the fill ends in a straight cut until
    // it comes within a corner's radius of the track's end.
    double fillEndRadius = 0;

    // Bars, in DIPs along the bar.
    int fillPx = 0;
    int pacePx = 0;            // Pace tick's start; the time track's fill length.
    double paceEndRadius = 0;  // The time track fill's corner radius toward 100%.
    PercentTextAlignment textAlignment = PercentTextAlignment::Right;  // Never Adaptive.
    double textOffset = 0;     // Percent text shift.
    bool darkText = false;     // Black percent text over a light fill (yellow and orange by default).
    // Percent or amount; empty while unavailable. Also the quota matrix's number, "–" while
    // unavailable.
    std::wstring text;

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
    // Quota matrix: the cell, down the first column, then the second from the third quota on (on a
    // side taskbar, all down the first).
    int matrixRow = 0, matrixColumn = 0;
    // Sparklines: the quota's chart, `sparkLeft` DIPs from the face's left and `sparkTop` from its
    // top, its shapes (AccountMeters describes them), and its key in the plot's top-right corner
    // instead of its bottom-right one.
    double sparkLeft = 0, sparkTop = 0;
    std::array<TileOutline, kSparkShapeCount> spark;
    bool sparkKeyTop = false;
};

struct AccountMeters {
    bool stale = false;
    bool warn = false;         // Stale with an error: the details flyout's stale marker.
    IdentityMark mark = IdentityMark::None;
    // The outage mark's color (the usage palette's orange, or its red for a major outage) and size
    // in DIPs; 0 without one.
    uint32_t markColor = 0;
    double markSize = 0;
    int barMask = 0;           // Quota slots shown.
    std::array<QuotaMeter, kQuotaBarCount> quotas;
    std::wstring labelText;    // Bars' Label identity; the outage glyph follows it.
    double labelOpacity = 0;
    double identityOpacity = 0;  // Tiles' identity, and bars' logo identity.
    // Ring tiles: the badge's box from the ring area's top-left, the identity text's fitted font
    // size and width, and the warning mark's top margin, into the tile's padding.
    TileRingGeometry ring;
    BadgeBox ringBadge;
    double ringTextFont = 0;
    double ringTextWidth = 0;
    double ringWarnTop = 0;
    // Face tiles (IsFaceTile): the face's size, holding the meter shapes. Gauges also put their
    // number and key in it: fonts, and cap height centres from the face's top.
    double faceWidth = 0, faceHeight = 0;
    double faceFont = 0, faceKeyFont = 0;
    double faceNumberCenter = 0, faceKeyCenter = 0;
    // Logo + headline tiles and gauges: the most-used quota's number (gauges: without "%"), window
    // key and color.
    std::wstring headline;
    std::wstring headlineKey;
    uint32_t headlineColor = 0;
    // Logo + headline tiles with the countdown: the time until that quota resets.
    std::wstring headlineCountdown;
    // Logo + headline tiles: each segment's length, the headline bar length, shortened on a side
    // taskbar until the row of segments and their gaps fits its width.
    double headlineBarLength = 0;
    // Sparklines: a chart per quota in the mask (QuotaMeter::spark), display.sparklineWidth by
    // sparkChartHeight (the tile's height at the chart height), side by side in slot order in the
    // face, or on a side taskbar one under another, kSparkChartGap apart. A chart plots used % across the window, or % left in remaining mode: the lit
    // part up to now (or the whole plot), the recorded usage, the even-pace diagonal (with pace
    // markers on), the current time (line and caret, each with its cue on) and reading (ringed or
    // plain), and the projection (ProjectUsage) from the current reading to the reset or, reaching
    // 100% before it, to the limit, marked in the 100% color. Without a window the plot is
    // lit whole and the current reading sits at its right edge, alone. With
    // display.sparklineAroundMs, a window longer than twice it spans only that much each side of
    // now, kept inside the window: earlier readings drop out, the diagonal is the
    // window's even use across that span, and the projection stops at its end (with the limit mark
    // only when it reaches the limit before then).
    uint32_t sparkLimitColor = 0;
    double sparkChartHeight = 0;
    double sparkKeyFont = 0;  // The key and extra usage text, display.sparklineTextSize.
    // The weekly chart's top-left corner (bottom-left with sparkExtraBottom), at the key's font:
    // the account's selected extra usage per display.sparklineExtra, its key (empty for dollars)
    // and number, in extra usage's usage color (0: the taskbar's text color). Empty without.
    std::wstring sparkExtraKey, sparkExtraText;
    uint32_t sparkExtraColor = 0;
    bool sparkExtraBottom = false;
    // Quota matrix: the numbers' and keys' font sizes, two rows shrunk to fit the tile (on a side
    // taskbar, one column of rows at the number size).
    double matrixFont = 0, matrixKeyFont = 0;
};

// A quota's recorded usage for a sparkline: its slot and reading. Empty without history.
using TileTraceSource = std::function<std::vector<TileTracePoint>(int slot, const WindowUsage& reading)>;

// `layout`: the layout the meters are drawn in. `badgeChars`: the tile badge's text length, 0
// without a badge. `textWidthPerPt`: the tile identity text's width per DIP of font size, 0
// without text. `traceOf`: a sparkline's recorded usage, asked for its charted quotas only.
// `outage`: the level the account's provider shows (none while its marks are off).
static AccountMeters AccountMeterState(const MeterDisplay& display, BarLayout layout,
                                       const AccountConfig& account, const AccountData& data,
                                       int badgeChars, double textWidthPerPt,
                                       const TileTraceSource& traceOf = {},
                                       OutageLevel outage = OutageLevel::None) {
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
    // An outage explains the staleness: its mark takes the stale mark's place.
    if (outage != OutageLevel::None) {
        meters.mark = IdentityMark::Outage;
        meters.markColor = display.usageColors[outage == OutageLevel::Major ? 3 : 2];
    } else if (meters.warn) {
        meters.mark = IdentityMark::Stale;
    }
    meters.labelText = meters.mark == IdentityMark::Stale  ? account.label + L"!" :
                       meters.mark == IdentityMark::Outage ? account.label + L"\u2009" : account.label;
    meters.labelOpacity = meters.stale ? 0.45 : 0.8;
    meters.identityOpacity = meters.stale ? 0.45 : 1.0;
    // The selected quotas with a reading; none before the first success, so a loading or signed-out
    // account shows only its identity.
    int count = 0;
    for (int w = 0; w < kQuotaBarCount; w++) {
        if (account.showBars[w] && usage[w]->pct >= 0) {
            meters.barMask |= 1 << w;
            count++;
        }
    }

    bool tiles = IsTileLayout(layout);
    bool concentric = layout == BarLayout::ConcentricRings;
    bool gauge = layout == BarLayout::Gauge, pies = layout == BarLayout::ClockPies;
    bool square = layout == BarLayout::RoundedSquare;
    bool spark = layout == BarLayout::Sparkline, matrix = layout == BarLayout::QuotaMatrix;
    // Arcs of circles: the rings and the gauge.
    bool circular = concentric || layout == BarLayout::SegmentedRing || gauge;
    bool ring = IsRingTile(layout);
    bool paceMarks = display.showPaceTicks && layout != BarLayout::LogoHeadline && !matrix;
    auto timed = [&](int w) {
        return usage[w]->windowDurationMs > 0 && usage[w]->resetUnixMs > usage[w]->windowDurationMs;
    };
    // The most-used tile quota: the headline's, and the only one a gauge shows.
    int top = -1;
    for (int w = 0; w < kQuotaBarCount; w++) {
        if (!(meters.barMask & (1 << w))) continue;
        if (top < 0 || usage[w]->pct > usage[top]->pct) top = w;
    }
    if (gauge) {
        meters.barMask = top >= 0 ? 1 << top : 0;
        count = meters.barMask ? 1 : 0;
    }
    if (spark) {
        // A chart per quota with a window; without any, the most-used quota's. Anthropic's extra
        // usage and OpenAI's credits go in the weekly chart's corner, instead of a chart of its
        // own, while selected and reported; credits have no window, so never get a chart.
        int charted = 0;
        for (int w = 0; w < kQuotaBarCount; w++) {
            if ((meters.barMask & (1 << w)) && timed(w)) charted |= 1 << w;
        }
        bool openAi = account.provider == L"openai";
        const WindowUsage& extra = data.extraUsage;
        bool extraReported = extra.pct >= 0 || data.extraUsedAmount >= 0 || (openAi && data.creditsBalance >= 0);
        if (display.sparklineExtra != SparklineExtra::Off && account.showBars[kExtraUsageBar] &&
            (openAi || account.provider == L"anthropic") && (charted & (1 << kWeeklyBar)) && extraReported) {
            charted &= ~(1 << kExtraUsageBar);
            // Percent of the limit, else the amount: dollars, or credits spent against the max or
            // the balance left; a percentage when it's all there is. Spent in used mode, left in
            // remaining mode (without a limit: spent dollars, the credits balance).
            bool dollars = false;
            bool amounts = data.extraUsedAmount >= 0 || data.extraLimitAmount >= 0;
            bool balance = openAi && data.creditsBalance >= 0;
            if (extra.pct >= 0 && (display.sparklineExtra == SparklineExtra::Percent || (!amounts && !balance))) {
                wchar_t text[32];
                swprintf(text, ARRAYSIZE(text), L"%.0f%%",
                         remainingMode ? std::clamp(100.0 - extra.pct, 0.0, 100.0) : extra.pct);
                meters.sparkExtraText = text;
            } else if (amounts) {
                double amount = remainingMode && data.extraLimitAmount >= 0 ?
                                    data.extraLimitAmount - data.extraUsedAmount : data.extraUsedAmount;
                dollars = account.provider == L"anthropic";
                meters.sparkExtraText = TaskbarAmountText(std::max(amount, 0.0), dollars);
            } else {
                meters.sparkExtraText = TaskbarAmountText(data.creditsBalance, false);
            }
            if (!dollars) meters.sparkExtraKey = QuotaKey(account.provider, kExtraUsageBar);
            meters.sparkExtraColor = extra.pct >= 0 || meters.stale ?
                UsageColor(extra.pct, meters.stale, yellow, orange, red, display.usageColors) : 0;
        }
        meters.barMask = charted ? charted : top >= 0 ? 1 << top : 0;
        count = std::popcount(unsigned(meters.barMask));
        meters.sparkChartHeight = std::round(display.tileHeight * display.sparklineHeight / 100.0);
        if (display.sideWidth > 0) {
            meters.faceWidth = count ? display.sparklineWidth : 0;
            meters.faceHeight = count * meters.sparkChartHeight + std::max(count - 1, 0) * kSparkChartGap;
        } else {
            meters.faceWidth = count * display.sparklineWidth + std::max(count - 1, 0) * kSparkChartGap;
            meters.faceHeight = meters.sparkChartHeight;
        }
        meters.sparkKeyFont = display.sparklineTextSize;
        meters.sparkLimitColor = UsageColor(100, false, yellow, orange, red, display.usageColors);
    }
    if (matrix) {
        // Two rows' cap heights and their gap fit the tile, 2 DIPs clear above and below.
        double font = display.headlineFontSize;
        if (count > 1 && display.sideWidth <= 0) {
            font = std::min(font, (display.tileHeight - 4 - kMatrixRowGap) / (2 * kTileCapShare));
        }
        meters.matrixFont = std::max(font, 7.0);
        meters.matrixKeyFont = meters.matrixFont * kHeadlineKeyScale;
    }
    meters.headlineBarLength = display.headlineBarLength;
    if (layout == BarLayout::LogoHeadline && display.sideWidth > 0 && count > 0) {
        double room = std::floor(display.sideWidth - 2 * kSideInset) - (count - 1) * display.barGap;
        meters.headlineBarLength =
            std::clamp(std::floor(room / count), double(kHeadlineBarLengthMin), double(display.headlineBarLength));
    }
    // Pies: one per quota in slot order, sized as a pair up to two, in columns of two from the
    // third on.
    double pieDiameter = 0, pieGap = 0;
    // The rounded square's centreline corner radius and perimeter.
    double squareCorner = 0, squarePerimeter = 0;
    if (IsFaceTile(layout)) {
        double size = std::min<double>(display.ringSize, display.tileHeight);
        if (pies) {
            meters.ring.size = size;
            pieGap = count > 2 ? 2 : 3;
            pieDiameter = count > 2 ? (size - pieGap) / 2 : size * 0.5625;
            int columns = count > 2 ? 2 : count, rows = count > 2 ? 2 : 1;
            meters.faceWidth = count ? columns * pieDiameter + (columns - 1) * pieGap : 0;
            meters.faceHeight = count ? rows * pieDiameter + (rows - 1) * pieGap : 0;
        } else {
            meters.ring = TileRingLayout(concentric, size, display.ringThickness, count, display.logoScale);
            meters.faceWidth = meters.faceHeight = size;
        }
    }
    if (square) {
        const TileRingGeometry& g = meters.ring;
        squareCorner = std::clamp(g.size * kTileSquareCornerShare, g.stroke / 2, g.radius);
        squarePerimeter = 8 * (g.radius - squareCorner) + 2 * std::numbers::pi * squareCorner;
    }
    if (gauge) {
        // Three digits fit across the hole; the key sits in the opening, between the arc's ends.
        const TileRingGeometry& g = meters.ring;
        meters.faceFont = std::clamp(std::min(g.size * 0.375, (2 * g.hole - 2) / 1.75), 7.0, 16.0);
        meters.faceKeyFont = std::max(6.0, meters.faceFont * kHeadlineKeyScale);
        meters.faceNumberCenter = g.size / 2 - 0.5;
        meters.faceKeyCenter = g.size / 2 + g.radius * 0.75;
    }
    if (ring) {
        if (badgeChars) {
            // Kept half a DIP inside the hole by default.
            const TileRingGeometry& g = meters.ring;
            meters.ringBadge = LogoBadgeBox(display, g.logo, badgeChars, g.hole - 0.5);
            meters.ringBadge.left += (g.size - g.logo) / 2;
            meters.ringBadge.top += (g.size - g.logo) / 2;
        }
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
    if (meters.mark == IdentityMark::Outage) {
        meters.markSize = ring ? std::clamp(meters.ring.size * kOutageRingMarkShare, kOutageRingMarkMin, kOutageRingMarkMax) :
                                 OutageMarkSize((tiles ? kHeadlineLogoSide : kBarLogoSide) * display.logoScale);
    }
    // Tiles fill per the bar mode, clamped to the meter; unavailable quotas stay empty.
    auto tileShown = [&](double pct) {
        if (pct < 0) return 0.0;
        return std::clamp(remainingMode ? 100.0 - pct : pct, 0.0, 100.0);
    };

    // A pace tick: from `from` to `to` DIPs along the unit direction (dx, dy) from (x, y),
    // `width` across.
    auto tick = [](double x, double y, double dx, double dy, double from, double to, double width) {
        double acrossX = -dy * width / 2, acrossY = dx * width / 2;
        auto point = [&](double distance, double side) {
            return TileOutlineSegment{x + dx * distance + side * acrossX, y + dy * distance + side * acrossY};
        };
        TileOutlineSegment start = point(from, 1);
        return TileOutline{{start.x, start.y, {point(to, 1), point(to, -1), point(from, -1)}}};
    };

    BarGeometry bar = BarGeometryFor(display);
    int position = 0;
    for (int w = 0; w < kQuotaBarCount; w++) {
        const WindowUsage& wu = *usage[w];
        QuotaMeter& m = meters.quotas[w];
        // Color stays keyed to actual usage so depleting quota still reds out.
        m.color = UsageColor(wu.pct, meters.stale, yellow, orange, red, display.usageColors);
        m.key = QuotaKey(account.provider, w);
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
                double pacePct = remainingMode ? remainingFraction * 100.0 :
                                                 (1.0 - remainingFraction) * 100.0;
                pacePct = std::clamp(pacePct, 0.0, 100.0);
                if (display.paceTickStyle == PaceTickStyle::TimeTrack) {
                    // From the bar's start to the pace; hidden until it reaches a DIP.
                    m.pacePx = (int)std::lround(display.barLength * pacePct / 100.0);
                    m.paceVisible = m.pacePx > 0;
                    m.paceEndRadius = std::max(0.0, bar.timeThickness / 2 - (display.barLength - m.pacePx));
                } else {
                    // Centred on the pace, kept inside the bar's rounded ends.
                    double radius = std::clamp(bar.radius, bar.paceLength / 2.0,
                                               (display.barLength - bar.paceLength) / 2.0);
                    double center = std::clamp(display.barLength * pacePct / 100.0,
                                               radius, display.barLength - radius);
                    m.pacePx = (int)std::lround(center - bar.paceLength / 2.0);
                }
            }
            // Adaptive text sits over the fill from the yellow threshold on, else over the track.
            m.textAlignment = display.percentTextAlignment == PercentTextAlignment::Adaptive ?
                (displayed >= yellow ? PercentTextAlignment::Left : PercentTextAlignment::Right) :
                display.percentTextAlignment;
            m.textOffset = m.textAlignment == PercentTextAlignment::Left ? bar.textInset :
                           m.textAlignment == PercentTextAlignment::Center ? 0 : -bar.textInset;
            // Light fills, above a third of white's luminance, take black text: the default
            // palette's yellow and orange, the colorblind one's sky blue and orange, and custom
            // colors as light as those. Stale grey keeps white text.
            m.darkText = m.textAlignment == PercentTextAlignment::Left && !meters.stale && wu.pct >= 0 &&
                         RelativeLuminance(m.color) > 1.0 / 3;
            if (wu.pct >= 0) {
                if (w == kExtraUsageBar && display.showExtraBarAmounts &&
                    data.extraLimitAmount >= 0) {
                    // Amount follows the bar mode: spent in used mode, left in remaining mode.
                    // Dollars for Anthropic, credits otherwise (whole ones on the taskbar).
                    double amount = remainingMode ? data.extraLimitAmount - data.extraUsedAmount :
                                                    data.extraUsedAmount;
                    m.text = TaskbarAmountText(std::max(amount, 0.0), account.provider == L"anthropic");
                } else {
                    wchar_t text[24];
                    swprintf(text, ARRAYSIZE(text), L"%.0f%%", displayed);
                    m.text = text;
                }
            }
            continue;
        }

        if (!(meters.barMask & (1 << w))) continue;
        double shown = tileShown(wu.pct);
        double fill = shown / 100;
        const TileRingGeometry& geometry = meters.ring;
        bool fullRing = false;
        if (circular || square) {
            // The square's arcs are its segmented ring's, in degrees of its perimeter from the
            // top edge's middle (270 degrees).
            TileArc slot = gauge ? kGaugeArc : concentric ? kConcentricArc : kSegmentedArcs[count - 1][position];
            m.radius = geometry.radius -
                       (concentric ? position * (geometry.stroke + geometry.gap) : 0.0);
            // Partial ring and square arcs reach half a stroke past their slot at both ends,
            // along the centreline; a gauge's is drawn as it is.
            fullRing = std::abs(slot.sweep) >= 360;
            double centreline = square ? squarePerimeter : 2 * std::numbers::pi * m.radius;
            double grow = fullRing || gauge ? 0.0 : geometry.stroke / 2 / centreline * 360;
            double direction = slot.sweep < 0 ? -1 : 1;
            m.arc = {slot.start - direction * grow, slot.sweep + 2 * direction * grow};
        }
        if (pies) m.arc = {270, 360};
        // Any usage fills at least 5 degrees.
        if (shown > 0 && m.arc.sweep) fill = std::max(fill, 5.0 / std::abs(m.arc.sweep));
        int slotPosition = position++;
        m.fillUnits = shown > 0 ? std::max(1, (int)std::lround(fill * 1000)) : 0;
        if (m.paceVisible) {
            double pace = remainingMode ? remainingFraction : 1.0 - remainingFraction;
            m.paceUnits = (int)std::lround(std::clamp(pace, 0.0, 1.0) * 1000);
        }
        double pace = m.paceUnits / 1000.0;
        if (square) {
            // Bands along the centreline, cut square; a fill from its arc's start. The tick
            // crosses the band along its normal, kept clear of a partial arc's ends.
            double center = geometry.size / 2;
            auto along = [&](double degrees) { return (degrees - 270) / 360 * squarePerimeter; };
            double end = m.arc.start + m.arc.sweep;
            double low = std::min(m.arc.start, end), high = std::max(m.arc.start, end);
            m.track = TileSquareBandOutline(center, geometry.radius, squareCorner, geometry.stroke, along(low),
                                            along(high));
            m.fillSweep = m.arc.sweep * fill;
            if (m.fillUnits) {
                double fillEnd = m.arc.start + m.fillSweep;
                m.fill = TileSquareBandOutline(center, geometry.radius, squareCorner, geometry.stroke,
                                               along(std::min(m.arc.start, fillEnd)),
                                               along(std::max(m.arc.start, fillEnd)));
            }
            m.paceAngle = m.arc.start + m.arc.sweep * pace;
            if (m.paceVisible) {
                if (!fullRing) {
                    double pad = kTilePaceHalo / 2 / squarePerimeter * 360;
                    m.paceAngle = std::max(low + pad, std::min(m.paceAngle, high - pad));
                }
                TileSquarePoint p = TileSquareAt(center, geometry.radius, squareCorner, along(m.paceAngle), 0);
                double reach = geometry.stroke / 2 + kTilePaceOverhang;
                m.paceHalo = tick(p.x, p.y, p.normalX, p.normalY, -reach, reach, kTilePaceHalo);
                m.paceCore = tick(p.x, p.y, p.normalX, p.normalY, -reach + kTilePaceCoreInset,
                                  reach - kTilePaceCoreInset, kTilePaceCore);
            }
        } else if (pies) {
            // Clockwise from the top: a disc, the fill a wedge, the hand from the centre to just
            // past the rim. Arcs in halves, so none turns more than 180 degrees.
            bool columns = count > 2;
            double r = pieDiameter / 2;
            double cx = (columns ? slotPosition / 2 : slotPosition) * (pieDiameter + pieGap) + r;
            double cy = (columns ? slotPosition % 2 : 0) * (pieDiameter + pieGap) + r;
            auto at = [&](double degrees) {
                double radians = degrees * std::numbers::pi / 180;
                return TileOutlineSegment{cx + r * std::cos(radians), cy + r * std::sin(radians), r, true};
            };
            auto pie = [&](double sweep) {
                TileOutlineSegment start = at(270);
                if (sweep >= 360) return TileOutline{{start.x, start.y, {at(90), at(270)}}};
                return TileOutline{{cx, cy, {{start.x, start.y}, at(270 + sweep / 2), at(270 + sweep)}}};
            };
            m.radius = r;
            m.track = pie(360);
            m.fillSweep = 360 * fill;
            if (m.fillUnits) m.fill = pie(m.fillSweep);
            m.paceAngle = 270 + 360 * pace;
            if (m.paceVisible) {
                double radians = m.paceAngle * std::numbers::pi / 180;
                double dx = std::cos(radians), dy = std::sin(radians);
                m.paceHalo = tick(cx, cy, dx, dy, 0, r + 1, 3);
                m.paceCore = tick(cx, cy, dx, dy, 0, r + 0.5, 1.5);
            }
        } else if (circular) {
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
                // A radial tick, kept clear of a partial arc's corners. It reaches past the ring
                // by the overhang, at most half the gap toward a neighbouring ring, and the core
                // stops short of the halo by its inset.
                if (!fullRing) {
                    double pad = (corner + kTilePaceHalo / 2) / m.radius * 180 / std::numbers::pi;
                    m.paceAngle = std::max(low + pad, std::min(m.paceAngle, high - pad));
                }
                double between = std::min(kTilePaceOverhang, geometry.gap / 2);
                double outside = concentric && slotPosition > 0 ? between : kTilePaceOverhang;
                double inside = concentric && slotPosition < count - 1 ? between : kTilePaceOverhang;
                double radians = m.paceAngle * std::numbers::pi / 180;
                double dx = std::cos(radians), dy = std::sin(radians);
                m.paceHalo = tick(center, center, dx, dy, inner - inside, outer + outside, kTilePaceHalo);
                m.paceCore = tick(center, center, dx, dy, inner - inside + kTilePaceCoreInset,
                                  outer + outside - kTilePaceCoreInset, kTilePaceCore);
            }
        } else if (spark) {
            // Left to right the window (or the day each side of now), bottom to top 0-100% inside
            // the insets, per the bar mode: used, or left (100 - used); positions snap to quarter
            // DIPs, so they change only when the drawing does.
            double width = display.sparklineWidth, height = meters.sparkChartHeight;
            if (display.sideWidth > 0) {
                m.sparkTop = slotPosition * (height + kSparkChartGap);
            } else {
                m.sparkLeft = slotPosition * (width + kSparkChartGap);
            }
            auto snap = [](double dip) { return std::round(dip * 4) / 4; };
            std::vector<TileTracePoint> trace = timed(w) && traceOf ? traceOf(w, wu) : std::vector<TileTracePoint>{};
            // A rolled-back clock can put now before the newest reading: the time, the current
            // reading and the projection take the later of the two.
            ULONGLONG now = trace.empty() ? display.now : std::max(display.now, trace.back().timeMs);
            // The span drawn: the window, or a long window's day each side of now, kept inside the
            // window.
            ULONGLONG windowStart = timed(w) ? wu.resetUnixMs - wu.windowDurationMs : 0;
            ULONGLONG start = windowStart, span = wu.windowDurationMs;
            const ULONGLONG around = display.sparklineAroundMs;
            if (timed(w) && around && span > 2 * around) {
                start = std::min(now > windowStart + around ? now - around : windowStart, wu.resetUnixMs - 2 * around);
                span = 2 * around;
            }
            auto xAt = [&](ULONGLONG timeMs) {
                return snap(std::clamp(((double)timeMs - (double)start) / (double)span, 0.0, 1.0) * width);
            };
            auto yAt = [&](double used) {
                double shown = std::clamp(remainingMode ? 100 - used : used, 0.0, 100.0);
                return snap(height - kSparkInset - (height - 2 * kSparkInset) * shown / 100);
            };
            // Clockwise from its top, or counterclockwise to cut a hole in a nonzero fill.
            auto circle = [](double x, double y, double r, bool clockwise = true) {
                return TileOutlineFigure{x, y - r, {{x, y + r, r, clockwise}, {x, y - r, r, clockwise}}};
            };
            std::array<TileOutline, kSparkShapeCount>& shapes = m.spark;
            // The outline: the plot's edge, drawn half its stroke in.
            if (display.sparklineOutline) {
                double o = kSparkOutlineStroke / 2, r = kSparkPlotRadius - o, right = width - o, bottom = height - o;
                shapes[kSparkOutline] = {{o + r, o, {{right - r, o}, {right, o + r, r}, {right, bottom - r},
                                                     {right - r, bottom, r}, {o + r, bottom}, {o, bottom - r, r},
                                                     {o, o + r}, {o + r, o, r}}}};
            }
            // The current reading: the dot in its ring, both cut out of the marks under them, or a
            // plain dot, at the reading size; `mark` DIPs around its centre.
            const bool ring = display.sparklineRing;
            const double scale = display.sparklineReadingScale;
            const double mark = (ring ? kSparkRingOuter : kSparkPlainNowRadius) * scale;
            std::optional<std::pair<double, double>> readingAt;
            std::optional<double> nowX;
            auto reading = [&](double x, double y) {
                readingAt = {x, y};
                if (ring) {
                    shapes[kSparkNowCut] = {circle(x, y, mark)};
                    shapes[kSparkNowRing] = {circle(x, y, mark), circle(x, y, kSparkRingInner * scale, false)};
                }
                shapes[kSparkNow] = {circle(x, y, (ring ? kSparkNowRadius : kSparkPlainNowRadius) * scale)};
            };
            // The plot's rounded rectangle up to `lit` DIPs across, clockwise from its top edge;
            // a cut through a corner meets its arc.
            auto litPlot = [&](double lit) -> TileOutline {
                double r = kSparkPlotRadius;
                if (lit <= 0) return {};
                lit = std::min(lit, width);
                auto cornerY = [&](double fromSide) {
                    return r - std::sqrt(std::max(2 * r * fromSide - fromSide * fromSide, 0.0));
                };
                TileOutlineSegment left{0, height - r, r}, top{0, r};
                if (lit < r) {
                    double y = cornerY(lit);
                    return {{lit, y, {{lit, height - y}, left, top, {lit, y, r}}}};
                }
                TileOutlineFigure figure{r, 0, {}};
                if (lit <= width - r) {
                    figure.segments = {{lit, 0}, {lit, height}};
                } else {
                    double y = cornerY(width - lit);
                    figure.segments = {{width - r, 0}, {lit, y, r}, {lit, height - y}, {width - r, height, r}};
                }
                figure.segments.insert(figure.segments.end(), {{r, height}, left, top, {r, 0, r}});
                return {std::move(figure)};
            };
            if (timed(w)) {
                // The window's even use across the span: 0% to 100% across the whole window.
                if (display.showPaceTicks) {
                    double from = 100.0 * double(start - windowStart) / double(wu.windowDurationMs);
                    double to = 100.0 * double(start + span - windowStart) / double(wu.windowDurationMs);
                    shapes[kSparkPace] = {{0, yAt(from), {{width, yAt(to)}}}};
                }
                // Readings before a span around now drop out; a run across its start begins there.
                std::vector<TileTracePoint> drawn = trace;
                if (start > windowStart) {
                    auto first = std::lower_bound(drawn.begin(), drawn.end(), start,
                                                  [](const TileTracePoint& point, ULONGLONG time) { return point.timeMs < time; });
                    if (first != drawn.begin() && first != drawn.end() && first->connect && first->timeMs > start) {
                        const TileTracePoint &before = first[-1], &after = *first;
                        first[-1] = {start,
                                     before.used + (after.used - before.used) * double(start - before.timeMs) /
                                                       double(after.timeMs - before.timeMs),
                                     false};
                        --first;
                    }
                    drawn.erase(drawn.begin(), first);
                }
                for (size_t begin = 0, end; begin < drawn.size(); begin = end) {
                    TileOutlineFigure line{xAt(drawn[begin].timeMs), yAt(drawn[begin].used), {}};
                    for (end = begin + 1; end < drawn.size() && drawn[end].connect; end++) {
                        line.segments.push_back({xAt(drawn[end].timeMs), yAt(drawn[end].used)});
                    }
                    if (line.segments.empty()) {
                        shapes[kSparkDots].push_back(circle(line.x, line.y, kSparkDotRadius));
                        continue;
                    }
                    // Up from the plot's bottom under the run's start, along it, then back down.
                    TileOutlineFigure under{line.x, height, {{line.x, line.y}}};
                    under.segments.insert(under.segments.end(), line.segments.begin(), line.segments.end());
                    under.segments.push_back({line.segments.back().x, height});
                    shapes[kSparkArea].push_back(std::move(under));
                    shapes[kSparkLines].push_back(std::move(line));
                }
                // Now: the lit part's edge, a line across the plot and a caret on its bottom edge
                // (below, once the text is placed).
                double x = xAt(now);
                nowX = x;
                shapes[kSparkLit] = litPlot(display.sparklineElapsed ? x : width);
                if (wu.pct >= 0) {
                    double y = yAt(wu.pct);
                    reading(x, y);
                    // None for a window its provider reports at its limit, whatever its percentage.
                    std::optional<UsageProjection> projection;
                    if (!meters.stale && !data.opencodeLimited[w]) {
                        projection = ProjectUsage(wu, trace, display.now,
                                                  w == kExtraUsageBar ? 0 : kProjectionLookbackMs);
                    }
                    if (projection) {
                        // Cut at the span's end, before a later reset or limit.
                        ULONGLONG toMs = std::min(projection->toMs, start + span);
                        double toUsed = projection->toUsed;
                        if (toMs < projection->toMs) {
                            toUsed = wu.pct + (toUsed - wu.pct) * ((double)toMs - (double)projection->fromMs) /
                                                  ((double)projection->toMs - (double)projection->fromMs);
                        }
                        double endX = xAt(toMs), endY = yAt(toUsed);
                        shapes[kSparkProjection] = {{x, y, {{endX, endY}}}};
                        if (projection->limit && toMs == projection->toMs) {
                            shapes[kSparkLimit] = {circle(endX, endY, kSparkLimitRadius)};
                        }
                    }
                }
            } else {
                shapes[kSparkLit] = litPlot(width);
                if (wu.pct >= 0) reading(width - mark, yAt(wu.pct));
            }

            // Text, in boxes estimated from its length: the key at the right, the weekly chart's
            // extra usage at the left, on the top or bottom edge.
            struct TextBox {
                double left, right;
                bool top;
            };
            double cap = meters.sparkKeyFont * kTileCapShare, inset = kSparkTextInset;
            auto boxTop = [&](TextBox box) { return box.top ? inset : height - inset - cap; };
            double keyLeft = width - inset - m.key.size() * meters.sparkKeyFont * kSparkTextWidthShare;
            double extraRight = inset;
            if (w == kWeeklyBar && !meters.sparkExtraText.empty()) {
                double chars = double(meters.sparkExtraKey.size() + meters.sparkExtraText.size());
                extraRight += chars * meters.sparkKeyFont * kSparkTextWidthShare +
                              (meters.sparkExtraKey.empty() ? 0 : kSparkExtraKeyGap);
            }
            // Text moves to the plot's other edge, clear of the current reading's mark: the key
            // from the bottom-right corner to the top-right, the extra usage from the top-left to
            // the bottom-left. Remaining mode's line falls from the top-left, so each starts on the
            // other edge there. The placement the mark overlaps the fewest texts in wins, preferring
            // fewer moves (the key's first); texts on one edge never overlap.
            auto hit = [&](TextBox box) {
                if (!readingAt) return false;
                auto [x, y] = *readingAt;
                return box.right > box.left && x + mark > box.left && x - mark < box.right &&
                       y + mark > boxTop(box) && y - mark < boxTop(box) + cap;
            };
            int best = 0, bestHits = 3;
            for (int placement = 0; placement < 4; placement++) {
                bool keyMoved = placement & 1, extraMoved = placement & 2;
                TextBox key{keyLeft, width - inset, keyMoved != remainingMode};
                TextBox extra{inset, extraRight, extraMoved == remainingMode};
                if (key.top == extra.top && extra.right > extra.left && key.left < extra.right &&
                    extra.left < key.right) {
                    continue;
                }
                int hits = int(hit(key)) + int(hit(extra));
                if (hits < bestHits) best = placement, bestHits = hits;
            }
            m.sparkKeyTop = bool(best & 1) != remainingMode;
            if (extraRight > inset) meters.sparkExtraBottom = bool(best & 2) != remainingMode;
            // With their cues on: the now line, inside the plot's edges, stops a DIP short of the
            // text it crosses on either edge; the caret yields to text on the bottom edge.
            if (nowX) {
                double x = *nowX;
                double lineX = std::clamp(x, kSparkNowLineWidth / 2, width - kSparkNowLineWidth / 2);
                double lineLeft = lineX - kSparkNowLineWidth / 2, lineRight = lineX + kSparkNowLineWidth / 2;
                std::vector<TextBox> texts{{keyLeft, width - inset, m.sparkKeyTop}};
                if (extraRight > inset) texts.push_back({inset, extraRight, !meters.sparkExtraBottom});
                double from = 1, to = height - 1;
                bool caret = true;
                for (TextBox box : texts) {
                    if (box.left < x + kSparkCaretHalfWidth && x - kSparkCaretHalfWidth < box.right && !box.top) {
                        caret = false;
                    }
                    if (box.left >= lineRight || box.right <= lineLeft) continue;
                    if (box.top) {
                        from = std::max(from, boxTop(box) + cap + 1);
                    } else {
                        to = std::min(to, boxTop(box) - 1);
                    }
                }
                if (display.sparklineNowLine && to > from) {
                    shapes[kSparkNowLine] = {{lineLeft, from, {{lineRight, from}, {lineRight, to}, {lineLeft, to}}}};
                }
                if (display.sparklineNowCaret && caret) {
                    shapes[kSparkCaret] = {{x - kSparkCaretHalfWidth, height,
                                            {{x, height - kSparkCaretHeight}, {x + kSparkCaretHalfWidth, height}}}};
                }
            }
        } else if (matrix) {
            // Per the bar mode, as the bars' text: remaining clamped to 0-100%, used unclamped.
            bool column = display.sideWidth > 0;
            m.matrixRow = column ? slotPosition : slotPosition % 2;
            m.matrixColumn = column ? 0 : slotPosition / 2;
            m.text = L"\u2013";
            if (wu.pct >= 0) {
                wchar_t text[24];
                swprintf(text, ARRAYSIZE(text), L"%.0f%%",
                         remainingMode ? std::clamp(100.0 - wu.pct, 0.0, 100.0) : wu.pct);
                m.text = text;
            }
        } else {
            // Headline segments: corners a quarter of the thickness; the fill cut flat until the
            // track's end. Any usage fills at least 2 DIPs, as on the bars.
            double length = meters.headlineBarLength;
            m.fillLength = m.fillUnits ? std::clamp(length * fill, 2.0, length) : 0;
            m.fillEndRadius = std::max(0.0, display.headlineBarThickness / 4.0 - (length - m.fillLength));
        }
    }

    // Headline and gauge: the most-used tile quota, shown per the bar mode in its usage color.
    if (layout == BarLayout::LogoHeadline || gauge) {
        bool countdown = layout == BarLayout::LogoHeadline && display.headlineCountdown;
        meters.headline = L"\u2013";
        if (countdown) meters.headlineCountdown = std::wstring(kHeadlineCountdownArrow) + L"\u2013";
        if (top >= 0) {
            wchar_t text[24];
            swprintf(text, ARRAYSIZE(text), gauge ? L"%.0f" : L"%.0f%%", tileShown(usage[top]->pct));
            meters.headline = text;
            meters.headlineKey = meters.quotas[top].key;
        }
        if (countdown && top >= 0) {
            // Rounded up to the minute, like the details flyout's reset times.
            ULONGLONG reset = usage[top]->resetUnixMs;
            meters.headlineCountdown = kHeadlineCountdownArrow;
            if (!reset) {
                meters.headlineCountdown += L"no reset";
            } else if (reset <= display.now) {
                meters.headlineCountdown += L"now";
            } else {
                ULONGLONG minutes = (reset - display.now + 59999) / 60000;
                ULONGLONG days = minutes / (24 * 60), hours = minutes / 60 % 24;
                minutes %= 60;
                wchar_t text[24];
                if (days) {
                    swprintf(text, ARRAYSIZE(text), hours ? L"%llud %lluh" : L"%llud", days, hours);
                } else if (hours) {
                    swprintf(text, ARRAYSIZE(text), minutes ? L"%lluh %llum" : L"%lluh", hours, minutes);
                } else {
                    swprintf(text, ARRAYSIZE(text), L"%llum", minutes);
                }
                meters.headlineCountdown += text;
            }
        }
        meters.headlineColor = UsageColor(top >= 0 ? usage[top]->pct : -1, meters.stale, yellow,
                                          orange, red, display.usageColors);
    }
    return meters;
}

// A sparkline chart's shape color (SparkShape `shape` of slot `slot`'s chart) on a light (`light`)
// or dark taskbar: the theme's chrome, the pace guide's color, or the quota's usage color (the
// area translucent, the current reading's dot lifted in its ring), the limit mark the 100% color.
static uint32_t SparkShapeColor(int shape, const AccountMeters& meters, int slot, const MeterDisplay& display,
                                bool light) {
    const TilePalette palette = TilePaletteFor(light);
    const uint32_t usage = meters.quotas[slot].color;
    switch (shape) {
        case kSparkLit: return palette.lit;
        case kSparkOutline: return palette.outline;
        case kSparkPace: return (PaceMarkColorsFor(display.paceTickColor, light).core & 0x00FFFFFF) | kSparkGuideAlpha;
        case kSparkArea: return (usage & 0x00FFFFFF) | kSparkAreaAlpha;
        case kSparkNowLine: return palette.nowLine;
        case kSparkCaret:
        case kSparkNowRing: return palette.nowMark;
        case kSparkNowCut: return palette.badgeHalo;
        case kSparkNow: return display.sparklineRing ? UsageHighlight(usage, kSparkNowLift) : usage;
        case kSparkLimit: return meters.sparkLimitColor;
        default: return usage;
    }
}
