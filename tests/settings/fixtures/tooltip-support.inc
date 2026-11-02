// Tooltip test support shared by the toolkit's and the history window's fixtures: pointers as
// the placement takes them and the plan's exclusion rectangle.

// A pointer at `cursor` whose `size` px image has its hotspot `hotspot` from its top-left.
static UiTooltipKey TestKey(POINT cursor, RECT work, LONG size = 32, POINT hotspot = {0, 0}, UINT dpi = 96) {
    UiTooltipKey key;
    key.cursor = cursor;
    LONG left = cursor.x - hotspot.x, top = cursor.y - hotspot.y;
    key.cursorRect = {left, top, left + size, top + size};
    key.work = work;
    key.dpi = dpi;
    return key;
}

// The pointer's exclusion rectangle: its image and hotspot, 8 DIP (rounded up to pixels) out.
static RECT TestExclusion(const UiTooltipKey& key) {
    LONG margin = LONG(std::ceil(8 * float(key.dpi) / 96));
    return {std::min(key.cursorRect.left, key.cursor.x) - margin, std::min(key.cursorRect.top, key.cursor.y) - margin,
            std::max(key.cursorRect.right, key.cursor.x + 1) + margin,
            std::max(key.cursorRect.bottom, key.cursor.y + 1) + margin};
}

// Inside the work area, not empty, and clear of the exclusion rectangle.
static bool TestClear(const UiTooltipSpot& spot, const UiTooltipKey& key) {
    RECT ex = TestExclusion(key);
    const RECT &r = spot.rect, &w = key.work;
    bool inside = r.left >= w.left && r.top >= w.top && r.right <= w.right && r.bottom <= w.bottom &&
                  r.right > r.left && r.bottom > r.top;
    return inside && (r.right <= ex.left || r.left >= ex.right || r.bottom <= ex.top || r.top >= ex.bottom);
}
