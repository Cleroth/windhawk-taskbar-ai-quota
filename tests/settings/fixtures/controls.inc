// Toolkit controls for the custom-drawn history window: segmented controls, ellipsis trimming,
// the scrolling navigation pane, the leading switch, status pills and info bar severities.
// Synthetic items with hand-derived expectations.

// Whether a layout's last character is cut off by its trimming.
static bool TestTrimmed(IDWriteTextLayout* layout, size_t length) {
    float x = 0, y = 0;
    DWRITE_HIT_TEST_METRICS hit{};
    CHECK(SUCCEEDED(layout->HitTestTextPosition(UINT32(length - 1), FALSE, &x, &y, &hit)));
    return hit.isTrimmed != FALSE;
}

static UINT32 TestLines(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS metrics{};
    CHECK(SUCCEEDED(layout->GetMetrics(&metrics)));
    return metrics.lineCount;
}

// A navigation pane 400 DIP tall at (0, top) with 20 entries on the 41 DIP pitch (6 DIP above
// the first, 6 below the last: 829 DIP of content), and a main navigation item and title.
static UiFrame TestPaneFrame(float top = 0) {
    UiFrame frame = TestFrame();
    frame.navPane = {0, top, 260, top + 400};
    for (int i = 0; i < 20; i++) {
        float y = top + 6 + 41.0f * float(i);
        frame.nav.push_back(TestItem(1000 + uint32_t(i), UiItemKind::Nav, {8, y, 252, y + 38},
                                     {UiActionKind::Navigate, i}));
    }
    frame.navHeight = 6 + 41 * 19 + 38 + 6;
    return frame;
}

static void RunControlTests() {
    Run("segmented: segments side by side inside the frame, one tab stop, a hit per segment, focus "
        "on the selected one; disabled blocks", [] {
        UiGraphics& g = TestGraphics();
        std::vector<UiOption> options = {{L"24h", 1}, {L"7d", 7}, {L"30d", 30}};
        float widths[] = {TestTextWidth(L"24h", UiFont::Note) + 24, TestTextWidth(L"7d", UiFont::Note) + 24,
                          TestTextWidth(L"30d", UiFont::Note) + 24};
        D2D1_SIZE_F size = UiSegmentedSize(g, options);
        CHECK_NEAR(size.width, 3 + widths[0] + widths[1] + widths[2] + 3);
        CHECK_NEAR(size.height, 32);
        UiFrame frame;
        UiAddSegmented(frame.main, 50, {100, 10, 100 + size.width, 42}, options, true,
                       {UiActionKind::Command, 7, {}, 3});
        const UiItem& control = frame.main[0];
        std::vector<D2D1_RECT_F> rects = UiSegmentRects(g, control);
        CHECK(rects.size() == 3);
        CHECK_NEAR(rects[0].left, 103);
        CHECK_NEAR(rects[0].top, 13);
        CHECK_NEAR(rects[0].bottom, 39);
        CHECK_NEAR(rects[0].right, 103 + widths[0]);
        CHECK_NEAR(rects[1].left, rects[0].right);
        CHECK_NEAR(rects[2].left, rects[1].right);
        CHECK_NEAR(rects[2].right, 100 + size.width - 3);
        CHECK(UiSelectedSegment(control) == 1);
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == 1 && stops[0].ref == UiFocusRef{50});
        CHECK(UiHitTest(g, frame.main, {(rects[2].left + rects[2].right) / 2, 26}) == (UiFocusRef{50, 2}));
        CHECK(UiHitTest(g, frame.main, {rects[0].left + 1, 26}) == (UiFocusRef{50, 0}));
        CHECK(UiHitTest(g, frame.main, {101, 26}) == UiFocusRef{50});  // Its frame: the control
        std::vector<D2D1_RECT_F> bounds = UiFocusBounds(g, control, -1);
        CHECK(bounds.size() == 1);
        CHECK_NEAR(bounds[0].left, rects[1].left);
        CHECK_NEAR(bounds[0].right, rects[1].right);
        CHECK(!UiActionOf(frame, {50, 2}));  // Segments select through the host
        frame.main[0].action.value = 99;  // No option selected: the ring takes the control
        CHECK(UiSelectedSegment(frame.main[0]) == -1);
        CHECK_NEAR(UiFocusBounds(g, frame.main[0], -1)[0].left, 100);
        frame.main[0].enabled = false;
        CHECK(UiHitTest(g, frame.main, {(rects[2].left + rects[2].right) / 2, 26}).id == 0);
        CHECK(UiFocusStops(frame).empty());
    });

    Run("segmented keys: arrows stop at the ends, Home/End, other keys aren't the control's", [] {
        UiItem control = TestItem(50, UiItemKind::Segmented, {0, 0, 200, 32}, {UiActionKind::Command, 7, {}, 3});
        control.options = {{L"24h", 1}, {L"7d", 7}, {L"30d", 30}};
        CHECK(UiSegmentKey(control, VK_LEFT) == 0);
        CHECK(UiSegmentKey(control, VK_RIGHT) == 2);
        CHECK(UiSegmentKey(control, VK_HOME) == 0);
        CHECK(UiSegmentKey(control, VK_END) == 2);
        control.action.value = 1;
        CHECK(UiSegmentKey(control, VK_LEFT) == 0);
        control.action.value = 30;
        CHECK(UiSegmentKey(control, VK_RIGHT) == 2);
        control.action.value = 99;
        CHECK(UiSegmentKey(control, VK_LEFT) == 0 && UiSegmentKey(control, VK_RIGHT) == 0);
        for (UINT key : {UINT(VK_UP), UINT(VK_DOWN), UINT(VK_RETURN), UINT(VK_SPACE), UINT(VK_F5), UINT('A')}) {
            CHECK(!UiSegmentKey(control, key));
        }
        control.options.clear();
        CHECK(!UiSegmentKey(control, VK_RIGHT));
    });

    Run("trimming: one line ending in an ellipsis at the width; fitting text unchanged; one sign per "
        "format; its own cache key; rich text too", [] {
        UiGraphics& g = TestGraphics();
        UiTextStyle body = UiTextStyleFor(UiFont::Body);
        std::wstring text = L"Research \u2014 shared Max seat for the whole team";
        IDWriteTextLayout* full = UiLayout(g, text, body, 120, false);
        IDWriteTextLayout* cut = UiLayout(g, text, body, 120, false, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        CHECK(full != cut);
        CHECK(UiLayoutSize(full).width > 120);
        CHECK(UiLayoutSize(cut).width <= 120.01f);
        CHECK(TestTrimmed(cut, text.size()) && !TestTrimmed(full, text.size()));
        // Trimming wins over wrapping.
        IDWriteTextLayout* wrapped = UiLayout(g, text, body, 120, true, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        CHECK(TestLines(wrapped) == 1 && TestLines(UiLayout(g, text, body, 120, true)) > 1);
        IDWriteTextLayout* fits = UiLayout(g, L"CC 6", body, 120, false, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        CHECK_NEAR(UiLayoutSize(fits).width, UiLayoutSize(UiLayout(g, L"CC 6", body, 120, false)).width);
        CHECK(!TestTrimmed(fits, 4));
        size_t signs = g.ellipses.size();
        UiTextStyle odd{UiFamily::Text, 17, 23, DWRITE_FONT_WEIGHT_NORMAL};  // No other test's style
        UiLayout(g, text, odd, 77, false, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        CHECK(g.ellipses.size() == signs + 1);
        UiLayout(g, L"Other text", odd, 50, false, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        CHECK(g.ellipses.size() == signs + 1);
        std::vector<UiRun> runs = {{L"Research \u2014 shared Max seat", UiRunStyle::Strong}, {L" \u00B7 Fable weekly"}};
        IDWriteTextLayout* rich = UiRichLayout(g, runs, UiFont::Caption, 100, DWRITE_TEXT_ALIGNMENT_LEADING, true);
        CHECK(rich != UiRichLayout(g, runs, UiFont::Caption, 100));
        CHECK(TestLines(rich) == 1 && UiLayoutSize(rich).width <= 100.01f);
        CHECK(TestTrimmed(rich, UiRunsText(runs).size()));
    });

    Run("navigation pane: its stops first, its surface; the scrollbar at its right edge; scrolling "
        "an entry into view within the range", [] {
        UiFrame frame = TestPaneFrame();
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == 20 + 7);
        CHECK(stops[0].ref == UiFocusRef{1000} && stops[0].surface == UiSurfaceId::Nav && stops[0].nav);
        CHECK(stops[19].ref == UiFocusRef{1019} && stops[20].ref == UiFocusRef{1});
        CHECK(stops[20].surface == UiSurfaceId::Main);
        UiSurfaceId surface = UiSurfaceId::Main;
        CHECK(UiFindItem(frame, 1005, &surface) && surface == UiSurfaceId::Nav);
        // Arrows and Home/End move among every navigation stop, pane and main.
        CHECK(UiKeyIntent(stops, {1019}, UiSurfaceId::Main, VK_DOWN, false, 40, 300).target == UiFocusRef{1});
        CHECK(UiKeyIntent(stops, {3}, UiSurfaceId::Main, VK_HOME, false, 40, 300).target == UiFocusRef{1000});

        UiScrollbar top = UiNavScrollbar(frame, 0, false);
        CHECK(top.visible);
        CHECK_NEAR(top.track.left, 248);
        CHECK_NEAR(top.track.right, 260);
        CHECK_NEAR(top.track.top, 2);
        CHECK_NEAR(top.track.bottom, 398);
        CHECK_NEAR(top.thumb.top, 2);
        CHECK(std::abs(top.thumb.bottom - top.thumb.top - 396.0 * 400 / 829) < 1e-3);
        CHECK_NEAR(top.thumb.right, 257);
        CHECK_NEAR(top.thumb.right - top.thumb.left, 2);
        UiScrollbar end = UiNavScrollbar(frame, 429, true);
        CHECK_NEAR(end.thumb.bottom, 398);
        CHECK_NEAR(end.thumb.right - end.thumb.left, 6);
        CHECK_NEAR(UiScrollForThumb(end, end.thumb.top, 400, 829), 429);

        // Into view with 6 DIP around, clamped to the 429 DIP range.
        auto entry = [&](int i) { return frame.nav[size_t(i)].rect; };
        CHECK_NEAR(UiNavScrollTo(frame, 0, entry(10)), 454 + 6 - 400);
        CHECK_NEAR(UiNavScrollTo(frame, 0, entry(19)), 429);
        CHECK_NEAR(UiNavScrollTo(frame, 429, entry(0)), 0);
        CHECK_NEAR(UiNavScrollTo(frame, 60, entry(5)), 60);
        CHECK_NEAR(UiNavScrollTo(frame, 0, {8, 900, 252, 938}), 429);  // Past the content: clamped

        // A pane below a header: the same, relative to the pane's top.
        UiFrame lower = TestPaneFrame(40);
        UiScrollbar bar = UiNavScrollbar(lower, 0, false);
        CHECK_NEAR(bar.track.top, 42);
        CHECK_NEAR(bar.track.bottom, 438);
        CHECK_NEAR(UiNavScrollTo(lower, 100, lower.nav[0].rect), 0);
        CHECK_NEAR(UiNavScrollTo(lower, 0, lower.nav[19].rect), 429);

        // Content that fits: no scrollbar, nothing to scroll.
        frame.navHeight = 400;
        CHECK(!UiNavScrollbar(frame, 0, false).visible);
        CHECK_NEAR(UiNavScrollTo(frame, 0, entry(19)), 0);
    });

    Run("leading switch: the switch then its label, the whole item toggles; the trailing one unchanged", [] {
        UiGraphics& g = TestGraphics();
        D2D1_SIZE_F size = UiToggleSize(g, L"Compare previous week");
        CHECK_NEAR(size.width, 40 + 8 + TestTextWidth(L"Compare previous week", UiFont::Note));
        CHECK_NEAR(size.height, 32);
        UiFrame frame;
        UiAction command{UiActionKind::Command, 1, {}, 4};
        UiAddToggle(frame.main, 60, {300, 10, 300 + size.width, 42}, false, true, command, L"Compare previous week");
        UiAddToggle(frame.main, 61, {300, 60, 300 + UiToggleSize(g).width, 92}, true, true, command);
        const UiItem& leading = frame.main[0];
        CHECK(leading.text == L"Compare previous week" && leading.align == DWRITE_TEXT_ALIGNMENT_TRAILING);
        CHECK(!leading.selected && leading.action.value == 1);
        CHECK(frame.main[1].text == L"On" && frame.main[1].align == DWRITE_TEXT_ALIGNMENT_LEADING);
        CHECK(UiHitTest(g, frame.main, {300 + size.width - 5, 26}) == UiFocusRef{60});  // On the label
        CHECK(UiHitTest(g, frame.main, {305, 26}) == UiFocusRef{60});                   // On the switch
        CHECK(UiFocusStops(frame).size() == 2);
    });

    Run("status pills: caption text after a dot, 20 DIP tall; decoration only", [] {
        UiGraphics& g = TestGraphics();
        D2D1_SIZE_F size = UiPillSize(g, L"Recording \u00B7 90 days");
        CHECK_NEAR(size.width, 9 + 6 + 4 + TestTextWidth(L"Recording \u00B7 90 days", UiFont::Caption) + 9);
        CHECK_NEAR(size.height, 20);
        UiFrame frame;
        frame.main.push_back(TestItem(70, UiItemKind::Card, {0, 0, 400, 100}, kTestOpen));
        UiAddPill(frame.main, 71, {10, 10, 10 + size.width, 30}, L"Recording \u00B7 90 days", UiInk::Success, true);
        UiAddPill(frame.main, 72, {10, 40, 60, 60}, L"Off", UiInk::Secondary, false);
        const UiItem& pill = frame.main[1];
        CHECK(pill.kind == UiItemKind::Pill && pill.font == UiFont::Caption && pill.ink == UiInk::Success &&
              pill.selected && !frame.main[2].selected);
        CHECK(!UiActive(pill) && !UiInteractiveKind(pill.kind));
        CHECK(UiHitTest(g, frame.main, {20, 20}) == UiFocusRef{70});  // Passes through to the card
        CHECK(UiFocusStops(frame).size() == 1);
    });

    Run("info bar severities: tinted card, circle and mark; a trailing button centered against the "
        "text; the plain info bar keeps its layout", [] {
        UiGraphics& g = TestGraphics();
        UiFrame frame;
        uint32_t id = 1;
        UiAction enable{UiActionKind::Command, 1, {}, 5};
        float bottom = UiAddInfoBar(g, frame.content, id, 777, 0, 10, 680, {{L"History recording is off."}},
                                    UiSeverity::Caution, UiInfoButton{778, L"Enable recording", enable});
        // One 19 DIP line: the button's 28 DIP and 10 DIP above and below it set the height.
        CHECK_NEAR(bottom, 10 + 48);
        const std::vector<UiItem>& items = frame.content;
        CHECK(items.size() == 5);
        CHECK(items[0].kind == UiItemKind::Card && items[0].ink == UiInk::Caution);
        CHECK(items[1].kind == UiItemKind::Glyph && items[1].text == L"\uF136" && items[1].ink == UiInk::Caution);
        CHECK(items[2].kind == UiItemKind::Glyph && items[2].text == L"\uF13C" && items[2].ink == UiInk::OnAccent);
        CHECK(items[3].id == 777 && items[3].kind == UiItemKind::Text);
        CHECK_NEAR(items[3].rect.top, 10 + (48 - 19) / 2.0);
        CHECK_NEAR(items[1].rect.top, items[3].rect.top + 1.5);
        float buttonWidth = TestTextWidth(L"Enable recording", UiFont::Note) + 24;
        const UiItem& button = items[4];
        CHECK(button.id == 778 && button.kind == UiItemKind::Button && button.font == UiFont::Note &&
              button.action.target == 5);
        CHECK_NEAR(button.rect.right, 680 - 13);
        CHECK_NEAR(button.rect.right - button.rect.left, buttonWidth);
        CHECK_NEAR(button.rect.top, 10 + 10);
        CHECK_NEAR(button.rect.bottom, 10 + 38);
        CHECK_NEAR(items[3].rect.right, button.rect.left - 12);
        CHECK(UiFocusStops(frame).size() == 1 && UiFocusStops(frame)[0].ref == UiFocusRef{778});

        // Error without a button: the info bar's padding; a red card and its mark.
        frame = {};
        id = 1;
        bottom = UiAddInfoBar(g, frame.content, id, 777, 0, 10, 680, {{L"History is not being saved."}},
                              UiSeverity::Error);
        CHECK_NEAR(bottom, 10 + 14 + 19 + 14);
        CHECK(frame.content[0].ink == UiInk::Danger && frame.content[2].text == L"\uF13D");
        CHECK_NEAR(frame.content[3].rect.right, 680 - 19);
        // Info: one accent glyph, a plain card.
        frame = {};
        id = 1;
        UiAddInfoBar(g, frame.content, id, 777, 0, 10, 680, {{L"Exported."}});
        CHECK(frame.content.size() == 3 && frame.content[0].ink == UiInk::Primary &&
              frame.content[1].text == L"\uF167" && frame.content[1].ink == UiInk::Accent);
    });

    Run("palette: info bar backgrounds and the selected segment per theme", [] {
        UiThemeInput dark, light, hc;
        light.kind = UiThemeKind::Light;
        hc.kind = UiThemeKind::HighContrast;
        UiPalette d = UiPaletteFor(dark), l = UiPaletteFor(light), h = UiPaletteFor(hc);
        CHECK(UiColorKey(d.cautionBackground) == 0xFF433519 && UiColorKey(d.dangerBackground) == 0xFF442726);
        CHECK(UiColorKey(l.cautionBackground) == 0xFFFFF4CE && UiColorKey(l.dangerBackground) == 0xFFFDE7E9);
        CHECK(UiColorKey(h.cautionBackground) == UiColorKey(h.background));
        CHECK(UiColorKey(h.segment) == UiColorKey(h.highlight));
        CHECK(UiColorKey(l.segment) == 0xFFFFFFFF && l.segmentBorder.a > 0 && d.segmentBorder.a == 0);
    });
}
