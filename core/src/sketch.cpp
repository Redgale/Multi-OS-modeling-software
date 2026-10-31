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
