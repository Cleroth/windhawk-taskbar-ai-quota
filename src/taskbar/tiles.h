// Tile layouts: segmented ring, concentric rings, logo + mini bars and logo + headline %. A tile
// shows the account's identity (provider logo, optionally with a number badge, or short text) and
// its tile quotas: the meters the bar layouts show, in slot order. BuildAccountTile creates a tile;
// UpdateAccountTile regenerates its meter shapes whenever their inputs change, then applies values
// in place.

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

static winrt::Windows::Foundation::Point TileRingPoint(double center, double radius,
                                                       double degrees) {
    double radians = degrees * std::numbers::pi / 180;
    return {(float)(center + radius * std::cos(radians)),
            (float)(center + radius * std::sin(radians))};
}

// Open arc in two segments, so a full circle renders too.
static Geometry TileArcGeometry(double center, double radius, double start, double sweep) {
    PathFigure figure;
    figure.StartPoint(TileRingPoint(center, radius, start));
    figure.IsClosed(false);
    figure.IsFilled(false);
    for (int half = 1; half <= 2; half++) {
        ArcSegment arc;
        arc.Point(TileRingPoint(center, radius, start + sweep * half / 2));
        arc.Size({(float)radius, (float)radius});
        arc.SweepDirection(sweep >= 0 ? SweepDirection::Clockwise :
                                        SweepDirection::Counterclockwise);
        figure.Segments().Append(arc);
    }
    PathGeometry geometry;
    geometry.Figures().Append(figure);
    return geometry;
}

// Tile chrome for the taskbar theme. Logo and text inherit the taskbar's text color instead.
struct TilePalette {
    winrt::Windows::UI::Color track;
    winrt::Windows::UI::Color hover;
    winrt::Windows::UI::Color open;  // Tile whose flyout is open.
    winrt::Windows::UI::Color badge;
    winrt::Windows::UI::Color badgeOutline;
};

static TilePalette TilePaletteFor(bool light) {
    if (light) {
        return {{0x24, 0, 0, 0}, {0x0A, 0, 0, 0}, {0xB3, 0xFF, 0xFF, 0xFF},
                {0xFF, 0xD8, 0xDB, 0xE2}, {0xFF, 0xEE, 0xF0, 0xF4}};
    }
    return {{0x2B, 0xFF, 0xFF, 0xFF}, {0x0F, 0xFF, 0xFF, 0xFF}, {0x1A, 0xFF, 0xFF, 0xFF},
            {0xFF, 0x3A, 0x3D, 0x44}, {0xFF, 0x1F, 0x21, 0x26}};
}

// Whether the mouse is over a tile of a tile layout.
static bool IsTileHovered(QuotaUiInstance const& state) {
    int index = state.flyout.hoverIndex;
    return index >= 0 && index < (int)state.accountRefs.size() &&
           state.accountRefs[index].tile.identity;
}

// Hover and open-flyout background of a tile; bar layouts have none.
static void UpdateTileHighlight(QuotaUiInstance& state, int index) {
    if (g_unloading || index < 0 || index >= (int)state.accountRefs.size()) return;
    AccountUiRefs const& ui = state.accountRefs[index];
    if (!ui.tile.identity || !ui.column) return;
    FlyoutUi const& fo = state.flyout;
    int level = 0;
    if (fo.popup.flyout && fo.popup.accountIndex == index) {
        level = 2;
    } else if (fo.hoverIndex == index) {
        // XAML can miss the pointer leaving the taskbar: hover only while it is over it.
        POINT pt{};
        RECT rect{};
        if (GetCursorPos(&pt) && GetWindowRect(state.hWnd, &rect) && PtInRect(&rect, pt)) level = 1;
    }
    int* applied = index < (int)state.applied.size() ? &state.applied[index].highlight : nullptr;
    if (applied && *applied == level) return;
    TilePalette palette = TilePaletteFor(state.tileLightTheme);
    try {
        ui.column.Background(SolidColorBrush(level == 2 ? palette.open :
                                             level == 1 ? palette.hover :
                                                          winrt::Windows::UI::Color{0, 0, 0, 0}));
        if (applied) *applied = level;
    } catch (...) {}
}

// Text width per DIP of font size, measured out of the tree; estimated if measuring fails.
static double TileTextWidthPerPt(TextBlock const& text) {
    try {
        text.Measure({INFINITY, INFINITY});
        double width = text.DesiredSize().Width;
        if (width > 0 && text.FontSize() > 0) return width / text.FontSize();
        return std::max<size_t>(text.Text().size(), 1) * 0.62;
    } catch (...) {
        return 2.5;
    }
}

// Builds account `index`'s tile: its root (hit target and flyout anchor), identity and meter
// containers. UpdateAccountTile sizes and fills the meters.
static StackPanel BuildAccountTile(std::vector<AccountConfig> const& accounts, size_t index,
                                   BarLayout layout, double height, AccountUiRefs& refs) {
    AccountConfig const& account = accounts[index];
    TileUiRefs& tile = refs.tile;
    tile.layout = layout;
    bool ring = layout == BarLayout::SegmentedRing || layout == BarLayout::ConcentricRings;
    FontFamily displayFont(L"Segoe UI Variable Display, Segoe UI");

    StackPanel column;
    column.Orientation(Orientation::Horizontal);
    column.VerticalAlignment(VerticalAlignment::Center);
    column.Height(height);
    column.CornerRadius({4, 4, 4, 4});
    column.Padding(ring ? Thickness{4, 0, 4, 0} :
                   layout == BarLayout::LogoBars ? Thickness{7, 0, 7, 0} :
                                                   Thickness{7, 0, 8, 0});
    column.Spacing(6);

    // Identity: the provider logo, with a number badge when several configured accounts share
    // the provider, or text. Rings resize it to their hole.
    Grid identity;
    identity.IsHitTestVisible(false);
    if (!ring) identity.VerticalAlignment(VerticalAlignment::Center);
    tile.identity = identity;
    if (account.tileIdentity != TileIdentity::Text) {
        try {
            Viewbox logo;
            double side = layout == BarLayout::LogoHeadline ? 16 : 15;
            logo.Width(side);
            logo.Height(side);
            logo.HorizontalAlignment(HorizontalAlignment::Center);
            logo.VerticalAlignment(VerticalAlignment::Center);
            logo.Child(LoadProviderLogo(account.provider, nullptr));
            identity.Children().Append(logo);
            tile.logo = logo;
        } catch (...) {
            Wh_Log(L"Tile logo failed to load; showing text");
        }
    }
    bool sharedProvider = std::count_if(accounts.begin(), accounts.end(),
        [&](AccountConfig const& other) { return other.provider == account.provider; }) > 1;
    if (tile.logo && account.tileIdentity == TileIdentity::LogoBadge && sharedProvider) {
        // The label's trailing digits (at most two), else its first letter, else its first
        // non-space character.
        std::wstring const& label = account.label;
        size_t digits = label.size();
        while (digits > 0 && iswdigit(label[digits - 1])) digits--;
        auto letter = std::find_if(label.begin(), label.end(),
                                   [](wchar_t ch) { return iswalpha(ch); });
        if (letter == label.end()) {
            letter = std::find_if(label.begin(), label.end(),
                                  [](wchar_t ch) { return !iswspace(ch); });
        }
        std::wstring badgeText;
        if (digits < label.size()) {
            badgeText = label.substr(label.size() - std::min<size_t>(label.size() - digits, 2));
        } else if (letter != label.end()) {
            badgeText.assign(1, (wchar_t)towupper(*letter));
        } else {
            badgeText = std::to_wstring(index + 1);
        }
        tile.badgeText = TextBlock{};
        tile.badgeText.Text(badgeText);
        tile.badgeText.FontFamily(FontFamily(L"Segoe UI Variable Small, Segoe UI"));
        tile.badgeText.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        tile.badgeText.FontSize(6.8);
        tile.badgeText.TextLineBounds(TextLineBounds::Tight);
        tile.badgeText.HorizontalAlignment(HorizontalAlignment::Center);
        tile.badgeText.VerticalAlignment(VerticalAlignment::Center);
        tile.badge = Border{};
        tile.badge.Height(10);
        tile.badge.MinWidth(10);
        tile.badge.Padding({1.5, 0, 1.5, 0});
        tile.badge.CornerRadius({5, 5, 5, 5});
        tile.badge.BorderThickness({1.2, 1.2, 1.2, 1.2});
        tile.badge.Child(tile.badgeText);
        if (ring) {
            tile.badge.HorizontalAlignment(HorizontalAlignment::Left);
            tile.badge.VerticalAlignment(VerticalAlignment::Top);
            tile.logo.Margin({0, 0, 2, 2});  // Shifts the logo up-left, away from the badge.
        } else {
            // Centred on the logo's lower-right corner.
            tile.badge.HorizontalAlignment(HorizontalAlignment::Right);
            tile.badge.VerticalAlignment(VerticalAlignment::Bottom);
            tile.badge.Margin({0, 0, -4, -4});
        }
        identity.Children().Append(tile.badge);
    }
    if (!tile.logo) {
        std::wstring text = account.tileText;
        if (text.empty()) {
            std::copy_if(account.label.begin(), account.label.end(), std::back_inserter(text),
                         [](wchar_t ch) { return !iswspace(ch); });
        }
        if (text.empty()) text = std::to_wstring(index + 1);
        tile.text = TextBlock{};
        tile.text.Text(text);
        tile.text.FontFamily(displayFont);
        tile.text.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        tile.text.FontSize(10);
        tile.text.TextWrapping(TextWrapping::NoWrap);
        tile.text.TextTrimming(TextTrimming::Clip);
        tile.text.TextLineBounds(TextLineBounds::Tight);
        tile.text.HorizontalAlignment(HorizontalAlignment::Center);
        tile.text.VerticalAlignment(VerticalAlignment::Center);
        tile.textWidthPerPt = TileTextWidthPerPt(tile.text);
        if (!ring) {
            // Shrinks to fit 26 DIPs, then clips.
            tile.text.FontSize(std::clamp(26 / tile.textWidthPerPt, 8.0, 12.0));
            tile.text.MaxWidth(26);
        }
        identity.Children().Append(tile.text);
    }
    // Stale-warning mark, like the bars' "!" after the label: at the logo or text's top-right
    // corner; UpdateAccountTile moves it to the tile's top-right corner in rings.
    tile.warn = TextBlock{};
    tile.warn.Text(L"!");
    tile.warn.FontSize(10);
    tile.warn.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
    tile.warn.TextLineBounds(TextLineBounds::Tight);
    tile.warn.HorizontalAlignment(HorizontalAlignment::Right);
    tile.warn.VerticalAlignment(VerticalAlignment::Top);
    tile.warn.Margin({0, -3, -4, 0});
    tile.warn.Visibility(Visibility::Collapsed);
    identity.Children().Append(tile.warn);

    if (ring) {
        Grid face;
        face.IsHitTestVisible(false);
        face.VerticalAlignment(VerticalAlignment::Center);
        Grid meters;
        face.Children().Append(meters);
        face.Children().Append(identity);
        tile.face = face;
        tile.meters = meters;
        column.Children().Append(face);
        return column;
    }

    column.Children().Append(identity);
    StackPanel meters;
    meters.IsHitTestVisible(false);
    meters.VerticalAlignment(VerticalAlignment::Center);
    tile.meters = meters;
    if (layout == BarLayout::LogoBars) {
        meters.Spacing(kMiniBarGap);
        column.Children().Append(meters);
        return column;
    }

    // Headline: the most-used quota's number and window key over per-quota segments.
    StackPanel headline;
    headline.IsHitTestVisible(false);
    headline.VerticalAlignment(VerticalAlignment::Center);
    headline.Spacing(3);
    StackPanel numberRow;
    numberRow.Orientation(Orientation::Horizontal);
    tile.headline = TextBlock{};
    tile.headline.FontFamily(displayFont);
    tile.headline.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    tile.headline.FontSize(14);
    tile.headline.TextLineBounds(TextLineBounds::Tight);
    tile.headline.VerticalAlignment(VerticalAlignment::Bottom);
    // Reserves "100%" so the tile keeps its width as values change.
    tile.headline.Text(L"100%");
    tile.headline.MinWidth(std::ceil(TileTextWidthPerPt(tile.headline) * 14));
    tile.headline.Text(L"");
    numberRow.Children().Append(tile.headline);
    tile.headlineKey = TextBlock{};
    tile.headlineKey.FontSize(9);
    tile.headlineKey.Opacity(0.6);
    tile.headlineKey.TextLineBounds(TextLineBounds::Tight);
    tile.headlineKey.VerticalAlignment(VerticalAlignment::Bottom);
    tile.headlineKey.Margin({3, 0, 0, 0});
    numberRow.Children().Append(tile.headlineKey);
    headline.Children().Append(numberRow);
    meters.Orientation(Orientation::Horizontal);
    meters.HorizontalAlignment(HorizontalAlignment::Left);
    meters.Spacing(2);
    headline.Children().Append(meters);
    column.Children().Append(headline);
    return column;
}

// Display settings shared by every tile in one update.
struct TileDisplay {
    ULONGLONG now = 0;
    BarMode barMode = BarMode::Used;
    int yellowThreshold = 0;
    int orangeThreshold = 0;
    int redThreshold = 0;
    bool colorblindMode = false;
    bool showPaceTicks = false;
    COLORREF paceTickColor = 0;
    int ringSize = 0;
    int ringThickness = 0;
    bool light = false;
};

static void UpdateAccountTile(QuotaUiInstance& state, size_t index, AccountConfig const& account,
                              AccountData const& d, int barMask, bool stale, bool warn,
                              TileDisplay const& display) {
    AccountUiRefs& ui = state.accountRefs[index];
    AppliedState& ap = state.applied[index];
    TileUiRefs& tile = ui.tile;
    if (!tile.identity || !tile.meters) return;
    BarLayout layout = tile.layout;
    bool concentric = layout == BarLayout::ConcentricRings;
    bool ring = concentric || layout == BarLayout::SegmentedRing;
    bool miniBars = layout == BarLayout::LogoBars;
    const std::array<const WindowUsage*, kQuotaBarCount> usage = {
        &d.win5h, &d.winWeek, &d.fableWeek, &d.extraUsage};
    int count = 0;
    for (int w = 0; w < kQuotaBarCount; w++) {
        if (barMask & (1 << w)) count++;
    }

    // The geometry guarantees hold for the settings' ranges.
    TileRingGeometry geometry;
    if (ring) {
        int size = std::clamp(display.ringSize, kRingSizeMin, kRingSizeMax);
        geometry = TileRingLayout(
            concentric, std::min<double>(size, state.tileHeight),
            std::clamp(display.ringThickness, kRingThicknessMin, kRingThicknessMax), count,
            tile.badge ? (int)tile.badgeText.Text().size() : 0);
    }
    double center = geometry.size / 2;
    // Arc and centreline radius of the tile quota at `position`.
    auto arcAt = [&](int position) {
        TileArc arc = concentric ? kConcentricArc : kSegmentedArcs[count - 1][position];
        double radius = geometry.radius -
                        (concentric ? position * (geometry.stroke + geometry.gap) : 0.0);
        return std::pair{arc, radius};
    };

    // Meter shapes: regenerated, and the identity resized, whenever an input changes.
    TileShapeKey shape{(int)layout, barMask, geometry.size, geometry.stroke, geometry.gap,
                       display.light ? 1 : 0,
                       display.showPaceTicks ? (int)display.paceTickColor : -1};
    if (shape != ap.tileShape) {
        TilePalette palette = TilePaletteFor(display.light);
        SolidColorBrush track(palette.track);
        SolidColorBrush paceCore(winrt::Windows::UI::Color{
            0xFF, GetRValue(display.paceTickColor), GetGValue(display.paceTickColor),
            GetBValue(display.paceTickColor)});
        SolidColorBrush paceOutline(winrt::Windows::UI::Color{0x8C, 0, 0, 0});
        auto meters = tile.meters.Children();
        meters.Clear();
        ui.fills.fill(nullptr);
        ui.paceTicks.fill(nullptr);
        tile.arcs.fill(nullptr);
        tile.dots.fill(nullptr);
        tile.meters.Visibility(count ? Visibility::Visible : Visibility::Collapsed);

        if (ring) {
            double size = geometry.size;
            tile.face.Width(size);
            tile.face.Height(size);
            auto ringShape = [&]() {
                wuxs::Path path;
                path.Width(size);
                path.Height(size);
                return path;
            };
            auto arcShape = [&](Brush const& stroke) {
                wuxs::Path path = ringShape();
                path.Stroke(stroke);
                path.StrokeThickness(geometry.stroke);
                path.StrokeStartLineCap(PenLineCap::Round);
                path.StrokeEndLineCap(PenLineCap::Round);
                return path;
            };
            // Tracks, then fills, then pace dots on top.
            int position = 0;
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                auto [arc, radius] = arcAt(position++);
                wuxs::Path trackArc = arcShape(track);
                trackArc.Data(TileArcGeometry(center, radius, arc.start, arc.sweep));
                meters.Append(trackArc);
            }
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                tile.arcs[w] = arcShape(track);
                tile.arcs[w].Visibility(Visibility::Collapsed);
                meters.Append(tile.arcs[w]);
            }
            for (int w = 0; display.showPaceTicks && w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                tile.dots[w] = ringShape();
                tile.dots[w].Fill(paceCore);
                tile.dots[w].Stroke(paceOutline);
                tile.dots[w].StrokeThickness(0.8);
                tile.dots[w].Visibility(Visibility::Collapsed);
                meters.Append(tile.dots[w]);
            }

            // Identity, sized to the hole.
            if (tile.logo) {
                double side = tile.badge ? geometry.logo - 1 : geometry.logo;
                tile.logo.Width(side);
                tile.logo.Height(side);
            }
            if (tile.badge) {
                double radius = geometry.badge / 2;
                tile.badge.Width(geometry.badgeWidth);
                tile.badge.Height(geometry.badge);
                tile.badge.CornerRadius({radius, radius, radius, radius});
                tile.badge.Margin({geometry.badgeLeft, geometry.badgeTop, 0, 0});
                tile.badgeText.FontSize(geometry.badgeFont);
            }
            if (tile.text) {
                // The largest font, from min(12, size / 3) down to 7, whose text fits across the
                // hole at half its cap height; clipped beyond that.
                double radius = geometry.hole - 0.8;
                auto chord = [&](double font) {
                    double half = font * 0.36;
                    return 2 * std::sqrt(std::max(radius * radius - half * half, 0.0));
                };
                double font = std::max(std::min(12.0, size * 0.33), 7.0);
                while (font > 7 && tile.textWidthPerPt * font > chord(font)) font -= 0.25;
                font = std::max(font, 7.0);
                tile.text.FontSize(font);
                tile.text.MaxWidth(chord(font));
            }
            // Into the tile's padding, clear of the ring.
            tile.warn.Margin({0, 1 - (state.tileHeight - size) / 2, -3, 0});
        } else {
            double length = miniBars ? kMiniBarLength : kHeadlineSegmentLength;
            double thickness = miniBars ? kMiniBarThickness : kHeadlineSegmentThickness;
            CornerRadius round{thickness / 2, thickness / 2, thickness / 2, thickness / 2};
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                Grid bar;
                bar.Width(length);
                bar.Height(thickness);
                Border trackBar;
                trackBar.CornerRadius(round);
                trackBar.Background(track);
                bar.Children().Append(trackBar);
                Border fill;
                fill.HorizontalAlignment(HorizontalAlignment::Left);
                fill.CornerRadius(round);
                fill.Width(0);
                bar.Children().Append(fill);
                ui.fills[w] = fill;
                if (miniBars && display.showPaceTicks) {
                    // Line tick with a dark outline, overhanging the bar by 2 DIPs.
                    Border tick;
                    tick.Width(3.5);
                    tick.Height(thickness + 4);
                    tick.Margin({0, -2, 0, -2});
                    tick.HorizontalAlignment(HorizontalAlignment::Left);
                    tick.CornerRadius({1.5, 1.5, 1.5, 1.5});
                    tick.Background(paceOutline);
                    Border core;
                    core.Width(2);
                    core.Height(thickness + 2.5);
                    core.CornerRadius({1, 1, 1, 1});
                    core.HorizontalAlignment(HorizontalAlignment::Center);
                    core.VerticalAlignment(VerticalAlignment::Center);
                    core.Background(paceCore);
                    tick.Child(core);
                    tick.Visibility(Visibility::Collapsed);
                    bar.Children().Append(tick);
                    ui.paceTicks[w] = tick;
                }
                meters.Append(bar);
            }
        }
        if (tile.badge) {
            tile.badge.Background(SolidColorBrush(palette.badge));
            tile.badge.BorderBrush(SolidColorBrush(palette.badgeOutline));
        }
        ap.tileShape = shape;
        ap.fillPx.fill(-1);
        ap.fillColor.fill(0);
        ap.pacePx.fill(-1);
        ap.paceVisible.fill(-1);
        ap.highlight = -1;
    }

    // Values. Fill follows the bar mode; color stays keyed to used % like the bars.
    auto shownPct = [&](double pct) {
        if (pct < 0) return 0.0;
        return std::clamp(display.barMode == BarMode::Remaining ? 100.0 - pct : pct, 0.0, 100.0);
    };
    int position = 0;
    for (int w = 0; w < kQuotaBarCount; w++) {
        if (!(barMask & (1 << w))) continue;
        const WindowUsage& wu = *usage[w];
        auto [arc, radius] = ring ? arcAt(position) : std::pair{TileArc{}, 0.0};
        position++;

        double shown = shownPct(wu.pct);
        double fill = shown / 100;
        if (ring && shown > 0) fill = std::max(fill, 5.0 / std::abs(arc.sweep));
        int fillUnits = shown > 0 ? std::max(1, (int)std::lround(fill * 1000)) : 0;
        auto color = UsageColor(wu.pct, stale, display.yellowThreshold, display.orangeThreshold,
                                display.redThreshold, display.colorblindMode);
        uint32_t colorValue = ((uint32_t)color.A << 24) | ((uint32_t)color.R << 16) |
                              ((uint32_t)color.G << 8) | color.B;
        if (fillUnits != ap.fillPx[w] || colorValue != ap.fillColor[w]) {
            SolidColorBrush brush(color);
            if (ring && tile.arcs[w]) {
                tile.arcs[w].Visibility(fillUnits ? Visibility::Visible : Visibility::Collapsed);
                if (fillUnits) {
                    tile.arcs[w].Data(
                        TileArcGeometry(center, radius, arc.start, arc.sweep * fill));
                }
                tile.arcs[w].Stroke(brush);
            } else if (!ring && ui.fills[w]) {
                double length = miniBars ? kMiniBarLength : kHeadlineSegmentLength;
                double minimum = miniBars ? kMiniBarThickness : kHeadlineSegmentThickness;
                ui.fills[w].Width(fillUnits ? std::clamp(length * fill, minimum, length) : 0);
                ui.fills[w].Background(brush);
            }
            ap.fillPx[w] = fillUnits;
            ap.fillColor[w] = colorValue;
        }

        // Pace marker, under the same conditions as the bars' pace ticks.
        FrameworkElement marker = ring ? tile.dots[w].try_as<FrameworkElement>() :
                                         ui.paceTicks[w].try_as<FrameworkElement>();
        bool paceVisible = marker && !stale && wu.pct >= 0 &&
                           wu.resetUnixMs > wu.windowDurationMs && wu.windowDurationMs > 0 &&
                           display.now >= wu.resetUnixMs - wu.windowDurationMs;
        int paceUnits = 0;
        if (paceVisible) {
            double remainingFraction =
                ((double)wu.resetUnixMs - (double)display.now) / wu.windowDurationMs;
            double pace = display.barMode == BarMode::Remaining ? remainingFraction :
                                                                  1.0 - remainingFraction;
            paceUnits = (int)std::lround(std::clamp(pace, 0.0, 1.0) * 1000);
        }
        if ((paceVisible ? 1 : 0) != ap.paceVisible[w]) {
            if (marker) {
                marker.Visibility(paceVisible ? Visibility::Visible : Visibility::Collapsed);
            }
            ap.paceVisible[w] = paceVisible ? 1 : 0;
        }
        if (paceVisible && paceUnits != ap.pacePx[w]) {
            double pace = paceUnits / 1000.0;
            if (ring) {
                EllipseGeometry dot;
                dot.Center(TileRingPoint(center, radius, arc.start + arc.sweep * pace));
                dot.RadiusX(geometry.dot);
                dot.RadiusY(geometry.dot);
                tile.dots[w].Data(dot);
            } else {
                ui.paceTicks[w].Margin({kMiniBarLength * pace - 1.75, -2, 0, -2});
            }
            ap.pacePx[w] = paceUnits;
        }
    }

    // Headline: the most-used tile quota, shown per the bar mode in its usage color.
    if (tile.headline) {
        int top = -1;
        for (int w = 0; w < kQuotaBarCount; w++) {
            if ((barMask & (1 << w)) && usage[w]->pct >= 0 &&
                (top < 0 || usage[w]->pct > usage[top]->pct)) {
                top = w;
            }
        }
        std::wstring number = L"\u2013";
        std::wstring key;
        if (top >= 0) {
            wchar_t text[16];
            swprintf(text, ARRAYSIZE(text), L"%.0f%%", shownPct(usage[top]->pct));
            number = text;
            key = top == kExtraUsageBar && account.provider == L"openai" ? L"Cr" :
                                                                           kQuotaKeys[top];
        }
        auto color = UsageColor(top >= 0 ? usage[top]->pct : -1, stale, display.yellowThreshold,
                                display.orangeThreshold, display.redThreshold,
                                display.colorblindMode);
        uint32_t colorValue = ((uint32_t)color.A << 24) | ((uint32_t)color.R << 16) |
                              ((uint32_t)color.G << 8) | color.B;
        std::wstring headline = number + L"\n" + key;
        if (headline != ap.headline || colorValue != ap.headlineColor) {
            tile.headline.Text(number);
            tile.headline.Foreground(SolidColorBrush(color));
            tile.headlineKey.Text(key);
            ap.headline = std::move(headline);
            ap.headlineColor = colorValue;
        }
    }

    // Identity dims while stale, like the bars' label; the warning mark replaces their "!".
    double identityOpacity = stale ? 0.45 : 1.0;
    if (identityOpacity != ap.identityOpacity) {
        tile.identity.Opacity(identityOpacity);
        ap.identityOpacity = identityOpacity;
    }
    if ((warn ? 1 : 0) != ap.tileWarn) {
        tile.warn.Visibility(warn ? Visibility::Visible : Visibility::Collapsed);
        ap.tileWarn = warn ? 1 : 0;
    }
}
