// Where the bars go on a taskbar: tray slots found by their neighbours' names, and the positions
// beside Start, where the bars lie over the taskbar's buttons and a margin in the taskbar's own
// layout makes room for them.

// Tray children the tray placements go before, left to right (After Show desktop goes past the
// last). A missing one passes the bars on to the next; none left puts them at the tray's end.
static constexpr PCWSTR kTrayAnchors[] = {L"ControlCenterButton", L"NotificationCenterButton",
                                          L"ShowDesktopStack"};

// The taskbar's buttons beside Start: the root grid the bars lie over, the repeater holding
// Widgets, Start and the apps, and Start.
struct StartArea {
    Grid rootGrid{nullptr};
    FrameworkElement repeater{nullptr};
    FrameworkElement start{nullptr};
};

// The edge a state of the taskbar's DockingStates group (RootGrid's) docks it at; Unknown for none.
static TaskbarEdge DockedEdge(VisualState const& state) {
    winrt::hstring name = state ? state.Name() : winrt::hstring{};
    if (name == L"DockedBottom") return TaskbarEdge::Bottom;
    if (name == L"DockedTop") return TaskbarEdge::Top;
    if (name == L"DockedLeft") return TaskbarEdge::Left;
    if (name == L"DockedRight") return TaskbarEdge::Right;
    return TaskbarEdge::Unknown;
}

static StartArea FindStartArea(FrameworkElement const& root) {
    StartArea area;
    FrameworkElement frame = FindChildByName(root, L"TaskbarFrame", 4);
    FrameworkElement rootGrid = frame ? FindChildByName(frame, L"RootGrid", 1) : nullptr;
    area.rootGrid = rootGrid ? rootGrid.try_as<Grid>() : nullptr;
    if (!area.rootGrid) return area;
    area.repeater = FindChildByName(rootGrid, L"TaskbarFrameRepeater", 1);
    if (!area.repeater) return area;
    int count = VisualTreeHelper::GetChildrenCount(area.repeater);
    for (int i = 0; i < count && !area.start; i++) {
        auto child = VisualTreeHelper::GetChild(area.repeater, i).try_as<FrameworkElement>();
        if (child && winrt::get_class_name(child) == L"Taskbar.ExperienceToggleButton" &&
            winrt::Windows::UI::Xaml::Automation::AutomationProperties::GetAutomationId(child) ==
                L"StartButton") {
            area.start = child;
        }
    }
    return area;
}

// The room a taskbar's bars take beside Start, per taskbar and margin: Start's right margin (Right
// of Start) or the repeater's left one (Left side). Other mods share a margin only additively
// (their old contribution out, their new one in), which keeps ours in it; a change that leaves
// less than our contribution means another mod took the margin: it is never touched again while
// that element lives, and the bars show in the tray. Each record also says why its taskbar shows
// the bars in the tray instead (the settings window's note).
//
// Records belong to the taskbar window's thread, which alone adds, changes and removes them and
// uses their weak references; the settings thread reads only the plain fields. The window's
// destruction removes them. g_placementMutex is a leaf. Never destroyed: weak references still
// held at unload are leaked. Side: the taskbar runs down a side of the screen, where the bars
// don't go beside Start.
enum class PlacementFallback : uint8_t { None, Unavailable, Conflict, Side };

struct PlacementRecord {
    HWND hWnd = nullptr;
    DWORD ownerThreadId = 0;
    bool start = false;  // Start's right margin, else the repeater's left
    winrt::weak_ref<FrameworkElement> element;
    double applied = 0;  // Our contribution to the margin
    double written = 0;  // The margin as we last wrote or accepted it
    bool lost = false;   // Another mod took the margin
    PlacementFallback fallback = PlacementFallback::None;
};

static std::mutex g_placementMutex;
[[clang::no_destroy]] static std::vector<PlacementRecord> g_placementRecords;

// This thread's record of a taskbar's margin (`add`: created if missing). A record another thread
// left on a reused HWND starts over; its weak reference is that thread's, so it is leaked.
static PlacementRecord* FindPlacementRecordLocked(HWND hWnd, bool start, bool add = false) {
    for (PlacementRecord& record : g_placementRecords) {
        if (record.hWnd != hWnd || record.start != start) continue;
        if (record.ownerThreadId != GetCurrentThreadId()) {
            (void)new winrt::weak_ref<FrameworkElement>(std::move(record.element));
            record = PlacementRecord{};
            record.hWnd = hWnd;
            record.start = start;
            record.ownerThreadId = GetCurrentThreadId();
        }
        return &record;
    }
    if (!add) return nullptr;
    PlacementRecord& record = g_placementRecords.emplace_back();
    record.hWnd = hWnd;
    record.ownerThreadId = GetCurrentThreadId();
    record.start = start;
    return &record;
}

// Records why a taskbar shows the bars in the tray (None: it doesn't) and tells an open settings
// window when that changes.
static void SetPlacementFallback(HWND hWnd, bool start, PlacementFallback fallback) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_placementMutex);
        PlacementRecord* record = FindPlacementRecordLocked(hWnd, start, fallback != PlacementFallback::None);
        if (record && record->fallback != fallback) {
            record->fallback = fallback;
            changed = true;
        }
    }
    if (changed) NotifySettingsWindowChanged();
}

// Taskbars on a left or right edge as their threads last built them, for the settings window: plain
// data under g_placementMutex, removed with the window. `unsupported`: the tray isn't a stack, so no
// bars show there.
struct SideTaskbarRecord {
    HWND hWnd = nullptr;
    bool unsupported = false;
};
[[clang::no_destroy]] static std::vector<SideTaskbarRecord> g_sideTaskbars;

// Records whether a taskbar is on a side and whether its bars can't show there, and tells an open
// settings window when that changes.
static void SetSideTaskbar(HWND hWnd, bool side, bool unsupported) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_placementMutex);
        auto it = std::find_if(g_sideTaskbars.begin(), g_sideTaskbars.end(),
                               [&](const SideTaskbarRecord& record) { return record.hWnd == hWnd; });
        if (!side) {
            if (it != g_sideTaskbars.end()) {
                g_sideTaskbars.erase(it);
                changed = true;
            }
        } else if (it == g_sideTaskbars.end()) {
            g_sideTaskbars.push_back({hWnd, unsupported});
            changed = true;
        } else if (it->unsupported != unsupported) {
            it->unsupported = unsupported;
            changed = true;
        }
    }
    if (changed) NotifySettingsWindowChanged();
}

// Gives a margin back: without our contribution, unless another mod took it (the record then stays,
// quarantined). A failed write keeps the record, so a later injection retries or resumes it.
static void GiveBackReservation(HWND hWnd, bool start, FrameworkElement const& element) {
    try {
        Thickness margin = element.Margin();
        double& value = start ? margin.Right : margin.Left;
        double base = 0, applied = 0;
        {
            std::lock_guard<std::mutex> lock(g_placementMutex);
            PlacementRecord* record = FindPlacementRecordLocked(hWnd, start);
            if (!record || record->lost) return;
            applied = record->applied;
            base = (std::fabs(value - record->written) <= 0.5 ? record->written : value) - applied;
            if (base < -0.5) {
                record->lost = true;
                Wh_Log(L"Placement: another mod took the margin beside Start (%.1f, ours %.1f)", value, applied);
                return;
            }
        }
        if (std::fabs(applied) > 0.5) {
            value = base;
            element.Margin(margin);
        }
        std::lock_guard<std::mutex> lock(g_placementMutex);
        std::erase_if(g_placementRecords, [&](const PlacementRecord& record) {
            return record.hWnd == hWnd && record.start == start && !record.lost &&
                   record.ownerThreadId == GetCurrentThreadId();
        });
    } catch (...) {
        Wh_Log(L"Placement: giving back the room beside Start failed");
    }
}

static void ReleaseStartAreaReservation(QuotaUiInstance& state) {
    FrameworkElement element = std::exchange(state.startAreaElement, nullptr);
    if (element) GiveBackReservation(state.hWnd, state.placement == TaskbarPlacement::RightOfStart, element);
}

// Keeps the bars beside Start: their room in the margin follows their width, then they move next
// to it, before the repeater (Left side) or after Start. Runs on the owner thread after each layout
// pass and writes only what changed, so the layout settles. Another mod taking the margin, or Start
// hiding, moves the bars to the tray through a rebuild; until then they are hidden, so they never
// lie over buttons without their room.
static void UpdateStartAreaPlacement(QuotaUiInstance& state) {
    FrameworkElement element = state.startAreaElement;
    FrameworkElement rootGrid = state.injectionParent;
    if (!element || !rootGrid || !state.quotaGrid || !state.quotaGrid.Children().Size()) return;
    const bool start = state.placement == TaskbarPlacement::RightOfStart;
    // Start hidden, or the element out of the taskbar's root grid (its margin no longer makes room).
    DependencyObject ancestor = VisualTreeHelper::GetParent(element);
    for (int depth = 0; ancestor && ancestor != rootGrid && depth < 16; depth++) {
        ancestor = VisualTreeHelper::GetParent(ancestor);
    }
    if (ancestor != rootGrid || (start && element.Visibility() == Visibility::Collapsed)) {
        Wh_Log(L"Placement: %s; the bars move to the tray",
               start ? L"Start was hidden or replaced" : L"the taskbar's buttons were replaced");
        state.quotaGrid.Visibility(Visibility::Collapsed);
        ReleaseStartAreaReservation(state);
        SetPlacementFallback(state.hWnd, start, PlacementFallback::Unavailable);
        StartRetryInject(true);
        return;
    }

    // The bars' width with their panel's margins (the leading 4 DIPs and the gap after accounts).
    double width = state.quotaGrid.Children().GetAt(0).DesiredSize().Width;
    Thickness margin = element.Margin();
    double& value = start ? margin.Right : margin.Left;
    double desired = 0;
    bool lost = false;
    {
        std::lock_guard<std::mutex> lock(g_placementMutex);
        PlacementRecord* record = FindPlacementRecordLocked(state.hWnd, start);
        if (!record || record->lost) return;
        if (std::fabs(value - record->written) > 0.5) {
            if (value - record->applied < -0.5) {
                record->lost = lost = true;
                Wh_Log(L"Placement: another mod took the margin beside Start (%.1f, ours %.1f)", value,
                       record->applied);
            } else {
                record->written = value;  // An additive change: ours is still in it.
            }
        }
        desired = record->written - record->applied + width;
    }
    if (lost) {
        // The margin is another mod's now: left as it is.
        state.quotaGrid.Visibility(Visibility::Collapsed);
        state.startAreaElement = nullptr;
        SetPlacementFallback(state.hWnd, start, PlacementFallback::Conflict);
        StartRetryInject(true);
        return;
    }
    if (std::fabs(desired - value) > 0.5) {
        value = desired;
        element.Margin(margin);
        std::lock_guard<std::mutex> lock(g_placementMutex);
        if (PlacementRecord* record = FindPlacementRecordLocked(state.hWnd, start)) {
            record->written = desired;
            record->applied = width;
        }
    }

    auto origin = element.TransformToVisual(rootGrid).TransformPoint({0, 0});
    double left = std::max(0.0, start ? origin.X + element.ActualWidth() : origin.X - width);
    if (std::fabs(state.quotaGrid.Margin().Left - left) > 0.5) state.quotaGrid.Margin({left, 0, 0, 0});
}
