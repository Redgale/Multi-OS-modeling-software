// SPDX-License-Identifier: GPL-3.0-or-later
#include "jig/sketch.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <tuple>
#include <memory>
#include <numbers>
#include <set>

#include <planegcs/GCS.h>

namespace jig
{
namespace
{

struct TypeName
{
    ConstraintType t;
    const char* n;
};
constexpr TypeName kTypeNames[] = {
    {ConstraintType::Coincident, "coincident"},
    {ConstraintType::PointOnCurve, "point_on_curve"},
    {ConstraintType::Horizontal, "horizontal"},
    {ConstraintType::Vertical, "vertical"},
    {ConstraintType::Parallel, "parallel"},
    {ConstraintType::Perpendicular, "perpendicular"},
    {ConstraintType::Equal, "equal"},
    {ConstraintType::Tangent, "tangent"},
    {ConstraintType::Midpoint, "midpoint"},
    {ConstraintType::Symmetric, "symmetric"},
    {ConstraintType::Fix, "fix"},
    {ConstraintType::Distance, "distance"},
    {ConstraintType::DistanceX, "distance_x"},
    {ConstraintType::DistanceY, "distance_y"},
    {ConstraintType::Radius, "radius"},
    {ConstraintType::Diameter, "diameter"},
    {ConstraintType::Angle, "angle"},
};

const char* entityTypeName(EntityType t)
{
    switch (t) {
        case EntityType::Point:
            return "point";
        case EntityType::Line:
            return "line";
        case EntityType::Circle:
            return "circle";
        case EntityType::Arc:
            return "arc";
    }
    return "point";
}

std::optional<EntityType> entityTypeFromString(std::string_view s)
{
    if (s == "point") {
        return EntityType::Point;
    }
    if (s == "line") {
        return EntityType::Line;
    }
    if (s == "circle") {
        return EntityType::Circle;
    }
    if (s == "arc") {
        return EntityType::Arc;
    }
    return std::nullopt;
}

bool isDimensional(ConstraintType t)
{
    switch (t) {
        case ConstraintType::Distance:
        case ConstraintType::DistanceX:
        case ConstraintType::DistanceY:
        case ConstraintType::Radius:
        case ConstraintType::Diameter:
        case ConstraintType::Angle:
            return true;
        default:
            return false;
    }
}

}  // namespace

const char* toString(ConstraintType t)
{
    for (const auto& tn : kTypeNames) {
        if (tn.t == t) {
            return tn.n;
        }
    }
    return "unknown";
}

std::optional<ConstraintType> constraintTypeFromString(std::string_view s)
{
    for (const auto& tn : kTypeNames) {
        if (s == tn.n) {
            return tn.t;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------- geometry

int Sketch::addPoint(double x, double y, bool internal)
{
    SketchEntity e;
    e.id = nextId_++;
    e.type = EntityType::Point;
    e.x = x;
    e.y = y;
    e.internal = internal;
    entities_[e.id] = e;
    return e.id;
}

int Sketch::addLine(int p1, int p2)
{
    SketchEntity e;
    e.id = nextId_++;
    e.type = EntityType::Line;
    e.p1 = p1;
    e.p2 = p2;
    entities_[e.id] = e;
    return e.id;
}

int Sketch::addLine(double x1, double y1, double x2, double y2)
{
    int a = addPoint(x1, y1, true);
    int b = addPoint(x2, y2, true);
    return addLine(a, b);
}

int Sketch::addCircle(double cx, double cy, double r)
{
    int c = addPoint(cx, cy, true);
    SketchEntity e;
    e.id = nextId_++;
    e.type = EntityType::Circle;
    e.center = c;
    e.r = r;
    entities_[e.id] = e;
    return e.id;
}

int Sketch::addArc(double cx, double cy, double r, double a0, double a1)
{
    int c = addPoint(cx, cy, true);
    int s = addPoint(cx + r * std::cos(a0), cy + r * std::sin(a0), true);
    int t = addPoint(cx + r * std::cos(a1), cy + r * std::sin(a1), true);
    SketchEntity e;
    e.id = nextId_++;
    e.type = EntityType::Arc;
    e.center = c;
    e.p1 = s;
    e.p2 = t;
    e.r = r;
    e.a0 = a0;
    e.a1 = a1;
    entities_[e.id] = e;
    return e.id;
}

int Sketch::addArcOn(int center, int p1, int p2, double r, double a0, double a1)
{
    SketchEntity e;
    e.id = nextId_++;
    e.type = EntityType::Arc;
    e.center = center;
    e.p1 = p1;
    e.p2 = p2;
    e.r = r;
    e.a0 = a0;
    e.a1 = a1;
    entities_[e.id] = e;
    return e.id;
}

// ---------------------------------------------------------------- trim

namespace
{
constexpr double kTau = 2 * std::numbers::pi;

double wrap(double a)  // into [0, 2pi)
{
    a = std::fmod(a, kTau);
    return a < 0 ? a + kTau : a;
}

struct Cut
{
    double param;   // on the trimmed curve
    double x, y;
    int entity;     // the curve that crosses it
    int point = 0;  // an existing point there, if any
};
}  // namespace

std::optional<Sketch::TrimPiece> Sketch::trimPiece(int entityId, double x, double y) const
{
    const SketchEntity* t = entity(entityId);
    if (!t || t->type == EntityType::Point) {
        return std::nullopt;
    }
    const double eps = 1e-7;
    // geometry of a curve as a line segment or a circle with an optional angle range
    struct Geo
    {
        bool line = false;
        double ax = 0, ay = 0, bx = 0, by = 0;  // line
        double cx = 0, cy = 0, r = 0, a0 = 0, span = kTau;  // circle / arc
    };
    auto geoOf = [&](const SketchEntity& e) {
        Geo g;
        if (e.type == EntityType::Line) {
            g.line = true;
            std::tie(g.ax, g.ay) = pointXY(e.p1);
            std::tie(g.bx, g.by) = pointXY(e.p2);
        }
        else {
            std::tie(g.cx, g.cy) = pointXY(e.center);
            g.r = e.r;
            if (e.type == EntityType::Arc) {
                g.a0 = wrap(e.a0);
                g.span = wrap(e.a1 - e.a0);
                if (g.span < eps) {
                    g.span = kTau;
                }
            }
        }
        return g;
    };
    auto onArc = [&](const Geo& g, double px, double py) {
        if (g.line) {
            const double dx = g.bx - g.ax, dy = g.by - g.ay, L2 = dx * dx + dy * dy;
            const double u = ((px - g.ax) * dx + (py - g.ay) * dy) / L2;
            return u > -1e-9 && u < 1 + 1e-9;
        }
        return g.span >= kTau - eps || wrap(std::atan2(py - g.cy, px - g.cx) - g.a0) <= g.span + 1e-9;
    };
    // where on the trimmed curve a point lies: t for lines, angle offset from a0 otherwise
    const Geo T = geoOf(*t);
    auto paramOf = [&](double px, double py) {
        if (T.line) {
            const double dx = T.bx - T.ax, dy = T.by - T.ay;
            return ((px - T.ax) * dx + (py - T.ay) * dy) / (dx * dx + dy * dy);
        }
        return wrap(std::atan2(py - T.cy, px - T.cx) - T.a0);
    };
    std::vector<double> cuts;
    for (auto& [id, e] : entities_) {
        if (id == entityId || e.type == EntityType::Point || e.construction) {
            continue;
        }
        const Geo C = geoOf(e);
        std::vector<std::pair<double, double>> pts;
        if (T.line && C.line) {
            const double rx = T.bx - T.ax, ry = T.by - T.ay, sx = C.bx - C.ax, sy = C.by - C.ay;
            const double den = rx * sy - ry * sx;
            if (std::abs(den) > 1e-12) {
                const double u = ((C.ax - T.ax) * sy - (C.ay - T.ay) * sx) / den;
                pts.emplace_back(T.ax + rx * u, T.ay + ry * u);
            }
        }
        else if (T.line != C.line) {
            const Geo& L = T.line ? T : C;
            const Geo& O = T.line ? C : T;
            const double dx = L.bx - L.ax, dy = L.by - L.ay;
            const double fx = L.ax - O.cx, fy = L.ay - O.cy;
            const double A = dx * dx + dy * dy, B = 2 * (fx * dx + fy * dy), Cc = fx * fx + fy * fy - O.r * O.r;
            const double disc = B * B - 4 * A * Cc;
            if (disc >= 0) {
                for (double sgn : {-1.0, 1.0}) {
                    const double u = (-B + sgn * std::sqrt(disc)) / (2 * A);
                    pts.emplace_back(L.ax + dx * u, L.ay + dy * u);
                }
            }
        }
        else {
            const double dx = C.cx - T.cx, dy = C.cy - T.cy, d = std::hypot(dx, dy);
            if (d > 1e-12 && d <= T.r + C.r + 1e-9 && d >= std::abs(T.r - C.r) - 1e-9) {
                const double a = (T.r * T.r - C.r * C.r + d * d) / (2 * d);
                const double h = std::sqrt(std::max(0.0, T.r * T.r - a * a));
                const double mx = T.cx + dx * a / d, my = T.cy + dy * a / d;
                pts.emplace_back(mx + h * dy / d, my - h * dx / d);
                pts.emplace_back(mx - h * dy / d, my + h * dx / d);
            }
        }
        for (auto [px, py] : pts) {
            if (!onArc(T, px, py) || !onArc(C, px, py)) {
                continue;
            }
            const double prm = paramOf(px, py);
            const double end = T.line ? 1.0 : T.span;
            if (prm > eps && prm < end - eps) {
                cuts.push_back(prm);
            }
        }
    }
    std::sort(cuts.begin(), cuts.end());
    TrimPiece piece;
    piece.entity = entityId;
    const double click = T.line ? std::clamp(paramOf(x, y), 0.0, 1.0) : paramOf(x, y);
    const bool closed = !T.line && T.span >= kTau - eps;
    if (closed) {
        if (cuts.size() < 2) {
            piece.whole = true;
            return piece;
        }
        // the gap between consecutive cuts (cyclic) that contains the click
        double from = cuts.back(), to = cuts.front();
        for (size_t i = 0; i + 1 < cuts.size(); ++i) {
            if (click >= cuts[i] && click <= cuts[i + 1]) {
                from = cuts[i];
                to = cuts[i + 1];
            }
        }
        piece.from = wrap(T.a0 + from);
        piece.to = wrap(T.a0 + to);
        return piece;
    }
    const double end = T.line ? 1.0 : T.span;
    double a = 0, b = end;
    for (double c : cuts) {
        if (c < click) {
            a = c;
        }
        else if (c > click) {
            b = c;
            break;
        }
    }
    piece.whole = a <= 0 && b >= end;
    piece.from = T.line ? a : T.a0 + a;
    piece.to = T.line ? b : T.a0 + b;
    return piece;
}

std::optional<std::pair<int, double>> Sketch::curveNear(double x, double y) const
{
    std::optional<std::pair<int, double>> best;
    for (auto& [id, e] : entities_) {
        if (e.type == EntityType::Point || e.construction) {
            continue;
        }
        double d = 1e300;
        if (e.type == EntityType::Line) {
            auto [ax, ay] = pointXY(e.p1);
            auto [bx, by] = pointXY(e.p2);
            const double dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
            const double u = L2 > 0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / L2, 0.0, 1.0) : 0.0;
            d = std::hypot(ax + dx * u - x, ay + dy * u - y);
        }
        else {
            auto [cx, cy] = pointXY(e.center);
            d = std::abs(std::hypot(x - cx, y - cy) - e.r);
            if (e.type == EntityType::Arc) {
                const double span = wrap(e.a1 - e.a0);
                if (span > 1e-9 && wrap(std::atan2(y - cy, x - cx) - e.a0) > span) {
                    auto [sx, sy] = pointXY(e.p1);
                    auto [tx, ty] = pointXY(e.p2);
                    d = std::min(std::hypot(x - sx, y - sy), std::hypot(x - tx, y - ty));
                }
            }
        }
        if (!best || d < best->second) {
            best = std::pair {id, d};
        }
    }
    return best;
}

Result<int> Sketch::filletCorner(double x, double y, double r, const std::string& radiusExpr)
{
    if (!(r > 1e-9)) {
        return makeError("sketch.fillet", "The corner radius must be larger than zero.");
    }
    // the corner: the line end nearest (x, y) that is shared with another line's end
    struct End
    {
        int line, point;  // point id of this end
        double px, py, ox, oy;  // the corner and the line's other end
    };
    std::vector<End> ends;
    for (auto& [id, e] : entities_) {
        if (e.type != EntityType::Line || e.construction) {
            continue;
        }
        auto [ax, ay] = pointXY(e.p1);
        auto [bx, by] = pointXY(e.p2);
        ends.push_back({id, e.p1, ax, ay, bx, by});
        ends.push_back({id, e.p2, bx, by, ax, ay});
    }
    std::optional<std::pair<End, End>> best;
    double bestD = 1e300;
    for (size_t i = 0; i < ends.size(); ++i) {
        for (size_t j = i + 1; j < ends.size(); ++j) {
            const End &a = ends[i], &b = ends[j];
            if (a.line == b.line || std::hypot(a.px - b.px, a.py - b.py) > 1e-6) {
                continue;
            }
            const double d = std::hypot(a.px - x, a.py - y);
            if (d < bestD) {
                bestD = d;
                best = std::pair {a, b};
            }
        }
    }
    if (!best) {
        return makeError("sketch.fillet", "There is no corner where two lines meet here.",
                         {"Click near the point where two lines join."});
    }
    const End a = best->first, b = best->second;
    const double px = a.px, py = a.py;
    double u1x = a.ox - px, u1y = a.oy - py, u2x = b.ox - px, u2y = b.oy - py;
    const double L1 = std::hypot(u1x, u1y), L2 = std::hypot(u2x, u2y);
    u1x /= L1, u1y /= L1, u2x /= L2, u2y /= L2;
    const double angle = std::acos(std::clamp(u1x * u2x + u1y * u2y, -1.0, 1.0));
    if (angle < 1e-6 || angle > std::numbers::pi - 1e-6) {
        return makeError("sketch.fillet", "These lines are in a straight line: there is no corner to round.");
    }
    const double d = r / std::tan(angle / 2);  // corner to tangent points
    if (d >= L1 - 1e-9 || d >= L2 - 1e-9) {
        return makeError("sketch.fillet", "A " + std::to_string(r) + " mm round does not fit this corner.",
                         {"Use a smaller radius: the round must fit on both lines."});
    }
    const double t1x = px + u1x * d, t1y = py + u1y * d, t2x = px + u2x * d, t2y = py + u2y * d;
    double bx = u1x + u2x, by = u1y + u2y;
    const double bl = std::hypot(bx, by);
    bx /= bl, by /= bl;
    const double cx = px + bx * r / std::sin(angle / 2), cy = py + by * r / std::sin(angle / 2);
    const int T1 = addPoint(t1x, t1y, true), T2 = addPoint(t2x, t2y, true), C = addPoint(cx, cy, true);
    // re-end the lines at the tangent points
    for (auto [e, tp] : {std::pair {a, T1}, std::pair {b, T2}}) {
        SketchEntity& l = entities_.at(e.line);
        (l.p1 == e.point ? l.p1 : l.p2) = tp;
    }
    // counter-clockwise from one tangent point to the other, the short way round
    double a1 = std::atan2(t1y - cy, t1x - cx), a2 = std::atan2(t2y - cy, t2x - cx);
    const double sweep = std::fmod(a2 - a1 + 4 * std::numbers::pi, 2 * std::numbers::pi);
    const bool fromT1 = sweep < std::numbers::pi;
    const int arc = fromT1 ? addArcOn(C, T1, T2, r, a1, a1 + sweep)
                           : addArcOn(C, T2, T1, r, a2, a2 + (2 * std::numbers::pi - sweep));
    // tangency: each line perpendicular to the radius through its tangent point
    for (auto [line, tp] : {std::pair {a.line, T1}, std::pair {b.line, T2}}) {
        const int radial = addLine(C, tp);
        setConstruction(radial, true);
        addConstraint(ConstraintType::Perpendicular, line, radial);
    }
    addConstraint(ConstraintType::Radius, arc, 0, 0, radiusExpr);
    // The sharp corner stays as a hidden "virtual" point on both lines, so the part's
    // overall sizes still hold: a line's length dimension now runs from its far end to
    // that corner.
    if (b.point != a.point) {
        addConstraint(ConstraintType::Coincident, a.point, b.point);
    }
    addConstraint(ConstraintType::PointOnCurve, a.point, a.line);
    addConstraint(ConstraintType::PointOnCurve, a.point, b.line);
    for (auto& [cid, c] : constraints_) {
        if (c.type == ConstraintType::Distance && !c.b && (c.a == a.line || c.a == b.line)) {
            const SketchEntity& l = entities_.at(c.a);
            const int far = (c.a == a.line) ? (l.p1 == T1 ? l.p2 : l.p1) : (l.p1 == T2 ? l.p2 : l.p1);
            c.a = far;
            c.b = a.point;
        }
    }
    return arc;
}

std::vector<int> Sketch::chainOf(int curve) const
{
    const SketchEntity* start = entity(curve);
    if (!start || start->type == EntityType::Point) {
        return {};
    }
    auto ends = [&](const SketchEntity& e) {
        std::vector<std::pair<double, double>> out;
        if (e.type == EntityType::Line || e.type == EntityType::Arc) {
            out.push_back(pointXY(e.p1));
            out.push_back(pointXY(e.p2));
        }
        return out;
    };
    std::vector<int> chain {curve};
    std::set<int> seen {curve};
    for (size_t i = 0; i < chain.size(); ++i) {
        const auto mine = ends(entities_.at(chain[i]));
        for (auto& [id, e] : entities_) {
            if (seen.count(id) || e.type == EntityType::Point || e.type == EntityType::Circle || e.construction) {
                continue;
            }
            bool touches = false;
            for (auto [x1, y1] : mine) {
                for (auto [x2, y2] : ends(e)) {
                    touches = touches || std::hypot(x1 - x2, y1 - y2) < 1e-6;
                }
            }
            if (touches) {
                seen.insert(id);
                chain.push_back(id);
            }
        }
    }
    return chain;
}

void Sketch::dropUnusedPoint(int pid)
{
    const SketchEntity* p = entity(pid);
    if (!p || !p->internal) {
        return;
    }
    for (auto& [id, e] : entities_) {
        if (e.type != EntityType::Point && (e.p1 == pid || e.p2 == pid || e.center == pid)) {
            return;
        }
    }
    for (auto& [id, t] : texts_) {
        if (t.anchor == pid) {
            return;
        }
    }
    entities_.erase(pid);
    std::erase_if(constraints_, [&](const auto& kv) {
        const auto& c = kv.second;
        return c.a == pid || c.b == pid || c.c == pid;
    });
}

bool Sketch::trim(int entityId, double x, double y)
{
    auto piece = trimPiece(entityId, x, y);
    if (!piece) {
        return false;
    }
    if (piece->whole) {
        return remove(entityId);
    }
    SketchEntity& t = entities_.at(entityId);
    const bool line = t.type == EntityType::Line;
    // the curve's own point where a cut lands: attach it to whatever crosses there
    auto pointAt = [&](double px, double py) {
        const int pid = addPoint(px, py, true);
        // the crossing curve(s): every other curve passing through (px, py)
        for (auto& [id, e] : entities_) {
            if (id == entityId || e.type == EntityType::Point || e.construction || id == pid) {
                continue;
            }
            // an existing endpoint there: share it
            for (int q : {e.p1, e.p2}) {
                if (q && q != pid) {
                    auto [qx, qy] = pointXY(q);
                    if (std::hypot(qx - px, qy - py) < 1e-6) {
                        addConstraint(ConstraintType::Coincident, pid, q);
                        return pid;
                    }
                }
            }
        }
        for (auto& [id, e] : entities_) {
            if (id == entityId || e.type == EntityType::Point || e.construction || id == pid) {
                continue;
            }
            double dist = 1e300;
            if (e.type == EntityType::Line) {
                auto [ax, ay] = pointXY(e.p1);
                auto [bx, by] = pointXY(e.p2);
                const double dx = bx - ax, dy = by - ay;
                const double u = std::clamp(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy), 0.0, 1.0);
                dist = std::hypot(ax + dx * u - px, ay + dy * u - py);
            }
            else {
                auto [cx, cy] = pointXY(e.center);
                dist = std::abs(std::hypot(px - cx, py - cy) - e.r);
            }
            if (dist < 1e-6) {
                addConstraint(ConstraintType::PointOnCurve, pid, id);
                return pid;
            }
        }
        return pid;
    };
    // dimensions that measured the whole curve no longer apply
    // a line's length, midpoint and equal-length constraints no longer apply
    auto dropSizeConstraints = [&] {
        std::erase_if(constraints_, [&](const auto& kv) {
            const auto& c = kv.second;
            if (c.type == ConstraintType::Distance && c.a == entityId && !c.b) {
                return true;
            }
            return (c.type == ConstraintType::Midpoint || c.type == ConstraintType::Equal) &&
                   (c.a == entityId || c.b == entityId);
        });
    };
    if (line) {
        const auto [ax, ay] = pointXY(t.p1);
        const auto [bx, by] = pointXY(t.p2);
        auto at = [&](double u) { return std::pair {ax + (bx - ax) * u, ay + (by - ay) * u}; };
        const int oldP1 = t.p1, oldP2 = t.p2;
        dropSizeConstraints();
        if (piece->from <= 0) {  // the start goes
            auto [px, py] = at(piece->to);
            const int np = pointAt(px, py);
            entities_.at(entityId).p1 = np;
            dropUnusedPoint(oldP1);
        }
        else if (piece->to >= 1) {  // the end goes
            auto [px, py] = at(piece->from);
            const int np = pointAt(px, py);
            entities_.at(entityId).p2 = np;
            dropUnusedPoint(oldP2);
        }
        else {  // the middle goes: two lines
            auto [px, py] = at(piece->from);
            auto [qx, qy] = at(piece->to);
            const int a = pointAt(px, py);
            const int b = pointAt(qx, qy);
            entities_.at(entityId).p2 = a;
            const int second = addLine(b, oldP2);
            for (auto [cid, c] : std::map<int, SketchConstraint>(constraints_)) {
                if ((c.type == ConstraintType::Horizontal || c.type == ConstraintType::Vertical) && c.a == entityId && !c.b) {
                    addConstraint(c.type, second);
                }
            }
        }
        return true;
    }
    // circles and arcs
    const auto [cx, cy] = pointXY(t.center);
    const double r = t.r;
    auto at = [&](double ang) { return std::pair {cx + r * std::cos(ang), cy + r * std::sin(ang)}; };
    if (t.type == EntityType::Circle) {
        // what is left runs counter-clockwise from the piece's end round to its start
        const double s0 = piece->to, s1 = piece->from;
        auto [sx, sy] = at(s0);
        auto [ex, ey] = at(s1);
        const int p1 = pointAt(sx, sy), p2 = pointAt(ex, ey);
        SketchEntity& e = entities_.at(entityId);
        e.type = EntityType::Arc;
        e.p1 = p1;
        e.p2 = p2;
        e.a0 = s0;
        e.a1 = s1 <= s0 ? s1 + kTau : s1;
        for (auto& [cid, c] : constraints_) {
            if (c.type == ConstraintType::Diameter && c.a == entityId) {
                c.type = ConstraintType::Radius;  // arcs are sized by radius
                c.expr = "(" + c.expr + ") / 2";
            }
        }
        return true;
    }
    // arc: angles from a0
    const double a0 = t.a0, a1 = t.a1 <= t.a0 ? t.a1 + kTau : t.a1;
    const int oldP1 = t.p1, oldP2 = t.p2;
    if (piece->from <= a0 + 1e-9) {
        auto [px, py] = at(piece->to);
        const int np = pointAt(px, py);
        SketchEntity& e = entities_.at(entityId);
        e.p1 = np;
        e.a0 = piece->to;
        dropUnusedPoint(oldP1);
    }
    else if (piece->to >= a1 - 1e-9) {
        auto [px, py] = at(piece->from);
        const int np = pointAt(px, py);
        SketchEntity& e = entities_.at(entityId);
        e.p2 = np;
        e.a1 = piece->from;
        dropUnusedPoint(oldP2);
    }
    else {
        auto [px, py] = at(piece->from);
        auto [qx, qy] = at(piece->to);
        const int a = pointAt(px, py), b = pointAt(qx, qy);
        SketchEntity& e = entities_.at(entityId);
        e.p2 = a;
        e.a1 = piece->from;
        const int second = addArcOn(e.center, b, oldP2, r, piece->to, a1);
        addConstraint(ConstraintType::Equal, entityId, second);  // the two pieces keep one radius
    }
    return true;
}

Sketch::SlotIds Sketch::addSlot(double x1, double y1, double x2, double y2, double r)
{
    const double ang = std::atan2(y2 - y1, x2 - x1);
    constexpr double h = std::numbers::pi / 2;
    SlotIds ids;
    ids.arc1 = addArc(x2, y2, r, ang - h, ang + h);      // far end
    ids.arc2 = addArc(x1, y1, r, ang + h, ang + 3 * h);  // near end
    const SketchEntity A1 = entities_.at(ids.arc1), A2 = entities_.at(ids.arc2);
    auto [sx1, sy1] = pointXY(A1.p2);
    auto [ex1, ey1] = pointXY(A2.p1);
    auto [sx2, sy2] = pointXY(A2.p2);
    auto [ex2, ey2] = pointXY(A1.p1);
    ids.side1 = addLine(sx1, sy1, ex1, ey1);
    ids.side2 = addLine(sx2, sy2, ex2, ey2);
    const SketchEntity T = entities_.at(ids.side1), B = entities_.at(ids.side2);
    addConstraint(ConstraintType::Coincident, T.p1, A1.p2);
    addConstraint(ConstraintType::Coincident, T.p2, A2.p1);
    addConstraint(ConstraintType::Coincident, B.p1, A2.p2);
    addConstraint(ConstraintType::Coincident, B.p2, A1.p1);
    // Tangency at a shared endpoint is expressed as "side line perpendicular to the
    // radius through that endpoint". A distance-style tangent plus coincidence is
    // rank-deficient at the solution and would be reported as redundant.
    for (auto [line, arcCenter, endPt] : {std::tuple {ids.side1, A1.center, A1.p2}, std::tuple {ids.side1, A2.center, A2.p1},
                                          std::tuple {ids.side2, A2.center, A2.p2}, std::tuple {ids.side2, A1.center, A1.p1}}) {
        const int radial = addLine(arcCenter, endPt);
        setConstruction(radial, true);
        addConstraint(ConstraintType::Perpendicular, line, radial);
    }
    addConstraint(ConstraintType::Equal, ids.arc1, ids.arc2);
    return ids;
}

Sketch::PolygonIds Sketch::addPolygon(double cx, double cy, double r, int n, double a0)
{
    PolygonIds ids;
    ids.circle = addCircle(cx, cy, r);
    setConstruction(ids.circle, true);
    std::vector<int> corners;
    for (int k = 0; k < n; ++k) {
        const double a = a0 + 2 * std::numbers::pi * k / n;
        corners.push_back(addPoint(cx + r * std::cos(a), cy + r * std::sin(a), true));
    }
    for (int k = 0; k < n; ++k) {
        ids.sides.push_back(addLine(corners[size_t(k)], corners[size_t((k + 1) % n)]));
        addConstraint(ConstraintType::PointOnCurve, corners[size_t(k)], ids.circle);
    }
    for (size_t k = 1; k < ids.sides.size(); ++k) {
        addConstraint(ConstraintType::Equal, ids.sides[0], ids.sides[k]);
    }
    return ids;
}

int Sketch::addText(double x, double y, std::string text, std::string heightExpr, double heightValue,
                    std::string font, double angle)
{
    SketchText t;
    t.anchor = addPoint(x, y);
    t.id = nextId_++;
    t.text = std::move(text);
    t.height = std::move(heightExpr);
    t.heightValue = heightValue;
    t.font = std::move(font);
    t.angle = angle;
    texts_[t.id] = t;
    return t.id;
}

SketchText* Sketch::text(int id)
{
    auto it = texts_.find(id);
    return it == texts_.end() ? nullptr : &it->second;
}

bool Sketch::remove(int entityId)
{
    if (auto t = texts_.find(entityId); t != texts_.end()) {
        const int anchor = t->second.anchor;
        texts_.erase(t);
        remove(anchor);
        return true;
    }
    std::erase_if(texts_, [&](const auto& kv) { return kv.second.anchor == entityId; });
    auto it = entities_.find(entityId);
    if (it == entities_.end()) {
        return false;
    }
    std::set<int> gone {entityId};
    const SketchEntity e = it->second;
    entities_.erase(it);
    // internal points not used by any other curve go too
    for (int pid : {e.p1, e.p2, e.center}) {
        if (!pid) {
            continue;
        }
        auto* p = entity(pid);
        if (!p || !p->internal) {
            continue;
        }
        bool used = false;
        for (auto& [id, o] : entities_) {
            if (o.p1 == pid || o.p2 == pid || o.center == pid) {
                used = true;
            }
        }
        if (!used) {
            entities_.erase(pid);
            gone.insert(pid);
        }
    }
    std::erase_if(constraints_, [&](const auto& kv) {
        const auto& c = kv.second;
        return gone.count(c.a) || gone.count(c.b) || gone.count(c.c);
    });
    return true;
}

void Sketch::setConstruction(int entityId, bool on)
{
    if (auto* e = entity(entityId)) {
        e->construction = on;
    }
}

int Sketch::addConstraint(ConstraintType type, int a, int b, int c, std::string expr)
{
    SketchConstraint k;
    k.id = nextId_++;
    k.type = type;
    k.a = a;
    k.b = b;
    k.c = c;
    k.expr = std::move(expr);
    if (type == ConstraintType::Fix) {
        auto [x, y] = pointXY(a);
        k.fx = x;
        k.fy = y;
    }
    constraints_[k.id] = k;
    return k.id;
}

bool Sketch::removeConstraint(int constraintId)
{
    return constraints_.erase(constraintId) > 0;
}

const SketchEntity* Sketch::entity(int id) const
{
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}

SketchEntity* Sketch::entity(int id)
{
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : &it->second;
}

std::pair<double, double> Sketch::pointXY(int pointId) const
{
    const auto* p = entity(pointId);
    return p ? std::pair {p->x, p->y} : std::pair {0.0, 0.0};
}

void Sketch::syncCurvePoints()
{
    for (auto& [id, e] : entities_) {
        if (e.type == EntityType::Arc) {
            auto [cx, cy] = pointXY(e.center);
            if (auto* s = entity(e.p1)) {
                s->x = cx + e.r * std::cos(e.a0);
                s->y = cy + e.r * std::sin(e.a0);
            }
            if (auto* t = entity(e.p2)) {
                t->x = cx + e.r * std::cos(e.a1);
                t->y = cy + e.r * std::sin(e.a1);
            }
        }
    }
}

// ---------------------------------------------------------------- solving

SolveReport Sketch::solve(const ParamLookup& lookup)
{
    for (auto& [id, t] : texts_) {
        auto q = evaluate(t.height, lookup, Dim::len());
        if (!q || !(q->value > 0)) {
            SolveReport r;
            r.state = SolveState::Failed;
            r.message = q ? "Text height must be larger than zero." : "Text height: " + q.error().message;
            return r;
        }
        t.heightValue = q->value;
    }
    return runSolver(lookup, nullptr, 0, 0);
}

SolveReport Sketch::drag(int pointId, double x, double y, const ParamLookup& lookup)
{
    return runSolver(lookup, &pointId, x, y);
}

SolveReport Sketch::runSolver(const ParamLookup& lookup, const int* dragPoint, double dx, double dy,
                              const std::vector<int>& skip)
{
    auto t0 = std::chrono::steady_clock::now();
    SolveReport rep;

    // Parameter storage must not reallocate: planegcs keeps raw pointers.
    size_t need = 2 + 2 * constraints_.size() + 4;
    for (auto& [id, e] : entities_) {
        need += e.type == EntityType::Point ? 2 : e.type == EntityType::Arc ? 3 : 1;
    }
    auto store = std::make_unique<std::vector<double>>();
    store->reserve(need);
    auto slot = [&](double v) -> double* {
        store->push_back(v);
        return &store->back();
    };

    std::map<int, GCS::Point> pts;
    std::map<int, GCS::Line> lines;
    std::map<int, GCS::Circle> circles;
    std::map<int, GCS::Arc> arcs;
    std::map<double*, int> paramOwner;  // parameter -> entity id (for free-geometry report)
    GCS::VEC_pD unknowns;

    for (auto& [id, e] : entities_) {
        if (e.type == EntityType::Point) {
            GCS::Point p;
            p.x = slot(e.x);
            p.y = slot(e.y);
            pts[id] = p;
            unknowns.push_back(p.x);
            unknowns.push_back(p.y);
            paramOwner[p.x] = id;
            paramOwner[p.y] = id;
        }
    }
    for (auto& [id, e] : entities_) {
        switch (e.type) {
            case EntityType::Line: {
                GCS::Line l;
                l.p1 = pts[e.p1];
                l.p2 = pts[e.p2];
                lines[id] = l;
                break;
            }
            case EntityType::Circle: {
                GCS::Circle c;
                c.center = pts[e.center];
                c.rad = slot(e.r);
                unknowns.push_back(c.rad);
                paramOwner[c.rad] = id;
                circles[id] = c;
                break;
            }
            case EntityType::Arc: {
                GCS::Arc a;
                a.center = pts[e.center];
                a.start = pts[e.p1];
                a.end = pts[e.p2];
                a.rad = slot(e.r);
                a.startAngle = slot(e.a0);
                a.endAngle = slot(e.a1);
                for (double* p : {a.rad, a.startAngle, a.endAngle}) {
                    unknowns.push_back(p);
                    paramOwner[p] = id;
                }
                arcs[id] = a;
                break;
            }
            default:
                break;
        }
    }
    // Endpoints of a curve belong to it for the free-geometry report.
    for (auto& [id, e] : entities_) {
        if (e.type == EntityType::Line || e.type == EntityType::Arc) {
            for (int pid : {e.p1, e.p2}) {
                if (pts.count(pid) && entity(pid)->internal) {
                    paramOwner[pts[pid].x] = id;
                    paramOwner[pts[pid].y] = id;
                }
            }
        }
    }

    GCS::System sys;
    for (auto& [id, a] : arcs) {
        sys.addConstraintArcRules(a, 0);
    }

    auto fail = [&](int cid, std::string msg) {
        rep.state = SolveState::Failed;
        rep.conflicting.push_back(cid);
        rep.message = std::move(msg);
        return rep;
    };
    auto isLine = [&](int id) { return lines.count(id) > 0; };
    auto isCircle = [&](int id) { return circles.count(id) > 0; };
    auto isArc = [&](int id) { return arcs.count(id) > 0; };
    auto isPoint = [&](int id) { return pts.count(id) > 0; };

    for (auto& [cid, k] : constraints_) {
        if (!k.driving || std::find(skip.begin(), skip.end(), cid) != skip.end()) {
            continue;  // driven dimensions only measure; skipped = known redundant
        }
        double* val = nullptr;
        if (isDimensional(k.type)) {
            Dim want = k.type == ConstraintType::Angle ? Dim::ang() : Dim::len();
            auto q = evaluate(k.expr, lookup, want);
            if (!q) {
                return fail(cid, q.error().message);
            }
            double v = q->value;
            if (k.type == ConstraintType::DistanceX || k.type == ConstraintType::DistanceY) {
                val = slot(v);
            }
            else {
                if (v <= 0 && k.type != ConstraintType::Angle) {
                    return fail(cid, "A dimension must be larger than zero (got "
                                         + formatQuantity(*q) + ").");
                }
                val = slot(v);
            }
        }
        const int tag = cid;
        switch (k.type) {
            case ConstraintType::Coincident:
                if (!isPoint(k.a) || !isPoint(k.b)) {
                    return fail(cid, "Coincident needs two points.");
                }
                sys.addConstraintP2PCoincident(pts[k.a], pts[k.b], tag);
                break;
            case ConstraintType::PointOnCurve:
                if (isLine(k.b)) {
                    sys.addConstraintPointOnLine(pts[k.a], lines[k.b], tag);
                }
                else if (isCircle(k.b)) {
                    sys.addConstraintPointOnCircle(pts[k.a], circles[k.b], tag);
                }
                else if (isArc(k.b)) {
                    sys.addConstraintPointOnArc(pts[k.a], arcs[k.b], tag);
                }
                else {
                    return fail(cid, "Point-on-curve needs a point and a curve.");
                }
                break;
            case ConstraintType::Horizontal:
                if (isLine(k.a)) {
                    sys.addConstraintHorizontal(lines[k.a], tag);
                }
                else if (isPoint(k.a) && isPoint(k.b)) {
                    sys.addConstraintHorizontal(pts[k.a], pts[k.b], tag);
                }
                else {
                    return fail(cid, "Horizontal needs a line or two points.");
                }
                break;
            case ConstraintType::Vertical:
                if (isLine(k.a)) {
                    sys.addConstraintVertical(lines[k.a], tag);
                }
                else if (isPoint(k.a) && isPoint(k.b)) {
                    sys.addConstraintVertical(pts[k.a], pts[k.b], tag);
                }
                else {
                    return fail(cid, "Vertical needs a line or two points.");
                }
                break;
            case ConstraintType::Parallel:
                if (!isLine(k.a) || !isLine(k.b)) {
                    return fail(cid, "Parallel needs two lines.");
                }
                sys.addConstraintParallel(lines[k.a], lines[k.b], tag);
                break;
            case ConstraintType::Perpendicular:
                if (!isLine(k.a) || !isLine(k.b)) {
                    return fail(cid, "Perpendicular needs two lines.");
                }
                sys.addConstraintPerpendicular(lines[k.a], lines[k.b], tag);
                break;
            case ConstraintType::Equal:
                if (isLine(k.a) && isLine(k.b)) {
                    sys.addConstraintEqualLength(lines[k.a], lines[k.b], tag);
                }
                else if (isCircle(k.a) && isCircle(k.b)) {
                    sys.addConstraintEqualRadius(circles[k.a], circles[k.b], tag);
                }
                else if (isCircle(k.a) && isArc(k.b)) {
                    sys.addConstraintEqualRadius(circles[k.a], arcs[k.b], tag);
                }
                else if (isArc(k.a) && isCircle(k.b)) {
                    sys.addConstraintEqualRadius(circles[k.b], arcs[k.a], tag);
                }
                else if (isArc(k.a) && isArc(k.b)) {
                    sys.addConstraintEqualRadius(arcs[k.a], arcs[k.b], tag);
                }
                else {
                    return fail(cid, "Equal needs two lines or two round curves.");
                }
                break;
            case ConstraintType::Tangent: {
                auto leftOf = [&](GCS::Line& l, GCS::Point& c) {
                    double ax = *l.p2.x - *l.p1.x, ay = *l.p2.y - *l.p1.y;
                    return ax * (*c.y - *l.p1.y) - ay * (*c.x - *l.p1.x) >= 0;
                };
                if (isLine(k.a) && isCircle(k.b)) {
                    sys.addConstraintTangent(lines[k.a], circles[k.b],
                                             leftOf(lines[k.a], circles[k.b].center), tag);
                }
                else if (isLine(k.a) && isArc(k.b)) {
                    sys.addConstraintTangent(lines[k.a], arcs[k.b],
                                             leftOf(lines[k.a], arcs[k.b].center), tag);
                }
                else if (isCircle(k.a) && isCircle(k.b)) {
                    sys.addConstraintTangent(circles[k.a], circles[k.b], tag);
                }
                else if (isArc(k.a) && isArc(k.b)) {
                    sys.addConstraintTangent(arcs[k.a], arcs[k.b], tag);
                }
                else if (isCircle(k.a) && isArc(k.b)) {
                    sys.addConstraintTangent(circles[k.a], arcs[k.b], tag);
                }
                else {
                    return fail(cid, "Tangent needs a line or curve and a round curve.");
                }
                break;
            }
            case ConstraintType::Midpoint:
                if (!isPoint(k.a) || !isLine(k.b)) {
                    return fail(cid, "Midpoint needs a point and a line.");
                }
                sys.addConstraintPointOnLine(pts[k.a], lines[k.b], tag);
                sys.addConstraintPointOnPerpBisector(pts[k.a], lines[k.b], tag);
                break;
            case ConstraintType::Symmetric:
                if (!isPoint(k.a) || !isPoint(k.b) || !isLine(k.c)) {
                    return fail(cid, "Symmetry needs two points and a line.");
                }
                sys.addConstraintP2PSymmetric(pts[k.a], pts[k.b], lines[k.c], tag);
                break;
            case ConstraintType::Fix:
                if (!isPoint(k.a)) {
                    return fail(cid, "Fix needs a point.");
                }
                sys.addConstraintCoordinateX(pts[k.a], slot(k.fx), tag);
                sys.addConstraintCoordinateY(pts[k.a], slot(k.fy), tag);
                break;
            case ConstraintType::Distance:
                if (isLine(k.a) && !k.b) {
                    sys.addConstraintP2PDistance(lines[k.a].p1, lines[k.a].p2, val, tag);
                }
                else if (isPoint(k.a) && isPoint(k.b)) {
                    sys.addConstraintP2PDistance(pts[k.a], pts[k.b], val, tag);
                }
                else if (isPoint(k.a) && isLine(k.b)) {
                    GCS::Line& l = lines[k.b];
                    double ax = *l.p2.x - *l.p1.x, ay = *l.p2.y - *l.p1.y;
                    bool ccw = ax * (*pts[k.a].y - *l.p1.y) - ay * (*pts[k.a].x - *l.p1.x) >= 0;
                    sys.addConstraintP2LDistance(pts[k.a], l, val, ccw, tag);
                }
                else {
                    return fail(cid, "Distance needs a line, two points, or a point and a line.");
                }
                break;
            case ConstraintType::DistanceX:
            case ConstraintType::DistanceY: {
                bool isX = k.type == ConstraintType::DistanceX;
                if (!isPoint(k.a) || (k.b && !isPoint(k.b))) {
                    return fail(cid, "Horizontal/vertical distance needs points.");
                }
                if (!k.b) {
                    isX ? sys.addConstraintCoordinateX(pts[k.a], val, tag)
                        : sys.addConstraintCoordinateY(pts[k.a], val, tag);
                }
                else {
                    double* pa = isX ? pts[k.a].x : pts[k.a].y;
                    double* pb = isX ? pts[k.b].x : pts[k.b].y;
                    sys.addConstraintDifference(pa, pb, val, tag);
                }
                break;
            }
            case ConstraintType::Radius:
            case ConstraintType::Diameter: {
                bool dia = k.type == ConstraintType::Diameter;
                if (isCircle(k.a)) {
                    dia ? sys.addConstraintCircleDiameter(circles[k.a], val, tag)
                        : sys.addConstraintCircleRadius(circles[k.a], val, tag);
                }
                else if (isArc(k.a)) {
                    dia ? sys.addConstraintArcDiameter(arcs[k.a], val, tag)
                        : sys.addConstraintArcRadius(arcs[k.a], val, tag);
                }
                else {
                    return fail(cid, "Radius and diameter need a circle or arc.");
                }
                break;
            }
            case ConstraintType::Angle:
                if (!isLine(k.a) || !isLine(k.b)) {
                    return fail(cid, "Angle needs two lines.");
                }
                sys.addConstraintL2LAngle(lines[k.a], lines[k.b], val, tag);
                break;
        }
    }

    GCS::Point target;
    if (dragPoint) {
        if (!isPoint(*dragPoint)) {
            return fail(0, "That point does not exist.");
        }
        target.x = slot(dx);
        target.y = slot(dy);
        sys.addConstraintP2PCoincident(pts[*dragPoint], target, GCS::DefaultTemporaryConstraint);
    }

    sys.declareUnknowns(unknowns);
    sys.initSolution(GCS::DogLeg);
    rep.dof = sys.dofsNumber();
    GCS::VEC_I conflicting, redundant;
    sys.getConflicting(conflicting);
    sys.getRedundant(redundant);
    std::set<int> cset(conflicting.begin(), conflicting.end());
    std::set<int> rset(redundant.begin(), redundant.end());
    cset.erase(0);
    rset.erase(0);
    rep.conflicting.assign(cset.begin(), cset.end());
    rep.redundant.assign(rset.begin(), rset.end());

    auto finish = [&] {
        rep.solveMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return rep;
    };

    if (!rep.conflicting.empty()) {
        rep.state = SolveState::OverConstrained;
        rep.message = "Some constraints contradict each other. Remove one of the highlighted "
                      "constraints, or turn a dimension into a reference (driven) dimension.";
        return finish();
    }

    if (!rep.redundant.empty() && skip.empty()) {
        // Redundant constraints say nothing new, so solve without them (the rest still
        // satisfies them) and report them so the user can remove the duplicates.
        SolveReport again = runSolver(lookup, dragPoint, dx, dy, rep.redundant);
        if (again.state != SolveState::Failed) {
            again.redundant = rep.redundant;
            again.state = SolveState::OverConstrained;
            again.message = "Some constraints say the same thing twice. Remove one of the "
                            "highlighted constraints.";
        }
        return again;
    }
    GCS::SolveStatus st = sys.solve(GCS::DogLeg);
    if (st != GCS::SolveStatus::Success) {
        // Second chance with a different algorithm before reporting failure.
        sys.undoSolution();
        st = sys.solve(GCS::LevenbergMarquardt);
    }
    if (st != GCS::SolveStatus::Success) {
        sys.undoSolution();
        rep.state = SolveState::Failed;
        rep.message = "These constraints can't all be met with this geometry. Undo the last "
                      "change, or drag the geometry closer to the shape you want first.";
        return finish();
    }
    sys.applySolution();

    // Write the solution back.
    for (auto& [id, p] : pts) {
        entities_[id].x = *p.x;
        entities_[id].y = *p.y;
    }
    for (auto& [id, c] : circles) {
        entities_[id].r = *c.rad;
    }
    for (auto& [id, a] : arcs) {
        auto& e = entities_[id];
        e.r = *a.rad;
        e.a0 = *a.startAngle;
        e.a1 = *a.endAngle;
    }
    syncCurvePoints();

    GCS::VEC_pD dependent;
    sys.getDependentParams(dependent);
    std::set<int> freeIds;
    for (double* p : dependent) {
        auto it = paramOwner.find(p);
        if (it != paramOwner.end()) {
            freeIds.insert(it->second);
        }
    }
    rep.freeEntities.assign(freeIds.begin(), freeIds.end());

    if (rep.dof == 0) {
        rep.state = SolveState::FullyConstrained;
        rep.message = "Fully defined: nothing can move.";
    }
    else {
        rep.state = SolveState::UnderConstrained;
        rep.message = std::to_string(rep.dof) + " degree" + (rep.dof == 1 ? "" : "s")
                    + " of freedom left: some geometry can still move.";
    }
    return finish();
}

std::vector<GapMarker> Sketch::openEndpoints(double nearTolerance) const
{
    struct End
    {
        int pid;
        double x, y;
    };
    std::vector<End> ends;
    for (auto& [id, e] : entities_) {
        if (e.construction) {
            continue;
        }
        if (e.type == EntityType::Line || e.type == EntityType::Arc) {
            for (int pid : {e.p1, e.p2}) {
                auto [x, y] = pointXY(pid);
                ends.push_back({pid, x, y});
            }
        }
    }
    constexpr double kSame = 1e-6;
    std::vector<GapMarker> open;
    for (size_t i = 0; i < ends.size(); ++i) {
        int count = 0;
        for (size_t j = 0; j < ends.size(); ++j) {
            if (std::hypot(ends[i].x - ends[j].x, ends[i].y - ends[j].y) < kSame) {
                ++count;
            }
        }
        if (count == 1) {
            open.push_back({ends[i].pid, ends[i].x, ends[i].y, std::nullopt, 0});
        }
    }
    for (auto& g : open) {
        double best = nearTolerance;
        for (auto& o : open) {
            if (o.pointId == g.pointId) {
                continue;
            }
            double d = std::hypot(g.x - o.x, g.y - o.y);
            if (d <= best) {
                best = d;
                g.nearestPointId = o.pointId;
                g.gap = d;
            }
        }
    }
    return open;
}

// ---------------------------------------------------------------- json

nlohmann::json Sketch::toJson() const
{
    nlohmann::json ents = nlohmann::json::array();
    for (auto& [id, e] : entities_) {
        nlohmann::json j {{"id", id}, {"type", entityTypeName(e.type)}};
        if (e.construction) {
            j["construction"] = true;
        }
        switch (e.type) {
            case EntityType::Point:
                j["x"] = e.x;
                j["y"] = e.y;
                if (e.internal) {
                    j["internal"] = true;
                }
                break;
            case EntityType::Line:
                j["p1"] = e.p1;
                j["p2"] = e.p2;
                break;
            case EntityType::Circle:
                j["center"] = e.center;
                j["r"] = e.r;
                break;
            case EntityType::Arc:
                j["center"] = e.center;
                j["p1"] = e.p1;
                j["p2"] = e.p2;
                j["r"] = e.r;
                j["a0"] = e.a0;
                j["a1"] = e.a1;
                break;
        }
        ents.push_back(j);
    }
    nlohmann::json cons = nlohmann::json::array();
    for (auto& [id, k] : constraints_) {
        nlohmann::json j {{"id", id}, {"type", toString(k.type)}, {"a", k.a}};
        if (k.b) {
            j["b"] = k.b;
        }
        if (k.c) {
            j["c"] = k.c;
        }
        if (!k.expr.empty()) {
            j["value"] = k.expr;
        }
        if (!k.driving) {
            j["driving"] = false;
        }
        if (k.type == ConstraintType::Fix) {
            j["fx"] = k.fx;
            j["fy"] = k.fy;
        }
        cons.push_back(j);
    }
    nlohmann::json j {{"entities", ents}, {"constraints", cons}, {"next_id", nextId_}};
    if (!texts_.empty()) {
        nlohmann::json txt = nlohmann::json::array();
        for (auto& [id, t] : texts_) {
            nlohmann::json jt {{"id", id}, {"anchor", t.anchor}, {"text", t.text}, {"height", t.height},
                               {"height_value", t.heightValue}};
            if (!t.font.empty()) {
                jt["font"] = t.font;
            }
            if (t.angle != 0) {
                jt["angle"] = t.angle;
            }
            txt.push_back(jt);
        }
        j["texts"] = txt;
    }
    return j;
}

Result<Sketch> Sketch::fromJson(const nlohmann::json& j)
{
    Sketch s;
    try {
        for (auto& je : j.at("entities")) {
            SketchEntity e;
            e.id = je.at("id").get<int>();
            auto t = entityTypeFromString(je.at("type").get<std::string>());
            if (!t) {
                return makeError("file.sketch", "The file contains an unknown sketch shape.");
            }
            e.type = *t;
            e.construction = je.value("construction", false);
            e.internal = je.value("internal", false);
            e.x = je.value("x", 0.0);
            e.y = je.value("y", 0.0);
            e.p1 = je.value("p1", 0);
            e.p2 = je.value("p2", 0);
            e.center = je.value("center", 0);
            e.r = je.value("r", 0.0);
            e.a0 = je.value("a0", 0.0);
            e.a1 = je.value("a1", 0.0);
            s.entities_[e.id] = e;
        }
        for (auto& jc : j.at("constraints")) {
            SketchConstraint k;
            k.id = jc.at("id").get<int>();
            auto t = constraintTypeFromString(jc.at("type").get<std::string>());
            if (!t) {
                return makeError("file.sketch", "The file contains an unknown constraint.");
            }
            k.type = *t;
            k.a = jc.value("a", 0);
            k.b = jc.value("b", 0);
            k.c = jc.value("c", 0);
            k.expr = jc.value("value", std::string {});
            k.driving = jc.value("driving", true);
            k.fx = jc.value("fx", 0.0);
            k.fy = jc.value("fy", 0.0);
            s.constraints_[k.id] = k;
        }
        for (auto& jt : j.value("texts", nlohmann::json::array())) {
            SketchText t;
            t.id = jt.at("id").get<int>();
            t.anchor = jt.at("anchor").get<int>();
            t.text = jt.value("text", std::string {});
            t.height = jt.value("height", std::string("5 mm"));
            t.heightValue = jt.value("height_value", 5.0);
            t.font = jt.value("font", std::string {});
            t.angle = jt.value("angle", 0.0);
            if (!s.entities_.count(t.anchor)) {
                return makeError("file.sketch", "A text in this file has lost its anchor point.");
            }
            s.texts_[t.id] = t;
        }
        s.nextId_ = j.value("next_id", 1);
        int maxId = 0;
        for (auto& [id, t] : s.texts_) {
            maxId = std::max(maxId, id);
        }
        for (auto& [id, e] : s.entities_) {
            maxId = std::max(maxId, id);
        }
        for (auto& [id, k] : s.constraints_) {
            maxId = std::max(maxId, id);
        }
        s.nextId_ = std::max(s.nextId_, maxId + 1);
    }
    catch (const nlohmann::json::exception& ex) {
        return makeError("file.sketch", "A sketch in this file is damaged.", {}, ex.what());
    }
    return s;
}

}  // namespace jig
