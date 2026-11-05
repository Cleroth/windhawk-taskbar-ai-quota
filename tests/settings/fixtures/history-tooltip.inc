// The tooltip window's promises for the history window, measured with the production fonts and
// the production readout rows (history/chart.h HistoryChartRow, on the formats of history/view.h):
// the single-account readout guarantee on the smallest supported work area (800 x 590 DIP, the
// window's minimum size) with the default and the largest assumed pointer (32 and 256 DIP) at
// every position, and complete-text tooltips placed whole there. OBSERVE lines report the
// measurements. Then the render sheet of readout rows, forms, placements and text tooltips
// (mockups s-tooltips, q6-rows, q6a-tooltip).

// A readout row's mandatory part as the production row prints it (period 0 current, 1 previous;
// a time in the zone of TestZone, `now` deciding its year form) in `locale`'s language.
static std::vector<UiRun> TestReadoutDetail(int meter, int period, double value, ULONGLONG time, ULONGLONG now = kWeekTo,
                                            const I18nLocale& locale = {}) {
    return HistoryChartRow(locale, L"", {}, 2, meter, period, value, time, now, TestZone()).detail;
}

static float TestRunsWidth(const std::vector<UiRun>& runs) {
    return UiLayoutSize(UiRichLayout(TestGraphics(), runs, UiFont::Caption, 100000)).width;
}

static std::string TestAscii(const std::wstring& text) {
    std::string out;
    for (wchar_t c : text) out += c == L'\u00B7' ? '.' : c < 0x80 ? char(c) : '?';
    return out;
}

static std::string TestUtf8(const std::wstring& text) {
    std::string out(text.size() * 3, '\0');
    out.resize(size_t(std::max(WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(),
                                                   int(out.size()), nullptr, nullptr), 0)));
    return out;
}

// One Anthropic account in All meters while comparing: its five series in both periods (by their
// chip names), the most rows one account's readout has. Every row carries `detail`.
static UiTooltipContent TestAccountReadout(const std::vector<UiRun>& detail, int count = 10) {
    static constexpr PCWSTR kTitles[] = {L"5h", L"Weekly", L"Fable weekly", L"Extra budget", L"Extra spending (USD)"};
    UiTooltipContent content;
    content.key = uint64_t(count);
    content.more = [](int more) { return L"+" + std::to_wstring(more) + L" more \u2014 hide lines in the legend"; };
    for (int i = 0; i < count; i++) content.rows.push_back({kTitles[size_t(i / 2) % 5], detail, UiRgb(0xA184FF), 2});
    return content;
}

// A graphics' locale for a scope, as a window's client sets it for its frames.
struct TestLocaleScope {
    UiGraphics& g;
    std::wstring previous;
    TestLocaleScope(UiGraphics& g, const I18nLocale& locale)
        : g(g), previous(std::exchange(g.locale, std::wstring(locale.LocaleName()))) {}
    ~TestLocaleScope() { g.locale = previous; }
};

// The single-account readout guarantee in a language (`name`: none for English), measured with
// its locale's text formats: its widest mandatory part, then ten such rows placed everywhere. The
// width bound, the time forms and the ten rows are English's promises; other languages report
// their widths and the fewest rows placed, and are checked to never cover the pointer.
static void RunReadoutGuarantee(const I18nLocale& locale, const std::string& name) {
    const bool english = locale == I18nLocale();
    std::string suffix = english ? "" : " (" + name + ")";
    std::vector<UiRun> widest;
    Run("readout guarantee" + suffix + ": the widest mandatory parts the production rows print (both periods, each "
        "unit's rounded and abbreviated values, times in and outside the current year on every day and month)", [&] {
        TestLocaleScope scope(TestGraphics(), locale);
        // Segoe UI digits are tabular: 0, 8 and 9 stand for every digit.
        HistoryUiLocalTime zone = TestZone();
        struct Time {
            float width;
            std::wstring text;
            ULONGLONG ms, now;
        };
        std::vector<Time> times;
        for (ULONGLONG now : {TestLocalMs(2026, 12, 31, 23, 59), TestLocalMs(2027, 1, 15, 12, 0)}) {
            for (WORD month = 1; month <= 12; month++) {
                for (WORD day = 1; day <= 28; day++) {
                    for (auto [hour, minute] : {std::pair<WORD, WORD>{0, 0}, {8, 8}, {18, 48}, {23, 59}}) {
                        ULONGLONG ms = TestLocalMs(2026, month, day, hour, minute);
                        std::wstring time = HistoryUiTime(locale, ms, now, zone);
                        times.push_back({TestRunsWidth({{time}}), time, ms, now});
                    }
                }
            }
        }
        std::sort(times.begin(), times.end(), [](const Time& a, const Time& b) { return a.width > b.width; });
        // English: the widest is a previous year's, without a weekday: `25 May 2026 23:59`.
        if (english) {
            CHECK(times[0].text.size() == 17 && times[0].text.find(L" 2026 ") == 6 && std::iswdigit(times[0].text[0]));
            auto current = std::find_if(times.begin(), times.end(), [](const Time& t) { return !std::iswdigit(t.text[0]); });
            CHECK(current != times.end());
            std::cout << "OBSERVE readout widest current-year time: " << current->width << " DIP '"
                      << TestAscii(current->text) << "'\n";
        }
        std::cout << "OBSERVE readout widest time" << suffix << ": " << times[0].width << " DIP '"
                  << TestUtf8(times[0].text) << "'\n";
        const double values[] = {0,       9.95,     38.8,      99.99,      100,         999.95,       8888.88,
                                 9999.94, 9999.996, 10000,     88888.8,    99949,       999949,       9999949,
                                 99999949, 999999949, 9999999949, 99999999949, 888888888888, 999949999999, 1e12};
        struct Unit {
            const char* name;
            int meter;
        };
        float overall = 0;
        for (Unit unit : {Unit{"percent", 0}, Unit{"USD", 6}, Unit{"credits", 7}}) {
            std::vector<std::pair<float, double>> printed;
            for (double value : values) printed.push_back({TestRunsWidth({{HistoryUiValue(unit.meter, value, 1)}}), value});
            std::sort(printed.rbegin(), printed.rend());
            float best = 0;
            std::vector<UiRun> bestRuns;
            for (int period : {0, 1}) {
                for (size_t i = 0; i < 3; i++) {
                    for (size_t j = 0; j < 3; j++) {
                        std::vector<UiRun> runs =
                            TestReadoutDetail(unit.meter, period, printed[i].second, times[j].ms, times[j].now, locale);
                        if (float width = TestRunsWidth(runs); width > best) {
                            best = width;
                            bestRuns = runs;
                        }
                    }
                }
            }
            std::cout << "OBSERVE readout widest mandatory part" << suffix << ", " << unit.name << ": " << best
                      << " DIP '" << TestUtf8(UiRunsText(bestRuns)) << "'\n";
            if (best > overall) {
                overall = best;
                widest = bestRuns;
            }
        }
        // The plan's arithmetic took 285 DIP (two one-line columns of 64 DIP titles at the floor).
        CHECK(overall > 200 && (!english || overall <= 285));
    });

    Run("readout guarantee" + suffix + ": one account's ten rows of the widest mandatory part all placed, clear "
        "of the pointer, at every pointer position on an 800 x 590 DIP work area, with a 32 and a 256 DIP pointer "
        "(hotspot at its corner, middle and far corner), at 96 and 144 DPI", [&] {
        UiGraphics& g = TestGraphics();
        TestLocaleScope scope(g, locale);
        CHECK(!widest.empty());
        UiTooltipContent account = TestAccountReadout(widest), many = TestAccountReadout(widest, 200);
        UiTooltipMeasure measure = UiMeasureReadout(g, account), manyMeasure = UiMeasureReadout(g, many);
        int failures = 0, covering = 0;
        for (UINT dpi : {96u, 144u}) {
            float scale = float(dpi) / 96;
            RECT work{0, 0, LONG(std::lround(800 * scale)), LONG(std::lround(590 * scale))};
            LONG step = LONG(std::lround(8 * scale));
            for (int pointer : {32, 256}) {
                LONG size = LONG(std::lround(float(pointer) * scale));
                int fewest = INT_MAX, smallest = INT_MAX;
                std::string capacityAt, fewestAt;
                auto record = [&](const UiTooltipKey& key, bool capacity) {
                    UiTrimCaches(g);
                    auto spot = UiTooltipPlacement(g, account, measure, key);
                    covering += spot && !TestClear(*spot, key);
                    int shown = spot && TestClear(*spot, key) ? spot->layout.shown : 0;
                    if (shown < fewest) {
                        fewest = shown;
                        fewestAt = std::to_string(key.cursor.x) + "," + std::to_string(key.cursor.y);
                    }
                    if (shown < 10 && failures++ < 8) {
                        std::cout << "OBSERVE guarantee" << suffix << " failure at " << dpi << " DPI, " << pointer
                                  << " DIP pointer, " << key.cursor.x << "," << key.cursor.y << ": " << shown << " rows\n";
                    }
                    if (!capacity) return;
                    auto full = UiTooltipPlacement(g, many, manyMeasure, key);
                    int rows = full ? full->layout.columns * full->layout.rows : 0;
                    if (rows < smallest) {
                        smallest = rows;
                        capacityAt = std::to_string(key.cursor.x) + "," + std::to_string(key.cursor.y) + " (" +
                                     (full ? std::to_string(full->layout.columns) + " x " +
                                                 std::to_string(full->layout.rows) +
                                                 (full->layout.narrow ? " two-line" : " one-line") + ", side " +
                                                 std::to_string(int(full->side)) : std::string("none")) + ")";
                    }
                };
                for (POINT hotspot : {POINT{0, 0}, POINT{size / 2, size / 2}, POINT{size - 1, size - 1}}) {
                    for (LONG y = work.top; y < work.bottom; y += step) {
                        for (LONG x = work.left; x < work.right; x += step) {
                            record(TestKey({x, y}, work, size, hotspot, dpi), (x / step + y / step) % 3 == 0);
                        }
                    }
                }
                // The balanced position: the exclusion rectangle in the work area's middle.
                record(TestKey({work.right / 2, work.bottom / 2}, work, size, {size / 2, size / 2}, dpi), true);
                std::cout << "OBSERVE guarantee" << suffix << " " << dpi << " DPI, " << pointer
                          << " DIP pointer: fewest rows placed "
                          << fewest << " of 10 (at " << fewestAt << "); smallest capacity " << smallest << " at "
                          << capacityAt << "\n";
            }
        }
        CHECK((!english || failures == 0) && covering == 0);
    });
}

static void RunHistoryTooltipTests() {
    RunReadoutGuarantee({}, "");
    RunReadoutGuarantee(I18nLocale(*I18nLanguageByTag(L"zh-Hans")), "zh-Hans");
    RunReadoutGuarantee(I18nLocale(*I18nLanguageByTag(L"qps-ploc")), "qps-ploc");

    Run("complete-text tooltips: the Export description and a long chip title with its explanation, "
        "measured at their full height, placed whole and clear at every pointer position at the floor "
        "with the 256 DIP pointer", [] {
        UiGraphics& g = TestGraphics();
        UiFrame frame = TestHistoryFrame(TestHistoryView());
        const UiItem* exportButton = UiFindItem(frame, kHistoryUiExport);
        CHECK(exportButton && exportButton->tooltip.size() == 2);
        UiTooltipContent exportTip, chip;
        exportTip.key = 3;
        exportTip.text = exportButton->tooltip;
        chip.key = 4;
        chip.text = {{L"Research \u2014 shared Max seat for the whole team \u00B7 Fable weekly used (%)", UiRunStyle::Strong},
                     {L"\nSolid / filled: current week. Dashed / hollow: previous week. Click to hide or show this line.",
                      UiRunStyle::Normal, UiInk::Secondary}};
        int failures = 0;
        for (const UiTooltipContent* tip : {&exportTip, &chip}) {
            D2D1_SIZE_F size = UiLayoutSize(UiRichLayout(g, tip->text, UiFont::Caption, 420));
            std::cout << "OBSERVE text tooltip " << tip->key << ": " << size.width << " x " << size.height
                      << " DIP at 420 DIP\n";
            for (UINT dpi : {96u, 144u}) {
                float scale = float(dpi) / 96;
                RECT work{0, 0, LONG(std::lround(800 * scale)), LONG(std::lround(590 * scale))};
                LONG step = LONG(std::lround(10 * scale)), pointer = LONG(std::lround(256 * scale));
                for (POINT hotspot : {POINT{0, 0}, POINT{pointer / 2, pointer / 2}}) {
                    for (LONG y = work.top; y < work.bottom; y += step) {
                        for (LONG x = work.left; x < work.right; x += step) {
                            UiTrimCaches(g);
                            UiTooltipKey key = TestKey({x, y}, work, pointer, hotspot, dpi);
                            auto spot = UiTooltipPlacement(g, *tip, {}, key);
                            if (!spot || !TestClear(*spot, key) || spot->layout.column > 420) failures++;
                        }
                    }
                }
            }
        }
        CHECK(failures == 0);
    });
}

// Render sheet ---------------------------------------------------------------------------------

// The arrow pointer of the mockups (a 32 DIP image, hotspot at its top-left).
static constexpr PCWSTR kTestArrowPath = L"M.5,.5V17.6L4.6,13.7L7.6,20.6L10.4,19.4L7.5,12.6H13.3Z";

// In `locale`'s language: its times and hint (the rest of the readout follows later phases), with
// its text formats; the expected English text is checked only in English.
static void RenderTooltipSheet(UiGraphics& g, IWICImagingFactory* wic, const std::wstring& path,
                               const UiThemeInput& theme, UINT dpi, const I18nLocale& locale = {}) {
    constexpr float kWidth = 1180, kHeight = 1740;
    const bool english = locale == I18nLocale();
    TestLocaleScope scope(g, locale);
    float scale = float(dpi) / 96;
    winrt::com_ptr<IWICBitmap> bitmap = RenderBitmap(wic, {kWidth, kHeight}, dpi);
    winrt::com_ptr<ID2D1RenderTarget> target = RenderTarget(g, bitmap.get(), dpi);
    UiTargetCache cache;
    UiPalette palette = UiPaletteFor(theme);
    const bool hc = theme.kind == UiThemeKind::HighContrast, light = theme.kind == UiThemeKind::Light;
    target->BeginDraw();
    target->Clear(palette.background);
    D2D1_COLOR_F muted = palette.textMuted, faint = UiFade(palette.text, hc ? 1.0f : 0.25f);
    auto text = [&](const std::wstring& line, UiFont font, float x, float y, D2D1_COLOR_F color) {
        UiDrawLayout(target.get(), cache, UiLayout(g, line, UiTextStyleFor(font), 1100, false), {x, y}, color);
    };
    // A tooltip as its window shows it: the popup surface, Windows' rounded border, the items.
    auto tooltip = [&](const UiTooltipContent& content, const UiTooltipMeasure& measure, const UiTooltipLayout& layout,
                       D2D1_POINT_2F at, D2D1_SIZE_F size) {
        D2D1_RECT_F box{at.x, at.y, at.x + size.width, at.y + size.height};
        UiFillRect(target.get(), cache, box, 4, palette.popup, scale);
        UiStrokeRect(target.get(), cache, box, 4, hc ? palette.cardBorder : palette.controlBorder, 1, scale);
        UiPaintItems(target.get(), g, cache, palette, UiTooltipItems(content, measure, layout), {}, at, scale, -1e9f,
                     1e9f);
    };
    auto outline = [&](D2D1_RECT_F rect) { UiStrokeRect(target.get(), cache, rect, 0, faint, 1, scale); };
    // Line colors as the chart would give them: three hues, or text and link in high contrast.
    const uint32_t hues[3] = {light ? 0x0E8F78u : 0x33C3A6u, light ? 0x6F4FE6u : 0xA184FFu, light ? 0x2B6FD6u : 0x5B9BFFu};
    auto color = [&](size_t series) {
        return hc ? (series % 2 ? palette.link : palette.text) : UiRgb(hues[series % 3]);
    };
    // Production rows (HistoryChartRow) of a meter's value at a local time (Oct 2026, now kWeekTo).
    auto row = [&](std::wstring title, size_t series, int meter, int period, double value, WORD day, WORD hour,
                   WORD minute) {
        return HistoryChartRow(locale, std::move(title), color(series), 2, meter, period, value,
                               TestLocalMs(2026, 10, day, hour, minute), kWeekTo, TestZone());
    };
    // The mockups' six coincident rows: three accounts' weekly lines in both periods.
    UiTooltipContent six;
    six.key = 1;
    six.more = [locale](int more) {
        return I18nText(locale, TrCtx(L"chart-readout", L"+{count} more \u2014 hide lines in the legend or select one account"),
                        {{L"count", more}});
    };
    six.rows = {row(L"OAI \u00B7 Weekly", 0, 1, 0, 38.0, 27, 12, 31), row(L"OAI \u00B7 Weekly", 0, 1, 1, 37.6, 20, 12, 32),
                row(L"CC 2 \u00B7 Weekly", 1, 1, 0, 38.3, 27, 12, 32), row(L"CC 2 \u00B7 Weekly", 1, 1, 1, 38.8, 20, 12, 31),
                row(L"Go \u00B7 Gemini weekly", 2, 1, 0, 37.4, 27, 12, 31),
                row(L"Go \u00B7 Gemini weekly", 2, 1, 1, 38.1, 20, 12, 31)};
    CHECK(!english || UiRunsText(six.rows[1].detail) == L"[Previous week] 37.6% \u00B7 Tue 20 Oct 12:32");
    UiTooltipMeasure sixMeasure = UiMeasureReadout(g, six);

    // Placement on each side of the pointer in a 540 x 260 DIP work area (s-tooltips): the
    // exclusion rectangle outlined, the tooltip where the production placement puts it.
    text(L"Readout placement: the same six rows beside the pointer, never over its image and 8 DIP around it",
         UiFont::BodyStrong, 16, 12, palette.text);
    struct Area {
        PCWSTR caption;
        float x, y;
        POINT at;
    };
    const Area areas[] = {{L"Below (first choice): room below", 16, 60, {95, 40}},
                          {L"Right: the pointer near the bottom edge", 600, 60, {35, 225}},
                          {L"Above: near the bottom-right corner", 16, 370, {380, 225}},
                          {L"Left: near the right edge; above and below hold fewer rows", 600, 370, {500, 125}}};
    for (const Area& area : areas) {
        text(area.caption, UiFont::Caption, area.x, area.y - 20, muted);
        D2D1_RECT_F box{area.x, area.y, area.x + 540, area.y + 260};
        UiFillRect(target.get(), cache, box, 0, UiFade(palette.text, hc ? 0 : 0.03f), scale);
        outline(box);
        auto px = [&](float dip) { return LONG(std::lround(dip * scale)); };
        RECT work{px(box.left), px(box.top), px(box.right), px(box.bottom)};
        POINT cursor{px(box.left + float(area.at.x)), px(box.top + float(area.at.y))};
        UiTooltipKey key = TestKey(cursor, work, px(32), {0, 0}, dpi);
        RECT ex = TestExclusion(key);
        outline({float(ex.left) / scale, float(ex.top) / scale, float(ex.right) / scale, float(ex.bottom) / scale});
        auto spot = UiTooltipPlacement(g, six, sixMeasure, key);
        CHECK(spot && TestClear(*spot, key));
        tooltip(six, sixMeasure, spot->layout, {float(spot->rect.left) / scale, float(spot->rect.top) / scale},
                {float(spot->rect.right - spot->rect.left) / scale, float(spot->rect.bottom - spot->rect.top) / scale});
        UiItem arrow;
        arrow.kind = UiItemKind::Shape;
        arrow.viewBox = 32;
        float x = float(cursor.x) / scale, y = float(cursor.y) / scale;
        arrow.rect = {x, y, x + 32, y + 32};
        arrow.parts = {{kTestArrowPath, UiRgb(0xFFFFFF)}, {kTestArrowPath, UiRgb(0x000000), UiInk::Inherit, 1, 1}};
        UiPaintItems(target.get(), g, cache, palette, {arrow}, {}, {0, 0}, scale, -1e9f, 1e9f);
    }

    // Row forms in regions (q6-rows): the production layout drawn at the region's top-left.
    float y = 680;
    text(L"Readout rows: one-line and two-line forms, a shortened name, columns, the \"+N more\" hint",
         UiFont::BodyStrong, 16, y, palette.text);
    y += 36;
    auto region = [&](const std::wstring& caption, const UiTooltipContent& content, D2D1_SIZE_F size) {
        text(caption, UiFont::Caption, 16, y, muted);
        y += 20;
        outline({16, y, 16 + size.width, y + size.height});
        UiTooltipMeasure measure = UiMeasureReadout(g, content);
        auto layout = UiLayoutReadout(g, content, measure, size);
        CHECK(layout);
        tooltip(content, measure, *layout, {16, y}, {layout->width, layout->height});
        y += size.height + 18;
        return *layout;
    };
    UiTooltipContent one;
    one.key = 2;
    one.rows = {row(L"CC 2 \u00B7 Weekly", 1, 1, 0, 93.0, 31, 14, 31)};
    UiTooltipLayout oneLine = region(L"One-line row: swatch, name, then the never-cut part (520 x 60 region)", one, {520, 60});
    CHECK(!english || !oneLine.narrow);
    UiTooltipContent credits;
    credits.key = 3;
    credits.rows = {row(L"OAI \u00B7 Credits remaining", 0, 7, 0, 1240, 31, 14, 30),
                    row(L"OAI \u00B7 Credits remaining", 0, 7, 1, 1987.5, 24, 14, 29),
                    row(L"OAI \u00B7 Weekly", 1, 1, 1, 41.7, 24, 14, 29)};
    float narrowWidth = std::ceil(2 * 8 + 22 + std::max(64.0f, UiMeasureReadout(g, credits).detail) + 30);
    CHECK(region(L"Two-line rows: a side region too narrow for one line (" + std::to_wstring(int(narrowWidth)) +
                     L" x 150): the never-cut part under the name",
                 credits, {narrowWidth, 150}).narrow);
    UiTooltipContent cut;
    cut.key = 4;
    cut.rows = {row(L"Research \u2014 shared Max seat \u00B7 Weekly", 2, 1, 0, 44.2, 27, 12, 30),
                row(L"CC 2 \u00B7 Weekly", 1, 1, 1, 44.8, 20, 12, 33)};
    region(L"Shortened name: only the name is cut, never the period, value or time (440 x 60)", cut, {440, 60});
    // The production formats' times: a previous year's (the year in the weekday's place) and the
    // widest mandatory part the readout guarantee is measured with.
    UiTooltipContent years;
    years.key = 7;
    ULONGLONG now = TestLocalMs(2027, 1, 15, 12, 0);
    years.rows = {HistoryChartRow(locale, L"OAI \u00B7 Credits remaining", color(0), 2, 7, 1, 9999.94,
                                  TestLocalMs(2026, 5, 25, 23, 59), now, TestZone()),
                  HistoryChartRow(locale, L"OAI \u00B7 Weekly", color(1), 2, 5, 0, 41.7, TestLocalMs(2027, 1, 12, 9, 5), now,
                                  TestZone())};
    CHECK(!english || (UiRunsText(years.rows[0].detail).ends_with(L"25 May 2026 23:59") &&
                       UiRunsText(years.rows[1].detail).ends_with(L"Tue 12 Jan 09:05")));
    region(L"Times: a previous year's in the year form (the guarantee's widest mandatory part), this year's with "
           L"the weekday (520 x 60)", years, {520, 60});
    UiTooltipContent account =
        TestAccountReadout(TestReadoutDetail(1, 1, 61.4, TestLocalMs(2026, 10, 24, 14, 30), kWeekTo, locale));
    for (size_t i = 0; i < account.rows.size(); i++) {
        account.rows[i].color = color(i / 2);
        account.rows[i].detail = TestReadoutDetail(1, int(i % 2), 30.0 + 7 * double(i),
                                                   i % 2 ? TestLocalMs(2026, 10, 24, 14, 30) : TestLocalMs(2026, 10, 31, 14, 31),
                                                   kWeekTo, locale);
    }
    UiTooltipLayout columns =
        region(L"Two columns: one account comparing (its ten rows) in the floor's 800 x 159 region", account, {800, 159});
    CHECK(!english || columns.columns == 2);
    UiTooltipContent twenty = six;
    twenty.key = 5;
    for (int i = 0; i < 14; i++) twenty.rows.push_back(six.rows[size_t(i) % 6]);
    UiTooltipLayout more =
        region(L"\"+N more\": twenty coincident rows in the same region, the hint in the last slot", twenty, {800, 159});
    CHECK(!english || more.more == 7);

    // Complete-text tooltips (s-tooltips): a chip's full name and line styles, the icon buttons'.
    text(L"Complete-text tooltips: wrapped at 420 DIP at most, never cut", UiFont::BodyStrong, 16, y, palette.text);
    y += 36;
    UiTooltipContent chip;
    chip.key = 6;
    chip.text = {{L"CC 2 \u00B7 Fable weekly used (%)", UiRunStyle::Strong},
                 {L"\nSolid / filled: current week. Dashed / hollow: previous week. Click to hide or show this line.",
                  UiRunStyle::Normal, UiInk::Secondary}};
    HistoryUiView view = TestHistoryView(theme.kind);
    view.locale = locale;
    UiFrame frame = TestHistoryFrame(view);
    std::vector<UiTooltipContent> texts = {chip};
    for (uint32_t id : {kHistoryUiRefresh, kHistoryUiExport, kHistoryUiMore}) {
        UiTooltipContent tip;
        tip.key = 10 + id;
        tip.text = UiFindItem(frame, id)->tooltip;
        texts.push_back(std::move(tip));
    }
    float x = 16, rowBottom = y;
    for (const UiTooltipContent& tip : texts) {
        auto px = [&](float dip) { return LONG(std::lround(dip * scale)); };
        UiTooltipKey key = TestKey({px(x), px(y) - px(60)}, {px(x), px(y), px(kWidth - 16), px(kHeight - 16)}, px(32),
                                   {0, 0}, dpi);
        auto spot = UiTooltipPlacement(g, tip, {}, key);
        CHECK(spot);
        D2D1_SIZE_F size{float(spot->rect.right - spot->rect.left) / scale,
                         float(spot->rect.bottom - spot->rect.top) / scale};
        tooltip(tip, {}, spot->layout, {float(spot->rect.left) / scale, float(spot->rect.top) / scale}, size);
        x += size.width + 24;
        rowBottom = std::max(rowBottom, y + size.height);
    }
    CHECK(rowBottom + 16 <= kHeight);

    winrt::check_hresult(target->EndDraw());
    cache.Clear();
    SaveRender(wic, bitmap.get(), path);
    std::cout << "RENDER " << TestAscii(path) << '\n';
}

static void RunHistoryTooltipRenders(const std::wstring& directory) {
    UiGraphics g;
    UiCreateGraphics(g);
    winrt::com_ptr<IWICImagingFactory> wic;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          __uuidof(IWICImagingFactory), wic.put_void()));
    for (const RenderTheme& theme : RenderThemes()) {
        for (UINT dpi : {96u, 144u, 192u}) {
            std::string name = std::string("render-history-tooltips-") + theme.name + "-" + std::to_string(dpi) + ".png";
            Run(name, [&] {
                RenderTooltipSheet(g, wic.get(), directory + L"\\" + std::wstring(name.begin(), name.end()), theme.input,
                                   dpi);
            });
        }
    }
    // The localization review's languages, light at 96 and 144 DPI.
    for (std::string_view tag : {"zh-Hans", "qps-ploc"}) {
        std::wstring wide(tag.begin(), tag.end());
        for (UINT dpi : {96u, 144u}) {
            std::string name =
                "render-history-tooltips-" + std::string(tag) + "-light-" + std::to_string(dpi) + ".png";
            Run(name, [&] {
                RenderTooltipSheet(g, wic.get(), directory + L"\\" + std::wstring(name.begin(), name.end()),
                                   RenderThemes()[0].input, dpi, I18nLocale(*I18nLanguageByTag(wide)));
            });
        }
    }
}
