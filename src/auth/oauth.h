// OAuth client constants, authorize URLs, and token-endpoint exchange and refresh.

// Public OAuth clients used by the official CLIs and GitHub's copilot.vim; the mod runs the same
// flows so a sign-in here is independent of (and never touches) OpenCode/Claude Code/Codex or any
// other app's GitHub credentials.
static constexpr PCWSTR kAnthropicClientId = L"9d1c250a-e61b-44d9-88ed-5944d1962f5e";
static constexpr PCWSTR kAnthropicTokenHost = L"console.anthropic.com";
static constexpr PCWSTR kAnthropicTokenPath = L"/v1/oauth/token";
// The page that shows the code#state to paste; on this PC a localhost redirect takes its place.
static constexpr PCWSTR kAnthropicRedirect = L"https://console.anthropic.com/oauth/code/callback";
static constexpr PCWSTR kAnthropicScope = L"org:create_api_key user:profile user:inference";
static constexpr PCWSTR kOpenAiClientId = L"app_EMoamEEZ73f0CkXaXp7hrann";
static constexpr PCWSTR kOpenAiTokenHost = L"auth.openai.com";
static constexpr PCWSTR kOpenAiTokenPath = L"/oauth/token";
static constexpr PCWSTR kOpenAiScope =
    L"openid profile email offline_access api.connectors.read api.connectors.invoke";
// GitHub's device flow; sign-in and refresh share the token endpoint.
static constexpr PCWSTR kCopilotClientId = L"Iv1.b507a08c87ecfe98";
static constexpr PCWSTR kCopilotScope = L"read:user";
static constexpr PCWSTR kCopilotTokenHost = L"github.com";
static constexpr PCWSTR kCopilotTokenPath = L"/login/oauth/access_token";
static constexpr PCWSTR kCopilotVerificationUrl = L"https://github.com/login/device";
static constexpr PCWSTR kOAuthUserAgent = L"taskbar-ai-quota/0.1";

static std::string UrlEncode(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

static std::string UrlDecode(const std::string& s) {
    auto hex = [](char h) -> int {
        if (h >= '0' && h <= '9') return h - '0';
        if (h >= 'a' && h <= 'f') return h - 'a' + 10;
        if (h >= 'A' && h <= 'F') return h - 'A' + 10;
        return -1;
    };
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '%' && i + 2 < s.size()) {
            int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += (c == '+') ? ' ' : c;
    }
    return out;
}

static std::wstring BuildAnthropicAuthorizeUrl(const std::string& redirect, const std::string& challenge,
                                               const std::string& state) {
    std::string url = "https://claude.ai/oauth/authorize?code=true";
    url += "&client_id=" + UrlEncode(WideToUtf8(kAnthropicClientId));
    url += "&response_type=code";
    url += "&redirect_uri=" + UrlEncode(redirect);
    url += "&scope=" + UrlEncode(WideToUtf8(kAnthropicScope));
    url += "&code_challenge=" + challenge;
    url += "&code_challenge_method=S256";
    url += "&state=" + state;
    return Utf8ToWide(url);
}

static std::wstring BuildOpenAiAuthorizeUrl(int port, const std::string& challenge,
                                            const std::string& state) {
    std::string redirect = "http://localhost:" + std::to_string(port) + "/auth/callback";
    std::string url = "https://auth.openai.com/oauth/authorize?response_type=code";
    url += "&client_id=" + UrlEncode(WideToUtf8(kOpenAiClientId));
    url += "&redirect_uri=" + UrlEncode(redirect);
    url += "&scope=" + UrlEncode(WideToUtf8(kOpenAiScope));
    url += "&code_challenge=" + challenge;
    url += "&code_challenge_method=S256";
    url += "&id_token_add_organizations=true";
    url += "&codex_cli_simplified_flow=true";
    url += "&originator=codex_cli_rs";
    url += "&state=" + state;
    return Utf8ToWide(url);
}

enum class TokenEndpointResult {
    Success,
    Rejected,
    TransientFailure,
};

static std::wstring ParseOAuthError(const std::string& body) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        std::wstring desc = GetStr(root, L"error_description");
        if (!desc.empty()) return desc;
        return GetStr(root, L"error");
    } catch (...) {
        return {};
    }
}

// Fills *tok from an OAuth token response (access/refresh/id_token), extracting the OpenAI
// ChatGPT-Account-Id and access-token expiry from JWT claims when present. Used for both the
// authorization-code exchange and refresh.
static bool ParseTokenResponse(const std::string& body, bool anthropic, StoredToken* tok,
                               std::wstring* err) {
    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        std::wstring access = GetStr(root, L"access_token");
        std::wstring refresh = GetStr(root, L"refresh_token");
        if (access.empty()) {
            *err = L"no access token in response";
            return false;
        }
        tok->accessToken = access;
        if (!refresh.empty()) tok->refreshToken = refresh;

        tok->expiresMs = 0;
        double expiresIn = GetNum(root, L"expires_in", 0);
        if (expiresIn > 0) tok->expiresMs = NowUnixMs() + (ULONGLONG)(expiresIn * 1000);

        if (!anthropic) {
            std::wstring idToken = GetStr(root, L"id_token");
            if (auto claims = ParseJwtPayload(idToken.empty() ? access : idToken)) {
                if (auto authObj = GetObj(claims, L"https://api.openai.com/auth")) {
                    std::wstring acc = GetStr(authObj, L"chatgpt_account_id");
                    if (!acc.empty()) tok->accountId = acc;
                }
            }
            // OpenAI responses omit expires_in; derive expiry from the access-token JWT exp.
            if (tok->expiresMs == 0) {
                if (auto claims = ParseJwtPayload(access)) {
                    double exp = GetNum(claims, L"exp", 0);
                    if (exp > 0) tok->expiresMs = (ULONGLONG)(exp * 1000);
                }
            }
        }
        return true;
    } catch (...) {
        *err = L"invalid token response";
        return false;
    }
}

static TokenEndpointResult PostTokenEndpoint(bool anthropic, const std::wstring& contentType,
                                              const std::string& body, StoredToken* tok,
                                              std::wstring* err, int* retryAfterSec = nullptr) {
    if (retryAfterSec) *retryAfterSec = 0;
    std::wstring headers = L"Content-Type: " + contentType + L"\r\nAccept: application/json\r\n";
    PCWSTR host = anthropic ? kAnthropicTokenHost : kOpenAiTokenHost;
    PCWSTR path = anthropic ? kAnthropicTokenPath : kOpenAiTokenPath;
    HttpResult r = HttpRequest(L"POST", host, path, kOAuthUserAgent, headers, body);
    if (!r.ok) {
        *err = L"network error";
        return TokenEndpointResult::TransientFailure;
    }
    if (r.status < 200 || r.status >= 300) {
        if (retryAfterSec) *retryAfterSec = r.retryAfterSec;
        std::wstring detail = ParseOAuthError(r.body);
        *err = detail.empty() ? (L"HTTP " + std::to_wstring(r.status)) : detail;
        bool rejected = r.status >= 400 && r.status < 500 && r.status != 408 &&
                        r.status != 429;
        return rejected ? TokenEndpointResult::Rejected
                        : TokenEndpointResult::TransientFailure;
    }
    return ParseTokenResponse(r.body, anthropic, tok, err)
               ? TokenEndpointResult::Success
               : TokenEndpointResult::TransientFailure;
}

// grant_type=refresh_token. Anthropic sends JSON; OpenAI sends JSON too (its auth-code
// exchange is the form-encoded one). Rotated refresh tokens come back in the response.
static TokenEndpointResult RefreshToken(const std::wstring& provider, StoredToken* tok,
                                        std::wstring* err, int* retryAfterSec) {
    if (retryAfterSec) *retryAfterSec = 0;
    if (tok->refreshToken.empty()) {
        *err = L"no refresh token";
        return TokenEndpointResult::Rejected;
    }
    bool anthropic = provider == L"anthropic";
    std::string body;
    try {
        JsonObject obj;
        obj.SetNamedValue(L"grant_type", JsonValue::CreateStringValue(L"refresh_token"));
        obj.SetNamedValue(L"refresh_token",
                          JsonValue::CreateStringValue(winrt::hstring(tok->refreshToken)));
        obj.SetNamedValue(L"client_id", JsonValue::CreateStringValue(
                                            winrt::hstring(anthropic ? kAnthropicClientId : kOpenAiClientId)));
        body = WideToUtf8(std::wstring(obj.Stringify().c_str()));
    } catch (...) {
        *err = L"internal error";
        return TokenEndpointResult::TransientFailure;
    }
    return PostTokenEndpoint(anthropic, L"application/json", body, tok, err, retryAfterSec);
}
