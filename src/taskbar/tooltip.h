// Quota colors and the per-account tooltip.

static winrt::Windows::UI::Color UsageColor(double pct, bool stale, int yellowThreshold,
                                            int orangeThreshold, int redThreshold,
                                            bool colorblindMode) {
    if (stale || pct < 0) return {255, 0x9E, 0x9E, 0x9E};

    if (colorblindMode) {
        if (pct >= redThreshold) return {255, 0xD5, 0x5E, 0x00};
        if (pct >= orangeThreshold) return {255, 0xE6, 0x9F, 0x00};
        if (pct >= yellowThreshold) return {255, 0x56, 0xB4, 0xE9};
        return {255, 0x00, 0x72, 0xB2};
    }

    if (pct >= redThreshold) return {255, 0xE5, 0x39, 0x35};
    if (pct >= orangeThreshold) return {255, 0xFB, 0x8C, 0x00};
    if (pct >= yellowThreshold) return {255, 0xFD, 0xD8, 0x35};
    return {255, 0x43, 0xA0, 0x47};
}

static void UpdateQuotaToolTip(ToolTip const& toolTip, std::wstring const& tip, bool hasError) {
    constexpr double maxWidth = 460;
    auto muted = SolidColorBrush(winrt::Windows::UI::Color{255, 0xD6, 0xD6, 0xD6});
    auto quotaLabel = SolidColorBrush(winrt::Windows::UI::Color{255, 0x9A, 0xBE, 0xFF});
    auto infoLabel = SolidColorBrush(winrt::Windows::UI::Color{255, 0xC7, 0x9B, 0xFF});
    auto creditLabel = SolidColorBrush(winrt::Windows::UI::Color{255, 0xFF, 0xD7, 0x66});
    auto duration = SolidColorBrush(winrt::Windows::UI::Color{255, 0xB7, 0xE4, 0xA3});
    auto accent = SolidColorBrush(hasError ?
        winrt::Windows::UI::Color{255, 0xFF, 0xB4, 0xA9} :
        winrt::Windows::UI::Color{255, 0xC7, 0x9B, 0xFF});
    auto border = SolidColorBrush(hasError ?
        winrt::Windows::UI::Color{0xB8, 0xD1, 0x34, 0x38} :
        winrt::Windows::UI::Color{0x72, 0x8A, 0xD1, 0xFF});

    size_t firstBreak = tip.find(L'\n');
    std::wstring title = firstBreak == std::wstring::npos ? tip : tip.substr(0, firstBreak);
    std::wstring body = firstBreak == std::wstring::npos ? L"" : tip.substr(firstBreak + 1);

    TextBlock titleBlock;
    titleBlock.Text(winrt::hstring(title));
    titleBlock.FontSize(12.5);
    titleBlock.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    titleBlock.Foreground(accent);
    titleBlock.TextWrapping(TextWrapping::Wrap);
    titleBlock.MaxWidth(maxWidth);
    titleBlock.IsHitTestVisible(false);

    StackPanel content;
    content.Orientation(Orientation::Vertical);
    content.MaxWidth(maxWidth);
    content.IsHitTestVisible(false);
    content.Children().Append(titleBlock);

    if (!body.empty()) {
        auto appendRun = [](TextBlock const& textBlock, std::wstring const& text,
                            Brush const& brush, bool bold = false) {
            if (text.empty()) return;
            wuxd::Run run;
            run.Text(winrt::hstring(text));
            run.Foreground(brush);
            if (bold) run.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            textBlock.Inlines().Append(run);
        };

        bool firstLine = true;
        for (size_t pos = 0; pos <= body.size();) {
            size_t next = body.find(L'\n', pos);
            std::wstring line = next == std::wstring::npos ? body.substr(pos) : body.substr(pos, next - pos);

            TextBlock lineBlock;
            lineBlock.FontSize(12);
            lineBlock.LineHeight(16);
            lineBlock.TextWrapping(TextWrapping::Wrap);
            lineBlock.MaxWidth(maxWidth);
            lineBlock.Margin(firstLine ? Thickness{0, 4, 0, 0} : Thickness{0, 1, 0, 0});
            lineBlock.IsHitTestVisible(false);

            Brush labelBrush = muted;
            size_t labelEnd = std::wstring::npos;
            size_t highlightStart = std::wstring::npos;
            size_t highlightEnd = std::wstring::npos;
            bool labelBold = false;
            bool quotaLine = false;
            bool errorLine = false;
            if (line.rfind(L"error:", 0) == 0) {
                labelBrush = accent;
                labelEnd = 6;
                labelBold = true;
                errorLine = true;
            } else if (line.rfind(L"credits:", 0) == 0) {
                labelBrush = creditLabel;
                labelEnd = 8;
                highlightStart = line.find_first_not_of(L" ", labelEnd);
                if (highlightStart != std::wstring::npos) {
                    highlightEnd = highlightStart;
                    while (highlightEnd < line.size()) {
                        wchar_t ch = line[highlightEnd];
                        if ((ch < L'0' || ch > L'9') && ch != L'.') break;
                        highlightEnd++;
                    }
                }
                labelBold = true;
            } else if (line.rfind(L"extra usage:", 0) == 0) {
                labelBrush = creditLabel;
                labelEnd = 12;
                labelBold = true;
                quotaLine = true;
            } else if (line.rfind(L"updated:", 0) == 0) {
                labelBrush = infoLabel;
                labelEnd = 8;
                highlightStart = line.find(L"no data yet", labelEnd);
                if (highlightStart != std::wstring::npos) highlightEnd = highlightStart + 11;
                labelBold = true;
            } else if (line.rfind(L"rate limits:", 0) == 0) {
                labelBrush = muted;
                labelEnd = 12;
                labelBold = true;
            } else if (size_t colon = line.find(L':');
                       colon != std::wstring::npos && colon > 0 && colon + 1 < line.size() && line[colon + 1] == L' ') {
                labelBrush = quotaLabel;
                labelEnd = colon + 1;
                labelBold = true;
                quotaLine = true;
            }

            size_t textStart = 0;
            if (labelEnd != std::wstring::npos) {
                appendRun(lineBlock, line.substr(0, labelEnd), labelBrush, labelBold);
                textStart = labelEnd;
            }

            if (errorLine) {
                appendRun(lineBlock, line.substr(textStart), accent);
                content.Children().Append(lineBlock);
                firstLine = false;
                if (next == std::wstring::npos) break;
                pos = next + 1;
                continue;
            }

            size_t cursor = textStart;
            if (quotaLine) {
                while (cursor < line.size()) {
                    size_t percentEnd = line.find(L"%", cursor);
                    if (percentEnd == std::wstring::npos) break;
                    size_t percentStart = percentEnd;
                    while (percentStart > cursor) {
                        wchar_t ch = line[percentStart - 1];
                        if ((ch < L'0' || ch > L'9') && ch != L'.') break;
                        percentStart--;
                    }
                    if (percentStart == percentEnd) break;
                    appendRun(lineBlock, line.substr(cursor, percentStart - cursor), muted);
                    appendRun(lineBlock, line.substr(percentStart, percentEnd + 1 - percentStart), duration, true);
                    cursor = percentEnd + 1;
                }
            }

            size_t inPos = line.find(L"in ", cursor);
            if (inPos != std::wstring::npos) {
                size_t durationStart = inPos + 3;
                size_t durationEnd = line.find(L" (", durationStart);
                size_t dashEnd = line.find(L" - ", durationStart);
                if (dashEnd != std::wstring::npos && (durationEnd == std::wstring::npos || dashEnd < durationEnd)) {
                    durationEnd = dashEnd;
                }
                if (durationEnd == std::wstring::npos) durationEnd = line.size();

                appendRun(lineBlock, line.substr(cursor, durationStart - cursor), muted);
                appendRun(lineBlock, line.substr(durationStart, durationEnd - durationStart), duration, true);
                appendRun(lineBlock, line.substr(durationEnd), muted);
            } else if (highlightStart != std::wstring::npos && highlightStart < highlightEnd) {
                appendRun(lineBlock, line.substr(cursor, highlightStart - cursor), muted);
                appendRun(lineBlock, line.substr(highlightStart, highlightEnd - highlightStart), duration, true);
                appendRun(lineBlock, line.substr(highlightEnd), muted);
            } else {
                appendRun(lineBlock, line.substr(cursor), muted);
            }

            content.Children().Append(lineBlock);
            firstLine = false;
            if (next == std::wstring::npos) break;
            pos = next + 1;
        }
    }

    toolTip.BorderBrush(border);
    toolTip.Content(content);
}
