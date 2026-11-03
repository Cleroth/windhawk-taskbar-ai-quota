// Tile layouts: segmented ring, concentric rings and logo + headline %. A tile shows the account's
// identity (provider logo, optionally with a number badge, or short text) and its tile quotas: the
// meters the bar layouts show, in slot order. BuildAccountTile creates a tile; UpdateAccountTile
// regenerates its meter shapes whenever their inputs change, then applies values in place. Bar
// columns with a logo identity show the same identity (BuildTileIdentity, UpdateTileIdentity), and
// every column takes the tiles' hover and open-flyout background. Values, identity text and
// geometry come from the meter model (meter-model.h).

// A ring tile shape (meter-model.h), moved `offset` DIPs right and down. PathGeometry fills
// even-odd by default, as the outline expects.
static Geometry TileOutlineGeometry(TileOutline const& outline, double offset) {
    PathGeometry geometry;
    for (TileOutlineFigure const& source : outline) {
        PathFigure figure;
        figure.StartPoint({(float)(source.x + offset), (float)(source.y + offset)});
        figure.IsClosed(true);
        figure.IsFilled(true);
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
// chosen), with a number badge when another configured account shares the provider (UpdateQuotaUi
// shows it while a visible one does), or text, and the stale-warning mark. Rings
// resize the logo to their hole and place the badge; headline tiles take a 16 DIP logo, other
// columns 15 DIP, at the logo size. UpdateTileIdentity applies its state.
static Grid BuildTileIdentity(std::vector<AccountConfig> const& accounts, size_t index,
                              BarLayout layout, MeterDisplay const& display, TileUiRefs& tile) {
    AccountConfig const& account = accounts[index];
    bool ring = layout == BarLayout::SegmentedRing || layout == BarLayout::ConcentricRings;
    double side = (layout == BarLayout::LogoHeadline ? kHeadlineLogoSide : kBarLogoSide) * display.logoScale;
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
// containers. UpdateAccountTile sizes and fills the meters.
static StackPanel BuildAccountTile(std::vector<AccountConfig> const& accounts, size_t index,
                                   BarLayout layout, MeterDisplay const& display, AccountUiRefs& refs) {
    TileUiRefs& tile = refs.tile;
    tile.layout = layout;
    bool ring = layout == BarLayout::SegmentedRing || layout == BarLayout::ConcentricRings;

    StackPanel column;
    column.Orientation(Orientation::Horizontal);
    column.VerticalAlignment(VerticalAlignment::Center);
    column.Height(display.tileHeight);
    column.CornerRadius({4, 4, 4, 4});
    column.Padding(ring ? Thickness{4, 0, 4, 0} : Thickness{7, 0, 8, 0});
    column.Spacing(6);
    Grid identity = BuildTileIdentity(accounts, index, layout, display, tile);

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

    // Headline: the most-used quota's number and window key over per-quota segments, at the
    // headline sizes and the bar gap.
    column.Children().Append(identity);
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
    tile.headline.FontFamily(FontFamily(L"Segoe UI Variable Display, Segoe UI"));
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
    for (PCWSTR key : {kQuotaKeys[0], kQuotaKeys[1], kQuotaKeys[2], kQuotaKeys[3], L"Cr"}) {
        tile.headlineKey.Text(key);
        keyWidth = std::max(keyWidth, TileTextWidthPerPt(tile.headlineKey) * keySize);
    }
    tile.headlineKey.Text(L"");
    numberRow.MinWidth(reserve + std::ceil(keyWidth));
    headline.Children().Append(numberRow);
    meters.Orientation(Orientation::Horizontal);
    meters.HorizontalAlignment(HorizontalAlignment::Left);
    meters.Spacing(display.barGap);
    headline.Children().Append(meters);
    column.Children().Append(headline);
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
    bool ring = layout == BarLayout::ConcentricRings || layout == BarLayout::SegmentedRing;
    int barMask = meters.barMask;
    TileRingGeometry const& geometry = meters.ring;
    double thickness = display.headlineBarThickness;

    // Meter shapes: regenerated, and the identity resized, whenever an input changes.
    TileShapeKey shape{(int)layout, barMask, geometry.size, geometry.stroke, geometry.gap,
                       light ? 1 : 0, display.showPaceTicks ? (int)display.paceTickColor : -1};
    if (shape != ap.tileShape) {
        TilePalette palette = TilePaletteFor(light);
        SolidColorBrush track(ArgbColor(palette.track));
        PaceMarkColors paceColors = PaceMarkColorsFor(display.paceTickColor, light);
        SolidColorBrush paceCore(ArgbColor(paceColors.core));
        SolidColorBrush paceHalo(ArgbColor(paceColors.halo));
        auto meterShapes = tile.meters.Children();
        meterShapes.Clear();
        ui.fills.fill(nullptr);
        tile.arcs.fill(nullptr);
        tile.paceHalos.fill(nullptr);
        tile.paceCores.fill(nullptr);
        tile.meters.Visibility(barMask ? Visibility::Visible : Visibility::Collapsed);

        if (ring) {
            double size = geometry.size;
            tile.face.Width(size);
            tile.face.Height(size);
            // Larger than the ring area by kTileRingOverflow on every side, pulled back by
            // negative margins, so layout never clips the pace ticks.
            auto ringShape = [&](Brush const& fill) {
                wuxs::Path path;
                path.Width(size + 2 * kTileRingOverflow);
                path.Height(size + 2 * kTileRingOverflow);
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

            // Identity, sized to the hole; the badge's box centred on the model's.
            if (tile.logo) {
                tile.logo.Width(geometry.logo);
                tile.logo.Height(geometry.logo);
            }
            if (tile.badge) {
                BadgeBox const& box = meters.ringBadge;
                Canvas::SetLeft(tile.badge, box.left + box.width / 2 - tile.badge.Width() / 2);
                Canvas::SetTop(tile.badge, box.top + box.height / 2 - tile.badge.Height() / 2);
            }
            if (tile.text) {
                tile.text.FontSize(meters.ringTextFont);
                tile.text.MaxWidth(meters.ringTextWidth);
            }
            tile.warn.Margin({0, meters.ringWarnTop, -3, 0});
        } else {
            // Headline segments: corners a quarter of the thickness.
            double radius = thickness / 4;
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(barMask & (1 << w))) continue;
                Grid bar;
                bar.Width(display.headlineBarLength);
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
        ap.highlight = -1;
    }

    // Values, applied when their update keys change.
    for (int w = 0; w < kQuotaBarCount; w++) {
        if (!(barMask & (1 << w))) continue;
        QuotaMeter const& m = meters.quotas[w];
        if (m.fillUnits != ap.fillPx[w] || m.color != ap.fillColor[w]) {
            if (ring && tile.arcs[w]) {
                // Ring fills are flat.
                tile.arcs[w].Visibility(m.fillUnits ? Visibility::Visible : Visibility::Collapsed);
                if (m.fillUnits) tile.arcs[w].Data(TileOutlineGeometry(m.fill, kTileRingOverflow));
                tile.arcs[w].Fill(SolidColorBrush(ArgbColor(m.color)));
            } else if (!ring && ui.fills[w]) {
                // Start corners round with the track, shaded like the bars' fills.
                double start = thickness / 4, end = m.fillEndRadius;
                ui.fills[w].Width(m.fillLength);
                ui.fills[w].CornerRadius({start, end, end, start});
                ui.fills[w].Background(UsageFillBrush(m.color, false));
            }
            ap.fillPx[w] = m.fillUnits;
            ap.fillColor[w] = m.color;
        }

        // A ring's tick is a halo and a core shape; headline segments have none.
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

    UpdateTileIdentity(tile, ap, meters, display, light);
}
