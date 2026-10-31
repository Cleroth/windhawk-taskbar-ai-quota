// Logo path data: pure SVG path strings, parsed by every renderer.

static void RunLogoTests() {
    Run("logo paths are SVG path data", [] {
        for (PCWSTR path : {kOpenAiLogoPath, kClaudeLogoPath, kGeminiLogoPath}) {
            std::wstring text = path;
            CHECK(text.size() > 100);
            CHECK(text.front() == L'M' || text.front() == L'm');
            CHECK(text.find_first_not_of(L"0123456789.,- MmLlHhVvCcSsQqTtAaZz") == std::wstring::npos);
        }
    });
}
