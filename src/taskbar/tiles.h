// Tile layouts: logo + headline %, the face tiles (rings, rounded square, gauge, clock pies), the
// burn-up sparkline and the quota matrix. A tile shows the account's identity (provider logo,
// optionally with a number badge, or short text) and its tile quotas: the meters the bar layouts
// show, in slot order (a gauge: the most-used one). BuildAccountTile creates a tile;
// UpdateAccountTile
// regenerates its meter shapes whenever their inputs change, then applies values in place. Bar
// columns with a logo identity show the same identity (BuildTileIdentity, UpdateTileIdentity), and
// every column takes the tiles' hover and open-flyout background. Values, identity text and
// geometry come from the meter model (meter-model.h).

// A tile shape (meter-model.h), moved `offset` DIPs right and down: closed figures to fill, or
// open ones to stroke. PathGeometry fills even-odd by default, as the outline expects.
static Geometry TileOutlineGeometry(TileOutline const& outline, double offset, bool closed = true) {
    PathGeometry geometry;
    for (TileOutlineFigure const& source : outline) {
        PathFigure figure;
        figure.StartPoint({(float)(source.x + offset), (float)(source.y + offset)});
        figure.IsClosed(closed);
        figure.IsFilled(closed);
        for (TileOutlineSegment const& segment : source.segments) {
            winrt::Windows::Foundation::Point end{(float)(segment.x + offset),
                                                  (float)(segment.y + offset)};
            if (segment.radius > 0) {
                ArcSegment arc;
                arc.Point(end);
                arc.Size({(float)segment.radius, (float)segment.radius});
                arc.SweepDirection(segment.clockwise ? SweepDirection::Clockwise :
                                                       SweepDirection::Counterclockwise);
                figure.Segments().Append(arc);
            } else {
                LineSegment line;
                line.Point(end);
                figure.Segments().Append(line);
            }
        }
        geometry.Figures().Append(figure);
    }
    return geometry;
}

// A bar or headline segment fill: the usage color's highlight at its top (vertical bars: left)
// edge, shading to the color 60% across.
static LinearGradientBrush UsageFillBrush(uint32_t color, bool vertical) {
    LinearGradientBrush shade;
    shade.StartPoint({0.f, 0.f});
    shade.EndPoint(vertical ? winrt::Windows::Foundation::Point{1.f, 0.f} :
                              winrt::Windows::Foundation::Point{0.f, 1.f});
    GradientStop top;
    top.Color(ArgbColor(UsageHighlight(color)));
    top.Offset(0.0);
    GradientStop body;
    body.Color(ArgbColor(color));
    body.Offset(0.6);
    shade.GradientStops().Append(top);
    shade.GradientStops().Append(body);
    return shade;
}

// Whether the mouse is over a column, in any layout.
static bool IsTileHovered(QuotaUiInstance const& state) {
    int index = state.flyout.hoverIndex;
    return index >= 0 && index < (int)state.accountRefs.size();
}

// Hover and open-flyout background of a column, in any layout.
static void UpdateTileHighlight(QuotaUiInstance& state, int index) {
    if (g_unloading || index < 0 || index >= (int)state.accountRefs.size()) return;
    AccountUiRefs const& ui = state.accountRefs[index];
    if (!ui.column) return;
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
    // Hover and open colors follow the taskbar theme, which can change while either lasts.
    int key = level ? level * 2 + (state.lightTheme ? 1 : 0) : 0;
    int* applied = index < (int)state.applied.size() ? &state.applied[index].highlight : nullptr;
    if (applied && *applied == key) return;
    TilePalette palette = TilePaletteFor(state.lightTheme);
    try {
        ui.column.Background(SolidColorBrush(ArgbColor(level == 2 ? palette.open :
                                                       level == 1 ? palette.hover : 0)));
        if (applied) *applied = key;
    } catch (...) {}
}

// Usage graphs that swap spans on hover: updates the tiles once the hovered account changed.
static void UpdateHoverSpans(QuotaUiInstance& state) {
    if (state.buildSparkHoverSwap && state.sparkSwapIndex != state.flyout.hoverIndex) UpdateQuotaUi(state);
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

// Builds account `index`'s identity into `tile`: the provider logo (in its brand colors when
// chosen), with a number badge when another configured account, hidden or not, shares the
// provider, or text, and the stale-warning mark. Ring tiles
// resize the logo to their hole and place the badge; other tiles take a 16 DIP logo, bar
// columns 15 DIP, at the logo size. UpdateTileIdentity applies its state.
static Grid BuildTileIdentity(std::vector<AccountConfig> const& accounts, size_t index,
                              BarLayout layout, MeterDisplay const& display, TileUiRefs& tile) {
    AccountConfig const& account = accounts[index];
    bool ring = IsRingTile(layout);
    double side = (IsTileLayout(layout) ? kHeadlineLogoSide : kBarLogoSide) * display.logoScale;
    Grid identity;
    identity.IsHitTestVisible(false);
    if (!ring) identity.VerticalAlignment(VerticalAlignment::Center);
    tile.identity = identity;
    if (account.tileIdentity != TileIdentity::Text) {
        try {
            Viewbox logo;
            logo.Width(side);
            logo.Height(side);
            logo.HorizontalAlignment(HorizontalAlignment::Center);
            logo.VerticalAlignment(VerticalAlignment::Center);
            // Brand colors, except in high contrast, where it inherits the text color.
            const LogoBrand* brand = ProviderLogoBrand(display.logoBrands, account.provider);
            logo.Child(LoadProviderLogo(account.provider,
                                        brand && !IsHighContrast() ? LogoBrandBrush(*brand) : Brush{nullptr}));
            identity.Children().Append(logo);
            tile.logo = logo;
        } catch (...) {
            Wh_Log(L"Tile logo failed to load; showing text");
        }
    }
    TileIdentityText identityText = TileIdentityFor(accounts, index);
    if (tile.logo && !identityText.badgeText.empty()) {
        // The text, in the badge color unless it inherits the taskbar's, over eight copies in the
        // halo brush, kBadgeOutline DIPs around it, centred in a box that never clips them. The
        // Canvas places the box from the identity's top-left without sizing the identity.
        double size = display.badgeSize;
        uint32_t color = BadgeColorsFor(display.badgeColor, false).text;
        tile.badgeHalo = SolidColorBrush{};
        tile.badge = Grid{};
        tile.badge.Width(4 * size);
        tile.badge.Height(2 * size);
        for (int k = 0; k <= 8; k++) {
            TextBlock text;
            text.Text(identityText.badgeText);
            text.FontFamily(FontFamily(L"Segoe UI Variable Small, Segoe UI"));
            text.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
            text.FontSize(size);
            text.TextLineBounds(TextLineBounds::Tight);
            text.HorizontalAlignment(HorizontalAlignment::Center);
            text.VerticalAlignment(VerticalAlignment::Center);
            if (k < 8) {
                TranslateTransform shift;
                shift.X(std::cos(k * std::numbers::pi / 4) * kBadgeOutline);
                shift.Y(std::sin(k * std::numbers::pi / 4) * kBadgeOutline);
                text.RenderTransform(shift);
                text.Foreground(tile.badgeHalo);
            } else {
                if (color) text.Foreground(SolidColorBrush(ArgbColor(color)));
                tile.badgeText = text;
            }
            tile.badge.Children().Append(text);
        }
        if (!ring) {
            BadgeBox box = LogoBadgeBox(display, side, (int)identityText.badgeText.size());
            Canvas::SetLeft(tile.badge, box.left + box.width / 2 - 2 * size);
            Canvas::SetTop(tile.badge, box.top + box.height / 2 - size);
            // Down a side taskbar the badge's reach past the logo counts across it, on both sides
            // so the logo stays centred: an account it widens past the taskbar shrinks to fit. Bar
            // columns replace this with their identity margins, which hold the same reach.
            if (display.sideWidth > 0) {
                double reach = std::max({-box.left, box.left + box.width - side, 0.0});
                identity.Margin({reach, 0, reach, 0});
            }
        }
        Canvas place;
        place.Children().Append(tile.badge);
        identity.Children().Append(place);
    }
    if (!tile.logo) {
        tile.text = TextBlock{};
        tile.text.Text(identityText.text);
        tile.text.FontFamily(FontFamily(L"Segoe UI Variable Display, Segoe UI"));
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
    // Stale-warning mark, in place of the "!" after a label: at the logo or text's top-right
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
    return identity;
}

// Builds account `index`'s tile: its root (hit target and flyout anchor), identity and meter
// containers. UpdateAccountTile sizes and fills the meters. On a side taskbar the identity sits
// above the meters (rings keep it inside), all centred across the tile, which takes its height
// from them.
static StackPanel BuildAccountTile(std::vector<AccountConfig> const& accounts, size_t index,
                                   BarLayout layout, MeterDisplay const& display, AccountUiRefs& refs) {
    TileUiRefs& tile = refs.tile;
    tile.layout = layout;
    bool ring = IsRingTile(layout);
    bool side = display.sideWidth > 0;

    StackPanel column;
    column.Orientation(side && !ring ? Orientation::Vertical : Orientation::Horizontal);
    column.VerticalAlignment(VerticalAlignment::Center);
    if (!side || ring) column.Height(display.tileHeight);
    column.CornerRadius({4, 4, 4, 4});
    column.Padding(side && !ring ? Thickness{kSideInset, 4, kSideInset, 4} :
                   ring ? Thickness{kSideInset, 0, kSideInset, 0} : Thickness{7, 0, 8, 0});
    column.Spacing(side ? 4 : 6);
    Grid identity = BuildTileIdentity(accounts, index, layout, display, tile);
    auto append = [&](FrameworkElement const& child) {
        if (side) child.HorizontalAlignment(HorizontalAlignment::Center);
        column.Children().Append(child);
    };

    if (ring) {
        Grid face;
        face.IsHitTestVisible(false);
        face.VerticalAlignment(VerticalAlignment::Center);
        Grid meters;
        face.Children().Append(meters);
        face.Children().Append(identity);
        tile.face = face;
        tile.meters = meters;
        append(face);
        return column;
    }
    append(identity);
    FontFamily displayFont(L"Segoe UI Variable Display, Segoe UI");

    if (IsFaceTile(layout)) {
        // Gauges and pies: the face beside the identity. A gauge's number and key are centred
        // across it, moved down from its middle to their model centres (UpdateAccountTile).
        Grid face;
        face.IsHitTestVisible(false);
        face.VerticalAlignment(VerticalAlignment::Center);
        Grid meters;
        face.Children().Append(meters);
        if (layout == BarLayout::Gauge) {
            auto centred = [&](TextBlock const& text) {
                text.TextLineBounds(TextLineBounds::Tight);
                text.HorizontalAlignment(HorizontalAlignment::Center);
                text.VerticalAlignment(VerticalAlignment::Center);
                text.RenderTransform(TranslateTransform{});
                face.Children().Append(text);
            };
            tile.headline = TextBlock{};
            tile.headline.FontFamily(displayFont);
            tile.headline.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            centred(tile.headline);
            tile.headlineKey = TextBlock{};
            tile.headlineKey.Opacity(0.6);
            centred(tile.headlineKey);
        }
        tile.face = face;
        tile.meters = meters;
        append(face);
        return column;
    }

    if (layout == BarLayout::Sparkline) {
        // The charts beside the identity, one per quota slot: each its plot's background, its
        // shapes (UpdateAccountTile), then its key at the right; the weekly chart's extra usage
        // key and number on one baseline at the left. UpdateAccountTile places, sizes and shows
        // them, the text in its corners.
        Grid face;
        face.IsHitTestVisible(false);
        face.VerticalAlignment(VerticalAlignment::Center);
        for (int w = 0; w < kQuotaBarCount; w++) {
            SparkChartUi& chart = tile.charts[w];
            chart.root = Grid{};
            chart.root.HorizontalAlignment(HorizontalAlignment::Left);
            chart.root.VerticalAlignment(VerticalAlignment::Top);
            chart.root.Visibility(Visibility::Collapsed);
            chart.plot = Border{};
            chart.plot.CornerRadius({kSparkPlotRadius, kSparkPlotRadius, kSparkPlotRadius, kSparkPlotRadius});
            chart.root.Children().Append(chart.plot);
            chart.shapes = Grid{};
            chart.root.Children().Append(chart.shapes);
            chart.key = TextBlock{};
            chart.key.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            chart.key.Opacity(0.75);
            chart.key.TextLineBounds(TextLineBounds::Tight);
            chart.key.HorizontalAlignment(HorizontalAlignment::Right);
            chart.root.Children().Append(chart.key);
            if (w == kWeeklyBar) {
                StackPanel corner;
                corner.Orientation(Orientation::Horizontal);
                corner.Spacing(kSparkExtraKeyGap);
                corner.HorizontalAlignment(HorizontalAlignment::Left);
                chart.extra = corner;
                chart.extraKey = TextBlock{};
                chart.extraKey.Opacity(0.6);
                chart.extraText = TextBlock{};
                chart.extraText.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
                for (TextBlock const& text : {chart.extraKey, chart.extraText}) {
                    text.TextLineBounds(TextLineBounds::Tight);
                    text.VerticalAlignment(VerticalAlignment::Bottom);
                    text.Visibility(Visibility::Collapsed);
                    corner.Children().Append(text);
                }
                chart.root.Children().Append(corner);
            }
            face.Children().Append(chart.root);
        }
        tile.face = face;
        tile.meters = face;
        append(face);
        return column;
    }

    if (layout == BarLayout::QuotaMatrix) {
        // Up to two rows and columns of cells beside the identity (on a side taskbar, a column of
        // up to four rows under it), each a quota's key and number on one baseline;
        // UpdateAccountTile places and sizes them.
        Grid matrix;
        matrix.IsHitTestVisible(false);
        matrix.VerticalAlignment(VerticalAlignment::Center);
        for (int k = 0; k < (side ? kQuotaBarCount : 2); k++) {
            RowDefinition row;
            row.Height({0, GridUnitType::Auto});
            matrix.RowDefinitions().Append(row);
        }
        for (int k = 0; k < (side ? 1 : 2); k++) {
            ColumnDefinition cellColumn;
            cellColumn.Width({0, GridUnitType::Auto});
            matrix.ColumnDefinitions().Append(cellColumn);
        }
        for (int w = 0; w < kQuotaBarCount; w++) {
            StackPanel cell;
            cell.Orientation(Orientation::Horizontal);
            cell.Spacing(kMatrixKeyGap);
            cell.Visibility(Visibility::Collapsed);
            TextBlock key;
            key.Opacity(0.6);
            key.TextLineBounds(TextLineBounds::Tight);
            key.VerticalAlignment(VerticalAlignment::Bottom);
            cell.Children().Append(key);
            TextBlock value;
            value.FontFamily(displayFont);
            value.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            value.TextLineBounds(TextLineBounds::Tight);
            value.VerticalAlignment(VerticalAlignment::Bottom);
            cell.Children().Append(value);
            matrix.Children().Append(cell);
            tile.matrixCells[w] = cell;
            tile.matrixKeys[w] = key;
            tile.matrixValues[w] = value;
        }
        tile.meters = matrix;
        append(matrix);
        return column;
    }

    // Headline: the most-used quota's number and window key over per-quota segments at the
    // headline sizes and the bar gap, or with the countdown, a clock glyph and the time until that
    // quota resets at the key's size.
    StackPanel meters;
    meters.IsHitTestVisible(false);
    meters.VerticalAlignment(VerticalAlignment::Center);
    tile.meters = meters;
    StackPanel headline;
    headline.IsHitTestVisible(false);
    headline.VerticalAlignment(VerticalAlignment::Center);
    headline.Spacing(3);
    StackPanel numberRow;
    numberRow.Orientation(Orientation::Horizontal);
    double fontSize = display.headlineFontSize, keySize = fontSize * kHeadlineKeyScale;
    tile.headline = TextBlock{};
    tile.headline.FontFamily(displayFont);
    tile.headline.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    tile.headline.FontSize(fontSize);
    tile.headline.TextLineBounds(TextLineBounds::Tight);
    tile.headline.VerticalAlignment(VerticalAlignment::Bottom);
    numberRow.Children().Append(tile.headline);
    tile.headlineKey = TextBlock{};
    tile.headlineKey.FontSize(keySize);
    tile.headlineKey.Opacity(0.6);
    tile.headlineKey.TextLineBounds(TextLineBounds::Tight);
    tile.headlineKey.VerticalAlignment(VerticalAlignment::Bottom);
    tile.headlineKey.Margin({3, 0, 0, 0});
    numberRow.Children().Append(tile.headlineKey);
    // The key follows the number; the row reserves "100%" and the widest key so the tile keeps
    // its width as values change.
    tile.headline.Text(L"100%");
    double reserve = std::ceil(TileTextWidthPerPt(tile.headline) * fontSize) + 3;
    tile.headline.Text(L"");
    double keyWidth = 0;
    for (PCWSTR key : {kQuotaKeys[0], kQuotaKeys[1], kQuotaKeys[2], kQuotaKeys[3], L"Ar", L"Cr", L"Mo"}) {
        tile.headlineKey.Text(key);
        keyWidth = std::max(keyWidth, TileTextWidthPerPt(tile.headlineKey) * keySize);
    }
    tile.headlineKey.Text(L"");
    numberRow.MinWidth(reserve + std::ceil(keyWidth));
    headline.Children().Append(numberRow);
    if (display.headlineCountdown) {
        // Behind a reset arrow; reserves "00h 00m", so minute ticks keep the tile's width.
        tile.countdown = TextBlock{};
        tile.countdown.FontSize(keySize);
        tile.countdown.Opacity(0.7);
        tile.countdown.TextLineBounds(TextLineBounds::Tight);
        tile.countdown.HorizontalAlignment(HorizontalAlignment::Left);
        tile.countdown.Text(kHeadlineCountdownReserve);
        tile.countdown.MinWidth(std::ceil(TileTextWidthPerPt(tile.countdown) * keySize));
        tile.countdown.Text(L"");
        headline.Children().Append(tile.countdown);
    } else {
        meters.Orientation(Orientation::Horizontal);
        meters.HorizontalAlignment(HorizontalAlignment::Left);
        meters.Spacing(display.barGap);
        headline.Children().Append(meters);
    }
    append(headline);
    return column;
}

// The identity's state: dimmed while stale, like a label; the warning mark in place of the
// label's "!"; the badge's halo color for a light (`light`) or dark taskbar.
static void UpdateTileIdentity(TileUiRefs const& tile, AppliedState& ap, AccountMeters const& meters,
                               MeterDisplay const& display, bool light) {
    if (!tile.identity) return;
    if (meters.identityOpacity != ap.identityOpacity) {
        tile.identity.Opacity(meters.identityOpacity);
        ap.identityOpacity = meters.identityOpacity;
    }
    if ((meters.warn ? 1 : 0) != ap.tileWarn) {
        tile.warn.Visibility(meters.warn ? Visibility::Visible : Visibility::Collapsed);
        ap.tileWarn = meters.warn ? 1 : 0;
    }
    if (tile.badgeHalo && (light ? 1 : 0) != ap.badgeLight) {
        tile.badgeHalo.Color(ArgbColor(BadgeColorsFor(display.badgeColor, light).halo));
        ap.badgeLight = light ? 1 : 0;
    }
}

// `meters`: AccountMeterState of the account in the tile's layout. `light`: light taskbar chrome.
static void UpdateAccountTile(QuotaUiInstance& state, size_t index, AccountMeters const& meters,
                              MeterDisplay const& display, bool light) {
    AccountUiRefs& ui = state.accountRefs[index];
    AppliedState& ap = state.applied[index];
    TileUiRefs& tile = ui.tile;
    if (!tile.identity || !tile.meters) return;
    BarLayout layout = tile.layout;
    bool face = IsFaceTile(layout);
    bool spark = layout == BarLayout::Sparkline, matrix = layout == BarLayout::QuotaMatrix;
    int barMask = meters.barMask;
    TileRingGeometry const& geometry = meters.ring;
    double thickness = display.headlineBarThickness;

    // Meter shapes: regenerated, and the identity resized, whenever an input changes. Sparklines
    // key them on the face's size, the quota matrix on its font.
    TileShapeKey shape{(int)layout, barMask,
                       spark ? meters.faceHeight : matrix ? meters.matrixFont : geometry.size,
                       spark ? meters.faceWidth : geometry.stroke, geometry.gap, light ? 1 : 0,
                       display.showPaceTicks ? (int)display.paceTickColor : -1};
    if (shape != ap.tileShape) {
        TilePalette palette = TilePaletteFor(light);
        SolidColorBrush track(ArgbColor(palette.track));
        PaceMarkColors paceColors = PaceMarkColorsFor(display.paceTickColor, light);
        SolidColorBrush paceCore(ArgbColor(paceColors.core));
        SolidColorBrush paceHalo(ArgbColor(paceColors.halo));
        tile.meters.Visibility(barMask ? Visibility::Visible : Visibility::Collapsed);
        auto meterShapes = tile.meters.Children();
        if (!matrix && !spark) {
            meterShapes.Clear();
            ui.fills.fill(nullptr);
            tile.arcs.fill(nullptr);
            tile.paceHalos.fill(nullptr);
            tile.paceCores.fill(nullptr);
        }

        if (matrix) {
            // Each shown cell in its place; keys reserve the widest key and numbers "100%" at
            // their fonts, so the tile keeps its width as numbers change. Spacing goes only
            // between rows and columns in use.
            TextBlock keyProbe, valueProbe;
            keyProbe.FontSize(meters.matrixKeyFont);
            double keyWidth = 0;
            for (PCWSTR key : {kQuotaKeys[0], kQuotaKeys[1], kQuotaKeys[2], kQuotaKeys[3], L"Ar", L"Cr", L"Mo"}) {
                keyProbe.Text(key);
                keyWidth = std::max(keyWidth, TileTextWidthPerPt(keyProbe) * meters.matrixKeyFont);
            }
            valueProbe.FontFamily(FontFamily(L"Segoe UI Variable Display, Segoe UI"));
            valueProbe.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            valueProbe.FontSize(meters.matrixFont);
            valueProbe.Text(L"100%");
            double valueWidth = TileTextWidthPerPt(valueProbe) * meters.matrixFont;
            int rows = 1, columns = 1;
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!tile.matrixCells[w]) continue;
                bool shown = barMask & (1 << w);
                tile.matrixCells[w].Visibility(shown ? Visibility::Visible : Visibility::Collapsed);
                if (!shown) continue;
                QuotaMeter const& m = meters.quotas[w];
                Grid::SetRow(tile.matrixCells[w], m.matrixRow);
                Grid::SetColumn(tile.matrixCells[w], m.matrixColumn);
                rows = std::max(rows, m.matrixRow + 1);
                columns = std::max(columns, m.matrixColumn + 1);
                tile.matrixKeys[w].FontSize(meters.matrixKeyFont);
                tile.matrixKeys[w].MinWidth(std::ceil(keyWidth));
                tile.matrixKeys[w].Text(m.key);
                tile.matrixValues[w].FontSize(meters.matrixFont);
                tile.matrixValues[w].MinWidth(std::ceil(valueWidth));
            }
            if (auto grid = tile.meters.try_as<Grid>()) {
                grid.RowSpacing(rows > 1 ? kMatrixRowGap : 0);
                grid.ColumnSpacing(columns > 1 ? kMatrixColumnGap : 0);
            }
        } else if (spark) {
            // Each charted quota's chart at its place in the face, as large as its plot; its
            // shapes larger by kSparkOverflow on every side and pulled back by negative margins, so
            // layout never clips marks at its edges. In SparkShape order, stroked per
            // kSparkShapeStyles; the values below draw and color them.
            tile.face.Width(meters.faceWidth);
            tile.face.Height(meters.faceHeight);
            for (int w = 0; w < kQuotaBarCount; w++) {
                SparkChartUi& chart = tile.charts[w];
                bool shown = barMask & (1 << w);
                chart.root.Visibility(shown ? Visibility::Visible : Visibility::Collapsed);
                chart.shapes.Children().Clear();
                chart.paths.clear();
                if (!shown) continue;
                chart.root.Margin({meters.quotas[w].sparkLeft, meters.quotas[w].sparkTop, 0, 0});
                chart.root.Width(display.sparklineWidth);
                chart.root.Height(meters.sparkChartHeight);
                chart.plot.Background(SolidColorBrush(ArgbColor(palette.plot)));
                chart.key.FontSize(meters.sparkKeyFont);
                chart.key.Text(meters.quotas[w].key);
                if (chart.extra) {
                    // Clipped to the plot's insets.
                    chart.extra.MaxWidth(std::max(display.sparklineWidth - 2 * kSparkTextInset, 0.0));
                    chart.extraKey.FontSize(meters.sparkKeyFont);
                    chart.extraText.FontSize(meters.sparkKeyFont);
                }
                for (SparkShapeStyle const& style : kSparkShapeStyles) {
                    wuxs::Path path;
                    path.Width(display.sparklineWidth + 2 * kSparkOverflow);
                    path.Height(meters.sparkChartHeight + 2 * kSparkOverflow);
                    path.Margin({-kSparkOverflow, -kSparkOverflow, -kSparkOverflow, -kSparkOverflow});
                    path.Visibility(Visibility::Collapsed);
                    if (style.stroke > 0) {
                        path.StrokeThickness(style.stroke);
                        if (style.dashes[0] > 0) {
                            DoubleCollection dashes;
                            for (double length : style.dashes) dashes.Append(length);
                            path.StrokeDashArray(dashes);
                        } else {
                            path.StrokeLineJoin(PenLineJoin::Round);
                            path.StrokeStartLineCap(PenLineCap::Round);
                            path.StrokeEndLineCap(PenLineCap::Round);
                        }
                    }
                    chart.shapes.Children().Append(path);
                    chart.paths.push_back(path);
                }
            }
        } else if (face) {
            tile.face.Width(meters.faceWidth);
            tile.face.Height(meters.faceHeight);
            // Larger than the face by kTileRingOverflow on every side, pulled back by negative
            // margins, so layout never clips the pace ticks.
            auto ringShape = [&](Brush const& fill) {
                wuxs::Path path;
                path.Width(meters.faceWidth + 2 * kTileRingOverflow);
                path.Height(meters.faceHeight + 2 * kTileRingOverflow);
                path.Margin({-kTileRingOverflow, -kTileRingOverflow, -kTileRingOverflow,
                             -kTileRingOverflow});
                path.Fill(fill);
                return path;
            };
            // Tracks, then fills, then pace ticks on top.
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                wuxs::Path trackArc = ringShape(track);
                trackArc.Data(TileOutlineGeometry(meters.quotas[w].track, kTileRingOverflow));
                meterShapes.Append(trackArc);
            }
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                tile.arcs[w] = ringShape(track);
                tile.arcs[w].Visibility(Visibility::Collapsed);
                meterShapes.Append(tile.arcs[w]);
            }
            for (int w = 0; display.showPaceTicks && w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                tile.paceHalos[w] = ringShape(paceHalo);
                tile.paceCores[w] = ringShape(paceCore);
                tile.paceHalos[w].Visibility(Visibility::Collapsed);
                tile.paceCores[w].Visibility(Visibility::Collapsed);
                meterShapes.Append(tile.paceHalos[w]);
                meterShapes.Append(tile.paceCores[w]);
            }

            if (IsRingTile(layout)) {
                // Identity, sized to the hole; the badge's box centred on the model's.
                if (tile.logo) {
                    tile.logo.Width(geometry.logo);
                    tile.logo.Height(geometry.logo);
                }
                if (tile.badge) {
                    BadgeBox const& box = meters.ringBadge;
                    Canvas::SetLeft(tile.badge, box.left + box.width / 2 - tile.badge.Width() / 2);
                    Canvas::SetTop(tile.badge, box.top + box.height / 2 - tile.badge.Height() / 2);
                    // Down a side taskbar, an offset badge's reach past the ring counts across it,
                    // as on other tiles' identities.
                    if (display.sideWidth > 0) {
                        double reach = std::max({-box.left, box.left + box.width - meters.faceWidth, 0.0});
                        tile.face.Margin({reach, 0, reach, 0});
                    }
                }
                if (tile.text) {
                    tile.text.FontSize(meters.ringTextFont);
                    tile.text.MaxWidth(meters.ringTextWidth);
                }
                tile.warn.Margin({0, meters.ringWarnTop, -3, 0});
            } else if (tile.headline && tile.headlineKey) {
                // A gauge's number and key at their fonts and centres.
                tile.headline.FontSize(meters.faceFont);
                tile.headlineKey.FontSize(meters.faceKeyFont);
                double middle = meters.faceHeight / 2;
                if (auto shift = tile.headline.RenderTransform().try_as<TranslateTransform>()) {
                    shift.Y(meters.faceNumberCenter - middle);
                }
                if (auto shift = tile.headlineKey.RenderTransform().try_as<TranslateTransform>()) {
                    shift.Y(meters.faceKeyCenter - middle);
                }
            }
        } else if (!tile.countdown) {
            // Headline segments: corners a quarter of the thickness.
            double radius = thickness / 4;
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                Grid bar;
                bar.Width(meters.headlineBarLength);
                bar.Height(thickness);
                Border trackBar;
                trackBar.CornerRadius({radius, radius, radius, radius});
                trackBar.Background(track);
                bar.Children().Append(trackBar);
                Border fill;
                fill.HorizontalAlignment(HorizontalAlignment::Left);
                fill.Width(0);
                bar.Children().Append(fill);
                ui.fills[w] = fill;
                meterShapes.Append(bar);
            }
        }
        ap.tileShape = shape;
        ap.fillPx.fill(-1);
        ap.fillColor.fill(0);
        ap.pacePx.fill(-1);
        ap.paceVisible.fill(-1);
        ap.percentTexts.fill(L"");
        for (std::vector<double>& drawn : ap.spark) drawn.clear();
        ap.highlight = -1;
    }

    // Values, applied when their update keys change.
    if (matrix) {
        for (int w = 0; w < kQuotaBarCount; w++) {
            QuotaMeter const& m = meters.quotas[w];
            if (!(barMask & (1 << w)) || !tile.matrixValues[w] ||
                (m.text == ap.percentTexts[w] && m.color == ap.fillColor[w])) {
                continue;
            }
            tile.matrixValues[w].Text(m.text);
            tile.matrixValues[w].Foreground(SolidColorBrush(ArgbColor(m.color)));
            ap.percentTexts[w] = m.text;
            ap.fillColor[w] = m.color;
        }
    } else if (spark) {
        // Each chart's shapes and its key's corner, redrawn when any point, color or the corner
        // changes.
        const double inset = kSparkTextInset;
        for (int w = 0; w < kQuotaBarCount; w++) {
            SparkChartUi& chart = tile.charts[w];
            if (!(barMask & (1 << w)) || chart.paths.size() != kSparkShapeCount) continue;
            QuotaMeter const& m = meters.quotas[w];
            std::array<uint32_t, kSparkShapeCount> colors;
            std::vector<double> drawn{m.sparkKeyTop ? 1.0 : 0.0};
            for (int k = 0; k < kSparkShapeCount; k++) {
                colors[k] = SparkShapeColor(k, meters, w, display, light);
                drawn.insert(drawn.end(), {(double)colors[k], (double)m.spark[k].size()});
                for (TileOutlineFigure const& figure : m.spark[k]) {
                    drawn.insert(drawn.end(), {figure.x, figure.y, (double)figure.segments.size()});
                    for (TileOutlineSegment const& segment : figure.segments) {
                        drawn.insert(drawn.end(), {segment.x, segment.y, segment.radius, segment.clockwise ? 1.0 : 0.0});
                    }
                }
            }
            if (drawn == ap.spark[w]) continue;
            chart.key.VerticalAlignment(m.sparkKeyTop ? VerticalAlignment::Top : VerticalAlignment::Bottom);
            chart.key.Margin(m.sparkKeyTop ? Thickness{0, inset, inset, 0} : Thickness{0, 0, inset, inset});
            for (int k = 0; k < kSparkShapeCount; k++) {
                wuxs::Path& path = chart.paths[k];
                path.Visibility(m.spark[k].empty() ? Visibility::Collapsed : Visibility::Visible);
                if (m.spark[k].empty()) continue;
                bool stroked = kSparkShapeStyles[k].stroke > 0;
                Geometry data = TileOutlineGeometry(m.spark[k], kSparkOverflow, !stroked);
                if (auto geometry = data.try_as<PathGeometry>()) geometry.FillRule(FillRule::Nonzero);
                path.Data(data);
                SolidColorBrush brush(ArgbColor(colors[k]));
                if (stroked) {
                    path.Stroke(brush);
                } else {
                    path.Fill(brush);
                }
            }
            ap.spark[w] = std::move(drawn);
        }

        // The weekly chart's extra usage in its corner: its key, collapsed without one, and number.
        SparkChartUi& weekly = tile.charts[kWeeklyBar];
        std::wstring extra = meters.sparkExtraKey + L"\n" + meters.sparkExtraText + (meters.sparkExtraBottom ? L"\nb" : L"\nt");
        if (weekly.extra && (extra != ap.sparkExtra || meters.sparkExtraColor != ap.sparkExtraColor)) {
            bool bottom = meters.sparkExtraBottom;
            weekly.extra.VerticalAlignment(bottom ? VerticalAlignment::Bottom : VerticalAlignment::Top);
            weekly.extra.Margin(bottom ? Thickness{inset, 0, 0, inset} : Thickness{inset, inset, 0, 0});
            weekly.extraKey.Text(meters.sparkExtraKey);
            weekly.extraKey.Visibility(meters.sparkExtraKey.empty() ? Visibility::Collapsed : Visibility::Visible);
            weekly.extraText.Text(meters.sparkExtraText);
            weekly.extraText.Visibility(meters.sparkExtraText.empty() ? Visibility::Collapsed : Visibility::Visible);
            if (meters.sparkExtraColor) {
                weekly.extraText.Foreground(SolidColorBrush(ArgbColor(meters.sparkExtraColor)));
            } else {
                weekly.extraText.ClearValue(TextBlock::ForegroundProperty());
            }
            ap.sparkExtra = std::move(extra);
            ap.sparkExtraColor = meters.sparkExtraColor;
        }
    }
    for (int w = 0; w < kQuotaBarCount && !matrix && !spark; w++) {
        if (!(barMask & (1 << w))) continue;
        QuotaMeter const& m = meters.quotas[w];
        if (m.fillUnits != ap.fillPx[w] || m.color != ap.fillColor[w]) {
            if (face && tile.arcs[w]) {
                // Face fills are flat.
                tile.arcs[w].Visibility(m.fillUnits ? Visibility::Visible : Visibility::Collapsed);
                if (m.fillUnits) tile.arcs[w].Data(TileOutlineGeometry(m.fill, kTileRingOverflow));
                tile.arcs[w].Fill(SolidColorBrush(ArgbColor(m.color)));
            } else if (!face && ui.fills[w]) {
                // Start corners round with the track, shaded like the bars' fills.
                double start = thickness / 4, end = m.fillEndRadius;
                ui.fills[w].Width(m.fillLength);
                ui.fills[w].CornerRadius({start, end, end, start});
                ui.fills[w].Background(UsageFillBrush(m.color, false));
            }
            ap.fillPx[w] = m.fillUnits;
            ap.fillColor[w] = m.color;
        }

        // A face tile's tick is a halo and a core shape; headline segments have none.
        bool paceVisible = tile.paceHalos[w] && tile.paceCores[w] && m.paceVisible;
        if ((paceVisible ? 1 : 0) != ap.paceVisible[w]) {
            Visibility visibility = paceVisible ? Visibility::Visible : Visibility::Collapsed;
            if (tile.paceHalos[w]) tile.paceHalos[w].Visibility(visibility);
            if (tile.paceCores[w]) tile.paceCores[w].Visibility(visibility);
            ap.paceVisible[w] = paceVisible ? 1 : 0;
        }
        if (paceVisible && m.paceUnits != ap.pacePx[w]) {
            tile.paceHalos[w].Data(TileOutlineGeometry(m.paceHalo, kTileRingOverflow));
            tile.paceCores[w].Data(TileOutlineGeometry(m.paceCore, kTileRingOverflow));
            ap.pacePx[w] = m.paceUnits;
        }
    }

    if (tile.headlineKey) {
        std::wstring headline = meters.headline + L"\n" + meters.headlineKey + L"\n" + meters.headlineCountdown;
        if (headline != ap.headline || meters.headlineColor != ap.headlineColor) {
            if (tile.headline) {
                tile.headline.Text(meters.headline);
                tile.headline.Foreground(SolidColorBrush(ArgbColor(meters.headlineColor)));
            }
            tile.headlineKey.Text(meters.headlineKey);
            if (tile.countdown) tile.countdown.Text(meters.headlineCountdown);
            ap.headline = std::move(headline);
            ap.headlineColor = meters.headlineColor;
        }
    }

    UpdateTileIdentity(tile, ap, meters, display, light);
}
