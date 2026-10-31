// Input of the custom-drawn windows as pure transitions: pointer state, keyboard intents
// (focus moves, activation, scrolling) and the scroll model. The host applies the results.

// Pointer state shared by the window's surfaces. pressed survives leaving the element; release
// over the same element activates it.
struct UiPointerState {
    UiFocusRef hot, pressed;
};

// Returns whether the hot element changed.
static bool UiPointerMove(UiPointerState& pointer, UiFocusRef hit) {
    if (pointer.hot == hit) return false;
    pointer.hot = hit;
    return true;
}

// The element to activate, if the release ends a press on it.
static UiFocusRef UiPointerRelease(UiPointerState& pointer, UiFocusRef hit) {
    UiFocusRef pressed = pointer.pressed;
    pointer.pressed = {};
    return pressed.id && pressed == hit ? pressed : UiFocusRef{};
}

enum class UiIntentKind : uint8_t { None, Focus, Invoke, ScrollBy, ScrollTo };

struct UiIntent {
    UiIntentKind kind = UiIntentKind::None;
    UiFocusRef target;
    float scroll = 0;
};

// Keys on the focused surface. Tab cycles all stops; Up/Down/Home/End move within the
// navigation pane, Up/Down between content stops (scrolling past the ends); Page Up/Down and
// Home/End scroll content; Enter and Space activate.
static UiIntent UiKeyIntent(const std::vector<UiFocusStop>& stops, UiFocusRef focus,
                            UiSurfaceId surface, UINT key, bool shift, float lineStep,
                            float pageStep) {
    auto at = std::find_if(stops.begin(), stops.end(),
                           [&](const UiFocusStop& stop) { return stop.ref == focus; });
    bool known = at != stops.end();
    bool inNav = known && at->nav;
    auto move = [&](int direction, bool nav) -> UiIntent {
        int count = int(stops.size());
        int index = known ? int(at - stops.begin()) : (direction > 0 ? -1 : count);
        for (int i = index + direction; i >= 0 && i < count; i += direction) {
            if (stops[i].nav == nav) return {UiIntentKind::Focus, stops[i].ref};
        }
        return {};
    };

    switch (key) {
        case VK_TAB: {
            if (stops.empty()) return {};
            int count = int(stops.size());
            int index = known ? int(at - stops.begin()) : (shift ? 0 : -1);
            return {UiIntentKind::Focus, stops[size_t((index + (shift ? -1 : 1) + count) % count)].ref};
        }
        case VK_UP:
        case VK_DOWN: {
            int direction = key == VK_DOWN ? 1 : -1;
            if (inNav) return move(direction, true);
            if (surface == UiSurfaceId::Content) {
                UiIntent next = known ? move(direction, false) : UiIntent{};
                if (next.kind != UiIntentKind::None) return next;
                return {UiIntentKind::ScrollBy, {}, direction * lineStep};
            }
            return {};
        }
        case VK_HOME:
        case VK_END:
            if (inNav) {
                auto stop = key == VK_HOME ?
                    std::find_if(stops.begin(), stops.end(), [](const UiFocusStop& s) { return s.nav; }) :
                    std::find_if(stops.rbegin(), stops.rend(), [](const UiFocusStop& s) { return s.nav; }).base() - 1;
                return {UiIntentKind::Focus, stop->ref};
            }
            return {UiIntentKind::ScrollTo, {},
                    key == VK_HOME ? 0.0f : std::numeric_limits<float>::max()};
        case VK_PRIOR:
        case VK_NEXT:
            return {UiIntentKind::ScrollBy, {}, key == VK_NEXT ? pageStep : -pageStep};
        case VK_RETURN:
        case VK_SPACE:
            return known ? UiIntent{UiIntentKind::Invoke, focus} : UiIntent{};
    }
    return {};
}

// Keys while a dropdown popup is open (keyboard focus stays on the field): arrows, Home/End and
// Page Up/Down move the highlight; Enter and Space choose it; Esc and F4 close; Tab closes and
// then moves focus as usual (pass). Other keys do nothing.
enum class UiOptionIntentKind : uint8_t { None, Highlight, Choose, Close, CloseAndPass };

struct UiOptionIntent {
    UiOptionIntentKind kind = UiOptionIntentKind::None;
    int index = -1;
};

static UiOptionIntent UiOptionKey(int count, int highlight, UINT key) {
    if (count <= 0) return {UiOptionIntentKind::Close};
    int last = count - 1;
    switch (key) {
        case VK_UP: return {UiOptionIntentKind::Highlight, highlight < 0 ? last : std::max(highlight - 1, 0)};
        case VK_DOWN: return {UiOptionIntentKind::Highlight, highlight < 0 ? 0 : std::min(highlight + 1, last)};
        case VK_HOME:
        case VK_PRIOR: return {UiOptionIntentKind::Highlight, 0};
        case VK_END:
        case VK_NEXT: return {UiOptionIntentKind::Highlight, last};
        case VK_RETURN:
        case VK_SPACE:
            return highlight >= 0 && highlight <= last ? UiOptionIntent{UiOptionIntentKind::Choose, highlight} :
                                                         UiOptionIntent{UiOptionIntentKind::Close};
        case VK_ESCAPE:
        case VK_F4: return {UiOptionIntentKind::Close};
        case VK_TAB: return {UiOptionIntentKind::CloseAndPass};
    }
    return {};
}

// Sliders: the value under a pointer x (rounded to whole steps, clamped), and what keys step it
// by: arrows 1, Page Up/Down a tenth of the range, Home/End all of it (the step clamps).
static int UiSliderValueAt(const UiItem& slider, float x) {
    float width = slider.rect.right - slider.rect.left;
    if (width <= 0 || slider.maximum <= slider.minimum) return slider.minimum;
    float t = std::clamp((x - slider.rect.left) / width, 0.0f, 1.0f);
    return slider.minimum + int(std::lround(t * float(slider.maximum - slider.minimum)));
}

static std::optional<int> UiSliderKey(const UiItem& slider, UINT key) {
    int range = std::max(slider.maximum - slider.minimum, 1);
    switch (key) {
        case VK_RIGHT:
        case VK_UP: return 1;
        case VK_LEFT:
        case VK_DOWN: return -1;
        case VK_PRIOR: return std::max(range / 10, 1);
        case VK_NEXT: return -std::max(range / 10, 1);
        case VK_HOME: return -range;
        case VK_END: return range;
    }
    return std::nullopt;
}

static float UiClampScroll(float offset, float viewportHeight, float contentHeight) {
    return std::clamp(offset, 0.0f, std::max(contentHeight - viewportHeight, 0.0f));
}

// The smallest scroll that shows [top, bottom) with `margin` around it.
static float UiScrollIntoView(float offset, float viewportHeight, float top, float bottom,
                              float margin) {
    if (top - margin < offset) return top - margin;
    if (bottom + margin > offset + viewportHeight) {
        return std::min(top - margin, bottom + margin - viewportHeight);
    }
    return offset;
}

// The scroll offset that puts the thumb's top edge at `thumbTop` (track coordinates).
static float UiScrollForThumb(const UiScrollbar& bar, float thumbTop, float viewportHeight,
                              float contentHeight) {
    float travel = (bar.track.bottom - bar.track.top) - (bar.thumb.bottom - bar.thumb.top);
    if (travel <= 0) return 0;
    float fraction = std::clamp((thumbTop - bar.track.top) / travel, 0.0f, 1.0f);
    return fraction * std::max(contentHeight - viewportHeight, 0.0f);
}
