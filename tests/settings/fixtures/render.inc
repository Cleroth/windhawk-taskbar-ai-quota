// Offscreen renders of the settings window's client area (navigation, pinned header, viewport)
// into WIC bitmaps: PNG evidence for review, no pixel goldens. The composition mirrors the host:
// main items, then the content clipped to the viewport at the scroll offset, then the scrollbar.

// A row of buttons in every style and state, for the states sheet.
static void AddButtonRow(std::vector<UiItem>& items, uint32_t& id, float x, float y) {
    struct Sample {
        PCWSTR text, icon;
        UiButtonStyle style;
        bool enabled;
    };
    const Sample samples[] = {
        {L"Standard", L"", UiButtonStyle::Standard, true},
        {L"Add account", L"\uE710", UiButtonStyle::Accent, true},
        {L"Subtle", L"", UiButtonStyle::Subtle, true},
        {L"Remove account", L"", UiButtonStyle::Danger, true},
        {L"Disabled", L"", UiButtonStyle::Standard, false},
    };
    for (const Sample& sample : samples) {
        UiItem button;
        button.id = id++;
        button.kind = UiItemKind::Button;
        button.text = sample.text;
        button.icon = sample.icon;
        button.iconSize = 12;
        button.button = sample.style;
        button.enabled = sample.enabled;
        button.action = kTestOpen;
        float width = sample.icon[0] ? 150 : 120;
        button.rect = {x, y, x + width, y + 32};
        items.push_back(std::move(button));
        x += width + 8;
    }
}

// The host's composition; `view` paints the client-drawn preview items, as the settings client
// does. The pointer (state.pointer) is in the coordinates of `pointerContent` ? content : main.
static void RenderFrame(UiGraphics& g, IWICImagingFactory* wic, const std::wstring& path,
                        const UiFrame& frame, const SettingsView& view, D2D1_SIZE_F client, UINT dpi,
                        const UiThemeInput& theme, UiPaintState state, float scroll,
                        bool scrollbarExpanded, bool pointerContent = true, const RenderOverlay& overlay = {}) {
    float scale = float(dpi) / 96;
    winrt::com_ptr<IWICBitmap> bitmap = RenderBitmap(wic, client, dpi);
    winrt::com_ptr<ID2D1RenderTarget> target = RenderTarget(g, bitmap.get(), dpi);
    UiTargetCache cache;
    UiPalette palette = UiPaletteFor(theme);
    D2D1_POINT_2F pointer = state.pointer;
    UiCustomPaint custom = [&](const UiItem& item) {
        SettingsPaintPreview(target.get(), cache, g, theme, view, item, state.pointer);
    };
    target->BeginDraw();
    target->Clear(palette.background);
    state.pointer = pointerContent ? D2D1_POINT_2F{-1e9f, -1e9f} : pointer;
    UiPaintItems(target.get(), g, cache, palette, frame.main, state, {0, 0}, scale, -1e9f, 1e9f, &custom);
    const D2D1_RECT_F& v = frame.viewport;
    float height = v.bottom - v.top;
    target->PushAxisAlignedClip(&v, D2D1_ANTIALIAS_MODE_ALIASED);
    state.pointer = pointerContent ? pointer : D2D1_POINT_2F{-1e9f, -1e9f};
    UiPaintItems(target.get(), g, cache, palette, frame.content, state, {v.left, v.top - scroll},
                 scale, scroll, scroll + height, &custom);
    D2D1::Matrix3x2F shift = D2D1::Matrix3x2F::Translation(v.left, v.top);
    target->SetTransform(&shift);
    UiPaintScrollbar(target.get(), cache, palette,
                     UiScrollbarFor({v.right - v.left, height}, scroll, frame.contentHeight,
                                    scrollbarExpanded),
                     scrollbarExpanded, scale);
    D2D1::Matrix3x2F identity = D2D1::Matrix3x2F::Identity();
    target->SetTransform(&identity);
    target->PopAxisAlignedClip();
    if (overlay) overlay(target.get(), cache, palette);
    winrt::check_hresult(target->EndDraw());
    cache.Clear();
    SaveRender(wic, bitmap.get(), path);
    std::string narrow;
    for (wchar_t c : path) narrow += c < 0x80 ? char(c) : '?';
    std::cout << "RENDER " << narrow << '\n';
}

// A pointer over column `column` of a preview strip item at `r`, as the painter places it.
static D2D1_POINT_2F PreviewPointer(UiGraphics& g, const SettingsView& view, const D2D1_RECT_F& r, int column) {
    SettingsPreviewStrip strip = SettingsPreviewLayout(g, view.settings, view.status.taskbarHeight, view.scale,
                                                       view.now, false, false, 0);
    float trayLeft = r.right - r.left - strip.trayWidth;
    float overflow = SettingsPreviewOverflow(strip, r.right - r.left);
    float left = r.left + trayLeft - strip.rightMargin - strip.width + std::clamp(view.previewScroll, 0.0f, overflow);
    const D2D1_RECT_F& c = strip.columns[size_t(column)];
    return {left + (c.left + c.right) / 2, r.top + (c.top + c.bottom) / 2};
}

static SettingsView LayoutView(BarLayout layout, UINT dpi) {
    SettingsView view = TestView(SettingsUiPage::Layout);
    view.settings.barLayout = layout;
    view.status.taskbarHeight = 48;
    view.scale = float(dpi) / 96;
    view.now = kNow;
    return view;
}

// Preview strips at 1:1, one row per variant with its caption. Layout sheet: every layout,
// percent text modes and alignments, quota keys, bar modes, amounts, colorblind colors, pace
// styles and colors, label positions, extreme sizes, overflow (scrolled and not), hover, and a
// 32 DIP taskbar. Display sheet: percentage text never/hover/always, each alignment, bar modes
// (CC 2's weekly quota is past 100%), amounts, the colorblind palette, thresholds (ordered,
// unordered, extreme) and the stale marker (Go is stale) on bars and tiles. Each strip is a Preview item painted by the production painter.
static void RenderPreviewSheet(UiGraphics& g, IWICImagingFactory* wic, const std::wstring& path,
                               const UiThemeInput& theme, UINT dpi, bool display = false) {
    struct Row {
        const wchar_t* caption;
        std::function<void(Settings&)> edit;
        int hover = -1;
        double taskbar = 48;
        float scroll = 0;
    };
    auto always = [](Settings& s) { s.percentTextVisibility = PercentTextVisibility::Always; };
    auto thresholds = [](Settings& s, int yellow, int orange, int red) {
        s.yellowThreshold = yellow;
        s.orangeThreshold = orange;
        s.redThreshold = red;
    };
    const std::vector<Row> displayRows = {
        {L"Defaults (hover)", [](Settings&) {}},
        {L"Hover CC 2", [](Settings&) {}, 2},
        {L"Never, hover CC 2", [](Settings& s) { s.percentTextVisibility = PercentTextVisibility::Never; }, 2},
        {L"Always, adaptive", always},
        {L"Always, left", [&](Settings& s) { always(s); s.percentTextAlignment = PercentTextAlignment::Left; }},
        {L"Always, center", [&](Settings& s) { always(s); s.percentTextAlignment = PercentTextAlignment::Center; }},
        {L"Always, right", [&](Settings& s) { always(s); s.percentTextAlignment = PercentTextAlignment::Right; }},
        {L"Remaining, adaptive", [&](Settings& s) { always(s); s.barMode = BarMode::Remaining; }},
        {L"Remaining, hover CC 2", [](Settings& s) { s.barMode = BarMode::Remaining; }, 2},
        {L"Amounts, used", [&](Settings& s) { always(s); s.showExtraBarAmounts = true; }},
        {L"Amounts, remaining",
         [&](Settings& s) { always(s); s.showExtraBarAmounts = true; s.barMode = BarMode::Remaining; }},
        {L"Colorblind", [&](Settings& s) { always(s); s.colorblindMode = true; }},
        {L"Colorblind, remaining",
         [&](Settings& s) { always(s); s.colorblindMode = true; s.barMode = BarMode::Remaining; }},
        {L"Stale marker off", [&](Settings& s) { always(s); s.showStaleWarning = false; }},
        {L"Thresholds 20 / 40 / 60", [&](Settings& s) { always(s); thresholds(s, 20, 40, 60); }},
        {L"Unordered 80 / 30 / 20", [&](Settings& s) { always(s); thresholds(s, 80, 30, 20); }},
        {L"Thresholds 0 / 0 / 0", [&](Settings& s) { always(s); thresholds(s, 0, 0, 0); }},
        {L"Thresholds 100 / 100 / 100", [&](Settings& s) { always(s); thresholds(s, 100, 100, 100); }},
        {L"Vertical, colorblind",
         [&](Settings& s) { always(s); s.barLayout = BarLayout::Vertical; s.colorblindMode = true; }},
        {L"Vertical, remaining, amounts",
         [&](Settings& s) {
             always(s);
             s.barLayout = BarLayout::Vertical;
             s.barMode = BarMode::Remaining;
             s.showExtraBarAmounts = true;
         }},
        {L"Ring", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; }},
        {L"Ring, stale marker off",
         [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.showStaleWarning = false; }},
        {L"Ring, colorblind", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.colorblindMode = true; }},
        {L"Ring, remaining", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.barMode = BarMode::Remaining; }},
        {L"Concentric, 20 / 40 / 60",
         [&](Settings& s) { s.barLayout = BarLayout::ConcentricRings; thresholds(s, 20, 40, 60); }},
        {L"Logo + bars, colorblind", [](Settings& s) { s.barLayout = BarLayout::LogoBars; s.colorblindMode = true; }},
        {L"Headline, colorblind",
         [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.colorblindMode = true; }},
        {L"Headline, remaining", [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.barMode = BarMode::Remaining; }},
    };
    const std::vector<Row> layoutRows = {
        {L"Stacked bars", [](Settings&) {}},
        {L"Hover CC 2", [](Settings&) {}, 2},
        {L"Percent always, adaptive", always},
        {L"Percent left", [&](Settings& s) { always(s); s.percentTextAlignment = PercentTextAlignment::Left; }},
        {L"Percent center", [&](Settings& s) { always(s); s.percentTextAlignment = PercentTextAlignment::Center; }},
        {L"Percent right", [&](Settings& s) { always(s); s.percentTextAlignment = PercentTextAlignment::Right; }},
        {L"Quota keys, percent", [&](Settings& s) { always(s); s.showBarLabels = true; }},
        {L"Remaining mode", [&](Settings& s) { always(s); s.barMode = BarMode::Remaining; }},
        {L"Amounts", [&](Settings& s) { always(s); s.showExtraBarAmounts = true; }},
        {L"Colorblind", [&](Settings& s) { always(s); s.colorblindMode = true; }},
        {L"Pace line", [](Settings& s) { s.paceTickStyle = PaceTickStyle::Line; }},
        {L"Pace notch", [](Settings& s) { s.paceTickStyle = PaceTickStyle::Notch; }},
        {L"Pace dot", [](Settings& s) { s.paceTickStyle = PaceTickStyle::Dot; }},
        {L"Pace pink", [](Settings& s) { s.paceTickColor = RGB(0xFF, 0x6E, 0xC7); }},
        {L"Pace off", [](Settings& s) { s.showPaceTicks = false; }},
        {L"Label top", [](Settings& s) { s.labelPosition = LabelPosition::Top; }},
        {L"Label right", [](Settings& s) { s.labelPosition = LabelPosition::Right; }},
        {L"Label bottom", [](Settings& s) { s.labelPosition = LabelPosition::Bottom; }},
        {L"Label hidden", [](Settings& s) { s.labelPosition = LabelPosition::Hidden; }},
        {L"Labels 24, text 14", [&](Settings& s) { always(s); s.labelFontSize = 24; s.percentFontSize = 14; }},
        {L"Bars 10 x 2", [](Settings& s) { s.barLength = 10; s.barThickness = 2; }},
        {L"Bars 60 x 14", [&](Settings& s) { always(s); s.barLength = 60; s.barThickness = 14; }},
        {L"Bars 300, overflow", [](Settings& s) { s.barLength = 300; }},
        {L"Overflow scrolled", [](Settings& s) { s.barLength = 300; }, -1, 48, 260},
        {L"Bars, 32 DIP taskbar", [](Settings&) {}, -1, 32},
        {L"Vertical bars", [](Settings& s) { s.barLayout = BarLayout::Vertical; }},
        {L"Vertical, keys, percent",
         [&](Settings& s) { always(s); s.barLayout = BarLayout::Vertical; s.showBarLabels = true; }},
        {L"Segmented ring", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; }},
        {L"Ring, hover OAI", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; }, 0},
        {L"Ring 24 / 6",
         [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.ringSize = 24; s.ringThickness = 6; }},
        {L"Ring 40 / 1",
         [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.ringSize = 40; s.ringThickness = 1; }},
        {L"Ring, 32 DIP taskbar", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; }, -1, 32},
        {L"Concentric rings", [](Settings& s) { s.barLayout = BarLayout::ConcentricRings; }},
        {L"Concentric, remaining",
         [](Settings& s) { s.barLayout = BarLayout::ConcentricRings; s.barMode = BarMode::Remaining; }},
        {L"Logo + bars", [](Settings& s) { s.barLayout = BarLayout::LogoBars; }},
        {L"Logo + bars, hover CC 1", [](Settings& s) { s.barLayout = BarLayout::LogoBars; }, 1},
        {L"Headline %", [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; }},
        {L"Headline, remaining",
         [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.barMode = BarMode::Remaining; }},
    };
    // The Layout sheet's strips are the window's 680 DIPs; the Display sheet's fit four columns of
    // stacked bars, so every value shows.
    const std::vector<Row>& rows = display ? displayRows : layoutRows;
    constexpr float kCaption = 170, kRowGap = 12;
    const float kStrip = display ? 800 : 680;
    float height = kRowGap;
    for (const Row& row : rows) height += float(row.taskbar) + kRowGap;
    winrt::com_ptr<IWICBitmap> bitmap = RenderBitmap(wic, {kCaption + kStrip + kRowGap, height}, dpi);
    winrt::com_ptr<ID2D1RenderTarget> target = RenderTarget(g, bitmap.get(), dpi);
    UiTargetCache cache;
    UiPalette palette = UiPaletteFor(theme);
    target->BeginDraw();
    target->Clear(palette.background);
    float y = kRowGap;
    for (const Row& row : rows) {
        SettingsView view = LayoutView(BarLayout::Stacked, dpi);
        row.edit(view.settings);
        view.status.taskbarHeight = row.taskbar;
        view.previewScroll = row.scroll;
        UiItem strip;
        strip.kind = UiItemKind::Preview;
        strip.rect = {kCaption, y, kCaption + kStrip, y + float(row.taskbar)};
        D2D1_POINT_2F pointer = row.hover >= 0 ? PreviewPointer(g, view, strip.rect, row.hover) :
                                                 D2D1_POINT_2F{-1e9f, -1e9f};
        UiDrawLayout(target.get(), cache, UiLayout(g, row.caption, UiTextStyleFor(UiFont::Caption), kCaption - 16, false),
                     {8, y + float(row.taskbar) / 2 - 8}, palette.textSecondary);
        SettingsPaintPreview(target.get(), cache, g, theme, view, strip, pointer);
        y += float(row.taskbar) + kRowGap;
    }
    winrt::check_hresult(target->EndDraw());
    cache.Clear();
    SaveRender(wic, bitmap.get(), path);
    std::string narrow;
    for (wchar_t c : path) narrow += c < 0x80 ? char(c) : '?';
    std::cout << "RENDER " << narrow << '\n';
}

static void RunRenders(const std::wstring& directory) {
    winrt::check_hresult(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    {
        UiGraphics g;
        UiCreateGraphics(g);
        winrt::com_ptr<IWICImagingFactory> wic;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                              __uuidof(IWICImagingFactory), wic.put_void()));
        // The approved mockups' window is 1000 x 860 with a 40 DIP caption: 1000 x 820 client.
        const D2D1_SIZE_F client{1000, 820};
        auto path = [&](const std::string& name) {
            return directory + L"\\" + std::wstring(name.begin(), name.end());
        };
        static constexpr const char* kStyleNames[] = {"stacked", "vertical", "ring", "concentric",
                                                      "logobars", "headline"};
        for (const RenderTheme& theme : RenderThemes()) {
            for (UINT dpi : {96u, 144u, 192u}) {
                const std::pair<SettingsUiPage, const char*> pages[] = {
                    {SettingsUiPage::About, "about"}, {SettingsUiPage::Accounts, "accounts"},
                    {SettingsUiPage::Behavior, "behavior"}, {SettingsUiPage::History, "history"}};
                for (auto [page, pageName] : pages) {
                    std::string name = std::string("render-") + pageName + "-" + theme.name + "-" +
                                       std::to_string(dpi) + ".png";
                    Run(name, [&] {
                        SettingsView view = page == SettingsUiPage::Accounts ? AccountsView() : TestView(page);
                        view.version = L"2.0.1";
                        UiFrame frame;
                        SettingsUiBuildFrame(g, view, client, frame);
                        UiPaintState state;
                        state.focus = {uint32_t(int(page) + 1)};
                        RenderFrame(g, wic.get(), path(name), frame, view, client, dpi, theme.input, state, 0,
                                    false);
                        CHECK(true);
                    });
                }
                // Layout with each tile style (every DPI for stacked bars and the segmented ring).
                for (int style = 0; style < 6; style++) {
                    if (dpi != 144 && style != 0 && style != 2) continue;
                    std::string name = std::string("render-layout-") + kStyleNames[style] + "-" + theme.name +
                                       "-" + std::to_string(dpi) + ".png";
                    Run(name, [&] {
                        SettingsView view = LayoutView(BarLayout(style), dpi);
                        UiFrame frame;
                        SettingsUiBuildFrame(g, view, client, frame);
                        UiPaintState state;
                        state.focus = {2};
                        RenderFrame(g, wic.get(), path(name), frame, view, client, dpi, theme.input, state, 0,
                                    false);
                        CHECK(UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll)));
                    });
                }
                // Display with the segmented ring, as the mockup: its bar-only rows disabled.
                std::string display = std::string("render-display-") + theme.name + "-" + std::to_string(dpi) + ".png";
                Run(display, [&] {
                    SettingsView view = LayoutView(BarLayout::SegmentedRing, dpi);
                    view.page = SettingsUiPage::Display;
                    UiFrame frame;
                    SettingsUiBuildFrame(g, view, client, frame);
                    UiPaintState state;
                    state.focus = {3};
                    RenderFrame(g, wic.get(), path(display), frame, view, client, dpi, theme.input, state, 0, false);
                    CHECK(!Control(frame, SettingsField::PercentTextVisibility).enabled);
                });
            }

            // Display at the mockup's full height (1000 x 1298 window, 40 DIP caption): the
            // segmented ring as in the mockup; stacked bars with percentages always, remaining
            // mode and the colorblind palette; then the states sheet: percentage text off (its
            // dependent rows disabled), an unordered red threshold typed (box editing, slider and
            // preview ordered), a hovered toggle.
            std::string full = std::string("render-display-full-") + theme.name + "-144.png";
            Run(full, [&] {
                const D2D1_SIZE_F tall{1000, 1258};
                SettingsView view = LayoutView(BarLayout::SegmentedRing, 144);
                view.page = SettingsUiPage::Display;
                UiFrame frame;
                SettingsUiBuildFrame(g, view, tall, frame);
                UiPaintState state;
                state.focus = {3};
                RenderFrame(g, wic.get(), path(full), frame, view, tall, 144, theme.input, state, 0, false);

                view = LayoutView(BarLayout::Stacked, 144);
                view.page = SettingsUiPage::Display;
                view.settings.percentTextVisibility = PercentTextVisibility::Always;
                view.settings.barMode = BarMode::Remaining;
                view.settings.colorblindMode = true;
                frame = {};
                SettingsUiBuildFrame(g, view, tall, frame);
                std::string bars = std::string("render-display-bars-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(bars), frame, view, tall, 144, theme.input, state, 0, false);

                TestStore store;
                store.settings.percentTextVisibility = PercentTextVisibility::Never;
                SettingsEdits edits(store);
                edits.Reload();
                edits.SetText(0, SettingsField::RedThreshold, L"60");
                view = LayoutView(BarLayout::Stacked, 144);
                view.page = SettingsUiPage::Display;
                view.settings = edits.Projection();
                view.drafts = edits.drafts;
                frame = {};
                SettingsUiBuildFrame(g, view, tall, frame);
                state = {};
                state.focus = {SettingsControlId(SettingsField::RedThreshold)};
                state.editing = SettingsControlId(SettingsField::RedThreshold);
                state.hot = {SettingsControlId(SettingsField::ShowStaleWarning)};
                std::string states = std::string("render-display-states-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(states), frame, view, tall, 144, theme.input, state, 0, false);
                CHECK(Control(frame, SettingsField::RedThreshold).text == L"60" &&
                      Control(frame, SettingsField::RedThreshold, 1).action.value == 75);
            });

            // Accounts at 144 DPI as the mockups: OpenAI expanded (the mockup's 1000 x 953 window);
            // the Add account menu open with OpenAI under the pointer, painted where the host opens
            // it; no accounts. Then the states: CC 1 expanded with a duplicate label typed (its box
            // editing, its error), the pointer on a chip and the focus ring on Remove account; Go
            // dragged by its handle between OAI and CC 1, lifted under the pointer.
            std::string expanded = std::string("render-accounts-expanded-") + theme.name + "-144.png";
            Run(expanded, [&] {
                const D2D1_SIZE_F tall{1000, 913};
                SettingsView view = AccountsView({0});
                UiFrame frame;
                SettingsUiBuildFrame(g, view, tall, frame);
                RenderFrame(g, wic.get(), path(expanded), frame, view, tall, 144, theme.input, {}, 0, false);

                view = AccountsView();
                frame = {};
                SettingsUiBuildFrame(g, view, client, frame);
                const UiItem& add = *UiFindItem(frame, SettingsControlId(SettingsField::AccountAdd));
                std::vector<UiItem> rows;
                D2D1_SIZE_F size = UiLayoutOptions(g, add.options, add.action, add.rect.right - add.rect.left, rows);
                D2D1_RECT_F box{add.rect.right - size.width, add.rect.bottom + 4, add.rect.right,
                                add.rect.bottom + 4 + size.height};
                RenderOverlay menu = [&](ID2D1RenderTarget* target, UiTargetCache& cache, const UiPalette& palette) {
                    UiFillRect(target, cache, box, 8, palette.popup, 1.5f);
                    UiStrokeRect(target, cache, box, 8, palette.controlBorder, 1, 1.5f);
                    UiPaintState hover;
                    hover.hot = {2};
                    UiPaintItems(target, g, cache, palette, rows, hover, {box.left, box.top}, 1.5f, -1e9f, 1e9f);
                };
                UiPaintState state;
                state.open = add.id;
                state.focus = {add.id};
                std::string menuName = std::string("render-accounts-add-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(menuName), frame, view, client, 144, theme.input, state, 0, false, true,
                            menu);

                view = TestView(SettingsUiPage::Accounts);
                frame = {};
                SettingsUiBuildFrame(g, view, client, frame);
                std::string empty = std::string("render-accounts-empty-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(empty), frame, view, client, 144, theme.input, {}, 0, false);

                view = AccountsView({1});
                view.drafts.push_back({12, SettingsField::AccountLabel, 1, true, L"CC 2", 0, false, false,
                                       L"Another account of this provider uses this label."});
                frame = {};
                SettingsUiBuildFrame(g, view, client, frame);
                state = {};
                state.editing = AccountId(2, SettingsField::AccountLabel);
                state.hot = {AccountId(2, SettingsField::AccountBars, kFableWeeklyBar)};
                state.focus = {AccountId(2, SettingsField::AccountRemove, 1)};
                state.focusVisible = true;
                std::string states = std::string("render-accounts-states-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(states), frame, view, client, 144, theme.input, state, 0, false);

                view = AccountsView();
                std::rotate(view.settings.accounts.begin() + 1, view.settings.accounts.begin() + 3,
                            view.settings.accounts.end());
                std::rotate(view.accounts.begin() + 1, view.accounts.begin() + 3, view.accounts.end());
                view.dragKey = 14;
                view.dragTop = 14 + 72 + 30;
                frame = {};
                SettingsUiBuildFrame(g, view, client, frame);
                state = {};
                state.hot = {AccountId(4, SettingsField::AccountMove)};
                state.pressed = state.hot;
                state.focus = {AccountId(4, SettingsField::AccountExpand)};
                state.focusVisible = true;
                std::string dragged = std::string("render-accounts-drag-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(dragged), frame, view, client, 144, theme.input, state, 0, false);
                CHECK(rows.size() == 3 && UiFindItem(frame, AccountId(4, SettingsField::AccountExpand, 1))->selected);
            });

            // Layout states at 144 DPI. Ring: the taskbar showing the sample (switch on), the
            // pointer on the second preview tile, a gallery card hovered, a slider dragged, the
            // focus ring on another. Bars, scrolled to the pace markers and spacing: the pointer
            // on the third column (its hover percentages), a slider hovered, the focus ring on a
            // swatch, a spacing box editing.
            std::string states = std::string("render-layout-states-") + theme.name + "-144.png";
            Run(states, [&] {
                SettingsView view = LayoutView(BarLayout::SegmentedRing, 144);
                view.status.visualTest = true;
                UiFrame frame;
                SettingsUiBuildFrame(g, view, client, frame);
                UiPaintState state;
                const UiItem& strip = *UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll));
                state.pointer = PreviewPointer(g, view, strip.rect, 1);
                state.hot = {SettingsControlId(SettingsField::BarLayout, 4)};
                state.pressed = {SettingsControlId(SettingsField::RingThickness, 1)};
                state.focus = {SettingsControlId(SettingsField::RingSize, 1)};
                state.focusVisible = true;
                RenderFrame(g, wic.get(), path(states), frame, view, client, 144, theme.input, state, 0, false,
                            false);

                view = LayoutView(BarLayout::Stacked, 144);
                frame = {};
                SettingsUiBuildFrame(g, view, client, frame);
                state = {};
                const UiItem& bars = *UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll));
                state.pointer = PreviewPointer(g, view, bars.rect, 2);
                state.hot = {SettingsControlId(SettingsField::PercentFontSize, 1)};
                state.focus = {SettingsControlId(SettingsField::PaceTickColor, 3)};
                state.focusVisible = true;
                state.editing = SettingsControlId(SettingsField::AccountMargin);
                std::string barStates = std::string("render-layout-states-bars-") + theme.name + "-144.png";
                float end = frame.contentHeight - (frame.viewport.bottom - frame.viewport.top);
                RenderFrame(g, wic.get(), path(barStates), frame, view, client, 144, theme.input, state, end, true,
                            false);
                CHECK(true);
            });

            // Overflow: bars too wide for the strip (left fade), then scrolled (both fades); a
            // window too short for the pinned preview, scrolled.
            std::string overflow = std::string("render-layout-overflow-") + theme.name + "-144.png";
            Run(overflow, [&] {
                SettingsView view = LayoutView(BarLayout::Stacked, 144);
                view.settings.barLength = 180;
                UiFrame frame;
                SettingsUiBuildFrame(g, view, client, frame);
                const UiItem& strip = *UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll));
                CHECK(strip.maximum > 0);
                RenderFrame(g, wic.get(), path(overflow), frame, view, client, 144, theme.input, {}, 0, false);
                view.previewScroll = float(strip.maximum) / 2;
                frame = {};
                SettingsUiBuildFrame(g, view, client, frame);
                std::string scrolled = std::string("render-layout-overflow-scrolled-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(scrolled), frame, view, client, 144, theme.input, {}, 0, false);

                const D2D1_SIZE_F shortClient{1000, 440};
                view = LayoutView(BarLayout::SegmentedRing, 144);
                frame = {};
                SettingsUiBuildFrame(g, view, shortClient, frame);
                UiSurfaceId surface = UiSurfaceId::Main;
                UiFindItem(frame, SettingsControlId(SettingsField::PreviewScroll), &surface);
                CHECK(surface == UiSurfaceId::Content);
                std::string unpinned = std::string("render-layout-unpinned-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(unpinned), frame, view, shortClient, 144, theme.input, {}, 60, false);
            });

            // Preview strips per variant: 100%, 125% and 150% on the dark taskbar, 125% on the
            // others.
            for (UINT dpi : {96u, 120u, 144u}) {
                if (dpi != 120 && std::string(theme.name) != "dark") continue;
                for (bool display : {false, true}) {
                    std::string sheet = std::string(display ? "render-display-preview-sheet-" : "render-preview-sheet-") +
                                        theme.name + "-" + std::to_string(dpi) + ".png";
                    Run(sheet, [&] {
                        RenderPreviewSheet(g, wic.get(), path(sheet), theme.input, dpi, display);
                        CHECK(true);
                    });
                }
            }

            // Controls in their states at 144 DPI: the Behavior page with Custom chosen, an
            // invalid draft with its error, a toggle off, the Show on popup open (the field
            // pressed), the custom box editing, a hovered toggle and the keyboard focus ring on
            // another; then History with recording off (a disabled box and its reason); then the
            // popup's rows with the pointer on the third.
            std::string controls = std::string("render-controls-") + theme.name + "-144.png";
            Run(controls, [&] {
                SettingsView view = TestView(SettingsUiPage::Behavior);
                view.settings.showDetailsOnHover = false;
                view.pollCustom = true;
                view.drafts.push_back({0, SettingsField::PollMinutes, 1, true, L"7x", 10, false, false,
                                       L"Enter a whole number from 2 to 1440."});
                UiFrame frame;
                SettingsUiBuildFrame(g, view, client, frame);
                UiPaintState state;
                state.open = SettingsControlId(SettingsField::ShowOn);
                state.editing = SettingsControlId(SettingsField::PollMinutes);
                state.hot = {SettingsControlId(SettingsField::SkipHiddenPolling)};
                state.focus = {SettingsControlId(SettingsField::Notifications)};
                state.focusVisible = true;
                RenderFrame(g, wic.get(), path(controls), frame, view, client, 144, theme.input, state, 0,
                            false);

                view = TestView(SettingsUiPage::History);
                view.settings.historyEnabled = false;
                frame = {};
                SettingsUiBuildFrame(g, view, client, frame);
                std::string off = std::string("render-history-off-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(off), frame, view, client, 144, theme.input, {}, 0, false);

                // The popup surface as ui/popup.h paints it.
                view = TestView(SettingsUiPage::Behavior);
                frame = {};
                SettingsUiBuildFrame(g, view, client, frame);
                const UiItem& field = *UiFindItem(frame, SettingsControlId(SettingsField::ShowOn));
                std::vector<UiItem> rows;
                D2D1_SIZE_F size = UiLayoutOptions(g, field.options, field.action,
                                                   field.rect.right - field.rect.left, rows);
                winrt::com_ptr<IWICBitmap> bitmap = RenderBitmap(wic.get(), size, 144);
                winrt::com_ptr<ID2D1RenderTarget> target = RenderTarget(g, bitmap.get(), 144);
                UiTargetCache cache;
                UiPalette palette = UiPaletteFor(theme.input);
                UiPaintState popupState;
                popupState.hot = {3};
                target->BeginDraw();
                target->Clear(palette.popup);
                UiPaintItems(target.get(), g, cache, palette, rows, popupState, {0, 0}, 1.5f, -1e9f, 1e9f);
                winrt::check_hresult(target->EndDraw());
                cache.Clear();
                std::string popup = std::string("render-popup-") + theme.name + "-144.png";
                SaveRender(wic.get(), bitmap.get(), path(popup));
                std::cout << "RENDER " << popup << '\n';
                CHECK(rows.size() == 4 && rows[0].selected);
            });

            // Interaction states at 144 DPI on a short window, scrolled: a hovered link card, the
            // keyboard focus ring on an inline link, a button row and the expanded scrollbar;
            // then a hovered license link.
            std::string name = std::string("render-states-") + theme.name + "-144.png";
            Run(name, [&] {
                SettingsView view;
                view.page = SettingsUiPage::About;
                view.version = L"2.0.1";
                UiFrame frame;
                const D2D1_SIZE_F shortClient{1000, 560};
                SettingsUiBuildFrame(g, view, shortClient, frame);
                uint32_t id = 900;
                AddButtonRow(frame.content, id, 20, frame.contentHeight);
                frame.contentHeight += 32 + 28;
                UiPaintState state;
                uint32_t linkCard = 0, logos = 0, license = 0;
                for (const UiItem& item : frame.content) {
                    if (item.kind == UiItemKind::Card && item.action.kind != UiActionKind::None && !linkCard) {
                        linkCard = item.id;
                    }
                    if (item.runs.size() == 3) logos = item.id;
                    if (item.kind == UiItemKind::Link) license = item.id;
                }
                state.hot = {linkCard};
                state.focus = {logos, 0};
                state.focusVisible = true;
                RenderFrame(g, wic.get(), path(name), frame, view, shortClient, 144, theme.input, state, 120, true);
                UiPaintState linkState;
                linkState.hot = {license};
                std::string second = std::string("render-states-link-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(second), frame, view, shortClient, 144, theme.input, linkState, 300,
                            false);
                CHECK(linkCard && logos && license);
            });
        }
    }
    CoUninitialize();
}
