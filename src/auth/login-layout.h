// The sign-in window's text and layout (the window: auth/login.h, its controls:
// auth/login-controls.h): every text it shows, and its controls' rectangles and client size in
// pixels at its DPI from that text measured in its font. Pure: the caller measures.

// The buttons' labels, each in every state it shows: Copy link turns into Copied or Copy failed
// (the device flow's button, which copies the code and opens GitHub, into Code copied), Cancel
// into Close once a device sign-in ends without a token.
struct LoginWindowLabels {
    std::wstring open, copy, copied, copyFailed, signIn, cancel, close;
};

static LoginWindowLabels LoginWindowLabelsFor(const I18nLocale& locale, bool device) {
    auto text = [&](I18nKey key) { return I18nText(locale, key); };
    return {.open = text(TrCtx(L"sign-in-button", L"Open in browser")),
            .copy = device ? text(TrCtx(L"sign-in-button", L"Copy code and open GitHub")) :
                             text(TrCtx(L"sign-in-button", L"Copy link")),
            .copied = device ? text(TrCtx(L"sign-in-button", L"Code copied")) :
                               text(TrCtx(L"sign-in-button", L"Copied")),
            .copyFailed = text(TrCtx(L"sign-in-button", L"Copy failed")),
            .signIn = text(TrCtx(L"sign-in-button", L"Sign in")),
            .cancel = text(TrCtx(L"sign-in-button", L"Cancel")),
            .close = text(TrCtx(L"sign-in-button", L"Close"))};
}

// What a sign-in window shows, every state's text built before the window exists, so that
// creating, relabeling and measuring it allocates nothing.
struct LoginWindowText {
    // GitHub's device flow shows its code (read-only), a status line, one copy button and Cancel.
    // The others show Open in browser, Copy link, `alternate` when set (a loopback flow's other
    // way to sign in) and Cancel; a pasted code or key (`paste`) adds a box, masked when `secret`,
    // and Sign in below it.
    bool device = false;
    bool paste = false;
    bool secret = false;
    std::wstring instructions;
    std::wstring otherSignIn;  // The instructions after a code from another sign-in; empty: never
    std::wstring userCode;
    std::vector<std::wstring> statuses;  // Every text the device flow's status line shows
    std::wstring alternate;
    LoginWindowLabels labels;
};

struct LoginLayout {
    SIZE client{};
    // `code`: the device flow's code or the box for a pasted code or key. A control the flow
    // lacks stays empty.
    RECT instructions{}, code{}, status{}, open{}, copy{}, alternate{}, signIn{}, cancel{};
};

// `width(text)`: the pixel width of `text` on one line; `wrappedHeight(text, width)`: its pixel
// height wrapped at `width` pixels; `lineHeight`: the font's. Lengths are DIPs, each edge scaled
// on its own: a button as wide as its widest label with 10 DIP on each side, the instructions and
// the status line as tall as their tallest text wrapped at the content's width, each never
// smaller than its English size. Only the buttons widen the content: text wraps.
template <typename Width, typename WrappedHeight>
static LoginLayout LoginWindowLayout(const LoginWindowText& text, UINT dpi, int lineHeight, const Width& width,
                                     const WrappedHeight& wrappedHeight) {
    auto px = [dpi](int dips) { return MulDiv(dips, (int)dpi, 96); };
    auto dips = [dpi](int pixels) { return (int)(((long long)pixels * 96 + dpi - 1) / dpi); };
    auto rect = [&](int x, int y, int w, int h) { return RECT{px(x), px(y), px(x) + px(w), px(y) + px(h)}; };
    auto button = [&](int least, std::initializer_list<const std::wstring*> states) {
        int widest = 0;
        for (const std::wstring* label : states) widest = std::max(widest, width(*label));
        return std::max(least, dips(widest) + 20);
    };
    constexpr int kSide = 12, kGap = 6;
    const LoginWindowLabels& labels = text.labels;
    const int buttonHeight = std::max(30, dips(lineHeight + px(10)));
    const int boxHeight = std::max(24, dips(lineHeight + px(6)));
    const int open = text.device ? 0 : button(120, {&labels.open});
    const int copy = button(text.device ? 246 : 120, {&labels.copy, &labels.copied, &labels.copyFailed});
    const int alternate = text.alternate.empty() ? 0 : button(82, {&text.alternate});
    const int signIn = text.paste ? button(84, {&labels.signIn}) : 0;
    const int cancel = text.device ? button(84, {&labels.cancel, &labels.close}) : button(84, {&labels.cancel});

    // The link buttons from the left; Cancel at the right on their row, or after Sign in under the
    // box.
    const int links = (text.device ? 0 : open + kGap) + copy + (alternate ? kGap + alternate : 0);
    const int content = std::max({424, text.paste ? links : links + kGap + cancel,
                                  text.paste ? signIn + kGap + cancel : 0});
    int instructions = wrappedHeight(text.instructions, px(content));
    if (!text.otherSignIn.empty()) instructions = std::max(instructions, wrappedHeight(text.otherSignIn, px(content)));
    instructions = std::max(52, dips(instructions));

    LoginLayout layout;
    int y = 10;
    layout.instructions = rect(kSide, y, content, instructions);
    y += instructions + kGap;
    if (text.device) {
        int status = 0;
        for (const std::wstring& line : text.statuses) status = std::max(status, wrappedHeight(line, px(content)));
        status = std::max(20, dips(status));
        layout.code = rect(kSide, y, content, boxHeight);
        y += boxHeight + 8;
        layout.status = rect(kSide, y, content, status);
        y += status + 8;
    } else {
        layout.open = rect(kSide, y, open, buttonHeight);
    }
    const int copyLeft = kSide + (text.device ? 0 : open + kGap);
    layout.copy = rect(copyLeft, y, copy, buttonHeight);
    if (alternate) layout.alternate = rect(copyLeft + copy + kGap, y, alternate, buttonHeight);
    if (text.paste) {
        y += buttonHeight + 12;
        layout.code = rect(kSide, y, content, boxHeight);
        y += boxHeight + 12;
        layout.signIn = rect(kSide + content - cancel - kGap - signIn, y, signIn, buttonHeight);
    }
    layout.cancel = rect(kSide + content - cancel, y, cancel, buttonHeight);
    layout.client = {px(kSide + content + kSide), px(y + buttonHeight + (text.device ? 12 : 10))};
    return layout;
}
