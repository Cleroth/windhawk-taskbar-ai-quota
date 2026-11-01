// Synthetic inputs and hand-derived expectations, never a second copy of the model's rules.
static int passed = 0, failed = 0, assertions = 0;
static constexpr ULONGLONG kNow = 1700000000000ULL;
static constexpr ULONGLONG kMinute = 60000;
static constexpr ULONGLONG kFiveHours = 18000000;

#define CHECK(condition) do { ++assertions; if (!(condition)) throw std::runtime_error( \
    std::string("line ") + std::to_string(__LINE__) + ": " #condition); } while (false)
#define CHECK_NEAR(actual, expected) do { ++assertions; double a_ = (actual), e_ = (expected); \
    if (!(std::abs(a_ - e_) < 1e-6)) throw std::runtime_error(std::string("line ") + \
    std::to_string(__LINE__) + ": " #actual " = " + std::to_string(a_) + ", expected " + \
    std::to_string(e_)); } while (false)

static void Run(const std::string& name, const std::function<void()>& body) {
    try { body(); ++passed; std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failed; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
}

static UiGraphics& TestGraphics() {
    static UiGraphics graphics = [] {
        UiGraphics created;
        UiCreateGraphics(created);
        return created;
    }();
    return graphics;
}

static float TestTextWidth(PCWSTR text, UiFont font) {
    return std::ceil(UiLayoutSize(UiLayout(TestGraphics(), text, UiTextStyleFor(font), 10000, false)).width);
}

static MeterDisplay Display(const Settings& settings = {}, double scale = 1, double taskbar = 48) {
    return MeterDisplayFor(settings, kNow, scale, taskbar);
}

static AccountConfig Account(std::wstring provider = L"anthropic", std::wstring label = L"A",
                             std::array<bool, kQuotaBarCount> bars = {true, true, true, true}) {
    AccountConfig account;
    account.provider = provider;
    account.label = label;
    account.showBars = bars;
    return account;
}

// Fetched successfully at kNow; quotas without a window unless set.
static AccountData Fresh(std::array<double, kQuotaBarCount> pct = {-1, -1, -1, -1}) {
    AccountData data;
    data.win5h.pct = pct[0];
    data.winWeek.pct = pct[1];
    data.fableWeek.pct = pct[2];
    data.extraUsage.pct = pct[3];
    data.lastSuccessMs = kNow;
    data.stale = false;
    return data;
}

// A five-hour window that resets `resetInMs` after kNow.
static WindowUsage Window(double pct, long long resetInMs) {
    return {pct, (ULONGLONG)((long long)kNow + resetInMs), kFiveHours};
}

// Meters of an account with the first slot selected and reading `first`.
static AccountMeters First(WindowUsage first, const MeterDisplay& display = Display(),
                           BarLayout layout = BarLayout::Stacked) {
    AccountData data = Fresh();
    data.win5h = first;
    return AccountMeterState(display, layout, Account(L"anthropic", L"A", {true, false, false, false}),
                             data, 0, 0);
}

static QuotaMeter Bar(double pct, const MeterDisplay& display = Display()) {
    return First({pct, 0, 0}, display).quotas[0];
}

static Settings With(const std::function<void(Settings&)>& edit) {
    Settings settings;
    edit(settings);
    return settings;
}
