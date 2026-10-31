// Meter model: what every quota meter shows (fill, usage color, pace mark, text) and the tile,
// ring and pace tick geometry, as plain values from explicit inputs. Pure: no XAML, WinRT,
// window, registry or global state; colors are ARGB. Renderers own measuring, theme detection,
// fonts and applied-state caching.
//
// MeterDisplayFor is the entry. It clamps the display settings to their ranges, and every other
// function here relies on them (clamp bounds, ring and pace tick fit).

// Tile sizes, in DIPs.
static constexpr double kTileHeight = 40;  // Nominal; shorter taskbars get shorter tiles.
static constexpr double kMiniBarLength = 38;
static constexpr double kMiniBarThickness = 4;
static constexpr double kMiniBarGap = 3;
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

// Ring geometry. The centre keeps at least kTileRingHoleMin of radius so the identity always
// fits; strokes and gaps shrink instead, down to kTileRingStrokeMin.
static constexpr double kTileRingHoleMin = 7.0;
static constexpr double kTileRingStrokeMin = 0.75;
// Visible space between neighbouring segmented arcs, after their round caps.
static constexpr double kTileArcGapMin = 1.0;

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
    double dot = 0;          // Pace dot radius; its outline straddles it.
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
        // Round caps reach into the angular gaps between arcs.
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
    g.dot = std::clamp(stroke * 0.57, 1.15, 1.9);
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
    // One value per usage color band.
    const double green = display.yellowThreshold / 2.0;
    const double yellow = (display.yellowThreshold + display.orangeThreshold) / 2.0;
    const double orange = (display.orangeThreshold + display.redThreshold) / 2.0;
    const double red = (display.redThreshold + 100) / 2.0;
    const ULONGLONG now = display.now;
    // One to four taskbar quotas, each from slots its provider reports; two Anthropic accounts
    // share a provider so tiles show number badges. A negative value is a quota not reported.
    struct Sample {
        PCWSTR provider;
        PCWSTR label;
        std::array<double, kQuotaBarCount> pct;
        std::array<bool, kQuotaBarCount> showBars;
    };
    const std::array<Sample, 4> samples = {{
        {L"openai", L"OAI", {green, yellow, -1, green / 2}, {true, true, false, true}},
        {L"anthropic", L"CC 1", {orange, green, -1, -1}, {true, true, false, false}},
        {L"anthropic", L"CC 2", {green / 2, red, yellow, green}, {true, true, true, true}},
        {L"antigravity", L"Go", {green, green * 1.5, -1, -1}, {true, false, false, false}},
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
        if (account.provider == L"antigravity") {
            // Claude/GPT pools appear only in the details flyout.
            accountData.antigravityThirdParty5h = {
                yellow, now + (ULONGLONG)std::lround(kFiveHourWindowMs * 0.4), kFiveHourWindowMs};
            accountData.antigravityThirdPartyWeek = {
                green, now + (ULONGLONG)std::lround(kWeeklyWindowMs * 0.6), kWeeklyWindowMs};
        }
        accountData.plan = L"Visual test";
        accountData.lastSuccessMs = now;
        accountData.stale = false;
        data->push_back(std::move(accountData));
    }
}

// Bar layouts' pace tick and percent text placement, in DIPs along and across the bar. Pixel
// details scale with 1 / rasterizationScale.
struct BarGeometry {
    struct Point {
        double along, cross;
    };
    double paceLength;     // Pace tick outline.
    double paceCrossSize;
    double paceRadius;     // Corner radius; dots only.
    double coreLength;     // Line, notch and dot core, centred in the outline.
    double coreCrossSize;
    double coreRadius;     // Dots only.
    std::array<std::array<Point, 12>, 2> caret;  // Caret outline, then core polygon.
    double textInset;      // Percent text shift away from its aligned edge.
};

static BarGeometry BarGeometryFor(const MeterDisplay& display) {
    double pixel = 1.0 / display.rasterizationScale;
    PaceTickStyle style = display.paceTickStyle;
    BarGeometry g{};
    g.paceLength = (style == PaceTickStyle::Caret ? 5 : 4) * pixel;
    g.paceCrossSize = style == PaceTickStyle::Notch ?
                          std::max(2.0, std::ceil(display.barThickness * 0.6)) :
                      style == PaceTickStyle::Dot ? 4 * pixel : display.barThickness;
    g.paceRadius = style == PaceTickStyle::Dot ? 2 * pixel : 0;
    g.coreLength = (style == PaceTickStyle::Dot ? 3 : 2) * pixel;
    g.coreCrossSize = style == PaceTickStyle::Dot ? 3 * pixel : g.paceCrossSize;
    g.coreRadius = style == PaceTickStyle::Dot ? 1.5 * pixel : 0;
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

// A tile's identity text. Logo identities show a number badge when several configured accounts
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
    double fillLength = 0;     // Mini bar or headline segment fill.
    double paceOffset = 0;     // Mini bar pace tick's start.
    TileArc arc{};             // Ring arc and its centreline radius.
    double radius = 0;
    double fillSweep = 0;      // Ring fill, in degrees from the arc's start.
    double paceAngle = 0;      // Ring pace dot centre, in degrees.
};

struct AccountMeters {
    bool stale = false;
    bool warn = false;         // Stale with an error: the bars' "!" or the tiles' mark.
    int barMask = 0;           // Quota slots shown.
    std::array<QuotaMeter, kQuotaBarCount> quotas;
    std::wstring labelText;    // Bars' label.
    double labelOpacity = 0;
    double identityOpacity = 0;  // Tiles' identity.
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
    meters.labelText = meters.warn ? account.label + L"!" : account.label;
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
    bool miniBars = layout == BarLayout::LogoBars;
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
            if (m.paceVisible) {
                // Centred on the pace, kept inside the bar's rounded ends.
                double pacePct = remainingMode ? remainingFraction * 100.0 :
                                                 (1.0 - remainingFraction) * 100.0;
                pacePct = std::clamp(pacePct, 0.0, 100.0);
                double radius = std::clamp(std::max(1.0, display.barThickness / 2.0),
                                           bar.paceLength / 2.0,
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
        if (ring) {
            m.arc = concentric ? kConcentricArc : kSegmentedArcs[count - 1][position];
            m.radius = meters.ring.radius -
                       (concentric ? position * (meters.ring.stroke + meters.ring.gap) : 0.0);
            // Any usage fills at least 5 degrees.
            if (shown > 0) fill = std::max(fill, 5.0 / std::abs(m.arc.sweep));
        }
        position++;
        m.fillUnits = shown > 0 ? std::max(1, (int)std::lround(fill * 1000)) : 0;
        if (m.paceVisible) {
            double pace = remainingMode ? remainingFraction : 1.0 - remainingFraction;
            m.paceUnits = (int)std::lround(std::clamp(pace, 0.0, 1.0) * 1000);
        }
        double pace = m.paceUnits / 1000.0;
        if (ring) {
            m.fillSweep = m.arc.sweep * fill;
            m.paceAngle = m.arc.start + m.arc.sweep * pace;
        } else {
            double length = miniBars ? kMiniBarLength : kHeadlineSegmentLength;
            double minimum = miniBars ? kMiniBarThickness : kHeadlineSegmentThickness;
            m.fillLength = m.fillUnits ? std::clamp(length * fill, minimum, length) : 0;
            // Line tick, 3.5 DIPs wide, centred on the pace.
            m.paceOffset = kMiniBarLength * pace - 1.75;
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
