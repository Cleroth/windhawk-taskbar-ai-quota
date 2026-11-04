// Logo path data: pure SVG path strings, parsed by every renderer.

static void RunLogoTests() {
    Run("logo paths are SVG path data", [] {
        for (PCWSTR path : {kOpenAiLogoPath, kClaudeLogoPath, kGeminiLogoPath, kCopilotLogoPath}) {
            std::wstring text = path;
            CHECK(text.size() > 100);
            CHECK(text.front() == L'M' || text.front() == L'm');
            CHECK(text.find_first_not_of(L"0123456789.,- MmLlHhVvCcSsQqTtAaZz") == std::wstring::npos);
        }
        // Each provider's own mark; Antigravity uses Gemini's.
        CHECK(ProviderLogoPath(L"anthropic") == kClaudeLogoPath && ProviderLogoPath(L"openai") == kOpenAiLogoPath &&
              ProviderLogoPath(L"antigravity") == kGeminiLogoPath && ProviderLogoPath(L"copilot") == kCopilotLogoPath);
    });

    Run("logo brand colors: Claude's orange and the Gemini gradient, each only when chosen", [] {
        CHECK(!ProviderLogoBrand({}, L"anthropic") && !ProviderLogoBrand({}, L"antigravity"));
        // OpenAI's and GitHub Copilot's marks have none.
        const LogoBrands both{true, true};
        CHECK(!ProviderLogoBrand(both, L"openai") && !ProviderLogoBrand(both, L"copilot") && !ProviderLogoBrand(both, L""));
        // Claude: flat #D97757 (hsl(14.8, 63.1%, 59.6%), the Claude symbol's fill).
        const LogoBrand* claude = ProviderLogoBrand({true, false}, L"anthropic");
        CHECK(claude && claude->from == 0xFFD97757 && claude->to == 0xFFD97757);
        CHECK(!ProviderLogoBrand({true, false}, L"antigravity"));
        // Gemini: #9168C0 to #1BA1E3 along the official gradient's 18.7 degree axis, from its
        // first stop (offset 0.067 of 29.8 from (2.78, 11.38) in a 28 view box) to its last
        // (0.672), scaled to 24.
        const LogoBrand* gemini = ProviderLogoBrand({false, true}, L"antigravity");
        CHECK(gemini && gemini->from == 0xFF9168C0 && gemini->to == 0xFF1BA1E3);
        CHECK(!ProviderLogoBrand({false, true}, L"anthropic"));
        CHECK(std::abs(gemini->x0 - 4.006f) < 1e-3f && std::abs(gemini->y0 - 10.303f) < 1e-3f);
        CHECK(std::abs(gemini->x1 - 18.645f) < 1e-3f && std::abs(gemini->y1 - 15.253f) < 1e-3f);
    });
}
