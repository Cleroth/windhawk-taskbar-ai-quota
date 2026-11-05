// Null-tolerant JSON accessors and error-body summaries.

static JsonObject GetObj(JsonObject const& o, PCWSTR name) {
    if (!o || !o.HasKey(name)) return nullptr;
    auto v = o.GetNamedValue(name);
    return v.ValueType() == JsonValueType::Object ? v.GetObject() : nullptr;
}

static double GetNum(JsonObject const& o, PCWSTR name, double def = -1) {
    if (!o || !o.HasKey(name)) return def;
    auto v = o.GetNamedValue(name);
    return v.ValueType() == JsonValueType::Number ? v.GetNumber() : def;
}

static std::wstring GetStr(JsonObject const& o, PCWSTR name) {
    if (!o || !o.HasKey(name)) return {};
    auto v = o.GetNamedValue(name);
    if (v.ValueType() != JsonValueType::String) return {};
    auto s = v.GetString();
    return std::wstring(s.c_str(), s.size());
}

static bool GetBool(JsonObject const& o, PCWSTR name) {
    if (!o || !o.HasKey(name)) return false;
    auto v = o.GetNamedValue(name);
    return v.ValueType() == JsonValueType::Boolean && v.GetBoolean();
}

// A response body's shape, for an error's detail.
static std::wstring DescribeJsonBody(const std::string& body) {
    if (body.empty()) return L"empty body";  // i18n-en

    try {
        auto root = JsonObject::Parse(Utf8ToWide(body));
        std::wstring keys;
        int count = 0;
        for (auto const& kv : root) {
            if (count == 8) {
                keys += L", ...";
                break;
            }
            if (!keys.empty()) keys += L", ";
            auto key = kv.Key();
            keys += std::wstring(key.c_str(), key.size());
            count++;
        }
        return keys.empty() ? L"JSON object with no keys" : L"keys: " + keys;  // i18n-en
    } catch (...) {
        return L"non-object or invalid JSON body";  // i18n-en
    }
}
