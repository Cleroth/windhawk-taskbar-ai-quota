// Regional number formats (src/i18n/numbers.h): Windows' strings read into a format, numbers in
// it (fixed and trimmed decimals never grouped, whole numbers in its digit groups), integer
// arguments shown as their regional text while plurals select by the number, the taskbar's
// amounts (the meter model) and the details flyout's amount templates as it composes them, and the
// settings window's number boxes: what they show parses back in every region, typed variants, the
// edit controller and a page's box. Expected text is written out by hand from the regions'
// strings (support.inc) and the production catalogs.

static void RunI18nNumberTests() {
    Run("regional formats: Windows' strings read into a format; an empty, overlong or malformed part keeps "
        "en-US's", [] {
        const I18nNumberFormat en;
        CHECK(I18nNumberFormatFrom(L".", L",", L"3;0") == en);
        CHECK(en.Decimal() == L"." && en.Group() == L"," && en.count == 1 && en.sizes[0] == 3 && en.repeat);
        I18nNumberFormat de = TestNumbers(L"de-DE");
        CHECK(de.Decimal() == L"," && de.Group() == L"." && de.count == 1 && de.sizes[0] == 3 && de.repeat);
        CHECK(TestNumbers(L"fr-FR").Decimal() == L"," && TestNumbers(L"fr-FR").Group() == L"\u202F");
        I18nNumberFormat hi = TestNumbers(L"hi-IN");
        CHECK(hi.Decimal() == L"." && hi.count == 2 && hi.sizes[0] == 3 && hi.sizes[1] == 2 && hi.repeat);
        I18nNumberFormat one = TestNumbers(L"one-group");
        CHECK(one.count == 1 && one.sizes[0] == 3 && !one.repeat);
        I18nNumberFormat none = TestNumbers(L"no-grouping");
        CHECK(none.count == 0 && !none.repeat && none.Group() == L",");
        // A malformed grouping keeps en-US's grouping; the separators stay as given.
        for (std::wstring_view grouping : {L"", L"3;", L";3", L"3;x", L"3;0;2", L"12;0", L"3;2;2;2;2;2;2;2;2"}) {
            I18nNumberFormat f = I18nNumberFormatFrom(L",", L".", grouping);
            CHECK(f.count == 1 && f.sizes[0] == 3 && f.repeat && f.Decimal() == L"," && f.Group() == L".");
        }
        // Separators: empty or longer than Windows' 3 characters keep en-US's; others are taken whole.
        CHECK(I18nNumberFormatFrom(L"", L"", L"3;0") == en);
        CHECK(I18nNumberFormatFrom(L"dec.", L"grp,", L"3;0") == en);
        CHECK(I18nNumberFormatFrom(L"\u066B", L"' '", L"3;0").Decimal() == L"\u066B" &&
              I18nNumberFormatFrom(L"\u066B", L"' '", L"3;0").Group() == L"' '");
        // The default context: English with en-US numbers; a format is part of the context.
        CHECK(I18nLocale().Numbers() == en && I18nLocale(kI18nLanguages[1]).Numbers() == en);
        CHECK(!(TestRegional(L"en", L"de-DE") == I18nLocale()) &&
              &TestRegional(L"en", L"de-DE").Language() == &I18nLocale().Language());
    });

    Run("regional numbers: fixed and trimmed decimals never grouped; whole numbers in the region's groups", [] {
        const I18nLocale en, de = TestRegional(L"en", L"de-DE"), fr = TestRegional(L"en", L"fr-FR"),
                         hi = TestRegional(L"en", L"hi-IN"), ch = TestRegional(L"en", L"de-CH"),
                         none = TestRegional(L"en", L"no-grouping"), one = TestRegional(L"en", L"one-group");
        CHECK_TEXT(I18nFixed(en, 1234.567, 2), L"1234.57");
        CHECK_TEXT(I18nTrimmed(en, 1234.5, 2), L"1234.5");
        CHECK_TEXT(I18nInteger(en, 1234567), L"1,234,567");
        // de-DE
        CHECK_TEXT(I18nFixed(de, 1234.567, 2), L"1234,57");
        CHECK_TEXT(I18nFixed(de, 0.5, 2), L"0,50");
        CHECK_TEXT(I18nFixed(de, 42, 0), L"42");
        CHECK_TEXT(I18nTrimmed(de, 12.5, 2), L"12,5");
        CHECK_TEXT(I18nTrimmed(de, 12.004, 2), L"12");
        CHECK_TEXT(I18nTrimmed(de, 100, 2), L"100");  // A whole number keeps its zeros
        CHECK_TEXT(I18nTrimmed(de, 100, 0), L"100");
        CHECK_TEXT(I18nInteger(de, 999), L"999");
        CHECK_TEXT(I18nInteger(de, 1000), L"1.000");
        CHECK_TEXT(I18nInteger(de, 1234567), L"1.234.567");
        CHECK_TEXT(I18nInteger(de, -1234567), L"-1.234.567");
        CHECK_TEXT(I18nInteger(de, std::numeric_limits<unsigned long long>::max()), L"18.446.744.073.709.551.615");
        CHECK_TEXT(I18nInteger(de, std::numeric_limits<long long>::min()), L"-9.223.372.036.854.775.808");
        // fr-FR: narrow no-break spaces between groups.
        CHECK_TEXT(I18nInteger(fr, 1234567), L"1\u202F234\u202F567");
        CHECK_TEXT(I18nFixed(fr, -0.25, 2), L"-0,25");
        // hi-IN: three, then twos.
        CHECK_TEXT(I18nInteger(hi, 1234), L"1,234");
        CHECK_TEXT(I18nInteger(hi, 12345), L"12,345");
        CHECK_TEXT(I18nInteger(hi, 123456), L"1,23,456");
        CHECK_TEXT(I18nInteger(hi, 1234567), L"12,34,567");
        CHECK_TEXT(I18nInteger(hi, 123456789), L"12,34,56,789");
        CHECK_TEXT(I18nFixed(hi, 1234567.891, 2), L"1234567.89");
        // de-CH's apostrophe, grouping off, one group of three.
        CHECK_TEXT(I18nInteger(ch, 1234567), L"1\u2019234\u2019567");
        CHECK_TEXT(I18nInteger(none, 1234567), L"1234567");
        CHECK_TEXT(I18nInteger(one, 1234567), L"1234,567");
        CHECK_TEXT(I18nInteger(one, 999), L"999");
        // ASCII as to_chars writes it, in the region's format.
        CHECK_TEXT(I18nDigits(de, "-1234.5", true), L"-1.234,5");
        CHECK_TEXT(I18nDigits(de, "-1234.5", false), L"-1234,5");
        CHECK_TEXT(I18nDigits(hi, "-123456.75", true), L"-1,23,456.75");
        CHECK_TEXT(I18nDigits(I18nLocale(kI18nLanguages[0], I18nNumberFormatFrom(L"\u066B", L"' '", L"3;0")), "1234.5",
                              true),
                   L"1' '234\u066B5");
    });

    Run("i18n numbers shown as text: a plural selects by the number, which shows as the text given in '#' and "
        "{name}; empty text shows its digits", [] {
        const I18nLocale en, fr(kTestFr, TestNumbers(L"fr-FR")), es(kTestEs, TestNumbers(L"de-DE"));
        auto days = Tr(L"{n, plural, one{# day} other{# days}}");
        CHECK_TEXT(I18nText(en, days, {{L"n", 1234567, L"1,234,567"}}), L"1,234,567 days");
        CHECK_TEXT(I18nText(en, days, {{L"n", 1, L"1"}}), L"1 day");
        CHECK_TEXT(I18nText(en, days, {{L"n", -1, L"-1"}}), L"-1 day");
        CHECK_TEXT(I18nText(en, days, {{L"n", 3, L""}}), L"3 days");
        CHECK_TEXT(I18nText(en, Tr(L"{n} items, {m}"), {{L"n", 1234567, L"12,34,567"}, {L"m", 5}}), L"12,34,567 items, 5");
        // French: a million is many, which the translation leaves to other.
        CHECK_TEXT(I18nText(fr, days, {{L"n", 1000000, I18nInteger(fr, 1000000)}}), L"1\u202F000\u202F000 jours");
        CHECK_TEXT(I18nText(fr, days, {{L"n", 1, I18nInteger(fr, 1)}}), L"1 jour");
        // Spanish: a million takes many's "de".
        auto units = Tr(L"{n, plural, one{# unit} other{# units}}");
        CHECK_TEXT(I18nText(es, units, {{L"n", 1000000, I18nInteger(es, 1000000)}}), L"1.000.000 de unidades");
        CHECK_TEXT(I18nText(es, units, {{L"n", 2000, I18nInteger(es, 2000)}}), L"2.000 unidades");
        CHECK_TEXT(I18nText(es, units, {{L"n", 1, I18nInteger(es, 1)}}), L"1 unidad");
    });

    Run("taskbar amounts in a regional format: the decimal separator, never grouped; $, k and M kept", [] {
        const I18nLocale de = TestRegional(L"en", L"de-DE"), fr = TestRegional(L"de", L"fr-FR"),
                         hi = TestRegional(L"en", L"hi-IN");
        CHECK_TEXT(TaskbarAmountText(de, 12.34, true), L"$12,34");
        CHECK_TEXT(TaskbarAmountText(de, 999.994, true), L"$999,99");
        CHECK_TEXT(TaskbarAmountText(de, 999.996, true), L"$1k");
        CHECK_TEXT(TaskbarAmountText(de, 1500, true), L"$1,5k");
        CHECK_TEXT(TaskbarAmountText(de, 999.4, false), L"999");
        CHECK_TEXT(TaskbarAmountText(de, 1234, false), L"1,23k");
        CHECK_TEXT(TaskbarAmountText(de, 9995, false), L"9,99k");  // Three significant digits: 9.99 below 9.995
        CHECK_TEXT(TaskbarAmountText(de, 62500, false), L"62,5k");
        CHECK_TEXT(TaskbarAmountText(de, 110616, false), L"111k");
        CHECK_TEXT(TaskbarAmountText(de, 1250000, false), L"1,25M");
        CHECK_TEXT(TaskbarAmountText(de, 2.5e9, false), L"2500M");
        CHECK_TEXT(TaskbarAmountText(fr, 12.34, true), L"$12,34");
        CHECK_TEXT(TaskbarAmountText(hi, 12.34, true), L"$12.34");
        CHECK_TEXT(TaskbarAmountText(hi, 2.5e9, false), L"2500M");
        // Through the meter model: the extra usage bar's amount in the display's format.
        AccountData data = Fresh({25, -1, -1, 25});
        data.extraUsedAmount = 12.5;
        data.extraLimitAmount = 50;
        auto bar = [&](const I18nLocale& locale, std::wstring provider) {
            MeterDisplay display = MeterDisplayFor(With([](Settings& s) { s.showExtraBarAmounts = true; }), locale,
                                                   TestMeasure, kNow, 1, 48);
            return AccountMeterState(display, BarLayout::Stacked, Account(provider), data, 0, 0).quotas[3].text;
        };
        CHECK_TEXT(bar(de, L"anthropic"), L"$12,50");
        CHECK_TEXT(bar(I18nLocale(), L"anthropic"), L"$12.50");
        data.extraUsedAmount = 1500.5;
        data.extraLimitAmount = 6000;
        CHECK_TEXT(bar(fr, L"copilot"), L"1,5k");
    });

    Run("details flyout amounts in a regional format, as the flyout composes them: dollars to the cent, credits "
        "whole, GitHub Copilot's credits trimmed, never grouped; English and German", [] {
        for (auto [tag, spent, credits, copilot] :
             {std::tuple{L"en", L"$12,50 of $1234,00 spent", L"123456 left of 200000 credits",
                         L"1234,5 of 1500 AI credits used"},
              std::tuple{L"de", L"$12,50 von $1234,00 ausgegeben", L"123456 von 200000 Guthaben \u00FCbrig",
                         L"1234,5 von 1500 KI-Guthaben verbraucht"}}) {
            const I18nLocale locale = TestRegional(tag, L"de-DE");
            CHECK_TEXT(I18nText(locale, TrCtx(L"quota-amount", L"{used} of {limit} spent"),
                                {{L"used", L"$" + I18nFixed(locale, 12.5, 2)},
                                 {L"limit", L"$" + I18nFixed(locale, 1234, 2)}}),
                       spent);
            CHECK_TEXT(I18nText(locale, TrCtx(L"quota-amount", L"{left} left of {limit} credits"),
                                {{L"left", I18nFixed(locale, 123456, 0)}, {L"limit", I18nFixed(locale, 200000, 0)}}),
                       credits);
            std::wstring unit = std::wstring_view(tag) == L"en" ? L"AI credits" : L"KI-Guthaben";
            CHECK_TEXT(I18nText(locale, TrCtx(L"quota-amount", L"{used} of {limit} {unit} used"),
                                {{L"used", I18nTrimmed(locale, 1234.5, 2)}, {L"limit", I18nTrimmed(locale, 1500, 2)},
                                 {L"unit", unit}}),
                       copilot);
        }
    });

    Run("number boxes in a regional format: what a box shows parses back in every region; typed variants: a plain "
        "space for fr-FR's narrow no-break space, plain digits, groups in the region's sizes only", [] {
        SettingsNumberRange wide{-2000000000, 2000000000};
        auto parse = [&](std::wstring_view region, std::wstring_view text) {
            SettingsParsedNumber parsed = SettingsParseNumber(text, wide, TestNumbers(region));
            return parsed.ok ? parsed.value : -999;
        };
        for (std::wstring_view region :
             {L"en-US", L"de-DE", L"fr-FR", L"ru-RU", L"hi-IN", L"de-CH", L"no-grouping", L"one-group"}) {
            const I18nLocale locale(kI18nLanguages[0], TestNumbers(region));
            for (int value : {0, 7, 999, 1000, 1440, 3650, 12345, 123456, 1234567, 999999999, -1234567}) {
                CHECK(parse(region, I18nInteger(locale, value)) == value);
            }
        }
        CHECK_TEXT(I18nInteger(TestRegional(L"en", L"de-DE"), 3650), L"3.650");
        CHECK_TEXT(I18nInteger(TestRegional(L"en", L"fr-FR"), 3650), L"3\u202F650");
        CHECK_TEXT(I18nInteger(TestRegional(L"en", L"hi-IN"), 1000000000), L"1,00,00,00,000");
        // de-DE: dots group; its decimal comma is no separator.
        CHECK(parse(L"de-DE", L"1.440") == 1440 && parse(L"de-DE", L" 1.440 ") == 1440 && parse(L"de-DE", L"1440") == 1440);
        CHECK(parse(L"de-DE", L"1,440") == -999 && parse(L"de-DE", L"1 440") == -999 && parse(L"de-DE", L"14.40") == -999);
        // fr-FR: any space for its narrow no-break space, also typed as a plain or no-break space.
        CHECK(parse(L"fr-FR", L"1\u202F440") == 1440 && parse(L"fr-FR", L"1 440") == 1440 &&
              parse(L"fr-FR", L"1\u00A0440") == 1440 && parse(L"fr-FR", L"1440") == 1440);
        CHECK(parse(L"fr-FR", L"1.440") == -999 && parse(L"fr-FR", L"1,440") == -999 && parse(L"fr-FR", L"1 4400") == -999);
        CHECK(parse(L"fr-FR", L"-1 234 567") == -1234567 && parse(L"fr-FR", L"1  440") == -999);
        // ru-RU's no-break space, typed as a plain one.
        CHECK(parse(L"ru-RU", L"1\u00A0440") == 1440 && parse(L"ru-RU", L"1 440") == 1440);
        // hi-IN: three, then twos; western threes don't fit.
        CHECK(parse(L"hi-IN", L"12,34,567") == 1234567 && parse(L"hi-IN", L"1,23,456") == 123456 &&
              parse(L"hi-IN", L"34,567") == 34567 && parse(L"hi-IN", L"1234567") == 1234567);
        CHECK(parse(L"hi-IN", L"1,234,567") == -999 && parse(L"hi-IN", L"123,456") == -999);
        // de-CH: its apostrophe only.
        CHECK(parse(L"de-CH", L"1\u2019440") == 1440 && parse(L"de-CH", L"1'440") == -999);
        // Grouping off: digits only. One group: the rest of the digits before it, ungrouped.
        CHECK(parse(L"no-grouping", L"1440") == 1440 && parse(L"no-grouping", L"1,440") == -999);
        CHECK(parse(L"one-group", L"1234,567") == 1234567 && parse(L"one-group", L"1,567") == 1567 &&
              parse(L"one-group", L"1,234,567") == -999);
    });

    Run("number boxes in a regional format: the controller parses typed text and writes steps in the frame's "
        "format, typed text keeping its format across a regional change; a page's box shows its value in the "
        "region's groups", [] {
        TestStore store;
        store.settings.historyRetentionDays = 120;
        SettingsEdits edits(store);
        edits.Reload();
        const I18nLocale de = TestRegional(L"en", L"de-DE"), fr = TestRegional(L"de", L"fr-FR"),
                         hi = TestRegional(L"en", L"hi-IN");
        edits.SetText(0, kRetention, L"1.440", de);
        CHECK(edits.Projection().historyRetentionDays == 1440 && edits.Find(0, kRetention)->valid);
        edits.SetText(0, kRetention, L"1,440", de);  // de-DE's decimal comma: not a whole number
        CHECK(edits.Projection().historyRetentionDays == 1440 && !edits.Find(0, kRetention)->valid);
        edits.Revert(0, kRetention);
        edits.SetText(0, kRetention, L"2 000", fr);
        CHECK(edits.Projection().historyRetentionDays == 2000 && edits.Find(0, kRetention)->valid);
        edits.Step(0, kRetention, 1, de);
        CHECK(edits.Find(0, kRetention)->raw == L"2.001");
        edits.Step(0, kRetention, 1, fr);
        CHECK(edits.Find(0, kRetention)->raw == L"2\u202F002");
        edits.Step(0, kRetention, 1, hi);
        CHECK(edits.Find(0, kRetention)->raw == L"2,003" && edits.Projection().historyRetentionDays == 2003);
        CHECK(store.Applied().empty());
        // A regional change while text is typed: the text keeps parsing in the format the box showed
        // it in; a draft typed anew takes the frame's.
        edits.Revert(0, kRetention);
        edits.SetText(0, kRetention, L"1,440");
        edits.SetText(0, kRetention, L"1,441", de);
        CHECK(edits.Projection().historyRetentionDays == 1441 && edits.Find(0, kRetention)->valid);
        edits.Revert(0, kRetention);
        edits.SetText(0, kRetention, L"1.442", de);
        CHECK(edits.Projection().historyRetentionDays == 1442 && edits.Find(0, kRetention)->valid);
        // The History page's custom box: kept forever shows the most days.
        auto box = [](const I18nLocale& locale) {
            SettingsView view = TestView(SettingsUiPage::History);
            view.locale = locale;
            view.settings.historyRetentionDays = 0;
            UiFrame frame;
            SettingsUiBuildFrame(TestGraphics(), view, {1000, 820}, frame);
            return Control(frame, kRetention).text;
        };
        CHECK_TEXT(box(I18nLocale()), L"3,650");
        CHECK_TEXT(box(de), L"3.650");
        CHECK_TEXT(box(fr), L"3\u202F650");
        CHECK_TEXT(box(TestRegional(L"en", L"no-grouping")), L"3650");
    });
}
