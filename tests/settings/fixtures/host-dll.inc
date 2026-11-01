// Native-host probe DLL: the extracted toolkit host with a stub client and window icons, run on
// its own worker thread like the settings thread (per-monitor v2, STA, host scope ended before
// the apartment), optionally on a hidden desktop. host-probe.cpp loads it, drives these exports
// and unloads it.

static constexpr PCWSTR kProbeClass = L"AiQuotaProbeUi";
static constexpr PCWSTR kProbeViewportClass = L"AiQuotaProbeUiView";
static constexpr PCWSTR kProbePopupClass = L"AiQuotaProbeUiPopup";
static constexpr UINT kProbePing = WM_APP + 70;        // Answered with kProbeMagic.
static constexpr UINT kProbeLoseTarget = WM_APP + 71;  // Device loss through the production path.
static constexpr UINT kProbeModal = WM_APP + 72;       // A tracked message box.
static constexpr UINT kProbeCommand = WM_APP + 73;     // wParam: ProbeCommand, lParam: argument.
static constexpr UINT kProbeColor = WM_APP + 74;       // The tracked color dialog.
static constexpr LRESULT kProbeMagic = 0x51A7E;
static constexpr uint32_t kProbeDropdown = 500, kProbeNumberBox = 600, kProbeSlider = 700,
                          kProbePreview = 800, kProbeCard = 900, kProbeHandle = 901, kProbeTextBox = 910,
                          kProbeMenu = 920;

// kColorResult: 1 cancelled (or ended by a close), 2 chosen. kCloses: PrepareClose calls,
// kClosesDragging: those that found a handle drag or capture; kAutoScrolls: the viewport's
// auto-scroll timer ticks.
enum ProbeCounter { kBuilds, kAttaches, kDestroying, kDestroyed, kCancels, kLossResult, kColorResult,
                    kCloses, kClosesDragging, kAutoScrolls, kCounterCount };

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
};
// The surfaces with something to repaint, read in the same command as the event (a paint may
// follow as soon as the command returns).
enum ProbeUpdate { kMainUpdate = 1, kViewportUpdate = 2 };
enum ProbeState { kWindow = 1, kVisible = 2, kCaptured = 4, kNoActivate = 8, kOwned = 16,
                  kNotActive = 32, kOpen = 64, kAnyCapture = 128 };
enum ProbeEdit { kEditExists = 1, kEditFocused = 2, kEditEnabled = 4 };

static std::atomic<int> g_counters[kCounterCount];
static std::atomic<HWND> g_probeWindow{nullptr};
static std::atomic<HWND> g_probeModal{nullptr};
static std::atomic<bool> g_probeUnloading{false};
static bool g_probeFailAttach = false;
static HDESK g_probeDesktop = nullptr;
static HANDLE g_probeThread = nullptr;

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
// settings window's card drag. The color dialog is tracked like the settings client's.
struct ProbeClient final : UiHostClient {
    int choice = 0, sliderValue = 50;
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

    void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) override {
        g_counters[kBuilds]++;
        UiItem nav;
        nav.id = 1;
        nav.kind = UiItemKind::Nav;
        nav.rect = {8, 6, 192, 44};
        nav.text = L"Probe";
        nav.icon = L"\uE946";
        nav.selected = true;
        nav.action = {UiActionKind::Navigate, 0};
        frame.main.push_back(nav);
        frame.viewport = {200, 40, std::max(client.width, 200.0f), std::max(client.height, 40.0f)};
        UiAddDropdown(frame.content, kProbeDropdown, {20, 0, 280, 32},
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
            case UiActionKind::Command: choice = action.value; break;
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
        UiPopup& popup = host.popup;
        UiTextBox* box = UiFindTextBox(host.boxes, kProbeNumberBox);
        HWND edit = box ? box->window : nullptr;
        auto updates = [&] {
            return (GetUpdateRect(host.main, nullptr, FALSE) ? kMainUpdate : 0) |
                   (GetUpdateRect(host.viewport, nullptr, FALSE) ? kViewportUpdate : 0);
        };
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
        }
        return -1;
    }
    bool Message(UiHost& host, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) override {
        switch (message) {
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
        }
        return false;
    }
    bool Attach(UiHost& host) override {
        g_counters[kAttaches]++;
        if (g_probeFailAttach) return false;
        g_probeWindow = host.main;
        SetWindowSubclass(host.viewport, ProbeViewportProc, 0, 0);
        return true;
    }
    bool CancelModal() override {
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
        config.title = L"Native-host probe";
        config.size = {640, 480};
        config.activate = false;
        config.position = POINT{-12000, -12000};
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

extern "C" __declspec(dllexport) HWND ProbeWindow() { return g_probeWindow; }
extern "C" __declspec(dllexport) HWND ProbeModalWindow() { return g_probeModal; }
extern "C" __declspec(dllexport) int ProbeCount(int counter) { return g_counters[counter]; }
extern "C" __declspec(dllexport) PCWSTR ProbeClassName() { return kProbeClass; }

// Unload's close: set the cancellation flag first, then close, like Wh_ModUninit.
extern "C" __declspec(dllexport) void ProbeUnload() {
    g_probeUnloading = true;
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
