// Toolkit and settings shell: synthetic item lists with hand-derived expectations.

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
        // The Gemini star touches every edge of its 24 view box (its XAML brand gradient maps
        // relative to its bounds).
        CHECK(SUCCEEDED(UiPathGeometry(g, kGeminiLogoPath)->GetBounds(nullptr, &bounds)));
        CHECK_NEAR(bounds.left, 0);
        CHECK_NEAR(bounds.top, 0);
        CHECK_NEAR(bounds.right, 24);
        CHECK_NEAR(bounds.bottom, 24);
        for (PCWSTR path : {kClaudeLogoPath, kOpenAiLogoPath, kCopilotLogoPath}) {
            CHECK(SUCCEEDED(UiPathGeometry(g, path)->GetBounds(nullptr, &bounds)));
            CHECK(bounds.left > -0.5f && bounds.top > -0.5f && bounds.right < 24.5f &&
                  bounds.bottom < 24.5f && bounds.right - bounds.left > 20);
        }
        // OpenCode's frame: 20 x 24 from x 2, with a 10 x 14.4 hole from (7, 4.8) wound the other
        // way, so the nonzero fill leaves it empty.
        ID2D1PathGeometry* opencode = UiPathGeometry(g, kOpenCodeLogoPath);
        CHECK(SUCCEEDED(opencode->GetBounds(nullptr, &bounds)));
        CHECK_NEAR(bounds.left, 2);
        CHECK_NEAR(bounds.top, 0);
        CHECK_NEAR(bounds.right, 22);
        CHECK_NEAR(bounds.bottom, 24);
        auto filled = [&](float x, float y) {
            BOOL inside = FALSE;
            opencode->FillContainsPoint({x, y}, nullptr, &inside);
            return bool(inside);
        };
        CHECK(filled(4.5f, 12) && filled(19.5f, 12) && filled(12, 2.4f) && filled(12, 21.6f));
        CHECK(!filled(12, 12) && !filled(7.5f, 5.3f) && !filled(16.5f, 18.7f));
        CHECK(!filled(1, 12) && !filled(23, 12));
        // An arc from 190 to 260 degrees around (26, 26), radius 23.25, clockwise. Its midpoint
        // (225 degrees) is at 26 - 23.25 / sqrt(2); the opposite sweep's isn't.
        ID2D1PathGeometry* arc = UiPathGeometry(g, L"M3.103,21.963 A23.25,23.25 0 0 1 21.963,3.103");
        CHECK(SUCCEEDED(arc->GetBounds(nullptr, &bounds)));
        CHECK(std::abs(bounds.left - 3.103f) < 0.01f && std::abs(bounds.right - 21.963f) < 0.01f);
        float mid = 26 - 23.25f / std::numbers::sqrt2_v<float>;
        BOOL on = FALSE, opposite = TRUE;
        arc->StrokeContainsPoint({mid, mid}, 0.5f, nullptr, nullptr, &on);
        arc->StrokeContainsPoint({15.5f, 15.5f}, 0.5f, nullptr, nullptr, &opposite);
        CHECK(on && !opposite);
        // Relative arcs close a circle of radius 1.7.
        CHECK(SUCCEEDED(UiPathGeometry(g, L"M4.371,14.025a1.7,1.7 0 1 0 3.4,0a1.7,1.7 0 1 0 -3.4,0Z")
                            ->GetBounds(nullptr, &bounds)));
        CHECK(std::abs(bounds.right - bounds.left - 3.4f) < 0.01f);
        CHECK(std::abs(bounds.bottom - bounds.top - 3.4f) < 0.01f);
        // App icon bars (view box 16): 4-unit tracks from x 1 to 15 at y 0, 6 and 12, with
        // rounded corners (the corner point lies outside); fills from x 1 to 6, 10 and 13, square
        // at their end; each fill's highlight is its taskbar one.
        static constexpr float kFillEnds[3] = {6, 10, 13};
        for (int i = 0; i < 3; i++) {
            ID2D1PathGeometry* track = UiPathGeometry(g, kSettingsAppIconTracks[i]);
            CHECK(SUCCEEDED(track->GetBounds(nullptr, &bounds)));
            CHECK_NEAR(bounds.left, 1);
            CHECK_NEAR(bounds.right, 15);
            CHECK_NEAR(bounds.top, 6 * i);
            CHECK_NEAR(bounds.bottom, 6 * i + 4);
            BOOL corner = TRUE, inside = FALSE;
            track->FillContainsPoint({1.1f, 6 * i + 0.1f}, nullptr, &corner);
            track->FillContainsPoint({14.5f, 6 * i + 2.0f}, nullptr, &inside);
            CHECK(!corner && inside);
            ID2D1PathGeometry* fill = UiPathGeometry(g, kSettingsAppIconFills[i]);
            CHECK(SUCCEEDED(fill->GetBounds(nullptr, &bounds)));
            CHECK_NEAR(bounds.left, 1);
            CHECK_NEAR(bounds.right, kFillEnds[i]);
            BOOL endCorner = FALSE;
            fill->FillContainsPoint({kFillEnds[i] - 0.1f, 6 * i + 0.1f}, nullptr, &endCorner);
            CHECK(endCorner);
            CHECK(kSettingsAppIconHighlights[i] == (UsageHighlight(0xFF000000 | kSettingsAppIconColors[i]) & 0xFFFFFF));
        }
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
        view.page = SettingsUiPage::Accounts;
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        int navs = 0;
        for (const UiItem& item : frame.main) {
            if (item.kind != UiItemKind::Nav) continue;
            navs++;
            CHECK(item.selected == (item.text == L"Accounts"));
            CHECK_NEAR(item.rect.bottom - item.rect.top, 38);
        }
        CHECK(navs == 6);
        CHECK(UiFindItem(frame, 100)->runs[0].text == L"Accounts");
        CHECK_NEAR(frame.viewport.left, 260);
        CHECK_NEAR(frame.viewport.top, 56);
        CHECK_NEAR(frame.viewport.bottom, 820);
        CHECK(frame.content.front().kind == UiItemKind::Card);
        CHECK_NEAR(frame.content.front().rect.left, 20);
        CHECK_NEAR(frame.content.front().rect.right, 700);  // 740 - 40
        frame = {};
        SettingsUiBuildFrame(g, view, {2000, 820}, frame);
        CHECK_NEAR(frame.content.front().rect.left, 360);   // 20 + (1740 - 60 - 1000) / 2
        CHECK_NEAR(frame.content.front().rect.right, 1360);
        CHECK_NEAR(UiFindItem(frame, 100)->rect.left, 260 + 360);
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
            if (item.action.kind == UiActionKind::OpenUrl) urls.push_back(item.action.text);
            for (const UiRun& run : item.runs) {
                if (run.style == UiRunStyle::Link) urls.push_back(run.action.text);
            }
        }
        CHECK(cards == 8);
        CHECK_NEAR(frame.contentHeight, lastBottom + 28);
        // Copy version info: a card under the app card, 62 DIP like the links; its button says
        // for a moment whether it copied, keeping its id.
        const UiItem* copy = UiFindItem(frame, SettingsControlId(SettingsField::CopyVersionInfo));
        CHECK(copy && copy->kind == UiItemKind::Button && copy->text == L"Copy" && copy->enabled &&
              copy->action.kind == UiActionKind::Command &&
              copy->action.target == uint32_t(SettingsField::CopyVersionInfo));
        const UiItem* copyCard = nullptr;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Card && !copyCard && item.rect.top > frame.content[0].rect.bottom) copyCard = &item;
        }
        CHECK(copyCard && copy && copyCard->rect.top < copy->rect.top && copy->rect.bottom < copyCard->rect.bottom);
        if (copyCard) {
            CHECK_NEAR(copyCard->rect.top - frame.content[0].rect.bottom, 3);
            CHECK_NEAR(copyCard->rect.bottom - copyCard->rect.top, 62);
        }
        for (std::wstring_view text : {L"Copy version info",
                                       L"The mod\u2019s name and version and the Windows build, for bug reports."}) {
            CHECK(std::any_of(frame.content.begin(), frame.content.end(),
                              [&](const UiItem& item) { return UiRunsText(item.runs) == text; }));
        }
        for (auto [copied, label] : {std::pair{true, L"Copied"}, std::pair{false, L"Copy failed"}}) {
            SettingsView shown = view;
            shown.versionCopied = copied;
            UiFrame after;
            SettingsUiBuildFrame(g, shown, {1000, 820}, after);
            const UiItem* button = UiFindItem(after, SettingsControlId(SettingsField::CopyVersionInfo));
            CHECK(button && button->text == label && button->action.target == uint32_t(SettingsField::CopyVersionInfo));
        }
        std::vector<std::wstring> expected = {
            L"https://github.com/Cleroth/windhawk-taskbar-ai-quota",
            L"https://github.com/Cleroth/windhawk-taskbar-ai-quota/issues/new",
            L"https://github.com/Cleroth/windhawk-taskbar-ai-quota#usage-history",
            L"https://github.com/amirf147", L"https://github.com/HughMacdonald", L"https://github.com/incconutwo",
            L"https://github.com/smnio", L"https://github.com/Sof2245", L"https://github.com/yalov",
            L"https://simpleicons.org", L"https://github.com/primer/octicons", L"https://opensource.org/license/mit"};
        CHECK(urls == expected);
        // Contributors: the names, linked, in one sentence.
        CHECK(std::any_of(frame.content.begin(), frame.content.end(), [](const UiItem& item) {
            return UiRunsText(item.runs) ==
                   L"Thanks to amirf147, Hugh Macdonald, incconutwo, smnio, Sof2245 and yalov.";
        }));
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
            if (item.font == UiFont::Caption && !item.runs.empty() && item.runs.size() == 5) {
                CHECK(item.rect.bottom - item.rect.top >= 48);
            }
        }
        CHECK(linkCards == 3);
        // The logos card shows each provider's mark once, OpenCode's last.
        std::vector<PCWSTR> marks;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Shape && item.parts.size() == 1) marks.push_back(item.parts[0].path);
        }
        std::erase_if(marks, [](PCWSTR path) {
            return path != kClaudeLogoPath && path != kOpenAiLogoPath && path != kGeminiLogoPath &&
                   path != kCopilotLogoPath && path != kOpenCodeLogoPath;
        });
        CHECK((marks == std::vector<PCWSTR>{kClaudeLogoPath, kOpenAiLogoPath, kGeminiLogoPath, kCopilotLogoPath,
                                            kOpenCodeLogoPath}));
        // The pages, Copy, the link cards, the contributors' six links, the logos' two, then the
        // license's.
        CHECK(UiFocusStops(frame).size() == 6 + 1 + 3 + 6 + 3);
    });

    Run("controls: toggles, dropdowns and number boxes are tab stops; spin buttons click only", [] {
        UiGraphics& g = TestGraphics();
        UiFrame frame;
        frame.viewport = {260, 56, 1000, 820};
        UiAction command{UiActionKind::Command, 0, {}, 9};
        UiAddToggle(frame.content, 300, {500, 10, 500 + UiToggleSize(g, UiToggleText{L"On", L"Off"}).width, 42}, true, true, command,
                    UiToggleText{L"On", L"Off"});
        UiAddDropdown(frame.content, 310, {300, 60, 560, 92}, {{L"One", 1}, {L"Two", 2}}, true,
                      {UiActionKind::Command, 2, {}, 9});
        UiAddNumberBox(frame.content, 320, {300, 110, 300 + UiNumberBoxSize(g, L"days").width, 142},
                       L"90", L"days", true, command);
        UiAddNumberBox(frame.content, 330, {300, 160, 456, 192}, L"5", L"days", false, command);
        CHECK(frame.content[1].text == L"Two");  // The dropdown shows its value's option
        CHECK(frame.content[0].text == L"On" && frame.content[0].selected);
        std::vector<UiFocusRef> expected = {{300}, {310}, {320}};
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == expected.size());
        for (size_t i = 0; i < stops.size(); i++) CHECK(stops[i].ref == expected[i]);
        // Number box at x 300..396: text area after 11 DIP padding, before 49 DIP of spin buttons;
        // up (321) and down (322) are 22 DIP each, ending 3 DIP before the edge; the unit (323)
        // starts 10 DIP after the box.
        const UiItem* box = UiFindItem(frame, 320);
        CHECK_NEAR(box->rect.right, 396);
        D2D1_RECT_F text = UiNumberBoxTextRect(*box);
        CHECK_NEAR(text.left, 311);
        CHECK_NEAR(text.right, 347);
        CHECK(UiHitTest(g, frame.content, {360, 126}) == UiFocusRef{321});
        CHECK(UiHitTest(g, frame.content, {380, 126}) == UiFocusRef{322});
        CHECK(UiHitTest(g, frame.content, {320, 126}) == UiFocusRef{320});
        CHECK(UiFindItem(frame, 321)->owner == 320 && UiFindItem(frame, 323)->kind == UiItemKind::Text);
        CHECK_NEAR(UiFindItem(frame, 323)->rect.left, 406);
        const UiAction* up = UiActionOf(frame, {321});
        const UiAction* down = UiActionOf(frame, {322});
        CHECK(up && up->kind == UiActionKind::Step && up->value == 1 && up->target == 9);
        CHECK(down && down->value == -1);
        // A disabled box and its buttons take no input.
        CHECK(UiHitTest(g, frame.content, {320, 176}).id == 0);
        CHECK(!UiActionOf(frame, {331}));
    });

    Run("card notes: a disabled card dims its title; reason and error lines add height", [] {
        UiGraphics& g = TestGraphics();
        UiCardSpec spec;
        spec.title = L"Custom interval";
        spec.description = {{L"2 to 1440 minutes."}};
        std::vector<UiItem> plain, noted;
        uint32_t id = 1;
        float plainHeight = UiAddCard(g, plain, id, 0, 0, 680, spec).card.bottom;
        spec.enabled = false;
        spec.reason = L"Choose Custom in Cloud poll interval to edit.";
        spec.error = L"Enter a whole number from 2 to 1440.";
        id = 1;
        UiCardLayout layout = UiAddCard(g, noted, id, 0, 0, 680, spec);
        // Title and description fit the 68 DIP minimum; each note adds 3 + 16.
        CHECK_NEAR(plainHeight, 68);
        CHECK_NEAR(layout.card.bottom, 1 + 12 + 20 + 16 + 19 + 19 + 12 + 1);
        CHECK(!noted[1].enabled && noted[2].enabled);
        CHECK(noted[3].kind == UiItemKind::Glyph && noted[3].text == L"\uE946" &&
              noted[3].ink == UiInk::Secondary);
        CHECK(noted[5].kind == UiItemKind::Glyph && noted[5].text == L"\uE783" &&
              noted[5].ink == UiInk::Danger && noted[6].runs[0].text == spec.error);
        CHECK(noted[4].rect.top >= noted[2].rect.bottom + 3 && noted[6].rect.top >= noted[4].rect.bottom);
    });

    Run("dropdown popup: rows, current value selected, placement below, above or clamped", [] {
        UiGraphics& g = TestGraphics();
        std::vector<UiItem> rows;
        UiAction field{UiActionKind::Command, 10, {}, 4};
        D2D1_SIZE_F size = UiLayoutOptions(g, {{L"5 minutes", 5}, {L"10 minutes", 10}, {L"Custom", 0}},
                                           field, 200, rows);
        CHECK_NEAR(size.width, 200);
        CHECK_NEAR(size.height, 4 + 3 * 36);
        CHECK(rows.size() == 3 && !rows[0].selected && rows[1].selected);
        CHECK(rows[2].action.value == 0 && rows[2].action.target == 4 && rows[2].kind == UiItemKind::Option);
        CHECK_NEAR(rows[1].rect.top, 4 + 36);
        CHECK_NEAR(rows[1].rect.bottom - rows[1].rect.top, 32);
        UiLayoutOptions(g, {{std::wstring(60, L'W'), 1}}, field, 100, rows);
        CHECK(rows[0].rect.right > 300);
        RECT work{0, 0, 1000, 800};
        POINT below = UiPopupPlacement({100, 100, 300, 132}, {200, 150}, work, 4);
        CHECK(below.x == 100 && below.y == 136);
        POINT above = UiPopupPlacement({100, 700, 300, 732}, {200, 150}, work, 4);
        CHECK(above.y == 700 - 4 - 150);
        POINT clamped = UiPopupPlacement({900, 100, 1100, 132}, {200, 150}, work, 4);
        CHECK(clamped.x == 800);
        POINT tall = UiPopupPlacement({100, 300, 300, 332}, {200, 700}, work, 4);
        CHECK(tall.y == 100);  // More room below: bottom-aligned to the work area
    });

    Run("dropdown popup keys: arrows, Home/End, Enter chooses, Esc closes, Tab passes on", [] {
        CHECK(UiOptionKey(3, 0, VK_DOWN).index == 1);
        CHECK(UiOptionKey(3, 2, VK_DOWN).index == 2);
        CHECK(UiOptionKey(3, -1, VK_UP).index == 2);
        CHECK(UiOptionKey(3, 1, VK_HOME).index == 0 && UiOptionKey(3, 1, VK_NEXT).index == 2);
        CHECK(UiOptionKey(3, 1, VK_RETURN).kind == UiOptionIntentKind::Choose);
        CHECK(UiOptionKey(3, -1, VK_SPACE).kind == UiOptionIntentKind::Close);
        CHECK(UiOptionKey(3, 1, VK_ESCAPE).kind == UiOptionIntentKind::Close);
        CHECK(UiOptionKey(3, 1, VK_TAB).kind == UiOptionIntentKind::CloseAndPass);
        CHECK(UiOptionKey(3, 1, 'A').kind == UiOptionIntentKind::None);
    });

    Run("info bar: its links are focus stops of the fixed text id", [] {
        UiGraphics& g = TestGraphics();
        UiFrame frame;
        uint32_t id = 1;
        float bottom = UiAddInfoBar(g, frame.content, id, 777, 0, 10, 680,
                                    {{L"See "}, {L"Accounts", UiRunStyle::Link, UiInk::Inherit,
                                                 {UiActionKind::Navigate, 0}},
                                     {L" and "}, {L"Behavior", UiRunStyle::Link, UiInk::Inherit,
                                                  {UiActionKind::Navigate, 3}}});
        CHECK_NEAR(bottom, 10 + 14 + 19 + 14);  // One 19 DIP line, 13 DIP padding, 1 DIP border
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == 2 && stops[0].ref == (UiFocusRef{777, 0}) && stops[1].ref == (UiFocusRef{777, 1}));
        CHECK(UiActionOf(frame, {777, 1})->value == 3);
        CHECK(frame.content[1].ink == UiInk::Accent);
    });

    Run("menus, nested cards, text boxes, chips and drag handles", [] {
        UiGraphics& g = TestGraphics();
        // A menu's options have details: 53 DIP two-line rows in 4 DIP padding, 310 DIP wide,
        // nothing selected; it opens right-aligned under its button.
        std::vector<UiItem> rows;
        UiAction add{UiActionKind::Command, -1, {}, 4};
        D2D1_SIZE_F size = UiLayoutOptions(g, {{L"First", 0, L"Detail line", {{kClaudeLogoPath}}},
                                               {L"Second", 1, L"Another detail", {{kOpenAiLogoPath}}}},
                                           add, 131, rows);
        CHECK_NEAR(size.width, 310);
        CHECK_NEAR(size.height, 8 + 2 * 53);
        CHECK(rows.size() == 2 && !rows[0].selected && !rows[1].selected && rows[1].action.value == 1);
        CHECK_NEAR(rows[1].rect.top, 4 + 53);
        CHECK_NEAR(rows[1].rect.left, 4);
        CHECK(UiRunsText(rows[1].runs) == L"Another detail" && rows[1].parts.size() == 1);
        POINT right = UiPopupPlacement({700, 100, 831, 132}, {310, 114}, {0, 0, 1000, 800}, 4, true);
        CHECK(right.x == 831 - 310 && right.y == 136);
        // A nested card: a rule instead of a panel, text 56 DIP in from the panel's edge.
        UiCardSpec spec;
        spec.title = L"Label";
        spec.trailing = {200, 32};
        spec.nested = true;
        spec.minHeight = 60;
        std::vector<UiItem> items;
        uint32_t id = 1;
        UiCardLayout nested = UiAddCard(g, items, id, 21, 100, 678, spec);
        CHECK(items[0].kind == UiItemKind::Rule && items[1].kind == UiItemKind::Text);
        CHECK_NEAR(items[1].rect.left, 20 + 56);
        CHECK_NEAR(nested.trailing.right, 700 - 19);
        CHECK_NEAR(nested.card.bottom - nested.card.top, 60);
        // A text box's text area keeps 11 DIP on both sides; it is a tab stop, a handle isn't.
        UiFrame frame;
        frame.content.push_back(TestItem(10, UiItemKind::Card, {0, 0, 400, 68}, kTestOpen));
        frame.content.back().button = UiButtonStyle::Subtle;
        UiItem handle = TestItem(11, UiItemKind::Handle, {5, 14, 35, 54}, {UiActionKind::Command, 0, {}, 5});
        handle.owner = 10;
        frame.content.push_back(handle);
        UiItem shown = TestItem(12, UiItemKind::Chip, {300, 24, 330, 44});
        frame.content.push_back(shown);
        UiItem box = TestItem(13, UiItemKind::TextBox, {100, 80, 300, 112}, {UiActionKind::Command, 0, {}, 6});
        box.maximum = 4;
        frame.content.push_back(box);
        D2D1_RECT_F text = UiNumberBoxTextRect(box);
        CHECK_NEAR(text.left, 111);
        CHECK_NEAR(text.right, 289);
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == 2 && stops[0].ref == UiFocusRef{10} && stops[1].ref == UiFocusRef{13});
        CHECK(UiHitTest(g, frame.content, {20, 30}) == UiFocusRef{11});
        CHECK(UiHitTest(g, frame.content, {310, 30}) == UiFocusRef{10});  // A shown chip passes through
        frame.content[2].enabled = false;
        CHECK(UiHitTest(g, frame.content, {310, 30}).id == 0);  // A disabled one blocks
    });
}
