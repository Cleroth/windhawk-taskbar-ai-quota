// Offscreen renders of the history window's client area (the approved mockups' 1100 x 830 window
// without its 40 DIP caption) as the host composes it: main items (an export's progress), the
// navigation pane at its scroll, the content clipped to the viewport at its scroll with its
// scrollbar (the chart and its legend chips through the production painter, as the client draws
// them), the More menu and a chart's readout where the host shows them. Sheets stack captioned
// frames (or their top parts). PNG evidence for review, no pixel goldens.

struct HistoryRenderPart {
    std::wstring caption;
    HistoryUiView view;
    D2D1_SIZE_F client{1100, 790};
    float crop = 0;        // Only the frame's top part (DIPs); 0: all of it
    UiPaintState state{};
    float scroll = 0;      // The content's; negative: that far above its end
    bool navEnd = false;   // The navigation pane scrolled to its end
    bool menu = false;     // The More menu open, the pointer on Open history folder (s-menu)
    size_t exported = 0;   // The rows a running export wrote (view.exporting)
    // The chart: a controller over a synthetic journal (history-chart.inc) lays it out for the
    // frame's chart item and draws it; its lines replace the view's (`chartView`: its whole view
    // does). `hover`: the pointer on the laid out chart (content coordinates), its readout shown
    // where the tooltip's placement puts it within the frame.
    std::shared_ptr<TestChartWindow> chart{};
    bool chartView = false;
    std::function<D2D1_POINT_2F(const HistoryChart&)> hover{};
};

static void RenderHistorySheet(UiGraphics& g, IWICImagingFactory* wic, const std::wstring& path,
                               std::vector<HistoryRenderPart> parts, const UiThemeInput& theme, UINT dpi) {
    constexpr float kMargin = 16, kCaption = 24;
    float scale = float(dpi) / 96;
    bool sheet = parts.size() > 1;
    float width = 0, height = sheet ? kMargin : 0;
    for (const HistoryRenderPart& part : parts) {
        width = std::max(width, part.client.width);
        height += (part.crop ? part.crop : part.client.height) + (sheet ? kCaption + kMargin : 0);
    }
    if (sheet) width += 2 * kMargin;
    winrt::com_ptr<IWICBitmap> bitmap = RenderBitmap(wic, {width, height}, dpi);
    winrt::com_ptr<ID2D1RenderTarget> target = RenderTarget(g, bitmap.get(), dpi);
    UiTargetCache cache;
    UiPalette palette = UiPaletteFor(theme);
    target->BeginDraw();
    target->Clear(sheet ? UiOver(UiFade(palette.text, 0.08f), palette.background) : palette.background);
    float y = sheet ? kMargin : 0;
    for (HistoryRenderPart& part : parts) {
        HistoryController* controller = part.chart ? &part.chart->controller : nullptr;
        if (controller && part.chartView) {
            part.view = controller->view;
        } else if (controller) {
            // The hand-made cards take the colors of the lines they name.
            part.view.lines = controller->view.lines;
            for (HistoryUiCard& card : part.view.cards) {
                for (const HistoryUiLine& line : part.view.lines) {
                    if (card.title == line.name) card.color = line.color;
                }
            }
        }
        part.view.theme = theme.kind;
        UiFrame frame;
        HistoryUiBuildFrame(g, part.view, part.client, frame);
        UiTooltipContent readout;
        D2D1_POINT_2F pointer{};
        if (controller) {
            if (const UiItem* chart = UiFindItem(frame, kHistoryUiChart)) controller->LayChart(g, chart->rect, scale);
            if (part.hover) {
                pointer = part.hover(controller->chart);
                controller->Hover(pointer, readout, palette);
            }
        }
        float x = sheet ? kMargin : 0;
        if (sheet) {
            UiDrawLayout(target.get(), cache, UiLayout(g, part.caption, UiTextStyleFor(UiFont::BodyStrong), width, false),
                         {x, y + 1}, palette.text);
            y += kCaption;
        }
        D2D1::Matrix3x2F at = D2D1::Matrix3x2F::Translation(x, y);
        target->SetTransform(&at);
        D2D1_RECT_F bounds{0, 0, part.client.width, part.crop ? part.crop : part.client.height};
        target->PushAxisAlignedClip(&bounds, D2D1_ANTIALIAS_MODE_ALIASED);
        UiFillRect(target.get(), cache, bounds, 0, palette.background, scale);
        UiPaintState state = part.state;
        if (part.menu) state.open = kHistoryUiMore;
        const HistoryChart none;
        const std::vector<size_t> picked;
        UiCustomPaint custom = [&](const UiItem& item) {
            if (!controller && item.id != kHistoryUiProgress) return;
            HistoryUiPaintCustom(target.get(), g, cache, palette, controller ? controller->chart : none,
                                 controller ? controller->picked : picked, part.exported, item, state, scale);
        };
        UiPaintItems(target.get(), g, cache, palette, frame.main, state, {0, 0}, scale, -1e9f, 1e9f, &custom);
        const D2D1_RECT_F& pane = frame.navPane;
        float navScroll = part.navEnd ? std::max(frame.navHeight - (pane.bottom - pane.top), 0.0f) : 0;
        UiPaintNavPane(target.get(), g, cache, palette, frame, state, std::round(navScroll * scale) / scale, false,
                       scale);
        const D2D1_RECT_F& v = frame.viewport;
        float viewHeight = v.bottom - v.top;
        float scroll = part.scroll >= 0 ? part.scroll : frame.contentHeight - viewHeight + part.scroll;
        scroll = std::round(UiClampScroll(scroll, viewHeight, frame.contentHeight) * scale) / scale;
        target->PushAxisAlignedClip(&v, D2D1_ANTIALIAS_MODE_ALIASED);
        UiPaintItems(target.get(), g, cache, palette, frame.content, state, {v.left, v.top - scroll}, scale, scroll,
                     scroll + viewHeight, &custom);
        D2D1::Matrix3x2F shift = D2D1::Matrix3x2F::Translation(v.left, v.top) * at;
        target->SetTransform(&shift);
        UiPaintScrollbar(target.get(), cache, palette,
                         UiScrollbarFor({v.right - v.left, viewHeight}, scroll, frame.contentHeight, false), false, scale);
        target->SetTransform(&at);
        target->PopAxisAlignedClip();
        if (part.menu) {
            // Right-aligned 4 DIP under the button, as the host places it on a roomy work area.
            const UiItem& more = *UiFindItem(frame, kHistoryUiMore);
            std::vector<UiItem> rows;
            D2D1_SIZE_F size = UiLayoutOptions(g, more.options, more.action, more.rect.right - more.rect.left, rows);
            float left = std::round((more.rect.right - size.width) * scale) / scale;
            D2D1_RECT_F box{left, more.rect.bottom + 4, left + size.width, more.rect.bottom + 4 + size.height};
            UiFillRect(target.get(), cache, box, 8, palette.popup, scale);
            UiStrokeRect(target.get(), cache, box, 8,
                         palette.kind == UiThemeKind::HighContrast ? palette.cardBorder : palette.controlBorder, 1,
                         scale);
            UiPaintState hover;
            hover.hot = {2};
            UiPaintItems(target.get(), g, cache, palette, rows, hover, {box.left, box.top}, scale, -1e9f, 1e9f);
        }
        if (readout.key) {
            // The frame as the screen (its top-left at 0, its client the work area), the mockups'
            // 32 DIP arrow pointer.
            auto px = [&](float dip) { return LONG(std::lround(dip * scale)); };
            POINT cursor{px(v.left + pointer.x), px(v.top + pointer.y - scroll)};
            UiTooltipKey key = TestKey(cursor, {0, 0, px(bounds.right), px(bounds.bottom)}, px(32), {0, 0}, dpi);
            UiTooltipMeasure measure = UiMeasureReadout(g, readout);
            if (std::optional<UiTooltipSpot> spot = UiTooltipPlacement(g, readout, measure, key)) {
                D2D1_RECT_F box{float(spot->rect.left) / scale, float(spot->rect.top) / scale,
                                float(spot->rect.right) / scale, float(spot->rect.bottom) / scale};
                UiFillRect(target.get(), cache, box, 4, palette.popup, scale);
                UiStrokeRect(target.get(), cache, box, 4,
                             palette.kind == UiThemeKind::HighContrast ? palette.cardBorder : palette.controlBorder, 1,
                             scale);
                UiPaintItems(target.get(), g, cache, palette, UiTooltipItems(readout, measure, spot->layout), {},
                             {box.left, box.top}, scale, -1e9f, 1e9f);
            }
            UiItem arrow;
            arrow.kind = UiItemKind::Shape;
            arrow.viewBox = 32;
            float ax = float(cursor.x) / scale, ay = float(cursor.y) / scale;
            arrow.rect = {ax, ay, ax + 32, ay + 32};
            arrow.parts = {{kTestArrowPath, UiRgb(0xFFFFFF)}, {kTestArrowPath, UiRgb(0x000000), UiInk::Inherit, 1, 1}};
            UiPaintItems(target.get(), g, cache, palette, {arrow}, {}, {0, 0}, scale, -1e9f, 1e9f);
        }
        target->PopAxisAlignedClip();
        if (sheet) UiStrokeRect(target.get(), cache, UiInflate(bounds, 1, 1), 0, UiFade(palette.text, 0.3f), 1, scale);
        D2D1::Matrix3x2F identity = D2D1::Matrix3x2F::Identity();
        target->SetTransform(&identity);
        y += bounds.bottom + (sheet ? kMargin : 0);
    }
    winrt::check_hresult(target->EndDraw());
    cache.Clear();
    SaveRender(wic, bitmap.get(), path);
    std::string narrow;
    for (wchar_t c : path) narrow += c < 0x80 ? char(c) : '?';
    std::cout << "RENDER " << narrow << '\n';
}

// The mockups' sidebar overflowing (q13b-scroll, s-menu): ten more accounts, one without a
// value, and two removed ones.
static HistoryUiView TestManyAccounts() {
    HistoryUiView view = TestHistoryView();
    struct Entry {
        uint32_t provider;
        PCWSTR label;
        double pct;
    };
    const Entry more[] = {{2, L"OAI 2", 18}, {1, L"CC 3", 71}, {1, L"Research \u2014 shared Max seat", 55},
                          {1, L"CC 4", 8}, {2, L"Codex CI", 84}, {3, L"Go 2", 12}, {1, L"CC 5", 39},
                          {2, L"OAI Team", 66}, {1, L"CC 6", 91}, {1, L"Claude personal", -1}};
    uint32_t slot = 40;
    for (const Entry& e : more) {
        HistoryUiViewAccount account;
        account.historyId = 0x100 + slot;
        account.slot = slot++;
        account.provider = e.provider;
        account.label = e.label;
        account.weeklyPct = e.pct;
        account.lastPollMs = kWeekTo;
        view.accounts.push_back(std::move(account));
    }
    view.accounts.push_back(TestRemoved(0x3F9C1A2B4D5E6F70, 60, 1));
    view.accounts.push_back(TestRemoved(0xA01B77C2E9D40C15, 61, 2));
    return view;
}

static void RunHistoryRenders(const std::wstring& directory) {
    UiGraphics g;
    UiCreateGraphics(g);
    winrt::com_ptr<IWICImagingFactory> wic;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          __uuidof(IWICImagingFactory), wic.put_void()));
    auto weekly = [] { return TestWeeklyView(); };
    // The charts: controllers over the mockups' synthetic journals (history-chart.inc).
    auto select = [](uint64_t id, HistoryView view, bool compare = false) {
        return [=](HistoryController& c) {
            c.Command(HistoryUiCommand::Select, 0, id);
            c.Command(HistoryUiCommand::View, int(view));
            if (compare) c.Command(HistoryUiCommand::Compare, 1);
        };
    };
    std::shared_ptr<TestChartWindow> week = TestChartOf(TestMockupWeek());

    // s-light: All accounts, Weekly.
    HistoryRenderPart defaults{L"", weekly()};
    defaults.chart = week;

    // s-account: OAI with its recording off (credits); CC 1 paused, its value stale (extra
    // budget, dollars details only).
    HistoryRenderPart oai{L"One account: OpenAI, Extra usage, recording off for this account", TestHistoryView()};
    oai.view.selected = 0;
    oai.view.view = HistoryView::ExtraUsage;
    oai.view.accounts[0].recordingOff = true;
    oai.view.cards = {TestCard(L"Credits remaining", 7, L"1240.00 credits", L"Thu 29 Oct 17:31",
                               {L"Peak 2500.00 credits \u00B7 Sat 24 Oct 14:32", L"Coverage 123 h (73.2%)"})};
    oai.chart = TestChartOf(TestMockupWeek(), select(0x0A, HistoryView::ExtraUsage));
    HistoryRenderPart cc1{L"One account: Anthropic, Extra usage, paused (hidden, its value stale)", TestHistoryView()};
    cc1.view.selected = 1;
    cc1.view.view = HistoryView::ExtraUsage;
    cc1.view.accounts[1].paused = true;
    cc1.view.accounts[1].stale = true;
    cc1.view.cards = {TestCard(L"Extra budget", 3, L"22%", L"Fri 30 Oct 20:32",
                               TestLines(L"22% \u00B7 Fri 30 Oct 20:32", L"150 h (89.3%)", L"0 h")),
                      TestCard(L"Extra spending (USD) \u2014 details only", 6, L"USD 11.00", L"Fri 30 Oct 20:32",
                               {L"Peak USD 11.00 \u00B7 Fri 30 Oct 20:32"})};
    cc1.view.cards[1].detailsOnly = true;
    cc1.chart = TestChartOf(TestMockupWeek(), select(0xC1, HistoryView::ExtraUsage));

    // s-compare: CC 2, Weekly, comparing (two columns of both periods).
    HistoryRenderPart compare{L"", TestHistoryView()};
    compare.view.selected = 2;
    compare.view.compare = true;
    compare.chart = TestChartOf(TestMockupCompare(), select(0xC2, HistoryView::Weekly, true));
    compare.view.cards = {
        TestCard(L"Weekly", 1, L"93%", L"now",
                 {L"Current: Peak 93% \u00B7 Sat 31 Oct 14:31", L"Coverage 158 h (94.0%)", L"Est. \u226590%: 4 h",
                  L"Previous: Peak 88% \u00B7 Sat 24 Oct 14:32", L"Coverage 166 h (98.8%)", L"Est. \u226590%: 0 h"}),
        TestCard(L"Fable weekly", 2, L"58%", L"now",
                 {L"Current: Peak 63% \u00B7 Sun 25 Oct 22:32", L"Coverage 166 h (98.8%)", L"Est. \u226590%: 0 h",
                  L"Previous: Peak 61% \u00B7 Sat 24 Oct 14:32", L"Coverage 154 h (91.7%)", L"Est. \u226590%: 0 h"})};

    // s-states: recording off; Enable recording failed; storage failing; waiting, with keyboard
    // focus on the range segments and the pointer on Refresh.
    HistoryRenderPart off{L"Recording off: pill Off, caution bar with Enable recording", weekly(), {1100, 790}, 330};
    off.view.recordingEnabled = false;
    off.view.canEnableRecording = true;
    off.view.pill = HistoryUiPill::Off;
    HistoryRenderPart consent{L"Enable recording failed: its error notice above the recording-off bar", off.view,
                              {1100, 790}, 390};
    consent.view.notice = {L"Could not save recording consent. Recording is still off.", UiSeverity::Error};
    HistoryRenderPart storage{L"Storage failing: pill Not saving, error and cap bars", weekly(), {1100, 790}, 400};
    storage.view.pill = HistoryUiPill::NotSaving;
    storage.view.storageError = L"History storage is in use by another Explorer session, or is inaccessible.";
    storage.view.storageCapped = true;
    HistoryRenderPart waiting{L"Waiting; keyboard focus on the range segments, the pointer on Refresh", weekly(),
                              {1100, 790}, 250};
    waiting.view.pill = HistoryUiPill::Waiting;
    waiting.state.focus = {kHistoryUiRanges};
    waiting.state.focusVisible = true;
    waiting.state.hot = {kHistoryUiRefresh};
    // An export writing: its progress in the status slot, every control but the chips disabled;
    // at the minimum width, under the title.
    HistoryRenderPart exporting{L"Export running: progress in the status slot; every control but the chips disabled",
                                weekly(), {1100, 790}, 250};
    exporting.view.exporting = true;
    exporting.exported = 12345;
    HistoryRenderPart exportingMin{L"Export running at the minimum width: progress under the title", weekly(),
                                   {800, 550}, 250};
    exportingMin.view.exporting = true;
    exportingMin.exported = 1234567;
    off.chart = consent.chart = storage.chart = waiting.chart = exporting.chart = exportingMin.chart = week;

    // s-states: each operation notice (Q3 a) above the chart's card.
    std::vector<HistoryRenderPart> notices;
    for (auto [caption, text, severity] :
         {std::tuple{L"Export done", L"Exported 12,345 meter rows (all accounts) to C:\\Users\\clero\\Documents\\"
                                     L"usage-history.csv. Labels omitted; times UTC.", UiSeverity::Info},
          std::tuple{L"Export failed", L"CSV export failed (Windows error 5). Existing destination unchanged.",
                     UiSeverity::Error},
          std::tuple{L"Clear done", L"All local history cleared. Future polls can start recording again.",
                     UiSeverity::Info},
          std::tuple{L"Clear failed", L"Could not clear all history. History storage is in use by another Explorer "
                                      L"session, or is inaccessible.", UiSeverity::Error},
          std::tuple{L"Delete history done", L"Deleted the history of removed account account_3f9c1a2b4d5e6f70.",
                     UiSeverity::Info},
          std::tuple{L"Delete history failed", L"Could not delete this account's history. History storage is in use "
                                               L"by another Explorer session, or is inaccessible.", UiSeverity::Error},
          std::tuple{L"Nothing recorded yet (Open history folder)", L"Nothing has been recorded yet. The folder is "
                                                                    L"created with the first recorded sample.",
                     UiSeverity::Info}}) {
        HistoryRenderPart part{caption, weekly(), {1100, 790}, 190};
        part.view.notice = {text, severity};
        part.chart = week;
        notices.push_back(std::move(part));
    }

    // s-empty: loading (no status yet), no accounts, a view no account supports (one account,
    // All accounts); then no readings in range with the No readings card, dollars only, and the
    // No readings card beside plotted accounts.
    HistoryRenderPart loading{L"Loading (first load; no status yet)", TestHistoryView(), {1100, 790}, 530};
    loading.view.loading = true;
    loading.view.status.clear();
    HistoryRenderPart none{L"No accounts configured", TestHistoryView(), {1100, 790}, 530};
    none.view.accounts.clear();
    HistoryRenderPart unsupported{L"View unsupported, one account (Google Antigravity has no extra usage)",
                                  TestHistoryView(), {1100, 790}, 550};
    unsupported.view.selected = 3;
    unsupported.view.view = HistoryView::ExtraUsage;
    HistoryRenderPart unsupportedAll{L"View unsupported, All accounts", TestHistoryView(), {1100, 790}, 530};
    unsupportedAll.view.view = HistoryView::ExtraUsage;
    HistoryRenderPart noReadings{L"No readings in range (24h while recording was off) and the No readings card",
                                 TestHistoryView()};
    noReadings.view.recordingEnabled = false;
    noReadings.view.canEnableRecording = true;
    noReadings.view.pill = HistoryUiPill::Off;
    noReadings.view.range = HistoryUiRange::Day;
    noReadings.view.noReadings = {{2, L"OAI", L"OpenAI"}, {1, L"CC 1", L"Anthropic"}, {1, L"CC 2", L"Anthropic"},
                                  {3, L"Go", L"Google Antigravity"}};
    // Brand-colored logos in the sidebar and the No readings card (high contrast: the ink).
    noReadings.view.logoBrands = {true, true};
    noReadings.chart = TestChartOf({}, [](HistoryController& c) { c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day)); });
    HistoryRenderPart dollars{L"Dollars only: no percentage line, the spending card below", TestHistoryView(),
                              {1100, 790}, 640};
    dollars.view.selected = 2;
    dollars.view.view = HistoryView::ExtraUsage;
    dollars.view.cards = {TestCard(L"Extra spending (USD) \u2014 details only", 6, L"USD 10.00", L"now",
                                   {L"Peak USD 10.00 \u00B7 Sat 31 Oct 10:31"})};
    dollars.view.cards[0].detailsOnly = true;
    dollars.chart = TestChartOf(TestMockupWeek(), select(0xC2, HistoryView::ExtraUsage));
    HistoryRenderPart partial{L"No readings card beside plotted accounts (CC 1 recording off, Go paused)", weekly()};
    partial.view.cards = {partial.view.cards[0], partial.view.cards[2], partial.view.cards[3]};
    partial.view.noReadings = {{1, L"CC 1", L"Anthropic \u00B7 recording off"},
                               {3, L"Go", L"Google Antigravity \u00B7 paused"}};
    partial.view.accounts[3].stale = true;
    std::vector<HistorySample> some = TestMockupWeek();
    std::erase_if(some, [](const HistorySample& s) { return s.accountId == 0xC1 || s.accountId == 0x60; });
    partial.chart = TestChartOf(some);

    // s-menu: the More menu open, the pointer on Open history folder.
    HistoryRenderPart menu{L"", weekly(), {1100, 790}, 330};
    menu.menu = true;
    menu.chart = week;

    // s-min: the minimum window (800 x 590), scrolled near the end.
    HistoryRenderPart minimum{L"", weekly(), {800, 550}};
    minimum.scroll = -90;
    minimum.chart = week;

    // q13b-scroll, s-menu: a short window's sidebar scrolled to its end, keyboard focus on the
    // last removed account, the pointer on Codex CI; CC 6 selected in All meters.
    HistoryRenderPart nav{L"", TestManyAccounts(), {1100, 550}};
    nav.view.selected = 12;
    nav.view.view = HistoryView::AllMeters;
    nav.view.logoBrands = {true, true};
    nav.navEnd = true;
    nav.state.focus = {kHistoryUiNavAccount + 61};
    nav.state.focusVisible = true;
    nav.state.hot = {kHistoryUiNavAccount + 44};
    nav.view.cards = {
        TestCard(L"5h", 0, L"12%", L"now", TestLines(L"58% \u00B7 Sun 25 Oct 12:32", L"141 h (83.9%)", L"0 h")),
        TestCard(L"Weekly", 1, L"91%", L"now", TestLines(L"91% \u00B7 Sat 31 Oct 14:32", L"166 h (98.8%)", L"3 h")),
        TestCard(L"Fable weekly", 2, L"58%", L"now", TestLines(L"63% \u00B7 Sun 25 Oct 23:32", L"166 h (98.8%)", L"0 h")),
        TestCard(L"Extra budget", 3, L"20%", L"Sat 31 Oct 04:31",
                 TestLines(L"20% \u00B7 Sat 31 Oct 04:31", L"158 h (94.0%)", L"0 h")),
        TestCard(L"Extra spending (USD)", 6, L"USD 10.00", L"Sat 31 Oct 04:31",
                 {L"Peak USD 10.00 \u00B7 Sat 31 Oct 04:31", L"Coverage 158 h (94.0%)"})};
    nav.chart = TestChartOf(TestMockupWeek(), select(0xC2, HistoryView::AllMeters));

    // s-band: CC 2 above 100%.
    HistoryRenderPart band{L"", weekly()};
    band.view.accounts[2].weeklyPct = 112;
    band.view.cards[2] = TestCard(L"CC 2 \u00B7 Weekly", 4, L"112%", L"now",
                                  TestLines(L"112% \u00B7 Sat 31 Oct 14:31", L"166 h (98.8%)", L"15.1 h"));
    band.chart = TestChartOf(TestMockupBand());

    // The chart sheets: whole views from the controllers, cropped to the header and the chart's card.
    auto real = [](std::wstring caption, std::shared_ptr<TestChartWindow> window, float crop = 520) {
        HistoryRenderPart part{std::move(caption), {}, {1100, 790}, crop};
        part.chart = std::move(window);
        part.chartView = true;
        return part;
    };
    // Every view (s-light, s-short, s-account; All meters with both extra axes).
    std::vector<HistoryRenderPart> views = {
        real(L"Short-term, All accounts, 24h: five-hour windows, Go's Claude/GPT windows varying",
             TestChartOf(TestMockupShort(), [](HistoryController& c) {
                 c.Command(HistoryUiCommand::View, int(HistoryView::ShortTerm));
                 c.Command(HistoryUiCommand::Range, int(HistoryUiRange::Day));
             })),
        real(L"Weekly, All accounts, 7d (a scoped quota 2 DIP wide)", week),
        real(L"Extra usage, All accounts: credits on the right axis; dollars details only",
             TestChartOf(TestMockupWeek(), [](HistoryController& c) { c.Command(HistoryUiCommand::View, int(HistoryView::ExtraUsage)); })),
        real(L"All meters, All accounts: percent, USD and credits axes",
             TestChartOf(TestMockupWeek(), [](HistoryController& c) { c.Command(HistoryUiCommand::View, int(HistoryView::AllMeters)); })),
        real(L"All meters, one account (CC 2): five-hour windows, weekly lines, dollars on their axis",
             TestChartOf(TestMockupWeek(), select(0xC2, HistoryView::AllMeters))),
    };
    // Each range's ticks (s-ticks) over two weekly lines kept 900 days.
    std::vector<HistoryRenderPart> ticks;
    for (auto [range, caption] : {std::pair{HistoryUiRange::Day, L"24h: hours, every 2 h at this width; midnight shows the day"},
                                  std::pair{HistoryUiRange::Week, L"7d: days, `Sat 24`"},
                                  std::pair{HistoryUiRange::Month, L"30d: dates, `Oct 5`, weekly on Mondays"},
                                  std::pair{HistoryUiRange::Quarter, L"90d: dates, every other Monday"},
                                  std::pair{HistoryUiRange::All, L"All retained (900 days): months, `Oct 2026`, quarterly"}}) {
        int value = int(range);
        ticks.push_back(real(caption, TestChartOf(TestMockupLong(900), [value](HistoryController& c) {
            c.Command(HistoryUiCommand::Range, value);
        })));
        ticks.back().chart->ops.settings.settings.historyRetentionDays = 900;
        ticks.back().chart->controller.Load();
    }
    // States: comparing (s-compare), above 100% (s-band), every line hidden (s-legend), chips off,
    // under the pointer and focused (q8a-filter), the chart focused with a chip under the pointer
    // (s-hc), and a readout on the chart (q6a-tooltip: six coincident lines, comparing).
    std::vector<HistoryRenderPart> states = {
        real(L"Comparing CC 2's weekly lines: previous week dashed, lone readings filled and hollow",
             TestChartOf(TestMockupCompare(), select(0xC2, HistoryView::Weekly, true))),
        real(L"Above 100%: the axis to 125%, the band to its top", TestChartOf(TestMockupBand())),
        real(L"Every line hidden: the percent axis alone, no band", TestChartOf(TestMockupWeek(), [](HistoryController& c) {
                 for (const HistoryUiLine& line : std::vector<HistoryUiLine>(c.view.lines)) {
                     c.Command(HistoryUiCommand::Line, line.meter, line.historyId);
                 }
             })),
    };
    auto falling = TestChartOf(TestMockupBand());
    falling->ops.settings.settings.barMode = BarMode::Remaining;
    falling->controller.Load();
    states.push_back(real(L"Remaining bar mode: shares left fall, the band at the bottom; CC 2 past 100% below 0%; cards",
                          falling, 790));
    auto filtered = TestChartOf(TestMockupWeek(), [](HistoryController& c) {
        for (size_t i : {size_t(1), size_t(4)}) {
            HistoryUiLine line = c.view.lines[i];
            c.Command(HistoryUiCommand::Line, line.meter, line.historyId);
        }
    });
    states.push_back(real(L"CC 1 and Go hidden; the pointer on CC 2's chip; keyboard focus on Go's (hidden) chip", filtered));
    states.back().state.hot = {filtered->controller.view.lines[2].chip};
    states.back().state.focus = {filtered->controller.view.lines[4].chip};
    states.back().state.focusVisible = true;
    states.push_back(real(L"Keyboard focus on the chart; the pointer on CC 2's chip (emphasis); CC 1 hidden",
                          TestChartOf(TestMockupWeek(), [](HistoryController& c) {
                              c.Command(HistoryUiCommand::Line, c.view.lines[1].meter, c.view.lines[1].historyId);
                          })));
    states.back().state.hot = {states.back().chart->controller.view.lines[2].chip};
    states.back().state.focus = {kHistoryUiChart};
    states.back().state.focusVisible = true;
    states.push_back(real(L"The readout on the chart: six coincident lines at hour 70, comparing",
                          TestChartOf(TestMockupWeek(), [](HistoryController& c) { c.Command(HistoryUiCommand::Compare, 1); }),
                          640));
    states.back().hover = [](const HistoryChart& c) {
        const std::vector<HistoryRenderPoint>& points = c.render[0][0].points;
        ULONGLONG target = kWeekFrom + 70 * kHourMs;
        size_t best = 0;
        for (size_t i = 0; i < points.size(); i++) {
            auto distance = [&](size_t k) {
                ULONGLONG time = c.snapshot->samples[points[k].sourceRow].timeMs;
                return time > target ? time - target : target - time;
            };
            if (distance(i) < distance(best)) best = i;
        }
        return HistoryChartPoint(c, c.lines[0], points[best]);
    };

    // One removed account (CC 2 no longer configured): its info bar with Delete history.
    std::vector<AccountConfig> kept = TestMockupAccounts();
    kept.erase(kept.begin() + 2);
    auto removedChart = TestChartOf(TestMockupWeek(), select(0xC2, HistoryView::AllMeters), kept);
    removedChart->ops.settings.polls.erase(removedChart->ops.settings.polls.begin() + 2);
    removedChart->controller.Load();
    HistoryRenderPart removed = real(L"One account: removed (CC 2 no longer configured), its bar with Delete history",
                                     removedChart, 640);

    const std::pair<const char*, std::vector<HistoryRenderPart>> sheets[] = {
        {"chart-views", views},
        {"chart-ticks", ticks},
        {"chart-states", states},
        {"default", {defaults}},
        {"account", {oai, cc1, removed}},
        {"compare", {compare}},
        {"states", {off, consent, storage, waiting, exporting, exportingMin}},
        {"notices", notices},
        {"empty", {loading, none, unsupported, unsupportedAll}},
        {"empty-readings", {noReadings, dollars, partial}},
        {"menu", {menu}},
        {"min", {minimum}},
        {"nav", {nav}},
        {"band", {band}},
    };
    for (const RenderTheme& theme : RenderThemes()) {
        for (UINT dpi : {96u, 144u, 192u}) {
            for (const auto& [sheet, parts] : sheets) {
                std::string name = std::string("render-history-") + sheet + "-" + theme.name + "-" +
                                   std::to_string(dpi) + ".png";
                Run(name, [&] {
                    RenderHistorySheet(g, wic.get(), directory + L"\\" + std::wstring(name.begin(), name.end()), parts,
                                       theme.input, dpi);
                    CHECK(true);
                });
            }
        }
    }
}
