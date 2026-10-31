// Settings preview: the taskbar strip under the Layout title and the tile style gallery's
// thumbnails, drawn from the shared meter model (taskbar/meter-model.h) with the visual test
// accounts at 1:1 DIPs. Pure: the layout mirrors the taskbar's XAML (quota-grid.h, tiles.h,
// quota-update.h) as draw operations; painting takes an explicit target and theme.
//
// Units: the strip is as tall as the primary taskbar in DIPs (converted with that taskbar's
// DPI); the meter model's pixel details (pace ticks, text offsets) and snapping use the preview
// window's scale. Text uses the fonts the taskbar's XAML resolves: Segoe UI Variable with
// automatic optical size (its default font) for labels, percentages and keys, the named Display
// and Small instances for tile text and badges.

// One draw operation, in strip DIPs. Colors are ARGB.
struct SettingsPreviewOp {
    enum class Kind : uint8_t { Rect, Arc, Dot, Polygon, Text, Logo };
    Kind kind = Kind::Rect;
    int hover = -1;                     // Drawn only while this column is hovered; -1: always
    D2D1_RECT_F rect{};                 // Rect, Dot bounds, Text box (aligns in its width), Logo box
    float radius = 0;                   // Rect corners; Arc centerline radius
    float stroke = 0;                   // Rect border (inside), Arc width, Dot outline
    uint32_t color = 0;                 // Fill, arc or text color
    uint32_t outline = 0;               // Rect border and Dot outline
    float opacity = 1;
    D2D1_POINT_2F center{};             // Arc
    float start = 0, sweep = 0;         // Arc: degrees clockwise from the right
    std::vector<D2D1_POINT_2F> points;  // Polygon
    std::wstring text;
    UiTextStyle style;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    bool clip = false;                  // Text clipped to its box (XAML MaxWidth, clip trimming)
    PCWSTR path = nullptr;              // Logo path data (24 DIP view box), scaled to the box
};

// A strip: one column per sample account, laid out from x 0 (the first column's margin box) and
// centered on the strip's height; the tray from its own left edge.
struct SettingsPreviewStrip {
    std::vector<SettingsPreviewOp> ops, tray;
    std::vector<D2D1_RECT_F> columns;  // Hover targets: the XAML columns without their margins
    float width = 0;                   // Columns with their margins
    float trayWidth = 0;
    float height = 0;
    float rightMargin = 0;             // Tray gap
};

// The taskbar's layout of the sample accounts in `settings`. Gallery: the two Anthropic sample
// accounts on a 48 DIP strip, without a tray. `light` and `text` color the tile chrome and the
// taskbar's text (logos, labels, keys, tray).
static SettingsPreviewStrip SettingsPreviewLayout(UiGraphics& g, const Settings& settings,
                                                  double taskbarHeight, float scale, ULONGLONG now,
                                                  bool gallery, bool light, uint32_t text) {
    using Op = SettingsPreviewOp;
    SettingsPreviewStrip strip;
    MeterDisplay display = MeterDisplayFor(settings, now, scale, taskbarHeight);
    std::vector<AccountConfig> accounts;
    std::vector<AccountData> data;
    BuildVisualTestSnapshot(display, &accounts, &data);
    if (gallery) {
        accounts = {accounts[1], accounts[2]};
        data = {data[1], data[2]};
    }
    const BarLayout layout = BarLayout(std::clamp(int(settings.barLayout), int(BarLayout::Stacked),
                                                  int(BarLayout::LogoHeadline)));
    const float H = gallery ? 48 : float(std::clamp(taskbarHeight > 0 ? taskbarHeight : 48.0, 24.0, 96.0));
    const float margin = float(std::clamp(settings.accountMargin, 0, 500));
    strip.height = H;
    strip.rightMargin = float(std::clamp(settings.rightMargin, 0, 500));
    const TilePalette palette = TilePaletteFor(light);
    const uint32_t pace = 0xFF000000 | uint32_t(GetRValue(display.paceTickColor)) << 16 |
                          uint32_t(GetGValue(display.paceTickColor)) << 8 | GetBValue(display.paceTickColor);

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
        op.path = provider == L"openai" ? kOpenAiLogoPath :
                  provider == L"anthropic" ? kClaudeLogoPath : kGeminiLogoPath;
        add(ops, std::move(op));
    };
    // A number badge (tiles.h): a pill with a border, its bold text centered by its cap height.
    auto badge = [&](std::vector<Op>& ops, D2D1_RECT_F box, const std::wstring& s, float font, float opacity) {
        Op op;
        op.rect = box;
        op.radius = (box.bottom - box.top) / 2;
        op.color = palette.badge;
        op.outline = palette.badgeOutline;
        op.stroke = 1.2f;
        op.opacity = opacity;
        add(ops, std::move(op));
        UiTextStyle style{UiFamily::Small, font, 0, DWRITE_FONT_WEIGHT_BOLD};
        label(ops, s, style, box.left, (box.top + box.bottom) / 2 - measure(s, style).cap / 2,
              box.right - box.left, DWRITE_TEXT_ALIGNMENT_CENTER, text, opacity, true);
    };

    float x = 0;
    for (size_t i = 0; i < accounts.size(); i++) {
        const AccountConfig& account = accounts[i];
        const int column = int(i);
        float x0 = x + margin, width = 0, top = 0, height = 0;
        std::vector<Op>& ops = strip.ops;

        if (!IsTileLayout(layout)) {
            // Bars (quota-grid.h): a label beside, above or below a stack of bars; each bar a
            // track, its fill and pace tick, an optional quota key and the percentage text.
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
            const float itemWidth = float(vertical ? th : len) + (keys ? keyColumn : 0);
            const float itemHeight = float(vertical ? len : th);
            const BarGeometry geometry = BarGeometryFor(display);
            AccountMeters meters = AccountMeterState(display, layout, account, data[i], 0, 0);
            int count = std::popcount(unsigned(meters.barMask));

            float barsWidth = 0, barsHeight = 0;
            if (count) {
                barsWidth = vertical ? count * itemWidth + (count - 1) * gap : itemWidth;
                barsHeight = vertical ? float(len) : count * float(th) + (count - 1) * gap + 2;
            }
            bool labelled = position != LabelPosition::Hidden;
            bool side = position == LabelPosition::Left || position == LabelPosition::Right;
            Measured labelText = measure(meters.labelText, labelStyle);
            float labelWidth = labelled ? labelText.width : 0;
            // Side labels have a -2 DIP top margin; stacked ones the label gap toward the bars.
            if (side) {
                width = labelWidth + labelGap + barsWidth;
                height = std::max(labelText.height - 2, barsHeight);
            } else {
                width = std::max(labelWidth, barsWidth);
                height = (labelled ? labelText.height + labelGap : 0) + barsHeight;
            }
            if (!count) {
                // A small context-menu target stays when no quota shows.
                width = std::max(width, float(std::max(th, 8)));
                height = std::max(height, float(std::max(th, 8)));
            }
            top = (H - height) / 2;

            if (labelled) {
                float left = position == LabelPosition::Right ? barsWidth + labelGap :
                             side ? 0 : (width - labelWidth) / 2;
                float labelTop = side ? top + (height - labelText.height) / 2 - 1 :
                                 position == LabelPosition::Top ? top : top + barsHeight + labelGap;
                label(ops, meters.labelText, labelStyle, x0 + left, labelTop, labelWidth,
                      DWRITE_TEXT_ALIGNMENT_LEADING, text, meters.labelOpacity);
            }
            float barsLeft = x0 + (position == LabelPosition::Left ? labelWidth + labelGap : 0);
            float barsTop = side ? top + (height - barsHeight) / 2 :
                            position == LabelPosition::Top ? top + labelText.height + labelGap : top;
            float panelWidth = side ? barsWidth : width;
            int k = 0;
            for (int w = 0; w < kQuotaBarCount; w++) {
                if (!(meters.barMask & (1 << w))) continue;
                const QuotaMeter& q = meters.quotas[w];
                // Stacked items center in the panel; vertical ones line up from its left.
                float itemLeft = vertical ? barsLeft + k * (itemWidth + gap) :
                                            barsLeft + (panelWidth - itemWidth) / 2;
                float itemTop = vertical ? barsTop : barsTop + 1 + k * (float(th) + gap);
                float tx = itemLeft + (keys ? keyColumn : 0), ty = itemTop;
                float trackWidth = float(vertical ? th : len), trackHeight = itemHeight;
                float radius = std::max(1.0f, th / 2.0f);
                rect(ops, {tx, ty, tx + trackWidth, ty + trackHeight}, radius, 0x46808080);
                if (q.fillPx > 0) {
                    rect(ops, vertical ? D2D1_RECT_F{tx, ty + len - q.fillPx, tx + th, ty + len} :
                                         D2D1_RECT_F{tx, ty, tx + q.fillPx, ty + th},
                         radius, q.color);
                }
                if (display.showPaceTicks && q.paceVisible) {
                    bool notch = display.paceTickStyle == PaceTickStyle::Notch;
                    float along = geometry.paceLength, cross = geometry.paceCrossSize;
                    D2D1_POINT_2F at = vertical ?
                        D2D1_POINT_2F{notch ? tx : tx + (th - cross) / 2, ty + len - q.pacePx - along} :
                        D2D1_POINT_2F{tx + q.pacePx, notch ? ty : ty + (th - cross) / 2};
                    if (display.paceTickStyle == PaceTickStyle::Caret) {
                        // A dark halo polygon, then the core.
                        for (int layer = 0; layer < 2; layer++) {
                            Op op;
                            op.kind = Op::Kind::Polygon;
                            op.color = layer ? pace : 0xC0000000;
                            for (auto [a, c] : geometry.caret[layer]) {
                                op.points.push_back(vertical ? D2D1_POINT_2F{at.x + float(c), at.y + float(a)} :
                                                               D2D1_POINT_2F{at.x + float(a), at.y + float(c)});
                            }
                            add(ops, std::move(op));
                        }
                    } else {
                        float boxWidth = vertical ? cross : along, boxHeight = vertical ? along : cross;
                        rect(ops, {at.x, at.y, at.x + boxWidth, at.y + boxHeight}, geometry.paceRadius,
                             0xC0000000);
                        float coreWidth = vertical ? geometry.coreCrossSize : geometry.coreLength;
                        float coreHeight = vertical ? geometry.coreLength : geometry.coreCrossSize;
                        float cx = at.x + (boxWidth - coreWidth) / 2, cy = at.y + (boxHeight - coreHeight) / 2;
                        rect(ops, {cx, cy, cx + coreWidth, cy + coreHeight}, geometry.coreRadius, pace);
                    }
                }
                if (keys) {
                    label(ops, w == kExtraUsageBar && account.provider == L"openai" ? L"Cr" : kQuotaKeys[w],
                          textStyle, itemLeft, itemTop + (itemHeight - measure(L"5h", textStyle).height) / 2 - 0.5f,
                          keyColumn - 3, DWRITE_TEXT_ALIGNMENT_TRAILING, text, 0.8f);
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
            // Tiles (tiles.h): the identity (logo, badge, or text) and the meters in a 4 DIP
            // rounded column; hovered, the column takes the tile hover fill.
            const float T = float(display.tileHeight);
            const bool ring = layout == BarLayout::SegmentedRing || layout == BarLayout::ConcentricRings;
            const bool miniBars = layout == BarLayout::LogoBars;
            TileIdentityText identity = TileIdentityFor(accounts, i);
            const bool hasLogo = account.tileIdentity != TileIdentity::Text;
            const bool hasBadge = hasLogo && identity.badge;
            const UiTextStyle idStyle{UiFamily::Display, 10, 0, DWRITE_FONT_WEIGHT_SEMI_BOLD};
            double perPt = hasLogo ? 0 : std::max(measure(identity.text, idStyle).width, 0.1f) / 10.0;
            AccountMeters meters = AccountMeterState(display, layout, account, data[i],
                                                     hasBadge ? int(identity.badgeText.size()) : 0, perPt);
            const float opacity = float(meters.identityOpacity);
            const UiTextStyle warnStyle{UiFamily::Taskbar, 10, 0, DWRITE_FONT_WEIGHT_BOLD};
            const float padLeft = ring ? 4 : 7, padRight = ring ? 4 : miniBars ? 7 : 8;
            top = (H - T) / 2;
            height = T;
            size_t hoverAt = ops.size();
            int count = std::popcount(unsigned(meters.barMask));

            if (ring) {
                const TileRingGeometry& geometry = meters.ring;
                const float S = float(geometry.size);
                const float faceLeft = x0 + padLeft, faceTop = top + (T - S) / 2;
                const D2D1_POINT_2F center{faceLeft + S / 2, faceTop + S / 2};
                width = padLeft + S + padRight;
                // Tracks, then fills, then pace dots on top.
                for (int pass = 0; pass < 3; pass++) {
                    for (int w = 0; w < kQuotaBarCount; w++) {
                        const QuotaMeter& q = meters.quotas[w];
                        if (!(meters.barMask & (1 << w))) continue;
                        if (pass == 2) {
                            if (!display.showPaceTicks || !q.paceVisible) continue;
                            double radians = q.paceAngle * std::numbers::pi / 180;
                            float px = center.x + float(q.radius * std::cos(radians));
                            float py = center.y + float(q.radius * std::sin(radians));
                            float dot = float(geometry.dot);
                            Op op;
                            op.kind = Op::Kind::Dot;
                            op.rect = {px - dot, py - dot, px + dot, py + dot};
                            op.color = pace;
                            op.outline = 0x8C000000;
                            op.stroke = 0.8f;
                            add(ops, std::move(op));
                            continue;
                        }
                        if (pass == 1 && !q.fillUnits) continue;
                        Op op;
                        op.kind = Op::Kind::Arc;
                        op.center = center;
                        op.radius = float(q.radius);
                        op.stroke = float(geometry.stroke);
                        op.start = float(q.arc.start);
                        op.sweep = float(pass ? q.fillSweep : q.arc.sweep);
                        op.color = pass ? q.color : palette.track;
                        add(ops, std::move(op));
                    }
                }
                // Identity, sized to the hole; with a badge the logo shifts up-left by a DIP.
                if (hasLogo) {
                    float side = float(hasBadge ? geometry.logo - 1 : geometry.logo);
                    float area = hasBadge ? S - 2 : S;
                    float left = faceLeft + (area - side) / 2, logoTop = faceTop + (area - side) / 2;
                    logo(ops, account.provider, {left, logoTop, left + side, logoTop + side}, opacity);
                    if (hasBadge) {
                        float left2 = faceLeft + float(geometry.badgeLeft), top2 = faceTop + float(geometry.badgeTop);
                        badge(ops, {left2, top2, left2 + float(geometry.badgeWidth), top2 + float(geometry.badge)},
                              identity.badgeText, float(geometry.badgeFont), opacity);
                    }
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
            } else {
                // Logo + bars and headline: the identity, then the meters 6 DIP after it.
                float idWidth, idHeight, font = 0;
                if (hasLogo) {
                    idWidth = idHeight = miniBars ? 15 : 16;
                } else {
                    font = float(std::clamp(26 / perPt, 8.0, 12.0));
                    Measured m = measure(identity.text, {UiFamily::Display, font, 0, DWRITE_FONT_WEIGHT_SEMI_BOLD});
                    idWidth = std::min(m.width, 26.0f);
                    idHeight = m.cap;
                }
                float idLeft = x0 + padLeft, idTop = top + (T - idHeight) / 2;
                if (hasLogo) {
                    logo(ops, account.provider, {idLeft, idTop, idLeft + idWidth, idTop + idHeight}, opacity);
                    if (hasBadge) {
                        // Centered on the logo's lower-right corner, overhanging it by 4 DIPs.
                        UiTextStyle style{UiFamily::Small, 6.8f, 0, DWRITE_FONT_WEIGHT_BOLD};
                        float badgeWidth = std::max(10.0f, measure(identity.badgeText, style).width + 3 + 2.4f);
                        float right = idLeft + idWidth + 4, bottom = idTop + idHeight + 4;
                        badge(ops, {right - badgeWidth, bottom - 10, right, bottom}, identity.badgeText, 6.8f, opacity);
                    }
                } else {
                    label(ops, identity.text, {UiFamily::Display, font, 0, DWRITE_FONT_WEIGHT_SEMI_BOLD}, idLeft,
                          idTop, idWidth, DWRITE_TEXT_ALIGNMENT_LEADING, text, opacity, true, -1, true);
                }
                if (meters.warn) {
                    Measured m = measure(L"!", warnStyle);
                    label(ops, L"!", warnStyle, idLeft + idWidth + 4 - m.width, idTop - 3, m.width,
                          DWRITE_TEXT_ALIGNMENT_LEADING, text, opacity, true);
                }
                float metersLeft = idLeft + idWidth + 6;
                float content = 0;
                if (miniBars) {
                    // Bars 38 x 4 DIP, 3 DIP apart; pace: a line tick with a dark outline,
                    // overhanging the bar by 2 DIPs.
                    float barsHeight = count ? count * kMiniBarThickness + (count - 1) * kMiniBarGap : 0;
                    float y = top + (T - float(barsHeight)) / 2;
                    for (int w = 0; w < kQuotaBarCount; w++) {
                        const QuotaMeter& q = meters.quotas[w];
                        if (!(meters.barMask & (1 << w))) continue;
                        float left = metersLeft, thick = float(kMiniBarThickness);
                        rect(ops, {left, y, left + float(kMiniBarLength), y + thick}, thick / 2, palette.track);
                        if (q.fillUnits) {
                            rect(ops, {left, y, left + float(q.fillLength), y + thick}, thick / 2, q.color);
                        }
                        if (display.showPaceTicks && q.paceVisible) {
                            float tick = left + float(q.paceOffset);
                            rect(ops, {tick, y - 2, tick + 3.5f, y + thick + 2}, 1.5f, 0x8C000000);
                            rect(ops, {tick + 0.75f, y - 1.25f, tick + 2.75f, y + thick + 1.25f}, 1, pace);
                        }
                        y += thick + float(kMiniBarGap);
                    }
                    content = count ? float(kMiniBarLength) : 0;
                } else {
                    // Headline: the most-used quota's number and window key (bottom-aligned on
                    // the baseline), over per-quota segments.
                    UiTextStyle numberStyle{UiFamily::Display, 14, 0, DWRITE_FONT_WEIGHT_SEMI_BOLD};
                    UiTextStyle keyStyle{UiFamily::Taskbar, 9, 0, DWRITE_FONT_WEIGHT_NORMAL};
                    Measured number = measure(meters.headline, numberStyle);
                    float numberWidth = std::max(number.width, std::ceil(measure(L"100%", numberStyle).width));
                    Measured key = measure(meters.headlineKey, keyStyle);
                    float rowHeight = std::max(number.cap, meters.headlineKey.empty() ? 0.0f : key.cap);
                    float segments = count ? count * float(kHeadlineSegmentLength) + (count - 1) * 2.0f : 0;
                    float panelHeight = rowHeight + (count ? 3 + float(kHeadlineSegmentThickness) : 0);
                    float panelTop = top + (T - panelHeight) / 2;
                    label(ops, meters.headline, numberStyle, metersLeft, panelTop + rowHeight - number.cap,
                          numberWidth, DWRITE_TEXT_ALIGNMENT_LEADING, meters.headlineColor, 1, true);
                    if (!meters.headlineKey.empty()) {
                        label(ops, meters.headlineKey, keyStyle, metersLeft + numberWidth + 3,
                              panelTop + rowHeight - key.cap, key.width, DWRITE_TEXT_ALIGNMENT_LEADING, text,
                              0.6f, true);
                    }
                    float left = metersLeft, y = panelTop + rowHeight + 3;
                    float thick = float(kHeadlineSegmentThickness);
                    for (int w = 0; w < kQuotaBarCount; w++) {
                        const QuotaMeter& q = meters.quotas[w];
                        if (!(meters.barMask & (1 << w))) continue;
                        rect(ops, {left, y, left + float(kHeadlineSegmentLength), y + thick}, thick / 2,
                             palette.track);
                        if (q.fillUnits) {
                            rect(ops, {left, y, left + float(q.fillLength), y + thick}, thick / 2, q.color);
                        }
                        left += float(kHeadlineSegmentLength) + 2;
                    }
                    content = std::max(numberWidth + 3 + (meters.headlineKey.empty() ? 0 : key.width), segments);
                }
                width = padLeft + idWidth + (content > 0 ? 6 + content : 0) + padRight;
            }
            Op hover;
            hover.rect = {x0, top, x0 + width, top + T};
            hover.radius = 4;
            hover.color = palette.hover;
            hover.hover = column;
            ops.insert(ops.begin() + std::ptrdiff_t(hoverAt), std::move(hover));
        }
        strip.columns.push_back({x0, top, x0 + width, top + height});
        x = x0 + width + margin;
    }
    strip.width = x;

    // Tray (mockup .tray): overflow chevron, three status glyphs, then the two-line clock.
    if (!gallery) {
        auto glyph = [&](PCWSTR s, float size, float left, float width) {
            label(strip.tray, s, UiIconStyle(size), left, H / 2 - size / 2, width,
                  DWRITE_TEXT_ALIGNMENT_CENTER, text, 1);
        };
        glyph(L"\uE70E", 11, 0, 24);
        float left = 24 + 4;
        for (PCWSTR icon : {L"\uE701", L"\uE767", L"\uE83F"}) {
            glyph(icon, 14, left, 26);
            left += 26;
        }
        left += 4 + 8;
        UiTextStyle clock{UiFamily::Taskbar, 12, 16, DWRITE_FONT_WEIGHT_NORMAL};
        float clockWidth = std::max(measure(L"14:32", clock).width, measure(L"2026-10-31", clock).width);
        label(strip.tray, L"14:32", clock, left, H / 2 - 16, clockWidth, DWRITE_TEXT_ALIGNMENT_TRAILING, text, 1);
        label(strip.tray, L"2026-10-31", clock, left, H / 2, clockWidth, DWRITE_TEXT_ALIGNMENT_TRAILING, text, 1);
        strip.trayWidth = left + clockWidth + 10 + 4;
    }
    return strip;
}

// How far a strip `width` DIPs wide must scroll to show every column: the panel keeps a 4 DIP
// margin at its left and the tray gap at its right.
static float SettingsPreviewOverflow(const SettingsPreviewStrip& strip, float width) {
    return std::max(4 + strip.width + strip.rightMargin + strip.trayWidth - width, 0.0f);
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
                    float radius = std::min({op.radius, (r.right - r.left) / 2, (r.bottom - r.top) / 2});
                    UiFillRect(target, cache, r, radius, color(op.color, op.opacity), scale * zoom);
                    if (op.stroke > 0) {
                        D2D1_ROUNDED_RECT edge{UiInflate(r, -op.stroke / 2, -op.stroke / 2),
                                               std::max(radius - op.stroke / 2, 0.0f),
                                               std::max(radius - op.stroke / 2, 0.0f)};
                        target->DrawRoundedRectangle(&edge, UiBrush(target, cache, color(op.outline, op.opacity)),
                                                     op.stroke);
                    }
                    break;
                }
                case Op::Kind::Arc:
                case Op::Kind::Polygon: {
                    // Arcs in two halves, so a full circle draws too.
                    winrt::com_ptr<ID2D1PathGeometry> path;
                    winrt::check_hresult(g.d2d->CreatePathGeometry(path.put()));
                    winrt::com_ptr<ID2D1GeometrySink> sink;
                    winrt::check_hresult(path->Open(sink.put()));
                    auto point = [&](float degrees) {
                        double radians = degrees * std::numbers::pi / 180;
                        return D2D1::Point2F(op.center.x + float(op.radius * std::cos(radians)),
                                             op.center.y + float(op.radius * std::sin(radians)));
                    };
                    if (op.kind == Op::Kind::Arc) {
                        sink->BeginFigure(point(op.start), D2D1_FIGURE_BEGIN_HOLLOW);
                        for (int half = 1; half <= 2; half++) {
                            D2D1_ARC_SEGMENT arc{point(op.start + op.sweep * float(half) / 2),
                                                 {op.radius, op.radius}, 0,
                                                 op.sweep >= 0 ? D2D1_SWEEP_DIRECTION_CLOCKWISE :
                                                                 D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                                 D2D1_ARC_SIZE_SMALL};
                            sink->AddArc(&arc);
                        }
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    } else if (!op.points.empty()) {
                        sink->BeginFigure(op.points[0], D2D1_FIGURE_BEGIN_FILLED);
                        for (size_t p = 1; p < op.points.size(); p++) sink->AddLine(op.points[p]);
                        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    }
                    winrt::check_hresult(sink->Close());
                    ID2D1SolidColorBrush* brush = UiBrush(target, cache, color(op.color, op.opacity));
                    if (op.kind == Op::Kind::Arc) target->DrawGeometry(path.get(), brush, op.stroke, g.roundCaps.get());
                    else target->FillGeometry(path.get(), brush, nullptr);
                    break;
                }
                case Op::Kind::Dot: {
                    D2D1_ELLIPSE dot{{(r.left + r.right) / 2, (r.top + r.bottom) / 2}, (r.right - r.left) / 2,
                                     (r.bottom - r.top) / 2};
                    target->FillEllipse(&dot, UiBrush(target, cache, color(op.color, op.opacity)));
                    target->DrawEllipse(&dot, UiBrush(target, cache, color(op.outline, op.opacity)), op.stroke);
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
                    target->FillGeometry(UiPathGeometry(g, op.path),
                                         UiBrush(target, cache, color(op.color, op.opacity)), nullptr);
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
        // Columns right-anchored before the tray gap, scrolled right by the preview scroll;
        // content beyond the strip fades at that edge, with a thin position bar along the bottom.
        const D2D1_RECT_F& r = item.rect;
        float trayLeft = std::max(r.right - r.left - strip.trayWidth, 0.0f);
        float overflow = SettingsPreviewOverflow(strip, r.right - r.left);
        float scroll = std::clamp(view.previewScroll, 0.0f, overflow);
        float left = UiSnap(r.left + trayLeft - strip.rightMargin - strip.width + scroll, scale);
        D2D1_RECT_F tiles{r.left, r.top, r.left + trayLeft, r.bottom};
        int hovered = -1;
        if (UiContains(tiles, pointer)) {
            for (size_t i = 0; i < strip.columns.size(); i++) {
                if (UiContains(strip.columns[i], {pointer.x - left, pointer.y - r.top})) hovered = int(i);
            }
        }
        UiFillRect(target, cache, r, 4, color(background), scale);
        target->PushAxisAlignedClip(&r, D2D1_ANTIALIAS_MODE_ALIASED);
        target->PushAxisAlignedClip(&tiles, D2D1_ANTIALIAS_MODE_ALIASED);
        paint(strip.ops, {left, r.top}, 1, hovered);
        target->SetTransform(&base);
        target->PopAxisAlignedClip();
        paint(strip.tray, {r.left + trayLeft, r.top}, 1, -1);
        target->SetTransform(&base);
        if (overflow > 0) {
            D2D1_COLOR_F solid = color(background), clear = color(background, 0);
            if (scroll < overflow) {
                UiFillGradient(target, cache, {r.left, r.top, r.left + std::min(96.0f, trayLeft), r.bottom}, 4,
                               solid, clear, false);
            }
            if (scroll > 0) {
                UiFillGradient(target, cache, {r.left + std::max(trayLeft - 48, 0.0f), r.top, r.left + trayLeft, r.bottom},
                               0, clear, solid, false);
            }
            float thumbWidth = trayLeft * trayLeft / (trayLeft + overflow);
            float thumbLeft = r.left + (trayLeft - thumbWidth) * (1 - scroll / overflow);
            UiFillRect(target, cache, {thumbLeft, r.bottom - 4, thumbLeft + thumbWidth, r.bottom - 2}, 1,
                       color(text, 0.4f), scale);
        }
        target->PopAxisAlignedClip();
        if (hc) UiStrokeRect(target, cache, r, 4, color(text), 1, scale);
    }
    target->SetTransform(&base);
    target->SetTextAntialiasMode(antialias);
}

// The preview card (mockup .pvcard): "Preview", the sample-data note, the Show on taskbar switch
// and the strip, as tall as the taskbar. Items take `items`' coordinates from (x, y); returns
// the card's bottom.
static float SettingsAddPreviewCard(UiGraphics& g, const SettingsView& view, std::vector<UiItem>& items,
                                    uint32_t& id, float x, float y, float width) {
    constexpr float kPadX = 1 + 14, kPadTop = 1 + 12, kRow = 20, kGap = 10, kPadBottom = 14 + 1;
    SettingsPreviewStrip strip = SettingsPreviewLayout(g, view.settings, view.status.taskbarHeight, view.scale,
                                                       view.now, false, false, 0);
    float stripWidth = std::max(width - 2 * kPadX, 1.0f);
    float overflow = SettingsPreviewOverflow(strip, stripWidth);
    float bottom = y + kPadTop + kRow + kGap + strip.height + kPadBottom;

    UiItem card;
    card.id = id++;
    card.kind = UiItemKind::Card;
    card.rect = {x, y, x + width, bottom};
    items.push_back(std::move(card));

    // Title and note, then the switch with its label at the right. The note says when the
    // tiles don't fit; it and then the label are left out where they wouldn't fit on one line.
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
    std::wstring note = overflow > 0 ? L"Sample data, wider than shown: drag to scroll" :
                                       L"Sample data, updates as you change settings";
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
    preview.maximum = int(std::ceil(overflow));
    preview.action = {UiActionKind::Pan, int(std::min(view.previewScroll, overflow)), {},
                      uint32_t(SettingsField::PreviewScroll)};
    items.push_back(std::move(preview));
    return bottom;
}
