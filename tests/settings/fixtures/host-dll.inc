// Native-host probe DLL: the extracted toolkit host with a stub client and window icons, run on
// its own worker thread like the settings thread (per-monitor v2, STA, host scope ended before
// the apartment), optionally on a hidden desktop. host-probe.cpp loads it, drives these exports
// and unloads it.

static constexpr PCWSTR kProbeClass = L"AiQuotaProbeUi";
static constexpr PCWSTR kProbeViewportClass = L"AiQuotaProbeUiView";
static constexpr PCWSTR kProbePopupClass = L"AiQuotaProbeUiPopup";
static constexpr PCWSTR kProbeTipClass = L"AiQuotaProbeUiTip";
static constexpr UINT kProbePing = WM_APP + 70;        // Answered with kProbeMagic.
static constexpr UINT kProbeLoseTarget = WM_APP + 71;  // Device loss through the production path.
static constexpr UINT kProbeModal = WM_APP + 72;       // A tracked message box.
static constexpr UINT kProbeCommand = WM_APP + 73;     // wParam: ProbeCommand, lParam: argument.
static constexpr UINT kProbeColor = WM_APP + 74;       // The tracked color dialog.
static constexpr UINT kProbeHistoryDialog = WM_APP + 75;  // wParam: ProbeDialogKind, in the history scope.
static constexpr UINT kProbeNested = WM_APP + 76;      // A Yes/No box over the history scope's dialog.
static constexpr LRESULT kProbeMagic = 0x51A7E;
static constexpr uint32_t kProbeDropdown = 500, kProbeNumberBox = 600, kProbeSlider = 700,
                          kProbePreview = 800, kProbeCard = 900, kProbeHandle = 901, kProbeTextBox = 910,
                          kProbeMenu = 920, kProbeSegments = 1000, kProbeSegmentsOff = 1001, kProbeNav = 2000,
                          kProbeChart = 1100, kProbeTipButton = 1101, kProbeProgress = 1200;
// The history window's dialogs (history/modal.h) as its ops run them: an OK/Cancel message box
// (kProbeNested opens a Yes/No box over it from inside, as the save dialog's overwrite prompt),
// an OK box (About), the save dialog, and two scopes at once (kTwoScopes: the outer one hooked,
// the inner one refused, the outer one still current after the inner one's end).
enum ProbeDialogKind { kAskBox, kInfoBox, kSaveDialog, kTwoScopes };
// The activation request (UiHostConfig::activateMessage), registered by name in both processes.
static UINT ProbeActivateMessage() {
    static const UINT message = RegisterWindowMessageW(L"AiQuotaProbeUiActivate");
    return message;
}

// kColorResult: 1 cancelled (or ended by a close), 2 chosen. kCloses: PrepareClose calls,
// kClosesDragging: those that found a handle drag or capture; kAutoScrolls: the viewport's
// auto-scroll timer ticks. Tooltip mode: the chart's hover reports (with a point, ended) and
// repaints asked for; the tooltip shown and hidden; mouse and activation messages that reached
// it; presses that reached the main window or the viewport; 1 if the tooltip window still
// existed at the main window's NCDESTROY. Every mode: theme notifications that reached the main
// window (WM_SETTINGCHANGE, WM_THEMECHANGED, WM_SYSCOLORCHANGE, WM_DWMCOLORIZATIONCOLORCHANGED),
// icons set on it (WM_SETICON), and the process's GDI and USER object counts at its NCDESTROY.
// History dialogs: the outer dialog's result (a message box's answer; the save dialog: 1 chosen,
// else 100 + its error; -1: no hook, nothing shown), the nested box's answer, 1 if a scope or a
// published dialog was left at NCDESTROY. Progress mode: the progress item's paints.
enum ProbeCounter { kBuilds, kAttaches, kDestroying, kDestroyed, kCancels, kLossResult, kColorResult,
                    kCloses, kClosesDragging, kAutoScrolls, kChartHovers, kChartLeaves, kChartRepaints,
                    kTipShows, kTipHides, kTipMouse, kTipActivate, kWindowPresses, kTipAlive, kThemeMessages,
                    kSetIcons, kGdiAtDestroyed, kUserAtDestroyed, kOuterAnswer, kNestedAnswer, kScopeLeft,
                    kProgressPaints, kCounterCount };

// Commands run on the window thread inside the client's message handler, like client code.
enum ProbeCommand {
    kOpenPopup,    // The dropdown's popup through the host; 1 if it has a window
    kPopupState,   // ProbeState bits
    kFieldKey,     // WM_KEYDOWN `argument` to the viewport (the field's surface)
    kPopupMouse,   // 0/1: left down/up on the third row; 2/3: down/up outside the popup
    kSendMain,     // SendMessage(main, LOWORD(argument), HIWORD(argument), 0)
    kAction,       // 0: actions received, 1: last kind, 2: last value, 3: last text as a number
    kFocus,        // Focus item `argument` from the keyboard; 1 if its window has keyboard focus
    kEditState,    // ProbeEdit bits of the number box's text box
    kEditWindow,   // Its window handle's low 32 bits
    kEditChar,     // WM_CHAR `argument` to it
    kEditKey,      // WM_KEYDOWN `argument` to it
    kEditText,     // Its text as a number
    kRebuild,      // Requests a rebuild; returns the builds so far
    kScroll,       // Scrolls the content to `argument` DIPs
    kFocusedItem,  // The host's focused item id
    kDropFocus,    // Keyboard focus to nothing, as when another app activates
    kSliderMouse,  // argument / 1000: 0 left down, 1 move, 2 up; % 1000: percent along the slider;
                   // ProbeUpdate bits right after it
    kPreviewMouse, // argument / 1000: 0 left down at the preview's center, 1 move by % 1000 pixels, 2 up
    kPreviewWheel, // A sideways wheel notch (right) over the preview, sent to the main window
    kDropCapture,  // ReleaseCapture, as when another window takes capture
    kHandleMouse,  // argument / 1000: 0 left down on the handle (scrolled 150 DIP into view), 1 move
                   // % 1000 DIPs below the press, 2 up there, 3 move % 1000 DIPs above the press;
                   // ProbeUpdate bits right after it
    kSysKey,       // WM_SYSKEYDOWN `argument` (Alt held) to the viewport
    kTextBoxLimit, // The free text box's character limit
    kTextBoxChar,  // WM_CHAR `argument` to it
    kTextBoxKey,   // WM_KEYDOWN `argument` to it
    kItemTop,      // Item `argument`'s top, content DIPs rounded
    kTextBoxTop,   // The free text box's top in viewport pixels
    kPaintNow,     // Paints what is pending on both surfaces; ProbeUpdate bits right after it
    kSliderSweep,  // Moves a slider drag to argument / 1000 then % 1000 percent; paints what is
                   // pending nested (as a modal's paints would, no rebuild there) and runs the
                   // dispatch end's rebuild check. ProbeUpdate bits after the moves | bits after
                   // that << 2 | builds meanwhile << 4
    kSliderValue,  // The built slider's value
    kLiveMain,     // `argument` 1: a slide's live rebuild names the main window too
    kPaintMain,    // Invalidates both surfaces and paints the main window; ProbeUpdate bits after
    kDeclineClose, // The next PrepareClose keeps the window
    kViewHeight,   // The viewport's height, DIPs
    kScrollTop,    // The content's scroll, DIPs rounded
    kStopAutoScroll, // Kills the auto-scroll timer, as if paints and input starved it
    kHistoryMode,  // `argument` > 0: history-like frame (navigation pane of that many entries, segmented
                   // controls in the header); returns the builds so far
    kNavScroll,    // The navigation pane's scroll, DIPs rounded
    kNavRange,     // Its scroll range, DIPs rounded
    kNavVisible,   // 1 if entry `argument` lies wholly inside the pane at its scroll
    kWheel,        // A wheel notch to the main window: argument / 10: 0 over the pane, 1 over the
                   // content; % 10: 0 down, 1 up
    kNavMouse,     // argument / 10000: 0 move, 1 left down, 2 up at x 100 DIP, y % 10000 DIPs (main);
                   // returns the pointer's element right after it (kHot), before the leave that
                   // follows (the real cursor is elsewhere)
    kNavBar,       // argument / 1000: 0 move over the pane's scrollbar thumb, 1 move there and press, 2
                   // move % 1000 pixels below the press, 3 release; returns navBarHot | navBarDragging
                   // << 1 | an element hot << 2 right after it
    kSelectNav,    // Selects entry `argument` (as a client handling a request) and rebuilds
    kHot,          // The pointer's element: id * 16 + link + 1
    kSegmentMouse, // argument / 100: 0 the segmented control, 1 the disabled one; / 10 % 10: the
                   // segment; % 10: 0 move, 1 left down, 2 up (main window, the segment's center);
                   // returns the pointer's element right after it (kHot)
    kClientKeys,   // 0: WM_KEYDOWN the client saw, 1: the last one's key, 2: activation requests
                   // seen, 3: 1 if the popup was still open when the last request arrived
    kTipMode,      // `argument` 1: tooltip mode (a chart in the content, an icon button with a tooltip
                   // on the main surface); returns the builds so far
    kChartMouse,   // argument >> 24: 0 move, 1 left down, 2 up, at (argument >> 12 & 0xFFF, argument &
                   // 0xFFF) DIPs in the chart, to the viewport; ProbeUpdate bits right after it
    kTipButton,    // 0: move over the icon button's center, 1 left down there, 2 up, 3 move to an empty
                   // spot of the main surface; returns the pointer's element (kHot)
    kTipState,     // ProbeTip bits
    kTipRect,      // The tooltip window's rectangle: 0 left, 1 top, 2 right, 3 bottom (screen pixels)
    kTipKey,       // The shown content's key
    kTipNcHit,     // WM_NCHITTEST at the tooltip's center, as the window under it would be asked
    kTipFromPoint, // WindowFromPoint at the tooltip's center: 1 tooltip, 2 viewport, 3 main, 0 other
    kTipUnregister,// Unregisters the tooltip class (no tooltip window yet): 1 if it did
    kTipRegister,  // Registers it again as the host does: 1 if it did
    kEnableMain,   // EnableWindow(main, argument)
    kTipLoseTarget,// Device loss on the tooltip's surface through the production path: 1 if recreated
    kMoveMain,     // Moves the main window to (argument, argument) on the screen
    kSendView,     // SendMessage(viewport, LOWORD(argument), HIWORD(argument), 0)
    kCursorWidth,  // The width of the cursor rectangle the tooltip keeps clear of, pixels
    kHandles,      // The process's object counts, read on the window thread: USER << 16 | GDI;
                   // argument 1: after the thread's pending messages (sent and posted) and paints
                   // are handled, so nothing the window does is half done
    kDialogMode,   // `argument` 1: closes cancel the history scope's dialogs (as the history client)
    kLatch,        // The client's close latch, without a close
    kProgressMode, // `argument` 1: an export-like progress item (client-drawn decoration) on the main
                   // surface; returns the builds so far
    kProgressLoop, // From inside this handler, ten repaints of the progress item through UiHostPaintNow
                   // (the row count changing), as a long write does; before the sixth, `argument` 1
                   // loses the main target through the production path, 2 faults the host, 3 moves
                   // the item in the built frame. Returns repaints done | progress paints << 8 | builds
                   // << 16 | main targets created << 24 | 1 << 28 if the last paint had the item's
                   // current rectangle | 1 << 29 if it showed the last count asked for
};
// The tooltip window's state, read in one command.
enum ProbeTip { kTipWindow = 1, kTipVisible = 2, kTipOwned = 4, kTipNoActivate = 8, kTipNotActive = 16,
                kTipNoCapture = 32, kTipClear = 64, kTipReadout = 128, kTipText = 256, kTipPending = 512 };
// The surfaces with something to repaint, read in the same command as the event (a paint may
// follow as soon as the command returns).
enum ProbeUpdate { kMainUpdate = 1, kViewportUpdate = 2 };
enum ProbeState { kWindow = 1, kVisible = 2, kCaptured = 4, kNoActivate = 8, kOwned = 16,
                  kNotActive = 32, kOpen = 64, kAnyCapture = 128 };
enum ProbeEdit { kEditExists = 1, kEditFocused = 2, kEditEnabled = 4 };

static std::atomic<int> g_counters[kCounterCount];
static std::atomic<HWND> g_probeWindow{nullptr};
static std::atomic<HWND> g_probeModal{nullptr};
static std::atomic<HWND> g_probeDialog{nullptr};  // The history scope's published dialog (g_historyDialog)
static std::atomic<bool> g_probeUnloading{false};
static bool g_probeFailAttach = false;
static HDESK g_probeDesktop = nullptr;
static HANDLE g_probeThread = nullptr;
static bool g_probeInteractive = false;  // On the user's desktop, activated, in tooltip mode
static UiHost* g_probeHost = nullptr;     // The window thread's host, for its message hooks
static HWND g_probeLastTip = nullptr;     // The tooltip window last seen by the hooks
static HHOOK g_probeHooks[2] = {};

// The window thread's message hooks (every mode): what reaches the tooltip window (mouse,
// activation and focus messages; its shows and hides), presses that reach the window, and theme
// notifications and icons that reach the main window.
static void ProbeRecord(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    UiHost* host = g_probeHost;
    if (!host || !window) return;
    bool mouse = (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST) ||
                 (message >= WM_NCMOUSEMOVE && message <= WM_NCXBUTTONDBLCLK);
    if (window == host->main) {
        if (message == WM_SETTINGCHANGE || message == WM_THEMECHANGED || message == WM_SYSCOLORCHANGE ||
            message == WM_DWMCOLORIZATIONCOLORCHANGED) {
            g_counters[kThemeMessages]++;
        }
        if (message == WM_SETICON) g_counters[kSetIcons]++;
    }
    if (window == host->tooltip.window) {
        g_probeLastTip = window;
        if (mouse) g_counters[kTipMouse]++;
        if (message == WM_MOUSEACTIVATE || message == WM_SETFOCUS ||
            (message == WM_ACTIVATE && LOWORD(wParam) != WA_INACTIVE) || (message == WM_NCACTIVATE && wParam)) {
            g_counters[kTipActivate]++;
        }
        if (message == WM_WINDOWPOSCHANGED) {
            UINT flags = reinterpret_cast<const WINDOWPOS*>(lParam)->flags;
            if (flags & SWP_SHOWWINDOW) g_counters[kTipShows]++;
            if (flags & SWP_HIDEWINDOW) g_counters[kTipHides]++;
        }
    } else if ((window == host->main || window == host->viewport) &&
               (message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN)) {
        g_counters[kWindowPresses]++;
    }
}

// Tooltip mode's pointer stays where the probe moved it: the leave that the real cursor's
// position (elsewhere) posts right after the move is dropped.
static void ProbeKeepPointer() {
    MSG leave{};
    while (PeekMessageW(&leave, nullptr, WM_MOUSELEAVE, WM_MOUSELEAVE, PM_REMOVE)) {}
}

static LRESULT CALLBACK ProbeGetMessageHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0 && wParam == PM_REMOVE) {
        const MSG* m = reinterpret_cast<const MSG*>(lParam);
        ProbeRecord(m->hwnd, m->message, m->wParam, m->lParam);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

static LRESULT CALLBACK ProbeCallWndHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code >= 0) {
        const CWPSTRUCT* m = reinterpret_cast<const CWPSTRUCT*>(lParam);
        ProbeRecord(m->hwnd, m->message, m->wParam, m->lParam);
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// Counts the viewport's auto-scroll timer ticks.
static LRESULT CALLBACK ProbeViewportProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id,
                                          DWORD_PTR) {
    if (message == WM_TIMER && wParam == kUiAutoScrollTimer) g_counters[kAutoScrolls]++;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(window, ProbeViewportProc, id);
    return DefSubclassProc(window, message, wParam, lParam);
}

static LRESULT CALLBACK ProbeModalHook(int code, WPARAM wParam, LPARAM lParam) {
    if (code == HCBT_ACTIVATE && !g_probeModal.load()) {
        wchar_t name[16] = {};
        if (GetClassNameW(HWND(wParam), name, 16) && wcscmp(name, L"#32770") == 0) {
            g_probeModal = HWND(wParam);
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// A navigation item; a dropdown, a number box, a slider and a preview strip; cards long enough
// to scroll; then an account-like card (a subtle header with a drag handle), a free text box
// and a menu button. Choices, typing, reverts, slides and focus loss behave like a settings
// client's drafts: they change what the next build shows, then request a rebuild. A handle
// drag's first report rebuilds; later ones move the card and what follows it in place, like the
// settings window's card drag. The color dialog is tracked like the settings client's. In
// history mode (kHistoryMode) the navigation is a scrolling pane of entries, selected by their
// activation, and the header holds a segmented control and a disabled one. Keys and activation
// requests the host passes on are recorded. In tooltip mode (kTipMode) a chart sits above the
// cards, its readout naming the 50 DIP column under the pointer, and an icon button with a
// tooltip on the main surface. In dialog mode (kDialogMode) a close latches and cancels the history
// scope's dialogs as the history client does; in progress mode (kProgressMode) a client-drawn
// progress item sits on the main surface, repainted from inside a handler as an export's write does.
struct ProbeClient final : UiHostClient {
    int choice = 0, sliderValue = 50, segment = 0;
    std::wstring boxText = L"10", freeText;
    int actions = 0;
    UiAction last;
    HWND colorDialog = nullptr;
    std::array<COLORREF, 16> customColors{};
    POINT handlePress{}, lastHandle{};  // Viewport pixels of the last press on the handle, the last event
    int dragFrom = INT_MIN;             // A handle drag's first pointer y, and how far the card moved since
    float dragShift = 0;
    bool liveMain = false;              // A slide's live rebuild names the main window
    int declineCloses = 0;
    int navEntries = 0, navSelected = 0;
    POINT barPress{};                   // Main pixels of the last press on the pane's scrollbar
    int keys = 0, activations = 0;
    WPARAM lastKey = 0;
    bool popupAtActivation = false;
    bool tipMode = g_probeInteractive;
    int pick = 0;  // The chart's hovered column + 1; 0: none
    // History dialogs: the close latch and the scope's context, as the history client's
    bool dialogMode = false, latched = false;
    HistoryDialogContext dialogs{&g_probeDialog, [this] { return latched || g_probeUnloading.load(); }};
    // Progress mode: the count shown, and what the item's last paint saw
    bool progressMode = false;
    int progressValue = 0, progressSeen = -1;
    D2D1_RECT_F progressRect{};

    void PaintCustom(UiHost&, ID2D1RenderTarget* target, UiTargetCache& cache, const UiItem& item,
                     const UiPaintState&) override {
        if (item.id != kProbeProgress) return;
        g_counters[kProgressPaints]++;
        progressSeen = progressValue;
        progressRect = item.rect;
        D2D1_RECT_F bar = item.rect;
        bar.right = bar.left + (bar.right - bar.left) * float(progressValue + 1) / 10;
        target->FillRectangle(&bar, UiBrush(target, cache, UiRgb(0x43A047)));
    }

    // The chart's readout: three rows naming the hovered 50 DIP column; a new column repaints.
    bool ChartHover(UiHost& host, uint32_t chart, std::optional<D2D1_POINT_2F> point,
                    UiTooltipContent& readout) override {
        if (!point) {
            g_counters[kChartLeaves]++;
            return std::exchange(pick, 0) != 0;
        }
        g_counters[kChartHovers]++;
        const UiItem* item = UiFindItem(host.frame, chart);
        int next = item ? 1 + int((point->x - item->rect.left) / 50) : 0;
        bool changed = std::exchange(pick, next) != next;
        if (uint64_t(pick) != readout.key) {
            readout.key = uint64_t(pick);
            for (int i = 0; i < 3; i++) {
                readout.rows.push_back({L"Probe line " + std::to_wstring(i + 1),
                                        {{L"[Current]", UiRunStyle::Normal, UiInk::Secondary},
                                         {L" " + std::to_wstring(pick * 10 + i) + L".0%"},
                                         {L" \u00B7 Sat 31 Oct 14:31", UiRunStyle::Normal, UiInk::Secondary}},
                                        UiRgb(0x5B9BFF), 2});
            }
        }
        if (changed) g_counters[kChartRepaints]++;
        return changed;
    }

    void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) override {
        g_counters[kBuilds]++;
        if (navEntries) {
            frame.navPane = {0, 0, 200, std::max(client.height, 1.0f)};
            for (int i = 0; i < navEntries; i++) {
                UiItem entry;
                entry.id = kProbeNav + uint32_t(i);
                entry.kind = UiItemKind::Nav;
                entry.rect = {8, 6 + 41.0f * float(i), 192, 44 + 41.0f * float(i)};
                entry.text = L"Entry " + std::to_wstring(i);
                entry.runs.push_back({std::to_wstring(i) + L"%"});
                entry.selected = i == navSelected;
                entry.action = {UiActionKind::Navigate, i};
                frame.nav.push_back(std::move(entry));
            }
            frame.navHeight = 6 + 41.0f * float(navEntries) - 3 + 6;
            const std::vector<UiOption> segments = {{L"One", 0}, {L"Two", 1}, {L"Three", 2}, {L"Four", 3}};
            const std::vector<UiOption> off = {{L"A", 0}, {L"B", 1}};
            float width = UiSegmentedSize(*host.graphics, segments).width;
            UiAddSegmented(frame.main, kProbeSegments, {210, 4, 210 + width, 36}, segments, true,
                           {UiActionKind::Command, segment, {}, 15});
            UiAddSegmented(frame.main, kProbeSegmentsOff, {220 + width, 4, 220 + width + 70, 36}, off, false,
                           {UiActionKind::Command, 0, {}, 16});
        } else {
            UiItem nav;
            nav.id = 1;
            nav.kind = UiItemKind::Nav;
            nav.rect = {8, 6, 192, 44};
            nav.text = L"Probe";
            nav.icon = L"\uE946";
            nav.selected = true;
            nav.action = {UiActionKind::Navigate, 0};
            frame.main.push_back(nav);
        }
        if (progressMode) {
            UiItem progress;
            progress.id = kProbeProgress;
            progress.kind = UiItemKind::Custom;
            progress.rect = {250, 8, 400, 32};
            frame.main.push_back(std::move(progress));
        }
        if (tipMode) {
            UiItem button;
            button.id = kProbeTipButton;
            button.kind = UiItemKind::Button;
            button.button = UiButtonStyle::Subtle;
            button.rect = {210, 4, 242, 34};
            button.icon = L"\uE72C";
            button.iconSize = 15;
            button.tooltip = {{L"Refresh (F5)"}};
            button.action = {UiActionKind::Command, 0, {}, 17};
            frame.main.push_back(std::move(button));
        }
        frame.viewport = {200, 40, std::max(client.width, 200.0f), std::max(client.height, 40.0f)};
        UiAddDropdown(*host.graphics, frame.content, kProbeDropdown, {20, 0, 280, 32},
                      {{L"First", 0}, {L"Second", 1}, {L"Third", 2}}, true,
                      {UiActionKind::Command, choice, {}, 7});
        UiAddNumberBox(frame.content, kProbeNumberBox, {20, 44, 20 + 96 + 10 + 40, 76}, boxText,
                       L"days", true, {UiActionKind::Command, 0, {}, 8});
        UiAddSlider(frame.content, kProbeSlider, {300, 0, 420, 32}, sliderValue, 0, 100, true,
                    {UiActionKind::Command, 0, {}, 9});
        UiItem preview;
        preview.id = kProbePreview;
        preview.kind = UiItemKind::Preview;
        preview.rect = {300, 44, 420, 76};
        preview.maximum = 100;
        preview.action = {UiActionKind::Pan, 0, {}, 10};
        frame.content.push_back(preview);
        uint32_t id = 10;
        float y = 88;
        if (tipMode) {
            UiItem chart;
            chart.id = kProbeChart;
            chart.kind = UiItemKind::Chart;
            chart.rect = {20, y, 400, y + 200};
            frame.content.push_back(std::move(chart));
            y += 212;
        }
        for (int i = 0; i < 12; i++) {
            UiCardSpec spec;
            spec.glyph = L"\uE774";
            spec.title = L"Card " + std::to_wstring(i);
            spec.description = {{L"Description"}};
            spec.trailingGlyph = L"\uE8A7";
            spec.action = {UiActionKind::OpenUrl, 0, L"https://example.invalid"};
            y = UiAddCard(*host.graphics, frame.content, id, 20, y, 360, spec).card.bottom + 3;
        }
        y += dragShift;
        UiItem card;
        card.id = kProbeCard;
        card.kind = UiItemKind::Card;
        card.button = UiButtonStyle::Subtle;
        card.rect = {20, y, 380, y + 68};
        card.action = {UiActionKind::Command, 1, {}, 11};
        frame.content.push_back(card);
        UiItem handle;
        handle.id = kProbeHandle;
        handle.kind = UiItemKind::Handle;
        handle.text = L"\uE784";
        handle.rect = {25, y + 14, 55, y + 54};
        handle.owner = kProbeCard;
        handle.action = {UiActionKind::Command, 0, {}, 12};
        frame.content.push_back(handle);
        y += 76;
        UiItem text;
        text.id = kProbeTextBox;
        text.kind = UiItemKind::TextBox;
        text.rect = {20, y, 220, y + 32};
        text.text = freeText;
        text.runs.push_back({L"Hint"});
        text.maximum = 4;
        text.action = {UiActionKind::Command, 0, {}, 13};
        frame.content.push_back(text);
        UiItem menu;
        menu.id = kProbeMenu;
        menu.kind = UiItemKind::Button;
        menu.rect = {240, y, 380, y + 32};
        menu.text = L"Menu";
        menu.options = {{L"One", 0, L"First"}, {L"Two", 1, L"Second"}};
        menu.action = {UiActionKind::Command, -1, {}, 14};
        frame.content.push_back(menu);
        frame.contentHeight = y + 32 + 28;
    }
    void Invoke(UiHost& host, UiAction action) override {
        actions++;
        switch (action.kind) {
            case UiActionKind::Navigate: navSelected = action.value; break;
            case UiActionKind::Command: (action.target == 15 ? segment : choice) = action.value; break;
            case UiActionKind::TextChange: (action.target == 13 ? freeText : boxText) = action.text; break;
            case UiActionKind::TextRevert: (action.target == 13 ? freeText : boxText) = action.target == 13 ? L"" : L"10"; break;
            case UiActionKind::Slide:
                sliderValue = action.value;
                last = std::move(action);
                UiHostRequestLiveRebuild(host, liveMain);
                return;
            case UiActionKind::Drag:
                if (dragFrom != INT_MIN) {
                    float dy = float(action.value - dragFrom) - dragShift;
                    dragShift += dy;
                    UiHostMoveContent(host, [&](std::vector<UiItem>& items) {
                        auto card = std::find_if(items.begin(), items.end(),
                                                 [](const UiItem& item) { return item.id == kProbeCard; });
                        if (!dy || card == items.end()) return false;
                        for (; card != items.end(); ++card) {
                            card->rect.top += dy;
                            card->rect.bottom += dy;
                        }
                        return true;
                    });
                    last = std::move(action);
                    return;
                }
                dragFrom = action.value;
                break;
            case UiActionKind::Drop:
            case UiActionKind::DragCancel:
                dragFrom = INT_MIN;
                dragShift = 0;
                break;
            default: break;
        }
        last = std::move(action);
        UiHostRequestRebuild(host, false);
    }
    int Command(UiHost& host, int command, int argument) {
        // Synthetic input is sent with no keys or buttons held. A thread on the hidden desktop
        // never receives input, so its key state stays the physical keyboard's at the thread's
        // creation: a Shift the user held then turns End into a selection and Tab into Shift+Tab.
        BYTE released[256] = {};
        SetKeyboardState(released);
        UiPopup& popup = host.popup;
        UiTextBox* box = UiFindTextBox(host.boxes, kProbeNumberBox);
        HWND edit = box ? box->window : nullptr;
        auto updates = [&] {
            return (GetUpdateRect(host.main, nullptr, FALSE) ? kMainUpdate : 0) |
                   (GetUpdateRect(host.viewport, nullptr, FALSE) ? kViewportUpdate : 0);
        };
        auto hot = [&] { return int(host.pointer.hot.id) * 16 + host.pointer.hot.link + 1; };
        switch (command) {
            case kOpenPopup:
                UiHostOpenPopup(host, kProbeDropdown);
                return popup.window != nullptr;
            case kPopupState: {
                int state = GetCapture() ? kAnyCapture : 0;
                if (!popup.window) return state;
                state |= kWindow;
                if (IsWindowVisible(popup.window)) state |= kVisible;
                if (GetCapture() == popup.window) state |= kCaptured;
                if (GetWindowLongPtrW(popup.window, GWL_EXSTYLE) & WS_EX_NOACTIVATE) state |= kNoActivate;
                if (GetWindow(popup.window, GW_OWNER) == host.main) state |= kOwned;
                if (GetActiveWindow() != popup.window && GetFocus() != popup.window) state |= kNotActive;
                if (popup.open) state |= kOpen;
                return state;
            }
            case kFieldKey:
                SendMessageW(host.viewport, WM_KEYDOWN, WPARAM(argument), 0);
                return 0;
            case kPopupMouse: {
                if (!popup.window || popup.rows.size() < 3) return 0;
                float scale = float(popup.dpi) / 96;
                const D2D1_RECT_F& row = popup.rows[2].rect;
                POINT at = argument < 2 ?
                    POINT{LONG((row.left + row.right) / 2 * scale), LONG((row.top + row.bottom) / 2 * scale)} :
                    POINT{-40, -40};
                SendMessageW(popup.window, argument % 2 ? WM_LBUTTONUP : WM_LBUTTONDOWN,
                             argument % 2 ? 0 : MK_LBUTTON, MAKELPARAM(at.x, at.y));
                return 1;
            }
            case kSendMain:
                SendMessageW(host.main, LOWORD(argument), HIWORD(argument), 0);
                return 0;
            case kAction:
                return argument == 0 ? actions : argument == 1 ? int(last.kind) :
                       argument == 2 ? last.value : argument == 4 ? int(last.target) : _wtoi(last.text.c_str());
            case kFocus:
                UiHostSetFocus(host, {uint32_t(argument), -1}, true);
                return GetFocus() == UiHostFocusWindow(host, {uint32_t(argument), -1});
            case kEditState:
                return !edit ? 0 : kEditExists | (GetFocus() == edit ? kEditFocused : 0) |
                                   (IsWindowEnabled(edit) ? kEditEnabled : 0);
            case kEditWindow:
                return int(reinterpret_cast<intptr_t>(edit));
            case kEditChar:
                if (edit) SendMessageW(edit, WM_CHAR, WPARAM(argument), 0);
                return 0;
            case kEditKey:
                if (edit) SendMessageW(edit, WM_KEYDOWN, WPARAM(argument), 0);
                return 0;
            case kEditText:
                return edit ? _wtoi(UiTextBoxText(edit).c_str()) : -1;
            case kRebuild:
                UiHostRequestRebuild(host, false);
                return g_counters[kBuilds];
            case kScroll:
                UiHostScrollTo(host, float(argument));
                return 0;
            case kFocusedItem:
                return int(host.focus.id);
            case kDropFocus:
                SetFocus(nullptr);
                return 0;
            case kSliderMouse:
            case kPreviewMouse: {
                // Pixels in the viewport (the content's surface), from the item's DIPs.
                float scale = UiHostScale(host);
                const UiItem* item = UiFindItem(host.frame, command == kSliderMouse ? kProbeSlider : kProbePreview);
                if (!item) return 0;
                int kind = argument / 1000, amount = argument % 1000;
                float x = command == kSliderMouse ?
                    item->rect.left + (item->rect.right - item->rect.left) * float(amount) / 100 :
                    (item->rect.left + item->rect.right) / 2;
                POINT at{LONG(std::lround(x * scale)) + (command == kPreviewMouse && kind == 1 ? amount : 0),
                         LONG(std::lround(((item->rect.top + item->rect.bottom) / 2 - host.scroll) * scale))};
                UINT message = kind == 0 ? WM_LBUTTONDOWN : kind == 1 ? WM_MOUSEMOVE : WM_LBUTTONUP;
                SendMessageW(host.viewport, message, kind == 2 ? 0 : MK_LBUTTON, MAKELPARAM(at.x, at.y));
                return updates();
            }
            case kPreviewWheel: {
                const UiItem* item = UiFindItem(host.frame, kProbePreview);
                if (!item) return 0;
                float scale = UiHostScale(host);
                POINT at{LONG(std::lround((item->rect.left + item->rect.right) / 2 * scale)),
                         LONG(std::lround(((item->rect.top + item->rect.bottom) / 2 - host.scroll) * scale))};
                ClientToScreen(host.viewport, &at);
                SendMessageW(host.main, WM_MOUSEHWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(at.x, at.y));
                return 1;
            }
            case kDropCapture:
                ReleaseCapture();
                return 0;
            case kHandleMouse: {
                // Away from the viewport's edges, which scroll a handle drag.
                float scale = UiHostScale(host);
                const UiItem* item = UiFindItem(host.frame, kProbeHandle);
                if (!item) return 0;
                int kind = argument / 1000, amount = argument % 1000;
                if (kind == 0) {
                    UiHostScrollTo(host, item->rect.top - 150);
                    handlePress = {LONG(std::lround((item->rect.left + item->rect.right) / 2 * scale)),
                                   LONG(std::lround(((item->rect.top + item->rect.bottom) / 2 - host.scroll) * scale))};
                }
                LONG moved = LONG(std::lround(amount * scale));
                POINT at{handlePress.x, handlePress.y + (kind == 1 ? moved : kind == 3 ? -moved : 0)};
                if (kind == 2) at = lastHandle;
                lastHandle = at;
                UINT message = kind == 0 ? WM_LBUTTONDOWN : kind == 2 ? WM_LBUTTONUP : WM_MOUSEMOVE;
                SendMessageW(host.viewport, message, kind == 2 ? 0 : MK_LBUTTON, MAKELPARAM(at.x, at.y));
                return updates();
            }
            case kSysKey:
                SendMessageW(host.viewport, WM_SYSKEYDOWN, WPARAM(argument), 0x20000000);
                return 0;
            case kTextBoxLimit:
            case kTextBoxChar:
            case kTextBoxKey: {
                UiTextBox* free = UiFindTextBox(host.boxes, kProbeTextBox);
                if (!free || !free->window) return -1;
                if (command == kTextBoxLimit) return int(SendMessageW(free->window, EM_GETLIMITTEXT, 0, 0));
                SendMessageW(free->window, command == kTextBoxChar ? WM_CHAR : WM_KEYDOWN, WPARAM(argument), 0);
                return 0;
            }
            case kItemTop: {
                const UiItem* item = UiFindItem(host.frame, uint32_t(argument));
                return item ? int(std::lround(item->rect.top)) : INT_MIN;
            }
            case kTextBoxTop: {
                UiTextBox* free = UiFindTextBox(host.boxes, kProbeTextBox);
                RECT rect{};
                if (!free || !free->window || !GetWindowRect(free->window, &rect)) return INT_MIN;
                MapWindowPoints(nullptr, host.viewport, reinterpret_cast<POINT*>(&rect), 2);
                return int(rect.top);
            }
            case kPaintNow:
                UpdateWindow(host.main);
                UpdateWindow(host.viewport);
                return updates();
            case kSliderSweep: {
                const UiItem* item = UiFindItem(host.frame, kProbeSlider);
                if (!item) return -1;
                float scale = UiHostScale(host);
                int builds = g_counters[kBuilds];
                for (int percent : {argument / 1000, argument % 1000}) {
                    float x = item->rect.left + (item->rect.right - item->rect.left) * float(percent) / 100;
                    POINT at{LONG(std::lround(x * scale)),
                             LONG(std::lround(((item->rect.top + item->rect.bottom) / 2 - host.scroll) * scale))};
                    SendMessageW(host.viewport, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(at.x, at.y));
                }
                int moved = updates();
                UpdateWindow(host.main);
                UpdateWindow(host.viewport);
                UiHostRebuildIfPending(host);
                return moved | updates() << 2 | (g_counters[kBuilds] - builds) << 4;
            }
            case kSliderValue: {
                const UiItem* item = UiFindItem(host.frame, kProbeSlider);
                return item ? item->action.value : -1;
            }
            case kLiveMain:
                liveMain = argument != 0;
                return 0;
            case kPaintMain:
                InvalidateRect(host.main, nullptr, FALSE);
                InvalidateRect(host.viewport, nullptr, FALSE);
                UpdateWindow(host.main);
                return updates();
            case kDeclineClose:
                declineCloses = 1;
                return 0;
            case kViewHeight:
                return int(std::lround(UiHostViewHeight(host)));
            case kScrollTop:
                return int(std::lround(host.scroll));
            case kStopAutoScroll:
                KillTimer(host.viewport, kUiAutoScrollTimer);
                return 0;
            case kHistoryMode:
                navEntries = argument;
                UiHostRequestRebuild(host, true);
                return g_counters[kBuilds];
            case kNavScroll:
                return int(std::lround(host.navScroll));
            case kNavRange: {
                const D2D1_RECT_F& pane = host.frame.navPane;
                return int(std::lround(std::max(host.frame.navHeight - (pane.bottom - pane.top), 0.0f)));
            }
            case kNavVisible: {
                const UiItem* entry = UiFindItem(host.frame, kProbeNav + uint32_t(argument));
                const D2D1_RECT_F& pane = host.frame.navPane;
                return entry && entry->rect.top - host.navScroll >= pane.top &&
                       entry->rect.bottom - host.navScroll <= pane.bottom;
            }
            case kWheel: {
                // Screen pixels at the middle of the pane or of the viewport.
                RECT client{};
                HWND over = argument / 10 ? host.viewport : host.main;
                GetClientRect(over, &client);
                POINT at{argument / 10 ? client.right / 2 : LONG(std::lround(100 * UiHostScale(host))),
                         client.bottom / 2};
                ClientToScreen(over, &at);
                SendMessageW(host.main, WM_MOUSEWHEEL, MAKEWPARAM(0, argument % 10 ? WHEEL_DELTA : -WHEEL_DELTA),
                             MAKELPARAM(at.x, at.y));
                return 1;
            }
            case kNavMouse: {
                float scale = UiHostScale(host);
                int kind = argument / 10000;
                POINT at{LONG(std::lround(100 * scale)), LONG(std::lround(float(argument % 10000) * scale))};
                UINT message = kind == 0 ? WM_MOUSEMOVE : kind == 1 ? WM_LBUTTONDOWN : WM_LBUTTONUP;
                SendMessageW(host.main, message, kind == 1 ? MK_LBUTTON : 0, MAKELPARAM(at.x, at.y));
                return hot();
            }
            case kNavBar: {
                float scale = UiHostScale(host);
                int kind = argument / 1000;
                UiScrollbar bar = UiNavScrollbar(host.frame, host.navScroll, true);
                if (kind < 2) {
                    barPress = {LONG(std::lround((bar.thumb.left + bar.thumb.right) / 2 * scale)),
                                LONG(std::lround((bar.thumb.top + bar.thumb.bottom) / 2 * scale))};
                }
                POINT at{barPress.x, barPress.y + (kind == 2 ? argument % 1000 : 0)};
                if (kind == 1) SendMessageW(host.main, WM_MOUSEMOVE, 0, MAKELPARAM(at.x, at.y));
                UINT message = kind == 1 ? WM_LBUTTONDOWN : kind == 3 ? WM_LBUTTONUP : WM_MOUSEMOVE;
                SendMessageW(host.main, message, kind == 1 || kind == 2 ? MK_LBUTTON : 0, MAKELPARAM(at.x, at.y));
                return int(host.navBarHot) | int(host.navBarDragging) << 1 | int(host.pointer.hot.id != 0) << 2;
            }
            case kSelectNav:
                navSelected = argument;
                UiHostRequestRebuild(host, false);
                return 0;
            case kHot:
                return hot();
            case kSegmentMouse: {
                const UiItem* item = UiFindItem(host.frame, argument / 100 ? kProbeSegmentsOff : kProbeSegments);
                if (!item) return -1;
                std::vector<D2D1_RECT_F> rects = UiSegmentRects(*host.graphics, *item);
                size_t index = size_t(argument / 10 % 10);
                if (index >= rects.size()) return -1;
                float scale = UiHostScale(host);
                POINT at{LONG(std::lround((rects[index].left + rects[index].right) / 2 * scale)),
                         LONG(std::lround((rects[index].top + rects[index].bottom) / 2 * scale))};
                int kind = argument % 10;
                UINT message = kind == 0 ? WM_MOUSEMOVE : kind == 1 ? WM_LBUTTONDOWN : WM_LBUTTONUP;
                SendMessageW(host.main, message, kind == 1 ? MK_LBUTTON : 0, MAKELPARAM(at.x, at.y));
                return hot();
            }
            case kClientKeys:
                return argument == 0 ? keys : argument == 1 ? int(lastKey) :
                       argument == 2 ? activations : int(popupAtActivation);
            case kTipMode:
                tipMode = argument != 0;
                UiHostRequestRebuild(host, true);
                return g_counters[kBuilds];
            case kChartMouse: {
                const UiItem* chart = UiFindItem(host.frame, kProbeChart);
                if (!chart) return -1;
                float scale = UiHostScale(host);
                int kind = argument >> 24;
                float x = chart->rect.left + float(argument >> 12 & 0xFFF), y = chart->rect.top + float(argument & 0xFFF);
                POINT at{LONG(std::lround(x * scale)), LONG(std::lround((y - host.scroll) * scale))};
                UINT message = kind == 0 ? WM_MOUSEMOVE : kind == 1 ? WM_LBUTTONDOWN : WM_LBUTTONUP;
                SendMessageW(host.viewport, message, kind == 1 ? MK_LBUTTON : 0, MAKELPARAM(at.x, at.y));
                ProbeKeepPointer();
                return updates();
            }
            case kTipButton: {
                const UiItem* button = UiFindItem(host.frame, kProbeTipButton);
                if (!button) return -1;
                float scale = UiHostScale(host);
                POINT at = argument == 3 ? POINT{LONG(std::lround(400 * scale)), LONG(std::lround(20 * scale))} :
                    POINT{LONG(std::lround((button->rect.left + button->rect.right) / 2 * scale)),
                          LONG(std::lround((button->rect.top + button->rect.bottom) / 2 * scale))};
                UINT message = argument == 1 ? WM_LBUTTONDOWN : argument == 2 ? WM_LBUTTONUP : WM_MOUSEMOVE;
                SendMessageW(host.main, message, argument == 1 ? MK_LBUTTON : 0, MAKELPARAM(at.x, at.y));
                ProbeKeepPointer();
                return hot();
            }
            case kTipState: {
                const UiTooltip& tip = host.tooltip;
                int state = (tip.pending ? kTipPending : 0) | (tip.model.content.rows.empty() ? 0 : kTipReadout) |
                            (tip.model.content.text.empty() ? 0 : kTipText);
                if (!tip.window) return state;
                state |= kTipWindow;
                if (IsWindowVisible(tip.window)) state |= kTipVisible;
                if (GetWindow(tip.window, GW_OWNER) == host.main) state |= kTipOwned;
                LONG_PTR style = GetWindowLongPtrW(tip.window, GWL_EXSTYLE);
                if ((style & WS_EX_NOACTIVATE) && (style & WS_EX_TOOLWINDOW)) state |= kTipNoActivate;
                if (GetActiveWindow() != tip.window && GetFocus() != tip.window && GetForegroundWindow() != tip.window) {
                    state |= kTipNotActive;
                }
                if (GetCapture() != tip.window) state |= kTipNoCapture;
                // Where the window really is: inside the work area, clear of the pointer's image
                // and hotspot 8 DIP out.
                RECT r{};
                GetWindowRect(tip.window, &r);
                if (tip.model.spot) {
                    const UiTooltipKey& key = tip.model.at;
                    LONG margin = LONG(std::ceil(8 * float(key.dpi) / 96));
                    RECT ex{std::min(key.cursorRect.left, key.cursor.x) - margin,
                            std::min(key.cursorRect.top, key.cursor.y) - margin,
                            std::max(key.cursorRect.right, key.cursor.x + 1) + margin,
                            std::max(key.cursorRect.bottom, key.cursor.y + 1) + margin};
                    const RECT& w = key.work;
                    bool inside = r.left >= w.left && r.top >= w.top && r.right <= w.right && r.bottom <= w.bottom;
                    bool apart = r.right <= ex.left || r.left >= ex.right || r.bottom <= ex.top || r.top >= ex.bottom;
                    if (inside && apart) state |= kTipClear;
                }
                return state;
            }
            case kTipRect: {
                RECT r{};
                if (!host.tooltip.window || !GetWindowRect(host.tooltip.window, &r)) return INT_MIN;
                return argument == 0 ? r.left : argument == 1 ? r.top : argument == 2 ? r.right : r.bottom;
            }
            case kTipKey:
                return int(host.tooltip.model.content.key);
            case kTipNcHit:
            case kTipFromPoint: {
                RECT r{};
                if (!host.tooltip.window || !GetWindowRect(host.tooltip.window, &r)) return -1;
                POINT center{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
                if (command == kTipNcHit) {
                    return int(SendMessageW(host.tooltip.window, WM_NCHITTEST, 0, MAKELPARAM(center.x, center.y)));
                }
                HWND under = WindowFromPoint(center);
                return under == host.tooltip.window ? 1 : under == host.viewport ? 2 : under == host.main ? 3 : 0;
            }
            case kTipUnregister:
                return UnregisterClassW(kProbeTipClass, GetModuleHandleW(nullptr)) != FALSE;
            case kTipRegister: {
                WNDCLASSEXW tipClass{sizeof(tipClass)};
                tipClass.style = CS_DROPSHADOW;
                tipClass.lpfnWndProc = UiHostTooltipProc;
                tipClass.hInstance = GetModuleHandleW(nullptr);
                tipClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
                tipClass.lpszClassName = kProbeTipClass;
                return RegisterClassExW(&tipClass) != 0;
            }
            case kEnableMain:
                EnableWindow(host.main, argument != 0);
                return 0;
            case kTipLoseTarget: {
                UiTooltip& tip = host.tooltip;
                if (!tip.window) return 0;
                UpdateWindow(tip.window);
                int before = tip.surface.created;
                bool had = tip.surface.target != nullptr;
                UiSurfaceDrawResult(tip.surface, D2DERR_RECREATE_TARGET);
                bool discarded = !tip.surface.target && tip.surface.cache.layouts.empty();
                UpdateWindow(tip.window);
                return had && discarded && tip.surface.target && tip.surface.created == before + 1 &&
                       IsWindowVisible(tip.window);
            }
            case kMoveMain:
                SetWindowPos(host.main, nullptr, argument, argument, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            case kSendView:
                SendMessageW(host.viewport, LOWORD(argument), HIWORD(argument), 0);
                return 0;
            case kCursorWidth: {
                RECT cursor = UiCursorRect({0, 0}, host.dpi);
                return int(cursor.right - cursor.left);
            }
            case kHandles: {
                if (argument == 1) {
                    for (MSG msg{}; PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE);) {
                        if (msg.message == WM_QUIT) {
                            PostQuitMessage(int(msg.wParam));
                            break;
                        }
                        TranslateMessage(&msg);
                        DispatchMessageW(&msg);
                    }
                    UpdateWindow(host.main);
                    GdiFlush();
                }
                HANDLE process = GetCurrentProcess();
                return int(GetGuiResources(process, GR_USEROBJECTS) << 16 | GetGuiResources(process, GR_GDIOBJECTS));
            }
            case kDialogMode:
                dialogMode = argument != 0;
                return 0;
            case kLatch:
                latched = true;
                return 0;
            case kProgressMode:
                progressMode = argument != 0;
                UiHostRequestRebuild(host, false);
                return g_counters[kBuilds];
            case kProgressLoop: {
                int builds = g_counters[kBuilds], paints = g_counters[kProgressPaints], created = host.mainSurface.created;
                int painted = 0;
                for (int i = 0; i < 10; i++) {
                    if (i == 5 && argument == 1) UiSurfaceDrawResult(host.mainSurface, D2DERR_RECREATE_TARGET);
                    if (i == 5 && argument == 2) UiHostFault(host);
                    for (UiItem& item : host.frame.main) {
                        if (i == 5 && argument == 3 && item.id == kProbeProgress) {
                            item.rect.left += 40;
                            item.rect.right += 40;
                        }
                    }
                    progressValue = i;
                    painted += UiHostPaintNow(host, kProbeProgress);
                }
                const UiItem* item = UiFindItem(host.frame, kProbeProgress);
                bool current = item && item->rect.left == progressRect.left && item->rect.right == progressRect.right;
                return painted | (g_counters[kProgressPaints] - paints) << 8 | (g_counters[kBuilds] - builds) << 16 |
                       (host.mainSurface.created - created) << 24 | int(current) << 28 | int(progressSeen == 9) << 29;
            }
        }
        return -1;
    }
    bool Message(UiHost& host, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) override {
        if (message == ProbeActivateMessage()) {
            activations++;
            popupAtActivation = host.popup.window != nullptr;
            return true;
        }
        switch (message) {
            case WM_KEYDOWN:
                keys++;
                lastKey = wParam;
                return false;
            case kProbePing:
                result = kProbeMagic;
                return true;
            case kProbeCommand:
                result = Command(host, int(wParam), int(lParam));
                return true;
            case kProbeLoseTarget: {
                UpdateWindow(host.viewport);
                int before = host.viewSurface.created;
                bool had = host.viewSurface.target != nullptr;
                UiSurfaceDrawResult(host.viewSurface, D2DERR_RECREATE_TARGET);
                bool discarded = !host.viewSurface.target && host.viewSurface.cache.layouts.empty();
                UpdateWindow(host.viewport);
                result = had && discarded && host.viewSurface.target &&
                         host.viewSurface.created == before + 1;
                g_counters[kLossResult] = int(result) + 1;
                return true;
            }
            case kProbeModal: {
                HHOOK hook = SetWindowsHookExW(WH_CBT, ProbeModalHook, nullptr, GetCurrentThreadId());
                MessageBoxW(host.main, L"Native-host probe: closed by the probe.", L"Probe",
                            MB_OKCANCEL);
                if (hook) UnhookWindowsHookEx(hook);
                g_probeModal = nullptr;
                result = 1;
                return true;
            }
            case kProbeColor:
                g_counters[kColorResult] =
                    UiChooseColor(host.main, RGB(1, 2, 3), customColors, colorDialog) ? 2 : 1;
                result = 1;
                return true;
            // As the history window's ops show a dialog: the popup and tooltip closed, then the dialog
            // in the history scope (history/modal.h).
            case kProbeHistoryDialog: {
                UiHostClosePopup(host);
                HistoryDialogScope scope(dialogs);
                if (wParam == kTwoScopes) {
                    bool refused = false;
                    {
                        HistoryDialogScope inner(dialogs);
                        refused = !inner.hook;
                    }
                    g_counters[kOuterAnswer] = int(scope.hook != nullptr) | int(refused) << 1 |
                                               int(g_historyDialogScope == &scope) << 2;
                    return true;
                }
                if (!scope.hook) {
                    g_counters[kOuterAnswer] = -1;
                    return true;
                }
                if (wParam == kSaveDialog) {
                    std::wstring path = L"probe.csv";
                    path.resize(32768);
                    OPENFILENAMEW picker{};
                    picker.lStructSize = sizeof(picker);
                    picker.hwndOwner = host.main;
                    picker.lpstrFilter = L"CSV files (*.csv)\0*.csv\0\0";
                    picker.lpstrFile = path.data();
                    picker.nMaxFile = DWORD(path.size());
                    picker.lpstrDefExt = L"csv";
                    picker.Flags = OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                    g_counters[kOuterAnswer] = GetSaveFileNameW(&picker) ? 1 : 100 + int(CommDlgExtendedError());
                } else {
                    g_counters[kOuterAnswer] = MessageBoxW(host.main, L"History dialog probe.", L"Probe",
                                                           wParam == kInfoBox ? MB_OK : MB_OKCANCEL);
                }
                result = 1;
                return true;
            }
            case kProbeNested:
                g_counters[kNestedAnswer] = MessageBoxW(g_probeDialog.load(), L"Replace it?", L"Probe", MB_YESNO);
                return true;
        }
        return false;
    }
    bool Attach(UiHost& host) override {
        g_counters[kAttaches]++;
        if (g_probeFailAttach) return false;
        g_probeWindow = host.main;
        SetWindowSubclass(host.viewport, ProbeViewportProc, 0, 0);
        g_probeHost = &host;
        g_probeLastTip = nullptr;
        g_probeHooks[0] = SetWindowsHookExW(WH_GETMESSAGE, ProbeGetMessageHook, nullptr, GetCurrentThreadId());
        g_probeHooks[1] = SetWindowsHookExW(WH_CALLWNDPROC, ProbeCallWndHook, nullptr, GetCurrentThreadId());
        return true;
    }
    bool CancelModal() override {
        // As the history client: the latch first, then the scope's running dialog.
        if (dialogMode) {
            latched = true;
            bool running = HistoryDialogCancel();
            if (running) g_counters[kCancels]++;
            return running;
        }
        HWND modal = colorDialog && IsWindow(colorDialog) ? colorDialog : g_probeModal.load();
        if (!modal || !IsWindow(modal)) return false;
        g_counters[kCancels]++;
        PostMessageW(modal, WM_COMMAND, IDCANCEL, 0);
        return true;
    }
    bool Unloading() override { return g_probeUnloading; }
    bool PrepareClose(UiHost& host) override {
        g_counters[kCloses]++;
        if (host.handleDrag || dragFrom != INT_MIN || GetCapture()) g_counters[kClosesDragging]++;
        if (declineCloses > 0) {
            declineCloses--;
            return false;
        }
        return true;
    }
    void Destroying() override { g_counters[kDestroying]++; }
    void Destroyed() override {
        g_counters[kDestroyed]++;
        g_counters[kTipAlive] = g_probeLastTip && IsWindow(g_probeLastTip);
        g_counters[kScopeLeft] = g_historyDialogScope != nullptr || g_probeDialog.load() != nullptr;
        g_counters[kGdiAtDestroyed] = int(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS));
        g_counters[kUserAtDestroyed] = int(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS));
        for (HHOOK& hook : g_probeHooks) {
            if (hook) UnhookWindowsHookEx(std::exchange(hook, nullptr));
        }
        g_probeHost = nullptr;
        g_probeWindow = nullptr;
    }
};

static DWORD WINAPI ProbeThread(LPVOID) {
    // A thread joins another desktop only before it owns windows, hooks or a COM apartment.
    if (g_probeDesktop && !SetThreadDesktop(g_probeDesktop)) return 2;
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    bool created = false;
    {
        ProbeClient client;
        UiHostConfig config;
        config.mainClass = kProbeClass;
        config.viewportClass = kProbeViewportClass;
        config.popupClass = kProbePopupClass;
        config.tooltipClass = kProbeTipClass;
        config.title = L"Native-host probe";
        config.size = {640, 480};
        config.activate = g_probeInteractive;
        config.activateMessage = ProbeActivateMessage();
        if (!g_probeInteractive) config.position = POINT{-12000, -12000};
        // Window icons: a palette-inked track, a colored arc and a dot.
        UiItem icon;
        icon.kind = UiItemKind::Shape;
        icon.viewBox = 16;
        icon.parts = {{L"M2,8a6,6 0 1 0 12,0a6,6 0 1 0 -12,0", {}, UiInk::Primary, 0.2f, 2},
                      {L"M8,2A6,6 0 0 1 14,8", UiRgb(0x43A047), UiInk::Inherit, 1, 2},
                      {L"M6.5,8a1.5,1.5 0 1 0 3,0a1.5,1.5 0 1 0 -3,0Z", UiRgb(0xE53935)}};
        config.icon = icon;
        created = UiHostRun(config, client);
    }
    winrt::uninit_apartment();
    return created ? 0 : 1;
}

extern "C" __declspec(dllexport) BOOL ProbeOpen(BOOL failAttach, HDESK desktop) {
    if (g_probeThread) return FALSE;
    for (auto& counter : g_counters) counter = 0;
    g_probeFailAttach = failAttach;
    g_probeDesktop = desktop;
    g_probeUnloading = false;
    g_probeThread = CreateThread(nullptr, 0, ProbeThread, nullptr, 0, nullptr);
    return g_probeThread != nullptr;
}

// This process's windows on the calling thread's desktop, top-level and message-only, with their
// threads and classes (diagnostics when handle counts move): "class@thread" separated by spaces.
extern "C" __declspec(dllexport) int ProbeWindowInventory(wchar_t* buffer, int size) {
    std::wstring list;
    int count = 0;
    auto add = [&](HWND window) {
        DWORD process = 0, thread = GetWindowThreadProcessId(window, &process);
        if (process != GetCurrentProcessId()) return;
        wchar_t name[64] = {};
        GetClassNameW(window, name, 64);
        list += std::wstring(name) + L"@" + std::to_wstring(thread) + L" ";
        count++;
    };
    using Add = decltype(add);
    EnumWindows([](HWND window, LPARAM param) -> BOOL {
        (*reinterpret_cast<Add*>(param))(window);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&add));
    for (HWND window = nullptr; (window = FindWindowExW(HWND_MESSAGE, window, nullptr, nullptr));) add(window);
    if (buffer && size > 0) lstrcpynW(buffer, list.c_str(), size);
    return count;
}

// The cursor rectangle around the real cursor on the calling thread's desktop (reads only).
extern "C" __declspec(dllexport) RECT ProbeCursorRect(POINT* cursor) {
    GetCursorPos(cursor);
    return UiCursorRect(*cursor, 96);
}

// The next window opens on the user's desktop, activated, centered on the cursor's monitor, in
// tooltip mode (the interactive routing mode).
extern "C" __declspec(dllexport) void ProbeSetInteractive(BOOL interactive) { g_probeInteractive = interactive; }
extern "C" __declspec(dllexport) HWND ProbeWindow() { return g_probeWindow; }
extern "C" __declspec(dllexport) HWND ProbeModalWindow() { return g_probeModal; }
extern "C" __declspec(dllexport) int ProbeCount(int counter) { return g_counters[counter]; }
extern "C" __declspec(dllexport) PCWSTR ProbeClassName() { return kProbeClass; }

// Unload's close: set the cancellation flag first, then close, like Wh_ModUninit.
extern "C" __declspec(dllexport) void ProbeUnload() {
    g_probeUnloading = true;
    if (HWND window = g_probeWindow.load()) PostMessageW(window, WM_CLOSE, 0, 0);
}

// The history scope's published dialog, and unload's close of the history window as
// CloseHistoryWindow does it: the flag, a cancel and close to the published dialog, then the window.
extern "C" __declspec(dllexport) HWND ProbeHistoryDialog() { return g_probeDialog; }
extern "C" __declspec(dllexport) void ProbeCloseHistory() {
    g_probeUnloading = true;
    if (HWND dialog = g_probeDialog.load()) {
        PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        PostMessageW(dialog, WM_CLOSE, 0, 0);
    }
    if (HWND window = g_probeWindow.load()) PostMessageW(window, WM_CLOSE, 0, 0);
}

// Joins the worker: its exit code (0 = the window was created), or -1 on timeout.
extern "C" __declspec(dllexport) int ProbeJoin(DWORD timeout) {
    if (!g_probeThread) return -2;
    if (WaitForSingleObject(g_probeThread, timeout) != WAIT_OBJECT_0) return -1;
    DWORD code = 0;
    GetExitCodeThread(g_probeThread, &code);
    CloseHandle(g_probeThread);
    g_probeThread = nullptr;
    return int(code);
}
