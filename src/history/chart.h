// Usage history chart (approved mockups s-light, s-hc, s-account, s-short, s-ticks, s-compare,
// s-legend, s-band, q6a-tooltip, q8a-filter, q6-rows): line colors per theme, the axes, the
// layout with its local time ticks, the projection painting and picking share, picking, readout
// rows, emphasis and the scene, a draw list the painter executes (history/chart-paint.h). Pure:
// explicit inputs; geometry in DIPs in the chart item's coordinates. The classic graph's rules
// (history/view-model.h, paint.h) carry over: per-pixel reduction, every nearby line in both
// periods picked at a recorded endpoint, muted lines first, the previous week dashed.

// Colors -----------------------------------------------------------------------------------------

// A series' classic color in the mockups' palette, dark or light; a scoped quota's tint keeps a
// quarter of the theme's text color over its account's hue.
static D2D1_COLOR_F HistoryUiHue(COLORREF color, bool light) {
    static constexpr uint32_t kHues[][2] = {
        {0x4F95FF, 0x2A6FD6}, {0xF0923F, 0xC8621A}, {0x4CC38A, 0x1F8F5A}, {0xE36BD0, 0xB8389F},
        {0xA184FF, 0x6F4FE6}, {0x33C3A6, 0x0E8F78}, {0xD9B44A, 0x9A7A12}, {0xFF7A70, 0xD0463C},
    };
    static_assert(std::size(kHues) == std::size(kHistoryColors));
    for (size_t k = 0; k < std::size(kHistoryColors); k++) {
        COLORREF base = kHistoryColors[k];
        COLORREF tint = RGB((GetRValue(base) * 3 + 255) / 4, (GetGValue(base) * 3 + 255) / 4,
                            (GetBValue(base) * 3 + 255) / 4);
        if (color != base && color != tint) continue;
        D2D1_COLOR_F hue = UiRgb(kHues[k][light]);
        return color == base ? hue : UiOver(UiRgb(light ? 0x000000 : 0xFFFFFF, 0.25f), hue);
    }
    return {float(GetRValue(color)) / 255, float(GetGValue(color)) / 255, float(GetBValue(color)) / 255, 1};
}

// A line's color: its hue; high contrast alternates the text and link colors by series (as the
// cards do).
static D2D1_COLOR_F HistoryChartColor(const HistoryUiLine& line, const UiPalette& p) {
    if (p.kind == UiThemeKind::HighContrast) return line.series % 2 ? p.link : p.text;
    return HistoryUiHue(line.color, p.kind == UiThemeKind::Light);
}

// The graph card's surface: muted lines mix into it, hollow dots and messages are filled with it.
static D2D1_COLOR_F HistoryChartBackground(const UiPalette& p) {
    return UiOver(p.card, p.background);
}

// Axes -------------------------------------------------------------------------------------------

// One unit's axis: its bounds and the values it labels (the percent axis also rules them).
struct HistoryChartAxis {
    bool visible = false;
    double low = 0, high = 1;
    std::vector<double> values;
};

// Axis labels: rounded, trailing decimal zeros dropped, abbreviated from 10,000.
static std::wstring HistoryChartAxisLabel(size_t axis, double value) {
    std::wstring number = HistoryUiNumber(value, 2);
    size_t end = std::min(number.find_first_not_of(L"-0123456789."), number.size()), dot = number.find(L'.');
    if (dot < end) {
        size_t last = number.find_last_not_of(L'0', end - 1);
        number.erase(last == dot ? dot : last + 1, end - (last == dot ? dot : last + 1));
    }
    return number + (axis == 0 ? L"%" : axis == 1 ? L" USD" : L" cr");
}

// Axes over the visible lines (percent, USD, credits; never shared by unlike units). Percent is
// always shown, from 0 (or below) to 100, or the next multiple of 25 above its readings; labels
// 0, its bounds, 100 and 50 (the layout keeps those with room). An extra axis shows while one of
// its lines does, from 0 (or below) to 1, 2, 3, 4, 5, 6 or 8 times a power of ten at or above its
// readings, labeled at its bounds and middle.
static std::array<HistoryChartAxis, 3> HistoryChartAxes(const std::vector<HistoryUiLine>& lines) {
    std::array<HistoryChartAxis, 3> axes;
    axes[0].visible = true;
    axes[0].high = 100;
    for (const HistoryUiLine& line : lines) {
        if (line.hidden) continue;
        HistoryChartAxis& axis = axes[line.meter < 6 ? 0 : size_t(line.meter - 5)];
        axis.visible = true;
        axis.low = std::min(axis.low, line.low);
        axis.high = std::max(axis.high, line.high);
    }
    auto nice = [](double value) {
        if (!(value > 0) || !std::isfinite(value)) return value;
        double power = std::pow(10.0, std::floor(std::log10(value)));
        for (double m : {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0}) {
            if (m * power >= value * (1 - 1e-9)) return std::isfinite(m * power) ? m * power : value;
        }
        return value;
    };
    auto quarter = [](double value) {
        double up = std::ceil(value / 25) * 25;
        return std::isfinite(up) ? up : value;
    };
    HistoryChartAxis& percent = axes[0];
    if (percent.high > 100) percent.high = quarter(percent.high);
    if (percent.low < 0) percent.low = -quarter(-percent.low);
    for (double value : {0.0, percent.low, percent.high, 100.0, 50.0}) {
        if (value >= percent.low && value <= percent.high &&
            std::find(percent.values.begin(), percent.values.end(), value) == percent.values.end()) {
            percent.values.push_back(value);
        }
    }
    for (size_t i = 1; i < 3; i++) {
        HistoryChartAxis& axis = axes[i];
        axis.high = nice(axis.high);
        if (axis.low < 0) axis.low = -nice(-axis.low);
        axis.values = {axis.low, axis.low / 2 + axis.high / 2, axis.high};
    }
    return axes;
}

// Layout -----------------------------------------------------------------------------------------

struct HistoryChartTick {
    ULONGLONG time = 0;
    std::wstring label;
};

// What a chart's reduction was made for: the snapshot's generation, the settings' revision, the
// period, the selection (history ID), view and comparison, the lines it reduced (the visible
// ones' chips) and the plot's width in device pixels. Together they fix the chart's lines, so
// each line keeps its reduced periods.
struct HistoryChartKey {
    uint64_t generation = 0, settings = 0;
    ULONGLONG from = 0, to = 0, readFrom = 0;
    uint64_t selected = 0;
    HistoryView view = HistoryView::Weekly;
    bool compare = false;
    std::vector<uint32_t> lines;
    int width = -1;
    bool operator==(const HistoryChartKey&) const = default;
};

// A laid out, reduced chart: everything painting and picking read, self-contained (its lines and
// the snapshot its reduced rows index), so a later load or refresh can't skew it before the next
// build.
struct HistoryChart {
    // Data: the view's lines, the period ([readFrom, from) is the previous week while comparing),
    // the red threshold, the snapshot with its generation, and a message instead of lines
    std::vector<HistoryUiLine> lines;
    ULONGLONG from = 0, to = 0, readFrom = 0;
    bool compare = false;
    int redThreshold = 90;
    std::shared_ptr<const HistorySnapshot> snapshot;
    uint64_t generation = 0;
    std::wstring message;

    // Layout at `scale`: the item's rectangle and the plot in it, the plot's device-pixel width,
    // the axes with their label columns (percent left, USD and credits right) and the time ticks
    D2D1_RECT_F rect{}, plot{};
    float scale = 1;
    int width = 0;
    std::array<HistoryChartAxis, 3> axes;
    std::array<float, 3> columns{};
    std::vector<HistoryChartTick> ticks;

    // Each line's periods (current, previous) reduced to the plot's pixels (hidden lines none),
    // and what for
    std::vector<std::array<HistoryRenderSeries, 2>> render;
    HistoryChartKey key;
};

static const UiTextStyle kHistoryChartAxisStyle{UiFamily::Small, 11, 14, DWRITE_FONT_WEIGHT_NORMAL};

// A value's height on its axis in the plot, clamped to it; large finite bounds don't overflow.
static float HistoryChartY(const HistoryChartAxis& axis, const D2D1_RECT_F& plot, double value) {
    double magnitude = std::max({1.0, std::abs(axis.low), std::abs(axis.high)});
    double low = axis.low / magnitude, span = axis.high / magnitude - low;
    double fraction = span > 0 ? (value / magnitude - low) / span : 0;
    return plot.bottom - float(std::clamp(fraction, 0.0, 1.0)) * (plot.bottom - plot.top);
}

// A reduced point: its pixel column of the plot and its value's height on its line's axis.
static D2D1_POINT_2F HistoryChartPoint(const HistoryChart& c, const HistoryUiLine& line, const HistoryRenderPoint& point) {
    return {c.plot.left + float(point.x) / c.scale,
            HistoryChartY(c.axes[line.meter < 6 ? 0 : size_t(line.meter - 5)], c.plot, point.value)};
}

// The plot inside the chart's item (mockup .plot): 10 DIP above it, 26 below for the time
// labels, the percent labels' column on the left (44 DIP, wider for wider labels: 8 DIP each
// side), on the right 26 DIP or the extra axes' columns (8 DIP, the widest label, 8 DIP between,
// 12 after), snapped to device pixels. Percent gridlines keep 14 DIP apart, the first of the
// axis's values first. Its device-pixel width is the reduction's.
static void HistoryChartLayout(UiGraphics& g, HistoryChart& c, D2D1_RECT_F rect, float scale) {
    auto widest = [&](size_t axis) {
        float width = 0;
        for (double value : c.axes[axis].values) {
            width = std::max(width, std::ceil(UiLayoutSize(UiLayout(g, HistoryChartAxisLabel(axis, value),
                                                                    kHistoryChartAxisStyle, 10000, false)).width));
        }
        return width;
    };
    c.columns = {widest(0), 0, 0};
    float left = std::max(44.0f, c.columns[0] + 16), right = 0;
    for (size_t axis = 1; axis < 3; axis++) {
        if (!c.axes[axis].visible) continue;
        c.columns[axis] = widest(axis);
        right += 8 + c.columns[axis];
    }
    right = right > 0 ? right + 12 : 26;
    c.rect = rect;
    c.scale = scale;
    float l = UiSnap(rect.left + left, scale), t = UiSnap(rect.top + 10, scale);
    c.plot = {l, t, std::max(UiSnap(rect.right - right, scale), l), std::max(UiSnap(rect.bottom - 26, scale), t)};
    c.width = int(std::lround((c.plot.right - c.plot.left) * scale));
    std::vector<double> kept;
    for (double value : c.axes[0].values) {
        float y = HistoryChartY(c.axes[0], c.plot, value);
        if (std::all_of(kept.begin(), kept.end(),
                        [&](double k) { return std::abs(HistoryChartY(c.axes[0], c.plot, k) - y) >= 14; })) {
            kept.push_back(value);
        }
    }
    std::sort(kept.begin(), kept.end());
    c.axes[0].values = kept;
}

// Time ticks over [from, to) at local boundaries (approved s-ticks), labeled at most 2 days:
// every 1, 2, 3, 6 or 12 hours (`14:00`, midnight as `Sat 31`); at most 14 days: every 1 or 2
// days (`Sat 24`); at most 120 days: every 1 or 2 days, or Mondays every 1, 2 or 4 weeks
// (`Oct 5`); beyond: every 1, 2, 3, 6, 12, 24 or 60 months from January (`Oct 2026`). The step
// is the smallest keeping labels 52, 56 and 64 DIP apart over `width`. A boundary is where the
// local clock (`localTime`) first reaches a new hour, day or month: daylight saving moves it, a
// repeated hour counts once, a skipped one never. A scan in steps shorter than the unit finds
// each, a bisection its millisecond.
static std::vector<HistoryChartTick> HistoryChartTicks(ULONGLONG from, ULONGLONG to, float width,
                                                       const HistoryUiLocalTime& localTime) {
    std::vector<HistoryChartTick> ticks;
    if (to <= from + 1 || width <= 0) return ticks;
    enum class Unit { Hour, Day, Month };
    double span = double(to - from), days = span / double(kHistoryDayMs);
    Unit unit = days <= 2 ? Unit::Hour : days <= 120 ? Unit::Day : Unit::Month;
    auto mod = [](int64_t a, int64_t b) { return (a % b + b) % b; };
    // Days since 1 January 1970 of a local date (proleptic Gregorian); hours and months count
    // from then and from year 0.
    auto day = [](const SYSTEMTIME& t) {
        int64_t year = int64_t(t.wYear) - (t.wMonth <= 2);
        int64_t era = (year >= 0 ? year : year - 399) / 400, yoe = year - era * 400;
        int64_t doy = (153 * (int64_t(t.wMonth) + (t.wMonth > 2 ? -3 : 9)) + 2) / 5 + t.wDay - 1;
        return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
    };
    auto bucket = [&](const SYSTEMTIME& t) {
        return unit == Unit::Hour ? day(t) * 24 + t.wHour :
               unit == Unit::Day  ? day(t) : int64_t(t.wYear) * 12 + t.wMonth - 1;
    };
    auto valid = [](const SYSTEMTIME& t) { return t.wMonth >= 1 && t.wMonth <= 12 && t.wDay >= 1 && t.wDay <= 31; };

    std::vector<int> steps = unit == Unit::Hour  ? std::vector<int>{1, 2, 3, 6, 12} :
                             unit == Unit::Month ? std::vector<int>{1, 2, 3, 6, 12, 24, 60} :
                             days <= 14          ? std::vector<int>{1, 2} : std::vector<int>{1, 2, 7, 14, 28};
    double unitMs = unit == Unit::Hour ? 3600000.0 : unit == Unit::Day ? double(kHistoryDayMs) : 30.44 * kHistoryDayMs;
    float spacing = unit == Unit::Hour ? 52.0f : unit == Unit::Day ? 56.0f : 64.0f;
    int step = steps.back();
    for (int s : steps) {
        if (width * float(double(s) * unitMs / span) >= spacing) {
            step = s;
            break;
        }
    }
    // Mondays are days 4 + 7k; every second or fourth one by k.
    auto chosen = [&](int64_t b) {
        if (unit != Unit::Day || step < 7) return mod(b, step) == 0;
        return mod(b, 7) == 4 && mod((b - 4) / 7, step / 7) == 0;
    };
    auto label = [&](const SYSTEMTIME& t) -> std::wstring {
        std::wstring month = kHistoryUiMonths[t.wMonth - 1];
        if (unit == Unit::Month) return month + L' ' + std::to_wstring(t.wYear);
        if (unit == Unit::Day && days > 14) return month + L' ' + std::to_wstring(t.wDay);
        if (unit == Unit::Hour && t.wHour) {
            wchar_t clock[8];
            swprintf(clock, ARRAYSIZE(clock), L"%02u:%02u", unsigned(t.wHour % 24), unsigned(t.wMinute % 60));
            return clock;
        }
        return std::wstring(kHistoryUiDays[mod(day(t) + 4, 7)]) + L' ' + std::to_wstring(t.wDay);
    };

    SYSTEMTIME start = localTime(from);
    if (!valid(start)) return ticks;
    int64_t seen = bucket(start);
    ULONGLONG scan = unit == Unit::Hour ? 15 * 60000ULL : unit == Unit::Day ? 3600000ULL : kHistoryDayMs;
    for (ULONGLONG lo = from; lo < to - 1;) {
        ULONGLONG hi = std::min(lo + scan, to - 1);
        SYSTEMTIME t = localTime(hi);
        if (!valid(t)) return {};
        if (bucket(t) <= seen) {
            lo = hi;
            continue;
        }
        while (hi - lo > 1) {
            ULONGLONG mid = lo + (hi - lo) / 2;
            SYSTEMTIME m = localTime(mid);
            if (valid(m) && bucket(m) > seen) hi = mid;
            else lo = mid;
        }
        SYSTEMTIME at = localTime(hi);
        seen = bucket(at);
        if (chosen(seen)) ticks.push_back({hi, label(at)});
        lo = hi;
    }
    return ticks;
}

// Picking ----------------------------------------------------------------------------------------

struct HistoryChartHit {
    size_t line = 0;
    int period = 0;
    size_t point = 0;
};

// Every visible line in both periods within 5 DIP and half its width of `at` (item
// coordinates), in line then period order: per line and period the recorded endpoint nearest
// `at` of the closest drawn segment (or a lone reading), never an interpolated value; a break
// is never a segment. Nothing outside the plot.
static std::vector<HistoryChartHit> HistoryChartPick(const HistoryChart& c, D2D1_POINT_2F at) {
    std::vector<HistoryChartHit> hits;
    const D2D1_RECT_F& plot = c.plot;
    if (c.width <= 0 || c.render.size() != c.lines.size() || at.x < plot.left || at.x > plot.right ||
        at.y < plot.top || at.y > plot.bottom) {
        return hits;
    }
    for (size_t index = 0; index < c.lines.size(); index++) {
        const HistoryUiLine& line = c.lines[index];
        if (line.hidden) continue;
        float tolerance = 5 + float(line.stroke) / 2;
        float minX = (at.x - tolerance - plot.left) * c.scale, maxX = (at.x + tolerance - plot.left) * c.scale;
        for (int period = 0; period < (c.compare ? 2 : 1); period++) {
            const std::vector<HistoryRenderPoint>& points = c.render[index][size_t(period)].points;
            auto first = std::lower_bound(points.begin(), points.end(), minX,
                                          [](const HistoryRenderPoint& point, float x) { return float(point.x) < x; });
            float best = tolerance * tolerance;
            size_t nearest = SIZE_MAX;
            for (size_t i = size_t(first - points.begin()); i < points.size(); i++) {
                D2D1_POINT_2F b = HistoryChartPoint(c, line, points[i]);
                float bx = at.x - b.x, by = at.y - b.y, distance = bx * bx + by * by;
                size_t endpoint = i;
                if (i && points[i].connect) {
                    D2D1_POINT_2F a = HistoryChartPoint(c, line, points[i - 1]);
                    float dx = b.x - a.x, dy = b.y - a.y, ax = at.x - a.x, ay = at.y - a.y;
                    float length = dx * dx + dy * dy;
                    float t = length > 0 ? std::clamp((ax * dx + ay * dy) / length, 0.0f, 1.0f) : 0;
                    float ex = ax - t * dx, ey = ay - t * dy;
                    distance = ex * ex + ey * ey;
                    if (ax * ax + ay * ay < bx * bx + by * by) endpoint = i - 1;
                }
                if (distance <= best) {
                    best = distance;
                    nearest = endpoint;
                }
                // One endpoint past the band: its incoming segment can cross it.
                if (float(points[i].x) > maxX) break;
            }
            if (nearest != SIZE_MAX) hits.push_back({index, period, nearest});
        }
    }
    return hits;
}

// Readout ----------------------------------------------------------------------------------------

// A readout row (approved q6-rows, with the chip names): the line's name and swatch, then never
// cut its period, its value rounded with its unit and the recorded local time, compact.
static UiReadoutRow HistoryChartRow(std::wstring name, D2D1_COLOR_F color, int stroke, int meter, int period,
                                    double value, ULONGLONG time, ULONGLONG now, const HistoryUiLocalTime& localTime) {
    return {std::move(name),
            {{period ? L"[Previous week]" : L"[Current]", UiRunStyle::Normal, UiInk::Secondary},
             {L" " + HistoryUiValue(meter, value, 1)},
             {L" \u00B7 " + HistoryUiTime(time, now, localTime), UiRunStyle::Normal, UiInk::Secondary}},
            color,
            float(stroke)};
}

// The readout of picked points: one row each, in their order, keyed by the recorded samples
// they name (none: key 0). Its overflow hint names what shows the rest.
static UiTooltipContent HistoryChartReadout(const HistoryChart& c, const std::vector<HistoryChartHit>& hits,
                                            const UiPalette& p, bool oneAccount, ULONGLONG now,
                                            const HistoryUiLocalTime& localTime) {
    UiTooltipContent content;
    if (!c.snapshot) return content;
    uint64_t key = 14695981039346656037ULL;
    auto mix = [&](uint64_t value) {
        for (int i = 0; i < 8; i++) key = (key ^ ((value >> (8 * i)) & 0xFF)) * 1099511628211ULL;
    };
    mix(c.generation);
    for (const HistoryChartHit& hit : hits) {
        const HistoryUiLine& line = c.lines[hit.line];
        const HistoryRenderPoint& point = c.render[hit.line][size_t(hit.period)].points[hit.point];
        if (point.sourceRow >= c.snapshot->samples.size()) continue;
        mix(line.chip);
        mix(uint64_t(hit.period));
        mix(point.sourceRow);
        content.rows.push_back(HistoryChartRow(line.name, HistoryChartColor(line, p), line.stroke, line.meter,
                                               hit.period, point.value, c.snapshot->samples[point.sourceRow].timeMs,
                                               now, localTime));
    }
    if (content.rows.empty()) return {};
    content.key = key ? key : 1;
    content.more = oneAccount ? L" \u2014 hide lines in the legend" :
                                L" \u2014 hide lines in the legend or select one account";
    return content;
}

// Emphasis ---------------------------------------------------------------------------------------

// The lines drawn emphasized, the others muted (none: no emphasis): the hovered chip's, else the
// lines picked on the chart, else the chip with keyboard focus; never a hidden line.
static std::vector<size_t> HistoryChartEmphasis(const std::vector<HistoryUiLine>& lines,
                                                const std::vector<size_t>& picked, const UiPaintState& state) {
    auto chip = [&](uint32_t id) {
        for (size_t i = 0; i < lines.size(); i++) {
            if (id && lines[i].chip == id && !lines[i].hidden) return std::vector<size_t>{i};
        }
        return std::vector<size_t>{};
    };
    if (std::vector<size_t> hovered = chip(state.hot.id); !hovered.empty()) return hovered;
    if (!picked.empty()) return picked;
    return state.focusVisible ? chip(state.focus.id) : std::vector<size_t>{};
}

// Scene ------------------------------------------------------------------------------------------

// Fill and Hatch: a rectangle (the band; high contrast hatches it). Rule: a horizontal line
// (gridlines, the threshold). Label: text in its box (a message over a backdrop of `fill`). Run:
// a polyline of one continuous stretch, round-capped and joined. Dot: a lone reading, `fill`
// inside (the background: hollow). Square: a flat run compressed into one point, stroke-wide.
enum class HistoryChartMark : uint8_t { Fill, Hatch, Rule, Label, Run, Dot, Square };

struct HistoryChartPrim {
    HistoryChartMark mark = HistoryChartMark::Rule;
    D2D1_COLOR_F color{};
    D2D1_COLOR_F fill{};
    D2D1_RECT_F rect{};
    std::vector<D2D1_POINT_2F> points;
    float width = 1;          // DIPs
    float dash = 0, gap = 0;  // In stroke widths; 0: solid
    std::wstring text;
    UiTextStyle style;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    bool clipped = false;     // Inside the plot's clip
    int line = -1, period = -1;
};

// The draw list in order, and the clip of the lines (the plot 3 DIP out).
struct HistoryChartScene {
    D2D1_RECT_F clip{};
    std::vector<HistoryChartPrim> prims;
};

// The chart as drawn (mockup .plot): while a percent line shows, the red band from the threshold
// to the percent axis's top (high contrast: hatched) and its dashed line and label; percent
// gridlines; each axis's labels (percent left, extra axes in their columns right) and the time
// labels below; then, clipped, the lines: muted ones first, each line's previous week (dashed,
// lone readings hollow) before its current period, 3 or 2 DIP wide; and the message over the
// plot. Emphasis mutes the other lines to a quarter of their color (high contrast: emphasized in
// the selection color, the others grey).
static HistoryChartScene HistoryChartBuildScene(const HistoryChart& c, const std::vector<size_t>& emphasis,
                                                const UiPalette& p) {
    HistoryChartScene scene;
    const bool hc = p.kind == UiThemeKind::HighContrast, light = p.kind == UiThemeKind::Light;
    const float s = c.scale;
    const D2D1_RECT_F& plot = c.plot;
    scene.clip = UiInflate(plot, 3, 3);
    D2D1_COLOR_F background = HistoryChartBackground(p), ink = p.textMuted;
    auto add = [&](HistoryChartMark mark, D2D1_COLOR_F color) -> HistoryChartPrim& {
        HistoryChartPrim& prim = scene.prims.emplace_back();
        prim.mark = mark;
        prim.color = color;
        return prim;
    };
    auto label = [&](std::wstring text, D2D1_RECT_F box, DWRITE_TEXT_ALIGNMENT align, D2D1_COLOR_F color) {
        HistoryChartPrim& prim = add(HistoryChartMark::Label, color);
        prim.text = std::move(text);
        prim.rect = box;
        prim.style = kHistoryChartAxisStyle;
        prim.align = align;
    };
    // Rules a whole number of device pixels wide (a DIP, rounded down), on pixel boundaries.
    float rule = std::max(1.0f, std::floor(s)) / s;
    auto line = [&](float y, D2D1_COLOR_F color, float dash) {
        float top = std::round(y * s - rule * s / 2) / s + rule / 2;
        HistoryChartPrim& prim = add(HistoryChartMark::Rule, color);
        prim.points = {{plot.left, top}, {plot.right, top}};
        prim.width = rule;
        prim.dash = prim.gap = dash / rule;
    };

    const HistoryChartAxis& percent = c.axes[0];
    bool percentLine = std::any_of(c.lines.begin(), c.lines.end(),
                                   [](const HistoryUiLine& l) { return !l.hidden && l.meter < 6; });
    bool threshold = percentLine && c.redThreshold >= percent.low && c.redThreshold <= percent.high;
    float red = HistoryChartY(percent, plot, c.redThreshold);
    if (threshold && red > plot.top) {
        add(hc ? HistoryChartMark::Hatch : HistoryChartMark::Fill,
            hc ? UiFade(p.textDisabled, 0.55f) : light ? UiRgb(0xC42B1C, 0.07f) : UiRgb(0xE53935, 0.08f))
            .rect = {plot.left, plot.top, plot.right, red};
    }
    for (double value : percent.values) {
        line(HistoryChartY(percent, plot, value), hc ? p.textDisabled : light ? UiRgb(0, 0.09f) : UiRgb(0xFFFFFF, 0.08f), 0);
    }
    if (threshold) {
        line(red, hc ? p.text : light ? UiRgb(0xC42B1C, 0.5f) : UiRgb(0xE53935, 0.55f), 4);
    }
    float x = plot.right;
    for (size_t i = 0; i < c.axes.size(); i++) {
        const HistoryChartAxis& axis = c.axes[i];
        if (!axis.visible) continue;
        if (i) x += 8;
        for (double value : axis.values) {
            float y = HistoryChartY(axis, plot, value);
            label(HistoryChartAxisLabel(i, value),
                  i ? D2D1_RECT_F{x, y - 7, x + c.columns[i], y + 7} : D2D1_RECT_F{c.rect.left, y - 7, plot.left - 8, y + 7},
                  i ? DWRITE_TEXT_ALIGNMENT_LEADING : DWRITE_TEXT_ALIGNMENT_TRAILING, ink);
        }
        if (i) x += c.columns[i];
    }
    if (threshold) {
        label(std::to_wstring(c.redThreshold) + L"%", {plot.left, red - 16, plot.right - 2, red - 2},
              DWRITE_TEXT_ALIGNMENT_TRAILING, hc ? p.text : light ? UiRgb(0xC42B1C) : UiRgb(0xE57373));
    }
    for (const HistoryChartTick& tick : c.ticks) {
        float at = plot.left + float(double(tick.time - c.from) / double(c.to - c.from)) * (plot.right - plot.left);
        label(tick.label, {at - 60, c.rect.bottom - 18, at + 60, c.rect.bottom - 4}, DWRITE_TEXT_ALIGNMENT_CENTER, ink);
    }

    // Lines, muted first; per line its previous week, then its current period.
    auto emphasized = [&](size_t i) { return std::find(emphasis.begin(), emphasis.end(), i) != emphasis.end(); };
    std::vector<size_t> order;
    for (size_t i = 0; i < c.lines.size() && i < c.render.size(); i++) {
        if (!c.lines[i].hidden) order.push_back(i);
    }
    std::stable_partition(order.begin(), order.end(), [&](size_t i) { return !emphasized(i); });
    for (size_t i : order) {
        const HistoryUiLine& l = c.lines[i];
        D2D1_COLOR_F base = HistoryChartColor(l, p), color = base;
        if (!emphasis.empty()) {
            color = emphasized(i) ? (hc ? p.highlight : base) :
                    hc            ? p.textDisabled : UiOver(UiFade(base, 0.25f), background);
        }
        float width = float(l.stroke);
        for (int period : {1, 0}) {
            if (period && !c.compare) continue;
            std::vector<D2D1_POINT_2F> run;
            size_t samples = 0;
            LONG lastY = 0;
            int lastX = -1;
            auto flush = [&] {
                if (run.empty()) return;
                HistoryChartPrim& prim = add(run.size() > 1 ? HistoryChartMark::Run :
                                             samples > 1    ? HistoryChartMark::Square : HistoryChartMark::Dot,
                                             color);
                prim.clipped = true;
                prim.line = int(i);
                prim.period = period;
                if (prim.mark == HistoryChartMark::Square) {
                    prim.rect = {run[0].x - width / 2, run[0].y - width / 2, run[0].x + width / 2, run[0].y + width / 2};
                } else if (prim.mark == HistoryChartMark::Dot) {
                    prim.points = {run[0]};
                    prim.width = 1.6f;
                    prim.fill = period ? background : color;
                } else {
                    prim.points = run;
                    prim.width = width;
                    if (period) {
                        prim.dash = 3 / width;
                        prim.gap = 6 / width;
                    }
                }
                run.clear();
                samples = 0;
            };
            for (const HistoryRenderPoint& point : c.render[i][size_t(period)].points) {
                if (!point.connect) flush();
                D2D1_POINT_2F at = HistoryChartPoint(c, l, point);
                // Points on the same device pixel are one vertex.
                LONG y = std::lround(at.y * s);
                if (run.empty() || point.x != lastX || y != lastY) run.push_back(at);
                lastX = point.x;
                lastY = y;
                samples++;
            }
            flush();
        }
    }

    if (!c.message.empty()) {
        float middle = (plot.top + plot.bottom) / 2;
        HistoryChartPrim& prim = add(HistoryChartMark::Label, p.textSecondary);
        prim.text = c.message;
        prim.rect = {plot.left, middle - 9.5f, plot.right, middle + 9.5f};
        prim.style = UiTextStyleFor(UiFont::Note);
        prim.align = DWRITE_TEXT_ALIGNMENT_CENTER;
        prim.fill = background;
    }
    return scene;
}
