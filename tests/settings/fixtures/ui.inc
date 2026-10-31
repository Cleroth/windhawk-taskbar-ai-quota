// Toolkit and settings shell: synthetic item lists with hand-derived expectations.

static UiGraphics& TestGraphics() {
    static UiGraphics graphics = [] {
        UiGraphics created;
        UiCreateGraphics(created);
        return created;
    }();
    return graphics;
}

static UiItem TestItem(uint32_t id, UiItemKind kind, D2D1_RECT_F rect, UiAction action = {}) {
    UiItem item;
    item.id = id;
    item.kind = kind;
    item.rect = rect;
    item.action = std::move(action);
    return item;
}

static const UiAction kTestOpen{UiActionKind::OpenUrl, 0, L"https://example.invalid"};

// Navigation 1-3, a title, then content: card 200, text 201 with two links, disabled button
// 202, link 203.
static UiFrame TestFrame() {
    UiFrame frame;
    for (uint32_t i = 1; i <= 3; i++) {
        frame.main.push_back(TestItem(i, UiItemKind::Nav, {8, 41.0f * i, 252, 41.0f * i + 38},
                                      {UiActionKind::Navigate, int(i)}));
    }
    frame.main[1].selected = true;
    UiItem title = TestItem(100, UiItemKind::Text, {280, 6, 900, 42});
    title.runs = {{L"Title"}};
    frame.main.push_back(title);
    frame.viewport = {260, 56, 1000, 820};
    frame.content.push_back(TestItem(200, UiItemKind::Card, {20, 0, 700, 68}, kTestOpen));
    UiItem text = TestItem(201, UiItemKind::Text, {40, 100, 640, 140});
    text.runs = {{L"Read "}, {L"first", UiRunStyle::Link, UiInk::Inherit, kTestOpen},
                 {L" and "}, {L"second", UiRunStyle::Link, UiInk::Inherit, kTestOpen}, {L"."}};
    frame.content.push_back(text);
    UiItem disabled = TestItem(202, UiItemKind::Button, {40, 150, 140, 182}, kTestOpen);
    disabled.enabled = false;
    frame.content.push_back(disabled);
    frame.content.push_back(TestItem(203, UiItemKind::Link, {40, 200, 140, 232}, kTestOpen));
    frame.contentHeight = 260;
    return frame;
}

static void RunUiTests() {
    Run("svg paths: logos and app icon parse with the expected extent", [] {
        UiGraphics& g = TestGraphics();
        D2D1_RECT_F bounds{};
        // The Gemini star touches every edge of its 24 view box.
        CHECK(SUCCEEDED(UiPathGeometry(g, kGeminiLogoPath)->GetBounds(nullptr, &bounds)));
        CHECK_NEAR(bounds.left, 0);
        CHECK_NEAR(bounds.top, 0);
        CHECK_NEAR(bounds.right, 24);
        CHECK_NEAR(bounds.bottom, 24);
        for (PCWSTR path : {kClaudeLogoPath, kOpenAiLogoPath}) {
            CHECK(SUCCEEDED(UiPathGeometry(g, path)->GetBounds(nullptr, &bounds)));
            CHECK(bounds.left > -0.5f && bounds.top > -0.5f && bounds.right < 24.5f &&
                  bounds.bottom < 24.5f && bounds.right - bounds.left > 20);
        }
        // First track arc: from 190 to 260 degrees around (26, 26), radius 23.25, clockwise.
        // Its midpoint (225 degrees) is at 26 - 23.25 / sqrt(2); the opposite sweep's isn't.
        ID2D1PathGeometry* arc = UiPathGeometry(g, kSettingsAboutTracks[0]);
        CHECK(SUCCEEDED(arc->GetBounds(nullptr, &bounds)));
        CHECK(std::abs(bounds.left - 3.103f) < 0.01f && std::abs(bounds.right - 21.963f) < 0.01f);
        float mid = 26 - 23.25f / std::numbers::sqrt2_v<float>;
        BOOL on = FALSE, opposite = TRUE;
        arc->StrokeContainsPoint({mid, mid}, 0.5f, nullptr, nullptr, &on);
        arc->StrokeContainsPoint({15.5f, 15.5f}, 0.5f, nullptr, nullptr, &opposite);
        CHECK(on && !opposite);
        // Dots are closed circles of radius 1.7.
        CHECK(SUCCEEDED(UiPathGeometry(g, kSettingsAboutDots[0])->GetBounds(nullptr, &bounds)));
        CHECK(std::abs(bounds.right - bounds.left - 3.4f) < 0.01f);
        CHECK(std::abs(bounds.bottom - bounds.top - 3.4f) < 0.01f);
        static constexpr PCWSTR kBroken = L"M0 0 L 3";
        bool threw = false;
        try { UiPathGeometry(g, kBroken); } catch (const winrt::hresult_error&) { threw = true; }
        CHECK(threw);
    });

    Run("palette: accents per theme, high contrast only from system colors", [] {
        UiThemeInput dark;
        dark.accent.light2 = 0x112233;
        dark.accent.light3 = 0x445566;
        UiPalette p = UiPaletteFor(dark);
        CHECK_NEAR(p.accent.r, 0x11 / 255.0);
        CHECK_NEAR(p.link.b, 0x66 / 255.0);
        CHECK_NEAR(p.text.r, 1);
        UiThemeInput light;
        light.kind = UiThemeKind::Light;
        light.accent.dark1 = 0x0A0B0C;
        p = UiPaletteFor(light);
        CHECK_NEAR(p.accent.b, 0x0C / 255.0);
        CHECK_NEAR(p.text.r, 0);
        UiThemeInput hc;
        hc.kind = UiThemeKind::HighContrast;
        hc.system = {0x000001, 0x000002, 0x000003, 0x000004, 0x000005, 0x000006, 0x000007, 0x000008};
        p = UiPaletteFor(hc);
        auto is = [](D2D1_COLOR_F color, uint32_t rgb) {
            return std::abs(color.b - float(rgb) / 255) < 1e-6f && color.r == 0 && color.g == 0 &&
                   color.a == 1;
        };
        CHECK(is(p.background, 1) && is(p.card, 1) && is(p.text, 2) && is(p.textMuted, 2));
        CHECK(is(p.accent, 3) && is(p.textOnAccent, 4) && is(p.textDisabled, 5));
        CHECK(is(p.link, 6) && is(p.control, 7) && is(p.controlBorder, 8) && is(p.cardBorder, 2));
    });

    Run("pinned header unpins below max(280, half the client)", [] {
        CHECK(!UiUnpinHeader(800, 56, 0));
        CHECK(!UiUnpinHeader(800, 56, 160));   // viewport 584 >= 400
        CHECK(!UiUnpinHeader(500, 56, 160));   // viewport 284 >= 280
        CHECK(UiUnpinHeader(480, 56, 160));    // viewport 264 < 280
        CHECK(UiUnpinHeader(1000, 56, 460));   // viewport 484 < 500
    });

    Run("focus order: main then content, inline links, disabled skipped", [] {
        std::vector<UiFocusStop> stops = UiFocusStops(TestFrame());
        std::vector<UiFocusRef> expected = {{1}, {2}, {3}, {200}, {201, 0}, {201, 1}, {203}};
        CHECK(stops.size() == expected.size());
        for (size_t i = 0; i < stops.size(); i++) CHECK(stops[i].ref == expected[i]);
        CHECK(stops[2].nav && !stops[3].nav && stops[3].surface == UiSurfaceId::Content);
    });

    Run("keys: tab wraps, arrows stay in their group, enter activates", [] {
        std::vector<UiFocusStop> stops = UiFocusStops(TestFrame());
        auto key = [&](UiFocusRef focus, UINT vk, bool shift = false,
                       UiSurfaceId surface = UiSurfaceId::Content) {
            return UiKeyIntent(stops, focus, surface, vk, shift, 40, 300);
        };
        CHECK(key({3}, VK_TAB).target == UiFocusRef{200});
        CHECK(key({1}, VK_TAB, true).target == UiFocusRef{203});
        CHECK(key({203}, VK_TAB).target == UiFocusRef{1});
        CHECK(key({}, VK_TAB).target == UiFocusRef{1});
        CHECK(key({1}, VK_DOWN, false, UiSurfaceId::Main).target == UiFocusRef{2});
        CHECK(key({3}, VK_DOWN, false, UiSurfaceId::Main).kind == UiIntentKind::None);
        CHECK(key({1}, VK_END, false, UiSurfaceId::Main).target == UiFocusRef{3});
        CHECK(key({3}, VK_HOME, false, UiSurfaceId::Main).target == UiFocusRef{1});
        CHECK(key({200}, VK_DOWN).target == (UiFocusRef{201, 0}));
        CHECK(key({200}, VK_UP).kind == UiIntentKind::ScrollBy && key({200}, VK_UP).scroll == -40);
        CHECK(key({203}, VK_DOWN).kind == UiIntentKind::ScrollBy && key({203}, VK_DOWN).scroll == 40);
        CHECK(key({200}, VK_NEXT).scroll == 300 && key({200}, VK_PRIOR).scroll == -300);
        CHECK(key({200}, VK_HOME).kind == UiIntentKind::ScrollTo && key({200}, VK_HOME).scroll == 0);
        CHECK(key({201, 1}, VK_RETURN).kind == UiIntentKind::Invoke);
        CHECK(key({201, 1}, VK_SPACE).target == (UiFocusRef{201, 1}));
        CHECK(key({}, VK_RETURN).kind == UiIntentKind::None);
    });

    Run("focus after rebuild: kept, next, previous, then navigation", [] {
        auto stops = [](std::initializer_list<uint32_t> ids) {
            std::vector<UiFocusStop> list;
            for (uint32_t id : ids) list.push_back({{id}});
            return list;
        };
        auto before = stops({1, 2, 3, 4});
        UiFocusRef nav{9};
        CHECK(UiResolveFocus(before, stops({1, 2, 3, 4}), {2}, nav) == UiFocusRef{2});
        CHECK(UiResolveFocus(before, stops({1, 3}), {2}, nav) == UiFocusRef{3});
        CHECK(UiResolveFocus(before, stops({1, 3}), {4}, nav) == UiFocusRef{3});
        CHECK(UiResolveFocus(before, stops({}), {2}, nav) == nav);
        CHECK(UiResolveFocus(before, stops({1}), {7}, nav) == nav);
    });

    Run("hit testing: inline links, topmost interactive item, disabled blocks", [] {
        UiGraphics& g = TestGraphics();
        UiFrame frame = TestFrame();
        const UiItem& text = frame.content[1];
        std::vector<D2D1_RECT_F> second = UiFocusBounds(g, text, 1);
        CHECK(second.size() == 1);
        D2D1_POINT_2F center{(second[0].left + second[0].right) / 2,
                             (second[0].top + second[0].bottom) / 2};
        CHECK(UiHitTest(g, frame.content, center) == (UiFocusRef{201, 1}));
        CHECK(UiHitTest(g, frame.content, {text.rect.left + 1, text.rect.top + 5}).id == 0);
        // A card under a text item stays hittable outside the links.
        frame.content[1].rect = {40, 10, 640, 50};
        CHECK(UiHitTest(g, frame.content, {41, 15}) == UiFocusRef{200});
        CHECK(UiHitTest(g, frame.content, {60, 160}).id == 0);
        frame.content[2].rect = {30, 5, 60, 30};  // Disabled button over the card.
        CHECK(UiHitTest(g, frame.content, {35, 20}).id == 0);
        CHECK(UiHitTest(g, frame.content, {50, 210}) == UiFocusRef{203});
        CHECK(UiHitTest(g, frame.main, {20, 90}) == UiFocusRef{2});
    });

    Run("pointer: release activates only the pressed element", [] {
        UiPointerState pointer;
        CHECK(UiPointerMove(pointer, {5}));
        CHECK(!UiPointerMove(pointer, {5}));
        pointer.pressed = {5};
        CHECK(UiPointerRelease(pointer, {5}) == UiFocusRef{5});
        CHECK(pointer.pressed.id == 0);
        pointer.pressed = {5};
        CHECK(UiPointerRelease(pointer, {6}).id == 0);
        CHECK(UiPointerRelease(pointer, {}).id == 0);
    });

    Run("scroll model: clamp, into view, scrollbar geometry", [] {
        CHECK(UiClampScroll(-5, 400, 800) == 0);
        CHECK(UiClampScroll(500, 400, 800) == 400);
        CHECK(UiClampScroll(100, 400, 300) == 0);
        CHECK(UiScrollIntoView(100, 400, 50, 80, 12) == 38);
        CHECK(UiScrollIntoView(100, 400, 480, 520, 12) == 132);
        CHECK(UiScrollIntoView(100, 400, 200, 240, 12) == 100);
        CHECK(!UiScrollbarFor({500, 400}, 0, 400, false).visible);
        UiScrollbar top = UiScrollbarFor({500, 400}, 0, 800, false);
        CHECK(top.visible);
        CHECK_NEAR(top.thumb.top, 2);
        CHECK_NEAR(top.thumb.bottom - top.thumb.top, 198);  // 396 * 400 / 800
        CHECK_NEAR(top.thumb.right - top.thumb.left, 2);
        UiScrollbar end = UiScrollbarFor({500, 400}, 400, 800, true);
        CHECK_NEAR(end.thumb.bottom, 398);
        CHECK_NEAR(end.thumb.right - end.thumb.left, 6);
        CHECK_NEAR(end.thumb.right, 497);
        CHECK_NEAR(UiScrollForThumb(end, 2 + 99, 400, 800), 200);
    });

    Run("settings shell: navigation, header, capped and centered content", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view;
        view.page = SettingsUiPage::Layout;
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        int navs = 0;
        for (const UiItem& item : frame.main) {
            if (item.kind != UiItemKind::Nav) continue;
            navs++;
            CHECK(item.selected == (item.text == L"Layout"));
            CHECK_NEAR(item.rect.bottom - item.rect.top, 38);
        }
        CHECK(navs == 6);
        CHECK(frame.main.back().runs[0].text == L"Layout");
        CHECK_NEAR(frame.viewport.left, 260);
        CHECK_NEAR(frame.viewport.top, 56);
        CHECK_NEAR(frame.viewport.bottom, 820);
        CHECK(frame.content.front().kind == UiItemKind::Card);
        CHECK_NEAR(frame.content.front().rect.left, 20);
        CHECK_NEAR(frame.content.front().rect.right, 700);  // 740 - 40
        bool placeholder = std::any_of(frame.content.begin(), frame.content.end(),
            [](const UiItem& item) { return !item.runs.empty() && item.runs[0].text == L"Not built yet"; });
        CHECK(placeholder);
        frame = {};
        SettingsUiBuildFrame(g, view, {2000, 820}, frame);
        CHECK_NEAR(frame.content.front().rect.left, 360);   // 20 + (1740 - 60 - 1000) / 2
        CHECK_NEAR(frame.content.front().rect.right, 1360);
        CHECK_NEAR(frame.main.back().rect.left, 260 + 360);
    });

    Run("about page: cards stack, links open their pages", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view;
        view.page = SettingsUiPage::About;
        view.version = L"9.8.7";
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        std::vector<std::wstring> urls;
        float lastBottom = 0;
        int cards = 0;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Card) {
                CHECK(item.rect.top >= lastBottom);
                lastBottom = item.rect.bottom;
                CHECK_NEAR(item.rect.right - item.rect.left, 680);
                cards++;
            }
            if (item.action.kind == UiActionKind::OpenUrl) urls.push_back(item.action.url);
            for (const UiRun& run : item.runs) {
                if (run.style == UiRunStyle::Link) urls.push_back(run.action.url);
            }
        }
        CHECK(cards == 6);
        CHECK_NEAR(frame.contentHeight, lastBottom + 28);
        std::vector<std::wstring> expected = {
            L"https://github.com/Cleroth/windhawk-taskbar-ai-quota",
            L"https://github.com/Cleroth/windhawk-taskbar-ai-quota/issues/new",
            L"https://github.com/Cleroth/windhawk-taskbar-ai-quota#usage-history",
            L"https://simpleicons.org", L"https://opensource.org/license/mit"};
        CHECK(urls == expected);
        bool version = std::any_of(frame.content.begin(), frame.content.end(), [](const UiItem& item) {
            return !item.runs.empty() && item.runs[0].text.starts_with(L"Version 9.8.7");
        });
        CHECK(version);
        // Link cards: 62 DIP minimum; the logos description wraps to several caption lines.
        CHECK_NEAR(frame.content[0].rect.top, 2);
        int linkCards = 0;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Card && item.action.kind == UiActionKind::OpenUrl) {
                CHECK_NEAR(item.rect.bottom - item.rect.top, 62);
                linkCards++;
            }
            if (item.font == UiFont::Caption && !item.runs.empty() && item.runs.size() == 3) {
                CHECK(item.rect.bottom - item.rect.top >= 48);
            }
        }
        CHECK(linkCards == 3);
        CHECK(UiFocusStops(frame).size() == 6 + 3 + 2);
    });
}
