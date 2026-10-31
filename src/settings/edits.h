// Edit controller of the settings window: drafts, the render projection, the commit queue and
// the navigation and close flushes. Pure: it talks to a SettingsStore (production:
// settings/store.h; tests: an in-memory store with failure injection) and reads no globals.
//
// Drafts are keyed by account key (0: global settings) and field. Every change bumps a revision;
// a commit request carries the revision it was made for and is skipped once the draft is gone
// or newer. The drafts, not the queue, are the source of truth.

enum class SettingsField : uint8_t {
    None,
    // Behavior
    ShowOn,  // Monitor mode and number: 0 primary, -1 all, N display N
    ShowDetailsOnHover,
    PollMinutes,
    SkipHiddenPolling,
    Notifications,
    // History
    RecordHistory,  // This Windows user's recording consent
    RetentionDays,
    // Layout
    BarLayout,        // Tile style: a BarLayout
    BarLength,
    BarThickness,
    BarGap,
    RingSize,
    RingThickness,
    LabelPosition,    // A LabelPosition
    LabelFontSize,
    LabelGap,
    ShowBarLabels,    // Quota keys
    PercentFontSize,  // Bar text size
    ShowPaceTicks,
    PaceTickStyle,    // A PaceTickStyle
    PaceTickColor,    // 0xBBGGRR
    AccountMargin,
    RightMargin,      // Tray gap
    // Display
    BarMode,                // A BarMode
    PercentTextVisibility,  // A PercentTextVisibility
    PercentTextAlignment,   // A PercentTextAlignment
    ShowExtraBarAmounts,
    ColorblindMode,
    YellowThreshold,
    OrangeThreshold,
    RedThreshold,
    ShowStaleWarning,
    ShowOpenAiExtraLimits,
    // Operation groups: page resets
    ResetBehavior,
    ResetHistory,
    ResetLayout,
    ResetDisplay,
    // Window commands, never drafts or commits
    OpenHistory,
    OpenHistoryFolder,
    PaceTickColorCustom,  // The color dialog
    ShowOnTaskbar,        // Visual test mode
    PreviewScroll,        // The preview strip's sideways scroll
};

struct SettingsNumberRange {
    int minimum = 0, maximum = 0;
};

static std::optional<SettingsNumberRange> SettingsFieldRange(SettingsField field) {
    switch (field) {
        case SettingsField::PollMinutes: return SettingsNumberRange{2, 24 * 60};
        case SettingsField::RetentionDays: return SettingsNumberRange{1, 3650};
        case SettingsField::BarLength: return SettingsNumberRange{10, 500};
        case SettingsField::BarThickness: return SettingsNumberRange{2, 50};
        case SettingsField::RingSize: return SettingsNumberRange{kRingSizeMin, kRingSizeMax};
        case SettingsField::RingThickness: return SettingsNumberRange{kRingThicknessMin, kRingThicknessMax};
        case SettingsField::LabelFontSize:
        case SettingsField::PercentFontSize: return SettingsNumberRange{6, 24};
        case SettingsField::BarGap:
        case SettingsField::LabelGap:
        case SettingsField::AccountMargin:
        case SettingsField::RightMargin: return SettingsNumberRange{0, 500};
        case SettingsField::YellowThreshold:
        case SettingsField::OrangeThreshold:
        case SettingsField::RedThreshold: return SettingsNumberRange{0, 100};
        default: return std::nullopt;
    }
}

// The fields a page reset returns to their defaults.
static std::vector<SettingsField> SettingsGroupFields(SettingsField group) {
    switch (group) {
        case SettingsField::ResetBehavior:
            return {SettingsField::ShowOn, SettingsField::ShowDetailsOnHover, SettingsField::PollMinutes,
                    SettingsField::SkipHiddenPolling, SettingsField::Notifications};
        case SettingsField::ResetHistory:
            return {SettingsField::RecordHistory, SettingsField::RetentionDays};
        case SettingsField::ResetLayout:
            return {SettingsField::BarLayout,     SettingsField::BarLength,
                    SettingsField::BarThickness,  SettingsField::BarGap,
                    SettingsField::RingSize,      SettingsField::RingThickness,
                    SettingsField::LabelPosition, SettingsField::LabelFontSize,
                    SettingsField::LabelGap,      SettingsField::ShowBarLabels,
                    SettingsField::PercentFontSize, SettingsField::ShowPaceTicks,
                    SettingsField::PaceTickStyle, SettingsField::PaceTickColor,
                    SettingsField::AccountMargin, SettingsField::RightMargin};
        case SettingsField::ResetDisplay:
            return {SettingsField::BarMode,          SettingsField::PercentTextVisibility,
                    SettingsField::PercentTextAlignment, SettingsField::ShowExtraBarAmounts,
                    SettingsField::ColorblindMode,   SettingsField::YellowThreshold,
                    SettingsField::OrangeThreshold,  SettingsField::RedThreshold,
                    SettingsField::ShowStaleWarning, SettingsField::ShowOpenAiExtraLimits};
        default: return {};
    }
}

static int SettingsFieldValue(const Settings& s, SettingsField field) {
    switch (field) {
        case SettingsField::ShowOn:
            return s.taskbarMonitorMode == TaskbarMonitorMode::Primary ? 0 :
                   s.taskbarMonitorMode == TaskbarMonitorMode::All     ? -1 :
                                                                         s.taskbarMonitorNumber;
        case SettingsField::ShowDetailsOnHover: return s.showDetailsOnHover;
        case SettingsField::PollMinutes: return s.pollMinutes;
        case SettingsField::SkipHiddenPolling: return s.skipHiddenPolling;
        case SettingsField::Notifications: return s.enableNotifications;
        case SettingsField::RecordHistory: return s.historyEnabled;
        case SettingsField::RetentionDays: return s.historyRetentionDays;
        case SettingsField::BarLayout: return int(s.barLayout);
        case SettingsField::BarLength: return s.barLength;
        case SettingsField::BarThickness: return s.barThickness;
        case SettingsField::BarGap: return s.barGap;
        case SettingsField::RingSize: return s.ringSize;
        case SettingsField::RingThickness: return s.ringThickness;
        case SettingsField::LabelPosition: return int(s.labelPosition);
        case SettingsField::LabelFontSize: return s.labelFontSize;
        case SettingsField::LabelGap: return s.labelGap;
        case SettingsField::ShowBarLabels: return s.showBarLabels;
        case SettingsField::PercentFontSize: return s.percentFontSize;
        case SettingsField::ShowPaceTicks: return s.showPaceTicks;
        case SettingsField::PaceTickStyle: return int(s.paceTickStyle);
        case SettingsField::PaceTickColor: return int(s.paceTickColor & 0x00FFFFFF);
        case SettingsField::AccountMargin: return s.accountMargin;
        case SettingsField::RightMargin: return s.rightMargin;
        case SettingsField::BarMode: return int(s.barMode);
        case SettingsField::PercentTextVisibility: return int(s.percentTextVisibility);
        case SettingsField::PercentTextAlignment: return int(s.percentTextAlignment);
        case SettingsField::ShowExtraBarAmounts: return s.showExtraBarAmounts;
        case SettingsField::ColorblindMode: return s.colorblindMode;
        case SettingsField::YellowThreshold: return s.yellowThreshold;
        case SettingsField::OrangeThreshold: return s.orangeThreshold;
        case SettingsField::RedThreshold: return s.redThreshold;
        case SettingsField::ShowStaleWarning: return s.showStaleWarning;
        case SettingsField::ShowOpenAiExtraLimits: return s.showOpenAiExtraLimits;
        default: return 0;
    }
}

// Sets one field, or a group's fields to their defaults, to the desired value (never toggles).
// Numbers clamp to their range and choices to their values, so every assigned value is one the
// preview can draw; normalization follows when the settings are applied.
static void SettingsAssign(Settings& s, SettingsField field, int value) {
    const Settings defaults;
    if (std::optional<SettingsNumberRange> range = SettingsFieldRange(field)) {
        value = std::clamp(value, range->minimum, range->maximum);
    }
    switch (field) {
        case SettingsField::ShowOn:
            s.taskbarMonitorMode = value == 0 ? TaskbarMonitorMode::Primary :
                                   value < 0  ? TaskbarMonitorMode::All : TaskbarMonitorMode::Specific;
            if (value > 0) s.taskbarMonitorNumber = value;
            break;
        case SettingsField::ShowDetailsOnHover: s.showDetailsOnHover = value != 0; break;
        case SettingsField::PollMinutes: s.pollMinutes = value; break;
        case SettingsField::SkipHiddenPolling: s.skipHiddenPolling = value != 0; break;
        case SettingsField::Notifications: s.enableNotifications = value != 0; break;
        case SettingsField::RecordHistory: s.historyEnabled = value != 0; break;
        case SettingsField::RetentionDays: s.historyRetentionDays = value; break;
        case SettingsField::BarLayout:
            s.barLayout = BarLayout(std::clamp(value, int(BarLayout::Stacked), int(BarLayout::LogoHeadline)));
            break;
        case SettingsField::BarLength: s.barLength = value; break;
        case SettingsField::BarThickness: s.barThickness = value; break;
        case SettingsField::BarGap: s.barGap = value; break;
        case SettingsField::RingSize: s.ringSize = value; break;
        case SettingsField::RingThickness: s.ringThickness = value; break;
        case SettingsField::LabelPosition:
            s.labelPosition = LabelPosition(std::clamp(value, int(LabelPosition::Hidden),
                                                       int(LabelPosition::Bottom)));
            break;
        case SettingsField::LabelFontSize: s.labelFontSize = value; break;
        case SettingsField::LabelGap: s.labelGap = value; break;
        case SettingsField::ShowBarLabels: s.showBarLabels = value != 0; break;
        case SettingsField::PercentFontSize: s.percentFontSize = value; break;
        case SettingsField::ShowPaceTicks: s.showPaceTicks = value != 0; break;
        case SettingsField::PaceTickStyle:
            s.paceTickStyle = PaceTickStyle(std::clamp(value, int(PaceTickStyle::Caret),
                                                       int(PaceTickStyle::Dot)));
            break;
        case SettingsField::PaceTickColor: s.paceTickColor = COLORREF(value) & 0x00FFFFFF; break;
        case SettingsField::AccountMargin: s.accountMargin = value; break;
        case SettingsField::RightMargin: s.rightMargin = value; break;
        case SettingsField::BarMode:
            s.barMode = BarMode(std::clamp(value, int(BarMode::Used), int(BarMode::Remaining)));
            break;
        case SettingsField::PercentTextVisibility:
            s.percentTextVisibility = PercentTextVisibility(std::clamp(
                value, int(PercentTextVisibility::Never), int(PercentTextVisibility::Always)));
            break;
        case SettingsField::PercentTextAlignment:
            s.percentTextAlignment = PercentTextAlignment(std::clamp(
                value, int(PercentTextAlignment::Adaptive), int(PercentTextAlignment::Right)));
            break;
        case SettingsField::ShowExtraBarAmounts: s.showExtraBarAmounts = value != 0; break;
        case SettingsField::ColorblindMode: s.colorblindMode = value != 0; break;
        case SettingsField::YellowThreshold: s.yellowThreshold = value; break;
        case SettingsField::OrangeThreshold: s.orangeThreshold = value; break;
        case SettingsField::RedThreshold: s.redThreshold = value; break;
        case SettingsField::ShowStaleWarning: s.showStaleWarning = value != 0; break;
        case SettingsField::ShowOpenAiExtraLimits: s.showOpenAiExtraLimits = value != 0; break;
        case SettingsField::ResetDisplay:
            s.barMode = defaults.barMode;
            s.percentTextVisibility = defaults.percentTextVisibility;
            s.percentTextAlignment = defaults.percentTextAlignment;
            s.showExtraBarAmounts = defaults.showExtraBarAmounts;
            s.colorblindMode = defaults.colorblindMode;
            s.yellowThreshold = defaults.yellowThreshold;
            s.orangeThreshold = defaults.orangeThreshold;
            s.redThreshold = defaults.redThreshold;
            s.showStaleWarning = defaults.showStaleWarning;
            s.showOpenAiExtraLimits = defaults.showOpenAiExtraLimits;
            break;
        case SettingsField::ResetLayout:
            s.barLayout = defaults.barLayout;
            s.barLength = defaults.barLength;
            s.barThickness = defaults.barThickness;
            s.barGap = defaults.barGap;
            s.ringSize = defaults.ringSize;
            s.ringThickness = defaults.ringThickness;
            s.labelPosition = defaults.labelPosition;
            s.labelFontSize = defaults.labelFontSize;
            s.labelGap = defaults.labelGap;
            s.showBarLabels = defaults.showBarLabels;
            s.percentFontSize = defaults.percentFontSize;
            s.showPaceTicks = defaults.showPaceTicks;
            s.paceTickStyle = defaults.paceTickStyle;
            s.paceTickColor = defaults.paceTickColor;
            s.accountMargin = defaults.accountMargin;
            s.rightMargin = defaults.rightMargin;
            break;
        case SettingsField::ResetBehavior:
            s.taskbarMonitorMode = defaults.taskbarMonitorMode;
            s.taskbarMonitorNumber = defaults.taskbarMonitorNumber;
            s.showDetailsOnHover = defaults.showDetailsOnHover;
            s.pollMinutes = defaults.pollMinutes;
            s.skipHiddenPolling = defaults.skipHiddenPolling;
            s.enableNotifications = defaults.enableNotifications;
            break;
        case SettingsField::ResetHistory:
            s.historyEnabled = defaults.historyEnabled;
            s.historyRetentionDays = defaults.historyRetentionDays;
            break;
        default: break;
    }
}

// Number text: optional surrounding spaces and sign, digits with optional digit grouping
// (`separator`; any space character when it is one), groups of three after the first.
// Empty, malformed and overflowing text fails; out-of-range values clamp.
struct SettingsParsedNumber {
    bool ok = false;
    int value = 0;
};

static SettingsParsedNumber SettingsParseNumber(std::wstring_view text, SettingsNumberRange range,
                                                wchar_t separator) {
    auto space = [](wchar_t c) { return iswspace(c) || c == 0x00A0 || c == 0x202F; };
    while (!text.empty() && space(text.front())) text.remove_prefix(1);
    while (!text.empty() && space(text.back())) text.remove_suffix(1);
    bool negative = false;
    if (!text.empty() && (text.front() == L'-' || text.front() == L'+')) {
        negative = text.front() == L'-';
        text.remove_prefix(1);
    }
    bool spaceSeparator = space(separator);
    long long value = 0;
    int digits = 0, group = 0;
    bool grouped = false;
    for (wchar_t c : text) {
        if (c >= L'0' && c <= L'9') {
            value = value * 10 + (c - L'0');
            if (value > INT_MAX) return {};
            digits++;
            group++;
            continue;
        }
        bool isSeparator = c == separator || (spaceSeparator && space(c));
        // The first group has 1-3 digits; every later one exactly three.
        if (!isSeparator || group == 0 || group > 3 || (grouped && group != 3)) return {};
        grouped = true;
        group = 0;
    }
    if (!digits || (grouped && group != 3)) return {};
    long long signedValue = negative ? -value : value;
    return {true, int(std::clamp<long long>(signedValue, range.minimum, range.maximum))};
}

// Digit grouping by threes with `separator`.
static std::wstring SettingsFormatNumber(int value, wchar_t separator) {
    std::wstring digits = std::to_wstring(value < 0 ? -(long long)value : (long long)value);
    std::wstring text;
    for (size_t i = 0; i < digits.size(); i++) {
        if (i && (digits.size() - i) % 3 == 0) text += separator;
        text += digits[i];
    }
    return value < 0 ? L"-" + text : text;
}

static std::wstring SettingsRangeError(SettingsNumberRange range) {
    return L"Enter a whole number from " + std::to_wstring(range.minimum) + L" to " +
           std::to_wstring(range.maximum) + L".";
}

// Store ------------------------------------------------------------------------------------------

struct SettingsDisplay {
    int number = 0;  // 1-based, the taskbars' order (primary first)
    int width = 0, height = 0;
    bool primary = false;
};

// Recording state while consent is on.
enum class SettingsRecording : uint8_t { Recording, PendingSave, StorageError };

struct SettingsStatus {
    std::vector<SettingsDisplay> displays;
    SettingsRecording recording = SettingsRecording::Recording;
    std::wstring historyFolder;
    wchar_t groupSeparator = L',';
    double taskbarHeight = 0;  // The primary taskbar's, in DIPs at its own DPI; 0 if unknown
    bool visualTest = false;   // The taskbar shows the sample accounts
};

struct SettingsBaseline {
    Settings settings;
    SettingsStatus status;
};

enum class SettingsSaveResult : uint8_t { Changed, Unchanged, Failed };

// What the controller reads and writes. Each Apply is one transaction on fresh settings.
struct SettingsStore {
    virtual SettingsBaseline Load() = 0;
    // A field (or a group's fields) set to `value`, saved and published.
    virtual SettingsSaveResult Apply(SettingsField field, int value) = 0;
    // Unload or cancellation: stop committing.
    virtual bool Unloading() = 0;

protected:
    ~SettingsStore() = default;
};

// Controller -------------------------------------------------------------------------------------

struct SettingsDraft {
    uint64_t key = 0;
    SettingsField field = SettingsField::None;
    uint64_t revision = 0;
    bool text = false;   // Typed into a number box: `raw` is its text
    std::wstring raw;
    int value = 0;       // Desired value; for text, the last valid one
    bool valid = true;   // `raw` parses
    bool failed = false; // Its last save failed; not retried until the user acts on the field
    std::wstring error;  // Shown with the control
};

struct SettingsRequest {
    uint64_t key = 0;
    SettingsField field = SettingsField::None;
    uint64_t revision = 0;  // 0: a group, which has no draft
    int value = 0;
};

enum class SettingsCloseResult : uint8_t { Saved, Unloading, Blocked };

struct SettingsEdits {
    explicit SettingsEdits(SettingsStore& store) : store(&store) {}

    // Store and its last load
    SettingsStore* store;
    SettingsBaseline baseline;

    // Drafts and requests
    std::vector<SettingsDraft> drafts;
    std::deque<SettingsRequest> queue;
    uint64_t revisions = 0;

    // Baseline and statuses only: drafts, requests and text boxes stay as they are.
    void Reload() { baseline = store->Load(); }

    // What builders and previews see: the baseline with each draft's desired value. Thresholds
    // stay ordered as saving orders them (settings/storage.h): each at least the one before, so
    // an unordered draft shows what it will save.
    Settings Projection() const {
        Settings s = baseline.settings;
        for (const SettingsDraft& draft : drafts) {
            if (!draft.key) SettingsAssign(s, draft.field, draft.value);
        }
        s.orangeThreshold = std::max(s.orangeThreshold, s.yellowThreshold);
        s.redThreshold = std::max(s.redThreshold, s.orangeThreshold);
        return s;
    }

    SettingsDraft* Find(uint64_t key, SettingsField field) {
        for (SettingsDraft& draft : drafts) {
            if (draft.key == key && draft.field == field) return &draft;
        }
        return nullptr;
    }

    // A text box's text (every change). Text that doesn't parse keeps the last valid value.
    void SetText(uint64_t key, SettingsField field, std::wstring text) {
        std::optional<SettingsNumberRange> range = SettingsFieldRange(field);
        if (!range) return;
        SettingsDraft& draft = Touch(key, field);
        SettingsParsedNumber parsed = SettingsParseNumber(text, *range, baseline.status.groupSeparator);
        draft.text = true;
        draft.raw = std::move(text);
        draft.valid = parsed.ok;
        if (parsed.ok) draft.value = parsed.value;
    }

    // Spin buttons and arrow keys: the last valid value stepped, as text. Returns the draft's
    // revision for the debounced commit (CommitStep); 0 for fields without a range.
    uint64_t Step(uint64_t key, SettingsField field, int delta) {
        std::optional<SettingsNumberRange> range = SettingsFieldRange(field);
        if (!range) return 0;
        SettingsDraft& draft = Touch(key, field);
        draft.value = std::clamp(draft.value + delta, range->minimum, range->maximum);
        draft.text = true;
        draft.raw = SettingsFormatNumber(draft.value, baseline.status.groupSeparator);
        draft.valid = true;
        return draft.revision;
    }

    // A step's debounce ran out: commits only the stepped revision, and never retries it after
    // a failed save. Typing or another change since then made a newer draft, which waits for its
    // own Enter, focus loss or flush.
    bool CommitStep(uint64_t key, SettingsField field, uint64_t revision) {
        const SettingsDraft* draft = Find(key, field);
        return draft && revision && draft->revision == revision && !draft->failed &&
               RequestCommit(key, field);
    }

    // Enter, focus loss, a finished step: queues a valid draft (the caller then has the queue
    // processed). An invalid draft gets its error and stays unqueued. A failed draft retries,
    // except on focus loss, which isn't an action on the field.
    bool RequestCommit(uint64_t key, SettingsField field, bool focusLoss = false) {
        SettingsDraft* draft = Find(key, field);
        if (!draft || (focusLoss && draft->failed)) return false;
        if (!draft->valid) {
            if (std::optional<SettingsNumberRange> range = SettingsFieldRange(field)) {
                draft->error = SettingsRangeError(*range);
            }
            return false;
        }
        draft->failed = false;
        draft->error.clear();
        bool queued = std::any_of(queue.begin(), queue.end(), [&](const SettingsRequest& request) {
            return request.key == key && request.field == field && request.revision == draft->revision;
        });
        if (!queued) queue.push_back({key, field, draft->revision, draft->value});
        return true;
    }

    // Esc: back to the committed value; its queued requests go too.
    void Revert(uint64_t key, SettingsField field) {
        std::erase_if(drafts, [&](const SettingsDraft& d) { return d.key == key && d.field == field; });
        std::erase_if(queue, [&](const SettingsRequest& r) { return r.key == key && r.field == field; });
    }

    // A slider drag: the desired value (clamped to the field's range) as a draft, committed when
    // the drag ends (RequestCommit). Returns the draft's revision.
    uint64_t SetValue(uint64_t key, SettingsField field, int value) {
        if (std::optional<SettingsNumberRange> range = SettingsFieldRange(field)) {
            value = std::clamp(value, range->minimum, range->maximum);
        }
        SettingsDraft& draft = Touch(key, field);
        draft.text = false;
        draft.raw.clear();
        draft.value = value;
        draft.valid = true;
        return draft.revision;
    }

    // Discrete controls: the desired value, committed at once with whatever is queued. Returns
    // the number of failed saves.
    int Commit(uint64_t key, SettingsField field, int value) {
        uint64_t revision = SetValue(key, field, value);
        queue.push_back({key, field, revision, Find(key, field)->value});
        return Process();
    }

    // A page reset: its fields' drafts and requests are dropped, then the group commits.
    int Reset(SettingsField group) {
        for (SettingsField field : SettingsGroupFields(group)) Revert(0, field);
        queue.push_back({0, group, 0, 0});
        return Process();
    }

    // Runs the queue in order, one transaction per request, skipping superseded ones.
    int Process() {
        int failures = 0;
        while (!queue.empty()) {
            if (store->Unloading()) {
                queue.clear();
                break;
            }
            SettingsRequest request = queue.front();
            queue.pop_front();
            if (request.revision) {
                const SettingsDraft* draft = Find(request.key, request.field);
                if (!draft || draft->revision != request.revision) continue;
            }
            if (!Run(request)) failures++;
        }
        return failures;
    }

    // Page change, which never blocks: valid drafts commit in order, invalid ones revert
    // without saving, failed ones stay with their error.
    int Navigate() {
        queue.clear();
        int failures = 0;
        for (const auto& [key, field] : Keys()) {
            SettingsDraft* draft = Find(key, field);
            if (!draft || draft->failed) continue;
            if (!draft->valid) {
                Revert(key, field);
                continue;
            }
            if (store->Unloading()) break;
            if (!Run({key, field, draft->revision, draft->value})) failures++;
        }
        return failures;
    }

    // Deliberate close: every draft commits, one transaction at a time, rechecking unload in
    // between. An invalid or failing draft stops it with its error (Blocked): the window stays
    // unless the user discards.
    SettingsCloseResult PrepareClose() {
        queue.clear();
        for (const auto& [key, field] : Keys()) {
            SettingsDraft* draft = Find(key, field);
            if (!draft) continue;
            if (store->Unloading()) return SettingsCloseResult::Unloading;
            if (!draft->valid) {
                if (std::optional<SettingsNumberRange> range = SettingsFieldRange(field)) {
                    draft->error = SettingsRangeError(*range);
                }
                return SettingsCloseResult::Blocked;
            }
            if (!Run({key, field, draft->revision, draft->value})) return SettingsCloseResult::Blocked;
        }
        return SettingsCloseResult::Saved;
    }

    void Discard() {
        drafts.clear();
        queue.clear();
    }

private:
    // The draft of a user action, created from the projection; a new revision clears any error.
    SettingsDraft& Touch(uint64_t key, SettingsField field) {
        SettingsDraft* draft = Find(key, field);
        if (!draft) {
            SettingsDraft created;
            created.key = key;
            created.field = field;
            created.value = SettingsFieldValue(Projection(), field);
            drafts.push_back(std::move(created));
            draft = &drafts.back();
        }
        draft->revision = ++revisions;
        draft->failed = false;
        draft->error.clear();
        return *draft;
    }

    std::vector<std::pair<uint64_t, SettingsField>> Keys() const {
        std::vector<std::pair<uint64_t, SettingsField>> keys;
        for (const SettingsDraft& draft : drafts) keys.emplace_back(draft.key, draft.field);
        return keys;
    }

    // One transaction. Saved (or already so): the draft it was made for is done and the
    // baseline reloads. Failed: that draft keeps its value with the error.
    bool Run(const SettingsRequest& request) {
        SettingsSaveResult result = store->Apply(request.field, request.value);
        auto same = [&](const SettingsDraft& d) {
            return d.key == request.key && d.field == request.field && d.revision == request.revision;
        };
        if (result == SettingsSaveResult::Failed) {
            if (request.revision) {
                auto draft = std::find_if(drafts.begin(), drafts.end(), same);
                if (draft != drafts.end()) {
                    draft->failed = true;
                    draft->error = L"This change couldn't be saved.";
                }
            }
            return false;
        }
        if (request.revision) std::erase_if(drafts, same);
        Reload();
        return true;
    }
};
