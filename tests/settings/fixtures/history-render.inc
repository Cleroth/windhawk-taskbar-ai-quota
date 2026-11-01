// Offscreen renders of the history window's client area (the approved mockups' 1100 x 830 window
// without its 40 DIP caption) as the host composes it: main items, the navigation pane at its
// scroll, the content clipped to the viewport at its scroll with its scrollbar, and the More menu
// where the host opens it. Sheets stack captioned frames (or their top parts). PNG evidence for
// review, no pixel goldens.

struct HistoryRenderPart {
    std::wstring caption;
    HistoryUiView view;
    D2D1_SIZE_F client{1100, 790};
    float crop = 0;        // Only the frame's top part (DIPs); 0: all of it
    UiPaintState state{};
    float scroll = 0;      // The content's; negative: that far above its end
    bool navEnd = false;   // The navigation pane scrolled to its end
    bool menu = false;     // The More menu open, the pointer on its first row
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
        part.view.theme = theme.kind;
        UiFrame frame;
        HistoryUiBuildFrame(g, part.view, part.client, frame);
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
        UiPaintItems(target.get(), g, cache, palette, frame.main, state, {0, 0}, scale, -1e9f, 1e9f);
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
                     scroll + viewHeight);
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
            hover.hot = {1};
            UiPaintItems(target.get(), g, cache, palette, rows, hover, {box.left, box.top}, scale, -1e9f, 1e9f);
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
    auto weekly = [] {
        HistoryUiView view = TestHistoryView();
        view.cards = TestWeeklyCards();
        return view;
    };

    // s-light: All accounts, Weekly.
    HistoryRenderPart defaults{L"", weekly()};

    // s-account: OAI with its recording off (credits); CC 1 paused, its value stale (extra
    // budget, dollars details only).
    HistoryRenderPart oai{L"One account: OpenAI, Extra usage, recording off for this account", TestHistoryView()};
    oai.view.selected = 0;
    oai.view.view = HistoryView::ExtraUsage;
    oai.view.accounts[0].recordingOff = true;
    oai.view.cards = {TestCard(L"Credits remaining", 7, L"1240.00 credits", L"Thu 29 Oct 17:31",
                               {L"Peak 2500.00 credits \u00B7 Sat 24 Oct 14:32", L"Coverage 123 h (73.2%)"})};
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

    // s-compare: CC 2, Weekly, comparing (two columns of both periods).
    HistoryRenderPart compare{L"", TestHistoryView()};
    compare.view.selected = 2;
    compare.view.compare = true;
    compare.view.cards = {
        TestCard(L"Weekly", 1, L"93%", L"now",
                 {L"Current: Peak 93% \u00B7 Sat 31 Oct 14:31", L"Coverage 158 h (94.0%)", L"Est. \u226590%: 4 h",
                  L"Previous: Peak 88% \u00B7 Sat 24 Oct 14:32", L"Coverage 166 h (98.8%)", L"Est. \u226590%: 0 h"}),
        TestCard(L"Fable weekly", 2, L"58%", L"now",
                 {L"Current: Peak 63% \u00B7 Sun 25 Oct 22:32", L"Coverage 166 h (98.8%)", L"Est. \u226590%: 0 h",
                  L"Previous: Peak 61% \u00B7 Sat 24 Oct 14:32", L"Coverage 154 h (91.7%)", L"Est. \u226590%: 0 h"})};

    // s-states: recording off; storage failing; waiting, with keyboard focus on the range
    // segments and the pointer on Refresh.
    HistoryRenderPart off{L"Recording off: pill Off, caution bar with Enable recording", weekly(), {1100, 790}, 330};
    off.view.recordingEnabled = false;
    off.view.pill = HistoryUiPill::Off;
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
    noReadings.view.pill = HistoryUiPill::Off;
    noReadings.view.range = HistoryUiRange::Day;
    noReadings.view.noReadings = {{2, L"OAI", L"OpenAI"}, {1, L"CC 1", L"Anthropic"}, {1, L"CC 2", L"Anthropic"},
                                  {3, L"Go", L"Google Antigravity"}};
    HistoryRenderPart dollars{L"Dollars only: no percentage line, the spending card below", TestHistoryView(),
                              {1100, 790}, 640};
    dollars.view.selected = 2;
    dollars.view.view = HistoryView::ExtraUsage;
    dollars.view.cards = {TestCard(L"Extra spending (USD) \u2014 details only", 6, L"USD 10.00", L"now",
                                   {L"Peak USD 10.00 \u00B7 Sat 31 Oct 10:31"})};
    dollars.view.cards[0].detailsOnly = true;
    HistoryRenderPart partial{L"No readings card beside plotted accounts (CC 1 recording off, Go paused)", weekly()};
    partial.view.cards = {partial.view.cards[0], partial.view.cards[2], partial.view.cards[3]};
    partial.view.noReadings = {{1, L"CC 1", L"Anthropic \u00B7 recording off"},
                               {3, L"Go", L"Google Antigravity \u00B7 paused"}};
    partial.view.accounts[3].stale = true;

    // s-menu: the More menu open (Settings), the pointer on it.
    HistoryRenderPart menu{L"", weekly(), {1100, 790}, 330};
    menu.menu = true;

    // s-min: the minimum window (800 x 590), scrolled near the end.
    HistoryRenderPart minimum{L"", weekly(), {800, 550}};
    minimum.scroll = -90;

    // q13b-scroll, s-menu: a short window's sidebar scrolled to its end, keyboard focus on the
    // last removed account, the pointer on Codex CI; CC 6 selected in All meters.
    HistoryRenderPart nav{L"", TestManyAccounts(), {1100, 550}};
    nav.view.selected = 12;
    nav.view.view = HistoryView::AllMeters;
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

    // s-band: CC 2 above 100%.
    HistoryRenderPart band{L"", weekly()};
    band.view.accounts[2].weeklyPct = 112;
    band.view.cards[2] = TestCard(L"CC 2 \u00B7 Weekly", 4, L"112%", L"now",
                                  TestLines(L"112% \u00B7 Sat 31 Oct 14:31", L"166 h (98.8%)", L"15.1 h"));

    const std::pair<const char*, std::vector<HistoryRenderPart>> sheets[] = {
        {"default", {defaults}},
        {"account", {oai, cc1}},
        {"compare", {compare}},
        {"states", {off, storage, waiting}},
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
