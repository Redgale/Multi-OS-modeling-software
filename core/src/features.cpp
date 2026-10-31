// SPDX-License-Identifier: GPL-3.0-or-later
// Feature recompute. Each function reads a feature's plain-data parameters and
// the geometry state before it, and returns the state after it plus a status.
// A failing feature returns no state: the document keeps the previous geometry
// (clean rollback) and marks the step in the timeline.
#include <algorithm>
#include <cmath>
#include <sstream>

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include "jig/document.hpp"

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
    return failed(e.message, e.suggestions, e.detail);
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
            status.message += " " + std::to_string(gaps.size()) + " line end"
                            + (gaps.size() == 1 ? " is" : "s are") + " not connected.";
            if (best < 1e300) {
                status.suggestions.push_back("The smallest gap is " + mm(best)
                                             + ": drag those ends together to close it.");
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

FeatureOutcome applyExtrude(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    std::string sketchId = p.value("sketch", std::string {});
    auto sit = in->sketches.find(sketchId);
    if (sit == in->sketches.end()) {
        return failed("The sketch this extrude uses (" + sketchId + ") is missing or has a problem.",
                      {"Fix the sketch first: it is marked in the timeline.",
                       "Edit this extrude and pick another sketch."});
    }
    const SketchState& sk = *sit->second;
    std::vector<const Profile*> profiles;
    const auto& want = p.contains("profiles") ? p["profiles"] : nlohmann::json("all");
    if (want.is_string() && want.get<std::string>() == "all") {
        for (auto& r : sk.profiles.regions) {
            profiles.push_back(&r);
        }
    }
    else {
        for (auto& k : want) {
            const std::string key = k.get<std::string>();
            const Profile* hit = nullptr;
            for (auto& r : sk.profiles.regions) {
                if (r.key == key) {
                    hit = &r;
                }
            }
            if (hit) {
                profiles.push_back(hit);
            }
        }
        if (profiles.size() < want.size()) {
            if (want.size() == 1 && sk.profiles.regions.size() == 1) {
                profiles = {&sk.profiles.regions[0]};
                status.level = StatusLevel::Warning;
                status.message = "The sketch's closed area changed shape; the extrude now uses its "
                                 "only closed area. Check the result.";
            }
            else {
                return failed("Some of the closed areas this extrude used are gone from the sketch.",
                              {"Edit the extrude and pick the areas again."});
            }
        }
    }
    if (profiles.empty()) {
        return failed("The sketch has no closed area to extrude.",
                      {"Close the outline: look for small gaps between line ends.",
                       "Turn on 'Show gaps' in the sketch."});
    }

    ExtrudeInput ei;
    ei.featureId = f.id;
    ei.profiles = profiles;
    ei.normal = sk.frame.Direction();
    std::string dir = p.value("direction", std::string("one"));
    ei.direction = dir == "symmetric" ? ExtrudeDirection::Symmetric
                 : dir == "reverse"   ? ExtrudeDirection::Reverse
                                      : ExtrudeDirection::OneSide;
    if (p.value("extent", std::string("distance")) == "all") {
        Bnd_Box all;
        for (auto& [id, b] : in->bodies) {
            all.Add(boxOf(b->shape()));
        }
        double d = all.IsVoid() ? 1000 : std::sqrt(all.SquareExtent()) * 2 + 10;
        // through everything: from far behind the sketch plane to far beyond it
        ei.distance = 2 * d;
        ei.direction = ExtrudeDirection::Symmetric;
    }
    else {
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
    if (!tool) {
        return failed(tool.error());
    }

    auto out = copyState(in);
    if (*op == BoolOp::NewBody || (in->bodies.empty() && *op == BoolOp::Join)) {
        out->bodies[f.id] = std::make_shared<NamedShape>(std::move(tool.value()));
        return {out, status};
    }
    if (in->bodies.empty()) {
        return failed("There is no body to " + std::string(*op == BoolOp::Cut ? "cut" : "intersect") + " yet.",
                      {"Use 'New body' for the first shape."});
    }
    std::string target = p.value("target", std::string {});
    if (target.empty()) {
        Bnd_Box tb = boxOf(tool->shape());
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
        return failed("The body this extrude should change no longer exists.",
                      {"Edit the extrude and choose a body."});
    }
    auto res = booleanOp(f.id, *op, *bit->second, tool.value());
    if (!res) {
        return failed(res.error());
    }
    out->bodies[target] = std::make_shared<NamedShape>(std::move(res.value()));
    return {out, status};
}

// ---------------------------------------------------------------- fillet

FeatureOutcome applyFillet(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in)
{
    const auto& p = f.params;
    FeatureStatus status;
    auto r = doc.evalLength(p.value("radius", std::string("1 mm")));
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
            msg += std::to_string(missing) + " of the edges this fillet rounded no longer exist. ";
        }
        if (ambiguous) {
            msg += std::to_string(ambiguous) + " edge" + (ambiguous == 1 ? " was" : "s were")
                 + " split, and Jigsmith can't tell which piece you meant.";
        }
        return failed(msg,
                      {"Edit the fillet and pick the edges again.",
                       "Or roll back to the step that changed them."},
                      unresolved);
    }
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    if (edges.empty()) {
        return failed("This fillet has no edges.", {"Edit the fillet and pick edges to round."});
    }
    auto res = filletEdges(f.id, *in->bodies.at(bodyId), edges, r.value());
    if (!res) {
        return failed(res.error());
    }
    if (rebound) {
        status.level = StatusLevel::Warning;
        status.message = std::to_string(rebound) + " edge" + (rebound == 1 ? " was" : "s were")
                       + " re-attached by shape similarity after an earlier change. Check the result.";
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
