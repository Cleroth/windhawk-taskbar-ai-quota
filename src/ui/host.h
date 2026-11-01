// Window host of the custom-drawn windows (native): a main window (navigation, pinned header)
// and its scrolling viewport child, each with its own render target, the main window optionally
// with a scrolling navigation pane; the dropdown popup and the number boxes' native text boxes;
// theme, window icons, DPI, pointer and keyboard input, focus, scrolling, close paths and
// teardown. A client supplies the content and the window-specific behavior. Rules: ui/AGENTS.md.

struct UiHost;

// What a window built on the host provides. Calls arrive on the window thread inside the host's
// exception boundary (they may throw: the host fault-closes). Actions arrive as copies; item
// references don't survive client calls.
struct UiHostClient {
    // Builds the frame for a client area of `client` DIPs.
    virtual void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) = 0;
    // Item and link activations, dropdown choices, number box steps and text events.
    virtual void Invoke(UiHost& host, UiAction action) = 0;
    // Main window messages the host doesn't handle, WM_KEYDOWN that neither surface uses, and
    // the activation request once the host brought the window back; true if handled.
    virtual bool Message(UiHost& host, UINT message, WPARAM wParam, LPARAM lParam,
                         LRESULT& result) = 0;
    // End of the main window's WM_CREATE; false fails creation.
    virtual bool Attach(UiHost& host) = 0;
    // Forces a tracked modal (message box, dialog) to end; true while one is still running.
    virtual bool CancelModal() = 0;
    // Unload or cancellation: close without saving.
    virtual bool Unloading() = 0;
    // Deliberate user close; false keeps the window.
    virtual bool PrepareClose(UiHost& host) = 0;
    // Main window WM_DESTROY, then WM_NCDESTROY.
    virtual void Destroying() = 0;
    virtual void Destroyed() = 0;
    // Draws a client-drawn item (UiCustomPaint) on a surface's target, inside its BeginDraw:
    // never pumps messages, rebuilds or shows a modal.
    virtual void PaintCustom(UiHost&, ID2D1RenderTarget*, UiTargetCache&, const UiItem&,
                             const UiPaintState&) {}

protected:
    ~UiHostClient() = default;
};

struct UiHostConfig {
    PCWSTR mainClass = nullptr, viewportClass = nullptr, title = nullptr;
    PCWSTR popupClass = nullptr;       // Dropdown popup; nullptr: dropdowns don't open.
    UINT activateMessage = 0;          // Registered "show yourself" message; 0 = none.
    D2D1_SIZE_F size{1000, 860};       // Initial outer size, DIPs.
    D2D1_SIZE_F minSize{500, 400};     // Minimum outer size, DIPs.
    bool activate = true;              // Show active and in the foreground.
    std::optional<POINT> position;     // Top-left in pixels; default: centered on the cursor's monitor.
    std::optional<UiItem> icon;        // Title bar and Alt+Tab icon (a Shape); none: the default.
};

struct UiHost {
    // Wiring, owned by UiHostRun's scope
    const UiHostConfig* config = nullptr;
    UiHostClient* client = nullptr;
    UiGraphics* graphics = nullptr;
    UiThemeReader* themes = nullptr;

    // Windows, targets, DPI and theme; the main window's icons (ICON_SMALL, ICON_BIG), redrawn
    // for its DPI and palette and destroyed once it is gone
    HWND main = nullptr, viewport = nullptr;
    UiSurface mainSurface, viewSurface;
    UINT dpi = 96;
    UiThemeInput theme;
    UiPalette palette;
    std::array<HICON, 2> icons{};

    // Frame and interaction. The pointer position (item coordinates of its surface) feeds
    // client-drawn hover; a slider, preview or handle drag keeps its item until release. A
    // slider drag reports values that differ from its last report (the frame may lag behind); a
    // handle's drag starts once the pointer moved (handleMoved) and scrolls while near an edge.
    // The navigation pane scrolls on its own, with its own scrollbar.
    UiFrame frame;
    std::vector<UiFocusStop> stops;
    UiPointerState pointer;
    UiFocusRef focus;
    bool focusVisible = false;
    float scroll = 0, navScroll = 0;
    bool scrollbarHot = false, scrollDragging = false, navBarHot = false, navBarDragging = false;
    float scrollDragOffset = 0;
    D2D1_POINT_2F pointerAt{-1e9f, -1e9f};
    UiSurfaceId pointerSurface = UiSurfaceId::Main;
    uint32_t sliderDrag = 0, panDrag = 0, handleDrag = 0;
    int slideValue = 0;
    bool handleMoved = false, autoScrolling = false;
    ULONGLONG autoScrollAt = 0;  // The auto-scroll's last step (or its start)
    float dragAnchor = 0;  // Slider: pointer offset from the thumb; preview: last pointer x;
                           // handle: the press's content y

    // Dropdown popup and number boxes' text boxes
    UiPopup popup;
    UiTextBoxes boxes;
    UiFocusRef pendingFocus;  // Focused once the next rebuild has built it
    bool pendingFocusVisible = false;

    // Lifecycle: structural rebuilds wait until dispatch unwinds (ui/AGENTS.md); live ones until
    // the next paint. rebuildMain: the next rebuild repaints the main window.
    int dispatchDepth = 0;
    bool rebuildPending = false, resetScroll = false, liveRebuild = false, rebuildMain = false;
    bool closing = false, faulted = false;
};

static LRESULT CALLBACK UiHostTextBoxProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR, DWORD_PTR data);

// The viewport's timer that scrolls while a handle is dragged near its top or bottom edge.
static constexpr UINT_PTR kUiAutoScrollTimer = 0x7E01;

static void UiHostRequestRebuild(UiHost& host, bool resetScroll) {
    host.rebuildPending = true;
    host.rebuildMain = true;
    host.resetScroll = host.resetScroll || resetScroll;
}

// A change at the pointer's rate (a slider's drag): the rebuild runs once per frame, before the
// next paint, and repaints the main window only if `main` changes (or its layout does).
static void UiHostRequestLiveRebuild(UiHost& host, bool main) {
    host.liveRebuild = true;
    host.rebuildMain = host.rebuildMain || main;
    if (main && host.main) InvalidateRect(host.main, nullptr, FALSE);
    if (host.viewport) InvalidateRect(host.viewport, nullptr, FALSE);
}

// Focuses an element after the next rebuild (one that the rebuild may create or enable).
static void UiHostRequestFocus(UiHost& host, UiFocusRef ref, bool visible) {
    host.pendingFocus = ref;
    host.pendingFocusVisible = visible;
}

static float UiHostScale(const UiHost& host) {
    return float(host.dpi) / 96.0f;
}

static float UiHostViewHeight(const UiHost& host) {
    return host.frame.viewport.bottom - host.frame.viewport.top;
}

static void UiHostInvalidate(const UiHost& host) {
    if (host.main) InvalidateRect(host.main, nullptr, FALSE);
    if (host.viewport) InvalidateRect(host.viewport, nullptr, FALSE);
}

// Fault close: nothing is saved. WM_CLOSE cancels tracked modals and destroys the window once
// they have unwound; until then nothing draws or acts.
static void UiHostFault(UiHost& host) {
    host.faulted = true;
    host.closing = true;
    UiDiscardSurface(host.mainSurface);
    UiDiscardSurface(host.viewSurface);
    if (host.main) PostMessageW(host.main, WM_CLOSE, 0, 0);
}

static void UiHostApplyTheme(UiHost& host) {
    host.theme = UiReadTheme(*host.themes);
    host.palette = UiPaletteFor(host.theme);
    UiApplyChrome(host.main, host.palette);
    if (host.config->icon) {
        UiSetWindowIcons(host.main, *host.graphics, host.palette, *host.config->icon, host.dpi, host.icons);
    }
    host.mainSurface.cache.Clear();
    host.viewSurface.cache.Clear();
    host.popup.surface.cache.Clear();
    UiHostInvalidate(host);
    for (const UiTextBox& box : host.boxes.boxes) {
        if (box.window) InvalidateRect(box.window, nullptr, TRUE);
    }
}

static UiScrollbar UiHostScrollbar(const UiHost& host) {
    const D2D1_RECT_F& v = host.frame.viewport;
    return UiScrollbarFor({v.right - v.left, v.bottom - v.top}, host.scroll,
                          host.frame.contentHeight, host.scrollbarHot || host.scrollDragging);
}

static UiScrollbar UiHostNavScrollbar(const UiHost& host) {
    return UiNavScrollbar(host.frame, host.navScroll, host.navBarHot || host.navBarDragging);
}

// The element under a client point (pixels) of a surface: on the main surface, inside the
// navigation pane only its items, at the pane's scroll.
static UiFocusRef UiHostHit(UiHost& host, UiSurfaceId surface, POINT point) {
    float scale = UiHostScale(host);
    D2D1_POINT_2F at{float(point.x) / scale, float(point.y) / scale};
    if (surface == UiSurfaceId::Main && UiContains(host.frame.navPane, at)) {
        surface = UiSurfaceId::Nav;
        at.y += host.navScroll;
    }
    if (surface == UiSurfaceId::Content) at.y += host.scroll;
    return UiHitTest(*host.graphics, UiSurfaceItems(host.frame, surface), at);
}

// Dropdown popup -------------------------------------------------------------------------------

static void UiHostClosePopup(UiHost& host) {
    if (!host.popup.open && !host.popup.window) return;
    UiPopupClose(host.popup);
    UiHostInvalidate(host);
}

// Opens a dropdown's popup under (or above) its field, or a menu button's under it, right-
// aligned: owned by the main window, shown without activation, at the DPI of its monitor,
// holding capture.
static void UiHostOpenPopup(UiHost& host, uint32_t id) {
    UiSurfaceId surface = UiSurfaceId::Main;
    const UiItem* item = UiFindItem(host.frame, id, &surface);
    if (!item || (item->kind != UiItemKind::Dropdown && item->kind != UiItemKind::Button) ||
        !item->enabled || item->options.empty() || !host.config->popupClass || host.closing) {
        return;
    }
    bool menu = item->kind == UiItemKind::Button;
    UiHostClosePopup(host);
    UiPopup& popup = host.popup;
    float scale = UiHostScale(host);
    D2D1_SIZE_F size = UiLayoutOptions(*host.graphics, item->options, item->action,
                                       item->rect.right - item->rect.left, popup.rows);
    float shift = surface == UiSurfaceId::Content ? -host.scroll : surface == UiSurfaceId::Nav ? -host.navScroll : 0;
    RECT field{LONG(std::lround(item->rect.left * scale)),
               LONG(std::lround((item->rect.top + shift) * scale)),
               LONG(std::lround(item->rect.right * scale)),
               LONG(std::lround((item->rect.bottom + shift) * scale))};
    MapWindowPoints(surface == UiSurfaceId::Content ? host.viewport : host.main, nullptr,
                    reinterpret_cast<POINT*>(&field), 2);
    popup.highlight = -1;
    for (size_t i = 0; i < popup.rows.size(); i++) {
        if (popup.rows[i].selected) popup.highlight = int(i);
    }

    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromRect(&field, MONITOR_DEFAULTTONEAREST), &monitor);
    auto place = [&](float popupScale) {
        SIZE pixels{LONG(std::lround(size.width * popupScale)), LONG(std::lround(size.height * popupScale))};
        POINT at = UiPopupPlacement(field, pixels, monitor.rcWork, int(std::lround(4 * scale)), menu);
        return RECT{at.x, at.y, at.x + pixels.cx, at.y + pixels.cy};
    };
    RECT rect = place(scale);
    HWND window = CreateWindowExW(kUiPopupExStyle, host.config->popupClass, nullptr, WS_POPUP,
                                  rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                                  host.main, nullptr, GetModuleHandleW(nullptr), &host);
    if (!window) {
        Wh_Log(L"Could not create the dropdown popup: %lu", GetLastError());
        popup.rows.clear();
        return;
    }
    popup.window = popup.surface.window = window;
    popup.dpi = WindowDpi(window);
    if (popup.dpi != host.dpi) {
        rect = place(float(popup.dpi) / 96);
        SetWindowPos(window, nullptr, rect.left, rect.top, rect.right - rect.left,
                     rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(window, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    popup.field = id;
    popup.open = true;
    SetWindowPos(window, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SetCapture(window);
    UiHostInvalidate(host);
}

// A chosen row: the popup closes, then the client gets the field's action with the row's value.
static void UiHostChoose(UiHost& host, int row) {
    if (row < 0 || row >= int(host.popup.rows.size())) return;
    UiAction action = host.popup.rows[size_t(row)].action;
    UiHostClosePopup(host);
    if (!host.closing) host.client->Invoke(host, std::move(action));
}

// An item's event, as a copy of its action: a number box's text box (text events, steps), a
// slider's drag and keys, a preview's pans.
static void UiHostItemEvent(UiHost& host, uint32_t id, UiActionKind kind, std::wstring text = {},
                            int value = 0) {
    const UiItem* item = UiFindItem(host.frame, id);
    if (host.closing || !item) return;
    UiAction action = item->action;
    action.kind = kind;
    action.text = std::move(text);
    action.value = value;
    host.client->Invoke(host, std::move(action));
}

// A segment chosen by the pointer or the keys: the control's action with that segment's value;
// the selected one changes nothing.
static void UiHostSelectSegment(UiHost& host, uint32_t id, int segment) {
    const UiItem* item = UiFindItem(host.frame, id);
    if (host.closing || !item || item->kind != UiItemKind::Segmented || !UiActive(*item) || segment < 0 ||
        segment >= int(item->options.size()) || segment == UiSelectedSegment(*item)) {
        return;
    }
    UiAction action = item->action;
    action.value = item->options[size_t(segment)].value;
    host.client->Invoke(host, std::move(action));
}

// Text boxes -----------------------------------------------------------------------------------

// Native text boxes sit over their items' text areas, vertically centered, scrolled with the
// content.
static void UiHostPlaceTextBoxes(UiHost& host) {
    float scale = UiHostScale(host);
    for (const UiTextBox& box : host.boxes.boxes) {
        const UiItem* item = UiFindItem(host.frame, box.id);
        if (!item || !box.window) continue;
        D2D1_RECT_F text = UiNumberBoxTextRect(*item);
        float center = (item->rect.top + item->rect.bottom) / 2 - host.scroll;
        int left = int(std::lround(text.left * scale)), right = int(std::lround(text.right * scale));
        int top = int(std::lround(center * scale)) - host.boxes.fontHeight / 2;
        SetWindowPos(box.window, nullptr, left, top, std::max(right - left, 1), host.boxes.fontHeight,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// After a rebuild: one native text box per number box or text box (kept by id), others
// destroyed; text set only when the client's text changed and no IME composition is running.
// A text box's limit is its item's maximum; its placeholder shows as the native cue banner.
static void UiHostSyncTextBoxes(UiHost& host) {
    UiTextBoxes& boxes = host.boxes;
    auto native = [](const UiItem* item) {
        return item && (item->kind == UiItemKind::NumberBox || item->kind == UiItemKind::TextBox);
    };
    // Removed first, so their focus-loss notifications find no box.
    std::vector<HWND> removed;
    std::erase_if(boxes.boxes, [&](const UiTextBox& box) {
        if (native(UiFindItem(host.frame, box.id)) && box.window) return false;
        if (box.window) removed.push_back(box.window);
        return true;
    });
    for (HWND window : removed) DestroyWindow(window);
    HFONT font = UiTextBoxFont(boxes, host.dpi);
    for (const UiItem& item : host.frame.content) {
        if (!native(&item)) continue;
        UiTextBox* box = UiFindTextBox(boxes, item.id);
        if (!box) {
            DWORD align = item.align == DWRITE_TEXT_ALIGNMENT_CENTER ? ES_CENTER : ES_LEFT;
            HWND window = CreateWindowExW(0, L"EDIT", item.text.c_str(),
                                          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | align, 0, 0, 0, 0,
                                          host.viewport, nullptr, GetModuleHandleW(nullptr), nullptr);
            if (!window) {
                Wh_Log(L"Could not create a text box: %lu", GetLastError());
                continue;
            }
            if (!SetWindowSubclass(window, UiHostTextBoxProc, 0, reinterpret_cast<DWORD_PTR>(&host))) {
                Wh_Log(L"Could not subclass a text box");
                DestroyWindow(window);
                continue;
            }
            boxes.boxes.push_back({item.id, window, item.text});
            box = &boxes.boxes.back();
            SendMessageW(window, WM_SETFONT, WPARAM(font), FALSE);
            SendMessageW(window, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
            SendMessageW(window, EM_LIMITTEXT, item.kind == UiItemKind::TextBox ? std::max(item.maximum, 1) : 64, 0);
        } else if (!box->composing && item.text != box->text) {
            UiSetTextBoxText(boxes, *box, item.text);
        }
        if (std::wstring cue = UiRunsText(item.runs); item.kind == UiItemKind::TextBox && cue != box->cue) {
            box->cue = cue;
            SendMessageW(box->window, EM_SETCUEBANNER, FALSE, reinterpret_cast<LPARAM>(box->cue.c_str()));
        }
        if (bool(IsWindowEnabled(box->window)) != item.enabled) EnableWindow(box->window, item.enabled);
    }
    UiHostPlaceTextBoxes(host);
}

// Focus and scrolling ----------------------------------------------------------------------------

// The window that holds keyboard focus for an element: a number or text box's native text box,
// else the element's surface.
static HWND UiHostFocusWindow(UiHost& host, UiFocusRef ref) {
    UiSurfaceId surface = UiSurfaceId::Main;
    const UiItem* item = UiFindItem(host.frame, ref.id, &surface);
    if (!item) return host.main;
    if (item->kind == UiItemKind::NumberBox || item->kind == UiItemKind::TextBox) {
        UiTextBox* box = UiFindTextBox(host.boxes, ref.id);
        if (box && box->window && IsWindowEnabled(box->window)) return box->window;
    }
    return surface == UiSurfaceId::Content ? host.viewport : host.main;
}

// Scrolls to a pixel-aligned offset and re-targets the pointer, whose element moved under it.
// Scrolling closes the popup and moves the text boxes.
static void UiHostScrollTo(UiHost& host, float offset) {
    float scale = UiHostScale(host);
    offset = UiClampScroll(offset, UiHostViewHeight(host), host.frame.contentHeight);
    offset = std::round(offset * scale) / scale;
    if (offset == host.scroll) return;
    UiHostClosePopup(host);
    host.scroll = offset;
    POINT cursor{};
    RECT client{};
    if (!host.pointer.pressed.id && !host.scrollbarHot && GetCursorPos(&cursor) &&
        ScreenToClient(host.viewport, &cursor) && GetClientRect(host.viewport, &client) &&
        PtInRect(&client, cursor)) {
        UiPointerMove(host.pointer, UiHostHit(host, UiSurfaceId::Content, cursor));
        host.pointerSurface = UiSurfaceId::Content;
        host.pointerAt = {float(cursor.x) / scale, float(cursor.y) / scale + offset};
    }
    UiHostPlaceTextBoxes(host);
    InvalidateRect(host.viewport, nullptr, FALSE);
}

// The navigation pane's scroll, as UiHostScrollTo: pixel-aligned, closing the popup and
// re-targeting a pointer over the main window.
static void UiHostScrollNav(UiHost& host, float offset) {
    float scale = UiHostScale(host);
    const D2D1_RECT_F& pane = host.frame.navPane;
    offset = UiClampScroll(offset, pane.bottom - pane.top, host.frame.navHeight);
    offset = std::round(offset * scale) / scale;
    if (offset == host.navScroll) return;
    UiHostClosePopup(host);
    host.navScroll = offset;
    POINT cursor{};
    RECT client{};
    if (!host.pointer.pressed.id && !host.navBarHot && GetCursorPos(&cursor) &&
        ScreenToClient(host.main, &cursor) && GetClientRect(host.main, &client) && PtInRect(&client, cursor)) {
        UiPointerMove(host.pointer, UiHostHit(host, UiSurfaceId::Main, cursor));
        host.pointerSurface = UiSurfaceId::Main;
        host.pointerAt = {float(cursor.x) / scale, float(cursor.y) / scale};
    }
    InvalidateRect(host.main, nullptr, FALSE);
}

// Moves focus to an element (keyboard focus to its window), scrolling content or the
// navigation pane into view when the move came from the keyboard. Focus leaving the popup's
// field closes the popup.
static void UiHostSetFocus(UiHost& host, UiFocusRef ref, bool visible) {
    UiSurfaceId surface = UiSurfaceId::Main;
    const UiItem* item = UiFindItem(host.frame, ref.id, &surface);
    if (!item) return;
    if (ref.id != host.popup.field) UiHostClosePopup(host);
    host.focus = ref;
    host.focusVisible = visible;
    if (surface == UiSurfaceId::Content && visible) {
        std::vector<D2D1_RECT_F> bounds = UiFocusBounds(*host.graphics, *item, ref.link);
        if (!bounds.empty()) {
            UiHostScrollTo(host, UiScrollIntoView(host.scroll, UiHostViewHeight(host),
                                                  bounds.front().top, bounds.back().bottom, 12));
        }
    }
    if (surface == UiSurfaceId::Nav && visible) {
        UiHostScrollNav(host, UiNavScrollTo(host.frame, host.navScroll, item->rect));
    }
    HWND target = UiHostFocusWindow(host, ref);
    if (GetFocus() != target) {
        SetFocus(target);
        // A text box reached with the keyboard selects its text.
        if (visible && target != host.main && target != host.viewport) {
            SendMessageW(target, EM_SETSEL, 0, -1);
        }
    }
    UiHostInvalidate(host);
}

// Activation. The action is copied first: the client may rebuild, pump messages or show a modal.
// A dropdown or menu button opens its popup; a number or text box only takes focus (its native
// text box reports edits); a slider, a preview and a drag handle report their drags instead; a
// segmented control selects the activated segment (its keys select the others).
static void UiHostInvoke(UiHost& host, UiFocusRef ref) {
    if (host.closing) return;
    const UiItem* item = UiFindItem(host.frame, ref.id);
    if (item && item->kind == UiItemKind::Segmented) {
        UiHostSelectSegment(host, ref.id, ref.link);
        return;
    }
    if (item && ref.link < 0 &&
        (item->kind == UiItemKind::Dropdown || (item->kind == UiItemKind::Button && !item->options.empty()))) {
        UiHostOpenPopup(host, ref.id);
        return;
    }
    if (item && ref.link < 0 &&
        (item->kind == UiItemKind::NumberBox || item->kind == UiItemKind::TextBox ||
         item->kind == UiItemKind::Slider || item->kind == UiItemKind::Preview ||
         item->kind == UiItemKind::Handle)) {
        return;
    }
    const UiAction* action = UiActionOf(host.frame, ref);
    if (!action) return;
    UiAction copy = *action;
    host.client->Invoke(host, std::move(copy));
}

// Drag handles ----------------------------------------------------------------------------------

// A handle drag with the pointer at content y: once it moved 4 DIP from the press it reports
// where the pointer is, and the viewport scrolls while it is near its top or bottom, a step every
// 30 ms (up to 20 DIP, faster further out). The timer, started on entering that zone, steps a
// still pointer; a moving one steps with its reports, since input and paints outrank WM_TIMER.
static void UiHostDragHandle(UiHost& host, float y) {
    if (!host.handleMoved && std::abs(y - host.dragAnchor) < 4) return;
    host.handleMoved = true;
    float inView = y - host.scroll, height = UiHostViewHeight(host);
    bool edge = inView < 24 || inView > height - 24;
    ULONGLONG now = GetTickCount64();
    if (edge != host.autoScrolling) {
        host.autoScrolling = edge;
        host.autoScrollAt = now;
        if (edge) SetTimer(host.viewport, kUiAutoScrollTimer, 30, nullptr);
        else KillTimer(host.viewport, kUiAutoScrollTimer);
    } else if (edge && now - host.autoScrollAt >= 30) {
        // Steps keep their speed however late they come (up to three at once).
        float steps = float(std::min<ULONGLONG>(now - host.autoScrollAt, 90)) / 30;
        host.autoScrollAt = now;
        float step = inView < 24 ? -std::min(24 - inView, 20.0f) : std::min(inView - height + 24, 20.0f);
        UiHostScrollTo(host, host.scroll + step * steps);
        y = inView + host.scroll;
    }
    UiHostItemEvent(host, host.handleDrag, UiActionKind::Drag, {}, int(std::lround(y)));
}

// A drag's live layout: `move` shifts built content items in place and returns whether any
// moved; only the viewport repaints, without a rebuild, and text boxes follow at its paint.
// Skipped while a rebuild is pending, which lays out from the client's state anyway.
template <class Move> static void UiHostMoveContent(UiHost& host, Move&& move) {
    if (host.rebuildPending || host.closing || !host.viewport || !move(host.frame.content)) return;
    host.boxes.moved = true;
    InvalidateRect(host.viewport, nullptr, FALSE);
}

// Ends a handle drag before its capture goes; one that moved reports `end`.
static void UiHostEndHandleDrag(UiHost& host, UiActionKind end) {
    uint32_t handle = std::exchange(host.handleDrag, 0);
    bool moved = std::exchange(host.handleMoved, false);
    host.autoScrolling = false;
    if (host.viewport) KillTimer(host.viewport, kUiAutoScrollTimer);
    if (handle && moved) UiHostItemEvent(host, handle, end);
}

// Deferred structural rebuild, once the outermost dispatch is done with the old items. A live
// one waits for the paint its request caused (which runs it), unless a nested paint (a modal's)
// consumed that. A rebuild closes the popup; keyboard focus follows the focused element (a
// requested one, or one whose text box was removed). The main window repaints when requested,
// when its layout changed or when the pointer's element went. A newly selected entry of the
// navigation pane scrolls into view.
static void UiHostRebuildIfPending(UiHost& host) {
    auto selectedNav = [](const std::vector<UiItem>& items) {
        auto at = std::find_if(items.begin(), items.end(), [](const UiItem& item) {
            return item.kind == UiItemKind::Nav && item.selected;
        });
        return at != items.end() ? at->id : 0;
    };
    if (host.liveRebuild && host.dispatchDepth == 1 && host.main && host.viewport &&
        !GetUpdateRect(host.main, nullptr, FALSE) && !GetUpdateRect(host.viewport, nullptr, FALSE)) {
        host.rebuildPending = true;
    }
    for (int pass = 0; pass < 4 && host.dispatchDepth == 1 && host.rebuildPending && host.main &&
                       host.viewport && !host.closing;
         pass++) {
        host.rebuildPending = false;
        host.liveRebuild = false;
        D2D1_RECT_F viewport = host.frame.viewport;
        size_t mainItems = host.frame.main.size() + host.frame.nav.size();
        uint32_t paneSelected = selectedNav(host.frame.nav);
        UiHostClosePopup(host);
        RECT client{};
        GetClientRect(host.main, &client);
        float scale = UiHostScale(host);
        UiTrimCaches(*host.graphics);
        UiFrame frame;
        host.client->Build(host, {float(client.right) / scale, float(client.bottom) / scale}, frame);
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        UiFocusRef fallback{selectedNav(frame.nav) ? selectedNav(frame.nav) : selectedNav(frame.main), -1};
        // Keyboard focus starts on the selected navigation item, without a ring.
        host.focus = UiResolveFocus(host.stops, stops, host.focus.id ? host.focus : fallback,
                                    fallback);
        host.frame = std::move(frame);
        host.stops = std::move(stops);
        const D2D1_RECT_F& v = host.frame.viewport;
        bool repaintMain = std::exchange(host.rebuildMain, false) ||
                           mainItems != host.frame.main.size() + host.frame.nav.size() ||
                           v.left != viewport.left || v.top != viewport.top || v.right != viewport.right ||
                           v.bottom != viewport.bottom;
        const D2D1_RECT_F& pane = host.frame.navPane;
        host.navScroll = std::round(UiClampScroll(host.navScroll, pane.bottom - pane.top, host.frame.navHeight) *
                                    scale) / scale;
        if (const UiItem* entry = UiFindItem(host.frame, selectedNav(host.frame.nav));
            entry && entry->id != paneSelected) {
            UiHostScrollNav(host, UiNavScrollTo(host.frame, host.navScroll, entry->rect));
        }
        for (UiFocusRef* ref : {&host.pointer.hot, &host.pointer.pressed}) {
            if (ref->id && !UiFindItem(host.frame, ref->id)) {
                *ref = {};
                repaintMain = true;
            }
        }
        host.scroll = host.resetScroll ? 0 : host.scroll;
        host.resetScroll = false;
        host.scroll = std::round(UiClampScroll(host.scroll, UiHostViewHeight(host),
                                               host.frame.contentHeight) * scale) / scale;
        int left = int(std::lround(v.left * scale)), top = int(std::lround(v.top * scale));
        SetWindowPos(host.viewport, nullptr, left, top, int(std::lround(v.right * scale)) - left,
                     int(std::lround(v.bottom * scale)) - top, SWP_NOZORDER | SWP_NOACTIVATE);
        UiHostSyncTextBoxes(host);
        if (repaintMain) InvalidateRect(host.main, nullptr, FALSE);
        InvalidateRect(host.viewport, nullptr, FALSE);

        UiFocusRef pending = std::exchange(host.pendingFocus, {});
        bool active = GetActiveWindow() == host.main;
        if (pending.id && UiFindItem(host.frame, pending.id)) {
            if (active) UiHostSetFocus(host, pending, host.pendingFocusVisible);
            else host.focus = pending;
        } else if (active) {
            HWND focus = GetFocus();
            if ((!focus || focus == host.main || focus == host.viewport) &&
                focus != UiHostFocusWindow(host, host.focus)) {
                UiHostSetFocus(host, host.focus, host.focusVisible);
            }
        }
    }
}

// Window procedures ------------------------------------------------------------------------------

static LRESULT UiHostProc(UiHost& host, HWND window, UiSurfaceId surface, UINT message,
                          WPARAM wParam, LPARAM lParam) {
    const bool isMain = surface == UiSurfaceId::Main;
    UiSurface& own = isMain ? host.mainSurface : host.viewSurface;
    host.dispatchDepth++;
    LRESULT result = 0;
    bool handled = true;
    try {
        switch (message) {
            // Creation and teardown
            case WM_CREATE:
                if (!isMain) {
                    own.window = window;
                    break;
                }
                host.main = own.window = window;
                host.dpi = WindowDpi(window);
                host.viewport = CreateWindowExW(0, host.config->viewportClass, nullptr,
                                                WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 0, 0,
                                                window, nullptr, GetModuleHandleW(nullptr), &host);
                if (!host.viewport) {
                    Wh_Log(L"Could not create the viewport: %lu", GetLastError());
                    result = -1;
                    break;
                }
                UiHostApplyTheme(host);
                if (!host.client->Attach(host)) {
                    result = -1;
                    break;
                }
                UiHostRequestRebuild(host, true);
                break;
            case WM_CLOSE:
                if (!isMain) break;
                // A tracked modal ends first; the close is retried once its call has unwound.
                if (host.client->CancelModal()) {
                    PostMessageW(window, WM_CLOSE, 0, 0);
                    break;
                }
                // So does a handle drag, cancelled with its capture: the close is retried once
                // the cancel's rebuild has put the content back, whatever the close then decides.
                if (host.handleDrag) {
                    UiHostEndHandleDrag(host, UiActionKind::DragCancel);
                    if (HWND capture = GetCapture(); capture == host.main || capture == host.viewport) {
                        ReleaseCapture();
                    }
                    PostMessageW(window, WM_CLOSE, 0, 0);
                    break;
                }
                UiHostClosePopup(host);
                if (!host.faulted && !host.client->Unloading() && !host.client->PrepareClose(host)) {
                    break;
                }
                host.closing = true;
                DestroyWindow(window);
                break;
            case WM_DESTROY:
                if (isMain) {
                    host.closing = true;
                    UiPopupClose(host.popup);
                    if (HWND capture = GetCapture(); capture == host.main || capture == host.viewport) {
                        ReleaseCapture();
                    }
                    host.client->Destroying();
                }
                break;
            case WM_NCDESTROY:
                // Children (viewport, text boxes) and the owned popup are gone before the main
                // window's NCDESTROY; each surface releases its target with its window. Host
                // state outlives them all.
                UiDiscardSurface(own);
                own.window = nullptr;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                if (isMain) {
                    // Quit first: the loop must end even if the client's teardown throws.
                    host.main = nullptr;
                    PostQuitMessage(0);
                    UiReleaseTextBoxes(host.boxes);
                    host.client->Destroyed();
                } else {
                    host.viewport = nullptr;
                }
                handled = false;
                break;

            // Size, position, DPI, theme. Each closes the popup.
            case WM_SIZE:
                UiResizeSurface(own);
                if (isMain) UiHostClosePopup(host);
                if (isMain && wParam != SIZE_MINIMIZED) UiHostRequestRebuild(host, false);
                break;
            case WM_MOVE:
            case WM_ENTERSIZEMOVE:
                UiHostClosePopup(host);
                handled = false;
                break;
            case WM_SHOWWINDOW:
                if (!wParam) UiHostClosePopup(host);
                handled = false;
                break;
            case WM_GETMINMAXINFO:
                if (isMain) {
                    auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                    info->ptMinTrackSize = {LONG(host.config->minSize.width * UiHostScale(host)),
                                            LONG(host.config->minSize.height * UiHostScale(host))};
                }
                break;
            case WM_DPICHANGED: {
                UiHostClosePopup(host);
                host.dpi = HIWORD(wParam);
                UiDiscardSurface(host.mainSurface);
                UiDiscardSurface(host.viewSurface);
                if (host.config->icon) {
                    UiSetWindowIcons(window, *host.graphics, host.palette, *host.config->icon, host.dpi, host.icons);
                }
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                UiHostRequestRebuild(host, false);
                break;
            }
            // A theme change also rebuilds: built content may hold colors the client derived
            // from the theme.
            case WM_SETTINGCHANGE:
                if (isMain && (wParam == SPI_SETHIGHCONTRAST ||
                               (lParam && wcscmp(reinterpret_cast<PCWSTR>(lParam),
                                                 L"ImmersiveColorSet") == 0))) {
                    UiHostApplyTheme(host);
                    UiHostRequestRebuild(host, false);
                }
                handled = false;
                break;
            case WM_THEMECHANGED:
            case WM_SYSCOLORCHANGE:
            case WM_DWMCOLORIZATIONCOLORCHANGED:
                if (isMain) {
                    UiHostApplyTheme(host);
                    UiHostRequestRebuild(host, false);
                }
                handled = false;
                break;

            // Activation and modes: the popup closes before default handling (a system command
            // such as minimize is swallowed while the popup holds capture).
            case WM_ACTIVATE:
                if (LOWORD(wParam) == WA_INACTIVE) UiHostClosePopup(host);
                handled = false;
                break;
            case WM_CANCELMODE:
            case WM_SYSCOMMAND:
                UiHostClosePopup(host);
                // Cancellation ends a drag on either surface (its capture loss reports it).
                if (HWND capture = GetCapture();
                    message == WM_CANCELMODE && (capture == host.main || capture == host.viewport)) {
                    ReleaseCapture();
                }
                handled = false;
                break;
            case WM_SYSKEYDOWN: {
                const UiItem* focused = UiFindItem(host.frame, host.focus.id);
                if (host.popup.open) {
                    UiHostClosePopup(host);
                } else if (wParam == VK_DOWN && !host.closing && focused &&
                           focused->kind == UiItemKind::Dropdown) {
                    UiHostOpenPopup(host, host.focus.id);  // Alt+Down
                    break;
                } else if ((wParam == VK_UP || wParam == VK_DOWN) && !host.closing && focused &&
                           !host.handleDrag) {
                    // Alt+Up/Down moves an element with a drag handle by one.
                    auto handle = std::find_if(host.frame.content.begin(), host.frame.content.end(),
                                               [&](const UiItem& item) {
                                                   return item.kind == UiItemKind::Handle &&
                                                          item.owner == host.focus.id && UiActive(item);
                                               });
                    if (handle != host.frame.content.end()) {
                        host.focusVisible = true;
                        UiHostItemEvent(host, handle->id, UiActionKind::Step, {}, wParam == VK_UP ? -1 : 1);
                        break;
                    }
                }
                handled = false;
                break;
            }

            // Paint
            case WM_ERASEBKGND:
                result = 1;
                break;
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC: {
                UiTextBox* box = UiFindTextBox(host.boxes, reinterpret_cast<HWND>(lParam));
                if (!box) {
                    handled = false;
                    break;
                }
                auto [text, fill] = UiTextBoxColors(host.palette, GetFocus() == box->window,
                                                    IsWindowEnabled(box->window) != FALSE);
                SetTextColor(reinterpret_cast<HDC>(wParam), text);
                SetBkColor(reinterpret_cast<HDC>(wParam), fill);
                result = reinterpret_cast<LRESULT>(UiTextBoxBrush(host.boxes, fill));
                break;
            }
            case WM_PAINT: {
                // Once per frame, before either surface draws: a live rebuild, and text boxes
                // whose items a drag moved (a native window move costs a sizeable part of a
                // frame).
                if (host.liveRebuild && host.dispatchDepth == 1 && !host.closing) {
                    host.rebuildPending = true;
                    UiHostRebuildIfPending(host);
                }
                if (!isMain && !host.closing && std::exchange(host.boxes.moved, false)) {
                    UiHostPlaceTextBoxes(host);
                }
                {
                    PAINTSTRUCT paint{};
                    BeginPaint(window, &paint);
                    struct PaintEnd {
                        HWND window;
                        PAINTSTRUCT* paint;
                        ~PaintEnd() { EndPaint(window, paint); }
                    } paintEnd{window, &paint};
                    if (host.faulted) break;
                    ID2D1HwndRenderTarget* target =
                        UiSurfaceTarget(own, host.graphics->d2d.get(), host.dpi);
                    if (!target) {
                        UiHostFault(host);
                        break;
                    }
                    float scale = UiHostScale(host);
                    UiTextBox* editing = UiFindTextBox(host.boxes, GetFocus());
                    UiPaintState state{host.pointer.hot, host.pointer.pressed, host.focus,
                                       host.focusVisible && GetFocus() == window,
                                       host.popup.open ? host.popup.field : 0,
                                       editing ? editing->id : 0};
                    if (host.pointerSurface == surface) state.pointer = host.pointerAt;
                    UiCustomPaint custom = [&](const UiItem& item) {
                        host.client->PaintCustom(host, target, own.cache, item, state);
                    };
                    target->BeginDraw();
                    D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
                    target->SetTransform(&identity);
                    target->Clear(host.palette.background);
                    if (isMain) {
                        UiPaintItems(target, *host.graphics, own.cache, host.palette, host.frame.main,
                                     state, {0, 0}, scale, -1e9f, 1e9f, &custom);
                        UiPaintNavPane(target, *host.graphics, own.cache, host.palette, host.frame, state,
                                       host.navScroll, host.navBarHot || host.navBarDragging, scale);
                    } else {
                        float height = UiHostViewHeight(host);
                        UiPaintItems(target, *host.graphics, own.cache, host.palette, host.frame.content,
                                     state, {0, -host.scroll}, scale, host.scroll, host.scroll + height,
                                     &custom);
                        UiPaintScrollbar(target, own.cache, host.palette, UiHostScrollbar(host),
                                         host.scrollbarHot || host.scrollDragging, scale);
                    }
                    if (!UiSurfaceDrawResult(own, target->EndDraw())) UiHostFault(host);
                }
                // The viewport paints in the main window's cycle: pointer input arriving during
                // the main window's present would otherwise have it painted first again and again.
                if (isMain && host.viewport && !host.closing) UpdateWindow(host.viewport);
                break;
            }

            // Pointer
            case WM_MOUSEMOVE: {
                POINT point{short(LOWORD(lParam)), short(HIWORD(lParam))};
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                TrackMouseEvent(&track);
                float scale = UiHostScale(host);
                float x = float(point.x) / scale;
                host.pointerSurface = surface;
                host.pointerAt = {x, float(point.y) / scale + (isMain ? 0 : host.scroll)};
                // A slider drag reports each new value; a preview drag pans by whole DIPs.
                if (host.sliderDrag) {
                    const UiItem* slider = UiFindItem(host.frame, host.sliderDrag);
                    int value = slider ? UiSliderValueAt(*slider, x - host.dragAnchor) : 0;
                    if (slider && value != host.slideValue) {
                        host.slideValue = value;
                        UiHostItemEvent(host, host.sliderDrag, UiActionKind::Slide, {}, value);
                    }
                    break;
                }
                if (host.panDrag) {
                    int moved = int(x - host.dragAnchor);
                    if (moved) {
                        host.dragAnchor += float(moved);
                        UiHostItemEvent(host, host.panDrag, UiActionKind::Pan, {}, moved);
                    }
                    break;
                }
                if (host.handleDrag) {
                    UiHostDragHandle(host, host.pointerAt.y);
                    break;
                }
                if (!isMain && host.scrollDragging) {
                    UiHostScrollTo(host, UiScrollForThumb(UiHostScrollbar(host),
                                                          float(point.y) / scale - host.scrollDragOffset,
                                                          UiHostViewHeight(host),
                                                          host.frame.contentHeight));
                    break;
                }
                const D2D1_RECT_F& pane = host.frame.navPane;
                if (isMain && host.navBarDragging) {
                    UiHostScrollNav(host, UiScrollForThumb(UiHostNavScrollbar(host),
                                                           float(point.y) / scale - host.scrollDragOffset,
                                                           pane.bottom - pane.top, host.frame.navHeight));
                    break;
                }
                // Each scrollbar takes the pointer within 16 DIP of its pane's right edge.
                bool overBar = !isMain && UiHostScrollbar(host).visible &&
                               float(point.x) / scale >= host.frame.viewport.right -
                                                             host.frame.viewport.left - 16;
                bool overNavBar = isMain && UiHostNavScrollbar(host).visible &&
                                  UiContains({pane.right - 16, pane.top, pane.right, pane.bottom}, host.pointerAt);
                bool changed = overBar != host.scrollbarHot || overNavBar != host.navBarHot;
                host.scrollbarHot = overBar;
                host.navBarHot = overNavBar;
                changed |= UiPointerMove(host.pointer, overBar || overNavBar ? UiFocusRef{} :
                                                                               UiHostHit(host, surface, point));
                // A preview's hover follows the pointer within it.
                const UiItem* hot = UiFindItem(host.frame, host.pointer.hot.id);
                if (changed) UiHostInvalidate(host);
                else if (hot && hot->kind == UiItemKind::Preview) InvalidateRect(window, nullptr, FALSE);
                break;
            }
            case WM_MOUSELEAVE:
                // While a press or drag holds capture, only this surface's pointer position
                // changes; outside the window every move reports a leave, so nothing else repaints.
                if (host.pointerSurface == surface) host.pointerAt = {-1e9f, -1e9f};
                if (host.pointer.pressed.id || host.scrollDragging || host.navBarDragging) {
                    InvalidateRect(window, nullptr, FALSE);
                    break;
                }
                host.pointer.hot = {};
                host.scrollbarHot = host.navBarHot = false;
                UiHostInvalidate(host);
                break;
            case WM_LBUTTONDOWN: {
                if (host.closing) break;
                POINT point{short(LOWORD(lParam)), short(HIWORD(lParam))};
                float scale = UiHostScale(host);
                UiScrollbar bar = UiHostScrollbar(host);
                if (!isMain && host.scrollbarHot && bar.visible) {
                    float y = float(point.y) / scale;
                    if (y >= bar.thumb.top && y < bar.thumb.bottom) {
                        host.scrollDragging = true;
                        host.scrollDragOffset = y - bar.thumb.top;
                        SetCapture(window);
                    } else {
                        float page = UiHostViewHeight(host) - 40;
                        UiHostScrollTo(host, host.scroll + (y < bar.thumb.top ? -page : page));
                    }
                    InvalidateRect(window, nullptr, FALSE);
                    break;
                }
                if (UiScrollbar navBar = UiHostNavScrollbar(host); isMain && host.navBarHot && navBar.visible) {
                    float y = float(point.y) / scale;
                    if (y >= navBar.thumb.top && y < navBar.thumb.bottom) {
                        host.navBarDragging = true;
                        host.scrollDragOffset = y - navBar.thumb.top;
                        SetCapture(window);
                    } else {
                        float page = host.frame.navPane.bottom - host.frame.navPane.top - 40;
                        UiHostScrollNav(host, host.navScroll + (y < navBar.thumb.top ? -page : page));
                    }
                    InvalidateRect(window, nullptr, FALSE);
                    break;
                }
                UiFocusRef hit = UiHostHit(host, surface, point);
                host.pointer.hot = hit;
                host.pointer.pressed = hit;
                if (hit.id) {
                    SetCapture(window);
                    // A spin button keeps focus on its number box's text box and a drag handle
                    // gives it to its owner; a preview takes no focus; a segmented control takes
                    // it as a whole.
                    const UiItem* item = UiFindItem(host.frame, hit.id);
                    UiItemKind kind = item ? item->kind : UiItemKind::Text;
                    UiFocusRef target = kind == UiItemKind::SpinButton || kind == UiItemKind::Handle ?
                                            UiFocusRef{item->owner, -1} :
                                        kind == UiItemKind::Segmented ? UiFocusRef{hit.id, -1} : hit;
                    if (kind != UiItemKind::Preview && UiFindItem(host.frame, target.id) &&
                        (target != host.focus || GetFocus() != UiHostFocusWindow(host, target))) {
                        UiHostSetFocus(host, target, false);
                    }
                    // Drags: a slider jumps to the press unless it grabs the thumb; a preview
                    // pans with the pointer; a handle drags once the pointer moved.
                    float x = float(point.x) / scale;
                    const UiItem* slider = UiFindItem(host.frame, hit.id);
                    if (slider && slider->kind == UiItemKind::Slider) {
                        float offset = x - UiSliderThumbX(*slider);
                        host.sliderDrag = hit.id;
                        host.dragAnchor = std::abs(offset) <= 10 ? offset : 0;
                        host.slideValue = slider->action.value;
                        int value = UiSliderValueAt(*slider, x - host.dragAnchor);
                        if (value != host.slideValue) {
                            host.slideValue = value;
                            UiHostItemEvent(host, hit.id, UiActionKind::Slide, {}, value);
                        }
                    } else if (kind == UiItemKind::Preview) {
                        host.panDrag = hit.id;
                        host.dragAnchor = x;
                    } else if (kind == UiItemKind::Handle && GetCapture() == window) {
                        host.handleDrag = hit.id;
                        host.handleMoved = host.autoScrolling = false;
                        host.dragAnchor = float(point.y) / scale + (isMain ? 0 : host.scroll);
                    }
                } else {
                    host.focusVisible = false;
                    if (GetFocus() != window) SetFocus(window);
                }
                UiHostInvalidate(host);
                break;
            }
            case WM_LBUTTONUP: {
                // A slider drag ends wherever the pointer is: its value commits. A handle drag
                // that moved drops there.
                POINT point{short(LOWORD(lParam)), short(HIWORD(lParam))};
                uint32_t slider = std::exchange(host.sliderDrag, 0);
                uint32_t handle = std::exchange(host.handleDrag, 0);
                bool dropped = std::exchange(host.handleMoved, false);
                host.panDrag = 0;
                if (handle) {
                    host.autoScrolling = false;
                    KillTimer(host.viewport, kUiAutoScrollTimer);
                }
                UiFocusRef activate = UiPointerRelease(host.pointer, UiHostHit(host, surface, point));
                host.scrollDragging = host.navBarDragging = false;
                if (GetCapture() == window) ReleaseCapture();
                UiHostInvalidate(host);
                if (slider) UiHostItemEvent(host, slider, UiActionKind::SlideEnd);
                else if (handle && dropped) UiHostItemEvent(host, handle, UiActionKind::Drop);
                else if (activate.id) UiHostInvoke(host, activate);
                break;
            }
            case WM_CAPTURECHANGED:
                // Losing capture ends a slider drag as a release does, and cancels a handle's.
                host.pointer.pressed = {};
                host.scrollDragging = host.navBarDragging = false;
                host.panDrag = 0;
                if (uint32_t slider = std::exchange(host.sliderDrag, 0)) {
                    UiHostItemEvent(host, slider, UiActionKind::SlideEnd);
                }
                UiHostEndHandleDrag(host, UiActionKind::DragCancel);
                UiHostInvalidate(host);
                break;
            case WM_TIMER:
                // A handle held near the viewport's top or bottom: its drag steps the scroll
                // (UiHostDragHandle) with the cursor where it is. Without a cursor position only
                // the drag's reports step it.
                if (!isMain && wParam == kUiAutoScrollTimer) {
                    POINT cursor{};
                    if (!host.handleDrag || !GetCursorPos(&cursor) || !ScreenToClient(window, &cursor)) {
                        KillTimer(window, kUiAutoScrollTimer);
                        break;
                    }
                    UiHostDragHandle(host, float(cursor.y) / UiHostScale(host) + host.scroll);
                    break;
                }
                handled = isMain && host.client->Message(host, message, wParam, lParam, result);
                break;
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL: {
                // With the popup open, a wheel over it does nothing and one elsewhere closes it.
                // Otherwise a sideways wheel (or Shift) over a preview pans it, 48 DIPs a notch,
                // and the vertical wheel scrolls the navigation pane over it, or the content
                // over the page column (header or content).
                POINT point{short(LOWORD(lParam)), short(HIWORD(lParam))};
                if (host.popup.open) {
                    RECT popup{};
                    GetWindowRect(host.popup.window, &popup);
                    if (!PtInRect(&popup, point)) UiHostClosePopup(host);
                    break;
                }
                float notches = float(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
                ScreenToClient(host.main, &point);
                float scale = UiHostScale(host);
                if (message == WM_MOUSEHWHEEL || GetKeyState(VK_SHIFT) < 0) {
                    const D2D1_RECT_F& v = host.frame.viewport;
                    bool content = UiContains(v, {float(point.x) / scale, float(point.y) / scale});
                    POINT at = content ? POINT{point.x - LONG(std::lround(v.left * scale)),
                                               point.y - LONG(std::lround(v.top * scale))} : point;
                    UiFocusRef hit = UiHostHit(host, content ? UiSurfaceId::Content : UiSurfaceId::Main, at);
                    const UiItem* item = UiFindItem(host.frame, hit.id);
                    if (item && item->kind == UiItemKind::Preview) {
                        int moved = int(std::lround(notches * 48)) * (message == WM_MOUSEHWHEEL ? -1 : 1);
                        UiHostItemEvent(host, hit.id, UiActionKind::Pan, {}, moved);
                        break;
                    }
                }
                if (message == WM_MOUSEHWHEEL) {
                    handled = false;
                    break;
                }
                const D2D1_RECT_F& pane = host.frame.navPane;
                bool nav = UiContains(pane, {float(point.x) / scale, float(point.y) / scale});
                if (!nav && float(point.x) / scale < host.frame.viewport.left) break;
                UINT lines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                float page = nav ? pane.bottom - pane.top - 40 : UiHostViewHeight(host) - 40;
                float step = lines == WHEEL_PAGESCROLL ? page : float(lines) * 16;
                if (nav) UiHostScrollNav(host, host.navScroll - notches * step);
                else UiHostScrollTo(host, host.scroll - notches * step);
                break;
            }
            case WM_SETCURSOR: {
                if (LOWORD(lParam) != HTCLIENT || reinterpret_cast<HWND>(wParam) != window) {
                    handled = false;
                    break;
                }
                const UiItem* item = UiFindItem(host.frame, host.pointer.hot.id);
                bool link = item && ((host.pointer.hot.link >= 0 && item->kind == UiItemKind::Text) ||
                                     item->kind == UiItemKind::Link);
                bool handle = item && item->kind == UiItemKind::Handle;
                SetCursor(LoadCursorW(nullptr, link ? IDC_HAND : handle ? IDC_SIZENS : IDC_ARROW));
                result = TRUE;
                break;
            }

            // Keyboard and focus. While the popup is open, the field's keys drive it.
            case WM_KEYDOWN: {
                if (host.closing) break;
                // Esc cancels a handle drag; other keys wait until it ends.
                if (host.handleDrag) {
                    if (wParam == VK_ESCAPE) {
                        UiHostEndHandleDrag(host, UiActionKind::DragCancel);
                        if (HWND capture = GetCapture(); capture == host.main || capture == host.viewport) {
                            ReleaseCapture();
                        }
                    }
                    break;
                }
                if (host.popup.open) {
                    UiOptionIntent option = UiOptionKey(int(host.popup.rows.size()),
                                                        host.popup.highlight, UINT(wParam));
                    switch (option.kind) {
                        case UiOptionIntentKind::Highlight:
                            host.popup.highlight = option.index;
                            InvalidateRect(host.popup.window, nullptr, FALSE);
                            break;
                        case UiOptionIntentKind::Choose:
                            UiHostChoose(host, option.index);
                            break;
                        case UiOptionIntentKind::Close:
                        case UiOptionIntentKind::CloseAndPass:
                            UiHostClosePopup(host);
                            break;
                        case UiOptionIntentKind::None:
                            break;
                    }
                    if (option.kind != UiOptionIntentKind::CloseAndPass) break;
                }
                const UiItem* focused = UiFindItem(host.frame, host.focus.id);
                if (wParam == VK_F4 && focused && focused->kind == UiItemKind::Dropdown) {
                    UiHostOpenPopup(host, host.focus.id);
                    break;
                }
                // A focused slider's arrows, Page Up/Down and Home/End step its value; a focused
                // segmented control's arrows and Home/End select.
                if (focused && focused->kind == UiItemKind::Slider && focused->enabled) {
                    if (std::optional<int> step = UiSliderKey(*focused, UINT(wParam))) {
                        host.focusVisible = true;
                        UiHostItemEvent(host, host.focus.id, UiActionKind::Step, {}, *step);
                        break;
                    }
                }
                if (focused && focused->kind == UiItemKind::Segmented && focused->enabled) {
                    if (std::optional<int> segment = UiSegmentKey(*focused, UINT(wParam))) {
                        host.focusVisible = true;
                        InvalidateRect(window, nullptr, FALSE);
                        UiHostSelectSegment(host, host.focus.id, *segment);
                        break;
                    }
                }
                UiIntent intent = UiKeyIntent(host.stops, host.focus, surface, UINT(wParam),
                                              GetKeyState(VK_SHIFT) < 0, 40,
                                              std::max(UiHostViewHeight(host) - 40, 40.0f));
                switch (intent.kind) {
                    case UiIntentKind::Focus:
                        UiHostSetFocus(host, intent.target, true);
                        break;
                    case UiIntentKind::Invoke:
                        host.focusVisible = true;
                        InvalidateRect(window, nullptr, FALSE);
                        if (!(lParam & 0x40000000)) UiHostInvoke(host, intent.target);  // Not on auto-repeat.
                        break;
                    case UiIntentKind::ScrollBy:
                        UiHostScrollTo(host, host.scroll + intent.scroll);
                        break;
                    case UiIntentKind::ScrollTo:
                        UiHostScrollTo(host, intent.scroll);
                        break;
                    case UiIntentKind::None:
                        // Keys the window doesn't use (F5, Esc, shortcuts) are the client's.
                        handled = host.client->Message(host, message, wParam, lParam, result);
                        break;
                }
                break;
            }
            case WM_SETFOCUS: {
                // Activation focuses the main window; keyboard focus belongs to the focused
                // element's window.
                if (isMain && host.viewport) {
                    HWND target = UiHostFocusWindow(host, host.focus);
                    if (target != window) SetFocus(target);
                }
                InvalidateRect(window, nullptr, FALSE);
                break;
            }
            case WM_KILLFOCUS:
                UiHostClosePopup(host);
                InvalidateRect(window, nullptr, FALSE);
                break;

            // Text box notifications (viewport)
            case WM_COMMAND: {
                UiTextBox* box = lParam ? UiFindTextBox(host.boxes, reinterpret_cast<HWND>(lParam)) :
                                          nullptr;
                if (!box) {
                    handled = false;
                    break;
                }
                uint32_t id = box->id;
                switch (HIWORD(wParam)) {
                    case EN_CHANGE: {
                        if (host.boxes.programmatic || box->composing) break;
                        std::wstring text = UiTextBoxText(box->window);
                        if (text == box->text) break;
                        box->text = text;
                        UiHostItemEvent(host, id, UiActionKind::TextChange, std::move(text));
                        break;
                    }
                    case EN_SETFOCUS:
                        if (id != host.popup.field) UiHostClosePopup(host);
                        host.focus = {id, -1};
                        InvalidateRect(box->window, nullptr, TRUE);
                        UiHostInvalidate(host);
                        break;
                    case EN_KILLFOCUS:
                        InvalidateRect(box->window, nullptr, TRUE);
                        UiHostInvalidate(host);
                        if (!host.boxes.leavingWindow) {
                            UiHostItemEvent(host, id, UiActionKind::TextBlur, box->text);
                        }
                        break;
                }
                break;
            }

            default:
                handled = false;
                if (!isMain) break;
                if (host.config->activateMessage && message == host.config->activateMessage) {
                    // Brings back the window, or the enabled visible modal it owns; never
                    // anything else it owns (the popup closes first). Then the client sees the
                    // request (a selection it carries).
                    handled = true;
                    if (host.closing || host.client->Unloading()) break;
                    UiHostClosePopup(host);
                    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
                    HWND target = GetLastActivePopup(window);
                    if (target != window && !(IsWindowEnabled(target) && IsWindowVisible(target))) {
                        target = window;
                    }
                    SetForegroundWindow(target);
                    if (!host.closing) host.client->Message(host, message, wParam, lParam, result);
                } else {
                    handled = host.client->Message(host, message, wParam, lParam, result);
                }
                break;
        }
        UiHostRebuildIfPending(host);
    } catch (...) {
        Wh_Log(L"Window callback failed (message 0x%04X); closing the window", message);
        handled = true;
        result = message == WM_CREATE ? -1 : 0;
        UiHostFault(host);
    }
    host.dispatchDepth--;
    return handled ? result : DefWindowProcW(window, message, wParam, lParam);
}

// The popup: hover and press select rows; an outside press closes it and swallows its release;
// losing capture, WM_CANCELMODE or a wheel outside it close it. Never activates.
static LRESULT CALLBACK UiHostPopupProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* found = reinterpret_cast<UiHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!found) return DefWindowProcW(window, message, wParam, lParam);
    UiHost& host = *found;
    UiPopup& popup = host.popup;
    host.dispatchDepth++;
    LRESULT result = 0;
    bool handled = true;
    try {
        POINT point{short(LOWORD(lParam)), short(HIWORD(lParam))};
        switch (message) {
            case WM_MOUSEACTIVATE:
                result = MA_NOACTIVATE;
                break;
            case WM_ERASEBKGND:
                result = 1;
                break;
            case WM_PAINT: {
                PAINTSTRUCT paint{};
                BeginPaint(window, &paint);
                bool painted = host.faulted || popup.window != window ||
                               UiPaintPopup(popup, *host.graphics, host.palette);
                EndPaint(window, &paint);
                if (!painted) UiHostClosePopup(host);
                break;
            }
            case WM_MOUSEMOVE:
                if (popup.open) {
                    int row = UiPopupRowAt(popup, point);
                    if (row >= 0 && row != popup.highlight) {
                        popup.highlight = row;
                        InvalidateRect(window, nullptr, FALSE);
                    }
                }
                break;
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN: {
                if (!popup.open) break;
                RECT client{};
                GetClientRect(window, &client);
                if (PtInRect(&client, point)) {
                    if (message == WM_LBUTTONDOWN) popup.pressed = UiPopupRowAt(popup, point);
                    InvalidateRect(window, nullptr, FALSE);
                    break;
                }
                popup.open = false;
                popup.swallowUp = message + 1;  // The matching WM_*BUTTONUP
                ShowWindow(window, SW_HIDE);
                UiHostInvalidate(host);
                break;
            }
            case WM_LBUTTONUP:
            case WM_RBUTTONUP:
            case WM_MBUTTONUP:
                if (popup.swallowUp) {
                    if (message == popup.swallowUp) UiHostClosePopup(host);
                } else if (message == WM_LBUTTONUP && popup.open) {
                    int pressed = std::exchange(popup.pressed, -1);
                    int row = UiPopupRowAt(popup, point);
                    if (row >= 0 && row == pressed) UiHostChoose(host, row);
                    else InvalidateRect(window, nullptr, FALSE);
                }
                break;
            case WM_CAPTURECHANGED:
            case WM_CANCELMODE:
                if (popup.window == window) UiHostClosePopup(host);
                break;
            case WM_MOUSEWHEEL:
            case WM_MOUSEHWHEEL: {
                RECT rect{};
                GetWindowRect(window, &rect);
                if (!popup.open || !PtInRect(&rect, point)) UiHostClosePopup(host);
                break;
            }
            case WM_SETCURSOR:
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                result = TRUE;
                break;
            case WM_NCDESTROY:
                UiDiscardSurface(popup.surface);
                popup.surface.window = nullptr;
                if (popup.window == window) {
                    popup.window = nullptr;
                    popup.open = false;
                }
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                handled = false;
                break;
            default:
                handled = false;
                break;
        }
        UiHostRebuildIfPending(host);
    } catch (...) {
        Wh_Log(L"Popup callback failed (message 0x%04X); closing the window", message);
        handled = true;
        result = 0;
        UiHostFault(host);
    }
    host.dispatchDepth--;
    return handled ? result : DefWindowProcW(window, message, wParam, lParam);
}

// A number or text box's native text box: Tab moves focus along the item order, Enter commits,
// Esc reverts, Up/Down step a number box; focus moving elsewhere in the window reports focus
// loss. During IME composition every message goes to native handling and the composed text is
// reported when it ends.
static LRESULT CALLBACK UiHostTextBoxProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR, DWORD_PTR data) {
    UiHost& host = *reinterpret_cast<UiHost*>(data);
    host.dispatchDepth++;
    LRESULT result = 0;
    bool handled = false;
    try {
        UiTextBox* box = UiFindTextBox(host.boxes, window);
        bool commands = box && !box->composing && !host.closing;
        switch (message) {
            case WM_IME_STARTCOMPOSITION:
                if (box) box->composing = true;
                break;
            case WM_IME_ENDCOMPOSITION: {
                if (box) box->composing = false;
                result = DefSubclassProc(window, message, wParam, lParam);
                handled = true;
                box = UiFindTextBox(host.boxes, window);
                if (!box || host.closing) break;
                std::wstring text = UiTextBoxText(window);
                if (text == box->text) break;
                box->text = text;
                UiHostItemEvent(host, box->id, UiActionKind::TextChange, std::move(text));
                break;
            }
            case WM_KEYDOWN: {
                if (!commands) break;
                uint32_t id = box->id;
                handled = true;
                switch (wParam) {
                    case VK_TAB: {
                        UiIntent intent = UiKeyIntent(host.stops, {id, -1}, UiSurfaceId::Content,
                                                      VK_TAB, GetKeyState(VK_SHIFT) < 0, 40, 40);
                        if (intent.kind == UiIntentKind::Focus) UiHostSetFocus(host, intent.target, true);
                        break;
                    }
                    case VK_RETURN:
                        UiHostItemEvent(host, id, UiActionKind::TextCommit, box->text);
                        break;
                    case VK_ESCAPE:
                        UiHostItemEvent(host, id, UiActionKind::TextRevert);
                        break;
                    case VK_UP:
                    case VK_DOWN: {
                        const UiItem* item = UiFindItem(host.frame, id);
                        if (!item || item->kind != UiItemKind::NumberBox) {
                            handled = false;
                            break;
                        }
                        UiHostItemEvent(host, id, UiActionKind::Step, {}, wParam == VK_UP ? 1 : -1);
                        break;
                    }
                    default:
                        handled = false;
                        break;
                }
                break;
            }
            case WM_CHAR:
                // Their keys were handled above; the characters would only beep.
                handled = commands && (wParam == L'\t' || wParam == L'\r' || wParam == 0x1B);
                break;
            case WM_KILLFOCUS: {
                // The EDIT notifies EN_KILLFOCUS from its default handling.
                HWND next = reinterpret_cast<HWND>(wParam);
                bool inside = next && host.main && (next == host.main || IsChild(host.main, next));
                host.boxes.leavingWindow = !inside;
                result = DefSubclassProc(window, message, wParam, lParam);
                host.boxes.leavingWindow = false;
                handled = true;
                break;
            }
            case WM_NCDESTROY:
                RemoveWindowSubclass(window, UiHostTextBoxProc, 0);
                if (box) box->window = nullptr;
                break;
        }
        if (!handled) result = DefSubclassProc(window, message, wParam, lParam);
        UiHostRebuildIfPending(host);
    } catch (...) {
        Wh_Log(L"Text box callback failed (message 0x%04X); closing the window", message);
        result = 0;
        UiHostFault(host);
    }
    host.dispatchDepth--;
    return result;
}

static LRESULT CALLBACK UiHostMainProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* host = reinterpret_cast<UiHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return host ? UiHostProc(*host, window, UiSurfaceId::Main, message, wParam, lParam) :
                  DefWindowProcW(window, message, wParam, lParam);
}

static LRESULT CALLBACK UiHostViewportProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(
            reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* host = reinterpret_cast<UiHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    return host ? UiHostProc(*host, window, UiSurfaceId::Content, message, wParam, lParam) :
                  DefWindowProcW(window, message, wParam, lParam);
}

// Runs one window on the calling thread (an STA) until it closes: registers the window classes,
// keeps every graphics and WinRT object in one scope that ends before the caller uninitializes
// the apartment, destroys the window (then its icons) inside that scope on every path, and
// unregisters the classes. Returns false when the window could not be created.
static bool UiHostRun(const UiHostConfig& config, UiHostClient& client) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    // A stale registration of the same name (an instance that never unregistered) is replaced.
    auto registerClass = [&](PCWSTR name, WNDPROC proc, UINT style) {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.style = style;
        windowClass.lpfnWndProc = proc;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.lpszClassName = name;
        if (RegisterClassExW(&windowClass)) return true;
        if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS && UnregisterClassW(name, instance) &&
            RegisterClassExW(&windowClass)) {
            return true;
        }
        Wh_Log(L"Could not register window class %s: %lu", name, GetLastError());
        return false;
    };
    bool mainRegistered = registerClass(config.mainClass, UiHostMainProc, 0);
    bool viewportRegistered = mainRegistered && registerClass(config.viewportClass, UiHostViewportProc, 0);
    // Without its class the popup can't open; the window works otherwise.
    bool popupRegistered = viewportRegistered && config.popupClass &&
                           registerClass(config.popupClass, UiHostPopupProc, CS_DROPSHADOW);
    UiHostConfig effective = config;
    if (!popupRegistered) effective.popupClass = nullptr;
    bool created = false;
    if (viewportRegistered) {
        UiGraphics graphics;
        UiThemeReader themes;
        UiHost host;
        host.config = &effective;
        host.client = &client;
        host.graphics = &graphics;
        host.themes = &themes;
        try {
            UiCreateGraphics(graphics);
            POINT cursor{};
            GetCursorPos(&cursor);
            MONITORINFO monitor{sizeof(monitor)};
            GetMonitorInfoW(MonitorFromPoint(config.position.value_or(cursor), MONITOR_DEFAULTTONEAREST),
                            &monitor);
            POINT origin = config.position.value_or(POINT{monitor.rcWork.left, monitor.rcWork.top});
            // Created small on the target monitor, then sized at that monitor's DPI.
            HWND window = CreateWindowExW(WS_EX_APPWINDOW, config.mainClass, config.title,
                                          WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, origin.x, origin.y,
                                          100, 100, nullptr, nullptr, instance, &host);
            if (window) {
                created = true;
                float scale = UiHostScale(host);
                int workWidth = int(monitor.rcWork.right - monitor.rcWork.left);
                int workHeight = int(monitor.rcWork.bottom - monitor.rcWork.top);
                int width = std::min(int(std::lround(config.size.width * scale)), workWidth);
                int height = std::min(int(std::lround(config.size.height * scale)), workHeight);
                if (!config.position) {
                    origin = {monitor.rcWork.left + (workWidth - width) / 2,
                              monitor.rcWork.top + (workHeight - height) / 2};
                }
                SetWindowPos(window, nullptr, origin.x, origin.y, width, height,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                if (client.Unloading()) {
                    PostMessageW(window, WM_CLOSE, 0, 0);
                } else {
                    ShowWindow(window, config.activate ? SW_SHOW : SW_SHOWNOACTIVATE);
                    if (config.activate) SetForegroundWindow(window);
                }
                MSG message{};
                int got;
                while ((got = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                if (got < 0) Wh_Log(L"Window message loop failed: %lu", GetLastError());
            } else {
                Wh_Log(L"Could not create the window: %lu", GetLastError());
            }
        } catch (...) {
            Wh_Log(L"Window setup failed");
        }
        if (host.main && IsWindow(host.main)) DestroyWindow(host.main);
        UiPopupClose(host.popup);
        UiReleaseTextBoxes(host.boxes);
        for (HICON icon : host.icons) {
            if (icon) DestroyIcon(icon);
        }
    }
    if (popupRegistered) UnregisterClassW(config.popupClass, instance);
    if (viewportRegistered) UnregisterClassW(config.viewportClass, instance);
    if (mainRegistered) UnregisterClassW(config.mainClass, instance);
    return created;
}
