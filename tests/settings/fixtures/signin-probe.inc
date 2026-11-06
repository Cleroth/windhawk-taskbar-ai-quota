// Sign-in window probe (hand-written, not production code). The production controls
// (auth/login-controls.h: creation, the message font, the measure and the layout of
// auth/login-layout.h applied) in probe windows that are never shown, with comctl32 6 activated
// as Explorer's manifest gives the sign-in window, for every language and the pseudo-locale at 96
// and 144 DPI in each sign-in's flow, its text built as the sign-ins build it (auth/login.h).
// Checks what the controls themselves report against that layout: each button's ideal size
// (BCM_GETIDEALSIZE) for every label it shows, each static's every text as the control paints it
// (WM_PRINTCLIENT) inside its rectangle, each box's text rectangle (EM_GETRECT) a line tall, no
// control over another or outside the client, a DPI change laying out as a new window, and the
// production frame's client at the window's own DPI. Themes draw at the probe monitor's DPI
// whatever DPI the layout is for. Renders every flow per language and DPI, with the states that
// change text (render-signin-<tag>-<dpi>.png); OBSERVE lines report the measures.
// Usage: signin-probe.exe <render directory>
#include "check.inc"
#include <commctrl.h>

static constexpr PCWSTR kSignInProbeClass = L"AiQuotaSignInProbe";

enum class SignInFlow { Anthropic, OpenAi, AnthropicCode, OpenCode, Copilot };
static constexpr std::pair<SignInFlow, const char*> kSignInFlows[] = {
    {SignInFlow::Anthropic, "anthropic"}, {SignInFlow::OpenAi, "openai"}, {SignInFlow::AnthropicCode, "anthropic-code"},
    {SignInFlow::OpenCode, "opencode"},   {SignInFlow::Copilot, "copilot"}};
static constexpr PCWSTR kSignInTags[] = {L"en", L"de", L"es", L"fr", L"ja", L"pt-BR", L"ru", L"zh-Hans", L"qps-ploc"};
static constexpr PCWSTR kSignInRenderTags[] = {L"en", L"qps-ploc", L"de", L"ru", L"ja", L"zh-Hans"};

static std::string Narrow(std::wstring_view text) {
    std::string out;
    for (wchar_t c : text) out += c < 128 ? char(c) : '?';
    return out;
}

// The window's text as the sign-ins (instructions, a loopback flow's alternate, the device flow's
// statuses) and ShowLoginDialog (flags, labels, a stated code's mismatch) build it.
static LoginWindowText SignInText(const I18nLocale& locale, SignInFlow flow) {
    LoginWindowText text;
    text.device = flow == SignInFlow::Copilot;
    text.paste = flow == SignInFlow::AnthropicCode || flow == SignInFlow::OpenCode;
    text.secret = flow == SignInFlow::OpenCode;
    text.labels = LoginWindowLabelsFor(locale, text.device);
    switch (flow) {
        case SignInFlow::Anthropic:
            text.alternate = I18nText(locale, TrCtx(L"sign-in-button", L"Use a code"));
            text.instructions =
                I18nText(locale,
                         TrCtx(L"sign-in-window",
                               L"Open the sign-in link in a browser on this PC and approve access on claude.ai. This "
                               L"window closes once sign-in completes. On another device, choose {button}."),
                         {{L"button", text.alternate}});
            break;
        case SignInFlow::OpenAi:
            text.instructions = I18nText(locale, TrCtx(L"sign-in-window",
                                                       L"Open the sign-in link in a browser on this PC and approve "
                                                       L"access. This window closes once sign-in completes."));
            break;
        case SignInFlow::AnthropicCode:
            text.instructions = I18nText(locale, TrCtx(L"sign-in-window",
                                                       L"Open the sign-in link in a browser and approve access on "
                                                       L"claude.ai, then paste the code shown on the page below."));
            text.otherSignIn = I18nText(locale, TrCtx(L"sign-in-window",
                                                      L"That code belongs to another sign-in link. Open this window's "
                                                      L"link, approve access, then paste the code its page shows."));
            break;
        case SignInFlow::OpenCode:
            text.instructions = I18nText(locale, TrCtx(L"sign-in-window",
                                                       L"Open the OpenCode console in a browser, copy an API key of the "
                                                       L"workspace with your Go subscription, and paste it below."));
            break;
        case SignInFlow::Copilot: {
            text.userCode = L"WDJB-MJHT";
            text.instructions =
                I18nText(locale, TrCtx(L"sign-in-window",
                                       L"Copy the code and open GitHub below, or enter it at github.com/login/device on "
                                       L"any device, signed in to the GitHub account with Copilot. This window closes "
                                       L"once sign-in completes."));
            auto retrying = [&](const AccountError& failure) {
                return I18nText(locale, TrCtx(L"sign-in-status", L"{reason}, retrying\u2026"),
                                {{L"reason", AccountErrorText(locale, failure, AccountErrorForm::Alone)}});
            };
            text.statuses = {
                I18nText(locale, TrCtx(L"sign-in-status", L"Waiting for approval on GitHub\u2026")),
                I18nText(locale, TrCtx(L"sign-in-status", L"The code expired. Close this window and sign in again.")),
                I18nText(locale, TrCtx(L"sign-in-status", L"Sign-in failed. See the Windhawk log for details.")),
                I18nText(locale, TrCtx(L"sign-in-status", L"Sign-in was denied on GitHub.")),
                retrying({AccountErrorCode::Network}),
                retrying({.code = AccountErrorCode::Http, .status = 503}),
                retrying({AccountErrorCode::UnexpectedTokenResponse}),
                retrying({AccountErrorCode::InvalidTokenResponse})};
            break;
        }
    }
    return text;
}

// A probe window with the production controls laid out for `text` at `dpi`; never shown.
struct SignInWindow {
    HWND window = nullptr;
    HFONT font = nullptr;

    SignInWindow(const LoginWindowText& text, UINT dpi, DWORD style = WS_POPUP, DWORD exStyle = 0) {
        window = CreateWindowExW(exStyle, kSignInProbeClass, L"", style, 0, 0, 100, 100, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
        if (!window) throw std::runtime_error("CreateWindowExW failed: " + std::to_string(GetLastError()));
        CreateLoginControls(window, text, text.statuses.empty() ? L"" : text.statuses[0].c_str());
        LayoutLoginControls(window, text, dpi, font);
    }
    ~SignInWindow() {
        DestroyWindow(window);
        if (font) DeleteObject(font);
    }
    SignInWindow(const SignInWindow&) = delete;
    SignInWindow& operator=(const SignInWindow&) = delete;

    HWND Child(int id) const { return GetDlgItem(window, id); }
    RECT Rect(int id) const {
        RECT r{};
        if (HWND child = Child(id)) {
            GetWindowRect(child, &r);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&r), 2);
        }
        return r;
    }
    SIZE Client() const {
        RECT r{};
        GetClientRect(window, &r);
        return {r.right, r.bottom};
    }
};

static constexpr int kSignInControls[] = {kLoginInstructions, kLoginCode, kLoginStatus, kLoginOpenLink, kLoginCopyLink,
                                          kLoginAlternate, IDOK, IDCANCEL};

static std::wstring WindowText(HWND control) {
    std::wstring text(size_t(GetWindowTextLengthW(control)) + 1, L'\0');
    text.resize(size_t(GetWindowTextW(control, text.data(), int(text.size()))));
    return text;
}

// A 32-bit top-down DIB section selected into its own memory DC, filled with `fill`.
struct SignInBitmap {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    const uint32_t* bits = nullptr;
    int width = 0, height = 0;

    SignInBitmap(int w, int h, COLORREF fill) : width(w), height(h) {
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        void* pixels = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        if (!bitmap || !dc) throw std::runtime_error("DIB section failed");
        bits = static_cast<const uint32_t*>(pixels);
        previous = SelectObject(dc, bitmap);
        HBRUSH brush = CreateSolidBrush(fill);
        RECT all{0, 0, w, h};
        FillRect(dc, &all, brush);
        DeleteObject(brush);
    }
    ~SignInBitmap() {
        SelectObject(dc, previous);
        DeleteDC(dc);
        DeleteObject(bitmap);
    }
    SignInBitmap(const SignInBitmap&) = delete;
    SignInBitmap& operator=(const SignInBitmap&) = delete;

    COLORREF At(int x, int y) const {
        uint32_t p = bits[size_t(y) * size_t(width) + size_t(x)];
        return RGB((p >> 16) & 0xFF, (p >> 8) & 0xFF, p & 0xFF);
    }
};

// The bounds of what a static paints of `text` (WM_PRINTCLIENT) at its width, given room for
// several more lines below; text and size restored after.
static RECT PaintedInk(HWND control, const std::wstring& text) {
    RECT client{};
    GetClientRect(control, &client);
    std::wstring saved = WindowText(control);
    const int tall = client.bottom * 3 + 200;
    SetWindowTextW(control, text.c_str());
    SetWindowPos(control, nullptr, 0, 0, client.right, tall, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const COLORREF sentinel = RGB(255, 0, 255), face = GetSysColor(COLOR_3DFACE);
    RECT ink{LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN};
    {
        SignInBitmap canvas(client.right, tall, sentinel);
        SendMessageW(control, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(canvas.dc), PRF_CLIENT | PRF_ERASEBKGND);
        GdiFlush();
        for (int y = 0; y < tall; y++) {
            for (int x = 0; x < client.right; x++) {
                COLORREF c = canvas.At(x, y);
                if (c == sentinel || c == face) continue;
                ink = {std::min(ink.left, LONG(x)), std::min(ink.top, LONG(y)), std::max(ink.right, LONG(x + 1)),
                       std::max(ink.bottom, LONG(y + 1))};
            }
        }
    }
    SetWindowPos(control, nullptr, 0, 0, client.right, client.bottom, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowTextW(control, saved.c_str());
    return ink.left == LONG_MAX ? RECT{} : ink;
}

// `text` measured in the control's own font and DC: one line's width, or wrapped at `wrap`.
static SIZE ControlMeasure(HWND control, const std::wstring& text, int wrap = 0) {
    HDC dc = GetDC(control);
    HGDIOBJ previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)));
    SIZE size{};
    if (wrap) {
        RECT bounds{0, 0, wrap, 0};
        DrawTextW(dc, text.c_str(), int(text.size()), &bounds, DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS | DT_NOPREFIX);
        size = {bounds.right, bounds.bottom};
    } else {
        GetTextExtentPoint32W(dc, text.c_str(), int(text.size()), &size);
    }
    SelectObject(dc, previous);
    ReleaseDC(control, dc);
    return size;
}

static int ControlLineHeight(HWND control) {
    HDC dc = GetDC(control);
    HGDIOBJ previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)));
    TEXTMETRICW metrics{};
    GetTextMetricsW(dc, &metrics);
    SelectObject(dc, previous);
    ReleaseDC(control, dc);
    return metrics.tmHeight;
}

static std::string Size(SIZE size) {
    return std::to_string(size.cx) + "x" + std::to_string(size.cy);
}

// Every control's checks for one flow; returns its OBSERVE line's measures.
static std::string CheckSignInWindow(const SignInWindow& w, const LoginWindowText& text) {
    std::string observed = "client " + Size(w.Client());
    const SIZE client = w.Client();
    // Each control inside the client, none over another.
    std::vector<std::pair<int, RECT>> placed;
    for (int id : kSignInControls) {
        if (!w.Child(id)) continue;
        RECT r = w.Rect(id);
        CHECK(r.left >= 0 && r.top >= 0 && r.right <= client.cx && r.bottom <= client.cy && r.right > r.left &&
              r.bottom > r.top);
        for (const auto& other : placed) {
            RECT overlap{};
            CHECK(!IntersectRect(&overlap, &r, &other.second));
        }
        placed.push_back({id, r});
    }
    // Buttons: the control's ideal size for each label it shows fits its rectangle.
    const LoginWindowLabels& labels = text.labels;
    auto button = [&](int id, const char* name, std::initializer_list<const std::wstring*> states) {
        HWND control = w.Child(id);
        CHECK(control);
        RECT r = w.Rect(id);
        std::wstring shown = WindowText(control);
        observed += std::string(" ") + name + "=" + std::to_string(r.right - r.left) + "x" + std::to_string(r.bottom - r.top);
        for (const std::wstring* label : states) {
            SetWindowTextW(control, label->c_str());
            SIZE ideal{};
            CHECK(SendMessageW(control, BCM_GETIDEALSIZE, 0, reinterpret_cast<LPARAM>(&ideal)));
            CHECK(ideal.cx <= r.right - r.left && ideal.cy <= r.bottom - r.top);
            observed += "(" + std::to_string(ControlMeasure(control, *label).cx) + "/" + std::to_string(ideal.cx) + ")";
        }
        SetWindowTextW(control, shown.c_str());
    };
    if (!text.device) button(kLoginOpenLink, "open", {&labels.open});
    button(kLoginCopyLink, "copy", {&labels.copy, &labels.copied, &labels.copyFailed});
    if (!text.alternate.empty()) button(kLoginAlternate, "alternate", {&text.alternate});
    if (text.paste) button(IDOK, "signin", {&labels.signIn});
    if (text.device) {
        button(IDCANCEL, "cancel", {&labels.cancel, &labels.close});
    } else {
        button(IDCANCEL, "cancel", {&labels.cancel});
    }
    // Statics: every text the control shows, as it paints it, inside its rectangle, and no word
    // wider than it.
    auto statics = [&](int id, const char* name, std::vector<const std::wstring*> texts) {
        HWND control = w.Child(id);
        CHECK(control);
        RECT r = w.Rect(id);
        const int width = r.right - r.left, height = r.bottom - r.top, line = ControlLineHeight(control);
        observed += std::string(" ") + name + "=" + std::to_string(width) + "x" + std::to_string(height) + "(";
        for (const std::wstring* shown : texts) {
            RECT ink = PaintedInk(control, *shown);
            CHECK(ink.bottom > 0 && ink.bottom <= height);
            SIZE measured = ControlMeasure(control, *shown, width);
            CHECK(measured.cx <= width && measured.cy <= height);
            observed += std::to_string(measured.cy / std::max(line, 1)) + "L/" + std::to_string(ink.bottom) + " ";
        }
        observed.back() = ')';
    };
    std::vector<const std::wstring*> instructions{&text.instructions};
    if (!text.otherSignIn.empty()) instructions.push_back(&text.otherSignIn);
    statics(kLoginInstructions, "instructions", instructions);
    if (text.device) {
        std::vector<const std::wstring*> statuses;
        for (const std::wstring& status : text.statuses) statuses.push_back(&status);
        statics(kLoginStatus, "status", statuses);
    }
    // Boxes: the text rectangle a line tall; the device code inside it.
    if (HWND box = w.Child(kLoginCode)) {
        RECT format{};
        SendMessageW(box, EM_GETRECT, 0, reinterpret_cast<LPARAM>(&format));
        const int line = ControlLineHeight(box);
        CHECK(format.bottom - format.top >= line);
        if (text.device) CHECK(ControlMeasure(box, text.userCode).cx <= format.right - format.left);
        observed += " box=" + Size({w.Rect(kLoginCode).right - w.Rect(kLoginCode).left,
                                    w.Rect(kLoginCode).bottom - w.Rect(kLoginCode).top}) +
                    "(text " + std::to_string(format.bottom - format.top) + ", line " + std::to_string(line) + ")";
    }
    return observed;
}

// The window's client as its controls paint it, each printed at its place; WM_PRINT leaves out a
// themed box's border, so a box gets a plain one.
static void PrintSignInWindow(const SignInWindow& w, HDC dc, int left, int top) {
    for (int id : kSignInControls) {
        HWND control = w.Child(id);
        if (!control) continue;
        RECT r = w.Rect(id);
        POINT origin{};
        SetViewportOrgEx(dc, left + r.left, top + r.top, &origin);
        SendMessageW(control, WM_PRINT, reinterpret_cast<WPARAM>(dc),
                     PRF_NONCLIENT | PRF_CLIENT | PRF_ERASEBKGND | PRF_CHILDREN);
        SetViewportOrgEx(dc, origin.x, origin.y, nullptr);
        if (id == kLoginCode) {
            RECT border{left + r.left, top + r.top, left + r.right, top + r.bottom};
            FrameRect(dc, &border, GetSysColorBrush(COLOR_BTNSHADOW));
        }
    }
}

static void SaveSignInPng(const SignInBitmap& sheet, const std::wstring& path) {
    winrt::com_ptr<IWICImagingFactory> wic;
    winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                          IID_PPV_ARGS(wic.put())));
    winrt::com_ptr<IWICBitmap> bitmap;
    winrt::check_hresult(wic->CreateBitmapFromHBITMAP(sheet.bitmap, nullptr, WICBitmapIgnoreAlpha, bitmap.put()));
    winrt::com_ptr<IWICStream> stream;
    winrt::check_hresult(wic->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
    winrt::check_hresult(frame->Initialize(nullptr));
    winrt::check_hresult(frame->SetSize(UINT(sheet.width), UINT(sheet.height)));
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    winrt::check_hresult(frame->SetPixelFormat(&format));
    winrt::check_hresult(frame->WriteSource(bitmap.get(), nullptr));
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(encoder->Commit());
}

// One sheet per language and DPI: each flow as it opens, then the states that change text (a
// code from another sign-in with Copy failed; a device sign-in ended on its tallest status with
// Code copied and Close), each under its name.
static void RenderSignInSheet(const std::wstring& directory, PCWSTR tag, UINT dpi) {
    const I18nLocale locale(*I18nLanguageByTag(tag));
    struct Row {
        std::string name;
        std::unique_ptr<SignInWindow> window;
    };
    std::vector<LoginWindowText> texts;
    for (const auto& flow : kSignInFlows) texts.push_back(SignInText(locale, flow.first));
    std::vector<Row> rows;
    for (size_t i = 0; i < std::size(kSignInFlows); i++) {
        rows.push_back({kSignInFlows[i].second, std::make_unique<SignInWindow>(texts[i], dpi)});
        const LoginWindowText& text = texts[i];
        const SignInWindow& w = *rows.back().window;
        if (kSignInFlows[i].first == SignInFlow::OpenCode) {
            SetWindowTextW(w.Child(kLoginCode), L"sk-0123456789abcdef");
        } else if (kSignInFlows[i].first == SignInFlow::AnthropicCode) {
            rows.push_back({"anthropic-code: another sign-in's code", std::make_unique<SignInWindow>(text, dpi)});
            const SignInWindow& other = *rows.back().window;
            SetWindowTextW(other.Child(kLoginInstructions), text.otherSignIn.c_str());
            SetWindowTextW(other.Child(kLoginCopyLink), text.labels.copyFailed.c_str());
            SetWindowTextW(other.Child(kLoginCode), L"Kq3vX9#OtherState");
        } else if (kSignInFlows[i].first == SignInFlow::Copilot) {
            rows.push_back({"copilot: ended (tallest status)", std::make_unique<SignInWindow>(text, dpi)});
            const SignInWindow& ended = *rows.back().window;
            HWND status = ended.Child(kLoginStatus);
            const std::wstring* tallest = &text.statuses[0];
            RECT r = ended.Rect(kLoginStatus);
            for (const std::wstring& line : text.statuses) {
                if (ControlMeasure(status, line, r.right - r.left).cy >
                    ControlMeasure(status, *tallest, r.right - r.left).cy) {
                    tallest = &line;
                }
            }
            SetWindowTextW(status, tallest->c_str());
            SetWindowTextW(ended.Child(kLoginCopyLink), text.labels.copied.c_str());
            EnableWindow(ended.Child(kLoginCopyLink), FALSE);
            SetWindowTextW(ended.Child(IDCANCEL), text.labels.close.c_str());
        }
    }
    const int pad = MulDiv(8, int(dpi), 96), caption = MulDiv(20, int(dpi), 96);
    int width = 0, height = pad;
    for (const Row& row : rows) {
        width = std::max(width, int(row.window->Client().cx) + 2 * pad);
        height += caption + int(row.window->Client().cy) + pad;
    }
    SignInBitmap sheet(width, height, RGB(0xFF, 0xFF, 0xFF));
    HFONT label = CreateFontW(-MulDiv(12, int(dpi), 96), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HGDIOBJ previous = SelectObject(sheet.dc, label);
    SetBkMode(sheet.dc, TRANSPARENT);
    int y = pad;
    HBRUSH face = GetSysColorBrush(COLOR_3DFACE);
    for (const Row& row : rows) {
        std::wstring name(row.name.begin(), row.name.end());
        TextOutW(sheet.dc, pad, y, name.c_str(), int(name.size()));
        y += caption;
        SIZE client = row.window->Client();
        RECT area{pad, y, pad + client.cx, y + client.cy};
        FillRect(sheet.dc, &area, face);
        PrintSignInWindow(*row.window, sheet.dc, pad, y);
        y += client.cy + pad;
    }
    SelectObject(sheet.dc, previous);
    DeleteObject(label);
    GdiFlush();
    std::wstring path = directory + L"\\render-signin-" + tag + L"-" + std::to_wstring(dpi) + L".png";
    SaveSignInPng(sheet, path);
    std::cout << "RENDER " << Narrow(path) << '\n';
}

static void RunSignInProbe(const std::wstring& directory) {
    HWND probe = CreateWindowExW(0, kSignInProbeClass, L"", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr,
                                 GetModuleHandleW(nullptr), nullptr);
    const UINT monitorDpi = GetDpiForWindow(probe);
    DestroyWindow(probe);
    std::cout << "OBSERVE signin monitor DPI " << monitorDpi << '\n';

    for (PCWSTR tag : kSignInTags) {
        for (UINT dpi : {96u, 144u}) {
            Run("sign-in window " + Narrow(tag) + " at " + std::to_string(dpi) +
                    " DPI: every flow's buttons fit their ideal size in every label, statics paint every text inside "
                    "their rectangle, boxes a line tall, nothing overlaps",
                [&] {
                    const I18nLocale locale(*I18nLanguageByTag(tag));
                    for (const auto& [flow, name] : kSignInFlows) {
                        LoginWindowText text = SignInText(locale, flow);
                        SignInWindow w(text, dpi);
                        std::string observed = CheckSignInWindow(w, text);
                        std::cout << "OBSERVE signin " << Narrow(tag) << ' ' << dpi << ' ' << name << ' ' << observed
                                  << '\n';
                    }
                });
        }
    }

    Run("sign-in window: English keeps the fixed geometry it had at 96, 120, 144 and 192 DPI in every flow", [&] {
        // DIPs, each edge scaled: instructions 424 x 52 at (12, 10); the link buttons 120, 120 and
        // Use a code 82 wide, Cancel 84 at the right, all 30 tall at y 68; a paste flow's box 424 x 24 at
        // y 110 and Sign in, Cancel at y 146; the device flow's code at y 68, status 424 x 20 at
        // y 100, its 246-wide button and Cancel at y 128; clients 448 x 108, 186 and 170.
        struct Expected {
            int id, x, y, w, h;
        };
        const std::vector<Expected> link = {{kLoginInstructions, 12, 10, 424, 52}, {kLoginOpenLink, 12, 68, 120, 30},
                                            {kLoginCopyLink, 138, 68, 120, 30}, {IDCANCEL, 352, 68, 84, 30}};
        const std::vector<Expected> alternate = {{kLoginAlternate, 264, 68, 82, 30}};
        const std::vector<Expected> paste = {{kLoginInstructions, 12, 10, 424, 52}, {kLoginOpenLink, 12, 68, 120, 30},
                                             {kLoginCopyLink, 138, 68, 120, 30}, {kLoginCode, 12, 110, 424, 24},
                                             {IDOK, 262, 146, 84, 30}, {IDCANCEL, 352, 146, 84, 30}};
        const std::vector<Expected> device = {{kLoginInstructions, 12, 10, 424, 52}, {kLoginCode, 12, 68, 424, 24},
                                              {kLoginStatus, 12, 100, 424, 20}, {kLoginCopyLink, 12, 128, 246, 30},
                                              {IDCANCEL, 352, 128, 84, 30}};
        const I18nLocale english;
        for (UINT dpi : {96u, 120u, 144u, 192u}) {
            auto px = [dpi](int dips) { return MulDiv(dips, int(dpi), 96); };
            for (const auto& [flow, name] : kSignInFlows) {
                LoginWindowText text = SignInText(english, flow);
                SignInWindow w(text, dpi);
                std::vector<Expected> expected = text.device ? device : text.paste ? paste : link;
                if (!text.alternate.empty()) expected.insert(expected.end(), alternate.begin(), alternate.end());
                const int height = text.device ? 170 : text.paste ? 186 : 108;
                CHECK(w.Client().cx == px(448) && w.Client().cy == px(height));
                for (const Expected& e : expected) {
                    RECT r = w.Rect(e.id);
                    CHECK(r.left == px(e.x) && r.top == px(e.y) && r.right - r.left == px(e.w) &&
                          r.bottom - r.top == px(e.h));
                }
                std::cout << "OBSERVE signin en " << dpi << ' ' << name << ' ' << CheckSignInWindow(w, text) << '\n';
            }
        }
    });

    Run("sign-in window: a DPI change lays the window out as a new one at the new DPI, in a new font", [&] {
        for (PCWSTR tag : {L"en", L"de", L"qps-ploc"}) {
            const I18nLocale locale(*I18nLanguageByTag(tag));
            for (const auto& [flow, name] : kSignInFlows) {
                LoginWindowText text = SignInText(locale, flow);
                SignInWindow changed(text, 96), fresh(text, 144);
                HFONT before = changed.font;
                LayoutLoginControls(changed.window, text, 144, changed.font);
                CHECK(changed.font && changed.font != before);
                CHECK(changed.Client().cx == fresh.Client().cx && changed.Client().cy == fresh.Client().cy);
                for (int id : kSignInControls) {
                    RECT a = changed.Rect(id), b = fresh.Rect(id);
                    CHECK(EqualRect(&a, &b));
                    if (HWND control = changed.Child(id)) {
                        CHECK(reinterpret_cast<HFONT>(SendMessageW(control, WM_GETFONT, 0, 0)) == changed.font);
                    }
                }
            }
        }
    });

    Run("sign-in window: the production frame (caption, dialog frame, topmost) keeps the laid-out client at the "
        "window's own DPI", [&] {
        for (PCWSTR tag : {L"en", L"qps-ploc"}) {
            const I18nLocale locale(*I18nLanguageByTag(tag));
            for (const auto& [flow, name] : kSignInFlows) {
                LoginWindowText text = SignInText(locale, flow);
                SignInWindow framed(text, monitorDpi, WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                    WS_EX_TOPMOST | WS_EX_DLGMODALFRAME);
                SignInWindow bare(text, monitorDpi);
                CHECK(framed.Client().cx == bare.Client().cx && framed.Client().cy == bare.Client().cy);
            }
        }
    });

    for (PCWSTR tag : kSignInRenderTags) {
        for (UINT dpi : {96u, 144u}) RenderSignInSheet(directory, tag, dpi);
    }
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) {
        std::cerr << "Usage: signin-probe.exe <render directory>\n";
        return 2;
    }
    const std::wstring directory = argv[1];
    // The login thread's awareness.
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::check_hresult(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    // comctl32 6, as Explorer's manifest gives the sign-in window's controls.
    const std::wstring manifest = directory + L"\\signin-probe.manifest";
    if (FILE* file = _wfopen(manifest.c_str(), L"wb")) {
        std::fputs("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                   "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\"><dependency>"
                   "<dependentAssembly><assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" "
                   "version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" "
                   "language=\"*\"/></dependentAssembly></dependency></assembly>",
                   file);
        std::fclose(file);
    }
    ACTCTXW context{sizeof(context)};
    context.lpSource = manifest.c_str();
    HANDLE activation = CreateActCtxW(&context);
    ULONG_PTR cookie = 0;
    if (activation == INVALID_HANDLE_VALUE || !ActivateActCtx(activation, &cookie)) {
        std::cerr << "comctl32 6 activation context failed: " << GetLastError() << '\n';
        return 1;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kSignInProbeClass;
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wc)) {
        std::cerr << "RegisterClassExW failed: " << GetLastError() << '\n';
        return 1;
    }
    try {
        RunSignInProbe(directory);
    } catch (const std::exception& e) {
        ++failed;
        std::cout << "FAIL sign-in probe: " << e.what() << '\n';
    }
    std::cout << "RESULT " << passed << " passed, " << failed << " failed; " << assertions << " assertions\n";
    DeactivateActCtx(0, cookie);
    ReleaseActCtx(activation);
    CoUninitialize();
    return failed ? 1 : 0;
}
