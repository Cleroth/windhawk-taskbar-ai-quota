// Localization of the taskbar's meter model and its coded errors on the production catalogs: the
// quota keys and the headline countdown (its forms and its width reserve) in Simplified Chinese,
// Russian and the pseudo-locale, the settings preview strip drawing them, measured taskbar text
// (the bar key column, sparkline text placement, the gauge key's fit, and in the preview strip in
// every language keys inside their column, matrix numbers past their key and the countdown's
// width holding every form), an account error's text per language with its English log
// rendering, and the Accounts page's state for the coded error; the details flyout's extra quota
// names (plural forms around the reported name) with the extra quotas' merging, the order of a
// number and its unit, the visual test samples' text, and the flyout's plural and nested templates
// (free resets, the outage line, a reset line, a retry, a Copilot amount). Expected text is
// written out by hand from the production catalogs and the pseudo-locale's rules; widths from
// synthetic measures (TestMeasure, TestWideMeasure) unless they come from DirectWrite.

// Text widths with wide scripts: CJK characters (from U+2E80) an em, others 0.6 of the font size,
// in any font.
static double TestWideMeasure(std::wstring_view text, TileFont, double size) {
    double ems = 0;
    for (wchar_t c : text) ems += c >= 0x2E80 ? 1 : 0.6;
    return ems * size;
}

// Every account's keys, slot by slot: an Anthropic account's four, then Antigravity's Argon weekly,
// OpenAI's credits, GitHub Copilot's and OpenCode Go's monthly.
static std::vector<std::wstring> TestQuotaKeys(const I18nLocale& locale) {
    return {QuotaKey(locale, L"anthropic", kFiveHourBar),     QuotaKey(locale, L"anthropic", kWeeklyBar),
            QuotaKey(locale, L"anthropic", kFableWeeklyBar),  QuotaKey(locale, L"anthropic", kExtraUsageBar),
            QuotaKey(locale, L"antigravity", kFableWeeklyBar), QuotaKey(locale, L"openai", kExtraUsageBar),
            QuotaKey(locale, L"copilot", kExtraUsageBar),     QuotaKey(locale, L"opencode", kExtraUsageBar)};
}

// The headline countdown of a quota at 50% in a week-long window resetting `resetIn` after kNow (0:
// without a reset time), in `locale`.
static std::wstring TestCountdown(const I18nLocale& locale, long long resetIn) {
    Settings settings;
    settings.headlineCountdown = true;
    MeterDisplay display = MeterDisplayFor(settings, locale, TestMeasure, kNow, 1, 48);
    ULONGLONG reset = resetIn ? ULONGLONG((long long)kNow + resetIn) : 0;
    return First({50, reset, reset ? kWeeklyWindowMs : 0}, display, BarLayout::LogoHeadline).headlineCountdown;
}

static void RunI18nTaskbarTests() {
    const ULONGLONG hour = 60 * kMinute, day = 24 * hour;

    Run("quota keys: English as before; Chinese, Russian and the pseudo-locale from their catalogs, the "
        "brand-derived keys kept", [] {
        CHECK((TestQuotaKeys({}) == std::vector<std::wstring>{L"5h", L"7d", L"Fa", L"Ex", L"Ar", L"Cr", L"Mo", L"Mo"}));
        // 5h 7天 Fa 额外 Ar 积分 月 月
        CHECK((TestQuotaKeys(TestLocale(L"zh-Hans")) ==
               std::vector<std::wstring>{L"5h", L"7\u5929", L"Fa", L"\u989D\u5916", L"Ar", L"\u79EF\u5206", L"\u6708",
                                         L"\u6708"}));
        // 5ч 7д Fa Ex Ar Кр Мс Мс
        CHECK((TestQuotaKeys(TestLocale(L"ru")) ==
               std::vector<std::wstring>{L"5\u0447", L"7\u0434", L"Fa", L"Ex", L"Ar", L"\u041A\u0440", L"\u041C\u0441",
                                         L"\u041C\u0441"}));
        // Accented, bracketed and padded by one: [5ĥ ] [7ð ] [Ƒå ] [Éẋ ] [Åŕ ] [Çŕ ] [Ṁö ]
        CHECK((TestQuotaKeys(TestLocale(L"qps-ploc")) ==
               std::vector<std::wstring>{L"[5\u0125 ]", L"[7\u00F0 ]", L"[\u0191\u00E5 ]", L"[\u00C9\u1E8B ]",
                                         L"[\u00C5\u0155 ]", L"[\u00C7\u0155 ]", L"[\u1E40\u00F6 ]", L"[\u1E40\u00F6 ]"}));
        // The reserve list holds each key once, in QuotaKey's order.
        std::vector<std::wstring> reserved;
        for (I18nKey key : kQuotaKeys) reserved.push_back(I18nText(TestLocale(L"zh-Hans"), key));
        CHECK((reserved == std::vector<std::wstring>{L"5h", L"7\u5929", L"Fa", L"\u989D\u5916", L"Ar", L"\u79EF\u5206",
                                                     L"\u6708"}));
    });

    Run("quota keys reach the meters in the display's language: bar keys, the headline's, the matrix's and "
        "the weekly chart's extra usage key", [] {
        AccountData data = Fresh({30, 80, 10, 20});
        data.winWeek = {80, kNow + 3 * 24 * 60 * kMinute, kWeeklyWindowMs};
        Settings settings;
        MeterDisplay zh = MeterDisplayFor(settings, TestLocale(L"zh-Hans"), TestMeasure, kNow, 1, 48);
        AccountMeters bars = AccountMeterState(zh, BarLayout::Stacked, Account(L"openai"), data, 0, 0);
        CHECK(bars.quotas[0].key == L"5h" && bars.quotas[1].key == L"7\u5929" && bars.quotas[3].key == L"\u79EF\u5206");
        AccountMeters headline = AccountMeterState(zh, BarLayout::LogoHeadline, Account(L"openai"), data, 0, 0);
        CHECK(headline.headline == L"80%" && headline.headlineKey == L"7\u5929");
        MeterDisplay ru = MeterDisplayFor(settings, TestLocale(L"ru"), TestMeasure, kNow, 1, 48);
        AccountMeters matrix = AccountMeterState(ru, BarLayout::QuotaMatrix, Account(L"copilot"), data, 0, 0);
        CHECK(matrix.quotas[0].key == L"5\u0447" && matrix.quotas[3].key == L"\u041C\u0441");
        // OpenAI's credits in the weekly chart's corner: their key ("Cr").
        data.hasCredits = true;
        data.creditsBalance = 400;
        data.extraUsage = {};
        Settings spark;
        spark.sparklineExtra = SparklineExtra::Amount;
        MeterDisplay pseudo = MeterDisplayFor(spark, TestLocale(L"qps-ploc"), TestMeasure, kNow, 1, 48);
        AccountMeters chart = AccountMeterState(pseudo, BarLayout::Sparkline, Account(L"openai"), data, 0, 0);
        CHECK(chart.sparkExtraKey == L"[\u00C7\u0155 ]" && chart.sparkExtraText == L"400");
        CHECK(chart.quotas[1].key == L"[7\u00F0 ]");
    });

    Run("headline countdown in Chinese, Russian and the pseudo-locale: each form from its template with its "
        "numbers; now and no reset; English as before", [&] {
        const I18nLocale zh = TestLocale(L"zh-Hans"), ru = TestLocale(L"ru"), pseudo = TestLocale(L"qps-ploc");
        CHECK(TestCountdown({}, 3 * day + 12 * hour) == L"\u21BB 3d 12h");
        CHECK(TestCountdown({}, 45 * kMinute) == L"\u21BB 45m");
        // Chinese keeps the compact Latin units; its now and no reset are its own (现在, 无重置).
        CHECK(TestCountdown(zh, 3 * day + 12 * hour) == L"\u21BB 3d 12h");
        CHECK(TestCountdown(zh, 3 * day) == L"\u21BB 3d");
        CHECK(TestCountdown(zh, hour + 30 * kMinute) == L"\u21BB 1h 30m");
        CHECK(TestCountdown(zh, 0 - (long long)kMinute) == L"\u21BB \u73B0\u5728");
        CHECK(TestCountdown(zh, 0) == L"\u21BB \u65E0\u91CD\u7F6E");
        // Russian: 3д 12ч, 3д, 1ч 30м, 2ч, 45м; сейчас; без сброса.
        CHECK(TestCountdown(ru, 3 * day + 12 * hour) == L"\u21BB 3\u0434 12\u0447");
        CHECK(TestCountdown(ru, 3 * day) == L"\u21BB 3\u0434");
        CHECK(TestCountdown(ru, hour + 30 * kMinute) == L"\u21BB 1\u0447 30\u043C");
        CHECK(TestCountdown(ru, 2 * hour) == L"\u21BB 2\u0447");
        CHECK(TestCountdown(ru, 44 * kMinute + 1) == L"\u21BB 45\u043C");
        CHECK(TestCountdown(ru, 0 - (long long)kMinute) == L"\u21BB \u0441\u0435\u0439\u0447\u0430\u0441");
        CHECK(TestCountdown(ru, 0) == L"\u21BB \u0431\u0435\u0437 \u0441\u0431\u0440\u043E\u0441\u0430");
        // The pseudo-locale: [3ð 12ĥ ļ], [3ð ], [1ĥ 30ɱ ļ], [2ĥ ], [45ɱ ], [ñöŵ ļ], [ñö ŕéšéţ ļö].
        CHECK(TestCountdown(pseudo, 3 * day + 12 * hour) == L"\u21BB [3\u00F0 12\u0125 \u013C]");
        CHECK(TestCountdown(pseudo, 3 * day) == L"\u21BB [3\u00F0 ]");
        CHECK(TestCountdown(pseudo, hour + 30 * kMinute) == L"\u21BB [1\u0125 30\u0271 \u013C]");
        CHECK(TestCountdown(pseudo, 2 * hour) == L"\u21BB [2\u0125 ]");
        CHECK(TestCountdown(pseudo, 45 * kMinute) == L"\u21BB [45\u0271 ]");
        CHECK(TestCountdown(pseudo, 0 - (long long)kMinute) == L"\u21BB [\u00F1\u00F6\u0175 \u013C]");
        CHECK(TestCountdown(pseudo, 0) == L"\u21BB [\u00F1\u00F6 \u0155\u00E9\u0161\u00E9\u0163 \u013C\u00F6]");
    });

    Run("headline countdown reserve: the widest of every form behind the arrow in the language, each number "
        "at two digits, in regular text at the key's size", [] {
        std::vector<std::wstring> measured;
        bool regular = true;
        // One DIP a character, so the widest form is the longest.
        TileTextMeasure count = [&](std::wstring_view text, TileFont font, double size) {
            regular = regular && font == TileFont::Text && size == 9;
            measured.emplace_back(text);
            return double(text.size());
        };
        // Russian: 00д 00ч, 00д, 00ч 00м, 00ч, 00м, сейчас, без сброса; no reset the widest, 12
        // characters with the arrow.
        CHECK_NEAR(HeadlineCountdownReserve(TestLocale(L"ru"), count, 9), 12);
        CHECK((measured == std::vector<std::wstring>{
                               L"\u21BB 00\u0434 00\u0447", L"\u21BB 00\u0434", L"\u21BB 00\u0447 00\u043C",
                               L"\u21BB 00\u0447", L"\u21BB 00\u043C",
                               L"\u21BB \u0441\u0435\u0439\u0447\u0430\u0441",
                               L"\u21BB \u0431\u0435\u0437 \u0441\u0431\u0440\u043E\u0441\u0430"}));
        CHECK(regular);
        // English: no reset's 10 characters outcount 00h 00m's 9; Japanese (リセットなし) 8, its
        // hours-and-minutes form 9; the pseudo-locale's no reset ([ñö ŕéšéţ ļö]) 15.
        measured.clear();
        CHECK_NEAR(HeadlineCountdownReserve({}, count, 9), 10);
        CHECK(measured.size() == 7 && measured[2] == L"\u21BB 00h 00m" && measured[5] == L"\u21BB now" &&
              measured[6] == L"\u21BB no reset");
        CHECK_NEAR(HeadlineCountdownReserve(TestLocale(L"ja"), count, 9), 9);
        CHECK_NEAR(HeadlineCountdownReserve(TestLocale(L"qps-ploc"), count, 9), 15);
        // As DirectWrite lays them out at the 9 DIP key font, the reserve and the widest form: in
        // English the hours-and-minutes form, the reserve before every form counted.
        for (const wchar_t* tag : {L"en", L"de", L"ru", L"ja", L"zh-Hans", L"qps-ploc"}) {
            std::wstring widest;
            double width = 0;
            TileTextMeasure laid = [&](std::wstring_view text, TileFont, double size) {
                double w = UiLayoutSize(UiLayout(TestGraphics(), text, {UiFamily::Taskbar, float(size), 0,
                                                                        DWRITE_FONT_WEIGHT_NORMAL},
                                                 100000, false)).width;
                if (w > width) width = w, widest = text;
                return w;
            };
            double reserve = HeadlineCountdownReserve(TestLocale(tag), laid, 9);
            std::string narrow;
            for (wchar_t c : widest) narrow += c < 0x80 ? char(c) : '?';
            std::cout << "OBSERVE countdown reserve " << std::string(tag, tag + wcslen(tag)) << ": " << reserve
                      << " DIP, " << narrow << '\n';
        }
    });

    Run("preview strip in a language: bar keys, the headline's key and countdown and the matrix's keys as "
        "the model gives them; the matrix reserves its widest localized key", [] {
        auto texts = [](const SettingsPreviewStrip& strip) {
            std::vector<std::wstring> found;
            for (const SettingsPreviewOp& op : strip.ops) {
                if (op.kind == SettingsPreviewOp::Kind::Text) found.push_back(op.text);
            }
            return found;
        };
        auto has = [](const std::vector<std::wstring>& found, std::wstring_view text) {
            return std::find(found.begin(), found.end(), text) != found.end();
        };
        const I18nLocale zh = TestLocale(L"zh-Hans"), ru = TestLocale(L"ru");
        Settings bars = StackedSettings();
        bars.showBarLabels = true;
        std::vector<std::wstring> zhBars =
            texts(SettingsPreviewLayout(TestGraphics(), bars, zh, 48, 1, kNow, false, false, 0xFFFFFFFF));
        CHECK(has(zhBars, L"7\u5929") && has(zhBars, L"\u989D\u5916") && has(zhBars, L"\u79EF\u5206"));
        CHECK(!has(zhBars, L"7d") && !has(zhBars, L"Ex"));
        Settings headline = With([](Settings& s) {
            s.barLayout = BarLayout::LogoHeadline;
            s.headlineCountdown = true;
        });
        std::vector<std::wstring> ruHeadline =
            texts(SettingsPreviewLayout(TestGraphics(), headline, ru, 48, 1, kNow, false, false, 0xFFFFFFFF));
        CHECK(has(ruHeadline, L"7\u0434"));
        CHECK(std::any_of(ruHeadline.begin(), ruHeadline.end(), [](const std::wstring& text) {
            return text.starts_with(L"\u21BB ") && text.find_first_of(L"\u0434\u0447\u043C") != std::wstring::npos;
        }));
        // The quota matrix: each cell's number sits past the widest key's reserve and the key gap,
        // wider in Chinese than English by the widest keys' difference.
        Settings matrix = With([](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; });
        auto numberLeft = [&](const I18nLocale& locale) {
            SettingsPreviewStrip strip = SettingsPreviewLayout(TestGraphics(), matrix, locale, 48, 1, kNow, false, false,
                                                               0xFFFFFFFF);
            float keyLeft = 0;
            for (const SettingsPreviewOp& op : strip.ops) {
                if (op.kind != SettingsPreviewOp::Kind::Text) continue;
                if (op.text == QuotaKey(locale, L"openai", kFiveHourBar)) keyLeft = op.rect.left;
                if (keyLeft && op.text.ends_with(L"%")) return op.rect.left - keyLeft;
            }
            return 0.0f;
        };
        auto widestKey = [&](const I18nLocale& locale) {
            UiGraphics& g = TestGraphics();
            float widest = 0;
            for (I18nKey key : kQuotaKeys) {
                widest = std::max(widest, UiLayoutSize(UiLayout(g, I18nText(locale, key), {UiFamily::Taskbar, 9, 0,
                                                                                         DWRITE_FONT_WEIGHT_NORMAL},
                                                                  100000, false)).width);
            }
            return widest;
        };
        float english = numberLeft({}), chinese = numberLeft(zh);
        CHECK(english > 0 && chinese > english);
        std::cout << "OBSERVE matrix number offset: English " << english << " DIP, Chinese " << chinese
                  << " DIP; widest keys at 9 DIP: " << widestKey({}) << " / " << widestKey(zh) << '\n';
    });

    Run("bar key column: the widest key as measured at the percent font, rounded up to a DIP, then 3 DIPs; "
        "none without keys or on tiles; a side taskbar's stacked bars give it their room", [] {
        const Settings keys = With([](Settings& s) {
            s.barLayout = BarLayout::Stacked;
            s.showBarLabels = true;
        });
        auto column = [](const Settings& settings, const wchar_t* tag, const TileTextMeasure& measure = TestWideMeasure) {
            return MeterDisplayFor(settings, TestLocale(tag), measure, kNow, 1, 48).barKeyColumn;
        };
        // English: every key two characters, 10.8 DIPs at 9: 11 + 3.
        CHECK_NEAR(Display(keys).barKeyColumn, 14);
        // 额外 and 积分, 追加: two em-wide characters, 18 DIPs: 21. Russian's Мс two narrow ones: 14.
        // The pseudo-locale's five characters ([Ṁö ]): 27 + 3.
        CHECK_NEAR(column(keys, L"zh-Hans"), 21);
        CHECK_NEAR(column(keys, L"ja"), 21);
        CHECK_NEAR(column(keys, L"ru"), 14);
        CHECK_NEAR(column(keys, L"qps-ploc"), 30);
        // At a 14 DIP percent font: 28 + 3; vertical bars alike.
        Settings large = keys;
        large.percentFontSize = 14;
        CHECK_NEAR(column(large, L"zh-Hans"), 31);
        large.barLayout = BarLayout::Vertical;
        CHECK_NEAR(column(large, L"zh-Hans"), 31);
        // Measured in regular text at the percent font: a key of 9.4 DIPs rounds up to 10.
        bool regular = true;
        TileTextMeasure fixed = [&](std::wstring_view, TileFont font, double size) {
            regular = regular && font == TileFont::Text && size == 9;
            return 9.4;
        };
        CHECK_NEAR(column(keys, L"de", fixed), 13);
        CHECK(regular);
        // Keys off, or a tile style: no column.
        CHECK_NEAR(column(With([](Settings& s) { s.barLayout = BarLayout::Stacked; }), L"zh-Hans"), 0);
        CHECK_NEAR(column(With([](Settings& s) {
                              s.barLayout = BarLayout::QuotaMatrix;
                              s.showBarLabels = true;
                          }), L"zh-Hans"),
                   0);
        // Down a 48 DIP side taskbar (40 DIPs across): 100 DIP bars shrink to 40 - 21 in Chinese.
        CHECK(MeterDisplayFor(keys, TestLocale(L"zh-Hans"), TestWideMeasure, kNow, 1, 0, 48).barLength == 19);
    });

    Run("sparkline text placement by measured width: a wide key or extra usage key moves where a narrower one "
        "clears the current reading", [] {
        // The default 48 x 32 chart, 8 DIP text (5.6 cap height), the ring 3.6 DIPs around the
        // reading. Weekly alone, 10% at 62.5% of the window: the reading at (30, 27.25), the ring
        // from x 26.4 to 33.6 across the bottom box (y 23.4 to 29). English "7d" (9.6 DIPs) from x
        // 35.4: clear, kept at the bottom; Chinese 7天 (4.8 + 8 DIPs) from 32.2: hit, moved to
        // the top.
        auto weeklyKey = [](const wchar_t* tag) {
            AccountData data = Fresh();
            data.winWeek = {10, kNow + (ULONGLONG)std::llround(0.375 * kWeeklyWindowMs), kWeeklyWindowMs};
            MeterDisplay display = MeterDisplayFor(With([](Settings& s) { s.barLayout = BarLayout::Sparkline; }),
                                                   TestLocale(tag), TestWideMeasure, kNow, 1, 48);
            return AccountMeterState(display, BarLayout::Sparkline, Account(L"anthropic", L"A", {false, true, false, false}),
                                     data, 0, 0).quotas[kWeeklyBar];
        };
        CHECK(weeklyKey(L"en").key == L"7d" && !weeklyKey(L"en").sparkKeyTop);
        CHECK(weeklyKey(L"zh-Hans").key == L"7\u5929" && weeklyKey(L"zh-Hans").sparkKeyTop);
        // The extra usage's percentage and key on an 80 DIP weekly chart: "40%" (14.4 DIPs), the 2
        // DIP gap and "Ex" (9.6) from x 3 to 29 in English; 额外 (16) to 35.4 in Chinese. 110% at
        // 25% of the window, (20, 2), the ring from x 16.4: inside both, which move to the bottom
        // edge (the key at its right end, from 64.2 in Chinese, never in the way); at 45%, (36, 2),
        // from 32.4: past English's, inside Chinese's.
        auto extraBottom = [](const wchar_t* tag, double share) {
            AccountData data = Fresh();
            data.winWeek = {110, kNow + (ULONGLONG)std::llround((1 - share) * kWeeklyWindowMs), kWeeklyWindowMs};
            data.extraUsage = {40, kNow + 10 * 24 * 60 * kMinute, 30ULL * 24 * 60 * kMinute};
            data.extraUsedAmount = 20;
            data.extraLimitAmount = 50;
            MeterDisplay display = MeterDisplayFor(With([](Settings& s) {
                                                       s.barLayout = BarLayout::Sparkline;
                                                       s.sparklineWidth = 80;
                                                       s.sparklineExtra = SparklineExtra::Percent;
                                                   }),
                                                   TestLocale(tag), TestWideMeasure, kNow, 1, 48);
            AccountMeters meters = AccountMeterState(display, BarLayout::Sparkline,
                                                     Account(L"anthropic", L"A", {false, true, false, true}), data, 0, 0);
            CHECK(meters.sparkExtraText == L"40%" && meters.sparkExtraKey == QuotaKey(TestLocale(tag), L"anthropic", 3));
            return meters.sparkExtraBottom;
        };
        CHECK(extraBottom(L"en", 0.25) && extraBottom(L"zh-Hans", 0.25));
        CHECK(!extraBottom(L"en", 0.45) && extraBottom(L"zh-Hans", 0.45));
        // Each text measured in its own font: the key and the number semibold, the extra usage's
        // key regular, all at the text size.
        std::vector<std::pair<std::wstring, TileFont>> measured;
        TileTextMeasure record = [&](std::wstring_view text, TileFont font, double size) {
            if (size == 8) measured.emplace_back(text, font);
            return TestMeasure(text, font, size);
        };
        AccountData data = Fresh();
        data.winWeek = {10, kNow + 3 * 24 * 60 * kMinute, kWeeklyWindowMs};
        data.extraUsage = {40, kNow + 10 * 24 * 60 * kMinute, 30ULL * 24 * 60 * kMinute};
        MeterDisplay display = MeterDisplayFor(With([](Settings& s) {
                                                   s.barLayout = BarLayout::Sparkline;
                                                   s.sparklineExtra = SparklineExtra::Percent;
                                               }),
                                               TestLocale(L"de"), record, kNow, 1, 48);
        AccountMeterState(display, BarLayout::Sparkline, Account(L"anthropic", L"A", {false, true, false, true}), data, 0, 0);
        CHECK((measured == std::vector<std::pair<std::wstring, TileFont>>{
                               {L"7T", TileFont::TextSemiBold}, {L"40%", TileFont::TextSemiBold}, {L"Ex", TileFont::Text}}));
    });

    Run("gauge key: shrinks from the key's font until it keeps 2 DIPs from the arc's ends, never under 6 DIPs", [] {
        // The default 32 DIP gauge: a 3 DIP stroke at radius 14, its inner edge (12.5) ending 8.84
        // DIPs below and beside the centre, where the opening is narrowest; the number at 12 DIPs,
        // the key at 9/14 of it, 7.71. Fit: half the key's width and 2 within 8.84.
        auto keyFont = [](const wchar_t* tag, std::array<double, kQuotaBarCount> pct,
                          const TileTextMeasure& measure = TestWideMeasure) {
            MeterDisplay display = MeterDisplayFor(With([](Settings& s) { s.barLayout = BarLayout::Gauge; }),
                                                   TestLocale(tag), measure, kNow, 1, 48);
            AccountMeters meters = AccountMeterState(display, BarLayout::Gauge,
                                                     Account(L"anthropic", L"A", {true, true, false, true}), Fresh(pct), 0, 0);
            return meters.faceKeyFont;
        };
        const double key = 12.0 * 9 / 14;
        // Extra usage the most used. English "Ex", 9.26 DIPs: fits as it is.
        CHECK_NEAR(keyFont(L"en", {10, 20, -1, 90}), key);
        // 额外, two ems: 7.71 + 2 past 8.84; four quarter-DIP steps down, 6.71 + 2 fits.
        CHECK_NEAR(keyFont(L"zh-Hans", {10, 20, -1, 90}), key - 1);
        // Measured at each size, not scaled: with 3 DIPs more under 7 DIPs (16.43 at 6.71), it
        // never fits and stops at 6.
        CHECK_NEAR(keyFont(L"zh-Hans", {10, 20, -1, 90},
                           [](std::wstring_view text, TileFont font, double size) {
                               return TestWideMeasure(text, font, size) + (size < 7 ? 3 : 0);
                           }),
                   6);
        // 7天, 1.6 ems (6.17 + 2): fits.
        CHECK_NEAR(keyFont(L"zh-Hans", {10, 90, -1, 20}), key);
        // The pseudo-locale's [Éẋ ], three ems: down to 6, where it still doesn't fit.
        CHECK_NEAR(keyFont(L"qps-ploc", {10, 20, -1, 90}), 6);
        // Without a reading, the key's font as it is.
        CHECK_NEAR(keyFont(L"qps-ploc", {-1, -1, -1, -1}), key);
    });

    Run("preview strip in every language: bar keys inside their column, matrix numbers past their key and its "
        "gap, the headline countdown's reserve holding every form", [] {
        UiGraphics& g = TestGraphics();
        auto width = [&](const SettingsPreviewOp& op) {
            return UiLayoutSize(UiLayout(g, op.text, op.style, 100000, false)).width;
        };
        for (const wchar_t* tag : {L"en", L"de", L"ru", L"ja", L"zh-Hans", L"qps-ploc"}) {
            const I18nLocale locale = TestLocale(tag);
            std::vector<std::wstring> keys;
            for (I18nKey key : kQuotaKeys) keys.push_back(I18nText(locale, key));
            auto isKey = [&](const std::wstring& text) { return std::find(keys.begin(), keys.end(), text) != keys.end(); };
            for (float scale : {1.0f, 1.5f}) {
                // Each key's text (trailing-aligned in its box) as wide as its box at most.
                for (BarLayout layout : {BarLayout::Stacked, BarLayout::Vertical}) {
                    for (int size : {9, 14}) {
                        Settings s = With([&](Settings& s) {
                            s.barLayout = layout;
                            s.showBarLabels = true;
                            s.percentFontSize = size;
                        });
                        int found = 0;
                        for (const SettingsPreviewOp& op :
                             SettingsPreviewLayout(g, s, locale, 48, scale, kNow, false, false, 0xFFFFFFFF).ops) {
                            if (op.kind != SettingsPreviewOp::Kind::Text || !isKey(op.text) || op.style.size != size) continue;
                            found++;
                            CHECK(width(op) <= op.rect.right - op.rect.left + 0.01f);
                        }
                        CHECK(found == 10);
                    }
                }
                // Matrix cells: the number at least the key's width and 3 DIPs past its left.
                std::vector<SettingsPreviewOp> ops =
                    SettingsPreviewLayout(g, With([](Settings& s) { s.barLayout = BarLayout::QuotaMatrix; }), locale, 48,
                                          scale, kNow, false, false, 0xFFFFFFFF)
                        .ops;
                int cells = 0;
                for (size_t i = 0; i + 1 < ops.size(); i++) {
                    if (ops[i].kind != SettingsPreviewOp::Kind::Text || !isKey(ops[i].text)) continue;
                    cells++;
                    CHECK(ops[i + 1].rect.left >= ops[i].rect.left + width(ops[i]) + 3 - 0.01f);
                }
                CHECK(cells == 10);
            }
            // The headline countdown: an account's tile as wide without a reset time as with one.
            Settings countdown = With([](Settings& s) {
                s.barLayout = BarLayout::LogoHeadline;
                s.headlineCountdown = true;
            });
            auto tileWidth = [&](ULONGLONG reset) {
                std::vector<AccountConfig> accounts = {Account(L"anthropic", L"A", {true, false, false, false})};
                std::vector<AccountData> data = {Fresh()};
                data[0].win5h = {40, reset, reset ? kFiveHours : 0};
                SettingsPreviewStrip strip = SettingsPreviewLayout(g, countdown, locale, 48, 1, kNow, false, false,
                                                                   0xFFFFFFFF, &accounts, &data);
                return strip.columns.at(0).right - strip.columns.at(0).left;
            };
            const float timed = tileWidth(kNow + 4 * 60 * kMinute + 30 * kMinute);
            CHECK_NEAR(tileWidth(0), timed);
            CHECK_NEAR(tileWidth(kNow - kMinute), timed);
        }
    });

    Run("account errors: a coded error formats in the reader's language and logs in English; a message "
        "without a code shows and logs as is; none is empty", [] {
        const AccountError notRunning{AccountErrorCode::AntigravityNotRunning};
        CHECK(bool(notRunning) && notRunning.detail.empty());
        CHECK(AccountErrorText({}, notRunning) == L"Antigravity not running");
        // Antigravity 未运行; Antigravity не запущен.
        CHECK(AccountErrorText(TestLocale(L"zh-Hans"), notRunning) == L"Antigravity \u672A\u8FD0\u884C");
        CHECK(AccountErrorText(TestLocale(L"ru"), notRunning) ==
              L"Antigravity \u043D\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D");
        // The log and its dedupe key take the English text, whatever the UI's language.
        CHECK(AccountErrorText(I18nLocale(), notRunning) == AccountErrorText({}, notRunning));
        const AccountError message{AccountErrorCode::Message, L"network error"};
        CHECK(AccountErrorText(TestLocale(L"zh-Hans"), message) == L"network error" &&
              AccountErrorText({}, message) == L"network error");
        AccountError cleared = notRunning;
        cleared = {};
        CHECK(!cleared && cleared.code == AccountErrorCode::None && AccountErrorText({}, cleared).empty());
        CHECK(notRunning == AccountError{AccountErrorCode::AntigravityNotRunning} && !(notRunning == message));
        // A coded error with no text of its own still marks a stale account.
        AccountData data = Fresh({10, -1, -1, -1});
        data.stale = true;
        data.error = notRunning;
        MeterDisplay display = Display(With([](Settings& s) { s.showStaleWarning = true; }));
        CHECK(AccountMeterState(display, BarLayout::Stacked, Account(L"antigravity"), data, 0, 0).warn);
    });

    Run("accounts page: the coded error's state reads Local app not running in the page's language; another "
        "code's alone form, and provider text whole, ellipsized with the card's tooltip; key chips localized", [] {
        auto stateOf = [](const UiFrame& frame) {
            std::vector<std::wstring> states;
            for (const UiItem& item : frame.content) {
                for (const UiRun& run : item.runs) {
                    if (item.kind == UiItemKind::Text && run.ink == UiInk::Caution) states.push_back(run.text);
                }
            }
            return states;
        };
        auto frameOf = [](const I18nLocale& locale, AccountError error) {
            SettingsView view = AccountsView();
            view.locale = locale;
            // Go (Antigravity) in its last poll's error.
            view.accounts[3].status.error = std::move(error);
            view.accounts[3].status.polled = false;
            UiFrame frame;
            SettingsUiBuildFrame(TestGraphics(), view, {1000, 820}, frame);
            return frame;
        };
        UiFrame english = frameOf({}, {AccountErrorCode::AntigravityNotRunning});
        CHECK((stateOf(english) == std::vector<std::wstring>{L"Sign-in needed", L"Local app not running"}));
        // 本地应用未运行; Локальное приложение не запущено.
        CHECK(stateOf(frameOf(TestLocale(L"zh-Hans"), {AccountErrorCode::AntigravityNotRunning})).back() ==
              L"\u672C\u5730\u5E94\u7528\u672A\u8FD0\u884C");
        CHECK(stateOf(frameOf(TestLocale(L"ru"), {AccountErrorCode::AntigravityNotRunning})).back() ==
              L"\u041B\u043E\u043A\u0430\u043B\u044C\u043D\u043E\u0435 \u043F\u0440\u0438\u043B\u043E\u0436\u0435"
              L"\u043D\u0438\u0435 \u043D\u0435 \u0437\u0430\u043F\u0443\u0449\u0435\u043D\u043E");
        // Another error of the local app: its alone form, as the first letter's capital was before;
        // provider text whole on a line that ellipsizes it, the card header's tooltip holding it.
        CHECK(stateOf(frameOf({}, {AccountErrorCode::LanguageServerNetwork})).back() ==
              L"Network error (language server)");
        const std::wstring provider = L"X" + std::wstring(69, L'x');
        UiFrame cut = frameOf(TestLocale(L"zh-Hans"), {AccountErrorCode::Message, provider});
        CHECK(stateOf(cut).back() == provider);
        const UiItem* header = UiFindItem(cut, AccountId(4, SettingsField::AccountExpand));
        CHECK(header && UiRunsText(header->tooltip).find(provider) != std::wstring::npos);
        CHECK(std::any_of(cut.content.begin(), cut.content.end(), [&](const UiItem& item) {
            return item.trim && UiRunsText(item.runs).ends_with(provider);
        }));
        // The quota chips in the card headers: OAI's 5h and 7d (5h, 7天).
        UiFrame chinese = frameOf(TestLocale(L"zh-Hans"), {});
        std::vector<std::wstring> chips;
        for (const UiItem& item : chinese.content) {
            if (item.kind == UiItemKind::Chip) chips.push_back(item.text);
        }
        CHECK(std::find(chips.begin(), chips.end(), L"7\u5929") != chips.end() &&
              std::find(chips.begin(), chips.end(), L"7d") == chips.end());
    });

    Run("extra quota names: the reported name as is, then its window's length in whole hours with the "
        "language's plural forms; English as before", [&] {
        auto name = [](const I18nLocale& locale, std::wstring reported, ULONGLONG durationMs) {
            return ExtraQuotaName(locale, {std::move(reported), {50, kNow + 60 * kMinute, durationMs}, durationMs});
        };
        CHECK(name({}, L"Sonnet", kWeeklyWindowMs) == L"Sonnet weekly");
        CHECK(name({}, L"Spark", kFiveHourWindowMs) == L"Spark 5-hour");
        CHECK(name({}, L"Gemini", 2 * day) == L"Gemini 2-day" && name({}, L"Gemini", day) == L"Gemini 1-day");
        CHECK(name({}, L"Flash", 36 * hour) == L"Flash 36-hour" && name({}, L"Flash", hour) == L"Flash 1-hour");
        CHECK(name({}, L"Raw bucket", 90 * kMinute) == L"Raw bucket");
        CHECK(name({}, L"Argon current limit", 0) == L"Argon current limit");
        // Russian: Gemini: 2 дня, 5 дней, 21 день, 11 дней; Flash: 1 час, 3 часа, 36 часов; Spark: 5 часов;
        // Sonnet: неделя.
        const I18nLocale ru = TestLocale(L"ru");
        CHECK(name(ru, L"Gemini", 2 * day) == L"Gemini: 2 \u0434\u043D\u044F");
        CHECK(name(ru, L"Gemini", 5 * day) == L"Gemini: 5 \u0434\u043D\u0435\u0439");
        CHECK(name(ru, L"Gemini", 21 * day) == L"Gemini: 21 \u0434\u0435\u043D\u044C");
        CHECK(name(ru, L"Gemini", 11 * day) == L"Gemini: 11 \u0434\u043D\u0435\u0439");
        CHECK(name(ru, L"Flash", hour) == L"Flash: 1 \u0447\u0430\u0441");
        CHECK(name(ru, L"Flash", 3 * hour) == L"Flash: 3 \u0447\u0430\u0441\u0430");
        CHECK(name(ru, L"Flash", 36 * hour) == L"Flash: 36 \u0447\u0430\u0441\u043E\u0432");
        CHECK(name(ru, L"Spark", kFiveHourWindowMs) == L"Spark: 5 \u0447\u0430\u0441\u043E\u0432");
        CHECK(name(ru, L"Sonnet", kWeeklyWindowMs) == L"Sonnet: \u043D\u0435\u0434\u0435\u043B\u044F");
        CHECK(name(ru, L"Raw bucket", 90 * kMinute) == L"Raw bucket");
        // German: one and other; Japanese: 2 日間; Chinese: 每周.
        const I18nLocale de = TestLocale(L"de");
        CHECK(name(de, L"Spark", kFiveHourWindowMs) == L"Spark (5 Stunden)" && name(de, L"Spark", hour) == L"Spark (1 Stunde)");
        CHECK(name(de, L"Gemini", 2 * day) == L"Gemini (2 Tage)" && name(de, L"Gemini", day) == L"Gemini (1 Tag)");
        CHECK(name(de, L"Sonnet", kWeeklyWindowMs) == L"Sonnet w\u00F6chentlich");
        CHECK(name(TestLocale(L"ja"), L"Gemini", 2 * day) == L"Gemini 2 \u65E5\u9593");
        CHECK(name(TestLocale(L"zh-Hans"), L"Sonnet", kWeeklyWindowMs) == L"Sonnet \u6BCF\u5468");

        // Quotas whose names read alike in English, the window's length included, merge: the more used
        // reading stays under the first one's name. A repeated window, windows without a length in
        // whole hours, and a name that spells out another's length; another window lists apart.
        AccountData data;
        AddExtraQuota(&data, L"Spark", {30, kNow + hour, kFiveHourWindowMs});
        AddExtraQuota(&data, L"Spark", {60, kNow + hour, kWeeklyWindowMs});
        AddExtraQuota(&data, L"Spark", {40, kNow + hour, kFiveHourWindowMs});
        AddExtraQuota(&data, L"Spark", {20, kNow + hour, kFiveHourWindowMs});
        AddExtraQuota(&data, L"Raw", {10, 0, 0});
        AddExtraQuota(&data, L"Raw", {15, 0, 90 * kMinute});
        AddExtraQuota(&data, L"Other", {10, 0, day});
        AddExtraQuota(&data, L"Other 1-day", {25, 0, 0});
        AddExtraQuota(&data, L"Pro weekly", {5, 0, 0});
        AddExtraQuota(&data, L"Pro", {35, 0, kWeeklyWindowMs});
        std::vector<std::wstring> names;
        std::vector<double> used;
        for (const NamedQuota& quota : data.extraQuotas) {
            names.push_back(ExtraQuotaName({}, quota));
            used.push_back(quota.usage.pct);
        }
        CHECK((names == std::vector<std::wstring>{L"Spark 5-hour", L"Spark weekly", L"Raw", L"Other 1-day",
                                                  L"Pro weekly"}));
        CHECK((used == std::vector<double>{40, 60, 15, 25, 35}));
        // The merged readings keep their own windows: the Other row's has none now, the Pro row's a week.
        CHECK(data.extraQuotas[3].usage.windowDurationMs == 0 && data.extraQuotas[4].usage.windowDurationMs == kWeeklyWindowMs);
    });

    Run("visual test samples: the plan chip in the display's language; labels, model names and the sample "
        "incident as written", [] {
        auto build = [](const I18nLocale& locale) {
            std::vector<AccountConfig> accounts;
            std::vector<AccountData> data;
            BuildVisualTestSnapshot(MeterDisplayFor(Settings{}, locale, TestMeasure, kNow, 1, 48), &accounts, &data);
            return std::pair{accounts, data};
        };
        auto names = [](const AccountData& data, const I18nLocale& locale) {
            std::vector<std::wstring> found;
            for (const NamedQuota& quota : data.extraQuotas) found.push_back(ExtraQuotaName(locale, quota));
            return found;
        };
        const I18nLocale zh = TestLocale(L"zh-Hans");
        auto [accounts, data] = build(zh);
        // 视觉测试
        CHECK(data.size() == 4 && data[0].plan == L"\u89C6\u89C9\u6D4B\u8BD5" && data[3].plan == data[0].plan);
        CHECK(accounts[0].label == L"OAI" && accounts[1].label == L"CC 1" && accounts[2].label == L"CC 2" &&
              accounts[3].label == L"Go");
        // The OAI sample's Spark limits and CC 2's model weeklies: English as before; Chinese around the names.
        CHECK((names(data[0], {}) == std::vector<std::wstring>{L"Spark 5-hour", L"Spark weekly"}));
        CHECK((names(data[2], {}) == std::vector<std::wstring>{L"Sonnet weekly", L"Opus weekly"}));
        CHECK((names(data[2], zh) == std::vector<std::wstring>{L"Sonnet \u6BCF\u5468", L"Opus \u6BCF\u5468"}));
        // Визуальный тест
        CHECK(build(TestLocale(L"ru")).second[1].plan ==
              L"\u0412\u0438\u0437\u0443\u0430\u043B\u044C\u043D\u044B\u0439 \u0442\u0435\u0441\u0442");
        CHECK(build({}).second[1].plan == L"Visual test");
        ProviderOutage outage = VisualTestOutage(L"openai", kNow);
        CHECK(outage.component == L"Codex API" && outage.title == L"Elevated error rates");
    });

    Run("details flyout: a number and its unit in each language's order, the unit first in Japanese and "
        "Chinese", [] {
        auto reading = [](const wchar_t* tag, const wchar_t* unit) {
            return I18nText(TestLocale(tag), TrCtx(L"quota-unit-order", L"{number} {unit}"),
                            {{L"number", L"45%"}, {L"unit", unit}});
        };
        CHECK(reading(L"en", L"used") == L"45% used");
        // 使用済み 45%, 残り 45%; 已用 45%.
        CHECK(reading(L"ja", L"\u4F7F\u7528\u6E08\u307F") == L"\u4F7F\u7528\u6E08\u307F 45%");
        CHECK(reading(L"ja", L"\u6B8B\u308A") == L"\u6B8B\u308A 45%");
        CHECK(reading(L"zh-Hans", L"\u5DF2\u7528") == L"\u5DF2\u7528 45%");
        for (const wchar_t* tag : {L"de", L"es", L"fr", L"pt-BR", L"ru"}) CHECK(reading(tag, L"x") == L"45% x");
        // The pseudo-locale keeps the English order inside its brackets.
        std::wstring pseudo = reading(L"qps-ploc", L"x");
        CHECK(pseudo.find(L"45%") < pseudo.find(L'x'));
    });

    Run("details flyout templates on the production catalogs: Russian free resets by plural form and with "
        "the next expiry; the French outage line's others (one for 0 and 1) and a Russian one through its "
        "start and incident; a German reset line and retry; a Russian Copilot amount; English as before", [] {
        const I18nLocale ru = TestLocale(L"ru"), fr = TestLocale(L"fr"), de = TestLocale(L"de");
        auto freeResets = [](const I18nLocale& locale, int count) {
            return I18nText(locale, TrCtx(L"free-resets",
                                          L"{count, plural, one{# free reset available} other{# free resets available}}"),
                            {{L"count", count}});
        };
        CHECK(freeResets({}, 1) == L"1 free reset available" && freeResets({}, 2) == L"2 free resets available");
        // Доступен 1 бесплатный сброс; Доступно 2 бесплатных сброса; Доступно 5 бесплатных сбросов; Доступен 21
        // бесплатный сброс.
        CHECK(freeResets(ru, 1) == L"\u0414\u043E\u0441\u0442\u0443\u043F\u0435\u043D 1 \u0431\u0435\u0441\u043F"
                                   L"\u043B\u0430\u0442\u043D\u044B\u0439 \u0441\u0431\u0440\u043E\u0441");
        CHECK(freeResets(ru, 2) == L"\u0414\u043E\u0441\u0442\u0443\u043F\u043D\u043E 2 \u0431\u0435\u0441\u043F"
                                   L"\u043B\u0430\u0442\u043D\u044B\u0445 \u0441\u0431\u0440\u043E\u0441\u0430");
        CHECK(freeResets(ru, 5) == L"\u0414\u043E\u0441\u0442\u0443\u043F\u043D\u043E 5 \u0431\u0435\u0441\u043F"
                                   L"\u043B\u0430\u0442\u043D\u044B\u0445 \u0441\u0431\u0440\u043E\u0441\u043E\u0432");
        CHECK(freeResets(ru, 21) == L"\u0414\u043E\u0441\u0442\u0443\u043F\u0435\u043D 21 \u0431\u0435\u0441\u043F"
                                    L"\u043B\u0430\u0442\u043D\u044B\u0439 \u0441\u0431\u0440\u043E\u0441");
        // через 2 д, then: Доступно 3 бесплатных сброса · ближайший истекает через 2 д.
        std::wstring when = I18nText(ru, TrCtx(L"reset-when", L"in {duration}"),
                                     {{L"duration", I18nText(ru, TrCtx(L"duration", L"{days}d"), {{L"days", 2}})}});
        CHECK(when == L"\u0447\u0435\u0440\u0435\u0437 2 \u0434");
        CHECK(I18nText(ru, TrCtx(L"free-resets", L"{count, plural, one{# free reset available} other{# free resets "
                                                 L"available}} \u00B7 next expires {when}"),
                       {{L"count", 3}, {L"when", when}}) ==
              L"\u0414\u043E\u0441\u0442\u0443\u043F\u043D\u043E 3 \u0431\u0435\u0441\u043F\u043B\u0430\u0442\u043D"
              L"\u044B\u0445 \u0441\u0431\u0440\u043E\u0441\u0430 \u00B7 \u0431\u043B\u0438\u0436\u0430\u0439\u0448"
              L"\u0438\u0439 \u0438\u0441\u0442\u0435\u043A\u0430\u0435\u0442 \u0447\u0435\u0440\u0435\u0437 2 "
              L"\u0434");

        auto others = [](const I18nLocale& locale, int count) {
            return I18nText(locale, TrCtx(L"outage-line", L"{component} and {count, plural, one{# more} other{# more}}: "
                                                          L"{level}"),
                            {{L"component", L"Codex API"},
                             {L"count", count},
                             {L"level", I18nText(locale, TrCtx(L"outage-level", L"partial outage"))}});
        };
        CHECK(others({}, 1) == L"Codex API and 1 more: partial outage");
        CHECK(others(fr, 1) == L"Codex API et 1 autre\u00A0: panne partielle");
        CHECK(others(fr, 0) == L"Codex API et 0 autre\u00A0: panne partielle");
        CHECK(others(fr, 2) == L"Codex API et 2 autres\u00A0: panne partielle");
        // Codex API: снижение производительности · с 14:05 · Elevated error rates (the incident as written).
        std::wstring line = I18nText(ru, TrCtx(L"outage-line", L"{component}: {level}"),
                                     {{L"component", L"Codex API"},
                                      {L"level", I18nText(ru, TrCtx(L"outage-level", L"degraded performance"))}});
        line = I18nText(ru, TrCtx(L"outage-line", L"{status} \u00B7 since {time}"), {{L"status", line}, {L"time", L"14:05"}});
        line = I18nText(ru, TrCtx(L"outage-line", L"{status} \u00B7 {incident}"),
                        {{L"status", line}, {L"incident", L"Elevated error rates"}});
        CHECK(line == L"Codex API: \u0441\u043D\u0438\u0436\u0435\u043D\u0438\u0435 \u043F\u0440\u043E\u0438\u0437"
                      L"\u0432\u043E\u0434\u0438\u0442\u0435\u043B\u044C\u043D\u043E\u0441\u0442\u0438 \u00B7 \u0441 "
                      L"14:05 \u00B7 Elevated error rates");

        // Reset in 3 T. 4 Std. (Fr 04:59): the duration in the reset phrase in the line; English before.
        auto resets = [](const I18nLocale& locale) {
            std::wstring duration =
                I18nText(locale, TrCtx(L"duration", L"{days}d {hours}h"), {{L"days", 3}, {L"hours", 4}});
            std::wstring when = I18nText(locale, TrCtx(L"reset-when", L"in {duration} ({time})"),
                                         {{L"duration", duration}, {L"time", L"Fr 04:59"}});
            return I18nText(locale, TrCtx(L"quota-reset", L"Resets {when}"), {{L"when", when}});
        };
        CHECK(resets({}) == L"Resets in 3d 4h (Fr 04:59)");
        CHECK(resets(de) == L"Reset in 3 T. 4 Std. (Fr 04:59)");
        // Fehler: Antigravity läuft nicht – neuer Versuch in 4 Min. 12 Sek.
        auto retry = [](const I18nLocale& locale) {
            return I18nText(locale, TrCtx(L"flyout-note", L"Error: {error} - retry in {duration}"),
                            {{L"error", AccountErrorText(locale, {AccountErrorCode::AntigravityNotRunning})},
                             {L"duration", I18nText(locale, TrCtx(L"duration", L"{minutes}m {seconds}s"),
                                                    {{L"minutes", 4}, {L"seconds", 12}})}});
        };
        CHECK(retry({}) == L"Error: Antigravity not running - retry in 4m 12s");
        CHECK(retry(de) == L"Fehler: Antigravity l\u00E4uft nicht \u2013 neuer Versuch in 4 Min. 12 Sek.");

        // Использовано 12.5 из 300 (премиум-запросы), then Дополнительно: 4 (премиум-запросы): the unit's
        // running-text name as an argument, twice on two lines.
        auto copilot = [](const I18nLocale& locale) {
            return I18nText(locale, TrCtx(L"quota-amount", L"{used} of {limit} {unit} used\n"
                                                           L"Additional usage: {overage} {unit}"),
                            {{L"used", L"12.5"},
                             {L"limit", L"300"},
                             {L"unit", I18nText(locale, TrCtx(L"copilot-unit", L"premium requests"))},
                             {L"overage", L"4"}});
        };
        CHECK(copilot({}) == L"12.5 of 300 premium requests used\nAdditional usage: 4 premium requests");
        CHECK(copilot(ru) == L"\u0418\u0441\u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u043D\u043E 12.5 \u0438"
                             L"\u0437 300 (\u043F\u0440\u0435\u043C\u0438\u0443\u043C-\u0437\u0430\u043F\u0440\u043E"
                             L"\u0441\u044B)\n\u0414\u043E\u043F\u043E\u043B\u043D\u0438\u0442\u0435\u043B\u044C"
                             L"\u043D\u043E: 4 (\u043F\u0440\u0435\u043C\u0438\u0443\u043C-\u0437\u0430\u043F\u0440"
                             L"\u043E\u0441\u044B)");
    });
}
