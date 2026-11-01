// SPDX-License-Identifier: GPL-3.0-or-later
// Feature recompute. Each function reads a feature's plain-data parameters and
// the geometry state before it, and returns the state after it plus a status.
// A failing feature returns no state: the document keeps the previous geometry
// (clean rollback) and marks the step in the timeline.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <set>
#include <sstream>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <gp_Cylinder.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepTools.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepGProp_Face.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include "jig/document.hpp"
#include "jig/screws.hpp"

namespace jig
{
namespace
{

FeatureOutcome failed(std::string message, std::vector<std::string> suggestions = {}, std::string detail = {})
{
    FeatureOutcome o;
    o.status.level = StatusLevel::Error;
    o.status.message = std::move(message);
    o.status.suggestions = std::move(suggestions);
    o.status.detail = std::move(detail);
    return o;
}

FeatureOutcome failed(const Error& e)
{
    FeatureOutcome o = failed(e.message, e.suggestions, e.detail);
    o.status.fixes = e.fixes;
    return o;
}

// The same, with the kernel's generic "size" fix pointed at this step's own parameter.
FeatureOutcome failed(Error e, const std::string& sizeParam)
{
    for (auto& f : e.fixes) {
        if (f.param == "size") {
            f.param = sizeParam;
        }
    }
    return failed(e);
}

std::shared_ptr<ModelState> copyState(const std::shared_ptr<const ModelState>& in)
{
    return std::make_shared<ModelState>(*in);
}

std::optional<OriginPlane> planeFromString(const std::string& s)
{
    if (s == "XY") {
        return OriginPlane::XY;
    }
    if (s == "XZ") {
        return OriginPlane::XZ;
    }
    if (s == "YZ") {
        return OriginPlane::YZ;
    }
    return std::nullopt;
}

std::string mm(double v)
{
    std::ostringstream os;
    os.precision(3);
    os << v << " mm";
    return os.str();
}

// ---------------------------------------------------------------- planes

// A plane reference: "XY"/"XZ"/"YZ", the id of a construction plane, or {"face": ref}
// (a flat face). Used by sketches and construction planes.
Result<gp_Ax3> planeFrame(const nlohmann::json& ref, const std::shared_ptr<const ModelState>& in, FeatureStatus& status)
{
    if (ref.is_string()) {
        const std::string name = ref.get<std::string>();
        if (auto o = planeFromString(name)) {
            return originPlaneFrame(*o, 0);
        }
        auto it = in->planes.find(name);
        if (it == in->planes.end()) {
            return makeError("plane.missing", fmt("The plane '%1' is missing or comes later in the timeline.", {name}),
                             {"Pick another plane."});
        }
        return it->second;
    }
    if (ref.is_object() && ref.contains("face")) {
        ShapeRef fr = ShapeRef::fromJson(ref["face"]);
        auto bit = in->bodies.find(fr.body);
        if (bit == in->bodies.end()) {
            return makeError("plane.face", "The body of the face this step uses no longer exists.");
        }
        Resolution r = resolve(*bit->second, fr);
        if (r.status == ResolveStatus::Missing || r.status == ResolveStatus::Ambiguous) {
            return makeError("plane.face", "The face this step uses no longer exists or was split.",
                             {"Edit the step and pick the face again."});
        }
        if (r.status == ResolveStatus::Rebound && status.level == StatusLevel::Ok) {
            status.level = StatusLevel::Warning;
            status.message = "The original face changed; the most similar face is used. Check it.";
        }
        return faceFrame(bit->second->face(r.index));
    }
    return makeError("plane.ref", "This step refers to a plane in a way Jigsmith does not understand.");
}

// An origin axis (X, Y, Z) or a construction axis by its feature id.
std::optional<gp_Ax1> namedAxis(const std::string& name, const std::shared_ptr<const ModelState>& in)
{
    if (name == "X" || name == "Y" || name == "Z") {
        return gp_Ax1(gp_Pnt(0, 0, 0), name == "X" ? gp_Dir(1, 0, 0) : name == "Y" ? gp_Dir(0, 1, 0) : gp_Dir(0, 0, 1));
    }
    if (auto it = in->axes.find(name); it != in->axes.end()) {
        return it->second;
    }
    return std::nullopt;
}

// The origin (O) or a construction point by its feature id.
std::optional<gp_Pnt> namedPoint(const std::string& name, const std::shared_ptr<const ModelState>& in)
{
    if (name == "O") {
        return gp_Pnt(0, 0, 0);
    }
    if (auto it = in->points.find(name); it != in->points.end()) {
        return it->second;
    }
    return std::nullopt;
}

FeatureOutcome missingAxis(const std::string& name)
{
    return failed(fmt("The axis '%1' does not exist (or comes later in the timeline).", {name}),
                  {"Pick X, Y, Z or a construction axis made earlier."});
}

FeatureOutcome applyAxis(const Document&, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    gp_Ax1 axis;
    if (p.contains("normal")) {
        // square to a plane or flat face, through a point (the origin, or a construction point)
        auto pl = planeFrame(p["normal"], in, status);
        if (!pl) {
            return failed(pl.error());
        }
        const std::string name = p.value("at", std::string("O"));
        auto at = namedPoint(name, in);
        if (!at) {
            return failed(fmt("The point '%1' does not exist (or comes later in the timeline).", {name}),
                          {"Pick the origin (O) or a construction point made earlier."});
        }
        axis = gp_Ax1(*at, pl->Direction());
    }
    else if (p.contains("from")) {
        // through two construction points (or the origin)
        const std::string n1 = p.value("from", std::string {}), n2 = p.value("to", std::string {});
        auto a = namedPoint(n1, in), b = namedPoint(n2, in);
        if (!a || !b) {
            return failed(fmt("The point '%1' does not exist (or comes later in the timeline).", {!a ? n1 : n2}),
                          {"Pick the origin (O) or a construction point made earlier."});
        }
        if (a->Distance(*b) < 1e-7) {
            return failed("The two points are in the same place, so no axis runs through them.",
                          {"Pick two different points."});
        }
        axis = gp_Ax1(*a, gp_Dir(gp_Vec(*a, *b)));
    }
    else if (p.contains("through")) {
        // along a straight edge, or through the centre of a round edge or face
        const auto& t = p["through"];
        const bool face = t.contains("face");
        ShapeRef ref = ShapeRef::fromJson(face ? t["face"] : t.value("edge", nlohmann::json::object()));
        auto bit = in->bodies.find(ref.body);
        if (bit == in->bodies.end()) {
            return failed("The body this axis runs through no longer exists.", {"Edit the axis and pick again."});
        }
        Resolution r = resolve(*bit->second, ref);
        if (r.status == ResolveStatus::Missing || r.status == ResolveStatus::Ambiguous) {
            return failed("The edge or face this axis runs through no longer exists.", {"Edit the axis and pick again."});
        }
        const MeasureItem m = measureItem(face ? bit->second->face(r.index) : bit->second->edge(r.index));
        if (!m.axis) {
            return failed(fmt("An axis can run along a straight edge or through a round edge or face (a hole, a "
                              "cylinder); this %1 has no axis.", {m.kind}));
        }
        axis = *m.axis;
    }
    else {
        // where two planes or flat faces meet
        auto a = planeFrame(p.value("base", nlohmann::json("XY")), in, status);
        auto b = planeFrame(p.value("base2", nlohmann::json("XZ")), in, status);
        if (!a || !b) {
            return failed(!a ? a.error() : b.error());
        }
        const gp_Vec n1(a->Direction()), n2(b->Direction());
        const gp_Vec d = n1.Crossed(n2);
        if (d.Magnitude() < 1e-9) {
            return failed("The two planes are parallel, so they never meet.", {"Pick two planes that cross."});
        }
        // the point on both planes nearest the origin
        const double h1 = n1.Dot(gp_Vec(a->Location().XYZ())), h2 = n2.Dot(gp_Vec(b->Location().XYZ()));
        const double n12 = n1.Dot(n2), det = 1 - n12 * n12;
        const double c1 = (h1 - h2 * n12) / det, c2 = (h2 - h1 * n12) / det;
        axis = gp_Ax1(gp_Pnt((n1 * c1 + n2 * c2).XYZ()), gp_Dir(d));
    }
    auto out = copyState(in);
    out->axes[f.id] = axis;
    return {out, status};
}

FeatureOutcome applyPoint(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    gp_Pnt at;
    if (p.contains("through")) {
        // at an end, the middle or the centre of an edge, or the centre of a face
        const auto& t = p["through"];
        const bool face = t.contains("face");
        ShapeRef ref = ShapeRef::fromJson(face ? t["face"] : t.value("edge", nlohmann::json::object()));
        auto bit = in->bodies.find(ref.body);
        if (bit == in->bodies.end()) {
            return failed("The body this point sits on no longer exists.", {"Edit the point and pick again."});
        }
        Resolution r = resolve(*bit->second, ref);
        if (r.status == ResolveStatus::Missing || r.status == ResolveStatus::Ambiguous) {
            return failed("The edge or face this point sits on no longer exists.", {"Edit the point and pick again."});
        }
        if (face) {
            GProp_GProps props;
            BRepGProp::SurfaceProperties(bit->second->face(r.index), props);
            at = props.CentreOfMass();
        }
        else if (p.contains("plane")) {
            // where the edge (or its straight continuation) crosses a plane or flat face
            FeatureStatus ignored;
            auto pl = planeFrame(p["plane"], in, ignored);
            if (!pl) {
                return failed(pl.error());
            }
            BRepAdaptor_Curve c(TopoDS::Edge(bit->second->edge(r.index)));
            const gp_Pln plane(*pl);
            const gp_Dir n = plane.Axis().Direction();
            auto side = [&](double u) { return gp_Vec(plane.Location(), c.Value(u)).Dot(gp_Vec(n)); };
            double u0 = c.FirstParameter(), u1 = c.LastParameter();
            if (c.GetType() == GeomAbs_Line) {
                // a straight edge: its line, so the plane may be beyond its ends
                const gp_Lin l = c.Line();
                const double along = gp_Vec(l.Direction()).Dot(gp_Vec(n));
                if (std::abs(along) < 1e-9) {
                    return failed("The edge runs parallel to the plane, so it never crosses it.",
                                  {"Pick an edge that points through the plane."});
                }
                const double t = -gp_Vec(plane.Location(), l.Location()).Dot(gp_Vec(n)) / along;
                at = l.Location().Translated(gp_Vec(l.Direction()) * t);
            }
            else {
                // a curve: the first crossing along it, found by halving
                const int steps = 64;
                bool found = false;
                double a = u0, fa = side(u0);
                for (int k = 1; k <= steps && !found; ++k) {
                    const double b = u0 + (u1 - u0) * k / steps, fb = side(b);
                    if (std::abs(fa) < 1e-9) {
                        at = c.Value(a);
                        found = true;
                        break;
                    }
                    if (fa * fb < 0) {
                        double lo = a, hi = b, flo = fa;
                        for (int it = 0; it < 60; ++it) {
                            const double mid = 0.5 * (lo + hi), fm = side(mid);
                            (fm * flo <= 0 ? hi : lo) = mid;
                            if (fm * flo > 0) {
                                flo = fm;
                            }
                        }
                        at = c.Value(0.5 * (lo + hi));
                        found = true;
                    }
                    a = b;
                    fa = fb;
                }
                if (!found && std::abs(side(u1)) < 1e-9) {
                    at = c.Value(u1);
                    found = true;
                }
                if (!found) {
                    return failed("This curved edge does not reach the plane.", {"Pick another edge or plane."});
                }
            }
        }
        else {
            const TopoDS_Edge e = TopoDS::Edge(bit->second->edge(r.index));
            const std::string where = p.value("end", std::string("start"));
            BRepAdaptor_Curve c(e);
            if (where == "center") {
                const MeasureItem m = measureItem(e);
                if (!m.radius || !m.axis) {
                    return failed(fmt("Only a circle or an arc has a centre; this is a %1.", {m.kind}),
                                  {"Choose its start, end or middle instead."});
                }
                at = m.axis->Location();
            }
            else if (where == "start" || where == "end" || where == "mid") {
                double u0 = c.FirstParameter(), u1 = c.LastParameter();
                if (e.Orientation() == TopAbs_REVERSED) {
                    std::swap(u0, u1);
                }
                at = c.Value(where == "start" ? u0 : where == "end" ? u1 : 0.5 * (u0 + u1));
            }
            else {
                return failed(fmt("A point on an edge sits at its start, end, middle or centre, not at '%1'.", {where}));
            }
        }
    }
    else {
        // typed coordinates, each an expression (parameters allowed)
        const auto xyz = p.value("at", nlohmann::json::array({"0 mm", "0 mm", "0 mm"}));
        if (!xyz.is_array() || xyz.size() != 3) {
            return failed("A point needs three coordinates: X, Y and Z.");
        }
        double v[3];
        for (int i = 0; i < 3; ++i) {
            auto q = doc.evalLength(xyz[i].is_string() ? xyz[i].get<std::string>() : xyz[i].dump());
            if (!q) {
                return failed(q.error());
            }
            v[i] = q.value();
        }
        at = gp_Pnt(v[0], v[1], v[2]);
    }
    auto out = copyState(in);
    out->points[f.id] = at;
    return {out, {}};
}

std::vector<std::string> bodyList(const nlohmann::json& j);

FeatureOutcome applyRemove(const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    std::vector<std::string> bodies = bodyList(f.params.value("bodies", nlohmann::json::array()));
    if (bodies.empty()) {
        return failed("Pick at least one body to remove.");
    }
    auto out = copyState(in);
    for (const std::string& b : bodies) {
        if (!out->bodies.erase(b)) {
            return failed(fmt("The body this step removes (%1) no longer exists.", {b}),
                          {"Delete this step, or edit an earlier step so the body is made again."});
        }
    }
    return {out, {}};
}

FeatureOutcome applyImport(const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    auto shape = shapeFromText(f.params.value("brep", std::string {}));
    if (!shape) {
        return failed(shape.error());
    }
    auto bodies = importedBodies(f.id, shape.value());
    if (!bodies) {
        return failed(bodies.error());
    }
    auto out = copyState(in);
    for (size_t k = 0; k < bodies->size(); ++k) {
        const std::string id = bodies->size() == 1 ? f.id : f.id + "." + std::to_string(k + 1);
        out->bodies[id] = std::make_shared<NamedShape>(std::move((*bodies)[k]));
    }
    return {out, {}};
}

FeatureOutcome applyPlane(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    const std::string kind = p.value("kind", std::string("offset"));
    auto base = planeFrame(p.value("base", nlohmann::json("XY")), in, status);
    if (!base) {
        return failed(base.error());
    }
    gp_Ax3 frame = base.value();
    if (kind == "offset") {
        auto d = doc.evalLength(p.value("distance", std::string("10 mm")));
        if (!d) {
            return failed(d.error());
        }
        frame.Translate(gp_Vec(frame.Direction()) * d.value());
    }
    else if (kind == "angle") {
        auto a = doc.evalAngle(p.value("angle", std::string("45 deg")));
        if (!a) {
            return failed(a.error());
        }
        gp_Ax1 axis;
        const auto ax = p.value("axis", nlohmann::json("X"));
        if (ax.is_object() && ax.contains("edge")) {
            ShapeRef er = ShapeRef::fromJson(ax["edge"]);
            auto bit = in->bodies.find(er.body);
            if (bit == in->bodies.end()) {
                return failed("The body of the edge this plane turns around no longer exists.");
            }
            Resolution r = resolve(*bit->second, er);
            if (r.status == ResolveStatus::Missing || r.status == ResolveStatus::Ambiguous) {
                return failed("The edge this plane turns around no longer exists.", {"Edit the plane and pick an edge."});
            }
            const MeasureItem m = measureItem(bit->second->edge(r.index));
            if (m.kind != "straight edge" || !m.axis) {
                return failed("A plane can only turn around a straight edge.");
            }
            axis = *m.axis;
        }
        else {
            const std::string n = ax.is_string() ? ax.get<std::string>() : "X";
            auto na = namedAxis(n, in);
            if (!na) {
                return missingAxis(n);
            }
            axis = *na;
        }
        if (std::abs(axis.Direction().Dot(frame.Direction())) > 1e-6) {
            return failed("The turning axis must lie in the base plane (it is not parallel to it).",
                          {"Pick an axis or edge that lies in the base plane."});
        }
        frame.Rotate(axis, a.value());
    }
    else if (kind == "midplane") {
        auto other = planeFrame(p.value("base2", nlohmann::json("XY")), in, status);
        if (!other) {
            return failed(other.error());
        }
        const gp_Dir n1 = frame.Direction(), n2 = other->Direction();
        if (!n1.IsParallel(n2, 1e-6)) {
            return failed("A midplane needs two parallel faces or planes.", {"Pick two faces that face each other."});
        }
        const double gap = gp_Vec(frame.Location(), other->Location()).Dot(gp_Vec(n1));
        frame.Translate(gp_Vec(n1) * (gap / 2));
    }
    else {
        return failed("This construction plane has an unknown kind.");
    }
    auto out = copyState(in);
    out->planes[f.id] = frame;
    return {out, status};
}

// ---------------------------------------------------------------- sketch

FeatureOutcome applySketch(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    auto st = std::make_shared<SketchState>();
    FeatureStatus status;

    if (p.contains("face")) {
        ShapeRef ref = ShapeRef::fromJson(p["face"]);
        auto bit = in->bodies.find(ref.body);
        if (bit == in->bodies.end()) {
            return failed("The body this sketch sits on no longer exists.",
                          {"Edit the sketch and choose a new plane or face."});
        }
        Resolution r = resolve(*bit->second, ref);
        if (r.status == ResolveStatus::Missing) {
            return failed("The face this sketch sits on no longer exists.",
                          {"Edit the sketch and choose a new plane or face."});
        }
        if (r.status == ResolveStatus::Ambiguous) {
            return failed("The face this sketch sits on was split, and Jigsmith can't tell which "
                          "piece you meant.",
                          {"Edit the sketch and pick the face again."});
        }
        if (r.status == ResolveStatus::Rebound) {
            status.level = StatusLevel::Warning;
            status.message = "The original face changed; the sketch was re-attached to the most "
                             "similar face. Check it is the right one.";
        }
        auto fr = faceFrame(bit->second->face(r.index));
        if (!fr) {
            return failed(fr.error());
        }
        st->frame = fr.value();
    }
    else if (const std::string pl = p.value("plane", std::string("XY")); !planeFromString(pl)) {
        // a construction plane, by id
        auto fr = planeFrame(nlohmann::json(pl), in, status);
        if (!fr) {
            return failed(fr.error());
        }
        st->frame = fr.value();
    }
    else {
        auto plane = planeFromString(p.value("plane", std::string("XY")));
        if (!plane) {
            return failed("This sketch refers to an unknown plane.");
        }
        double offset = 0;
        if (p.contains("offset")) {
            auto o = doc.evalLength(p["offset"].get<std::string>());
            if (!o) {
                return failed(o.error());
            }
            offset = o.value();
        }
        st->frame = originPlaneFrame(*plane, offset);
    }

    auto sk = Sketch::fromJson(p.value("geometry", nlohmann::json {{"entities", nlohmann::json::array()},
                                                                   {"constraints", nlohmann::json::array()}}));
    if (!sk) {
        return failed(sk.error());
    }
    st->solved = std::move(sk.value());
    // projected edges follow the model as it is before this sketch
    for (const auto& link : st->solved.links()) {
        ShapeRef ref = ShapeRef::fromJson(link.value("ref", nlohmann::json::object()));
        auto bit = in->bodies.find(ref.body);
        Resolution r = bit == in->bodies.end() ? Resolution {} : resolve(*bit->second, ref);
        TopoDS_Shape edge;
        if (bit != in->bodies.end() && r.status != ResolveStatus::Missing && r.status != ResolveStatus::Ambiguous) {
            edge = bit->second->edge(r.index);
        }
        auto followed = followLink(st->solved, st->frame, link, edge);
        if (!followed && status.level == StatusLevel::Ok) {
            status.level = StatusLevel::Warning;
            status.message = followed.error().message;
            status.suggestions = followed.error().suggestions;
        }
    }
    st->report = st->solved.solve(doc.lookup());
    if (st->report.state == SolveState::Failed
        || (st->report.state == SolveState::OverConstrained && !st->report.conflicting.empty())) {
        auto o = failed(st->report.message,
                        {"Open the sketch to see the highlighted constraints.",
                         "Undo the last change to the sketch."});
        return o;
    }
    auto prof = buildProfiles(st->solved, st->frame);
    if (!prof) {
        return failed(prof.error());
    }
    st->profiles = std::move(prof.value());
    if (!st->profiles.notes.empty() && status.level == StatusLevel::Ok) {
        status.level = StatusLevel::Warning;
        status.message = st->profiles.notes.front();
    }

    if (st->report.state == SolveState::OverConstrained && status.level == StatusLevel::Ok) {
        status.level = StatusLevel::Warning;
        status.message = st->report.message;
    }
    bool hasCurves = false;
    for (auto& [id, e] : st->solved.entities()) {
        hasCurves = hasCurves || (e.type != EntityType::Point && !e.construction);
    }
    if (hasCurves && st->profiles.regions.empty() && status.level == StatusLevel::Ok) {
        auto gaps = st->solved.openEndpoints();
        status.level = StatusLevel::Warning;
        status.message = "This sketch has no closed area yet, so it can't be extruded.";
        if (!gaps.empty()) {
            double best = 1e300;
            for (auto& g : gaps) {
                if (g.nearestPointId) {
                    best = std::min(best, g.gap);
                }
            }
            status.message += " " + (gaps.size() == 1 ? std::string("1 line end is not connected.")
                                                      : fmt("%1 line ends are not connected.", {std::to_string(gaps.size())}));
            if (best < 1e300) {
                status.suggestions.push_back(fmt("The smallest gap is %1: drag those ends together to close it.", {mm(best)}));
            }
        }
        status.suggestions.push_back("Turn on 'Show gaps' to highlight open ends.");
    }
    if (status.level == StatusLevel::Ok) {
        status.message = st->report.message;
    }

    auto out = copyState(in);
    out->sketches[f.id] = st;
    return {out, status};
}

// ---------------------------------------------------------------- extrude

std::optional<BoolOp> opFromString(const std::string& s)
{
    if (s == "new") {
        return BoolOp::NewBody;
    }
    if (s == "join") {
        return BoolOp::Join;
    }
    if (s == "cut") {
        return BoolOp::Cut;
    }
    if (s == "intersect") {
        return BoolOp::Intersect;
    }
    return std::nullopt;
}

Bnd_Box boxOf(const TopoDS_Shape& s)
{
    Bnd_Box b;
    BRepBndLib::Add(s, b);
    return b;
}

// Chooses the closed areas a feature uses ("all" or a list of region keys).
std::optional<FeatureOutcome> selectProfiles(const SketchState& sk, const nlohmann::json& p, FeatureStatus& status,
                                             const std::string& what, std::vector<const Profile*>& profiles)
{
    const auto& want = p.contains("profiles") ? p["profiles"] : nlohmann::json("all");
    if (want.is_string() && want.get<std::string>() == "all") {
        for (auto& r : sk.profiles.regions) {
            profiles.push_back(&r);
        }
    }
    else {
        for (auto& k : want) {
            const std::string key = k.get<std::string>();
            for (auto& r : sk.profiles.regions) {
                if (r.key == key) {
                    profiles.push_back(&r);
                }
            }
        }
        if (profiles.size() < want.size()) {
            if (want.size() == 1 && sk.profiles.regions.size() == 1) {
                profiles = {&sk.profiles.regions[0]};
                status.level = StatusLevel::Warning;
                status.message = fmt("The sketch's closed area changed shape; the %1 now uses its only closed area. "
                                     "Check the result.", {what});
            }
            else {
                return failed(fmt("Some of the closed areas this %1 used are gone from the sketch.", {what}),
                              {fmt("Edit the %1 and pick the areas again.", {what})});
            }
        }
    }
    if (profiles.empty()) {
        return failed(fmt("The sketch has no closed area to %1.", {what}),
                      {"Close the outline: look for small gaps between line ends.",
                       "Turn on 'Show gaps' in the sketch."});
    }
    return std::nullopt;
}

// Applies a tool shape to the bodies with Join/Cut/Intersect/New body, and records it
// so a Pattern can repeat it.
FeatureOutcome applyTool(const Feature& f, BoolOp op, NamedShape tool, const std::shared_ptr<const ModelState>& in,
                         FeatureStatus status)
{
    const auto& p = f.params;
    auto toolPtr = std::make_shared<const NamedShape>(std::move(tool));
    auto out = copyState(in);
    if (op == BoolOp::NewBody || (in->bodies.empty() && op == BoolOp::Join)) {
        out->bodies[f.id] = toolPtr;
        out->tools[f.id] = {toolPtr, BoolOp::NewBody, f.id};
        return {out, status};
    }
    if (in->bodies.empty()) {
        return failed(op == BoolOp::Cut ? std::string("There is no body to cut yet.") : std::string("There is no body to intersect yet."),
                      {"Use 'New body' for the first shape."});
    }
    std::string target = p.value("target", std::string {});
    if (target.empty()) {
        Bnd_Box tb = boxOf(toolPtr->shape());
        for (auto& [id, b] : in->bodies) {
            if (!boxOf(b->shape()).IsOut(tb)) {
                target = id;
                break;
            }
        }
        if (target.empty()) {
            target = in->bodies.begin()->first;
        }
    }
    auto bit = in->bodies.find(target);
    if (bit == in->bodies.end()) {
        return failed("The body this step should change no longer exists.", {"Edit the step and choose a body."});
    }
    auto res = booleanOp(f.id, op, *bit->second, *toolPtr);
    if (!res) {
        return failed(res.error());
    }
    out->bodies[target] = std::make_shared<NamedShape>(std::move(res.value()));
    out->tools[f.id] = {toolPtr, op, target};
    return {out, status};
}

const SketchState* findSketch(const std::shared_ptr<const ModelState>& in, const std::string& id)
{
    auto it = in->sketches.find(id);
    return it == in->sketches.end() ? nullptr : it->second.get();
}

FeatureOutcome missingSketch(const std::string& what, const std::string& id)
{
    return failed(fmt("The sketch this %1 uses (%2) is missing or has a problem.", {what, id}),
                  {"Fix the sketch first: it is marked in the timeline.", fmt("Edit this %1 and pick another sketch.", {what})});
}

FeatureOutcome applyExtrude(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    const std::string sketchId = p.value("sketch", std::string {});
    const SketchState* skp = findSketch(in, sketchId);
    if (!skp) {
        return missingSketch("extrude", sketchId);
    }
    const SketchState& sk = *skp;
    std::vector<const Profile*> profiles;
    std::vector<Profile> walls;  // open curves: walls built here, extruded as plain areas
    const nlohmann::json prof = p.value("profiles", nlohmann::json("all"));
    const bool openCurves = prof == nlohmann::json("open") || prof == nlohmann::json::array({"open"});
    if (openCurves) {
        auto t = doc.evalLength(p.value("thin", std::string("2 mm")));
        if (!t) {
            return failed(t.error());
        }
        auto w = openChainWalls(sk.solved, sk.frame, t.value());
        if (!w) {
            return failed(w.error());
        }
        walls = std::move(w.value());
        for (auto& wp : walls) {
            profiles.push_back(&wp);
        }
    }
    else if (auto bad = selectProfiles(sk, p, status, "extrude", profiles)) {
        return *bad;
    }
    ExtrudeInput ei;
    ei.featureId = f.id;
    ei.profiles = profiles;
    ei.normal = sk.frame.Direction();
    std::string dir = p.value("direction", std::string("one"));
    ei.direction = dir == "symmetric" ? ExtrudeDirection::Symmetric
                 : dir == "reverse"   ? ExtrudeDirection::Reverse
                 : dir == "two"       ? ExtrudeDirection::TwoSides
                                      : ExtrudeDirection::OneSide;
    if (ei.direction == ExtrudeDirection::TwoSides) {
        auto d2 = doc.evalLength(p.value("distance2", std::string("10 mm")));
        if (!d2) {
            return failed(d2.error());
        }
        ei.distance2 = d2.value();
    }
    if (p.contains("taper")) {
        auto t = doc.evalAngle(p.value("taper", std::string("0 deg")));
        if (!t) {
            return failed(t.error());
        }
        ei.taper = t.value();
    }
    if (p.contains("thin") && !openCurves) {
        auto t = doc.evalLength(p.value("thin", std::string("0 mm")));
        if (!t) {
            return failed(t.error());
        }
        ei.thin = t.value();
        const std::string side = p.value("thin_side", std::string("center"));
        ei.thinSide = side == "inside" ? -1 : side == "outside" ? 1 : 0;
    }
    std::optional<TopoDS_Face> trimAt;  // up to a curved or slanted face: cut off there
    if (p.value("extent", std::string("distance")) == "to") {
        // up to a plane or flat face parallel to the sketch: the distance follows it
        const nlohmann::json toRef = p.value("to", nlohmann::json("XY"));
        const gp_Vec n(sk.frame.Direction());
        std::optional<TopoDS_Face> face;
        if (toRef.is_object() && toRef.contains("face")) {
            ShapeRef ref = ShapeRef::fromJson(toRef["face"]);
            auto bit = in->bodies.find(ref.body);
            Resolution r = bit == in->bodies.end() ? Resolution {} : resolve(*bit->second, ref);
            if (bit == in->bodies.end() || r.status == ResolveStatus::Missing || r.status == ResolveStatus::Ambiguous) {
                return failed("The face this extrude goes up to no longer exists.", {"Edit the extrude and pick a face again."});
            }
            face = TopoDS::Face(bit->second->face(r.index));
        }
        const bool flat = !face || measureItem(*face).normal.has_value();
        auto to = flat ? planeFrame(toRef, in, status) : Result<gp_Ax3>(gp_Ax3());
        if (!to) {
            return failed(to.error());
        }
        if (!flat || !to->Direction().IsParallel(sk.frame.Direction(), 1e-6)) {
            // curved or slanted: extrude past it, then cut off where it meets it
            if (p.contains("to_offset")) {
                auto off = doc.evalLength(p.value("to_offset", std::string("0 mm")));
                if (off && std::abs(off.value()) > 1e-9) {
                    return failed("An offset beyond a curved or slanted face is not supported yet.",
                                  {"Set the offset to 0, or pick a face parallel to the sketch."});
                }
            }
            const TopoDS_Face target = face ? *face : BRepBuilderAPI_MakeFace(gp_Pln(*to)).Face();
            Bnd_Box all;
            for (auto& [id, b] : in->bodies) {
                all.Add(boxOf(b->shape()));
            }
            all.Add(boxOf(profiles.front()->face));
            if (face) {
                all.Add(boxOf(*face));
            }
            const double reach = std::sqrt(all.SquareExtent()) * 2 + 10;
            // which way: toward the face (its centre, or the plane's point nearest the sketch)
            GProp_GProps g;
            BRepGProp::SurfaceProperties(profiles.front()->face, g);
            gp_Pnt aim;
            if (face) {
                GProp_GProps fg;
                BRepGProp::SurfaceProperties(*face, fg);
                aim = fg.CentreOfMass();
            }
            else {
                const gp_Pln pl(*to);
                const gp_Vec v(pl.Location(), g.CentreOfMass());
                aim = g.CentreOfMass().Translated(-gp_Vec(pl.Axis().Direction()) * v.Dot(gp_Vec(pl.Axis().Direction())));
            }
            const double side = gp_Vec(g.CentreOfMass(), aim).Dot(n);
            if (std::abs(side) < 1e-7) {
                return failed("That face is level with the sketch, so there is no way to tell which way to extrude.");
            }
            ei.distance = reach;
            ei.direction = side > 0 ? ExtrudeDirection::OneSide : ExtrudeDirection::Reverse;
            trimAt = target;
        }
    }
    if (p.value("extent", std::string("distance")) == "to" && !trimAt) {
        auto to = planeFrame(p.value("to", nlohmann::json("XY")), in, status);
        if (!to) {
            return failed(to.error());
        }
        const gp_Vec n(sk.frame.Direction());
        double d = gp_Vec(sk.frame.Location(), to->Location()).Dot(n);
        if (p.contains("to_offset")) {
            auto off = doc.evalLength(p.value("to_offset", std::string("0 mm")));
            if (!off) {
                return failed(off.error());
            }
            d += d >= 0 ? off.value() : -off.value();
        }
        if (std::abs(d) < 1e-7) {
            return failed("That face or plane is in the sketch's own plane, so there is nothing to extrude up to.");
        }
        ei.distance = std::abs(d);
        ei.direction = d > 0 ? ExtrudeDirection::OneSide : ExtrudeDirection::Reverse;
    }
    else if (p.value("extent", std::string("distance")) == "all") {
        Bnd_Box all;
        for (auto& [id, b] : in->bodies) {
            all.Add(boxOf(b->shape()));
        }
        double d = all.IsVoid() ? 1000 : std::sqrt(all.SquareExtent()) * 2 + 10;
        // through everything: from far behind the sketch plane to far beyond it
        ei.distance = 2 * d;
        ei.direction = ExtrudeDirection::Symmetric;
    }
    else if (!trimAt) {
        auto d = doc.evalLength(p.value("distance", std::string("10 mm")));
        if (!d) {
            return failed(d.error());
        }
        ei.distance = d.value();
        if (ei.distance < 0) {
            ei.distance = -ei.distance;
            ei.direction = ei.direction == ExtrudeDirection::Reverse ? ExtrudeDirection::OneSide
                                                                     : ExtrudeDirection::Reverse;
        }
    }
    auto op = opFromString(p.value("op", std::string("new")));
    if (!op) {
        return failed("This extrude has an unknown operation.");
    }
    auto tool = makeExtrusion(ei);
    if (tool && trimAt) {
        TopoDS_Compound start;
        BRep_Builder bb;
        bb.MakeCompound(start);
        for (const Profile* pr : profiles) {
            bb.Add(start, pr->face);
        }
        tool = trimToFace(tool.value(), *trimAt, start, f.id, f.id + "/end");
    }
    if (!tool) {
        Error e = tool.error();
        if (e.code == "extrude.taper" && std::abs(ei.taper) > 1e-9) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.3g deg", ei.taper * 90 / std::numbers::pi);  // half the angle
            e.fixes.push_back({std::string("Use ") + buf, "taper", buf});
        }
        if (e.code == "extrude.thin" && ei.thin > 1e-9) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.3g mm", ei.thin / 2);
            e.fixes.push_back({std::string("Use ") + buf, "thin", buf});
        }
        return failed(e);
    }
    return applyTool(f, *op, std::move(tool.value()), in, status);
}

// ---------------------------------------------------------------- press pull

// Pushes or pulls a flat face straight along its normal: out adds material, in removes it.
FeatureOutcome applyPressPull(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    ShapeRef ref = ShapeRef::fromJson(p.value("face", nlohmann::json::object()));
    auto bit = in->bodies.find(ref.body);
    if (bit == in->bodies.end()) {
        return failed("The body of the face to push or pull no longer exists.", {"Edit the step and click a face."});
    }
    Resolution r = resolve(*bit->second, ref);
    if (r.status == ResolveStatus::Missing || r.status == ResolveStatus::Ambiguous) {
        return failed("The face to push or pull no longer exists.", {"Edit the step and click a face."});
    }
    const TopoDS_Face face = TopoDS::Face(bit->second->face(r.index));
    auto d = doc.evalLength(p.value("distance", std::string("5 mm")));
    if (!d) {
        return failed(d.error());
    }
    if (std::abs(d.value()) < 1e-9) {
        return failed("Push or pull by more than zero.");
    }
    if (measureItem(face).kind != "flat face") {
        // a whole round face (a hole's wall, a round boss): its radius changes; out adds material
        BRepAdaptor_Surface sa(face);
        if (sa.GetType() != GeomAbs_Cylinder) {
            return failed("Press pull moves flat faces and whole round ones (a hole, a round boss). To change this "
                          "face, edit the step that made it.");
        }
        double u0, u1, v0, v1;
        BRepTools::UVBounds(face, u0, u1, v0, v1);
        if (u1 - u0 < 2 * std::numbers::pi - 1e-6) {
            return failed("This round face goes only part of the way round, so press pull cannot change its size.",
                          {"Edit the step that made it (a fillet's radius, say)."});
        }
        const gp_Cylinder cyl = sa.Cylinder();
        const double R = cyl.Radius();
        const gp_Ax1 axis = cyl.Axis();
        BRepGProp_Face gf(face);
        gp_Pnt c;
        gp_Vec n;
        gf.Normal((u0 + u1) / 2, (v0 + v1) / 2, c, n);
        gp_Vec radial(axis.Location(), c);
        radial -= gp_Vec(axis.Direction()) * radial.Dot(gp_Vec(axis.Direction()));
        const bool hole = n.Dot(radial) < 0;  // the outside of the material faces the axis
        const double R2 = hole ? R - d.value() : R + d.value();
        if (!(R2 > 1e-6)) {
            return failed(hole ? "That would close the hole up completely." : "That would make the round part disappear.",
                          {"Push or pull by less than the radius."});
        }
        const gp_Dir ad = axis.Direction();
        const gp_Ax2 base(axis.Location().Translated(gp_Vec(ad) * v0), ad);
        const double h = v1 - v0, inner = std::min(R, R2), outer = std::max(R, R2);
        TopoDS_Shape ring = BRepAlgoAPI_Cut(BRepPrimAPI_MakeCylinder(base, outer, h).Shape(),
                                            BRepPrimAPI_MakeCylinder(base, inner, h).Shape())
                                .Shape();
        NamedShape tool(ring);
        int caps = 0;
        for (int i = 0; i < tool.faceCount(); ++i) {
            BRepAdaptor_Surface fs(TopoDS::Face(tool.face(i)));
            if (fs.GetType() == GeomAbs_Cylinder) {
                tool.setFaceName(i, f.id + (std::abs(fs.Cylinder().Radius() - R2) < 1e-9 ? "/wall" : "/back"));
            }
            else {
                tool.setFaceName(i, f.id + "/cap~" + std::to_string(caps++));
            }
        }
        tool.deriveEdgeNames();
        Feature g = f;
        g.params["target"] = ref.body;
        return applyTool(g, d.value() > 0 ? BoolOp::Join : BoolOp::Cut, std::move(tool), in, status);
    }
    // the outward normal at the face's centre (the face's orientation says which side is out)
    BRepGProp_Face gf(face);
    double u0, u1, v0, v1;
    gf.Bounds(u0, u1, v0, v1);
    gp_Pnt c;
    gp_Vec n;
    gf.Normal((u0 + u1) / 2, (v0 + v1) / 2, c, n);
    if (n.Magnitude() < 1e-12) {
        return failed("The direction of this face could not be found.");
    }
    Profile prof;
    prof.key = "face";
    prof.face = face;
    int k = 0;
    for (TopExp_Explorer e(face, TopAbs_EDGE); e.More(); e.Next()) {
        if (!prof.edgeEntity.IsBound(e.Current())) {
            prof.edgeEntity.Bind(e.Current(), ++k);
        }
    }
    ExtrudeInput ei;
    ei.featureId = f.id;
    ei.profiles = {&prof};
    ei.normal = gp_Dir(n);
    ei.distance = std::abs(d.value());
    ei.direction = d.value() > 0 ? ExtrudeDirection::OneSide : ExtrudeDirection::Reverse;
    auto tool = makeExtrusion(ei);
    if (!tool) {
        return failed(tool.error());
    }
    Feature g = f;
    g.params["target"] = ref.body;
    return applyTool(g, d.value() > 0 ? BoolOp::Join : BoolOp::Cut, std::move(tool.value()), in, status);
}

// ---------------------------------------------------------------- combine, split, move

std::vector<std::string> bodyList(const nlohmann::json& v)
{
    std::vector<std::string> out;
    if (v.is_string()) {
        out.push_back(v.get<std::string>());
    }
    else if (v.is_array()) {
        for (auto& x : v) {
            out.push_back(x.get<std::string>());
        }
    }
    return out;
}

FeatureOutcome applyCombine(const Document&, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    const std::string target = p.value("target", std::string {});
    auto tit = in->bodies.find(target);
    if (tit == in->bodies.end()) {
        return failed("The body to combine into no longer exists.", {"Edit the step and choose a body."});
    }
    auto op = opFromString(p.value("op", std::string("join")));
    if (!op || *op == BoolOp::NewBody) {
        return failed("Combine joins, cuts or intersects; choose one of those.");
    }
    const auto tools = bodyList(p.value("tools", nlohmann::json::array()));
    if (tools.empty()) {
        return failed("Pick at least one body to combine with.");
    }
    NamedShape acc = *tit->second;
    auto out = copyState(in);
    for (const std::string& t : tools) {
        auto it = in->bodies.find(t);
        if (it == in->bodies.end() || t == target) {
            return failed("A body this combine uses no longer exists.", {"Edit the step and pick the bodies again."});
        }
        auto r = booleanOp(f.id, *op, acc, *it->second);
        if (!r) {
            return failed(r.error());
        }
        acc = std::move(r.value());
        if (!p.value("keep_tools", false)) {
            out->bodies.erase(t);
        }
    }
    out->bodies[target] = std::make_shared<NamedShape>(std::move(acc));
    return {out, status};
}

FeatureOutcome applySplit(const Document&, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    const std::string bodyId = p.value("body", std::string {});
    auto bit = in->bodies.find(bodyId);
    if (bit == in->bodies.end()) {
        return failed("The body to split no longer exists.", {"Edit the step and choose a body."});
    }
    const nlohmann::json pref = p.value("plane", nlohmann::json("YZ"));
    if (pref.is_object() && pref.contains("face")) {
        // a curved face cuts the body where it reaches through it
        ShapeRef ref = ShapeRef::fromJson(pref["face"]);
        auto fb = in->bodies.find(ref.body);
        Resolution r = fb == in->bodies.end() ? Resolution {} : resolve(*fb->second, ref);
        if (fb == in->bodies.end() || r.status == ResolveStatus::Missing || r.status == ResolveStatus::Ambiguous) {
            return failed("The face to split with no longer exists.", {"Edit the step and click a face again."});
        }
        const TopoDS_Face face = TopoDS::Face(fb->second->face(r.index));
        if (!measureItem(face).normal) {
            auto pieces = splitByFace(*bit->second, face, f.id);
            if (!pieces) {
                return failed(pieces.error());
            }
            auto out = copyState(in);
            out->bodies[bodyId] = std::make_shared<NamedShape>(std::move((*pieces)[0]));  // the biggest keeps the name
            for (size_t k = 1; k < pieces->size(); ++k) {
                out->bodies[k == 1 ? f.id : f.id + "." + std::to_string(k)] = std::make_shared<NamedShape>(std::move((*pieces)[k]));
            }
            return {out, status};
        }
    }
    auto fr = planeFrame(pref, in, status);
    if (!fr) {
        return failed(fr.error());
    }
    // Two big boxes, one on each side of the plane; each piece is the body minus the
    // other side. The new faces are named <split id>/split.
    Bnd_Box bb = boxOf(bit->second->shape());
    const double L = std::sqrt(bb.SquareExtent()) * 2 + gp_Vec(gp_Pnt(0, 0, 0), fr->Location()).Magnitude() + 10;
    auto halfBox = [&](double side) {
        const gp_Ax3& a = fr.value();
        gp_Pnt corner(a.Location().XYZ() - a.XDirection().XYZ() * L - a.YDirection().XYZ() * L
                      + (side < 0 ? a.Direction().XYZ() * (-L) : gp_XYZ(0, 0, 0)));
        TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Ax2(corner, a.Direction(), a.XDirection()), 2 * L, 2 * L, L).Shape();
        NamedShape ns(box);
        for (int i = 0; i < ns.faceCount(); ++i) {
            ns.setFaceName(i, f.id + "/split");
        }
        ns.deriveEdgeNames();
        return ns;
    };
    auto above = booleanOp(f.id, BoolOp::Cut, *bit->second, halfBox(-1));
    auto below = booleanOp(f.id, BoolOp::Cut, *bit->second, halfBox(+1));
    if (!above || !below) {
        return failed("The split plane does not cross this body, so there is nothing to split.",
                      {"Pick a plane that passes through the body.", "Use an offset plane to move it."});
    }
    auto out = copyState(in);
    out->bodies[bodyId] = std::make_shared<NamedShape>(std::move(above.value()));
    out->bodies[f.id] = std::make_shared<NamedShape>(std::move(below.value()));
    return {out, status};
}

FeatureOutcome applyMove(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    std::vector<std::string> bodies = bodyList(p.value("bodies", nlohmann::json::array()));
    if (bodies.empty()) {
        return failed("Pick at least one body to move.");
    }
    double d[3] = {0, 0, 0};
    const char* keys[3] = {"dx", "dy", "dz"};
    for (int k = 0; k < 3; ++k) {
        auto v = doc.evalLength(p.value(keys[k], std::string("0 mm")));
        if (!v) {
            return failed(v.error());
        }
        d[k] = v.value();
    }
    auto ang = doc.evalAngle(p.value("angle", std::string("0 deg")));
    if (!ang) {
        return failed(ang.error());
    }
    const std::string axis = p.value("axis", std::string("Z"));
    auto turn = namedAxis(axis, in);
    if (!turn) {
        return missingAxis(axis);
    }
    gp_Trsf rot, move;
    rot.SetRotation(*turn, ang.value());
    gp_Vec shift(d[0], d[1], d[2]);
    if (p.contains("from") || p.contains("to")) {
        // point to point: the (turned) "from" point lands on "to", plus any typed shift
        const std::string n1 = p.value("from", std::string("O")), n2 = p.value("to", std::string("O"));
        auto a = namedPoint(n1, in), b = namedPoint(n2, in);
        if (!a || !b) {
            return failed(fmt("The point '%1' does not exist (or comes later in the timeline).", {!a ? n1 : n2}),
                          {"Pick the origin (O) or a construction point made earlier."});
        }
        shift += gp_Vec(a->Transformed(rot), *b);
    }
    move.SetTranslation(shift);
    const gp_Trsf t = move * rot;  // turn about the origin axis, then shift
    const bool copy = p.value("copy", false);
    auto out = copyState(in);
    int k = 0;
    for (const std::string& b : bodies) {
        auto it = in->bodies.find(b);
        if (it == in->bodies.end()) {
            return failed("A body this step moves no longer exists.", {"Edit the step and pick the bodies again."});
        }
        if (copy) {
            const std::string id = bodies.size() == 1 ? f.id : f.id + "." + std::to_string(++k);
            out->bodies[id] = std::make_shared<NamedShape>(transformedCopy(*it->second, t, f.id + "/copy:"));
        }
        else {
            out->bodies[b] = std::make_shared<NamedShape>(transformedCopy(*it->second, t, ""));
        }
    }
    return {out, status};
}

// ---------------------------------------------------------------- revolve

FeatureOutcome applyRevolve(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    const std::string sketchId = p.value("sketch", std::string {});
    const SketchState* skp = findSketch(in, sketchId);
    if (!skp) {
        return missingSketch("revolve", sketchId);
    }
    const SketchState& sk = *skp;
    // Axis: a line of the sketch (usually a construction line), or an origin axis.
    gp_Ax1 axis;
    const nlohmann::json ax = p.value("axis", nlohmann::json::object());
    if (ax.contains("line")) {
        const auto* e = sk.solved.entity(ax["line"].get<int>());
        if (!e || e->type != EntityType::Line) {
            return failed("The axis line of this revolve is gone from the sketch.",
                          {"Edit the revolve and pick an axis again."});
        }
        auto to3d = [&](int pid) {
            auto [x, y] = sk.solved.pointXY(pid);
            return gp_Pnt(sk.frame.Location().XYZ() + sk.frame.XDirection().XYZ() * x + sk.frame.YDirection().XYZ() * y);
        };
        const gp_Pnt a = to3d(e->p1), b = to3d(e->p2);
        if (a.Distance(b) < 1e-9) {
            return failed("The axis line has no length.");
        }
        axis = gp_Ax1(a, gp_Dir(gp_Vec(a, b)));
    }
    else {
        const std::string name = ax.contains("axis") ? ax.value("axis", std::string {}) : ax.value("origin", std::string("Z"));
        auto na = namedAxis(name, in);
        if (!na) {
            return missingAxis(name);
        }
        axis = *na;
    }
    // profiles: the axis line itself never forms a region (it is usually construction)
    std::vector<const Profile*> profiles;
    if (auto bad = selectProfiles(sk, p, status, "revolve", profiles)) {
        return *bad;
    }
    auto angle = doc.evalAngle(p.value("angle", std::string("360 deg")));
    if (!angle) {
        return failed(angle.error());
    }
    double sweep = angle.value();
    std::optional<TopoDS_Face> trimAt;  // a curved face (or a plane the axis is not in): cut off there
    if (p.value("extent", std::string("angle")) == "to") {
        const nlohmann::json toRef = p.value("to", nlohmann::json("XY"));
        std::optional<TopoDS_Face> face;
        if (toRef.is_object() && toRef.contains("face")) {
            ShapeRef ref = ShapeRef::fromJson(toRef["face"]);
            auto bit = in->bodies.find(ref.body);
            Resolution r = bit == in->bodies.end() ? Resolution {} : resolve(*bit->second, ref);
            if (bit == in->bodies.end() || r.status == ResolveStatus::Missing || r.status == ResolveStatus::Ambiguous) {
                return failed("The face this revolve turns up to no longer exists.", {"Edit the revolve and pick a face again."});
            }
            face = TopoDS::Face(bit->second->face(r.index));
        }
        if (face && !measureItem(*face).normal) {
            trimAt = face;
            sweep = 2 * std::numbers::pi * 0.999;  // almost a full turn, then cut off at the face
        }
    }
    if (p.value("extent", std::string("angle")) == "to" && !trimAt) {
        // up to a plane or flat face the axis lies in: turn (the positive way) until the
        // profile reaches it
        auto to = planeFrame(p.value("to", nlohmann::json("XY")), in, status);
        if (!to) {
            return failed(to.error());
        }
        const gp_Vec d(axis.Direction()), n(to->Direction());
        const double off = std::abs(gp_Vec(to->Location(), axis.Location()).Dot(n));
        if (std::abs(d.Dot(n)) > 1e-6 || off > 1e-6) {
            return failed("Revolve can only turn up to a plane or flat face that the axis lies in.",
                          {"Pick a face or plane through the axis, or type an angle."});
        }
        GProp_GProps props;
        BRepGProp::SurfaceProperties(profiles.front()->face, props);
        gp_Vec r0(axis.Location(), props.CentreOfMass());
        r0 -= d * r0.Dot(d);
        if (r0.Magnitude() < 1e-7) {
            return failed("The profile sits on the axis, so there is no way to tell where it starts turning.");
        }
        const gp_Vec t = d.Crossed(n);
        double best = 0;
        for (const gp_Vec& v : {t, -t}) {
            double a = std::atan2(r0.Crossed(v).Dot(d), r0.Dot(v));
            if (a <= 1e-7) {
                a += 2 * std::numbers::pi;
            }
            if (best == 0 || a < best) {
                best = a;
            }
        }
        sweep = best;
    }
    auto op = opFromString(p.value("op", std::string("new")));
    if (!op) {
        return failed("This revolve has an unknown operation.");
    }
    auto tool = makeRevolution(f.id, profiles, axis, sweep);
    if (tool && trimAt) {
        TopoDS_Compound start;
        BRep_Builder bb;
        bb.MakeCompound(start);
        for (const Profile* pr : profiles) {
            bb.Add(start, pr->face);
        }
        tool = trimToFace(tool.value(), *trimAt, start, f.id, f.id + "/end");
    }
    if (!tool) {
        return failed(tool.error());
    }
    return applyTool(f, *op, std::move(tool.value()), in, status);
}

// ---------------------------------------------------------------- hole

FeatureOutcome applyHole(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    const std::string sketchId = p.value("sketch", std::string {});
    const SketchState* skp = findSketch(in, sketchId);
    if (!skp) {
        return missingSketch("hole", sketchId);
    }
    const SketchState& sk = *skp;
    // Hole centres: sketch points that are not the end or centre of a curve, or the
    // points listed in "points".
    std::set<int> used;
    for (auto& [id, e] : sk.solved.entities()) {
        if (e.type != EntityType::Point) {
            used.insert({e.p1, e.p2, e.center});
        }
    }
    for (auto& [id, t] : sk.solved.texts()) {
        used.insert(t.anchor);  // a text's anchor is not a hole centre
    }
    for (auto& [id, a] : sk.solved.artworks()) {
        used.insert(a.anchor);
    }
    std::set<int> wanted;
    if (p.contains("points") && p["points"].is_array()) {
        for (auto& v : p["points"]) {
            wanted.insert(v.get<int>());
        }
    }
    std::vector<HolePoint> pts;
    for (auto& [id, e] : sk.solved.entities()) {
        if (e.type != EntityType::Point || e.construction) {
            continue;
        }
        if (wanted.empty() ? (!used.count(id) && !e.internal) : wanted.count(id) > 0) {
            pts.push_back({id, e.x, e.y});
        }
    }
    if (pts.empty()) {
        return failed("The hole sketch has no points to put holes at.",
                      {"Add a Point in the sketch where each hole should go."});
    }
    HoleSpec spec;
    const std::string type = p.value("type", std::string("simple"));
    spec.type = type == "counterbore" ? HoleSpec::Type::Counterbore
              : type == "countersink" ? HoleSpec::Type::Countersink
                                      : HoleSpec::Type::Simple;
    // A screw size from the clearance table gives the defaults; typed sizes still win.
    std::optional<ScrewSize> screw;
    std::string sizeDefault = "3.4 mm", cbD = "6.5 mm", cbH = "3.5 mm", csD = "6.5 mm", csA = "90 deg";
    if (p.contains("size")) {
        const std::string size = p.value("size", std::string {});
        auto fit = holeFitFromString(p.value("fit", std::string("normal")));
        if (!fit) {
            return failed("The hole fit must be close, normal or loose.");
        }
        auto cd = clearanceDiameter(size, *fit);
        if (!cd) {
            return failed(cd.error());
        }
        screw = screwSize(size);
        auto mm = [](double v) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.6g mm", v);
            return std::string(buf);
        };
        sizeDefault = mm(cd.value());
        cbD = mm(screw->cboreDiameter);
        cbH = mm(screw->cboreDepth);
        csD = mm(screw->csinkDiameter);
        csA = std::to_string(int(screw->csinkAngleDeg)) + " deg";
    }
    auto len = [&](const char* key, const std::string& def) { return doc.evalLength(p.value(key, def)); };
    auto d = len("diameter", sizeDefault);
    if (!d) {
        return failed(d.error());
    }
    spec.diameter = d.value();
    spec.through = p.value("extent", std::string("distance")) == "all";
    if (!spec.through) {
        auto dp = len("depth", "10 mm");
        if (!dp) {
            return failed(dp.error());
        }
        spec.depth = dp.value();
    }
    if (spec.type == HoleSpec::Type::Counterbore) {
        auto cd = len("cbore_diameter", cbD), ch = len("cbore_depth", cbH);
        if (!cd || !ch) {
            return failed(!cd ? cd.error() : ch.error());
        }
        spec.cboreDiameter = cd.value();
        spec.cboreDepth = ch.value();
    }
    if (spec.type == HoleSpec::Type::Countersink) {
        auto cd = len("csink_diameter", csD);
        auto ca = doc.evalAngle(p.value("csink_angle", csA));
        if (!cd || !ca) {
            return failed(!cd ? cd.error() : ca.error());
        }
        spec.csinkDiameter = cd.value();
        spec.csinkAngle = ca.value();
    }
    // a drill point (e.g. 118°) at the bottom of a blind hole; flat when absent
    if (!spec.through && p.contains("tip") && p.value("tip", std::string {}) != "flat") {
        auto ta = doc.evalAngle(p.value("tip", std::string {}));
        if (!ta) {
            return failed(ta.error());
        }
        spec.tipAngle = ta.value();
    }
    Bnd_Box all;
    for (auto& [id, b] : in->bodies) {
        all.Add(boxOf(b->shape()));
    }
    const double through = all.IsVoid() ? 1000 : std::sqrt(all.SquareExtent()) * 2 + 10;
    auto tool = makeHoleTool(f.id, sk.frame, pts, spec, through);
    if (!tool) {
        return failed(tool.error());
    }
    return applyTool(f, BoolOp::Cut, std::move(tool.value()), in, status);
}

// ---------------------------------------------------------------- pattern

FeatureOutcome applyPattern(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    // what is repeated: the shape a step added or removed, or a whole body
    const std::string bodyId = p.value("body", std::string {});
    ToolRecord rec;
    if (!bodyId.empty()) {
        auto bit = in->bodies.find(bodyId);
        if (bit == in->bodies.end()) {
            return failed(fmt("The body this pattern repeats (%1) no longer exists.", {bodyId}),
                          {"Edit the pattern and pick a body."});
        }
        rec.tool = bit->second;
        rec.op = p.value("op", std::string("new")) == "join" ? BoolOp::Join : BoolOp::NewBody;
        rec.target = bodyId;
    }
    else {
        const std::string src = p.value("feature", std::string {});
        auto rit = in->tools.find(src);
        if (rit == in->tools.end()) {
            return failed(fmt("The step this pattern repeats (%1) is missing, after the pattern, or has a problem.", {src}),
                          {"Pattern an Extrude, Revolve or Hole that comes earlier in the timeline, or a body."});
        }
        rec = rit->second;
    }
    const std::string kind = p.value("kind", std::string("circular"));
    auto dirOf = [&](const std::string& n) {
        auto na = namedAxis(n, in);  // an origin axis or a construction axis's direction
        return na ? na->Direction() : gp_Dir(1, 0, 0);
    };
    std::vector<gp_Trsf> moves;
    if (kind == "path") {
        // along a chain of sketch curves: spread evenly, or at a set spacing; the copies keep
        // their direction, or turn with the path ("follow")
        const nlohmann::json pj = p.value("path", nlohmann::json::object());
        const std::string sid = pj.value("sketch", std::string {});
        const SketchState* sk = findSketch(in, sid);
        if (!sk) {
            return failed(fmt("The sketch with this pattern's path (%1) is missing or comes later.", {sid}),
                          {"Edit the pattern and pick the sketch with the path."});
        }
        std::vector<int> curves;
        const nlohmann::json cj = pj.value("curves", nlohmann::json("all"));
        if (cj.is_array()) {
            for (auto& c : cj) {
                curves.push_back(c.get<int>());
            }
        }
        else {
            for (auto& [id, e] : sk->solved.entities()) {
                if (e.type != EntityType::Point) {
                    curves.push_back(id);
                }
            }
        }
        auto path = sketchPath(sk->solved, sk->frame, curves);
        if (!path) {
            return failed(path.error());
        }
        auto n = evaluate(p.value("count", std::string("5")), doc.lookup());
        if (!n || !(n->dim == Dim::none()) || n->value < 2 || n->value > 1000) {
            return failed("The number of copies must be a whole number from 2 to 1000.");
        }
        const int cnt = int(std::lround(n->value));
        const double L = path->length;
        double step = path->closed ? L / cnt : L / (cnt - 1);
        if (p.contains("spacing")) {
            auto sp = doc.evalLength(p.value("spacing", std::string("10 mm")));
            if (!sp) {
                return failed(sp.error());
            }
            if (!(sp.value() > 0)) {
                return failed("The spacing must be larger than zero.");
            }
            step = sp.value();
            if (!path->closed && step * (cnt - 1) > L + 1e-7) {
                return failed(fmt("%1 copies %2 apart do not fit on a path %3 long.",
                                  {std::to_string(cnt), formatQuantity(Quantity {step, Dim::len()}), formatQuantity(Quantity {L, Dim::len()})}),
                              {"Use fewer copies, a smaller spacing, or spread them over the whole path."});
            }
        }
        // the path runs from the end nearer the shape being repeated
        Bnd_Box sb = boxOf(rec.tool->shape());
        double x0, y0, z0, x1, y1, z1;
        sb.Get(x0, y0, z0, x1, y1, z1);
        const gp_Pnt mid((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
        gp_Pnt a, b;
        gp_Vec ta, tb;
        pathAt(*path, 0, a, ta);
        pathAt(*path, L, b, tb);
        const bool backwards = !path->closed && b.Distance(mid) < a.Distance(mid) - 1e-9;
        auto at = [&](double sAlong, gp_Pnt& pt, gp_Vec& t) {
            pathAt(*path, backwards ? L - sAlong : sAlong, pt, t);
            if (backwards) {
                t.Reverse();
            }
        };
        const bool follow = p.value("orient", std::string("same")) == "follow";
        gp_Pnt p0;
        gp_Vec t0;
        at(0, p0, t0);
        const gp_Dir normal = sk->frame.Direction();
        for (int k = 1; k < cnt; ++k) {
            gp_Pnt pk;
            gp_Vec tk;
            at(step * k, pk, tk);
            gp_Trsf move;
            move.SetTranslation(gp_Vec(p0, pk));
            if (follow && t0.Magnitude() > 1e-12 && tk.Magnitude() > 1e-12) {
                const double ang = std::atan2(t0.Crossed(tk).Dot(gp_Vec(normal)), t0.Dot(tk));
                gp_Trsf turn;
                turn.SetRotation(gp_Ax1(pk, normal), ang);
                move = turn * move;
            }
            moves.push_back(move);
        }
    }
    else if (kind == "circular") {
        auto n = evaluate(p.value("count", std::string("6")), doc.lookup());
        if (!n || !(n->dim == Dim::none()) || n->value < 2 || n->value > 1000) {
            return failed("The number of copies must be a whole number from 2 to 1000.");
        }
        auto ang = doc.evalAngle(p.value("angle", std::string("360 deg")));
        if (!ang) {
            return failed(ang.error());
        }
        const int cnt = int(std::lround(n->value));
        const bool full = std::abs(ang.value() - 2 * std::numbers::pi) < 1e-9;
        const double step = full ? ang.value() / cnt : ang.value() / (cnt - 1);
        const std::string axisName = p.value("axis", std::string("Z"));
        auto na = namedAxis(axisName, in);
        if (!na) {
            return missingAxis(axisName);
        }
        const gp_Ax1 axis = *na;
        for (int k = 1; k < cnt; ++k) {
            gp_Trsf t;
            t.SetRotation(axis, step * k);
            moves.push_back(t);
        }
    }
    else {
        auto count = [&](const char* key) -> std::optional<int> {
            auto n = evaluate(p.value(key, std::string("1")), doc.lookup());
            if (!n || !(n->dim == Dim::none()) || n->value < 1 || n->value > 1000) {
                return std::nullopt;
            }
            return int(std::lround(n->value));
        };
        auto n1 = count("count1"), n2 = count("count2");
        auto s1 = doc.evalLength(p.value("spacing1", std::string("10 mm")));
        auto s2 = doc.evalLength(p.value("spacing2", std::string("10 mm")));
        if (!n1 || !n2) {
            return failed("The number of copies in each direction must be a whole number from 1 to 1000.");
        }
        if (!s1 || !s2) {
            return failed(!s1 ? s1.error() : s2.error());
        }
        for (const char* key : {"dir1", "dir2"}) {
            const std::string n = p.value(key, std::string(key[3] == '1' ? "X" : "Y"));
            if (!namedAxis(n, in)) {
                return missingAxis(n);
            }
        }
        const gp_Dir d1 = dirOf(p.value("dir1", std::string("X"))), d2 = dirOf(p.value("dir2", std::string("Y")));
        for (int i = 0; i < *n1; ++i) {
            for (int j = 0; j < *n2; ++j) {
                if (i == 0 && j == 0) {
                    continue;
                }
                gp_Trsf t;
                t.SetTranslation(gp_Vec(d1) * (s1.value() * i) + gp_Vec(d2) * (s2.value() * j));
                moves.push_back(t);
            }
        }
    }
    if (moves.empty()) {
        return failed("This pattern makes no copies.", {"Use a count of 2 or more."});
    }
    std::vector<NamedShape> copies;
    for (size_t k = 0; k < moves.size(); ++k) {
        copies.push_back(transformedCopy(*rec.tool, moves[k], f.id + "/" + std::to_string(k + 1) + ":"));
    }
    NamedShape all = namedCompound(copies);
    if (rec.op == BoolOp::NewBody) {
        auto out = copyState(in);
        for (size_t k = 0; k < copies.size(); ++k) {
            out->bodies[f.id + "." + std::to_string(k + 1)] = std::make_shared<NamedShape>(copies[k]);
        }
        return {out, status};
    }
    auto bit = in->bodies.find(rec.target);
    if (bit == in->bodies.end()) {
        return failed("The body the pattern should change no longer exists.");
    }
    if (!bodyId.empty()) {
        // copies of a body joined one by one: each needs to touch the ones before it, not
        // necessarily the original
        NamedShape acc = *bit->second;
        for (auto& c : copies) {
            auto step = booleanOp(f.id, BoolOp::Join, acc, c);
            if (!step) {
                Error e = step.error();
                if (e.code == "boolean.not_touching") {
                    e.message = "Some copies do not touch the others, so they cannot be joined into one body.";
                    e.suggestions = {"Make them new bodies instead.", "Use a smaller spacing."};
                }
                return failed(e);
            }
            acc = std::move(step.value());
        }
        auto out = copyState(in);
        out->bodies[rec.target] = std::make_shared<NamedShape>(std::move(acc));
        return {out, status};
    }
    auto res = booleanOp(f.id, rec.op, *bit->second, all);
    if (!res) {
        Error e = res.error();
        if (e.code == "boolean.not_touching") {
            e.message = "Some copies do not touch the body, so they cannot be joined.";
            e.suggestions = {"Use fewer copies or a smaller spacing.", "Check the pattern axis or directions."};
        }
        return failed(e);
    }
    auto out = copyState(in);
    out->bodies[rec.target] = std::make_shared<NamedShape>(std::move(res.value()));
    return {out, status};
}

// ---------------------------------------------------------------- mirror

FeatureOutcome applyMirror(const Document&, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    // the mirror plane: an origin plane, a construction plane or a flat face
    auto frame = planeFrame(p.value("plane", nlohmann::json("YZ")), in, status);
    if (!frame) {
        return failed(frame.error());
    }
    gp_Trsf t;
    t.SetMirror(gp_Ax2(frame->Location(), frame->Direction()));
    auto out = copyState(in);
    if (p.contains("feature")) {
        // mirror what a step added or removed, and do the same with the copy
        const std::string src = p.value("feature", std::string {});
        auto rit = in->tools.find(src);
        if (rit == in->tools.end()) {
            return failed(fmt("The step this mirror copies (%1) is missing, after the mirror, or has a problem.", {src}),
                          {"Mirror an Extrude, Revolve or Hole that comes earlier in the timeline."});
        }
        const ToolRecord& rec = rit->second;
        NamedShape copy = transformedCopy(*rec.tool, t, f.id + "/mirror:");
        if (rec.op == BoolOp::NewBody) {
            out->bodies[f.id] = std::make_shared<NamedShape>(std::move(copy));
            return {out, status};
        }
        auto bit = in->bodies.find(rec.target);
        if (bit == in->bodies.end()) {
            return failed("The body the mirrored step should change no longer exists.");
        }
        auto res = booleanOp(f.id, rec.op, *bit->second, copy);
        if (!res) {
            Error e = res.error();
            if (e.code == "boolean.not_touching") {
                e.message = "The mirrored copy does not touch the body, so it cannot be joined.";
                e.suggestions = {"Check the mirror plane."};
            }
            return failed(e);
        }
        out->bodies[rec.target] = std::make_shared<NamedShape>(std::move(res.value()));
        return {out, status};
    }
    std::string bodyId = p.value("body", std::string {});
    if (bodyId.empty() && in->bodies.size() == 1) {
        bodyId = in->bodies.begin()->first;
    }
    auto bit = in->bodies.find(bodyId);
    if (bit == in->bodies.end()) {
        return failed("There is no body to mirror.", {"Edit the mirror and choose a body."});
    }
    NamedShape copy = transformedCopy(*bit->second, t, f.id + "/mirror:");
    if (p.value("op", std::string("join")) == "new") {
        out->bodies[f.id] = std::make_shared<NamedShape>(std::move(copy));
        return {out, status};
    }
    auto res = booleanOp(f.id, BoolOp::Join, *bit->second, copy);
    if (!res) {
        Error e = res.error();
        if (e.code == "boolean.not_touching") {
            e.message = "The mirrored copy does not touch the original, so they cannot be joined.";
            e.suggestions = {"Mirror as a new body instead.", "Model the half so it meets the mirror plane."};
        }
        return failed(e);
    }
    out->bodies[bodyId] = std::make_shared<NamedShape>(std::move(res.value()));
    return {out, status};
}

// ---------------------------------------------------------------- fillet

FeatureOutcome applyFillet(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in,
                           bool chamfer = false)
{
    const auto& p = f.params;
    FeatureStatus status;
    auto r = doc.evalLength(p.value(chamfer ? "distance" : "radius", std::string("1 mm")));
    if (!r) {
        return failed(r.error());
    }
    std::string bodyId;
    std::vector<int> edges;
    int missing = 0, ambiguous = 0, rebound = 0;
    std::string unresolved;  // technical detail for "more info" and bug reports
    for (auto& jr : p.value("edges", nlohmann::json::array())) {
        ShapeRef ref = ShapeRef::fromJson(jr);
        if (bodyId.empty()) {
            bodyId = ref.body;
        }
        if (ref.body != bodyId) {
            return failed("A fillet can round edges of one body at a time.",
                          {"Split this into one fillet per body."});
        }
        auto bit = in->bodies.find(ref.body);
        if (bit == in->bodies.end()) {
            ++missing;
            continue;
        }
        Resolution res = resolve(*bit->second, ref);
        switch (res.status) {
            case ResolveStatus::Exact:
                edges.push_back(res.index);
                break;
            case ResolveStatus::Rebound:
                ++rebound;
                edges.push_back(res.index);
                break;
            case ResolveStatus::Ambiguous:
                ++ambiguous;
                unresolved += "ambiguous: " + ref.name + "; ";
                break;
            case ResolveStatus::Missing:
                ++missing;
                unresolved += "missing: " + ref.name + "; ";
                break;
        }
    }
    if (missing || ambiguous) {
        std::string msg;
        if (missing) {
            msg += fmt(chamfer ? "%1 of the edges this chamfer bevelled no longer exist." : "%1 of the edges this fillet rounded no longer exist.",
                       {std::to_string(missing)}) + " ";
        }
        if (ambiguous) {
            msg += ambiguous == 1 ? std::string("1 edge was split, and Jigsmith can't tell which piece you meant.")
                                  : fmt("%1 edges were split, and Jigsmith can't tell which piece you meant.", {std::to_string(ambiguous)});
        }
        return failed(msg,
                      {chamfer ? "Edit the chamfer and pick the edges again." : "Edit the fillet and pick the edges again.",
                       "Or roll back to the step that changed them."},
                      unresolved);
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    if (edges.empty()) {
        return failed(chamfer ? "This chamfer has no edges." : "This fillet has no edges.",
                      {chamfer ? "Edit the chamfer and pick edges to bevel." : "Edit the fillet and pick edges to round."});
    }
    EdgeOptions opt;
    opt.flip = p.value("flip", false);
    const std::string type = p.value("type", std::string("equal"));
    if (chamfer && type == "two") {
        opt.mode = EdgeOptions::Mode::TwoDistances;
        auto d2 = doc.evalLength(p.value("distance2", std::string("1 mm")));
        if (!d2) {
            return failed(d2.error());
        }
        opt.second = d2.value();
    }
    else if (chamfer && type == "angle") {
        opt.mode = EdgeOptions::Mode::DistanceAngle;
        auto a = doc.evalAngle(p.value("angle", std::string("45 deg")));
        if (!a) {
            return failed(a.error());
        }
        opt.second = a.value();
    }
    else if (!chamfer && type == "variable") {
        opt.mode = EdgeOptions::Mode::Variable;
        auto r2 = doc.evalLength(p.value("radius2", std::string("1 mm")));
        if (!r2) {
            return failed(r2.error());
        }
        opt.second = r2.value();
    }
    auto res = chamfer ? chamferEdges(f.id, *in->bodies.at(bodyId), edges, r.value(), opt)
                       : filletEdges(f.id, *in->bodies.at(bodyId), edges, r.value(), opt);
    if (!res) {
        return failed(res.error(), chamfer ? "distance" : "radius");
    }
    if (rebound) {
        status.level = StatusLevel::Warning;
        status.message = rebound == 1 ? std::string("1 edge was re-attached by shape similarity after an earlier change. Check the result.")
                                      : fmt("%1 edges were re-attached by shape similarity after an earlier change. Check the result.",
                                            {std::to_string(rebound)});
    }
    auto out = copyState(in);
    out->bodies[bodyId] = std::make_shared<NamedShape>(std::move(res.value()));
    return {out, status};
}

// ---------------------------------------------------------------- shell

FeatureOutcome applyShell(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    auto t = doc.evalLength(p.value("thickness", std::string("2 mm")));
    if (!t) {
        return failed(t.error());
    }
    std::string bodyId = p.value("body", std::string {});
    std::vector<int> faces;
    int missing = 0, ambiguous = 0, rebound = 0;
    for (auto& jr : p.value("faces", nlohmann::json::array())) {
        ShapeRef ref = ShapeRef::fromJson(jr);
        if (bodyId.empty()) {
            bodyId = ref.body;
        }
        auto bit = in->bodies.find(ref.body);
        if (bit == in->bodies.end()) {
            ++missing;
            continue;
        }
        Resolution res = resolve(*bit->second, ref);
        if (res.status == ResolveStatus::Exact || res.status == ResolveStatus::Rebound) {
            faces.push_back(res.index);
            rebound += res.status == ResolveStatus::Rebound;
        }
        else if (res.status == ResolveStatus::Ambiguous) {
            ++ambiguous;
        }
        else {
            ++missing;
        }
    }
    if (missing || ambiguous) {
        return failed("Some of the faces this shell left open no longer exist or were split.",
                      {"Edit the shell and pick the open faces again."});
    }
    if (bodyId.empty() && in->bodies.size() == 1) {
        bodyId = in->bodies.begin()->first;
    }
    auto bit = in->bodies.find(bodyId);
    if (bit == in->bodies.end()) {
        return failed("There is no body to hollow out.", {"Create a solid first, then shell it."});
    }
    auto res = shellSolid(f.id, *bit->second, faces, t.value(), p.value("direction", std::string("inside")) == "outside");
    if (!res) {
        return failed(res.error(), "thickness");
    }
    if (rebound) {
        status.level = StatusLevel::Warning;
        status.message = "An open face was re-attached by shape similarity after an earlier change. Check it.";
    }
    auto out = copyState(in);
    out->bodies[bodyId] = std::make_shared<NamedShape>(std::move(res.value()));
    return {out, status};
}

}  // namespace

FeatureOutcome applyFeature(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    try {
        switch (f.kind) {
            case FeatureKind::Sketch:
                return applySketch(doc, f, in);
            case FeatureKind::Extrude:
                return applyExtrude(doc, f, in);
            case FeatureKind::Fillet:
                return applyFillet(doc, f, in);
            case FeatureKind::Chamfer:
                return applyFillet(doc, f, in, true);
            case FeatureKind::Shell:
                return applyShell(doc, f, in);
            case FeatureKind::Revolve:
                return applyRevolve(doc, f, in);
            case FeatureKind::Hole:
                return applyHole(doc, f, in);
            case FeatureKind::Pattern:
                return applyPattern(doc, f, in);
            case FeatureKind::Mirror:
                return applyMirror(doc, f, in);
            case FeatureKind::Plane:
                return applyPlane(doc, f, in);
            case FeatureKind::Combine:
                return applyCombine(doc, f, in);
            case FeatureKind::Split:
                return applySplit(doc, f, in);
            case FeatureKind::Move:
                return applyMove(doc, f, in);
            case FeatureKind::Axis:
                return applyAxis(doc, f, in);
            case FeatureKind::PressPull:
                return applyPressPull(doc, f, in);
            case FeatureKind::Point:
                return applyPoint(doc, f, in);
            case FeatureKind::Import:
                return applyImport(f, in);
            case FeatureKind::Remove:
                return applyRemove(f, in);
        }
    }
    catch (const std::exception& ex) {
        return failed("Something went wrong in this step, so it was skipped.",
                      {"Undo the last change.", "Please report this as a bug."}, ex.what());
    }
    catch (...) {
        return failed("Something went wrong in this step, so it was skipped.",
                      {"Undo the last change.", "Please report this as a bug."});
    }
    return failed("Unknown feature type.");
}

}  // namespace jig
