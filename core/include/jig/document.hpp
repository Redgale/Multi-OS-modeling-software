// SPDX-License-Identifier: GPL-3.0-or-later
// The headless document: parameters, the feature timeline, deterministic
// incremental recompute and transactional undo (architecture rules 2-4).
//
// The editable *model* (parameters + features + rollback marker) is small plain
// data. Geometry is always derived from it by recompute and cached per feature;
// a cache entry is reused only when the feature, the parameter values it reads,
// and everything before it are unchanged. Undo/redo swap whole model snapshots
// (D-007), which is trivially correct because recompute is deterministic.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jig/expr.hpp"
#include <gp_Ax3.hxx>

#include "jig/kernel.hpp"
#include "jig/result.hpp"
#include "jig/sketch.hpp"

namespace jig
{

constexpr int kFormatVersion = 1;

enum class ParamKind
{
    Length,
    Angle,
    Number
};

struct Parameter
{
    std::string name;
    std::string expr;  // e.g. "90 mm", "width / 2"
    ParamKind kind = ParamKind::Length;
    std::string comment;
};

enum class FeatureKind
{
    Sketch,
    Extrude,
    Fillet,
    Chamfer,
    Shell,
    Revolve,
    Hole,
    Pattern,
    Mirror,
    Plane,  // construction plane (offset, at an angle, midplane)
    Combine,
    Split,
    Move,
    Axis,  // construction axis (along an edge, through a round face, where two planes meet)
    PressPull,  // push or pull a flat face of a body
    Point,      // construction point (typed position, end or centre of an edge, centre of a face)
    Import,     // a solid brought in from a STEP file, kept inside the design
    Remove,     // bodies taken out of the design from here on (delete a body)
    Loft,       // a solid through closed areas of two or more sketches, in order
    Sweep,      // a sketch's closed area pushed along a path of curves in another sketch
    Pipe,       // a round tube or rod along a path of sketch curves
    Coil,       // a helix with a round or square section (springs)
    Primitive,  // a box, cylinder, sphere or torus placed on a plane, no sketch
    Rib,        // a thin wall from an open sketch curve to the body (a gusset)
    Emboss,     // a sketch's areas wrapped onto a round face, raised or sunk
    Draft,      // faces of a body sloped by an angle around a neutral plane
    Scale,      // bodies made bigger or smaller about a point (or stretched per axis)
    Thread,     // a helical thread cut into a shaft or the wall of a hole
    Canvas,     // a picture placed on a plane to trace over (display only)
    Joint       // a part placed against another and moved by the joint's motion
};

const char* toString(FeatureKind k);
std::optional<FeatureKind> featureKindFromString(std::string_view s);

struct Feature
{
    std::string id;    // stable, never reused: "s1", "e2", "f3"
    std::string name;  // user-visible, renameable: "Base sketch"
    FeatureKind kind = FeatureKind::Sketch;
    bool suppressed = false;
    std::string group;  // timeline group ("Base", "Mounting holes"); empty = none
    nlohmann::json params = nlohmann::json::object();  // kind-specific, see features.cpp
    nlohmann::json extra = nlohmann::json::object();   // unknown fields, preserved on save
};

struct Model
{
    std::vector<Parameter> parameters;
    std::vector<Feature> features;
    int rollback = -1;  // number of active features; -1 = all
    std::map<std::string, int> counters;  // next id number per prefix
    LengthUnit units = LengthUnit::Millimetre;  // how sizes are shown and typed (geometry is always mm)
    nlohmann::json extra = nlohmann::json::object();
};

enum class StatusLevel
{
    Ok,
    Warning,
    Error,
    Suppressed,
    RolledBack  // after the rollback marker: not computed
};

struct FeatureStatus
{
    StatusLevel level = StatusLevel::Ok;
    std::string message;
    std::vector<std::string> suggestions;
    std::string detail;
    std::vector<Fix> fixes;  // one-click repairs (set a parameter of this step)
};

struct SketchState
{
    gp_Ax3 frame;
    Sketch solved;
    SolveReport report;
    ProfileSet profiles;
};

// The shape a feature added or removed, so a Pattern can repeat it.
struct ToolRecord
{
    std::shared_ptr<const NamedShape> tool;
    BoolOp op = BoolOp::Join;
    std::string target;  // body it was applied to
};

// Geometry after a feature has been applied. Immutable once built; shared between
// cache entries.
struct ModelState
{
    std::map<std::string, std::shared_ptr<const NamedShape>> bodies;
    std::map<std::string, std::shared_ptr<const SketchState>> sketches;
    std::map<std::string, ToolRecord> tools;
    std::map<std::string, gp_Ax3> planes;  // construction planes by feature id
    std::map<std::string, gp_Ax1> axes;    // construction axes by feature id
    std::map<std::string, gp_Pnt> points;  // construction points by feature id
    // pictures placed on planes to trace over (display only): the frame's origin is the
    // picture's centre, x along its width, y along its height
    struct Canvas
    {
        gp_Ax3 frame;
        double width = 0, height = 0, opacity = 0.5;
    };
    std::map<std::string, Canvas> canvases;
};

// Two parts that overlap, and by how much (Inspect ▸ Interference).
struct Interference
{
    std::string a, b;
    double volume = 0;  // mm^3
    TopoDS_Shape overlap;
};
std::vector<Interference> interferences(const ModelState& st);


class Document
{
public:
    Document();

    // ---- model access (read-only; change it through transactions) -------
    const Model& model() const
    {
        return model_;
    }
    const Feature* feature(const std::string& id) const;
    int featureIndex(const std::string& id) const;
    int activeCount() const;  // features before the rollback marker

    // ---- parameters -------------------------------------------------------
    ParamLookup lookup() const;
    Result<Quantity> evalParam(const std::string& name) const;
    // Evaluates a parameter of a model that is not (yet) the document's own.
    static Result<Quantity> evalParamIn(const Model& m, const std::string& name);
    Result<double> evalLength(const std::string& expr) const;
    LengthUnit units() const
    {
        return model_.units;
    }
    // Typed input made explicit in the design's unit (see withLengthUnit).
    std::string lengthInput(const std::string& expr) const;
    std::string fmtLength(double mm) const
    {
        return formatLength(mm, model_.units);
    }
    // A sensible starting value for a size field: the millimetre value in metric
    // designs, the inch value (already a round number) in inch and foot designs.
    std::string defaultLength(double mm, double inches) const;
    Result<double> evalAngle(const std::string& expr) const;

    // ---- transactions -----------------------------------------------------
    // Runs `edit` on a copy of the model. If it returns an error the document is
    // untouched. Otherwise the change is committed, recomputed, and pushed to undo.
    using Edit = std::function<Result<bool>(Model&)>;
    Result<bool> transact(const std::string& label, const Edit& edit);
    bool canUndo() const
    {
        return !undo_.empty();
    }
    bool canRedo() const
    {
        return !redo_.empty();
    }
    std::string undoLabel() const;
    std::string redoLabel() const;
    bool undo();
    bool redo();
    size_t undoDepth() const
    {
        return undo_.size();
    }

    // New stable id with a prefix ("s", "e", "f"): s1, s2, ...
    static std::string newId(Model& m, const std::string& prefix);
    // Inserts a feature at the rollback marker (end of the timeline when not rolled back).
    static void insertAtMarker(Model& m, Feature f);
    static Feature* findFeature(Model& m, const std::string& id);
    // Ids of later features that refer to `id` (by sketch id, body id or face names
    // made by it). Used to warn before deleting or reordering.
    std::vector<std::string> dependents(const std::string& id) const;
    // Moves a step to position `to` in the timeline (as one undoable change). Refused,
    // with an explanation, when the step would come before something it uses, after
    // something that uses it, or when any step that worked would stop working.
    Result<bool> moveFeature(const std::string& id, int to);

    // ---- recompute --------------------------------------------------------
    void recompute();
    // How recomputes run. The runner is handed the work and must return only once it has
    // run (it may run it on another thread and keep its own event loop going meanwhile).
    // The work reads the model and writes nothing the rest of the program reads; the
    // results are put in place afterwards on the calling thread. Default: run it right here.
    using Runner = std::function<void(const Document& doc, const std::function<void()>& work)>;
    static void setRecomputeRunner(Runner r);
    // While a recompute runs: steps done so far and steps to do (both 0 when idle).
    std::pair<int, int> recomputeProgress() const
    {
        return {progressDone_.load(), progressTotal_.load()};
    }
    bool recomputing() const
    {
        return busy_;
    }
    const ModelState& state() const;  // at the rollback marker
    const ModelState& stateBefore(int featureIndex) const;
    const FeatureStatus& status(int featureIndex) const;
    int recomputedLastTime() const
    {
        return recomputed_;
    }
    double lastRecomputeMs() const
    {
        return lastMs_;
    }
    // Monotonic counter bumped on every change: lets views know when to refresh.
    uint64_t revision() const
    {
        return revision_;
    }

    // ---- how the design is shown (saved with it, never an undo step) -------
    // Bodies hidden in the view and timeline groups shown folded. Changing them bumps
    // revision() so the change counts as unsaved, but the model and its results stay.
    const std::set<std::string>& hiddenBodies() const
    {
        return hidden_;
    }
    void setHiddenBodies(std::set<std::string> ids);
    const std::set<std::string>& foldedGroups() const
    {
        return folded_;
    }
    void setFoldedGroups(std::set<std::string> groups);
    // Sketches a later step uses are hidden by default; these were shown again by hand.
    const std::set<std::string>& shownSketches() const
    {
        return shownSketches_;
    }
    void setShownSketches(std::set<std::string> ids);
    // How each body looks: a colour ("#rrggbb") and a finish (matte, satin, glossy, metal).
    // Bodies without one use the theme's colour. An empty colour removes the body's entry.
    struct Appearance
    {
        std::string color;
        std::string finish = "satin";
        bool operator==(const Appearance&) const = default;
    };
    const std::map<std::string, Appearance>& appearances() const
    {
        return appearances_;
    }
    // Refuses colours that are not "#rrggbb" and unknown finishes.
    Result<bool> setAppearance(const std::string& body, const Appearance& a);
    // Camera positions saved under a name (what is stored is up to the app); a null value
    // removes one.
    const std::map<std::string, nlohmann::json>& namedViews() const
    {
        return views_;
    }
    void setNamedView(const std::string& name, nlohmann::json camera);

    // ---- files ------------------------------------------------------------
    nlohmann::json toJson() const;
    static Result<std::unique_ptr<Document>> fromJson(const nlohmann::json& j);
    // Replaces the model without an undo entry (used by open/recovery).
    void resetModel(Model m);

    // Combined shape of all bodies (for export).
    TopoDS_Shape combinedShape() const;

private:
    struct CacheEntry
    {
        std::string key;
        std::shared_ptr<const ModelState> after;
        FeatureStatus status;
    };
    struct Snapshot
    {
        std::string label;
        Model model;
    };

    std::string cacheKey(const Feature& f, const std::string& prevKey) const;

    Model model_;
    std::vector<Snapshot> undo_, redo_;
    std::map<std::string, CacheEntry> cache_;
    std::vector<std::shared_ptr<const ModelState>> states_;  // after each active feature
    std::vector<FeatureStatus> statuses_;
    std::shared_ptr<const ModelState> empty_;
    int recomputed_ = 0;
    double lastMs_ = 0;
    std::atomic<int> progressDone_ {0}, progressTotal_ {0};
    bool busy_ = false;
    uint64_t revision_ = 0;
    std::set<std::string> hidden_, folded_, shownSketches_;
    std::map<std::string, Appearance> appearances_;
    std::map<std::string, nlohmann::json> views_;
};

// Applies one feature to `in`. Implemented in features.cpp. Never throws.
struct FeatureOutcome
{
    std::shared_ptr<const ModelState> after;
    FeatureStatus status;
};
FeatureOutcome applyFeature(const Document& doc, const Feature& f, const std::shared_ptr<const ModelState>& in);

}  // namespace jig
