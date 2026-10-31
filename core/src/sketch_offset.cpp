// SPDX-License-Identifier: GPL-3.0-or-later
// Sketch Offset: parallel copies of curves. Closed outlines go through OCCT's planar
// offset (sharp corners), and the result is turned back into sketch lines and arcs.
#include <cmath>
#include <map>
#include <numbers>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>

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
                return makeError("offset.too_far", "An offset of " + std::to_string(-d) +
                                                       " mm inward is bigger than the radius.",
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

Result<std::vector<int>> projectEdge(Sketch& s, const gp_Ax3& frame, const TopoDS_Shape& shape)
{
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
        made.push_back(s.addLine(pointAt(ax, ay), pointAt(bx, by)));
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
            s.addConstraint(ConstraintType::Fix, s.entity(id)->center);
            s.addConstraint(ConstraintType::Radius, id, 0, 0, std::to_string(r) + " mm");
            made.push_back(id);
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
        return made;
    }
    return makeError("project.curve", "Only straight and round edges can be projected yet.");
}

}  // namespace jig
