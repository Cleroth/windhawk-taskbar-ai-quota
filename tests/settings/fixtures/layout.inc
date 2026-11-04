// Layout page: slider input, the Layout fields and their reset group, the render projection the
// preview reads, the preview strip model (settings/preview.h, bar styles, time tracks, face tiles
// and the headline countdown included), the page builder and the pinned preview header. Synthetic
// inputs, hand-derived expectations.

static bool HasTextIn(const std::vector<UiItem>& items, std::wstring_view text) {
    return std::any_of(items.begin(), items.end(), [&](const UiItem& item) {
        return UiRunsText(item.runs).find(text) != std::wstring::npos;
    });
}

static std::vector<SettingsPreviewOp> OpsOfColor(const SettingsPreviewStrip& strip, uint32_t color) {
    std::vector<SettingsPreviewOp> ops;
    for (const SettingsPreviewOp& op : strip.ops) {
        if (op.color == color) ops.push_back(op);
    }
    return ops;
}

static SettingsPreviewStrip Strip(const Settings& settings, double taskbar = 48, bool gallery = false,
                                  float scale = 1) {
    return SettingsPreviewLayout(TestGraphics(), settings, taskbar, scale, kNow, gallery, false, 0xFFFFFFFF);
}

// The preview of `accounts` with their `data` in place of the sample accounts.
static SettingsPreviewStrip StripOf(const Settings& settings, const std::vector<AccountConfig>& accounts,
                                    const std::vector<AccountData>& data, double taskbar = 48) {
    return SettingsPreviewLayout(TestGraphics(), settings, taskbar, 1, kNow, false, false, 0xFFFFFFFF, &accounts,
                                 &data);
}

// The bar label's line at `size` DIPs, from Segoe UI Variable's design metrics (2048 units per em:
// ascent 2210, descent 514, cap height 1434, no line gap): its space over the cap height and
// under the baseline, and the cap height.
static double AboveCap(double size) { return size * (2210 - 1434) / 2048.0; }
static double BelowBaseline(double size) { return size * 514 / 2048.0; }
static double CapHeight(double size) { return size * 1434 / 2048.0; }

// DIP positions built from DirectWrite's float metrics.
static bool NearDip(double actual, double expected) { return std::abs(actual - expected) < 1e-3; }

// The default number badge's overhang past a logo's lower-right corner, which it centres on: one
// character at 13 DIPs makes a 0.6 * 13 + 2 = 9.8 by 0.7 * 13 + 2 = 11.1 DIP box, outline included.
static constexpr double kBadgeRight = 4.9, kBadgeBelow = 5.55;

static std::vector<SettingsPreviewOp> OpsOfKind(const SettingsPreviewStrip& strip, SettingsPreviewOp::Kind kind) {
    std::vector<SettingsPreviewOp> ops;
    for (const SettingsPreviewOp& op : strip.ops) {
        if (op.kind == kind) ops.push_back(op);
    }
    return ops;
}

// The text operations showing `text`.
static std::vector<SettingsPreviewOp> TextOps(const SettingsPreviewStrip& strip, std::wstring_view text) {
    std::vector<SettingsPreviewOp> ops;
    for (const SettingsPreviewOp& op : OpsOfKind(strip, SettingsPreviewOp::Kind::Text)) {
        if (op.text == text) ops.push_back(op);
    }
    return ops;
}

static SettingsPreviewOp FirstText(const SettingsPreviewStrip& strip) {
    auto text = std::find_if(strip.ops.begin(), strip.ops.end(), [](const SettingsPreviewOp& op) {
        return op.kind == SettingsPreviewOp::Kind::Text;
    });
    return text == strip.ops.end() ? SettingsPreviewOp{} : *text;
}

static bool Finite(const SettingsPreviewStrip& strip) {
    auto finite = [](D2D1_RECT_F r) {
        return std::isfinite(r.left) && std::isfinite(r.top) && std::isfinite(r.right) && std::isfinite(r.bottom);
    };
    for (const auto* ops : {&strip.ops, &strip.tray}) {
        for (const SettingsPreviewOp& op : *ops) {
            if (!finite(op.rect) || !std::isfinite(op.radius)) return false;
            for (D2D1_POINT_2F p : op.points) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
            }
            for (const TileOutlineFigure& figure : op.shape) {
                if (!std::isfinite(figure.x) || !std::isfinite(figure.y)) return false;
                for (const TileOutlineSegment& s : figure.segments) {
                    if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.radius)) return false;
                }
            }
        }
    }
    return std::all_of(strip.columns.begin(), strip.columns.end(), finite) && std::isfinite(strip.width);
}

static void RunLayoutTests() {
    Run("slider: thumb, value under the pointer, keys", [] {
        std::vector<UiItem> items;
        UiAddSlider(items, 7, {100, 0, 320, 32}, 100, 10, 500, true, {UiActionKind::Command, 0, {}, 3});
        const UiItem& slider = items[0];
        CHECK(slider.action.value == 100 && slider.action.target == 3);
        CHECK(std::abs(UiSliderThumbX(slider) - (100 + 220.0 * 90 / 490)) < 1e-3);  // Float math
        CHECK(UiSliderValueAt(slider, 100) == 10 && UiSliderValueAt(slider, 320) == 500);
        CHECK(UiSliderValueAt(slider, 210) == 255);
        CHECK(UiSliderValueAt(slider, -50) == 10 && UiSliderValueAt(slider, 999) == 500);
        CHECK(*UiSliderKey(slider, VK_RIGHT) == 1 && *UiSliderKey(slider, VK_UP) == 1);
        CHECK(*UiSliderKey(slider, VK_LEFT) == -1 && *UiSliderKey(slider, VK_DOWN) == -1);
        CHECK(*UiSliderKey(slider, VK_PRIOR) == 49 && *UiSliderKey(slider, VK_NEXT) == -49);
        CHECK(*UiSliderKey(slider, VK_HOME) == -490 && *UiSliderKey(slider, VK_END) == 490);
        CHECK(!UiSliderKey(slider, 'A'));
        UiAddSlider(items, 8, {0, 0, 100, 32}, 9, 1, 6, true, {UiActionKind::Command, 0, {}, 4});
        CHECK(items[1].action.value == 6 && *UiSliderKey(items[1], VK_PRIOR) == 1);
        // The thumb overhangs the track by its radius: its center at either end hits the slider.
        std::vector<UiItem> one{items[1]};
        CHECK(UiHitTest(TestGraphics(), one, {100, 16}) == UiFocusRef{8});
        CHECK(UiHitTest(TestGraphics(), one, {-9, 16}) == UiFocusRef{8});
        CHECK(UiHitTest(TestGraphics(), one, {111, 16}).id == 0);
        // A slider is a tab stop; a preview strip only pans.
        UiItem preview = TestItem(9, UiItemKind::Preview, {0, 40, 300, 88}, {UiActionKind::Pan, 0, {}, 4});
        CHECK(UiFocusable(slider) && UiActive(preview) && !UiFocusable(preview));
        UiItem centered = TestItem(10, UiItemKind::NumberBox, {0, 0, 60, 32});
        centered.align = DWRITE_TEXT_ALIGNMENT_CENTER;
        CHECK_NEAR(UiNumberBoxTextRect(centered).left, 6);
        CHECK_NEAR(UiNumberBoxTextRect(centered).right, 54);
    });

    Run("layout fields: desired values, ranges and choices clamp", [] {
        Settings s;
        const std::pair<SettingsField, int> values[] = {
            {SettingsField::BarLayout, 4},     {SettingsField::BarStyle, 1},
            {SettingsField::BarLength, 180},  {SettingsField::BarThickness, 12},
            {SettingsField::BarGap, 5},        {SettingsField::RingSize, 36},    {SettingsField::RingThickness, 5},
            {SettingsField::HeadlineFontSize, 18}, {SettingsField::HeadlineBarLength, 24},
            {SettingsField::HeadlineBarThickness, 6}, {SettingsField::HeadlineCountdown, 1},
            {SettingsField::SparklineWidth, 64}, {SettingsField::SparklineHeight, 65}, {SettingsField::SparklineExtra, 2},
            {SettingsField::SparklineTextSize, 12}, {SettingsField::SparklineElapsed, 0},
            {SettingsField::SparklineNowLine, 0}, {SettingsField::SparklineNowCaret, 1}, {SettingsField::SparklineRing, 1},
            {SettingsField::SparklineOutline, 0},
            {SettingsField::LogoScale, 150},   {SettingsField::ClaudeLogoBrand, 1}, {SettingsField::GeminiLogoBrand, 1},
            {SettingsField::BadgeSize, 12}, {SettingsField::BadgeColor, 0x654321},
            {SettingsField::BadgeOffsetX, -5}, {SettingsField::BadgeOffsetY, 7},
            {SettingsField::LabelPosition, 2}, {SettingsField::LabelFontSize, 14}, {SettingsField::LabelGap, 7},
            {SettingsField::ShowBarLabels, 1}, {SettingsField::PercentFontSize, 10}, {SettingsField::ShowPaceTicks, 0},
            {SettingsField::PaceTickStyle, 2}, {SettingsField::PaceTickColor, 0x123456},
            {SettingsField::AccountMargin, 9}, {SettingsField::RightMargin, 11}};
        for (auto [field, value] : values) {
            SettingsAssign(s, field, value);
            CHECK(SettingsFieldValue(s, field) == value);
        }
        CHECK(s.barLayout == BarLayout::ConcentricRings && s.barStyle == BarStyle::Rail &&
              s.labelPosition == LabelPosition::Top && s.headlineCountdown &&
              s.paceTickStyle == PaceTickStyle::Notch && s.paceTickColor == 0x123456 && s.badgeColor == 0x654321 &&
              s.logoBrands.claude && s.logoBrands.gemini && s.sparklineHeight == 65 &&
              s.sparklineExtra == SparklineExtra::Percent && s.sparklineTextSize == 12 && !s.sparklineElapsed &&
              !s.sparklineNowLine && s.sparklineNowCaret && s.sparklineRing && !s.sparklineOutline);
        CHECK(SettingsFieldRange(SettingsField::SparklineTextSize)->minimum == 6 &&
              SettingsFieldRange(SettingsField::SparklineTextSize)->maximum == 16);
        for (SettingsField toggle : {SettingsField::SparklineElapsed, SettingsField::SparklineNowLine,
                                     SettingsField::SparklineNowCaret, SettingsField::SparklineRing,
                                     SettingsField::SparklineOutline}) {
            CHECK(!SettingsFieldRange(toggle));
        }
        CHECK(SettingsFieldRange(SettingsField::SparklineHeight)->minimum == 50 &&
              SettingsFieldRange(SettingsField::SparklineHeight)->maximum == 100);
        CHECK(!SettingsFieldRange(SettingsField::SparklineExtra));
        const std::tuple<SettingsField, int, int> clamps[] = {
            {SettingsField::BarLength, 99999, 500},   {SettingsField::BarThickness, 0, 2},
            {SettingsField::RingSize, 100, 40},       {SettingsField::RingThickness, -3, 1},
            {SettingsField::HeadlineFontSize, 9, 10}, {SettingsField::HeadlineFontSize, 99, 24},
            {SettingsField::HeadlineBarLength, 0, 4}, {SettingsField::HeadlineBarLength, 999, 60},
            {SettingsField::HeadlineBarThickness, 1, 2}, {SettingsField::HeadlineBarThickness, 11, 10},
            {SettingsField::LabelFontSize, 100, 24},  {SettingsField::PercentFontSize, 1, 6},
            {SettingsField::BarGap, -1, 0},           {SettingsField::AccountMargin, 501, 500},
            {SettingsField::BarLayout, 12, 9},        {SettingsField::BarLayout, -1, 0},
            {SettingsField::BarLayout, 8, 8},         {SettingsField::SparklineWidth, 10, 24},
            {SettingsField::SparklineWidth, 999, 120}, {SettingsField::SparklineHeight, 10, 50},
            {SettingsField::SparklineHeight, 999, 100}, {SettingsField::SparklineExtra, 7, 2},
            {SettingsField::SparklineExtra, -1, 0},   {SettingsField::SparklineTextSize, 2, 6},
            {SettingsField::SparklineTextSize, 99, 16}, {SettingsField::SparklineRing, 5, 1},
            {SettingsField::SparklineElapsed, 7, 1},   {SettingsField::SparklineOutline, -4, 1},
            {SettingsField::BarLayout, 6, 6},         {SettingsField::PaceTickStyle, 4, 4},
            {SettingsField::LabelPosition, 7, 4},     {SettingsField::PaceTickStyle, 9, 4},
            {SettingsField::HeadlineCountdown, 5, 1}, {SettingsField::HeadlineCountdown, 0, 0},
            {SettingsField::BarStyle, 5, 1},          {SettingsField::BarStyle, -1, 0},
            {SettingsField::LogoScale, 10, 50},       {SettingsField::LogoScale, 999, 200},
            {SettingsField::BadgeSize, 1, 6},         {SettingsField::BadgeSize, 99, 20},
            {SettingsField::BadgeOffsetX, -50, -20},  {SettingsField::BadgeOffsetY, 50, 20},
            {SettingsField::PaceTickColor, 0x12345678, 0x345678}, {SettingsField::BadgeColor, 0x87654321, 0x654321},
            {SettingsField::ClaudeLogoBrand, 7, 1},  {SettingsField::GeminiLogoBrand, 0, 0}};
        for (auto [field, value, expected] : clamps) {
            SettingsAssign(s, field, value);
            CHECK(SettingsFieldValue(s, field) == expected);
        }
    });

    Run("layout reset: the thirty-seven Layout fields back to defaults, nothing else", [] {
        Settings changed;
        changed.barLayout = BarLayout::LogoHeadline;
        changed.barStyle = BarStyle::Rail;
        changed.barLength = 220;
        changed.barThickness = 14;
        changed.barGap = 6;
        changed.ringSize = 38;
        changed.ringThickness = 5;
        changed.headlineFontSize = 20;
        changed.headlineBarLength = 30;
        changed.headlineBarThickness = 7;
        changed.headlineCountdown = true;
        changed.sparklineWidth = 80;
        changed.sparklineHeight = 60;
        changed.sparklineExtra = SparklineExtra::Off;
        changed.sparklineTextSize = 12;
        changed.sparklineElapsed = false;
        changed.sparklineNowLine = false;
        changed.sparklineNowCaret = true;
        changed.sparklineRing = true;
        changed.sparklineOutline = false;
        changed.logoScale = 150;
        changed.logoBrands = {true, true};
        changed.badgeSize = 12;
        changed.badgeColor = 0x00FF00;
        changed.badgeOffsetX = -5;
        changed.badgeOffsetY = 7;
        changed.labelPosition = LabelPosition::Bottom;
        changed.labelFontSize = 15;
        changed.labelGap = 9;
        changed.showBarLabels = true;
        changed.percentFontSize = 12;
        changed.showPaceTicks = false;
        changed.paceTickStyle = PaceTickStyle::Dot;
        changed.paceTickColor = 0x00FF00;
        changed.accountMargin = 12;
        changed.rightMargin = 30;
        // Other pages' fields stay.
        changed.barMode = BarMode::Remaining;
        changed.pollMinutes = 30;
        changed.historyRetentionDays = 900;
        changed.yellowThreshold = 40;
        changed.percentTextVisibility = PercentTextVisibility::Always;
        changed.showDetailsOnHover = false;
        // The identity is the account's: the Layout reset keeps it.
        changed.accounts.push_back(Account());
        changed.accounts.back().tileIdentity = TileIdentity::Text;
        Settings expected = changed;
        const Settings defaults;
        expected.barLayout = defaults.barLayout;
        expected.barStyle = defaults.barStyle;
        expected.barLength = defaults.barLength;
        expected.barThickness = defaults.barThickness;
        expected.barGap = defaults.barGap;
        expected.ringSize = defaults.ringSize;
        expected.ringThickness = defaults.ringThickness;
        expected.headlineFontSize = defaults.headlineFontSize;
        expected.headlineBarLength = defaults.headlineBarLength;
        expected.headlineBarThickness = defaults.headlineBarThickness;
        expected.headlineCountdown = defaults.headlineCountdown;
        expected.sparklineWidth = defaults.sparklineWidth;
        expected.sparklineHeight = defaults.sparklineHeight;
        expected.sparklineExtra = defaults.sparklineExtra;
        expected.sparklineTextSize = defaults.sparklineTextSize;
        expected.sparklineElapsed = defaults.sparklineElapsed;
        expected.sparklineNowLine = defaults.sparklineNowLine;
        expected.sparklineNowCaret = defaults.sparklineNowCaret;
        expected.sparklineRing = defaults.sparklineRing;
        expected.sparklineOutline = defaults.sparklineOutline;
        expected.logoScale = defaults.logoScale;
        expected.logoBrands = defaults.logoBrands;
        expected.badgeSize = defaults.badgeSize;
        expected.badgeColor = defaults.badgeColor;
        expected.badgeOffsetX = defaults.badgeOffsetX;
        expected.badgeOffsetY = defaults.badgeOffsetY;
        expected.labelPosition = defaults.labelPosition;
        expected.labelFontSize = defaults.labelFontSize;
        expected.labelGap = defaults.labelGap;
        expected.showBarLabels = defaults.showBarLabels;
        expected.percentFontSize = defaults.percentFontSize;
        expected.showPaceTicks = defaults.showPaceTicks;
        expected.paceTickStyle = defaults.paceTickStyle;
        expected.paceTickColor = defaults.paceTickColor;
        expected.accountMargin = defaults.accountMargin;
        expected.rightMargin = defaults.rightMargin;
        Settings reset = changed;
        SettingsAssign(reset, SettingsField::ResetLayout, 0);
        CHECK(reset == expected);
        CHECK(reset.barLayout == BarLayout::Sparkline);
        CHECK(reset.sparklineHeight == 80 && reset.sparklineExtra == SparklineExtra::Amount && reset.sparklineTextSize == 8 &&
              reset.sparklineElapsed && reset.sparklineNowLine && !reset.sparklineNowCaret && !reset.sparklineRing &&
              reset.sparklineOutline);
        CHECK(SettingsGroupFields(SettingsField::ResetLayout).size() == 37);
        // Each group field is one the reset restores.
        for (SettingsField field : SettingsGroupFields(SettingsField::ResetLayout)) {
            CHECK(SettingsFieldValue(reset, field) == SettingsFieldValue(defaults, field));
            CHECK(SettingsFieldValue(changed, field) != SettingsFieldValue(defaults, field));
        }
        // The page reset drops only Layout drafts.
        TestStore store;
        store.settings = changed;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, SettingsField::BarLength, L"300");
        edits.SetText(0, kRetention, L"120");
        CHECK(edits.Reset(SettingsField::ResetLayout) == 0);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == kRetention);
        CHECK(store.settings.barLength == 100 && store.settings.historyRetentionDays == 900);
    });

    Run("projection: typed, dragged and stepped sizes stay in range; bad text keeps the last valid", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        constexpr SettingsField kLength = SettingsField::BarLength;
        edits.SetText(0, kLength, L"");
        CHECK(edits.Projection().barLength == 100);
        edits.SetText(0, kLength, L"250");
        CHECK(edits.Projection().barLength == 250);
        for (PCWSTR bad : {L"12a", L"-", L"99999999999999999999", L"2147483648", L"\uFF15"}) {
            edits.SetText(0, kLength, bad);
            CHECK(edits.Projection().barLength == 250);
        }
        edits.SetText(0, kLength, L"-5");
        CHECK(edits.Projection().barLength == 10);
        edits.SetText(0, kLength, L"2,500");
        CHECK(edits.Projection().barLength == 500);
        // Dragging: clamped values, valid even after invalid text.
        edits.SetText(0, kLength, L"12a");
        CHECK(edits.SetValue(0, kLength, 9999) && edits.Projection().barLength == 500);
        CHECK(edits.drafts[0].valid && !edits.drafts[0].text);
        edits.Step(0, SettingsField::RingThickness, 10);
        CHECK(edits.Projection().ringThickness == 6);
        edits.SetValue(0, SettingsField::LabelPosition, 99);
        CHECK(edits.Projection().labelPosition == LabelPosition::Bottom);
        CHECK(store.Applied().empty());
        // The preview draws any projection.
        CHECK(Finite(Strip(edits.Projection())));
        // The drag's release commits the dragged value.
        edits.SetValue(0, kLength, 180);
        CHECK(edits.RequestCommit(0, kLength) && edits.Process() == 0);
        CHECK(store.Applied() == std::vector<std::wstring>{Op(kLength, 180)});
    });

    Run("preview: ring tiles right-sized from the tile height; hover fills per column", [] {
        Settings s = StackedSettings();
        s.barLayout = BarLayout::SegmentedRing;
        SettingsPreviewStrip strip = Strip(s);
        // Four 3 + 32 + 3 DIP tiles: the box's 4 DIP padding takes all of the 3 DIP account margin,
        // so the boxes touch; tiles 40 tall on a 48 DIP taskbar.
        CHECK(strip.columns.size() == 4 && strip.height == 48);
        CHECK_NEAR(strip.width, 4 * 38);
        CHECK_NEAR(strip.columns[0].left, 0);
        CHECK_NEAR(strip.columns[1].left, 38);
        CHECK_NEAR(strip.columns[0].top, 4);
        CHECK_NEAR(strip.columns[0].bottom, 44);
        // A 10 DIP margin: the full 4 DIP padding, 6 DIPs outside the box.
        s.accountMargin = 10;
        strip = Strip(s);
        CHECK_NEAR(strip.width, 4 * (6 + 40 + 6));
        CHECK_NEAR(strip.columns[0].left, 6);
        CHECK_NEAR(strip.columns[1].left, 6 + 40 + 6 + 6);
        // Headline tiles pad 7 DIPs before and 8 after: 3 and 2 DIPs outside at 10, none at 3.
        s.barLayout = BarLayout::LogoHeadline;
        strip = Strip(s);
        CHECK_NEAR(strip.columns[0].left, 3);
        CHECK_NEAR(strip.columns[1].left - strip.columns[0].right, 2 + 3);
        CHECK_NEAR(strip.width, strip.columns[3].right + 2);
        s.accountMargin = 3;
        strip = Strip(s);
        CHECK_NEAR(strip.columns[0].left, 0);
        CHECK_NEAR(strip.columns[1].left, strip.columns[0].right);
        CHECK_NEAR(strip.width, strip.columns[3].right);
        s.barLayout = BarLayout::SegmentedRing;
        std::vector<int> hovers;
        for (const SettingsPreviewOp& op : strip.ops) {
            if (op.hover >= 0) hovers.push_back(op.hover);
        }
        CHECK((hovers == std::vector<int>{0, 1, 2, 3}));
        CHECK(OpsOfColor(strip, TilePaletteFor(false).hover).size() == 4);
        // A 32 DIP taskbar: 24 DIP tiles, rings capped to them.
        strip = Strip(s, 32);
        CHECK(strip.height == 32);
        CHECK_NEAR(strip.width, 4 * (24 + 6));
        CHECK_NEAR(strip.columns[0].top, 4);
        CHECK(Strip(s, 0).height == 48);
        // Gallery thumbnails: the two Anthropic accounts, no tray.
        strip = Strip(s, 48, true);
        CHECK(strip.columns.size() == 2 && strip.tray.empty() && strip.trayWidth == 0);
        CHECK_NEAR(strip.width, 2 * 38);
        CHECK(!Strip(s).tray.empty() && Strip(s).trayWidth > 100);
    });

    Run("preview: bars mirror the XAML columns; percent text follows its visibility", [] {
        Settings s = StackedSettings();
        SettingsPreviewStrip strip = Strip(s);
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
        // Sample bars: OpenAI 3, Claude 2 and 4, Antigravity 1.
        CHECK(tracks.size() == 10);
        for (const SettingsPreviewOp& track : tracks) {
            CHECK_NEAR(track.rect.right - track.rect.left, 100);
            CHECK_NEAR(track.rect.bottom - track.rect.top, 8);
        }
        auto percents = [](const SettingsPreviewStrip& strip) {
            std::vector<SettingsPreviewOp> ops;
            for (const SettingsPreviewOp& op : strip.ops) {
                if (op.kind == SettingsPreviewOp::Kind::Text && op.text.find(L'%') != std::wstring::npos) {
                    ops.push_back(op);
                }
            }
            return ops;
        };
        std::vector<SettingsPreviewOp> hover = percents(strip);
        CHECK(hover.size() == 10);
        CHECK(std::all_of(hover.begin(), hover.end(), [](const SettingsPreviewOp& op) { return op.hover >= 0; }));
        CHECK(hover.front().hover == 0 && hover.back().hover == 3);
        s.percentTextVisibility = PercentTextVisibility::Always;
        std::vector<SettingsPreviewOp> always = percents(Strip(s));
        CHECK(always.size() == 10 && always[0].hover == -1);
        s.percentTextVisibility = PercentTextVisibility::Never;
        CHECK(percents(Strip(s)).empty());
        // Left labels: the box's padding (the 3 DIP account margin) into it, the label, the label
        // gap, then the 100 DIP bars, the padding before its end.
        s = StackedSettings();
        strip = Strip(s);
        const SettingsPreviewOp label = FirstText(strip);
        CHECK(label.text == L"OAI");
        CHECK_NEAR(label.rect.left, strip.columns[0].left + 3);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left, label.rect.right + 3);
        CHECK_NEAR(strip.columns[0].right - strip.columns[0].left, 3 + label.rect.right - label.rect.left + 3 + 100 + 3);
        // Quota keys add a ceil(9 * 1.4) + 3 DIP column before each track.
        s.showBarLabels = true;
        strip = Strip(s);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left - FirstText(strip).rect.right, 3 + 16);
        // Hidden labels: just the bars in the box.
        s = StackedSettings();
        s.labelPosition = LabelPosition::Hidden;
        strip = Strip(s);
        CHECK_NEAR(strip.columns[0].right - strip.columns[0].left, 3 + 100 + 3);
        // Vertical bars stand 100 DIPs tall.
        s.barLayout = BarLayout::Vertical;
        strip = Strip(s);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.bottom - OpsOfColor(strip, 0x46808080)[0].rect.top, 100);
        CHECK_NEAR(strip.columns[0].bottom - strip.columns[0].top, 100);
    });

    Run("preview: bar columns take the tiles' box and hover fill, whatever their identity", [] {
        const uint32_t hover = TilePaletteFor(false).hover;
        // Every sample column (OAI's label, the Anthropic pair's badged logos, Go's logo), Stacked
        // and Vertical: the hover fill first, the box's 4 DIP rounded rectangle, only while
        // hovered. Its 7 DIP side padding comes out of the account margin, so contents sit twice
        // the margin apart: at 0 none, at 3 (the default) all of the margin with the boxes
        // touching, at 10 the full padding with 3 DIPs outside the box.
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical}) {
            for (int margin : {0, 3, 10}) {
                Settings s = StackedSettings();
                s.barLayout = layout;
                s.accountMargin = margin;
                SettingsPreviewStrip strip = Strip(s);
                std::vector<SettingsPreviewOp> fills = OpsOfColor(strip, hover);
                CHECK(fills.size() == 4 && strip.columns.size() == 4);
                CHECK(strip.ops[0].color == hover && strip.ops[0].hover == 0);
                for (size_t i = 0; i < fills.size(); i++) {
                    const D2D1_RECT_F& box = strip.columns[i];
                    CHECK(fills[i].hover == int(i) && fills[i].kind == SettingsPreviewOp::Kind::Rect);
                    CHECK_NEAR(fills[i].radius, 4);
                    CHECK_NEAR(fills[i].rect.left, box.left); CHECK_NEAR(fills[i].rect.right, box.right);
                    CHECK_NEAR(fills[i].rect.top, box.top); CHECK_NEAR(fills[i].rect.bottom, box.bottom);
                }
                const float padding = float(std::min(margin, 7)), outside = float(margin) - padding;
                CHECK_NEAR(strip.columns[0].left, outside);
                // Badged columns are fractional DIPs wide: positions sum in floats.
                for (size_t i = 1; i < 4; i++) CHECK(NearDip(strip.columns[i].left - strip.columns[i - 1].right, 2 * outside));
                CHECK(NearDip(strip.width, strip.columns[3].right + outside));
                // The bars alone: the padding around them; the first track that far into its box.
                s.labelPosition = LabelPosition::Hidden;
                strip = Strip(s);
                CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left, strip.columns[0].left + padding);
                if (layout == BarLayout::Stacked) {
                    for (const D2D1_RECT_F& box : strip.columns) CHECK_NEAR(box.right - box.left, padding + 100 + padding);
                }
            }
        }

        // Stacked: the tile height, 40 DIPs on a 48 DIP taskbar (CC 2's four bars just fill it:
        // 4 * 8 + 3 * 2 + 2 DIPs), the content centred across it.
        Settings s = StackedSettings();
        SettingsPreviewStrip strip = Strip(s);
        for (const D2D1_RECT_F& box : strip.columns) {
            CHECK_NEAR(box.top, 4); CHECK_NEAR(box.bottom, 44);
        }
        // OAI's three bars: 3 * 8 + 2 * 2 + 2 DIPs, 5 DIPs from either edge.
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
        CHECK_NEAR(tracks[0].rect.top - 1, 4 + 5); CHECK_NEAR(tracks[2].rect.bottom + 1, 44 - 5);
        // A 32 DIP taskbar: 24 DIP tiles. Go's one bar keeps that height; OAI's 30 DIPs and CC 2's
        // 40 grow their boxes, centred on the strip.
        strip = Strip(s, 32);
        CHECK_NEAR(strip.columns[3].top, 4); CHECK_NEAR(strip.columns[3].bottom, 28);
        CHECK_NEAR(strip.columns[0].top, 1); CHECK_NEAR(strip.columns[0].bottom, 31);
        CHECK_NEAR(strip.columns[2].top, -4); CHECK_NEAR(strip.columns[2].bottom, 36);
        // Vertical: 100 DIP bars grow it, still centred on the strip; 32 DIP ones sit in the
        // middle of the 40 DIP box.
        s.barLayout = BarLayout::Vertical;
        strip = Strip(s);
        CHECK_NEAR(strip.columns[0].top, -26); CHECK_NEAR(strip.columns[0].bottom, 74);
        s.barLength = 32;
        strip = Strip(s);
        CHECK_NEAR(strip.columns[0].top, 4); CHECK_NEAR(strip.columns[0].bottom, 44);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.top, 8);

        // The gallery's Anthropic pair: their logos badged "1" and "2" (each the number over eight
        // halo copies), the bars after the logo, the badge's overhang and the 3 DIP gap; no label.
        s = StackedSettings();
        strip = Strip(s, 48, true);
        std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        CHECK(logos.size() == 2 && OpsOfColor(strip, TilePaletteFor(false).badgeHalo).size() == 16 &&
              OpsOfColor(strip, hover).size() == 2);
        CHECK(TextOps(strip, L"1").size() == 9 && TextOps(strip, L"2").size() == 9 && TextOps(strip, L"CC 1").empty());
        CHECK_NEAR(logos[0].rect.left, strip.columns[0].left + 3);
        CHECK(NearDip(OpsOfColor(strip, 0x46808080)[0].rect.left, strip.columns[0].left + 3 + 15 + kBadgeRight + 3));
    });

    Run("preview: each identity form at every identity position in Stacked; the badge counts in its box", [] {
        // CC 1: its logo, badged "1" as CC 2 shares its provider; CC 2: its label (its short text
        // is for tiles); OAI: its logo alone. All fresh with two bars: 2 * 8 + 2 + 2 = 20 DIPs
        // tall, 100 wide.
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"CC 1", {true, true, false, false}),
                                               Account(L"anthropic", L"CC 2", {true, true, false, false}),
                                               Account(L"openai", L"OAI", {true, true, false, false})};
        accounts[1].tileIdentity = TileIdentity::Text;
        accounts[1].tileText = L"AB";
        accounts[2].tileIdentity = TileIdentity::Logo;
        std::vector<AccountData> data(3, Fresh({30, 40, -1, -1}));
        for (LabelPosition position : {LabelPosition::Left, LabelPosition::Right, LabelPosition::Top,
                                       LabelPosition::Bottom, LabelPosition::Hidden}) {
            // The box's full 7 DIP padding, 3 DIPs outside it.
            Settings s = StackedSettings();
            s.accountMargin = 10;
            s.labelPosition = position;
            SettingsPreviewStrip strip = StripOf(s, accounts, data);
            CHECK(strip.columns.size() == 3);
            const D2D1_RECT_F cc1 = strip.columns[0], cc2 = strip.columns[1], oai = strip.columns[2];
            std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
            std::vector<SettingsPreviewOp> badges = TextOps(strip, L"1");
            std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
            std::vector<SettingsPreviewOp> labels = TextOps(strip, L"CC 2");
            CHECK(tracks.size() == 6);
            // Bars never show the short text, nor a logo account's label.
            CHECK(TextOps(strip, L"AB").empty() && TextOps(strip, L"CC 1").empty() && TextOps(strip, L"OAI").empty());
            if (position == LabelPosition::Hidden) {
                // The bars alone, 7 DIPs into each box, centred on the 40 DIP tile height.
                CHECK(logos.empty() && badges.empty() && labels.empty());
                for (size_t c = 0; c < 3; c++) {
                    CHECK_NEAR(strip.columns[c].right - strip.columns[c].left, 7 + 100 + 7);
                    CHECK_NEAR(tracks[2 * c].rect.left, strip.columns[c].left + 7);
                    CHECK_NEAR(tracks[2 * c].rect.top, 15);
                }
                continue;
            }
            CHECK(logos.size() == 2 && logos[0].path == kClaudeLogoPath && logos[1].path == kOpenAiLogoPath);
            CHECK_NEAR(logos[0].rect.right - logos[0].rect.left, 15);
            CHECK_NEAR(logos[1].rect.bottom - logos[1].rect.top, 15);
            CHECK_NEAR(logos[0].opacity, 1);
            // The badge: "1" in bold at 13 DIPs in the taskbar's text color, its cap height centred on the logo's
            // lower-right corner, over eight copies in the halo color a DIP around it (Segoe UI
            // Variable's cap height over its line, as for the label).
            CHECK(badges.size() == 9);
            const SettingsPreviewOp& number = badges[8];
            CHECK(number.color == 0xFFFFFFFF && number.style.size == 13 && number.style.family == UiFamily::Small &&
                  number.style.weight == DWRITE_FONT_WEIGHT_BOLD && number.align == DWRITE_TEXT_ALIGNMENT_CENTER);
            CHECK(NearDip((number.rect.left + number.rect.right) / 2, logos[0].rect.right));
            CHECK(NearDip(number.rect.top + AboveCap(13) + CapHeight(13) / 2, logos[0].rect.bottom));
            for (size_t k = 0; k < 8; k++) {
                CHECK(badges[k].color == TilePaletteFor(false).badgeHalo && badges[k].style.size == 13);
                const double dx = badges[k].rect.left - number.rect.left, dy = badges[k].rect.top - number.rect.top;
                CHECK(NearDip(std::hypot(dx, dy), 1));
            }
            CHECK(NearDip(badges[0].rect.left - number.rect.left, 1) && NearDip(badges[2].rect.top - number.rect.top, 1));
            // The label in the label font at 0.8.
            CHECK(labels.size() == 1 && labels[0].style.size == 11 && labels[0].style.family == UiFamily::Taskbar);
            CHECK_NEAR(labels[0].opacity, 0.8);
            const SettingsPreviewOp& label = labels[0];
            const float labelWidth = label.rect.right - label.rect.left;
            switch (position) {
                case LabelPosition::Left:
                    // CC 1: its box (logo and badge, 19.9 x 26.1 DIPs: the badge's 5.55 below
                    // counted above too) 7 DIPs into the 40 DIP box, the logo centred across the
                    // 20 DIP bars (y 14 to 34) like OAI's, then the 3 DIP gap from the badge.
                    CHECK_NEAR(cc1.top, 4); CHECK_NEAR(cc1.bottom, 44);
                    CHECK_NEAR(logos[0].rect.left, 10); CHECK(NearDip(logos[0].rect.top, 16.5));
                    CHECK(NearDip(tracks[0].rect.left, logos[0].rect.right + kBadgeRight + 3));
                    CHECK(NearDip(tracks[0].rect.left, 32.9));
                    CHECK_NEAR(tracks[0].rect.top, 15);
                    CHECK_NEAR(cc1.right, tracks[0].rect.right + 7);
                    // CC 2's label as ever: 7 DIPs in, the gap, the bars.
                    CHECK_NEAR(label.rect.left, cc2.left + 7);
                    CHECK_NEAR(tracks[2].rect.left, label.rect.right + 3);
                    CHECK_NEAR(cc2.right, tracks[2].rect.right + 7);
                    // OAI: the 15 DIP logo centred, the gap from it.
                    CHECK_NEAR(logos[1].rect.left, oai.left + 7); CHECK_NEAR(logos[1].rect.top, 16.5);
                    CHECK_NEAR(tracks[4].rect.left, logos[1].rect.right + 3);
                    CHECK_NEAR(oai.right - oai.left, 7 + 15 + 3 + 100 + 7);
                    break;
                case LabelPosition::Right:
                    CHECK_NEAR(tracks[0].rect.left, 10);
                    CHECK_NEAR(logos[0].rect.left, tracks[0].rect.right + 3);
                    CHECK(NearDip(logos[0].rect.top, 16.5));
                    CHECK(NearDip(cc1.right, logos[0].rect.right + kBadgeRight + 7));
                    CHECK_NEAR(label.rect.left, tracks[2].rect.right + 3);
                    CHECK_NEAR(cc2.right, label.rect.right + 7);
                    CHECK_NEAR(logos[1].rect.left, tracks[4].rect.right + 3);
                    CHECK_NEAR(oai.right, logos[1].rect.right + 7);
                    break;
                case LabelPosition::Top: {
                    // The logo centred over the bars (the badge's overhang counted on both sides), a
                    // logo's box padded like the label's line: its space over the cap height above,
                    // under the baseline below, then the gap to the bars' panel (a DIP above the
                    // first track). CC 1: 20.55 + 4.17 + 2.76 + 3 + 20 DIPs grow its column, centred
                    // on the strip.
                    const double a = AboveCap(11), b = BelowBaseline(11);
                    const double badgeBottom = logos[0].rect.bottom + kBadgeBelow;
                    CHECK(NearDip(cc1.bottom - cc1.top, 15 + kBadgeBelow + a + b + 3 + 20) &&
                          NearDip(cc1.top + cc1.bottom, 48));
                    CHECK(NearDip(logos[0].rect.top, cc1.top + a));
                    CHECK(NearDip(logos[0].rect.left, 10 + (100 - 15) / 2.0));
                    CHECK(NearDip(tracks[0].rect.top - 1 - badgeBottom, 3 + b));
                    CHECK_NEAR(tracks[0].rect.left, 10); CHECK_NEAR(cc1.right, 10 + 100 + 7);
                    // CC 2's label as ever, its line box a + cap + b; the bars as far from its
                    // baseline as from CC 1's badge.
                    CHECK_NEAR(label.rect.left, cc2.left + 7 + (100 - labelWidth) / 2);
                    CHECK_NEAR(tracks[2].rect.top - 1, label.rect.bottom + 3);
                    CHECK(NearDip(label.rect.bottom - label.rect.top, a + CapHeight(11) + b));
                    CHECK(NearDip(tracks[2].rect.top - (label.rect.bottom - b), tracks[0].rect.top - badgeBottom));
                    // OAI: 15 + 4.17 + 2.76 + 3 + 20 DIPs.
                    CHECK(NearDip(oai.bottom - oai.top, 15 + a + b + 3 + 20));
                    CHECK(NearDip(logos[1].rect.top, oai.top + a));
                    CHECK_NEAR(logos[1].rect.left, oai.left + 7 + 42.5);
                    CHECK(NearDip(tracks[4].rect.top - 1 - logos[1].rect.bottom, 3 + b));
                    break;
                }
                case LabelPosition::Bottom: {
                    // Under the bars: the gap, the label line's space over its cap height, the logo's
                    // box; its space under the baseline at the column's bottom.
                    const double a = AboveCap(11), b = BelowBaseline(11);
                    CHECK(NearDip(tracks[0].rect.top - 1, cc1.top));
                    CHECK(NearDip(logos[0].rect.top - (tracks[1].rect.bottom + 1), 3 + a));
                    CHECK(NearDip(cc1.bottom - (logos[0].rect.bottom + kBadgeBelow), b));
                    CHECK(NearDip(logos[0].rect.left, 10 + (100 - 15) / 2.0));
                    // CC 2's label as ever; the bars as far from its cap height as from CC 1's logo.
                    CHECK_NEAR(label.rect.top, tracks[3].rect.bottom + 1 + 3);
                    CHECK(NearDip(label.rect.top + a - tracks[3].rect.bottom, logos[0].rect.top - tracks[1].rect.bottom));
                    CHECK(NearDip(logos[1].rect.top - (tracks[5].rect.bottom + 1), 3 + a));
                    CHECK(NearDip(oai.bottom - logos[1].rect.bottom, b));
                    CHECK_NEAR(logos[1].rect.left, oai.left + 7 + 42.5);
                    break;
                }
                case LabelPosition::Hidden:
                    break;
            }
        }

        // No gap: beside the bars they meet the badge's overhang, never under it, or a plain logo's
        // edge; over them the label line's space under its baseline stays.
        Settings s = StackedSettings();
        s.labelGap = 0;
        SettingsPreviewStrip strip = StripOf(s, accounts, data);
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
        std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        CHECK(NearDip(tracks[0].rect.left, logos[0].rect.right + kBadgeRight));
        CHECK_NEAR(tracks[4].rect.left, logos[1].rect.right);
        s.labelPosition = LabelPosition::Top;
        strip = StripOf(s, accounts, data);
        CHECK(NearDip(OpsOfColor(strip, 0x46808080)[0].rect.top - 1 -
                          (OpsOfKind(strip, SettingsPreviewOp::Kind::Logo)[0].rect.bottom + kBadgeBelow),
                      BelowBaseline(11)));
        // A wider gap moves every form's bars alike.
        s = StackedSettings();
        s.labelGap = 9;
        strip = StripOf(s, accounts, data);
        tracks = OpsOfColor(strip, 0x46808080);
        CHECK(NearDip(tracks[0].rect.left, OpsOfKind(strip, SettingsPreviewOp::Kind::Logo)[0].rect.right + kBadgeRight + 9));
        CHECK_NEAR(tracks[2].rect.left, TextOps(strip, L"CC 2")[0].rect.right + 9);
        // Tiles show the Label form's short text instead.
        s = StackedSettings();
        s.barLayout = BarLayout::LogoHeadline;
        strip = StripOf(s, accounts, data);
        CHECK(TextOps(strip, L"AB").size() == 1 && TextOps(strip, L"CC 2").empty());
    });

    Run("preview: the identity in Vertical: beside the upright bars, or centred over and under them", [] {
        // The same three accounts; 32 DIP bars: 2 * 8 + 2 = 18 DIPs wide.
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"CC 1", {true, true, false, false}),
                                               Account(L"anthropic", L"CC 2", {true, true, false, false}),
                                               Account(L"openai", L"OAI", {true, true, false, false})};
        accounts[1].tileIdentity = TileIdentity::Text;
        accounts[2].tileIdentity = TileIdentity::Logo;
        std::vector<AccountData> data(3, Fresh({30, 40, -1, -1}));
        Settings s = StackedSettings();
        s.accountMargin = 10;  // The box's full 7 DIP padding, 3 DIPs outside it.
        s.barLayout = BarLayout::Vertical;
        s.barLength = 32;
        auto center = [](const SettingsPreviewOp& a, const SettingsPreviewOp& b) {
            return (a.rect.left + b.rect.right) / 2;
        };

        // Left: CC 1's 19.9 x 26.1 DIP box, its logo centred on the bars, the gap, the bars, inside
        // the 40 DIP box (bars y 8 to 40).
        SettingsPreviewStrip strip = StripOf(s, accounts, data);
        std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
        SettingsPreviewOp label = TextOps(strip, L"CC 2")[0];
        D2D1_RECT_F cc1 = strip.columns[0];
        CHECK_NEAR(cc1.top, 4); CHECK_NEAR(cc1.bottom, 44);
        CHECK_NEAR(logos[0].rect.left, 10); CHECK(NearDip(logos[0].rect.top, 8 + (32 - 15) / 2.0));
        CHECK(NearDip(tracks[0].rect.left, logos[0].rect.right + kBadgeRight + 3)); CHECK_NEAR(tracks[0].rect.top, 8);
        CHECK_NEAR(tracks[1].rect.left, tracks[0].rect.right + 2);
        CHECK_NEAR(cc1.right, tracks[1].rect.right + 7);
        CHECK_NEAR(tracks[2].rect.left, label.rect.right + 3);
        CHECK_NEAR(tracks[4].rect.left, logos[1].rect.right + 3);

        // Top: centred over the bars and the bars under them, whichever is wider; a logo's box
        // padded like the label's line. CC 1: 20.55 + 4.17 + 2.76 + 3 + 32 DIPs grow its column,
        // centred on the strip; its 24.8 DIP wide box (the badge's 4.9 right counted left too) is
        // wider than the bars, which centre under the logo.
        const double a = AboveCap(11), b = BelowBaseline(11);
        s.labelPosition = LabelPosition::Top;
        strip = StripOf(s, accounts, data);
        logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        tracks = OpsOfColor(strip, 0x46808080);
        label = TextOps(strip, L"CC 2")[0];
        cc1 = strip.columns[0];
        CHECK(NearDip(cc1.bottom - cc1.top, 15 + kBadgeBelow + a + b + 3 + 32) && NearDip(cc1.top + cc1.bottom, 48));
        CHECK(NearDip(cc1.right - cc1.left, 7 + 15 + 2 * kBadgeRight + 7));
        CHECK(NearDip(logos[0].rect.left, 10 + kBadgeRight)); CHECK(NearDip(logos[0].rect.top, cc1.top + a));
        CHECK(NearDip(tracks[0].rect.top - (logos[0].rect.bottom + kBadgeBelow), 3 + b));
        CHECK(NearDip(tracks[0].rect.left, 10 + (15 + 2 * kBadgeRight - 18) / 2));
        CHECK(NearDip(center(tracks[0], tracks[1]), (logos[0].rect.left + logos[0].rect.right) / 2));
        CHECK_NEAR(center(tracks[2], tracks[3]), (label.rect.left + label.rect.right) / 2);
        CHECK_NEAR(tracks[2].rect.top, label.rect.bottom + 3);
        CHECK_NEAR(center(tracks[4], tracks[5]), (logos[1].rect.left + logos[1].rect.right) / 2);
        CHECK(NearDip(tracks[4].rect.top - logos[1].rect.bottom, 3 + b));
        CHECK_NEAR(strip.columns[2].right - strip.columns[2].left, 7 + 18 + 7);

        // Bottom: the gap and the line's space over the cap height under the bars, the space under
        // the baseline at the column's bottom.
        s.labelPosition = LabelPosition::Bottom;
        strip = StripOf(s, accounts, data);
        logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        tracks = OpsOfColor(strip, 0x46808080);
        CHECK(NearDip(logos[0].rect.top - tracks[0].rect.bottom, 3 + a));
        CHECK(NearDip(strip.columns[0].bottom - (logos[0].rect.bottom + kBadgeBelow), b));
        CHECK_NEAR(TextOps(strip, L"CC 2")[0].rect.top, tracks[2].rect.bottom + 3);
        CHECK(NearDip(logos[1].rect.top - tracks[4].rect.bottom, 3 + a));

        // Right and Hidden.
        s.labelPosition = LabelPosition::Right;
        strip = StripOf(s, accounts, data);
        logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        tracks = OpsOfColor(strip, 0x46808080);
        CHECK_NEAR(logos[0].rect.left, tracks[1].rect.right + 3);
        CHECK(NearDip(strip.columns[0].right, logos[0].rect.right + kBadgeRight + 7));
        s.labelPosition = LabelPosition::Hidden;
        strip = StripOf(s, accounts, data);
        CHECK(OpsOfKind(strip, SettingsPreviewOp::Kind::Logo).empty() && TextOps(strip, L"CC 2").empty());
        CHECK_NEAR(strip.columns[0].right - strip.columns[0].left, 7 + 18 + 7);
        CHECK_NEAR(strip.columns[0].top, 4); CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.top, 8);
    });

    Run("preview: the stale mark per identity form, in the identity's box; accounts without quotas", [] {
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"CC 1", {true, true, false, false}),
                                               Account(L"anthropic", L"CC 2", {true, true, false, false}),
                                               Account(L"openai", L"OAI", {true, true, false, false})};
        accounts[1].tileIdentity = TileIdentity::Text;
        accounts[2].tileIdentity = TileIdentity::Logo;
        std::vector<AccountData> data(3, Fresh({30, 40, -1, -1}));
        for (AccountData& d : data) {
            d.stale = true;
            d.error = L"timeout";
        }

        // Stale with an error: "!" on each logo's top-right corner, 4 DIPs past it, dimmed with it;
        // the label ends in "!" instead. A 9 DIP badge, whose overhang the mark's exceeds.
        Settings s = StackedSettings();
        s.badgeSize = 9;
        SettingsPreviewStrip strip = StripOf(s, accounts, data);
        std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        std::vector<SettingsPreviewOp> marks = TextOps(strip, L"!");
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
        CHECK(marks.size() == 2 && TextOps(strip, L"CC 2!").size() == 1 && TextOps(strip, L"CC 2").empty());
        CHECK_NEAR(TextOps(strip, L"CC 2!")[0].opacity, 0.45);
        for (size_t m = 0; m < 2; m++) {
            CHECK_NEAR(marks[m].rect.right, logos[m].rect.right + 4);
            CHECK_NEAR(marks[m].opacity, 0.45); CHECK_NEAR(logos[m].opacity, 0.45);
            CHECK(marks[m].style.size == 10 && marks[m].style.weight == DWRITE_FONT_WEIGHT_BOLD);
        }
        for (const SettingsPreviewOp& badge : TextOps(strip, L"1")) CHECK_NEAR(badge.opacity, 0.45);
        // The mark counts in the box while shown: 4 DIPs right (past the badge's 3.7), and 3
        // above, counted below too, so neither logo moves. CC 1: 19 x 23.3 DIPs (the badge's 4.15
        // below and above cover the mark), its logo centred on its bars (y 14 to 34).
        CHECK(NearDip(logos[0].rect.top, 16.5));
        CHECK_NEAR(tracks[0].rect.left, logos[0].rect.right + 4 + 3); CHECK_NEAR(tracks[0].rect.top, 15);
        // OAI: 19 x 21, centred on its bars too: the gap from the mark.
        CHECK_NEAR(logos[1].rect.top, 16.5);
        CHECK_NEAR(tracks[4].rect.left, marks[1].rect.right + 3);
        // Bottom: the gap and the label line's space over its cap height, then the mark's 3 DIPs,
        // under the bars.
        s.labelPosition = LabelPosition::Bottom;
        strip = StripOf(s, accounts, data);
        tracks = OpsOfColor(strip, 0x46808080);
        CHECK(NearDip(OpsOfKind(strip, SettingsPreviewOp::Kind::Logo)[1].rect.top - (tracks[5].rect.bottom + 1),
                      3 + AboveCap(11) + 3));
        // Top: the mark inside the box, above the logo, under the line's space over the cap height.
        s.labelPosition = LabelPosition::Top;
        strip = StripOf(s, accounts, data);
        CHECK(NearDip(OpsOfKind(strip, SettingsPreviewOp::Kind::Logo)[0].rect.top - strip.columns[0].top,
                      AboveCap(11) + 3));
        // Marks off: none; the boxes as fresh ones'.
        s = StackedSettings();
        s.showStaleWarning = false;
        strip = StripOf(s, accounts, data);
        CHECK(TextOps(strip, L"!").empty() && TextOps(strip, L"CC 2").size() == 1);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[4].rect.left, OpsOfKind(strip, SettingsPreviewOp::Kind::Logo)[1].rect.right + 3);

        // No quota reported: no bars and no gap; the identity alone in the 40 DIP box, its box
        // included (above or below the bars' place, padded like the label's line). Hidden: the
        // 8 DIP context-menu target.
        data = {Fresh(), Fresh(), Fresh()};
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical}) {
            for (LabelPosition position : {LabelPosition::Left, LabelPosition::Right, LabelPosition::Top,
                                           LabelPosition::Bottom}) {
                s = StackedSettings();
                s.accountMargin = 10;  // The box's full 7 DIP padding, 3 DIPs outside it.
                s.barLayout = layout;
                s.labelPosition = position;
                strip = StripOf(s, accounts, data);
                CHECK(OpsOfColor(strip, 0x46808080).empty());
                logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
                const SettingsPreviewOp label = TextOps(strip, L"CC 2")[0];
                // CC 1's badge counts on both sides across where the bars would be: horizontally
                // above or below them, vertically beside them.
                const bool across = position == LabelPosition::Top || position == LabelPosition::Bottom;
                const double a = across ? AboveCap(11) : 0, b = across ? BelowBaseline(11) : 0;
                CHECK(NearDip(strip.columns[0].right - strip.columns[0].left, 7 + 15 + (across ? 2 : 1) * kBadgeRight + 7));
                CHECK_NEAR(strip.columns[1].right - strip.columns[1].left, 7 + label.rect.right - label.rect.left + 7);
                CHECK_NEAR(strip.columns[2].right - strip.columns[2].left, 7 + 15 + 7);
                CHECK(NearDip(logos[0].rect.left, strip.columns[0].left + 7 + (across ? kBadgeRight : 0)));
                CHECK(NearDip(logos[0].rect.top, across ? 4 + (40 - 15 - kBadgeBelow - a - b) / 2 + a : 4 + (40 - 15) / 2.0));
                CHECK(NearDip(logos[1].rect.top, 4 + (40 - 15 - a - b) / 2 + a));
                for (const D2D1_RECT_F& box : strip.columns) {
                    CHECK_NEAR(box.top, 4); CHECK_NEAR(box.bottom, 44);
                }
            }
            s.labelPosition = LabelPosition::Hidden;
            strip = StripOf(s, accounts, data);
            for (const D2D1_RECT_F& box : strip.columns) CHECK_NEAR(box.right - box.left, 7 + 8 + 7);
        }
    });

    Run("preview: above or below the bars a logo keeps a label's distance, at 11 and 15 DIP labels", [] {
        // CC 1: its logo badged "1"; CC 2: its label; OAI: its logo alone; two bars each (the bars'
        // panel a DIP past the tracks). Fresh, then stale with an error (the mark 3 DIPs over the
        // logo, the label's "!").
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"CC 1", {true, true, false, false}),
                                               Account(L"anthropic", L"CC 2", {true, true, false, false}),
                                               Account(L"openai", L"OAI", {true, true, false, false})};
        accounts[1].tileIdentity = TileIdentity::Text;
        accounts[2].tileIdentity = TileIdentity::Logo;
        for (int size : {11, 15}) {
            const double a = AboveCap(size), b = BelowBaseline(size);
            for (bool stale : {false, true}) {
                std::vector<AccountData> data(3, Fresh({30, 40, -1, -1}));
                for (AccountData& d : data) {
                    d.stale = stale;
                    if (stale) d.error = L"timeout";
                }
                const double mark = stale ? 3 : 0;
                for (LabelPosition position : {LabelPosition::Top, LabelPosition::Bottom}) {
                    Settings s = StackedSettings();
                    s.labelFontSize = size;
                    s.labelPosition = position;
                    SettingsPreviewStrip strip = StripOf(s, accounts, data);
                    std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
                    std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
                    const double badgeBottom = logos[0].rect.bottom + kBadgeBelow;
                    const SettingsPreviewOp label = TextOps(strip, stale ? L"CC 2!" : L"CC 2")[0];
                    CHECK(logos.size() == 2 && tracks.size() == 6 && TextOps(strip, L"!").size() == (stale ? 2u : 0u));
                    // The label's line box: the space over the cap height, the cap height, the space
                    // under the baseline.
                    CHECK(label.style.size == float(size));
                    CHECK(NearDip(label.rect.bottom - label.rect.top, a + CapHeight(size) + b));
                    // Each column outgrows the tile height: its box is its content.
                    if (position == LabelPosition::Top) {
                        // The bars' panel starts gap + b under the visible bottom: the badge's, the
                        // logo's, the label's baseline. Over the visible top (the mark, else the
                        // logo) the space over the cap height.
                        CHECK(NearDip(tracks[0].rect.top - 1 - badgeBottom, 3 + b));
                        CHECK(NearDip(tracks[4].rect.top - 1 - logos[1].rect.bottom, 3 + b));
                        CHECK(NearDip(tracks[2].rect.top - 1 - (label.rect.bottom - b), 3 + b));
                        CHECK(NearDip(logos[0].rect.top - mark - strip.columns[0].top, a));
                        CHECK(NearDip(logos[1].rect.top - mark - strip.columns[2].top, a));
                    } else {
                        // The visible top (the mark, else the logo; the label's cap height) starts
                        // gap + a under the bars' panel; the space under the baseline closes the box.
                        CHECK(NearDip(logos[0].rect.top - mark - (tracks[1].rect.bottom + 1), 3 + a));
                        CHECK(NearDip(logos[1].rect.top - mark - (tracks[5].rect.bottom + 1), 3 + a));
                        CHECK(NearDip(label.rect.top + a - (tracks[3].rect.bottom + 1), 3 + a));
                        CHECK(NearDip(strip.columns[0].bottom - badgeBottom, b));
                        CHECK(NearDip(strip.columns[2].bottom - logos[1].rect.bottom, b));
                    }
                }
            }
        }
        // Beside the bars nothing changes with the label size: CC 1's logo stays centred on its
        // bars.
        Settings s = StackedSettings();
        s.labelFontSize = 15;
        std::vector<AccountData> data(3, Fresh({30, 40, -1, -1}));
        SettingsPreviewStrip strip = StripOf(s, accounts, data);
        const SettingsPreviewOp logo = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo)[0];
        CHECK(NearDip(logo.rect.top, 16.5));
        CHECK(NearDip(OpsOfColor(strip, 0x46808080)[0].rect.left, logo.rect.right + kBadgeRight + 3));
    });

    Run("preview: the logo size in every style; the badge's size and offsets", [] {
        // CC 1: its logo badged "1"; CC 2: its label; OAI: its logo alone; two bars each.
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"CC 1", {true, true, false, false}),
                                               Account(L"anthropic", L"CC 2", {true, true, false, false}),
                                               Account(L"openai", L"OAI", {true, true, false, false})};
        accounts[1].tileIdentity = TileIdentity::Text;
        accounts[2].tileIdentity = TileIdentity::Logo;
        std::vector<AccountData> data(3, Fresh({30, 40, -1, -1}));
        auto logos = [](const SettingsPreviewStrip& strip) { return OpsOfKind(strip, SettingsPreviewOp::Kind::Logo); };
        // The number's centre: across, and down its cap height.
        auto numberX = [](const SettingsPreviewOp& op) { return (op.rect.left + op.rect.right) / 2; };
        auto numberY = [](const SettingsPreviewOp& op) { return op.rect.top + AboveCap(op.style.size) + CapHeight(op.style.size) / 2; };

        // Beside bars: 15 DIP logos at 100%, 7.5 at 50%, 30 at 200%; the badge stays on the
        // corner, the bars after its overhang.
        for (auto [percent, side] : {std::pair{50, 7.5}, std::pair{100, 15.0}, std::pair{200, 30.0}}) {
            Settings s = StackedSettings();
            s.logoScale = percent;
            SettingsPreviewStrip strip = StripOf(s, accounts, data);
            std::vector<SettingsPreviewOp> logo = logos(strip);
            std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
            CHECK(NearDip(logo[0].rect.right - logo[0].rect.left, side) && NearDip(logo[1].rect.bottom - logo[1].rect.top, side));
            const SettingsPreviewOp number = TextOps(strip, L"1")[8];
            CHECK(NearDip(numberX(number), logo[0].rect.right) && NearDip(numberY(number), logo[0].rect.bottom));
            CHECK(NearDip(tracks[0].rect.left, logo[0].rect.right + kBadgeRight + 3));
            CHECK(NearDip(tracks[4].rect.left, logo[1].rect.right + 3));
        }

        // The badge size: the number at 14 DIPs, its box 0.6 * 14 + 2 = 10.4 DIPs wide.
        Settings s = StackedSettings();
        s.badgeSize = 14;
        SettingsPreviewStrip strip = StripOf(s, accounts, data);
        std::vector<SettingsPreviewOp> logo = logos(strip);
        SettingsPreviewOp number = TextOps(strip, L"1")[8];
        CHECK(number.style.size == 14 && TextOps(strip, L"1")[0].style.size == 14);
        CHECK(NearDip(numberX(number), logo[0].rect.right));
        CHECK(NearDip(OpsOfColor(strip, 0x46808080)[0].rect.left, logo[0].rect.right + 5.2 + 3));

        // The badge color: white by default, the taskbar's text (dark on a light taskbar) outlined
        // in the taskbar's color; light blue stays on either taskbar, outlined dark; a dark color
        // is outlined light. The logo keeps the taskbar's text color.
        struct Case {
            std::optional<COLORREF> color;
            bool light;
            uint32_t text, number, halo;
        };
        for (const Case& c : {Case{std::nullopt, false, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF1C1C1C},
                              Case{std::nullopt, true, 0xFF1A1A1A, 0xFF1A1A1A, 0xFFF3F3F3},
                              Case{RGB(0x4C, 0xC2, 0xFF), false, 0xFFFFFFFF, 0xFF4CC2FF, 0xFF1C1C1C},
                              Case{RGB(0x4C, 0xC2, 0xFF), true, 0xFF1A1A1A, 0xFF4CC2FF, 0xFF1C1C1C},
                              Case{RGB(0x1C, 0x1C, 0x1C), false, 0xFFFFFFFF, 0xFF1C1C1C, 0xFFF3F3F3}}) {
            s = StackedSettings();
            if (c.color) s.badgeColor = *c.color;
            strip = SettingsPreviewLayout(TestGraphics(), s, 48, 1, kNow, false, c.light, c.text, &accounts, &data);
            std::vector<SettingsPreviewOp> badge = TextOps(strip, L"1");
            CHECK(badge.size() == 9 && badge[8].color == c.number && logos(strip)[0].color == c.text);
            for (size_t k = 0; k < 8; k++) CHECK(badge[k].color == c.halo);
        }

        // 6 left and 6 up: the box (4.1 to 13.9 across, 3.45 to 14.55 down) stays within the logo,
        // so the logo's box is a plain logo's.
        s = StackedSettings();
        s.badgeOffsetX = -6;
        s.badgeOffsetY = -6;
        strip = StripOf(s, accounts, data);
        logo = logos(strip);
        number = TextOps(strip, L"1")[8];
        CHECK(NearDip(numberX(number), logo[0].rect.right - 6) && NearDip(numberY(number), logo[0].rect.bottom - 6));
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left, logo[0].rect.right + 3);
        CHECK_NEAR(logo[0].rect.top, 16.5);
        // 20 left: 9.9 DIPs past the logo's left edge, which the box takes.
        s.badgeOffsetX = -20;
        strip = StripOf(s, accounts, data);
        CHECK(NearDip(logos(strip)[0].rect.left, strip.columns[0].left + 3 + 20 + kBadgeRight - 15));
        // Lowered or raised, the badge leaves the logo centred on its bars (y 14 to 34), the bars
        // where they were.
        for (int y : {-20, -10, 10, 20}) {
            s = StackedSettings();
            s.badgeOffsetY = y;
            strip = StripOf(s, accounts, data);
            logo = logos(strip);
            number = TextOps(strip, L"1")[8];
            CHECK(NearDip(logo[0].rect.top, 16.5) && NearDip(numberY(number), logo[0].rect.bottom + y));
            CHECK(NearDip(OpsOfColor(strip, 0x46808080)[0].rect.top, 15));
        }
        // Above the bars, moved either way, it leaves the logo centred over them.
        for (int x : {-20, 20}) {
            s = StackedSettings();
            s.labelPosition = LabelPosition::Top;
            s.badgeOffsetX = x;
            strip = StripOf(s, accounts, data);
            logo = logos(strip);
            const SettingsPreviewOp track = OpsOfColor(strip, 0x46808080)[0];
            CHECK(NearDip((logo[0].rect.left + logo[0].rect.right) / 2, (track.rect.left + track.rect.right) / 2));
        }

        // Headline tiles: 16 DIP logos at 100%, 24 at 150%, badged on the corner.
        s = StackedSettings();
        s.barLayout = BarLayout::LogoHeadline;
        s.logoScale = 150;
        strip = StripOf(s, accounts, data);
        logo = logos(strip);
        number = TextOps(strip, L"1")[8];
        CHECK(NearDip(logo[0].rect.right - logo[0].rect.left, 24) && NearDip(logo[1].rect.right - logo[1].rect.left, 24));
        CHECK(NearDip(numberX(number), logo[0].rect.right) && NearDip(numberY(number), logo[0].rect.bottom));

        // Rings: 0.44 of the 32 DIP ring at 100%, scaled, centred; at most 1.2 DIPs inside the
        // 12.5 DIP hole. The badge on the diagonal, its box's far corner half a DIP inside the hole.
        s.barLayout = BarLayout::SegmentedRing;
        strip = StripOf(s, accounts, data);
        logo = logos(strip);
        number = TextOps(strip, L"1")[8];
        CHECK(NearDip(logo[0].rect.right - logo[0].rect.left, 0.44 * 32 * 1.5));
        const double cx = (logo[0].rect.left + logo[0].rect.right) / 2, cy = (logo[0].rect.top + logo[0].rect.bottom) / 2;
        CHECK(NearDip(cx, strip.columns[0].left + 3 + 16));
        const double along = numberX(number) - cx;
        CHECK(along > 0 && NearDip(numberY(number) - cy, along));
        CHECK(NearDip(std::hypot(along + kBadgeRight, along + kBadgeBelow), 12));
        // Offsets move it from there.
        s.badgeOffsetX = 2;
        s.badgeOffsetY = -3;
        number = TextOps(StripOf(s, accounts, data), L"1")[8];
        CHECK(NearDip(numberX(number) - cx, along + 2) && NearDip(numberY(number) - cy, along - 3));
        s = StackedSettings();
        s.barLayout = BarLayout::SegmentedRing;
        s.logoScale = 200;
        logo = logos(StripOf(s, accounts, data));
        CHECK(NearDip(logo[0].rect.right - logo[0].rect.left, 22.6));
    });

    Run("preview: logos take their brand colors only when chosen, in the same boxes", [] {
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"CC", {true, true, false, false}),
                                               Account(L"openai", L"OAI", {true, true, false, false}),
                                               Account(L"antigravity", L"Go", {true, true, false, false})};
        std::vector<AccountData> data(3, Fresh({30, 40, -1, -1}));
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::SegmentedRing, BarLayout::LogoHeadline}) {
            Settings s = StackedSettings();
            s.barLayout = layout;
            const std::vector<SettingsPreviewOp> plain = OpsOfKind(
                SettingsPreviewLayout(TestGraphics(), s, 48, 1, kNow, false, true, 0xFF1A1A1A, &accounts, &data),
                SettingsPreviewOp::Kind::Logo);
            CHECK(plain.size() == 3);
            for (const SettingsPreviewOp& op : plain) CHECK(!op.brand && op.color == 0xFF1A1A1A);
            // Claude and Gemini only: OpenAI's mark has none. The text color stays the high
            // contrast fallback.
            s.logoBrands = {true, true};
            const std::vector<SettingsPreviewOp> brand = OpsOfKind(
                SettingsPreviewLayout(TestGraphics(), s, 48, 1, kNow, false, true, 0xFF1A1A1A, &accounts, &data),
                SettingsPreviewOp::Kind::Logo);
            CHECK(brand.size() == 3 && brand[0].brand == &kClaudeLogoBrand && !brand[1].brand &&
                  brand[2].brand == &kGeminiLogoBrand);
            for (size_t i = 0; i < brand.size(); i++) {
                CHECK(brand[i].color == 0xFF1A1A1A && brand[i].path == plain[i].path);
                CHECK(NearDip(brand[i].rect.left, plain[i].rect.left) && NearDip(brand[i].rect.top, plain[i].rect.top) &&
                      NearDip(brand[i].rect.right, plain[i].rect.right));
            }
        }
    });

    Run("preview: pace marks per layout; none on headline tiles", [] {
        const uint32_t pace = 0xFF000000 | 0xDEDEDE;
        Settings s = StackedSettings();
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical, BarLayout::SegmentedRing,
                                 BarLayout::ConcentricRings}) {
            s.barLayout = layout;
            CHECK(!OpsOfColor(Strip(s), pace).empty());
        }
        s.barLayout = BarLayout::LogoHeadline;
        SettingsPreviewStrip strip = Strip(s);
        CHECK(OpsOfColor(strip, pace).empty());
        CHECK(std::none_of(strip.ops.begin(), strip.ops.end(), [](const SettingsPreviewOp& op) {
            return op.kind == SettingsPreviewOp::Kind::Shape || op.kind == SettingsPreviewOp::Kind::Polygon;
        }));
        CHECK(OpsOfColor(strip, 0xC0000000).empty());
        s.barLayout = BarLayout::Stacked;
        s.showPaceTicks = false;
        CHECK(OpsOfColor(Strip(s), pace).empty());
        // Carets are two polygons per mark; ring ticks a core shape after each halo shape.
        s.showPaceTicks = true;
        strip = Strip(s);
        CHECK(std::count_if(strip.ops.begin(), strip.ops.end(), [](const SettingsPreviewOp& op) {
                  return op.kind == SettingsPreviewOp::Kind::Polygon;
              }) == 2 * int(OpsOfColor(strip, pace).size()));
        for (BarLayout layout : {BarLayout::SegmentedRing, BarLayout::ConcentricRings}) {
            s.barLayout = layout;
            strip = Strip(s);
            for (size_t i = 0; i < strip.ops.size(); i++) {
                if (strip.ops[i].color != pace) continue;
                CHECK(strip.ops[i].kind == SettingsPreviewOp::Kind::Shape && i > 0);
                CHECK(strip.ops[i - 1].kind == SettingsPreviewOp::Kind::Shape && strip.ops[i - 1].color == 0xC0000000);
            }
        }
    });

    Run("preview: full track and thin rail bars, shaded fills cut flat, ticks past the track", [] {
        auto fills = [](const SettingsPreviewStrip& strip) {
            std::vector<SettingsPreviewOp> ops;
            for (const SettingsPreviewOp& op : strip.ops) {
                if (op.corners) ops.push_back(op);
            }
            return ops;
        };
        auto polygons = [](const SettingsPreviewStrip& strip) {
            std::vector<SettingsPreviewOp> ops;
            for (const SettingsPreviewOp& op : strip.ops) {
                if (op.kind == SettingsPreviewOp::Kind::Polygon) ops.push_back(op);
            }
            return ops;
        };
        // The span of a polygon across a horizontal bar: its top and height.
        auto span = [](const SettingsPreviewOp& op) {
            float top = 1e9f, bottom = -1e9f;
            for (D2D1_POINT_2F p : op.points) {
                top = std::min(top, p.y);
                bottom = std::max(bottom, p.y);
            }
            return std::pair{top, bottom - top};
        };
        using Corners = std::array<float, 4>;
        Settings s = StackedSettings();
        SettingsPreviewStrip strip = Strip(s);
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
        std::vector<SettingsPreviewOp> shaded = fills(strip);
        CHECK(tracks.size() == 10 && shaded.size() == 10 && OpsOfColor(strip, 0x66808080).empty());
        // Track corners: a quarter of the 8 DIP thickness.
        CHECK_NEAR(tracks[0].radius, 2);
        // OAI's 5-hour quota: 25% green under its highlight, rounded at the start, cut flat at the end.
        const SettingsPreviewOp& oai = shaded[0];
        CHECK(oai.color == 0xFF43A047 && oai.shade == UsageHighlight(0xFF43A047) && !oai.shadeLeft);
        CHECK_NEAR(oai.rect.left, tracks[0].rect.left);
        CHECK_NEAR(oai.rect.right - oai.rect.left, 25);
        CHECK_NEAR(oai.rect.bottom - oai.rect.top, 8);
        CHECK((*oai.corners == Corners{2, 0, 0, 2}));
        // CC 2's weekly quota, past 100%: red, reaching the track's rounded end.
        auto full = std::find_if(shaded.begin(), shaded.end(), [](const SettingsPreviewOp& op) {
            return op.rect.right - op.rect.left > 99;
        });
        CHECK(full != shaded.end() && full->color == 0xFFE53935 && (*full->corners == Corners{2, 2, 2, 2}));
        // Antigravity is stale: grey, shaded too.
        CHECK(shaded.back().color == 0xFF9E9E9E && shaded.back().shade == UsageHighlight(0xFF9E9E9E));
        // OAI's caret: the 8 DIP bar and a DIP past each side (2 DIP gap), halo then core.
        std::vector<SettingsPreviewOp> carets = polygons(strip);
        CHECK(carets[0].color == 0xC0000000 && carets[1].color == 0xFFDEDEDE);
        auto [caretTop, caretHeight] = span(carets[0]);
        CHECK_NEAR(caretTop, tracks[0].rect.top - 1);
        CHECK_NEAR(caretHeight, 10);
        CHECK_NEAR(span(carets[1]).second, 10);

        // No gap, no overhang; a 4 DIP gap at 150% leaves two device pixels.
        s.barGap = 0;
        CHECK_NEAR(span(polygons(Strip(s))[0]).second, 8);
        s.barGap = 4;
        CHECK(std::abs(span(polygons(Strip(s, 48, false, 1.5f))[0]).second - (8 + 2 * 2 / 1.5)) < 1e-4);  // Floats
        // A notch hangs from the overhang's outer edge: its core 1 + 5 DIPs deep and flush with
        // that edge, its halo a pixel deeper.
        s = StackedSettings();
        s.paceTickStyle = PaceTickStyle::Notch;
        strip = Strip(s);
        std::vector<SettingsPreviewOp> notches = OpsOfColor(strip, 0xC0000000);
        std::vector<SettingsPreviewOp> cores = OpsOfColor(strip, 0xFFDEDEDE);
        tracks = OpsOfColor(strip, 0x46808080);
        CHECK(!notches.empty() && cores.size() == notches.size());
        CHECK_NEAR(notches[0].rect.top, tracks[0].rect.top - 1);
        CHECK_NEAR(notches[0].rect.bottom - notches[0].rect.top, 7);
        CHECK_NEAR(cores[0].rect.top, notches[0].rect.top);
        CHECK_NEAR(cores[0].rect.bottom - cores[0].rect.top, 6);
        CHECK_NEAR(cores[0].rect.left, notches[0].rect.left + 1);
        CHECK_NEAR(cores[0].rect.right - cores[0].rect.left, 2);
        // A line's halo spans the bar and a DIP past each side, square; the core a pixel short of
        // it at both ends.
        s.paceTickStyle = PaceTickStyle::Line;
        strip = Strip(s);
        std::vector<SettingsPreviewOp> halos = OpsOfColor(strip, 0xC0000000);
        cores = OpsOfColor(strip, 0xFFDEDEDE);
        tracks = OpsOfColor(strip, 0x46808080);
        CHECK_NEAR(halos[0].rect.top, tracks[0].rect.top - 1);
        CHECK_NEAR(halos[0].rect.bottom - halos[0].rect.top, 10);
        CHECK_NEAR(halos[0].radius, 0);
        CHECK_NEAR(cores[0].rect.top, halos[0].rect.top + 1);
        CHECK_NEAR(cores[0].rect.bottom, halos[0].rect.bottom - 1);
        CHECK_NEAR(cores[0].rect.left, halos[0].rect.left + 1);
        CHECK_NEAR(cores[0].radius, 0);
        // Vertical bars: a notch hangs from the left edge, its core flush with it.
        s.paceTickStyle = PaceTickStyle::Notch;
        s.barLayout = BarLayout::Vertical;
        strip = Strip(s);
        notches = OpsOfColor(strip, 0xC0000000);
        cores = OpsOfColor(strip, 0xFFDEDEDE);
        tracks = OpsOfColor(strip, 0x46808080);
        CHECK_NEAR(notches[0].rect.left, tracks[0].rect.left - 1);
        CHECK_NEAR(notches[0].rect.right - notches[0].rect.left, 7);
        CHECK_NEAR(cores[0].rect.left, notches[0].rect.left);
        CHECK_NEAR(cores[0].rect.right - cores[0].rect.left, 6);
        CHECK_NEAR(cores[0].rect.top, notches[0].rect.top + 1);

        // Vertical bars fill upward: start corners at the bottom, the highlight on the left.
        s = StackedSettings();
        s.barLayout = BarLayout::Vertical;
        strip = Strip(s);
        shaded = fills(strip);
        CHECK(shaded[0].shadeLeft && (*shaded[0].corners == Corners{0, 0, 2, 2}));
        CHECK_NEAR(shaded[0].rect.bottom - shaded[0].rect.top, 25);
        CHECK_NEAR(shaded[0].rect.bottom, OpsOfColor(strip, 0x46808080)[0].rect.bottom);

        // Thin rail: a 2 DIP rail centred across the 8 DIP bar, fills rounded at both ends, ticks
        // within the bar.
        s = StackedSettings();
        s.barStyle = BarStyle::Rail;
        strip = Strip(s);
        std::vector<SettingsPreviewOp> rails = OpsOfColor(strip, 0x66808080);
        shaded = fills(strip);
        CHECK(rails.size() == 10 && OpsOfColor(strip, 0x46808080).empty() && shaded.size() == 10);
        CHECK_NEAR(rails[0].rect.right - rails[0].rect.left, 100);
        CHECK_NEAR(rails[0].rect.bottom - rails[0].rect.top, 2);
        CHECK_NEAR(rails[0].rect.top, shaded[0].rect.top + 3);
        CHECK_NEAR(rails[0].radius, 1);
        CHECK((*shaded[0].corners == Corners{2, 2, 2, 2}));
        auto [railTop, railHeight] = span(polygons(strip)[0]);
        CHECK_NEAR(railTop, shaded[0].rect.top);
        CHECK_NEAR(railHeight, 8);
        // A line over the rail spans the bar: 1 px halo corners, 0.5 px core corners.
        s.paceTickStyle = PaceTickStyle::Line;
        strip = Strip(s);
        halos = OpsOfColor(strip, 0xC0000000);
        cores = OpsOfColor(strip, 0xFFDEDEDE);
        CHECK_NEAR(halos[0].rect.top, fills(strip)[0].rect.top);
        CHECK_NEAR(halos[0].rect.bottom - halos[0].rect.top, 8);
        CHECK_NEAR(halos[0].radius, 1);
        CHECK_NEAR(cores[0].rect.bottom - cores[0].rect.top, 6);
        CHECK_NEAR(cores[0].radius, 0.5);
        s.paceTickStyle = PaceTickStyle::Caret;
        // Vertical rails stand centred across the bar.
        s.barLayout = BarLayout::Vertical;
        strip = Strip(s);
        rails = OpsOfColor(strip, 0x66808080);
        CHECK_NEAR(rails[0].rect.right - rails[0].rect.left, 2);
        CHECK_NEAR(rails[0].rect.left, fills(strip)[0].rect.left + 3);
    });

    Run("preview: ring tiles fill the model's shapes; headline segments cut flat and shaded", [] {
        auto roughly = [](double actual, double expected) { return std::abs(actual - expected) < 1e-4; };  // Floats
        auto shaded = [](const SettingsPreviewStrip& strip) {
            std::vector<SettingsPreviewOp> ops;
            for (const SettingsPreviewOp& op : strip.ops) {
                if (op.corners) ops.push_back(op);
            }
            return ops;
        };
        using Corners = std::array<float, 4>;
        const uint32_t track = TilePaletteFor(false).track;
        Settings s = StackedSettings();
        s.barLayout = BarLayout::SegmentedRing;
        SettingsPreviewStrip strip = Strip(s);
        // Ten quotas on four tiles: a track shape each, then a flat fill shape per quota in use.
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, track);
        CHECK(tracks.size() == 10 && shaded(strip).empty());
        CHECK(std::all_of(tracks.begin(), tracks.end(), [](const SettingsPreviewOp& op) {
            return op.kind == SettingsPreviewOp::Kind::Shape;
        }));
        // OAI's ring area: the box's padding (the 3 DIP account margin) into its column, centred on
        // the 40 DIP tile; the model's shapes.
        std::vector<AccountConfig> accounts;
        std::vector<AccountData> data;
        BuildVisualTestSnapshot(Display(s), &accounts, &data);
        AccountMeters oai = AccountMeterState(Display(s), BarLayout::SegmentedRing, accounts[0], data[0], 0, 0);
        CHECK(roughly(tracks[0].origin.x, strip.columns[0].left + 3) && roughly(tracks[0].origin.y, 8));
        CHECK(tracks[0].shape.size() == 1 && tracks[0].shape[0].segments.size() == 9);
        CHECK_NEAR(tracks[0].shape[0].x, oai.quotas[0].track[0].x);
        CHECK_NEAR(tracks[0].shape[0].segments[4].y, oai.quotas[0].track[0].segments[4].y);
        auto green = std::find_if(strip.ops.begin(), strip.ops.end(), [](const SettingsPreviewOp& op) {
            return op.color == 0xFF43A047;
        });
        CHECK(green != strip.ops.end() && green->kind == SettingsPreviewOp::Kind::Shape && green->shade == 0);
        CHECK_NEAR(green->shape[0].x, oai.quotas[0].fill[0].x);

        // Headline segments: 16 x 4 tracks with 1 DIP corners, 2 DIPs apart, fills shaded and cut
        // flat, no ticks.
        s.barLayout = BarLayout::LogoHeadline;
        strip = Strip(s);
        tracks = OpsOfColor(strip, track);
        std::vector<SettingsPreviewOp> fills = shaded(strip);
        CHECK(tracks.size() == 10 && fills.size() == 10);
        CHECK_NEAR(tracks[0].radius, 1); CHECK_NEAR(tracks[0].rect.right - tracks[0].rect.left, 16);
        CHECK_NEAR(tracks[0].rect.bottom - tracks[0].rect.top, 4);
        CHECK(roughly(tracks[1].rect.left - tracks[0].rect.right, 2));
        // OAI's 5-hour quota: 25% of 16 DIPs, cut flat; CC 2's weekly one fills the segment.
        CHECK(roughly(fills[0].rect.right - fills[0].rect.left, 4) && (*fills[0].corners == Corners{1, 0, 0, 1}));
        CHECK(fills[0].shade == UsageHighlight(fills[0].color));
        auto full = std::find_if(fills.begin(), fills.end(), [&](const SettingsPreviewOp& op) { return op.color == 0xFFE53935; });
        CHECK(full != fills.end() && roughly(full->rect.right - full->rect.left, 16) &&
              (*full->corners == Corners{1, 1, 1, 1}));
        CHECK(OpsOfColor(strip, 0xC0000000).empty() && OpsOfColor(strip, 0xFFDEDEDE).empty());

        // The headline sizes and the bar gap: 24 x 6 tracks (1.5 DIP corners) 5 DIPs apart in a row,
        // and on each tile a 20 DIP number with its key at 9/14 of it.
        s.headlineFontSize = 20;
        s.headlineBarLength = 24;
        s.headlineBarThickness = 6;
        s.barGap = 5;
        strip = Strip(s);
        tracks = OpsOfColor(strip, track);
        fills = shaded(strip);
        CHECK(tracks.size() == 10 && fills.size() == 10);
        CHECK_NEAR(tracks[0].radius, 1.5); CHECK_NEAR(tracks[0].rect.right - tracks[0].rect.left, 24);
        CHECK_NEAR(tracks[0].rect.bottom - tracks[0].rect.top, 6);
        CHECK(roughly(tracks[1].rect.left - tracks[0].rect.right, 5) && roughly(tracks[2].rect.top, tracks[0].rect.top));
        CHECK(roughly(fills[0].rect.right - fills[0].rect.left, 6) && (*fills[0].corners == Corners{1.5f, 0, 0, 1.5f}));
        auto sized = [&](float size) {
            return std::count_if(strip.ops.begin(), strip.ops.end(), [&](const SettingsPreviewOp& op) {
                return op.kind == SettingsPreviewOp::Kind::Text && roughly(op.style.size, size);
            });
        };
        CHECK(sized(20) == 4 && sized(20 * 9 / 14.0f) == 4);
    });

    Run("preview: the headline key follows its number; the tile keeps its width as values change", [] {
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"CC", {true, true, false, false})};
        Settings s = StackedSettings();
        s.barLayout = BarLayout::LogoHeadline;
        auto at = [&](std::array<double, kQuotaBarCount> pct) {
            std::vector<AccountData> data = {Fresh(pct)};
            return StripOf(s, accounts, data);
        };
        auto width = [](const SettingsPreviewStrip& strip) { return strip.columns[0].right - strip.columns[0].left; };
        // The key 3 DIPs after the number, whatever its width.
        SettingsPreviewStrip low = at({5, 2, -1, -1}), full = at({30, 100, -1, -1}), none = at({-1, -1, -1, -1});
        CHECK(NearDip(TextOps(low, L"5h")[0].rect.left, TextOps(low, L"5%")[0].rect.right + 3));
        CHECK(NearDip(TextOps(full, L"7d")[0].rect.left, TextOps(full, L"100%")[0].rect.right + 3));
        // The row reserves "100%" and the widest key: the same width with a short number, another
        // key, or no reading.
        CHECK(NearDip(width(low), width(full)) && NearDip(width(none), width(full)));
        CHECK(TextOps(none, L"\u2013").size() == 1 && TextOps(none, L"5h").empty());
        // A larger number widens the row with it.
        s.headlineFontSize = 20;
        CHECK(width(at({5, 2, -1, -1})) > width(low) + 5);
    });

    Run("preview: the default pace color turns dark with a light halo on a light taskbar", [] {
        auto strip = [](const Settings& settings, bool light) {
            return SettingsPreviewLayout(TestGraphics(), settings, 48, 1, kNow, false, light,
                                         light ? 0xE4000000 : 0xFFFFFFFF);
        };
        Settings s = StackedSettings();
        SettingsPreviewStrip lightStrip = strip(s, true);
        size_t marks = OpsOfColor(strip(s, false), 0xFFDEDEDE).size();
        CHECK(marks > 0 && OpsOfColor(lightStrip, 0xFFDEDEDE).empty());
        CHECK(OpsOfColor(lightStrip, 0xFF1A1A1A).size() == marks);
        CHECK(OpsOfColor(lightStrip, 0x80FFFFFF).size() == marks && OpsOfColor(lightStrip, 0xC0000000).empty());
        s.paceTickStyle = PaceTickStyle::Line;
        CHECK(OpsOfColor(strip(s, true), 0xFF1A1A1A).size() == marks);
        // A chosen color stays, with the dark halo.
        s.paceTickColor = RGB(0xFF, 0x6E, 0xC7);
        lightStrip = strip(s, true);
        CHECK(OpsOfColor(lightStrip, 0xFFFF6EC7).size() == marks && OpsOfColor(lightStrip, 0xC0000000).size() == marks);
        // Tile ticks follow the taskbar the same way.
        s = StackedSettings();
        for (BarLayout layout : {BarLayout::SegmentedRing, BarLayout::ConcentricRings}) {
            s.barLayout = layout;
            size_t ticks = OpsOfColor(strip(s, false), 0xFFDEDEDE).size();
            lightStrip = strip(s, true);
            CHECK(ticks == 8 && OpsOfColor(lightStrip, 0xFFDEDEDE).empty());
            CHECK(OpsOfColor(lightStrip, 0xFF1A1A1A).size() == ticks && OpsOfColor(lightStrip, 0x80FFFFFF).size() == ticks);
            CHECK(OpsOfColor(strip(s, false), 0xC0000000).size() == ticks);
        }
    });

    Run("preview: a time track's small bar under each bar, its fill the elapsed time; vertical bars right of it", [] {
        using Corners = std::array<float, 4>;
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"A", {true, true, false, false})};
        accounts[0].tileIdentity = TileIdentity::Logo;
        Settings s = StackedSettings();
        s.paceTickStyle = PaceTickStyle::TimeTrack;
        auto at = [&](long long resetIn) {
            AccountData data = Fresh();
            data.win5h = Window(10, resetIn);
            data.winWeek = Window(10, kFiveHours);
            return StripOf(s, accounts, {data});
        };
        // A quarter elapsed: under the 100 x 8 bar, 1 DIP down, a 100 x 2 track with 1 DIP corners,
        // then a 25 DIP fill in the pace color, its far end square. The weekly bar starts 2 DIPs
        // (the bar gap) under the 5-hour bar's small bar, its fill empty.
        SettingsPreviewStrip strip = at(13500000);
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080), fills = OpsOfColor(strip, 0xFFDEDEDE);
        CHECK(tracks.size() == 4 && fills.size() == 1 && OpsOfColor(strip, 0xC0000000).empty());
        const SettingsPreviewOp &bar = tracks[0], &small = tracks[1], &weekly = tracks[2];
        CHECK(NearDip(small.rect.left, bar.rect.left) && NearDip(small.rect.right - small.rect.left, 100) &&
              NearDip(small.rect.top, bar.rect.bottom + 1) && NearDip(small.rect.bottom - small.rect.top, 2) &&
              NearDip(small.radius, 1));
        CHECK(NearDip(weekly.rect.top, small.rect.bottom + 2));
        CHECK(NearDip(fills[0].rect.left, small.rect.left) && NearDip(fills[0].rect.right - fills[0].rect.left, 25) &&
              NearDip(fills[0].rect.top, small.rect.top) && NearDip(fills[0].rect.bottom, small.rect.bottom));
        CHECK((*fills[0].corners == Corners{1, 0, 0, 1}));
        // All elapsed: the fill reaches the end and rounds there too.
        strip = at(0);
        fills = OpsOfColor(strip, 0xFFDEDEDE);
        CHECK(NearDip(fills[0].rect.right - fills[0].rect.left, 100) && (*fills[0].corners == Corners{1, 1, 1, 1}));
        // None elapsed: the small bars stay, without a fill.
        strip = at(kFiveHours);
        CHECK(OpsOfColor(strip, 0xFFDEDEDE).empty() && OpsOfColor(strip, 0x46808080).size() == 4);
        // Markers off: no small bars.
        s.showPaceTicks = false;
        CHECK(OpsOfColor(at(13500000), 0x46808080).size() == 2);
        s.showPaceTicks = true;
        // Vertical bars: right of each bar, 1 DIP out, filling up from the bottom; the next bar the
        // gap after it.
        s.barLayout = BarLayout::Vertical;
        strip = at(13500000);
        tracks = OpsOfColor(strip, 0x46808080);
        fills = OpsOfColor(strip, 0xFFDEDEDE);
        CHECK(NearDip(tracks[1].rect.left, tracks[0].rect.right + 1) && NearDip(tracks[1].rect.right - tracks[1].rect.left, 2) &&
              NearDip(tracks[1].rect.top, tracks[0].rect.top) && NearDip(tracks[1].rect.bottom, tracks[0].rect.bottom));
        CHECK(NearDip(tracks[2].rect.left, tracks[1].rect.right + 2));
        CHECK(NearDip(fills[0].rect.bottom, tracks[1].rect.bottom) && NearDip(fills[0].rect.bottom - fills[0].rect.top, 25) &&
              (*fills[0].corners == Corners{0, 0, 1, 1}));
    });

    Run("preview: gauges and pies beside the identity, the rounded square around it; the face sets the column", [] {
        // One account with its logo alone (16 DIPs on tiles), the weekly quota the most used. A 10
        // DIP margin: tiles pad 7 before and 8 after, ring tiles 4 each.
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"A", {true, true, false, false})};
        accounts[0].tileIdentity = TileIdentity::Logo;
        std::vector<AccountData> data = {Fresh({30, 80, -1, -1})};
        const uint32_t track = TilePaletteFor(false).track;
        auto width = [](const SettingsPreviewStrip& strip) { return strip.columns[0].right - strip.columns[0].left; };
        auto shapesAt = [](const SettingsPreviewStrip& strip, float x, float y) {
            std::vector<SettingsPreviewOp> shapes = OpsOfKind(strip, SettingsPreviewOp::Kind::Shape);
            for (const SettingsPreviewOp& op : shapes) CHECK(NearDip(op.origin.x, x) && NearDip(op.origin.y, y));
            return shapes;
        };
        auto capCentre = [](const SettingsPreviewOp& op) { return op.rect.top + AboveCap(op.style.size) + CapHeight(op.style.size) / 2; };
        Settings s = StackedSettings();
        s.accountMargin = 10;

        // Gauge: 7 + 16 + 6 + 32 + 8 DIPs; the logo centred on the 40 DIP tile, the face 6 DIPs after it.
        s.barLayout = BarLayout::Gauge;
        SettingsPreviewStrip strip = StripOf(s, accounts, data);
        CHECK(NearDip(strip.columns[0].left, 3) && NearDip(width(strip), 69) && NearDip(strip.width, 3 + 69 + 2));
        std::vector<SettingsPreviewOp> logo = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        CHECK(logo.size() == 1 && NearDip(logo[0].rect.left, 10) && NearDip(logo[0].rect.top, 16) &&
              NearDip(logo[0].rect.right, 26) && NearDip(logo[0].rect.bottom, 32));
        // The weekly arc alone: its track, then its orange fill.
        std::vector<SettingsPreviewOp> shapes = shapesAt(strip, 32, 8);
        CHECK(shapes.size() == 2 && shapes[0].color == track && shapes[1].color == 0xFFFB8C00);
        // Its number without %, the key under it, both centred across the face, their cap heights
        // centred 15.5 and 10.5 + 16 DIPs from its top.
        std::vector<SettingsPreviewOp> number = TextOps(strip, L"80"), key = TextOps(strip, L"7d");
        CHECK(number.size() == 1 && key.size() == 1 && TextOps(strip, L"80%").empty() && TextOps(strip, L"30").empty());
        CHECK(NearDip(number[0].style.size, 12) && number[0].color == 0xFFFB8C00 && number[0].opacity == 1);
        CHECK(NearDip(key[0].style.size, 12 * 9 / 14.0) && key[0].color == 0xFFFFFFFF && NearDip(key[0].opacity, 0.6));
        CHECK(NearDip((number[0].rect.left + number[0].rect.right) / 2, 32 + 16) &&
              NearDip((key[0].rect.left + key[0].rect.right) / 2, 32 + 16));
        CHECK(NearDip(capCentre(number[0]), 8 + 15.5) && NearDip(capCentre(key[0]), 8 + 26.5));

        // Pies: two 18 DIP pies 3 apart, a 39 x 18 face centred on the tile; no text.
        s.barLayout = BarLayout::ClockPies;
        strip = StripOf(s, accounts, data);
        CHECK(NearDip(strip.columns[0].left, 3) && NearDip(width(strip), 7 + 16 + 6 + 39 + 8));
        shapes = shapesAt(strip, 32, 15);
        CHECK(shapes.size() == 4 && shapes[0].color == track && shapes[1].color == track &&
              shapes[2].color == 0xFF43A047 && shapes[3].color == 0xFFFB8C00);
        CHECK(OpsOfKind(strip, SettingsPreviewOp::Kind::Text).empty());
        // Four quotas: a 32 DIP square of pies.
        std::vector<AccountConfig> four = accounts;
        four[0].showBars = {true, true, true, true};
        strip = StripOf(s, four, {Fresh({10, 20, 30, 40})});
        CHECK(NearDip(width(strip), 7 + 16 + 6 + 32 + 8));
        CHECK(shapesAt(strip, 32, 8).size() == 8);

        // Rounded square: the face 4 DIPs in, the logo centred in it at the hole's 14.08 DIPs.
        s.barLayout = BarLayout::RoundedSquare;
        strip = StripOf(s, accounts, data);
        CHECK(NearDip(strip.columns[0].left, 6) && NearDip(width(strip), 4 + 32 + 4) && NearDip(strip.width, 6 + 40 + 6));
        CHECK(shapesAt(strip, 10, 8).size() == 4);
        logo = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        CHECK(logo.size() == 1 && NearDip(logo[0].rect.right - logo[0].rect.left, 14.08) &&
              NearDip((logo[0].rect.left + logo[0].rect.right) / 2, 10 + 16) &&
              NearDip((logo[0].rect.top + logo[0].rect.bottom) / 2, 8 + 16));
    });

    Run("preview: pace ticks on gauges, pies and rounded squares, a core shape after each halo", [] {
        const uint32_t pace = 0xFFDEDEDE;
        auto ticks = [&](BarLayout layout) {
            Settings s = StackedSettings();
            s.barLayout = layout;
            SettingsPreviewStrip strip = Strip(s);
            for (size_t i = 0; i < strip.ops.size(); i++) {
                if (strip.ops[i].color != pace) continue;
                CHECK(strip.ops[i].kind == SettingsPreviewOp::Kind::Shape && i > 0);
                CHECK(strip.ops[i - 1].kind == SettingsPreviewOp::Kind::Shape && strip.ops[i - 1].color == 0xC0000000);
            }
            return OpsOfColor(strip, pace).size();
        };
        // Pies and squares: one per quota, as on rings. Gauges: one per fresh account (Go is stale).
        size_t rings = ticks(BarLayout::SegmentedRing);
        CHECK(rings > 0 && ticks(BarLayout::ClockPies) == rings && ticks(BarLayout::RoundedSquare) == rings);
        CHECK(ticks(BarLayout::Gauge) == 3);
        Settings off = StackedSettings();
        off.showPaceTicks = false;
        for (BarLayout layout : {BarLayout::Gauge, BarLayout::ClockPies, BarLayout::RoundedSquare}) {
            off.barLayout = layout;
            CHECK(OpsOfColor(Strip(off), pace).empty() && OpsOfColor(Strip(off), 0xC0000000).empty());
        }
    });

    Run("preview: the headline countdown replaces the quota segments under the number", [] {
        const ULONGLONG hour = 60 * kMinute;
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"CC", {true, true, false, false})};
        accounts[0].tileIdentity = TileIdentity::Logo;
        Settings s = StackedSettings();
        s.barLayout = BarLayout::LogoHeadline;
        s.headlineCountdown = true;
        auto at = [&](ULONGLONG weeklyResetIn, double weekly = 80) {
            AccountData data = Fresh();
            data.win5h = {30, kNow + 45 * kMinute, kFiveHourWindowMs};
            data.winWeek = {weekly, kNow + weeklyResetIn, kWeeklyWindowMs};
            return StripOf(s, accounts, {data});
        };
        auto width = [](const SettingsPreviewStrip& strip) { return strip.columns[0].right - strip.columns[0].left; };
        const uint32_t track = TilePaletteFor(false).track;
        // The weekly quota's: 3 days 12 hours, at the key's 9 DIPs, under the number's left edge, its
        // cap height 3 DIPs below the number's baseline; the panel centred on the tile.
        SettingsPreviewStrip strip = at((3 * 24 + 12) * hour);
        std::vector<SettingsPreviewOp> countdown = TextOps(strip, L"\u21BB 3d 12h");
        SettingsPreviewOp number = TextOps(strip, L"80%")[0];
        CHECK(countdown.size() == 1 && NearDip(countdown[0].style.size, 9) && NearDip(countdown[0].opacity, 0.7) &&
              countdown[0].color == 0xFFFFFFFF);
        CHECK(NearDip(countdown[0].rect.left, number.rect.left));
        const double numberCapTop = number.rect.top + AboveCap(14);
        CHECK(NearDip(countdown[0].rect.top + AboveCap(9), numberCapTop + CapHeight(14) + 3));
        CHECK(NearDip(numberCapTop, 24 - (CapHeight(14) + 3 + CapHeight(9)) / 2));
        // No segments: no tracks or fills.
        CHECK(OpsOfColor(strip, track).empty());
        CHECK(std::none_of(strip.ops.begin(), strip.ops.end(), [](const SettingsPreviewOp& op) { return op.corners.has_value(); }));
        // The tile reserves the widest form: a shorter countdown, the 5-hour quota's alone, or none
        // without readings keep its width.
        SettingsPreviewStrip soon = at(30 * kMinute), fiveHour = at(hour, -1);
        SettingsPreviewStrip blank = StripOf(s, accounts, {Fresh()});
        CHECK(TextOps(soon, L"\u21BB 30m").size() == 1 && NearDip(width(soon), width(strip)));
        CHECK(TextOps(fiveHour, L"\u21BB 45m").size() == 1 && NearDip(width(fiveHour), width(strip)));
        CHECK(TextOps(blank, L"\u21BB \u2013").size() == 1 && NearDip(width(blank), width(strip)));
        // Off: the two segments are back, no countdown.
        s.headlineCountdown = false;
        strip = at((3 * 24 + 12) * hour);
        CHECK(OpsOfColor(strip, track).size() == 2 && TextOps(strip, L"\u21BB 3d 12h").empty());
    });

    Run("preview: sparkline charts beside the identity, each plot's shapes in drawing order and its key", [] {
        using Kind = SettingsPreviewOp::Kind;
        // One account with its logo alone (16 DIPs), its 5-hour quota half way through the window;
        // a 10 DIP margin: tiles pad 7 before and 8 after. The preview charts the visual test
        // samples' history of each reading.
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"A", {true, false, false, false})};
        accounts[0].tileIdentity = TileIdentity::Logo;
        auto at = [&](const Settings& settings, WindowUsage reading, double taskbar = 48) {
            AccountData data = Fresh();
            data.win5h = reading;
            return StripOf(settings, accounts, {data}, taskbar);
        };
        auto width = [](const SettingsPreviewStrip& strip) { return strip.columns[0].right - strip.columns[0].left; };
        // Channels within one step of a hand-picked OKLCH lift.
        auto nearColor = [](uint32_t actual, uint32_t expected) {
            if (actual >> 24 != expected >> 24) return false;
            for (int shift : {16, 8, 0}) {
                if (std::abs(int(actual >> shift & 0xFF) - int(expected >> shift & 0xFF)) > 1) return false;
            }
            return true;
        };
        // The operations after the plot's background.
        auto after = [](const SettingsPreviewStrip& strip, uint32_t plotColor) {
            auto plotOp = std::find_if(strip.ops.begin(), strip.ops.end(),
                                       [&](const SettingsPreviewOp& op) { return op.color == plotColor; });
            return std::vector<SettingsPreviewOp>(plotOp + 1, strip.ops.end());
        };
        struct Mark {
            Kind kind;
            uint32_t color;
            float stroke;
            std::vector<float> dashes;
        };
        auto expect = [](const std::vector<SettingsPreviewOp>& marks, std::initializer_list<Mark> expected) {
            size_t k = 0;
            for (const Mark& mark : expected) {
                CHECK(marks[k].kind == mark.kind && marks[k].color == mark.color);
                CHECK(NearDip(marks[k].strokeWidth, mark.stroke) && marks[k].dashes == mark.dashes);
                CHECK(NearDip(marks[k].origin.x, 32) && NearDip(marks[k].origin.y, 8));
                // Filled nonzero, so overlapping dots merge and the ring keeps its hole.
                CHECK(marks[k].kind == Kind::Stroke || marks[k].winding);
                k++;
            }
        };
        Settings s = StackedSettings();
        s.barLayout = BarLayout::Sparkline;
        s.accountMargin = 10;
        const uint32_t plotColor = 0x59000000;
        const Mark lit{Kind::Shape, 0x1FFFFFFF, 0, {}}, outline{Kind::Stroke, 0x2EFFFFFF, 1, {}},
            diagonal{Kind::Stroke, 0x80DEDEDE, 1, {2, 2}},
            area{Kind::Shape, 0x4743A047, 0, {}}, runs{Kind::Stroke, 0xFF43A047, 1.3f, {}},
            dots{Kind::Shape, 0xFF43A047, 0, {}}, nowLine{Kind::Shape, 0x80FFFFFF, 0, {}},
            projection{Kind::Stroke, 0xFF43A047, 1.2f, {1, 1.5f}}, plainDot{Kind::Shape, 0xFF43A047, 0, {}};

        // 7 + 16 + 6 + 48 + 8 DIPs; the plot 6 DIPs after the logo, 48 x 32 (80% of the 40 DIP
        // tile) centred on the tile, 3 DIP corners.
        SettingsPreviewStrip strip = at(s, Window(25, 9000000));
        CHECK(NearDip(strip.columns[0].left, 3) && NearDip(width(strip), 85));
        std::vector<SettingsPreviewOp> plot = OpsOfColor(strip, plotColor);
        CHECK(plot.size() == 1 && plot[0].kind == Kind::Rect && NearDip(plot[0].radius, 3));
        CHECK(NearDip(plot[0].rect.left, 32) && NearDip(plot[0].rect.top, 8) && NearDip(plot[0].rect.right, 80) &&
              NearDip(plot[0].rect.bottom, 40));
        // By default its shapes, one operation each from the plot's corner: the lit part, the solid
        // 1 DIP outline, the dashed diagonal at half the pace color's alpha, the areas at 0x47 alpha,
        // the runs and the lone reading in the usage color (25%: green), the now line, the dashed
        // projection and the plain dot in the usage color; then the key.
        std::vector<SettingsPreviewOp> marks = after(strip, plotColor);
        CHECK(marks.size() == 10);
        expect(marks, {lit, outline, diagonal, area, runs, dots, nowLine, projection, plainDot});
        // The model's shapes: lit to x 24, half way; the outline from (3, 0.5) to the right side at
        // x 47.5 and down to y 29; two runs and their areas, one dot; the now line 1 DIP wide around
        // x 24 from y 1 to 31; the projection from the reading at (24, 23) at 25% to 50% (y 16) at the
        // reset; the dot's top 2 DIPs above the reading.
        CHECK(NearDip(marks[0].shape[0].segments[0].x, 24) && NearDip(marks[0].shape[0].segments[1].y, 32));
        CHECK(NearDip(marks[1].shape[0].x, 3) && NearDip(marks[1].shape[0].y, 0.5) && marks[1].shape[0].segments.size() == 8 &&
              NearDip(marks[1].shape[0].segments[2].x, 47.5) && NearDip(marks[1].shape[0].segments[2].y, 29));
        CHECK(marks[3].shape.size() == 2 && marks[4].shape.size() == 2 && marks[5].shape.size() == 1);
        CHECK(NearDip(marks[6].shape[0].x, 23.5) && NearDip(marks[6].shape[0].y, 1) &&
              NearDip(marks[6].shape[0].segments[1].x, 24.5) && NearDip(marks[6].shape[0].segments[1].y, 31));
        CHECK(NearDip(marks[7].shape[0].x, 24) && NearDip(marks[7].shape[0].y, 23) &&
              NearDip(marks[7].shape[0].segments[0].x, 48) && NearDip(marks[7].shape[0].segments[0].y, 16));
        CHECK(NearDip(marks[8].shape[0].x, 24) && NearDip(marks[8].shape[0].y, 21));
        // The key at the text size (8 DIPs by default), semi-bold, its right edge and baseline 3 DIPs
        // in from the plot's bottom-right corner.
        const SettingsPreviewOp& key = marks[9];
        CHECK(key.kind == Kind::Text && key.text == L"5h" && NearDip(key.style.size, 8) &&
              key.style.family == UiFamily::Taskbar && key.style.weight == DWRITE_FONT_WEIGHT_SEMI_BOLD &&
              key.color == 0xFFFFFFFF && NearDip(key.opacity, 0.75));
        CHECK(NearDip(key.rect.right, 77) && NearDip(key.rect.top + AboveCap(8) + CapHeight(8), 37));

        // Every cue on: the caret after the projection, then the cut-out in the taskbar's color, the
        // ring, and the dot lifted from green; the caret's tip 3.2 DIPs up from the bottom, the
        // cut-out's top 4.1 DIPs above the reading, the ring's two circles, the dot's top 2.2 above it.
        Settings cued = s;
        cued.sparklineNowCaret = cued.sparklineRing = true;
        marks = after(at(cued, Window(25, 9000000)), plotColor);
        CHECK(marks.size() == 13);
        expect(marks, {lit, outline, diagonal, area, runs, dots, nowLine, projection, {Kind::Shape, 0xF0FFFFFF, 0, {}},
                       {Kind::Shape, 0xFF1C1C1C, 0, {}}, {Kind::Shape, 0xF0FFFFFF, 0, {}}});
        CHECK(marks[11].kind == Kind::Shape && nearColor(marks[11].color, 0xFF4DA950) && marks[12].text == L"5h");
        CHECK(NearDip(marks[8].shape[0].segments[0].x, 24) && NearDip(marks[8].shape[0].segments[0].y, 28.8));
        CHECK(NearDip(marks[9].shape[0].x, 24) && NearDip(marks[9].shape[0].y, 18.9) && marks[10].shape.size() == 2);
        CHECK(NearDip(marks[11].shape[0].x, 24) && NearDip(marks[11].shape[0].y, 20.8));
        // Every cue off: the whole plot lit, no now line or caret, the plain dot; the outline stays.
        Settings bare = s;
        bare.sparklineElapsed = bare.sparklineNowLine = false;
        marks = after(at(bare, Window(25, 9000000)), plotColor);
        CHECK(marks.size() == 9);
        expect(marks, {lit, outline, diagonal, area, runs, dots, projection, plainDot});
        CHECK(marks[0].shape[0].segments.size() == 8 && NearDip(marks[0].shape[0].segments[1].x, 48));
        // The outline off too: the diagonal right after the lit part.
        bare.sparklineOutline = false;
        marks = after(at(bare, Window(25, 9000000)), plotColor);
        CHECK(marks.size() == 8);
        expect(marks, {lit, diagonal, area, runs, dots, projection, plainDot});
        // The outline alone off, every other default kept: no outline operation, the rest as by default.
        Settings unframed = s;
        unframed.sparklineOutline = false;
        strip = at(unframed, Window(25, 9000000));
        marks = after(strip, plotColor);
        CHECK(marks.size() == 9 && OpsOfColor(strip, 0x2EFFFFFF).empty());
        expect(marks, {lit, diagonal, area, runs, dots, nowLine, projection, plainDot});
        // Elapsed time alone off: the whole plot lit, the now line still there.
        Settings unlit = s;
        unlit.sparklineElapsed = false;
        marks = after(at(unlit, Window(25, 9000000)), plotColor);
        CHECK(marks.size() == 10 && marks[0].shape[0].segments.size() == 8 && marks[6].color == 0x80FFFFFF);

        // Over pace (80% half way, orange): the runs, the lone reading, the projection and the dot
        // orange, then the limit mark in the 100% color, last before the key.
        strip = at(s, Window(80, 9000000));
        std::vector<SettingsPreviewOp> limit = OpsOfColor(strip, 0xFFE53935);
        CHECK(limit.size() == 1 && limit[0].kind == Kind::Shape && OpsOfColor(strip, 0xFFFB8C00).size() == 4);
        CHECK(strip.ops.back().kind == Kind::Text && strip.ops[strip.ops.size() - 2].color == 0xFFE53935);
        // Markers off: no diagonal; the outline, the runs and the projection stroked.
        Settings off = s;
        off.showPaceTicks = false;
        strip = at(off, Window(25, 9000000));
        CHECK(OpsOfKind(strip, Kind::Stroke).size() == 3 && OpsOfColor(strip, 0x80DEDEDE).empty());
        // No history (the OpenAI sample has none): the outline, the diagonal and the projection; the
        // lit part, the now line and the dot.
        std::vector<AccountConfig> openai = {Account(L"openai", L"O", {true, false, false, false})};
        openai[0].tileIdentity = TileIdentity::Logo;
        AccountData openaiData = Fresh();
        openaiData.win5h = Window(25, 9000000);
        strip = StripOf(s, openai, {openaiData});
        std::vector<SettingsPreviewOp> strokes = OpsOfKind(strip, Kind::Stroke), shapes = OpsOfKind(strip, Kind::Shape);
        CHECK(strokes.size() == 3 && strokes[0].color == 0x2EFFFFFF && NearDip(strokes[0].strokeWidth, 1) &&
              NearDip(strokes[1].strokeWidth, 1) && NearDip(strokes[2].strokeWidth, 1.2));
        CHECK(shapes.size() == 3 && shapes[0].color == 0x1FFFFFFF && shapes[1].color == 0x80FFFFFF &&
              shapes[2].color == 0xFF43A047);
        // Without a window: the whole plot lit and outlined, the dot alone its radius in from the
        // right edge at (46, 23), on the key's box at the bottom: the key moved to the top-right
        // corner, its cap height 3 DIPs below the plot's top.
        strip = at(s, {25, 0, 0});
        shapes = OpsOfKind(strip, Kind::Shape);
        strokes = OpsOfKind(strip, Kind::Stroke);
        CHECK(strokes.size() == 1 && strokes[0].color == 0x2EFFFFFF && TextOps(strip, L"5h").size() == 1);
        CHECK(shapes.size() == 2 && shapes[0].color == 0x1FFFFFFF && shapes[0].shape[0].segments.size() == 8);
        CHECK(NearDip(shapes[1].shape[0].x, 46) && NearDip(shapes[1].shape[0].y, 21));
        const SettingsPreviewOp moved = TextOps(strip, L"5h")[0];
        CHECK(NearDip(moved.rect.right, 77) && NearDip(moved.rect.top + AboveCap(8), 11));

        // The chart width sets the column; the chart height the plot's, a percentage of the tile; the
        // key keeps the text size.
        Settings wide = s;
        wide.sparklineWidth = 120;
        wide.sparklineHeight = 100;
        strip = at(wide, Window(25, 9000000));
        plot = OpsOfColor(strip, plotColor);
        CHECK(NearDip(width(strip), 7 + 16 + 6 + 120 + 8) && NearDip(plot[0].rect.right - plot[0].rect.left, 120));
        CHECK(NearDip(plot[0].rect.top, 4) && NearDip(plot[0].rect.bottom, 44) && NearDip(TextOps(strip, L"5h")[0].style.size, 8));
        Settings low = s;
        low.sparklineHeight = 50;
        strip = at(low, Window(25, 9000000));
        plot = OpsOfColor(strip, plotColor);
        CHECK(NearDip(plot[0].rect.top, 14) && NearDip(plot[0].rect.bottom, 34) && NearDip(TextOps(strip, L"5h")[0].style.size, 8));
        // A 32 DIP taskbar's 24 DIP tile: 80% is 19 DIPs, 6.5 DIPs down the strip.
        strip = at(s, Window(25, 9000000), 32);
        plot = OpsOfColor(strip, plotColor);
        CHECK(NearDip(plot[0].rect.top, 6.5) && NearDip(plot[0].rect.bottom, 25.5) &&
              NearDip(TextOps(strip, L"5h")[0].style.size, 8));
        // Text sizes 6 and 12: the key at that size, its right edge and baseline still 3 DIPs in.
        for (int size : {6, 12}) {
            Settings sized = s;
            sized.sparklineTextSize = size;
            const SettingsPreviewOp sizedKey = TextOps(at(sized, Window(25, 9000000)), L"5h")[0];
            CHECK(NearDip(sizedKey.style.size, size) && NearDip(sizedKey.rect.right, 77) &&
                  NearDip(sizedKey.rect.top + AboveCap(size) + CapHeight(size), 37));
        }
        // A light taskbar: its plot, lit and outline colors, the diagonal in its dark pace color, the
        // now line black at its alpha; with every cue on also the caret and ring, and the cut-out in
        // its color.
        AccountData data = Fresh();
        data.win5h = Window(25, 9000000);
        std::vector<AccountData> one = {data};
        SettingsPreviewStrip light =
            SettingsPreviewLayout(TestGraphics(), s, 48, 1, kNow, false, true, 0xE4000000, &accounts, &one);
        CHECK(OpsOfColor(light, 0x14000000).size() == 1 && OpsOfColor(light, 0x12000000).size() == 1 &&
              OpsOfColor(light, 0x24000000).size() == 1 && OpsOfColor(light, 0x24000000)[0].kind == Kind::Stroke &&
              OpsOfColor(light, 0x801A1A1A).size() == 1 && OpsOfColor(light, 0x80000000).size() == 1 &&
              OpsOfColor(light, 0xF0000000).empty() && OpsOfColor(light, 0xFFF3F3F3).empty());
        light = SettingsPreviewLayout(TestGraphics(), cued, 48, 1, kNow, false, true, 0xE4000000, &accounts, &one);
        CHECK(OpsOfColor(light, 0xF0000000).size() == 2 && OpsOfColor(light, 0xFFF3F3F3).size() == 1);
    });

    Run("preview: an account's sparkline charts side by side; extra usage in the weekly chart's corner", [] {
        using Kind = SettingsPreviewOp::Kind;
        // One account with its logo alone, a 10 DIP margin as above: its 5-hour quota at 25% half way
        // through its window, the weekly one at `weekly`% `share` of the way through its own (half
        // way at 60% unless given), and `used` of `limit` dollars of monthly extra usage (OpenAI: a
        // credits balance).
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"A", {true, true, false, true})};
        accounts[0].tileIdentity = TileIdentity::Logo;
        auto at = [&](const Settings& settings, double used = -1, double limit = -1, double balance = -1,
                      std::wstring provider = L"anthropic", double weekly = 60, double share = 0.5) {
            std::vector<AccountConfig> configured = accounts;
            configured[0].provider = provider;
            AccountData data = Fresh();
            data.win5h = Window(25, 9000000);
            data.winWeek = {weekly, kNow + (ULONGLONG)std::llround((1 - share) * kWeeklyWindowMs), kWeeklyWindowMs};
            if (limit > 0) data.extraUsage = {used * 100 / limit, kNow + 10 * 24 * 60 * kMinute, 30ULL * 24 * 60 * kMinute};
            data.extraUsedAmount = used;
            data.extraLimitAmount = limit;
            data.creditsBalance = balance;
            return StripOf(settings, configured, {data});
        };
        auto width = [](const SettingsPreviewStrip& strip) { return strip.columns[0].right - strip.columns[0].left; };
        auto only = [](const SettingsPreviewStrip& strip, std::wstring_view text) {
            std::vector<SettingsPreviewOp> ops = TextOps(strip, text);
            CHECK(ops.size() == 1);
            return ops[0];
        };
        // Text whose cap height starts 3 DIPs below the plots' top (8).
        auto capTop = [](const SettingsPreviewOp& op) { return op.rect.top + AboveCap(op.style.size); };
        const uint32_t plotColor = 0x59000000;
        Settings s = StackedSettings();
        s.barLayout = BarLayout::Sparkline;
        s.accountMargin = 10;

        // No extra usage reported: two 48 DIP plots 4 apart (7 + 16 + 6 + 48 + 4 + 48 + 8), each key
        // 3 DIPs in from its own plot's corner, each chart's shapes from its own corner; no other text.
        SettingsPreviewStrip two = at(s);
        std::vector<SettingsPreviewOp> plots = OpsOfColor(two, plotColor);
        CHECK(plots.size() == 2 && NearDip(width(two), 137));
        CHECK(NearDip(plots[0].rect.left, 32) && NearDip(plots[0].rect.right, 80) && NearDip(plots[1].rect.left, 84) &&
              NearDip(plots[1].rect.right, 132) && NearDip(plots[1].rect.top, 8) && NearDip(plots[1].rect.bottom, 40));
        CHECK(NearDip(only(two, L"5h").rect.right, 77) && NearDip(only(two, L"7d").rect.right, 129));
        CHECK(OpsOfKind(two, Kind::Text).size() == 2);
        auto second = std::find_if(two.ops.begin(), two.ops.end(), [&](const SettingsPreviewOp& op) {
            return op.color == plotColor && op.rect.left > 50;
        });
        CHECK(second != two.ops.end() && std::next(second)->kind != Kind::Text);
        for (auto op = std::next(second); op != two.ops.end() && op->kind != Kind::Text; ++op) {
            CHECK(NearDip(op->origin.x, 84) && NearDip(op->origin.y, 8));
        }

        // $20 of $50 spent (40%): no chart of its own, the column's width kept. Amount: "$20.00"
        // without a key, semi-bold at the key's 8 DIPs in extra usage's green, its left and cap
        // height 3 DIPs in from the weekly plot's top-left corner.
        SettingsPreviewStrip spent = at(s, 20, 50);
        CHECK(OpsOfColor(spent, plotColor).size() == 2 && NearDip(width(spent), 137) && TextOps(spent, L"Ex").empty());
        const SettingsPreviewOp amount = only(spent, L"$20.00");
        CHECK(amount.color == 0xFF43A047 && amount.opacity == 1 && NearDip(amount.style.size, 8) &&
              amount.style.family == UiFamily::Taskbar && amount.style.weight == DWRITE_FONT_WEIGHT_SEMI_BOLD);
        CHECK(NearDip(amount.rect.left, 87) && NearDip(capTop(amount), 11) && amount.clip && amount.rect.right < 129);
        // After the weekly chart's key.
        CHECK(std::prev(std::find_if(spent.ops.begin(), spent.ops.end(), [](const SettingsPreviewOp& op) {
                  return op.text == L"$20.00";
              }))->text == L"7d");
        // Percent: the "Ex" key first, regular at 0.6 opacity in the text color, the number 2 DIPs
        // after it, both on the same cap line; remaining mode the share left.
        Settings percent = s;
        percent.sparklineExtra = SparklineExtra::Percent;
        SettingsPreviewStrip shares = at(percent, 20, 50);
        const SettingsPreviewOp ex = only(shares, L"Ex"), share = only(shares, L"40%");
        CHECK(ex.style.weight == DWRITE_FONT_WEIGHT_NORMAL && NearDip(ex.style.size, 8) && NearDip(ex.opacity, 0.6) &&
              ex.color == 0xFFFFFFFF && share.color == 0xFF43A047 && share.style.weight == DWRITE_FONT_WEIGHT_SEMI_BOLD);
        CHECK(NearDip(ex.rect.left, 87) && NearDip(share.rect.left, ex.rect.right + 2) && NearDip(capTop(ex), 11) &&
              NearDip(capTop(share), 11));
        percent.barMode = BarMode::Remaining;
        CHECK(TextOps(at(percent, 20, 50), L"60%").size() == 1);
        // OpenAI credits without a max: "Cr" and the balance, in the taskbar's text color.
        SettingsPreviewStrip credits = at(s, -1, -1, 420, L"openai");
        const SettingsPreviewOp cr = only(credits, L"Cr"), left = only(credits, L"420");
        CHECK(NearDip(cr.rect.left, 87) && NearDip(left.rect.left, cr.rect.right + 2) && left.color == 0xFFFFFFFF);
        // Off: no corner; extra usage's own chart third, 104 DIPs into the charts, with its key.
        Settings none = s;
        none.sparklineExtra = SparklineExtra::Off;
        SettingsPreviewStrip charted = at(none, 20, 50);
        plots = OpsOfColor(charted, plotColor);
        CHECK(plots.size() == 3 && NearDip(plots[2].rect.left, 136) && NearDip(width(charted), 189));
        CHECK(NearDip(only(charted, L"Ex").rect.right, 181) && TextOps(charted, L"$20.00").empty());

        // Text clear of the weekly reading. At 110% half way, (24, 2) under the corner's top-left
        // box: the amount moved to the bottom-left corner, its baseline 3 DIPs above the plot's
        // bottom (40); the key stays at the bottom-right. At 10% 80% of the way, (38.5, 27.25) under
        // the key's box: the key moved to the top-right corner, the amount stays at the top-left.
        auto baseline = [](const SettingsPreviewOp& op) { return op.rect.top + AboveCap(op.style.size) + CapHeight(op.style.size); };
        SettingsPreviewStrip high = at(s, 20, 50, -1, L"anthropic", 110);
        const SettingsPreviewOp lowAmount = only(high, L"$20.00"), lowKey = only(high, L"7d");
        CHECK(NearDip(lowAmount.rect.left, 87) && NearDip(baseline(lowAmount), 37) && NearDip(baseline(lowKey), 37) &&
              NearDip(lowKey.rect.right, 129) && lowAmount.rect.right < lowKey.rect.left);
        SettingsPreviewStrip late = at(s, 20, 50, -1, L"anthropic", 10, 0.8);
        const SettingsPreviewOp topAmount = only(late, L"$20.00"), topKey = only(late, L"7d");
        CHECK(NearDip(capTop(topAmount), 11) && NearDip(capTop(topKey), 11) && NearDip(topKey.rect.right, 129) &&
              topAmount.rect.right < topKey.rect.left);
        // Remaining mode, the reading clear of both: the amount left at the bottom-left, the key at the
        // top-right.
        Settings falling = s;
        falling.barMode = BarMode::Remaining;
        SettingsPreviewStrip fallingStrip = at(falling, 20, 50);
        const SettingsPreviewOp leftAmount = only(fallingStrip, L"$30.00"), leftKey = only(fallingStrip, L"7d");
        CHECK(NearDip(leftAmount.rect.left, 87) && NearDip(baseline(leftAmount), 37) && NearDip(capTop(leftKey), 11) &&
              NearDip(leftKey.rect.right, 129));

        // 24 DIP charts (the weekly one 28 DIPs after the first, at 60): the corner's text clipped 3
        // DIPs in from the plot's right edge, at x 81, like the weekly key's right edge.
        Settings narrow = s;
        narrow.sparklineWidth = 24;
        SettingsPreviewStrip clipped = at(narrow, 20, 50);
        plots = OpsOfColor(clipped, plotColor);
        CHECK(plots.size() == 2 && NearDip(plots[1].rect.left, 60) && NearDip(plots[1].rect.right, 84));
        const SettingsPreviewOp cut = only(clipped, L"$20.00");
        CHECK(cut.clip && NearDip(cut.rect.left, 63) && NearDip(cut.rect.right, 81) && NearDip(only(clipped, L"7d").rect.right, 81));
        narrow.sparklineExtra = SparklineExtra::Percent;
        clipped = at(narrow, 20, 50);
        const SettingsPreviewOp cutKey = only(clipped, L"Ex"), cutShare = only(clipped, L"40%");
        CHECK(cutKey.clip && NearDip(cutKey.rect.left, 63) && cutKey.rect.right < 81);
        CHECK(cutShare.clip && NearDip(cutShare.rect.left, cutKey.rect.right + 2) && NearDip(cutShare.rect.right, 81));
        // At 110% half way, the dot at (12, 2) over the corner: moving it to the bottom would put it on
        // the key, and moving both the key under the dot; neither moves, apart.
        narrow.sparklineExtra = SparklineExtra::Amount;
        SettingsPreviewStrip crowded = at(narrow, 20, 50, -1, L"anthropic", 110);
        const SettingsPreviewOp topCut = only(crowded, L"$20.00"), bottomKey = only(crowded, L"7d");
        CHECK(NearDip(capTop(topCut), 11) && NearDip(topCut.rect.right, 81) && NearDip(baseline(bottomKey), 37) &&
              NearDip(bottomKey.rect.right, 81));
    });

    Run("preview: a quota matrix's cells beside the identity, down the first column, rows centred on the tile", [] {
        // One account with its logo alone and a 10 DIP margin, as above.
        std::vector<AccountConfig> accounts = {Account(L"anthropic", L"A")};
        accounts[0].tileIdentity = TileIdentity::Logo;
        Settings s = StackedSettings();
        s.barLayout = BarLayout::QuotaMatrix;
        s.accountMargin = 10;
        auto at = [&](std::array<double, kQuotaBarCount> pct, std::array<bool, kQuotaBarCount> bars = {true, true, true, true},
                      double taskbar = 48) {
            std::vector<AccountConfig> configured = accounts;
            configured[0].showBars = bars;
            return StripOf(s, configured, {Fresh(pct)}, taskbar);
        };
        auto width = [](const SettingsPreviewStrip& strip) { return strip.columns[0].right - strip.columns[0].left; };
        auto only = [](const SettingsPreviewStrip& strip, std::wstring_view text) {
            std::vector<SettingsPreviewOp> ops = TextOps(strip, text);
            CHECK(ops.size() == 1);
            return ops[0];
        };
        auto baseline = [](const SettingsPreviewOp& op) {
            return op.rect.top + AboveCap(op.style.size) + CapHeight(op.style.size);
        };

        // Numbers at the 14 DIP number size, semi-bold, in their usage colors; keys at 9 DIPs in the
        // taskbar's text color.
        SettingsPreviewStrip four = at({5, 60, 80, 100});
        const SettingsPreviewOp k5 = only(four, L"5h"), k7 = only(four, L"7d"), kFa = only(four, L"Fa"), kEx = only(four, L"Ex");
        const SettingsPreviewOp n5 = only(four, L"5%"), n60 = only(four, L"60%"), n80 = only(four, L"80%"),
                                n100 = only(four, L"100%");
        for (const SettingsPreviewOp& n : {n5, n60, n80, n100}) {
            CHECK(NearDip(n.style.size, 14) && n.style.family == UiFamily::Display &&
                  n.style.weight == DWRITE_FONT_WEIGHT_SEMI_BOLD && n.opacity == 1);
        }
        CHECK(n5.color == 0xFF43A047 && n60.color == 0xFFFDD835 && n80.color == 0xFFFB8C00 && n100.color == 0xFFE53935);
        for (const SettingsPreviewOp& k : {k5, k7, kFa, kEx}) {
            CHECK(NearDip(k.style.size, 9) && k.style.family == UiFamily::Taskbar && k.color == 0xFFFFFFFF &&
                  NearDip(k.opacity, 0.6));
        }
        // The first column's keys 6 DIPs after the logo, its numbers 3 DIPs past the widest key
        // ("Cr" included, from an OpenAI tile); the second column a cell (which reserves "100%") and
        // 8 DIPs further.
        std::vector<AccountConfig> openai = {Account(L"openai", L"O", {false, false, false, true})};
        openai[0].tileIdentity = TileIdentity::Logo;
        const SettingsPreviewOp kCr = only(StripOf(s, openai, {Fresh({-1, -1, -1, 40})}), L"Cr");
        CHECK(NearDip(kCr.style.size, 9));
        float keyReserve = 0;
        for (const SettingsPreviewOp& k : {k5, k7, kFa, kEx, kCr}) keyReserve = std::max(keyReserve, k.rect.right - k.rect.left);
        keyReserve = std::ceil(keyReserve);
        const float cell = keyReserve + 3 + std::ceil(n100.rect.right - n100.rect.left);
        CHECK(NearDip(k5.rect.left, 32) && NearDip(k7.rect.left, 32));
        CHECK(NearDip(n5.rect.left, 32 + keyReserve + 3) && NearDip(n60.rect.left, n5.rect.left));
        CHECK(NearDip(kFa.rect.left, 32 + cell + 8) && NearDip(kEx.rect.left, kFa.rect.left));
        CHECK(NearDip(n80.rect.left, kFa.rect.left + keyReserve + 3) && NearDip(n100.rect.left, n80.rect.left));
        CHECK(NearDip(width(four), 7 + 16 + 6 + 2 * cell + 8 + 8));
        // Each row's keys and numbers on one baseline; rows a cap height and 5 DIPs apart, the pair
        // centred on the 40 DIP tile (24 DIPs down the strip).
        const double cap = CapHeight(14);
        CHECK(NearDip(baseline(k5), baseline(n5)) && NearDip(baseline(kFa), baseline(n5)) && NearDip(baseline(n80), baseline(n5)));
        CHECK(NearDip(baseline(k7), baseline(n60)) && NearDip(baseline(kEx), baseline(n60)) && NearDip(baseline(n100), baseline(n60)));
        CHECK(NearDip(baseline(n60) - baseline(n5), cap + 5));
        CHECK(NearDip((baseline(n5) - cap + baseline(n60)) / 2, 24));
        // The tile keeps its width as numbers change, and before the first reading (dashes).
        AccountData before = Fresh();
        before.lastSuccessMs = 0;
        SettingsPreviewStrip waiting = StripOf(s, accounts, {before});
        CHECK(NearDip(width(at({1, 2, 3, 4})), width(four)) && NearDip(width(waiting), width(four)));
        CHECK(TextOps(waiting, L"\u2013").size() == 4);
        // A number wider than "100%" widens its column, as XAML's auto columns do.
        SettingsPreviewStrip wide = at({1234, 60, 80, 100});
        const SettingsPreviewOp n1234 = only(wide, L"1234%");
        const float wider = std::max(n1234.rect.right - n1234.rect.left, std::ceil(n100.rect.right - n100.rect.left));
        CHECK(NearDip(only(wide, L"Fa").rect.left, 32 + keyReserve + 3 + wider + 8));
        CHECK(NearDip(width(wide), 7 + 16 + 6 + keyReserve + 3 + wider + 8 + cell + 8));

        // Two quotas: one column of two rows. Three: the third alone atop the second column. One: a
        // single row centred on the tile.
        SettingsPreviewStrip two = at({5, 60, -1, -1}, {true, true, false, false});
        CHECK(NearDip(width(two), 7 + 16 + 6 + cell + 8) &&
              NearDip(baseline(only(two, L"60%")) - baseline(only(two, L"5%")), cap + 5));
        SettingsPreviewStrip three = at({5, 60, 80, -1}, {true, true, true, false});
        CHECK(NearDip(width(three), width(four)) && NearDip(only(three, L"Fa").rect.left, 32 + cell + 8) &&
              NearDip(baseline(only(three, L"80%")), baseline(only(three, L"5%"))));
        SettingsPreviewStrip single = at({5, -1, -1, -1}, {true, false, false, false});
        CHECK(NearDip(width(single), 7 + 16 + 6 + cell + 8) && NearDip(baseline(only(single, L"5%")) - cap / 2, 24));
        // No readings after a success: no cells.
        SettingsPreviewStrip none = at({-1, -1, -1, -1});
        CHECK(OpsOfKind(none, SettingsPreviewOp::Kind::Text).empty() && NearDip(width(none), 7 + 16 + 6 + 8));
        // A 32 DIP taskbar's 24 DIP tile: two rows shrink the numbers to 15 / 1.4 DIPs, still centred
        // (16 DIPs down).
        SettingsPreviewStrip low = at({5, 60, 80, 100}, {true, true, true, true}, 32);
        const SettingsPreviewOp low5 = only(low, L"5%"), low60 = only(low, L"60%");
        CHECK(NearDip(low5.style.size, 15 / 1.4) && NearDip(only(low, L"5h").style.size, 15 / 1.4 * 9 / 14));
        CHECK(NearDip((baseline(low5) - CapHeight(15 / 1.4) + baseline(low60)) / 2, 16));
    });

    Run("preview paint: sparkline strokes draw, inside the plots' rows", [] {
        // The sample accounts painted by the production painter with pace markers on and off: only
        // the dashed diagonals differ, all inside the 32 DIP plots 8 DIPs down the 48 DIP strip.
        UiGraphics& g = TestGraphics();
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        CHECK(SUCCEEDED(com) || com == RPC_E_CHANGED_MODE);
        constexpr UINT kWidth = 1000, kHeight = 48;
        std::vector<std::vector<BYTE>> painted;
        {
            winrt::com_ptr<IWICImagingFactory> wic;
            winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                  __uuidof(IWICImagingFactory), wic.put_void()));
            for (bool pace : {true, false}) {
                SettingsView view = TestView(SettingsUiPage::Layout);
                view.settings.barLayout = BarLayout::Sparkline;
                view.settings.showPaceTicks = pace;
                view.status.taskbarHeight = 48;
                view.now = kNow;
                UiItem item;
                item.kind = UiItemKind::Preview;
                item.rect = {0, 0, float(kWidth), float(kHeight)};
                winrt::com_ptr<IWICBitmap> bitmap;
                winrt::check_hresult(wic->CreateBitmap(kWidth, kHeight, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                                       bitmap.put()));
                D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
                    D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                    96, 96);
                winrt::com_ptr<ID2D1RenderTarget> target;
                winrt::check_hresult(g.d2d->CreateWicBitmapRenderTarget(bitmap.get(), &properties, target.put()));
                UiTargetCache cache;
                target->BeginDraw();
                target->Clear(D2D1::ColorF(0, 0, 0, 1));
                SettingsPaintPreview(target.get(), cache, g, UiThemeInput{}, view, item, {-1e9f, -1e9f});
                winrt::check_hresult(target->EndDraw());
                cache.Clear();
                target = nullptr;
                std::vector<BYTE> pixels(size_t(kWidth) * kHeight * 4);
                WICRect all{0, 0, INT(kWidth), INT(kHeight)};
                winrt::check_hresult(bitmap->CopyPixels(&all, kWidth * 4, UINT(pixels.size()), pixels.data()));
                painted.push_back(std::move(pixels));
            }
        }
        if (SUCCEEDED(com)) CoUninitialize();
        int differing = 0, top = INT_MAX, bottom = -1;
        for (UINT y = 0; y < kHeight; y++) {
            for (UINT x = 0; x < kWidth; x++) {
                const size_t at = (size_t(y) * kWidth + x) * 4;
                if (std::equal(painted[0].begin() + at, painted[0].begin() + at + 4, painted[1].begin() + at)) continue;
                differing++;
                top = std::min(top, int(y));
                bottom = std::max(bottom, int(y));
            }
        }
        CHECK(differing > 0 && top >= 8 && bottom < 40);
    });

    Run("preview: the new tile styles stay drawable and bounded at extreme settings; unknown styles fall back", [] {
        Settings large = StackedSettings();
        large.ringSize = 40;
        large.ringThickness = 6;
        large.logoScale = 200;
        large.accountMargin = large.rightMargin = 500;
        large.headlineFontSize = 24;
        large.headlineCountdown = true;
        large.paceTickStyle = PaceTickStyle::TimeTrack;
        large.sparklineWidth = 120;
        large.sparklineHeight = 100;
        large.sparklineExtra = SparklineExtra::Percent;
        Settings wild = large;
        wild.ringSize = 900;
        wild.ringThickness = -1;
        wild.logoScale = -5;
        wild.accountMargin = 99999;
        wild.sparklineWidth = 1 << 30;
        wild.sparklineHeight = -(1 << 30);
        wild.sparklineExtra = SparklineExtra(42);
        for (const Settings& base : {large, wild}) {
            for (BarLayout layout : {BarLayout::Gauge, BarLayout::ClockPies, BarLayout::RoundedSquare,
                                     BarLayout::LogoHeadline, BarLayout::Stacked, BarLayout::Vertical,
                                     BarLayout::Sparkline, BarLayout::QuotaMatrix}) {
                Settings s = base;
                s.barLayout = layout;
                for (double taskbar : {0.0, 24.0, 48.0, 1e9}) {
                    // Per column two 500 DIP margins, a 32 DIP logo, 6 DIPs and a face (pies: 48
                    // DIPs across; sparklines: three 120 DIP plots 4 apart), or bars: well under
                    // 1000 + 600.
                    SettingsPreviewStrip strip = Strip(s, taskbar, false, 2.5f);
                    CHECK(Finite(strip));
                    CHECK(strip.width < 4 * 1600 && strip.height <= 96);
                }
            }
        }
        // A style past the last one previews as the last one.
        Settings last;
        last.barLayout = kLastBarLayout;
        Settings unknown;
        unknown.barLayout = BarLayout(42);
        SettingsPreviewStrip expected = Strip(last), actual = Strip(unknown);
        CHECK(actual.columns.size() == expected.columns.size() && NearDip(actual.width, expected.width));
        CHECK(OpsOfKind(actual, SettingsPreviewOp::Kind::Shape).size() == OpsOfKind(expected, SettingsPreviewOp::Kind::Shape).size());
    });

    Run("controller: the bar style commits as chosen and the preview follows", [] {
        TestStore store;
        store.settings = StackedSettings();
        SettingsEdits edits(store);
        edits.Reload();
        CHECK(edits.Projection().barStyle == BarStyle::Track);
        CHECK(edits.Commit(0, SettingsField::BarStyle, int(BarStyle::Rail)) == 0);
        CHECK(store.settings.barStyle == BarStyle::Rail);
        CHECK(store.Applied() == std::vector<std::wstring>{Op(SettingsField::BarStyle, 1)});
        CHECK(OpsOfColor(Strip(edits.Projection()), 0x66808080).size() == 10);
        // Out-of-range choices clamp to a style the preview draws.
        CHECK(edits.Commit(0, SettingsField::BarStyle, 7) == 0 && store.settings.barStyle == BarStyle::Rail);
        CHECK(edits.Commit(0, SettingsField::BarStyle, -3) == 0 && store.settings.barStyle == BarStyle::Track);
        // The Layout reset brings back the full track.
        CHECK(edits.Commit(0, SettingsField::BarStyle, int(BarStyle::Rail)) == 0);
        CHECK(edits.Reset(SettingsField::ResetLayout) == 0 && store.settings.barStyle == BarStyle::Track);
    });

    Run("controller: the sparkline's chart height and extra usage corner commit as chosen and the preview follows", [] {
        TestStore store;
        store.settings.barLayout = BarLayout::Sparkline;
        SettingsEdits edits(store);
        edits.Reload();
        // The samples' plots: 32 DIPs at the default 80% of the 40 DIP tile, 20 at 50%.
        auto plotHeights = [](const SettingsPreviewStrip& strip) {
            std::vector<SettingsPreviewOp> plots = OpsOfColor(strip, 0x59000000);
            CHECK(!plots.empty());
            return std::all_of(plots.begin(), plots.end(), [&](const SettingsPreviewOp& op) {
                return NearDip(op.rect.bottom - op.rect.top, plots[0].rect.bottom - plots[0].rect.top);
            }) ? plots[0].rect.bottom - plots[0].rect.top : -1.0f;
        };
        CHECK(NearDip(plotHeights(Strip(edits.Projection())), 32));
        CHECK(edits.Commit(0, SettingsField::SparklineHeight, 50) == 0 && store.settings.sparklineHeight == 50);
        CHECK(NearDip(plotHeights(Strip(edits.Projection())), 20));
        CHECK(edits.Commit(0, SettingsField::SparklineHeight, 140) == 0 && store.settings.sparklineHeight == 100);
        // The corner off: the CC 2 sample's dollars gone; then Percent; out-of-range choices clamp.
        auto dollars = [](const SettingsPreviewStrip& strip) {
            return std::count_if(strip.ops.begin(), strip.ops.end(), [](const SettingsPreviewOp& op) {
                return op.kind == SettingsPreviewOp::Kind::Text && op.text.starts_with(L"$");
            });
        };
        CHECK(dollars(Strip(edits.Projection())) == 1);
        CHECK(edits.Commit(0, SettingsField::SparklineExtra, int(SparklineExtra::Off)) == 0 &&
              store.settings.sparklineExtra == SparklineExtra::Off);
        CHECK(dollars(Strip(edits.Projection())) == 0);
        CHECK(edits.Commit(0, SettingsField::SparklineExtra, 9) == 0 && store.settings.sparklineExtra == SparklineExtra::Percent);
        CHECK(edits.Commit(0, SettingsField::SparklineExtra, -2) == 0 && store.settings.sparklineExtra == SparklineExtra::Off);
        // The Layout reset brings back 80% and the amount.
        CHECK(edits.Reset(SettingsField::ResetLayout) == 0 && store.settings.sparklineHeight == 80 &&
              store.settings.sparklineExtra == SparklineExtra::Amount);
    });

    Run("preview: extreme and out-of-range settings stay drawable and bounded", [] {
        Settings large = StackedSettings();
        large.barLength = 500;
        large.barThickness = 50;
        large.labelFontSize = large.percentFontSize = 24;
        large.barGap = large.labelGap = large.accountMargin = large.rightMargin = 500;
        large.showBarLabels = true;
        large.percentTextVisibility = PercentTextVisibility::Always;
        large.headlineFontSize = 24;
        large.headlineBarLength = 60;
        large.headlineBarThickness = 10;
        Settings wild = large;
        wild.headlineFontSize = -5;
        wild.headlineBarLength = 100000;
        wild.headlineBarThickness = -1;
        wild.barLength = 100000;
        wild.barThickness = -5;
        wild.labelFontSize = wild.percentFontSize = 0;
        wild.barGap = wild.labelGap = -10;
        wild.accountMargin = wild.rightMargin = 99999;
        wild.ringSize = 900;
        wild.ringThickness = -1;
        wild.labelPosition = LabelPosition(42);
        wild.barLayout = BarLayout(42);
        for (const Settings& base : {large, wild}) {
            for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical, BarLayout::LogoHeadline,
                                     BarLayout::SegmentedRing, BarLayout::ConcentricRings}) {
                Settings s = base;
                if (&base == &large || layout != BarLayout::Stacked) s.barLayout = layout;
                for (double taskbar : {0.0, 24.0, 48.0, 1e9}) {
                    // Bounded by the clamped ranges: per column two 500 DIP margins, a 500 DIP
                    // label gap, four bars with keys (37 + 50) and three 500 DIP gaps, and a
                    // label's text.
                    SettingsPreviewStrip strip = Strip(s, taskbar, false, 2.5f);
                    CHECK(Finite(strip));
                    CHECK(strip.width < 4 * (2 * 500 + 500 + 4 * (37 + 50) + 3 * 500 + 200) && strip.height <= 96);
                }
            }
        }
    });

    Run("preview: overflow beyond the strip and its scroll range", [] {
        Settings s = StackedSettings();
        s.barLayout = BarLayout::SegmentedRing;
        SettingsPreviewStrip strip = Strip(s);
        float fits = 4 + strip.width + strip.rightMargin + strip.trayWidth;
        CHECK_NEAR(SettingsPreviewOverflow(strip, fits), 0);
        CHECK_NEAR(SettingsPreviewOverflow(strip, fits + 100), 0);
        CHECK_NEAR(SettingsPreviewOverflow(strip, fits - 50), 50);
    });

    Run("layout page: bars sections, controls from the projection", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::Layout);
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        // Ten gallery cards, three a row: the tile styles in BarLayout order.
        const PCWSTR styles[] = {L"Stacked bars",     L"Vertical bars", L"Usage graph", L"Segmented ring",
                                 L"Concentric rings", L"Gauge %",       L"Clock pies",  L"Rounded square",
                                 L"Headline %",       L"Quota matrix"};
        for (int i = 0; i < 10; i++) {
            const UiItem& card = Control(frame, SettingsField::BarLayout, uint32_t(i));
            CHECK(card.kind == UiItemKind::Gallery && card.selected == (i == 0) && card.action.value == i);
            CHECK(card.text == styles[i]);
        }
        CHECK(std::count_if(frame.content.begin(), frame.content.end(),
                            [](const UiItem& item) { return item.kind == UiItemKind::Gallery; }) == 10);
        CHECK(std::size(kSettingsTileStyles) == 10 && int(BarLayout::Sparkline) == 2 &&
              int(BarLayout::ConcentricRings) == 4 && int(BarLayout::Gauge) == 5 && int(BarLayout::ClockPies) == 6 &&
              int(BarLayout::LogoHeadline) == 8 && int(kLastBarLayout) == 9);
        const UiItem& first = Control(frame, SettingsField::BarLayout, 0);
        const UiItem& fourth = Control(frame, SettingsField::BarLayout, 3);
        const UiItem& fifth = Control(frame, SettingsField::BarLayout, 4);
        const UiItem& seventh = Control(frame, SettingsField::BarLayout, 6);
        const UiItem& eighth = Control(frame, SettingsField::BarLayout, 7);
        CHECK_NEAR(fourth.rect.left, first.rect.left);
        CHECK_NEAR(fourth.rect.top - first.rect.top, (first.rect.bottom - first.rect.top) + 8);
        CHECK_NEAR(fifth.rect.top, fourth.rect.top);
        CHECK_NEAR(fifth.rect.left - fourth.rect.right, 8);
        CHECK_NEAR(seventh.rect.left, first.rect.left);
        CHECK_NEAR(seventh.rect.top - fourth.rect.top, (first.rect.bottom - first.rect.top) + 8);
        CHECK_NEAR(eighth.rect.top, seventh.rect.top);
        const UiItem& tenth = Control(frame, SettingsField::BarLayout, 9);
        CHECK_NEAR(tenth.rect.left, first.rect.left);
        CHECK_NEAR(tenth.rect.top - seventh.rect.top, (first.rect.bottom - first.rect.top) + 8);
        const UiItem& barStyle = Control(frame, SettingsField::BarStyle);
        CHECK(barStyle.kind == UiItemKind::Dropdown && barStyle.text == L"Full track" && barStyle.options.size() == 2 &&
              barStyle.options[1].text == L"Thin rail" && barStyle.options[1].value == int(BarStyle::Rail));
        const UiItem& length = Control(frame, SettingsField::BarLength, 1);
        CHECK(length.kind == UiItemKind::Slider && length.minimum == 10 && length.maximum == 500 &&
              length.action.value == 100);
        const UiItem& lengthBox = Control(frame, SettingsField::BarLength);
        CHECK(lengthBox.kind == UiItemKind::NumberBox && lengthBox.text == L"100" &&
              lengthBox.align == DWRITE_TEXT_ALIGNMENT_CENTER);
        CHECK_NEAR(lengthBox.rect.right - lengthBox.rect.left, 60);
        CHECK_NEAR(length.rect.left - lengthBox.rect.right, 14);
        CHECK_NEAR(length.rect.right - length.rect.left, 220);
        CHECK(Control(frame, SettingsField::BarThickness, 1).action.value == 8);
        CHECK(Control(frame, SettingsField::BarGap).text == L"2" &&
              Control(frame, SettingsField::BarGap, 3).runs[0].text == L"px");
        // The identity is per account: the Identity and text section's first card goes to
        // Accounts, before Identity position; then Label font size and Identity gap.
        const UiItem& identity = Control(frame, SettingsField::BarLayout, 10);
        CHECK(identity.kind == UiItemKind::Card && identity.action.kind == UiActionKind::Navigate &&
              identity.action.value == int(SettingsUiPage::Accounts));
        CHECK(HasText(frame, L"Account identity is set per account in Accounts") &&
              HasText(frame, L"Logo + number badge, logo only, or label.") && !HasText(frame, L"Logo on bars"));
        const UiItem& position = Control(frame, SettingsField::LabelPosition);
        CHECK(identity.rect.top > Control(frame, SettingsField::BarGap).rect.bottom &&
              identity.rect.bottom < position.rect.top);
        CHECK(position.text == L"Left" && position.options.size() == 5 && position.options[4].text == L"Hidden");
        CHECK(HasText(frame, L"Identity position") && HasText(frame, L"Where each account\u2019s identity sits") &&
              HasText(frame, L"Identity gap") && HasText(frame, L"Space between the identity and the bars.") &&
              HasText(frame, L"Text of accounts whose identity is Label") && !HasText(frame, L"Label position") &&
              !HasText(frame, L"Label gap") && !HasText(frame, L"Labels"));
        CHECK(position.rect.bottom < Control(frame, SettingsField::LabelFontSize, 1).rect.top &&
              Control(frame, SettingsField::LabelFontSize, 1).rect.bottom < Control(frame, SettingsField::LabelGap).rect.top);
        CHECK(Control(frame, SettingsField::LabelFontSize, 1).enabled);
        // Then the logo and badge sliders, before Quota keys.
        const UiItem& logoSize = Control(frame, SettingsField::LogoScale, 1);
        const UiItem& badgeSize = Control(frame, SettingsField::BadgeSize, 1);
        const UiItem& badgeX = Control(frame, SettingsField::BadgeOffsetX, 1);
        const UiItem& badgeY = Control(frame, SettingsField::BadgeOffsetY, 1);
        CHECK(logoSize.kind == UiItemKind::Slider && logoSize.minimum == 50 && logoSize.maximum == 200 &&
              logoSize.action.value == 100 && logoSize.enabled && Control(frame, SettingsField::LogoScale).text == L"100");
        CHECK(badgeSize.minimum == 6 && badgeSize.maximum == 20 && badgeSize.action.value == 13);
        CHECK(badgeX.minimum == -20 && badgeX.maximum == 20 && badgeX.action.value == 0 && badgeY.minimum == -20 &&
              badgeY.maximum == 20 && Control(frame, SettingsField::BadgeOffsetY).text == L"0");
        // The badge color between its size and offsets: four swatches, the first (white, the
        // taskbar's text color) chosen, then light blue, then Custom...
        const UiItem& badgeDefault = Control(frame, SettingsField::BadgeColor, 1);
        const UiItem& badgeCustom = Control(frame, SettingsField::BadgeColorCustom);
        for (uint32_t i = 1; i <= 4; i++) {
            const UiItem& swatch = Control(frame, SettingsField::BadgeColor, i);
            CHECK(swatch.kind == UiItemKind::Swatch && swatch.selected == (i == 1) && swatch.enabled);
        }
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::BadgeColor, 5)));
        CHECK(badgeDefault.action.value == int(RGB(0xFF, 0xFF, 0xFF)) &&
              Control(frame, SettingsField::BadgeColor, 2).action.value == int(RGB(0x4C, 0xC2, 0xFF)) &&
              Control(frame, SettingsField::BadgeColor, 3).action.value == int(RGB(0x1C, 0x1C, 0x1C)));
        CHECK(badgeCustom.text == L"Custom..." && badgeCustom.enabled &&
              badgeCustom.action.target == uint32_t(SettingsField::BadgeColorCustom));
        // The logo colors after Logo size: a dropdown per logo with brand colors, the taskbar's
        // text color chosen.
        const UiItem& claudeLogo = Control(frame, SettingsField::ClaudeLogoBrand);
        const UiItem& geminiLogo = Control(frame, SettingsField::GeminiLogoBrand);
        for (const UiItem* logo : {&claudeLogo, &geminiLogo}) {
            CHECK(logo->kind == UiItemKind::Dropdown && logo->enabled && logo->text == L"Taskbar text color" &&
                  logo->options.size() == 2 && logo->options[0].value == 0 && logo->options[1].value == 1);
        }
        CHECK(claudeLogo.options[1].text == L"Orange" && geminiLogo.options[1].text == L"Gemini gradient");
        CHECK(HasText(frame, L"Claude logo") && HasText(frame, L"Antigravity logo") &&
              HasText(frame, L"or Claude\u2019s orange") && HasText(frame, L"its purple-to-blue gradient"));
        CHECK(logoSize.rect.bottom < claudeLogo.rect.top && claudeLogo.rect.bottom < geminiLogo.rect.top &&
              geminiLogo.rect.bottom < badgeSize.rect.top);
        CHECK(Control(frame, SettingsField::LabelGap).rect.bottom < logoSize.rect.top && logoSize.rect.bottom < badgeSize.rect.top &&
              badgeSize.rect.bottom < badgeDefault.rect.top && badgeDefault.rect.bottom < badgeX.rect.top &&
              badgeX.rect.bottom < badgeY.rect.top && badgeY.rect.bottom < Control(frame, SettingsField::ShowBarLabels).rect.top);
        CHECK(HasText(frame, L"Logo size") && HasText(frame, L"Badge size") && HasText(frame, L"Badge horizontal offset") &&
              HasText(frame, L"Badge vertical offset") && HasText(frame, L"Percent of the standard size; rings keep it inside the hole.") &&
              HasText(frame, L"Badge color") && HasText(frame, L"White, the default, follows the taskbar\u2019s text color"));
        CHECK(!Control(frame, SettingsField::ShowBarLabels).selected);
        CHECK(Control(frame, SettingsField::PercentFontSize, 1).action.value == 9);
        CHECK(Control(frame, SettingsField::ShowPaceTicks).selected);
        CHECK(Control(frame, SettingsField::PaceTickStyle).text == L"Caret");
        for (uint32_t i = 1; i <= 5; i++) {
            const UiItem& swatch = Control(frame, SettingsField::PaceTickColor, i);
            CHECK(swatch.kind == UiItemKind::Swatch && swatch.selected == (i == 1));
        }
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::PaceTickColor, 6)));
        CHECK(Control(frame, SettingsField::PaceTickColor, 4).action.value == int(RGB(0x4C, 0xC2, 0xFF)));
        CHECK(Control(frame, SettingsField::PaceTickColorCustom).text == L"Custom...");
        CHECK(Control(frame, SettingsField::AccountMargin).text == L"3" &&
              Control(frame, SettingsField::RightMargin).text == L"4");
        CHECK(Control(frame, SettingsField::ResetLayout).text == L"Reset");
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::RingSize, 1)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::HeadlineBarLength, 1)));
        for (PCWSTR text : {L"Tile style", L"Bars", L"Identity and text", L"Pace markers", L"Spacing",
                            L"Ticks mark how much", L"Reset Layout to defaults", L"sizes, identity and text, pace"}) {
            CHECK(HasText(frame, text));
        }
        // Gallery cards, sliders, swatches are tab stops; the preview strip isn't.
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        auto stop = [&](uint32_t id) {
            return std::any_of(stops.begin(), stops.end(), [&](const UiFocusStop& s) { return s.ref.id == id; });
        };
        CHECK(stop(SettingsControlId(SettingsField::BarLayout, 4)) && stop(SettingsControlId(SettingsField::BarLength, 1)) &&
              stop(SettingsControlId(SettingsField::PaceTickColor, 3)) &&
              stop(SettingsControlId(SettingsField::BarLayout, 10)) &&
              !stop(SettingsControlId(SettingsField::PreviewScroll)));
        // Vertical bars have it too.
        view.settings.barLayout = BarLayout::Vertical;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::BarLayout, 10).action.kind == UiActionKind::Navigate &&
              HasText(frame, L"Account identity is set per account in Accounts") &&
              HasText(frame, L"Identity position"));
    });

    Run("layout page: ring and headline sections", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::Layout);
        view.settings.barLayout = BarLayout::ConcentricRings;
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        const UiItem& size = Control(frame, SettingsField::RingSize, 1);
        CHECK(size.minimum == 24 && size.maximum == 40 && size.action.value == 32);
        CHECK(Control(frame, SettingsField::RingThickness, 1).maximum == 6);
        const UiItem& identity = Control(frame, SettingsField::BarLayout, 10);
        CHECK(identity.kind == UiItemKind::Card && identity.action.kind == UiActionKind::Navigate &&
              identity.action.value == int(SettingsUiPage::Accounts));
        CHECK(HasText(frame, L"Account identity is set per account in Accounts"));
        // The Identity section: the identity's card, then the logo and badge sliders, enabled.
        CHECK(Control(frame, SettingsField::RingThickness, 1).rect.bottom < identity.rect.top &&
              identity.rect.bottom < Control(frame, SettingsField::LogoScale, 1).rect.top);
        CHECK(Control(frame, SettingsField::LogoScale, 1).enabled && Control(frame, SettingsField::BadgeOffsetY, 1).enabled &&
              HasText(frame, L"Identity"));
        CHECK(Control(frame, SettingsField::LogoScale, 1).rect.bottom < Control(frame, SettingsField::ClaudeLogoBrand).rect.top &&
              Control(frame, SettingsField::GeminiLogoBrand).rect.bottom < Control(frame, SettingsField::BadgeSize, 1).rect.top);
        // A negative offset shows signed.
        view.settings.badgeOffsetX = -5;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::BadgeOffsetX).text == L"-5" &&
              Control(frame, SettingsField::BadgeOffsetX, 1).action.value == -5);
        view.settings.badgeOffsetX = 0;
        CHECK(HasText(frame, L"Ticks mark how much") && HasText(frame, L"dark on a light taskbar"));
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::BarLength, 1)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::BarStyle)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::PaceTickStyle)));
        CHECK(Control(frame, SettingsField::BarLayout, 4).selected);

        view.settings.barLayout = BarLayout::LogoHeadline;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::BarLayout, 8).selected);
        // The Headline section: the number size, its bars' length and thickness, then the bars'
        // gap, before the Identity section.
        const UiItem& number = Control(frame, SettingsField::HeadlineFontSize, 1);
        const UiItem& barLength = Control(frame, SettingsField::HeadlineBarLength, 1);
        const UiItem& barThickness = Control(frame, SettingsField::HeadlineBarThickness, 1);
        const UiItem& gap = Control(frame, SettingsField::BarGap);
        CHECK(number.kind == UiItemKind::Slider && number.minimum == 10 && number.maximum == 24 &&
              number.action.value == 14 && Control(frame, SettingsField::HeadlineFontSize).text == L"14");
        CHECK(barLength.minimum == 4 && barLength.maximum == 60 && barLength.action.value == 16);
        CHECK(barThickness.minimum == 2 && barThickness.maximum == 10 && barThickness.action.value == 4);
        CHECK(gap.kind == UiItemKind::NumberBox && gap.text == L"2");
        CHECK(number.rect.bottom < barLength.rect.top && barLength.rect.bottom < barThickness.rect.top &&
              barThickness.rect.bottom < gap.rect.top &&
              gap.rect.bottom < Control(frame, SettingsField::BarLayout, 10).rect.top);
        CHECK(HasText(frame, L"Headline") && HasText(frame, L"Number size") &&
              HasText(frame, L"its quota key scales with it") && !HasText(frame, L"fixed size"));
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::BarLength, 1)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::BarThickness, 1)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::RingSize, 1)));
        CHECK(Control(frame, SettingsField::BarLayout, 10).action.kind == UiActionKind::Navigate);
        CHECK(Control(frame, SettingsField::BarLayout, 10).rect.bottom < Control(frame, SettingsField::LogoScale, 1).rect.top &&
              Control(frame, SettingsField::BadgeSize, 1).enabled);
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::ShowPaceTicks)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::PaceTickColor, 1)) && !HasText(frame, L"Pace markers"));
        CHECK(Control(frame, SettingsField::AccountMargin).kind == UiItemKind::NumberBox);
    });

    Run("layout page: dependent rows disable with their reason; custom colors; draft text", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::Layout);
        view.settings.labelPosition = LabelPosition::Hidden;
        view.settings.showPaceTicks = false;
        view.settings.percentTextVisibility = PercentTextVisibility::Never;
        view.settings.paceTickColor = 0x123456;
        view.settings.badgeColor = 0x654321;
        view.settings.logoBrands.claude = true;
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::LabelPosition).text == L"Hidden");
        // The logo colors stay: the flyout and the windows show the logos without an identity.
        CHECK(Control(frame, SettingsField::ClaudeLogoBrand).enabled && Control(frame, SettingsField::GeminiLogoBrand).enabled &&
              Control(frame, SettingsField::ClaudeLogoBrand).text == L"Orange" &&
              Control(frame, SettingsField::GeminiLogoBrand).text == L"Taskbar text color");
        CHECK(!Control(frame, SettingsField::LabelFontSize, 1).enabled && !Control(frame, SettingsField::LabelFontSize).enabled);
        CHECK(!Control(frame, SettingsField::LabelGap).enabled);
        CHECK(!Control(frame, SettingsField::LogoScale, 1).enabled && !Control(frame, SettingsField::BadgeSize).enabled &&
              !Control(frame, SettingsField::BadgeOffsetX, 1).enabled && !Control(frame, SettingsField::BadgeOffsetY).enabled &&
              !Control(frame, SettingsField::BadgeColor, 1).enabled && !Control(frame, SettingsField::BadgeColorCustom).enabled);
        CHECK(HasText(frame, L"Choose an identity position to change this.") &&
              !HasText(frame, L"Choose a label position"));
        CHECK(!Control(frame, SettingsField::PercentFontSize, 1).enabled);
        CHECK(HasText(frame, L"Turn on Quota keys, or percentage text in Display, to change this."));
        CHECK(!Control(frame, SettingsField::PaceTickStyle).enabled && !Control(frame, SettingsField::PaceTickColorCustom).enabled);
        CHECK(HasText(frame, L"Turn on Pace markers to change this."));
        // A custom color shows as a sixth, chosen swatch.
        const UiItem& custom = Control(frame, SettingsField::PaceTickColor, 6);
        CHECK(custom.selected && !custom.enabled && custom.action.value == 0x123456);
        CHECK(!Control(frame, SettingsField::PaceTickColor, 1).selected);
        // And a custom badge color as a fifth.
        const UiItem& customBadge = Control(frame, SettingsField::BadgeColor, 5);
        CHECK(customBadge.selected && customBadge.action.value == 0x654321);
        CHECK(!Control(frame, SettingsField::BadgeColor, 1).selected);

        // A slider's box shows its draft's text and error; the slider the last valid value.
        view = TestView(SettingsUiPage::Layout);
        view.drafts.push_back({0, SettingsField::BarLength, 1, true, L"12a", 100, false, false,
                               L"Enter a whole number from 10 to 500."});
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::BarLength).text == L"12a");
        CHECK(Control(frame, SettingsField::BarLength, 1).action.value == 100);
        CHECK(HasText(frame, L"Enter a whole number from 10 to 500."));
        // A dragged value whose save failed shows its error too.
        view = TestView(SettingsUiPage::Layout);
        view.drafts.push_back({0, SettingsField::BarThickness, 1, false, L"", 20, true, true,
                               L"This change couldn't be saved."});
        view.settings.barThickness = 20;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::BarThickness).text == L"20");
        CHECK(HasText(frame, L"This change couldn't be saved."));
    });

    Run("layout page: gauge, pies and square sections with their size cards; no thickness for pies", [] {
        UiGraphics& g = TestGraphics();
        auto section = [](const UiFrame& frame, std::wstring_view text) {
            return std::any_of(frame.content.begin(), frame.content.end(), [&](const UiItem& item) {
                return item.kind == UiItemKind::Text && item.font == UiFont::BodyStrong && UiRunsText(item.runs) == text;
            });
        };
        struct Case {
            BarLayout layout;
            PCWSTR section, size, sizeText, thickness, thicknessText;
        };
        const PCWSTR aroundNumber = L"Narrows automatically when the arc doesn\u2019t fit around the number.";
        const PCWSTR aroundIdentity = L"Narrows automatically when the meters don\u2019t fit around the logo or text.";
        for (const Case& c : {Case{BarLayout::Gauge, L"Gauge", L"Gauge size", L"Diameter in pixels.", L"Gauge thickness", aroundNumber},
                              Case{BarLayout::ClockPies, L"Pies", L"Pie area",
                                   L"Height in pixels; one pie per quota, smaller from the second on.", nullptr, nullptr},
                              Case{BarLayout::RoundedSquare, L"Square", L"Square size", L"Side in pixels.",
                                   L"Square thickness", aroundIdentity}}) {
            SettingsView view = TestView(SettingsUiPage::Layout);
            view.settings.barLayout = c.layout;
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            CHECK(Control(frame, SettingsField::BarLayout, uint32_t(c.layout)).selected &&
                  !Control(frame, SettingsField::BarLayout, 3).selected);
            CHECK(section(frame, c.section) && !section(frame, L"Ring") && !section(frame, L"Headline") &&
                  !section(frame, L"Bars") && section(frame, L"Identity") && section(frame, L"Pace markers") &&
                  section(frame, L"Spacing"));
            // The ring size's range under the style's own title.
            const UiItem& size = Control(frame, SettingsField::RingSize, 1);
            CHECK(size.minimum == 24 && size.maximum == 40 && size.action.value == 32 && size.enabled);
            CHECK(HasText(frame, c.size) && HasText(frame, c.sizeText) && !HasText(frame, L"Ring size") &&
                  !HasText(frame, L"Ring thickness"));
            if (c.thickness) {
                const UiItem& thickness = Control(frame, SettingsField::RingThickness, 1);
                CHECK(thickness.maximum == 6 && thickness.action.value == 3 && thickness.enabled);
                CHECK(HasText(frame, c.thickness) && HasText(frame, c.thicknessText));
                CHECK(size.rect.bottom < thickness.rect.top &&
                      thickness.rect.bottom < Control(frame, SettingsField::BarLayout, 10).rect.top);
            } else {
                CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::RingThickness)) &&
                      !UiFindItem(frame, SettingsControlId(SettingsField::RingThickness, 1)) && !HasText(frame, L"thickness"));
                CHECK(size.rect.bottom < Control(frame, SettingsField::BarLayout, 10).rect.top);
            }
            // Then the identity's card and the logo cards; pace markers without the bars' style; no
            // bar or headline cards.
            CHECK(Control(frame, SettingsField::BarLayout, 10).action.kind == UiActionKind::Navigate &&
                  Control(frame, SettingsField::LogoScale, 1).enabled);
            CHECK(Control(frame, SettingsField::ShowPaceTicks).kind == UiItemKind::Toggle &&
                  Control(frame, SettingsField::PaceTickColor, 1).enabled &&
                  !UiFindItem(frame, SettingsControlId(SettingsField::PaceTickStyle)));
            CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::BarLength, 1)) &&
                  !UiFindItem(frame, SettingsControlId(SettingsField::BarStyle)) &&
                  !UiFindItem(frame, SettingsControlId(SettingsField::HeadlineBarLength, 1)) &&
                  !UiFindItem(frame, SettingsControlId(SettingsField::HeadlineCountdown)));
        }
    });

    Run("layout page: sparkline and matrix sections; the sparkline's pace markers without a marker style", [] {
        UiGraphics& g = TestGraphics();
        auto section = [](const UiFrame& frame, std::wstring_view text) {
            return std::any_of(frame.content.begin(), frame.content.end(), [&](const UiItem& item) {
                return item.kind == UiItemKind::Text && item.font == UiFont::BodyStrong && UiRunsText(item.runs) == text;
            });
        };
        auto absent = [](const UiFrame& frame, std::initializer_list<std::pair<SettingsField, uint32_t>> controls) {
            for (auto [field, part] : controls) CHECK(!UiFindItem(frame, SettingsControlId(field, part)));
        };
        SettingsView view = TestView(SettingsUiPage::Layout);
        view.settings.barLayout = BarLayout::Sparkline;
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::BarLayout, 2).selected && !Control(frame, SettingsField::BarLayout, 0).selected);
        CHECK(section(frame, L"Usage graph") && section(frame, L"Current position") && section(frame, L"Identity") &&
              section(frame, L"Pace markers") && section(frame, L"Spacing") && !section(frame, L"Matrix") &&
              !section(frame, L"Ring") && !section(frame, L"Headline") && !section(frame, L"Bars"));
        // Each chart's width, its height (percent of the tile), the text size and the extra usage
        // corner, then the current position cues, before the identity's card; no ring size.
        const UiItem& chartWidth = Control(frame, SettingsField::SparklineWidth, 1);
        const UiItem& chartHeight = Control(frame, SettingsField::SparklineHeight, 1);
        const UiItem& textSize = Control(frame, SettingsField::SparklineTextSize, 1);
        const UiItem& extra = Control(frame, SettingsField::SparklineExtra);
        CHECK(textSize.kind == UiItemKind::Slider && textSize.minimum == 6 && textSize.maximum == 16 &&
              textSize.action.value == 8 && textSize.enabled && Control(frame, SettingsField::SparklineTextSize).text == L"8");
        CHECK(HasText(frame, L"Text size") &&
              HasText(frame, L"Quota keys (5h, 7d, ...) and extra usage on the charts, in pixels."));
        // The chart outline: a toggle, on by default, between the text size and the extra usage.
        const UiItem& outline = Control(frame, SettingsField::SparklineOutline);
        CHECK(outline.kind == UiItemKind::Toggle && outline.enabled && outline.selected && outline.action.value == 0);
        CHECK(HasText(frame, L"Chart outline") && HasText(frame, L"A faint border around each chart."));
        // The cues: elapsed time and the now line on, the caret and the ring off by default; each a
        // toggle whose action flips it, in that order after the extra usage card.
        const std::tuple<SettingsField, PCWSTR, PCWSTR, bool> cueCards[] = {
            {SettingsField::SparklineElapsed, L"Elapsed time",
             L"Lights the part of the quota\u2019s window that has passed; the rest stays dim.", true},
            {SettingsField::SparklineNowLine, L"Now line", L"A thin line across the chart at the current time.", true},
            {SettingsField::SparklineNowCaret, L"Now caret",
             L"A small triangle on the chart\u2019s bottom edge at the current time.", false},
            {SettingsField::SparklineRing, L"Ringed reading",
             L"A ring around the current reading, cut out of the lines under it. Off: a plain dot.", false}};
        float cueTop = extra.rect.bottom;
        for (auto [field, title, description, on] : cueCards) {
            const UiItem& toggle = Control(frame, field);
            CHECK(toggle.kind == UiItemKind::Toggle && toggle.enabled && toggle.selected == on &&
                  toggle.action.value == int(!on));
            CHECK(HasText(frame, title) && HasText(frame, description));
            CHECK(toggle.rect.top > cueTop);
            cueTop = toggle.rect.bottom;
        }
        CHECK(chartWidth.kind == UiItemKind::Slider && chartWidth.minimum == 24 && chartWidth.maximum == 120 &&
              chartWidth.action.value == 48 && chartWidth.enabled && Control(frame, SettingsField::SparklineWidth).text == L"48");
        CHECK(chartHeight.kind == UiItemKind::Slider && chartHeight.minimum == 50 && chartHeight.maximum == 100 &&
              chartHeight.action.value == 80 && chartHeight.enabled && Control(frame, SettingsField::SparklineHeight).text == L"80");
        CHECK(extra.kind == UiItemKind::Dropdown && extra.enabled && extra.text == L"Amount" && extra.options.size() == 3 &&
              extra.options[0].text == L"Off" && extra.options[0].value == int(SparklineExtra::Off) &&
              extra.options[1].text == L"Amount" && extra.options[1].value == int(SparklineExtra::Amount) &&
              extra.options[2].text == L"Percent" && extra.options[2].value == int(SparklineExtra::Percent));
        CHECK(HasText(frame, L"Chart width") && HasText(frame, L"Pixels across each quota\u2019s whole window.") &&
              HasText(frame, L"Chart height") && HasText(frame, L"Percent of the tile\u2019s height, from 0% to 100% used, or left in Remaining mode.") &&
              HasText(frame, L"Extra usage") &&
              HasText(frame, L"Anthropic extra usage or OpenAI credits in the 7d chart\u2019s top-left corner "
                             L"(bottom-left in Remaining mode), when selected for the account and reported: spent "
                             L"in Used mode, left in Remaining mode. Percent shows the amount without a limit. "
                             L"Credits never get a chart of their own.") &&
              !HasText(frame, L"Ring size") && !HasText(frame, L"Pixels from 0% to 100% used."));
        const UiItem& identity = Control(frame, SettingsField::BarLayout, 10);
        CHECK(chartWidth.rect.bottom < chartHeight.rect.top && chartHeight.rect.bottom < textSize.rect.top &&
              textSize.rect.bottom < outline.rect.top && outline.rect.bottom < extra.rect.top &&
              cueTop < identity.rect.top);
        CHECK(identity.action.kind == UiActionKind::Navigate && Control(frame, SettingsField::LogoScale, 1).enabled);
        absent(frame, {{SettingsField::RingSize, 0}, {SettingsField::RingSize, 1}, {SettingsField::RingThickness, 1},
                       {SettingsField::BarLength, 1}, {SettingsField::BarStyle, 0}, {SettingsField::HeadlineFontSize, 1},
                       {SettingsField::HeadlineCountdown, 0}, {SettingsField::BarGap, 0}});
        // Pace markers: the toggle, describing the diagonal, and the color; no marker style.
        const UiItem& pace = Control(frame, SettingsField::ShowPaceTicks);
        CHECK(pace.kind == UiItemKind::Toggle && pace.selected && Control(frame, SettingsField::PaceTickColor, 1).enabled);
        absent(frame, {{SettingsField::PaceTickStyle, 0}});
        CHECK(HasText(frame, L"A diagonal marks even use of the quota across its window, to compare with its usage.") &&
              !HasText(frame, L"Ticks mark how much"));
        // Markers off: the color waits for them.
        view.settings.showPaceTicks = false;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(!Control(frame, SettingsField::PaceTickColor, 1).enabled && HasText(frame, L"Turn on Pace markers to change this."));
        // A chosen width, height and text size on the sliders and in their boxes; a chosen corner;
        // the cues flipped.
        view.settings.showPaceTicks = true;
        view.settings.sparklineWidth = 80;
        view.settings.sparklineHeight = 65;
        view.settings.sparklineTextSize = 12;
        view.settings.sparklineExtra = SparklineExtra::Percent;
        view.settings.sparklineElapsed = view.settings.sparklineNowLine = false;
        view.settings.sparklineNowCaret = view.settings.sparklineRing = true;
        view.settings.sparklineOutline = false;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::SparklineWidth, 1).action.value == 80 &&
              Control(frame, SettingsField::SparklineWidth).text == L"80");
        CHECK(Control(frame, SettingsField::SparklineHeight, 1).action.value == 65 &&
              Control(frame, SettingsField::SparklineHeight).text == L"65");
        CHECK(Control(frame, SettingsField::SparklineExtra).text == L"Percent");
        CHECK(Control(frame, SettingsField::SparklineTextSize, 1).action.value == 12 &&
              Control(frame, SettingsField::SparklineTextSize).text == L"12");
        CHECK(!Control(frame, SettingsField::SparklineElapsed).selected && !Control(frame, SettingsField::SparklineNowLine).selected &&
              Control(frame, SettingsField::SparklineNowCaret).selected && Control(frame, SettingsField::SparklineRing).selected &&
              Control(frame, SettingsField::SparklineRing).action.value == 0);
        CHECK(!Control(frame, SettingsField::SparklineOutline).selected &&
              Control(frame, SettingsField::SparklineOutline).action.value == 1);

        // Quota matrix: the number size, then the identity; no pace markers or other sizes.
        view = TestView(SettingsUiPage::Layout);
        view.settings.barLayout = BarLayout::QuotaMatrix;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::BarLayout, 9).selected);
        CHECK(section(frame, L"Matrix") && section(frame, L"Identity") && section(frame, L"Spacing") &&
              !section(frame, L"Pace markers") && !section(frame, L"Usage graph") && !section(frame, L"Headline"));
        const UiItem& number = Control(frame, SettingsField::HeadlineFontSize, 1);
        CHECK(number.kind == UiItemKind::Slider && number.minimum == 10 && number.maximum == 24 && number.action.value == 14 &&
              number.enabled && Control(frame, SettingsField::HeadlineFontSize).text == L"14");
        CHECK(HasText(frame, L"Number size") &&
              HasText(frame, L"Text size of each quota\u2019s percentage, in pixels; its key scales with it. Two rows "
                             L"shrink to fit the taskbar."));
        CHECK(number.rect.bottom < Control(frame, SettingsField::BarLayout, 10).rect.top &&
              Control(frame, SettingsField::BarLayout, 10).action.kind == UiActionKind::Navigate &&
              Control(frame, SettingsField::LogoScale, 1).enabled);
        absent(frame, {{SettingsField::ShowPaceTicks, 0}, {SettingsField::PaceTickColor, 1}, {SettingsField::PaceTickStyle, 0},
                       {SettingsField::SparklineWidth, 1}, {SettingsField::SparklineHeight, 1}, {SettingsField::SparklineExtra, 0},
                       {SettingsField::SparklineTextSize, 1}, {SettingsField::SparklineElapsed, 0},
                       {SettingsField::SparklineNowLine, 0}, {SettingsField::SparklineNowCaret, 0}, {SettingsField::SparklineRing, 0},
                       {SettingsField::SparklineOutline, 0}, {SettingsField::RingSize, 1}, {SettingsField::RingThickness, 1},
                       {SettingsField::HeadlineBarLength, 1}, {SettingsField::HeadlineCountdown, 0}, {SettingsField::BarGap, 0}});
        CHECK(!HasText(frame, L"Pace markers"));
        // The chart's width, height and corner are the sparkline's alone.
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::LogoHeadline, BarLayout::Gauge, BarLayout::ClockPies,
                                 BarLayout::SegmentedRing}) {
            view.settings.barLayout = layout;
            frame = {};
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            absent(frame, {{SettingsField::SparklineWidth, 0}, {SettingsField::SparklineWidth, 1},
                           {SettingsField::SparklineHeight, 0}, {SettingsField::SparklineHeight, 1},
                           {SettingsField::SparklineTextSize, 1}, {SettingsField::SparklineExtra, 0},
                           {SettingsField::SparklineElapsed, 0}, {SettingsField::SparklineNowLine, 0},
                           {SettingsField::SparklineNowCaret, 0}, {SettingsField::SparklineRing, 0},
                           {SettingsField::SparklineOutline, 0}});
            CHECK(!HasText(frame, L"Current position"));
        }
    });

    Run("layout page: Reset countdown on headline tiles disables the bar cards with its reason", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::Layout);
        view.settings.barLayout = BarLayout::LogoHeadline;
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        const PCWSTR reason = L"Turn off Reset countdown to change this.";
        // Off: a toggle after the number size, before the bars' cards, all enabled.
        const UiItem& toggle = Control(frame, SettingsField::HeadlineCountdown);
        CHECK(toggle.kind == UiItemKind::Toggle && !toggle.selected && toggle.enabled && toggle.action.value == 1);
        CHECK(HasText(frame, L"Reset countdown") &&
              HasText(frame, L"Time until that quota resets under the number, instead of each quota\u2019s bar."));
        CHECK(Control(frame, SettingsField::HeadlineFontSize, 1).rect.bottom < toggle.rect.top &&
              toggle.rect.bottom < Control(frame, SettingsField::HeadlineBarLength, 1).rect.top);
        for (auto [field, part] : {std::pair{SettingsField::HeadlineBarLength, 0u}, std::pair{SettingsField::HeadlineBarLength, 1u},
                                   std::pair{SettingsField::HeadlineBarThickness, 0u},
                                   std::pair{SettingsField::HeadlineBarThickness, 1u}, std::pair{SettingsField::BarGap, 0u}}) {
            CHECK(Control(frame, field, part).enabled);
        }
        CHECK(!HasText(frame, reason));
        // On: the bars' length, thickness and gap disabled with the reason; the number size, the
        // toggle and the identity cards stay.
        view.settings.headlineCountdown = true;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::HeadlineCountdown).selected && Control(frame, SettingsField::HeadlineCountdown).enabled &&
              Control(frame, SettingsField::HeadlineCountdown).action.value == 0);
        for (auto [field, part] : {std::pair{SettingsField::HeadlineBarLength, 0u}, std::pair{SettingsField::HeadlineBarLength, 1u},
                                   std::pair{SettingsField::HeadlineBarThickness, 0u},
                                   std::pair{SettingsField::HeadlineBarThickness, 1u}, std::pair{SettingsField::BarGap, 0u}}) {
            CHECK(!Control(frame, field, part).enabled);
        }
        CHECK(HasText(frame, reason));
        CHECK(Control(frame, SettingsField::HeadlineFontSize, 1).enabled && Control(frame, SettingsField::LogoScale, 1).enabled &&
              Control(frame, SettingsField::AccountMargin).enabled);
        // Other tile styles have no countdown card, whatever the setting.
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::SegmentedRing, BarLayout::Gauge}) {
            view.settings.barLayout = layout;
            frame = {};
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::HeadlineCountdown)) && !HasText(frame, reason));
            if (layout == BarLayout::Stacked) CHECK(Control(frame, SettingsField::BarGap).enabled);
        }
    });

    Run("layout page: Time track is the bars' fifth marker style", [] {
        UiGraphics& g = TestGraphics();
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical}) {
            SettingsView view = TestView(SettingsUiPage::Layout);
            view.settings.barLayout = layout;
            view.settings.paceTickStyle = PaceTickStyle::TimeTrack;
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            const UiItem& style = Control(frame, SettingsField::PaceTickStyle);
            CHECK(style.kind == UiItemKind::Dropdown && style.text == L"Time track" && style.enabled);
            CHECK(style.options.size() == 5 && style.options[3].text == L"Dot" && style.options[4].text == L"Time track" &&
                  style.options[4].value == int(PaceTickStyle::TimeTrack));
            CHECK(HasText(frame, L"centered dot, or a time track: a small bar under each bar that fills with the "
                                 L"elapsed time."));
            // Off: disabled with the markers' reason.
            view.settings.showPaceTicks = false;
            frame = {};
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            CHECK(!Control(frame, SettingsField::PaceTickStyle).enabled && HasText(frame, L"Turn on Pace markers to change this."));
        }
        // Chosen through the controller: committed, and the preview draws rails instead of carets.
        TestStore store;
        store.settings = StackedSettings();
        SettingsEdits edits(store);
        edits.Reload();
        CHECK(edits.Commit(0, SettingsField::PaceTickStyle, int(PaceTickStyle::TimeTrack)) == 0);
        CHECK(store.settings.paceTickStyle == PaceTickStyle::TimeTrack);
        SettingsPreviewStrip strip = Strip(edits.Projection());
        CHECK(!OpsOfColor(strip, 0xFFDEDEDE).empty() && OpsOfKind(strip, SettingsPreviewOp::Kind::Polygon).empty());
        // The Layout reset brings back the caret.
        CHECK(edits.Reset(SettingsField::ResetLayout) == 0 && store.settings.paceTickStyle == PaceTickStyle::Caret);
    });

    Run("layout header: preview pinned under the title, unpinned when the viewport gets short", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::Layout);
        view.settings.barLayout = BarLayout::SegmentedRing;
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        UiSurfaceId surface = UiSurfaceId::Content;
        const UiItem* strip = UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll), &surface);
        CHECK(strip && strip->kind == UiItemKind::Preview && surface == UiSurfaceId::Main);
        // Card: 13 DIP to the 20 DIP header row, 10, the 48 DIP strip, 15; then 12 to the viewport.
        CHECK_NEAR(strip->rect.top, 56 + 13 + 20 + 10);
        CHECK_NEAR(strip->rect.bottom - strip->rect.top, 48);
        CHECK_NEAR(frame.viewport.top, 56 + 106 + 12);
        CHECK(frame.content.front().runs[0].text == L"Tile style");
        CHECK_NEAR(frame.content.front().rect.top, 16);
        CHECK(strip->maximum == 0 && HasTextIn(frame.main, L"Example accounts, updates as you change settings"));
        const UiItem* toggle = UiFindItem(frame, SettingsControlId(SettingsField::ShowOnTaskbar));
        CHECK(toggle && toggle->kind == UiItemKind::Toggle && !toggle->selected && toggle->action.value == 1 &&
              toggle->text.empty());

        // Wide stacked bars overflow: the note says so and the strip can scroll.
        view.settings.barLayout = BarLayout::Stacked;
        view.settings.barLength = 200;
        view.status.visualTest = true;
        view.previewScroll = 1e6f;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        strip = UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll));
        CHECK(strip->maximum > 0 && strip->action.value >= strip->maximum - 1 && strip->action.value <= strip->maximum);
        CHECK(HasTextIn(frame.main, L"Example accounts, wider than shown: drag to scroll"));
        CHECK(UiFindItem(frame, SettingsControlId(SettingsField::ShowOnTaskbar))->selected);

        // The narrowest window: the note and the switch's label give way to the title.
        view = TestView(SettingsUiPage::Layout);
        frame = {};
        SettingsUiBuildFrame(g, view, {484, 820}, frame);
        CHECK(!HasTextIn(frame.main, L"Example accounts") && !HasTextIn(frame.main, L"Show on taskbar"));
        const UiItem* title = nullptr;
        for (const UiItem& item : frame.main) {
            if (!item.runs.empty() && item.runs[0].text == L"Preview") title = &item;
        }
        CHECK(title && UiFindItem(frame, SettingsControlId(SettingsField::ShowOnTaskbar))->rect.left >= title->rect.right);

        // 440 DIP tall: the viewport would get 440 - 56 - 118 < 280, so the preview scrolls.
        view = TestView(SettingsUiPage::Layout);
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 440}, frame);
        strip = UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll), &surface);
        CHECK(strip && surface == UiSurfaceId::Content);
        CHECK_NEAR(frame.viewport.top, 56);
        CHECK_NEAR(strip->rect.top, 13 + 20 + 10);
        const UiItem* section = nullptr;
        for (const UiItem& item : frame.content) {
            if (!item.runs.empty() && item.runs[0].text == L"Tile style") section = &item;
        }
        CHECK(section && std::abs(section->rect.top - (106 + 12 + 16)) < 1e-3);
    });

    Run("logos: winding and even-odd fills agree (XAML path data fills even-odd)", [] {
        UiGraphics& g = TestGraphics();
        for (PCWSTR path : {kOpenAiLogoPath, kClaudeLogoPath, kGeminiLogoPath}) {
            winrt::com_ptr<ID2D1PathGeometry> evenOdd;
            winrt::check_hresult(g.d2d->CreatePathGeometry(evenOdd.put()));
            winrt::com_ptr<ID2D1GeometrySink> sink;
            winrt::check_hresult(evenOdd->Open(sink.put()));
            sink->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
            CHECK(UiParseSvgPath(path, sink.get()));
            winrt::check_hresult(sink->Close());
            ID2D1PathGeometry* winding = UiPathGeometry(g, path);
            int differ = 0;
            for (int y = 0; y < 96; y++) {
                for (int x = 0; x < 96; x++) {
                    D2D1_POINT_2F point{(float(x) + 0.5f) / 4, (float(y) + 0.5f) / 4};
                    BOOL a = FALSE, b = FALSE;
                    winding->FillContainsPoint(point, nullptr, 0.01f, &a);
                    evenOdd->FillContainsPoint(point, nullptr, 0.01f, &b);
                    differ += a != b;
                }
            }
            CHECK(differ == 0);
        }
    });
}
