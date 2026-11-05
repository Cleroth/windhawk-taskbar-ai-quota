// Sign-in thread: the sign-in window, the Anthropic and OpenAI loopback listeners, GitHub device
// flow, code exchange, and OpenCode Go's pasted API key.

struct LoginRequest {
    std::wstring provider;
    std::wstring label;
    uint64_t idHash = 0;
    ULONGLONG authEpoch = 0;
};

static std::atomic<bool> g_loginInProgress{false};
static std::atomic<uint64_t> g_loginAccountIdentity{0};
static HANDLE g_loginThread = nullptr;
static std::mutex g_loginThreadMutex;  // Guards g_loginThread handoff vs. the unload join.
static std::atomic<HWND> g_loginWnd{nullptr};        // Sign-in window.
static std::atomic<SOCKET> g_loginSocket{INVALID_SOCKET};  // Loopback listener (Anthropic, OpenAI).

// Listens on 127.0.0.1 at the first of `ports` that binds (0: a free one Windows picks), held
// exclusively so no other process can bind it as well (RFC 8252, B.3).
static bool StartLoopback(std::initializer_list<int> ports, SOCKET* outSock, int* outPort) {
    for (int port : ports) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) continue;
        BOOL exclusive = TRUE;
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&exclusive, sizeof(exclusive));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int length = sizeof(addr);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) == 0 && listen(s, 1) == 0 &&
            getsockname(s, (sockaddr*)&addr, &length) == 0) {
            *outSock = s;
            *outPort = ntohs(addr.sin_port);
            return true;
        }
        closesocket(s);
    }
    return false;
}

// Answers one localhost request and closes it. Returns true once the sign-in is decided:
// `code` receives the URL-decoded authorization code, or `terminalError` the failure. Stray
// hits (favicon, other paths, etc.) return false so the sign-in keeps waiting.
static bool AnswerLoopbackCallback(SOCKET client, const std::string& expectedRoute,
                                   const std::string& expectedState, std::string* code,
                                   std::wstring* terminalError) {
    auto getParam = [](const std::string& query, const std::string& key) -> std::string {
        std::string pat = key + "=";
        for (size_t p = 0; (p = query.find(pat, p)) != std::string::npos; p += pat.size()) {
            if (p == 0 || query[p - 1] == '&' || query[p - 1] == '?') {
                size_t v = p + pat.size();
                size_t end = query.find('&', v);
                return UrlDecode(query.substr(v, end == std::string::npos ? std::string::npos : end - v));
            }
        }
        return {};
    };

    DWORD rcvTimeout = 3000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (char*)&rcvTimeout, sizeof(rcvTimeout));
    std::string request;
    char buf[2048];
    for (int i = 0; i < 16; i++) {
        int n = recv(client, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, n);
        if (request.find("\r\n\r\n") != std::string::npos || request.size() > 16384) break;
    }

    std::string path;
    size_t sp1 = request.find(' ');
    size_t sp2 = sp1 == std::string::npos ? std::string::npos : request.find(' ', sp1 + 1);
    if (sp1 != std::string::npos && sp2 != std::string::npos) {
        path = request.substr(sp1 + 1, sp2 - sp1 - 1);
    }
    size_t q = path.find('?');
    std::string route = q == std::string::npos ? path : path.substr(0, q);
    std::string query = q == std::string::npos ? "" : path.substr(q + 1);
    std::string received = getParam(query, "code");
    std::string state = getParam(query, "state");
    std::string oauthError = getParam(query, "error");
    std::string oauthErrorDesc = getParam(query, "error_description");
    bool callback = route == expectedRoute;
    bool stateOk = callback && (expectedState.empty() || state == expectedState);
    bool success = !received.empty() && stateOk;
    bool terminalFailure = stateOk && (received.empty() || !oauthError.empty());

    std::string html = success
        ? "<!doctype html><meta charset='utf-8'><body style='font-family:sans-serif;padding:2em'>"
          "<h3>Signed in.</h3><p>You can close this tab and return to the taskbar.</p></body>"
        : "<!doctype html><meta charset='utf-8'><body style='font-family:sans-serif;padding:2em'>"
          "<h3>Sign-in failed.</h3><p>You can close this tab and try again from the taskbar.</p></body>";
    std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                       "Connection: close\r\nContent-Length: " + std::to_string(html.size()) +
                       "\r\n\r\n" + html;
    send(client, resp.data(), (int)resp.size(), 0);
    closesocket(client);

    if (success) {
        *code = std::move(received);
        return true;
    }
    if (terminalFailure) {
        if (!oauthErrorDesc.empty()) *terminalError = Utf8ToWide(oauthErrorDesc);
        else if (!oauthError.empty()) *terminalError = Utf8ToWide(oauthError);
        else *terminalError = L"missing authorization code";
        return true;
    }
    return false;
}

// Sign-in window: offers the authorize link to copy or open, then either collects the pasted
// Anthropic code#state or OpenCode Go API key (masked) or waits for the Anthropic or OpenAI
// loopback callback; for GitHub's device flow it shows the code to enter and a status line while
// its message loop polls for the token. Runs its own message loop on the login thread; closed on
// cancel, submit, callback, its alternate button, a granted device sign-in, or unload. A device
// sign-in that ends otherwise stays open with the reason until closed.
enum {
    kLoginInstructions = 100,
    kLoginCode,
    kLoginCopyLink,
    kLoginOpenLink,
    kLoginStatus,
    kLoginAlternate,
};
constexpr UINT kLoginAccept = WM_APP + 1;  // FD_ACCEPT notification for the loopback listener.

struct LoginDialogState {
    // Inputs
    std::wstring url;  // The authorize link, or the device flow's verification page.
    // Loopback flow when set, answered on `callbackRoute`; device flow with `poll`; otherwise a
    // code is pasted, masked when `secret`.
    SOCKET listener = INVALID_SOCKET;
    std::string callbackRoute;
    bool secret = false;
    std::string expectedState;
    // A loopback flow's button that closes the window to sign in another way (alternateChosen).
    PCWSTR alternate = nullptr;
    // Device flow: the code to enter on the verification page, and the token poll the message
    // loop runs, never the window procedure, once GetTickCount64 reaches nextPollMs. It catches
    // everything itself, may set `status`, which the loop then shows, and returns the wait before
    // the next poll, or 0 when the sign-in ended: granted if it set ok, else the window keeps the
    // status, without polling, until closed. A fixed buffer, so showing it allocates nothing
    // outside the poll's own catch.
    std::wstring userCode;
    std::function<ULONGLONG()> poll;
    ULONGLONG nextPollMs = 0;
    wchar_t status[128] = L"";

    // Results
    std::wstring pastedCode;
    std::string callbackCode;
    std::wstring callbackError;
    bool alternateChosen = false;
    bool ok = false;
    bool done = false;

    // Window
    HWND edit = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    std::array<HICON, 2> icons{};  // App icon (ICON_SMALL, ICON_BIG), destroyed after the window
};

static void LayoutLoginDialog(HWND hWnd, LoginDialogState& state, UINT dpi) {
    auto sc = [dpi](int value) { return ScaleForDpi(value, dpi); };
    constexpr UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    // The device flow's code and status line sit between the instructions and the buttons.
    int buttonsTop = state.poll ? 128 : 68;
    SetWindowPos(GetDlgItem(hWnd, kLoginInstructions), nullptr, sc(12), sc(10), sc(424), sc(52), flags);
    if (state.poll) {
        SetWindowPos(GetDlgItem(hWnd, kLoginCode), nullptr, sc(12), sc(68), sc(424), sc(24), flags);
        SetWindowPos(GetDlgItem(hWnd, kLoginStatus), nullptr, sc(12), sc(100), sc(424), sc(20), flags);
    }
    // The device flow's one button spans the link flows' two.
    SetWindowPos(GetDlgItem(hWnd, kLoginCopyLink), nullptr, sc(12), sc(buttonsTop), sc(state.poll ? 246 : 120),
                 sc(30), flags);
    if (!state.poll) {
        SetWindowPos(GetDlgItem(hWnd, kLoginOpenLink), nullptr, sc(138), sc(buttonsTop), sc(120), sc(30), flags);
    }
    if (state.alternate) {
        SetWindowPos(GetDlgItem(hWnd, kLoginAlternate), nullptr, sc(264), sc(buttonsTop), sc(82), sc(30), flags);
    }
    if (state.edit) {
        SetWindowPos(state.edit, nullptr, sc(12), sc(110), sc(424), sc(24), flags);
        SetWindowPos(GetDlgItem(hWnd, IDOK), nullptr, sc(262), sc(146), sc(84), sc(30), flags);
    }
    SetWindowPos(GetDlgItem(hWnd, IDCANCEL), nullptr, sc(352), sc(state.edit ? 146 : buttonsTop),
                 sc(84), sc(30), flags);

    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    LOGFONTW fontDescription{};
    using SystemParametersInfoForDpi_t = BOOL(WINAPI*)(UINT, UINT, PVOID, UINT, UINT);
    auto systemParametersInfoForDpi = reinterpret_cast<SystemParametersInfoForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SystemParametersInfoForDpi"));
    if (systemParametersInfoForDpi &&
        systemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                                   &metrics, 0, dpi)) {
        fontDescription = metrics.lfMessageFont;
    } else {
        fontDescription.lfHeight = -MulDiv(9, (int)dpi, 72);
        fontDescription.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(fontDescription.lfFaceName, L"Segoe UI");
    }
    HFONT font = CreateFontIndirectW(&fontDescription);
    if (font) {
        for (int id : {(int)kLoginInstructions, (int)kLoginCode, (int)kLoginCopyLink,
                       (int)kLoginOpenLink, (int)kLoginStatus, (int)kLoginAlternate, IDOK, IDCANCEL}) {
            if (HWND child = GetDlgItem(hWnd, id)) {
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            }
        }
        if (state.font) DeleteObject(state.font);
        state.font = font;
    }
    state.dpi = dpi;
}

static LRESULT CALLBACK LoginDlgProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<LoginDialogState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    switch (msg) {
        case WM_CREATE: {
            st = reinterpret_cast<LoginDialogState*>(
                reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
            if (!st) return -1;
            SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
            if (st->listener != INVALID_SOCKET &&
                WSAAsyncSelect(st->listener, hWnd, kLoginAccept, FD_ACCEPT) != 0) {
                Wh_Log(L"Sign-in: could not watch the localhost listener (%d)", WSAGetLastError());
                return -1;
            }
            HINSTANCE hInst = GetModuleHandleW(nullptr);
            auto create = [&](DWORD exStyle, PCWSTR cls, PCWSTR text, DWORD style, int id) {
                return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                                       0, 0, 0, 0, hWnd, reinterpret_cast<HMENU>((INT_PTR)id),
                                       hInst, nullptr);
            };
            create(0, L"STATIC", L"", SS_LEFT, kLoginInstructions);
            if (st->poll) {
                // Read-only but selectable, so the code can also be copied by hand.
                create(WS_EX_CLIENTEDGE, L"EDIT", st->userCode.c_str(),
                       WS_TABSTOP | ES_READONLY | ES_CENTER | ES_AUTOHSCROLL, kLoginCode);
                create(0, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS | SS_NOPREFIX, kLoginStatus);
            }
            // The device flow's one button copies the code and opens the verification page.
            if (st->poll) {
                create(0, L"BUTTON", L"Copy code and open GitHub", WS_TABSTOP | BS_DEFPUSHBUTTON, kLoginCopyLink);
            } else {
                create(0, L"BUTTON", L"Copy link", WS_TABSTOP | BS_PUSHBUTTON, kLoginCopyLink);
                create(0, L"BUTTON", L"Open in browser", WS_TABSTOP | BS_PUSHBUTTON, kLoginOpenLink);
            }
            if (st->alternate) create(0, L"BUTTON", st->alternate, WS_TABSTOP | BS_PUSHBUTTON, kLoginAlternate);
            if (st->listener == INVALID_SOCKET && !st->poll) {
                st->edit = create(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                  WS_TABSTOP | ES_AUTOHSCROLL | (st->secret ? ES_PASSWORD : 0), kLoginCode);
                create(0, L"BUTTON", L"Sign in", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
            }
            create(0, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
            return 0;
        }
        case WM_COMMAND: {
            if (!st) break;
            switch (LOWORD(wParam)) {
                case kLoginCopyLink: {
                    const std::wstring& text = st->poll ? st->userCode : st->url;
                    bool copied = false;
                    if (OpenClipboard(hWnd)) {
                        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
                        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                            if (void* data = GlobalLock(mem)) {
                                memcpy(data, text.c_str(), bytes);
                                GlobalUnlock(mem);
                                copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, mem);
                            }
                            if (!copied) GlobalFree(mem);
                        }
                        CloseClipboard();
                    }
                    SetWindowTextW(GetDlgItem(hWnd, kLoginCopyLink),
                                   !copied ? L"Copy failed" : st->poll ? L"Code copied" : L"Copied");
                    if (st->poll) OpenUrl(st->url.c_str());
                    return 0;
                }
                case kLoginOpenLink:
                    OpenUrl(st->url.c_str());
                    return 0;
                case kLoginAlternate:
                    st->alternateChosen = true;
                    DestroyWindow(hWnd);
                    return 0;
                case IDOK: {
                    if (!st->edit) return 0;
                    int len = GetWindowTextLengthW(st->edit);
                    std::wstring buf(len + 1, L'\0');
                    GetWindowTextW(st->edit, buf.data(), len + 1);
                    buf.resize(len);
                    st->pastedCode = std::move(buf);
                    st->ok = true;
                    DestroyWindow(hWnd);
                    return 0;
                }
                case IDCANCEL:
                    DestroyWindow(hWnd);
                    return 0;
            }
            break;
        }
        case kLoginAccept: {
            if (!st || g_unloading || WSAGETSELECTEVENT(lParam) != FD_ACCEPT) return 0;
            SOCKET client = accept(st->listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) return 0;
            // Accepted sockets inherit the listener's async notifications and non-blocking
            // mode; make this one blocking so its receive timeout applies.
            WSAAsyncSelect(client, hWnd, 0, 0);
            u_long nonBlocking = 0;
            ioctlsocket(client, FIONBIO, &nonBlocking);
            if (AnswerLoopbackCallback(client, st->callbackRoute, st->expectedState, &st->callbackCode,
                                       &st->callbackError)) {
                st->ok = !st->callbackCode.empty();
                DestroyWindow(hWnd);
            }
            return 0;
        }
        case WM_DPICHANGED: {
            auto* suggested = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (st) {
                LayoutLoginDialog(hWnd, *st, HIWORD(wParam));
                SetAppWindowIcons(hWnd, HIWORD(wParam), st->icons);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
            UiSetWindowAppId(hWnd, nullptr);
            if (st) st->done = true;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Returns true once a code was pasted or the loopback callback delivered one.
static bool ShowLoginDialog(const std::wstring& title, const std::wstring& instructions,
                            LoginDialogState& st) {
    static PCWSTR kClass = L"AiQuotaLoginDlg_" WH_MOD_ID;
    HINSTANCE hInst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = LoginDlgProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&wc)) {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS ||
            !UnregisterClassW(kClass, hInst) || !RegisterClassExW(&wc)) {
            return false;
        }
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    if (!GetMonitorInfoW(monitor, &monitorInfo)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &monitorInfo.rcWork, 0);
    }
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_DLGMODALFRAME;
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    HWND wnd = CreateWindowExW(exStyle, kClass, title.c_str(),
                               style, monitorInfo.rcWork.left, monitorInfo.rcWork.top, 460, 224,
                               nullptr, nullptr, hInst, &st);
    if (!wnd) {
        UnregisterClassW(kClass, hInst);
        return false;
    }
    g_loginWnd.store(wnd);

    UINT dpi = WindowDpi(wnd);
    // The taskbar shows the app icon instead of File Explorer's (ui/window-icon.h).
    UiSetWindowAppId(wnd, kWindowAppId);
    SetAppWindowIcons(wnd, dpi, st.icons);
    RECT windowRect{0, 0, ScaleForDpi(448, dpi), ScaleForDpi(st.edit ? 186 : st.poll ? 170 : 108, dpi)};
    using AdjustWindowRectExForDpi_t = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    auto adjustWindowRectExForDpi = reinterpret_cast<AdjustWindowRectExForDpi_t>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (!adjustWindowRectExForDpi ||
        !adjustWindowRectExForDpi(&windowRect, style, FALSE, exStyle, dpi)) {
        AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);
    }
    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;
    int x = monitorInfo.rcWork.left +
            (monitorInfo.rcWork.right - monitorInfo.rcWork.left - width) / 2;
    int y = monitorInfo.rcWork.top +
            (monitorInfo.rcWork.bottom - monitorInfo.rcWork.top - height) / 2;
    SetWindowPos(wnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    LayoutLoginDialog(wnd, st, dpi);
    SetWindowTextW(GetDlgItem(wnd, kLoginInstructions), instructions.c_str());
    if (st.poll) SetWindowTextW(GetDlgItem(wnd, kLoginStatus), st.status);
    ShowWindow(wnd, SW_SHOW);
    SetForegroundWindow(wnd);
    SetFocus(st.edit ? st.edit : GetDlgItem(wnd, kLoginCopyLink));

    MSG msg;
    if (st.poll) {
        // Device flow: waits for input, unload or the next poll's time, and polls between pumps.
        // Input that arrived during a poll's request (Cancel, a close, unload's WM_CLOSE) is
        // handled before its result counts, so a sign-in closed meanwhile never completes. A
        // sign-in that ended without a token stops polling and waits, its reason shown, for the
        // window to close.
        bool stopped = false;
        auto pump = [&] {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) return false;
                if (!IsDialogMessageW(wnd, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            return true;
        };
        while (!g_unloading && !st.done) {
            ULONGLONG now = GetTickCount64();
            if (stopped || now < st.nextPollMs) {
                MsgWaitForMultipleObjectsEx(1, &g_stopEvent, stopped ? INFINITE : (DWORD)(st.nextPollMs - now),
                                            QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (!pump()) break;
                continue;
            }
            ULONGLONG wait = st.poll();
            ULONGLONG ended = GetTickCount64();
            if (!pump() || st.done || g_unloading) {
                st.ok = false;
                break;
            }
            if (!wait && st.ok) {
                DestroyWindow(wnd);
                break;
            }
            HWND status = GetDlgItem(wnd, kLoginStatus);
            wchar_t shown[ARRAYSIZE(st.status)] = L"";
            GetWindowTextW(status, shown, ARRAYSIZE(shown));
            if (wcscmp(shown, st.status) != 0) SetWindowTextW(status, st.status);
            if (!wait) {
                // Focus moves to Close before the button it could be on is disabled.
                stopped = true;
                HWND close = GetDlgItem(wnd, IDCANCEL);
                SetWindowTextW(close, L"Close");
                SetFocus(close);
                EnableWindow(GetDlgItem(wnd, kLoginCopyLink), FALSE);
                continue;
            }
            st.nextPollMs = ended + wait;
        }
    } else {
        while (!g_unloading) {
            int getMessageResult = GetMessageW(&msg, nullptr, 0, 0);
            if (getMessageResult <= 0) break;
            if (!IsDialogMessageW(wnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            if (st.done) break;
        }
    }
    g_loginWnd.store(nullptr);
    if (IsWindow(wnd)) DestroyWindow(wnd);
    if (st.font) DeleteObject(st.font);
    for (HICON icon : st.icons) {
        if (icon) DestroyIcon(icon);
    }
    // Unregister so a later mod reload can't reuse a class pointing at this now-unloaded WndProc.
    UnregisterClassW(kClass, hInst);
    return st.ok;
}

// On this PC the browser hands the code to a localhost listener, as Claude Code's own sign-in
// does; "Use a code" (or no listener) switches to the page that shows a code#state to paste, for
// another device. The exchange repeats the redirect its authorize link carried.
static void DoAnthropicLogin(const LoginRequest& req) {
    std::string verifier = RandomBase64Url(32);
    std::string challenge = Sha256Base64Url(verifier);
    std::string state = RandomBase64Url(32);
    if (verifier.empty() || challenge.empty() || state.empty()) {
        Wh_Log(L"Sign-in: crypto init failed");
        return;
    }

    std::wstring title = L"Sign in: " + req.label + L" (Anthropic)";
    std::string redirect;
    std::wstring code;
    SOCKET listener = INVALID_SOCKET;
    int port = 0;
    bool useCode = !StartLoopback({0}, &listener, &port);
    if (useCode) {
        Wh_Log(L"Sign-in [%s]: no localhost listener; asking for a code", req.label.c_str());
    } else {
        g_loginSocket.store(listener);
        redirect = "http://localhost:" + std::to_string(port) + "/callback";
        LoginDialogState dialog;
        dialog.url = BuildAnthropicAuthorizeUrl(redirect, challenge, state);
        dialog.listener = listener;
        dialog.callbackRoute = "/callback";
        dialog.expectedState = state;
        dialog.alternate = L"Use a code";
        bool received = ShowLoginDialog(
            title,
            L"Open the sign-in link in a browser on this PC and approve access on claude.ai. This "
            L"window closes once sign-in completes. On another device, choose Use a code.",
            dialog);
        SOCKET s = g_loginSocket.exchange(INVALID_SOCKET);
        if (s != INVALID_SOCKET) closesocket(s);
        if (g_unloading) return;
        useCode = dialog.alternateChosen;
        if (!received && !useCode) {
            if (!dialog.callbackError.empty()) {
                Wh_Log(L"Sign-in [%s] failed: %s", req.label.c_str(), dialog.callbackError.c_str());
            } else {
                Wh_Log(L"Sign-in [%s]: cancelled", req.label.c_str());
            }
            return;
        }
        code = Utf8ToWide(dialog.callbackCode);
    }
    if (useCode) {
        redirect = WideToUtf8(kAnthropicRedirect);
        LoginDialogState dialog;
        dialog.url = BuildAnthropicAuthorizeUrl(redirect, challenge, state);
        std::wstring instructions =
            L"Open the sign-in link in a browser and approve access on claude.ai, then paste "
            L"the code shown on the page below.";
        if (!ShowLoginDialog(title, instructions, dialog) || g_unloading) return;

        const std::wstring& pasted = dialog.pastedCode;
        size_t b = pasted.find_first_not_of(L" \t\r\n");
        size_t e = pasted.find_last_not_of(L" \t\r\n");
        code = b == std::wstring::npos ? L"" : pasted.substr(b, e - b + 1);
        size_t hashPos = code.find(L'#');  // pasted value is code#state.
        if (hashPos != std::wstring::npos) code = code.substr(0, hashPos);
    }
    if (code.empty()) {
        Wh_Log(L"Sign-in [%s]: no code entered", req.label.c_str());
        return;
    }

    std::string body;
    try {
        JsonObject obj;
        obj.SetNamedValue(L"grant_type", JsonValue::CreateStringValue(L"authorization_code"));
        obj.SetNamedValue(L"client_id", JsonValue::CreateStringValue(winrt::hstring(kAnthropicClientId)));
        obj.SetNamedValue(L"code", JsonValue::CreateStringValue(winrt::hstring(code)));
        obj.SetNamedValue(L"redirect_uri", JsonValue::CreateStringValue(winrt::hstring(Utf8ToWide(redirect))));
        obj.SetNamedValue(L"code_verifier", JsonValue::CreateStringValue(winrt::hstring(Utf8ToWide(verifier))));
        obj.SetNamedValue(L"state", JsonValue::CreateStringValue(winrt::hstring(Utf8ToWide(state))));
        body = WideToUtf8(std::wstring(obj.Stringify().c_str()));
    } catch (...) {
        Wh_Log(L"Sign-in [%s]: internal error", req.label.c_str());
        return;
    }

    StoredToken tok;
    std::wstring err;
    if (PostTokenEndpoint(/*anthropic*/ true, L"application/json", body, &tok, &err) ==
        TokenEndpointResult::Success) {
        TokenSaveResult saved = SaveStoredTokenIfCurrent(req.idHash, req.authEpoch, tok);
        if (saved == TokenSaveResult::Stale) {
            Wh_Log(L"Sign-in [%s]: cancelled before saving token", req.label.c_str());
            return;
        }
        if (saved != TokenSaveResult::Saved) {
            Wh_Log(L"Sign-in [%s] failed: could not save token", req.label.c_str());
            return;
        }
        RefreshQuotaByIdentity(req.idHash);
        Wh_Log(L"Sign-in [%s]: success", req.label.c_str());
    } else {
        Wh_Log(L"Sign-in [%s] failed: %s", req.label.c_str(), err.c_str());
    }
}

static void DoOpenAiLogin(const LoginRequest& req) {
    std::string verifier = RandomBase64Url(32);
    std::string challenge = Sha256Base64Url(verifier);
    std::string state = RandomBase64Url(32);
    if (verifier.empty() || challenge.empty() || state.empty()) {
        Wh_Log(L"Sign-in: crypto init failed");
        return;
    }

    SOCKET listener = INVALID_SOCKET;
    int port = 0;
    if (!StartLoopback({1455, 1457}, &listener, &port)) {
        Wh_Log(L"Sign-in [%s]: could not bind localhost:1455/1457 (Codex running?)",
               req.label.c_str());
        return;
    }
    g_loginSocket.store(listener);
    LoginDialogState dialog;
    dialog.callbackRoute = "/auth/callback";
    dialog.url = BuildOpenAiAuthorizeUrl(port, challenge, state);
    dialog.listener = listener;
    dialog.expectedState = state;
    std::wstring title = L"Sign in: " + req.label + L" (OpenAI)";
    std::wstring instructions =
        L"Open the sign-in link in a browser on this PC and approve access. This window "
        L"closes once sign-in completes.";
    bool received = ShowLoginDialog(title, instructions, dialog);
    SOCKET s = g_loginSocket.exchange(INVALID_SOCKET);
    if (s != INVALID_SOCKET) closesocket(s);
    if (g_unloading) return;
    if (!received) {
        if (!dialog.callbackError.empty()) {
            Wh_Log(L"Sign-in [%s] failed: %s", req.label.c_str(), dialog.callbackError.c_str());
        } else {
            Wh_Log(L"Sign-in [%s]: cancelled", req.label.c_str());
        }
        return;
    }

    std::string redirect = "http://localhost:" + std::to_string(port) + "/auth/callback";
    std::string formBody = "grant_type=authorization_code&code=" + UrlEncode(dialog.callbackCode) +
                           "&redirect_uri=" + UrlEncode(redirect) +
                           "&client_id=" + UrlEncode(WideToUtf8(kOpenAiClientId)) +
                           "&code_verifier=" + UrlEncode(verifier);

    StoredToken tok;
    std::wstring err;
    if (PostTokenEndpoint(/*anthropic*/ false, L"application/x-www-form-urlencoded", formBody,
                          &tok, &err) == TokenEndpointResult::Success) {
        TokenSaveResult saved = SaveStoredTokenIfCurrent(req.idHash, req.authEpoch, tok);
        if (saved == TokenSaveResult::Stale) {
            Wh_Log(L"Sign-in [%s]: cancelled before saving token", req.label.c_str());
            return;
        }
        if (saved != TokenSaveResult::Saved) {
            Wh_Log(L"Sign-in [%s] failed: could not save token", req.label.c_str());
            return;
        }
        RefreshQuotaByIdentity(req.idHash);
        Wh_Log(L"Sign-in [%s]: success", req.label.c_str());
    } else {
        Wh_Log(L"Sign-in [%s] failed: %s", req.label.c_str(), err.c_str());
    }
}

// GitHub device flow (copilot.vim's client): the window shows the code to enter on the fixed
// verification page while its message loop polls the token endpoint at GitHub's interval, until
// the user approves, the code expires, GitHub ends it or the window closes.
static void DoCopilotLogin(const LoginRequest& req) {
    const std::wstring headers =
        L"Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n";
    std::string clientId = "client_id=" + UrlEncode(WideToUtf8(kCopilotClientId));
    HttpResult r = HttpRequest(L"POST", L"github.com", L"/login/device/code", kOAuthUserAgent, headers,
                               clientId + "&scope=" + UrlEncode(WideToUtf8(kCopilotScope)));
    if (g_unloading) return;
    std::wstring deviceCode, userCode;
    double expiresIn = 0, interval = 0;
    if (r.ok && r.status == 200) {
        try {
            auto root = JsonObject::Parse(Utf8ToWide(r.body));
            deviceCode = GetStr(root, L"device_code");
            userCode = GetStr(root, L"user_code");
            expiresIn = GetNum(root, L"expires_in", 0);
            interval = GetNum(root, L"interval", 5);
        } catch (...) {}
    }
    if (deviceCode.empty() || deviceCode.size() > 1024 || userCode.empty() || userCode.size() > 32 ||
        !std::isfinite(expiresIn) || expiresIn <= 0 || !std::isfinite(interval)) {
        std::wstring detail = r.ok ? ParseOAuthError(r.body) : L"network error";
        if (detail.empty()) detail = L"HTTP " + std::to_wstring(r.status);
        Wh_Log(L"Sign-in [%s] failed: no device code (%s)", req.label.c_str(), detail.c_str());
        return;
    }
    // At most 15 minutes, polled no faster than every 5 seconds.
    ULONGLONG deadline = GetTickCount64() + (ULONGLONG)(std::min(expiresIn, 900.0) * 1000);
    ULONGLONG intervalMs = (ULONGLONG)(std::clamp(interval, 5.0, 900.0) * 1000);
    std::string pollBody = clientId + "&device_code=" + UrlEncode(WideToUtf8(deviceCode)) +
                           "&grant_type=" + UrlEncode("urn:ietf:params:oauth:grant-type:device_code");

    StoredToken tok;
    bool ended = false;  // The poll ended the sign-in and logged why.
    // The last transient failure: logged once per run of them, and named if the code expires.
    std::wstring transient;
    // The status line: waiting, retrying after a transient failure, or a terminal end's reason.
    static constexpr PCWSTR kWaiting = L"Waiting for approval on GitHub\u2026";
    static constexpr PCWSTR kExpired = L"The code expired. Close this window and sign in again.";
    static constexpr PCWSTR kFailed = L"Sign-in failed. See the Windhawk log for details.";
    LoginDialogState dialog;
    dialog.url = kCopilotVerificationUrl;
    dialog.userCode = userCode;
    dialog.nextPollMs = GetTickCount64() + intervalMs;
    wcsncpy_s(dialog.status, kWaiting, _TRUNCATE);
    dialog.poll = [&]() -> ULONGLONG {
        try {
            if (GetTickCount64() >= deadline) {
                ended = true;
                Wh_Log(L"Sign-in [%s] failed: the code expired%s", req.label.c_str(),
                       transient.empty() ? L"" : (L" (last poll: " + transient + L")").c_str());
                wcsncpy_s(dialog.status, kExpired, _TRUNCATE);
                return 0;
            }
            HttpResult poll = HttpRequest(L"POST", kCopilotTokenHost, kCopilotTokenPath, kOAuthUserAgent,
                                          headers, pollBody);
            // Network errors, 408, 429, 5xx and unreadable answers keep polling until the
            // deadline; only GitHub's own error codes end the sign-in.
            std::wstring failure;
            if (!poll.ok) {
                failure = L"network error";
            } else if (poll.status == 408 || poll.status == 429 || poll.status >= 500) {
                failure = L"HTTP " + std::to_wstring(poll.status);
            } else {
                try {
                    auto root = JsonObject::Parse(Utf8ToWide(poll.body));
                    std::wstring access = GetStr(root, L"access_token");
                    if (poll.status == 200 && !access.empty()) {
                        tok.accessToken = access;
                        tok.refreshToken = GetStr(root, L"refresh_token");
                        double expires = GetNum(root, L"expires_in", 0);
                        if (expires > 0 && expires < 1e9) tok.expiresMs = NowUnixMs() + (ULONGLONG)(expires * 1000);
                        dialog.ok = true;
                        return 0;
                    }
                    std::wstring code = GetStr(root, L"error");
                    if (code == L"slow_down") {
                        // Only ever slower: 5 seconds more, or GitHub's new interval when longer.
                        double next = GetNum(root, L"interval", 0);
                        intervalMs = std::max(intervalMs + 5000,
                                              next > 0 && next <= 900 ? (ULONGLONG)(next * 1000) : 0ULL);
                    } else if (code.empty()) {
                        failure = L"unexpected token response";
                    } else if (code != L"authorization_pending") {
                        ended = true;
                        std::wstring detail = GetStr(root, L"error_description");
                        Wh_Log(L"Sign-in [%s] failed: %s", req.label.c_str(),
                               detail.empty() ? code.c_str() : detail.c_str());
                        PCWSTR reason = code == L"access_denied" ? L"Sign-in was denied on GitHub." :
                                        code == L"expired_token" ? kExpired : kFailed;
                        wcsncpy_s(dialog.status, reason, _TRUNCATE);
                        return 0;
                    }
                } catch (winrt::hresult_error const&) {
                    failure = L"invalid token response";
                }
            }
            // A rate limit only ever slows polling down: 5 seconds more on 429, Retry-After when
            // longer.
            if (poll.ok) {
                ULONGLONG slower = poll.status == 429 ? intervalMs + 5000 : intervalMs;
                intervalMs = std::max(slower, poll.retryAfterSec > 0 ? (ULONGLONG)poll.retryAfterSec * 1000 : 0ULL);
            }
            if (failure.empty()) {
                transient.clear();
                wcsncpy_s(dialog.status, kWaiting, _TRUNCATE);
            } else {
                if (failure != transient) {
                    transient = failure;
                    Wh_Log(L"Sign-in [%s]: %s, polling on", req.label.c_str(), failure.c_str());
                }
                failure[0] = wchar_t(towupper(failure[0]));
                swprintf(dialog.status, ARRAYSIZE(dialog.status), L"%s, retrying\u2026", failure.c_str());
            }
            // Wakes at the expiry at the latest, where the next call ends without a request.
            ULONGLONG now = GetTickCount64();
            return now < deadline ? std::min(intervalMs, deadline - now) : 1;
        } catch (...) {
            ended = true;
            Wh_Log(L"Sign-in [%s] failed: invalid token response", req.label.c_str());
            wcsncpy_s(dialog.status, kFailed, _TRUNCATE);
            return 0;
        }
    };
    std::wstring title = L"Sign in: " + req.label + L" (GitHub Copilot)";
    std::wstring instructions =
        L"Copy the code and open GitHub below, or enter it at github.com/login/device on any device, "
        L"signed in to the GitHub account with Copilot. This window closes once sign-in completes.";
    bool granted = ShowLoginDialog(title, instructions, dialog);
    if (g_unloading) return;
    if (!granted) {
        if (!ended) Wh_Log(L"Sign-in [%s]: cancelled", req.label.c_str());
        return;
    }

    TokenSaveResult saved = SaveStoredTokenIfCurrent(req.idHash, req.authEpoch, tok);
    if (saved == TokenSaveResult::Stale) {
        Wh_Log(L"Sign-in [%s]: cancelled before saving token", req.label.c_str());
        return;
    }
    if (saved != TokenSaveResult::Saved) {
        Wh_Log(L"Sign-in [%s] failed: could not save token", req.label.c_str());
        return;
    }
    RefreshQuotaByIdentity(req.idHash);
    Wh_Log(L"Sign-in [%s]: success", req.label.c_str());
}

// OpenCode Go: an API key pasted from the console, stored as the access token without a refresh
// token or expiry; saving it requests a poll, whose answer is its only check.
static void DoOpenCodeLogin(const LoginRequest& req) {
    LoginDialogState dialog;
    dialog.url = kOpenCodeConsoleUrl;
    dialog.secret = true;
    std::wstring title = L"Sign in: " + req.label + L" (OpenCode Go)";
    std::wstring instructions =
        L"Open the OpenCode console in a browser, copy an API key of the workspace with your Go "
        L"subscription, and paste it below.";
    if (!ShowLoginDialog(title, instructions, dialog) || g_unloading) return;

    const std::wstring& pasted = dialog.pastedCode;
    size_t b = pasted.find_first_not_of(L" \t\r\n");
    size_t e = pasted.find_last_not_of(L" \t\r\n");
    StoredToken tok;
    if (b != std::wstring::npos) tok.accessToken = pasted.substr(b, e - b + 1);
    if (tok.accessToken.empty()) {
        Wh_Log(L"Sign-in [%s]: no key entered", req.label.c_str());
        return;
    }
    // A key is one printable token, far shorter than the token store's 16K characters.
    if (tok.accessToken.size() > 1024 ||
        std::any_of(tok.accessToken.begin(), tok.accessToken.end(),
                    [](wchar_t c) { return c <= L' ' || c == 0x7F; })) {
        Wh_Log(L"Sign-in [%s] failed: that is not an API key", req.label.c_str());
        return;
    }
    TokenSaveResult saved = SaveStoredTokenIfCurrent(req.idHash, req.authEpoch, tok);
    if (saved == TokenSaveResult::Stale) {
        Wh_Log(L"Sign-in [%s]: cancelled before saving key", req.label.c_str());
        return;
    }
    if (saved != TokenSaveResult::Saved) {
        Wh_Log(L"Sign-in [%s] failed: could not save key", req.label.c_str());
        return;
    }
    RefreshQuotaByIdentity(req.idHash);
    Wh_Log(L"Sign-in [%s]: key saved", req.label.c_str());
}

static DWORD WINAPI LoginThreadProc(LPVOID param) {
    std::unique_ptr<LoginRequest> req(reinterpret_cast<LoginRequest*>(param));
    using SetThreadDpiAwarenessContext_t = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        auto setDpi = reinterpret_cast<SetThreadDpiAwarenessContext_t>(
            GetProcAddress(user32, "SetThreadDpiAwarenessContext"));
        if (setDpi) setDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    }
    bool apartmentInitialized = false;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        apartmentInitialized = true;
    } catch (...) {}
    try {
        if (!g_unloading) {
            if (req->provider == L"anthropic") DoAnthropicLogin(*req);
            else if (req->provider == L"openai") DoOpenAiLogin(*req);
            else if (req->provider == L"copilot") DoCopilotLogin(*req);
            else if (req->provider == L"opencode") DoOpenCodeLogin(*req);
        }
    } catch (...) {
        Wh_Log(L"Sign-in: exception");
    }
    if (apartmentInitialized) winrt::uninit_apartment();
    g_loginAccountIdentity.store(0);
    g_loginInProgress.store(false);
    NotifySettingsWindowChanged();
    return 0;
}

// Kicks off a sign-in on a dedicated thread (the sign-in window's message loop blocks).
// One at a time; runs on a taskbar UI thread (menu click).
static void StartLoginByIdentity(uint64_t identityHash) {
    if (g_unloading) return;
    std::lock_guard<std::mutex> configLock(g_configEditMutex);
    AccountConfig account;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        auto it = std::find_if(g_settings.accounts.begin(), g_settings.accounts.end(),
            [&](const AccountConfig& candidate) {
                return AccountIdentityHash(candidate) == identityHash;
            });
        if (it == g_settings.accounts.end() ||
            (it->provider != L"anthropic" && it->provider != L"openai" && it->provider != L"copilot" &&
             it->provider != L"opencode")) {
            return;
        }
        account = *it;
    }
    bool expected = false;
    if (!g_loginInProgress.compare_exchange_strong(expected, true)) return;

    auto* req = new LoginRequest();
    req->provider = account.provider;
    req->label = account.label;
    req->idHash = identityHash;
    req->authEpoch = CurrentAuthEpoch(req->idHash);
    g_loginAccountIdentity.store(identityHash);
    NotifySettingsWindowChanged();

    // Hand off g_loginThread under the lock and re-check g_unloading: Wh_ModUninit sets
    // g_unloading before joining under the same lock, so we never spawn a thread into an
    // unloading DLL or leave a handle the join would miss. The previous login thread clears
    // g_loginInProgress shortly before it exits, so this sign-in is refused until it has.
    std::lock_guard<std::mutex> lk(g_loginThreadMutex);
    bool previousRunning = g_loginThread && WaitForSingleObject(g_loginThread, 0) != WAIT_OBJECT_0;
    if (g_unloading || previousRunning) {
        delete req;
        g_loginAccountIdentity.store(0);
        g_loginInProgress.store(false);
        NotifySettingsWindowChanged();
        return;
    }
    if (g_loginThread) {
        CloseHandle(g_loginThread);
        g_loginThread = nullptr;
    }
    g_loginThread = CreateThread(nullptr, 0, LoginThreadProc, req, 0, nullptr);
    if (!g_loginThread) {
        delete req;
        g_loginAccountIdentity.store(0);
        g_loginInProgress.store(false);
        NotifySettingsWindowChanged();
    }
}

static bool SignOutAccountByIdentity(uint64_t identityHash) {
    if (g_unloading) return false;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        auto it = std::find_if(g_settings.accounts.begin(), g_settings.accounts.end(),
            [&](const AccountConfig& account) {
                return AccountIdentityHash(account) == identityHash;
            });
        if (it == g_settings.accounts.end() || it->provider == L"antigravity") return false;
    }
    bool cleared = ClearStoredTokenAndBumpAuthEpoch(identityHash);
    RefreshQuotaByIdentity(identityHash);  // Re-fetch so the column flips to "not signed in".
    NotifySettingsWindowChanged();
    return cleared;
}
