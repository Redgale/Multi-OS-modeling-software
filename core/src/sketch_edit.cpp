// SPDX-License-Identifier: GPL-3.0-or-later
// Sketch editing tools that change existing curves: Extend, Break and Mirror.
#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>

#include "jig/sketch.hpp"

namespace jig
{
namespace
{
constexpr double kTau = 2 * std::numbers::pi;

double wrap(double a)  // into [0, 2pi)
{
    a = std::fmod(a, kTau);
    return a < 0 ? a + kTau : a;
}

Error editError(const std::string& code, const std::string& message)
{
    Error e;
    e.code = code;
    e.message = message;
    return e;
}

// A curve as plain geometry: a segment, or a circle with an angle range.
struct Geo
{
    bool line = false;
    double ax = 0, ay = 0, bx = 0, by = 0;
    double cx = 0, cy = 0, r = 0, a0 = 0, span = kTau;

    bool contains(double px, double py) const
    {
        if (line) {
            const double dx = bx - ax, dy = by - ay;
            const double u = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
            return u > -1e-9 && u < 1 + 1e-9;
        }
        return span >= kTau - 1e-9 || wrap(std::atan2(py - cy, px - cx) - a0) <= span + 1e-9;
    }
};

Geo geoOf(const Sketch& s, const SketchEntity& e)
{
    Geo g;
    if (e.type == EntityType::Line) {
        g.line = true;
        std::tie(g.ax, g.ay) = s.pointXY(e.p1);
        std::tie(g.bx, g.by) = s.pointXY(e.p2);
    }
    else {
        std::tie(g.cx, g.cy) = s.pointXY(e.center);
        g.r = e.r;
        if (e.type == EntityType::Arc) {
            g.a0 = wrap(e.a0);
            g.span = wrap(e.a1 - e.a0);
            if (g.span < 1e-9) {
                g.span = kTau;
            }
        }
    }
    return g;
}

// Where two curves' carriers cross: the line through A (not just its segment) or A's
// full circle, against B's line or circle. The caller filters by B's extent.
std::vector<std::pair<double, double>> carrierCrossings(const Geo& A, const Geo& B)
{
    std::vector<std::pair<double, double>> pts;
    if (A.line && B.line) {
        const double rx = A.bx - A.ax, ry = A.by - A.ay, sx = B.bx - B.ax, sy = B.by - B.ay;
        const double den = rx * sy - ry * sx;
        if (std::abs(den) > 1e-12) {
            const double u = ((B.ax - A.ax) * sy - (B.ay - A.ay) * sx) / den;
            pts.emplace_back(A.ax + rx * u, A.ay + ry * u);
        }
    }
    else if (A.line != B.line) {
        const Geo& L = A.line ? A : B;
        const Geo& O = A.line ? B : A;
        const double dx = L.bx - L.ax, dy = L.by - L.ay;
        const double fx = L.ax - O.cx, fy = L.ay - O.cy;
        const double a = dx * dx + dy * dy, b = 2 * (fx * dx + fy * dy), c = fx * fx + fy * fy - O.r * O.r;
        const double disc = b * b - 4 * a * c;
        if (disc >= 0) {
            for (double sgn : {-1.0, 1.0}) {
                const double u = (-b + sgn * std::sqrt(disc)) / (2 * a);
                pts.emplace_back(L.ax + dx * u, L.ay + dy * u);
            }
        }
    }
    else {
        const double dx = B.cx - A.cx, dy = B.cy - A.cy, d = std::hypot(dx, dy);
        if (d > 1e-12 && d <= A.r + B.r + 1e-9 && d >= std::abs(A.r - B.r) - 1e-9) {
            const double a = (A.r * A.r - B.r * B.r + d * d) / (2 * d);
            const double h = std::sqrt(std::max(0.0, A.r * A.r - a * a));
            const double mx = A.cx + dx * a / d, my = A.cy + dy * a / d;
            pts.emplace_back(mx + h * dy / d, my - h * dx / d);
            pts.emplace_back(mx - h * dy / d, my + h * dx / d);
        }
    }
    return pts;
}

// Mirror image of (x, y) across the line through (ax, ay) with unit direction (ux, uy).
std::pair<double, double> reflect(double x, double y, double ax, double ay, double ux, double uy)
{
    const double t = (x - ax) * ux + (y - ay) * uy;
    const double fx = ax + ux * t, fy = ay + uy * t;
    return {2 * fx - x, 2 * fy - y};
}
}  // namespace

// ---------------------------------------------------------------- extend

Result<int> Sketch::extend(int entityId, double x, double y)
{
    const SketchEntity* t = entity(entityId);
    if (!t || (t->type != EntityType::Line && t->type != EntityType::Arc)) {
        return editError("sketch.extend", "Extend works on lines and arcs (not ellipses or smooth curves yet).");
    }
    const Geo T = geoOf(*this, *t);
    // the end nearest the click
    const int endA = t->p1, endB = t->p2;
    auto [axx, ayy] = pointXY(endA);
    auto [bxx, byy] = pointXY(endB);
    const bool atEnd = std::hypot(x - bxx, y - byy) < std::hypot(x - axx, y - ayy);
    const int endPt = atEnd ? endB : endA;
    // a loose end only: nothing else may share or pin that point
    for (auto& [id, e] : entities_) {
        if (id != entityId && e.type != EntityType::Point && (e.p1 == endPt || e.p2 == endPt || e.center == endPt)) {
            return editError("sketch.extend", "That end is joined to another curve. Only a loose end can be extended.");
        }
    }
    for (auto& [cid, c] : constraints_) {
        if (c.type == ConstraintType::Coincident && (c.a == endPt || c.b == endPt)) {
            return editError("sketch.extend", "That end is joined to another curve. Only a loose end can be extended.");
        }
    }
    // how far to go: the nearest crossing beyond that end
    double best = 1e300;
    std::pair<double, double> hit;
    for (auto& [id, e] : entities_) {
        if (id == entityId || e.type == EntityType::Point || e.construction || e.type == EntityType::Ellipse ||
            e.type == EntityType::Spline) {
            continue;
        }
        const Geo C = geoOf(*this, e);
        for (auto [px, py] : carrierCrossings(T, C)) {
            if (!C.contains(px, py)) {
                continue;
            }
            double ahead = 0;  // how far past the end, along the curve
            if (T.line) {
                const double dx = T.bx - T.ax, dy = T.by - T.ay, L = std::hypot(dx, dy);
                const double u = ((px - T.ax) * dx + (py - T.ay) * dy) / (L * L);
                ahead = atEnd ? (u - 1) * L : -u * L;
            }
            else {
                const double ang = std::atan2(py - T.cy, px - T.cx);
                const double end = T.a0 + T.span;
                // counter-clockwise past the end angle, or clockwise before the start angle
                ahead = (atEnd ? wrap(ang - end) : wrap(T.a0 - ang)) * T.r;
                if (ahead / T.r >= kTau - T.span - 1e-9) {
                    continue;  // would wrap round onto the arc itself
                }
            }
            if (ahead > 1e-7 && ahead < best) {
                best = ahead;
                hit = {px, py};
            }
        }
    }
    if (best >= 1e299) {
        return editError("sketch.extend", "Nothing lies ahead of that end to extend it to.");
    }
    // the old end's own constraints (a fixed spot, a distance) would hold it back
    std::erase_if(constraints_, [&](const auto& kv) {
        const auto& c = kv.second;
        return c.a == endPt || c.b == endPt;
    });
    if (t->type == EntityType::Line) {
        dropSizeConstraints(entityId);
    }
    const int np = attachedPoint(entityId, hit.first, hit.second);
    SketchEntity& e = entities_.at(entityId);
    if (atEnd) {
        e.p2 = np;
    }
    else {
        e.p1 = np;
    }
    if (e.type == EntityType::Arc) {
        const double ang = std::atan2(hit.second - T.cy, hit.first - T.cx);
        if (atEnd) {
            e.a1 = e.a0 + wrap(ang - e.a0);
        }
        else {
            e.a0 -= wrap(e.a0 - ang);
        }
    }
    dropUnusedPoint(endPt);
    syncCurvePoints();
    return np;
}

// ---------------------------------------------------------------- break

Result<std::vector<int>> Sketch::breakAt(int entityId, double x, double y)
{
    const SketchEntity* t = entity(entityId);
    if (!t || t->type == EntityType::Point || t->type == EntityType::Ellipse || t->type == EntityType::Spline) {
        return editError("sketch.break", "Click a line, circle or arc to break it (not an ellipse or smooth curve yet).");
    }
    auto piece = trimPiece(entityId, x, y);
    if (!piece || piece->whole) {
        return editError("sketch.break", "Nothing crosses this curve there, so there is nowhere to break it.");
    }
    std::vector<int> out {entityId};
    if (t->type == EntityType::Line) {
        const auto [ax, ay] = pointXY(t->p1);
        const auto [bx, by] = pointXY(t->p2);
        std::vector<double> cuts;
        for (double u : {piece->from, piece->to}) {
            if (u > 1e-9 && u < 1 - 1e-9) {
                cuts.push_back(u);
            }
        }
        std::vector<int> hv;
        for (auto& [cid, c] : constraints_) {
            if ((c.type == ConstraintType::Horizontal || c.type == ConstraintType::Vertical) && c.a == entityId && !c.b) {
                hv.push_back(int(c.type));
            }
        }
        dropSizeConstraints(entityId);
        const int end = t->p2;
        int prev = 0;
        for (double u : cuts) {
            const int p = attachedPoint(entityId, ax + (bx - ax) * u, ay + (by - ay) * u);
            if (!prev) {
                entities_.at(entityId).p2 = p;
            }
            else {
                out.push_back(addLine(prev, p));
            }
            prev = p;
        }
        out.push_back(addLine(prev, end));
        for (size_t i = 1; i < out.size(); ++i) {
            for (int k : hv) {
                addConstraint(ConstraintType(k), out[i]);
            }
        }
        return out;
    }
    const auto [cx, cy] = pointXY(t->center);
    const double r = t->r;
    const int center = t->center;
    auto at = [&](double ang) { return std::pair {cx + r * std::cos(ang), cy + r * std::sin(ang)}; };
    // Pieces of one circle share their joint points (a shared point already says "same
    // place"; a coincident constraint or an equal radius on top would say it twice).
    if (t->type == EntityType::Circle) {
        // the circle keeps the rest; the clicked piece becomes a second arc
        const double s0 = piece->to, s1 = piece->from;
        auto [sx, sy] = at(s0);
        auto [ex, ey] = at(s1);
        const int p1 = attachedPoint(entityId, sx, sy), p2 = attachedPoint(entityId, ex, ey);
        SketchEntity& e = entities_.at(entityId);
        e.type = EntityType::Arc;
        e.p1 = p1;
        e.p2 = p2;
        e.a0 = s0;
        e.a1 = s1 <= s0 ? s1 + kTau : s1;
        for (auto& [cid, c] : constraints_) {
            if (c.type == ConstraintType::Diameter && c.a == entityId) {
                c.type = ConstraintType::Radius;
                c.expr = "(" + c.expr + ") / 2";
            }
        }
        // Closing the loop on p1 as well would over-define the second arc by one, so its
        // end is its own point, held on the curve that crosses there.
        const double f = piece->from, to = piece->to <= piece->from ? piece->to + kTau : piece->to;
        const int pe = addPoint(sx, sy, true);
        out.push_back(addArcOn(center, p2, pe, r, f, to));
        if (const int across = crossingCurveOf(p1)) {
            addConstraint(ConstraintType::PointOnCurve, pe, across);
        }
        else {
            addConstraint(ConstraintType::Coincident, pe, p1);
        }
        return out;
    }
    // an arc: cut at the piece's ends that fall inside it
    const double a0 = t->a0, a1 = t->a1 <= t->a0 ? t->a1 + kTau : t->a1;
    std::vector<double> cuts;
    for (double v : {piece->from, piece->to}) {
        double w = a0 + wrap(v - a0);
        if (w > a0 + 1e-9 && w < a1 - 1e-9) {
            cuts.push_back(w);
        }
    }
    std::sort(cuts.begin(), cuts.end());
    const int end = t->p2;
    std::vector<int> joints;
    for (double c : cuts) {
        auto [qx, qy] = at(c);
        joints.push_back(attachedPoint(entityId, qx, qy));
    }
    SketchEntity& e = entities_.at(entityId);
    e.p2 = joints.front();
    e.a1 = cuts.front();
    for (size_t i = 0; i < cuts.size(); ++i) {
        const bool last = i + 1 == cuts.size();
        out.push_back(addArcOn(center, joints[i], last ? end : joints[i + 1], r, cuts[i], last ? a1 : cuts[i + 1]));
    }
    return out;
}

// The curve a point was attached to by attachedPoint (0 if none).
int Sketch::crossingCurveOf(int pid) const
{
    for (auto& [cid, c] : constraints_) {
        if (c.type == ConstraintType::PointOnCurve && c.a == pid) {
            return c.b;
        }
        if (c.type == ConstraintType::Coincident && (c.a == pid || c.b == pid)) {
            const int q = c.a == pid ? c.b : c.a;
            for (auto& [id, e] : entities_) {
                if (e.type != EntityType::Point && (e.p1 == q || e.p2 == q)) {
                    return id;
                }
            }
        }
    }
    return 0;
}

// ---------------------------------------------------------------- mirror

Result<std::vector<int>> Sketch::mirror(const std::vector<int>& ids, int axisLine)
{
    const SketchEntity* ax = entity(axisLine);
    if (!ax || ax->type != EntityType::Line) {
        return editError("sketch.mirror", "Pick a line to mirror across.");
    }
    const auto [x0, y0] = pointXY(ax->p1);
    const auto [x1, y1] = pointXY(ax->p2);
    const double L = std::hypot(x1 - x0, y1 - y0);
    if (L < 1e-12) {
        return editError("sketch.mirror", "The mirror line has no length.");
    }
    const double ux = (x1 - x0) / L, uy = (y1 - y0) / L;
    const double theta = std::atan2(uy, ux);
    auto onAxis = [&](double px, double py) { return std::abs((px - x0) * uy - (py - y0) * ux) < 1e-9; };
    std::map<int, int> image;  // point id -> its mirror point (itself when on the line)
    auto mirrorPoint = [&](int pid) {
        if (auto it = image.find(pid); it != image.end()) {
            return it->second;
        }
        auto [px, py] = pointXY(pid);
        int m = pid;
        if (!onAxis(px, py)) {
            auto [mx, my] = reflect(px, py, x0, y0, ux, uy);
            m = addPoint(mx, my, entity(pid)->internal);
            addConstraint(ConstraintType::Symmetric, pid, m, axisLine);
        }
        image[pid] = m;
        return m;
    };
    // ties a new curve's own point to the original's: symmetric, or the same spot on the line
    auto tie = [&](int orig, int own) {
        auto [px, py] = pointXY(orig);
        if (onAxis(px, py)) {
            addConstraint(ConstraintType::Coincident, own, orig);
        }
        else {
            addConstraint(ConstraintType::Symmetric, orig, own, axisLine);
        }
    };
    std::vector<int> out;
    std::vector<int> todo;
    for (int id : ids) {
        if (id != axisLine && entity(id) && std::find(todo.begin(), todo.end(), id) == todo.end()) {
            todo.push_back(id);
        }
    }
    if (todo.empty()) {
        return editError("sketch.mirror", "Select the curves to mirror first (not the mirror line itself).");
    }
    for (int id : todo) {
        const SketchEntity e = *entity(id);
        switch (e.type) {
            case EntityType::Point: {
                const int m = mirrorPoint(id);
                if (m != id) {
                    out.push_back(m);
                }
                break;
            }
            case EntityType::Line: {
                const int a = mirrorPoint(e.p1), b = mirrorPoint(e.p2);
                if (a == e.p1 && b == e.p2) {
                    break;  // lies on the mirror line
                }
                const int l = addLine(a, b);
                entities_.at(l).construction = e.construction;
                out.push_back(l);
                break;
            }
            case EntityType::Circle: {
                auto [cx, cy] = pointXY(e.center);
                auto [mx, my] = reflect(cx, cy, x0, y0, ux, uy);
                const int c = addCircle(mx, my, e.r);
                entities_.at(c).construction = e.construction;
                tie(e.center, entities_.at(c).center);
                addConstraint(ConstraintType::Equal, id, c);
                out.push_back(c);
                break;
            }
            case EntityType::Arc: {
                // a mirror image runs the other way round: its start is the image of the end
                auto [cx, cy] = pointXY(e.center);
                auto [mx, my] = reflect(cx, cy, x0, y0, ux, uy);
                const double a0 = 2 * theta - e.a1, a1 = 2 * theta - e.a0;
                const int c = addArc(mx, my, e.r, a0, a0 + wrap(a1 - a0));
                const SketchEntity m = entities_.at(c);
                entities_.at(c).construction = e.construction;
                // both ends mirrored and the radius equal fix the copy (the centre follows)
                tie(e.p2, m.p1);
                tie(e.p1, m.p2);
                addConstraint(ConstraintType::Equal, id, c);
                out.push_back(c);
                break;
            }
            case EntityType::Ellipse: {
                // centre and axis end mirrored; the minor radius is copied (not linked)
                SketchEntity m = e;
                m.id = nextId_++;
                m.center = mirrorPoint(e.center);
                m.p1 = mirrorPoint(e.p1);
                entities_[m.id] = m;
                out.push_back(m.id);
                break;
            }
            case EntityType::Spline: {
                SketchEntity m = e;
                m.id = nextId_++;
                m.pts.clear();
                for (auto it = e.pts.rbegin(); it != e.pts.rend(); ++it) {  // keeps it running the same way round
                    m.pts.push_back(mirrorPoint(*it));
                }
                entities_[m.id] = m;
                out.push_back(m.id);
                break;
            }
        }
    }
    syncCurvePoints();
    return out;
}

// ---------------------------------------------------------------- move, copy, patterns

namespace
{
struct Turn
{
    double c = 1, s = 0, dx = 0, dy = 0, cx = 0, cy = 0, angle = 0;
    std::pair<double, double> operator()(double x, double y) const
    {
        const double rx = x - cx, ry = y - cy;
        return {cx + c * rx - s * ry + dx, cy + s * rx + c * ry + dy};
    }
};

Turn makeTurn(double dx, double dy, double angle, double cx, double cy)
{
    Turn t;
    t.c = std::cos(angle);
    t.s = std::sin(angle);
    t.dx = dx;
    t.dy = dy;
    t.cx = cx;
    t.cy = cy;
    t.angle = angle;
    return t;
}

// The selected curves plus the points they use, in a stable order.
std::vector<int> withPoints(const Sketch& s, const std::vector<int>& ids)
{
    std::vector<int> out;
    auto push = [&](int id) {
        if (id && std::find(out.begin(), out.end(), id) == out.end()) {
            out.push_back(id);
        }
    };
    for (int id : ids) {
        if (const SketchEntity* e = s.entity(id)) {
            push(id);
            if (e->type != EntityType::Point) {
                push(e->p1);
                push(e->p2);
                push(e->center);
                for (int pid : e->pts) {
                    push(pid);
                }
            }
        }
    }
    return out;
}
}  // namespace

Result<std::vector<int>> Sketch::copyCurves(const std::vector<int>& ids, double dx, double dy, double angle,
                                            double cx, double cy)
{
    const Turn T = makeTurn(dx, dy, angle, cx, cy);
    std::map<int, int> map;  // old id -> new id (points and curves)
    auto pointFor = [&](int pid) {
        if (auto it = map.find(pid); it != map.end()) {
            return it->second;
        }
        auto [px, py] = pointXY(pid);
        auto [nx, ny] = T(px, py);
        const SketchEntity* p = entity(pid);
        const int n = addPoint(nx, ny, p && p->internal);
        map[pid] = n;
        return n;
    };
    std::vector<int> curves;
    for (int id : ids) {
        const SketchEntity* e = entity(id);
        if (e && std::find(curves.begin(), curves.end(), id) == curves.end()) {
            curves.push_back(id);
        }
    }
    if (curves.empty()) {
        return editError("sketch.copy", "Select the curves to copy first.");
    }
    std::vector<int> out;
    for (int id : curves) {
        const SketchEntity e = *entity(id);
        int n = 0;
        switch (e.type) {
            case EntityType::Point:
                n = pointFor(id);
                break;
            case EntityType::Line:
                n = addLine(pointFor(e.p1), pointFor(e.p2));
                break;
            case EntityType::Circle: {
                auto [px, py] = pointXY(e.center);
                auto [nx, ny] = T(px, py);
                n = addCircle(nx, ny, e.r);
                if (auto it = map.find(e.center); it != map.end()) {  // a centre shared with another copied curve
                    const int own = entities_.at(n).center;
                    entities_.at(n).center = it->second;
                    dropUnusedPoint(own);
                }
                else {
                    map[e.center] = entities_.at(n).center;
                }
                break;
            }
            case EntityType::Arc:
                n = addArcOn(pointFor(e.center), pointFor(e.p1), pointFor(e.p2), e.r, e.a0 + angle, e.a1 + angle);
                break;
            case EntityType::Ellipse:
            case EntityType::Spline: {
                SketchEntity c = e;
                c.id = n = nextId_++;
                if (e.type == EntityType::Ellipse) {
                    c.center = pointFor(e.center);
                    c.p1 = pointFor(e.p1);
                }
                c.pts.clear();
                for (int pid : e.pts) {
                    c.pts.push_back(pointFor(pid));
                }
                entities_[n] = c;
                break;
            }
        }
        entities_.at(n).construction = e.construction;
        map[id] = n;
        out.push_back(n);
    }
    // constraints wholly among the copied items keep the copy's shape
    const double quarter = std::fmod(std::abs(angle) / (std::numbers::pi / 2), 2.0);
    const bool keepsAxes = std::abs(quarter) < 1e-9 || std::abs(quarter - 2) < 1e-9;  // 0° or 180°
    const bool swapsAxes = std::abs(quarter - 1) < 1e-9;                             // 90° or 270°
    const bool turned = std::abs(wrap(angle)) > 1e-9 && std::abs(wrap(angle) - kTau) > 1e-9;
    for (auto [cid, c] : std::map<int, SketchConstraint>(constraints_)) {
        auto mapped = [&](int id) { return id == 0 || map.count(id); };
        if (!mapped(c.a) || !mapped(c.b) || !mapped(c.c) || !c.a) {
            continue;
        }
        ConstraintType type = c.type;
        switch (c.type) {
            case ConstraintType::Fix:
                continue;  // a copy is never pinned where the original is
            case ConstraintType::Horizontal:
            case ConstraintType::Vertical:
                if (!keepsAxes && !swapsAxes) {
                    continue;
                }
                if (swapsAxes) {
                    type = c.type == ConstraintType::Horizontal ? ConstraintType::Vertical : ConstraintType::Horizontal;
                }
                break;
            case ConstraintType::DistanceX:
            case ConstraintType::DistanceY:
                if (turned || !c.b) {
                    continue;  // measured along the sketch axes, or from the origin
                }
                break;
            default:
                break;
        }
        const int k = addConstraint(type, map[c.a], c.b ? map[c.b] : 0, c.c ? map[c.c] : 0, c.expr);
        constraints_.at(k).driving = c.driving;
        constraints_.at(k).automatic = c.automatic;
    }
    syncCurvePoints();
    return out;
}

Result<bool> Sketch::moveCurves(const std::vector<int>& ids, double dx, double dy, double angle, double cx,
                                double cy)
{
    const Turn T = makeTurn(dx, dy, angle, cx, cy);
    const std::vector<int> items = withPoints(*this, ids);
    if (items.empty()) {
        return editError("sketch.move", "Select the curves to move first.");
    }
    for (int id : items) {
        SketchEntity& e = entities_.at(id);
        if (e.type == EntityType::Point) {
            std::tie(e.x, e.y) = T(e.x, e.y);
        }
        else if (e.type == EntityType::Arc) {
            e.a0 += angle;
            e.a1 += angle;
        }
    }
    for (auto& [cid, c] : constraints_) {
        if (c.type == ConstraintType::Fix && std::find(items.begin(), items.end(), c.a) != items.end()) {
            std::tie(c.fx, c.fy) = pointXY(c.a);  // pinned at its new place
        }
    }
    syncCurvePoints();
    return true;
}

Result<std::vector<int>> Sketch::patternRect(const std::vector<int>& ids, int n1, double dx1, double dy1, int n2,
                                             double dx2, double dy2)
{
    if (n1 < 1 || n2 < 1 || n1 * n2 < 2) {
        return editError("sketch.pattern", "A grid copy needs at least two in all.");
    }
    if (n1 * n2 > 400) {
        return editError("sketch.pattern", "That is more than 400 copies. Use fewer rows or columns.");
    }
    std::vector<int> out;
    for (int j = 0; j < n2; ++j) {
        for (int i = 0; i < n1; ++i) {
            if (i == 0 && j == 0) {
                continue;
            }
            auto r = copyCurves(ids, i * dx1 + j * dx2, i * dy1 + j * dy2);
            if (!r) {
                return r.error();
            }
            out.insert(out.end(), r->begin(), r->end());
        }
    }
    return out;
}

Result<std::vector<int>> Sketch::patternCircular(const std::vector<int>& ids, double cx, double cy, int n,
                                                 double total)
{
    if (n < 2 || n > 400) {
        return editError("sketch.pattern", "A ring copy needs 2 to 400 in all.");
    }
    if (std::abs(total) < 1e-9) {
        return editError("sketch.pattern", "The angle to spread the copies over must not be zero.");
    }
    const bool full = std::abs(std::abs(total) - kTau) < 1e-9;
    const double step = total / (full ? n : n - 1);
    std::vector<int> out;
    for (int k = 1; k < n; ++k) {
        auto r = copyCurves(ids, 0, 0, step * k, cx, cy);
        if (!r) {
            return r.error();
        }
        out.insert(out.end(), r->begin(), r->end());
    }
    return out;
}

}  // namespace jig
