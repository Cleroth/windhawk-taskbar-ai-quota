// Antigravity daemon discovery helpers (providers/antigravity.h): app_data_dir sanitization and
// daemon JSON parsing/validation.

static void RunAntigravityDaemonTests() {
    Run("antigravity app_data_dir: valid single relative segments stay; unsafe ones fall back", [] {
        for (const wchar_t* valid : {L"antigravity", L"antigravity-dev", L"custom_dir", L"antigravity.beta"}) {
            CHECK(SanitizeAntigravityAppDataDir(valid) == valid);
        }
        CHECK(SanitizeAntigravityAppDataDir(L"") == L"antigravity");
        for (const wchar_t* bad : {
                 L".", L"..", L"../other", L"a..b", L"a\\b", L"a/b", L"C:antigravity",
                 L"foo*", L"foo?", L"foo\"bar", L"foo<bar", L"foo>bar", L"foo|bar",
                 L"foo.", L"foo ", L"foo\nbar", L"foo\rbar", L"foo\tbar", L"foo\x01" L"bar",
                 L"foo\x7f" L"bar"}) {
            CHECK(SanitizeAntigravityAppDataDir(bad) == L"antigravity");
        }
        CHECK(SanitizeAntigravityAppDataDir(std::wstring(128, L'a')) == std::wstring(128, L'a'));
        CHECK(SanitizeAntigravityAppDataDir(std::wstring(129, L'a')) == L"antigravity");
    });

    Run("antigravity daemon JSON: valid desktop and CLI records match PID and listening ports", [] {
        const std::vector<int> ports = {58320, 58321, 58322};
        int httpsPort = -1;
        int httpPort = -1;
        std::wstring token = L"stale";

        // language_server.exe schema
        CHECK(ParseAntigravityDaemonJson(
            R"({"pid":20296,"httpsPort":58320,"httpPort":58321,"lsVersion":"2.35.2","csrfToken":"tok-desktop"})",
            20296, ports, &httpsPort, &httpPort, &token));
        CHECK(httpsPort == 58320 && httpPort == 58321 && token == L"tok-desktop");

        // agy.exe schema (includes lspPort)
        CHECK(ParseAntigravityDaemonJson(
            R"({"pid":4012,"httpsPort":58320,"httpPort":58321,"lspPort":58322,"lsVersion":"1.5.18","csrfToken":"tok-cli"})",
            4012, ports, &httpsPort, &httpPort, &token));
        CHECK(httpsPort == 58320 && httpPort == 58321 && token == L"tok-cli");

        // Single-protocol records (HTTPS-only or HTTP-only)
        CHECK(ParseAntigravityDaemonJson(
            R"({"pid":4012,"httpsPort":58320,"csrfToken":"tok-https"})",
            4012, {58320}, &httpsPort, &httpPort, &token));
        CHECK(httpsPort == 58320 && httpPort == 0 && token == L"tok-https");

        CHECK(ParseAntigravityDaemonJson(
            R"({"pid":4012,"httpsPort":0,"httpPort":58321,"csrfToken":"tok-http"})",
            4012, {58321}, &httpsPort, &httpPort, &token));
        CHECK(httpsPort == 0 && httpPort == 58321 && token == L"tok-http");
    });

    Run("antigravity daemon JSON: rejects PID mismatches, unverified ports, bad tokens and bad JSON", [] {
        const std::vector<int> ports = {58320, 58321};
        int httpsPort = 99;
        int httpPort = 99;
        std::wstring token = L"stale";

        // Partial port intersection keeps only the verified listening port.
        CHECK(ParseAntigravityDaemonJson(
            R"({"pid":20296,"httpsPort":58320,"httpPort":19999,"csrfToken":"tok-partial"})",
            20296, ports, &httpsPort, &httpPort, &token));
        CHECK(httpsPort == 58320 && httpPort == 0 && token == L"tok-partial");

        auto rejects = [&](const std::string& body, DWORD pid, const std::vector<int>& listening) {
            httpsPort = 99;
            httpPort = 99;
            token = L"stale";
            bool ok = ParseAntigravityDaemonJson(body, pid, listening, &httpsPort, &httpPort, &token);
            return !ok && httpsPort == 0 && httpPort == 0 && token.empty();
        };

        // PID checks
        CHECK(rejects(R"({"pid":20297,"httpsPort":58320,"httpPort":58321,"csrfToken":"tok"})", 20296, ports));
        CHECK(rejects(R"({"pid":20296.5,"httpsPort":58320,"httpPort":58321,"csrfToken":"tok"})", 20296, ports));
        CHECK(rejects(R"({"pid":"20296","httpsPort":58320,"httpPort":58321,"csrfToken":"tok"})", 20296, ports));
        CHECK(rejects(R"({"pid":0,"httpsPort":58320,"httpPort":58321,"csrfToken":"tok"})", 0, ports));
        CHECK(rejects(R"({"pid":-1,"httpsPort":58320,"httpPort":58321,"csrfToken":"tok"})", 20296, ports));
        CHECK(rejects(R"({"pid":4294967296,"httpsPort":58320,"httpPort":58321,"csrfToken":"tok"})", 20296, ports));
        CHECK(rejects(R"({"httpsPort":58320,"httpPort":58321,"csrfToken":"tok"})", 20296, ports));

        // Port checks
        CHECK(rejects(R"({"pid":20296,"httpsPort":58320,"httpPort":58321,"csrfToken":"tok"})", 20296, {}));
        CHECK(rejects(R"({"pid":20296,"httpsPort":11111,"httpPort":22222,"csrfToken":"tok"})", 20296, ports));
        CHECK(rejects(R"({"pid":20296,"httpsPort":0,"httpPort":65536,"csrfToken":"tok"})", 20296, ports));
        CHECK(rejects(R"({"pid":20296,"httpsPort":58320.5,"httpPort":"58321","csrfToken":"tok"})", 20296, ports));
        CHECK(rejects(R"({"pid":20296,"csrfToken":"tok"})", 20296, ports));

        // Token checks
        CHECK(rejects(R"({"pid":20296,"httpsPort":58320,"csrfToken":""})", 20296, ports));
        CHECK(rejects(R"({"pid":20296,"httpsPort":58320,"csrfToken":123})", 20296, ports));
        CHECK(rejects(R"({"pid":20296,"httpsPort":58320})", 20296, ports));
        CHECK(rejects(R"({"pid":20296,"httpsPort":58320,"csrfToken":"tok\r\nX-Injected: 1"})", 20296, ports));
        CHECK(rejects(R"({"pid":20296,"httpsPort":58320,"csrfToken":"tok\n"})", 20296, ports));
        CHECK(rejects(R"({"pid":20296,"httpsPort":58320,"csrfToken":"tok\u0001"})", 20296, ports));
        CHECK(rejects(R"({"pid":20296,"httpsPort":58320,"csrfToken":"tok\u007f"})", 20296, ports));

        // Invalid or non-object JSON and embedded NUL
        CHECK(rejects("", 20296, ports));
        CHECK(rejects("{", 20296, ports));
        CHECK(rejects("[]", 20296, ports));
        CHECK(rejects(std::string("{\"pid\":20296,\"httpsPort\":58320,\"csrfToken\":\"tok\0\"}", 51), 20296, ports));
    });
}
