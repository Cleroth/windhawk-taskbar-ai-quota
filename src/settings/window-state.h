// Settings window thread state.

static HANDLE g_settingsWindowThread = nullptr;
static std::mutex g_settingsWindowMutex;
static std::atomic<bool> g_settingsWindowCancelRequested{false};
