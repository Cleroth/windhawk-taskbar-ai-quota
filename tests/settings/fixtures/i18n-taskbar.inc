// Localization of the taskbar's meter model and its coded errors on the production catalogs: the
// quota keys and the headline countdown (its forms and its width reserve) in Simplified Chinese,
// Russian and the pseudo-locale, the settings preview strip drawing them, an account error's
// text per language with its English log rendering, and the Accounts page's state for the coded
// error. Expected text is written out by hand from the production catalogs and the pseudo-locale's
// rules.

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
    MeterDisplay display = MeterDisplayFor(settings, locale, kNow, 1, 48);
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
        MeterDisplay zh = MeterDisplayFor(settings, TestLocale(L"zh-Hans"), kNow, 1, 48);
        AccountMeters bars = AccountMeterState(zh, BarLayout::Stacked, Account(L"openai"), data, 0, 0);
        CHECK(bars.quotas[0].key == L"5h" && bars.quotas[1].key == L"7\u5929" && bars.quotas[3].key == L"\u79EF\u5206");
        AccountMeters headline = AccountMeterState(zh, BarLayout::LogoHeadline, Account(L"openai"), data, 0, 0);
        CHECK(headline.headline == L"80%" && headline.headlineKey == L"7\u5929");
        MeterDisplay ru = MeterDisplayFor(settings, TestLocale(L"ru"), kNow, 1, 48);
        AccountMeters matrix = AccountMeterState(ru, BarLayout::QuotaMatrix, Account(L"copilot"), data, 0, 0);
        CHECK(matrix.quotas[0].key == L"5\u0447" && matrix.quotas[3].key == L"\u041C\u0441");
        // OpenAI's credits in the weekly chart's corner: their key ("Cr").
        data.hasCredits = true;
        data.creditsBalance = 400;
        data.extraUsage = {};
        Settings spark;
        spark.sparklineExtra = SparklineExtra::Amount;
        MeterDisplay pseudo = MeterDisplayFor(spark, TestLocale(L"qps-ploc"), kNow, 1, 48);
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

    Run("headline countdown reserve: the hours-and-minutes form at zeros in the language, English as the "
        "sample it replaces", [] {
        CHECK(HeadlineCountdownReserve({}) == L"\u21BB 00h 00m");
        CHECK(HeadlineCountdownReserve(TestLocale(L"zh-Hans")) == L"\u21BB 00h 00m");
        CHECK(HeadlineCountdownReserve(TestLocale(L"ru")) == L"\u21BB 00\u0447 00\u043C");
        CHECK(HeadlineCountdownReserve(TestLocale(L"qps-ploc")) == L"\u21BB [00\u0125 00\u0271 \u013C]");
        CHECK(HeadlineCountdownReserve(TestLocale(L"de")) == L"\u21BB 00h 00m");
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

    Run("accounts page: the coded error's state reads Local app not running in the page's language; an "
        "uncoded message starts with a capital as before; key chips localized", [] {
        auto stateOf = [](const UiFrame& frame) {
            std::vector<std::wstring> states;
            for (const UiItem& item : frame.content) {
                if (item.kind == UiItemKind::Text && item.runs.size() == 2 && item.runs[1].ink == UiInk::Caution) {
                    states.push_back(item.runs[1].text);
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
        // Another error of the local app: its message, capitalized and cut at 60 characters as before.
        CHECK(stateOf(frameOf({}, {AccountErrorCode::Message, L"network error (language server)"})).back() ==
              L"Network error (language server)");
        CHECK(stateOf(frameOf(TestLocale(L"zh-Hans"), {AccountErrorCode::Message, std::wstring(70, L'x')})).back() ==
              L"X" + std::wstring(58, L'x') + L"\u2026");
        // The quota chips in the card headers: OAI's 5h and 7d (5h, 7天).
        UiFrame chinese = frameOf(TestLocale(L"zh-Hans"), {});
        std::vector<std::wstring> chips;
        for (const UiItem& item : chinese.content) {
            if (item.kind == UiItemKind::Chip) chips.push_back(item.text);
        }
        CHECK(std::find(chips.begin(), chips.end(), L"7\u5929") != chips.end() &&
              std::find(chips.begin(), chips.end(), L"7d") == chips.end());
    });
}
