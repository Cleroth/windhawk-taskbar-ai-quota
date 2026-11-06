// Localization of account errors, notifications, the sign-in window's text and the status pages'
// and providers' mod-authored data on the production catalogs: every coded account error's English
// in both forms (as the mod wrote it before codes), coded errors in German, Japanese and Russian
// with their English log rendering, causes nested with provider text and HTTP statuses, threshold
// notifications' titles and bodies per quota and their toast XML (escaped once), the sign-in
// window's title, instructions and status lines, an outage's incident (the mod's reasons and the
// page's own title, with the models after them) and Antigravity's Argon current limit. Expected
// text is written out by hand from the production catalogs.

static void RunI18nErrorTests() {
    using enum AccountErrorCode;
    const I18nLocale de = TestLocale(L"de"), ja = TestLocale(L"ja"), ru = TestLocale(L"ru"), fr = TestLocale(L"fr"),
                     zh = TestLocale(L"zh-Hans");
    auto alone = [](const I18nLocale& locale, const AccountError& error) {
        return AccountErrorText(locale, error, AccountErrorForm::Alone);
    };

    Run("account errors: each code's English inline as the mod wrote it, and alone with its first letter "
        "capitalized, as the Accounts page showed it", [&] {
        const std::pair<AccountError, std::wstring_view> cases[] = {
            {{NotSignedIn}, L"not signed in - click to sign in"},
            {{SessionExpired}, L"session expired - click to sign in"},
            {{Unauthorized}, L"unauthorized - click to sign in"},
            {{TokenRefreshFailed}, L"token refresh failed"},
            {{TokenSaveFailed}, L"could not save refreshed token"},
            {{Network}, L"network error"},
            {{.code = Http, .status = 502}, L"HTTP 502"},
            {{RateLimited}, L"rate limited by API"},
            {{NoCopilotAccess}, L"no Copilot access on this GitHub account"},
            {{NoOpenCodeSubscription}, L"no OpenCode Go subscription on this key"},
            {{UnexpectedResponse}, L"unexpected response format"},
            {{FetchThreadFailed}, L"fetch thread failed"},
            {{AntigravityNotRunning}, L"Antigravity not running"},
            {{AntigravityUnavailable}, L"Antigravity quota unavailable"},
            {{LanguageServerNetwork}, L"network error (language server)"},
            {{.code = LanguageServerHttp, .status = 404}, L"HTTP 404 from language server"},
            {{LanguageServerResponse}, L"unexpected response"},
            {{NoQuotaGroups}, L"no quota groups in response"},
            {{QuotaGroupsNotArray}, L"groups is not an array"},
            {{NoUsableQuotaBuckets}, L"no usable Antigravity quota buckets in response"},
            {{QuotaSummaryInvalid}, L"failed to parse quota summary JSON"},
            {{NoUserStatus}, L"no userStatus object"},
            {{UserStatusInvalid}, L"failed to parse user status JSON"},
            {{NoUsableQuota}, L"no usable Antigravity quota in response"},
        };
        for (const auto& [error, english] : cases) {
            CHECK_TEXT(AccountErrorText({}, error), std::wstring(english));
            std::wstring capitalized(english);
            capitalized[0] = wchar_t(towupper(capitalized[0]));
            CHECK_TEXT(alone({}, error), capitalized);
        }
        // The token endpoints' failures read as the sign-in log wrote them; the sign-in window's
        // retries take their alone form.
        CHECK_TEXT(AccountErrorText({}, {NoAccessToken}), L"no access token in response");
        CHECK_TEXT(AccountErrorText({}, {InvalidTokenResponse}), L"invalid token response");
        CHECK_TEXT(AccountErrorText({}, {UnexpectedTokenResponse}), L"unexpected token response");
        CHECK_TEXT(AccountErrorText({}, {InternalError}), L"internal error");
        CHECK_TEXT(alone({}, {InvalidTokenResponse}), L"Invalid token response");
        CHECK_TEXT(alone({}, {UnexpectedTokenResponse}), L"Unexpected token response");
        // Provider text shows as is in either form.
        CHECK_TEXT(alone({}, {Message, L"invalid_grant"}), L"invalid_grant");
    });

    Run("account errors in German, Japanese and Russian, inline and alone; the log's English whatever the "
        "language", [&] {
        // nicht angemeldet – zum Anmelden klicken; Nicht angemeldet – zum Anmelden klicken.
        CHECK_TEXT(AccountErrorText(de, {NotSignedIn}), L"nicht angemeldet \u2013 zum Anmelden klicken");
        CHECK_TEXT(alone(de, {NotSignedIn}), L"Nicht angemeldet \u2013 zum Anmelden klicken");
        // A noun: both forms alike (Netzwerkfehler (Sprachserver)).
        CHECK_TEXT(AccountErrorText(de, {LanguageServerNetwork}), L"Netzwerkfehler (Sprachserver)");
        CHECK_TEXT(alone(de, {LanguageServerNetwork}), L"Netzwerkfehler (Sprachserver)");
        CHECK_TEXT(AccountErrorText(de, {.code = LanguageServerHttp, .status = 404}), L"HTTP 404 vom Sprachserver");
        // Japanese has one form: サインインしていません - クリックしてサインイン; 言語サーバーから HTTP 404.
        CHECK_TEXT(AccountErrorText(ja, {NotSignedIn}), L"\u30B5\u30A4\u30F3\u30A4\u30F3\u3057\u3066\u3044\u307E\u305B"
                                                        L"\u3093 - \u30AF\u30EA\u30C3\u30AF\u3057\u3066\u30B5\u30A4"
                                                        L"\u30F3\u30A4\u30F3");
        CHECK_TEXT(alone(ja, {NotSignedIn}), AccountErrorText(ja, {NotSignedIn}));
        CHECK_TEXT(AccountErrorText(ja, {.code = LanguageServerHttp, .status = 404}), L"\u8A00\u8A9E\u30B5\u30FC\u30D0"
                                                                                      L"\u30FC\u304B\u3089 HTTP 404");
        // сеанс истёк — нажмите, чтобы войти; Сеанс истёк — нажмите, чтобы войти; HTTP 429.
        CHECK_TEXT(AccountErrorText(ru, {SessionExpired}), L"\u0441\u0435\u0430\u043D\u0441 \u0438\u0441\u0442\u0451"
                                                           L"\u043A \u2014 \u043D\u0430\u0436\u043C\u0438\u0442\u0435, "
                                                           L"\u0447\u0442\u043E\u0431\u044B \u0432\u043E\u0439\u0442"
                                                           L"\u0438");
        CHECK_TEXT(alone(ru, {SessionExpired}), L"\u0421\u0435\u0430\u043D\u0441 \u0438\u0441\u0442\u0451\u043A \u2014 "
                                                L"\u043D\u0430\u0436\u043C\u0438\u0442\u0435, \u0447\u0442\u043E\u0431"
                                                L"\u044B \u0432\u043E\u0439\u0442\u0438");
        CHECK_TEXT(AccountErrorText(ru, {.code = Http, .status = 429}), L"HTTP 429");
        // The fetch log and its dedupe key: English, so a language switch never logs anew.
        const AccountError error{SessionExpired};
        CHECK_TEXT(AccountErrorText(I18nLocale(), error), L"session expired - click to sign in");
        CHECK(AccountErrorText(I18nLocale(), error) != AccountErrorText(ru, error));
        // Codes compare, never text: a status changes the error.
        CHECK((AccountError{.code = Http, .status = 500} != AccountError{.code = Http, .status = 502}));
    });

    Run("account errors with a cause: provider text and statuses nested as is, the cause inline inside "
        "either form, in English and other languages", [&] {
        const AccountError provider{TokenRefreshFailed, L"invalid_grant: Refresh token expired", 0, Message};
        CHECK_TEXT(AccountErrorText({}, provider), L"token refresh failed: invalid_grant: Refresh token expired");
        CHECK_TEXT(alone({}, provider), L"Token refresh failed: invalid_grant: Refresh token expired");
        // échec de l’actualisation du jeton : invalid_grant: …; Не удалось обновить токен: invalid_grant: ….
        CHECK_TEXT(AccountErrorText(fr, provider),
                   L"\u00E9chec de l\u2019actualisation du jeton\u202F: invalid_grant: Refresh token expired");
        CHECK_TEXT(alone(ru, provider), L"\u041D\u0435 \u0443\u0434\u0430\u043B\u043E\u0441\u044C \u043E\u0431\u043D"
                                        L"\u043E\u0432\u0438\u0442\u044C \u0442\u043E\u043A\u0435\u043D: invalid_grant:"
                                        L" Refresh token expired");
        // Token-Aktualisierung fehlgeschlagen: HTTP 503; トークンの更新に失敗しました: ネットワーク エラー.
        CHECK_TEXT(AccountErrorText(de, {.code = TokenRefreshFailed, .status = 503, .cause = Http}),
                   L"Token-Aktualisierung fehlgeschlagen: HTTP 503");
        CHECK_TEXT(AccountErrorText(ja, {.code = TokenRefreshFailed, .cause = Network}),
                   L"\u30C8\u30FC\u30AF\u30F3\u306E\u66F4\u65B0\u306B\u5931\u6557\u3057\u307E\u3057\u305F: \u30CD\u30C3"
                   L"\u30C8\u30EF\u30FC\u30AF \u30A8\u30E9\u30FC");
        CHECK_TEXT(AccountErrorText({}, {.code = TokenRefreshFailed, .cause = InternalError}),
                   L"token refresh failed: internal error");
        CHECK_TEXT(AccountErrorText({}, {.code = TokenRefreshFailed, .cause = NoAccessToken}),
                   L"token refresh failed: no access token in response");
        // An unexpected response's shape: its keys as listed.
        const AccountError keys{UnexpectedResponse, L"five_hour, seven_day, ...", 0, BodyKeys};
        CHECK_TEXT(AccountErrorText({}, keys), L"unexpected response format (keys: five_hour, seven_day, ...)");
        CHECK_TEXT(alone({}, keys), L"Unexpected response format (keys: five_hour, seven_day, ...)");
        // unerwartetes Antwortformat (Schlüssel: …); 意外的响应格式（键：…）.
        CHECK_TEXT(AccountErrorText(de, keys), L"unerwartetes Antwortformat (Schl\u00FCssel: five_hour, seven_day, ...)");
        CHECK_TEXT(AccountErrorText(zh, keys), L"\u610F\u5916\u7684\u54CD\u5E94\u683C\u5F0F\uFF08\u952E\uFF1Afive_hour,"
                                               L" seven_day, ...\uFF09");
        CHECK_TEXT(AccountErrorText({}, {.code = UnexpectedResponse, .cause = EmptyBody}),
                   L"unexpected response format (empty body)");
        CHECK_TEXT(AccountErrorText({}, {.code = UnexpectedResponse, .cause = NoBodyKeys}),
                   L"unexpected response format (JSON object with no keys)");
        CHECK_TEXT(AccountErrorText({}, {.code = UnexpectedResponse, .cause = InvalidJsonBody}),
                   L"unexpected response format (non-object or invalid JSON body)");
    });

    Run("threshold notifications: each quota's title and the body in English as before, then German, "
        "Russian and Japanese; percentages as %.0f", [&] {
        auto text = [](const I18nLocale& locale, std::wstring_view provider, int slot, double pct,
                       ULONGLONG durationMs = 0, CopilotUnit unit = CopilotUnit::AiCredits,
                       std::wstring resets = L"") {
            AccountData data;
            std::array<WindowUsage*, kQuotaBarCount> windows = {&data.win5h, &data.winWeek, &data.fableWeek,
                                                                &data.extraUsage};
            *windows[size_t(slot)] = {pct, 0, durationMs};
            data.copilotUnit = unit;
            return ThresholdNotificationText(locale, std::wstring(provider), data, slot, resets);
        };
        auto title = [&](std::wstring_view provider, int slot, ULONGLONG durationMs = 0,
                         CopilotUnit unit = CopilotUnit::AiCredits) {
            return text({}, provider, slot, 91.4, durationMs, unit).first;
        };
        constexpr ULONGLONG hour = 60ULL * 60 * 1000;
        CHECK_TEXT(title(L"anthropic", kFiveHourBar), L"5h usage at 91%");
        CHECK_TEXT(title(L"anthropic", kWeeklyBar), L"weekly usage at 91%");
        CHECK_TEXT(title(L"anthropic", kFableWeeklyBar), L"Fable weekly usage at 91%");
        CHECK_TEXT(title(L"anthropic", kExtraUsageBar), L"monthly extra usage at 91%");
        CHECK_TEXT(title(L"openai", kExtraUsageBar), L"credits usage at 91%");
        CHECK_TEXT(title(L"opencode", kExtraUsageBar), L"monthly usage at 91%");
        CHECK_TEXT(title(L"copilot", kExtraUsageBar), L"AI credits usage at 91%");
        CHECK_TEXT(title(L"copilot", kExtraUsageBar, 0, CopilotUnit::PremiumRequests), L"premium requests usage at 91%");
        CHECK_TEXT(title(L"copilot", kExtraUsageBar, 0, CopilotUnit::ChatRequests), L"chat requests usage at 91%");
        CHECK_TEXT(title(L"antigravity", kFiveHourBar, 3 * hour), L"Gemini 3h usage at 91%");
        CHECK_TEXT(title(L"antigravity", kFiveHourBar, hour), L"Gemini 1h usage at 91%");
        CHECK_TEXT(title(L"antigravity", kFiveHourBar, 5 * hour), L"Gemini 5h usage at 91%");
        CHECK_TEXT(title(L"antigravity", kFiveHourBar, 0), L"Gemini current limit usage at 91%");
        CHECK_TEXT(title(L"antigravity", kWeeklyBar), L"Gemini weekly usage at 91%");
        CHECK_TEXT(title(L"antigravity", kFableWeeklyBar), L"Argon weekly usage at 91%");
        CHECK_TEXT(title(L"antigravity", kExtraUsageBar), L"monthly extra usage at 91%");
        // %.0f: 99.6 rounds up; a reported percentage far past 100 keeps every digit.
        CHECK_TEXT(text({}, L"openai", kWeeklyBar, 99.6).first, L"weekly usage at 100%");
        CHECK_TEXT(text({}, L"anthropic", kFiveHourBar, 1e20).first, L"5h usage at 100000000000000000000%");
        // The body: the provider, then its reset when given.
        auto [enTitle, enBody] = text({}, L"anthropic", kFiveHourBar, 91.4, 0, CopilotUnit::AiCredits,
                                      L"in 2h 5m (14:05)");
        CHECK_TEXT(enBody, L"Anthropic - resets in 2h 5m (14:05)");
        CHECK_TEXT(text({}, L"copilot", kExtraUsageBar, 91.4).second, L"GitHub Copilot");
        // 5 Std.: 91% genutzt / Anthropic – Reset in 2 Std. 5 Min. (14:05), a no-break space before
        // each unit.
        auto [deTitle, deBody] = text(de, L"anthropic", kFiveHourBar, 91.4, 0, CopilotUnit::AiCredits,
                                      L"in 2\u00A0Std. 5\u00A0Min. (14:05)");
        CHECK_TEXT(deTitle, L"5\u00A0Std.: 91% genutzt");
        CHECK_TEXT(deBody, L"Anthropic \u2013 Reset in 2\u00A0Std. 5\u00A0Min. (14:05)");
        // Премиум-запросы: использовано 95%; Argon, неделя: использовано 95%.
        CHECK_TEXT(text(ru, L"copilot", kExtraUsageBar, 95, 0, CopilotUnit::PremiumRequests).first,
                   L"\u041F\u0440\u0435\u043C\u0438\u0443\u043C-\u0437\u0430\u043F\u0440\u043E\u0441\u044B: \u0438"
                   L"\u0441\u043F\u043E\u043B\u044C\u0437\u043E\u0432\u0430\u043D\u043E 95%");
        CHECK_TEXT(text(ru, L"antigravity", kFableWeeklyBar, 95).first, L"Argon, \u043D\u0435\u0434\u0435\u043B\u044F: "
                                                                        L"\u0438\u0441\u043F\u043E\u043B\u044C\u0437"
                                                                        L"\u043E\u0432\u0430\u043D\u043E 95%");
        // Gemini 3 時間: 使用率 80% / OpenAI - 2時間後にリセット.
        auto [jaTitle, jaBody] = text(ja, L"openai", kFiveHourBar, 80, 0, CopilotUnit::AiCredits, L"2\u6642\u9593\u5F8C");
        CHECK_TEXT(text(ja, L"antigravity", kFiveHourBar, 80, 3 * hour).first, L"Gemini 3 \u6642\u9593: \u4F7F\u7528"
                                                                               L"\u7387 80%");
        CHECK_TEXT(jaTitle, L"5 \u6642\u9593: \u4F7F\u7528\u7387 80%");
        CHECK_TEXT(jaBody, L"OpenAI - 2\u6642\u9593\u5F8C\u306B\u30EA\u30BB\u30C3\u30C8");
    });

    Run("threshold notifications: the toast's XML escapes its text and link once, in any language", [&] {
        auto [title, body] = ThresholdNotificationText(fr, L"anthropic", [] {
            AccountData data;
            data.win5h = {91.4, 0, 0};
            return data;
        }(), kFiveHourBar, L"dans 2\u00A0h\u00A05\u00A0min (14:05)");
        // 5 h : utilisation à 91% / Anthropic – réinitialisation dans 2 h 5 min (14:05): no-break spaces
        // in the units, a narrow one before the colon.
        CHECK_TEXT(title, L"5\u00A0h\u202F: utilisation \u00E0 91%");
        CHECK_TEXT(body, L"Anthropic \u2013 r\u00E9initialisation dans 2\u00A0h\u00A05\u00A0min (14:05)");
        CHECK_TEXT(ThresholdToastXml(title, body, L"Au rythme <24 h> & \"plus\"", L"https://claude.ai/?a=1&b=2"),
                   L"<toast activationType=\"protocol\" launch=\"https://claude.ai/?a=1&amp;b=2\"><visual>"
                   L"<binding template=\"ToastGeneric\"><text>5\u00A0h\u202F: utilisation \u00E0 91%</text>"
                   L"<text>Anthropic \u2013 r\u00E9initialisation dans 2\u00A0h\u00A05\u00A0min (14:05)</text>"
                   L"<text>Au rythme &lt;24 h&gt; &amp; &quot;plus&quot;</text></binding></visual></toast>");
        // Without a link or projection; already escaped text is escaped again, as text.
        CHECK_TEXT(ThresholdToastXml(L"A &amp; B", L"x", L"", nullptr),
                   L"<toast><visual><binding template=\"ToastGeneric\"><text>A &amp;amp; B</text><text>x</text>"
                   L"</binding></visual></toast>");
    });

    Run("sign-in window: titles and instructions as before in English; German and Russian with the Use a "
        "code button named as it reads; the device flow's statuses and retries", [&] {
        auto signInTitle = [](const I18nLocale& locale, std::wstring_view provider) {
            return I18nText(locale, TrCtx(L"sign-in-window", L"Sign in: {label} ({provider})"),
                            {{L"label", L"Work & play"}, {L"provider", ProviderDisplayName(std::wstring(provider))}});
        };
        CHECK_TEXT(signInTitle({}, L"anthropic"), L"Sign in: Work & play (Anthropic)");
        CHECK_TEXT(signInTitle({}, L"openai"), L"Sign in: Work & play (OpenAI)");
        CHECK_TEXT(signInTitle({}, L"copilot"), L"Sign in: Work & play (GitHub Copilot)");
        CHECK_TEXT(signInTitle({}, L"opencode"), L"Sign in: Work & play (OpenCode Go)");
        CHECK_TEXT(signInTitle(de, L"copilot"), L"Anmelden: Work & play (GitHub Copilot)");
        CHECK_TEXT(signInTitle(fr, L"openai"), L"Connexion\u202F: Work & play (OpenAI)");
        auto loopback = [](const I18nLocale& locale) {
            return I18nText(locale,
                            TrCtx(L"sign-in-window",
                                  L"Open the sign-in link in a browser on this PC and approve access on claude.ai. This"
                                  L" "
                                  L"window closes once sign-in completes. On another device, choose {button}."),
                            {{L"button", I18nText(locale, TrCtx(L"sign-in-button", L"Use a code"))}});
        };
        CHECK_TEXT(loopback({}), L"Open the sign-in link in a browser on this PC and approve access on claude.ai. This "
                                 L"window closes once sign-in completes. On another device, choose Use a code.");
        // …На другом устройстве выберите «Ввести код».
        std::wstring russian = loopback(ru);
        CHECK(russian.starts_with(L"\u041E\u0442\u043A\u0440\u043E\u0439\u0442\u0435 \u0441\u0441\u044B\u043B\u043A"
                                  L"\u0443 \u0434\u043B\u044F \u0432\u0445\u043E\u0434\u0430 \u0432 \u0431\u0440\u0430"
                                  L"\u0443\u0437\u0435\u0440\u0435 \u043D\u0430 \u044D\u0442\u043E\u043C \u041F\u041A") &&
              russian.ends_with(L"\u041D\u0430 \u0434\u0440\u0443\u0433\u043E\u043C \u0443\u0441\u0442\u0440\u043E"
                                L"\u0439\u0441\u0442\u0432\u0435 \u0432\u044B\u0431\u0435\u0440\u0438\u0442\u0435 "
                                L"\u00AB\u0412\u0432\u0435\u0441\u0442\u0438 \u043A\u043E\u0434\u00BB."));
        CHECK(loopback(de).ends_with(L"Um sich auf einem anderen Ger\u00E4t anzumelden, w\u00E4hlen Sie \u201ECode "
                                     L"verwenden\u201C."));
        auto retrying = [](const I18nLocale& locale, const AccountError& failure) {
            return I18nText(locale, TrCtx(L"sign-in-status", L"{reason}, retrying\u2026"),
                            {{L"reason", AccountErrorText(locale, failure, AccountErrorForm::Alone)}});
        };
        CHECK_TEXT(retrying({}, {Network}), L"Network error, retrying\u2026");
        CHECK_TEXT(retrying({}, {.code = Http, .status = 503}), L"HTTP 503, retrying\u2026");
        CHECK_TEXT(retrying({}, {InvalidTokenResponse}), L"Invalid token response, retrying\u2026");
        CHECK_TEXT(retrying({}, {UnexpectedTokenResponse}), L"Unexpected token response, retrying\u2026");
        // ネットワーク エラー。再試行しています…; Ошибка сети — повторная попытка….
        CHECK_TEXT(retrying(ja, {Network}), L"\u30CD\u30C3\u30C8\u30EF\u30FC\u30AF \u30A8\u30E9\u30FC\u3002\u518D\u8A66"
                                            L"\u884C\u3057\u3066\u3044\u307E\u3059\u2026");
        CHECK_TEXT(retrying(ru, {Network}), L"\u041E\u0448\u0438\u0431\u043A\u0430 \u0441\u0435\u0442\u0438 \u2014 "
                                            L"\u043F\u043E\u0432\u0442\u043E\u0440\u043D\u0430\u044F \u043F\u043E\u043F"
                                            L"\u044B\u0442\u043A\u0430\u2026");
        CHECK_TEXT(I18nText(de, TrCtx(L"sign-in-status", L"Waiting for approval on GitHub\u2026")),
                   L"Warten auf Best\u00E4tigung auf GitHub\u2026");
        CHECK_TEXT(I18nText({}, TrCtx(L"sign-in-status", L"The code expired. Close this window and sign in again.")),
                   L"The code expired. Close this window and sign in again.");
    });

    Run("status pages: the mod's reasons in the reader's language, the page's title as written, the models "
        "after either; English as before", [&] {
        ProviderOutage outage{OutageLevel::Degraded, L"OpenCode Go", 0, L"", 0, OutageReason::Errors, L"glm-5.3-flash"};
        CHECK_TEXT(ProviderOutageIncident({}, outage), L"Elevated errors: glm-5.3-flash");
        // Taux d’erreurs élevé : glm-5.3-flash; 上游提供商问题.
        CHECK_TEXT(ProviderOutageIncident(fr, outage), L"Taux d\u2019erreurs \u00E9lev\u00E9\u202F: glm-5.3-flash");
        CHECK_TEXT(ProviderOutageIncident(zh, {.reason = OutageReason::Upstream}), L"\u4E0A\u6E38\u63D0\u4F9B\u5546"
                                                                                   L"\u95EE\u9898");
        CHECK_TEXT(ProviderOutageIncident({}, {.reason = OutageReason::Latency}), L"Slow responses");
        CHECK_TEXT(ProviderOutageIncident({}, {.reason = OutageReason::Upstream}), L"Upstream provider issues");
        CHECK_TEXT(ProviderOutageIncident({}, {.reason = OutageReason::Availability}), L"Availability issues");
        // The page's own text stays as written in every language.
        CHECK_TEXT(ProviderOutageIncident(ru, {.title = L"capacity", .models = L"a, b"}), L"capacity: a, b");
        CHECK_TEXT(ProviderOutageIncident(ja, {.title = L"Database upgrade"}), L"Database upgrade");
        CHECK_TEXT(ProviderOutageIncident(de, {.models = L"glm-5.3-flash"}), L"glm-5.3-flash");
        CHECK(ProviderOutageIncident(de, {}).empty());
    });

    Run("Antigravity's Argon current limit: named at display in the reader's language, merging as one "
        "pool apart from a reported Argon quota", [&] {
        AccountData data;
        AddExtraQuota(&data, L"Argon", {30, 0, 0}, true);
        AddExtraQuota(&data, L"Argon", {45, 0, 0}, true);
        AddExtraQuota(&data, L"Argon", {10, 0, 0});
        CHECK(data.extraQuotas.size() == 2 && data.extraQuotas[0].usage.pct == 45 && data.extraQuotas[0].currentLimit &&
              !data.extraQuotas[1].currentLimit);
        CHECK_TEXT(ExtraQuotaName({}, data.extraQuotas[0]), L"Argon current limit");
        CHECK_TEXT(ExtraQuotaName({}, data.extraQuotas[1]), L"Argon");
        // Argon: текущий лимит; Argon aktuelles Limit.
        CHECK_TEXT(ExtraQuotaName(ru, data.extraQuotas[0]), L"Argon: \u0442\u0435\u043A\u0443\u0449\u0438\u0439 \u043B"
                                                            L"\u0438\u043C\u0438\u0442");
        CHECK_TEXT(ExtraQuotaName(de, data.extraQuotas[0]), L"Argon aktuelles Limit");
    });
}
