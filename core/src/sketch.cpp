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

#include "curve_constraints.hpp"
#include "jig/kernel.hpp"  // offsetCurves, for offsets that follow their source

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
        case EntityType::Ellipse:
            return "ellipse";
        case EntityType::Spline:
            return "spline";
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
    if (s == "ellipse") {
        return EntityType::Ellipse;
    }
    if (s == "spline") {
        return EntityType::Spline;
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

int Sketch::addEllipse(double cx, double cy, double ax, double ay, double minor)
{
    SketchEntity e;
    e.id = nextId_++;
    e.type = EntityType::Ellipse;
    e.center = addPoint(cx, cy, true);
    e.p1 = addPoint(ax, ay, true);
    e.r = minor;
    entities_[e.id] = e;
    return e.id;
}

int Sketch::addSpline(const std::vector<std::pair<double, double>>& points, bool closed, bool control)
{
    SketchEntity e;
    e.id = nextId_++;
    e.type = EntityType::Spline;
    e.closed = closed && points.size() >= 3;
    e.control = control;
    for (auto [x, y] : points) {
        e.pts.push_back(addPoint(x, y, true));
    }
    entities_[e.id] = e;
    return e.id;
}

std::vector<Bezier2> Sketch::splinePieces(int splineId) const
{
    const SketchEntity* e = entity(splineId);
    if (!e || e->type != EntityType::Spline || e->pts.size() < 2) {
        return {};
    }
    std::vector<std::pair<double, double>> P;
    for (int pid : e->pts) {
        P.push_back(pointXY(pid));
    }
    return e->control ? detail::bsplinePieces(P, e->closed) : detail::catmullRom(P, e->closed);
}

std::vector<std::pair<double, double>> Sketch::curvePoints(int entityId, int perPiece) const
{
    std::vector<std::pair<double, double>> out;
    const SketchEntity* e = entity(entityId);
    if (!e) {
        return out;
    }
    switch (e->type) {
        case EntityType::Point:
            out.push_back({e->x, e->y});
            break;
        case EntityType::Line:
            out.push_back(pointXY(e->p1));
            out.push_back(pointXY(e->p2));
            break;
        case EntityType::Circle:
        case EntityType::Arc: {
            auto [cx, cy] = pointXY(e->center);
            double a0 = 0, span = 2 * std::numbers::pi;
            if (e->type == EntityType::Arc) {
                a0 = e->a0;
                span = std::fmod(e->a1 - e->a0 + 4 * std::numbers::pi, 2 * std::numbers::pi);
            }
            const int n = std::max(8, perPiece * 2);
            for (int i = 0; i <= n; ++i) {
                const double t = a0 + span * i / n;
                out.push_back({cx + e->r * std::cos(t), cy + e->r * std::sin(t)});
            }
            break;
        }
        case EntityType::Ellipse: {
            auto [cx, cy] = pointXY(e->center);
            auto [ax, ay] = pointXY(e->p1);
            const double a = std::hypot(ax - cx, ay - cy), th = std::atan2(ay - cy, ax - cx);
            const int n = std::max(8, perPiece * 2);
            for (int i = 0; i <= n; ++i) {
                const double t = 2 * std::numbers::pi * i / n;
                const double u = a * std::cos(t), v = e->r * std::sin(t);
                out.push_back({cx + u * std::cos(th) - v * std::sin(th), cy + u * std::sin(th) + v * std::cos(th)});
            }
            break;
        }
        case EntityType::Spline:
            for (auto& b : splinePieces(entityId)) {
                for (int i = out.empty() ? 0 : 1; i <= perPiece; ++i) {
                    const double t = double(i) / perPiece, m = 1 - t;
                    const double w0 = m * m * m, w1 = 3 * m * m * t, w2 = 3 * m * t * t, w3 = t * t * t;
                    out.push_back({w0 * b[0].first + w1 * b[1].first + w2 * b[2].first + w3 * b[3].first,
                                   w0 * b[0].second + w1 * b[1].second + w2 * b[2].second + w3 * b[3].second});
                }
            }
            break;
    }
    return out;
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

// Ellipses and splines are not cut by (or attached to) other curves yet.
bool freeForm(EntityType t)
{
    return t == EntityType::Ellipse || t == EntityType::Spline;
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
    if (!t || t->type == EntityType::Point || freeForm(t->type)) {
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
        std::vector<std::pair<double, double>> pts;
        if (freeForm(e.type)) {
            // an ellipse or smooth curve cuts too: where its short pieces cross this curve
            const auto Q = curvePoints(id, 32);
            for (size_t k = 0; k + 1 < Q.size(); ++k) {
                // a little slack at both ends: a crossing exactly at a sample point counts (rounding of
                // the sample may put it a hair outside either piece); repeats are merged below
                constexpr double wTol = 1e-9;
                const double qx = Q[k].first, qy = Q[k].second, ex = Q[k + 1].first - qx, ey = Q[k + 1].second - qy;
                if (T.line) {
                    const double rx = T.bx - T.ax, ry = T.by - T.ay, den = rx * ey - ry * ex;
                    if (std::abs(den) < 1e-12) {
                        continue;
                    }
                    const double w = ((qx - T.ax) * ry - (qy - T.ay) * rx) / den;  // along the piece
                    if (w >= -wTol && w <= 1 + wTol) {
                        pts.emplace_back(qx + ex * w, qy + ey * w);
                    }
                }
                else {
                    const double fx = qx - T.cx, fy = qy - T.cy;
                    const double A = ex * ex + ey * ey, B = 2 * (fx * ex + fy * ey), Cc = fx * fx + fy * fy - T.r * T.r;
                    const double disc = B * B - 4 * A * Cc;
                    if (A > 0 && disc >= 0) {
                        for (double sgn : {-1.0, 1.0}) {
                            const double w = (-B + sgn * std::sqrt(disc)) / (2 * A);
                            if (w >= -wTol && w <= 1 + wTol) {
                                pts.emplace_back(qx + ex * w, qy + ey * w);
                            }
                        }
                    }
                }
            }
            for (auto [px, py] : pts) {
                if (!onArc(T, px, py)) {
                    continue;
                }
                const double prm = paramOf(px, py);
                const double end = T.line ? 1.0 : T.span;
                if (!T.line && T.span >= kTau - eps) {
                    cuts.push_back(prm > end - eps ? 0.0 : prm);
                }
                else if (prm > eps && prm < end - eps) {
                    cuts.push_back(prm);
                }
            }
            continue;
        }
        const Geo C = geoOf(e);
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
            double prm = paramOf(px, py);
            const double end = T.line ? 1.0 : T.span;
            if (!T.line && T.span >= kTau - eps) {  // a full circle: every angle is inside
                cuts.push_back(prm > end - eps ? 0.0 : prm);
            }
            else if (prm > eps && prm < end - eps) {
                cuts.push_back(prm);
            }
        }
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [&](double u, double v) { return v - u < eps; }), cuts.end());
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

std::optional<std::pair<int, double>> Sketch::curveNear(double x, double y, bool construction) const
{
    std::optional<std::pair<int, double>> best;
    for (auto& [id, e] : entities_) {
        if (e.type == EntityType::Point || (e.construction && !construction)) {
            continue;
        }
        double d = 1e300;
        if (freeForm(e.type)) {  // distance to the drawn polyline
            auto pts = curvePoints(id, 32);
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                auto [ax, ay] = pts[i];
                auto [bx, by] = pts[i + 1];
                const double dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
                const double u = L2 > 0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / L2, 0.0, 1.0) : 0.0;
                d = std::min(d, std::hypot(ax + dx * u - x, ay + dy * u - y));
            }
        }
        else if (e.type == EntityType::Line) {
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
        return makeError("sketch.fillet", fmt("A %1 round does not fit this corner.", {formatQuantity(Quantity {r, Dim::len()})}),
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
        if (e.type != EntityType::Point && (e.p1 == pid || e.p2 == pid || e.center == pid ||
                                            std::find(e.pts.begin(), e.pts.end(), pid) != e.pts.end())) {
            return;
        }
    }
    for (auto& [id, t] : texts_) {
        if (t.anchor == pid) {
            return;
        }
    }
    for (auto& [id, a] : artworks_) {
        if (a.anchor == pid) {
            return;
        }
    }
    entities_.erase(pid);
    std::erase_if(constraints_, [&](const auto& kv) {
        const auto& c = kv.second;
        return c.a == pid || c.b == pid || c.c == pid;
    });
}

// A new point at (px, py) attached to whatever other curve passes there: the existing
// endpoint (coincident) or the curve (point on curve).
int Sketch::attachedPoint(int entityId, double px, double py)
{
    const int pid = addPoint(px, py, true);
    // the crossing curve(s): every other curve passing through (px, py)
    for (auto& [id, e] : entities_) {
        if (id == entityId || e.type == EntityType::Point || e.construction || id == pid) {
            continue;
        }
        // an existing endpoint there (a smooth curve's too): share it
        const int f0 = e.pts.empty() ? 0 : e.pts.front(), f1 = e.pts.empty() ? 0 : e.pts.back();
        for (int q : {e.p1, e.p2, f0, f1}) {
            if (q && q != pid) {
                auto [qx, qy] = pointXY(q);
                if (std::hypot(qx - px, qy - py) < 1e-6) {
                    addConstraint(ConstraintType::Coincident, pid, q);
                    // q held on the curve being cut is now held at its new end: once is enough
                    std::erase_if(constraints_, [&](const auto& kv) {
                        return kv.second.type == ConstraintType::PointOnCurve && kv.second.a == q &&
                               kv.second.b == entityId;
                    });
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
        if (freeForm(e.type)) {
            // cuts on these are found on short straight pieces, so allow for the pieces' sag;
            // the constraint then puts the point exactly on the curve
            std::vector<double> v {px, py};
            double size = 0;
            if (e.type == EntityType::Ellipse) {
                for (int q : {e.center, e.p1}) {
                    v.push_back(pointXY(q).first);
                    v.push_back(pointXY(q).second);
                }
                size = std::max(std::hypot(v[4] - v[2], v[5] - v[3]), e.r);
                if (std::abs(detail::pointOnEllipse(v, e.r)) < 3e-3 * size) {
                    dist = 0;
                }
            }
            else {
                for (int q : e.pts) {
                    v.push_back(pointXY(q).first);
                    v.push_back(pointXY(q).second);
                }
                for (size_t k = 4; k + 1 < v.size(); k += 2) {
                    size = std::max(size, std::hypot(v[k] - v[k - 2], v[k + 1] - v[k - 1]));
                }
                if (std::abs(detail::pointOnSmooth(v, e.closed, e.control)) < 1e-3 * std::max(size, 1.0)) {
                    dist = 0;
                }
            }
        }
        else if (e.type == EntityType::Line) {
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
}

// A line's length, midpoint and equal-length constraints no longer apply once it is cut.
void Sketch::dropSizeConstraints(int entityId)
{
    std::erase_if(constraints_, [&](const auto& kv) {
        const auto& c = kv.second;
        if (c.type == ConstraintType::Distance && c.a == entityId && !c.b) {
            return true;
        }
        return (c.type == ConstraintType::Midpoint || c.type == ConstraintType::Equal) &&
               (c.a == entityId || c.b == entityId);
    });
}

bool Sketch::splitFreeForm(int entityId, double x, double y, bool keepAll, std::vector<int>* made)
{
    const SketchEntity* t = entity(entityId);
    if (!t || !freeForm(t->type)) {
        return false;
    }
    // the curve as short pieces, and the distance along it to each point
    const auto P = curvePoints(entityId, 32);
    if (P.size() < 2) {
        return false;
    }
    std::vector<double> S(P.size(), 0.0);
    for (size_t k = 1; k < P.size(); ++k) {
        S[k] = S[k - 1] + std::hypot(P[k].first - P[k - 1].first, P[k].second - P[k - 1].second);
    }
    const double L = S.back();
    const bool closed = std::hypot(P.front().first - P.back().first, P.front().second - P.back().second) < 1e-9;
    auto at = [&](double s) {
        if (closed) {
            s = std::fmod(std::fmod(s, L) + L, L);
        }
        size_t k = 0;
        while (k + 2 < P.size() && S[k + 1] < s) {
            ++k;
        }
        const double seg = S[k + 1] - S[k], w = seg > 0 ? std::clamp((s - S[k]) / seg, 0.0, 1.0) : 0;
        return std::pair {P[k].first + (P[k + 1].first - P[k].first) * w, P[k].second + (P[k + 1].second - P[k].second) * w};
    };
    // where other curves cross it (their own short pieces, or exact lines)
    struct C
    {
        double s;
        int entity;
    };
    std::vector<C> cuts;
    for (auto& [id, e] : entities_) {
        if (id == entityId || e.type == EntityType::Point || e.construction) {
            continue;
        }
        const auto Q = curvePoints(id, 32);
        for (size_t i = 0; i + 1 < P.size(); ++i) {
            const double ax = P[i].first, ay = P[i].second, rx = P[i + 1].first - ax, ry = P[i + 1].second - ay;
            for (size_t j = 0; j + 1 < Q.size(); ++j) {
                const double bx = Q[j].first, by = Q[j].second, sx = Q[j + 1].first - bx, sy = Q[j + 1].second - by;
                const double den = rx * sy - ry * sx;
                if (std::abs(den) < 1e-14) {
                    continue;
                }
                const double u = ((bx - ax) * sy - (by - ay) * sx) / den, v = ((bx - ax) * ry - (by - ay) * rx) / den;
                if (u >= -1e-9 && u <= 1 + 1e-9 && v >= -1e-9 && v <= 1 + 1e-9) {  // exact hits at sample points too
                    const double s = S[i] + u * (S[i + 1] - S[i]);
                    if (closed || (s > 1e-7 && s < L - 1e-7)) {
                        cuts.push_back({s, id});
                    }
                }
            }
        }
    }
    std::sort(cuts.begin(), cuts.end(), [](const C& a, const C& b) { return a.s < b.s; });
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [&](const C& a, const C& b) { return b.s - a.s < 1e-6 * std::max(1.0, L); }), cuts.end());
    if (closed && cuts.size() > 1 && cuts.back().s - cuts.front().s > L - 1e-6 * std::max(1.0, L)) {
        cuts.pop_back();  // the same crossing at the closing point, seen from both ends
    }
    // where the click is along it
    double click = 0, best = 1e300;
    for (size_t k = 0; k + 1 < P.size(); ++k) {
        const double ax = P[k].first, ay = P[k].second, dx = P[k + 1].first - ax, dy = P[k + 1].second - ay;
        const double L2 = dx * dx + dy * dy, w = L2 > 0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / L2, 0.0, 1.0) : 0;
        const double d = std::hypot(ax + dx * w - x, ay + dy * w - y);
        if (d < best) {
            best = d;
            click = S[k] + w * std::sqrt(L2);
        }
    }
    // the pieces between cuts: [from, to] with the curve that ends each one there (0: an end)
    struct Piece
    {
        double from, to;
        int fromCut, toCut;
    };
    std::vector<Piece> pieces;
    if (closed) {
        if (cuts.size() < (keepAll ? 1u : 2u)) {
            if (!keepAll && cuts.size() < 2) {
                return remove(entityId);  // nothing to stop at: the whole curve goes
            }
            return false;
        }
        for (size_t k = 0; k < cuts.size(); ++k) {
            const C& a = cuts[k];
            const C& b = cuts[(k + 1) % cuts.size()];
            pieces.push_back({a.s, b.s > a.s ? b.s : b.s + L, a.entity, b.entity});
        }
    }
    else {
        double from = 0;
        int fromCut = 0;
        for (const C& c : cuts) {
            pieces.push_back({from, c.s, fromCut, c.entity});
            from = c.s;
            fromCut = c.entity;
        }
        pieces.push_back({from, L, fromCut, 0});
        if (pieces.size() < 2) {
            return keepAll ? false : remove(entityId);
        }
    }
    auto contains = [&](const Piece& p) {
        return (click >= p.from && click <= p.to) || (closed && click + L >= p.from && click + L <= p.to);
    };
    // the original's own end points stay, so curves joined to them stay joined
    const std::vector<int> fit = t->pts;
    const int startPt = !closed && !fit.empty() ? fit.front() : 0, endPt = !closed && !fit.empty() ? fit.back() : 0;
    std::map<long long, int> cutPoints;  // break: neighbouring pieces share the point between them
    std::vector<int> out;
    for (const Piece& p : pieces) {
        if (!keepAll && contains(p)) {
            continue;  // the clicked piece goes
        }
        const int m = std::max(4, int(std::ceil(48 * (p.to - p.from) / L)));
        std::vector<std::pair<double, double>> pts;
        for (int k = 0; k <= m; ++k) {
            pts.push_back(at(p.from + (p.to - p.from) * k / m));
        }
        const int id = addSpline(pts, false);
        SketchEntity& ne = entities_.at(id);
        auto reuse = [&](size_t slot, int keep) {
            const int made = ne.pts[slot];
            ne.pts[slot] = keep;
            entities_.erase(made);
        };
        const long long kf = std::llround(p.from * 1e6), kt = std::llround(std::fmod(p.to, L) * 1e6);
        if (p.from <= 1e-9 && startPt) {
            reuse(0, startPt);
        }
        else if (keepAll && cutPoints.count(kf)) {
            reuse(0, cutPoints[kf]);
        }
        else {
            cutPoints[kf] = ne.pts.front();
        }
        if (p.to >= L - 1e-9 && endPt && !closed) {
            reuse(ne.pts.size() - 1, endPt);
        }
        else if (keepAll && cutPoints.count(kt)) {
            reuse(ne.pts.size() - 1, cutPoints[kt]);
        }
        else {
            cutPoints[kt] = ne.pts.back();
        }
        out.push_back(id);
    }
    remove(entityId);
    if (made) {
        *made = out;
    }
    return true;
}

bool Sketch::trim(int entityId, double x, double y)
{
    if (const SketchEntity* e = entity(entityId); e && freeForm(e->type)) {
        return splitFreeForm(entityId, x, y, false, nullptr);  // an ellipse or smooth curve
    }
    auto piece = trimPiece(entityId, x, y);
    if (!piece) {
        return false;
    }
    if (piece->whole) {
        return remove(entityId);
    }
    SketchEntity& t = entities_.at(entityId);
    const bool line = t.type == EntityType::Line;
    auto pointAt = [&](double px, double py) { return attachedPoint(entityId, px, py); };
    if (line) {
        const auto [ax, ay] = pointXY(t.p1);
        const auto [bx, by] = pointXY(t.p2);
        auto at = [&](double u) { return std::pair {ax + (bx - ax) * u, ay + (by - ay) * u}; };
        const int oldP1 = t.p1, oldP2 = t.p2;
        dropSizeConstraints(entityId);
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

int Sketch::addArtwork(double x, double y, std::vector<ArtContour> contours, double naturalWidth,
                       std::string widthExpr, double widthValue, std::string name)
{
    SketchArtwork a;
    a.anchor = addPoint(x, y);
    a.id = nextId_++;
    a.contours = std::move(contours);
    a.naturalWidth = naturalWidth;
    a.width = std::move(widthExpr);
    a.widthValue = widthValue;
    a.name = std::move(name);
    const int id = a.id;
    artworks_[id] = std::move(a);
    return id;
}

SketchText* Sketch::text(int id)
{
    auto it = texts_.find(id);
    return it == texts_.end() ? nullptr : &it->second;
}

bool Sketch::remove(int entityId)
{
    if (auto w = artworks_.find(entityId); w != artworks_.end()) {
        const int anchor = w->second.anchor;
        artworks_.erase(w);
        remove(anchor);
        return true;
    }
    std::erase_if(artworks_, [&](const auto& kv) { return kv.second.anchor == entityId; });
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
    if (it->second.type == EntityType::Point) {
        // a point other curves end at (or are centred on): those curves go with it, as they
        // cannot exist without it
        std::vector<int> users;
        for (auto& [id, o] : entities_) {
            const bool inPts = std::find(o.pts.begin(), o.pts.end(), entityId) != o.pts.end();
            if (inPts && o.type == EntityType::Spline && o.pts.size() > (o.closed ? 3u : 2u)) {
                std::erase(o.pts, entityId);  // a smooth curve simply no longer passes through it
                continue;
            }
            if (o.p1 == entityId || o.p2 == entityId || o.center == entityId || inPts) {
                users.push_back(id);
            }
        }
        for (int u : users) {
            remove(u);
        }
        it = entities_.find(entityId);
        if (it == entities_.end()) {
            return true;  // it was an internal point and went with its last curve
        }
    }
    std::set<int> gone {entityId};
    const SketchEntity e = it->second;
    entities_.erase(it);
    // internal points not used by any other curve go too
    std::vector<int> own {e.p1, e.p2, e.center};
    own.insert(own.end(), e.pts.begin(), e.pts.end());
    for (int pid : own) {
        if (!pid) {
            continue;
        }
        auto* p = entity(pid);
        if (!p || !p->internal) {
            continue;
        }
        bool used = false;
        for (auto& [id, o] : entities_) {
            if (o.p1 == pid || o.p2 == pid || o.center == pid ||
                std::find(o.pts.begin(), o.pts.end(), pid) != o.pts.end()) {
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
    for (auto& [id, a] : artworks_) {
        if (a.width.empty()) {
            continue;
        }
        auto q = evaluate(a.width, lookup, Dim::len());
        if (!q || !(q->value > 0)) {
            SolveReport r;
            r.state = SolveState::Failed;
            r.message = q ? "Artwork width must be larger than zero." : "Artwork width: " + q.error().message;
            return r;
        }
        a.widthValue = q->value;
    }
    for (auto& [cid, k] : constraints_) {
        SketchEntity* el = entity(k.a);
        if (!el || el->type != EntityType::Ellipse || !k.driving ||
            (k.type != ConstraintType::Radius && k.type != ConstraintType::Diameter)) {
            continue;
        }
        auto q = evaluate(k.expr, lookup, Dim::len());
        if (!q || !(q->value > 0)) {
            SolveReport r;
            r.state = SolveState::Failed;
            r.conflicting.push_back(cid);
            r.message = q ? "An ellipse's minor radius must be larger than zero." : "Ellipse size: " + q.error().message;
            return r;
        }
        el->r = k.type == ConstraintType::Diameter ? q->value / 2 : q->value;
    }
    SolveReport rep = runSolver(lookup, nullptr, 0, 0);
    if (rebuildOffsets(lookup)) {
        rep = runSolver(lookup, nullptr, 0, 0);  // the copies are fixed where they were rebuilt
    }
    return rep;
}

SolveReport Sketch::drag(int pointId, double x, double y, const ParamLookup& lookup)
{
    SolveReport rep = runSolver(lookup, &pointId, x, y);
    if (rebuildOffsets(lookup)) {
        rep = runSolver(lookup, nullptr, 0, 0);
    }
    return rep;
}

namespace
{
// The offset copy made by offsetCurves, re-held by fixed points (and a circle's radius).
void holdInPlace(Sketch& s, const std::vector<int>& made, const std::set<int>& keepConstraints)
{
    std::vector<int> added;
    for (auto& [cid, c] : s.constraints()) {
        if (!keepConstraints.count(cid)) {
            added.push_back(cid);
        }
    }
    for (int cid : added) {
        s.removeConstraint(cid);
    }
    std::set<int> fixed;
    for (int id : made) {
        const SketchEntity* e = s.entity(id);
        if (!e) {
            continue;
        }
        for (int p : {e->p1, e->p2, e->center}) {
            if (p && fixed.insert(p).second) {
                s.addConstraint(ConstraintType::Fix, p);
            }
        }
        if (e->type == EntityType::Circle) {
            s.addConstraint(ConstraintType::Radius, id, 0, 0, std::to_string(e->r) + " mm");
        }
    }
}
}  // namespace

Result<std::vector<int>> Sketch::offsetLinked(const std::vector<int>& curves, const std::string& distanceExpr,
                                              const ParamLookup& lookup)
{
    auto d = evaluate(distanceExpr, lookup, Dim::len());
    if (!d) {
        return d.error();
    }
    std::set<int> keep;
    for (auto& [cid, c] : constraints_) {
        keep.insert(cid);
    }
    auto made = offsetCurves(*this, curves, d->value);
    if (!made) {
        return made;
    }
    holdInPlace(*this, made.value(), keep);
    links_.push_back({{"kind", "offset"}, {"source", curves}, {"distance", distanceExpr}, {"made", made.value()}});
    return made;
}

namespace
{
// Makes the copies a linked grid or ring describes.
Result<std::vector<int>> makePattern(Sketch& s, const std::vector<int>& curves, const nlohmann::json& spec,
                                     const ParamLookup& lookup)
{
    auto len = [&](const char* key, const char* def) { return evaluate(spec.value(key, std::string(def)), lookup, Dim::len()); };
    if (spec.value("kind", std::string {}) == "grid") {
        auto s1 = len("s1", "10 mm"), s2 = len("s2", "10 mm");
        if (!s1 || !s2) {
            return !s1 ? s1.error() : s2.error();
        }
        return s.patternRect(curves, spec.value("n1", 2), s1->value, 0, spec.value("n2", 1), 0, s2->value);
    }
    auto ang = evaluate(spec.value("angle", std::string("360 deg")), lookup, Dim::ang());
    if (!ang) {
        return ang.error();
    }
    const auto c = spec.value("center", std::vector<double> {0, 0});
    double cx = c.size() == 2 ? c[0] : 0, cy = c.size() == 2 ? c[1] : 0;
    if (spec.contains("center_point") && s.entity(spec["center_point"].get<int>())) {
        std::tie(cx, cy) = s.pointXY(spec["center_point"].get<int>());
    }
    return s.patternCircular(curves, cx, cy, spec.value("n", 6), ang->value);
}
}  // namespace

Result<std::vector<int>> Sketch::patternLinked(const std::vector<int>& curves, const nlohmann::json& spec,
                                               const ParamLookup& lookup)
{
    std::set<int> keep;
    for (auto& [cid, c] : constraints_) {
        keep.insert(cid);
    }
    auto made = makePattern(*this, curves, spec, lookup);
    if (!made) {
        return made;
    }
    holdInPlace(*this, made.value(), keep);
    links_.push_back({{"kind", "pattern"}, {"source", curves}, {"spec", spec}, {"made", made.value()}});
    return made;
}

bool Sketch::rebuildOffsets(const ParamLookup& lookup)
{
    bool any = false;
    for (auto& link : links_) {
        const std::string kind = link.value("kind", std::string {});
        if (kind != "offset" && kind != "pattern") {
            continue;
        }
        const auto source = link.value("source", std::vector<int> {});
        bool whole = !source.empty();
        for (int id : source) {
            whole = whole && entity(id);
        }
        if (!whole) {
            link["kind"] = kind + "-unlinked";  // its source is gone: the copy stays as it is
            continue;
        }
        auto d = kind == "offset" ? evaluate(link.value("distance", std::string("1 mm")), lookup, Dim::len())
                                  : Result<Quantity>(Quantity {0, Dim::len()});
        if (!d) {
            continue;
        }
        // a damaged pattern spec (a count that is not a number, say) unlinks the copies
        // instead of throwing out of the solve
        Result<std::vector<int>> made = std::vector<int> {};
        std::set<int> keep;
        try {
            const auto spec = link.value("spec", nlohmann::json::object());
            for (int id : link.value("made", std::vector<int> {})) {
                remove(id);
            }
            for (auto& [cid, c] : constraints_) {
                keep.insert(cid);
            }
            made = kind == "offset" ? offsetCurves(*this, source, d->value) : makePattern(*this, source, spec, lookup);
        }
        catch (const nlohmann::json::exception&) {
            link["kind"] = kind + "-unlinked";
            continue;
        }
        link["made"] = made ? made.value() : std::vector<int> {};
        if (made) {
            holdInPlace(*this, made.value(), keep);
        }
        any = true;
    }
    return any;
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
        if (e.type == EntityType::Line || e.type == EntityType::Arc || e.type == EntityType::Ellipse ||
            e.type == EntityType::Spline) {
            std::vector<int> own {e.p1, e.p2};
            own.insert(own.end(), e.pts.begin(), e.pts.end());
            for (int pid : own) {
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
                    return fail(cid, fmt("A dimension must be larger than zero (got %1).", {formatQuantity(*q)}));
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
                else if (const SketchEntity* f = entity(k.b); isPoint(k.a) && f && f->type == EntityType::Ellipse) {
                    const GCS::Point &p = pts[k.a], &c = pts[f->center], &ax = pts[f->p1];
                    const double minor = f->r;
                    auto* con = new detail::FuncConstraint({p.x, p.y, c.x, c.y, ax.x, ax.y}, [minor](const auto& v) {
                        return detail::pointOnEllipse(v, minor);
                    });
                    con->setTag(tag);
                    sys.addConstraint(con);
                }
                else if (isPoint(k.a) && f && f->type == EntityType::Spline && f->pts.size() >= 2) {
                    std::vector<double*> ps {pts[k.a].x, pts[k.a].y};
                    for (int q : f->pts) {
                        ps.push_back(pts[q].x);
                        ps.push_back(pts[q].y);
                    }
                    const bool closed = f->closed, control = f->control;
                    auto* con = new detail::FuncConstraint(std::move(ps), [closed, control](const auto& v) {
                        return detail::pointOnSmooth(v, closed, control);
                    });
                    con->setTag(tag);
                    sys.addConstraint(con);
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
                // Curves that meet at an end: tangent there (angle via that point). The plain
                // tangent form plus the shared end would say one thing twice (B-008).
                auto curveOf = [&](int id) -> GCS::Curve* {
                    if (isLine(id)) {
                        return &lines[id];
                    }
                    if (isArc(id)) {
                        return &arcs[id];
                    }
                    return nullptr;
                };
                auto endsOf = [&](int id) {
                    const SketchEntity* e = entity(id);
                    return e ? std::vector<int> {e->p1, e->p2} : std::vector<int> {};
                };
                auto joined = [&](int p, int q) {
                    if (p == q) {
                        return true;
                    }
                    for (auto& [oid, o] : constraints_) {
                        if (o.type == ConstraintType::Coincident && ((o.a == p && o.b == q) || (o.a == q && o.b == p))) {
                            return true;
                        }
                    }
                    return false;
                };
                // an ellipse or smooth curve
                const SketchEntity* ea = entity(k.a);
                const SketchEntity* eb = entity(k.b);
                auto freeCurve = [](const SketchEntity* e) {
                    return e && (e->type == EntityType::Ellipse || e->type == EntityType::Spline);
                };
                if (freeCurve(ea) || freeCurve(eb)) {
                    const SketchEntity* el = ea && ea->type == EntityType::Ellipse ? ea
                                             : eb && eb->type == EntityType::Ellipse ? eb : nullptr;
                    const int other = el == ea ? k.b : k.a;
                    if (el && isLine(other)) {  // a line touching an ellipse anywhere
                        GCS::Line& l = lines[other];
                        const GCS::Point &c = pts[el->center], &ax = pts[el->p1];
                        const double minor = el->r;
                        auto* con = new detail::FuncConstraint(
                            {l.p1.x, l.p1.y, l.p2.x, l.p2.y, c.x, c.y, ax.x, ax.y},
                            [minor](const auto& v) { return detail::lineTangentToEllipse(v, minor); });
                        con->setTag(tag);
                        sys.addConstraint(con);
                        break;
                    }
                    if (el && (isCircle(other) || isArc(other))) {  // touching it from inside or out
                        double* cx = isCircle(other) ? circles[other].center.x : arcs[other].center.x;
                        double* cy = isCircle(other) ? circles[other].center.y : arcs[other].center.y;
                        double* rad = isCircle(other) ? circles[other].rad : arcs[other].rad;
                        const GCS::Point &c = pts[el->center], &ax = pts[el->p1];
                        const double minor = el->r;
                        auto* con = new detail::FuncConstraint(
                            {cx, cy, rad, c.x, c.y, ax.x, ax.y},
                            [minor](const auto& v) { return detail::circleTangentToEllipse(v, minor); });
                        con->setTag(tag);
                        sys.addConstraint(con);
                        break;
                    }
                    if (el) {
                        return fail(cid, "A curve can be tangent to an ellipse only if it is a line, circle or arc.");
                    }
                    // a smooth curve: tangent where it meets the other curve at one of its ends
                    struct Dir
                    {
                        int end = 0;
                        std::array<double*, 4> v {};
                        bool perp = false;
                    };
                    auto dirsOf = [&](int id) {
                        std::vector<Dir> out;
                        const SketchEntity* e = entity(id);
                        if (isLine(id)) {
                            for (int q : {e->p1, e->p2}) {
                                out.push_back({q, {lines[id].p1.x, lines[id].p1.y, lines[id].p2.x, lines[id].p2.y}});
                            }
                        }
                        else if (isArc(id)) {
                            const GCS::Point& c = pts[e->center];
                            for (int q : {e->p1, e->p2}) {
                                out.push_back({q, {c.x, c.y, pts[q].x, pts[q].y}, true});
                            }
                        }
                        else if (e && e->type == EntityType::Spline && !e->closed && e->pts.size() >= 2) {
                            const size_t n = e->pts.size();
                            const int f0 = e->pts[0], f1 = e->pts[1], b0 = e->pts[n - 1], b1 = e->pts[n - 2];
                            out.push_back({f0, {pts[f0].x, pts[f0].y, pts[f1].x, pts[f1].y}});
                            out.push_back({b0, {pts[b0].x, pts[b0].y, pts[b1].x, pts[b1].y}});
                        }
                        return out;
                    };
                    bool made = false;
                    for (const Dir& da : dirsOf(k.a)) {
                        for (const Dir& db : dirsOf(k.b)) {
                            if (made || !joined(da.end, db.end)) {
                                continue;
                            }
                            std::vector<double*> ps(da.v.begin(), da.v.end());
                            ps.insert(ps.end(), db.v.begin(), db.v.end());
                            const bool pa = da.perp, pb = db.perp;
                            auto* con = new detail::FuncConstraint(std::move(ps), [pa, pb](const auto& v) {
                                return detail::sameDirection(v, pa, pb);
                            });
                            con->setTag(tag);
                            sys.addConstraint(con);
                            made = true;
                        }
                    }
                    if (!made) {
                        return fail(cid, "A smooth curve can be made tangent only where it meets the other curve "
                                         "at one of its ends.");
                    }
                    break;
                }
                int shared = 0;
                if (curveOf(k.a) && curveOf(k.b)) {
                    for (int p : endsOf(k.a)) {
                        for (int q : endsOf(k.b)) {
                            if (!shared && p && q && joined(p, q)) {
                                shared = p;
                            }
                        }
                    }
                }
                if (shared) {
                    GCS::Curve* ca = curveOf(k.a);
                    GCS::Curve* cb = curveOf(k.b);
                    // keep the way they meet now: running on (0) or doubling back (pi)
                    const double now = sys.calculateAngleViaPoint(*ca, *cb, pts[shared]);
                    double* ang = slot(std::abs(std::remainder(now, 2 * std::numbers::pi)) > std::numbers::pi / 2
                                           ? std::numbers::pi
                                           : 0.0);
                    sys.addConstraintAngleViaPoint(*ca, *cb, pts[shared], ang, tag);
                }
                else if (isLine(k.a) && isCircle(k.b)) {
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
                else if (entity(k.a) && entity(k.a)->type == EntityType::Ellipse) {
                    break;  // an ellipse's minor radius is set before solving
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
    // The solver only reports parameters that some constraint involves; one that nothing
    // involves (a lone point, a fit point of a smooth curve) is free as well.
    std::set<double*> touched;
    auto touchPoint = [&](int pid, bool x, bool y) {
        if (auto it = pts.find(pid); it != pts.end()) {
            if (x) {
                touched.insert(it->second.x);
            }
            if (y) {
                touched.insert(it->second.y);
            }
        }
    };
    auto touchAll = [&](int id) {
        touchPoint(id, true, true);
        if (auto it = lines.find(id); it != lines.end()) {
            touched.insert({it->second.p1.x, it->second.p1.y, it->second.p2.x, it->second.p2.y});
        }
        if (auto it = circles.find(id); it != circles.end()) {
            touched.insert({it->second.center.x, it->second.center.y, it->second.rad});
        }
        if (auto it = arcs.find(id); it != arcs.end()) {
            const GCS::Arc& a = it->second;
            touched.insert({a.center.x, a.center.y, a.start.x, a.start.y, a.end.x, a.end.y, a.rad, a.startAngle,
                            a.endAngle});
        }
    };
    for (auto& [id, a] : arcs) {
        touchAll(id);
    }
    for (auto& [cid, k] : constraints_) {
        if (!k.driving || std::find(skip.begin(), skip.end(), cid) != skip.end()) {
            continue;
        }
        const bool hz = k.type == ConstraintType::Horizontal || k.type == ConstraintType::DistanceY;
        const bool vt = k.type == ConstraintType::Vertical || k.type == ConstraintType::DistanceX;
        if (hz || vt) {
            for (int id : {k.a, k.b}) {
                touchPoint(id, vt, hz);
                if (auto it = lines.find(id); it != lines.end()) {
                    touched.insert(vt ? it->second.p1.x : it->second.p1.y);
                    touched.insert(vt ? it->second.p2.x : it->second.p2.y);
                }
            }
            continue;
        }
        for (int id : {k.a, k.b, k.c}) {
            touchAll(id);
        }
    }
    for (double* p : unknowns) {
        if (!touched.count(p)) {
            if (auto it = paramOwner.find(p); it != paramOwner.end()) {
                freeIds.insert(it->second);
            }
        }
    }
    rep.freeEntities.assign(freeIds.begin(), freeIds.end());

    if (rep.dof == 0) {
        rep.state = SolveState::FullyConstrained;
        rep.message = "Fully defined: nothing can move.";
    }
    else {
        rep.state = SolveState::UnderConstrained;
        rep.message = rep.dof == 1 ? std::string("1 degree of freedom left: some geometry can still move.")
                                   : fmt("%1 degrees of freedom left: some geometry can still move.", {std::to_string(rep.dof)});
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
        if (e.type == EntityType::Spline && !e.closed && e.pts.size() >= 2) {
            for (int pid : {e.pts.front(), e.pts.back()}) {
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
            case EntityType::Ellipse:
                j["center"] = e.center;
                j["p1"] = e.p1;
                j["r"] = e.r;
                break;
            case EntityType::Spline:
                j["points"] = e.pts;
                if (e.closed) {
                    j["closed"] = true;
                }
                if (e.control) {
                    j["control"] = true;
                }
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
            if (t.path) {
                jt["path"] = t.path;
            }
            txt.push_back(jt);
        }
        j["texts"] = txt;
    }
    if (!artworks_.empty()) {
        nlohmann::json arts = nlohmann::json::array();
        for (auto& [id, a] : artworks_) {
            nlohmann::json cs = nlohmann::json::array();
            for (auto& c : a.contours) {
                nlohmann::json jc = nlohmann::json::array();
                for (auto& seg : c) {
                    nlohmann::json flat = nlohmann::json::array();
                    for (auto& pt : seg) {
                        flat.push_back(pt[0]);
                        flat.push_back(pt[1]);
                    }
                    jc.push_back(flat);
                }
                cs.push_back(jc);
            }
            arts.push_back({{"id", id}, {"anchor", a.anchor}, {"name", a.name}, {"natural_width", a.naturalWidth},
                            {"width", a.width}, {"width_value", a.widthValue}, {"contours", cs}});
        }
        j["artworks"] = arts;
    }
    if (!links_.empty()) {
        j["links"] = links_;
    }
    return j;
}

bool Sketch::moveFixedPoint(int pointId, double x, double y)
{
    bool moved = false;
    for (auto& [cid, c] : constraints_) {
        if (c.type == ConstraintType::Fix && c.a == pointId) {
            c.fx = x;
            c.fy = y;
            moved = true;
        }
    }
    if (moved) {
        if (auto* e = entity(pointId)) {
            e->x = x;
            e->y = y;
        }
    }
    return moved;
}

bool Sketch::setConstraintExpr(int constraintId, std::string expr)
{
    auto it = constraints_.find(constraintId);
    if (it == constraints_.end()) {
        return false;
    }
    it->second.expr = std::move(expr);
    return true;
}

namespace
{
constexpr int kMaxId = 1'000'000'000;  // ids count up from 1; anything near INT_MAX is damage
bool validId(int id)
{
    return id > 0 && id < kMaxId;
}
}  // namespace

Result<Sketch> Sketch::fromJson(const nlohmann::json& j)
{
    Sketch s;
    try {
        for (auto& je : j.at("entities")) {
            SketchEntity e;
            e.id = je.at("id").get<int>();
            if (!validId(e.id)) {
                return makeError("file.sketch", "A sketch in this file has an impossible id; the file is damaged.");
            }
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
            e.pts = je.value("points", std::vector<int> {});
            e.closed = je.value("closed", false);
            e.control = je.value("control", false);
            s.entities_[e.id] = e;
        }
        // every curve's points must be points of this sketch (a damaged file could name others)
        auto isPointId = [&](int id) {
            auto it = s.entities_.find(id);
            return it != s.entities_.end() && it->second.type == EntityType::Point;
        };
        for (auto& [id, e] : s.entities_) {
            std::vector<int> need;
            switch (e.type) {
                case EntityType::Point:
                    break;
                case EntityType::Line:
                    need = {e.p1, e.p2};
                    break;
                case EntityType::Circle:
                    need = {e.center};
                    break;
                case EntityType::Arc:
                    need = {e.center, e.p1, e.p2};
                    break;
                case EntityType::Ellipse:
                    need = {e.center, e.p1};
                    break;
                case EntityType::Spline:
                    need = e.pts;
                    if (e.pts.size() < 2) {
                        need.push_back(0);  // too few points: refused below
                    }
                    break;
            }
            for (int q : need) {
                if (!isPointId(q)) {
                    return makeError("file.sketch", "A curve in this sketch refers to a point that is not there; the "
                                                    "file is damaged.");
                }
            }
        }
        for (auto& jc : j.at("constraints")) {
            SketchConstraint k;
            k.id = jc.at("id").get<int>();
            if (!validId(k.id)) {
                return makeError("file.sketch", "A sketch in this file has an impossible id; the file is damaged.");
            }
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
            if (!validId(t.id)) {
                return makeError("file.sketch", "A sketch in this file has an impossible id; the file is damaged.");
            }
            t.anchor = jt.at("anchor").get<int>();
            t.text = jt.value("text", std::string {});
            t.height = jt.value("height", std::string("5 mm"));
            t.heightValue = jt.value("height_value", 5.0);
            t.font = jt.value("font", std::string {});
            t.angle = jt.value("angle", 0.0);
            t.path = jt.value("path", 0);
            if (!isPointId(t.anchor)) {
                return makeError("file.sketch", "A text in this file has lost its anchor point.");
            }
            s.texts_[t.id] = t;
        }
        for (auto& ja : j.value("artworks", nlohmann::json::array())) {
            SketchArtwork a;
            a.id = ja.at("id").get<int>();
            if (!validId(a.id)) {
                return makeError("file.sketch", "A sketch in this file has an impossible id; the file is damaged.");
            }
            a.anchor = ja.at("anchor").get<int>();
            a.name = ja.value("name", std::string {});
            a.naturalWidth = ja.value("natural_width", 1.0);
            a.width = ja.value("width", std::string {});
            a.widthValue = ja.value("width_value", 0.0);
            for (auto& jc : ja.value("contours", nlohmann::json::array())) {
                ArtContour c;
                for (auto& flat : jc) {
                    ArtSegment seg;
                    for (size_t k = 0; k + 1 < flat.size(); k += 2) {
                        seg.push_back({flat[k].get<double>(), flat[k + 1].get<double>()});
                    }
                    if (seg.size() >= 2 && seg.size() <= 4) {
                        c.push_back(std::move(seg));
                    }
                }
                a.contours.push_back(std::move(c));
            }
            if (!isPointId(a.anchor)) {
                return makeError("file.sketch", "Artwork in this file has lost its anchor point.");
            }
            s.artworks_[a.id] = std::move(a);
        }
        for (auto& jl : j.value("links", nlohmann::json::array())) {
            // a link names curves of this sketch: its lists must be lists of numbers, and the
            // copies it made never include what it copies
            if (!jl.is_object()) {
                return makeError("file.sketch", "A linked copy in this sketch is damaged.");
            }
            for (const char* key : {"source", "made", "points"}) {
                if (jl.contains(key) && (!jl[key].is_array() || !std::all_of(jl[key].begin(), jl[key].end(), [](const auto& v) {
                                             return v.is_number_integer();
                                         }))) {
                    return makeError("file.sketch", "A linked copy in this sketch is damaged.");
                }
            }
            if ((jl.contains("kind") && !jl["kind"].is_string()) ||
                (jl.contains("distance") && !jl["distance"].is_string()) ||
                (jl.contains("spec") && !jl["spec"].is_object()) ||
                (jl.contains("radius") && !jl["radius"].is_number_integer())) {
                return makeError("file.sketch", "A linked copy in this sketch is damaged.");
            }
            const auto src = jl.value("source", std::vector<int> {});
            for (int m : jl.value("made", std::vector<int> {})) {
                if (std::find(src.begin(), src.end(), m) != src.end()) {
                    return makeError("file.sketch", "A linked copy in this sketch is damaged.");
                }
            }
            s.links_.push_back(jl);
        }
        s.nextId_ = std::clamp(j.value("next_id", 1), 1, kMaxId);
        int maxId = 0;
        for (auto& [id, t] : s.texts_) {
            maxId = std::max(maxId, id);
        }
        for (auto& [id, a] : s.artworks_) {
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
