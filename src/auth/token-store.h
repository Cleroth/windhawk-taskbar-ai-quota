// DPAPI-encrypted per-account token storage, auth epochs, and rename redirects.

// Per-account OAuth credentials, owned entirely by the mod (the CLI credential files are
// never touched). Persisted DPAPI-encrypted in Windhawk mod storage, keyed by identity hash.
struct StoredToken {
    std::wstring accessToken;
    std::wstring refreshToken;
    std::wstring accountId;  // OpenAI ChatGPT-Account-Id; empty for Anthropic.
    ULONGLONG expiresMs = 0;  // 0 = unknown; refresh is then driven reactively by 401s.
};

static std::mutex g_authMutex;
static std::mutex g_authEpochMutex;
static std::vector<std::pair<uint64_t, ULONGLONG>> g_authEpochs;

struct AuthRenameRedirect {
    uint64_t sourceIdentity;
    ULONGLONG sourceEpoch;
    uint64_t destinationIdentity;
    ULONGLONG destinationEpoch;
};

static std::vector<AuthRenameRedirect> g_authRenameRedirects;

enum class TokenSaveResult {
    Saved,
    Stale,
    Failed,
};

static ULONGLONG AuthEpochLocked(uint64_t idHash) {
    for (const auto& [hash, epoch] : g_authEpochs) {
        if (hash == idHash) return epoch;
    }
    return 0;
}

static ULONGLONG CurrentAuthEpoch(uint64_t idHash) {
    std::lock_guard<std::mutex> lk(g_authEpochMutex);
    return AuthEpochLocked(idHash);
}

static std::wstring TokenStorageKey(uint64_t idHash) {
    wchar_t buf[32];
    swprintf(buf, ARRAYSIZE(buf), L"auth_%016llx", (unsigned long long)idHash);
    return buf;
}

static bool LoadStoredToken(uint64_t idHash, StoredToken* out) {
    std::lock_guard<std::mutex> lk(g_authMutex);
    std::vector<wchar_t> buf(16384);
    Wh_GetStringValue(TokenStorageKey(idHash).c_str(), buf.data(), buf.size());
    std::wstring stored = buf.data();
    if (stored.empty()) return false;

    std::string plain = DpapiUnprotect(WideToUtf8(stored));
    if (plain.empty()) return false;
    try {
        auto root = JsonObject::Parse(Utf8ToWide(plain));
        out->accessToken = GetStr(root, L"access");
        out->refreshToken = GetStr(root, L"refresh");
        out->accountId = GetStr(root, L"accountId");
        out->expiresMs = (ULONGLONG)GetNum(root, L"expiresMs", 0);
        return !out->accessToken.empty() || !out->refreshToken.empty();
    } catch (...) {
        return false;
    }
}

static bool SaveStoredToken(uint64_t idHash, const StoredToken& t) {
    std::lock_guard<std::mutex> lk(g_authMutex);
    std::wstring json;
    try {
        JsonObject root;
        root.SetNamedValue(L"access", JsonValue::CreateStringValue(winrt::hstring(t.accessToken)));
        root.SetNamedValue(L"refresh", JsonValue::CreateStringValue(winrt::hstring(t.refreshToken)));
        root.SetNamedValue(L"accountId", JsonValue::CreateStringValue(winrt::hstring(t.accountId)));
        root.SetNamedValue(L"expiresMs", JsonValue::CreateNumberValue((double)t.expiresMs));
        json = root.Stringify().c_str();
    } catch (...) {
        return false;
    }
    std::string b64 = DpapiProtect(WideToUtf8(json));
    if (b64.empty()) return false;
    return Wh_SetStringValue(TokenStorageKey(idHash).c_str(), Utf8ToWide(b64).c_str());
}

static bool ClearStoredToken(uint64_t idHash) {
    std::lock_guard<std::mutex> lk(g_authMutex);
    std::wstring key = TokenStorageKey(idHash);
    Wh_DeleteValue(key.c_str());
    std::vector<wchar_t> stored(16384);
    Wh_GetStringValue(key.c_str(), stored.data(), stored.size());
    return !stored[0];
}

static TokenSaveResult SaveStoredTokenIfCurrent(uint64_t idHash, ULONGLONG authEpoch,
                                                const StoredToken& t,
                                                uint64_t* savedIdentity = nullptr) {
    if (savedIdentity) *savedIdentity = 0;
    std::lock_guard<std::mutex> lk(g_authEpochMutex);
    uint64_t destinationIdentity = idHash;
    ULONGLONG destinationEpoch = authEpoch;
    // Exact epochs let saves follow renames but stop at sign-out or any other invalidating bump.
    for (int hop = 0; AuthEpochLocked(destinationIdentity) != destinationEpoch; hop++) {
        if (hop >= 32) return TokenSaveResult::Stale;
        auto redirect = std::find_if(
            g_authRenameRedirects.begin(), g_authRenameRedirects.end(),
            [&](const AuthRenameRedirect& candidate) {
                return candidate.sourceIdentity == destinationIdentity &&
                       candidate.sourceEpoch == destinationEpoch;
            });
        if (redirect == g_authRenameRedirects.end()) return TokenSaveResult::Stale;
        destinationIdentity = redirect->destinationIdentity;
        destinationEpoch = redirect->destinationEpoch;
    }
    if (!SaveStoredToken(destinationIdentity, t)) return TokenSaveResult::Failed;
    if (savedIdentity) *savedIdentity = destinationIdentity;
    return TokenSaveResult::Saved;
}

static void BumpAuthEpochLocked(uint64_t idHash) {
    for (auto& [hash, epoch] : g_authEpochs) {
        if (hash == idHash) {
            epoch++;
            return;
        }
    }
    g_authEpochs.push_back({idHash, 1});
}

static bool ClearStoredTokenAndBumpAuthEpoch(uint64_t idHash) {
    std::lock_guard<std::mutex> lk(g_authEpochMutex);
    bool cleared = ClearStoredToken(idHash);
    BumpAuthEpochLocked(idHash);
    return cleared;
}

enum class TokenCopyResult {
    SourceMissing,
    Copied,
    DestinationOccupied,
    Failed,
};

static TokenCopyResult CopyStoredTokenForRename(uint64_t oldHash, uint64_t newHash) {
    std::lock_guard<std::mutex> authLock(g_authMutex);

    std::vector<wchar_t> oldValue(16384);
    std::vector<wchar_t> newValue(16384);
    Wh_GetStringValue(TokenStorageKey(oldHash).c_str(), oldValue.data(), oldValue.size());
    if (!oldValue[0]) return TokenCopyResult::SourceMissing;
    Wh_GetStringValue(TokenStorageKey(newHash).c_str(), newValue.data(), newValue.size());
    if (newValue[0]) return TokenCopyResult::DestinationOccupied;
    if (!Wh_SetStringValue(TokenStorageKey(newHash).c_str(), oldValue.data())) {
        return TokenCopyResult::Failed;
    }
    return TokenCopyResult::Copied;
}
