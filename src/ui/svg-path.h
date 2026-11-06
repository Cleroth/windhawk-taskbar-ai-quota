// SVG path data (M L H V C S Q T A Z, absolute and relative) to Direct2D path geometry. Pure.

static bool UiParseSvgPath(PCWSTR data, ID2D1GeometrySink* sink) {
    const wchar_t* p = data;
    auto skip = [&] {
        while (*p && (iswspace(*p) || *p == L',')) p++;
    };
    auto number = [&](float& out) {
        skip();
        wchar_t* end = nullptr;
        out = wcstof(p, &end);
        if (end == p) return false;
        p = end;
        return true;
    };
    auto flag = [&](bool& out) {
        skip();
        if (*p != L'0' && *p != L'1') return false;
        out = *p++ == L'1';
        return true;
    };

    wchar_t command = 0, previous = 0;
    D2D1_POINT_2F current{}, start{}, control{};
    bool open = false;
    for (;;) {
        skip();
        if (!*p) break;
        if (iswalpha(*p)) command = *p++;
        else if (!command) return false;
        bool relative = iswlower(command);
        wchar_t upper = wchar_t(towupper(command));
        auto point = [&](float x, float y) {
            return relative ? D2D1::Point2F(current.x + x, current.y + y) : D2D1::Point2F(x, y);
        };
        // Smooth curves reflect the previous control point only after a curve of their kind.
        auto reflected = [&](wchar_t a, wchar_t b) {
            return previous == a || previous == b ?
                       D2D1::Point2F(2 * current.x - control.x, 2 * current.y - control.y) :
                       current;
        };

        float x1, y1, x2, y2, x, y;
        if (upper == L'Z' || upper == L'M') {
            if (upper == L'M' && (!number(x) || !number(y))) return false;
            if (open) sink->EndFigure(upper == L'Z' ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            open = false;
            if (upper == L'M') start = point(x, y);
            current = start;
            if (upper == L'M') {
                sink->BeginFigure(current, D2D1_FIGURE_BEGIN_FILLED);
                open = true;
                command = relative ? L'l' : L'L';  // Further pairs are implicit lines.
            } else {
                command = 0;  // A close takes no numbers: one after it is malformed.
            }
            previous = upper;
            continue;
        }
        // Drawing after a close starts a new figure at the closed figure's start.
        if (!open) {
            sink->BeginFigure(current, D2D1_FIGURE_BEGIN_FILLED);
            open = true;
        }
        if (upper == L'L') {
            if (!number(x) || !number(y)) return false;
            current = point(x, y);
            sink->AddLine(current);
        } else if (upper == L'H') {
            if (!number(x)) return false;
            current.x = relative ? current.x + x : x;
            sink->AddLine(current);
        } else if (upper == L'V') {
            if (!number(y)) return false;
            current.y = relative ? current.y + y : y;
            sink->AddLine(current);
        } else if (upper == L'C' || upper == L'S') {
            D2D1_POINT_2F first;
            if (upper == L'C') {
                if (!number(x1) || !number(y1)) return false;
                first = point(x1, y1);
            } else {
                first = reflected(L'C', L'S');
            }
            if (!number(x2) || !number(y2) || !number(x) || !number(y)) return false;
            D2D1_BEZIER_SEGMENT segment{first, point(x2, y2), point(x, y)};
            sink->AddBezier(&segment);
            control = segment.point2;
            current = segment.point3;
        } else if (upper == L'Q' || upper == L'T') {
            D2D1_POINT_2F first;
            if (upper == L'Q') {
                if (!number(x1) || !number(y1)) return false;
                first = point(x1, y1);
            } else {
                first = reflected(L'Q', L'T');
            }
            if (!number(x) || !number(y)) return false;
            D2D1_QUADRATIC_BEZIER_SEGMENT segment{first, point(x, y)};
            sink->AddQuadraticBezier(&segment);
            control = first;
            current = segment.point2;
        } else if (upper == L'A') {
            float rx, ry, rotation;
            bool large, sweep;
            if (!number(rx) || !number(ry) || !number(rotation) || !flag(large) || !flag(sweep) ||
                !number(x) || !number(y)) {
                return false;
            }
            D2D1_ARC_SEGMENT segment{point(x, y), {std::abs(rx), std::abs(ry)}, rotation,
                                     sweep ? D2D1_SWEEP_DIRECTION_CLOCKWISE :
                                             D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                     large ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL};
            sink->AddArc(&segment);
            current = segment.point;
        } else {
            return false;
        }
        previous = upper;
    }
    if (open) sink->EndFigure(D2D1_FIGURE_END_OPEN);
    return true;
}

// The geometry of static path data, cached on the graphics by the string's address. Throws on
// malformed data.
static ID2D1PathGeometry* UiPathGeometry(UiGraphics& g, PCWSTR data) {
    auto& geometry = g.paths[data];
    if (geometry) return geometry.get();
    winrt::com_ptr<ID2D1PathGeometry> created;
    winrt::check_hresult(g.d2d->CreatePathGeometry(created.put()));
    winrt::com_ptr<ID2D1GeometrySink> sink;
    winrt::check_hresult(created->Open(sink.put()));
    sink->SetFillMode(D2D1_FILL_MODE_WINDING);
    bool parsed = UiParseSvgPath(data, sink.get());
    HRESULT closed = sink->Close();
    if (!parsed) winrt::throw_hresult(E_INVALIDARG);
    winrt::check_hresult(closed);
    geometry = std::move(created);
    return geometry.get();
}
