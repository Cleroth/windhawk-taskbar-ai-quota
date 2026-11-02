// Usage history window thread state and messages.

static HANDLE g_historyWindowThread = nullptr;
static std::mutex g_historyWindowMutex;
static std::atomic<HWND> g_historyDialog{nullptr};
static std::atomic<bool> g_historyWindowCancelRequested{false};
// History ID that the window's next load selects, from the latest open request; 0 keeps the
// current selection. OpenHistoryWindow stores it under g_historyWindowMutex.
static std::atomic<uint64_t> g_historyRequestedAccount{0};

// The window's activation request and explicit load, its posted refresh and export write, and its
// deferral check timer (main window; the toolkit's own timers use 0x7E01-0x7E02).
static constexpr UINT kHistoryActivateMessage = WM_APP + 181;
static constexpr UINT kHistoryLoadMessage = WM_APP + 182;
static constexpr UINT kHistoryRefreshMessage = WM_APP + 183;
static constexpr UINT kHistoryWriteMessage = WM_APP + 184;
static constexpr UINT_PTR kHistoryDeferralTimer = 0x4E01;
