// Tile layouts: segmented ring, concentric rings, logo + mini bars and logo + headline %. A tile
// shows the account's identity (provider logo, optionally with a number badge, or short text) and
// its tile quotas: the meters the bar layouts show, in slot order. BuildAccountTile creates a tile;
// UpdateAccountTile regenerates its meter shapes whenever their inputs change, then applies values
// in place. Values, identity text and geometry come from the meter model (meter-model.h).

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
    TilePalette palette = TilePaletteFor(state.lightTheme);
    try {
        ui.column.Background(SolidColorBrush(ArgbColor(level == 2 ? palette.open :
                                                       level == 1 ? palette.hover : 0)));
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
    TileIdentityText identityText = TileIdentityFor(accounts, index);
    if (tile.logo && identityText.badge) {
        tile.badgeText = TextBlock{};
        tile.badgeText.Text(identityText.badgeText);
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
        tile.text = TextBlock{};
        tile.text.Text(identityText.text);
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

// `meters`: AccountMeterState of the account in the tile's layout. `light`: light taskbar chrome.
static void UpdateAccountTile(QuotaUiInstance& state, size_t index, AccountMeters const& meters,
                              MeterDisplay const& display, bool light) {
    AccountUiRefs& ui = state.accountRefs[index];
    AppliedState& ap = state.applied[index];
    TileUiRefs& tile = ui.tile;
    if (!tile.identity || !tile.meters) return;
    BarLayout layout = tile.layout;
    bool ring = layout == BarLayout::ConcentricRings || layout == BarLayout::SegmentedRing;
    bool miniBars = layout == BarLayout::LogoBars;
    int barMask = meters.barMask;
    TileRingGeometry const& geometry = meters.ring;
    double center = geometry.size / 2;

    // Meter shapes: regenerated, and the identity resized, whenever an input changes.
    TileShapeKey shape{(int)layout, barMask, geometry.size, geometry.stroke, geometry.gap,
                       light ? 1 : 0, display.showPaceTicks ? (int)display.paceTickColor : -1};
    if (shape != ap.tileShape) {
        TilePalette palette = TilePaletteFor(light);
        SolidColorBrush track(ArgbColor(palette.track));
        SolidColorBrush paceCore(winrt::Windows::UI::Color{
            0xFF, GetRValue(display.paceTickColor), GetGValue(display.paceTickColor),
            GetBValue(display.paceTickColor)});
        SolidColorBrush paceOutline(winrt::Windows::UI::Color{0x8C, 0, 0, 0});
        auto meterShapes = tile.meters.Children();
        meterShapes.Clear();
        ui.fills.fill(nullptr);
        ui.paceTicks.fill(nullptr);
        tile.arcs.fill(nullptr);
        tile.dots.fill(nullptr);
        tile.meters.Visibility(barMask ? Visibility::Visible : Visibility::Collapsed);

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
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                QuotaMeter const& m = meters.quotas[w];
                wuxs::Path trackArc = arcShape(track);
                trackArc.Data(TileArcGeometry(center, m.radius, m.arc.start, m.arc.sweep));
                meterShapes.Append(trackArc);
            }
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                tile.arcs[w] = arcShape(track);
                tile.arcs[w].Visibility(Visibility::Collapsed);
                meterShapes.Append(tile.arcs[w]);
            }
            for (int w = 0; display.showPaceTicks && w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                tile.dots[w] = ringShape();
                tile.dots[w].Fill(paceCore);
                tile.dots[w].Stroke(paceOutline);
                tile.dots[w].StrokeThickness(0.8);
                tile.dots[w].Visibility(Visibility::Collapsed);
                meterShapes.Append(tile.dots[w]);
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
                tile.text.FontSize(meters.ringTextFont);
                tile.text.MaxWidth(meters.ringTextWidth);
            }
            tile.warn.Margin({0, meters.ringWarnTop, -3, 0});
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
                meterShapes.Append(bar);
            }
        }
        if (tile.badge) {
            tile.badge.Background(SolidColorBrush(ArgbColor(palette.badge)));
            tile.badge.BorderBrush(SolidColorBrush(ArgbColor(palette.badgeOutline)));
        }
        ap.tileShape = shape;
        ap.fillPx.fill(-1);
        ap.fillColor.fill(0);
        ap.pacePx.fill(-1);
        ap.paceVisible.fill(-1);
        ap.highlight = -1;
    }

    // Values, applied when their update keys change.
    for (int w = 0; w < kQuotaBarCount; w++) {
        if (!(barMask & (1 << w))) continue;
        QuotaMeter const& m = meters.quotas[w];
        if (m.fillUnits != ap.fillPx[w] || m.color != ap.fillColor[w]) {
            SolidColorBrush brush(ArgbColor(m.color));
            if (ring && tile.arcs[w]) {
                tile.arcs[w].Visibility(m.fillUnits ? Visibility::Visible : Visibility::Collapsed);
                if (m.fillUnits) {
                    tile.arcs[w].Data(TileArcGeometry(center, m.radius, m.arc.start, m.fillSweep));
                }
                tile.arcs[w].Stroke(brush);
            } else if (!ring && ui.fills[w]) {
                ui.fills[w].Width(m.fillLength);
                ui.fills[w].Background(brush);
            }
            ap.fillPx[w] = m.fillUnits;
            ap.fillColor[w] = m.color;
        }

        FrameworkElement marker = ring ? tile.dots[w].try_as<FrameworkElement>() :
                                         ui.paceTicks[w].try_as<FrameworkElement>();
        bool paceVisible = marker && m.paceVisible;
        if ((paceVisible ? 1 : 0) != ap.paceVisible[w]) {
            if (marker) {
                marker.Visibility(paceVisible ? Visibility::Visible : Visibility::Collapsed);
            }
            ap.paceVisible[w] = paceVisible ? 1 : 0;
        }
        if (paceVisible && m.paceUnits != ap.pacePx[w]) {
            if (ring) {
                EllipseGeometry dot;
                dot.Center(TileRingPoint(center, m.radius, m.paceAngle));
                dot.RadiusX(geometry.dot);
                dot.RadiusY(geometry.dot);
                tile.dots[w].Data(dot);
            } else {
                ui.paceTicks[w].Margin({m.paceOffset, -2, 0, -2});
            }
            ap.pacePx[w] = m.paceUnits;
        }
    }

    if (tile.headline) {
        std::wstring headline = meters.headline + L"\n" + meters.headlineKey;
        if (headline != ap.headline || meters.headlineColor != ap.headlineColor) {
            tile.headline.Text(meters.headline);
            tile.headline.Foreground(SolidColorBrush(ArgbColor(meters.headlineColor)));
            tile.headlineKey.Text(meters.headlineKey);
            ap.headline = std::move(headline);
            ap.headlineColor = meters.headlineColor;
        }
    }

    // Identity dims while stale, like the bars' label; the warning mark replaces their "!".
    if (meters.identityOpacity != ap.identityOpacity) {
        tile.identity.Opacity(meters.identityOpacity);
        ap.identityOpacity = meters.identityOpacity;
    }
    if ((meters.warn ? 1 : 0) != ap.tileWarn) {
        tile.warn.Visibility(meters.warn ? Visibility::Visible : Visibility::Collapsed);
        ap.tileWarn = meters.warn ? 1 : 0;
    }
}
