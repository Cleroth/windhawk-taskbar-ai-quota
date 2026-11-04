// Settings preview: the taskbar strip under the Layout and Display titles and the tile style
// gallery's thumbnails, drawn from the shared meter model (taskbar/meter-model.h) with the
// visual test accounts at 1:1 DIPs; the strip places them on a mock taskbar at the chosen position.
// Pure: the layout mirrors the taskbar's XAML (quota-grid.h, tiles.h, quota-update.h) as draw
// operations; painting takes an explicit target and theme.
//
// Units: the strip is as tall as the primary taskbar in DIPs (converted with that taskbar's
// DPI); the meter model's pixel details (pace ticks, text offsets) and snapping use the preview
// window's scale. Text uses the fonts the taskbar's XAML resolves: Segoe UI Variable with
// automatic optical size (its default font) for labels, percentages and keys, the named Display
// and Small instances for tile text and badges.

// One draw operation, in strip DIPs. Colors are ARGB.
struct SettingsPreviewOp {
    enum class Kind : uint8_t { Rect, Shape, Stroke, Polygon, Text, Logo };
    Kind kind = Kind::Rect;
    int hover = -1;                     // Drawn only while this column is hovered; -1: always
    D2D1_RECT_F rect{};                 // Rect, Text box (aligns in its width), Logo box
    float radius = 0;                   // Rect corners
    // Rect corners one by one instead (top-left, top-right, bottom-right, bottom-left), scaled
    // down together where they don't fit
    std::optional<std::array<float, 4>> corners;
    // Rect: shaded from this color at the top edge (left with shadeLeft) to `color` 60% across;
    // 0: flat
    uint32_t shade = 0;
    bool shadeLeft = false;
    uint32_t color = 0;                 // Fill or text color
    float opacity = 1;
    TileOutline shape;                  // Shape: a tile shape, filled, moved to `origin`; Stroke: its
    D2D1_POINT_2F origin{};             // open figures, stroked `strokeWidth` wide, dashed by
    float strokeWidth = 0;              // `dashes` in stroke widths (none: solid, round joins and
    std::vector<float> dashes;          // caps)
    bool winding = false;               // Shape filled nonzero instead of even-odd
    std::vector<D2D1_POINT_2F> points;  // Polygon
    std::wstring text;
    UiTextStyle style;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    bool clip = false;                  // Text clipped to its box (XAML MaxWidth, clip trimming)
    PCWSTR path = nullptr;              // Logo path data (24 DIP view box), scaled to the box
    const LogoBrand* brand = nullptr;   // Logo: its brand colors outside high contrast, else `color`
};

// A piece of the mock taskbar around the tiles: its operations from its own left edge.
struct SettingsPreviewPiece {
    std::vector<SettingsPreviewOp> ops;
    float width = 0;
};

// A strip: one column per sample account, laid out from x 0 (the first column's margin box) and
// centered on the strip's height, and the mock taskbar's pieces in its order: Start, the apps,
// then the tray's notification icons, network group, clock and Show desktop (none in the gallery).
struct SettingsPreviewStrip {
    std::vector<SettingsPreviewOp> ops;
    std::vector<SettingsPreviewPiece> pieces;
    std::vector<D2D1_RECT_F> columns;  // Hover targets: the XAML columns without their margins
    float width = 0;                   // Columns with their margins
    float height = 0;
    float rightMargin = 0;             // Gap after accounts
    TaskbarPlacement placement = TaskbarPlacement::BeforeIcons;
};

// The mock taskbar's Start and app icons (24 DIP view box: the Windows logo, a folder, a globe, a
// terminal; holes wound the other way) and their colors outside high contrast.
static constexpr PCWSTR kPreviewStartPath =
    L"M3 3h8.5v8.5H3zM12.5 3H21v8.5h-8.5zM3 12.5h8.5V21H3zM12.5 12.5H21V21h-8.5z";
static constexpr PCWSTR kPreviewAppPaths[] = {
    L"M2 6.5A2.5 2.5 0 0 1 4.5 4H9.6l2.2 2.2h7.7A2.5 2.5 0 0 1 22 8.7v9.8a2.5 2.5 0 0 1-2.5 2.5h-15"
    L"A2.5 2.5 0 0 1 2 18.5z",
    L"M12 2.5a9.5 9.5 0 1 1 0 19a9.5 9.5 0 1 1 0-19zM12 4.4a7.6 7.6 0 1 0 0 15.2a7.6 7.6 0 1 0 0-15.2z"
    L"M12 4.4a4.2 7.6 0 1 1 0 15.2a4.2 7.6 0 1 1 0-15.2zM12 4.4a2.4 7.6 0 1 0 0 15.2a2.4 7.6 0 1 0 0-15.2z"
    L"M4.4 11.1h15.2v1.8H4.4z",
    L"M4.5 4.5h15A2.5 2.5 0 0 1 22 7v10a2.5 2.5 0 0 1-2.5 2.5h-15A2.5 2.5 0 0 1 2 17V7a2.5 2.5 0 0 1 2.5-2.5z"
    L"M6 8.3V10.4L8.2 12L6 13.6V15.7L10.9 12zM11.5 14.3V15.9H17.5V14.3z",
};
static constexpr LogoBrand kPreviewStartBrand{0xFF4CC2FF, 0xFF0067C0, 3, 3, 21, 21};
static constexpr LogoBrand kPreviewAppBrands[] = {
    {0xFFFFD86B, 0xFFE9A21C, 0, 4, 0, 21},
    {0xFF2FA8E8, 0xFF1764C0, 4, 4, 20, 20},
    {0xFF7A8794, 0xFF3F4852, 0, 4, 0, 20},
};

// The taskbar's free space between the apps and the tray, at its narrowest.
static constexpr float kPreviewFreeSpace = 16;

// Where a strip's parts sit in a preview `width` DIPs wide: the pieces in order, the tiles at
// their placement's slot with their panel's margins (the leading 4 DIPs and the gap after
// accounts), and the free space growing to fill the preview. A taskbar wider than the preview
// scrolls by `scroll` from its home, which shows the tiles' end of it: the right for the tray
// placements, the left beside Start.
struct SettingsPreviewRow {
    float tiles = 0;                 // x 0 of the strip's operations and columns
    std::vector<float> pieces;       // Each piece's left edge
    float left = 0;                  // The taskbar's left edge: 0, or less once it overflows
    float overflow = 0;              // How much wider than the preview the taskbar is
    float minimum = 0, maximum = 0;  // The scroll's range
};

static SettingsPreviewRow SettingsPreviewRowAt(const SettingsPreviewStrip& strip, float width, float scroll) {
    // The slot: before Start (0), the apps (1) or a tray piece (2 to 5), or after the last.
    const size_t slot = strip.placement == TaskbarPlacement::LeftSide     ? 0 :
                        strip.placement == TaskbarPlacement::RightOfStart ? 1 :
                                                                            2 + size_t(strip.placement);
    const float block = 4 + strip.width + strip.rightMargin;
    float natural = block + kPreviewFreeSpace;
    for (const SettingsPreviewPiece& piece : strip.pieces) natural += piece.width;
    SettingsPreviewRow row;
    const bool start = IsStartAreaPlacement(strip.placement);
    row.overflow = std::max(natural - width, 0.0f);
    row.minimum = start ? -row.overflow : 0;
    row.maximum = start ? 0 : row.overflow;
    row.left = std::clamp(scroll, row.minimum, row.maximum) - (start ? 0 : row.overflow);
    float x = row.left;
    for (size_t i = 0; i <= strip.pieces.size(); i++) {
        if (i == 2) x += kPreviewFreeSpace + std::max(width - natural, 0.0f);
        if (i == slot) {
            row.tiles = x + 4;
            x += block;
        }
        if (i < strip.pieces.size()) {
            row.pieces.push_back(x);
            x += strip.pieces[i].width;
        }
    }
    return row;
}

// The taskbar's layout of the sample accounts in `settings`. Gallery: the two Anthropic sample
// accounts on a 48 DIP strip, without a tray. `light` and `text` color the tile chrome and the
// taskbar's text (logos without brand colors, labels, keys, tray). Tests may pass accounts with
// their data to lay out instead of the samples.
static SettingsPreviewStrip SettingsPreviewLayout(UiGraphics& g, const Settings& settings,
                                                  double taskbarHeight, float scale, ULONGLONG now,
                                                  bool gallery, bool light, uint32_t text,
                                                  const std::vector<AccountConfig>* testAccounts = nullptr,
                                                  const std::vector<AccountData>* testData = nullptr) {
    using Op = SettingsPreviewOp;
    SettingsPreviewStrip strip;
    MeterDisplay display = MeterDisplayFor(settings, now, scale, taskbarHeight);
    std::vector<AccountConfig> accounts;
    std::vector<AccountData> data;
    BuildVisualTestSnapshot(display, &accounts, &data);
    if (testAccounts && testData && testAccounts->size() == testData->size()) {
        accounts = *testAccounts;
        data = *testData;
    }
    if (gallery) {
        accounts = {accounts[1], accounts[2]};
        data = {data[1], data[2]};
    }
    const BarLayout layout = BarLayout(std::clamp(int(settings.barLayout), int(BarLayout::Stacked),
                                                  int(kLastBarLayout)));
    const float H = gallery ? 48 : float(std::clamp(taskbarHeight > 0 ? taskbarHeight : 48.0, 24.0, 96.0));
    const float margin = float(std::clamp(settings.accountMargin, 0, 500));
    strip.height = H;
    strip.rightMargin = float(std::clamp(settings.rightMargin, 0, 500));
    strip.placement = TaskbarPlacement(std::clamp(int(settings.placement), int(TaskbarPlacement::BeforeIcons),
                                                  int(kLastTaskbarPlacement)));
    const TilePalette palette = TilePaletteFor(light);
    const PaceMarkColors pace = PaceMarkColorsFor(display.paceTickColor, light);

    // Text as XAML lays it out: natural line height, width rounded up to device pixels.
    struct Measured {
        float width, height, baseline, cap;
    };
    auto measure = [&](std::wstring_view s, const UiTextStyle& style) {
        IDWriteTextLayout* layout = UiLayout(g, s, style, 100000, false);
        DWRITE_LINE_METRICS line{};
        UINT32 lines = 0;
        layout->GetLineMetrics(&line, 1, &lines);
        return Measured{std::ceil(UiLayoutSize(layout).width * scale - 0.01f) / scale, line.height,
                        line.baseline, UiFontMetricsFor(g, style).capHeight};
    };
    auto add = [](std::vector<Op>& ops, Op op) { ops.push_back(std::move(op)); };
    auto rect = [&](std::vector<Op>& ops, D2D1_RECT_F r, float radius, uint32_t color, int hover = -1) {
        Op op;
        op.rect = r;
        op.radius = radius;
        op.color = color;
        op.hover = hover;
        add(ops, std::move(op));
    };
    // Text whose box top is at `top` (natural) or whose cap height starts there (XAML Tight
    // line bounds: cap height to baseline).
    auto label = [&](std::vector<Op>& ops, std::wstring s, const UiTextStyle& style, float left,
                     float top, float width, DWRITE_TEXT_ALIGNMENT align, uint32_t color,
                     float opacity, bool tight = false, int hover = -1, bool clip = false) {
        Measured m = measure(s, style);
        Op op;
        op.kind = Op::Kind::Text;
        op.rect = {left, tight ? top + m.cap - m.baseline : top, left + width,
                   (tight ? top + m.cap - m.baseline : top) + m.height};
        op.text = std::move(s);
        op.style = style;
        op.align = align;
        op.color = color;
        op.opacity = opacity;
        op.hover = hover;
        op.clip = clip;
        add(ops, std::move(op));
    };
    auto logo = [&](std::vector<Op>& ops, const std::wstring& provider, D2D1_RECT_F box, float opacity) {
        Op op;
        op.kind = Op::Kind::Logo;
        op.rect = box;
        op.color = text;
        op.opacity = opacity;
        op.path = ProviderLogoPath(provider);
        op.brand = ProviderLogoBrand(display.logoBrands, provider);
        add(ops, std::move(op));
    };
    // A number badge (tiles.h) in the model's box (`box`, moved by `x`, `y`): its bold text in the
    // badge color (or the taskbar's text color) centered on the box's center by its cap height,
    // over eight copies in the halo color kBadgeOutline DIPs around it.
    const BadgeColors badgeColors = BadgeColorsFor(display.badgeColor, light);
    auto badge = [&](std::vector<Op>& ops, const BadgeBox& box, float x, float y, const std::wstring& s,
                     float opacity) {
        const UiTextStyle style{UiFamily::Small, float(display.badgeSize), 0, DWRITE_FONT_WEIGHT_BOLD};
        const Measured m = measure(s, style);
        const float left = x + float(box.left + box.width / 2) - m.width / 2;
        const float top = y + float(box.top + box.height / 2) - m.cap / 2;
        for (int k = 0; k <= 8; k++) {
            const double angle = k * std::numbers::pi / 4;
            const float dx = k < 8 ? float(std::cos(angle) * kBadgeOutline) : 0;
            const float dy = k < 8 ? float(std::sin(angle) * kBadgeOutline) : 0;
            label(ops, s, style, left + dx, top + dy, m.width, DWRITE_TEXT_ALIGNMENT_CENTER,
                  k < 8 ? badgeColors.halo : badgeColors.text ? badgeColors.text : text, opacity, true);
        }
    };

    // The identity (tiles.h): a `side` DIP logo with its number badge, or the short text, shrunk
    // to fit 26 DIPs, then clipped; "!" at its top-right corner while warned. From `left`,
    // centered on `center`; returns its width.
    const UiTextStyle warnStyle{UiFamily::Taskbar, 10, 0, DWRITE_FONT_WEIGHT_BOLD};
    auto identityOps = [&](std::vector<Op>& ops, size_t i, const AccountMeters& meters, float side, float left,
                           float center) {
        const AccountConfig& account = accounts[i];
        const TileIdentityText identity = TileIdentityFor(accounts, i);
        const float opacity = float(meters.identityOpacity);
        float width = side, height = side;
        if (account.tileIdentity != TileIdentity::Text) {
            float top = center - side / 2;
            logo(ops, account.provider, {left, top, left + side, top + side}, opacity);
            if (!identity.badgeText.empty()) {
                badge(ops, LogoBadgeBox(display, side, int(identity.badgeText.size())), left, top,
                      identity.badgeText, opacity);
            }
        } else {
            const UiTextStyle measured{UiFamily::Display, 10, 0, DWRITE_FONT_WEIGHT_SEMI_BOLD};
            double perPt = std::max(measure(identity.text, measured).width, 0.1f) / 10.0;
            const UiTextStyle style{UiFamily::Display, float(std::clamp(26 / perPt, 8.0, 12.0)), 0,
                                    DWRITE_FONT_WEIGHT_SEMI_BOLD};
            Measured m = measure(identity.text, style);
            width = std::min(m.width, 26.0f);
            height = m.cap;
            label(ops, identity.text, style, left, center - height / 2, width, DWRITE_TEXT_ALIGNMENT_LEADING, text,
                  opacity, true, -1, true);
        }
        if (meters.warn) {
            Measured m = measure(L"!", warnStyle);
            label(ops, L"!", warnStyle, left + width + 4 - m.width, center - height / 2 - 3, m.width,
                  DWRITE_TEXT_ALIGNMENT_LEADING, text, opacity, true);
        }
        return width;
    };

    float x = 0;
    for (size_t i = 0; i < accounts.size(); i++) {
        const AccountConfig& account = accounts[i];
        const int column = int(i);
        const float T = float(display.tileHeight);
        // The box's side padding (ring tiles 4 DIPs, other tiles 7 and 8, bars 7) comes out of
        // the account margin, shrinking with it.
        const bool ring = IsRingTile(layout);
        const float padLeft = std::min(ring ? 4.0f : 7.0f, margin);
        const float padRight = std::min(ring ? 4.0f : IsTileLayout(layout) ? 8.0f : 7.0f, margin);
        float x0 = x + margin - padLeft, width = 0, top = 0, height = 0;
        std::vector<Op>& ops = strip.ops;
        size_t hoverAt = ops.size();

        if (!IsTileLayout(layout)) {
            // Bars (quota-grid.h, quota-update.h): the tiles' box, at least the tile height, its
            // padding inside each side, holding the content centered across it: the identity
            // beside, above or below a stack of bars at the identity position, centered on them,
            // the identity gap between them while a bar shows; or the bars alone. Each bar a track
            // or thin rail, its fill and pace tick, an optional quota key and the percentage text;
            // a time track's small bar adds to its cell under it (vertical bars: right of it).
            const bool vertical = layout == BarLayout::Vertical;
            const int len = display.barLength, th = display.barThickness;
            const float gap = float(std::clamp(settings.barGap, 0, 500));
            const float labelGap = float(std::clamp(settings.labelGap, 0, 500));
            const LabelPosition position = LabelPosition(std::clamp(
                int(settings.labelPosition), int(LabelPosition::Hidden), int(LabelPosition::Bottom)));
            const bool keys = settings.showBarLabels;
            const float textSize = float(std::clamp(settings.percentFontSize, 6, 24));
            const UiTextStyle labelStyle{UiFamily::Taskbar, float(std::clamp(settings.labelFontSize, 6, 24)), 0,
                                         DWRITE_FONT_WEIGHT_NORMAL};
            const UiTextStyle textStyle{UiFamily::Taskbar, textSize, 0, DWRITE_FONT_WEIGHT_NORMAL};
            const float keyColumn = std::ceil(textSize * 1.4f) + 3;
            const BarGeometry geometry = BarGeometryFor(display);
            const float timeSize = float(geometry.timeGap + geometry.timeThickness);
            const float itemWidth = (vertical ? float(th) + timeSize : float(len)) + (keys ? keyColumn : 0);
            const float itemHeight = float(vertical ? len : th);
            AccountMeters meters = AccountMeterState(display, layout, account, data[i], 0, 0);
            int count = std::popcount(unsigned(meters.barMask));

            float barsWidth = 0, barsHeight = 0;
            if (count) {
                barsWidth = vertical ? count * itemWidth + (count - 1) * gap : itemWidth;
                barsHeight = vertical ? float(len) : count * (float(th) + timeSize) + (count - 1) * gap + 2;
            }
            // The identity: the account label in the label font, its "!" while warned, or the
            // logo at its size; its margins (BarIdentityMarginFor) make its box, with the label
            // line's space beyond its text as DirectWrite lays it out.
            const bool shown = position != LabelPosition::Hidden;
            const bool side = position == LabelPosition::Left || position == LabelPosition::Right;
            const bool labelForm = account.tileIdentity == TileIdentity::Text;
            const float logoSide = float(kBarLogoSide * display.logoScale);
            const TileIdentityText identity = TileIdentityFor(accounts, i);
            const Measured labelText = measure(meters.labelText, labelStyle), line = measure(L"A", labelStyle);
            const BarIdentityMargin margin = BarIdentityMarginFor(
                position, labelForm, logoSide,
                !labelForm && !identity.badgeText.empty() ?
                    LogoBadgeBox(display, logoSide, int(identity.badgeText.size())) : BadgeBox{},
                meters.warn, count ? labelGap : 0, line.baseline - line.cap, line.height - line.baseline);
            float boxWidth = 0, boxHeight = 0;
            if (shown) {
                boxWidth = (labelForm ? labelText.width : logoSide) + float(margin.left + margin.right);
                boxHeight = (labelForm ? labelText.height : logoSide) + float(margin.top + margin.bottom);
            }
            float contentWidth = barsWidth, contentHeight = barsHeight;
            if (side) {
                contentWidth = boxWidth + barsWidth;
                contentHeight = std::max(boxHeight, barsHeight);
            } else if (shown) {
                contentWidth = std::max(boxWidth, barsWidth);
                contentHeight = boxHeight + barsHeight;
            } else if (!count) {
                // Without a quota or an identity shown, a small context-menu target stays.
                contentWidth = contentHeight = float(std::max(th, 8));
            }
            height = std::max(T, contentHeight);
            top = (H - height) / 2;
            width = padLeft + contentWidth + padRight;
            const float contentLeft = x0 + padLeft, contentTop = top + (height - contentHeight) / 2;

            if (shown) {
                float boxLeft = position == LabelPosition::Right ? contentLeft + barsWidth :
                                side ? contentLeft : contentLeft + (contentWidth - boxWidth) / 2;
                float boxTop = side ? contentTop + (contentHeight - boxHeight) / 2 :
                               position == LabelPosition::Top ? contentTop : contentTop + barsHeight;
                float left = boxLeft + float(margin.left), idTop = boxTop + float(margin.top);
                if (labelForm) {
                    label(ops, meters.labelText, labelStyle, left, idTop, labelText.width, DWRITE_TEXT_ALIGNMENT_LEADING,
                          text, meters.labelOpacity);
                } else {
                    identityOps(ops, i, meters, logoSide, left, idTop + logoSide / 2);
                }
            }
            float barsLeft = position == LabelPosition::Left ? contentLeft + boxWidth :
                             side ? contentLeft : contentLeft + (contentWidth - barsWidth) / 2;
            float barsTop = side ? contentTop + (contentHeight - barsHeight) / 2 :
                            position == LabelPosition::Top ? contentTop + boxHeight : contentTop;
            int k = 0;
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(meters.barMask & (1 << w))) continue;
                const QuotaMeter& q = meters.quotas[w];
                // Stacked items share the panel's left; vertical ones line up from it.
                float itemLeft = barsLeft + (vertical ? k * (itemWidth + gap) : 0);
                float itemTop = vertical ? barsTop : barsTop + 1 + k * (float(th) + timeSize + gap);
                float tx = itemLeft + (keys ? keyColumn : 0), ty = itemTop;
                float trackHeight = itemHeight;
                float radius = float(geometry.radius);
                // A full track, or a thin rail centred across the bar.
                float rail = float(geometry.rail), inset = rail > 0 ? (th - rail) / 2 : 0;
                rect(ops, vertical ? D2D1_RECT_F{tx + inset, ty, tx + th - inset, ty + len} :
                                     D2D1_RECT_F{tx, ty + inset, tx + len, ty + th - inset},
                     rail > 0 ? rail / 2 : radius, rail > 0 ? kBarRailColor : kBarTrackColor);
                if (q.fillPx > 0) {
                    // Shaded across the bar; start corners round with the track, vertical bars
                    // fill upward.
                    Op fill;
                    fill.rect = vertical ? D2D1_RECT_F{tx, ty + len - q.fillPx, tx + th, ty + len} :
                                           D2D1_RECT_F{tx, ty, tx + q.fillPx, ty + th};
                    float end = float(q.fillEndRadius);
                    fill.corners = vertical ? std::array<float, 4>{end, end, radius, radius} :
                                              std::array<float, 4>{radius, end, end, radius};
                    fill.color = q.color;
                    fill.shade = UsageHighlight(q.color);
                    fill.shadeLeft = vertical;
                    add(ops, std::move(fill));
                }
                if (timeSize > 0) {
                    // The time track: its track as long as the bar, then its fill from the bar's
                    // start in the pace color; corners half its thickness, the fill's far end the
                    // meter's.
                    float gapTo = float(geometry.timeGap), r = float(geometry.timeThickness) / 2;
                    D2D1_RECT_F timeTrack = vertical ? D2D1_RECT_F{tx + th + gapTo, ty, tx + th + timeSize, ty + len} :
                                                       D2D1_RECT_F{tx, ty + th + gapTo, tx + len, ty + th + timeSize};
                    rect(ops, timeTrack, r, kBarTrackColor);
                    if (q.paceVisible) {
                        Op fill;
                        float pacePx = float(q.pacePx), end = float(q.paceEndRadius);
                        fill.rect = timeTrack;
                        if (vertical) {
                            fill.rect.top = ty + len - pacePx;
                            fill.corners = std::array<float, 4>{end, end, r, r};
                        } else {
                            fill.rect.right = tx + pacePx;
                            fill.corners = std::array<float, 4>{r, end, end, r};
                        }
                        fill.color = pace.core;
                        add(ops, std::move(fill));
                    }
                } else if (display.showPaceTicks && q.paceVisible) {
                    // Past the bar's sides by its overhang; a notch only past the top (vertical:
                    // left) one.
                    bool notch = display.paceTickStyle == PaceTickStyle::Notch;
                    float along = geometry.paceLength, cross = geometry.paceCrossSize;
                    float crossStart = notch ? -float(geometry.overhang) : (th - cross) / 2;
                    D2D1_POINT_2F at = vertical ? D2D1_POINT_2F{tx + crossStart, ty + len - q.pacePx - along} :
                                                  D2D1_POINT_2F{tx + q.pacePx, ty + crossStart};
                    if (display.paceTickStyle == PaceTickStyle::Caret) {
                        // A halo polygon, then the core.
                        for (int layer = 0; layer < 2; layer++) {
                            Op op;
                            op.kind = Op::Kind::Polygon;
                            op.color = layer ? pace.core : pace.halo;
                            for (auto [a, c] : geometry.caret[layer]) {
                                op.points.push_back(vertical ? D2D1_POINT_2F{at.x + float(c), at.y + float(a)} :
                                                               D2D1_POINT_2F{at.x + float(a), at.y + float(c)});
                            }
                            add(ops, std::move(op));
                        }
                    } else {
                        float boxWidth = vertical ? cross : along, boxHeight = vertical ? along : cross;
                        rect(ops, {at.x, at.y, at.x + boxWidth, at.y + boxHeight}, geometry.paceRadius,
                             pace.halo);
                        // The core centred along the tick, at its offset across it.
                        float coreWidth = vertical ? geometry.coreCrossSize : geometry.coreLength;
                        float coreHeight = vertical ? geometry.coreLength : geometry.coreCrossSize;
                        float offset = float(geometry.coreCrossOffset);
                        float cx = vertical ? at.x + offset : at.x + (boxWidth - coreWidth) / 2;
                        float cy = vertical ? at.y + (boxHeight - coreHeight) / 2 : at.y + offset;
                        rect(ops, {cx, cy, cx + coreWidth, cy + coreHeight}, geometry.coreRadius, pace.core);
                    }
                }
                if (keys) {
                    label(ops, QuotaKey(account.provider, w), textStyle, itemLeft,
                          itemTop + (itemHeight - measure(L"5h", textStyle).height) / 2 - 0.5f, keyColumn - 3, DWRITE_TEXT_ALIGNMENT_TRAILING, text, 0.8f);
                }
                if (settings.percentTextVisibility != PercentTextVisibility::Never && !q.text.empty()) {
                    // As wide as the bar (vertical: at least four characters), aligned per the
                    // meter, nudged up a DIP; hover-only text shows while the column is hovered.
                    float boxWidth = vertical ? std::max(float(th), textSize * 4) : float(len);
                    float left = !vertical || q.textAlignment == PercentTextAlignment::Left ? tx :
                                 q.textAlignment == PercentTextAlignment::Right ? tx + th - boxWidth :
                                                                                  tx + (th - boxWidth) / 2;
                    DWRITE_TEXT_ALIGNMENT align =
                        q.textAlignment == PercentTextAlignment::Left   ? DWRITE_TEXT_ALIGNMENT_LEADING :
                        q.textAlignment == PercentTextAlignment::Center ? DWRITE_TEXT_ALIGNMENT_CENTER :
                                                                          DWRITE_TEXT_ALIGNMENT_TRAILING;
                    label(ops, q.text, textStyle, left + float(q.textOffset),
                          ty + (trackHeight - measure(q.text, textStyle).height) / 2 - 1, boxWidth, align,
                          q.darkText ? 0xFF000000 : 0xFFFFFFFF, 0.9f, false,
                          settings.percentTextVisibility == PercentTextVisibility::Hover ? column : -1);
                }
                k++;
            }
        } else {
            // Tiles (tiles.h): the identity (logo, badge, or text) and the meters in the 4 DIP
            // rounded box.
            TileIdentityText identity = TileIdentityFor(accounts, i);
            const bool hasLogo = account.tileIdentity != TileIdentity::Text;
            const bool hasBadge = hasLogo && !identity.badgeText.empty();
            const UiTextStyle idStyle{UiFamily::Display, 10, 0, DWRITE_FONT_WEIGHT_SEMI_BOLD};
            double perPt = hasLogo ? 0 : std::max(measure(identity.text, idStyle).width, 0.1f) / 10.0;
            // Sparklines chart the visual test samples' recorded usage.
            AccountMeters meters = AccountMeterState(
                display, layout, account, data[i], hasBadge ? int(identity.badgeText.size()) : 0, perPt,
                [&](int, const WindowUsage& reading) { return VisualTestTrace(account, reading, now); });
            const float opacity = float(meters.identityOpacity);
            top = (H - T) / 2;
            height = T;
            int count = std::popcount(unsigned(meters.barMask));

            // A face tile's meter shapes in its face from (faceLeft, faceTop): tracks, then flat
            // fills, then pace ticks (halo, then core) on top.
            const float faceWidth = float(meters.faceWidth), faceHeight = float(meters.faceHeight);
            auto faceShapes = [&](float faceLeft, float faceTop) {
                auto shape = [&](const TileOutline& outline, uint32_t color) {
                    Op op;
                    op.kind = Op::Kind::Shape;
                    op.shape = outline;
                    op.origin = {faceLeft, faceTop};
                    op.color = color;
                    add(ops, std::move(op));
                };
                for (int pass = 0; pass < 3; pass++) {
                    for (int w = 0; w < kQuotaBarCount; w++) {
                        const QuotaMeter& q = meters.quotas[w];
                        if (!(meters.barMask & (1 << w))) continue;
                        if (pass == 0) {
                            shape(q.track, palette.track);
                        } else if (pass == 1 && q.fillUnits) {
                            shape(q.fill, q.color);
                        } else if (pass == 2 && display.showPaceTicks && q.paceVisible) {
                            shape(q.paceHalo, pace.halo);
                            shape(q.paceCore, pace.core);
                        }
                    }
                }
            };

            if (ring) {
                const TileRingGeometry& geometry = meters.ring;
                const float S = faceWidth;
                const float faceLeft = x0 + padLeft, faceTop = top + (T - faceHeight) / 2;
                width = padLeft + S + padRight;
                faceShapes(faceLeft, faceTop);
                // Identity, sized to the hole, with its badge in the model's box.
                if (hasLogo) {
                    float side = float(geometry.logo);
                    float left = faceLeft + (S - side) / 2, logoTop = faceTop + (S - side) / 2;
                    logo(ops, account.provider, {left, logoTop, left + side, logoTop + side}, opacity);
                    if (hasBadge) badge(ops, meters.ringBadge, faceLeft, faceTop, identity.badgeText, opacity);
                } else {
                    UiTextStyle style{UiFamily::Display, float(meters.ringTextFont), 0, DWRITE_FONT_WEIGHT_SEMI_BOLD};
                    Measured m = measure(identity.text, style);
                    float w = std::min(m.width, float(meters.ringTextWidth));
                    label(ops, identity.text, style, faceLeft + (S - w) / 2, faceTop + (S - m.cap) / 2, w,
                          DWRITE_TEXT_ALIGNMENT_LEADING, text, opacity, true, -1, true);
                }
                if (meters.warn) {
                    Measured m = measure(L"!", warnStyle);
                    label(ops, L"!", warnStyle, faceLeft + S + 3 - m.width, faceTop + float(meters.ringWarnTop),
                          m.width, DWRITE_TEXT_ALIGNMENT_LEADING, text, opacity, true);
                }
            } else if (IsFaceTile(layout)) {
                // Gauges and pies: the identity, then 6 DIP after it the face; a gauge's number
                // and key centred across it, their cap heights on the model's centres.
                float idWidth = identityOps(ops, i, meters, float(kHeadlineLogoSide * display.logoScale), x0 + padLeft,
                                            top + T / 2);
                float faceLeft = x0 + padLeft + idWidth + 6, faceTop = top + (T - faceHeight) / 2;
                faceShapes(faceLeft, faceTop);
                if (layout == BarLayout::Gauge) {
                    auto centred = [&](const std::wstring& s, const UiTextStyle& style, double center, uint32_t color,
                                       float alpha) {
                        Measured m = measure(s, style);
                        label(ops, s, style, faceLeft + (faceWidth - m.width) / 2, faceTop + float(center) - m.cap / 2,
                              m.width, DWRITE_TEXT_ALIGNMENT_LEADING, color, alpha, true);
                    };
                    centred(meters.headline, {UiFamily::Display, float(meters.faceFont), 0, DWRITE_FONT_WEIGHT_SEMI_BOLD},
                            meters.faceNumberCenter, meters.headlineColor, 1);
                    if (!meters.headlineKey.empty()) {
                        centred(meters.headlineKey, {UiFamily::Taskbar, float(meters.faceKeyFont), 0, DWRITE_FONT_WEIGHT_NORMAL},
                                meters.faceKeyCenter, text, 0.6f);
                    }
                }
                width = padLeft + idWidth + 6 + faceWidth + padRight;
            } else if (layout == BarLayout::Sparkline) {
                // Sparklines: the identity, then 6 DIP after it each charted quota's chart at its
                // place: its plot's background, its shapes in tiles.h's drawing order, and its key
                // kSparkTextInset in from its bottom-right corner (or top-right); on the weekly
                // chart, the extra usage's key and number kSparkExtraKeyGap apart, as far in from
                // its top-left corner (or bottom-left), clipped to the insets.
                float idWidth = identityOps(ops, i, meters, float(kHeadlineLogoSide * display.logoScale), x0 + padLeft,
                                            top + T / 2);
                float faceLeft = x0 + padLeft + idWidth + 6, faceTop = top + (T - faceHeight) / 2;
                const float chartWidth = float(display.sparklineWidth);
                const UiTextStyle keyStyle{UiFamily::Taskbar, float(meters.sparkKeyFont), 0, DWRITE_FONT_WEIGHT_SEMI_BOLD};
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (!(meters.barMask & (1 << w))) continue;
                    const QuotaMeter& q = meters.quotas[w];
                    const float left = faceLeft + float(q.sparkLeft);
                    rect(ops, {left, faceTop, left + chartWidth, faceTop + faceHeight}, float(kSparkPlotRadius),
                         palette.plot);
                    for (int k = 0; k < kSparkShapeCount; k++) {
                        if (q.spark[k].empty()) continue;
                        const SparkShapeStyle& style = kSparkShapeStyles[k];
                        Op op;
                        op.kind = style.stroke > 0 ? Op::Kind::Stroke : Op::Kind::Shape;
                        op.shape = q.spark[k];
                        op.origin = {left, faceTop};
                        op.color = SparkShapeColor(k, meters, w, display, light);
                        op.strokeWidth = float(style.stroke);
                        op.winding = true;
                        if (style.dashes[0] > 0) op.dashes = {float(style.dashes[0]), float(style.dashes[1])};
                        add(ops, std::move(op));
                    }
                    const float inset = float(kSparkTextInset);
                    Measured key = measure(q.key, keyStyle);
                    label(ops, q.key, keyStyle, left + chartWidth - inset - key.width,
                          q.sparkKeyTop ? faceTop + inset : faceTop + faceHeight - inset - key.cap, key.width,
                          DWRITE_TEXT_ALIGNMENT_LEADING, text, 0.75f, true);
                    if (w != kWeeklyBar || meters.sparkExtraText.empty()) continue;
                    const float extraTop = meters.sparkExtraBottom ? faceTop + faceHeight - inset - key.cap : faceTop + inset;
                    const float extraEnd = left + chartWidth - inset;
                    float extraLeft = left + inset;
                    if (!meters.sparkExtraKey.empty()) {
                        const UiTextStyle extraKeyStyle{UiFamily::Taskbar, float(meters.sparkKeyFont), 0,
                                                        DWRITE_FONT_WEIGHT_NORMAL};
                        Measured extraKey = measure(meters.sparkExtraKey, extraKeyStyle);
                        label(ops, meters.sparkExtraKey, extraKeyStyle, extraLeft, extraTop,
                              std::clamp(extraEnd - extraLeft, 0.0f, extraKey.width), DWRITE_TEXT_ALIGNMENT_LEADING,
                              text, 0.6f, true, -1, true);
                        extraLeft += extraKey.width + float(kSparkExtraKeyGap);
                    }
                    Measured extra = measure(meters.sparkExtraText, keyStyle);
                    if (extraLeft < extraEnd) {
                        label(ops, meters.sparkExtraText, keyStyle, extraLeft, extraTop,
                              std::min(extraEnd - extraLeft, extra.width), DWRITE_TEXT_ALIGNMENT_LEADING,
                              meters.sparkExtraColor ? meters.sparkExtraColor : text, 1, true, -1, true);
                    }
                }
                width = padLeft + idWidth + 6 + faceWidth + padRight;
            } else if (layout == BarLayout::QuotaMatrix) {
                // Quota matrix: the identity, then 6 DIP after it the cells, centred on the tile:
                // each the key (reserving the widest) 3 DIPs before the number (reserving "100%",
                // wider for a wider number in its column), on their baseline; rows and columns
                // their gaps apart.
                float idWidth = identityOps(ops, i, meters, float(kHeadlineLogoSide * display.logoScale), x0 + padLeft,
                                            top + T / 2);
                float cellsLeft = x0 + padLeft + idWidth + 6;
                UiTextStyle numberStyle{UiFamily::Display, float(meters.matrixFont), 0, DWRITE_FONT_WEIGHT_SEMI_BOLD};
                UiTextStyle keyStyle{UiFamily::Taskbar, float(meters.matrixKeyFont), 0, DWRITE_FONT_WEIGHT_NORMAL};
                float keyReserve = 0;
                for (PCWSTR k : {kQuotaKeys[0], kQuotaKeys[1], kQuotaKeys[2], kQuotaKeys[3], L"Cr", L"Mo"}) {
                    keyReserve = std::max(keyReserve, measure(k, keyStyle).width);
                }
                keyReserve = std::ceil(keyReserve);
                float numberReserve = std::ceil(measure(L"100%", numberStyle).width);
                std::array<float, 2> numberWidths{numberReserve, numberReserve};
                float cap = measure(L"0", numberStyle).cap;
                int rows = 1, columns = 1;
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (!(meters.barMask & (1 << w))) continue;
                    const QuotaMeter& q = meters.quotas[w];
                    rows = std::max(rows, q.matrixRow + 1);
                    columns = std::max(columns, q.matrixColumn + 1);
                    float& column = numberWidths[q.matrixColumn];
                    column = std::max(column, measure(q.text, numberStyle).width);
                }
                float cellsTop = top + (T - (rows * cap + (rows - 1) * float(kMatrixRowGap))) / 2;
                const float second = cellsLeft + keyReserve + float(kMatrixKeyGap) + numberWidths[0] + float(kMatrixColumnGap);
                for (int w = 0; w < kQuotaBarCount; w++) {
                    if (!(meters.barMask & (1 << w))) continue;
                    const QuotaMeter& q = meters.quotas[w];
                    float left = q.matrixColumn ? second : cellsLeft;
                    float baseline = cellsTop + q.matrixRow * (cap + float(kMatrixRowGap)) + cap;
                    Measured key = measure(q.key, keyStyle), number = measure(q.text, numberStyle);
                    label(ops, q.key, keyStyle, left, baseline - key.cap, key.width, DWRITE_TEXT_ALIGNMENT_LEADING, text,
                          0.6f, true);
                    label(ops, q.text, numberStyle, left + keyReserve + float(kMatrixKeyGap), baseline - number.cap,
                          number.width, DWRITE_TEXT_ALIGNMENT_LEADING, q.color, 1, true);
                }
                float cells = !count       ? 0 :
                              columns == 1 ? keyReserve + float(kMatrixKeyGap) + numberWidths[0] :
                                             second - cellsLeft + keyReserve + float(kMatrixKeyGap) + numberWidths[1];
                width = padLeft + idWidth + 6 + cells + padRight;
            } else {
                // Headline: the identity, then 6 DIP after it the most-used quota's number and,
                // 3 DIP after it, its window key (bottom-aligned on the baseline), in a row that
                // reserves "100%" and the widest key; under them per-quota segments the bar gap
                // apart: corners a quarter of their thickness, fills cut flat toward the end and
                // shaded like the bars'. With the countdown, 3 DIP under the row the time until
                // that quota resets at the key's size instead, reserving its widest form.
                float idWidth = identityOps(ops, i, meters, float(kHeadlineLogoSide * display.logoScale), x0 + padLeft,
                                            top + T / 2);
                float metersLeft = x0 + padLeft + idWidth + 6;
                const float fontSize = float(display.headlineFontSize), gap = float(display.barGap);
                UiTextStyle numberStyle{UiFamily::Display, fontSize, 0, DWRITE_FONT_WEIGHT_SEMI_BOLD};
                UiTextStyle keyStyle{UiFamily::Taskbar, fontSize * float(kHeadlineKeyScale), 0, DWRITE_FONT_WEIGHT_NORMAL};
                Measured number = measure(meters.headline, numberStyle);
                Measured key = measure(meters.headlineKey, keyStyle);
                float keyReserve = 0;
                for (PCWSTR k : {kQuotaKeys[0], kQuotaKeys[1], kQuotaKeys[2], kQuotaKeys[3], L"Cr", L"Mo"}) {
                    keyReserve = std::max(keyReserve, measure(k, keyStyle).width);
                }
                float rowWidth = std::max(std::ceil(measure(L"100%", numberStyle).width) + 3 + std::ceil(keyReserve),
                                          number.width + 3 + key.width);
                float rowHeight = std::max(number.cap, meters.headlineKey.empty() ? 0.0f : key.cap);
                const float length = float(display.headlineBarLength), thick = float(display.headlineBarThickness);
                const bool countdown = display.headlineCountdown;
                Measured remaining = measure(meters.headlineCountdown, keyStyle);
                float segments = countdown ? std::max(std::ceil(measure(kHeadlineCountdownReserve, keyStyle).width),
                                                      remaining.width) :
                                 count     ? count * length + (count - 1) * gap : 0;
                float panelHeight = rowHeight + (countdown ? 3 + remaining.cap : count ? 3 + thick : 0);
                float panelTop = top + (T - panelHeight) / 2;
                label(ops, meters.headline, numberStyle, metersLeft, panelTop + rowHeight - number.cap,
                      number.width, DWRITE_TEXT_ALIGNMENT_LEADING, meters.headlineColor, 1, true);
                if (!meters.headlineKey.empty()) {
                    label(ops, meters.headlineKey, keyStyle, metersLeft + number.width + 3,
                          panelTop + rowHeight - key.cap, key.width, DWRITE_TEXT_ALIGNMENT_LEADING, text,
                          0.6f, true);
                }
                if (countdown) {
                    label(ops, meters.headlineCountdown, keyStyle, metersLeft, panelTop + rowHeight + 3,
                          remaining.width, DWRITE_TEXT_ALIGNMENT_LEADING, text, 0.7f, true);
                }
                float left = metersLeft, y = panelTop + rowHeight + 3;
                for (int w = 0; w < kQuotaBarCount && !countdown; w++) {
                    if (!(meters.barMask & (1 << w))) continue;
                    const QuotaMeter& q = meters.quotas[w];
                    rect(ops, {left, y, left + length, y + thick}, thick / 4, palette.track);
                    if (q.fillUnits) {
                        Op fill;
                        fill.rect = {left, y, left + float(q.fillLength), y + thick};
                        float start = thick / 4, end = float(q.fillEndRadius);
                        fill.corners = std::array<float, 4>{start, end, end, start};
                        fill.color = q.color;
                        fill.shade = UsageHighlight(q.color);
                        add(ops, std::move(fill));
                    }
                    left += length + gap;
                }
                width = padLeft + idWidth + 6 + std::max(rowWidth, segments) + padRight;
            }
        }
        // Hovered, the box takes the tile hover fill.
        Op hover;
        hover.rect = {x0, top, x0 + width, top + height};
        hover.radius = 4;
        hover.color = palette.hover;
        hover.hover = column;
        ops.insert(ops.begin() + std::ptrdiff_t(hoverAt), std::move(hover));
        strip.columns.push_back({x0, top, x0 + width, top + height});
        x = x0 + width + margin - padRight;
    }
    strip.width = x;

    // The mock taskbar: Start and three apps on 40 DIP buttons (the first two running: a short
    // pill under them), then the tray (mockup .tray): the notification icons' chevron, the
    // network, volume and battery glyphs, the two-line clock with the notification bell, and Show
    // desktop's sliver. Icons are 20 DIPs, less on the lowest taskbars.
    if (!gallery) {
        auto glyph = [&](std::vector<Op>& ops, PCWSTR s, float size, float left, float width) {
            label(ops, s, UiIconStyle(size), left, H / 2 - size / 2, width, DWRITE_TEXT_ALIGNMENT_CENTER, text, 1);
        };
        auto icon = [&](std::vector<Op>& ops, PCWSTR path, const LogoBrand* brand, float center, float side) {
            Op op;
            op.kind = Op::Kind::Logo;
            op.rect = {center - side / 2, H / 2 - side / 2, center + side / 2, H / 2 + side / 2};
            op.color = text;
            op.path = path;
            op.brand = brand;
            add(ops, std::move(op));
        };
        auto mark = [&](std::vector<Op>& ops, D2D1_RECT_F r, float radius, float opacity) {
            Op op;
            op.rect = r;
            op.radius = radius;
            op.color = text;
            op.opacity = opacity;
            add(ops, std::move(op));
        };
        const float side = std::min(20.0f, H - 14);
        SettingsPreviewPiece start{{}, 40}, apps{{}, 3 * 40}, icons{{}, 24 + 4}, network{{}, 3 * 26 + 6}, clock,
            desktop{{}, 10};
        icon(start.ops, kPreviewStartPath, &kPreviewStartBrand, 20, side - 2);
        for (int k = 0; k < 3; k++) {
            const float center = 40.0f * float(k) + 20, pill = (H / 2 + side / 2 + H) / 2;
            icon(apps.ops, kPreviewAppPaths[k], &kPreviewAppBrands[k], center, side);
            if (k < 2) mark(apps.ops, {center - 3, pill - 1.5f, center + 3, pill + 1.5f}, 1.5f, 0.5f);
        }
        glyph(icons.ops, L"\uE70E", 11, 0, 24);
        const PCWSTR status[] = {L"\uE701", L"\uE767", L"\uE83F"};
        for (int k = 0; k < 3; k++) glyph(network.ops, status[k], 14, 26.0f * float(k), 26);
        UiTextStyle clockStyle{UiFamily::Taskbar, 12, 16, DWRITE_FONT_WEIGHT_NORMAL};
        float clockWidth = std::max(measure(L"14:32", clockStyle).width, measure(L"2026-10-31", clockStyle).width);
        label(clock.ops, L"14:32", clockStyle, 6, H / 2 - 16, clockWidth, DWRITE_TEXT_ALIGNMENT_TRAILING, text, 1);
        label(clock.ops, L"2026-10-31", clockStyle, 6, H / 2, clockWidth, DWRITE_TEXT_ALIGNMENT_TRAILING, text, 1);
        glyph(clock.ops, L"\uEA8F", 14, 6 + clockWidth + 6, 24);
        clock.width = 6 + clockWidth + 6 + 24 + 4;
        mark(desktop.ops, {4, H / 4, 5, H * 3 / 4}, 0, 0.4f);
        strip.pieces = {std::move(start), std::move(apps), std::move(icons), std::move(network), std::move(clock),
                        std::move(desktop)};
    }
    return strip;
}

// Paints a preview item: the strip (Preview; hover follows `pointer`, scrolled by
// view.previewScroll) or a tile style's thumbnail (Gallery; the style in action.value), its
// sample tiles scaled to fit. Colors follow the taskbar: the Windows mode with transparency off,
// or high contrast's system colors.
static void SettingsPaintPreview(ID2D1RenderTarget* target, UiTargetCache& cache, UiGraphics& g,
                                 const UiThemeInput& theme, const SettingsView& view,
                                 const UiItem& item, D2D1_POINT_2F pointer) {
    using Op = SettingsPreviewOp;
    const bool hc = theme.kind == UiThemeKind::HighContrast;
    const uint32_t background = hc ? 0xFF000000 | theme.system.window : theme.systemLight ? 0xFFEEEEEE : 0xFF1C1C1C;
    const uint32_t text = hc ? 0xFF000000 | theme.system.windowText : theme.systemLight ? 0xE4000000 : 0xFFFFFFFF;
    const uint32_t ink = theme.system.windowText;
    const bool light = hc ? ((ink >> 16 & 0xFF) * 299 + (ink >> 8 & 0xFF) * 587 + (ink & 0xFF) * 114) < 128000 :
                            theme.systemLight;
    auto color = [](uint32_t argb, float opacity = 1) {
        return D2D1_COLOR_F{float(argb >> 16 & 0xFF) / 255, float(argb >> 8 & 0xFF) / 255, float(argb & 0xFF) / 255,
                            float(argb >> 24) / 255 * opacity};
    };
    const float scale = view.scale;
    const bool gallery = item.kind == UiItemKind::Gallery;
    Settings settings = view.settings;
    if (gallery) settings.barLayout = BarLayout(item.action.value);
    SettingsPreviewStrip strip = SettingsPreviewLayout(g, settings, view.status.taskbarHeight, scale, view.now,
                                                       gallery, light, text);

    D2D1_MATRIX_3X2_F base{};
    target->GetTransform(&base);
    D2D1_TEXT_ANTIALIAS_MODE antialias = target->GetTextAntialiasMode();
    target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);  // As the taskbar's XAML text

    // Operations moved to `origin` and zoomed; hover operations only for `hovered`. Fills snap
    // to device pixels.
    auto paint = [&](const std::vector<Op>& ops, D2D1_POINT_2F origin, float zoom, int hovered) {
        D2D1::Matrix3x2F local = D2D1::Matrix3x2F::Scale(zoom, zoom) *
                                 D2D1::Matrix3x2F::Translation(origin.x, origin.y) *
                                 *D2D1::Matrix3x2F::ReinterpretBaseType(&base);
        target->SetTransform(&local);
        for (const Op& op : ops) {
            if (op.hover >= 0 && op.hover != hovered) continue;
            const D2D1_RECT_F& r = op.rect;
            switch (op.kind) {
                case Op::Kind::Rect: {
                    if (op.corners) {
                        // Each corner its own radius, like a XAML Border's CornerRadius.
                        D2D1_RECT_F s = UiSnapRect(r, scale * zoom);
                        float width = s.right - s.left, height = s.bottom - s.top;
                        if (width <= 0 || height <= 0) break;
                        std::array<float, 4> c = *op.corners;
                        float fit = std::min({1.0f, width / std::max(c[0] + c[1], 1e-3f),
                                              width / std::max(c[3] + c[2], 1e-3f),
                                              height / std::max(c[0] + c[3], 1e-3f),
                                              height / std::max(c[1] + c[2], 1e-3f)});
                        for (float& corner : c) corner *= fit;
                        winrt::com_ptr<ID2D1PathGeometry> path;
                        winrt::check_hresult(g.d2d->CreatePathGeometry(path.put()));
                        winrt::com_ptr<ID2D1GeometrySink> sink;
                        winrt::check_hresult(path->Open(sink.put()));
                        // A side up to the corner, then its arc.
                        auto side = [&](D2D1_POINT_2F to, D2D1_POINT_2F arcTo, float radius) {
                            sink->AddLine(to);
                            if (radius <= 0) return;
                            D2D1_ARC_SEGMENT arc{arcTo, {radius, radius}, 0, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                                 D2D1_ARC_SIZE_SMALL};
                            sink->AddArc(&arc);
                        };
                        sink->BeginFigure({s.left + c[0], s.top}, D2D1_FIGURE_BEGIN_FILLED);
                        side({s.right - c[1], s.top}, {s.right, s.top + c[1]}, c[1]);
                        side({s.right, s.bottom - c[2]}, {s.right - c[2], s.bottom}, c[2]);
                        side({s.left + c[3], s.bottom}, {s.left, s.bottom - c[3]}, c[3]);
                        side({s.left, s.top + c[0]}, {s.left + c[0], s.top}, c[0]);
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                        winrt::check_hresult(sink->Close());
                        ID2D1Brush* brush = nullptr;
                        if (op.shade) {
                            ID2D1LinearGradientBrush* gradient = UiGradientBrush(
                                target, cache, color(op.shade, op.opacity), color(op.color, op.opacity));
                            gradient->SetStartPoint({s.left, s.top});
                            gradient->SetEndPoint(op.shadeLeft ? D2D1_POINT_2F{s.left + 0.6f * width, s.top} :
                                                                 D2D1_POINT_2F{s.left, s.top + 0.6f * height});
                            brush = gradient;
                        } else {
                            brush = UiBrush(target, cache, color(op.color, op.opacity));
                        }
                        target->FillGeometry(path.get(), brush, nullptr);
                        break;
                    }
                    float radius = std::min({op.radius, (r.right - r.left) / 2, (r.bottom - r.top) / 2});
                    UiFillRect(target, cache, r, radius, color(op.color, op.opacity), scale * zoom);
                    break;
                }
                case Op::Kind::Shape:
                case Op::Kind::Stroke:
                case Op::Kind::Polygon: {
                    winrt::com_ptr<ID2D1PathGeometry> path;
                    winrt::check_hresult(g.d2d->CreatePathGeometry(path.put()));
                    winrt::com_ptr<ID2D1GeometrySink> sink;
                    winrt::check_hresult(path->Open(sink.put()));
                    const bool stroke = op.kind == Op::Kind::Stroke;
                    if (op.winding) sink->SetFillMode(D2D1_FILL_MODE_WINDING);
                    if (op.kind != Op::Kind::Polygon) {
                        // As XAML's PathGeometry draws it: filled even-odd (the sink's default),
                        // or open to stroke; not snapped.
                        auto point = [&](double x, double y) {
                            return D2D1::Point2F(op.origin.x + float(x), op.origin.y + float(y));
                        };
                        for (const TileOutlineFigure& figure : op.shape) {
                            sink->BeginFigure(point(figure.x, figure.y),
                                              stroke ? D2D1_FIGURE_BEGIN_HOLLOW : D2D1_FIGURE_BEGIN_FILLED);
                            for (const TileOutlineSegment& segment : figure.segments) {
                                if (segment.radius <= 0) {
                                    sink->AddLine(point(segment.x, segment.y));
                                    continue;
                                }
                                D2D1_ARC_SEGMENT arc{point(segment.x, segment.y),
                                                     {float(segment.radius), float(segment.radius)}, 0,
                                                     segment.clockwise ? D2D1_SWEEP_DIRECTION_CLOCKWISE :
                                                                         D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                                     D2D1_ARC_SIZE_SMALL};
                                sink->AddArc(&arc);
                            }
                            sink->EndFigure(stroke ? D2D1_FIGURE_END_OPEN : D2D1_FIGURE_END_CLOSED);
                        }
                    } else if (!op.points.empty()) {
                        // On device pixels, like XAML's layout rounding of the tick and its
                        // pixel-sized parts.
                        auto snap = [&](D2D1_POINT_2F p) {
                            return D2D1::Point2F(UiSnap(p.x, scale * zoom), UiSnap(p.y, scale * zoom));
                        };
                        sink->BeginFigure(snap(op.points[0]), D2D1_FIGURE_BEGIN_FILLED);
                        for (size_t p = 1; p < op.points.size(); p++) sink->AddLine(snap(op.points[p]));
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    }
                    winrt::check_hresult(sink->Close());
                    ID2D1Brush* brush = UiBrush(target, cache, color(op.color, op.opacity));
                    if (!stroke) {
                        target->FillGeometry(path.get(), brush, nullptr);
                        break;
                    }
                    // As the sparkline's XAML strokes: solid lines round, dashed ones flat.
                    const bool dashed = !op.dashes.empty();
                    const D2D1_CAP_STYLE cap = dashed ? D2D1_CAP_STYLE_FLAT : D2D1_CAP_STYLE_ROUND;
                    D2D1_STROKE_STYLE_PROPERTIES properties = D2D1::StrokeStyleProperties(
                        cap, cap, D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_ROUND, 10,
                        dashed ? D2D1_DASH_STYLE_CUSTOM : D2D1_DASH_STYLE_SOLID, 0);
                    winrt::com_ptr<ID2D1StrokeStyle> style;
                    winrt::check_hresult(g.d2d->CreateStrokeStyle(&properties, dashed ? op.dashes.data() : nullptr,
                                                                   UINT32(op.dashes.size()), style.put()));
                    target->DrawGeometry(path.get(), brush, op.strokeWidth, style.get());
                    break;
                }
                case Op::Kind::Text: {
                    IDWriteTextLayout* layout = UiLayout(g, op.text, op.style, std::max(r.right - r.left, 0.0f),
                                                         false, op.align);
                    if (op.clip) target->PushAxisAlignedClip(&r, D2D1_ANTIALIAS_MODE_ALIASED);
                    UiDrawLayout(target, cache, layout, {r.left, r.top}, color(op.color, op.opacity));
                    if (op.clip) target->PopAxisAlignedClip();
                    break;
                }
                case Op::Kind::Logo: {
                    float s = (r.right - r.left) / 24;
                    D2D1::Matrix3x2F logo = D2D1::Matrix3x2F::Scale(s, s) *
                                            D2D1::Matrix3x2F::Translation(r.left, r.top) * local;
                    target->SetTransform(&logo);
                    ID2D1Brush* brush = nullptr;
                    if (!op.brand || hc) {
                        brush = UiBrush(target, cache, color(op.color, op.opacity));
                    } else if (op.brand->from == op.brand->to) {
                        brush = UiBrush(target, cache, color(op.brand->from, op.opacity));
                    } else {
                        ID2D1LinearGradientBrush* gradient = UiGradientBrush(
                            target, cache, color(op.brand->from, op.opacity), color(op.brand->to, op.opacity));
                        gradient->SetStartPoint({op.brand->x0, op.brand->y0});
                        gradient->SetEndPoint({op.brand->x1, op.brand->y1});
                        brush = gradient;
                    }
                    target->FillGeometry(UiPathGeometry(g, op.path), brush, nullptr);
                    target->SetTransform(&local);
                    break;
                }
            }
        }
    };

    if (gallery) {
        // Wallpaper, a 48 DIP taskbar across its middle, the tiles scaled to fit and centered.
        D2D1_RECT_F thumb = UiGalleryThumbRect(item);
        D2D1_COLOR_F wallTop = hc ? color(background) : color(theme.systemLight ? 0xFFDCE5FA : 0xFF0E1734);
        D2D1_COLOR_F wallBottom = hc ? color(background) : color(theme.systemLight ? 0xFF8FB2FF : 0xFF2D63D8);
        UiFillGradient(target, cache, thumb, 4, wallTop, wallBottom, true);
        float barTop = (thumb.top + thumb.bottom - strip.height) / 2;
        D2D1_RECT_F bar{thumb.left, barTop, thumb.right, barTop + strip.height};
        UiFillRect(target, cache, bar, 0, color(background), scale);
        float top = 0, bottom = strip.height;
        for (const D2D1_RECT_F& column : strip.columns) {
            top = std::min(top, column.top);
            bottom = std::max(bottom, column.bottom);
        }
        float zoom = std::min({1.0f, (bar.right - bar.left - 12) / std::max(strip.width, 1.0f),
                               strip.height / std::max(bottom - top, 1.0f)});
        D2D1_POINT_2F origin{(bar.left + bar.right - strip.width * zoom) / 2,
                             (bar.top + bar.bottom) / 2 - (top + bottom) / 2 * zoom};
        target->PushAxisAlignedClip(&bar, D2D1_ANTIALIAS_MODE_ALIASED);
        paint(strip.ops, origin, zoom, -1);
        target->SetTransform(&base);
        target->PopAxisAlignedClip();
        // High contrast: the taskbar's edge in the text color.
        if (hc) UiStrokeRect(target, cache, bar, 0, color(text), 1, scale);
    } else {
        // The taskbar's pieces and the tiles where the row puts them at the preview scroll;
        // content beyond the strip fades at that edge, with a thin position bar along the bottom.
        const D2D1_RECT_F& r = item.rect;
        const float width = r.right - r.left;
        const SettingsPreviewRow row = SettingsPreviewRowAt(strip, width, view.previewScroll);
        const float left = UiSnap(r.left + row.tiles, scale);
        int hovered = -1;
        if (UiContains(r, pointer)) {
            for (size_t i = 0; i < strip.columns.size(); i++) {
                if (UiContains(strip.columns[i], {pointer.x - left, pointer.y - r.top})) hovered = int(i);
            }
        }
        UiFillRect(target, cache, r, 4, color(background), scale);
        target->PushAxisAlignedClip(&r, D2D1_ANTIALIAS_MODE_ALIASED);
        for (size_t i = 0; i < strip.pieces.size(); i++) {
            paint(strip.pieces[i].ops, {UiSnap(r.left + row.pieces[i], scale), r.top}, 1, -1);
        }
        paint(strip.ops, {left, r.top}, 1, hovered);
        target->SetTransform(&base);
        if (row.overflow > 0) {
            D2D1_COLOR_F solid = color(background), clear = color(background, 0);
            if (row.left < 0) {
                UiFillGradient(target, cache, {r.left, r.top, r.left + std::min(96.0f, width), r.bottom}, 4, solid,
                               clear, false);
            }
            if (row.left > -row.overflow) {
                UiFillGradient(target, cache, {r.right - std::min(48.0f, width), r.top, r.right, r.bottom}, 4, clear,
                               solid, false);
            }
            float thumbWidth = width * width / (width + row.overflow);
            float thumbLeft = r.left + (width - thumbWidth) * (-row.left / row.overflow);
            UiFillRect(target, cache, {thumbLeft, r.bottom - 4, thumbLeft + thumbWidth, r.bottom - 2}, 1,
                       color(text, 0.4f), scale);
        }
        target->PopAxisAlignedClip();
        if (hc) UiStrokeRect(target, cache, r, 4, color(text), 1, scale);
    }
    target->SetTransform(&base);
    target->SetTextAntialiasMode(antialias);
}

// The preview card (mockup .pvcard): "Preview", the example-accounts note, the Show on taskbar
// switch and the strip, as tall as the taskbar. Items take `items`' coordinates from (x, y);
// returns the card's bottom.
static float SettingsAddPreviewCard(UiGraphics& g, const SettingsView& view, std::vector<UiItem>& items,
                                    uint32_t& id, float x, float y, float width) {
    constexpr float kPadX = 1 + 14, kPadTop = 1 + 12, kRow = 20, kGap = 10, kPadBottom = 14 + 1;
    SettingsPreviewStrip strip = SettingsPreviewLayout(g, view.settings, view.status.taskbarHeight, view.scale,
                                                       view.now, false, false, 0);
    float stripWidth = std::max(width - 2 * kPadX, 1.0f);
    const SettingsPreviewRow home = SettingsPreviewRowAt(strip, stripWidth, 0);
    const bool cut = home.tiles < 0 || home.tiles + strip.width > stripWidth;
    float bottom = y + kPadTop + kRow + kGap + strip.height + kPadBottom;

    UiItem card;
    card.id = id++;
    card.kind = UiItemKind::Card;
    card.rect = {x, y, x + width, bottom};
    items.push_back(std::move(card));

    // Title and note, then the switch with its label at the right. The note says when the
    // tiles don't fit at the strip's home; it and then the label are left out where they wouldn't
    // fit on one line.
    float row = y + kPadTop, center = row + kRow / 2;
    UiTextStyle body = UiTextStyleFor(UiFont::Body), caption = UiTextStyleFor(UiFont::Caption);
    float titleWidth = std::ceil(UiLayoutSize(UiLayout(g, L"Preview", body, 1000, false)).width);
    PCWSTR switchLabel = L"Show on taskbar";
    float labelWidth = std::ceil(UiLayoutSize(UiLayout(g, switchLabel, caption, 1000, false)).width);
    float switchRight = x + width - kPadX, labelLeft = switchRight - 40 - 12 - labelWidth;
    if (labelLeft < x + kPadX + titleWidth + 12) {
        labelWidth = 0;
        labelLeft = switchRight - 40;
    }
    auto text = [&](std::wstring s, UiFont font, UiInk ink, D2D1_RECT_F rect) {
        UiItem line;
        line.id = id++;
        line.kind = UiItemKind::Text;
        line.font = font;
        line.ink = ink;
        line.rect = rect;
        line.runs.push_back({std::move(s)});
        items.push_back(std::move(line));
    };
    text(L"Preview", UiFont::Body, UiInk::Primary, {x + kPadX, row, x + kPadX + titleWidth, row + kRow});
    std::wstring note = cut ? L"Example accounts, wider than shown: drag to scroll" :
                              L"Example accounts, updates as you change settings";
    float noteLeft = x + kPadX + titleWidth + 12;
    if (UiLayoutSize(UiLayout(g, note, caption, 10000, false)).width <= labelLeft - 12 - noteLeft) {
        text(std::move(note), UiFont::Caption, UiInk::Muted, {noteLeft, center - 8, labelLeft - 12, center + 8});
    }
    if (labelWidth > 0) {
        text(switchLabel, UiFont::Caption, UiInk::Muted, {labelLeft, center - 8, labelLeft + labelWidth, center + 8});
    }
    bool shown = view.status.visualTest;
    UiAddToggle(items, SettingsControlId(SettingsField::ShowOnTaskbar), {labelLeft - 4, center - 16, switchRight, center + 16},
                shown, true, SettingsCommand(SettingsField::ShowOnTaskbar, !shown));
    items.back().text.clear();

    UiItem preview;
    preview.id = SettingsControlId(SettingsField::PreviewScroll);
    preview.kind = UiItemKind::Preview;
    float stripTop = row + kRow + kGap;
    preview.rect = {x + kPadX, stripTop, x + kPadX + stripWidth, stripTop + strip.height};
    preview.minimum = int(std::floor(home.minimum));
    preview.maximum = int(std::ceil(home.maximum));
    preview.action = {UiActionKind::Pan, int(std::clamp(view.previewScroll, home.minimum, home.maximum)), {},
                      uint32_t(SettingsField::PreviewScroll)};
    items.push_back(std::move(preview));
    return bottom;
}
