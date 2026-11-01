// SPDX-License-Identifier: GPL-3.0-or-later
// Sketch Offset: parallel copies of curves. Closed outlines go through OCCT's planar
// offset (sharp corners), and the result is turned back into sketch lines and arcs.
#include <cmath>
#include <map>
#include <numbers>

#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>
#include <GProp_GProps.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeFix_Face.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_HSequenceOfShape.hxx>

#include "jig/kernel.hpp"

namespace jig
{
namespace
{
constexpr double kTau = 2 * std::numbers::pi;

double ccw(double from, double to)  // counter-clockwise angle from `from` to `to`, in [0, 2pi)
{
    double d = std::fmod(to - from, kTau);
    return d < 0 ? d + kTau : d;
}
}  // namespace

Result<std::vector<int>> offsetCurves(Sketch& s, const std::vector<int>& curves, double d)
{
    if (curves.empty()) {
        return makeError("offset.empty", "Pick a curve or a closed outline to offset.");
    }
    if (std::abs(d) < 1e-9) {
        return makeError("offset.zero", "The offset distance must not be zero.");
    }
    std::vector<int> made;
    if (curves.size() == 1) {
        const SketchEntity e = *s.entity(curves[0]);
        if (e.type == EntityType::Line) {
            auto [ax, ay] = s.pointXY(e.p1);
            auto [bx, by] = s.pointXY(e.p2);
            const double L = std::hypot(bx - ax, by - ay);
            const double nx = -(by - ay) / L * d, ny = (bx - ax) / L * d;  // to the left
            const int l = s.addLine(ax + nx, ay + ny, bx + nx, by + ny);
            s.addConstraint(ConstraintType::Parallel, l, curves[0]);
            return std::vector<int> {l};
        }
        if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
            if (e.r + d <= 1e-9) {
                return makeError("offset.too_far", fmt("An offset of %1 inward is bigger than the radius.",
                                                           {formatQuantity(Quantity {-d, Dim::len()})}),
                                 {"Use a smaller distance."});
            }
            auto [cx, cy] = s.pointXY(e.center);
            const int c = e.type == EntityType::Circle ? s.addCircle(cx, cy, e.r + d)
                                                       : s.addArc(cx, cy, e.r + d, e.a0, e.a1);
            s.addConstraint(ConstraintType::Coincident, s.entity(c)->center, e.center);  // stays concentric
            return std::vector<int> {c};
        }
        return makeError("offset.kind", "Only lines, circles and arcs can be offset.");
    }
    try {
        // the outline as edges in the plane z = 0 (sketch coordinates)
        Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape;
        for (int id : curves) {
            const SketchEntity& e = *s.entity(id);
            if (e.type == EntityType::Line) {
                auto [ax, ay] = s.pointXY(e.p1);
                auto [bx, by] = s.pointXY(e.p2);
                edges->Append(BRepBuilderAPI_MakeEdge(gp_Pnt(ax, ay, 0), gp_Pnt(bx, by, 0)).Edge());
            }
            else if (e.type == EntityType::Arc) {
                auto [cx, cy] = s.pointXY(e.center);
                gp_Circ circ(gp_Ax2(gp_Pnt(cx, cy, 0), gp_Dir(0, 0, 1)), e.r);
                double a1 = e.a1;
                while (a1 <= e.a0) {
                    a1 += kTau;
                }
                edges->Append(BRepBuilderAPI_MakeEdge(circ, e.a0, a1).Edge());
            }
        }
        Handle(TopTools_HSequenceOfShape) wires;
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, 1e-6, false, wires);
        bool closed = false;
        if (wires->Length() == 1) {
            TopoDS_Vertex v1, v2;
            TopExp::Vertices(TopoDS::Wire(wires->Value(1)), v1, v2);
            closed = v1.IsNull() || v1.IsSame(v2);
        }
        if (!closed) {
            return makeError("offset.open", "Offset works on one closed outline, or on a single curve.",
                             {"Close the outline first, or offset the curves one at a time."});
        }
        const TopoDS_Face face = BRepBuilderAPI_MakeFace(TopoDS::Wire(wires->Value(1)), true).Face();
        BRepOffsetAPI_MakeOffset off(face, GeomAbs_Intersection);
        off.Perform(d);
        if (!off.IsDone()) {
            return makeError("offset.failed", "This outline could not be offset by that distance.",
                             {"Try a smaller distance: an inward offset must fit inside the outline."});
        }
        std::map<std::pair<long long, long long>, int> points;
        auto pointId = [&](const gp_Pnt& p) {
            const std::pair<long long, long long> k {std::llround(p.X() * 1e6), std::llround(p.Y() * 1e6)};
            auto it = points.find(k);
            if (it != points.end()) {
                return it->second;
            }
            const int id = s.addPoint(p.X(), p.Y(), true);
            points[k] = id;
            return id;
        };
        int wiresMade = 0;
        for (TopExp_Explorer wx(off.Shape(), TopAbs_WIRE); wx.More(); wx.Next()) {
            ++wiresMade;
            for (BRepTools_WireExplorer ex(TopoDS::Wire(wx.Current())); ex.More(); ex.Next()) {
                const TopoDS_Edge& edge = ex.Current();
                const gp_Pnt a = BRep_Tool::Pnt(TopExp::FirstVertex(edge, true));
                const gp_Pnt b = BRep_Tool::Pnt(TopExp::LastVertex(edge, true));
                BRepAdaptor_Curve c(edge);
                if (c.GetType() == GeomAbs_Line) {
                    if (a.Distance(b) > 1e-9) {
                        made.push_back(s.addLine(pointId(a), pointId(b)));
                    }
                }
                else if (c.GetType() == GeomAbs_Circle) {
                    const gp_Pnt ctr = c.Circle().Location();
                    const gp_Pnt m = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
                    const double aa = std::atan2(a.Y() - ctr.Y(), a.X() - ctr.X());
                    const double ab = std::atan2(b.Y() - ctr.Y(), b.X() - ctr.X());
                    const double am = std::atan2(m.Y() - ctr.Y(), m.X() - ctr.X());
                    const bool aToB = ccw(aa, am) < ccw(aa, ab);  // counter-clockwise from a to b?
                    const int center = s.addPoint(ctr.X(), ctr.Y(), true);
                    const int p1 = aToB ? pointId(a) : pointId(b), p2 = aToB ? pointId(b) : pointId(a);
                    const double a0 = aToB ? aa : ab, a1 = aToB ? ab : aa;
                    made.push_back(s.addArcOn(center, p1, p2, c.Circle().Radius(), a0, a1 <= a0 ? a1 + kTau : a1));
                }
                else {
                    return makeError("offset.curve", "The offset made a curve that sketches cannot hold.");
                }
            }
        }
        if (wiresMade != 1 || made.empty()) {
            return makeError("offset.failed", "This outline could not be offset by that distance.",
                             {"Try a smaller distance: an inward offset must fit inside the outline."});
        }
        return made;
    }
    catch (const Standard_Failure& f) {
        const char* m = f.GetMessageString();
        return makeError("offset.failed", "This outline could not be offset by that distance.", {}, m ? m : "");
    }
}

Result<std::vector<int>> projectEdge(Sketch& s, const gp_Ax3& frame, const TopoDS_Shape& shape, const nlohmann::json& ref)
{
    const bool link = !ref.is_null();
    if (shape.IsNull() || shape.ShapeType() != TopAbs_EDGE) {
        return makeError("project.kind", "Pick an edge to project.");
    }
    const TopoDS_Edge& edge = TopoDS::Edge(shape);
    BRepAdaptor_Curve c(edge);
    auto local = [&](const gp_Pnt& p) {
        const gp_Vec v(frame.Location(), p);
        return std::pair {v.Dot(gp_Vec(frame.XDirection())), v.Dot(gp_Vec(frame.YDirection()))};
    };
    // an existing sketch point at the same place is reused, so outlines stay closed
    auto pointAt = [&](double x, double y) {
        for (auto& [id, e] : s.entities()) {
            if (e.type == EntityType::Point) {
                auto [px, py] = s.pointXY(id);
                if (std::hypot(px - x, py - y) < 1e-6) {
                    return id;
                }
            }
        }
        const int id = s.addPoint(x, y, true);
        s.addConstraint(ConstraintType::Fix, id);
        return id;
    };
    std::vector<int> made;
    const gp_Pnt a = BRep_Tool::Pnt(TopExp::FirstVertex(edge, true)), b = BRep_Tool::Pnt(TopExp::LastVertex(edge, true));
    if (c.GetType() == GeomAbs_Line) {
        auto [ax, ay] = local(a);
        auto [bx, by] = local(b);
        if (std::hypot(bx - ax, by - ay) < 1e-7) {
            return makeError("project.point", "This edge points straight at the sketch, so it projects to a point.");
        }
        const int pa = pointAt(ax, ay), pb = pointAt(bx, by);
        made.push_back(s.addLine(pa, pb));
        if (link) {
            s.addLink({{"ref", ref}, {"kind", "line"}, {"points", {pa, pb}}});
        }
        return made;
    }
    if (c.GetType() == GeomAbs_Circle) {
        const gp_Circ circ = c.Circle();
        if (!circ.Axis().Direction().IsParallel(frame.Direction(), 1e-6)) {
            return makeError("project.slant", "This round edge is tilted to the sketch, so it would project to an "
                                              "ellipse, which sketches cannot hold yet.",
                             {"Project it in a sketch parallel to the edge's face."});
        }
        auto [cx, cy] = local(circ.Location());
        const double r = circ.Radius();
        const bool closed = BRep_Tool::IsClosed(edge) || a.Distance(b) < 1e-7;
        if (closed) {
            const int id = s.addCircle(cx, cy, r);
            const int centre = s.entity(id)->center;
            s.addConstraint(ConstraintType::Fix, centre);
            const int rc = s.addConstraint(ConstraintType::Radius, id, 0, 0, std::to_string(r) + " mm");
            made.push_back(id);
            if (link) {
                s.addLink({{"ref", ref}, {"kind", "circle"}, {"points", {centre}}, {"radius", rc}});
            }
            return made;
        }
        // an arc: counter-clockwise in the sketch, from whichever end that makes it
        auto [ax, ay] = local(a);
        auto [bx, by] = local(b);
        const gp_Pnt m = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
        auto [mx, my] = local(m);
        auto ang = [&](double x, double y) { return std::atan2(y - cy, x - cx); };
        const bool aToB = ccw(ang(ax, ay), ang(mx, my)) < ccw(ang(ax, ay), ang(bx, by));
        const double a0 = aToB ? ang(ax, ay) : ang(bx, by);
        const double sweep = aToB ? ccw(ang(ax, ay), ang(bx, by)) : ccw(ang(bx, by), ang(ax, ay));
        const int center = s.addPoint(cx, cy, true);
        s.addConstraint(ConstraintType::Fix, center);
        const int p1 = aToB ? pointAt(ax, ay) : pointAt(bx, by);
        const int p2 = aToB ? pointAt(bx, by) : pointAt(ax, ay);
        made.push_back(s.addArcOn(center, p1, p2, r, a0, a0 + sweep));
        if (link) {
            // the points at the edge's first and last ends, whichever way the arc runs
            s.addLink({{"ref", ref}, {"kind", "arc"}, {"points", {center, aToB ? p1 : p2, aToB ? p2 : p1}}});
        }
        return made;
    }
    return makeError("project.curve", "Only straight and round edges can be projected yet.");
}

Result<bool> followLink(Sketch& s, const gp_Ax3& frame, const nlohmann::json& link, const TopoDS_Shape& shape)
{
    if (shape.IsNull() || shape.ShapeType() != TopAbs_EDGE) {
        return makeError("project.gone", "A projected edge is gone; its copy stays where it was.");
    }
    const TopoDS_Edge& edge = TopoDS::Edge(shape);
    BRepAdaptor_Curve c(edge);
    auto local = [&](const gp_Pnt& p) {
        const gp_Vec v(frame.Location(), p);
        return std::pair {v.Dot(gp_Vec(frame.XDirection())), v.Dot(gp_Vec(frame.YDirection()))};
    };
    const std::string kind = link.value("kind", std::string {});
    const auto pts = link.value("points", std::vector<int> {});
    const gp_Pnt a = BRep_Tool::Pnt(TopExp::FirstVertex(edge, true)), b = BRep_Tool::Pnt(TopExp::LastVertex(edge, true));
    auto move = [&](int id, const gp_Pnt& p) {
        auto [x, y] = local(p);
        s.moveFixedPoint(id, x, y);
    };
    if (kind == "line" && c.GetType() == GeomAbs_Line && pts.size() == 2) {
        move(pts[0], a);
        move(pts[1], b);
        return true;
    }
    if ((kind == "circle" || kind == "arc") && c.GetType() == GeomAbs_Circle && !pts.empty()) {
        const gp_Circ circ = c.Circle();
        move(pts[0], circ.Location());
        if (kind == "circle" && link.contains("radius")) {
            s.setConstraintExpr(link["radius"].get<int>(), std::to_string(circ.Radius()) + " mm");
        }
        if (kind == "arc" && pts.size() == 3) {
            move(pts[1], a);
            move(pts[2], b);
        }
        return true;
    }
    return makeError("project.changed", "A projected edge changed shape; its copy stays where it was.",
                     {"Delete the copy and project the edge again."});
}

// ---------------------------------------------------------------- walls along open curves

Result<std::vector<Profile>> openChainWalls(const Sketch& s, const gp_Ax3& frame, double t)
{
    if (!(t > 1e-9)) {
        return makeError("extrude.thin", "A wall along open curves needs a thickness.");
    }
    auto to3d = [&](double x, double y) {
        return gp_Pnt(frame.Location().XYZ() + frame.XDirection().XYZ() * x + frame.YDirection().XYZ() * y);
    };
    // curves that can form a wall, and their two ends
    struct Piece
    {
        int id;
        std::pair<double, double> a, b;
    };
    std::vector<Piece> pieces;
    for (auto& [id, e] : s.entities()) {
        if (e.construction) {
            continue;
        }
        if (e.type == EntityType::Line || e.type == EntityType::Arc) {
            pieces.push_back({id, s.pointXY(e.p1), s.pointXY(e.p2)});
        }
        else if (e.type == EntityType::Spline && !e.closed && e.pts.size() >= 2) {
            pieces.push_back({id, s.pointXY(e.pts.front()), s.pointXY(e.pts.back())});
        }
    }
    auto same = [](std::pair<double, double> p, std::pair<double, double> q) {
        return std::hypot(p.first - q.first, p.second - q.second) < 1e-6;
    };
    // group pieces that touch end to end
    std::vector<int> group(pieces.size(), -1);
    int groups = 0;
    for (size_t i = 0; i < pieces.size(); ++i) {
        if (group[i] >= 0) {
            continue;
        }
        std::vector<size_t> stack {i};
        group[i] = groups;
        while (!stack.empty()) {
            const size_t k = stack.back();
            stack.pop_back();
            for (size_t j = 0; j < pieces.size(); ++j) {
                if (group[j] < 0 && (same(pieces[k].a, pieces[j].a) || same(pieces[k].a, pieces[j].b) ||
                                     same(pieces[k].b, pieces[j].a) || same(pieces[k].b, pieces[j].b))) {
                    group[j] = groups;
                    stack.push_back(j);
                }
            }
        }
        ++groups;
    }
    std::vector<Profile> out;
    try {
        for (int g = 0; g < groups; ++g) {
            std::vector<size_t> members;
            for (size_t i = 0; i < pieces.size(); ++i) {
                if (group[i] == g) {
                    members.push_back(i);
                }
            }
            // open when some end belongs to only one piece
            bool open = false;
            for (size_t i : members) {
                for (auto end : {pieces[i].a, pieces[i].b}) {
                    int n = 0;
                    for (size_t j : members) {
                        n += same(end, pieces[j].a) + same(end, pieces[j].b);
                    }
                    open |= n == 1;
                }
            }
            if (!open) {
                continue;
            }
            // The band: a strip along each line, a ring sector along each arc (a smooth curve
            // as short straight strips), and a mitre fill at every joint, fused into one area.
            const double h = t / 2;
            using P2 = std::pair<double, double>;
            // every part on the sketch plane itself, facing the same way, so the parts merge
            const gp_Pln pln(frame);
            auto planarFace = [&](const TopoDS_Wire& w) {
                TopoDS_Face f = BRepBuilderAPI_MakeFace(pln, w, true).Face();
                ShapeFix_Face fix(f);  // orients the wire to bound the area, whichever way it runs
                fix.Perform();
                return TopoDS_Shape(fix.Face());
            };
            auto rectFace = [&](P2 a, P2 b, P2 c, P2 d) {
                BRepBuilderAPI_MakePolygon poly(to3d(a.first, a.second), to3d(b.first, b.second),
                                                to3d(c.first, c.second), to3d(d.first, d.second), Standard_True);
                return planarFace(poly.Wire());
            };
            std::vector<TopoDS_Shape> parts;
            // per joint: the end point and the direction each piece leaves it in
            std::vector<std::pair<P2, std::pair<double, double>>> ends;
            auto strip = [&](P2 a, P2 b) {
                const double L = std::hypot(b.first - a.first, b.second - a.second);
                if (L < 1e-9) {
                    return;
                }
                const double nx = -(b.second - a.second) / L * h, ny = (b.first - a.first) / L * h;
                parts.push_back(rectFace({a.first + nx, a.second + ny}, {b.first + nx, b.second + ny},
                                         {b.first - nx, b.second - ny}, {a.first - nx, a.second - ny}));
                ends.push_back({a, {(b.first - a.first) / L, (b.second - a.second) / L}});
                ends.push_back({b, {(a.first - b.first) / L, (a.second - b.second) / L}});
            };
            int firstId = 0;
            for (size_t i : members) {
                const SketchEntity& e = *s.entity(pieces[i].id);
                firstId = firstId ? std::min(firstId, e.id) : e.id;
                if (e.type == EntityType::Line) {
                    strip(pieces[i].a, pieces[i].b);
                }
                else if (e.type == EntityType::Arc) {
                    if (e.r - h <= 1e-9) {
                        return makeError("extrude.thin", "The wall is thicker than an arc is round.",
                                         {"Use a thinner wall or a bigger arc."});
                    }
                    auto [cx, cy] = s.pointXY(e.center);
                    double a0 = e.a0, a1 = e.a1;
                    while (a1 <= a0) {
                        a1 += kTau;
                    }
                    const gp_Ax2 ax(to3d(cx, cy), frame.Direction(), frame.XDirection());
                    const TopoDS_Edge outer = BRepBuilderAPI_MakeEdge(gp_Circ(ax, e.r + h), a0, a1).Edge();
                    const TopoDS_Edge inner = BRepBuilderAPI_MakeEdge(gp_Circ(ax, e.r - h), a0, a1).Edge();
                    auto at = [&](double r, double a) { return to3d(cx + r * std::cos(a), cy + r * std::sin(a)); };
                    BRepBuilderAPI_MakeWire w;
                    w.Add(outer);
                    w.Add(BRepBuilderAPI_MakeEdge(at(e.r + h, a1), at(e.r - h, a1)).Edge());
                    w.Add(inner);
                    w.Add(BRepBuilderAPI_MakeEdge(at(e.r - h, a0), at(e.r + h, a0)).Edge());
                    parts.push_back(planarFace(w.Wire()));
                    // leaving each end along the tangent, away from the arc
                    ends.push_back({pieces[i].a, {std::sin(a0), -std::cos(a0)}});
                    ends.push_back({pieces[i].b, {-std::sin(a1), std::cos(a1)}});
                    // (the stored direction points into the arc, as for strips)
                    ends[ends.size() - 2].second = {-std::sin(a0), std::cos(a0)};
                    ends[ends.size() - 1].second = {std::sin(a1), -std::cos(a1)};
                }
                else {
                    auto pts = s.curvePoints(e.id, 16);
                    for (size_t k = 0; k + 1 < pts.size(); ++k) {
                        strip(pts[k], pts[k + 1]);
                    }
                }
            }
            // mitre fills where two pieces meet at an angle
            for (size_t x = 0; x < ends.size(); ++x) {
                for (size_t y = x + 1; y < ends.size(); ++y) {
                    if (!same(ends[x].first, ends[y].first)) {
                        continue;
                    }
                    const P2 J = ends[x].first;
                    const auto [ux, uy] = ends[x].second;  // into piece x
                    const auto [vx, vy] = ends[y].second;  // into piece y
                    const double cross = ux * vy - uy * vx;
                    if (std::abs(cross) < 1e-9) {
                        continue;  // straight on: nothing to fill
                    }
                    for (double side : {1.0, -1.0}) {
                        // the edge of each strip on this side, as lines through J + n*h along -u, -v
                        const P2 p1 {J.first - uy * h * side, J.second + ux * h * side};
                        const P2 p2 {J.first + vy * h * side, J.second - vx * h * side};
                        // where those two edges meet
                        const double den = ux * vy - uy * vx;
                        const double tt = ((p2.first - p1.first) * vy - (p2.second - p1.second) * vx) / den;
                        const P2 X {p1.first + ux * tt, p1.second + uy * tt};
                        if (std::hypot(X.first - J.first, X.second - J.second) > 50 * h) {
                            continue;  // nearly folded back: leave it square
                        }
                        parts.push_back(rectFace(J, p1, X, p2));
                    }
                }
            }
            if (parts.empty()) {
                continue;
            }
            TopoDS_Shape fused = parts[0];
            for (size_t k = 1; k < parts.size(); ++k) {
                BRepAlgoAPI_Fuse fu(fused, parts[k]);
                fu.Build();
                if (!fu.IsDone()) {
                    return makeError("extrude.thin", "A wall along these curves could not be built.", {"Try a thinner wall."});
                }
                fused = fu.Shape();
            }
            ShapeUpgrade_UnifySameDomain unify(fused, Standard_True, Standard_True, Standard_True);
            unify.Build();
            TopoDS_Face band;
            for (TopExp_Explorer fx(unify.Shape(), TopAbs_FACE); fx.More() && band.IsNull(); fx.Next()) {
                band = TopoDS::Face(fx.Current());
            }
            if (band.IsNull()) {
                return makeError("extrude.thin", "A wall of this thickness does not fit along these curves.");
            }
            Profile p;
            p.key = "W" + std::to_string(firstId);
            p.face = band;
            GProp_GProps gp;
            BRepGProp::SurfaceProperties(band, gp);
            p.area = gp.Mass();
            out.push_back(std::move(p));
        }
    }
    catch (const Standard_Failure&) {
        return makeError("extrude.thin", "A wall along these curves could not be built.", {"Try a thinner wall."});
    }
    if (out.empty()) {
        return makeError("extrude.thin", "This sketch has no open curves to build a wall along.",
                         {"Draw a line or a chain of lines that does not close."});
    }
    return out;
}

}  // namespace jig
