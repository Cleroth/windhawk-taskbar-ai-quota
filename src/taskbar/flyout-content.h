// Account details flyout card: builds the card, then applies account values to it in place.

enum class FlyoutFooterAction {
    History,
    Dashboard,
    Refresh,
    Settings,
};

// Display state shared with the taskbar bars for one account.
struct FlyoutDisplay {
    ULONGLONG now = 0;
    BarMode barMode = BarMode::Used;
    int yellowThreshold = 0;
    int orangeThreshold = 0;
    int redThreshold = 0;
    std::array<uint32_t, 4> usageColors{};  // MeterDisplay::usageColors
    bool showOpenAiExtraLimits = false;
    bool stale = false;
    bool warn = false;
    bool refreshing = false;
    bool visualTestMode = false;
    // What the provider's status page shows, and the taskbar mark's color for it (0: none).
    ProviderOutage outage;
    uint32_t outageColor = 0;
    I18nLocale locale;  // The grid's language
};

// The card's width, the widest its title grows before it's cut, and the widest a quota row's name
// grows before it wraps (DIP).
static constexpr double kFlyoutCardWidth = 420;
static constexpr double kFlyoutTitleMaxWidth = 170;
static constexpr double kFlyoutNameMaxWidth = 160;
// Quota rows' details cell margins (DIP).
static constexpr double kFlyoutDetailsLeft = 12;
static constexpr double kFlyoutDetailsRight = 16;
// Burn-up chart plot (DIP). Its fixed width lets updates compute geometry without a layout
// pass, and curves are reduced to one bin per column; 0% and 100% sit inside the vertical inset
// so their strokes stay in the plot.
static constexpr double kFlyoutChartWidth = kBurnUpColumns;
static constexpr double kFlyoutChartHeight = 60;
static constexpr double kFlyoutChartInset = 2;

// The reading each flyout row shows, by FlyoutRowIndex.
static std::array<const WindowUsage*, kFlyoutRowCount> FlyoutRowReadings(AccountData const& d) {
    return {&d.win5h, &d.winWeek, &d.fableWeek, &d.antigravityThirdParty5h,
            &d.antigravityThirdPartyWeek, &d.extraUsage};
}

// A projection's text when it reaches the limit before the reset, naming its rate's span, as the
// details flyout and threshold notifications show it; empty otherwise.
static std::wstring LimitProjectionText(const I18nLocale& locale, const std::optional<UsageProjection>& projected,
                                        const WindowUsage& reading) {
    if (!projected || !projected->limit) return {};
    ULONGLONG minutes = std::max<ULONGLONG>(1, (projected->toMs - projected->fromMs + 59999) / 60000);
    std::wstring duration = FormatShortDuration(locale, minutes * 60);
    SYSTEMTIME since;
    if (projected->sinceMs + kProjectionLookbackMs == projected->fromMs) {
        return I18nText(locale, TrCtx(L"usage-projection", L"At the last 24h's rate: limit in ~{duration}"),
                        {{L"duration", duration}});
    }
    if (projected->sinceMs != reading.resetUnixMs - reading.windowDurationMs &&
        UnixMsToLocalSystemTime(projected->sinceMs, &since)) {
        return I18nText(locale, TrCtx(L"usage-projection", L"At the rate since {time}: limit in ~{duration}"),
                        {{L"time", FormatLocalDayTime(since)}, {L"duration", duration}});
    }
    return I18nText(locale, TrCtx(L"usage-projection", L"At this window's average rate: limit in ~{duration}"),
                    {{L"duration", duration}});
}

// Fetch thread: the flyout's projection text for a fresh reading of a taskbar slot, from the
// account's published curve (the flyout row's) and its lookback, in the notification's language.
// Takes the settings and data locks; call holding neither.
static std::wstring NotificationProjectionText(const I18nLocale& locale, uint64_t historyId, int slot,
                                               const WindowUsage& reading) {
    static constexpr int kRows[kQuotaBarCount] = {kFlyoutFiveHourRow, kFlyoutWeeklyRow, kFlyoutFableRow,
                                                  kFlyoutExtraRow};
    std::vector<TileTracePoint> trace;
    if (std::optional<BurnUpCurve> curve = CopyBurnUpCurve(historyId, kRows[slot], reading)) {
        for (const BurnUpPoint& point : curve->points) trace.push_back({point.timeMs, point.used, point.connect});
    }
    return LimitProjectionText(
        locale, ProjectUsage(reading, trace, NowUnixMs(), slot == kExtraUsageBar ? 0 : kProjectionLookbackMs),
        reading);
}

// Builds the card for `provider` into state.flyout.popup, registering its button delegates there
// as they are created, in the grid's language. Values are filled by UpdateAccountFlyout.
static Border BuildFlyoutCard(QuotaUiInstance& state, std::wstring const& provider,
                              bool visualTestMode) {
    FlyoutPopup& popup = state.flyout.popup;
    FlyoutTheme const& theme = popup.theme;
    const I18nLocale& locale = state.buildLocale;
    FontFamily displayFont(L"Segoe UI Variable Display, Segoe UI");  // i18n-en
    FontFamily iconFont(L"Segoe Fluent Icons, Segoe MDL2 Assets");  // i18n-en
    // The presenter's font (ShowAccountFlyout), set on the text itself: UpdateAccountFlyout
    // measures the card before it is presented.
    FontFamily textFont(L"Segoe UI Variable Text, Segoe UI");  // i18n-en
    auto makeText = [&](double size, Brush const& brush) {
        TextBlock text;
        text.FontFamily(textFont);
        text.FontSize(size);
        text.Foreground(brush);
        text.TextWrapping(TextWrapping::Wrap);
        text.IsHitTestVisible(false);
        return text;
    };
    auto singleLine = [](TextBlock const& text) {
        text.TextWrapping(TextWrapping::NoWrap);
        text.TextTrimming(TextTrimming::CharacterEllipsis);
    };
    auto collapsed = [](FrameworkElement const& element) {
        element.Visibility(Visibility::Collapsed);
    };
    // Sections below the header draw a separator on their top edge.
    auto section = [&](UIElement const& child) {
        Border border;
        border.BorderBrush(theme.separator);
        border.BorderThickness({0, 1, 0, 0});
        border.Padding({16, 9, 16, 9});
        border.Child(child);
        return border;
    };
    auto addColumn = [](Grid const& grid, GridLength width) {
        ColumnDefinition column;
        column.Width(width);
        grid.ColumnDefinitions().Append(column);
        return column;
    };
    constexpr GridLength kAuto{0, GridUnitType::Auto};
    constexpr GridLength kStar{1, GridUnitType::Star};
    // Even-pace and projection strokes, shared by the charts and their legend. Dash lengths are
    // in stroke thicknesses.
    auto dashes = [](double dash, double gap) {
        DoubleCollection pattern;
        pattern.Append(dash);
        pattern.Append(gap);
        return pattern;
    };
    auto paceStroke = [&](wuxs::Line const& line) {
        line.Stroke(theme.chartGuide);
        line.StrokeThickness(1);
        line.StrokeDashArray(dashes(3, 3));
    };
    auto projectionStroke = [&](wuxs::Line const& line) {
        line.StrokeThickness(1.4);
        line.StrokeDashArray(dashes(1.5 / 1.4, 2.5 / 1.4));
        line.StrokeDashCap(PenLineCap::Round);
        line.StrokeStartLineCap(PenLineCap::Round);
        line.StrokeEndLineCap(PenLineCap::Round);
    };

    // Header, provider status, quota rows, chart legend, notes and footer, top to bottom. Under the
    // card's height limit (ShowAccountFlyout) only the quota rows shrink, and scroll.
    Grid content;
    auto appendSection = [&](FrameworkElement const& element, GridLength height) {
        RowDefinition definition;
        definition.Height(height);
        Grid::SetRow(element, (int)content.RowDefinitions().Size());
        content.RowDefinitions().Append(definition);
        content.Children().Append(element);
    };

    // Header: logo (in its brand colors when chosen, except in high contrast), account label,
    // provider, stale marker and plan chip; its tooltip holds them whole when they're cut
    // (UpdateAccountFlyout), so it takes the pointer.
    Grid header;
    header.Padding({16, 12, 16, 12});
    header.Background(FlyoutBrush(winrt::Windows::UI::Colors::Transparent()));
    for (int column = 0; column < 5; column++) addColumn(header, column == 2 ? kStar : kAuto);
    popup.header = header;
    try {
        LogoBrands brands;
        {
            std::lock_guard<std::mutex> lk(g_settingsMutex);
            brands = g_settings.logoBrands;
        }
        const LogoBrand* brand = theme.highContrast ? nullptr : ProviderLogoBrand(brands, provider);
        Viewbox logo;
        logo.Width(20);
        logo.Height(20);
        logo.Margin({0, 0, 10, 0});
        logo.VerticalAlignment(VerticalAlignment::Center);
        logo.IsHitTestVisible(false);
        logo.Child(LoadProviderLogo(provider, brand ? LogoBrandBrush(*brand) : theme.text));
        header.Children().Append(logo);
    } catch (...) {
        Wh_Log(L"Flyout logo failed to load");
    }

    popup.title = makeText(20, theme.text);
    popup.title.FontFamily(displayFont);
    popup.title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
    popup.title.LineHeight(24);
    popup.title.MaxWidth(kFlyoutTitleMaxWidth);
    popup.title.VerticalAlignment(VerticalAlignment::Center);
    singleLine(popup.title);
    Grid::SetColumn(popup.title, 1);
    header.Children().Append(popup.title);

    TextBlock providerName = makeText(12, theme.mutedText);
    providerName.Text(ProviderDisplayName(provider));
    providerName.Margin({10, 3, 0, 0});
    providerName.VerticalAlignment(VerticalAlignment::Center);
    singleLine(providerName);
    Grid::SetColumn(providerName, 2);
    header.Children().Append(providerName);

    TextBlock staleMarker = makeText(14, theme.warning);
    staleMarker.FontFamily(iconFont);
    staleMarker.Text(L"\uE7BA");
    staleMarker.Margin({8, 0, 0, 0});
    staleMarker.VerticalAlignment(VerticalAlignment::Center);
    collapsed(staleMarker);
    Grid::SetColumn(staleMarker, 3);
    header.Children().Append(staleMarker);
    popup.staleMarker = staleMarker;

    Border planChip;
    planChip.CornerRadius({10, 10, 10, 10});
    planChip.Padding({9, 2, 9, 2});
    planChip.Margin({10, 0, 0, 0});
    planChip.Background(theme.chip);
    planChip.BorderBrush(theme.chipBorder);
    planChip.BorderThickness({1, 1, 1, 1});
    planChip.VerticalAlignment(VerticalAlignment::Center);
    popup.plan = makeText(11.5, theme.secondaryText);
    singleLine(popup.plan);
    planChip.Child(popup.plan);
    collapsed(planChip);
    Grid::SetColumn(planChip, 4);
    header.Children().Append(planChip);
    popup.planChip = planChip;
    appendSection(header, kAuto);

    // Delegates check the popup they were created for.
    HWND hWnd = state.hWnd;
    uint64_t generation = popup.generation;
    Brush transparent = FlyoutBrush(winrt::Windows::UI::Colors::Transparent());

    // Provider status, while a watched component isn't operational: its glyph, then on one line the
    // component, its state, its start and the incident, trimmed at the end. A click closes the card
    // and opens the status page.
    if (const ProviderStatusSource* statusSource = FindProviderStatusSource(provider)) {
        Button line;
        if (theme.buttonStyle) line.Style(theme.buttonStyle);
        line.Background(transparent);
        line.HorizontalAlignment(HorizontalAlignment::Left);
        Grid row;
        row.Margin({5, 4, 6, 4});
        addColumn(row, kAuto);
        addColumn(row, kStar);
        popup.outageGlyph = makeText(14, theme.warning);
        popup.outageGlyph.FontFamily(iconFont);
        popup.outageGlyph.Text(kOutageGlyph);
        popup.outageGlyph.TextWrapping(TextWrapping::NoWrap);
        popup.outageGlyph.Margin({0, 1, 8, 0});
        row.Children().Append(popup.outageGlyph);
        popup.outageText = makeText(12, theme.text);
        popup.outageText.LineHeight(16);
        popup.outageText.VerticalAlignment(VerticalAlignment::Center);
        singleLine(popup.outageText);
        Grid::SetColumn(popup.outageText, 1);
        row.Children().Append(popup.outageText);
        line.Content(row);
        popup.outageButton = line;
        PCWSTR pageUrl = statusSource->pageUrl;
        popup.buttonHandlers.reserve(popup.buttonHandlers.size() + 1);
        auto token = line.Click([hWnd, generation, pageUrl](winrt::Windows::Foundation::IInspectable const&,
                                                            RoutedEventArgs const&) {
            if (g_unloading) return;
            try {
                auto* uiState = FindUiState(hWnd);
                if (!uiState || !uiState->flyout.popup.flyout || uiState->flyout.popup.generation != generation) {
                    return;
                }
                CloseAccountFlyout(*uiState);
                OpenUrl(pageUrl);
            } catch (...) {}
        });
        popup.buttonHandlers.push_back({line, token});
        Border outage = section(line);
        outage.Padding({10, 4, 10, 4});
        collapsed(outage);
        popup.outage = outage;
        appendSection(outage, kAuto);
    }

    // One row per quota: name, big number with over-pace arrow, then its visual and timings.
    // Rows stay in the tree and collapse when the quota is unavailable. All rows share one grid
    // so the number column fits its widest entry and the bars stay aligned. The extra quotas'
    // rows follow the main ones, without a chart.
    const std::array<std::array<winrt::Windows::Foundation::Point, 4>, 2> arrowStrokes = {{
        {{{1.5f, 12.f}, {6.f, 7.5f}, {9.f, 10.5f}, {14.5f, 5.f}}},
        {{{10.f, 5.f}, {14.5f, 5.f}, {14.5f, 9.5f}, {14.5f, 9.5f}}},
    }};
    Grid rowsGrid;
    popup.namesColumn = addColumn(rowsGrid, kAuto);
    popup.detailsColumn = addColumn(rowsGrid, kStar);
    ScrollViewer rowsScroller;
    rowsScroller.HorizontalScrollMode(ScrollMode::Disabled);
    rowsScroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    rowsScroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    rowsScroller.Content(rowsGrid);
    appendSection(rowsScroller, kStar);
    // The number and its unit ("45%", "used") in the language's order: its template's arguments'
    // order counts, the gap between them stays 4 DIPs.
    const std::wstring order = I18nText(locale, TrCtx(L"quota-unit-order", L"{number} {unit}"),
                                        {{L"number", L"\x1"}, {L"unit", L"\x2"}});
    const bool unitFirst = order.find(L'\x2') < order.find(L'\x1');
    auto addRow = [&](FlyoutQuotaRow& row, bool charted) {
        int gridRow = (int)rowsGrid.RowDefinitions().Size();
        RowDefinition rowDefinition;
        rowDefinition.Height({0, GridUnitType::Auto});
        rowsGrid.RowDefinitions().Append(rowDefinition);

        Border separator;
        separator.BorderBrush(theme.separator);
        separator.BorderThickness({0, 1, 0, 0});
        Grid::SetRow(separator, gridRow);
        Grid::SetColumnSpan(separator, 2);
        collapsed(separator);
        rowsGrid.Children().Append(separator);
        row.row = separator;

        StackPanel names;
        names.Margin({16, 9, 0, 9});
        Grid::SetRow(names, gridRow);
        collapsed(names);
        row.labels = names;
        row.name = makeText(13, theme.text);
        row.name.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        row.name.LineHeight(17);
        row.name.MaxWidth(kFlyoutNameMaxWidth);
        names.Children().Append(row.name);

        StackPanel value;
        value.Orientation(Orientation::Horizontal);
        value.Margin({0, 1, 0, 0});
        row.value = value;
        row.number = makeText(26, theme.text);
        row.number.FontFamily(displayFont);
        row.number.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        row.number.LineHeight(30);
        row.number.LineStackingStrategy(LineStackingStrategy::BlockLineHeight);
        row.number.CharacterSpacing(-19);
        row.number.TextWrapping(TextWrapping::NoWrap);

        Grid arrow;
        arrow.Width(16);
        arrow.Height(16);
        for (const auto& stroke : arrowStrokes) {
            wuxs::Polyline line;
            for (auto point : stroke) line.Points().Append(point);
            line.Stroke(theme.warning);
            line.StrokeThickness(1.5);
            line.StrokeStartLineCap(PenLineCap::Round);
            line.StrokeEndLineCap(PenLineCap::Round);
            line.StrokeLineJoin(PenLineJoin::Round);
            arrow.Children().Append(line);
        }
        Viewbox trend;
        trend.Width(14);
        trend.Height(14);
        trend.Margin({4, 0, 0, 5});
        trend.VerticalAlignment(VerticalAlignment::Bottom);
        trend.IsHitTestVisible(false);
        trend.Child(arrow);
        collapsed(trend);
        row.trend = trend;

        row.unit = makeText(12.5, theme.mutedText);
        row.unit.LineHeight(16);
        row.unit.LineStackingStrategy(LineStackingStrategy::BlockLineHeight);
        row.unit.TextWrapping(TextWrapping::NoWrap);
        row.unit.Margin(unitFirst ? Thickness{0, 0, 4, 3} : Thickness{4, 0, 0, 3});
        row.unit.VerticalAlignment(VerticalAlignment::Bottom);
        // The over-pace arrow follows the number.
        if (unitFirst) value.Children().Append(row.unit);
        value.Children().Append(row.number);
        value.Children().Append(trend);
        if (!unitFirst) value.Children().Append(row.unit);
        names.Children().Append(value);
        rowsGrid.Children().Append(names);

        StackPanel details;
        details.Margin({kFlyoutDetailsLeft, 11, kFlyoutDetailsRight, 9});
        details.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetRow(details, gridRow);
        Grid::SetColumn(details, 1);
        collapsed(details);
        row.details = details;

        // Bar: hatched track, usage fill and a pace tick overhanging the track by 2 DIP.
        row.visual = Grid{};
        row.visual.Height(20);
        Border track;
        track.Height(16);
        track.CornerRadius({5, 5, 5, 5});
        track.Background(theme.track);
        track.BorderBrush(theme.trackBorder);
        track.BorderThickness(theme.highContrast ? Thickness{1, 1, 1, 1} : Thickness{});
        track.VerticalAlignment(VerticalAlignment::Center);
        Grid fillLayer;
        row.fillColumn = addColumn(fillLayer, {0, GridUnitType::Star});
        row.emptyColumn = addColumn(fillLayer, {100, GridUnitType::Star});
        row.fill = Border{};
        row.fill.CornerRadius({5, 5, 5, 5});
        fillLayer.Children().Append(row.fill);
        track.Child(fillLayer);
        row.visual.Children().Append(track);

        Grid paceLayer;
        paceLayer.IsHitTestVisible(false);
        row.paceColumn = addColumn(paceLayer, {0, GridUnitType::Star});
        row.paceRestColumn = addColumn(paceLayer, {100, GridUnitType::Star});
        Border paceTick;
        paceTick.Width(4);
        paceTick.Height(20);
        paceTick.Margin({-2, 0, 0, 0});
        paceTick.HorizontalAlignment(HorizontalAlignment::Left);
        paceTick.CornerRadius({2, 2, 2, 2});
        paceTick.Background(theme.paceHalo);
        Border paceCore;
        paceCore.Width(2);
        paceCore.Height(18);
        paceCore.CornerRadius({1, 1, 1, 1});
        paceCore.Background(theme.paceTick);
        paceCore.HorizontalAlignment(HorizontalAlignment::Center);
        paceCore.VerticalAlignment(VerticalAlignment::Center);
        paceTick.Child(paceCore);
        Grid::SetColumn(paceTick, 1);
        collapsed(paceTick);
        paceLayer.Children().Append(paceTick);
        row.paceTick = paceTick;
        row.visual.Children().Append(paceLayer);
        details.Children().Append(row.visual);

        if (charted) {
            // Usage chart: card-tinted plot and the even-pace diagonal from the window start to
            // the reset (rising; UpdateAccountFlyout turns it in remaining mode), under the
            // recorded usage (area, segments, isolated readings), projection, now line and
            // current point, whose geometry UpdateAccountFlyout sets.
            Grid chart;
            chart.Width(kFlyoutChartWidth);
            chart.Height(kFlyoutChartHeight);
            chart.HorizontalAlignment(HorizontalAlignment::Left);
            chart.IsHitTestVisible(false);
            // The plot clips the drawing to its rounded corners and the canvas to its bounds,
            // so edge dots, the marker and fills never overhang. A high-contrast outline goes
            // on top so it doesn't inset the drawing.
            Border plot;
            plot.CornerRadius({4, 4, 4, 4});
            plot.Background(theme.chip);
            chart.Children().Append(plot);
            if (theme.highContrast) {
                Border outline;
                outline.CornerRadius({4, 4, 4, 4});
                outline.BorderBrush(theme.trackBorder);
                outline.BorderThickness({1, 1, 1, 1});
                chart.Children().Append(outline);
            }
            Canvas canvas;
            RectangleGeometry bounds;
            bounds.Rect({0, 0, (float)kFlyoutChartWidth, (float)kFlyoutChartHeight});
            canvas.Clip(bounds);
            wuxs::Line pace;
            pace.X1(0);
            pace.Y1(kFlyoutChartHeight - kFlyoutChartInset);
            pace.X2(kFlyoutChartWidth);
            pace.Y2(kFlyoutChartInset);
            paceStroke(pace);
            canvas.Children().Append(pace);
            row.chartPace = pace;
            row.chartArea = wuxs::Path{};
            canvas.Children().Append(row.chartArea);
            row.chartLines = wuxs::Path{};
            row.chartLines.StrokeThickness(1.6);
            row.chartLines.StrokeLineJoin(PenLineJoin::Round);
            row.chartLines.StrokeStartLineCap(PenLineCap::Round);
            row.chartLines.StrokeEndLineCap(PenLineCap::Round);
            canvas.Children().Append(row.chartLines);
            row.chartDots = wuxs::Path{};
            canvas.Children().Append(row.chartDots);
            row.chartProjection = wuxs::Line{};
            projectionStroke(row.chartProjection);
            collapsed(row.chartProjection);
            canvas.Children().Append(row.chartProjection);
            row.chartNow = wuxs::Line{};
            row.chartNow.Y2(kFlyoutChartHeight);
            row.chartNow.Stroke(theme.chartGuide);
            row.chartNow.StrokeThickness(1);
            canvas.Children().Append(row.chartNow);
            row.chartMarker = wuxs::Ellipse{};
            row.chartMarker.Width(6);
            row.chartMarker.Height(6);
            row.chartMarker.Stroke(theme.markerRing);
            row.chartMarker.StrokeThickness(1);
            canvas.Children().Append(row.chartMarker);
            plot.Child(canvas);
            collapsed(chart);
            details.Children().Append(chart);
            row.chart = chart;
        }

        row.amount = makeText(12, theme.secondaryText);
        row.amount.LineHeight(16);
        row.amount.Margin({0, 6, 0, 0});
        collapsed(row.amount);
        details.Children().Append(row.amount);
        row.reset = makeText(12, theme.mutedText);
        row.reset.LineHeight(16);
        row.reset.Margin({0, 6, 0, 0});
        details.Children().Append(row.reset);
        row.projection = makeText(12, theme.warning);
        row.projection.LineHeight(16);
        collapsed(row.projection);
        details.Children().Append(row.projection);
        rowsGrid.Children().Append(details);
    };
    for (FlyoutQuotaRow& row : popup.rows) addRow(row, true);
    if (provider == L"anthropic" || provider == L"openai" || provider == L"antigravity") {
        popup.extraRows.resize(kMaxExtraQuotas);
        for (FlyoutQuotaRow& row : popup.extraRows) addRow(row, false);
    }

    // Chart legend: line keys, then the data source, wrapping in the rest of the row.
    Grid legendGrid;
    addColumn(legendGrid, kAuto);
    addColumn(legendGrid, kStar);
    StackPanel keys;
    keys.Orientation(Orientation::Horizontal);
    wuxs::Line paceKey;
    paceStroke(paceKey);
    wuxs::Line projectionKey;
    projectionStroke(projectionKey);
    projectionKey.Stroke(theme.chartKey);
    for (auto [key, name] : {std::pair{paceKey, I18nText(locale, TrCtx(L"flyout-legend", L"even pace"))},
                             std::pair{projectionKey, I18nText(locale, TrCtx(L"flyout-legend", L"projection"))}}) {
        key.X1(1);
        key.X2(15);
        key.Y1(1);
        key.Y2(1);
        key.Width(16);
        key.Height(2);
        key.Margin({0, 0, 6, 0});
        key.VerticalAlignment(VerticalAlignment::Center);
        keys.Children().Append(key);
        TextBlock label = makeText(11, theme.mutedText);
        label.Text(name);
        label.TextWrapping(TextWrapping::NoWrap);
        label.Margin({0, 0, 14, 0});
        keys.Children().Append(label);
    }
    legendGrid.Children().Append(keys);
    TextBlock source = makeText(11, theme.mutedText);
    source.Text(I18nText(locale, TrCtx(L"flyout-legend", L"from local history")));
    source.TextAlignment(TextAlignment::Right);
    source.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(source, 1);
    legendGrid.Children().Append(source);
    Border legend = section(legendGrid);
    legend.Padding({16, 6, 16, 9});
    collapsed(legend);
    popup.legend = legend;
    appendSection(legend, kAuto);

    // Free-form lines: free resets, provider notes, errors and prompts.
    StackPanel notes;
    notes.Spacing(4);
    popup.freeResets = makeText(12, theme.secondaryText);
    popup.extraLines = makeText(12, theme.secondaryText);
    popup.error = makeText(12, theme.error);
    popup.status = makeText(12, theme.accent);
    for (TextBlock const& line : {popup.freeResets, popup.extraLines, popup.error, popup.status}) {
        line.LineHeight(16);
        collapsed(line);
        notes.Children().Append(line);
    }
    Border notesSection = section(notes);
    collapsed(notesSection);
    popup.notes = notesSection;
    appendSection(notesSection, kAuto);

    // Footer: update status, wrapping beside the actions (the footer grows from 40 DIPs to hold
    // it), and the actions.
    Grid footer;
    footer.MinHeight(40);
    footer.Padding({16, 0, 6, 0});
    footer.Background(theme.footer);
    footer.BorderBrush(theme.separator);
    footer.BorderThickness({0, 1, 0, 0});
    footer.CornerRadius({0, 0, 7, 7});
    addColumn(footer, kStar);
    addColumn(footer, kAuto);
    popup.updated = makeText(12, theme.mutedText);
    popup.updated.VerticalAlignment(VerticalAlignment::Center);
    popup.updated.Margin({0, 4, 0, 4});
    footer.Children().Append(popup.updated);

    StackPanel buttons;
    buttons.Orientation(Orientation::Horizontal);
    buttons.Spacing(2);
    buttons.VerticalAlignment(VerticalAlignment::Center);
    Grid::SetColumn(buttons, 1);
    uint64_t identity = popup.accountIdentity;
    uint64_t historyId = state.accountRefs[popup.accountIndex].historyId;
    auto addButton = [&](PCWSTR glyph, I18nKey key, FlyoutFooterAction action) {
        std::wstring name = I18nText(locale, key);
        Button button;
        if (theme.buttonStyle) button.Style(theme.buttonStyle);
        button.Width(32);
        button.Height(30);
        button.Padding({0, 0, 0, 0});
        button.Background(transparent);
        TextBlock icon = makeText(15, theme.text);
        icon.FontFamily(iconFont);
        icon.Text(glyph);
        icon.TextWrapping(TextWrapping::NoWrap);
        button.Content(icon);
        winrt::Windows::UI::Xaml::Automation::AutomationProperties::SetName(button, name);
        SetXamlToolTip(button, name, locale);
        popup.buttonHandlers.reserve(popup.buttonHandlers.size() + 1);
        auto token = button.Click(
            [hWnd, generation, identity, historyId, action](
                winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                if (g_unloading) return;
                try {
                    auto* uiState = FindUiState(hWnd);
                    if (!uiState || !uiState->flyout.popup.flyout ||
                        uiState->flyout.popup.generation != generation) {
                        return;
                    }
                    if (action == FlyoutFooterAction::Refresh) {
                        RefreshQuotaByIdentity(identity);
                        return;
                    }
                    // Actions that open a window or browser dismiss the flyout first.
                    CloseAccountFlyout(*uiState);
                    if (action == FlyoutFooterAction::History) OpenHistoryWindow(historyId);
                    else if (action == FlyoutFooterAction::Dashboard) OpenDashboardForIdentity(identity);
                    else OpenSettingsWindow();
                } catch (...) {}
            });
        popup.buttonHandlers.push_back({button, token});
        buttons.Children().Append(button);
    };
    addButton(L"\uE81C", TrCtx(L"taskbar-action", L"Usage history"), FlyoutFooterAction::History);
    if (!visualTestMode && provider != L"antigravity") {
        addButton(L"\uE8A7", TrCtx(L"taskbar-action", L"Open dashboard"), FlyoutFooterAction::Dashboard);
    }
    if (!visualTestMode) addButton(L"\uE72C", TrCtx(L"taskbar-action", L"Refresh"), FlyoutFooterAction::Refresh);
    addButton(L"\uE713", TrCtx(L"taskbar-action", L"Settings"), FlyoutFooterAction::Settings);
    footer.Children().Append(buttons);
    appendSection(footer, kAuto);

    // The flyout shows outside the tree: its card takes the language itself.
    Border card;
    SetXamlLanguage(card, locale);
    card.Width(kFlyoutCardWidth);
    card.CornerRadius({8, 8, 8, 8});
    card.BorderThickness({1, 1, 1, 1});
    card.BorderBrush(theme.cardBorder);
    card.Background(theme.card);
    if (!theme.highContrast) {
        card.RequestedTheme(theme.light ? ElementTheme::Light : ElementTheme::Dark);
    }
    // Composition materials draw on a layer under the content, inside the border's 7 DIP inner
    // corners, over the card's opaque fallback (SettleFlyoutMaterial).
    if (theme.material == Material::Mica || theme.material == Material::MicaAlt ||
        theme.material == Material::TintedWallpaper) {
        Border material;
        material.IsHitTestVisible(false);
        Grid surface;
        surface.Children().Append(material);
        surface.Children().Append(content);
        card.Child(surface);
        AttachFlyoutMaterial(state.flyout, material, 7.f);
    } else {
        card.Child(content);
    }
    return card;
}

// `traces` holds each row's recorded usage of its displayed reading (FlyoutRowReadings); rows
// without any show the bar.
static void UpdateAccountFlyout(
    QuotaUiInstance& state, AccountConfig const& account, AccountData const& d,
    FlyoutDisplay const& display,
    std::array<std::vector<TileTracePoint>, kFlyoutRowCount> const& traces) {
    FlyoutPopup& popup = state.flyout.popup;
    if (!popup.card) return;
    FlyoutTheme const& theme = popup.theme;
    auto show = [](FrameworkElement const& element, bool visible) {
        if (element) element.Visibility(visible ? Visibility::Visible : Visibility::Collapsed);
    };
    bool openAi = account.provider == L"openai";
    bool antigravity = account.provider == L"antigravity";
    bool copilot = account.provider == L"copilot";
    bool opencode = account.provider == L"opencode";
    const I18nLocale& locale = display.locale;
    wchar_t text[128];
    try {
        popup.title.Text(account.label);
        show(popup.staleMarker, display.warn);
        bool sparkPlan = d.plan.find(L"Spark") != std::wstring::npos ||  // i18n-en: the plan's name
                         d.plan.find(L"spark") != std::wstring::npos;
        std::wstring plan = openAi && sparkPlan && !display.showOpenAiExtraLimits ? L"" : d.plan;
        popup.plan.Text(plan);
        show(popup.planChip, !plan.empty());

        // "Claude Code and 1 more: partial outage · since 14:05 · Elevated errors", the incident last
        // so trimming takes only it; the whole line in the tooltip and to screen readers. The glyph in
        // the taskbar mark's color (high contrast: the theme's).
        const ProviderOutage& outage = display.outage;
        show(popup.outage, outage.level != OutageLevel::None);
        if (popup.outageText && popup.outageButton && outage.level != OutageLevel::None) {
            std::wstring level = I18nText(
                locale, outage.level == OutageLevel::Major    ? TrCtx(L"outage-level", L"major outage") :
                        outage.level == OutageLevel::Partial  ? TrCtx(L"outage-level", L"partial outage") :
                        outage.level == OutageLevel::Degraded ? TrCtx(L"outage-level", L"degraded performance") :
                                                                TrCtx(L"outage-level", L"under maintenance"));
            std::wstring line =
                outage.others > 0 ?
                    I18nText(locale,
                             TrCtx(L"outage-line", L"{component} and {count, plural, one{# more} other{# more}}: {level}"),
                             {{L"component", outage.component}, {L"count", outage.others}, {L"level", level}}) :
                    I18nText(locale, TrCtx(L"outage-line", L"{component}: {level}"),
                             {{L"component", outage.component}, {L"level", level}});
            SYSTEMTIME since;
            if (outage.sinceMs && UnixMsToLocalSystemTime(outage.sinceMs, &since)) {
                bool today = outage.sinceMs + 24ULL * 60 * 60 * 1000 > display.now;
                line = I18nText(locale, TrCtx(L"outage-line", L"{status} \u00B7 since {time}"),
                                {{L"status", line}, {L"time", today ? FormatLocalTime(since) : FormatLocalDayTime(since)}});
            }
            if (std::wstring incident = ProviderOutageIncident(locale, outage); !incident.empty()) {
                line = I18nText(locale, TrCtx(L"outage-line", L"{status} \u00B7 {incident}"),
                                {{L"status", line}, {L"incident", incident}});
            }
            if (std::wstring_view(popup.outageText.Text()) != line) {
                popup.outageText.Text(line);
                std::wstring tip = I18nText(locale, TrCtx(L"outage-action", L"{status}\nOpen the {provider} status page"),
                                            {{L"status", line}, {L"provider", ProviderDisplayName(account.provider)}});
                SetXamlToolTip(popup.outageButton, tip, locale);
                winrt::Windows::UI::Xaml::Automation::AutomationProperties::SetName(popup.outageButton, tip);
            }
            if (theme.highContrast || !display.outageColor) {
                popup.outageGlyph.Foreground(outage.level == OutageLevel::Major ? theme.error : theme.warning);
            } else {
                popup.outageGlyph.Foreground(FlyoutBrush(ArgbColor(display.outageColor)));
            }
        }

        // Antigravity pools are named by their reported window; the pools' and models' names stay
        // as written.
        auto poolName = [&](PCWSTR pool, ULONGLONG durationMs) {
            if (!durationMs) return I18nText(locale, TrCtx(L"quota-name", L"{name} current limit"), {{L"name", pool}});
            int hours = durationMs == 3ULL * 60 * 60 * 1000 ? 3 : durationMs == 60ULL * 60 * 1000 ? 1 : 5;
            return I18nText(locale, TrCtx(L"quota-name", L"{name} {hours}h"), {{L"name", pool}, {L"hours", hours}});
        };
        auto weekly = [&](PCWSTR name) {
            return I18nText(locale, TrCtx(L"quota-name", L"{name} weekly"), {{L"name", name}});
        };
        // OpenAI credits form a percentage only against the user's max and its selected bar;
        // otherwise the balance is shown as an amount.
        bool creditsBar = openAi && d.extraUsage.pct >= 0 && account.showBars[kExtraUsageBar];
        bool creditsAmount = openAi && !creditsBar &&
                             (d.creditsUnlimited || d.creditsBalance >= 0 || d.hasCredits);
        // GitHub Copilot's monthly allowance, in its unit, which also names its row: a percentage
        // of a finite allowance, else an unlimited pooled one without. Credits are fractional:
        // two decimals at most, trailing zeros trimmed, in the regional decimal separator.
        std::wstring copilotUnit = CopilotUnitName(locale, d.copilotUnit);
        auto copilotAmount = [&](double amount) { return I18nTrimmed(locale, amount, 2); };
        // Past a finite allowance, or beside a pooled one, the additional usage on a line of its own.
        bool overage = copilot && d.copilotOverage > 0;
        std::wstring overageAmount = overage ? copilotAmount(d.copilotOverage) : L"";
        struct RowSource {
            std::wstring name;
            bool available;
        };
        const std::array<RowSource, kFlyoutRowCount> sources = {{
            {antigravity ? poolName(L"Gemini", d.win5h.windowDurationMs) :  // i18n-en
                           I18nText(locale, TrCtx(L"quota-name", L"5-hour")),
             d.win5h.pct >= 0},
            {antigravity ? weekly(L"Gemini") : I18nText(locale, TrCtx(L"quota-name", L"Weekly")),  // i18n-en
             d.winWeek.pct >= 0},
            {weekly(antigravity ? L"Argon" : L"Fable"), d.fableWeek.pct >= 0},  // i18n-en
            {poolName(L"Claude/GPT", d.antigravityThirdParty5h.windowDurationMs),  // i18n-en
             antigravity && d.antigravityThirdParty5h.pct >= 0},
            {weekly(L"Claude/GPT"), antigravity && d.antigravityThirdPartyWeek.pct >= 0},  // i18n-en
            {I18nText(locale, openAi   ? TrCtx(L"quota-name", L"Credits") :
                              copilot  ? d.copilotUnit == CopilotUnit::PremiumRequests ?
                                             TrCtx(L"quota-name", L"Premium requests") :
                                         d.copilotUnit == CopilotUnit::ChatRequests ?
                                             TrCtx(L"quota-name", L"Chat requests") :
                                             TrCtx(L"quota-name", L"AI credits") :
                              opencode ? TrCtx(L"quota-name", L"Monthly") :
                                         TrCtx(L"quota-name", L"Extra usage")),
             openAi ? creditsBar || creditsAmount : d.extraUsage.pct >= 0 || (copilot && d.copilotUnlimited)},
        }};
        const auto readings = FlyoutRowReadings(d);
        // The extra quotas follow: OpenAI's additional limits only with their Display setting.
        const bool extrasShown = !openAi || display.showOpenAiExtraLimits;

        int visibleRows = 0;
        bool anyChart = false;
        constexpr size_t kMainRows = kFlyoutRowCount;
        for (size_t i = 0; i < kMainRows + popup.extraRows.size(); i++) {
            // The row's FlyoutRowIndex; -1 for an extra quota's row, which has no chart, projection
            // or amount.
            int r = i < kMainRows ? (int)i : -1;
            size_t extra = i - kMainRows;
            FlyoutQuotaRow& row = r >= 0 ? popup.rows[r] : popup.extraRows[extra];
            bool available = r >= 0 ? sources[r].available :
                                      extrasShown && extra < d.extraQuotas.size();
            if (!row.row) continue;
            for (auto const& part : {row.row, row.labels, row.details}) show(part, available);
            if (!available) continue;
            visibleRows++;
            const WindowUsage& wu = r >= 0 ? *readings[r] : d.extraQuotas[extra].usage;
            row.name.Text(r >= 0 ? sources[r].name : ExtraQuotaName(locale, d.extraQuotas[extra]));

            if (r == kFlyoutExtraRow && creditsAmount) {
                bool balance = !d.creditsUnlimited && d.creditsBalance >= 0;
                row.number.Text(d.creditsUnlimited ? I18nText(locale, TrCtx(L"quota-value", L"Unlimited")) :
                                balance            ? I18nFixed(locale, d.creditsBalance, 0) :
                                                     I18nText(locale, TrCtx(L"quota-value", L"Available")));
                row.unit.Text(balance ? I18nText(locale, TrCtx(L"quota-unit", L"left")) : L"");
                row.unit.Foreground(theme.mutedText);
                show(row.trend, false);
                show(row.visual, false);
                show(row.chart, false);
                show(row.amount, false);
                show(row.reset, false);
                show(row.projection, false);
                continue;
            }
            if (r == kFlyoutExtraRow && copilot) {
                // The number turns to the warning color only while the pool is used up.
                row.number.Foreground(d.copilotExhausted ? theme.warning : theme.text);
            }
            if (r == kFlyoutExtraRow && copilot && d.copilotUnlimited) {
                // No percentage: the pool's credits used when reported, else that it's unlimited,
                // or used up.
                bool used = !d.copilotExhausted && d.extraUsedAmount >= 0;
                row.number.Text(d.copilotExhausted ? I18nText(locale, TrCtx(L"quota-value", L"Used up")) :
                                used               ? copilotAmount(d.extraUsedAmount) :
                                                     I18nText(locale, TrCtx(L"quota-value", L"Unlimited")));
                row.unit.Text(used ? I18nText(locale, TrCtx(L"quota-unit", L"used")) : L"");
                row.unit.Foreground(theme.mutedText);
                show(row.trend, false);
                show(row.visual, false);
                show(row.chart, false);
                show(row.projection, false);
                row.amount.Text(overage ? I18nText(locale,
                                                   TrCtx(L"quota-amount", L"Pooled {unit}, no per-user limit\n"
                                                                          L"Additional usage: {overage} {unit}"),
                                                   {{L"unit", copilotUnit}, {L"overage", overageAmount}}) :
                                          I18nText(locale, TrCtx(L"quota-amount", L"Pooled {unit}, no per-user limit"),
                                                   {{L"unit", copilotUnit}}));
                show(row.amount, true);
                std::wstring reset = wu.resetUnixMs ? I18nText(locale, TrCtx(L"quota-reset", L"Resets {when}"),
                                                               {{L"when", FormatReset(locale, wu.resetUnixMs)}}) :
                                                      L"";
                row.reset.Text(reset);
                show(row.reset, !reset.empty());
                continue;
            }

            bool remainingMode = display.barMode == BarMode::Remaining;
            double shownPct = remainingMode ? std::clamp(100.0 - wu.pct, 0.0, 100.0) : wu.pct;
            swprintf(text, ARRAYSIZE(text), L"%.0f%%", shownPct);
            row.number.Text(text);
            row.unit.Text(I18nText(locale, remainingMode ? TrCtx(L"quota-unit", L"left") : TrCtx(L"quota-unit", L"used")));
            // An OpenCode Go window at its limit: the number in the warning color, and a note.
            int slot = r == kFlyoutFiveHourRow ? kFiveHourBar : r == kFlyoutWeeklyRow ? kWeeklyBar :
                       r == kFlyoutExtraRow ? kExtraUsageBar : -1;
            bool limited = opencode && slot >= 0 && d.opencodeLimited[slot];
            if (opencode) row.number.Foreground(limited ? theme.warning : theme.text);

            // The bar follows the bar mode like the taskbar; its color tracks consumption.
            double fillPct = std::clamp(shownPct, 0.0, 100.0);
            row.fillColumn.Width({fillPct, GridUnitType::Star});
            row.emptyColumn.Width({100.0 - fillPct, GridUnitType::Star});
            row.fill.MinWidth(fillPct > 0 ? 4.0 : 0.0);
            uint32_t colorValue = UsageColor(wu.pct, display.stale, display.yellowThreshold,
                                             display.orangeThreshold, display.redThreshold,
                                             display.usageColors);
            auto color = ArgbColor(colorValue);
            if (colorValue != row.fillColor) {
                row.fill.Background(FlyoutFillBrush(color));
                row.fillColor = colorValue;
            }

            bool paced = !display.stale && wu.windowDurationMs > 0 &&
                         wu.resetUnixMs > wu.windowDurationMs &&
                         display.now >= wu.resetUnixMs - wu.windowDurationMs;
            double elapsed = paced ?
                std::clamp(1.0 - ((double)wu.resetUnixMs - (double)display.now) /
                                     wu.windowDurationMs, 0.0, 1.0) : 0.0;
            show(row.paceTick, paced);
            if (paced) {
                double pacePct = (remainingMode ? 1.0 - elapsed : elapsed) * 100.0;
                row.paceColumn.Width({pacePct, GridUnitType::Star});
                row.paceRestColumn.Width({100.0 - pacePct, GridUnitType::Star});
            }

            // Over pace: more used than even use would have by now (fixed windows start at 0%).
            bool overPace = paced && wu.pct > 0 && elapsed >= 0.05 &&
                            display.now < wu.resetUnixMs && wu.pct > elapsed * 100.0;
            show(row.trend, overPace);
            row.unit.Foreground(overPace ? theme.warning : theme.mutedText);

            // The usage graph tiles' projection; the text, naming the rate's span, when it
            // reaches the limit before the reset. None once the limit is reached.
            static const std::vector<TileTracePoint> kNoTrace;
            const std::vector<TileTracePoint>& trace = r >= 0 ? traces[r] : kNoTrace;
            std::optional<UsageProjection> projected;
            if (r >= 0 && !display.stale && !limited) {
                projected = ProjectUsage(wu, trace, display.now,
                                         r == kFlyoutExtraRow ? 0 : kProjectionLookbackMs);
            }
            std::wstring projection = LimitProjectionText(locale, projected, wu);
            row.projection.Text(projection);
            show(row.projection, !projection.empty());

            // Usage chart in place of the bar: used %, or % left in remaining mode, over the
            // displayed reading's window, with the projection. Shapes are regenerated only when
            // the drawn geometry changes.
            bool charted = row.chart && !trace.empty() && wu.windowDurationMs > 0 &&
                           wu.resetUnixMs > wu.windowDurationMs;
            show(row.visual, !charted);
            show(row.chart, charted);
            if (charted) {
                anyChart = true;
                using winrt::Windows::Foundation::Point;
                double start = (double)(wu.resetUnixMs - wu.windowDurationMs);
                auto snap = [](double dip) { return (float)(std::round(dip * 4) / 4); };
                auto xAt = [&](ULONGLONG timeMs) {
                    return snap(std::clamp(((double)timeMs - start) / wu.windowDurationMs,
                                           0.0, 1.0) * kFlyoutChartWidth);
                };
                auto yAt = [&](double used) {
                    double shown = std::clamp(remainingMode ? 100.0 - used : used, 0.0, 100.0);
                    return snap(kFlyoutChartHeight - kFlyoutChartInset -
                                (kFlyoutChartHeight - 2 * kFlyoutChartInset) * shown / 100.0);
                };
                FlyoutChartShape shape;
                for (size_t begin = 0, end; begin < trace.size(); begin = end) {
                    std::vector<Point> run{{xAt(trace[begin].timeMs), yAt(trace[begin].used)}};
                    for (end = begin + 1; end < trace.size() && trace[end].connect; end++) {
                        run.push_back({xAt(trace[end].timeMs), yAt(trace[end].used)});
                    }
                    if (run.size() == 1) shape.dots.push_back(run.front());
                    else shape.lines.push_back(std::move(run));
                }
                // A rolled-back clock can put now before the newest reading.
                ULONGLONG now = std::max(display.now, trace.back().timeMs);
                shape.now = {xAt(now), yAt(wu.pct)};
                if (projected) shape.projectionEnd = {xAt(projected->toMs), yAt(projected->toUsed)};
                shape.color = colorValue;
                shape.remaining = remainingMode;

                FlyoutChartShape& drawn = row.chartShape;
                if (shape.remaining != drawn.remaining) {
                    row.chartPace.Y1(yAt(0));
                    row.chartPace.Y2(yAt(100));
                }
                if (shape.lines != drawn.lines || shape.dots != drawn.dots) {
                    // Each run is a stroked figure over a filled one closed to the plot bottom.
                    PathGeometry area;
                    PathGeometry lines;
                    for (auto const& run : shape.lines) {
                        PolyLineSegment outline;
                        for (Point point : run) outline.Points().Append(point);
                        outline.Points().Append({run.back().X, (float)kFlyoutChartHeight});
                        PathFigure under;
                        under.StartPoint({run.front().X, (float)kFlyoutChartHeight});
                        under.IsClosed(true);
                        under.Segments().Append(outline);
                        area.Figures().Append(under);

                        PolyLineSegment rest;
                        for (size_t p = 1; p < run.size(); p++) rest.Points().Append(run[p]);
                        PathFigure line;
                        line.StartPoint(run.front());
                        line.IsFilled(false);
                        line.Segments().Append(rest);
                        lines.Figures().Append(line);
                    }
                    GeometryGroup dots;
                    dots.FillRule(FillRule::Nonzero);
                    for (Point point : shape.dots) {
                        EllipseGeometry dot;
                        dot.Center(point);
                        dot.RadiusX(2);
                        dot.RadiusY(2);
                        dots.Children().Append(dot);
                    }
                    row.chartArea.Data(area);
                    row.chartLines.Data(lines);
                    row.chartDots.Data(dots);
                }
                if (shape.now != drawn.now || shape.projectionEnd != drawn.projectionEnd) {
                    row.chartNow.X1(shape.now.X);
                    row.chartNow.X2(shape.now.X);
                    Canvas::SetLeft(row.chartMarker, shape.now.X - 3.0);
                    Canvas::SetTop(row.chartMarker, shape.now.Y - 3.0);
                    bool projectionShown = shape.projectionEnd.X >= 0;
                    show(row.chartProjection, projectionShown);
                    if (projectionShown) {
                        row.chartProjection.X1(shape.now.X);
                        row.chartProjection.Y1(shape.now.Y);
                        row.chartProjection.X2(shape.projectionEnd.X);
                        row.chartProjection.Y2(shape.projectionEnd.Y);
                    }
                }
                if (shape.color != drawn.color) {
                    Brush usage = FlyoutShapeBrush(color);
                    row.chartArea.Fill(FlyoutShapeBrush({0x47, color.R, color.G, color.B}));
                    row.chartLines.Stroke(usage);
                    row.chartDots.Fill(usage);
                    row.chartProjection.Stroke(usage);
                    row.chartMarker.Fill(usage);
                }
                drawn = std::move(shape);
            }

            std::wstring amount;
            if (limited) {
                amount = I18nText(locale, TrCtx(L"quota-amount", L"Limit reached"));
            } else if (r == kFlyoutExtraRow && copilot) {
                // Past the allowance the bar stays full; the additional usage gets its own line.
                amount = I18nText(locale,
                                  overage ? TrCtx(L"quota-amount", L"{used} of {limit} {unit} used\n"
                                                                   L"Additional usage: {overage} {unit}") :
                                            TrCtx(L"quota-amount", L"{used} of {limit} {unit} used"),
                                  {{L"used", copilotAmount(d.extraUsedAmount)},
                                   {L"limit", copilotAmount(d.extraLimitAmount)},
                                   {L"unit", copilotUnit},
                                   {L"overage", overageAmount}});
            } else if (r == kFlyoutExtraRow && d.extraLimitAmount >= 0) {
                // Credits state the balance so the text reads the same in both bar modes. Amounts are
                // whole credits, or US dollars to the cent in the regional decimal separator, in
                // every language; never grouped.
                if (openAi) {
                    amount = I18nText(locale, TrCtx(L"quota-amount", L"{left} left of {limit} credits"),
                                      {{L"left", I18nFixed(locale, d.extraLimitAmount - d.extraUsedAmount, 0)},
                                       {L"limit", I18nFixed(locale, d.extraLimitAmount, 0)}});
                } else {
                    amount = I18nText(locale, TrCtx(L"quota-amount", L"{used} of {limit} spent"),
                                      {{L"used", L"$" + I18nFixed(locale, d.extraUsedAmount, 2)},
                                       {L"limit", L"$" + I18nFixed(locale, d.extraLimitAmount, 2)}});
                }
            }
            row.amount.Text(amount);
            show(row.amount, !amount.empty());

            std::wstring reset = wu.notYetStarted ? I18nText(locale, TrCtx(L"quota-reset", L"Not yet started")) :
                                 wu.resetUnixMs   ? I18nText(locale, TrCtx(L"quota-reset", L"Resets {when}"),
                                                             {{L"when", FormatReset(locale, wu.resetUnixMs)}}) :
                                                    L"";
            row.reset.Text(reset);
            show(row.reset, !reset.empty());
        }

        show(popup.legend, anyChart);
        // With charts the details column fits the fixed-width chart and names take the rest;
        // otherwise names fit their widest entry and bars take the rest. Names wrap past
        // kFlyoutNameMaxWidth or their column, a number and its unit never: with charts the card
        // grows past its width for one that its names column can't hold (a long unit, a word for
        // the number).
        if (popup.namesColumn && popup.detailsColumn &&
            (popup.namesColumn.Width().GridUnitType == GridUnitType::Star) != anyChart) {
            popup.namesColumn.Width(anyChart ? GridLength{1, GridUnitType::Star} :
                                               GridLength{0, GridUnitType::Auto});
            popup.detailsColumn.Width(anyChart ?
                GridLength{kFlyoutDetailsLeft + kFlyoutChartWidth + kFlyoutDetailsRight,
                           GridUnitType::Pixel} :
                GridLength{1, GridUnitType::Star});
        }
        double cardWidth = kFlyoutCardWidth;
        for (size_t i = 0; anyChart && i < kMainRows + popup.extraRows.size(); i++) {
            FlyoutQuotaRow& row = i < kMainRows ? popup.rows[i] : popup.extraRows[i - kMainRows];
            if (!row.labels || !row.value || row.labels.Visibility() != Visibility::Visible) continue;
            row.value.Measure({INFINITY, INFINITY});
            double names = row.labels.Margin().Left + row.value.DesiredSize().Width;
            cardWidth = std::max(cardWidth, std::ceil(names) + kFlyoutDetailsLeft + kFlyoutChartWidth +
                                                kFlyoutDetailsRight + 2);
        }
        if (popup.card.Width() != cardWidth) popup.card.Width(cardWidth);

        // The header's texts whole in its tooltip while the label passes its cap or the row the
        // card's width, which the screen's room caps (ShowAccountFlyout).
        if (popup.header) {
            TileTextMeasure measure = XamlTileTextMeasure(locale);
            popup.header.Measure({INFINITY, INFINITY});
            bool cut = measure(account.label, TileFont::Display, 20) > kFlyoutTitleMaxWidth ||
                       popup.header.DesiredSize().Width > std::min(cardWidth, popup.card.MaxWidth()) - 2;
            std::wstring tip = !cut        ? L"" :
                               plan.empty() ? account.label + L" \u00B7 " + ProviderDisplayName(account.provider) :
                                              account.label + L" \u00B7 " + ProviderDisplayName(account.provider) +
                                                  L" \u00B7 " + plan;
            if (tip != popup.headerTip) {
                if (tip.empty()) {
                    ToolTipService::SetToolTip(popup.header, nullptr);
                } else {
                    SetXamlToolTip(popup.header, tip, locale);
                }
                popup.headerTip = std::move(tip);
            }
        }

        // Free resets: Anthropic's a line per kind, with that kind's soonest expiry; another
        // provider's in one line.
        static constexpr I18nKey kResetKinds[kFreeResetKindCount] = {
            TrCtx(L"free-resets",
                  L"{count, plural, one{# five-hour reset available} other{# five-hour resets available}}"),
            TrCtx(L"free-resets", L"{count, plural, one{# Fable reset available} other{# Fable resets available}}"),
            TrCtx(L"free-resets", L"{count, plural, one{# weekly reset available} other{# weekly resets available}}"),
            TrCtx(L"free-resets", L"{count, plural, one{# full reset available} other{# full resets available}}"),
        };
        std::wstring freeResets;
        for (size_t kind = 0; kind < kFreeResetKindCount; kind++) {
            const auto& resets = d.freeResetKinds[kind];
            if (resets.count <= 0) continue;
            std::wstring line = I18nText(locale, kResetKinds[kind], {{L"count", resets.count}});
            if (resets.expiryMs > display.now) {
                line = I18nText(locale,
                                resets.count == 1 ? TrCtx(L"free-resets", L"{resets} \u00B7 expires {when}") :
                                                    TrCtx(L"free-resets", L"{resets} \u00B7 next expires {when}"),
                                {{L"resets", line}, {L"when", FormatReset(locale, resets.expiryMs, true)}});
            }
            if (!freeResets.empty()) freeResets += L'\n';
            freeResets += line;
        }
        if (freeResets.empty() && d.freeResets > 0) {
            freeResets =
                d.freeResetsExpiryMs > display.now ?
                    I18nText(locale,
                             TrCtx(L"free-resets", L"{count, plural, one{# free reset available} other{# free resets "
                                                   L"available}} \u00B7 next expires {when}"),
                             {{L"count", d.freeResets}, {L"when", FormatReset(locale, d.freeResetsExpiryMs, true)}}) :
                    I18nText(locale,
                             TrCtx(L"free-resets",
                                   L"{count, plural, one{# free reset available} other{# free resets available}}"),
                             {{L"count", d.freeResets}});
        }
        popup.freeResets.Text(freeResets);
        show(popup.freeResets, !freeResets.empty());
        std::wstring note;
        switch (d.note) {
            case AccountNote::None: break;
            case AccountNote::ExtraUsageUnlimited:
                note = I18nText(locale, TrCtx(L"flyout-note", L"extra usage: unlimited"));
                break;
            case AccountNote::NoRateLimits:
                note = I18nText(locale, TrCtx(L"flyout-note", L"rate limits: none reported"));
                break;
            case AccountNote::NoPremiumRequestAllowance:
                note = I18nText(locale, TrCtx(L"flyout-note", L"No premium request allowance on this plan."));
                break;
            case AccountNote::NoAiCreditAllowance:
                note = I18nText(locale, TrCtx(L"flyout-note", L"No AI-credit allowance on this plan."));
                break;
        }
        popup.extraLines.Text(note);
        show(popup.extraLines, !note.empty());
        std::wstring error;
        if (d.error) {
            std::wstring message = AccountErrorText(locale, d.error);
            error = d.retryDeadlineMs > display.now ?
                        I18nText(locale, TrCtx(L"flyout-note", L"Error: {error} - retry in {duration}"),
                                 {{L"error", message},
                                  {L"duration",
                                   FormatShortDuration(locale, (d.retryDeadlineMs - display.now + 999) / 1000)}}) :
                        I18nText(locale, TrCtx(L"flyout-note", L"Error: {error}"), {{L"error", message}});
        }
        popup.error.Text(error);
        show(popup.error, !error.empty());
        std::wstring status;
        if (d.needsLogin && !display.visualTestMode) {
            status = I18nText(locale, TrCtx(L"flyout-note", L"Sign-in required. Click the bars to sign in."));
        } else if (!visibleRows && d.lastSuccessMs && error.empty() && note.empty()) {
            status = I18nText(locale, TrCtx(L"flyout-note", L"No quotas reported."));
        }
        popup.status.Text(status);
        show(popup.status, !status.empty());
        show(popup.notes, !freeResets.empty() || !note.empty() || !error.empty() || !status.empty());

        std::wstring updated = display.visualTestMode ? I18nText(locale, TrCtx(L"flyout-footer", L"Visual test data")) :
                                                        FormatUpdated(locale, d.lastSuccessMs, display.stale);
        if (display.refreshing) {
            updated = I18nText(locale, TrCtx(L"flyout-footer", L"{status} - refreshing..."), {{L"status", updated}});
        }
        popup.updated.Text(updated);
    } catch (...) {
        Wh_Log(L"UpdateAccountFlyout: exception");
    }
}
