// Layout page: slider input, the Layout fields and their reset group, the render projection the
// preview reads, the preview strip model (settings/preview.h, bar styles included), the page
// builder and the pinned preview header. Synthetic inputs, hand-derived expectations.

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
            {SettingsField::BarLayout, 3},     {SettingsField::BarStyle, 1},
            {SettingsField::BarLength, 180},  {SettingsField::BarThickness, 12},
            {SettingsField::BarGap, 5},        {SettingsField::RingSize, 36},    {SettingsField::RingThickness, 5},
            {SettingsField::LabelPosition, 2}, {SettingsField::LabelFontSize, 14}, {SettingsField::LabelGap, 7},
            {SettingsField::ShowBarLabels, 1}, {SettingsField::PercentFontSize, 10}, {SettingsField::ShowPaceTicks, 0},
            {SettingsField::PaceTickStyle, 2}, {SettingsField::PaceTickColor, 0x123456},
            {SettingsField::AccountMargin, 9}, {SettingsField::RightMargin, 11}};
        for (auto [field, value] : values) {
            SettingsAssign(s, field, value);
            CHECK(SettingsFieldValue(s, field) == value);
        }
        CHECK(s.barLayout == BarLayout::ConcentricRings && s.barStyle == BarStyle::Rail &&
              s.labelPosition == LabelPosition::Top &&
              s.paceTickStyle == PaceTickStyle::Notch && s.paceTickColor == 0x123456);
        const std::tuple<SettingsField, int, int> clamps[] = {
            {SettingsField::BarLength, 99999, 500},   {SettingsField::BarThickness, 0, 2},
            {SettingsField::RingSize, 100, 40},       {SettingsField::RingThickness, -3, 1},
            {SettingsField::LabelFontSize, 100, 24},  {SettingsField::PercentFontSize, 1, 6},
            {SettingsField::BarGap, -1, 0},           {SettingsField::AccountMargin, 501, 500},
            {SettingsField::BarLayout, 9, 4},         {SettingsField::BarLayout, -1, 0},
            {SettingsField::LabelPosition, 7, 4},     {SettingsField::PaceTickStyle, 9, 3},
            {SettingsField::BarStyle, 5, 1},          {SettingsField::BarStyle, -1, 0},
            {SettingsField::PaceTickColor, 0x12345678, 0x345678}};
        for (auto [field, value, expected] : clamps) {
            SettingsAssign(s, field, value);
            CHECK(SettingsFieldValue(s, field) == expected);
        }
    });

    Run("layout reset: the seventeen Layout fields back to defaults, nothing else", [] {
        Settings changed;
        changed.barLayout = BarLayout::LogoHeadline;
        changed.barStyle = BarStyle::Rail;
        changed.barLength = 220;
        changed.barThickness = 14;
        changed.barGap = 6;
        changed.ringSize = 38;
        changed.ringThickness = 5;
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
        CHECK(SettingsGroupFields(SettingsField::ResetLayout).size() == 17);
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
        Settings s;
        s.barLayout = BarLayout::SegmentedRing;
        SettingsPreviewStrip strip = Strip(s);
        // Four 4 + 32 + 4 DIP tiles with 3 DIP margins; tiles 40 tall on a 48 DIP taskbar.
        CHECK(strip.columns.size() == 4 && strip.height == 48);
        CHECK_NEAR(strip.width, 4 * (40 + 6));
        CHECK_NEAR(strip.columns[0].left, 3);
        CHECK_NEAR(strip.columns[1].left, 49);
        CHECK_NEAR(strip.columns[0].top, 4);
        CHECK_NEAR(strip.columns[0].bottom, 44);
        std::vector<int> hovers;
        for (const SettingsPreviewOp& op : strip.ops) {
            if (op.hover >= 0) hovers.push_back(op.hover);
        }
        CHECK((hovers == std::vector<int>{0, 1, 2, 3}));
        CHECK(OpsOfColor(strip, TilePaletteFor(false).hover).size() == 4);
        // A 32 DIP taskbar: 24 DIP tiles, rings capped to them.
        strip = Strip(s, 32);
        CHECK(strip.height == 32);
        CHECK_NEAR(strip.width, 4 * (32 + 6));
        CHECK_NEAR(strip.columns[0].top, 4);
        CHECK(Strip(s, 0).height == 48);
        // Gallery thumbnails: the two Anthropic accounts, no tray.
        strip = Strip(s, 48, true);
        CHECK(strip.columns.size() == 2 && strip.tray.empty() && strip.trayWidth == 0);
        CHECK_NEAR(strip.width, 2 * 46);
        CHECK(!Strip(s).tray.empty() && Strip(s).trayWidth > 100);
    });

    Run("preview: bars mirror the XAML columns; percent text follows its visibility", [] {
        Settings s;
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
        // Left labels: 7 DIPs into the box, the label, the label gap, then the 100 DIP bars, 7 DIPs
        // before its end.
        s = {};
        strip = Strip(s);
        const SettingsPreviewOp label = FirstText(strip);
        CHECK(label.text == L"OAI");
        CHECK_NEAR(label.rect.left, strip.columns[0].left + 7);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left, label.rect.right + 3);
        CHECK_NEAR(strip.columns[0].right - strip.columns[0].left, 7 + label.rect.right - label.rect.left + 3 + 100 + 7);
        // Quota keys add a ceil(9 * 1.4) + 3 DIP column before each track.
        s.showBarLabels = true;
        strip = Strip(s);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left - FirstText(strip).rect.right, 3 + 16);
        // Hidden labels: just the bars in the box.
        s = {};
        s.labelPosition = LabelPosition::Hidden;
        strip = Strip(s);
        CHECK_NEAR(strip.columns[0].right - strip.columns[0].left, 7 + 100 + 7);
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
        // hovered; the 3 DIP account margins outside it.
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical}) {
            Settings s;
            s.barLayout = layout;
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
            CHECK_NEAR(strip.columns[0].left, 3);
            for (size_t i = 1; i < 4; i++) CHECK_NEAR(strip.columns[i].left - strip.columns[i - 1].right, 6);
            CHECK_NEAR(strip.width, strip.columns[3].right + 3);
        }

        // Stacked: the tile height, 40 DIPs on a 48 DIP taskbar (CC 2's four bars just fill it:
        // 4 * 8 + 3 * 2 + 2 DIPs), the content centred across it.
        Settings s;
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

        // The gallery's Anthropic pair: their logos badged "1" and "2", the bars after the badge's
        // 19 DIP box and the 3 DIP gap; no label.
        s = {};
        strip = Strip(s, 48, true);
        std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        CHECK(logos.size() == 2 && OpsOfColor(strip, TilePaletteFor(false).badge).size() == 2 &&
              OpsOfColor(strip, hover).size() == 2);
        CHECK(TextOps(strip, L"1").size() == 1 && TextOps(strip, L"2").size() == 1 && TextOps(strip, L"CC 1").empty());
        CHECK_NEAR(logos[0].rect.left, strip.columns[0].left + 7);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left, strip.columns[0].left + 7 + 19 + 3);
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
        const uint32_t badgeColor = TilePaletteFor(false).badge;
        for (LabelPosition position : {LabelPosition::Left, LabelPosition::Right, LabelPosition::Top,
                                       LabelPosition::Bottom, LabelPosition::Hidden}) {
            Settings s;
            s.labelPosition = position;
            SettingsPreviewStrip strip = StripOf(s, accounts, data);
            CHECK(strip.columns.size() == 3);
            const D2D1_RECT_F cc1 = strip.columns[0], cc2 = strip.columns[1], oai = strip.columns[2];
            std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
            std::vector<SettingsPreviewOp> badges = OpsOfColor(strip, badgeColor);
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
            // The badge: a 10 DIP pill on the logo's lower-right corner, 4 DIPs past it.
            CHECK(badges.size() == 1 && TextOps(strip, L"1").size() == 1);
            CHECK_NEAR(badges[0].rect.right, logos[0].rect.right + 4);
            CHECK_NEAR(badges[0].rect.bottom, logos[0].rect.bottom + 4);
            CHECK_NEAR(badges[0].rect.bottom - badges[0].rect.top, 10);
            // The label in the label font at 0.8.
            CHECK(labels.size() == 1 && labels[0].style.size == 11 && labels[0].style.family == UiFamily::Taskbar);
            CHECK_NEAR(labels[0].opacity, 0.8);
            const SettingsPreviewOp& label = labels[0];
            const float labelWidth = label.rect.right - label.rect.left;
            switch (position) {
                case LabelPosition::Left:
                    // CC 1: its 19 DIP box (logo and badge) 7 DIPs into the 40 DIP box, centred across
                    // the 20 DIP bars (y 14 to 34), then the 3 DIP gap from the badge.
                    CHECK_NEAR(cc1.top, 4); CHECK_NEAR(cc1.bottom, 44);
                    CHECK_NEAR(logos[0].rect.left, 10); CHECK_NEAR(logos[0].rect.top, 14.5);
                    CHECK_NEAR(tracks[0].rect.left, badges[0].rect.right + 3); CHECK_NEAR(tracks[0].rect.left, 32);
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
                    CHECK_NEAR(logos[0].rect.left, tracks[0].rect.right + 3); CHECK_NEAR(logos[0].rect.top, 14.5);
                    CHECK_NEAR(cc1.right, badges[0].rect.right + 7);
                    CHECK_NEAR(label.rect.left, tracks[2].rect.right + 3);
                    CHECK_NEAR(cc2.right, label.rect.right + 7);
                    CHECK_NEAR(logos[1].rect.left, tracks[4].rect.right + 3);
                    CHECK_NEAR(oai.right, logos[1].rect.right + 7);
                    break;
                case LabelPosition::Top: {
                    // Centred over the bars, a logo's box padded like the label's line: its space
                    // over the cap height above, under the baseline below, then the gap to the bars'
                    // panel (a DIP above the first track). CC 1: 19 + 4.17 + 2.76 + 3 + 20 DIPs grow
                    // its column, centred on the strip.
                    const double a = AboveCap(11), b = BelowBaseline(11);
                    CHECK(NearDip(cc1.bottom - cc1.top, 19 + a + b + 3 + 20) && NearDip(cc1.top + cc1.bottom, 48));
                    CHECK(NearDip(logos[0].rect.top, cc1.top + a));
                    CHECK_NEAR(logos[0].rect.left, 10 + (100 - 19) / 2.0);
                    CHECK(NearDip(tracks[0].rect.top - 1 - badges[0].rect.bottom, 3 + b));
                    CHECK_NEAR(tracks[0].rect.left, 10); CHECK_NEAR(cc1.right, 10 + 100 + 7);
                    // CC 2's label as ever, its line box a + cap + b; the bars as far from its
                    // baseline as from CC 1's badge.
                    CHECK_NEAR(label.rect.left, cc2.left + 7 + (100 - labelWidth) / 2);
                    CHECK_NEAR(tracks[2].rect.top - 1, label.rect.bottom + 3);
                    CHECK(NearDip(label.rect.bottom - label.rect.top, a + CapHeight(11) + b));
                    CHECK(NearDip(tracks[2].rect.top - (label.rect.bottom - b), tracks[0].rect.top - badges[0].rect.bottom));
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
                    CHECK(NearDip(cc1.bottom - badges[0].rect.bottom, b));
                    CHECK_NEAR(logos[0].rect.left, 10 + (100 - 19) / 2.0);
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
        Settings s;
        s.labelGap = 0;
        SettingsPreviewStrip strip = StripOf(s, accounts, data);
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
        CHECK_NEAR(tracks[0].rect.left, OpsOfColor(strip, badgeColor)[0].rect.right);
        CHECK_NEAR(tracks[4].rect.left, OpsOfKind(strip, SettingsPreviewOp::Kind::Logo)[1].rect.right);
        s.labelPosition = LabelPosition::Top;
        strip = StripOf(s, accounts, data);
        CHECK(NearDip(OpsOfColor(strip, 0x46808080)[0].rect.top - 1 - OpsOfColor(strip, badgeColor)[0].rect.bottom,
                      BelowBaseline(11)));
        // A wider gap moves every form's bars alike.
        s = {};
        s.labelGap = 9;
        strip = StripOf(s, accounts, data);
        tracks = OpsOfColor(strip, 0x46808080);
        CHECK_NEAR(tracks[0].rect.left, OpsOfColor(strip, badgeColor)[0].rect.right + 9);
        CHECK_NEAR(tracks[2].rect.left, TextOps(strip, L"CC 2")[0].rect.right + 9);
        // Tiles show the Label form's short text instead.
        s = {};
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
        const uint32_t badgeColor = TilePaletteFor(false).badge;
        Settings s;
        s.barLayout = BarLayout::Vertical;
        s.barLength = 32;
        auto center = [](const SettingsPreviewOp& a, const SettingsPreviewOp& b) {
            return (a.rect.left + b.rect.right) / 2;
        };

        // Left: CC 1's 19 DIP box, the gap, the bars, inside the 40 DIP box (bars y 8 to 40).
        SettingsPreviewStrip strip = StripOf(s, accounts, data);
        std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
        SettingsPreviewOp badge = OpsOfColor(strip, badgeColor)[0];
        SettingsPreviewOp label = TextOps(strip, L"CC 2")[0];
        D2D1_RECT_F cc1 = strip.columns[0];
        CHECK_NEAR(cc1.top, 4); CHECK_NEAR(cc1.bottom, 44);
        CHECK_NEAR(logos[0].rect.left, 10); CHECK_NEAR(logos[0].rect.top, 14.5);
        CHECK_NEAR(tracks[0].rect.left, badge.rect.right + 3); CHECK_NEAR(tracks[0].rect.top, 8);
        CHECK_NEAR(tracks[1].rect.left, tracks[0].rect.right + 2);
        CHECK_NEAR(cc1.right, tracks[1].rect.right + 7);
        CHECK_NEAR(tracks[2].rect.left, label.rect.right + 3);
        CHECK_NEAR(tracks[4].rect.left, logos[1].rect.right + 3);

        // Top: centred over the bars and the bars under them, whichever is wider; a logo's box
        // padded like the label's line. CC 1: 19 + 4.17 + 2.76 + 3 + 32 DIPs grow its column,
        // centred on the strip.
        const double a = AboveCap(11), b = BelowBaseline(11);
        s.labelPosition = LabelPosition::Top;
        strip = StripOf(s, accounts, data);
        logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        tracks = OpsOfColor(strip, 0x46808080);
        badge = OpsOfColor(strip, badgeColor)[0];
        label = TextOps(strip, L"CC 2")[0];
        cc1 = strip.columns[0];
        CHECK(NearDip(cc1.bottom - cc1.top, 19 + a + b + 3 + 32) && NearDip(cc1.top + cc1.bottom, 48));
        CHECK_NEAR(cc1.right - cc1.left, 7 + 19 + 7);
        CHECK_NEAR(logos[0].rect.left, 10); CHECK(NearDip(logos[0].rect.top, cc1.top + a));
        CHECK(NearDip(tracks[0].rect.top - badge.rect.bottom, 3 + b));
        CHECK_NEAR(tracks[0].rect.left, 10.5);
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
        CHECK(NearDip(strip.columns[0].bottom - OpsOfColor(strip, badgeColor)[0].rect.bottom, b));
        CHECK_NEAR(TextOps(strip, L"CC 2")[0].rect.top, tracks[2].rect.bottom + 3);
        CHECK(NearDip(logos[1].rect.top - tracks[4].rect.bottom, 3 + a));

        // Right and Hidden.
        s.labelPosition = LabelPosition::Right;
        strip = StripOf(s, accounts, data);
        logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
        tracks = OpsOfColor(strip, 0x46808080);
        CHECK_NEAR(logos[0].rect.left, tracks[1].rect.right + 3);
        CHECK_NEAR(strip.columns[0].right, OpsOfColor(strip, badgeColor)[0].rect.right + 7);
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
        const uint32_t badgeColor = TilePaletteFor(false).badge;

        // Stale with an error: "!" on each logo's top-right corner, 4 DIPs past it, dimmed with it;
        // the label ends in "!" instead.
        Settings s;
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
        CHECK_NEAR(OpsOfColor(strip, badgeColor)[0].opacity, 0.45);
        // The mark counts in the box while shown: 4 DIPs right (the badge's own), 3 above. CC 1:
        // 19 x 22 DIPs, centred across the 40 DIP box (y 13 to 35), its bars centred on it.
        CHECK_NEAR(logos[0].rect.top, 13 + 3);
        CHECK_NEAR(tracks[0].rect.left, logos[0].rect.right + 4 + 3); CHECK_NEAR(tracks[0].rect.top, 15);
        // OAI: 19 x 18, within its bars' 20 (y 14 to 34): the gap from the mark.
        CHECK_NEAR(logos[1].rect.top, 15 + 3);
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
        s = {};
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
                s = {};
                s.barLayout = layout;
                s.labelPosition = position;
                strip = StripOf(s, accounts, data);
                CHECK(OpsOfColor(strip, 0x46808080).empty());
                logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
                const SettingsPreviewOp label = TextOps(strip, L"CC 2")[0];
                CHECK_NEAR(strip.columns[0].right - strip.columns[0].left, 7 + 19 + 7);
                CHECK_NEAR(strip.columns[1].right - strip.columns[1].left, 7 + label.rect.right - label.rect.left + 7);
                CHECK_NEAR(strip.columns[2].right - strip.columns[2].left, 7 + 15 + 7);
                CHECK_NEAR(logos[0].rect.left, strip.columns[0].left + 7);
                const bool across = position == LabelPosition::Top || position == LabelPosition::Bottom;
                const double a = across ? AboveCap(11) : 0, b = across ? BelowBaseline(11) : 0;
                CHECK(NearDip(logos[0].rect.top, 4 + (40 - 19 - a - b) / 2 + a));
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
        const uint32_t badgeColor = TilePaletteFor(false).badge;
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
                    Settings s;
                    s.labelFontSize = size;
                    s.labelPosition = position;
                    SettingsPreviewStrip strip = StripOf(s, accounts, data);
                    std::vector<SettingsPreviewOp> logos = OpsOfKind(strip, SettingsPreviewOp::Kind::Logo);
                    std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, 0x46808080);
                    const SettingsPreviewOp badge = OpsOfColor(strip, badgeColor)[0];
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
                        CHECK(NearDip(tracks[0].rect.top - 1 - badge.rect.bottom, 3 + b));
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
                        CHECK(NearDip(strip.columns[0].bottom - badge.rect.bottom, b));
                        CHECK(NearDip(strip.columns[2].bottom - logos[1].rect.bottom, b));
                    }
                }
            }
        }
        // Beside the bars nothing changes with the label size: CC 1's box is 19 DIPs, centred.
        Settings s;
        s.labelFontSize = 15;
        std::vector<AccountData> data(3, Fresh({30, 40, -1, -1}));
        SettingsPreviewStrip strip = StripOf(s, accounts, data);
        CHECK_NEAR(OpsOfKind(strip, SettingsPreviewOp::Kind::Logo)[0].rect.top, 14.5);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left, OpsOfColor(strip, badgeColor)[0].rect.right + 3);
    });

    Run("preview: pace marks per layout; none on headline tiles", [] {
        const uint32_t pace = 0xFF000000 | 0xDEDEDE;
        Settings s;
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
        Settings s;
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
        s = {};
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
        s = {};
        s.barLayout = BarLayout::Vertical;
        strip = Strip(s);
        shaded = fills(strip);
        CHECK(shaded[0].shadeLeft && (*shaded[0].corners == Corners{0, 0, 2, 2}));
        CHECK_NEAR(shaded[0].rect.bottom - shaded[0].rect.top, 25);
        CHECK_NEAR(shaded[0].rect.bottom, OpsOfColor(strip, 0x46808080)[0].rect.bottom);

        // Thin rail: a 2 DIP rail centred across the 8 DIP bar, fills rounded at both ends, ticks
        // within the bar.
        s = {};
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
        Settings s;
        s.barLayout = BarLayout::SegmentedRing;
        SettingsPreviewStrip strip = Strip(s);
        // Ten quotas on four tiles: a track shape each, then a flat fill shape per quota in use.
        std::vector<SettingsPreviewOp> tracks = OpsOfColor(strip, track);
        CHECK(tracks.size() == 10 && shaded(strip).empty());
        CHECK(std::all_of(tracks.begin(), tracks.end(), [](const SettingsPreviewOp& op) {
            return op.kind == SettingsPreviewOp::Kind::Shape;
        }));
        // OAI's ring area: 4 DIPs into its column, centred on the 40 DIP tile; the model's shapes.
        std::vector<AccountConfig> accounts;
        std::vector<AccountData> data;
        BuildVisualTestSnapshot(Display(s), &accounts, &data);
        AccountMeters oai = AccountMeterState(Display(s), BarLayout::SegmentedRing, accounts[0], data[0], 0, 0);
        CHECK(roughly(tracks[0].origin.x, strip.columns[0].left + 4) && roughly(tracks[0].origin.y, 8));
        CHECK(tracks[0].shape.size() == 1 && tracks[0].shape[0].segments.size() == 9);
        CHECK_NEAR(tracks[0].shape[0].x, oai.quotas[0].track[0].x);
        CHECK_NEAR(tracks[0].shape[0].segments[4].y, oai.quotas[0].track[0].segments[4].y);
        auto green = std::find_if(strip.ops.begin(), strip.ops.end(), [](const SettingsPreviewOp& op) {
            return op.color == 0xFF43A047;
        });
        CHECK(green != strip.ops.end() && green->kind == SettingsPreviewOp::Kind::Shape && green->shade == 0);
        CHECK_NEAR(green->shape[0].x, oai.quotas[0].fill[0].x);

        // Headline segments: 9 x 3 tracks with 0.75 DIP corners, fills shaded and cut flat, no ticks.
        s.barLayout = BarLayout::LogoHeadline;
        strip = Strip(s);
        tracks = OpsOfColor(strip, track);
        std::vector<SettingsPreviewOp> fills = shaded(strip);
        CHECK(tracks.size() == 10 && fills.size() == 10);
        CHECK_NEAR(tracks[0].radius, 0.75); CHECK_NEAR(tracks[0].rect.right - tracks[0].rect.left, 9);
        // OAI's 5-hour quota: 25% of 9 DIPs, cut flat; CC 2's weekly one fills the segment.
        CHECK(roughly(fills[0].rect.right - fills[0].rect.left, 2.25) && (*fills[0].corners == Corners{0.75f, 0, 0, 0.75f}));
        CHECK(fills[0].shade == UsageHighlight(fills[0].color));
        auto full = std::find_if(fills.begin(), fills.end(), [&](const SettingsPreviewOp& op) { return op.color == 0xFFE53935; });
        CHECK(full != fills.end() && roughly(full->rect.right - full->rect.left, 9) &&
              (*full->corners == Corners{0.75f, 0.75f, 0.75f, 0.75f}));
        CHECK(OpsOfColor(strip, 0xC0000000).empty() && OpsOfColor(strip, 0xFFDEDEDE).empty());
    });

    Run("preview: the default pace color turns dark with a light halo on a light taskbar", [] {
        auto strip = [](const Settings& settings, bool light) {
            return SettingsPreviewLayout(TestGraphics(), settings, 48, 1, kNow, false, light,
                                         light ? 0xE4000000 : 0xFFFFFFFF);
        };
        Settings s;
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
        s = {};
        for (BarLayout layout : {BarLayout::SegmentedRing, BarLayout::ConcentricRings}) {
            s.barLayout = layout;
            size_t ticks = OpsOfColor(strip(s, false), 0xFFDEDEDE).size();
            lightStrip = strip(s, true);
            CHECK(ticks == 8 && OpsOfColor(lightStrip, 0xFFDEDEDE).empty());
            CHECK(OpsOfColor(lightStrip, 0xFF1A1A1A).size() == ticks && OpsOfColor(lightStrip, 0x80FFFFFF).size() == ticks);
            CHECK(OpsOfColor(strip(s, false), 0xC0000000).size() == ticks);
        }
    });

    Run("controller: the bar style commits as chosen and the preview follows", [] {
        TestStore store;
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

    Run("preview: extreme and out-of-range settings stay drawable and bounded", [] {
        Settings large;
        large.barLength = 500;
        large.barThickness = 50;
        large.labelFontSize = large.percentFontSize = 24;
        large.barGap = large.labelGap = large.accountMargin = large.rightMargin = 500;
        large.showBarLabels = true;
        large.percentTextVisibility = PercentTextVisibility::Always;
        Settings wild = large;
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
            for (int layout = 0; layout <= int(BarLayout::LogoHeadline); layout++) {
                Settings s = base;
                if (&base == &large || layout) s.barLayout = BarLayout(layout);
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
        Settings s;
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
        // Five gallery cards, three a row: the tile styles in BarLayout order.
        const PCWSTR styles[] = {L"Stacked bars", L"Vertical bars", L"Segmented ring", L"Concentric rings",
                                 L"Headline %"};
        for (int i = 0; i < 5; i++) {
            const UiItem& card = Control(frame, SettingsField::BarLayout, uint32_t(i));
            CHECK(card.kind == UiItemKind::Gallery && card.selected == (i == 0) && card.action.value == i);
            CHECK(card.text == styles[i]);
        }
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::BarLayout, 5)));
        CHECK(std::size(kSettingsTileStyles) == 5 && int(BarLayout::LogoHeadline) == 4);
        const UiItem& first = Control(frame, SettingsField::BarLayout, 0);
        const UiItem& fourth = Control(frame, SettingsField::BarLayout, 3);
        const UiItem& fifth = Control(frame, SettingsField::BarLayout, 4);
        CHECK_NEAR(fourth.rect.left, first.rect.left);
        CHECK_NEAR(fourth.rect.top - first.rect.top, (first.rect.bottom - first.rect.top) + 8);
        CHECK_NEAR(fifth.rect.top, fourth.rect.top);
        CHECK_NEAR(fifth.rect.left - fourth.rect.right, 8);
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
        const UiItem& identity = Control(frame, SettingsField::BarLayout, 8);
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
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::RingSize, 1)));
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
              stop(SettingsControlId(SettingsField::BarLayout, 8)) &&
              !stop(SettingsControlId(SettingsField::PreviewScroll)));
        // Vertical bars have it too.
        view.settings.barLayout = BarLayout::Vertical;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::BarLayout, 8).action.kind == UiActionKind::Navigate &&
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
        const UiItem& identity = Control(frame, SettingsField::BarLayout, 8);
        CHECK(identity.kind == UiItemKind::Card && identity.action.kind == UiActionKind::Navigate &&
              identity.action.value == int(SettingsUiPage::Accounts));
        CHECK(HasText(frame, L"Account identity is set per account in Accounts"));
        CHECK(HasText(frame, L"Ticks mark how much") && HasText(frame, L"dark on a light taskbar"));
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::BarLength, 1)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::BarStyle)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::PaceTickStyle)));
        CHECK(Control(frame, SettingsField::BarLayout, 3).selected);

        view.settings.barLayout = BarLayout::LogoHeadline;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::BarLayout, 4).selected);
        CHECK(HasText(frame, L"Headline % tiles have a fixed size") &&
              HasText(frame, L"Bar sizes and the identity position apply to Stacked and Vertical bars."));
        CHECK(Control(frame, SettingsField::BarLayout, 8).action.kind == UiActionKind::Navigate);
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
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(Control(frame, SettingsField::LabelPosition).text == L"Hidden");
        CHECK(!Control(frame, SettingsField::LabelFontSize, 1).enabled && !Control(frame, SettingsField::LabelFontSize).enabled);
        CHECK(!Control(frame, SettingsField::LabelGap).enabled);
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
