// Layout page: slider input, the Layout fields and their reset group, the render projection the
// preview reads, the preview strip model (settings/preview.h), the page builder and the pinned
// preview header. Synthetic inputs, hand-derived expectations.

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

static bool Finite(const SettingsPreviewStrip& strip) {
    auto finite = [](D2D1_RECT_F r) {
        return std::isfinite(r.left) && std::isfinite(r.top) && std::isfinite(r.right) && std::isfinite(r.bottom);
    };
    for (const auto* ops : {&strip.ops, &strip.tray}) {
        for (const SettingsPreviewOp& op : *ops) {
            if (!finite(op.rect) || !std::isfinite(op.radius) || !std::isfinite(op.sweep)) return false;
            for (D2D1_POINT_2F p : op.points) {
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) return false;
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
            {SettingsField::BarLayout, 3},     {SettingsField::BarLength, 180},  {SettingsField::BarThickness, 12},
            {SettingsField::BarGap, 5},        {SettingsField::RingSize, 36},    {SettingsField::RingThickness, 5},
            {SettingsField::LabelPosition, 2}, {SettingsField::LabelFontSize, 14}, {SettingsField::LabelGap, 7},
            {SettingsField::ShowBarLabels, 1}, {SettingsField::PercentFontSize, 10}, {SettingsField::ShowPaceTicks, 0},
            {SettingsField::PaceTickStyle, 2}, {SettingsField::PaceTickColor, 0x123456},
            {SettingsField::AccountMargin, 9}, {SettingsField::RightMargin, 11}};
        for (auto [field, value] : values) {
            SettingsAssign(s, field, value);
            CHECK(SettingsFieldValue(s, field) == value);
        }
        CHECK(s.barLayout == BarLayout::ConcentricRings && s.labelPosition == LabelPosition::Top &&
              s.paceTickStyle == PaceTickStyle::Notch && s.paceTickColor == 0x123456);
        const std::tuple<SettingsField, int, int> clamps[] = {
            {SettingsField::BarLength, 99999, 500},   {SettingsField::BarThickness, 0, 2},
            {SettingsField::RingSize, 100, 40},       {SettingsField::RingThickness, -3, 1},
            {SettingsField::LabelFontSize, 100, 24},  {SettingsField::PercentFontSize, 1, 6},
            {SettingsField::BarGap, -1, 0},           {SettingsField::AccountMargin, 501, 500},
            {SettingsField::BarLayout, 9, 5},         {SettingsField::BarLayout, -1, 0},
            {SettingsField::LabelPosition, 7, 4},     {SettingsField::PaceTickStyle, 9, 3},
            {SettingsField::PaceTickColor, 0x12345678, 0x345678}};
        for (auto [field, value, expected] : clamps) {
            SettingsAssign(s, field, value);
            CHECK(SettingsFieldValue(s, field) == expected);
        }
    });

    Run("layout reset: the sixteen Layout fields back to defaults, nothing else", [] {
        Settings changed;
        changed.barLayout = BarLayout::LogoBars;
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
        changed.accounts.push_back(Account());
        Settings expected = changed;
        const Settings defaults;
        expected.barLayout = defaults.barLayout;
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
        CHECK(SettingsGroupFields(SettingsField::ResetLayout).size() == 16);
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
        // Left labels: label, the label gap, then the 100 DIP bars.
        s = {};
        strip = Strip(s);
        const SettingsPreviewOp& label = strip.ops[0];
        CHECK(label.kind == SettingsPreviewOp::Kind::Text && label.text == L"OAI");
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left, label.rect.right + 3);
        CHECK_NEAR(strip.columns[0].right - strip.columns[0].left, label.rect.right - label.rect.left + 3 + 100);
        // Quota keys add a ceil(9 * 1.4) + 3 DIP column before each track.
        s.showBarLabels = true;
        strip = Strip(s);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.left - strip.ops[0].rect.right, 3 + 16);
        // Hidden labels: just the bars.
        s = {};
        s.labelPosition = LabelPosition::Hidden;
        strip = Strip(s);
        CHECK_NEAR(strip.columns[0].right - strip.columns[0].left, 100);
        // Vertical bars stand 100 DIPs tall.
        s.barLayout = BarLayout::Vertical;
        strip = Strip(s);
        CHECK_NEAR(OpsOfColor(strip, 0x46808080)[0].rect.bottom - OpsOfColor(strip, 0x46808080)[0].rect.top, 100);
        CHECK_NEAR(strip.columns[0].bottom - strip.columns[0].top, 100);
    });

    Run("preview: pace marks per layout; none on headline tiles", [] {
        const uint32_t pace = 0xFF000000 | 0xDEDEDE;
        Settings s;
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical, BarLayout::SegmentedRing,
                                 BarLayout::ConcentricRings, BarLayout::LogoBars}) {
            s.barLayout = layout;
            CHECK(!OpsOfColor(Strip(s), pace).empty());
        }
        s.barLayout = BarLayout::LogoHeadline;
        SettingsPreviewStrip strip = Strip(s);
        CHECK(OpsOfColor(strip, pace).empty());
        CHECK(std::none_of(strip.ops.begin(), strip.ops.end(), [](const SettingsPreviewOp& op) {
            return op.kind == SettingsPreviewOp::Kind::Dot || op.kind == SettingsPreviewOp::Kind::Polygon;
        }));
        s.barLayout = BarLayout::Stacked;
        s.showPaceTicks = false;
        CHECK(OpsOfColor(Strip(s), pace).empty());
        // Carets are two polygons per mark; dots on rings carry a dark outline.
        s.showPaceTicks = true;
        strip = Strip(s);
        CHECK(std::count_if(strip.ops.begin(), strip.ops.end(), [](const SettingsPreviewOp& op) {
                  return op.kind == SettingsPreviewOp::Kind::Polygon;
              }) == 2 * int(OpsOfColor(strip, pace).size()));
        s.barLayout = BarLayout::SegmentedRing;
        for (const SettingsPreviewOp& dot : OpsOfColor(Strip(s), pace)) {
            CHECK(dot.kind == SettingsPreviewOp::Kind::Dot && dot.outline == 0x8C000000);
        }
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
        for (int i = 0; i < 6; i++) {
            const UiItem& card = Control(frame, SettingsField::BarLayout, uint32_t(i));
            CHECK(card.kind == UiItemKind::Gallery && card.selected == (i == 0) && card.action.value == i);
        }
        CHECK(Control(frame, SettingsField::BarLayout, 2).text == L"Segmented ring");
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
        const UiItem& position = Control(frame, SettingsField::LabelPosition);
        CHECK(position.text == L"Left" && position.options.size() == 5);
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
        for (PCWSTR text : {L"Tile style", L"Bars", L"Labels", L"Pace markers", L"Spacing", L"Ticks mark how much",
                            L"Reset Layout to defaults"}) {
            CHECK(HasText(frame, text));
        }
        // Gallery cards, sliders, swatches are tab stops; the preview strip isn't.
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        auto stop = [&](uint32_t id) {
            return std::any_of(stops.begin(), stops.end(), [&](const UiFocusStop& s) { return s.ref.id == id; });
        };
        CHECK(stop(SettingsControlId(SettingsField::BarLayout, 5)) && stop(SettingsControlId(SettingsField::BarLength, 1)) &&
              stop(SettingsControlId(SettingsField::PaceTickColor, 3)) &&
              !stop(SettingsControlId(SettingsField::PreviewScroll)));
    });

    Run("layout page: ring, logo + bars and headline sections", [] {
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
        CHECK(HasText(frame, L"Dots mark how much"));
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::BarLength, 1)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::PaceTickStyle)));
        CHECK(Control(frame, SettingsField::BarLayout, 3).selected);

        view.settings.barLayout = BarLayout::LogoBars;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(HasText(frame, L"Logo + bars tiles have a fixed size"));
        CHECK(Control(frame, SettingsField::BarLayout, 8).action.kind == UiActionKind::Navigate);
        CHECK(Control(frame, SettingsField::ShowPaceTicks).kind == UiItemKind::Toggle && HasText(frame, L"Ticks mark"));
        CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::PaceTickStyle)) &&
              !UiFindItem(frame, SettingsControlId(SettingsField::RingSize, 1)));

        view.settings.barLayout = BarLayout::LogoHeadline;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(HasText(frame, L"Headline % tiles have a fixed size"));
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
        CHECK(HasText(frame, L"Choose a label position to change this."));
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
        CHECK(strip->maximum == 0 && HasTextIn(frame.main, L"Sample data, updates as you change settings"));
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
        CHECK(HasTextIn(frame.main, L"Sample data, wider than shown: drag to scroll"));
        CHECK(UiFindItem(frame, SettingsControlId(SettingsField::ShowOnTaskbar))->selected);

        // The narrowest window: the note and the switch's label give way to the title.
        view = TestView(SettingsUiPage::Layout);
        frame = {};
        SettingsUiBuildFrame(g, view, {484, 820}, frame);
        CHECK(!HasTextIn(frame.main, L"Sample data") && !HasTextIn(frame.main, L"Show on taskbar"));
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
