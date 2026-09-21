// Synthetic fixtures and independent expectations, never a second grouping implementation.
static int passed = 0, failed = 0, assertions = 0;
static std::filesystem::path captureDir;
static constexpr ULONGLONG baseTime = 1700000000000ULL;
static constexpr ULONGLONG hour = 3600000;

#define CHECK(condition) do { ++assertions; if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)

static void Run(const std::string& name, const std::function<void()>& body) {
    controls.clear(); textCalls.clear(); penCalls.clear();
    clientRect = {0, 0, 1200, 1800};
    g_unloading = false; g_historyWindowCancelRequested = false; forceHighContrast = false;
    windowData.clear(); windowRects.clear(); destroyedWindows.clear(); trackingCalls.clear(); tipCalls.clear();
    strokes.clear(); lines.clear(); ellipses.clear(); focusWindow = nullptr; callbackErrors = 0;
    tipText.clear(); tipTextPointer = nullptr; tipActive = false;
    monitorWork = {0, 0, 1920, 1080}; tipSize = {440, 220};
    failGetDc = failTipCreate = failTipAdd = failTracking = false;
    invalidations = measurements = focusRects = deleteFailures = 0; failedDeleteTypes.clear();
    clientQueries = scrollCalls = dcReleases = pointerReplacements = 0;
    invalidationWindows.clear(); defWindowCalls.clear(); defWindowResult = 0;
    tipCallback = nullptr; measurementCallback = nullptr; pointTestCallback = nullptr;
    tipRectCallback = nullptr; tipRectQueries = 0; failTipRect = false; tipOperations.clear();
    try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}

static HistoryWindowState Fixture(std::vector<uint32_t> providers = {1, 2, 3}) {
    HistoryWindowState state;
    state.graph = FakeHandle(1); state.meterCombo = FakeHandle(2);
    state.accountCombo = FakeHandle(3); state.exportButton = FakeHandle(4);
    state.from = state.readFrom = baseTime;
    state.to = baseTime + kHistoryWeekMs;
    for (auto provider : providers) {
        HistoryUiAccount account;
        account.id = 100 + state.accounts.size(); account.provider = provider;
        account.title = L"account-" + std::to_wstring(account.id);
        account.label = account.title;
        account.configured = true; account.recording = true;
        state.accounts.push_back(std::move(account));
    }
    return state;
}

static HistorySample& AddRow(HistoryWindowState& state, size_t account, ULONGLONG time,
                             std::initializer_list<std::pair<int, double>> values) {
    HistorySample sample;
    sample.accountId = state.accounts.at(account).id;
    sample.provider = state.accounts.at(account).provider;
    sample.session = 41; sample.timeMs = time; sample.intervalMs = 60000;
    for (auto [meter, value] : values) {
        sample.meters[meter].value = value;
        sample.meters[meter].durationMs = meter == 0 || meter == 4 ? 5 * hour :
            meter == 1 || meter == 2 || meter == 5 ? kHistoryWeekMs : 0;
    }
    state.accounts[account].rows.push_back(state.snapshot.samples.size());
    state.snapshot.samples.push_back(sample);
    return state.snapshot.samples.back();
}

static void AllLanes(HistoryWindowState& state, bool previousOnly = false) {
    for (size_t account = 0; account < state.accounts.size(); ++account) {
        ULONGLONG start = previousOnly ? state.readFrom : state.from;
        AddRow(state, account, start + 1000, {{0, 10}, {1, 20}, {2, 30}, {3, 40}, {4, 50}, {5, 60}, {6, 12.5}, {7, 800}});
        AddRow(state, account, start + 61000, {{0, 15}, {1, 25}, {2, 35}, {3, 45}, {4, 55}, {5, 65}, {6, 15.5}, {7, 750}});
    }
}

static const HistoryUiSeries& Series(const HistoryWindowState& state, size_t account, int meter) {
    auto it = std::find_if(state.visible.begin(), state.visible.end(), [&](const auto& line) {
        return line.account == account && line.meter == meter;
    });
    if (it == state.visible.end()) throw std::runtime_error("Missing account/meter " + std::to_string(account) + "/" + std::to_string(meter));
    return *it;
}

static std::set<int> Meters(const HistoryWindowState& state, size_t account = 0) {
    std::set<int> result;
    for (const auto& line : state.visible) if (line.account == account) result.insert(line.meter);
    return result;
}

static void SelectAccount(HistoryWindowState& state, int account) {
    controls[state.accountCombo].selection = account + 1;
    DispatchSelection(&state, kHistoryAccountCombo);
}

static void SelectView(HistoryWindowState& state, HistoryView view) {
    controls[state.meterCombo].selection = int(view);
    DispatchSelection(&state, kHistoryMeterCombo);
}

static size_t Occurrences(const std::wstring& text, const std::wstring& part) {
    size_t count = 0, pos = 0;
    while ((pos = text.find(part, pos)) != std::wstring::npos) { ++count; pos += part.size(); }
    return count;
}

static size_t StatusCount(const HistoryWindowState& state, size_t account, const std::wstring& status) {
    size_t count = 0;
    for (const auto& line : state.visible) if (line.account == account) count += Occurrences(line.title, status);
    for (const auto& title : state.noReadings)
        if (title.starts_with(state.accounts[account].title)) count += Occurrences(title, status);
    return count;
}

static bool Painted(const std::wstring& text) {
    return std::any_of(textCalls.begin(), textCalls.end(), [&](const auto& call) { return call.text.find(text) != std::wstring::npos; });
}

// Real GDI, fake BeginPaint/EndPaint: no HWND is created and no UI message loop runs.
struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HFONT font = nullptr;
    HGDIOBJ oldBitmap = nullptr;
    void* pixels = nullptr;
    BITMAPINFO info{};
    explicit Surface(UINT dpi, int width = 1000, int height = 600) {
        clientRect = {0, 0, MulDiv(width, dpi, 96), MulDiv(height, dpi, 96)};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = clientRect.right;
        info.bmiHeader.biHeight = -clientRect.bottom;
        info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        dc = CreateCompatibleDC(nullptr);
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (!dc || !bitmap || !pixels) throw std::runtime_error("Offscreen GDI allocation failed");
        oldBitmap = SelectObject(dc, bitmap); paintDc = dc;
        font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        if (!font) throw std::runtime_error("Fixture font allocation failed");
    }
    void Save(const std::string& stem, UINT dpi) {
        if (captureDir.empty()) return;
        GdiFlush();
        BITMAPFILEHEADER file{};
        file.bfType = 0x4d42; file.bfOffBits = sizeof(file) + sizeof(BITMAPINFOHEADER);
        DWORD bytes = DWORD(clientRect.right * clientRect.bottom * 4);
        file.bfSize = file.bfOffBits + bytes;
        auto path = captureDir / ("grouped-history-" + stem + "-" + std::to_string(dpi) + ".bmp");
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(&file), sizeof(file));
        out.write(reinterpret_cast<const char*>(&info.bmiHeader), sizeof(BITMAPINFOHEADER));
        out.write(static_cast<const char*>(pixels), bytes);
        if (!out) throw std::runtime_error("Capture write failed");
        std::cout << "CAPTURE " << path.string() << '\n';
    }
    ~Surface() {
        paintDc = nullptr;
        if (dc && oldBitmap) SelectObject(dc, oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (font) DeleteObject(font);
        if (dc) DeleteDC(dc);
    }
};

