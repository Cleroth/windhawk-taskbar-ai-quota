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
// `backdrop`: over a stand-in backdrop, as the host draws with one.
static void RenderFrame(UiGraphics& g, IWICImagingFactory* wic, const std::wstring& path,
                        const UiFrame& frame, const SettingsView& view, D2D1_SIZE_F client, UINT dpi,
                        const UiThemeInput& theme, UiPaintState state, float scroll,
                        bool scrollbarExpanded, bool pointerContent = true, const RenderOverlay& overlay = {},
                        bool backdrop = false) {
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
    if (backdrop) {
        RenderBackdrop(target.get(), {0, 0, client.width, client.height}, palette);
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    } else {
        target->Clear(palette.background);
    }
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
    SettingsPreviewStrip strip = SettingsPreviewLayout(g, view.settings, view.locale, view.status.taskbarHeight,
                                                       view.scale, view.now, false, false, 0);
    float left = r.left + SettingsPreviewRowAt(strip, r.right - r.left, view.previewScroll).tiles;
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
// styles and colors, extreme sizes, overflow (scrolled and not), hover, a 32 DIP taskbar, the
// sample identities (label, badged logos, a stale logo) on stacked and vertical bars at every
// identity position and two identity gaps, the hover box around each form (grown by tall vertical
// bars), tile fills near their ends and at their minimums, sparkline chart sizes, text sizes,
// current position cues and extra usage corners, logo sizes with the badge's size
// and offsets on bars, rings and headline tiles, badge colors and logo brand colors. Display sheet: percentage text never/hover/always, each alignment, bar modes
// (CC 2's weekly quota is past 100%), amounts, the colorblind palette, thresholds (ordered,
// unordered, extreme) and the stale marker (Go is stale) on bars and tiles. Placement sheet: ring
// tiles at each position, stacked bars at each (wider than the strip, at home), stacked bars
// scrolled to the other end, hover beside Start and in the tray, 32 and 72 DIP taskbars. I18n
// sheet, in `locale`: the localized text on the taskbar (bar keys, the headline's key and
// countdown, the gauge's key, chart keys and the weekly chart's extra usage key, the matrix's keys
// with their reserve). Each strip is a Preview item painted by the production painter.
enum class PreviewSheet : uint8_t { Layout, Display, Placement, I18n };

static void RenderPreviewSheet(UiGraphics& g, IWICImagingFactory* wic, const std::wstring& path,
                               const UiThemeInput& theme, UINT dpi, PreviewSheet sheet,
                               const I18nLocale& locale = {}) {
    struct Row {
        std::wstring caption;
        std::function<void(Settings&)> edit;
        int hover = -1;
        double taskbar = 48;
        float scroll = 0;
    };
    auto always = [](Settings& s) {
        s.percentTextVisibility = s.verticalPercentTextVisibility = PercentTextVisibility::Always;
    };
    auto thresholds = [](Settings& s, int yellow, int orange, int red) {
        s.yellowThreshold = yellow;
        s.orangeThreshold = orange;
        s.redThreshold = red;
    };
    // Custom: a light mint below yellow (dark text), navy, magenta and near-black from yellow on.
    auto custom = [](Settings& s) {
        s.usagePalette = UsagePalette::Custom;
        s.customUsageColors = {RGB(0xB8, 0xF0, 0xC8), RGB(0x30, 0x40, 0x90), RGB(0xC0, 0x40, 0xC0), RGB(0x20, 0x20, 0x20)};
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
        {L"Colorblind", [&](Settings& s) { always(s); s.usagePalette = UsagePalette::Colorblind; }},
        {L"Colorblind, remaining",
         [&](Settings& s) { always(s); s.usagePalette = UsagePalette::Colorblind; s.barMode = BarMode::Remaining; }},
        {L"Custom, left (mint, navy, magenta, near-black)",
         [&](Settings& s) { always(s); custom(s); s.percentTextAlignment = PercentTextAlignment::Left; }},
        {L"Stale marker off", [&](Settings& s) { always(s); s.showStaleWarning = false; }},
        {L"Provider outages off", [&](Settings& s) { always(s); s.showProviderOutages = false; }},
        {L"Label 15, logo 50%, outage mark (colorblind)", [&](Settings& s) {
             always(s);
             s.labelFontSize = 15;
             s.logoScale = 50;
             s.usagePalette = UsagePalette::Colorblind;
         }},
        {L"Thresholds 20 / 40 / 60", [&](Settings& s) { always(s); thresholds(s, 20, 40, 60); }},
        {L"Unordered 80 / 30 / 20", [&](Settings& s) { always(s); thresholds(s, 80, 30, 20); }},
        {L"Thresholds 0 / 0 / 0", [&](Settings& s) { always(s); thresholds(s, 0, 0, 0); }},
        {L"Thresholds 100 / 100 / 100", [&](Settings& s) { always(s); thresholds(s, 100, 100, 100); }},
        {L"Vertical, colorblind",
         [&](Settings& s) { always(s); s.barLayout = BarLayout::Vertical; s.usagePalette = UsagePalette::Colorblind; }},
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
        {L"Ring 24, provider outages off",
         [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.ringSize = 24; s.showProviderOutages = false; }},
        {L"Ring, colorblind", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.usagePalette = UsagePalette::Colorblind; }},
        {L"Ring, custom", [&](Settings& s) { s.barLayout = BarLayout::SegmentedRing; custom(s); }},
        {L"Usage graph, custom", [&](Settings& s) { s.barLayout = BarLayout::Sparkline; custom(s); }},
        {L"Ring, remaining", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.barMode = BarMode::Remaining; }},
        {L"Concentric, 20 / 40 / 60",
         [&](Settings& s) { s.barLayout = BarLayout::ConcentricRings; thresholds(s, 20, 40, 60); }},
        {L"Vertical 32, identity top, colorblind", [](Settings& s) {
             s.barLayout = BarLayout::Vertical;
             s.verticalBarLength = 32;
             s.labelPosition = LabelPosition::Top;
             s.usagePalette = UsagePalette::Colorblind;
         }, -1, 72},
        {L"Headline, colorblind",
         [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.usagePalette = UsagePalette::Colorblind; }},
        {L"Headline, custom", [&](Settings& s) { s.barLayout = BarLayout::LogoHeadline; custom(s); }},
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
        {L"Colorblind", [&](Settings& s) { always(s); s.usagePalette = UsagePalette::Colorblind; }},
        {L"Pace line", [](Settings& s) { s.paceTickStyle = PaceTickStyle::Line; }},
        {L"Pace notch", [](Settings& s) { s.paceTickStyle = PaceTickStyle::Notch; }},
        {L"Pace dot", [](Settings& s) { s.paceTickStyle = PaceTickStyle::Dot; }},
        {L"Pace time track", [](Settings& s) { s.paceTickStyle = PaceTickStyle::TimeTrack; }},
        {L"Time track, remaining",
         [](Settings& s) { s.paceTickStyle = PaceTickStyle::TimeTrack; s.barMode = BarMode::Remaining; }},
        {L"Time track, thin rail",
         [](Settings& s) { s.paceTickStyle = PaceTickStyle::TimeTrack; s.barStyle = BarStyle::Rail; }},
        {L"Time track, bars 60 x 14", [](Settings& s) {
             s.paceTickStyle = PaceTickStyle::TimeTrack;
             s.barLength = 60;
             s.barThickness = 14;
         }},
        {L"Time track, bars 10 x 2",
         [](Settings& s) { s.paceTickStyle = PaceTickStyle::TimeTrack; s.barLength = 10; s.barThickness = 2; }},
        {L"Time track, vertical 32", [](Settings& s) {
             s.paceTickStyle = PaceTickStyle::TimeTrack;
             s.barLayout = BarLayout::Vertical;
             s.verticalBarLength = 32;
         }},
        {L"Pace pink", [](Settings& s) { s.paceTickColor = RGB(0xFF, 0x6E, 0xC7); }},
        {L"Pace off", [](Settings& s) { s.showPaceTicks = false; }},
        {L"Gap 0, no overhang", [](Settings& s) { s.barGap = 0; }},
        {L"Gap 4, line", [](Settings& s) { s.barGap = 4; s.paceTickStyle = PaceTickStyle::Line; }},
        {L"Thin rail", [](Settings& s) { s.barStyle = BarStyle::Rail; }},
        {L"Thin rail, line", [](Settings& s) { s.barStyle = BarStyle::Rail; s.paceTickStyle = PaceTickStyle::Line; }},
        {L"Thin rail, notch", [](Settings& s) { s.barStyle = BarStyle::Rail; s.paceTickStyle = PaceTickStyle::Notch; }},
        {L"Thin rail 60 x 14", [&](Settings& s) {
             always(s);
             s.barStyle = BarStyle::Rail;
             s.barLength = 60;
             s.barThickness = 14;
         }},
        {L"Vertical, length 32", [](Settings& s) { s.barLayout = BarLayout::Vertical; s.verticalBarLength = 32; }},
        {L"Vertical 32, line", [](Settings& s) {
             s.barLayout = BarLayout::Vertical;
             s.verticalBarLength = 32;
             s.paceTickStyle = PaceTickStyle::Line;
         }},
        {L"Thin rail, vertical 32", [](Settings& s) {
             s.barStyle = BarStyle::Rail;
             s.barLayout = BarLayout::Vertical;
             s.verticalBarLength = 32;
         }},
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
        // Fills near their ends and minimum fills: yellow at 6 puts the green samples at 3% used
        // (97% remaining), at 8 at 4%, at 2 at 1%.
        {L"Ring 4, near full (97%)", [&](Settings& s) {
             s.barLayout = BarLayout::SegmentedRing;
             s.ringThickness = 4;
             s.barMode = BarMode::Remaining;
             thresholds(s, 6, 75, 90);
         }},
        {L"Ring 4, small fills (4%)", [&](Settings& s) {
             s.barLayout = BarLayout::SegmentedRing;
             s.ringThickness = 4;
             thresholds(s, 8, 75, 90);
         }},
        {L"Ring 24 / 6, 5\u00B0 fills", [&](Settings& s) {
             s.barLayout = BarLayout::SegmentedRing;
             s.ringSize = 24;
             s.ringThickness = 6;
             thresholds(s, 2, 75, 90);
         }},
        {L"Concentric 4, near full", [&](Settings& s) {
             s.barLayout = BarLayout::ConcentricRings;
             s.ringThickness = 4;
             s.barMode = BarMode::Remaining;
             thresholds(s, 6, 75, 90);
         }},
        {L"Concentric 40 / 6", [](Settings& s) {
             s.barLayout = BarLayout::ConcentricRings;
             s.ringSize = 40;
             s.ringThickness = 6;
         }},
        // The sample identities (OAI's label, the Anthropic pair's badged logos, Go's logo with
        // its stale mark) at each identity position, Stacked (60 DIP bars, so every column fits;
        // Top and Bottom also with 15 DIP labels) and Vertical; the identity gap; the hover box
        // around each form.
        {L"Bars 60, identity left", [](Settings& s) { s.barLength = 60; }},
        {L"Bars 60, identity right", [](Settings& s) { s.barLength = 60; s.labelPosition = LabelPosition::Right; }},
        {L"Bars 60, identity top",
         [](Settings& s) { s.barLength = 60; s.labelPosition = LabelPosition::Top; }, -1, 72},
        {L"Bars 60, identity bottom",
         [](Settings& s) { s.barLength = 60; s.labelPosition = LabelPosition::Bottom; }, -1, 72},
        {L"Bars 60, label 15, top", [](Settings& s) {
             s.barLength = 60;
             s.labelFontSize = 15;
             s.labelPosition = LabelPosition::Top;
         }, -1, 72},
        {L"Bars 60, label 15, bottom", [](Settings& s) {
             s.barLength = 60;
             s.labelFontSize = 15;
             s.labelPosition = LabelPosition::Bottom;
         }, -1, 72},
        {L"Bars 60, identity hidden", [](Settings& s) { s.barLength = 60; s.labelPosition = LabelPosition::Hidden; }},
        {L"Bars 60, identity gap 0", [](Settings& s) { s.barLength = 60; s.labelGap = 0; }},
        {L"Bars 60, gap 8, top", [](Settings& s) {
             s.barLength = 60;
             s.labelGap = 8;
             s.labelPosition = LabelPosition::Top;
         }, -1, 80},
        {L"Vertical 32, identity left", [](Settings& s) { s.barLayout = BarLayout::Vertical; s.verticalBarLength = 32; }},
        {L"Vertical 32, identity right", [](Settings& s) {
             s.barLayout = BarLayout::Vertical;
             s.verticalBarLength = 32;
             s.labelPosition = LabelPosition::Right;
         }},
        {L"Vertical 32, identity top", [](Settings& s) {
             s.barLayout = BarLayout::Vertical;
             s.verticalBarLength = 32;
             s.labelPosition = LabelPosition::Top;
         }, -1, 72},
        {L"Vertical 32, identity bottom", [](Settings& s) {
             s.barLayout = BarLayout::Vertical;
             s.verticalBarLength = 32;
             s.labelPosition = LabelPosition::Bottom;
         }, -1, 72},
        {L"Vertical 32, identity hidden", [](Settings& s) {
             s.barLayout = BarLayout::Vertical;
             s.verticalBarLength = 32;
             s.labelPosition = LabelPosition::Hidden;
         }},
        {L"Hover CC 1 (badged logo)", [](Settings&) {}, 1},
        {L"Bars 60, hover OAI (label)", [](Settings& s) { s.barLength = 60; }, 0},
        {L"Identity top, hover Go (stale)", [](Settings& s) { s.labelPosition = LabelPosition::Top; }, 3, 72},
        {L"Identity hidden, hover CC 2", [](Settings& s) { s.labelPosition = LabelPosition::Hidden; }, 2},
        {L"Vertical 32, hover CC 1", [](Settings& s) { s.barLayout = BarLayout::Vertical; s.verticalBarLength = 32; }, 1},
        {L"Vertical 80, hover Go (box grows)", [](Settings& s) {
             s.barLayout = BarLayout::Vertical;
             s.verticalBarLength = 80;
         }, 3, 96},
        {L"Hover CC 1, 32 DIP taskbar", [](Settings&) {}, 1, 32},
        {L"Headline %", [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; }},
        {L"Headline, remaining",
         [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.barMode = BarMode::Remaining; }},
        {L"Headline, near full", [&](Settings& s) {
             s.barLayout = BarLayout::LogoHeadline;
             s.barMode = BarMode::Remaining;
             thresholds(s, 6, 75, 90);
         }},
        // Headline sizes: the number and its bars at their extremes and with the bar gap.
        {L"Headline, number 20, bars 24 x 6", [](Settings& s) {
             s.barLayout = BarLayout::LogoHeadline;
             s.headlineFontSize = 20;
             s.headlineBarLength = 24;
             s.headlineBarThickness = 6;
         }},
        {L"Headline, number 10, bars 4 x 2, gap 0", [](Settings& s) {
             s.barLayout = BarLayout::LogoHeadline;
             s.headlineFontSize = 10;
             s.headlineBarLength = 4;
             s.headlineBarThickness = 2;
             s.barGap = 0;
         }},
        {L"Headline, number 24, bars 60 x 10, gap 4", [](Settings& s) {
             s.barLayout = BarLayout::LogoHeadline;
             s.headlineFontSize = 24;
             s.headlineBarLength = 60;
             s.headlineBarThickness = 10;
             s.barGap = 4;
         }},
        {L"Headline, 32 DIP taskbar", [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; }, -1, 32},
        // The reset countdown in place of the headline's segments; gauges, clock pies and rounded
        // squares at their defaults and extremes.
        {L"Headline, countdown",
         [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.headlineCountdown = true; }},
        {L"Headline, countdown, number 24", [](Settings& s) {
             s.barLayout = BarLayout::LogoHeadline;
             s.headlineCountdown = true;
             s.headlineFontSize = 24;
         }},
        {L"Headline, countdown, 32 DIP taskbar",
         [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.headlineCountdown = true; }, -1, 32},
        {L"Gauge %", [](Settings& s) { s.barLayout = BarLayout::Gauge; }},
        {L"Gauge, remaining", [](Settings& s) { s.barLayout = BarLayout::Gauge; s.barMode = BarMode::Remaining; }},
        {L"Gauge 24 / 6", [](Settings& s) { s.barLayout = BarLayout::Gauge; s.ringSize = 24; s.ringThickness = 6; }},
        {L"Gauge 40 / 1", [](Settings& s) { s.barLayout = BarLayout::Gauge; s.ringSize = 40; s.ringThickness = 1; }},
        {L"Gauge, 32 DIP taskbar", [](Settings& s) { s.barLayout = BarLayout::Gauge; }, -1, 32},
        {L"Clock pies", [](Settings& s) { s.barLayout = BarLayout::ClockPies; }},
        {L"Pies, remaining", [](Settings& s) { s.barLayout = BarLayout::ClockPies; s.barMode = BarMode::Remaining; }},
        {L"Pies 40", [](Settings& s) { s.barLayout = BarLayout::ClockPies; s.ringSize = 40; }},
        {L"Pies, 32 DIP taskbar", [](Settings& s) { s.barLayout = BarLayout::ClockPies; }, -1, 32},
        {L"Rounded square", [](Settings& s) { s.barLayout = BarLayout::RoundedSquare; }},
        {L"Square, remaining",
         [](Settings& s) { s.barLayout = BarLayout::RoundedSquare; s.barMode = BarMode::Remaining; }},
        {L"Square 24 / 6",
         [](Settings& s) { s.barLayout = BarLayout::RoundedSquare; s.ringSize = 24; s.ringThickness = 6; }},
        {L"Square 40 / 1",
         [](Settings& s) { s.barLayout = BarLayout::RoundedSquare; s.ringSize = 40; s.ringThickness = 1; }},
        {L"Square, near full", [&](Settings& s) {
             s.barLayout = BarLayout::RoundedSquare;
             s.barMode = BarMode::Remaining;
             thresholds(s, 6, 75, 90);
         }},
        {L"Square, 32 DIP taskbar", [](Settings& s) { s.barLayout = BarLayout::RoundedSquare; }, -1, 32},
        // Sparklines (the visual test samples' history; the OpenAI sample has none; OAI's credits
        // and CC 2's extra usage in their weekly charts' corners) and the quota matrix at their
        // defaults and extremes.
        {L"Usage graph", [](Settings& s) { s.barLayout = BarLayout::Sparkline; }},
        {L"Sparkline, remaining", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.barMode = BarMode::Remaining; }},
        {L"Sparkline, pace off", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.showPaceTicks = false; }},
        {L"Sparkline, extra off",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineExtra = SparklineExtra::Off; }},
        {L"Sparkline, extra percent",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineExtra = SparklineExtra::Percent; }},
        {L"Sparkline, percent, remaining", [](Settings& s) {
             s.barLayout = BarLayout::Sparkline;
             s.sparklineExtra = SparklineExtra::Percent;
             s.barMode = BarMode::Remaining;
         }},
        {L"Sparkline 24 wide, 50%",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineWidth = 24; s.sparklineHeight = 50; }},
        {L"Sparkline 24 wide, 100%",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineWidth = 24; s.sparklineHeight = 100; }},
        {L"Sparkline 120 wide, 100%",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineWidth = 120; s.sparklineHeight = 100; }},
        {L"Sparkline, all cues", [](Settings& s) {
             s.barLayout = BarLayout::Sparkline;
             s.sparklineNowCaret = true;
             s.sparklineRing = true;
         }},
        {L"Sparkline, caret on", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineNowCaret = true; }},
        {L"Sparkline, ring off", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineRing = false; }},
        {L"Sparkline, ring at 50%", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineReadingScale = 50; }},
        {L"Sparkline, ring at 200%", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineReadingScale = 200; }},
        {L"Sparkline, dot at 200%", [](Settings& s) {
             s.barLayout = BarLayout::Sparkline;
             s.sparklineRing = false;
             s.sparklineReadingScale = 200;
         }},
        {L"Sparkline, no cues", [](Settings& s) {
             s.barLayout = BarLayout::Sparkline;
             s.sparklineElapsed = false;
             s.sparklineNowLine = false;
             s.sparklineRing = false;
         }},
        {L"Sparkline, 12 hours each side",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineSpan = SparklineSpan::Hours12; }},
        {L"Sparkline, 24 hours each side",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineSpan = SparklineSpan::Hours24; }},
        {L"Sparkline, 48 hours each side",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineSpan = SparklineSpan::Hours48; }},
        {L"Sparkline, 24 hours each side, remaining", [](Settings& s) {
             s.barLayout = BarLayout::Sparkline;
             s.sparklineSpan = SparklineSpan::Hours24;
             s.barMode = BarMode::Remaining;
         }},
        {L"Sparkline, outline off", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineOutline = false; }},
        {L"Sparkline, outline and cues off", [](Settings& s) {
             s.barLayout = BarLayout::Sparkline;
             s.sparklineOutline = false;
             s.sparklineElapsed = false;
             s.sparklineNowLine = false;
             s.sparklineRing = false;
         }},
        {L"Sparkline, text 6", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineTextSize = 6; }},
        {L"Sparkline, text 12", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineTextSize = 12; }},
        {L"Sparkline, colorblind", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.usagePalette = UsagePalette::Colorblind; }},
        {L"Sparkline, 32 DIP taskbar", [](Settings& s) { s.barLayout = BarLayout::Sparkline; }, -1, 32},
        {L"Sparkline 50%, 32 DIP taskbar",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineHeight = 50; }, -1, 32},
        {L"Sparkline 100%, 32 DIP taskbar",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineHeight = 100; }, -1, 32},
        {L"Quota matrix", [](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; }},
        {L"Matrix, remaining", [](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; s.barMode = BarMode::Remaining; }},
        {L"Matrix, number 10", [](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; s.headlineFontSize = 10; }},
        {L"Matrix, number 24", [](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; s.headlineFontSize = 24; }},
        {L"Matrix, 32 DIP taskbar", [](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; }, -1, 32},
        // Logo sizes and the number badge's size and offsets per style.
        {L"Bars 60, logo 50%", [](Settings& s) { s.barLength = 60; s.logoScale = 50; }},
        {L"Bars 60, logo 150%, badge 12", [](Settings& s) { s.barLength = 60; s.logoScale = 150; s.badgeSize = 12; }},
        {L"Bars 60, logo 200%, top", [](Settings& s) {
             s.barLength = 60;
             s.logoScale = 200;
             s.labelPosition = LabelPosition::Top;
         }, -1, 96},
        {L"Bars 60, badge 16, offset -4 / -4",
         [](Settings& s) { s.barLength = 60; s.badgeSize = 16; s.badgeOffsetX = -4; s.badgeOffsetY = -4; }},
        {L"Bars 60, badge offset -20 / 0", [](Settings& s) { s.barLength = 60; s.badgeOffsetX = -20; }},
        {L"Bars 60, badge offset 0 / 10", [](Settings& s) { s.barLength = 60; s.badgeOffsetY = 10; }},
        {L"Bars 60, badge offset 0 / -10", [](Settings& s) { s.barLength = 60; s.badgeOffsetY = -10; }},
        {L"Ring, logo 150%", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.logoScale = 150; }},
        {L"Ring, badge 12", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; s.badgeSize = 12; }},
        {L"Ring, badge offset 4 / 4", [](Settings& s) {
             s.barLayout = BarLayout::SegmentedRing;
             s.badgeOffsetX = 4;
             s.badgeOffsetY = 4;
         }},
        {L"Concentric 40 / 6, badge 12", [](Settings& s) {
             s.barLayout = BarLayout::ConcentricRings;
             s.ringSize = 40;
             s.ringThickness = 6;
             s.badgeSize = 12;
         }},
        {L"Headline, logo 150%, badge 11",
         [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.logoScale = 150; s.badgeSize = 11; }},
        // Badge colors: each swatch past the default (the taskbar's text color), on bars and rings.
        {L"Bars 60, badge light blue",
         [](Settings& s) { s.barLength = 60; s.badgeColor = RGB(0x4C, 0xC2, 0xFF); }},
        {L"Bars 60, badge dark", [](Settings& s) { s.barLength = 60; s.badgeColor = RGB(0x1C, 0x1C, 0x1C); }},
        {L"Bars 60, badge pink", [](Settings& s) { s.barLength = 60; s.badgeColor = RGB(0xFF, 0x6E, 0xC7); }},
        {L"Ring, badge 12, light blue", [](Settings& s) {
             s.barLayout = BarLayout::SegmentedRing;
             s.badgeSize = 12;
             s.badgeColor = RGB(0x4C, 0xC2, 0xFF);
         }},
        // Logo brand colors (high contrast: the text color): Claude's orange and the Gemini
        // gradient on bars, rings and headline tiles; each alone.
        {L"Bars 60, brand logos", [](Settings& s) { s.barLength = 60; s.logoBrands = {true, true}; }},
        {L"Ring, logo 150%, brand logos", [](Settings& s) {
             s.barLayout = BarLayout::SegmentedRing;
             s.logoScale = 150;
             s.logoBrands = {true, true};
         }},
        {L"Headline, logo 200%, brand logos", [](Settings& s) {
             s.barLayout = BarLayout::LogoHeadline;
             s.logoScale = 200;
             s.logoBrands = {true, true};
         }},
        {L"Bars 60, orange Claude only", [](Settings& s) { s.barLength = 60; s.logoBrands.claude = true; }},
        {L"Bars 60, Gemini gradient only", [](Settings& s) { s.barLength = 60; s.logoBrands.gemini = true; }},
    };
    auto labels = [](Settings& s) { s.showBarLabels = true; };
    const std::vector<Row> i18nRows = {
        {L"Bars, keys", labels},
        {L"Bars, keys, percent always", [&](Settings& s) { labels(s); always(s); }},
        {L"Bars, keys at 14", [&](Settings& s) { labels(s); s.percentFontSize = 14; }},
        {L"Vertical bars, keys", [&](Settings& s) { labels(s); s.barLayout = BarLayout::Vertical; }},
        {L"Vertical, keys, percent always",
         [&](Settings& s) { labels(s); always(s); s.barLayout = BarLayout::Vertical; }},
        {L"Bars, keys, 32 DIP taskbar", labels, -1, 32},
        {L"Segmented ring", [](Settings& s) { s.barLayout = BarLayout::SegmentedRing; }},
        {L"Concentric rings", [](Settings& s) { s.barLayout = BarLayout::ConcentricRings; }},
        {L"Clock pies", [](Settings& s) { s.barLayout = BarLayout::ClockPies; }},
        {L"Rounded square", [](Settings& s) { s.barLayout = BarLayout::RoundedSquare; }},
        {L"Headline", [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; }},
        {L"Headline, countdown",
         [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.headlineCountdown = true; }},
        {L"Headline, countdown, number 24", [](Settings& s) {
             s.barLayout = BarLayout::LogoHeadline;
             s.headlineCountdown = true;
             s.headlineFontSize = 24;
         }},
        {L"Headline, countdown, 32 DIP taskbar",
         [](Settings& s) { s.barLayout = BarLayout::LogoHeadline; s.headlineCountdown = true; }, -1, 32},
        {L"Gauge %", [](Settings& s) { s.barLayout = BarLayout::Gauge; }},
        {L"Gauge, ring 24", [](Settings& s) { s.barLayout = BarLayout::Gauge; s.ringSize = 24; }},
        {L"Gauge, ring 40, thickness 6", [](Settings& s) {
             s.barLayout = BarLayout::Gauge;
             s.ringSize = 40;
             s.ringThickness = 6;
         }},
        {L"Usage graph", [](Settings& s) { s.barLayout = BarLayout::Sparkline; }},
        {L"Sparkline, text 12", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineTextSize = 12; }},
        {L"Usage graph, extra percent",
         [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.sparklineExtra = SparklineExtra::Percent; }},
        {L"Usage graph, remaining", [](Settings& s) { s.barLayout = BarLayout::Sparkline; s.barMode = BarMode::Remaining; }},
        {L"Quota matrix", [](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; }},
        {L"Matrix, number 24", [](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; s.headlineFontSize = 24; }},
        {L"Matrix, 32 DIP taskbar", [](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; }, -1, 32},
    };
    std::vector<Row> placementRows;
    const std::pair<PCWSTR, TaskbarPlacement> positions[] = {
        {L"before icons", TaskbarPlacement::BeforeIcons},     {L"before network", TaskbarPlacement::BeforeNetwork},
        {L"before clock", TaskbarPlacement::BeforeClock},     {L"after clock", TaskbarPlacement::AfterClock},
        {L"after Show desktop", TaskbarPlacement::AfterShowDesktop}, {L"left side", TaskbarPlacement::LeftSide},
        {L"right of Start", TaskbarPlacement::RightOfStart}};
    auto place = [](BarLayout layout, TaskbarPlacement placement) {
        return [layout, placement](Settings& s) {
            s.barLayout = layout;
            s.placement = placement;
        };
    };
    for (BarLayout layout : {BarLayout::SegmentedRing, BarLayout::Stacked}) {
        for (auto [name, placement] : positions) {
            placementRows.push_back(
                {std::wstring(layout == BarLayout::Stacked ? L"Bars, " : L"Ring, ") + name, place(layout, placement)});
        }
    }
    placementRows.push_back({L"Bars, before icons, left end", place(BarLayout::Stacked, TaskbarPlacement::BeforeIcons),
                             -1, 48, 1e4f});
    placementRows.push_back({L"Bars, left side, right end", place(BarLayout::Stacked, TaskbarPlacement::LeftSide), -1, 48,
                             -1e4f});
    placementRows.push_back({L"Ring, right of Start, hover", place(BarLayout::SegmentedRing, TaskbarPlacement::RightOfStart),
                             1});
    placementRows.push_back({L"Ring, after clock, hover", place(BarLayout::SegmentedRing, TaskbarPlacement::AfterClock), 3});
    placementRows.push_back({L"Ring, left side, 32 DIP", place(BarLayout::SegmentedRing, TaskbarPlacement::LeftSide), -1,
                             32});
    placementRows.push_back({L"Ring, after desktop, 32 DIP",
                             place(BarLayout::SegmentedRing, TaskbarPlacement::AfterShowDesktop), -1, 32});
    placementRows.push_back({L"Ring, before clock, 72 DIP", place(BarLayout::SegmentedRing, TaskbarPlacement::BeforeClock),
                             -1, 72});
    // The Layout and Placement sheets' strips are the window's 680 DIPs; the Display and I18n
    // sheets' fit four columns of stacked bars, so every value shows.
    const bool display = sheet == PreviewSheet::Display;
    const std::vector<Row>& rows = display                         ? displayRows :
                                   sheet == PreviewSheet::Layout   ? layoutRows :
                                   sheet == PreviewSheet::I18n     ? i18nRows : placementRows;
    constexpr float kCaption = 170, kRowGap = 12;
    const float kStrip = display || sheet == PreviewSheet::I18n ? 800 : 680;
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
        view.locale = locale;
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

// The Layout page's tile style gallery alone, in `tag`'s language: its cards as the page lays
// them out at `client` (each style's thumbnail by the production painter, its name under it).
static void RenderGallerySheet(UiGraphics& g, IWICImagingFactory* wic, const std::wstring& path, UINT dpi,
                               std::string_view tag, D2D1_SIZE_F client) {
    SettingsView view = I18nLayoutLocalized(I18nLayoutView(BarLayout::Stacked), tag);
    view.scale = float(dpi) / 96;
    UiFrame frame;
    SettingsUiBuildFrame(g, view, client, frame);
    std::vector<UiItem> cards;
    D2D1_RECT_F bounds{1e9f, 1e9f, -1e9f, -1e9f};
    for (const UiItem& item : frame.content) {
        if (item.kind != UiItemKind::Gallery) continue;
        cards.push_back(item);
        bounds = {std::min(bounds.left, item.rect.left), std::min(bounds.top, item.rect.top),
                  std::max(bounds.right, item.rect.right), std::max(bounds.bottom, item.rect.bottom)};
    }
    CHECK(cards.size() == std::size(kSettingsTileStyles));
    for (UiItem& card : cards) {
        card.rect = {card.rect.left - bounds.left + 8, card.rect.top - bounds.top + 8, card.rect.right - bounds.left + 8,
                     card.rect.bottom - bounds.top + 8};
    }
    const UiThemeInput theme = RenderThemes()[0].input;
    winrt::com_ptr<IWICBitmap> bitmap =
        RenderBitmap(wic, {bounds.right - bounds.left + 16, bounds.bottom - bounds.top + 16}, dpi);
    winrt::com_ptr<ID2D1RenderTarget> target = RenderTarget(g, bitmap.get(), dpi);
    UiTargetCache cache;
    UiPalette palette = UiPaletteFor(theme);
    UiCustomPaint custom = [&](const UiItem& item) {
        SettingsPaintPreview(target.get(), cache, g, theme, view, item, {-1e9f, -1e9f});
    };
    target->BeginDraw();
    target->Clear(palette.background);
    UiPaintItems(target.get(), g, cache, palette, cards, {}, {0, 0}, view.scale, -1e9f, 1e9f, &custom);
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
        static constexpr const char* kStyleNames[] = {"spark",   "stacked",  "vertical", "ring",     "concentric",
                                                      "gauge",   "pies",     "square",   "headline", "matrix"};
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
                for (int style = 0; style < int(std::size(kStyleNames)); style++) {
                    if (dpi != 144 && style != int(BarLayout::Stacked) && style != int(BarLayout::SegmentedRing)) continue;
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
                // Display with the segmented ring, as the mockup, without the bar text (Layout's).
                std::string display = std::string("render-display-") + theme.name + "-" + std::to_string(dpi) + ".png";
                Run(display, [&] {
                    SettingsView view = LayoutView(BarLayout::SegmentedRing, dpi);
                    view.page = SettingsUiPage::Display;
                    UiFrame frame;
                    SettingsUiBuildFrame(g, view, client, frame);
                    UiPaintState state;
                    state.focus = {3};
                    RenderFrame(g, wic.get(), path(display), frame, view, client, dpi, theme.input, state, 0, false);
                    CHECK(!UiFindItem(frame, SettingsControlId(SettingsField::PercentTextVisibility)));
                });
            }

            // Display at the mockup's full height (1000 x 1298 window, 40 DIP caption): the
            // segmented ring as in the mockup; stacked bars with percentages always, remaining
            // mode and the colorblind palette; then the states sheet: Tinted wallpaper with
            // transparency effects off (both material notes), an unordered red threshold typed (box
            // editing, slider and preview ordered), a hovered toggle.
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
                view.settings.usagePalette = UsagePalette::Colorblind;
                frame = {};
                SettingsUiBuildFrame(g, view, tall, frame);
                std::string bars = std::string("render-display-bars-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(bars), frame, view, tall, 144, theme.input, state, 0, false);

                // The Custom palette, scrolled to its colors: below yellow at its colorblind
                // preset, a picked yellow band and orange (an extra swatch each), red at its
                // default; the pointer on a swatch, keyboard focus on the orange band's Custom....
                view.settings.usagePalette = UsagePalette::Custom;
                view.settings.customUsageColors = {kColorblindUsageColors[0], RGB(0x30, 0x40, 0x90),
                                                   RGB(0xC0, 0x40, 0xC0), kDefaultUsageColors[3]};
                view.settings.percentTextAlignment = PercentTextAlignment::Left;
                view.settings.barMode = BarMode::Used;
                frame = {};
                SettingsUiBuildFrame(g, view, tall, frame);
                UiPaintState customState;
                customState.focus = {SettingsControlId(SettingsField::UsageColorCustom, 2)};
                customState.focusVisible = true;
                customState.hot = {SettingsControlId(SettingsField::UsageColorYellow, 2)};
                std::string customName = std::string("render-display-custom-") + theme.name + "-144.png";
                float customScroll = std::clamp(Control(frame, SettingsField::UsagePalette).rect.top - 60, 0.0f,
                                                std::max(frame.contentHeight - (frame.viewport.bottom - frame.viewport.top),
                                                         0.0f));
                RenderFrame(g, wic.get(), path(customName), frame, view, tall, 144, theme.input, customState,
                            customScroll, false);
                CHECK(Control(frame, SettingsField::UsageColorCustom, 2).enabled);

                TestStore store;
                store.settings.barLayout = BarLayout::Stacked;
                store.settings.material = Material::TintedWallpaper;
                SettingsEdits edits(store);
                edits.Reload();
                edits.SetText(0, SettingsField::RedThreshold, L"60");
                view = LayoutView(BarLayout::Stacked, 144);
                view.page = SettingsUiPage::Display;
                view.settings = edits.Projection();
                view.drafts = edits.drafts;
                view.transparencyEffects = false;
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

            // Over a stand-in backdrop (light and dark; high contrast never gets one): Accounts with
            // OpenAI expanded and its label box editing, Layout with the pinned preview, Display.
            if (theme.input.kind != UiThemeKind::HighContrast) {
                std::string accounts = std::string("render-backdrop-accounts-") + theme.name + "-144.png";
                Run(accounts, [&] {
                    SettingsView view = AccountsView({0});
                    UiFrame frame;
                    SettingsUiBuildFrame(g, view, client, frame);
                    UiPaintState state;
                    state.editing = AccountId(1, SettingsField::AccountLabel);
                    state.hot = {SettingsControlId(SettingsField::AccountAdd)};
                    RenderFrame(g, wic.get(), path(accounts), frame, view, client, 144, theme.input, state, 0,
                                false, true, {}, true);

                    view = LayoutView(BarLayout::Stacked, 144);
                    frame = {};
                    SettingsUiBuildFrame(g, view, client, frame);
                    std::string layout = std::string("render-backdrop-layout-") + theme.name + "-144.png";
                    RenderFrame(g, wic.get(), path(layout), frame, view, client, 144, theme.input, {}, 0, false, true,
                                {}, true);

                    view = LayoutView(BarLayout::Stacked, 144);
                    view.page = SettingsUiPage::Display;
                    view.settings.material = Material::Mica;
                    frame = {};
                    SettingsUiBuildFrame(g, view, client, frame);
                    state = {};
                    state.hot = {SettingsControlId(SettingsField::Material)};
                    std::string display = std::string("render-backdrop-display-") + theme.name + "-144.png";
                    RenderFrame(g, wic.get(), path(display), frame, view, client, 144, theme.input, state, 0, true,
                                true, {}, true);
                    CHECK(Control(frame, SettingsField::Material).action.value == int(Material::Mica));
                });
            }

            // Accounts at 144 DPI as the mockups: OpenAI expanded (the mockup's 1000 x 953 window);
            // the Add account menu open with OpenAI under the pointer, painted where the host opens
            // it; no accounts. Then the states: CC 1 expanded with a duplicate label typed (its box
            // editing, its error), the pointer on a chip and the focus ring on Remove account; Go
            // dragged by its handle between OAI and CC 1, lifted under the pointer, with brand-colored
            // logos.
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
                                       SettingsDraftError::LabelTaken});
                frame = {};
                SettingsUiBuildFrame(g, view, client, frame);
                state = {};
                state.editing = AccountId(2, SettingsField::AccountLabel);
                state.hot = {AccountId(2, SettingsField::AccountBars, kFableWeeklyBar)};
                state.focus = {AccountId(2, SettingsField::AccountRemove, 1)};
                state.focusVisible = true;
                std::string states = std::string("render-accounts-states-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(states), frame, view, client, 144, theme.input, state, 0, false);

                // The dragged sheet with brand-colored logos (high contrast: the ink).
                view = AccountsView();
                view.settings.logoBrands = {true, true};
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
                CHECK(rows.size() == 5 && UiFindItem(frame, AccountId(4, SettingsField::AccountExpand, 1))->selected);
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

                // Scrolled to the Identity section, its link to Accounts hovered.
                const UiItem& identity = *UiFindItem(frame, SettingsControlId(SettingsField::BarLayout, 10));
                state = {};
                state.hot = {identity.id};
                std::string identityName = std::string("render-layout-identity-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(identityName), frame, view, client, 144, theme.input, state,
                            std::max(identity.rect.top - 300, 0.0f), false);
                CHECK(identity.action.kind == UiActionKind::Navigate);

                // Scrolled to the logo and badge cards, the focus ring on a badge color swatch.
                const UiItem& badgeColor = *UiFindItem(frame, SettingsControlId(SettingsField::BadgeColor, 3));
                state = {};
                state.focus = {badgeColor.id};
                state.focusVisible = true;
                std::string badgeName = std::string("render-layout-badge-") + theme.name + "-144.png";
                RenderFrame(g, wic.get(), path(badgeName), frame, view, client, 144, theme.input, state,
                            std::max(badgeColor.rect.top - 400, 0.0f), false);
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
                        RenderPreviewSheet(g, wic.get(), path(sheet), theme.input, dpi,
                                           display ? PreviewSheet::Display : PreviewSheet::Layout);
                        CHECK(true);
                    });
                }
            }

            // The positions: the placement sheet, and Layout scrolled to the Placement section with
            // Left side chosen and two taskbars showing the bars in the tray (its note), the Position
            // field hovered; at 100% and 150%.
            for (UINT dpi : {96u, 144u}) {
                std::string sheet = std::string("render-placement-sheet-") + theme.name + "-" + std::to_string(dpi) + ".png";
                Run(sheet, [&] {
                    RenderPreviewSheet(g, wic.get(), path(sheet), theme.input, dpi, PreviewSheet::Placement);
                    CHECK(true);
                });
                std::string page = std::string("render-layout-placement-") + theme.name + "-" + std::to_string(dpi) + ".png";
                Run(page, [&] {
                    SettingsView view = LayoutView(BarLayout::Stacked, dpi);
                    view.settings.placement = TaskbarPlacement::LeftSide;
                    view.status.placementFallbacks = {{1, false}, {2, true}};
                    UiFrame frame;
                    SettingsUiBuildFrame(g, view, client, frame);
                    const UiItem& position = Control(frame, SettingsField::Placement);
                    UiPaintState state;
                    state.hot = {position.id};
                    float end = frame.contentHeight - (frame.viewport.bottom - frame.viewport.top);
                    RenderFrame(g, wic.get(), path(page), frame, view, client, dpi, theme.input, state,
                                std::clamp(position.rect.top - 160, 0.0f, std::max(end, 0.0f)), false);
                    CHECK(UiFindItem(frame, SettingsControlId(SettingsField::Placement, 8)));
                });
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
                                       SettingsDraftError::Range});
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
                    if (item.runs.size() == 5) logos = item.id;
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
        // The localization review's languages, light, each language stored as the choice and the
        // toolkit in its locale as the settings client sets it, pages whole (the client as tall as
        // the page; fixtures/i18n-layout.inc's views): every page at 96 DPI at the default width
        // and the window's minimum width (-min), Accounts with OAI's and CC 2's cards expanded, its
        // empty state, Layout (with a side taskbar's note) and its other styles' sections at the
        // minimum width; Behavior and History also at 144 DPI; then every dropdown's and menu's popup
        // rows, captioned, as ui/popup.h paints them.
        const D2D1_SIZE_F minClient = I18nLayoutMinClient();
        for (std::string_view tag : {"qps-ploc", "de", "ru", "ja", "zh-Hans"}) {
            std::wstring wide(tag.begin(), tag.end());
            I18nLocale locale(*I18nLanguageByTag(wide));
            auto renderPage = [&](const std::string& name, SettingsView view, D2D1_SIZE_F size, UINT dpi) {
                Run(name, [&] {
                    view = I18nLayoutLocalized(std::move(view), tag);
                    g.locale = locale.LocaleName();
                    UiFrame frame;
                    SettingsUiBuildFrame(g, view, size, frame);
                    D2D1_SIZE_F whole{size.width, std::max(size.height, frame.viewport.top + frame.contentHeight)};
                    frame = {};
                    SettingsUiBuildFrame(g, view, whole, frame);
                    UiPaintState state;
                    state.focus = {uint32_t(int(view.page) + 1)};
                    RenderFrame(g, wic.get(), path(name), frame, view, whole, dpi, RenderThemes()[0].input, state, 0,
                                false);
                    g.locale = L"en-US";
                    CHECK(true);
                });
            };
            SettingsView about = TestView(SettingsUiPage::About);
            about.version = L"2.0.1";
            SettingsView layout = TestView(SettingsUiPage::Layout);
            layout.status.sideTaskbars = {{2, false}};
            const std::pair<const char*, SettingsView> pages[] = {
                {"about", about},
                {"accounts", I18nAccountsView({0, 2})},
                {"accounts-empty", TestView(SettingsUiPage::Accounts)},
                {"behavior", TestView(SettingsUiPage::Behavior)},
                {"history", TestView(SettingsUiPage::History)},
                {"layout", layout},
                {"display", TestView(SettingsUiPage::Display)},
            };
            for (const auto& [pageName, view] : pages) {
                std::string name = "render-i18n-" + std::string(pageName) + "-" + std::string(tag) + "-light-";
                renderPage(name + "96.png", view, client, 96);
                renderPage(name + "96-min.png", view, minClient, 96);
                if (view.page == SettingsUiPage::Behavior || view.page == SettingsUiPage::History) {
                    renderPage(name + "144.png", view, client, 144);
                }
            }
            for (auto [style, styleName] : {std::pair{BarLayout::Sparkline, "spark"}, std::pair{BarLayout::SegmentedRing, "ring"},
                                            std::pair{BarLayout::LogoHeadline, "headline"},
                                            std::pair{BarLayout::QuotaMatrix, "matrix"}}) {
                renderPage("render-i18n-layout-" + std::string(styleName) + "-" + std::string(tag) + "-light-96-min.png",
                           I18nLayoutView(style), minClient, 96);
            }

            // The popups: each field's or menu button's rows under its caption, at the default width.
            std::string popups = "render-i18n-popups-" + std::string(tag) + "-light-96.png";
            Run(popups, [&] {
                g.locale = locale.LocaleName();
                struct Popup {
                    SettingsView view;
                    uint32_t id;
                };
                SettingsView spark = I18nLayoutView(BarLayout::Sparkline);
                std::vector<Popup> list = {
                    {TestView(SettingsUiPage::Accounts), SettingsControlId(SettingsField::AccountAdd)},
                    {I18nAccountsView({2}), AccountId(3, SettingsField::AccountTileIdentity)},
                    {I18nLayoutView(BarLayout::Stacked), SettingsControlId(SettingsField::Placement)},
                    {I18nLayoutView(BarLayout::Stacked), SettingsControlId(SettingsField::ClaudeLogoBrand)},
                    {I18nLayoutView(BarLayout::Stacked), SettingsControlId(SettingsField::GeminiLogoBrand)},
                    {I18nLayoutView(BarLayout::Stacked), SettingsControlId(SettingsField::BarStyle)},
                    {I18nLayoutView(BarLayout::Stacked), SettingsControlId(SettingsField::LabelPosition)},
                    {I18nLayoutView(BarLayout::Stacked), SettingsControlId(SettingsField::PercentTextVisibility)},
                    {I18nLayoutView(BarLayout::Stacked), SettingsControlId(SettingsField::PercentTextAlignment)},
                    {I18nLayoutView(BarLayout::Stacked), SettingsControlId(SettingsField::PaceTickStyle)},
                    {spark, SettingsControlId(SettingsField::SparklineExtra)},
                    {spark, SettingsControlId(SettingsField::SparklineSpan)},
                    {spark, SettingsControlId(SettingsField::SparklineHoverSpan)},
                    {TestView(SettingsUiPage::Display), SettingsControlId(SettingsField::Material)},
                    {TestView(SettingsUiPage::Display), SettingsControlId(SettingsField::BarMode)},
                    {TestView(SettingsUiPage::Display), SettingsControlId(SettingsField::UsagePalette)},
                    {TestView(SettingsUiPage::Behavior), SettingsControlId(SettingsField::Language)},
                    {TestView(SettingsUiPage::Behavior), SettingsControlId(SettingsField::ShowOn)},
                    {TestView(SettingsUiPage::Behavior), SettingsControlId(SettingsField::PollMinutes, 8)},
                    {TestView(SettingsUiPage::Behavior), SettingsControlId(SettingsField::Notifications)},
                    {TestView(SettingsUiPage::History), SettingsControlId(SettingsField::KeepHistory)},
                    {TestView(SettingsUiPage::History), SettingsControlId(SettingsField::HistoryRange)},
                };
                struct Placed {
                    std::wstring caption;
                    std::vector<UiItem> rows;
                    D2D1_SIZE_F size;
                };
                std::vector<Placed> placed;
                float width = 0, height = 8;
                for (const Popup& popup : list) {
                    SettingsView view = I18nLayoutLocalized(popup.view, tag);
                    UiFrame frame;
                    SettingsUiBuildFrame(g, view, client, frame);
                    const UiItem* field = UiFindItem(frame, popup.id);
                    CHECK(field && !field->options.empty());
                    Placed entry;
                    entry.caption = field->kind == UiItemKind::Button ? field->text : field->text + L" \u25BE";
                    entry.size = UiLayoutOptions(g, field->options, field->action, field->rect.right - field->rect.left,
                                                 entry.rows);
                    width = std::max(width, entry.size.width + 16);
                    height += 20 + entry.size.height + 12;
                    placed.push_back(std::move(entry));
                }
                winrt::com_ptr<IWICBitmap> bitmap = RenderBitmap(wic.get(), {width, height}, 96);
                winrt::com_ptr<ID2D1RenderTarget> target = RenderTarget(g, bitmap.get(), 96);
                UiTargetCache cache;
                UiPalette palette = UiPaletteFor(RenderThemes()[0].input);
                target->BeginDraw();
                target->Clear(palette.background);
                float y = 8;
                for (const Placed& entry : placed) {
                    UiDrawLayout(target.get(), cache, UiLayout(g, entry.caption, UiTextStyleFor(UiFont::Caption), width - 16, false),
                                 {8, y}, palette.textSecondary);
                    y += 20;
                    D2D1_RECT_F box{8, y, 8 + entry.size.width, y + entry.size.height};
                    UiFillRect(target.get(), cache, box, 8, palette.popup, 1);
                    UiStrokeRect(target.get(), cache, box, 8, palette.controlBorder, 1, 1);
                    UiPaintItems(target.get(), g, cache, palette, entry.rows, {}, {box.left, box.top}, 1, -1e9f, 1e9f);
                    y += entry.size.height + 12;
                }
                winrt::check_hresult(target->EndDraw());
                cache.Clear();
                SaveRender(wic.get(), bitmap.get(), path(popups));
                std::cout << "RENDER " << popups << '\n';
                g.locale = L"en-US";
            });

        }
        // The taskbar's localized text, measured in the locale, in English too for comparison: the
        // preview strip sheet (every tile style) and the tile style gallery at the default and the
        // window's minimum width.
        for (std::string_view tag : {"en", "qps-ploc", "de", "ru", "ja", "zh-Hans"}) {
            I18nLocale locale = I18nLayoutLocalized({}, tag).locale;
            for (UINT dpi : {96u, 144u}) {
                std::string sheet = "render-i18n-preview-sheet-" + std::string(tag) + "-light-" + std::to_string(dpi) +
                                    ".png";
                Run(sheet, [&] {
                    g.locale = locale.LocaleName();
                    RenderPreviewSheet(g, wic.get(), path(sheet), RenderThemes()[0].input, dpi, PreviewSheet::I18n, locale);
                    g.locale = L"en-US";
                    CHECK(true);
                });
                for (bool narrow : {false, true}) {
                    std::string gallery = "render-i18n-gallery-" + std::string(tag) + "-light-" + std::to_string(dpi) +
                                          (narrow ? "-min" : "") + ".png";
                    Run(gallery, [&] {
                        g.locale = locale.LocaleName();
                        RenderGallerySheet(g, wic.get(), path(gallery), dpi, tag, narrow ? minClient : client);
                        g.locale = L"en-US";
                    });
                }
            }
        }
        // A regional number format: the Display page whole under de-DE's decimal comma, in English and
        // German, the bars' text always shown with amounts on the extra usage bars, so the preview
        // strip shows CC 2's dollars in it.
        for (std::string_view tag : {"en", "de"}) {
            std::wstring wide(tag.begin(), tag.end());
            I18nLocale locale = TestRegional(wide, L"de-DE");
            std::string name = "render-i18n-display-" + std::string(tag) + "-de-DE-light-96.png";
            Run(name, [&] {
                SettingsView view = TestView(SettingsUiPage::Display);
                view.locale = locale;
                view.settings.language = wide;
                view.settings.showExtraBarAmounts = true;
                view.settings.percentTextVisibility = PercentTextVisibility::Always;
                g.locale = locale.LocaleName();
                UiFrame frame;
                SettingsUiBuildFrame(g, view, client, frame);
                D2D1_SIZE_F whole{client.width, std::max(client.height, frame.viewport.top + frame.contentHeight)};
                frame = {};
                SettingsUiBuildFrame(g, view, whole, frame);
                UiPaintState state;
                state.focus = {uint32_t(int(SettingsUiPage::Display) + 1)};
                RenderFrame(g, wic.get(), path(name), frame, view, whole, 96, RenderThemes()[0].input, state, 0, false);
                g.locale = L"en-US";
                CHECK(true);
            });
        }
    }
    CoUninitialize();
}
