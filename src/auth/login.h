// Sign-in thread: the sign-in window, OpenAI loopback listener, and code exchange.

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
static std::atomic<SOCKET> g_loginSocket{INVALID_SOCKET};  // OpenAI loopback listener.

static bool StartLoopback(SOCKET* outSock, int* outPort) {
    for (int port : {1455, 1457}) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) continue;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) == 0 && listen(s, 1) == 0) {
            *outSock = s;
            *outPort = port;
            return true;
        }
        closesocket(s);
    }
    return false;
}

// Answers one localhost request and closes it. Returns true once the sign-in is decided:
// `code` receives the URL-decoded authorization code, or `terminalError` the failure. Stray
// hits (favicon, etc.) return false so the sign-in keeps waiting.
static bool AnswerLoopbackCallback(SOCKET client, const std::string& expectedState,
                                   std::string* code, std::wstring* terminalError) {
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
    bool callback = route == "/auth/callback";
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
// Anthropic code#state or waits for the OpenAI loopback callback. Runs its own message loop on
// the login thread; closed on cancel, submit, callback, or unload.
enum {
    kLoginInstructions = 100,
    kLoginCode,
    kLoginCopyLink,
    kLoginOpenLink,
};
constexpr UINT kLoginAccept = WM_APP + 1;  // FD_ACCEPT notification for the loopback listener.

struct LoginDialogState {
    // Inputs
    std::wstring url;
    SOCKET listener = INVALID_SOCKET;  // Loopback flow when set; otherwise a code is pasted.
    std::string expectedState;

    // Results
    std::wstring pastedCode;
    std::string callbackCode;
    std::wstring callbackError;
    bool ok = false;
    bool done = false;

    // Window
    HWND edit = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
};

static void LayoutLoginDialog(HWND hWnd, LoginDialogState& state, UINT dpi) {
    auto sc = [dpi](int value) { return ScaleForDpi(value, dpi); };
    constexpr UINT flags = SWP_NOZORDER | SWP_NOACTIVATE;
    SetWindowPos(GetDlgItem(hWnd, kLoginInstructions), nullptr, sc(12), sc(10), sc(424), sc(52), flags);
    SetWindowPos(GetDlgItem(hWnd, kLoginCopyLink), nullptr, sc(12), sc(68), sc(120), sc(30), flags);
    SetWindowPos(GetDlgItem(hWnd, kLoginOpenLink), nullptr, sc(138), sc(68), sc(120), sc(30), flags);
    if (state.edit) {
        SetWindowPos(state.edit, nullptr, sc(12), sc(110), sc(424), sc(24), flags);
        SetWindowPos(GetDlgItem(hWnd, IDOK), nullptr, sc(262), sc(146), sc(84), sc(30), flags);
    }
    SetWindowPos(GetDlgItem(hWnd, IDCANCEL), nullptr, sc(352), sc(state.edit ? 146 : 68),
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
                       (int)kLoginOpenLink, IDOK, IDCANCEL}) {
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
            create(0, L"BUTTON", L"Copy link", WS_TABSTOP | BS_PUSHBUTTON, kLoginCopyLink);
            create(0, L"BUTTON", L"Open in browser", WS_TABSTOP | BS_PUSHBUTTON, kLoginOpenLink);
            if (st->listener == INVALID_SOCKET) {
                st->edit = create(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL,
                                  kLoginCode);
                create(0, L"BUTTON", L"Sign in", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
            }
            create(0, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
            return 0;
        }
        case WM_COMMAND: {
            if (!st) break;
            switch (LOWORD(wParam)) {
                case kLoginCopyLink: {
                    bool copied = false;
                    if (OpenClipboard(hWnd)) {
                        size_t bytes = (st->url.size() + 1) * sizeof(wchar_t);
                        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                            if (void* data = GlobalLock(mem)) {
                                memcpy(data, st->url.c_str(), bytes);
                                GlobalUnlock(mem);
                                copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, mem);
                            }
                            if (!copied) GlobalFree(mem);
                        }
                        CloseClipboard();
                    }
                    SetWindowTextW(GetDlgItem(hWnd, kLoginCopyLink), copied ? L"Copied" : L"Copy failed");
                    return 0;
                }
                case kLoginOpenLink:
                    OpenUrl(st->url.c_str());
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
            if (AnswerLoopbackCallback(client, st->expectedState, &st->callbackCode,
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
            if (st) LayoutLoginDialog(hWnd, *st, HIWORD(wParam));
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hWnd);
            return 0;
        case WM_DESTROY:
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
    RECT windowRect{0, 0, ScaleForDpi(448, dpi), ScaleForDpi(st.edit ? 186 : 108, dpi)};
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
    ShowWindow(wnd, SW_SHOW);
    SetForegroundWindow(wnd);
    SetFocus(st.edit ? st.edit : GetDlgItem(wnd, kLoginCopyLink));

    MSG msg;
    while (!g_unloading) {
        int getMessageResult = GetMessageW(&msg, nullptr, 0, 0);
        if (getMessageResult <= 0) break;
        if (!IsDialogMessageW(wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (st.done) break;
    }
    g_loginWnd.store(nullptr);
    if (IsWindow(wnd)) DestroyWindow(wnd);
    if (st.font) DeleteObject(st.font);
    // Unregister so a later mod reload can't reuse a class pointing at this now-unloaded WndProc.
    UnregisterClassW(kClass, hInst);
    return st.ok;
}

static void DoAnthropicLogin(const LoginRequest& req) {
    std::string verifier = RandomBase64Url(32);
    std::string challenge = Sha256Base64Url(verifier);
    std::string state = RandomBase64Url(32);
    if (verifier.empty() || challenge.empty() || state.empty()) {
        Wh_Log(L"Sign-in: crypto init failed");
        return;
    }

    LoginDialogState dialog;
    dialog.url = BuildAnthropicAuthorizeUrl(challenge, state);
    std::wstring title = L"Sign in: " + req.label + L" (Anthropic)";
    std::wstring instructions =
        L"Open the sign-in link in a browser and approve access on claude.ai, then paste "
        L"the code shown on the page below.";
    if (!ShowLoginDialog(title, instructions, dialog) || g_unloading) return;

    const std::wstring& pasted = dialog.pastedCode;
    size_t b = pasted.find_first_not_of(L" \t\r\n");
    size_t e = pasted.find_last_not_of(L" \t\r\n");
    std::wstring code = b == std::wstring::npos ? L"" : pasted.substr(b, e - b + 1);
    size_t hashPos = code.find(L'#');  // pasted value is code#state.
    if (hashPos != std::wstring::npos) code = code.substr(0, hashPos);
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
        obj.SetNamedValue(L"redirect_uri", JsonValue::CreateStringValue(winrt::hstring(kAnthropicRedirect)));
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
    if (!StartLoopback(&listener, &port)) {
        Wh_Log(L"Sign-in [%s]: could not bind localhost:1455/1457 (Codex running?)",
               req.label.c_str());
        return;
    }
    g_loginSocket.store(listener);
    LoginDialogState dialog;
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
            (it->provider != L"anthropic" && it->provider != L"openai")) return;
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
