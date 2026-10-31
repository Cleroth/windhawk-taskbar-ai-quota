// Window host of the custom-drawn windows (native): a main window (navigation, pinned header)
// and its scrolling viewport child, each with its own render target; theme, DPI, pointer and
// keyboard input, focus, scrolling, close paths and teardown. A client supplies the content and
// the window-specific behavior. Rules: ui/AGENTS.md.

struct UiHost;

// What a window built on the host provides. Calls arrive on the window thread inside the host's
// exception boundary (they may throw: the host fault-closes). Actions arrive as copies; item
// references don't survive client calls.
struct UiHostClient {
    // Builds the frame for a client area of `client` DIPs.
    virtual void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) = 0;
    virtual void Invoke(UiHost& host, UiAction action) = 0;
    // Messages the host doesn't handle (application and registered messages); true if handled.
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

protected:
    ~UiHostClient() = default;
};

struct UiHostConfig {
    PCWSTR mainClass = nullptr, viewportClass = nullptr, title = nullptr;
    UINT activateMessage = 0;          // Registered "show yourself" message; 0 = none.
    D2D1_SIZE_F size{1000, 860};       // Initial outer size, DIPs.
    D2D1_SIZE_F minSize{500, 400};     // Minimum outer size, DIPs.
    bool activate = true;              // Show active and in the foreground.
    std::optional<POINT> position;     // Top-left in pixels; default: centered on the cursor's monitor.
};

struct UiHost {
    // Wiring, owned by UiHostRun's scope
    const UiHostConfig* config = nullptr;
    UiHostClient* client = nullptr;
    UiGraphics* graphics = nullptr;
    UiThemeReader* themes = nullptr;

    // Windows, targets, DPI and theme
    HWND main = nullptr, viewport = nullptr;
    UiSurface mainSurface, viewSurface;
    UINT dpi = 96;
    UiThemeInput theme;
    UiPalette palette;

    // Frame and interaction
    UiFrame frame;
    std::vector<UiFocusStop> stops;
    UiPointerState pointer;
    UiFocusRef focus;
    bool focusVisible = false;
    float scroll = 0;
    bool scrollbarHot = false, scrollDragging = false;
    float scrollDragOffset = 0;

    // Lifecycle: structural rebuilds wait until dispatch unwinds (ui/AGENTS.md).
    int dispatchDepth = 0;
    bool rebuildPending = false, resetScroll = false;
    bool closing = false, faulted = false;
};

static void UiHostRequestRebuild(UiHost& host, bool resetScroll) {
    host.rebuildPending = true;
    host.resetScroll = host.resetScroll || resetScroll;
}

static float UiHostScale(const UiHost& host) {
    return float(host.dpi) / 96.0f;
}

static float UiHostViewHeight(const UiHost& host) {
    return host.frame.viewport.bottom - host.frame.viewport.top;
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
    host.mainSurface.cache.Clear();
    host.viewSurface.cache.Clear();
    InvalidateRect(host.main, nullptr, FALSE);
    if (host.viewport) InvalidateRect(host.viewport, nullptr, FALSE);
}

static UiScrollbar UiHostScrollbar(const UiHost& host) {
    const D2D1_RECT_F& v = host.frame.viewport;
    return UiScrollbarFor({v.right - v.left, v.bottom - v.top}, host.scroll,
                          host.frame.contentHeight, host.scrollbarHot || host.scrollDragging);
}

// The element under a client point (pixels) of a surface.
static UiFocusRef UiHostHit(UiHost& host, UiSurfaceId surface, POINT point) {
    float scale = UiHostScale(host);
    D2D1_POINT_2F at{float(point.x) / scale, float(point.y) / scale};
    if (surface == UiSurfaceId::Content) at.y += host.scroll;
    return UiHitTest(*host.graphics, surface == UiSurfaceId::Main ? host.frame.main : host.frame.content,
                     at);
}

// Scrolls to a pixel-aligned offset and re-targets the pointer, whose element moved under it.
static void UiHostScrollTo(UiHost& host, float offset) {
    float scale = UiHostScale(host);
    offset = UiClampScroll(offset, UiHostViewHeight(host), host.frame.contentHeight);
    offset = std::round(offset * scale) / scale;
    if (offset == host.scroll) return;
    host.scroll = offset;
    POINT cursor{};
    RECT client{};
    if (!host.pointer.pressed.id && !host.scrollbarHot && GetCursorPos(&cursor) &&
        ScreenToClient(host.viewport, &cursor) && GetClientRect(host.viewport, &client) &&
        PtInRect(&client, cursor)) {
        UiPointerMove(host.pointer, UiHostHit(host, UiSurfaceId::Content, cursor));
    }
    InvalidateRect(host.viewport, nullptr, FALSE);
}

// Moves focus to an element (keyboard focus to its surface), scrolling content into view when
// the move came from the keyboard.
static void UiHostSetFocus(UiHost& host, UiFocusRef ref, bool visible) {
    UiSurfaceId surface = UiSurfaceId::Main;
    const UiItem* item = UiFindItem(host.frame, ref.id, &surface);
    if (!item) return;
    host.focus = ref;
    host.focusVisible = visible;
    if (surface == UiSurfaceId::Content && visible) {
        std::vector<D2D1_RECT_F> bounds = UiFocusBounds(*host.graphics, *item, ref.link);
        if (!bounds.empty()) {
            UiHostScrollTo(host, UiScrollIntoView(host.scroll, UiHostViewHeight(host),
                                                  bounds.front().top, bounds.back().bottom, 12));
        }
    }
    HWND target = surface == UiSurfaceId::Main ? host.main : host.viewport;
    if (GetFocus() != target) SetFocus(target);
    InvalidateRect(host.main, nullptr, FALSE);
    InvalidateRect(host.viewport, nullptr, FALSE);
}

// Activation. The action is copied first: the client may rebuild, pump messages or show a modal.
static void UiHostInvoke(UiHost& host, UiFocusRef ref) {
    if (host.closing) return;
    const UiAction* action = UiActionOf(host.frame, ref);
    if (!action) return;
    UiAction copy = *action;
    host.client->Invoke(host, std::move(copy));
}

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
                if (!host.faulted && !host.client->Unloading() && !host.client->PrepareClose(host)) {
                    break;
                }
                host.closing = true;
                DestroyWindow(window);
                break;
            case WM_DESTROY:
                if (isMain) {
                    host.closing = true;
                    if (HWND capture = GetCapture(); capture == host.main || capture == host.viewport) {
                        ReleaseCapture();
                    }
                    host.client->Destroying();
                }
                break;
            case WM_NCDESTROY:
                // Children are gone before the main window's NCDESTROY; each surface releases its
                // target with its window. Host state outlives both.
                UiDiscardSurface(own);
                own.window = nullptr;
                SetWindowLongPtrW(window, GWLP_USERDATA, 0);
                if (isMain) {
                    // Quit first: the loop must end even if the client's teardown throws.
                    host.main = nullptr;
                    PostQuitMessage(0);
                    host.client->Destroyed();
                } else {
                    host.viewport = nullptr;
                }
                handled = false;
                break;

            // Size, DPI, theme
            case WM_SIZE:
                UiResizeSurface(own);
                if (isMain && wParam != SIZE_MINIMIZED) UiHostRequestRebuild(host, false);
                break;
            case WM_GETMINMAXINFO:
                if (isMain) {
                    auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                    info->ptMinTrackSize = {LONG(host.config->minSize.width * UiHostScale(host)),
                                            LONG(host.config->minSize.height * UiHostScale(host))};
                }
                break;
            case WM_DPICHANGED: {
                host.dpi = HIWORD(wParam);
                UiDiscardSurface(host.mainSurface);
                UiDiscardSurface(host.viewSurface);
                const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
                SetWindowPos(window, nullptr, suggested->left, suggested->top,
                             suggested->right - suggested->left, suggested->bottom - suggested->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
                UiHostRequestRebuild(host, false);
                break;
            }
            case WM_SETTINGCHANGE:
                if (isMain && (wParam == SPI_SETHIGHCONTRAST ||
                               (lParam && wcscmp(reinterpret_cast<PCWSTR>(lParam),
                                                 L"ImmersiveColorSet") == 0))) {
                    UiHostApplyTheme(host);
                }
                handled = false;
                break;
            case WM_THEMECHANGED:
            case WM_SYSCOLORCHANGE:
            case WM_DWMCOLORIZATIONCOLORCHANGED:
                if (isMain) UiHostApplyTheme(host);
                handled = false;
                break;

            // Paint
            case WM_ERASEBKGND:
                result = 1;
                break;
            case WM_PAINT: {
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
                UiPaintState state{host.pointer.hot, host.pointer.pressed, host.focus,
                                   host.focusVisible && GetFocus() == window};
                target->BeginDraw();
                D2D1_MATRIX_3X2_F identity = D2D1::Matrix3x2F::Identity();
                target->SetTransform(&identity);
                target->Clear(host.palette.background);
                if (isMain) {
                    UiPaintItems(target, *host.graphics, own.cache, host.palette, host.frame.main,
                                 state, {0, 0}, scale, -1e9f, 1e9f);
                } else {
                    float height = UiHostViewHeight(host);
                    UiPaintItems(target, *host.graphics, own.cache, host.palette, host.frame.content,
                                 state, {0, -host.scroll}, scale, host.scroll, host.scroll + height);
                    UiPaintScrollbar(target, own.cache, host.palette, UiHostScrollbar(host),
                                     host.scrollbarHot || host.scrollDragging, scale);
                }
                if (!UiSurfaceDrawResult(own, target->EndDraw())) UiHostFault(host);
                break;
            }

            // Pointer
            case WM_MOUSEMOVE: {
                POINT point{short(LOWORD(lParam)), short(HIWORD(lParam))};
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window, 0};
                TrackMouseEvent(&track);
                float scale = UiHostScale(host);
                if (!isMain && host.scrollDragging) {
                    UiHostScrollTo(host, UiScrollForThumb(UiHostScrollbar(host),
                                                          float(point.y) / scale - host.scrollDragOffset,
                                                          UiHostViewHeight(host),
                                                          host.frame.contentHeight));
                    break;
                }
                bool overBar = !isMain && UiHostScrollbar(host).visible &&
                               float(point.x) / scale >= host.frame.viewport.right -
                                                             host.frame.viewport.left - 16;
                bool changed = overBar != host.scrollbarHot;
                host.scrollbarHot = overBar;
                changed |= UiPointerMove(host.pointer, overBar ? UiFocusRef{} :
                                                                 UiHostHit(host, surface, point));
                if (changed) {
                    InvalidateRect(host.main, nullptr, FALSE);
                    InvalidateRect(host.viewport, nullptr, FALSE);
                }
                break;
            }
            case WM_MOUSELEAVE:
                if (!host.pointer.pressed.id && !host.scrollDragging) {
                    host.pointer.hot = {};
                    host.scrollbarHot = false;
                    InvalidateRect(host.main, nullptr, FALSE);
                    InvalidateRect(host.viewport, nullptr, FALSE);
                }
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
                UiFocusRef hit = UiHostHit(host, surface, point);
                host.pointer.hot = hit;
                host.pointer.pressed = hit;
                if (hit.id) SetCapture(window);
                if (hit.id) {
                    UiHostSetFocus(host, hit, false);
                } else {
                    host.focusVisible = false;
                    if (GetFocus() != window) SetFocus(window);
                }
                InvalidateRect(host.main, nullptr, FALSE);
                InvalidateRect(host.viewport, nullptr, FALSE);
                break;
            }
            case WM_LBUTTONUP: {
                POINT point{short(LOWORD(lParam)), short(HIWORD(lParam))};
                UiFocusRef activate = UiPointerRelease(host.pointer, UiHostHit(host, surface, point));
                host.scrollDragging = false;
                if (GetCapture() == window) ReleaseCapture();
                InvalidateRect(host.main, nullptr, FALSE);
                InvalidateRect(host.viewport, nullptr, FALSE);
                if (activate.id) UiHostInvoke(host, activate);
                break;
            }
            case WM_CAPTURECHANGED:
                host.pointer.pressed = {};
                host.scrollDragging = false;
                InvalidateRect(host.main, nullptr, FALSE);
                InvalidateRect(host.viewport, nullptr, FALSE);
                break;
            case WM_MOUSEWHEEL: {
                // Wheel over the page column (header or content) scrolls the content.
                POINT point{short(LOWORD(lParam)), short(HIWORD(lParam))};
                ScreenToClient(host.main, &point);
                if (float(point.x) / UiHostScale(host) < host.frame.viewport.left) break;
                UINT lines = 3;
                SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
                float notches = float(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA;
                float step = lines == WHEEL_PAGESCROLL ? UiHostViewHeight(host) - 40 : float(lines) * 16;
                UiHostScrollTo(host, host.scroll - notches * step);
                break;
            }
            case WM_SETCURSOR: {
                if (LOWORD(lParam) != HTCLIENT) {
                    handled = false;
                    break;
                }
                const UiItem* item = UiFindItem(host.frame, host.pointer.hot.id);
                bool link = item && (host.pointer.hot.link >= 0 || item->kind == UiItemKind::Link);
                SetCursor(LoadCursorW(nullptr, link ? IDC_HAND : IDC_ARROW));
                result = TRUE;
                break;
            }

            // Keyboard and focus
            case WM_KEYDOWN: {
                if (host.closing) break;
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
                        handled = false;
                        break;
                }
                break;
            }
            case WM_SETFOCUS: {
                // Activation focuses the main window; keyboard focus belongs to the surface of
                // the focused element.
                UiSurfaceId focused = UiSurfaceId::Main;
                if (isMain && UiFindItem(host.frame, host.focus.id, &focused) &&
                    focused == UiSurfaceId::Content && host.viewport) {
                    SetFocus(host.viewport);
                }
                InvalidateRect(window, nullptr, FALSE);
                break;
            }
            case WM_KILLFOCUS:
                InvalidateRect(window, nullptr, FALSE);
                break;

            default:
                handled = false;
                if (!isMain) break;
                if (host.config->activateMessage && message == host.config->activateMessage) {
                    // Brings back the window, or the enabled visible modal it owns; never
                    // anything else it owns.
                    handled = true;
                    if (host.closing || host.client->Unloading()) break;
                    if (IsIconic(window)) ShowWindow(window, SW_RESTORE);
                    HWND target = GetLastActivePopup(window);
                    if (target != window && !(IsWindowEnabled(target) && IsWindowVisible(target))) {
                        target = window;
                    }
                    SetForegroundWindow(target);
                } else if (message >= WM_APP) {
                    handled = host.client->Message(host, message, wParam, lParam, result);
                }
                break;
        }

        // Deferred structural rebuild, once the outermost dispatch is done with the old items.
        if (host.dispatchDepth == 1 && host.rebuildPending && host.main && host.viewport &&
            !host.closing) {
            host.rebuildPending = false;
            RECT client{};
            GetClientRect(host.main, &client);
            float scale = UiHostScale(host);
            UiTrimCaches(*host.graphics);
            UiFrame frame;
            host.client->Build(host, {float(client.right) / scale, float(client.bottom) / scale},
                               frame);
            std::vector<UiFocusStop> stops = UiFocusStops(frame);
            UiFocusRef fallback;
            for (const UiItem& item : frame.main) {
                if (item.kind == UiItemKind::Nav && item.selected) fallback = {item.id, -1};
            }
            // Keyboard focus starts on the selected navigation item, without a ring.
            host.focus = UiResolveFocus(host.stops, stops, host.focus.id ? host.focus : fallback,
                                        fallback);
            host.frame = std::move(frame);
            host.stops = std::move(stops);
            if (!UiFindItem(host.frame, host.pointer.hot.id)) host.pointer.hot = {};
            if (!UiFindItem(host.frame, host.pointer.pressed.id)) host.pointer.pressed = {};
            host.scroll = host.resetScroll ? 0 : host.scroll;
            host.resetScroll = false;
            host.scroll = std::round(UiClampScroll(host.scroll, UiHostViewHeight(host),
                                                   host.frame.contentHeight) * scale) / scale;
            const D2D1_RECT_F& v = host.frame.viewport;
            int left = int(std::lround(v.left * scale)), top = int(std::lround(v.top * scale));
            SetWindowPos(host.viewport, nullptr, left, top, int(std::lround(v.right * scale)) - left,
                         int(std::lround(v.bottom * scale)) - top, SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(host.main, nullptr, FALSE);
            InvalidateRect(host.viewport, nullptr, FALSE);
        }
    } catch (...) {
        Wh_Log(L"Window callback failed (message 0x%04X); closing the window", message);
        handled = true;
        result = message == WM_CREATE ? -1 : 0;
        UiHostFault(host);
    }
    host.dispatchDepth--;
    return handled ? result : DefWindowProcW(window, message, wParam, lParam);
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
// the apartment, destroys the window inside that scope on every path, and unregisters the
// classes. Returns false when the window could not be created.
static bool UiHostRun(const UiHostConfig& config, UiHostClient& client) {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    // A stale registration of the same name (an instance that never unregistered) is replaced.
    auto registerClass = [&](PCWSTR name, WNDPROC proc) {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
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
    bool mainRegistered = registerClass(config.mainClass, UiHostMainProc);
    bool viewportRegistered = mainRegistered && registerClass(config.viewportClass, UiHostViewportProc);
    bool created = false;
    if (viewportRegistered) {
        UiGraphics graphics;
        UiThemeReader themes;
        UiHost host;
        host.config = &config;
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
    }
    if (viewportRegistered) UnregisterClassW(config.viewportClass, instance);
    if (mainRegistered) UnregisterClassW(config.mainClass, instance);
    return created;
}
