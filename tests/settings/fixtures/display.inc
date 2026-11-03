// Display page: its fields and reset group, threshold ordering in the projection and after a
// save, the page builder per tile style, its pinned preview, and what the Display settings change
// in the preview strip (settings/preview.h). Synthetic inputs, hand-derived expectations.

static constexpr SettingsField kYellow = SettingsField::YellowThreshold;
static constexpr SettingsField kOrange = SettingsField::OrangeThreshold;
static constexpr SettingsField kRed = SettingsField::RedThreshold;

// Saving orders thresholds upward, each at least the one before (the production store's
// normalization).
static void OrderThresholds(Settings& s) {
    s.orangeThreshold = std::max(s.orangeThreshold, s.yellowThreshold);
    s.redThreshold = std::max(s.redThreshold, s.orangeThreshold);
}

static UiFrame DisplayFrame(const SettingsView& view, D2D1_SIZE_F client = {1000, 820}) {
    UiFrame frame;
    SettingsUiBuildFrame(TestGraphics(), view, client, frame);
    return frame;
}

static std::vector<const UiItem*> Dots(const UiFrame& frame) {
    std::vector<const UiItem*> dots;
    for (const UiItem& item : frame.content) {
        if (item.kind == UiItemKind::Swatch) dots.push_back(&item);
    }
    return dots;
}

static int TextCount(const UiFrame& frame, std::wstring_view text) {
    return int(std::count_if(frame.content.begin(), frame.content.end(),
                             [&](const UiItem& item) { return UiRunsText(item.runs) == text; }));
}

// The strip's percentage and amount texts (drawn at 0.9 opacity), in column and quota order.
static std::vector<SettingsPreviewOp> Values(const SettingsPreviewStrip& strip) {
    std::vector<SettingsPreviewOp> values;
    for (const SettingsPreviewOp& op : strip.ops) {
        if (op.kind == SettingsPreviewOp::Kind::Text && op.opacity == 0.9f) values.push_back(op);
    }
    return values;
}

static void RunDisplayPageTests() {
    Run("display fields: desired values, ranges and choices clamp", [] {
        Settings s;
        const std::pair<SettingsField, int> values[] = {
            {SettingsField::BarMode, 1},          {SettingsField::PercentTextVisibility, 2},
            {SettingsField::PercentTextAlignment, 3}, {SettingsField::ShowExtraBarAmounts, 1},
            {SettingsField::ColorblindMode, 1},   {kYellow, 30}, {kOrange, 60}, {kRed, 80},
            {SettingsField::ShowStaleWarning, 0}, {SettingsField::ShowOpenAiExtraLimits, 1}};
        for (auto [field, value] : values) {
            SettingsAssign(s, field, value);
            CHECK(SettingsFieldValue(s, field) == value);
        }
        CHECK(s.barMode == BarMode::Remaining && s.percentTextVisibility == PercentTextVisibility::Always &&
              s.percentTextAlignment == PercentTextAlignment::Right && s.showExtraBarAmounts && s.colorblindMode &&
              !s.showStaleWarning && s.showOpenAiExtraLimits);
        const std::tuple<SettingsField, int, int> clamps[] = {
            {kYellow, -5, 0}, {kRed, 101, 100}, {kOrange, 99999, 100},
            {SettingsField::BarMode, 7, 1}, {SettingsField::BarMode, -1, 0},
            {SettingsField::PercentTextVisibility, 9, 2}, {SettingsField::PercentTextVisibility, -3, 0},
            {SettingsField::PercentTextAlignment, 9, 3}};
        for (auto [field, value, expected] : clamps) {
            SettingsAssign(s, field, value);
            CHECK(SettingsFieldValue(s, field) == expected);
        }
        for (SettingsField field : {kYellow, kOrange, kRed}) {
            CHECK(SettingsFieldRange(field)->minimum == 0 && SettingsFieldRange(field)->maximum == 100);
        }
        CHECK(!SettingsFieldRange(SettingsField::BarMode));
    });

    Run("display reset: the ten Display fields back to defaults; every scalar setting in one page reset", [] {
        Settings changed;
        changed.barMode = BarMode::Remaining;
        changed.percentTextVisibility = PercentTextVisibility::Always;
        changed.percentTextAlignment = PercentTextAlignment::Center;
        changed.showExtraBarAmounts = true;
        changed.colorblindMode = true;
        changed.yellowThreshold = 20;
        changed.orangeThreshold = 40;
        changed.redThreshold = 60;
        changed.showStaleWarning = false;
        changed.showOpenAiExtraLimits = true;
        // Other pages' fields stay.
        changed.barLength = 220;
        changed.pollMinutes = 30;
        changed.historyRetentionDays = 900;
        changed.accounts.push_back(Account());
        Settings expected = changed;
        const Settings defaults;
        expected.barMode = defaults.barMode;
        expected.percentTextVisibility = defaults.percentTextVisibility;
        expected.percentTextAlignment = defaults.percentTextAlignment;
        expected.showExtraBarAmounts = defaults.showExtraBarAmounts;
        expected.colorblindMode = defaults.colorblindMode;
        expected.yellowThreshold = defaults.yellowThreshold;
        expected.orangeThreshold = defaults.orangeThreshold;
        expected.redThreshold = defaults.redThreshold;
        expected.showStaleWarning = defaults.showStaleWarning;
        expected.showOpenAiExtraLimits = defaults.showOpenAiExtraLimits;
        Settings reset = changed;
        SettingsAssign(reset, SettingsField::ResetDisplay, 0);
        CHECK(reset == expected);
        CHECK(SettingsGroupFields(SettingsField::ResetDisplay).size() == 10);
        for (SettingsField field : SettingsGroupFields(SettingsField::ResetDisplay)) {
            CHECK(SettingsFieldValue(reset, field) == SettingsFieldValue(defaults, field));
            CHECK(SettingsFieldValue(changed, field) != SettingsFieldValue(defaults, field));
        }

        // All 36 scalar settings changed: the four page resets restore every one, and no field
        // belongs to two groups (Show on holds the monitor mode and number; Keep history and its
        // custom days both set the retention).
        Settings every = changed;
        every.taskbarMonitorMode = TaskbarMonitorMode::Specific;
        every.taskbarMonitorNumber = 2;
        every.showDetailsOnHover = false;
        every.skipHiddenPolling = false;
        every.enableNotifications = false;
        every.historyEnabled = false;
        every.historyDefaultRange = HistoryUiRange::Quarter;
        every.barLayout = BarLayout::LogoHeadline;
        every.barStyle = BarStyle::Rail;
        every.barThickness = 14;
        every.barGap = 6;
        every.ringSize = 38;
        every.ringThickness = 5;
        every.labelPosition = LabelPosition::Bottom;
        every.labelFontSize = 15;
        every.labelGap = 9;
        every.showBarLabels = true;
        every.percentFontSize = 12;
        every.showPaceTicks = false;
        every.paceTickStyle = PaceTickStyle::Dot;
        every.paceTickColor = 0x00FF00;
        every.accountMargin = 12;
        every.rightMargin = 30;
        const SettingsField groups[] = {SettingsField::ResetBehavior, SettingsField::ResetHistory,
                                        SettingsField::ResetLayout, SettingsField::ResetDisplay};
        Settings restored = every;
        std::vector<SettingsField> seen;
        for (SettingsField group : groups) {
            SettingsAssign(restored, group, 0);
            std::vector<SettingsField> fields = SettingsGroupFields(group);
            seen.insert(seen.end(), fields.begin(), fields.end());
        }
        Settings fresh;
        fresh.accounts = every.accounts;
        CHECK(restored == fresh);
        std::sort(seen.begin(), seen.end());
        CHECK(seen.size() == 36 && std::adjacent_find(seen.begin(), seen.end()) == seen.end());
    });

    Run("projection: thresholds stay ordered while typing; out-of-range and bad text", [] {
        TestStore store;
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(0, kRed, L"60");
        Settings p = edits.Projection();
        CHECK(p.yellowThreshold == 50 && p.orangeThreshold == 75 && p.redThreshold == 75);
        CHECK(edits.drafts[0].value == 60 && edits.drafts[0].raw == L"60");
        edits.SetText(0, kYellow, L"80");
        p = edits.Projection();
        CHECK(p.yellowThreshold == 80 && p.orangeThreshold == 80 && p.redThreshold == 80);
        edits.SetText(0, kYellow, L"-5");
        p = edits.Projection();
        CHECK(p.yellowThreshold == 0 && p.orangeThreshold == 75 && p.redThreshold == 75);
        edits.SetText(0, kYellow, L"12a");
        CHECK(edits.Projection().yellowThreshold == 0);  // The last valid value
        edits.SetText(0, kOrange, L"999");
        p = edits.Projection();
        CHECK(p.orangeThreshold == 100 && p.redThreshold == 100);
        CHECK(Finite(Strip(p)));
        // A dragged red below orange projects at orange too.
        edits.Discard();
        edits.SetValue(0, kRed, 10);
        CHECK(edits.drafts[0].value == 10 && edits.Projection().redThreshold == 75);
        CHECK(store.Applied().empty());
    });

    Run("controller: a saved threshold shows its normalized value, also in the box typed into", [] {
        TestStore store;
        store.normalize = OrderThresholds;
        SettingsEdits edits(store);
        edits.Reload();
        auto view = [&] {
            SettingsView v = TestView(SettingsUiPage::Display);
            v.settings = edits.Projection();
            v.drafts = edits.drafts;
            return DisplayFrame(v);
        };
        // While typed, the box keeps the text; the slider and the preview show the ordered value.
        edits.SetText(0, kRed, L"60");
        UiFrame frame = view();
        CHECK(Control(frame, kRed).text == L"60" && Control(frame, kRed, 1).action.value == 75);
        CHECK(edits.RequestCommit(0, kRed) && edits.Process() == 0);
        CHECK(store.Applied() == std::vector<std::wstring>{Op(kRed, 60)});
        CHECK(store.settings.redThreshold == 75 && edits.drafts.empty());
        frame = view();
        CHECK(Control(frame, kRed).text == L"75" && Control(frame, kRed, 1).action.value == 75);
        // A yellow above the others pushes them up when saved.
        edits.SetText(0, kYellow, L"95");
        CHECK(edits.RequestCommit(0, kYellow) && edits.Process() == 0);
        CHECK(store.settings.yellowThreshold == 95 && store.settings.orangeThreshold == 95 &&
              store.settings.redThreshold == 95);
        frame = view();
        for (SettingsField field : {kYellow, kOrange, kRed}) CHECK(Control(frame, field).text == L"95");
        // The page reset drops threshold drafts and restores 50, 75 and 90.
        edits.SetText(0, kOrange, L"10");
        edits.SetText(0, SettingsField::RetentionDays, L"120");
        CHECK(edits.Reset(SettingsField::ResetDisplay) == 0);
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].field == SettingsField::RetentionDays);
        CHECK(store.settings.yellowThreshold == 50 && store.settings.orangeThreshold == 75 &&
              store.settings.redThreshold == 90);
    });

    Run("display page: controls follow the projection; dots follow the palette", [] {
        SettingsView view = TestView(SettingsUiPage::Display);
        UiFrame frame = DisplayFrame(view);
        const UiItem& mode = Control(frame, SettingsField::BarMode);
        CHECK(mode.kind == UiItemKind::Dropdown && mode.text == L"Used" && mode.options.size() == 2 && mode.enabled);
        CHECK(mode.options[1].text == L"Remaining" && mode.options[1].value == int(BarMode::Remaining));
        CHECK_NEAR(mode.rect.right - mode.rect.left, 200);
        const UiItem& percent = Control(frame, SettingsField::PercentTextVisibility);
        CHECK(percent.text == L"Show on hover" && percent.enabled && percent.options.size() == 3 &&
              percent.options[0].text == L"Never show" && percent.options[2].text == L"Always show");
        const UiItem& alignment = Control(frame, SettingsField::PercentTextAlignment);
        CHECK(alignment.text == L"Adaptive" && alignment.enabled && alignment.options.size() == 4);
        const UiItem& amounts = Control(frame, SettingsField::ShowExtraBarAmounts);
        CHECK(amounts.kind == UiItemKind::Toggle && !amounts.selected && amounts.enabled && amounts.action.value == 1);
        CHECK(!Control(frame, SettingsField::ColorblindMode).selected);
        const std::pair<SettingsField, int> thresholds[] = {{kYellow, 50}, {kOrange, 75}, {kRed, 90}};
        for (auto [field, value] : thresholds) {
            const UiItem& box = Control(frame, field);
            const UiItem& slider = Control(frame, field, 1);
            CHECK(box.kind == UiItemKind::NumberBox && box.text == std::to_wstring(value) &&
                  box.align == DWRITE_TEXT_ALIGNMENT_CENTER);
            CHECK(slider.kind == UiItemKind::Slider && slider.minimum == 0 && slider.maximum == 100 &&
                  slider.action.value == value);
            CHECK_NEAR(slider.rect.right - slider.rect.left, 220);
        }
        CHECK(Control(frame, SettingsField::ShowStaleWarning).selected);
        CHECK(!Control(frame, SettingsField::ShowOpenAiExtraLimits).selected);
        CHECK(Control(frame, SettingsField::ResetDisplay).text == L"Reset");
        for (PCWSTR text : {L"Values", L"Usage colors", L"Status", L"Details flyout", L"Reset Display to defaults",
                            L"Usage below this is green.", L"Thresholds stay in order: yellow, orange, red.",
                            L"Also the level for threshold notifications."}) {
            CHECK(HasText(frame, text));
        }
        CHECK(!HasText(frame, L"Stacked and Vertical bars only") && !HasText(frame, L"Show percentage text"));

        // Threshold dots: the usage colors, 14 DIP after the card padding with the title 16 DIP
        // after them; drawn, never targets.
        std::vector<const UiItem*> dots = Dots(frame);
        CHECK(dots.size() == 3);
        const COLORREF standard[] = {RGB(0xFD, 0xD8, 0x35), RGB(0xFB, 0x8C, 0x00), RGB(0xE5, 0x39, 0x35)};
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        for (size_t i = 0; i < dots.size(); i++) {
            CHECK(COLORREF(dots[i]->action.value) == standard[i]);
            CHECK_NEAR(dots[i]->rect.right - dots[i]->rect.left, 14);
            CHECK(!UiActive(*dots[i]) && !UiFocusable(*dots[i]));
            CHECK(std::none_of(stops.begin(), stops.end(),
                               [&](const UiFocusStop& stop) { return stop.ref.id == dots[i]->id; }));
        }
        const UiItem* card = nullptr;
        const UiItem* title = nullptr;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Card && item.rect.top < dots[0]->rect.top) card = &item;
            if (UiRunsText(item.runs) == L"Yellow from") title = &item;
        }
        CHECK(card && title);
        CHECK_NEAR(dots[0]->rect.left - card->rect.left, 19);
        CHECK_NEAR(title->rect.left - dots[0]->rect.right, 16);
        CHECK_NEAR((dots[0]->rect.top + dots[0]->rect.bottom) / 2, (card->rect.top + card->rect.bottom) / 2);
        CHECK(std::any_of(stops.begin(), stops.end(), [](const UiFocusStop& stop) {
            return stop.ref.id == SettingsControlId(kRed, 1);
        }));

        // Colorblind: the dots and the green band's wording follow the palette.
        view.settings.colorblindMode = true;
        frame = DisplayFrame(view);
        dots = Dots(frame);
        const COLORREF colorblind[] = {RGB(0x56, 0xB4, 0xE9), RGB(0xE6, 0x9F, 0x00), RGB(0xD5, 0x5E, 0x00)};
        for (size_t i = 0; i < dots.size(); i++) CHECK(COLORREF(dots[i]->action.value) == colorblind[i]);
        CHECK(HasText(frame, L"Usage below this is blue.") && !HasText(frame, L"Usage below this is green."));
        const UiItem& toggle = Control(frame, SettingsField::ColorblindMode);
        CHECK(toggle.selected && toggle.action.value == 0);
        // Equal thresholds keep a dot each.
        view.settings.yellowThreshold = view.settings.orangeThreshold = view.settings.redThreshold = 60;
        frame = DisplayFrame(view);
        dots = Dots(frame);
        CHECK(dots.size() == 3 && COLORREF(dots[0]->action.value) == colorblind[0]);
    });

    Run("display page: bar-only rows disable for tile styles; hidden text disables alignment and amounts", [] {
        SettingsView view = TestView(SettingsUiPage::Display);
        for (int style = 2; style < 5; style++) {
            view.settings.barLayout = BarLayout(style);
            UiFrame frame = DisplayFrame(view);
            CHECK(Control(frame, SettingsField::BarMode).enabled);
            for (SettingsField field : {SettingsField::PercentTextVisibility, SettingsField::PercentTextAlignment,
                                        SettingsField::ShowExtraBarAmounts}) {
                CHECK(!Control(frame, field).enabled);
            }
            std::wstring reason = L"Stacked and Vertical bars only. Current tile style: " +
                                  std::wstring(kSettingsTileStyles[style]) + L".";
            CHECK(TextCount(frame, reason) == 3);
            CHECK(Control(frame, kYellow, 1).enabled && Control(frame, SettingsField::ColorblindMode).enabled);
        }
        view.settings.barLayout = BarLayout::SegmentedRing;
        CHECK(TextCount(DisplayFrame(view), L"Stacked and Vertical bars only. Current tile style: Segmented ring.") == 3);

        view.settings.barLayout = BarLayout::Vertical;
        UiFrame frame = DisplayFrame(view);
        CHECK(Control(frame, SettingsField::PercentTextAlignment).enabled &&
              Control(frame, SettingsField::ShowExtraBarAmounts).enabled);

        view.settings.barLayout = BarLayout::Stacked;
        view.settings.percentTextVisibility = PercentTextVisibility::Never;
        frame = DisplayFrame(view);
        CHECK(Control(frame, SettingsField::PercentTextVisibility).enabled &&
              Control(frame, SettingsField::PercentTextVisibility).text == L"Never show");
        CHECK(!Control(frame, SettingsField::PercentTextAlignment).enabled &&
              !Control(frame, SettingsField::ShowExtraBarAmounts).enabled);
        CHECK(TextCount(frame, L"Show percentage text on hover or always to change this.") == 2);
        CHECK(!HasText(frame, L"Stacked and Vertical bars only"));
    });

    Run("display header: the preview pinned under the title as on Layout, unpinned when short", [] {
        SettingsView view = TestView(SettingsUiPage::Display);
        view.settings.barLayout = BarLayout::SegmentedRing;
        UiFrame frame = DisplayFrame(view);
        UiSurfaceId surface = UiSurfaceId::Content;
        const UiItem* strip = UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll), &surface);
        CHECK(strip && strip->kind == UiItemKind::Preview && surface == UiSurfaceId::Main);
        CHECK_NEAR(strip->rect.top, 56 + 13 + 20 + 10);
        CHECK_NEAR(frame.viewport.top, 56 + 106 + 12);
        CHECK(frame.content.front().runs[0].text == L"Values");
        CHECK_NEAR(frame.content.front().rect.top, 16);
        CHECK(UiFindItem(frame, SettingsControlId(SettingsField::ShowOnTaskbar))->kind == UiItemKind::Toggle);
        CHECK(HasTextIn(frame.main, L"Example accounts, updates as you change settings"));
        frame = DisplayFrame(view, {1000, 440});
        strip = UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll), &surface);
        CHECK(strip && surface == UiSurfaceId::Content);
        CHECK_NEAR(frame.viewport.top, 56);
    });

    Run("preview: percentage text visibility, bar mode, amounts and alignment", [] {
        // Sample bars in order: OAI 5h 7d Cr, CC 1 5h 7d, CC 2 5h 7d Fa Ex, Go 5h; used 25, 62.5,
        // 12.5 | 82.5, 25 | 12.5, 110, 62.5, 25 | 25 (stale) with the default thresholds.
        Settings s;
        std::vector<SettingsPreviewOp> values = Values(Strip(s));
        CHECK(values.size() == 10);
        CHECK(std::all_of(values.begin(), values.end(), [](const SettingsPreviewOp& op) { return op.hover >= 0; }));
        s.percentTextVisibility = PercentTextVisibility::Never;
        CHECK(Values(Strip(s)).empty());
        s.percentTextVisibility = PercentTextVisibility::Always;
        values = Values(Strip(s));
        CHECK(values.size() == 10 && values[0].hover == -1);
        CHECK(values[0].text == L"25%" && values[6].text == L"110%" && values[9].text == L"25%");
        // Remaining: what is left, the quota past 100% clamped to 0.
        s.barMode = BarMode::Remaining;
        values = Values(Strip(s));
        CHECK(values[0].text == L"75%" && values[6].text == L"0%" && values[9].text == L"75%");
        // Amounts: credits and dollars spent, then left.
        s.showExtraBarAmounts = true;
        values = Values(Strip(s));
        CHECK(values[2].text == L"875" && values[8].text == L"$37.50" && values[0].text == L"75%");
        s.barMode = BarMode::Used;
        values = Values(Strip(s));
        CHECK(values[2].text == L"125" && values[8].text == L"$12.50");

        // Dark text over yellow and orange fills, which adaptive alignment puts the text on from
        // the yellow threshold of the shown value.
        auto dark = [](const Settings& s) {
            std::vector<SettingsPreviewOp> values = Values(Strip(s));
            return std::count_if(values.begin(), values.end(),
                                 [](const SettingsPreviewOp& op) { return op.color == 0xFF000000; });
        };
        s = {};
        s.percentTextVisibility = PercentTextVisibility::Always;
        CHECK(dark(s) == 3);
        s.percentTextAlignment = PercentTextAlignment::Left;
        CHECK(dark(s) == 3);
        s.percentTextAlignment = PercentTextAlignment::Right;
        CHECK(dark(s) == 0);
        s.percentTextAlignment = PercentTextAlignment::Adaptive;
        s.barMode = BarMode::Remaining;
        CHECK(dark(s) == 0);  // 37.5, 17.5 and 37.5 left: under yellow, on the track
        values = Values(Strip(s));
        CHECK(values[1].align == DWRITE_TEXT_ALIGNMENT_TRAILING);

        // Tile styles have no percentage text: the strip doesn't change with it.
        Settings ring;
        ring.barLayout = BarLayout::SegmentedRing;
        size_t ops = Strip(ring).ops.size();
        ring.percentTextVisibility = PercentTextVisibility::Always;
        ring.showExtraBarAmounts = true;
        CHECK(Strip(ring).ops.size() == ops && Values(Strip(ring)).empty());
    });

    Run("preview: usage colors follow the palette and the thresholds; the stale account greys and marks", [] {
        // Go is stale: grey in either palette.
        auto fills = [](const Settings& s, uint32_t color) { return OpsOfColor(Strip(s), color).size(); };
        Settings s;
        CHECK(fills(s, 0xFF43A047) == 5 && fills(s, 0xFFFDD835) == 2 && fills(s, 0xFFFB8C00) == 1 &&
              fills(s, 0xFFE53935) == 1 && fills(s, 0xFF9E9E9E) == 1);
        s.colorblindMode = true;
        CHECK(fills(s, 0xFF0072B2) == 5 && fills(s, 0xFF56B4E9) == 2 && fills(s, 0xFFE69F00) == 1 &&
              fills(s, 0xFFD55E00) == 1 && fills(s, 0xFF9E9E9E) == 1);
        CHECK(fills(s, 0xFF43A047) == 0 && fills(s, 0xFFE53935) == 0);
        s.barLayout = BarLayout::SegmentedRing;
        CHECK(fills(s, 0xFFD55E00) == 1 && fills(s, 0xFFE53935) == 0 && fills(s, 0xFF9E9E9E) == 1);
        // The samples sit mid-band (red past the quota): thresholds move their values, every band
        // still shows.
        s = {};
        s.percentTextVisibility = PercentTextVisibility::Always;
        s.yellowThreshold = 20;
        s.orangeThreshold = 40;
        s.redThreshold = 60;
        std::vector<SettingsPreviewOp> values = Values(Strip(s));
        CHECK(values[0].text == L"10%" && values[3].text == L"50%" && values[6].text == L"110%");
        CHECK(fills(s, 0xFF43A047) == 5 && fills(s, 0xFFE53935) == 1);
        // Unordered thresholds reach the strip ordered: 80, 80, 80.
        s.orangeThreshold = 30;
        s.redThreshold = 20;
        s.yellowThreshold = 80;
        values = Values(Strip(s));
        CHECK(values[0].text == L"40%" && values[6].text == L"110%" && Finite(Strip(s)));

        // Mark stale data: Go, the stale account, shows its logo, which takes the mark (a Label
        // identity's "!": layout.inc). Tiles add the mark.
        auto marks = [](const SettingsPreviewStrip& strip, std::wstring_view text) {
            return std::count_if(strip.ops.begin(), strip.ops.end(), [&](const SettingsPreviewOp& op) {
                return op.kind == SettingsPreviewOp::Kind::Text && op.text == text;
            });
        };
        s = {};
        for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical}) {
            s.barLayout = layout;
            s.showStaleWarning = true;
            SettingsPreviewStrip marked = Strip(s);
            CHECK(marks(marked, L"Go!") == 0 && marks(marked, L"Go") == 0 && marks(marked, L"!") == 1);
            s.showStaleWarning = false;
            SettingsPreviewStrip unmarked = Strip(s);
            CHECK(marks(unmarked, L"Go!") == 0 && marks(unmarked, L"Go") == 0 && marks(unmarked, L"!") == 0);
        }
        for (BarLayout layout : {BarLayout::SegmentedRing, BarLayout::LogoHeadline}) {
            s.barLayout = layout;
            s.showStaleWarning = true;
            CHECK(marks(Strip(s), L"!") == 1);
            s.showStaleWarning = false;
            CHECK(marks(Strip(s), L"!") == 0);
        }
    });
}
