// Base64, SHA-256, JWT payload decoding, and DPAPI token encryption.

static std::string Base64Encode(const BYTE* data, size_t len) {
    if (!len) return {};
    DWORD outLen = 0;
    if (!CryptBinaryToStringA(data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              nullptr, &outLen) || !outLen) {
        return {};
    }
    std::string out(outLen, '\0');
    if (!CryptBinaryToStringA(data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                              out.data(), &outLen)) {
        return {};
    }
    out.resize(outLen);
    return out;
}

static std::vector<BYTE> Base64Decode(const std::string& s) {
    if (s.empty()) return {};
    DWORD binLen = 0;
    if (!CryptStringToBinaryA(s.data(), (DWORD)s.size(), CRYPT_STRING_BASE64, nullptr, &binLen,
                              nullptr, nullptr) || !binLen) {
        return {};
    }
    std::vector<BYTE> out(binLen);
    if (!CryptStringToBinaryA(s.data(), (DWORD)s.size(), CRYPT_STRING_BASE64, out.data(), &binLen,
                              nullptr, nullptr)) {
        return {};
    }
    out.resize(binLen);
    return out;
}

// RFC 7636 base64url (no padding). Used for PKCE and to decode JWT segments.
static std::string ToBase64Url(const BYTE* data, size_t len) {
    std::string b64 = Base64Encode(data, len);
    std::string out;
    out.reserve(b64.size());
    for (char c : b64) {
        if (c == '+') out += '-';
        else if (c == '/') out += '_';
        else if (c == '=') continue;
        else out += c;
    }
    return out;
}

static std::vector<BYTE> FromBase64Url(std::string s) {
    for (char& c : s) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    s.append((4 - s.size() % 4) % 4, '=');
    return Base64Decode(s);
}

static std::string RandomBase64Url(size_t bytes) {
    std::vector<BYTE> buf(bytes);
    if (BCryptGenRandom(nullptr, buf.data(), (ULONG)buf.size(),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        return {};
    }
    return ToBase64Url(buf.data(), buf.size());
}

// PKCE code challenge: base64url(SHA-256(verifier)).
static std::string Sha256Base64Url(const std::string& input) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    std::string out;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<BYTE, 32> digest{};
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        if (BCryptHashData(hash, (PUCHAR)input.data(), (ULONG)input.size(), 0) == 0 &&
            BCryptFinishHash(hash, digest.data(), (ULONG)digest.size(), 0) == 0) {
            out = ToBase64Url(digest.data(), digest.size());
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

// Decodes a JWT's payload segment into a JSON object (claims). Empty on any failure.
static JsonObject ParseJwtPayload(const std::wstring& jwt) {
    std::string narrow = WideToUtf8(jwt);
    size_t first = narrow.find('.');
    if (first == std::string::npos) return nullptr;
    size_t second = narrow.find('.', first + 1);
    std::string payload = narrow.substr(first + 1,
        second == std::string::npos ? std::string::npos : second - first - 1);
    std::vector<BYTE> bytes = FromBase64Url(payload);
    if (bytes.empty()) return nullptr;
    try {
        return JsonObject::Parse(Utf8ToWide(std::string((char*)bytes.data(), bytes.size())));
    } catch (...) {
        return nullptr;
    }
}

// DPAPI (current user) so stored tokens are not plaintext at rest. Returns base64 of the
// protected blob, or empty on failure.
static std::string DpapiProtect(const std::string& plain) {
    DATA_BLOB in{(DWORD)plain.size(), (BYTE*)plain.data()};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"taskbar-ai-quota", nullptr, nullptr, nullptr, 0, &out)) {
        return {};
    }
    std::string b64 = Base64Encode(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return b64;
}

static std::string DpapiUnprotect(const std::string& b64) {
    std::vector<BYTE> blob = Base64Decode(b64);
    if (blob.empty()) return {};
    DATA_BLOB in{(DWORD)blob.size(), blob.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) {
        return {};
    }
    std::string plain((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}
