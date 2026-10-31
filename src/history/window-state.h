// History window thread state, control IDs, meter and view tables, and view-model types.

static HANDLE g_historyWindowThread = nullptr;
static std::mutex g_historyWindowMutex;
static std::atomic<HWND> g_historyWindow{nullptr};
static std::atomic<HWND> g_historyDialog{nullptr};
static std::atomic<bool> g_historyWindowCancelRequested{false};
// History ID that the window's next load selects, from the latest open request; 0 keeps the
// current selection. OpenHistoryWindow stores it under g_historyWindowMutex.
static std::atomic<uint64_t> g_historyRequestedAccount{0};

static constexpr PCWSTR kHistoryWindowClass = L"AiQuotaHistory_" WH_MOD_ID;
static constexpr PCWSTR kHistoryGraphClass = L"AiQuotaHistoryGraph_" WH_MOD_ID;
static constexpr UINT kHistoryActivateMessage = WM_APP + 181;
static constexpr UINT kHistoryLoadMessage = WM_APP + 182;
static constexpr int kHistorySeriesHeight = 90;
static constexpr ULONGLONG kHistoryWeekMs = 7 * kHistoryDayMs;

enum HistoryControlId {
    kHistoryAccountCombo = 2800, kHistoryMeterCombo, kHistoryRangeCombo,
    kHistoryOverlayCheck, kHistoryRefreshButton, kHistoryExportButton,
    kHistoryClearButton, kHistorySettingsButton, kHistoryGraphControl, kHistoryInfoButton,
    kHistoryEnableButton,
};

static constexpr PCWSTR kHistoryMeterNames[kHistoryMeterCount] = {
    L"Short-term used (%)", L"Weekly used (%)", L"Fable weekly used (%)", L"Extra budget used (%)",
    L"Claude/GPT short-term used (%)", L"Claude/GPT weekly used (%)",
    L"Extra spending (USD)", L"Credits remaining",
};
static constexpr const char* kHistoryMeterIds[kHistoryMeterCount] = {
    "short", "weekly", "fable_weekly", "extra_percent", "third_party_short",
    "third_party_weekly", "anthropic_extra_used", "openai_credit_balance",
};

// Views group existing journal lanes; raw meter indices and CSV remain unchanged.
enum class HistoryView { ShortTerm, Weekly, ExtraUsage, AllMeters };
static constexpr PCWSTR kHistoryViewNames[] = {L"Short-term", L"Weekly", L"Extra usage", L"All meters"};
static constexpr unsigned kHistoryViewMeters[] = {
    (1U << 0) | (1U << 4), (1U << 1) | (1U << 2) | (1U << 5),
    (1U << 3) | (1U << 6) | (1U << 7), (1U << kHistoryMeterCount) - 1,
};
static constexpr unsigned kHistoryProviderMeters[] = {
    0, (1U << 0) | (1U << 1) | (1U << 2) | (1U << 3) | (1U << 6),
    (1U << 0) | (1U << 1) | (1U << 7), (1U << 0) | (1U << 1) | (1U << 4) | (1U << 5),
};
static constexpr COLORREF kHistoryColors[] = {
    RGB(0, 80, 230), RGB(220, 100, 0), RGB(0, 140, 40), RGB(200, 0, 160),
    RGB(115, 45, 200), RGB(0, 140, 170), RGB(35, 35, 35), RGB(220, 25, 35),
};

struct HistoryStatistics {
    bool hasValue = false;
    double peak = 0;
    ULONGLONG peakTime = 0;
    double latest = 0;
    ULONGLONG latestTime = 0;
    double coverageMs = 0;
    double nearLimitMs = 0;
    size_t validSamples = 0;
};

struct HistoryRenderPoint {
    int x = 0;
    double value = 0;
    size_t sourceRow = 0;  // Raw provenance for structural tests; paint never dereferences it.
    bool connect = false;
};

struct HistoryRenderSeries {
    std::vector<HistoryRenderPoint> points;
};

struct HistoryUiAccount {
    uint64_t id = 0;
    uint32_t provider = 0;
    std::wstring title, label;
    bool configured = false;
    bool recording = false;
    bool hidden = false;
    bool showWeekly = true, showFableWeekly = true;
    std::vector<size_t> rows;
};

struct HistoryUiSeries {
    size_t account = 0;
    int meter = 0;
    std::wstring title;
    std::wstring legendTitle, hoverTitle;
    RECT legendRect{};  // Unscrolled graph coordinates, shared by paint and hit testing.
    COLORREF color = 0;
    int stroke = 3;  // Logical pixels; scoped quotas use 2, never thin 1px dashes.
    bool plotted = true;
    HistoryStatistics current, previous;
    std::array<HistoryRenderSeries, 2> render;
};

struct HistoryAxis {
    double low = 0, high = 1;
    bool visible = false;
};

struct HistoryWindowState {
    HWND window = nullptr, graph = nullptr;
    HWND accountLabel = nullptr, meterLabel = nullptr, rangeLabel = nullptr;
    HWND accountCombo = nullptr, meterCombo = nullptr, rangeCombo = nullptr;
    HWND overlayCheck = nullptr, refreshButton = nullptr, exportButton = nullptr;
    HWND clearButton = nullptr, settingsButton = nullptr, legend = nullptr, status = nullptr;
    HWND infoButton = nullptr;
    HWND recordingBanner = nullptr, enableButton = nullptr;
    HWND exportTip = nullptr, graphTip = nullptr;
    HFONT font = nullptr;
    UINT dpi = 96;
    HistoryView allAccountsView = HistoryView::Weekly, accountView = HistoryView::AllMeters;
    int range = 0, selected = -1, scroll = 0, wheelRemainder = 0;
    int renderWidth = -1, layoutWidth = -1, graphHeight = 0;
    RECT plot{};  // Unscrolled; layout, reduction, hit tests and statistics share this geometry.
    POINT hoverPoint{-1, -1};  // Ignore mouse moves synthesized by positioning the tooltip.
    std::vector<size_t> hoverSeries;
    std::wstring graphTipText;
    bool graphTipActive = false, trackingMouse = false;
    bool overlay = false, closing = false, modal = false, loading = false;
    bool enabled = false, skipHidden = true, idsPendingSave = false;
    int retention = 90, redThreshold = 90;
    ULONGLONG from = 0, to = 0, readFrom = 0;
    std::array<HistoryAxis, 3> axes;  // Percent, USD, credits; never share unlike units.
    HistorySnapshot snapshot;
    std::vector<HistoryUiAccount> accounts;
    std::vector<HistoryUiSeries> visible;
    std::vector<std::wstring> noReadings;
    std::wstring snapshotStatus;
};

static void HistoryLayout(HistoryWindowState& state, bool newFont);
