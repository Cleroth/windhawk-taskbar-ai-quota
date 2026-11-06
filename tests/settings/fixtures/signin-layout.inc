// The sign-in window's layout (auth/login-layout.h) from measurements: each label's width given by
// its text in the production catalogs, as the sign-in probe measured them in Segoe UI 9 pt at
// that DPI (its OBSERVE lines, read 2026-11-06), and each wrapped text's lines, the instructions
// and statuses standing in as synthetic texts. Expected rectangles are derived by hand.

struct SignInMeasures {
    int lineHeight = 15;
    int wrapAt = 424;                     // Pixels the wrapped texts must be measured at
    std::map<std::wstring, int> widths;   // Each label's one-line width (px)
    std::map<std::wstring, int> lines;    // Each wrapped text's lines at wrapAt
};

// Label widths in `locale`, by the English: Open in browser, Copy link, Copied, Copy failed, Use a
// code, Sign in, Cancel, Close, Copy code and open GitHub, Code copied.
static std::map<std::wstring, int> SignInWidths(const I18nLocale& locale, std::array<int, 10> px) {
    LoginWindowLabels link = LoginWindowLabelsFor(locale, false), device = LoginWindowLabelsFor(locale, true);
    return {{link.open, px[0]},   {link.copy, px[1]},
            {link.copied, px[2]}, {link.copyFailed, px[3]},
            {I18nText(locale, TrCtx(L"sign-in-button", L"Use a code")), px[4]},
            {link.signIn, px[5]}, {link.cancel, px[6]},
            {link.close, px[7]},  {device.copy, px[8]},
            {device.copied, px[9]}};
}

enum class SignInTestFlow { Link, Alternate, Paste, Device };

// A flow's text: the production labels in `locale`; synthetic instructions, a paste flow's other
// sign-in text and the device flow's statuses.
static LoginWindowText SignInTestText(const I18nLocale& locale, SignInTestFlow flow) {
    LoginWindowText text;
    text.device = flow == SignInTestFlow::Device;
    text.paste = flow == SignInTestFlow::Paste;
    text.labels = LoginWindowLabelsFor(locale, text.device);
    text.instructions = L"instructions";
    if (text.paste) text.otherSignIn = L"other sign-in";
    if (text.device) text.statuses = {L"waiting", L"expired"};
    if (flow == SignInTestFlow::Alternate) text.alternate = I18nText(locale, TrCtx(L"sign-in-button", L"Use a code"));
    return text;
}

static LoginLayout SignInTestLayout(const LoginWindowText& text, UINT dpi, const SignInMeasures& m) {
    return LoginWindowLayout(
        text, dpi, m.lineHeight, [&](const std::wstring& label) { return m.widths.at(label); },
        [&](const std::wstring& wrapped, int width) {
            CHECK(width == m.wrapAt);
            return m.lines.at(wrapped) * m.lineHeight;
        });
}

static bool SignInRect(const RECT& r, LONG left, LONG top, LONG right, LONG bottom) {
    return r.left == left && r.top == top && r.right == right && r.bottom == bottom;
}

static bool SignInEmpty(const RECT& r) {
    return SignInRect(r, 0, 0, 0, 0);
}

static void RunSignInLayoutTests() {
    const I18nLocale en, de = TestLocale(L"de"), ru = TestLocale(L"ru"), ja = TestLocale(L"ja"),
                         pseudo = TestLocale(L"qps-ploc");
    using enum SignInTestFlow;
    // Synthetic instructions within three lines, the other sign-in's within two, statuses one.
    const std::map<std::wstring, int> threeLines = {
        {L"instructions", 3}, {L"other sign-in", 2}, {L"waiting", 1}, {L"expired", 1}};

    Run("sign-in layout: English keeps its fixed geometry in every flow at 96 and 144 DPI, its labels within "
        "their English widths", [&] {
        const SignInMeasures at96{15, 424, SignInWidths(en, {87, 50, 38, 60, 57, 36, 36, 29, 151, 67}), threeLines};
        LoginLayout link = SignInTestLayout(SignInTestText(en, Alternate), 96, at96);
        CHECK(link.client.cx == 448 && link.client.cy == 108);
        CHECK(SignInRect(link.instructions, 12, 10, 436, 62));
        CHECK(SignInRect(link.open, 12, 68, 132, 98) && SignInRect(link.copy, 138, 68, 258, 98));
        CHECK(SignInRect(link.alternate, 264, 68, 346, 98) && SignInRect(link.cancel, 352, 68, 436, 98));
        CHECK(SignInEmpty(link.code) && SignInEmpty(link.status) && SignInEmpty(link.signIn));
        LoginLayout openAi = SignInTestLayout(SignInTestText(en, Link), 96, at96);
        CHECK(openAi.client.cx == 448 && openAi.client.cy == 108 && SignInEmpty(openAi.alternate));
        CHECK(SignInRect(openAi.copy, 138, 68, 258, 98) && SignInRect(openAi.cancel, 352, 68, 436, 98));
        LoginLayout paste = SignInTestLayout(SignInTestText(en, Paste), 96, at96);
        CHECK(paste.client.cx == 448 && paste.client.cy == 186);
        CHECK(SignInRect(paste.open, 12, 68, 132, 98) && SignInRect(paste.copy, 138, 68, 258, 98));
        CHECK(SignInRect(paste.code, 12, 110, 436, 134));
        CHECK(SignInRect(paste.signIn, 262, 146, 346, 176) && SignInRect(paste.cancel, 352, 146, 436, 176));
        LoginLayout device = SignInTestLayout(SignInTestText(en, Device), 96, at96);
        CHECK(device.client.cx == 448 && device.client.cy == 170);
        CHECK(SignInRect(device.instructions, 12, 10, 436, 62) && SignInRect(device.code, 12, 68, 436, 92));
        CHECK(SignInRect(device.status, 12, 100, 436, 120));
        CHECK(SignInRect(device.copy, 12, 128, 258, 158) && SignInRect(device.cancel, 352, 128, 436, 158));
        CHECK(SignInEmpty(device.open) && SignInEmpty(device.alternate) && SignInEmpty(device.signIn));

        // Each length half as long again: 672 x 162, 279 and 255.
        const SignInMeasures at144{25, 636, SignInWidths(en, {132, 74, 57, 90, 87, 54, 51, 43, 228, 101}), threeLines};
        link = SignInTestLayout(SignInTestText(en, Alternate), 144, at144);
        CHECK(link.client.cx == 672 && link.client.cy == 162);
        CHECK(SignInRect(link.instructions, 18, 15, 654, 93));
        CHECK(SignInRect(link.open, 18, 102, 198, 147) && SignInRect(link.copy, 207, 102, 387, 147));
        CHECK(SignInRect(link.alternate, 396, 102, 519, 147) && SignInRect(link.cancel, 528, 102, 654, 147));
        paste = SignInTestLayout(SignInTestText(en, Paste), 144, at144);
        CHECK(paste.client.cx == 672 && paste.client.cy == 279 && SignInRect(paste.code, 18, 165, 654, 201));
        CHECK(SignInRect(paste.signIn, 393, 219, 519, 264) && SignInRect(paste.cancel, 528, 219, 654, 264));
        device = SignInTestLayout(SignInTestText(en, Device), 144, at144);
        CHECK(device.client.cx == 672 && device.client.cy == 255);
        CHECK(SignInRect(device.code, 18, 102, 654, 138) && SignInRect(device.status, 18, 150, 654, 180));
        CHECK(SignInRect(device.copy, 18, 192, 387, 237) && SignInRect(device.cancel, 528, 192, 654, 237));
    });

    Run("sign-in layout: English at 120 and 192 DPI scales each edge of the fixed geometry on its own", [&] {
        struct Case {
            UINT dpi;
            SignInMeasures measures;
        };
        const Case cases[] = {
            {120, {20, 530, SignInWidths(en, {109, 61, 48, 76, 73, 45, 44, 36, 189, 85}), threeLines}},
            {192, {32, 848, SignInWidths(en, {177, 100, 76, 121, 117, 74, 71, 58, 305, 135}), threeLines}}};
        for (const Case& c : cases) {
            auto px = [&](int dips) { return MulDiv(dips, int(c.dpi), 96); };
            auto fixed = [&](const RECT& r, int x, int y, int w, int h) {
                return SignInRect(r, px(x), px(y), px(x) + px(w), px(y) + px(h));
            };
            LoginLayout link = SignInTestLayout(SignInTestText(en, Alternate), c.dpi, c.measures);
            CHECK(link.client.cx == px(448) && link.client.cy == px(108));
            CHECK(fixed(link.instructions, 12, 10, 424, 52) && fixed(link.open, 12, 68, 120, 30));
            CHECK(fixed(link.copy, 138, 68, 120, 30) && fixed(link.alternate, 264, 68, 82, 30));
            CHECK(fixed(link.cancel, 352, 68, 84, 30));
            LoginLayout paste = SignInTestLayout(SignInTestText(en, Paste), c.dpi, c.measures);
            CHECK(paste.client.cy == px(186) && fixed(paste.code, 12, 110, 424, 24));
            CHECK(fixed(paste.signIn, 262, 146, 84, 30) && fixed(paste.cancel, 352, 146, 84, 30));
            LoginLayout device = SignInTestLayout(SignInTestText(en, Device), c.dpi, c.measures);
            CHECK(device.client.cy == px(170) && fixed(device.code, 12, 68, 424, 24));
            CHECK(fixed(device.status, 12, 100, 424, 20) && fixed(device.copy, 12, 128, 246, 30));
            CHECK(fixed(device.cancel, 352, 128, 84, 30));
        }
    });

    Run("sign-in layout: German widens Anthropic's window for Code verwenden and Kopieren fehlgeschlagen at 96 and "
        "144 DPI; its device flow grows for four lines of instructions and a two-line status", [&] {
        // 96 DPI: Copy link 150 (Kopieren fehlgeschlagen 130 + 20), Use a code 109 (89 + 20), Open
        // and Cancel at their English 120 and 84: 120 + 6 + 150 + 6 + 109 + 6 + 84 = 481.
        SignInMeasures m{15, 481, SignInWidths(de, {97, 71, 38, 130, 89, 55, 58, 51, 180, 68}),
                         {{L"instructions", 3}, {L"waiting", 1}, {L"expired", 2}}};
        LoginLayout link = SignInTestLayout(SignInTestText(de, Alternate), 96, m);
        CHECK(link.client.cx == 505 && link.client.cy == 108);
        CHECK(SignInRect(link.instructions, 12, 10, 493, 62));
        CHECK(SignInRect(link.open, 12, 68, 132, 98) && SignInRect(link.copy, 138, 68, 288, 98));
        CHECK(SignInRect(link.alternate, 294, 68, 403, 98) && SignInRect(link.cancel, 409, 68, 493, 98));
        // The device flow: Code kopieren und GitHub öffnen (180 + 20) within 246; instructions 4 x 15,
        // the status the expired text's 2 x 15.
        m.wrapAt = 424;
        m.lines[L"instructions"] = 4;
        LoginLayout device = SignInTestLayout(SignInTestText(de, Device), 96, m);
        CHECK(device.client.cx == 448 && device.client.cy == 188);
        CHECK(SignInRect(device.instructions, 12, 10, 436, 70) && SignInRect(device.code, 12, 76, 436, 100));
        CHECK(SignInRect(device.status, 12, 108, 436, 138));
        CHECK(SignInRect(device.copy, 12, 146, 258, 176) && SignInRect(device.cancel, 352, 146, 436, 176));

        // 144 DPI, in DIPs rounded up: Kopieren fehlgeschlagen 193 px = 129 + 20, Code verwenden
        // 133 px = 89 + 20; 120 + 6 + 149 + 6 + 109 + 6 + 84 = 480 DIPs, 720 px; edges at 1.5
        // rounding half up (149: 223.5 -> 224, 293: 439.5 -> 440, 109: 163.5 -> 164).
        m = {25, 720, SignInWidths(de, {146, 105, 57, 193, 133, 81, 86, 74, 270, 103}), {{L"instructions", 3}}};
        link = SignInTestLayout(SignInTestText(de, Alternate), 144, m);
        CHECK(link.client.cx == 756 && link.client.cy == 162);
        CHECK(SignInRect(link.instructions, 18, 15, 738, 93));
        CHECK(SignInRect(link.open, 18, 102, 198, 147) && SignInRect(link.copy, 207, 102, 431, 147));
        CHECK(SignInRect(link.alternate, 440, 102, 604, 147) && SignInRect(link.cancel, 612, 102, 738, 147));
    });

    Run("sign-in layout: Russian widens Open in browser and Copy link; a paste flow keeps its English width", [&] {
        // Открыть в браузере 109 + 20, Не удалось скопировать 135 + 20, Ввести код 59 + 20 within 82:
        // 129 + 6 + 155 + 6 + 82 + 6 + 84 = 468.
        SignInMeasures m{15, 468, SignInWidths(ru, {109, 108, 75, 135, 59, 33, 42, 46, 193, 89}),
                         {{L"instructions", 3}, {L"other sign-in", 2}}};
        LoginLayout link = SignInTestLayout(SignInTestText(ru, Alternate), 96, m);
        CHECK(link.client.cx == 492 && link.client.cy == 108);
        CHECK(SignInRect(link.open, 12, 68, 141, 98) && SignInRect(link.copy, 147, 68, 302, 98));
        CHECK(SignInRect(link.alternate, 308, 68, 390, 98) && SignInRect(link.cancel, 396, 68, 480, 98));
        // Open and Copy link alone on the first row (290) and Войти, Отмена under the box fit 424.
        m.wrapAt = 424;
        LoginLayout paste = SignInTestLayout(SignInTestText(ru, Paste), 96, m);
        CHECK(paste.client.cx == 448 && paste.client.cy == 186);
        CHECK(SignInRect(paste.open, 12, 68, 141, 98) && SignInRect(paste.copy, 147, 68, 302, 98));
        CHECK(SignInRect(paste.code, 12, 110, 436, 134));
        CHECK(SignInRect(paste.signIn, 262, 146, 346, 176) && SignInRect(paste.cancel, 352, 146, 436, 176));
    });

    Run("sign-in layout: Japanese widens Anthropic's window for its Copy failed; its device flow grows as German's",
        [&] {
        // ブラウザーで開く 82 within 120, コピーできませんでした 113 + 20, コードを使用 65 + 20,
        // キャンセル 52 within 84: 120 + 6 + 133 + 6 + 85 + 6 + 84 = 440.
        SignInMeasures m{15, 440, SignInWidths(ja, {82, 67, 68, 113, 65, 49, 52, 32, 162, 107}),
                         {{L"instructions", 3}, {L"waiting", 1}, {L"expired", 2}}};
        LoginLayout link = SignInTestLayout(SignInTestText(ja, Alternate), 96, m);
        CHECK(link.client.cx == 464 && link.client.cy == 108);
        CHECK(SignInRect(link.copy, 138, 68, 271, 98) && SignInRect(link.alternate, 277, 68, 362, 98));
        CHECK(SignInRect(link.cancel, 368, 68, 452, 98));
        m.wrapAt = 424;
        m.lines[L"instructions"] = 4;
        LoginLayout device = SignInTestLayout(SignInTestText(ja, Device), 96, m);
        CHECK(device.client.cx == 448 && device.client.cy == 188 && SignInRect(device.status, 12, 108, 436, 138));
    });

    Run("sign-in layout: the pseudo-locale's longest labels at 96 and 144 DPI, and its device flow", [&] {
        // Open in browser's pseudo text 128 + 20, Use a code's 82 + 20; Copy link and Cancel at their
        // English 120 and 84: 148 + 6 + 120 + 6 + 102 + 6 + 84 = 472.
        SignInMeasures m{15, 472, SignInWidths(pseudo, {128, 75, 59, 87, 82, 57, 57, 43, 205, 92}),
                         {{L"instructions", 3}, {L"waiting", 1}, {L"expired", 1}}};
        LoginLayout link = SignInTestLayout(SignInTestText(pseudo, Alternate), 96, m);
        CHECK(link.client.cx == 496 && link.client.cy == 108);
        CHECK(SignInRect(link.open, 12, 68, 160, 98) && SignInRect(link.copy, 166, 68, 286, 98));
        CHECK(SignInRect(link.alternate, 292, 68, 394, 98) && SignInRect(link.cancel, 400, 68, 484, 98));
        m.wrapAt = 424;
        m.lines[L"instructions"] = 4;
        LoginLayout device = SignInTestLayout(SignInTestText(pseudo, Device), 96, m);
        CHECK(device.client.cx == 448 && device.client.cy == 178);
        CHECK(SignInRect(device.status, 12, 108, 436, 128));
        CHECK(SignInRect(device.copy, 12, 136, 258, 166) && SignInRect(device.cancel, 352, 136, 436, 166));

        // 144 DPI: 193 px = 129 + 20, 122 px = 82 + 20: 149 + 6 + 120 + 6 + 102 + 6 + 84 = 473 DIPs;
        // 473 at 1.5 = 709.5 -> 710 px, the client 497 -> 746.
        m = {25, 710, SignInWidths(pseudo, {193, 110, 86, 129, 122, 84, 81, 62, 307, 135}), {{L"instructions", 3}}};
        link = SignInTestLayout(SignInTestText(pseudo, Alternate), 144, m);
        CHECK(link.client.cx == 746 && link.client.cy == 162);
        CHECK(SignInRect(link.open, 18, 102, 242, 147) && SignInRect(link.copy, 251, 102, 431, 147));
        CHECK(SignInRect(link.alternate, 440, 102, 593, 147) && SignInRect(link.cancel, 602, 102, 728, 147));
    });

    Run("sign-in layout: reserves: the instructions fit the other sign-in's text, Cancel fits Close in the device "
        "flow only, a long Sign in widens a paste flow, nothing measured keeps English's sizes", [&] {
        const auto widths = SignInWidths(en, {87, 50, 38, 60, 57, 36, 36, 100, 151, 67});
        // The other sign-in's text runs to four lines: 60 tall, every row 8 lower.
        LoginLayout paste = SignInTestLayout(SignInTestText(en, Paste), 96,
                                             {15, 424, widths, {{L"instructions", 2}, {L"other sign-in", 4}}});
        CHECK(SignInRect(paste.instructions, 12, 10, 436, 70) && SignInRect(paste.open, 12, 76, 132, 106));
        CHECK(SignInRect(paste.code, 12, 118, 436, 142) && SignInRect(paste.signIn, 262, 154, 346, 184));
        CHECK(paste.client.cx == 448 && paste.client.cy == 194);
        // Close at 100 px: the device flow's Cancel 120 wide; the link flow's keeps 84.
        LoginLayout device = SignInTestLayout(SignInTestText(en, Device), 96, {15, 424, widths, threeLines});
        CHECK(SignInRect(device.cancel, 316, 128, 436, 158));
        LoginLayout link = SignInTestLayout(SignInTestText(en, Link), 96, {15, 424, widths, threeLines});
        CHECK(SignInRect(link.cancel, 352, 68, 436, 98));
        // Sign in at 400 px: 420 + 6 + 84 = 510 wide, Sign in at the left margin.
        auto wide = widths;
        wide[LoginWindowLabelsFor(en, false).signIn] = 400;
        paste = SignInTestLayout(SignInTestText(en, Paste), 96, {15, 510, wide, threeLines});
        CHECK(paste.client.cx == 534 && SignInRect(paste.code, 12, 110, 522, 134));
        CHECK(SignInRect(paste.signIn, 12, 146, 432, 176) && SignInRect(paste.cancel, 438, 146, 522, 176));
        // Nothing measured (no DC): English's fixed sizes.
        LoginLayout none = LoginWindowLayout(SignInTestText(en, Alternate), 96, 0,
                                             [](const std::wstring&) { return 0; },
                                             [](const std::wstring&, int) { return 0; });
        CHECK(none.client.cx == 448 && none.client.cy == 108 && SignInRect(none.alternate, 264, 68, 346, 98));
    });

    Run("sign-in layout: a taller font's line grows the buttons and boxes past their English heights", [&] {
        // A 24 px line at 96 DPI: buttons 24 + 10 = 34, boxes 24 + 6 = 30; two lines of
        // instructions (48) keep 52, one status line 24.
        LoginLayout device = SignInTestLayout(
            SignInTestText(en, Device), 96,
            {24, 424, SignInWidths(en, {87, 50, 38, 60, 57, 36, 36, 29, 151, 67}),
             {{L"instructions", 2}, {L"waiting", 1}, {L"expired", 1}}});
        CHECK(SignInRect(device.instructions, 12, 10, 436, 62) && SignInRect(device.code, 12, 68, 436, 98));
        CHECK(SignInRect(device.status, 12, 106, 436, 130));
        CHECK(SignInRect(device.copy, 12, 138, 258, 172) && SignInRect(device.cancel, 352, 138, 436, 172));
        CHECK(device.client.cx == 448 && device.client.cy == 184);
    });
}
