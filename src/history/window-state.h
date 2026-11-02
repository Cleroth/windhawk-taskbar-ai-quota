// History window thread state, control IDs, and the classic window's view-model types.

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
// The custom-drawn window's posted refresh and export write, and its deferral check timer (main
// window; the toolkit's own timers use 0x7E01-0x7E02).
static constexpr UINT kHistoryRefreshMessage = WM_APP + 183;
static constexpr UINT kHistoryWriteMessage = WM_APP + 184;
static constexpr UINT_PTR kHistoryDeferralTimer = 0x4E01;
static constexpr int kHistorySeriesHeight = 90;

enum HistoryControlId {
    kHistoryAccountCombo = 2800, kHistoryMeterCombo, kHistoryRangeCombo,
    kHistoryOverlayCheck, kHistoryRefreshButton, kHistoryExportButton,
    kHistoryClearButton, kHistorySettingsButton, kHistoryGraphControl, kHistoryInfoButton,
    kHistoryEnableButton,
};

// A built series with its legend placement and per-period render cache.
struct HistoryUiSeries : HistorySeries {
    RECT legendRect{};  // Unscrolled graph coordinates, shared by paint and hit testing.
    std::array<HistoryRenderSeries, 2> render;
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
    bool reloadAfterModal = false;  // The journal changed while a dialog was open
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
