// Numbers in a regional format (I18nLocale::Numbers): Windows' strings read into one, and numbers
// written in it. Pure. Digits stay ASCII and the minus sign '-'; percent and currency signs,
// currency codes and K/M/G/T suffixes are the callers' and the same in every format. Text that is
// read back (CSV, logs, storage, protocol) never comes from here.

// API entry points carry [[maybe_unused]]: a build or test target may use only some of them.

// A format from Windows' LOCALE_SDECIMAL, LOCALE_STHOUSAND and LOCALE_SGROUPING strings. Grouping
// lists group sizes from the decimal separator leftwards, a last 0 repeating the one before:
// "3;0" (en-US: 1,234,567), "3;2;0" (hi-IN: 12,34,567), "3" (1234,567), "0" (none). A part that
// is empty, longer than Windows allows or malformed keeps en-US's.
[[maybe_unused]] static I18nNumberFormat I18nNumberFormatFrom(std::wstring_view decimal, std::wstring_view group,
                                                              std::wstring_view grouping) {
    I18nNumberFormat format;
    auto copy = [](std::wstring_view text, std::array<wchar_t, 4>& to) {
        if (text.empty() || text.size() >= to.size()) return;
        to = {};
        std::copy(text.begin(), text.end(), to.begin());
    };
    copy(decimal, format.decimal);
    copy(group, format.group);
    // Single digits between semicolons; a 0 only alone or last after another size.
    std::array<uint8_t, 8> sizes{};
    size_t count = 0;
    bool valid = !grouping.empty();
    for (size_t at = 0; valid && at <= grouping.size();) {
        size_t end = std::min(grouping.find(L';', at), grouping.size());
        std::wstring_view entry = grouping.substr(at, end - at);
        valid = entry.size() == 1 && entry[0] >= L'0' && entry[0] <= L'9' && count < sizes.size();
        if (valid) sizes[count++] = uint8_t(entry[0] - L'0');
        at = end + 1;
    }
    bool repeat = valid && count > 1 && sizes[count - 1] == 0;
    if (repeat) count--;
    if (valid && count == 1 && sizes[0] == 0) {
        count = 0;
        repeat = false;
    }
    if (!valid || std::find(sizes.begin(), sizes.begin() + count, uint8_t(0)) != sizes.begin() + count) {
        return format;
    }
    format.sizes = sizes;
    format.count = uint8_t(count);
    format.repeat = repeat;
    return format;
}

// A number as to_chars (and printf in the C locale) writes it, an optional '-', digits, then
// optionally '.' and digits, in the locale's format: its decimal separator, and its digit groups
// when `grouped`.
[[maybe_unused]] static std::wstring I18nDigits(const I18nLocale& locale, std::string_view ascii, bool grouped) {
    const I18nNumberFormat& format = locale.Numbers();
    size_t point = std::min(ascii.find('.'), ascii.size()), first = ascii.starts_with('-') ? 1 : 0;
    // Whether a group ends `digits` places left of the decimal separator.
    auto boundary = [&](size_t digits) {
        size_t at = 0;
        for (size_t i = 0; i < format.count; i++) {
            at += format.sizes[i];
            if (at >= digits) return at == digits;
        }
        size_t last = format.count ? format.sizes[format.count - 1] : 0;
        return format.repeat && last && (digits - at) % last == 0;
    };
    std::wstring text;
    for (size_t i = 0; i < ascii.size(); i++) {
        if (i == point) {
            text += format.Decimal();
            continue;
        }
        if (grouped && i > first && i < point && boundary(point - i)) text += format.Group();
        text += wchar_t(ascii[i]);
    }
    return text;
}

// `value` rounded to `decimals` places, its digits never grouped: 1234.50, de-DE 1234,50.
[[maybe_unused]] static std::wstring I18nFixed(const I18nLocale& locale, double value, int decimals) {
    char text[400];
    auto [end, error] = std::to_chars(text, std::end(text), value, std::chars_format::fixed, decimals);
    return error == std::errc{} ? I18nDigits(locale, {text, end}, false) : L"?";
}

// As I18nFixed, without trailing decimal zeros, nor the decimal separator they leave: 12.5, 166.
[[maybe_unused]] static std::wstring I18nTrimmed(const I18nLocale& locale, double value, int decimals) {
    char text[400];
    auto [end, error] = std::to_chars(text, std::end(text), value, std::chars_format::fixed, decimals);
    if (error != std::errc{}) return L"?";
    std::string_view printed(text, end);
    if (printed.find('.') != std::string_view::npos) {
        printed.remove_suffix(printed.size() - 1 - printed.find_last_not_of('0'));
        if (printed.ends_with('.')) printed.remove_suffix(1);
    }
    return I18nDigits(locale, printed, false);
}

// A whole number in the format's digit groups: 1,234,567, de-DE 1.234.567, hi-IN 12,34,567.
template <class T>
    requires std::is_integral_v<T>
static std::wstring I18nInteger(const I18nLocale& locale, T value) {
    char text[24];
    return I18nDigits(locale, {text, std::to_chars(text, std::end(text), value).ptr}, true);
}
