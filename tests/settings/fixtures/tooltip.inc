// Toolkit tooltips (ui/tooltip-layout.h) and the chart item: the readout layout search, the
// pointer-excluding placement in adversarial geometries, the tooltip's items, content and
// placement dedupe; a chart's hit testing, tab stop and scroll keys. Synthetic rows; text widths
// are measured with the production fonts and the expectations derived from them by hand.

// The plan's widest bounded mandatory part (rev 3.2 measurements: 281.5 DIP).
static constexpr PCWSTR kTestCreditsDetail = L"[Previous week] 9999.99 credits \u00B7 2026-10-25 23:59:59";
static constexpr PCWSTR kTestMockDetail = L"[Previous week] 37.6% \u00B7 Tue 20 Oct 12:32";
static constexpr PCWSTR kTestHint = L" \u2014 hide lines in the legend or select one account";

// `count` rows titled `title` with the mandatory part `detail`.
static UiTooltipContent TestReadout(int count, std::wstring title = L"Go \u00B7 Gemini weekly",
                                    std::wstring detail = kTestMockDetail, uint64_t key = 1) {
    UiTooltipContent content;
    content.key = key;
    content.more = kTestHint;
    for (int i = 0; i < count; i++) content.rows.push_back({title, {{detail}}, UiRgb(0x9B8AFB), 2});
    return content;
}

static float TestCaptionWidth(const std::wstring& text) {
    return UiLayoutSize(UiRichLayout(TestGraphics(), {{text}}, UiFont::Caption, 100000)).width;
}

// The most rows any of the four regions around the exclusion rectangle could show.
static int TestBestShown(const UiTooltipContent& content, const UiTooltipKey& key) {
    UiGraphics& g = TestGraphics();
    UiTooltipMeasure measure = UiMeasureReadout(g, content);
    RECT ex = TestExclusion(key);
    const RECT& w = key.work;
    const RECT regions[] = {{w.left, std::max(ex.bottom, w.top), w.right, w.bottom},
                            {std::max(ex.right, w.left), w.top, w.right, w.bottom},
                            {w.left, w.top, w.right, std::min(ex.top, w.bottom)},
                            {w.left, w.top, std::min(ex.left, w.right), w.bottom}};
    float scale = float(key.dpi) / 96;
    int best = 0;
    for (const RECT& r : regions) {
        if (r.right <= r.left || r.bottom <= r.top) continue;
        auto layout = UiLayoutReadout(g, content, measure, {float(r.right - r.left) / scale, float(r.bottom - r.top) / scale});
        if (layout) best = std::max(best, layout->shown);
    }
    return best;
}

static bool TestTipRect(D2D1_RECT_F a, D2D1_RECT_F b) {
    return std::abs(a.left - b.left) < 1e-3f && std::abs(a.top - b.top) < 1e-3f &&
           std::abs(a.right - b.right) < 1e-3f && std::abs(a.bottom - b.bottom) < 1e-3f;
}

static const UiItem* TestTipItem(const std::vector<UiItem>& items, UiItemKind kind, size_t nth) {
    for (const UiItem& item : items) {
        if (item.kind == kind && nth-- == 0) return &item;
    }
    return nullptr;
}

static void RunTooltipTests() {
    Run("readout layout: measured widths; one column of one-line rows; the hint takes the last slot; "
        "nothing below one row or one column", [] {
        UiGraphics& g = TestGraphics();
        UiTooltipContent six = TestReadout(6);
        six.more = L" \u2014 select one account";
        UiTooltipMeasure m = UiMeasureReadout(g, six);
        float title = TestCaptionWidth(L"Go \u00B7 Gemini weekly"), detail = std::ceil(TestCaptionWidth(kTestMockDetail));
        CHECK_NEAR(m.title, title);
        CHECK_NEAR(m.detail, detail);
        float wide = 2 * 8 + 22 + title + 12 + detail;
        auto layout = UiLayoutReadout(g, six, m, {wide + 10, 2 * 8 + 6 * 16 + 5 * 4});
        CHECK(layout && !layout->narrow && layout->columns == 1 && layout->rows == 6 && layout->shown == 6 &&
              layout->more == 0);
        CHECK_NEAR(layout->title, title);
        CHECK_NEAR(layout->width, wide);
        CHECK_NEAR(layout->height, 132);
        // One DIP short of six rows: five slots, "+2 more — select one account" (one line) in the last.
        CHECK(TestCaptionWidth(L"+2 more \u2014 select one account") < title + 12 + detail);
        layout = UiLayoutReadout(g, six, m, {wide + 10, 131});
        CHECK(layout && layout->rows == 5 && layout->shown == 4 && layout->more == 2);
        CHECK_NEAR(layout->hint, 16);
        CHECK_NEAR(layout->height, 2 * 8 + 5 * 16 + 4 * 4);
        // Capacity 1: the hint alone. Capacity 0 (under one line, or no column): nothing.
        layout = UiLayoutReadout(g, TestReadout(3), m, {wide + 10, 32});
        CHECK(layout && layout->shown == 0 && layout->more == 3);
        CHECK(!UiLayoutReadout(g, six, m, {wide + 10, 31.9f}));
        CHECK(!UiLayoutReadout(g, six, m, {2 * 8 + 22 + std::max(64.0f, detail) - 0.5f, 500}));
        CHECK(!UiLayoutReadout(g, {}, m, {800, 600}));
    });

    Run("readout layout: the narrow form where only it fits (264 DIP rejects both, 376 takes two-line "
        "rows); one-line rows where they fit more; the mandatory part never shortened", [] {
        UiGraphics& g = TestGraphics();
        UiTooltipContent credits = TestReadout(3, L"OAI \u00B7 Credits remaining", kTestCreditsDetail);
        UiTooltipMeasure m = UiMeasureReadout(g, credits);
        // The plan's arithmetic holds for this text: narrow needs 16 + 22 + M, wide 16 + 22 + 64 + 12 + M.
        CHECK(m.detail > 264 - 38 && 38 + m.detail <= 376 && 38 + 64 + 12 + m.detail > 376);
        CHECK(!UiLayoutReadout(g, credits, m, {264, 590}));
        auto narrow = UiLayoutReadout(g, credits, m, {376, 150});
        CHECK(narrow && narrow->narrow && narrow->columns == 1 && narrow->shown == 3);
        CHECK_NEAR(narrow->height, 2 * 8 + 3 * 32 + 2 * 4);
        CHECK_NEAR(narrow->title, m.title);  // 338 DIP of room
        CHECK_NEAR(narrow->column, 22 + std::max(m.title, m.detail));
        // The floor's above/below region, 800 x 159: one-line rows hold 7 per column in two columns (14),
        // two-line ones 4 (8); ten rows take two balanced columns, the titles cut to what is left.
        UiTooltipContent ten = TestReadout(10, L"OAI \u00B7 Credits remaining", kTestCreditsDetail);
        auto wide = UiLayoutReadout(g, ten, m, {800, 159});
        CHECK(wide && !wide->narrow && wide->columns == 2 && wide->rows == 5 && wide->shown == 10);
        float room = (784.0f - 16) / 2 - 22 - 12 - m.detail;
        CHECK(room >= 64);
        CHECK_NEAR(wide->title, std::min(m.title, room));
        std::vector<UiItem> items = UiTooltipItems(ten, m, *wide);
        CHECK(items.size() == 30);
        for (size_t i = 0; i < 10; i++) {
            const UiItem* detail = TestTipItem(items, UiItemKind::Text, 2 * i + 1);
            CHECK(detail && !detail->trim && detail->rect.right - detail->rect.left >= m.detail - 0.01f);
            CHECK(detail->rect.right <= wide->width - 8 + 0.01f);
        }
    });

    Run("readout layout: widest titles among equal capacities; one-line rows over wider titles", [] {
        UiGraphics& g = TestGraphics();
        std::wstring longTitle = L"Research \u2014 shared Max seat for the whole team \u00B7 Fable weekly used (%)";
        UiTooltipContent three = TestReadout(3, longTitle);
        UiTooltipMeasure m = UiMeasureReadout(g, three);
        CHECK(m.title > 300);
        auto layout = UiLayoutReadout(g, three, m, {1100, 100});
        CHECK(layout && layout->columns == 1 && !layout->narrow);
        CHECK_NEAR(layout->title, m.title);
        // Room for a 70 DIP title on one line, or the whole title on two: one-line rows win.
        float width = 16 + 22 + 70 + 12 + m.detail;
        CHECK(width - 16 - 22 < m.title);
        layout = UiLayoutReadout(g, three, m, {width, 300});
        CHECK(layout && !layout->narrow && layout->shown == 3);
        CHECK_NEAR(layout->title, 70);
    });

    Run("readout overflow: columns first, then the hint measured on its own: one line, or wrapped "
        "over more slots at the end of the last column", [] {
        UiGraphics& g = TestGraphics();
        UiTooltipContent twenty = TestReadout(20);
        UiTooltipMeasure m = UiMeasureReadout(g, twenty);
        auto layout = UiLayoutReadout(g, twenty, m, {800, 159});
        CHECK(layout && !layout->narrow && layout->columns == 2 && layout->rows == 7);
        CHECK(TestCaptionWidth(std::wstring(L"+7 more") + kTestHint) < layout->column - 22);
        CHECK(layout->shown == 13 && layout->more == 7);
        CHECK_NEAR(layout->hint, 16);
        std::vector<UiItem> items = UiTooltipItems(twenty, m, *layout);
        CHECK(items.size() == 13 * 3 + 1);
        const UiItem& hint = items.back();
        CHECK(hint.kind == UiItemKind::Text && hint.ink == UiInk::Muted &&
              UiRunsText(hint.runs) == std::wstring(L"+7 more") + kTestHint);
        CHECK_NEAR(hint.rect.left, 8 + layout->column + 16 + 22);
        CHECK_NEAR(hint.rect.top, 8 + 6 * 20);
        // Short rows in narrow columns: the hint wraps and takes several slots before the rows count.
        UiTooltipContent tiny = TestReadout(20, L"A", L"[Current] 1%");
        m = UiMeasureReadout(g, tiny);
        layout = UiLayoutReadout(g, tiny, m, {400, 100});
        CHECK(layout && layout->rows == 4 && layout->more > 0 && layout->hint > 16);
        CHECK(layout->shown + layout->more == 20 && layout->shown <= layout->columns * layout->rows - 2);
        items = UiTooltipItems(tiny, m, *layout);
        const UiItem& wrapped = items.back();
        const UiItem& last = items[items.size() - 2];
        CHECK(wrapped.ink == UiInk::Muted && wrapped.rect.bottom <= layout->height - 8 + 0.01f);
        CHECK(wrapped.rect.top >= last.rect.bottom - 0.01f || wrapped.rect.left > last.rect.right);
    });

    Run("tooltip placement: centre, corners and edges of a small work area, a readout larger than every "
        "region, a negative origin, a cursor partly outside, a large image off its hotspot, 144 DPI; "
        "always inside and clear, the most rows any region allows", [] {
        UiGraphics& g = TestGraphics();
        UiTooltipContent six = TestReadout(6);
        UiTooltipMeasure m = UiMeasureReadout(g, six);
        auto place = [&](const UiTooltipContent& content, const UiTooltipKey& key) {
            auto spot = UiTooltipPlacement(g, content, UiMeasureReadout(g, content), key);
            CHECK(spot && TestClear(*spot, key));
            CHECK(spot->layout.shown == TestBestShown(content, key));
            return *spot;
        };
        // Six one-line rows need 132 DIP; at the centre no region is that tall (122 above), so two
        // columns of two-line rows hold them.
        RECT small{0, 0, 540, 260};
        for (POINT at : {POINT{270, 130}, POINT{2, 2}, POINT{537, 2}, POINT{2, 257}, POINT{537, 257}, POINT{270, 0},
                         POINT{0, 130}, POINT{539, 130}, POINT{270, 259}}) {
            UiTooltipSpot spot = place(six, TestKey(at, small));
            CHECK(spot.layout.shown == 6 && spot.layout.more == 0);
            if (at.x == 270 && at.y == 130) CHECK(spot.layout.narrow && spot.layout.columns == 2);
        }
        // Larger than every region: columns where they fit, then the hint.
        RECT wide{0, 0, 1200, 300};
        UiTooltipSpot over = place(TestReadout(60), TestKey({600, 150}, wide));
        CHECK(over.layout.shown < 60 && over.layout.more == 60 - over.layout.shown && over.layout.columns >= 2);
        // A monitor left of and above the primary one.
        RECT negative{-1920, -300, 0, 780};
        UiTooltipSpot left = place(six, TestKey({-40, 760}, negative));
        CHECK(left.rect.right <= 0 && left.rect.top >= -300);
        // The cursor image hanging off the work area's right and bottom; the hotspot outside its left.
        place(six, TestKey({530, 250}, small));
        place(six, TestKey({-6, 120}, small, 32, {0, 0}));
        // A 256 px image whose hotspot is 100 px in and 60 down: clear of all of it. Placed for a
        // 32 px image at the same point, the rectangle would cover part of it.
        RECT screen{0, 0, 1920, 1040};
        UiTooltipKey big = TestKey({600, 400}, screen, 256, {100, 60});
        place(six, big);
        auto plain = UiTooltipPlacement(g, six, m, TestKey({600, 400}, screen));
        CHECK(plain && !TestClear(*plain, big));
        // 144 DPI: a 48 px pointer, 12 px margin, the size in pixels rounded up.
        UiTooltipKey hidpi = TestKey({600, 440}, {0, 0, 1200, 885}, 48, {0, 0}, 144);
        UiTooltipSpot scaled = place(six, hidpi);
        CHECK(scaled.rect.right - scaled.rect.left == LONG(std::ceil(scaled.layout.width * 1.5f - 0.01f)));
        CHECK(scaled.rect.bottom - scaled.rect.top == LONG(std::ceil(scaled.layout.height * 1.5f - 0.01f)));
    });

    Run("tooltip placement: below first, then right, above and left (s-tooltips' four cases); aligned "
        "with the pointer; a text block whole or nowhere", [] {
        UiGraphics& g = TestGraphics();
        UiTooltipContent six = TestReadout(6);
        UiTooltipMeasure m = UiMeasureReadout(g, six);
        RECT small{0, 0, 540, 260};
        auto layout = UiLayoutReadout(g, six, m, {540, 260});
        CHECK(layout && layout->width <= 465 && layout->width > 120 && layout->height == 132);
        struct Case {
            POINT at;
            UiTooltipSide side;
        };
        for (Case c : {Case{{95, 40}, UiTooltipSide::Below}, Case{{35, 225}, UiTooltipSide::Right},
                       Case{{380, 225}, UiTooltipSide::Above}, Case{{500, 125}, UiTooltipSide::Left}}) {
            UiTooltipKey key = TestKey(c.at, small);
            auto spot = UiTooltipPlacement(g, six, m, key);
            CHECK(spot && spot->side == c.side && spot->layout.shown == 6 && TestClear(*spot, key));
            RECT ex = TestExclusion(key);
            if (c.side == UiTooltipSide::Below) CHECK(spot->rect.top == ex.bottom && spot->rect.left == c.at.x);
            if (c.side == UiTooltipSide::Right) CHECK(spot->rect.left == ex.right && spot->rect.bottom == 260);
            if (c.side == UiTooltipSide::Above) CHECK(spot->rect.bottom == ex.top && spot->rect.right == 540);
            if (c.side == UiTooltipSide::Left) CHECK(spot->rect.right == ex.left && spot->rect.top == c.at.y);
        }
        // Equal layouts above and below: below.
        auto center = UiTooltipPlacement(g, six, m, TestKey({960, 500}, {0, 0, 1920, 1040}));
        CHECK(center && center->side == UiTooltipSide::Below);
        // A text block: placed whole at its measured height, or nowhere.
        UiTooltipContent text;
        text.key = 2;
        text.text = {{L"CC 2 \u00B7 Fable weekly used (%)", UiRunStyle::Strong},
                     {L"\nSolid / filled: current week. Dashed / hollow: previous week.", UiRunStyle::Normal,
                      UiInk::Secondary}};
        auto block = UiTooltipPlacement(g, text, {}, TestKey({300, 100}, {0, 0, 1920, 1040}));
        float height = UiLayoutSize(UiRichLayout(g, text.text, UiFont::Caption, 420)).height;
        CHECK(block && block->side == UiTooltipSide::Below && height >= 32);
        CHECK_NEAR(block->layout.height, height + 8 + 9);
        CHECK(block->rect.bottom - block->rect.top == LONG(std::ceil(height + 17 - 0.01f)));
        CHECK(block->layout.width <= 420 + 20);
        CHECK(!UiTooltipPlacement(g, text, {}, TestKey({30, 20}, {0, 0, 60, 40})));
        std::vector<UiItem> items = UiTooltipItems(text, {}, block->layout);
        CHECK(items.size() == 1 && items[0].kind == UiItemKind::Text && !items[0].trim);
        CHECK_NEAR(items[0].rect.left, 10);
        CHECK_NEAR(items[0].rect.top, 8);
        CHECK_NEAR(items[0].rect.right - items[0].rect.left, block->layout.column);
    });

    Run("tooltip items: per row a swatch, the title cut at its width and the mandatory part beside it, "
        "or under it in two-line rows", [] {
        UiGraphics& g = TestGraphics();
        UiTooltipContent credits = TestReadout(3, L"OAI \u00B7 Credits remaining", kTestCreditsDetail);
        UiTooltipMeasure m = UiMeasureReadout(g, credits);
        auto narrow = UiLayoutReadout(g, credits, m, {376, 150});
        CHECK(narrow && narrow->narrow);
        std::vector<UiItem> items = UiTooltipItems(credits, m, *narrow);
        CHECK(items.size() == 9);
        CHECK(items[0].kind == UiItemKind::Shape && items[0].parts.size() == 1 && items[0].parts[0].stroke == 2 &&
              items[0].parts[0].ink == UiInk::Inherit);
        CHECK(TestTipRect(items[0].rect, {8, 8, 24, 24}));
        CHECK(items[1].trim && TestTipRect(items[1].rect, {30, 8, 30 + narrow->title, 24}));
        CHECK(!items[2].trim && TestTipRect(items[2].rect, {30, 24, 30 + m.detail, 40}));
        CHECK(TestTipRect(items[3].rect, {8, 8 + 36, 24, 8 + 36 + 16}));
        UiTooltipContent six = TestReadout(6);
        m = UiMeasureReadout(g, six);
        auto wide = UiLayoutReadout(g, six, m, {540, 260});
        items = UiTooltipItems(six, m, *wide);
        CHECK(TestTipRect(items[2].rect, {30 + wide->title + 12, 8, 30 + wide->title + 12 + m.detail, 24}));
        CHECK(UiRunsText(items[1].runs) == L"Go \u00B7 Gemini weekly");
    });

    Run("tooltip dedupe: the same content at the same pointer does nothing; a moved pointer or a new "
        "cursor image moves it clear without new rows; a new key takes new rows; nothing that fits hides",
        [] {
        UiGraphics& g = TestGraphics();
        UiTooltipModel model;
        RECT screen{0, 0, 1920, 1040};
        UiTooltipKey at = TestKey({400, 300}, screen);
        CHECK(UiTooltipUpdate(g, model, TestReadout(6), at) == UiTooltipChange::Layout);
        CHECK(model.spot && TestClear(*model.spot, at));
        RECT first = model.spot->rect;
        CHECK(UiTooltipUpdate(g, model, TestReadout(6), at) == UiTooltipChange::None);
        // The same key with other rows (as a client that kept its pick): the shown rows stay.
        UiTooltipKey moved = TestKey({460, 330}, screen);
        CHECK(UiTooltipUpdate(g, model, TestReadout(2, L"Other"), moved) == UiTooltipChange::Move);
        CHECK(model.content.rows.size() == 6 && TestClear(*model.spot, moved) && model.spot->rect.left != first.left);
        UiTooltipKey image = TestKey({460, 330}, screen, 128, {64, 64});
        CHECK(UiTooltipUpdate(g, model, {1}, image) != UiTooltipChange::None);
        CHECK(model.content.rows.size() == 6 && TestClear(*model.spot, image));
        CHECK(UiTooltipUpdate(g, model, {1}, image) == UiTooltipChange::None);
        CHECK(UiTooltipUpdate(g, model, TestReadout(2, L"Other", kTestMockDetail, 9), image) == UiTooltipChange::Layout);
        CHECK(model.content.rows.size() == 2 && model.spot->layout.shown == 2);
        // A work area too small for anything: hidden; room again: shown again.
        UiTooltipKey cramped = TestKey({20, 20}, {0, 0, 60, 60});
        CHECK(UiTooltipUpdate(g, model, {9}, cramped) == UiTooltipChange::Hide && !model.spot);
        CHECK(UiTooltipUpdate(g, model, {9}, cramped) == UiTooltipChange::None);
        CHECK(UiTooltipUpdate(g, model, {9}, image) == UiTooltipChange::Layout && model.spot);
        // Key 0: nothing to show.
        CHECK(UiTooltipUpdate(g, model, {}, image) == UiTooltipChange::Hide && !model.spot);
    });

    Run("chart: hot-tracked without an action and a tab stop whose arrows scroll; disabled it blocks; a "
        "custom item is decoration", [] {
        UiGraphics& g = TestGraphics();
        UiFrame frame;
        frame.viewport = {0, 0, 800, 600};
        frame.content.push_back(TestItem(10, UiItemKind::Card, {0, 0, 700, 420}, kTestOpen));
        frame.content.push_back(TestItem(11, UiItemKind::Chart, {20, 40, 680, 340}));
        frame.content.push_back(TestItem(12, UiItemKind::Custom, {20, 350, 680, 370}));
        frame.content.push_back(TestItem(13, UiItemKind::Button, {20, 380, 120, 410}, kTestOpen));
        CHECK(UiActive(frame.content[1]) && UiFocusable(frame.content[1]));
        CHECK(!UiActive(frame.content[2]) && !UiFocusable(frame.content[2]));
        CHECK(UiHitTest(g, frame.content, {100, 100}) == UiFocusRef{11});
        CHECK(UiHitTest(g, frame.content, {100, 360}) == UiFocusRef{10});  // Through the custom item
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == 3 && stops[1].ref == UiFocusRef{11} && stops[1].chart && !stops[0].chart);
        auto key = [&](UiFocusRef focus, UINT vk) {
            return UiKeyIntent(stops, focus, UiSurfaceId::Content, vk, false, 40, 300);
        };
        UiIntent up = key({11}, VK_UP), down = key({11}, VK_DOWN);
        CHECK(up.kind == UiIntentKind::ScrollBy && up.scroll == -40 && down.kind == UiIntentKind::ScrollBy &&
              down.scroll == 40);
        CHECK(key({11}, VK_NEXT).kind == UiIntentKind::ScrollBy && key({11}, VK_NEXT).scroll == 300);
        CHECK(key({11}, VK_PRIOR).scroll == -300);
        CHECK(key({11}, VK_HOME).kind == UiIntentKind::ScrollTo && key({11}, VK_HOME).scroll == 0);
        CHECK(key({11}, VK_END).kind == UiIntentKind::ScrollTo && key({11}, VK_END).scroll > 1e30f);
        CHECK(key({11}, VK_TAB).target == UiFocusRef{13});
        CHECK(key({10}, VK_DOWN).kind == UiIntentKind::Focus && key({10}, VK_DOWN).target == UiFocusRef{11});
        frame.content[1].enabled = false;
        CHECK(UiHitTest(g, frame.content, {100, 100}).id == 0);
        CHECK(UiFocusStops(frame).size() == 2);
    });
}
