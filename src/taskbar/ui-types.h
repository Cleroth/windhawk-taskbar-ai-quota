// Per-taskbar XAML UI instances and the registry that owns them.

// Inputs of a tile's meter shapes; any change regenerates them.
struct TileShapeKey {
    int layout = -1;
    int barMask = -1;
    double size = -1;
    double stroke = -1;
    double gap = -1;
    int light = -1;
    int paceColor = -1;  // -1 without pace markers.

    bool operator==(const TileShapeKey&) const = default;
};

struct AppliedState {
    std::array<int, kQuotaBarCount> fillPx{-1, -1, -1, -1};
    std::array<uint32_t, kQuotaBarCount> fillColor{0, 0, 0, 0};
    std::array<int, kQuotaBarCount> pacePx{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> paceVisible{-1, -1, -1, -1};
    int paceLight = -1;  // Bars' pace tick colors for a light (1) or dark (0) taskbar.
    std::array<int, kQuotaBarCount> percentAlignments{-1, -1, -1, -1};
    std::array<int, kQuotaBarCount> percentDark{-1, -1, -1, -1};
    std::array<std::wstring, kQuotaBarCount> percentTexts;
    std::wstring labelText;
    double labelOpacity = -1;
    Thickness identityMargin{-1, -1, -1, -1};  // Bar columns' identity, label or logo.
    double columnOpacity = -1;
    int barMask = -1;
    int visible = -1;  // -1 unset, 0 collapsed, 1 visible.

    // Tile layouts. Per slot, fillPx and pacePx hold the fill and pace position in thousandths of
    // the meter.
    TileShapeKey tileShape;
    // Identity (tiles, a bar column's logo identity).
    double identityOpacity = -1;
    int tileWarn = -1;
    int badgeLight = -1;  // Badge halo color for a light (1) or dark (0) taskbar.
    // Every column: 0 none; hover (2) or open flyout (4), plus 1 on a light taskbar.
    int highlight = -1;
    std::wstring headline;
    uint32_t headlineColor = 0;
    // Sparklines: per slot, its chart's drawn shapes' points and colors and its key's corner,
    // flattened; the weekly chart's extra usage key, text and corner, and its color.
    std::array<std::vector<double>, kQuotaBarCount> spark;
    std::wstring sparkExtra;
    uint32_t sparkExtraColor = 0;
};

struct PointerHandlers {
    UIElement element{nullptr};
    winrt::event_token tappedToken{};
    winrt::event_token pointerEnteredToken{};
    winrt::event_token pointerExitedToken{};
    winrt::event_token pointerCanceledToken{};
};

struct MenuItemClickHandler {
    MenuFlyoutItem item{nullptr};
    winrt::event_token token{};
};

// A routed event handler added with AddHandler; RemoveHandler needs the same handler object.
struct RoutedHandler {
    UIElement element{nullptr};
    RoutedEvent event{nullptr};
    winrt::Windows::Foundation::IInspectable handler{nullptr};
};

// A sparkline chart's elements: placed in the face, its plot's background, its shapes in
// SparkShape order (created by UpdateAccountTile), its key, and on the weekly chart the extra
// usage's key and number in their panel.
struct SparkChartUi {
    Grid root{nullptr};
    Border plot{nullptr};
    Grid shapes{nullptr};
    std::vector<wuxs::Path> paths;
    TextBlock key{nullptr};
    StackPanel extra{nullptr};
    TextBlock extraKey{nullptr};
    TextBlock extraText{nullptr};
};

// A tile layout's elements (face tiles, logo + headline, sparkline, quota matrix); bar columns
// with a logo identity hold only the identity. Headline segment fills use AccountUiRefs::fills.
struct TileUiRefs {
    BarLayout layout = BarLayout::Stacked;
    // Face tiles (IsFaceTile): the face, holding the meters, with ring tiles' identity or a
    // gauge's number and key on top. Sparklines: the face holding the charts, also the meters.
    Grid face{nullptr};
    // Meter shapes, regenerated whenever their TileShapeKey changes.
    Panel meters{nullptr};
    // Logo (with an optional number badge) or text, plus the stale-warning mark.
    Grid identity{nullptr};
    FrameworkElement logo{nullptr};
    TextBlock text{nullptr};
    double textWidthPerPt = 0;  // Text width per DIP of font size.
    // Number badge: its text over copies in the halo brush, in a box placed on a Canvas.
    Grid badge{nullptr};
    TextBlock badgeText{nullptr};
    SolidColorBrush badgeHalo{nullptr};
    TextBlock warn{nullptr};
    // Logo + headline and gauges: the most-used quota's number and window key; headline tiles
    // with the countdown show it instead of their segments.
    TextBlock headline{nullptr};
    TextBlock headlineKey{nullptr};
    TextBlock countdown{nullptr};
    // Sparklines: per quota slot, its chart.
    std::array<SparkChartUi, kQuotaBarCount> charts;
    // Quota matrix: each slot's cell, its key and number.
    std::array<StackPanel, kQuotaBarCount> matrixCells{
        StackPanel{nullptr}, StackPanel{nullptr}, StackPanel{nullptr}, StackPanel{nullptr}};
    std::array<TextBlock, kQuotaBarCount> matrixKeys{
        TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}};
    std::array<TextBlock, kQuotaBarCount> matrixValues{
        TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}};
    // Face tiles' fills and pace ticks (halo and core) per quota slot.
    std::array<wuxs::Path, kQuotaBarCount> arcs{
        wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}};
    std::array<wuxs::Path, kQuotaBarCount> paceHalos{
        wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}};
    std::array<wuxs::Path, kQuotaBarCount> paceCores{
        wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}, wuxs::Path{nullptr}};
};

struct AccountUiRefs {
    // Tile root in every layout: hit target, flyout anchor and hover background.
    StackPanel column{nullptr};
    // Bar layouts: the identity and the bars, and the bars alone.
    FrameworkElement content{nullptr};
    FrameworkElement barArea{nullptr};
    // Captured at build time so the flyout matches the tile, including visual-test accounts.
    std::wstring provider;
    uint64_t identity = 0;
    uint64_t historyId = 0;
    MenuFlyout menu{nullptr};
    winrt::event_token menuOpeningToken{};
    winrt::event_token menuClosingToken{};
    // Hides this account; disabled while it is the last visible one.
    MenuFlyoutItem hideItem{nullptr};
    std::array<Border, kQuotaBarCount> tracks{
        Border{nullptr}, Border{nullptr}, Border{nullptr}, Border{nullptr}};
    std::array<Grid, kQuotaBarCount> barItems{
        Grid{nullptr}, Grid{nullptr}, Grid{nullptr}, Grid{nullptr}};
    std::array<Border, kQuotaBarCount> fills{
        Border{nullptr}, Border{nullptr}, Border{nullptr}, Border{nullptr}};
    std::array<Border, kQuotaBarCount> paceTicks{
        Border{nullptr}, Border{nullptr}, Border{nullptr}, Border{nullptr}};
    // Bars' pace tick core and halo, shared by the account's ticks; recolored with the theme.
    SolidColorBrush paceCore{nullptr};
    SolidColorBrush paceHalo{nullptr};
    std::array<TextBlock, kQuotaBarCount> percents{
        TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}, TextBlock{nullptr}};
    TextBlock label{nullptr};  // Bar layouts: the Label identity.
    TileUiRefs tile;
};

// Account details flyout. Brushes and styles come from LoadFlyoutTheme and are shared by the
// card's elements.
struct FlyoutTheme {
    bool light = false;
    bool highContrast = false;
    // The card's surface: `card` paints Acrylic and Solid. Mica, MicaAlt and TintedWallpaper are
    // drawn behind the card's content from the material fields (flyout-material.h), over the
    // blurred wallpaper; `card` holds their opaque fallback color. Solid without transparency
    // effects or in high contrast.
    Material material = Material::Solid;
    winrt::Windows::UI::Color materialTint{};
    float materialTintOpacity = 0;
    float materialLuminosityOpacity = 0;  // Mica's luminosity step; unused by TintedWallpaper.
    Brush card{nullptr};
    Brush cardBorder{nullptr};
    Brush footer{nullptr};
    Brush separator{nullptr};
    Brush chip{nullptr};
    Brush chipBorder{nullptr};
    Brush text{nullptr};
    Brush secondaryText{nullptr};
    Brush mutedText{nullptr};
    Brush warning{nullptr};
    Brush error{nullptr};
    Brush accent{nullptr};
    Brush track{nullptr};
    Brush trackBorder{nullptr};
    Brush paceTick{nullptr};
    Brush paceHalo{nullptr};
    // Burn-up charts: opaque card color around markers, guide lines (even pace, now) and the
    // legend's projection sample.
    Brush markerRing{nullptr};
    Brush chartGuide{nullptr};
    Brush chartKey{nullptr};
    Style buttonStyle{nullptr};
};

enum FlyoutRowIndex {
    kFlyoutFiveHourRow,
    kFlyoutWeeklyRow,
    kFlyoutPoolShortRow,  // Antigravity Claude/GPT pools.
    kFlyoutPoolWeeklyRow,
    kFlyoutFableRow,
    kFlyoutExtraRow,      // Anthropic extra usage or OpenAI credits.
    kFlyoutRowCount,
};

// A usage chart's drawn geometry in plot DIPs. It is the chart's update key: shapes are
// regenerated only when it changes.
struct FlyoutChartShape {
    std::vector<std::vector<winrt::Windows::Foundation::Point>> lines;  // Runs of 2+ readings.
    std::vector<winrt::Windows::Foundation::Point> dots;                // Isolated readings.
    winrt::Windows::Foundation::Point now{-1.f, -1.f};    // Current point; X is the now line.
    winrt::Windows::Foundation::Point limit{-1.f, -1.f};  // Projection end; X < 0 without one.
    uint32_t color = 0;
    bool remaining = false;  // Plots % left; the even-pace diagonal falls.

    bool operator==(const FlyoutChartShape&) const = default;
};

struct FlyoutQuotaRow {
    // Separator, name/number cell and details cell share one grid row; they collapse together.
    FrameworkElement row{nullptr};
    FrameworkElement labels{nullptr};
    FrameworkElement details{nullptr};
    TextBlock name{nullptr};
    TextBlock number{nullptr};
    TextBlock unit{nullptr};
    FrameworkElement trend{nullptr};

    // Bar: hatched track, usage fill and pace tick.
    Grid visual{nullptr};
    ColumnDefinition fillColumn{nullptr};
    ColumnDefinition emptyColumn{nullptr};
    Border fill{nullptr};
    ColumnDefinition paceColumn{nullptr};
    ColumnDefinition paceRestColumn{nullptr};
    FrameworkElement paceTick{nullptr};
    uint32_t fillColor = 0;

    // Usage chart, shown instead of the bar while the row has a recorded curve.
    FrameworkElement chart{nullptr};
    wuxs::Line chartPace{nullptr};
    wuxs::Path chartArea{nullptr};
    wuxs::Path chartLines{nullptr};
    wuxs::Path chartDots{nullptr};
    wuxs::Line chartProjection{nullptr};
    wuxs::Line chartNow{nullptr};
    wuxs::Ellipse chartMarker{nullptr};
    FlyoutChartShape chartShape;

    TextBlock amount{nullptr};
    TextBlock reset{nullptr};
    TextBlock projection{nullptr};
};

struct FlyoutButtonHandler {
    Button button{nullptr};
    winrt::event_token token{};
};

// Everything owned by one open popup; reset as a whole when it closes.
struct FlyoutPopup {
    Flyout flyout{nullptr};
    FrameworkElement card{nullptr};
    winrt::event_token closedToken{};
    std::vector<FlyoutButtonHandler> buttonHandlers;
    winrt::event_token closingToken{};
    // Visible top-level windows of this thread before the popup opened; a visible one that is
    // not listed is the popup's own window.
    std::vector<HWND> windowsBeforeOpen;
    FlyoutTheme theme;
    // The card's composition material, hidden until FlyoutUi::materialFactory has compiled
    // (SettleFlyoutMaterial).
    winrt::Windows::UI::Composition::Visual materialVisual{nullptr};

    TextBlock title{nullptr};
    FrameworkElement staleMarker{nullptr};
    FrameworkElement planChip{nullptr};
    TextBlock plan{nullptr};
    std::array<FlyoutQuotaRow, kFlyoutRowCount> rows;
    // Quota grid columns: name/number and details. Charts give details a fixed width.
    ColumnDefinition namesColumn{nullptr};
    ColumnDefinition detailsColumn{nullptr};
    // Chart legend, shown while a row shows a chart.
    FrameworkElement legend{nullptr};
    FrameworkElement notes{nullptr};
    TextBlock extraLimits{nullptr};
    TextBlock extraLines{nullptr};
    TextBlock error{nullptr};
    TextBlock status{nullptr};
    TextBlock updated{nullptr};

    int accountIndex = -1;
    uint64_t accountIdentity = 0;
    uint64_t generation = 0;
};

struct FlyoutUi {
    FlyoutPopup popup;
    // Incremented per open so callbacks from an older popup are no-ops.
    uint64_t openGeneration = 0;

    // Created on first use; stopped and revoked by ClearQuotaEventState.
    DispatcherTimer dwellTimer{nullptr};
    winrt::event_token dwellTimerToken{};
    // Runs only while a popup is open.
    DispatcherTimer pollTimer{nullptr};
    winrt::event_token pollTimerToken{};
    DispatcherTimer countdownTimer{nullptr};
    winrt::event_token countdownTimerToken{};

    // The card material's effect, kept across opens while its material, light/dark mode (which
    // set its tints) and compositor match; released by ClearQuotaEventState.
    winrt::Windows::UI::Composition::CompositionEffectFactory materialFactory{nullptr};
    Material materialFactoryMaterial = Material::Solid;
    bool materialFactoryLight = false;

    // Hover tracking by account index; -1 when unset.
    int hoverIndex = -1;
    int dwellIndex = -1;
    int hoverSuppressedIndex = -1;  // Explicitly closed while hovered: wait for pointer exit.
    ULONGLONG pointerOutsideTick = 0;  // When the pointer left the popup and its tile; 0 inside.
};

struct QuotaUiInstance {
    HWND hWnd = nullptr;
    DWORD ownerThreadId = 0;
    double rasterizationScale = 1.0;
    bool windowSubclassed = false;
    ULONGLONG buildSettingsGeneration = 0;
    bool buildVisualTestMode = false;
    BarLayout buildBarLayout = BarLayout::Stacked;
    double taskbarHeight = 0;  // DIPs, measured at build; 0 if unknown.
    // Bar layouts: the label line's space beyond its text at the label font size, over its cap
    // height and under its baseline, measured at build (BarIdentityMarginFor).
    double labelAboveCap = 0;
    double labelBelowBaseline = 0;
    // Light taskbar at the last update; picks tile chrome and the bars' default pace tick colors.
    bool lightTheme = false;
    Grid quotaGrid{nullptr};
    Panel injectionParent{nullptr};
    ColumnDefinition quotaColumnDefinition{nullptr};
    std::vector<PointerHandlers> pointerHandlers;
    std::vector<MenuItemClickHandler> menuItemClickHandlers;
    std::vector<RoutedHandler> routedHandlers;
    std::vector<AccountUiRefs> accountRefs;
    std::vector<AppliedState> applied;
    DispatcherTimer paceTimer{nullptr};
    winrt::event_token paceTimerToken{};
    FlyoutUi flyout;
    // Per-account show/hide toggle items, paired with their account index, for cross-instance
    // IsChecked/IsEnabled sync in UpdateQuotaUi (Click is revoked via menuItemClickHandlers).
    std::vector<std::pair<int, ToggleMenuFlyoutItem>> accountToggleItems;
    // The show/hide toggle whose click asked to keep its menu open: the menu's next close request
    // is cancelled and the submenu reopens with the toggle focused. The timer drops a request
    // that no close request followed.
    ToggleMenuFlyoutItem keepMenuOpenToggle{nullptr};
    DispatcherTimer keepMenuOpenTimer{nullptr};
    winrt::event_token keepMenuOpenTimerToken{};
    // Closes the open menu's "Show on taskbar" submenu once the pointer rests on another item.
    DispatcherTimer submenuCloseTimer{nullptr};
    winrt::event_token submenuCloseTimerToken{};
    MenuFlyout submenuCloseMenu{nullptr};
    MenuFlyoutSubItem submenuCloseItem{nullptr};
};

[[clang::no_destroy]] static std::optional<
    std::vector<std::unique_ptr<QuotaUiInstance>>> g_uiInstances{std::in_place};
static std::mutex g_uiInstancesMutex;

static const wchar_t* kRootName = L"AiQuota_Root";

using WindowThreadProc = bool (*)(void*);
struct TaskbarDisplayInfo {
    HWND hWnd = nullptr;
    bool primary = false;
    int monitorNumber = 0;
    RECT rect{};
};
