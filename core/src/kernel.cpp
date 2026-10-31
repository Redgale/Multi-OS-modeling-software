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
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepOffsetAPI_MakeOffsetShape.hxx>
#include <BRepOffsetAPI_MakeThickSolid.hxx>
#include <BRepGProp.hxx>
#include <BRepLProp_SLProps.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <gp_Lin.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_TangentialDeflection.hxx>
#include <GProp_GProps.hxx>
#include <Interface_Static.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <BRepTools_History.hxx>
#include <STEPControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <StlAPI_Writer.hxx>
#include <TColStd_SequenceOfAsciiString.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

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

Result<ProfileSet> buildProfiles(const Sketch& sketch, const gp_Ax3& frame)
{
    try {
        auto to3d = [&](double x, double y) {
            gp_XYZ p = frame.Location().XYZ() + frame.XDirection().XYZ() * x
                     + frame.YDirection().XYZ() * y;
            return gp_Pnt(p);
        };
        TopTools_ListOfShape tools;
        TopTools_DataMapOfShapeInteger origEntity;
        double ext = 1;
        auto grow = [&](double x, double y, double r) {
            ext = std::max({ext, std::abs(x) + r, std::abs(y) + r});
        };
        for (auto& [id, e] : sketch.entities()) {
            if (e.construction) {
                continue;
            }
            TopoDS_Edge edge;
            if (e.type == EntityType::Line) {
                auto [x1, y1] = sketch.pointXY(e.p1);
                auto [x2, y2] = sketch.pointXY(e.p2);
                if (std::hypot(x2 - x1, y2 - y1) < 1e-7) {
                    continue;
                }
                grow(x1, y1, 0);
                grow(x2, y2, 0);
                edge = BRepBuilderAPI_MakeEdge(to3d(x1, y1), to3d(x2, y2)).Edge();
            }
            else if (e.type == EntityType::Circle || e.type == EntityType::Arc) {
                auto [cx, cy] = sketch.pointXY(e.center);
                if (e.r < 1e-7) {
                    continue;
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
            else {
                continue;
            }
            tools.Append(edge);
            origEntity.Bind(edge, id);
        }
        ProfileSet set;
        if (tools.IsEmpty()) {
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
        return set;
    }
    catch (const Standard_Failure& f) {
        return makeError("sketch.profiles", "The sketch shape could not be processed.", {},
                         kernelText(f));
    }
}

// ---------------------------------------------------------------- extrude

Result<NamedShape> makeExtrusion(const ExtrudeInput& in)
{
    if (in.profiles.empty()) {
        return makeError("extrude.no_profile", "Pick at least one closed area (profile) to extrude.");
    }
    if (!(in.distance > 1e-6)) {
        return makeError("extrude.distance", "The extrude distance must be larger than zero.",
                         {"Type a distance such as 10 mm, or drag the arrow."});
    }
    try {
        gp_Vec n(in.normal);
        gp_Vec v = n * in.distance;
        gp_Vec shift(0, 0, 0);
        if (in.direction == ExtrudeDirection::Reverse) {
            v.Reverse();
        }
        else if (in.direction == ExtrudeDirection::Symmetric) {
            shift = n * (-in.distance / 2);
        }
        const bool multi = in.profiles.size() > 1;
        std::unique_ptr<NamedShape> acc;
        for (const Profile* p : in.profiles) {
            TopoDS_Face face = p->face;
            TopTools_DataMapOfShapeInteger edgeEntity = p->edgeEntity;
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
    if (spec.type == T::Counterbore && !(spec.cboreDiameter > spec.diameter && spec.cboreDepth > 0 && spec.cboreDepth < depth)) {
        return makeError("hole.counterbore", "The counterbore must be wider than the hole and shallower than it.");
    }
    if (spec.type == T::Countersink && !(spec.csinkDiameter > spec.diameter && spec.csinkAngle > 0.1 && spec.csinkAngle < 3.0)) {
        return makeError("hole.countersink", "The countersink must be wider than the hole, with an angle between 6 and 170 degrees.");
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
            return makeError("boolean.not_touching",
                             "The new shape does not touch the body, so it cannot be joined.",
                             {"Use 'New body' if you want a separate part.",
                              "Change the distance or sketch position so the shapes touch."});
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
                                      std::string* why = nullptr)
{
    try {
        std::unique_ptr<BRepFilletAPI_LocalOperation> mk;
        if (chamfer) {
            auto c = std::make_unique<BRepFilletAPI_MakeChamfer>(shape);
            for (auto& e : edges) {
                c->Add(r, e);
            }
            mk = std::move(c);
        }
        else {
            auto f = std::make_unique<BRepFilletAPI_MakeFillet>(shape);
            for (auto& e : edges) {
                f->Add(r, e);
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

std::string fmtMm(double v)
{
    std::ostringstream os;
    os.precision(v < 10 ? 2 : 3);
    os << v << " mm";
    return os.str();
}

}  // namespace

namespace
{
Result<NamedShape> edgeTreatment(const std::string& featureId,
                                 const NamedShape& body,
                                 const std::vector<int>& edgeIndices,
                                 double radius,
                                 bool chamfer)
{
    const std::string what = chamfer ? "chamfer" : "fillet";
    if (edgeIndices.empty()) {
        return makeError(what + ".no_edges", chamfer ? "Pick at least one edge to bevel." : "Pick at least one edge to round.");
    }
    if (!(radius > 1e-6)) {
        return makeError(what + ".radius", chamfer ? "The chamfer distance must be larger than zero."
                                                  : "The fillet radius must be larger than zero.");
    }
    std::vector<TopoDS_Edge> edges;
    for (int i : edgeIndices) {
        edges.push_back(TopoDS::Edge(body.edge(i)));
    }
    std::unique_ptr<BRepFilletAPI_LocalOperation> mk;
    std::string why;
    auto res = tryEdgeOp(body.shape(), edges, radius, chamfer, &mk, &why);
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
            if (tryEdgeOp(body.shape(), edges, r, chamfer)) {
                ok = r;
                break;
            }
            bad = r;
        }
        if (ok > 0) {
            for (int k = 0; k < 4; ++k) {
                double mid = 0.5 * (ok + bad);
                (tryEdgeOp(body.shape(), edges, mid, chamfer) ? ok : bad) = mid;
            }
        }
        std::vector<std::string> sug;
        if (ok > 0) {
            double s = std::floor(ok * 0.95 * 10) / 10;
            if (s > 0) {
                sug.push_back("Try " + fmtMm(s) + " or less.");
            }
        }
        sug.push_back(chamfer ? "Bevel fewer edges at once, or chamfer the longer edges first."
                              : "Round fewer edges at once, or fillet the larger edges first.");
        std::string msg;
        if (nearest < (chamfer ? 1.0 : 2.0) * radius) {
            msg = "A " + fmtMm(radius) + " " + what + " is too big here: the neighbouring face is only "
                + fmtMm(nearest) + " across.";
        }
        else {
            msg = chamfer ? "The bevels for a " + fmtMm(radius) + " chamfer could not be joined where these edges meet."
                          : "The rounded surfaces for a " + fmtMm(radius)
                                + " fillet could not be joined where these edges meet.";
        }
        return makeError(what + ".failed", msg, sug, why);
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
                               double radius)
{
    return edgeTreatment(featureId, body, edgeIndices, radius, false);
}

Result<NamedShape> chamferEdges(const std::string& featureId,
                                const NamedShape& body,
                                const std::vector<int>& edgeIndices,
                                double distance)
{
    return edgeTreatment(featureId, body, edgeIndices, distance, true);
}

// ---------------------------------------------------------------- shell

namespace
{
std::optional<TopoDS_Shape> tryShell(const TopoDS_Shape& shape,
                                     const TopTools_ListOfShape& openings,
                                     double t,
                                     std::unique_ptr<BRepOffsetAPI_MakeThickSolid>* keep = nullptr,
                                     std::string* why = nullptr)
{
    try {
        auto mk = std::make_unique<BRepOffsetAPI_MakeThickSolid>();
        mk->MakeThickSolidByJoin(shape, openings, -t, 1e-3);
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
        if (!(g.Mass() > 0)) {
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
                              double thickness)
{
    if (!(thickness > 1e-6)) {
        return makeError("shell.thickness", "The wall thickness must be larger than zero.");
    }
    if (openFaces.empty()) {
        // A closed hollow. OCCT's thick-solid builder returns only the inward offset solid
        // when no face is removed, so build it explicitly: original minus inward offset.
        try {
            BRepOffsetAPI_MakeOffsetShape off;
            off.PerformByJoin(body.shape(), -thickness, 1e-3);
            if (!off.IsDone() || off.Shape().IsNull()) {
                return makeError("shell.failed",
                                 "A " + fmtMm(thickness) + " wall does not fit inside this shape.",
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
                inner.setFaceName(u, featureId + "/inner:" + name);
            }
            inner.deriveEdgeNames();
            return booleanOp(featureId, BoolOp::Cut, body, inner);
        }
        catch (const Standard_Failure& f) {
            return makeError("shell.failed", "A " + fmtMm(thickness) + " wall does not fit inside this shape.",
                             {"Try a thinner wall."}, kernelText(f));
        }
    }
    TopTools_ListOfShape openings;
    for (int f : openFaces) {
        openings.Append(body.face(f));
    }
    std::unique_ptr<BRepOffsetAPI_MakeThickSolid> mk;
    std::string why;
    auto res = tryShell(body.shape(), openings, thickness, &mk, &why);
    if (!res) {
        double ok = 0, bad = thickness;
        for (double t = thickness * 0.5; t > thickness / 64; t *= 0.5) {
            if (tryShell(body.shape(), openings, t)) {
                ok = t;
                break;
            }
            bad = t;
        }
        if (ok > 0) {
            for (int k = 0; k < 4; ++k) {
                double mid = 0.5 * (ok + bad);
                (tryShell(body.shape(), openings, mid) ? ok : bad) = mid;
            }
        }
        std::vector<std::string> sug;
        if (ok > 0) {
            double s = std::floor(ok * 0.95 * 10) / 10;
            if (s > 0) {
                sug.push_back("Try " + fmtMm(s) + " or less.");
            }
        }
        sug.push_back("Add fillets to sharp inside corners first, or remove a different face.");
        return makeError("shell.failed",
                         "A " + fmtMm(thickness) + " wall does not fit inside this shape: some part of it is "
                             "thinner than twice the wall.",
                         sug, why);
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
            byName[featureId + "/inner:" + name].push_back(out.face(u));
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

}  // namespace jig
