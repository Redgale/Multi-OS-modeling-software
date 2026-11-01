// SPDX-License-Identifier: GPL-3.0-or-later
#include "jig/kernel.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <set>
#include <sstream>
#include <typeinfo>

#include <Bnd_Box.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepGProp.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepTools_History.hxx>
#include <ElSLib.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <Geom_BSplineCurve.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir2d.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <GProp_GProps.hxx>
#include <Interface_Static.hxx>
#include <LocOpe_DPrism.hxx>
#include <Poly_Triangulation.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <STEPControl_Reader.hxx>
#include <GCPnts_AbscissaPoint.hxx>
#include <BRepAdaptor_CompCurve.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <BRepTools.hxx>
#include <STEPControl_Writer.hxx>
#include <StlAPI_Writer.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColStd_Array1OfReal.hxx>
#include <TColStd_SequenceOfAsciiString.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>

namespace jig
{
namespace
{

std::string kernelText(const Standard_Failure& f)
{
    const char* m = f.GetMessageString();
    // typeid rather than DynamicType(): OCCT 8 no longer makes Standard_Failure a transient.
    return std::string(typeid(f).name()) + ": " + (m ? m : "");
}

std::string fmtMm(double v)
{
    std::ostringstream os;
    os.precision(v < 10 ? 2 : 3);
    os << v << " mm";
    return os.str();
}

std::string surfaceTypeName(GeomAbs_SurfaceType t)
{
    switch (t) {
        case GeomAbs_Plane:
            return "plane";
        case GeomAbs_Cylinder:
            return "cylinder";
        case GeomAbs_Cone:
            return "cone";
        case GeomAbs_Sphere:
            return "sphere";
        case GeomAbs_Torus:
            return "torus";
        default:
            return "freeform";
    }
}

std::string curveTypeName(GeomAbs_CurveType t)
{
    switch (t) {
        case GeomAbs_Line:
            return "line";
        case GeomAbs_Circle:
            return "circle";
        case GeomAbs_Ellipse:
            return "ellipse";
        default:
            return "freeform";
    }
}

gp_Pnt centroidOf(const TopoDS_Shape& s)
{
    GProp_GProps g;
    if (s.ShapeType() == TopAbs_FACE) {
        BRepGProp::SurfaceProperties(s, g);
    }
    else {
        BRepGProp::LinearProperties(s, g);
    }
    return g.CentreOfMass();
}

// Deterministic order for sub-shapes that share a name: by centroid, x then y then z.
void sortByCentroid(std::vector<TopoDS_Shape>& v)
{
    std::vector<std::pair<gp_Pnt, TopoDS_Shape>> tmp;
    for (auto& s : v) {
        tmp.emplace_back(centroidOf(s), s);
    }
    auto less = [](const gp_Pnt& a, const gp_Pnt& b) {
        constexpr double tol = 1e-6;
        if (std::abs(a.X() - b.X()) > tol) {
            return a.X() < b.X();
        }
        if (std::abs(a.Y() - b.Y()) > tol) {
            return a.Y() < b.Y();
        }
        return a.Z() < b.Z();
    };
    std::stable_sort(tmp.begin(), tmp.end(), [&](auto& a, auto& b) { return less(a.first, b.first); });
    for (size_t i = 0; i < v.size(); ++i) {
        v[i] = tmp[i].second;
    }
}

std::vector<TopoDS_Shape> toVector(const TopTools_ListOfShape& l, TopAbs_ShapeEnum type)
{
    std::vector<TopoDS_Shape> v;
    for (TopTools_ListIteratorOfListOfShape it(l); it.More(); it.Next()) {
        if (it.Value().ShapeType() == type) {
            v.push_back(it.Value());
        }
    }
    return v;
}

// Give `name` to faces in `out` (suffix ~k when the face was split in several).
void assignNames(NamedShape& out, std::vector<TopoDS_Shape> faces, const std::string& name)
{
    std::vector<TopoDS_Shape> present;
    for (auto& f : faces) {
        int i = out.faceIndex(f);
        if (i >= 0 && out.faceName(i).empty()) {
            present.push_back(f);
        }
    }
    if (present.size() == 1) {
        out.setFaceName(out.faceIndex(present[0]), name);
        return;
    }
    sortByCentroid(present);
    for (size_t k = 0; k < present.size(); ++k) {
        out.setFaceName(out.faceIndex(present[k]), name + "~" + std::to_string(k));
    }
}

// Carries face names from the inputs of a history-producing operation into its result.
// Inputs earlier in the list win when two faces map to the same result face.
void propagateFaceNames(BRepBuilderAPI_MakeShape& op,
                        const std::vector<const NamedShape*>& inputs,
                        NamedShape& out)
{
    for (const NamedShape* in : inputs) {
        for (int i = 0; i < in->faceCount(); ++i) {
            const TopoDS_Shape& f = in->face(i);
            if (op.IsDeleted(f)) {
                continue;
            }
            const TopTools_ListOfShape& mods = op.Modified(f);
            if (mods.IsEmpty()) {
                int j = out.faceIndex(f);
                if (j >= 0 && out.faceName(j).empty()) {
                    out.setFaceName(j, in->faceName(i));
                }
            }
            else {
                assignNames(out, toVector(mods, TopAbs_FACE), in->faceName(i));
            }
        }
    }
}

void nameLeftovers(NamedShape& out, const std::string& featureId)
{
    std::vector<TopoDS_Shape> unnamed;
    for (int i = 0; i < out.faceCount(); ++i) {
        if (out.faceName(i).empty()) {
            unnamed.push_back(out.face(i));
        }
    }
    if (!unnamed.empty()) {
        assignNames(out, unnamed, featureId + "/new");
    }
    out.deriveEdgeNames();
}

double fpScore(const Fingerprint& a, const Fingerprint& b, double scale)
{
    double s = 0;
    if (a.type != b.type) {
        s += 10;
    }
    double d = std::sqrt((a.cx - b.cx) * (a.cx - b.cx) + (a.cy - b.cy) * (a.cy - b.cy)
                         + (a.cz - b.cz) * (a.cz - b.cz));
    s += d / std::max(scale, 1e-9);
    double m = std::max({std::abs(a.size), std::abs(b.size), 1e-9});
    s += std::abs(a.size - b.size) / m;
    double na = a.nx * a.nx + a.ny * a.ny + a.nz * a.nz;
    double nb = b.nx * b.nx + b.ny * b.ny + b.nz * b.nz;
    if (na > 0.5 && nb > 0.5) {
        double dot = a.nx * b.nx + a.ny * b.ny + a.nz * b.nz;
        s += (1.0 - dot);  // 0 when parallel, 2 when opposite
    }
    return s;
}

double bodyScale(const TopoDS_Shape& s)
{
    Bnd_Box b;
    BRepBndLib::Add(s, b);
    if (b.IsVoid()) {
        return 1;
    }
    return std::sqrt(b.SquareExtent());
}

}  // namespace

// ---------------------------------------------------------------- Fingerprint / ShapeRef

nlohmann::json Fingerprint::toJson() const
{
    return {{"type", type}, {"c", {cx, cy, cz}}, {"size", size}, {"n", {nx, ny, nz}}};
}

Fingerprint Fingerprint::fromJson(const nlohmann::json& j)
{
    Fingerprint f;
    f.type = j.value("type", std::string {});
    if (j.contains("c") && j["c"].size() == 3) {
        f.cx = j["c"][0];
        f.cy = j["c"][1];
        f.cz = j["c"][2];
    }
    f.size = j.value("size", 0.0);
    if (j.contains("n") && j["n"].size() == 3) {
        f.nx = j["n"][0];
        f.ny = j["n"][1];
        f.nz = j["n"][2];
    }
    return f;
}

nlohmann::json ShapeRef::toJson() const
{
    return {{"kind", kind == SubShape::Face ? "face" : "edge"},
            {"body", body},
            {"name", name},
            {"fp", fp.toJson()}};
}

ShapeRef ShapeRef::fromJson(const nlohmann::json& j)
{
    ShapeRef r;
    r.kind = j.value("kind", std::string("edge")) == "face" ? SubShape::Face : SubShape::Edge;
    r.body = j.value("body", std::string {});
    r.name = j.value("name", std::string {});
    if (j.contains("fp")) {
        r.fp = Fingerprint::fromJson(j["fp"]);
    }
    return r;
}

// ---------------------------------------------------------------- NamedShape

NamedShape::NamedShape(TopoDS_Shape s)
    : shape_(std::move(s))
{
    TopExp::MapShapes(shape_, TopAbs_FACE, faces_);
    TopExp::MapShapes(shape_, TopAbs_EDGE, edges_);
    faceNames_.assign(faces_.Extent(), {});
    edgeNames_.assign(edges_.Extent(), {});
}

std::vector<int> NamedShape::findFaces(const std::string& name) const
{
    std::vector<int> r;
    for (int i = 0; i < faceCount(); ++i) {
        if (faceNames_[i] == name) {
            r.push_back(i);
        }
    }
    return r;
}

std::vector<int> NamedShape::findEdges(const std::string& name) const
{
    std::vector<int> r;
    for (int i = 0; i < edgeCount(); ++i) {
        if (edgeNames_[i] == name) {
            r.push_back(i);
        }
    }
    return r;
}

std::vector<int> NamedShape::edgesOfFace(int faceIdx) const
{
    std::set<int> r;
    for (TopExp_Explorer ex(face(faceIdx), TopAbs_EDGE); ex.More(); ex.Next()) {
        int i = edgeIndex(ex.Current());
        if (i >= 0) {
            r.insert(i);
        }
    }
    return {r.begin(), r.end()};
}

std::vector<int> NamedShape::facesOfEdge(int edgeIdx) const
{
    std::vector<int> r;
    for (int i = 0; i < faceCount(); ++i) {
        for (TopExp_Explorer ex(face(i), TopAbs_EDGE); ex.More(); ex.Next()) {
            if (ex.Current().IsSame(edge(edgeIdx))) {
                r.push_back(i);
                break;
            }
        }
    }
    return r;
}

void NamedShape::deriveEdgeNames()
{
    TopTools_IndexedDataMapOfShapeListOfShape anc;
    TopExp::MapShapesAndAncestors(shape_, TopAbs_EDGE, TopAbs_FACE, anc);
    std::map<std::string, std::vector<int>> groups;
    for (int i = 0; i < edgeCount(); ++i) {
        std::vector<std::string> names;
        int k = anc.FindIndex(edge(i));
        if (k > 0) {
            std::set<int> seen;
            for (TopTools_ListIteratorOfListOfShape it(anc(k)); it.More(); it.Next()) {
                int fi = faceIndex(it.Value());
                if (fi >= 0 && seen.insert(fi).second) {
                    names.push_back(faceNames_[fi].empty() ? "?" : faceNames_[fi]);
                }
            }
        }
        std::sort(names.begin(), names.end());
        std::string base = "E[";
        for (size_t n = 0; n < names.size(); ++n) {
            base += (n ? "|" : "") + names[n];
        }
        base += "]";
        groups[base].push_back(i);
    }
    for (auto& [base, idx] : groups) {
        if (idx.size() == 1) {
            edgeNames_[idx[0]] = base;
            continue;
        }
        std::vector<TopoDS_Shape> es;
        for (int i : idx) {
            es.push_back(edge(i));
        }
        sortByCentroid(es);
        for (size_t k = 0; k < es.size(); ++k) {
            edgeNames_[edgeIndex(es[k])] = base + "#" + std::to_string(k);
        }
    }
}

Fingerprint NamedShape::faceFingerprint(int i) const
{
    Fingerprint fp;
    const TopoDS_Face& f = TopoDS::Face(face(i));
    BRepAdaptor_Surface s(f);
    fp.type = surfaceTypeName(s.GetType());
    GProp_GProps g;
    BRepGProp::SurfaceProperties(f, g);
    fp.size = g.Mass();
    gp_Pnt c = g.CentreOfMass();
    fp.cx = c.X();
    fp.cy = c.Y();
    fp.cz = c.Z();
    if (s.GetType() == GeomAbs_Plane) {
        gp_Dir n = s.Plane().Axis().Direction();
        if (f.Orientation() == TopAbs_REVERSED) {
            n.Reverse();
        }
        fp.nx = n.X();
        fp.ny = n.Y();
        fp.nz = n.Z();
    }
    return fp;
}

Fingerprint NamedShape::edgeFingerprint(int i) const
{
    Fingerprint fp;
    const TopoDS_Edge& e = TopoDS::Edge(edge(i));
    BRepAdaptor_Curve c(e);
    fp.type = curveTypeName(c.GetType());
    GProp_GProps g;
    BRepGProp::LinearProperties(e, g);
    fp.size = g.Mass();
    gp_Pnt m = c.Value(0.5 * (c.FirstParameter() + c.LastParameter()));
    fp.cx = m.X();
    fp.cy = m.Y();
    fp.cz = m.Z();
    if (c.GetType() == GeomAbs_Line) {
        gp_Dir d = c.Line().Direction();
        // direction sign is meaningless for matching: canonicalise
        if (d.X() < -1e-9 || (std::abs(d.X()) < 1e-9 && (d.Y() < -1e-9 || (std::abs(d.Y()) < 1e-9 && d.Z() < 0)))) {
            d.Reverse();
        }
        fp.nx = d.X();
        fp.ny = d.Y();
        fp.nz = d.Z();
    }
    return fp;
}

ShapeRef NamedShape::faceRef(const std::string& body, int i) const
{
    return {SubShape::Face, body, faceNames_[i], faceFingerprint(i)};
}

ShapeRef NamedShape::edgeRef(const std::string& body, int i) const
{
    return {SubShape::Edge, body, edgeNames_[i], edgeFingerprint(i)};
}

Resolution resolve(const NamedShape& body, const ShapeRef& ref)
{
    Resolution r;
    const bool face = ref.kind == SubShape::Face;
    auto fpOf = [&](int i) { return face ? body.faceFingerprint(i) : body.edgeFingerprint(i); };
    auto nameOf = [&](int i) -> const std::string& { return face ? body.faceName(i) : body.edgeName(i); };
    const int n = face ? body.faceCount() : body.edgeCount();
    const double scale = bodyScale(body.shape());

    std::vector<int> exact;
    std::vector<int> family;  // same name with a split suffix (~k or #k)
    for (int i = 0; i < n; ++i) {
        const std::string& nm = nameOf(i);
        if (nm == ref.name) {
            exact.push_back(i);
        }
        else if (nm.size() > ref.name.size() && nm.compare(0, ref.name.size(), ref.name) == 0
                 && (nm[ref.name.size()] == '~' || nm[ref.name.size()] == '#')) {
            family.push_back(i);
        }
    }
    if (exact.size() == 1) {
        r.status = ResolveStatus::Exact;
        r.index = exact[0];
        return r;
    }

    auto pickBest = [&](const std::vector<int>& cands, double acceptBelow) {
        std::vector<std::pair<double, int>> scored;
        for (int i : cands) {
            scored.emplace_back(fpScore(ref.fp, fpOf(i), scale), i);
        }
        std::sort(scored.begin(), scored.end());
        if (scored.empty() || scored[0].first > acceptBelow) {
            return false;
        }
        bool clear = scored.size() == 1 || scored[1].first > 2.0 * scored[0].first + 0.05;
        if (clear) {
            r.status = ResolveStatus::Rebound;
            r.index = scored[0].second;
        }
        else {
            r.status = ResolveStatus::Ambiguous;
            for (auto& [s, i] : scored) {
                if (s <= 2.0 * scored[0].first + 0.05) {
                    r.candidates.push_back(i);
                }
            }
        }
        return true;
    };

    if (exact.size() > 1 && pickBest(exact, 1e9)) {
        return r;
    }
    if (!family.empty() && pickBest(family, 1e9)) {
        return r;
    }
    std::vector<int> all(n);
    for (int i = 0; i < n; ++i) {
        all[i] = i;
    }
    if (pickBest(all, 0.35)) {
        return r;
    }
    r.status = ResolveStatus::Missing;
    return r;
}

// ---------------------------------------------------------------- sketch frames & profiles

gp_Ax3 originPlaneFrame(OriginPlane p, double offset)
{
    switch (p) {
        case OriginPlane::XY:
            return gp_Ax3(gp_Pnt(0, 0, offset), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
        case OriginPlane::XZ:
            return gp_Ax3(gp_Pnt(0, -offset, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
        case OriginPlane::YZ:
            return gp_Ax3(gp_Pnt(offset, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
    }
    return {};
}

Result<gp_Ax3> faceFrame(const TopoDS_Shape& shape)
{
    if (shape.IsNull() || shape.ShapeType() != TopAbs_FACE) {
        return makeError("sketch.face", "A sketch can only be placed on a flat face.");
    }
    BRepAdaptor_Surface s(TopoDS::Face(shape));
    if (s.GetType() != GeomAbs_Plane) {
        return makeError("sketch.face",
                         "That face is curved. A sketch needs a flat face or a plane.",
                         {"Pick a flat face, or create a tangent construction plane first."});
    }
    gp_Pln pl = s.Plane();
    gp_Dir n = pl.Axis().Direction();
    if (shape.Orientation() == TopAbs_REVERSED) {
        n.Reverse();
    }
    gp_Pnt o = pl.Location();
    gp_Vec ov(gp_Pnt(0, 0, 0), o);
    double dist = ov.Dot(gp_Vec(n));
    gp_Pnt origin(n.X() * dist, n.Y() * dist, n.Z() * dist);
    gp_Vec x(1, 0, 0);
    if (std::abs(gp_Vec(n).Dot(x)) > 0.99) {
        x = gp_Vec(0, 1, 0);
    }
    gp_Vec xp = x - gp_Vec(n) * x.Dot(gp_Vec(n));
    return gp_Ax3(origin, n, gp_Dir(xp));
}

namespace
{
// The 3D edge of one sketch curve on its plane (null for a point, or a curve too small to
// build). `ext` grows to the largest local coordinate used, for sizing helpers.
TopoDS_Edge sketchEdge(const Sketch& sketch, int id, const SketchEntity& e, const gp_Ax3& frame, double& ext)
{
    auto to3d = [&](double x, double y) {
        gp_XYZ p = frame.Location().XYZ() + frame.XDirection().XYZ() * x + frame.YDirection().XYZ() * y;
        return gp_Pnt(p);
    };
    auto grow = [&](double x, double y, double r) {
        ext = std::max({ext, std::abs(x) + r, std::abs(y) + r});
    };
    TopoDS_Edge edge;
    if (e.type == EntityType::Line) {
        auto [x1, y1] = sketch.pointXY(e.p1);
        auto [x2, y2] = sketch.pointXY(e.p2);
        if (std::hypot(x2 - x1, y2 - y1) < 1e-7) {
            return {};
        }
        grow(x1, y1, 0);
        grow(x2, y2, 0);
        edge = BRepBuilderAPI_MakeEdge(to3d(x1, y1), to3d(x2, y2)).Edge();
    }
    else if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
        auto [cx, cy] = sketch.pointXY(e.center);
        if (e.r < 1e-7) {
            return {};
        }
        grow(cx, cy, e.r);
        gp_Circ circ(gp_Ax2(to3d(cx, cy), frame.Direction(), frame.XDirection()), e.r);
        if (e.type == EntityType::Circle) {
            edge = BRepBuilderAPI_MakeEdge(circ).Edge();
        }
        else {
            double a0 = e.a0, a1 = e.a1;
            while (a1 <= a0) {
                a1 += 2 * std::numbers::pi;
            }
            edge = BRepBuilderAPI_MakeEdge(circ, a0, a1).Edge();
        }
    }
    else if (e.type == EntityType::Ellipse) {
        auto [cx, cy] = sketch.pointXY(e.center);
        auto [ax, ay] = sketch.pointXY(e.p1);
        double major = std::hypot(ax - cx, ay - cy), minor = e.r;
        if (major < 1e-7 || minor < 1e-7) {
            return {};
        }
        gp_Dir2d ux(ax - cx, ay - cy);
        if (minor > major) {  // OCCT wants the longer axis first
            std::swap(major, minor);
            ux = gp_Dir2d(-ux.Y(), ux.X());
        }
        grow(cx, cy, major);
        const gp_Dir xd(frame.XDirection().XYZ() * ux.X() + frame.YDirection().XYZ() * ux.Y());
        edge = BRepBuilderAPI_MakeEdge(gp_Elips(gp_Ax2(to3d(cx, cy), frame.Direction(), xd), major, minor)).Edge();
    }
    else if (e.type == EntityType::Spline) {
        // the Bézier pieces joined into one cubic B-spline (each inner knot repeated 3 times)
        const auto pieces = sketch.splinePieces(id);
        if (pieces.empty()) {
            return {};
        }
        const int n = int(pieces.size());
        TColgp_Array1OfPnt poles(1, 3 * n + 1);
        int k = 1;
        for (int i = 0; i < n; ++i) {
            for (int j = i == 0 ? 0 : 1; j < 4; ++j) {
                poles.SetValue(k++, to3d(pieces[size_t(i)][size_t(j)].first, pieces[size_t(i)][size_t(j)].second));
                grow(pieces[size_t(i)][size_t(j)].first, pieces[size_t(i)][size_t(j)].second, 0);
            }
        }
        TColStd_Array1OfReal knots(1, n + 1);
        TColStd_Array1OfInteger mults(1, n + 1);
        for (int i = 0; i <= n; ++i) {
            knots.SetValue(i + 1, i);
            mults.SetValue(i + 1, i == 0 || i == n ? 4 : 3);
        }
        if (poles.First().Distance(poles.Last()) < 1e-7 && !e.closed) {
            return {};  // an open curve that starts and ends in one place
        }
        Handle(Geom_BSplineCurve) bs = new Geom_BSplineCurve(poles, knots, mults, 3);
        edge = BRepBuilderAPI_MakeEdge(bs).Edge();
    }
    else {
        return {};
    }
    return edge;
}
}  // namespace

Result<ProfileSet> buildProfiles(const Sketch& sketch, const gp_Ax3& frame)
{
    try {
        TopTools_ListOfShape tools;
        TopTools_DataMapOfShapeInteger origEntity;
        double ext = 1;
        for (auto& [id, e] : sketch.entities()) {
            if (e.construction) {
                continue;
            }
            const TopoDS_Edge edge = sketchEdge(sketch, id, e, frame, ext);
            if (edge.IsNull()) {
                continue;
            }
            tools.Append(edge);
            origEntity.Bind(edge, id);
        }
        ProfileSet set;
        auto addText = [&]() -> Result<bool> {
            for (auto& [tid, st] : sketch.texts()) {
                auto [ax, ay] = sketch.pointXY(st.anchor);
                SketchText t = st;
                if (const SketchEntity* c = t.path ? sketch.entity(t.path) : nullptr; c && c->type != EntityType::Point) {
                    t.pathPts = sketch.curvePoints(t.path, 64);  // follow it
                }
                auto out = textFaces(t, ax, ay, frame);
                if (!out) {
                    return out.error();
                }
                if (!out->note.empty()) {
                    set.notes.push_back(out->note);
                }
                int k = 0;
                for (auto& f : out->faces) {
                    Profile p;
                    p.key = "T" + std::to_string(tid) + "." + std::to_string(++k);
                    p.face = f;
                    for (TopExp_Explorer ee(f, TopAbs_EDGE); ee.More(); ee.Next()) {
                        p.edgeEntity.Bind(ee.Current(), tid);
                    }
                    GProp_GProps g;
                    BRepGProp::SurfaceProperties(f, g);
                    p.area = g.Mass();
                    set.regions.push_back(std::move(p));
                }
            }
            for (auto& [aid, art] : sketch.artworks()) {
                auto [ax, ay] = sketch.pointXY(art.anchor);
                auto faces = artworkFaces(art, ax, ay, frame);
                if (!faces) {
                    return faces.error();
                }
                int k = 0;
                for (auto& f : faces.value()) {
                    Profile p;
                    p.key = "G" + std::to_string(aid) + "." + std::to_string(++k);
                    p.face = f;
                    for (TopExp_Explorer ee(f, TopAbs_EDGE); ee.More(); ee.Next()) {
                        p.edgeEntity.Bind(ee.Current(), aid);
                    }
                    GProp_GProps g;
                    BRepGProp::SurfaceProperties(f, g);
                    p.area = g.Mass();
                    set.regions.push_back(std::move(p));
                }
            }
            return true;
        };
        if (tools.IsEmpty()) {
            if (auto r = addText(); !r) {
                return r.error();
            }
            return set;
        }
        double m = ext * 2 + 10;
        gp_Pln pln(frame);
        TopoDS_Face big = BRepBuilderAPI_MakeFace(pln, -m, m, -m, m).Face();
        TopTools_ListOfShape args;
        args.Append(big);
        BRepAlgoAPI_Splitter sp;
        sp.SetArguments(args);
        sp.SetTools(tools);
        sp.SetFuzzyValue(1e-7);
        sp.Build();
        if (!sp.IsDone() || sp.HasErrors()) {
            return makeError("sketch.profiles", "The sketch curves could not be split into regions.");
        }
        TopTools_IndexedMapOfShape boundary;
        for (TopExp_Explorer ex(big, TopAbs_EDGE); ex.More(); ex.Next()) {
            const TopTools_ListOfShape& mods = sp.Modified(ex.Current());
            if (mods.IsEmpty()) {
                boundary.Add(ex.Current());
            }
            for (TopTools_ListIteratorOfListOfShape it(mods); it.More(); it.Next()) {
                boundary.Add(it.Value());
            }
        }
        TopTools_DataMapOfShapeInteger pieceEntity;
        for (TopTools_ListIteratorOfListOfShape it(tools); it.More(); it.Next()) {
            int id = origEntity.Find(it.Value());
            const TopTools_ListOfShape& mods = sp.Modified(it.Value());
            if (mods.IsEmpty()) {
                pieceEntity.Bind(it.Value(), id);
            }
            for (TopTools_ListIteratorOfListOfShape m2(mods); m2.More(); m2.Next()) {
                if (!pieceEntity.IsBound(m2.Value())) {
                    pieceEntity.Bind(m2.Value(), id);
                }
            }
        }
        for (TopExp_Explorer ex(sp.Shape(), TopAbs_FACE); ex.More(); ex.Next()) {
            const TopoDS_Face& f = TopoDS::Face(ex.Current());
            bool outer = false;
            Profile p;
            std::set<int> ids;
            for (TopExp_Explorer ee(f, TopAbs_EDGE); ee.More(); ee.Next()) {
                if (boundary.Contains(ee.Current())) {
                    outer = true;
                    break;
                }
                if (pieceEntity.IsBound(ee.Current())) {
                    int id = pieceEntity.Find(ee.Current());
                    ids.insert(id);
                    p.edgeEntity.Bind(ee.Current(), id);
                }
            }
            if (outer) {
                continue;
            }
            for (int id : ids) {
                p.key += (p.key.empty() ? "" : ",") + std::to_string(id);
            }
            p.face = f;
            GProp_GProps g;
            BRepGProp::SurfaceProperties(f, g);
            p.area = g.Mass();
            set.regions.push_back(std::move(p));
        }
        std::sort(set.regions.begin(), set.regions.end(),
                  [](const Profile& a, const Profile& b) { return a.key < b.key; });
        if (auto r = addText(); !r) {
            return r.error();
        }
        return set;
    }
    catch (const Standard_Failure& f) {
        return makeError("sketch.profiles", "The sketch shape could not be processed.", {},
                         kernelText(f));
    }
}

// ---------------------------------------------------------------- extrude

Result<SketchPath> sketchPath(const Sketch& sketch, const gp_Ax3& frame, const std::vector<int>& curves)
{
    try {
        Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape;
        double ext = 1;
        for (int id : curves) {
            const SketchEntity* e = sketch.entity(id);
            if (!e) {
                continue;
            }
            const TopoDS_Edge edge = sketchEdge(sketch, id, *e, frame, ext);
            if (!edge.IsNull()) {
                edges->Append(edge);
            }
        }
        if (edges->IsEmpty()) {
            return makeError("pattern.path", "The path has no curves.", {"Draw the path as lines, arcs or a smooth curve."});
        }
        Handle(TopTools_HSequenceOfShape) wires;
        ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, 1e-6, Standard_False, wires);
        if (wires.IsNull() || wires->Length() != 1) {
            return makeError("pattern.path", "The path's curves are not joined end to end.",
                             {"Join the ends (they should share a point), or use one chain of curves."});
        }
        SketchPath out;
        out.wire = TopoDS::Wire(wires->Value(1));
        BRepAdaptor_CompCurve c(out.wire);
        out.length = GCPnts_AbscissaPoint::Length(c);
        out.closed = c.Value(c.FirstParameter()).Distance(c.Value(c.LastParameter())) < 1e-6;
        return out;
    }
    catch (const Standard_Failure& f) {
        return makeError("pattern.path", "The path could not be followed.", {}, kernelText(f));
    }
}

void pathAt(const SketchPath& path, double s, gp_Pnt& point, gp_Vec& tangent)
{
    BRepAdaptor_CompCurve c(path.wire);
    const double u = s <= 0 ? c.FirstParameter()
                   : s >= path.length ? c.LastParameter()
                                      : GCPnts_AbscissaPoint(c, s, c.FirstParameter()).Parameter();
    c.D1(u, point, tangent);
    if (tangent.Magnitude() > 1e-12) {
        tangent.Normalize();
    }
}

Result<std::vector<NamedShape>> splitByFace(const NamedShape& body, const TopoDS_Face& tool, const std::string& featureId)
{
    try {
        BRepAlgoAPI_Splitter split;
        TopTools_ListOfShape args, tools;
        args.Append(body.shape());
        tools.Append(tool);
        split.SetArguments(args);
        split.SetTools(tools);
        split.Build();
        if (!split.IsDone()) {
            return makeError("split.failed", "The body could not be cut with that face.");
        }
        std::vector<std::pair<double, NamedShape>> pieces;
        for (TopExp_Explorer ex(split.Shape(), TopAbs_SOLID); ex.More(); ex.Next()) {
            NamedShape piece(ex.Current());
            propagateFaceNames(split, {&body}, piece);
            std::vector<TopoDS_Shape> cut;
            for (int i = 0; i < piece.faceCount(); ++i) {
                if (piece.faceName(i).empty()) {
                    cut.push_back(piece.face(i));
                }
            }
            assignNames(piece, cut, featureId + "/split");
            piece.deriveEdgeNames();
            GProp_GProps g;
            BRepGProp::VolumeProperties(ex.Current(), g);
            pieces.emplace_back(g.Mass(), std::move(piece));
        }
        if (pieces.size() < 2) {
            return makeError("split.misses", "That face does not cut all the way through this body, so there is nothing to split.",
                             {"Pick a face that reaches right across the body, or a plane."});
        }
        std::stable_sort(pieces.begin(), pieces.end(), [](auto& a, auto& b) { return a.first > b.first; });
        std::vector<NamedShape> out;
        for (auto& [v, ns] : pieces) {
            out.push_back(std::move(ns));
        }
        return out;
    }
    catch (const Standard_Failure& f) {
        return makeError("split.failed", "The body could not be cut with that face.", {}, kernelText(f));
    }
}

Result<NamedShape> trimToFace(const NamedShape& body, const TopoDS_Face& target, const TopoDS_Shape& start,
                              const std::string& featureId, const std::string& endName)
{
    try {
        TopoDS_Shape tool = target;
        BRepAdaptor_Surface surf(target);
        if (surf.GetType() == GeomAbs_Plane) {
            // a flat face reaches as far as its plane: a face big enough to cross the body
            Bnd_Box bb;
            BRepBndLib::Add(body.shape(), bb);
            const double big = std::sqrt(bb.SquareExtent()) * 4 + 100;
            tool = BRepBuilderAPI_MakeFace(surf.Plane(), -big, big, -big, big).Face();
        }
        BRepAlgoAPI_Splitter split;
        TopTools_ListOfShape args, tools;
        args.Append(body.shape());
        tools.Append(tool);
        split.SetArguments(args);
        split.SetTools(tools);
        split.Build();
        if (!split.IsDone()) {
            return makeError("extrude.to_face", "The extrude could not be cut off at that face.");
        }
        GProp_GProps whole;
        BRepGProp::VolumeProperties(body.shape(), whole);
        TopoDS_Shape keep;
        int pieces = 0;
        int touching = 0;
        for (TopExp_Explorer ex(split.Shape(), TopAbs_SOLID); ex.More(); ex.Next()) {
            ++pieces;
            BRepExtrema_DistShapeShape d(ex.Current(), start);
            if (d.IsDone() && d.Value() < 1e-6) {
                keep = ex.Current();
                ++touching;
            }
        }
        if (pieces < 2 || touching != 1) {
            return makeError("extrude.to_face", "That face does not cross the whole extrude, so there is no clean place to stop.",
                             {"Pick a face that covers the sketch's outline, or a flat face or plane."});
        }
        NamedShape out(keep);
        propagateFaceNames(split, {&body}, out);
        // still holding the far end of the sweep: the face only covered part of the outline
        for (int i = 0; i < out.faceCount(); ++i) {
            const std::string& n = out.faceName(i);
            if (n == endName || n.rfind(endName + "~", 0) == 0) {
                return makeError("extrude.to_face", "That face does not cross the whole extrude, so there is no clean place to stop.",
                                 {"Pick a face that covers the sketch's outline, or a flat face or plane."});
            }
        }
        // the faces the cut made (named after the target) are the new end
        std::vector<TopoDS_Shape> ends;
        for (int i = 0; i < out.faceCount(); ++i) {
            if (out.faceName(i).empty()) {
                ends.push_back(out.face(i));
            }
        }
        assignNames(out, ends, endName);
        nameLeftovers(out, featureId);
        return out;
    }
    catch (const Standard_Failure& f) {
        return makeError("extrude.to_face", "The extrude could not be cut off at that face.", {}, kernelText(f));
    }
}

Result<NamedShape> makeExtrusion(const ExtrudeInput& in)
{
    if (in.profiles.empty()) {
        return makeError("extrude.no_profile", "Pick at least one closed area (profile) to extrude.");
    }
    if (!(in.distance > 1e-6)) {
        return makeError("extrude.distance", "The extrude distance must be larger than zero.",
                         {"Type a distance such as 10 mm, or drag the arrow."});
    }
    if (in.direction == ExtrudeDirection::TwoSides && !(in.distance2 >= 0)) {
        return makeError("extrude.distance", "The second distance must not be negative.");
    }
    if (std::abs(in.taper) >= std::numbers::pi / 2 - 1e-3) {
        return makeError("extrude.taper", "The taper angle must be between -89 and 89 degrees.");
    }
    try {
        gp_Vec n(in.normal);
        // `ahead` goes along the extrude direction from the sketch plane, `behind` the other way
        double ahead = in.distance, behind = 0;
        if (in.direction == ExtrudeDirection::Reverse) {
            n.Reverse();
        }
        else if (in.direction == ExtrudeDirection::Symmetric) {
            ahead = behind = in.distance / 2;
        }
        else if (in.direction == ExtrudeDirection::TwoSides) {
            behind = in.distance2;
        }
        const gp_Vec v = n * (ahead + behind);
        const gp_Vec shift = n * (-behind);
        const bool multi = in.profiles.size() > 1;
        std::unique_ptr<NamedShape> acc;
        for (const Profile* p : in.profiles) {
            TopoDS_Face face = p->face;
            TopTools_DataMapOfShapeInteger edgeEntity = p->edgeEntity;
            if (in.thin > 1e-9) {
                // a wall along the outline: the area between two offsets of the profile
                const double t = in.thin;
                const double outer = in.thinSide < 0 ? 0 : in.thinSide > 0 ? t : t / 2;
                const double inner = outer - t;
                auto offsetFace = [&](double d) -> std::optional<TopoDS_Shape> {
                    if (std::abs(d) < 1e-12) {
                        return TopoDS_Shape(p->face);
                    }
                    BRepOffsetAPI_MakeOffset off(p->face, GeomAbs_Intersection);
                    off.Perform(d);
                    if (!off.IsDone()) {
                        return std::nullopt;
                    }
                    TopoDS_Shape f;
                    for (TopExp_Explorer wx(off.Shape(), TopAbs_WIRE); wx.More(); wx.Next()) {
                        f = BRepBuilderAPI_MakeFace(TopoDS::Wire(wx.Current()), true).Face();
                        break;
                    }
                    if (f.IsNull()) {
                        return std::nullopt;
                    }
                    return f;
                };
                auto o = offsetFace(outer), i = offsetFace(inner);
                if (!o || !i) {
                    return makeError("extrude.thin", fmt("A wall of %1 does not fit this outline.", {fmtMm(t)}),
                                     {"Use a thinner wall, or put it outside the outline."});
                }
                BRepAlgoAPI_Cut ring(*o, *i);
                ring.Build();
                TopoDS_Shape r = ring.Shape();
                TopExp_Explorer fx(r, TopAbs_FACE);
                if (!ring.IsDone() || !fx.More()) {
                    return makeError("extrude.thin", fmt("A wall of %1 does not fit this outline.", {fmtMm(t)}),
                                     {"Use a thinner wall, or put it outside the outline."});
                }
                face = TopoDS::Face(fx.Current());
                edgeEntity.Clear();  // the wall's sides are named outer/inner below
            }
            if (std::abs(in.taper) > 1e-12) {
                // a drafted prism: the outline leans in (positive) or out as it goes
                TopoDS_Face spine = face;
                gp_Dir fn = BRepAdaptor_Surface(spine).Plane().Axis().Direction();
                if (spine.Orientation() == TopAbs_REVERSED) {
                    fn.Reverse();
                }
                if (fn.Dot(gp_Dir(n)) < 0) {
                    spine.Reverse();
                }
                // One drafted prism per side, each narrowing away from the sketch plane.
                // OCCT measures the height along the slanted side, hence the cosine.
                const double c = std::cos(in.taper);
                const std::string sfx = multi ? "[" + p->key + "]" : "";
                std::unique_ptr<NamedShape> both;
                for (int side = 0; side < 2; ++side) {
                    const double h = side == 0 ? ahead : behind;
                    if (h < 1e-9) {
                        continue;
                    }
                    // the behind half: the profile mirrored in its own plane, so its surface
                    // normal (which the drafted prism follows) points the other way
                    TopoDS_Face sp = spine;
                    std::map<int, int> mirroredEnt;
                    TopTools_DataMapOfShapeInteger sideEnt;
                    for (TopTools_DataMapIteratorOfDataMapOfShapeInteger it(edgeEntity); it.More(); it.Next()) {
                        sideEnt.Bind(it.Key(), it.Value());
                    }
                    if (side == 1) {
                        gp_Trsf m;
                        m.SetMirror(gp_Ax2(BRepAdaptor_Surface(spine).Plane().Location(), gp_Dir(n)));
                        BRepBuilderAPI_Transform tr(spine, m, true);
                        sp = TopoDS::Face(tr.Shape());
                        sideEnt.Clear();
                        for (TopTools_DataMapIteratorOfDataMapOfShapeInteger it(edgeEntity); it.More(); it.Next()) {
                            for (TopTools_ListIteratorOfListOfShape mm(tr.Modified(it.Key())); mm.More(); mm.Next()) {
                                sideEnt.Bind(mm.Value(), it.Value());
                            }
                        }
                    }
                    LocOpe_DPrism dp(sp, h / c, in.taper);
                    if (!dp.IsDone() || !BRepCheck_Analyzer(dp.Shape()).IsValid()) {
                        return makeError("extrude.taper", "This taper is too steep for this shape and distance.",
                                         {"Use a smaller angle or a shorter distance."});
                    }
                    NamedShape part(dp.Shape());
                    for (TopExp_Explorer ee(sp, TopAbs_EDGE); ee.More(); ee.Next()) {
                        int ent = 0;
                        for (TopTools_DataMapIteratorOfDataMapOfShapeInteger it(sideEnt); it.More(); it.Next()) {
                            if (it.Key().IsSame(ee.Current())) {
                                ent = it.Value();
                            }
                        }
                        assignNames(part, toVector(dp.Shapes(ee.Current()), TopAbs_FACE),
                                    in.featureId + "/side:" + std::to_string(ent) + (side == 1 ? ":back" : ""));
                    }
                    // caps by position along the direction: the far one is "end" ahead and
                    // "start" behind; the one on the sketch plane is "start" for a one-sided extrude
                    const gp_Pnt o = BRepAdaptor_Surface(spine).Plane().Location();
                    for (int fi = 0; fi < part.faceCount(); ++fi) {
                        if (!part.faceName(fi).empty() || part.faceFingerprint(fi).type != "plane") {
                            continue;
                        }
                        GProp_GProps g;
                        BRepGProp::SurfaceProperties(part.face(fi), g);
                        const double along = gp_Vec(o, g.CentreOfMass()).Dot(n);
                        if (std::abs(std::abs(along) - h) < 1e-6) {
                            assignNames(part, {part.face(fi)}, in.featureId + (side == 0 ? "/end" : "/start") + sfx);
                        }
                        else if (std::abs(along) < 1e-6 && side == 0 && behind < 1e-9) {
                            assignNames(part, {part.face(fi)}, in.featureId + "/start" + sfx);
                        }
                    }
                    nameLeftovers(part, in.featureId);
                    if (!both) {
                        both = std::make_unique<NamedShape>(std::move(part));
                    }
                    else {
                        auto fused = booleanOp(in.featureId, BoolOp::Join, *both, part);
                        if (!fused) {
                            return fused.error();
                        }
                        both = std::make_unique<NamedShape>(std::move(fused.value()));
                    }
                }
                NamedShape ns = std::move(*both);
                if (!acc) {
                    acc = std::make_unique<NamedShape>(std::move(ns));
                }
                else {
                    auto fused = booleanOp(in.featureId, BoolOp::Join, *acc, ns);
                    acc = std::make_unique<NamedShape>(fused ? std::move(fused.value()) : namedCompound({*acc, ns}));
                }
                continue;
            }
            if (shift.Magnitude() > 0) {
                gp_Trsf t;
                t.SetTranslation(shift);
                BRepBuilderAPI_Transform tr(p->face, t, true);
                face = TopoDS::Face(tr.Shape());
                edgeEntity.Clear();
                for (TopTools_DataMapIteratorOfDataMapOfShapeInteger it(p->edgeEntity); it.More(); it.Next()) {
                    const TopTools_ListOfShape& mods = tr.Modified(it.Key());
                    for (TopTools_ListIteratorOfListOfShape m(mods); m.More(); m.Next()) {
                        edgeEntity.Bind(m.Value(), it.Value());
                    }
                }
            }
            BRepPrimAPI_MakePrism prism(face, v, false);
            prism.Build();
            if (!prism.IsDone()) {
                return makeError("extrude.failed", "This profile could not be extruded.");
            }
            NamedShape ns(prism.Shape());
            std::map<int, std::vector<TopoDS_Shape>> sides;
            for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
                int ent = edgeEntity.IsBound(ee.Current()) ? edgeEntity.Find(ee.Current()) : 0;
                for (auto& g : toVector(prism.Generated(ee.Current()), TopAbs_FACE)) {
                    sides[ent].push_back(g);
                }
            }
            for (auto& [ent, faces] : sides) {
                assignNames(ns, faces, in.featureId + "/side:" + std::to_string(ent));
            }
            std::string sfx = multi ? "[" + p->key + "]" : "";
            assignNames(ns, {prism.FirstShape()}, in.featureId + "/start" + sfx);
            assignNames(ns, {prism.LastShape()}, in.featureId + "/end" + sfx);
            nameLeftovers(ns, in.featureId);
            if (!acc) {
                acc = std::make_unique<NamedShape>(std::move(ns));
            }
            else {
                auto fused = booleanOp(in.featureId, BoolOp::Join, *acc, ns);
                if (!fused) {
                    // separate areas (letters, two islands, pieces meeting only at a corner)
                    // stay separate pieces of one tool
                    acc = std::make_unique<NamedShape>(namedCompound({*acc, ns}));
                    continue;
                }
                if (!fused) {
                    return fused.error();
                }
                acc = std::make_unique<NamedShape>(std::move(fused.value()));
            }
        }
        return std::move(*acc);
    }
    catch (const Standard_Failure& f) {
        return makeError("extrude.failed", "This profile could not be extruded.", {}, kernelText(f));
    }
}

// ---------------------------------------------------------------- revolve

// The face a revolve swept from profile edge `e`, found by geometry: the profile edge
// lies on the face it sweeps. Used where Generated() reports nothing, which happens
// for edges that touch the axis (the flat top of a knob, the floor of a hole).
std::optional<int> sweptFace(const NamedShape& ns, const TopoDS_Edge& e, const gp_Ax1& axis)
{
    if (BRep_Tool::Degenerated(e)) {
        return std::nullopt;
    }
    BRepAdaptor_Curve c(e);
    const gp_Pnt mid = c.Value((c.FirstParameter() + c.LastParameter()) / 2);
    if (gp_Lin(axis).Distance(mid) < 1e-7) {
        return std::nullopt;  // lies on the axis: sweeps no face
    }
    const TopoDS_Vertex mv = BRepBuilderAPI_MakeVertex(mid);
    for (int i = 0; i < ns.faceCount(); ++i) {
        if (!ns.faceName(i).empty()) {
            continue;
        }
        BRepExtrema_DistShapeShape d(mv, ns.face(i));
        if (d.IsDone() && d.Value() < 1e-6) {
            return i;
        }
    }
    return std::nullopt;
}

Result<NamedShape> makeRevolution(const std::string& featureId,
                                  const std::vector<const Profile*>& profiles,
                                  const gp_Ax1& axis,
                                  double angle)
{
    if (profiles.empty()) {
        return makeError("revolve.no_profile", "Pick at least one closed area (profile) to revolve.");
    }
    if (!(std::abs(angle) > 1e-9)) {
        return makeError("revolve.angle", "The revolve angle must not be zero.");
    }
    const bool full = std::abs(std::abs(angle) - 2 * std::numbers::pi) < 1e-9;
    const auto crosses = makeError("revolve.crosses_axis", "The outline crosses the revolve axis, so it would sweep through itself.",
                                   {"Keep the whole outline on one side of the axis line.",
                                    "Draw the axis exactly along the edge of the profile, not through it."});
    try {
        // An outline with area on both sides of the axis cannot be revolved.
        for (const Profile* p : profiles) {
            BRepAdaptor_Surface surf(p->face);
            if (surf.GetType() != GeomAbs_Plane) {
                continue;
            }
            const gp_Dir n = surf.Plane().Axis().Direction();
            if (std::abs(n.Dot(axis.Direction())) > 1e-9) {
                continue;  // the axis is not in the sketch plane
            }
            const gp_Vec side = gp_Vec(n).Crossed(gp_Vec(axis.Direction()));
            bool pos = false, neg = false;
            for (TopExp_Explorer ee(p->face, TopAbs_EDGE); ee.More(); ee.Next()) {
                BRepAdaptor_Curve c(TopoDS::Edge(ee.Current()));
                for (int k = 0; k <= 8; ++k) {
                    const gp_Pnt q = c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * k / 8);
                    const double d = gp_Vec(axis.Location(), q).Dot(side);
                    pos |= d > 1e-7;
                    neg |= d < -1e-7;
                }
            }
            if (pos && neg) {
                return crosses;
            }
        }
        std::unique_ptr<NamedShape> acc;
        const bool multi = profiles.size() > 1;
        for (const Profile* p : profiles) {
            BRepPrimAPI_MakeRevol rev(p->face, axis, full ? 2 * std::numbers::pi : angle, false);
            rev.Build();
            if (!rev.IsDone()) {
                return makeError("revolve.failed", "This profile could not be revolved.");
            }
            if (!BRepCheck_Analyzer(rev.Shape()).IsValid()) {
                return crosses;
            }
            NamedShape ns(rev.Shape());
            std::map<int, std::vector<TopoDS_Shape>> sides;
            for (TopExp_Explorer ee(p->face, TopAbs_EDGE); ee.More(); ee.Next()) {
                const int ent = p->edgeEntity.IsBound(ee.Current()) ? p->edgeEntity.Find(ee.Current()) : 0;
                for (auto& g : toVector(rev.Generated(ee.Current()), TopAbs_FACE)) {
                    sides[ent].push_back(g);
                }
            }
            for (auto& [ent, faces] : sides) {
                assignNames(ns, faces, featureId + "/side:" + std::to_string(ent));
            }
            if (!full) {
                const std::string sfx = multi ? "[" + p->key + "]" : "";
                assignNames(ns, {rev.FirstShape()}, featureId + "/start" + sfx);
                assignNames(ns, {rev.LastShape()}, featureId + "/end" + sfx);
            }
            for (TopExp_Explorer ee(p->face, TopAbs_EDGE); ee.More(); ee.Next()) {
                const int ent = p->edgeEntity.IsBound(ee.Current()) ? p->edgeEntity.Find(ee.Current()) : 0;
                if (!sides.count(ent)) {
                    if (auto i = sweptFace(ns, TopoDS::Edge(ee.Current()), axis)) {
                        assignNames(ns, {ns.face(*i)}, featureId + "/side:" + std::to_string(ent));
                    }
                }
            }
            nameLeftovers(ns, featureId);
            if (!acc) {
                acc = std::make_unique<NamedShape>(std::move(ns));
            }
            else {
                auto fused = booleanOp(featureId, BoolOp::Join, *acc, ns);
                if (!fused) {
                    // separate areas (letters, two islands, pieces meeting only at a corner)
                    // stay separate pieces of one tool
                    acc = std::make_unique<NamedShape>(namedCompound({*acc, ns}));
                    continue;
                }
                if (!fused) {
                    return fused.error();
                }
                acc = std::make_unique<NamedShape>(std::move(fused.value()));
            }
        }
        return std::move(*acc);
    }
    catch (const Standard_Failure& f) {
        return makeError("revolve.failed", "This profile could not be revolved.", {}, kernelText(f));
    }
}

// ---------------------------------------------------------------- holes

Result<NamedShape> makeHoleTool(const std::string& featureId,
                                const gp_Ax3& frame,
                                const std::vector<HolePoint>& points,
                                const HoleSpec& spec,
                                double throughDepth)
{
    if (points.empty()) {
        return makeError("hole.no_points", "Place at least one point in the sketch to mark where the hole goes.",
                         {"Add a Point in the sketch at each hole centre."});
    }
    const double r = spec.diameter / 2;
    if (!(r > 1e-6)) {
        return makeError("hole.diameter", "The hole diameter must be larger than zero.");
    }
    const double depth = spec.through ? throughDepth : spec.depth;
    if (!(depth > 1e-6)) {
        return makeError("hole.depth", "The hole depth must be larger than zero.");
    }
    using T = HoleSpec::Type;
    // one-click fixes: sizes in proportion to the hole (a counterbore about 1.8 times as wide)
    auto rounded = [](double v) { return std::round(v * 10) / 10; };
    if (spec.type == T::Counterbore && !(spec.cboreDiameter > spec.diameter && spec.cboreDepth > 0 && spec.cboreDepth < depth)) {
        Error err = makeError("hole.counterbore", "The counterbore must be wider than the hole and shallower than it.");
        if (!(spec.cboreDiameter > spec.diameter)) {
            const double w = rounded(spec.diameter * 1.8);
            err.fixes.push_back({fmt("Make the counterbore %1 wide", {fmtMm(w)}), "cbore_diameter", fmtMm(w)});
        }
        if (!(spec.cboreDepth > 0 && spec.cboreDepth < depth)) {
            const double h = rounded(std::min(depth / 2, spec.diameter));
            if (h > 0) {
                err.fixes.push_back({fmt("Make the counterbore %1 deep", {fmtMm(h)}), "cbore_depth", fmtMm(h)});
            }
        }
        return err;
    }
    if (spec.tipAngle != 0 && !(spec.tipAngle > 0.5 && spec.tipAngle < 3.0)) {
        Error err = makeError("hole.tip", "A drill point angle must be between 30 and 170 degrees (118° is usual).");
        err.fixes.push_back({"Use 118 deg", "tip", "118 deg"});
        err.fixes.push_back({"Use a flat bottom", "tip", "flat"});
        return err;
    }
    if (spec.type == T::Countersink && !(spec.csinkDiameter > spec.diameter && spec.csinkAngle > 0.1 && spec.csinkAngle < 3.0)) {
        Error err = makeError("hole.countersink", "The countersink must be wider than the hole, with an angle between 6 and 170 degrees.");
        if (!(spec.csinkDiameter > spec.diameter)) {
            const double w = rounded(spec.diameter * 2);
            err.fixes.push_back({fmt("Make the countersink %1 wide", {fmtMm(w)}), "csink_diameter", fmtMm(w)});
        }
        if (!(spec.csinkAngle > 0.1 && spec.csinkAngle < 3.0)) {
            err.fixes.push_back({"Use 90 deg", "csink_angle", "90 deg"});
        }
        return err;
    }
    try {
        // Half-section in (radius, depth) coordinates; depth runs into the material
        // (-normal). It starts a little above the face so the cut always opens cleanly.
        const double lift = std::max(0.05, 0.01 * depth);
        struct Seg
        {
            double r0, z0, r1, z1;
            const char* role;
        };
        std::vector<Seg> segs;
        double rTop = r, zTop = -lift;
        if (spec.type == T::Counterbore) {
            const double R = spec.cboreDiameter / 2, d = spec.cboreDepth;
            segs = {{0, -lift, R, -lift, "top"}, {R, -lift, R, d, "cbore"}, {R, d, r, d, "cbore_floor"},
                    {r, d, r, depth, "wall"}, {r, depth, 0, depth, "floor"}};
        }
        else if (spec.type == T::Countersink) {
            const double R = spec.csinkDiameter / 2;
            const double h = (R - r) / std::tan(spec.csinkAngle / 2);
            const double Rl = R + lift * std::tan(spec.csinkAngle / 2);
            segs = {{0, -lift, Rl, -lift, "top"}, {Rl, -lift, r, h, "csink"}, {r, h, r, depth, "wall"},
                    {r, depth, 0, depth, "floor"}};
        }
        else {
            segs = {{0, zTop, rTop, zTop, "top"}, {r, -lift, r, depth, "wall"}, {r, depth, 0, depth, "floor"}};
        }
        if (spec.tipAngle > 0 && !spec.through) {
            // the flat floor becomes a cone; the depth is still measured to the shoulder
            segs.back() = {r, depth, 0, depth + r / std::tan(spec.tipAngle / 2), "tip"};
        }
        std::vector<NamedShape> tools;
        const gp_Dir down = frame.Direction().Reversed();
        const gp_Dir radial = frame.XDirection();
        for (const HolePoint& hp : points) {
            const gp_Pnt c(frame.Location().XYZ() + frame.XDirection().XYZ() * hp.x + frame.YDirection().XYZ() * hp.y);
            auto at = [&](double rr, double zz) {
                return gp_Pnt(c.XYZ() + radial.XYZ() * rr + down.XYZ() * zz);
            };
            BRepBuilderAPI_MakeWire wire;
            std::vector<std::pair<TopoDS_Edge, std::string>> roles;
            for (auto& sg : segs) {
                TopoDS_Edge e = BRepBuilderAPI_MakeEdge(at(sg.r0, sg.z0), at(sg.r1, sg.z1)).Edge();
                wire.Add(e);
                roles.emplace_back(wire.Edge(), sg.role);
            }
            // close along the axis
            wire.Add(BRepBuilderAPI_MakeEdge(at(0, segs.back().z1), at(0, segs.front().z0)).Edge());
            TopoDS_Face face = BRepBuilderAPI_MakeFace(wire.Wire(), true).Face();
            BRepPrimAPI_MakeRevol rev(face, gp_Ax1(c, down), 2 * std::numbers::pi, false);
            rev.Build();
            NamedShape ns(rev.Shape());
            for (TopExp_Explorer ee(face, TopAbs_EDGE); ee.More(); ee.Next()) {
                std::string role;
                for (auto& [re, rn] : roles) {
                    if (re.IsSame(ee.Current())) {
                        role = rn;
                    }
                }
                if (role.empty()) {
                    continue;
                }
                const std::string name = featureId + "/" + role + ":p" + std::to_string(hp.id);
                auto gen = toVector(rev.Generated(ee.Current()), TopAbs_FACE);
                if (gen.empty()) {
                    if (auto i = sweptFace(ns, TopoDS::Edge(ee.Current()), gp_Ax1(c, down))) {
                        gen = {ns.face(*i)};
                    }
                }
                assignNames(ns, gen, name);
            }
            nameLeftovers(ns, featureId);
            tools.push_back(std::move(ns));
        }
        return namedCompound(tools);
    }
    catch (const Standard_Failure& f) {
        return makeError("hole.failed", "The hole could not be made.", {}, kernelText(f));
    }
}

// ---------------------------------------------------------------- copies

NamedShape transformedCopy(const NamedShape& src, const gp_Trsf& t, const std::string& prefix)
{
    BRepBuilderAPI_Transform tr(src.shape(), t, true);
    NamedShape out(tr.Shape());
    for (int i = 0; i < src.faceCount(); ++i) {
        assignNames(out, toVector(tr.Modified(src.face(i)), TopAbs_FACE), prefix + src.faceName(i));
    }
    // a copy keeps the topology order, so anything the history missed maps by index
    for (int i = 0; i < out.faceCount() && i < src.faceCount(); ++i) {
        if (out.faceName(i).empty()) {
            out.setFaceName(i, prefix + src.faceName(i));
        }
    }
    out.deriveEdgeNames();
    return out;
}

NamedShape namedCompound(const std::vector<NamedShape>& parts)
{
    TopoDS_Compound c;
    BRep_Builder b;
    b.MakeCompound(c);
    for (auto& p : parts) {
        b.Add(c, p.shape());
    }
    NamedShape out(c);
    for (auto& p : parts) {
        for (int i = 0; i < p.faceCount(); ++i) {
            const int j = out.faceIndex(p.face(i));
            if (j >= 0) {
                out.setFaceName(j, p.faceName(i));
            }
        }
    }
    out.deriveEdgeNames();
    return out;
}

// ---------------------------------------------------------------- booleans

Result<NamedShape> booleanOp(const std::string& featureId,
                             BoolOp op,
                             const NamedShape& target,
                             const NamedShape& tool)
{
    if (op == BoolOp::NewBody) {
        return tool;
    }
    try {
        std::unique_ptr<BRepAlgoAPI_BooleanOperation> b;
        switch (op) {
            case BoolOp::Join:
                b = std::make_unique<BRepAlgoAPI_Fuse>();
                break;
            case BoolOp::Cut:
                b = std::make_unique<BRepAlgoAPI_Cut>();
                break;
            default:
                b = std::make_unique<BRepAlgoAPI_Common>();
                break;
        }
        TopTools_ListOfShape a, t;
        a.Append(target.shape());
        t.Append(tool.shape());
        b->SetArguments(a);
        b->SetTools(t);
        b->SetFuzzyValue(1e-6);
        b->Build();
        if (!b->IsDone() || b->HasErrors()) {
            return makeError("boolean.failed",
                             "The new shape could not be combined with the body.",
                             {"Check that the shapes overlap or touch properly.",
                              "Try a slightly different size so faces don't exactly coincide."});
        }
        TopoDS_Shape res = b->Shape();
        int solids = 0;
        for (TopExp_Explorer ex(res, TopAbs_SOLID); ex.More(); ex.Next()) {
            ++solids;
        }
        if (solids == 0) {
            return makeError(op == BoolOp::Cut ? "boolean.cut_everything" : "boolean.empty",
                             op == BoolOp::Cut ? "This cut would remove the whole body."
                                               : "These shapes do not overlap, so nothing is left.",
                             {"Check the distance and direction."});
        }
        int before = 0;
        for (TopExp_Explorer ex(target.shape(), TopAbs_SOLID); ex.More(); ex.Next()) {
            ++before;
        }
        if (op == BoolOp::Join && solids > before) {
            Error err = makeError("boolean.not_touching",
                                  "The new shape does not touch the body, so it cannot be joined.",
                                  {"Use 'New body' if you want a separate part.",
                                   "Change the distance or sketch position so the shapes touch."});
            err.fixes.push_back({"Make it a new body", "op", "new"});
            return err;
        }
        // Merge coplanar / co-cylindrical pieces the boolean left behind (otherwise a
        // fused base and back would keep a seam across the bottom face that a fillet
        // trips over). Names go through both histories, composed, so a face that was
        // split by the boolean and re-merged here keeps its original name.
        ShapeUpgrade_UnifySameDomain usd(res, true, true, false);
        usd.Build();
        TopoDS_Shape fin = usd.Shape();
        Handle(BRepTools_History) hist = usd.History();
        if (!BRepCheck_Analyzer(fin).IsValid()) {
            return makeError("boolean.invalid",
                             "Combining the shapes gave a damaged result, so it was not applied.",
                             {"Try a slightly different size so faces don't exactly coincide."});
        }
        NamedShape out(fin);
        auto through = [&](const TopoDS_Shape& f) {
            std::vector<TopoDS_Shape> result;
            if (b->IsDeleted(f)) {
                return result;
            }
            const TopTools_ListOfShape& m1 = b->Modified(f);
            std::vector<TopoDS_Shape> stage = m1.IsEmpty() ? std::vector<TopoDS_Shape> {f}
                                                           : toVector(m1, TopAbs_FACE);
            for (auto& s : stage) {
                if (!hist.IsNull() && hist->IsRemoved(s)) {
                    continue;
                }
                if (hist.IsNull() || hist->Modified(s).IsEmpty()) {
                    result.push_back(s);
                }
                else {
                    for (auto& m : toVector(hist->Modified(s), TopAbs_FACE)) {
                        result.push_back(m);
                    }
                }
            }
            std::vector<TopoDS_Shape> uniq;
            for (auto& s : result) {
                bool dup = false;
                for (auto& u : uniq) {
                    dup = dup || u.IsSame(s);
                }
                if (!dup) {
                    uniq.push_back(s);
                }
            }
            return uniq;
        };
        for (const NamedShape* in : {&target, &tool}) {
            for (int i = 0; i < in->faceCount(); ++i) {
                assignNames(out, through(in->face(i)), in->faceName(i));
            }
        }
        nameLeftovers(out, featureId);
        return out;
    }
    catch (const Standard_Failure& f) {
        return makeError("boolean.failed", "The new shape could not be combined with the body.", {},
                         kernelText(f));
    }
}

// ---------------------------------------------------------------- fillet

namespace
{

// Fillet and chamfer share OCCT's local-operation machinery, our naming and our
// failure diagnosis; only the builder differs.
std::optional<TopoDS_Shape> tryEdgeOp(const TopoDS_Shape& shape,
                                      const std::vector<TopoDS_Edge>& edges,
                                      double r,
                                      bool chamfer,
                                      std::unique_ptr<BRepFilletAPI_LocalOperation>* keep = nullptr,
                                      std::string* why = nullptr,
                                      const EdgeOptions& opt = {},
                                      const std::vector<TopoDS_Face>& sideFaces = {})
{
    using Mode = EdgeOptions::Mode;
    try {
        std::unique_ptr<BRepFilletAPI_LocalOperation> mk;
        if (chamfer) {
            auto c = std::make_unique<BRepFilletAPI_MakeChamfer>(shape);
            for (size_t i = 0; i < edges.size(); ++i) {
                const auto& e = edges[i];
                if (opt.mode == Mode::TwoDistances && i < sideFaces.size()) {
                    c->Add(r, opt.second, e, sideFaces[i]);  // r measured on sideFaces[i]
                }
                else if (opt.mode == Mode::DistanceAngle && i < sideFaces.size()) {
                    c->AddDA(r, opt.second, e, sideFaces[i]);
                }
                else {
                    c->Add(r, e);
                }
            }
            mk = std::move(c);
        }
        else {
            auto f = std::make_unique<BRepFilletAPI_MakeFillet>(shape);
            for (auto& e : edges) {
                if (opt.mode == Mode::Variable) {
                    opt.flip ? f->Add(opt.second, r, e) : f->Add(r, opt.second, e);
                }
                else {
                    f->Add(r, e);
                }
            }
            mk = std::move(f);
        }
        mk->Build();
        if (!mk->IsDone()) {
            return std::nullopt;
        }
        TopoDS_Shape res = mk->Shape();
        if (!BRepCheck_Analyzer(res).IsValid()) {
            if (why) {
                *why = "result failed BRepCheck";
            }
            return std::nullopt;
        }
        if (keep) {
            *keep = std::move(mk);
        }
        return res;
    }
    catch (const Standard_Failure& f) {
        if (why) {
            *why = kernelText(f);
        }
        return std::nullopt;
    }
}


}  // namespace

namespace
{
Result<NamedShape> edgeTreatment(const std::string& featureId,
                                 const NamedShape& body,
                                 const std::vector<int>& edgeIndices,
                                 double radius,
                                 bool chamfer,
                                 const EdgeOptions& opt = {})
{
    const std::string what = chamfer ? "chamfer" : "fillet";
    if (edgeIndices.empty()) {
        return makeError(what + ".no_edges", chamfer ? "Pick at least one edge to bevel." : "Pick at least one edge to round.");
    }
    if (!(radius > 1e-6)) {
        return makeError(what + ".radius", chamfer ? "The chamfer distance must be larger than zero."
                                                  : "The fillet radius must be larger than zero.");
    }
    if (opt.mode != EdgeOptions::Mode::Equal && !(opt.second > 1e-9)) {
        return makeError(what + ".second", opt.mode == EdgeOptions::Mode::DistanceAngle
                                               ? "The chamfer angle must be larger than zero."
                                               : "The second size must be larger than zero.");
    }
    if (opt.mode == EdgeOptions::Mode::DistanceAngle && opt.second >= std::numbers::pi / 2) {
        return makeError(what + ".second", "The chamfer angle must be less than 90 degrees.");
    }
    std::vector<TopoDS_Edge> edges;
    std::vector<TopoDS_Face> sides;  // the face each first size is measured on
    for (int i : edgeIndices) {
        edges.push_back(TopoDS::Edge(body.edge(i)));
        // deterministic: the adjacent face whose name sorts first (or last, flipped)
        std::vector<int> fs = body.facesOfEdge(i);
        std::sort(fs.begin(), fs.end(), [&](int a, int b) { return body.faceName(a) < body.faceName(b); });
        if (!fs.empty()) {
            sides.push_back(TopoDS::Face(body.face(opt.flip ? fs.back() : fs.front())));
        }
    }
    std::unique_ptr<BRepFilletAPI_LocalOperation> mk;
    std::string why;
    auto res = tryEdgeOp(body.shape(), edges, radius, chamfer, &mk, &why, opt, sides);
    if (!res) {
        // Probable cause: the shortest edge next to the selected edges.
        double nearest = 1e300;
        std::set<int> sel(edgeIndices.begin(), edgeIndices.end());
        for (int i : edgeIndices) {
            for (int f : body.facesOfEdge(i)) {
                for (int e : body.edgesOfFace(f)) {
                    if (sel.count(e)) {
                        continue;
                    }
                    GProp_GProps g;
                    BRepGProp::LinearProperties(body.edge(e), g);
                    nearest = std::min(nearest, g.Mass());
                }
            }
        }
        // Find the largest radius that works (bisection, a handful of kernel calls).
        double ok = 0, bad = radius;
        for (double r = radius * 0.5; r > radius / 64; r *= 0.5) {
            if (tryEdgeOp(body.shape(), edges, r, chamfer, nullptr, nullptr, opt, sides)) {
                ok = r;
                break;
            }
            bad = r;
        }
        if (ok > 0) {
            for (int k = 0; k < 4; ++k) {
                double mid = 0.5 * (ok + bad);
                (tryEdgeOp(body.shape(), edges, mid, chamfer, nullptr, nullptr, opt, sides) ? ok : bad) = mid;
            }
        }
        std::vector<std::string> sug;
        double fixSize = 0;
        if (ok > 0) {
            double s = std::floor(ok * 0.95 * 10) / 10;
            if (s > 0) {
                sug.push_back(fmt("Try %1 or less.", {fmtMm(s)}));
                fixSize = s;
            }
        }
        sug.push_back(chamfer ? "Bevel fewer edges at once, or chamfer the longer edges first."
                              : "Round fewer edges at once, or fillet the larger edges first.");
        std::string msg;
        if (nearest < (chamfer ? 1.0 : 2.0) * radius) {
            msg = fmt(chamfer ? "A %1 chamfer is too big here: the neighbouring face is only %2 across."
                              : "A %1 fillet is too big here: the neighbouring face is only %2 across.",
                      {fmtMm(radius), fmtMm(nearest)});
        }
        else {
            msg = fmt(chamfer ? "The bevels for a %1 chamfer could not be joined where these edges meet."
                              : "The rounded surfaces for a %1 fillet could not be joined where these edges meet.",
                      {fmtMm(radius)});
        }
        Error err = makeError(what + ".failed", msg, sug, why);
        if (fixSize > 0) {
            err.fixes.push_back({fmt("Use %1", {fmtMm(fixSize)}), "size", fmtMm(fixSize)});
        }
        return err;
    }
    NamedShape out(*res);
    propagateFaceNames(*mk, {&body}, out);
    for (size_t k = 0; k < edges.size(); ++k) {
        assignNames(out, toVector(mk->Generated(edges[k]), TopAbs_FACE),
                    featureId + "/" + what + ":" + body.edgeName(edgeIndices[k]));
    }
    std::vector<TopoDS_Shape> corners;
    for (auto& e : edges) {
        for (TopExp_Explorer v(e, TopAbs_VERTEX); v.More(); v.Next()) {
            for (auto& g : toVector(mk->Generated(v.Current()), TopAbs_FACE)) {
                corners.push_back(g);
            }
        }
    }
    if (!corners.empty()) {
        assignNames(out, corners, featureId + "/corner");
    }
    nameLeftovers(out, featureId);
    return out;
}
}  // namespace

Result<NamedShape> filletEdges(const std::string& featureId,
                               const NamedShape& body,
                               const std::vector<int>& edgeIndices,
                               double radius,
                               const EdgeOptions& options)
{
    return edgeTreatment(featureId, body, edgeIndices, radius, false, options);
}

Result<NamedShape> chamferEdges(const std::string& featureId,
                                const NamedShape& body,
                                const std::vector<int>& edgeIndices,
                                double distance,
                                const EdgeOptions& options)
{
    return edgeTreatment(featureId, body, edgeIndices, distance, true, options);
}

// ---------------------------------------------------------------- shell

namespace
{
std::optional<TopoDS_Shape> tryShell(const TopoDS_Shape& shape,
                                     const TopTools_ListOfShape& openings,
                                     double t,
                                     std::unique_ptr<BRepOffsetAPI_MakeThickSolid>* keep = nullptr,
                                     std::string* why = nullptr,
                                     bool outside = false)
{
    try {
        // Our own checks, so the answer does not depend on the OCCT version (7.9 and 8.0
        // build a self-overlapping "shell" where 7.6 refuses): a wall can never be thicker
        // than the part's smallest size, and an inward shell must remove material and stay
        // inside the original outline.
        Bnd_Box ob;
        BRepBndLib::AddOptimal(shape, ob, false, false);
        double x0, y0, z0, x1, y1, z1;
        ob.Get(x0, y0, z0, x1, y1, z1);
        if (!outside && t >= std::min({x1 - x0, y1 - y0, z1 - z0})) {
            if (why) {
                *why = "thicker than the part";
            }
            return std::nullopt;
        }
        GProp_GProps og;
        BRepGProp::VolumeProperties(shape, og);
        auto mk = std::make_unique<BRepOffsetAPI_MakeThickSolid>();
        // outward walls keep sharp corners (arc joins would round every outside edge)
        mk->MakeThickSolidByJoin(shape, openings, outside ? t : -t, 1e-3, BRepOffset_Skin, outside, false,
                                 outside ? GeomAbs_Intersection : GeomAbs_Arc);
        mk->Build();
        if (!mk->IsDone()) {
            return std::nullopt;
        }
        TopoDS_Shape res = mk->Shape();
        if (res.IsNull() || !BRepCheck_Analyzer(res).IsValid()) {
            if (why) {
                *why = "result failed BRepCheck";
            }
            return std::nullopt;
        }
        GProp_GProps g;
        BRepGProp::VolumeProperties(res, g);
        if (!(g.Mass() > 0) || (!outside && g.Mass() >= og.Mass() * (1 - 1e-9))) {
            return std::nullopt;
        }
        Bnd_Box rb;
        BRepBndLib::AddOptimal(res, rb, false, false);
        double a0, b0, c0, a1, b1, c1;
        rb.Get(a0, b0, c0, a1, b1, c1);
        // inside: the result stays within the part; outside: within the part grown by the wall
        const double grow = outside ? t : 0;
        const double tol = 1e-4 * std::max({x1 - x0, y1 - y0, z1 - z0}) + 1e-6 + grow;
        if (a0 < x0 - tol || b0 < y0 - tol || c0 < z0 - tol || a1 > x1 + tol || b1 > y1 + tol || c1 > z1 + tol) {
            if (why) {
                *why = "result grows outside the part";
            }
            return std::nullopt;
        }
        if (keep) {
            *keep = std::move(mk);
        }
        return res;
    }
    catch (const Standard_Failure& f) {
        if (why) {
            *why = kernelText(f);
        }
        return std::nullopt;
    }
}
}  // namespace

Result<NamedShape> shellSolid(const std::string& featureId,
                              const NamedShape& body,
                              const std::vector<int>& openFaces,
                              double thickness,
                              bool outside)
{
    const std::string wall = outside ? "/outer:" : "/inner:";
    if (!(thickness > 1e-6)) {
        return makeError("shell.thickness", "The wall thickness must be larger than zero.");
    }
    if (openFaces.empty()) {
        // A closed hollow. OCCT's thick-solid builder returns only the inward offset solid
        // when no face is removed, so build it explicitly: original minus inward offset.
        try {
            const Bnd_Box bb = [&] {
                Bnd_Box b;
                BRepBndLib::AddOptimal(body.shape(), b, false, false);
                return b;
            }();
            double x0, y0, z0, x1, y1, z1;
            bb.Get(x0, y0, z0, x1, y1, z1);
            if (!outside && 2 * thickness >= std::min({x1 - x0, y1 - y0, z1 - z0})) {
                return makeError("shell.failed", fmt("A %1 wall does not fit inside this shape.", {fmtMm(thickness)}),
                                 {"Try a thinner wall."});
            }
            BRepOffsetAPI_MakeOffsetShape off;
            off.PerformByJoin(body.shape(), outside ? thickness : -thickness, 1e-3, BRepOffset_Skin, outside, false,
                              outside ? GeomAbs_Intersection : GeomAbs_Arc);
            if (!off.IsDone() || off.Shape().IsNull()) {
                return makeError("shell.failed",
                                 fmt("A %1 wall does not fit inside this shape.", {fmtMm(thickness)}),
                                 {"Try a thinner wall."});
            }
            NamedShape inner(off.Shape());
            for (int u = 0; u < inner.faceCount(); ++u) {
                const Fingerprint fu = inner.faceFingerprint(u);
                double best = 1e300;
                std::string name;
                for (int f = 0; f < body.faceCount(); ++f) {
                    const Fingerprint fb = body.faceFingerprint(f);
                    const double d = std::hypot(fb.cx - fu.cx, fb.cy - fu.cy, fb.cz - fu.cz)
                                   + (fb.type == fu.type ? 0 : 1e6);
                    if (d < best) {
                        best = d;
                        name = body.faceName(f);
                    }
                }
                inner.setFaceName(u, featureId + wall + name);
            }
            inner.deriveEdgeNames();
            // inside: the part minus a smaller copy; outside: a bigger copy minus the part
            return outside ? booleanOp(featureId, BoolOp::Cut, inner, body) : booleanOp(featureId, BoolOp::Cut, body, inner);
        }
        catch (const Standard_Failure& f) {
            return makeError("shell.failed", fmt("A %1 wall does not fit inside this shape.", {fmtMm(thickness)}),
                             {"Try a thinner wall."}, kernelText(f));
        }
    }
    TopTools_ListOfShape openings;
    for (int f : openFaces) {
        openings.Append(body.face(f));
    }
    std::unique_ptr<BRepOffsetAPI_MakeThickSolid> mk;
    std::string why;
    auto res = tryShell(body.shape(), openings, thickness, &mk, &why, outside);
    if (!res) {
        double ok = 0, bad = thickness;
        for (double t = thickness * 0.5; t > thickness / 64; t *= 0.5) {
            if (tryShell(body.shape(), openings, t, nullptr, nullptr, outside)) {
                ok = t;
                break;
            }
            bad = t;
        }
        if (ok > 0) {
            for (int k = 0; k < 4; ++k) {
                double mid = 0.5 * (ok + bad);
                (tryShell(body.shape(), openings, mid, nullptr, nullptr, outside) ? ok : bad) = mid;
            }
        }
        std::vector<std::string> sug;
        double fixSize = 0;
        if (ok > 0) {
            double s = std::floor(ok * 0.95 * 10) / 10;
            if (s > 0) {
                sug.push_back(fmt("Try %1 or less.", {fmtMm(s)}));
                fixSize = s;
            }
        }
        sug.push_back("Add fillets to sharp inside corners first, or remove a different face.");
        Error err = makeError("shell.failed",
                              fmt("A %1 wall does not fit inside this shape: some part of it is thinner than twice the wall.",
                                  {fmtMm(thickness)}),
                              sug, why);
        if (fixSize > 0) {
            err.fixes.push_back({fmt("Use %1", {fmtMm(fixSize)}), "size", fmtMm(fixSize)});
        }
        return err;
    }
    NamedShape out(*res);
    propagateFaceNames(*mk, {&body}, out);
    // Inner walls: named after the outer face they are offset from (closest centroid
    // among faces of the same surface type), e.g. "sh1/inner:e1/side:7".
    std::vector<int> unnamed;
    for (int i = 0; i < out.faceCount(); ++i) {
        if (out.faceName(i).empty()) {
            unnamed.push_back(i);
        }
    }
    std::map<std::string, std::vector<TopoDS_Shape>> byName;
    const double tol = 1e-6 * std::max(1.0, bodyScale(body.shape()));
    for (int u : unnamed) {
        const Fingerprint fu = out.faceFingerprint(u);
        // An outer face OCCT rebuilt without reporting it (closed hollows do this) is the
        // same surface as an original face: it keeps that face's name.
        std::string same;
        for (int f = 0; f < body.faceCount() && same.empty(); ++f) {
            const Fingerprint fb = body.faceFingerprint(f);
            if (fb.type == fu.type && std::hypot(fb.cx - fu.cx, fb.cy - fu.cy, fb.cz - fu.cz) < tol
                && std::abs(fb.size - fu.size) <= 1e-6 * std::max(1.0, fb.size)) {
                same = body.faceName(f);
            }
        }
        if (!same.empty()) {
            byName[same].push_back(out.face(u));
            continue;
        }
        double best = 1e300;
        std::string name;
        for (int f = 0; f < body.faceCount(); ++f) {
            const Fingerprint fb = body.faceFingerprint(f);
            if (fb.type != fu.type) {
                continue;
            }
            const double d = std::hypot(fb.cx - fu.cx, fb.cy - fu.cy, fb.cz - fu.cz);
            if (d < best) {
                best = d;
                name = body.faceName(f);
            }
        }
        if (!name.empty()) {
            byName[featureId + wall + name].push_back(out.face(u));
        }
    }
    for (auto& [n, faces] : byName) {
        assignNames(out, faces, n);
    }
    nameLeftovers(out, featureId);
    return out;
}


// ---------------------------------------------------------------- queries

bool pointInside(const TopoDS_Shape& solid, double x, double y, double z)
{
    try {
        BRepClass3d_SolidClassifier c(solid, gp_Pnt(x, y, z), 1e-6);
        return c.State() == TopAbs_IN;
    }
    catch (const Standard_Failure&) {
        return false;
    }
}

std::vector<int> tangentChain(const NamedShape& body, int start, double angleTolDeg)
{
    std::vector<int> chain {start};
    if (start < 0 || start >= body.edgeCount()) {
        return chain;
    }
    TopTools_IndexedDataMapOfShapeListOfShape v2e;
    TopExp::MapShapesAndAncestors(body.shape(), TopAbs_VERTEX, TopAbs_EDGE, v2e);
    const double tol = std::cos(angleTolDeg * std::numbers::pi / 180);
    auto tangentAt = [](const TopoDS_Edge& e, const TopoDS_Vertex& v) -> std::optional<gp_Dir> {
        BRepAdaptor_Curve c(e);
        const double u = BRep_Tool::Parameter(v, e);
        gp_Pnt p;
        gp_Vec d;
        c.D1(u, p, d);
        if (d.Magnitude() < 1e-12) {
            return std::nullopt;
        }
        return gp_Dir(d);
    };
    std::vector<int> todo {start};
    std::set<int> seen {start};
    while (!todo.empty()) {
        const int cur = todo.back();
        todo.pop_back();
        const TopoDS_Edge& ce = TopoDS::Edge(body.edge(cur));
        for (TopExp_Explorer vx(ce, TopAbs_VERTEX); vx.More(); vx.Next()) {
            const TopoDS_Vertex& v = TopoDS::Vertex(vx.Current());
            const int k = v2e.FindIndex(v);
            if (k <= 0) {
                continue;
            }
            auto t0 = tangentAt(ce, v);
            for (TopTools_ListIteratorOfListOfShape it(v2e(k)); it.More(); it.Next()) {
                const int other = body.edgeIndex(it.Value());
                if (other < 0 || seen.count(other)) {
                    continue;
                }
                auto t1 = tangentAt(TopoDS::Edge(it.Value()), v);
                if (t0 && t1 && std::abs(t0->Dot(*t1)) >= tol) {
                    seen.insert(other);
                    chain.push_back(other);
                    todo.push_back(other);
                }
            }
        }
    }
    return chain;
}

// ---------------------------------------------------------------- inspect

MassProperties massProperties(const TopoDS_Shape& s)
{
    MassProperties m;
    if (s.IsNull()) {
        return m;
    }
    try {
        GProp_GProps v;
        BRepGProp::VolumeProperties(s, v);
        m.volume = v.Mass();
        gp_Pnt c = v.CentreOfMass();
        m.cx = c.X();
        m.cy = c.Y();
        m.cz = c.Z();
        GProp_GProps a;
        BRepGProp::SurfaceProperties(s, a);
        m.area = a.Mass();
        Bnd_Box b;
        BRepBndLib::AddOptimal(s, b, false, false);
        if (!b.IsVoid()) {
            b.Get(m.xmin, m.ymin, m.zmin, m.xmax, m.ymax, m.zmax);
        }
        TopTools_IndexedMapOfShape f, e, so;
        TopExp::MapShapes(s, TopAbs_FACE, f);
        TopExp::MapShapes(s, TopAbs_EDGE, e);
        TopExp::MapShapes(s, TopAbs_SOLID, so);
        m.faces = f.Extent();
        m.edges = e.Extent();
        m.solids = so.Extent();
        m.valid = BRepCheck_Analyzer(s).IsValid();
    }
    catch (const Standard_Failure&) {
        m.valid = false;
    }
    return m;
}

// ---------------------------------------------------------------- measure

MeasureItem measureItem(const TopoDS_Shape& s)
{
    MeasureItem m;
    try {
        if (s.ShapeType() == TopAbs_FACE) {
            const TopoDS_Face& f = TopoDS::Face(s);
            GProp_GProps g;
            BRepGProp::SurfaceProperties(f, g);
            m.size = g.Mass();
            BRepAdaptor_Surface a(f);
            switch (a.GetType()) {
                case GeomAbs_Plane: {
                    m.kind = word("flat face");
                    gp_Dir n = a.Plane().Axis().Direction();
                    if (f.Orientation() == TopAbs_REVERSED) {
                        n.Reverse();
                    }
                    m.normal = n;
                    break;
                }
                case GeomAbs_Cylinder:
                    m.kind = word("round face (cylinder)");
                    m.radius = a.Cylinder().Radius();
                    m.axis = a.Cylinder().Axis();
                    break;
                case GeomAbs_Cone:
                    m.kind = word("cone");
                    m.radius = a.Cone().RefRadius();
                    m.axis = a.Cone().Axis();
                    break;
                case GeomAbs_Sphere:
                    m.kind = word("ball face (sphere)");
                    m.radius = a.Sphere().Radius();
                    break;
                case GeomAbs_Torus:
                    m.kind = word("rounded face (torus)");
                    m.radius = a.Torus().MinorRadius();
                    m.axis = a.Torus().Axis();
                    break;
                default:
                    m.kind = word("curved face");
            }
        }
        else if (s.ShapeType() == TopAbs_EDGE) {
            const TopoDS_Edge& e = TopoDS::Edge(s);
            GProp_GProps g;
            BRepGProp::LinearProperties(e, g);
            m.size = g.Mass();
            BRepAdaptor_Curve c(e);
            if (c.GetType() == GeomAbs_Line) {
                m.kind = word("straight edge");
                m.axis = gp_Ax1(c.Line().Location(), c.Line().Direction());
            }
            else if (c.GetType() == GeomAbs_Circle) {
                const bool closed = BRep_Tool::IsClosed(e) || std::abs(c.LastParameter() - c.FirstParameter() - 2 * std::numbers::pi) < 1e-7;
                m.kind = closed ? "circle" : "arc";
                m.radius = c.Circle().Radius();
                m.axis = c.Circle().Axis();
            }
            else {
                m.kind = word("curved edge");
            }
        }
    }
    catch (const Standard_Failure&) {
        m.kind = word("shape");
    }
    return m;
}

Result<MeasureBetween> measureBetween(const TopoDS_Shape& a, const TopoDS_Shape& b)
{
    try {
        MeasureBetween r;
        BRepExtrema_DistShapeShape d(a, b);
        if (!d.IsDone() || d.NbSolution() == 0) {
            return makeError("measure.failed", "The distance between these two could not be measured.");
        }
        r.distance = d.Value();
        r.p1 = d.PointOnShape1(1);
        r.p2 = d.PointOnShape2(1);
        const MeasureItem ia = measureItem(a), ib = measureItem(b);
        // a direction for flat faces (normal) and straight edges (along the edge)
        auto dirOf = [](const MeasureItem& m) -> std::optional<std::pair<gp_Dir, bool>> {
            if (m.normal) {
                return std::pair {*m.normal, true};
            }
            if (m.kind == "straight edge" && m.axis) {
                return std::pair {m.axis->Direction(), false};
            }
            return std::nullopt;
        };
        auto da = dirOf(ia), db = dirOf(ib);
        if (da && db) {
            double ang = da->first.Angle(db->first);  // 0..pi
            ang = std::min(ang, std::numbers::pi - ang);
            if (da->second != db->second) {
                ang = std::numbers::pi / 2 - ang;  // face normal vs line direction
            }
            r.angle = std::max(0.0, ang);
        }
        const bool roundA = ia.radius && ia.axis && ia.kind != "straight edge";
        const bool roundB = ib.radius && ib.axis && ib.kind != "straight edge";
        if (roundA && roundB && ia.axis->Direction().IsParallel(ib.axis->Direction(), 1e-6)) {
            r.centres = gp_Lin(*ia.axis).Distance(ib.axis->Location());
        }
        return r;
    }
    catch (const Standard_Failure& f) {
        return makeError("measure.failed", "The distance between these two could not be measured.", {}, kernelText(f));
    }
}

// ---------------------------------------------------------------- display

DisplayMesh tessellate(const TopoDS_Shape& original, double linearDeflection)
{
    DisplayMesh out;
    if (original.IsNull()) {
        return out;
    }
    try {
        // Mesh a private copy: meshing writes triangulations into the shared shape data.
        TopoDS_Shape s = BRepBuilderAPI_Copy(original, true, false).Shape();
        BRepMesh_IncrementalMesh mesher(s, linearDeflection, false, 0.35, true);
        TopTools_IndexedMapOfShape faces, edges;
        TopExp::MapShapes(s, TopAbs_FACE, faces);
        TopExp::MapShapes(s, TopAbs_EDGE, edges);
        for (int fi = 1; fi <= faces.Extent(); ++fi) {
            const TopoDS_Face& face = TopoDS::Face(faces(fi));
            TopLoc_Location loc;
            Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull()) {
                continue;
            }
            const gp_Trsf tr = loc.Transformation();
            const bool rev = face.Orientation() == TopAbs_REVERSED;
            const uint32_t base = static_cast<uint32_t>(out.positions.size() / 3);
            // Normals from the exact surface at each node's UV (works on OCCT 7.6 .. 8.x).
            BRepAdaptor_Surface surf(face, false);
            BRepLProp_SLProps props(surf, 1, 1e-7);
            const bool haveUV = tri->HasUVNodes();
            for (int i = 1; i <= tri->NbNodes(); ++i) {
                gp_Pnt p = tri->Node(i).Transformed(tr);
                gp_Dir n(0, 0, 1);
                if (haveUV) {
                    gp_Pnt2d uv = tri->UVNode(i);
                    props.SetParameters(uv.X(), uv.Y());
                    if (props.IsNormalDefined()) {
                        n = props.Normal();
                    }
                }
                n = n.Transformed(tr);
                if (rev) {
                    n.Reverse();
                }
                out.positions.insert(out.positions.end(),
                                     {float(p.X()), float(p.Y()), float(p.Z())});
                out.normals.insert(out.normals.end(), {float(n.X()), float(n.Y()), float(n.Z())});
            }
            for (int i = 1; i <= tri->NbTriangles(); ++i) {
                int a, b, c;
                tri->Triangle(i).Get(a, b, c);
                if (rev) {
                    std::swap(b, c);
                }
                out.indices.insert(out.indices.end(),
                                   {base + uint32_t(a - 1), base + uint32_t(b - 1), base + uint32_t(c - 1)});
                out.triFace.push_back(uint32_t(fi - 1));
            }
        }
        for (int ei = 1; ei <= edges.Extent(); ++ei) {
            const TopoDS_Edge& e = TopoDS::Edge(edges(ei));
            if (BRep_Tool::Degenerated(e)) {
                continue;
            }
            BRepAdaptor_Curve c(e);
            GCPnts_TangentialDeflection d(c, 0.2, linearDeflection);
            for (int i = 1; i < d.NbPoints(); ++i) {
                gp_Pnt a = d.Value(i), b = d.Value(i + 1);
                out.edgeLines.insert(out.edgeLines.end(), {float(a.X()), float(a.Y()), float(a.Z()),
                                                           float(b.X()), float(b.Y()), float(b.Z())});
                out.segEdge.push_back(uint32_t(ei - 1));
            }
        }
    }
    catch (const Standard_Failure&) {
        // A display mesh failure must never take the app down; show what we have.
    }
    return out;
}

// ---------------------------------------------------------------- interchange

namespace
{
// Writes through a temporary file then renames, so a crash never leaves a half file.
template<typename WriteFn>
Result<bool> atomicWrite(const std::string& path, WriteFn&& fn)
{
    namespace fs = std::filesystem;
    fs::path target(path);
    fs::path tmp = target;
    tmp += ".partial";
    std::error_code ec;
    if (!fn(tmp.string())) {
        fs::remove(tmp, ec);
        return makeError("export.write", "The file could not be written.",
                         {"Check that the folder exists and you can write to it."});
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return makeError("export.write", "The file could not be written.", {}, ec.message());
    }
    return true;
}
}  // namespace

Result<bool> exportStl(const TopoDS_Shape& original, const std::string& path, double deflection)
{
    if (original.IsNull()) {
        return makeError("export.empty", "There is nothing to export yet.");
    }
    try {
        TopoDS_Shape s = BRepBuilderAPI_Copy(original, true, false).Shape();
        BRepMesh_IncrementalMesh mesher(s, deflection, false, 0.2, true);
        return atomicWrite(path, [&](const std::string& p) {
            StlAPI_Writer w;
            w.ASCIIMode() = false;
            return static_cast<bool>(w.Write(s, p.c_str()));
        });
    }
    catch (const Standard_Failure& f) {
        return makeError("export.stl", "The STL file could not be created.", {}, kernelText(f));
    }
}

Result<bool> exportStep(const TopoDS_Shape& s, const std::string& path)
{
    if (s.IsNull()) {
        return makeError("export.empty", "There is nothing to export yet.");
    }
    try {
        return atomicWrite(path, [&](const std::string& p) {
            STEPControl_Writer w;
            Interface_Static::SetCVal("write.step.unit", "MM");
            if (w.Transfer(s, STEPControl_AsIs) != IFSelect_RetDone) {
                return false;
            }
            return w.Write(p.c_str()) == IFSelect_RetDone;
        });
    }
    catch (const Standard_Failure& f) {
        return makeError("export.step", "The STEP file could not be created.", {}, kernelText(f));
    }
}

Result<ImportedShape> importStep(const std::string& path)
{
    try {
        STEPControl_Reader r;
        if (r.ReadFile(path.c_str()) != IFSelect_RetDone) {
            return makeError("import.step", "This STEP file could not be read. It may be damaged.");
        }
        ImportedShape out;
        TColStd_SequenceOfAsciiString len, ang, solid;
        r.FileUnits(len, ang, solid);
        if (len.Length() > 0) {
            std::string u = len.Value(1).ToCString();
            for (auto& c : u) {
                c = char(std::tolower(static_cast<unsigned char>(c)));
            }
            out.fileUnit = u.find("milli") != std::string::npos ? "mm"
                         : u.find("centi") != std::string::npos ? "cm"
                         : u.find("inch") != std::string::npos  ? "in"
                         : u.find("foot") != std::string::npos || u.find("feet") != std::string::npos ? "ft"
                         : u.find("metre") != std::string::npos || u.find("meter") != std::string::npos ? "m"
                                                                 : u;
        }
        if (r.TransferRoots() == 0) {
            return makeError("import.step", "This STEP file has no shapes Jigsmith can use.");
        }
        out.shape = r.OneShape();
        return out;
    }
    catch (const Standard_Failure& f) {
        return makeError("import.step", "This STEP file could not be read. It may be damaged.", {},
                         kernelText(f));
    }
}

std::string shapeToText(const TopoDS_Shape& s)
{
    std::ostringstream os;
    BRepTools::Write(s, os);
    return os.str();
}

Result<TopoDS_Shape> shapeFromText(const std::string& text)
{
    try {
        std::istringstream is(text);
        TopoDS_Shape s;
        BRep_Builder b;
        BRepTools::Read(s, is, b);
        if (s.IsNull()) {
            return makeError("import.damaged", "The imported solid stored in this design is damaged.",
                             {"Delete the import step and import the file again."});
        }
        return s;
    }
    catch (const Standard_Failure& f) {
        return makeError("import.damaged", "The imported solid stored in this design is damaged.",
                         {"Delete the import step and import the file again."}, kernelText(f));
    }
}

Result<std::vector<NamedShape>> importedBodies(const std::string& id, const TopoDS_Shape& s)
{
    std::vector<NamedShape> out;
    for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) {
        NamedShape b(ex.Current());
        for (int i = 0; i < b.faceCount(); ++i) {
            b.setFaceName(i, id + "/face:" + std::to_string(i + 1));
        }
        b.deriveEdgeNames();
        out.push_back(std::move(b));
    }
    if (out.empty()) {
        return makeError("import.no_solid", "This file has surfaces but no closed solid, so there is nothing to build on.",
                         {"Export a solid (not just surfaces) from the other program."});
    }
    return out;
}

// ---------------------------------------------------------------- section caps

Result<TopoDS_Shape> sectionFaces(const TopoDS_Shape& solid, const gp_Pln& plane)
{
    try {
        Bnd_Box box;
        BRepBndLib::Add(solid, box);
        if (box.IsVoid()) {
            return TopoDS_Shape();
        }
        const double m = std::sqrt(box.SquareExtent()) + 10;
        // a square of the plane, centred where the box's centre projects onto it
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        const gp_Pnt c((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
        double u, v;
        ElSLib::Parameters(plane, c, u, v);
        TopoDS_Face sheet = BRepBuilderAPI_MakeFace(plane, u - m, u + m, v - m, v + m).Face();
        BRepAlgoAPI_Common common(solid, sheet);
        common.Build();
        if (!common.IsDone()) {
            return makeError("section.failed", "The cut could not be worked out for this shape.");
        }
        return common.Shape();
    }
    catch (const Standard_Failure& f) {
        return makeError("section.failed", "The cut could not be worked out for this shape.", {}, kernelText(f));
    }
}

}  // namespace jig
