// SPDX-License-Identifier: GPL-3.0-or-later
#include "jig/document.hpp"

#include <chrono>
#include <cstdio>
#include <functional>
#include <set>

#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>

namespace jig
{

const char* toString(FeatureKind k)
{
    switch (k) {
        case FeatureKind::Sketch:
            return "sketch";
        case FeatureKind::Extrude:
            return "extrude";
        case FeatureKind::Fillet:
            return "fillet";
        case FeatureKind::Chamfer:
            return "chamfer";
        case FeatureKind::Shell:
            return "shell";
        case FeatureKind::Revolve:
            return "revolve";
        case FeatureKind::Hole:
            return "hole";
        case FeatureKind::Pattern:
            return "pattern";
        case FeatureKind::Mirror:
            return "mirror";
        case FeatureKind::Plane:
            return "plane";
        case FeatureKind::Combine:
            return "combine";
        case FeatureKind::Split:
            return "split";
        case FeatureKind::Move:
            return "move";
        case FeatureKind::Axis:
            return "axis";
        case FeatureKind::PressPull:
            return "presspull";
        case FeatureKind::Point:
            return "point";
        case FeatureKind::Import:
            return "import";
        case FeatureKind::Remove:
            return "remove";
        case FeatureKind::Loft:
            return "loft";
        case FeatureKind::Sweep:
            return "sweep";
        case FeatureKind::Pipe:
            return "pipe";
        case FeatureKind::Coil:
            return "coil";
        case FeatureKind::Primitive:
            return "shape";
        case FeatureKind::Rib:
            return "rib";
        case FeatureKind::Emboss:
            return "emboss";
        case FeatureKind::Draft:
            return "draft";
        case FeatureKind::Scale:
            return "scale";
    }
    return "sketch";
}

std::optional<FeatureKind> featureKindFromString(std::string_view s)
{
    if (s == "sketch") {
        return FeatureKind::Sketch;
    }
    if (s == "extrude") {
        return FeatureKind::Extrude;
    }
    if (s == "fillet") {
        return FeatureKind::Fillet;
    }
    if (s == "chamfer") {
        return FeatureKind::Chamfer;
    }
    if (s == "shell") {
        return FeatureKind::Shell;
    }
    for (auto k : {FeatureKind::Revolve, FeatureKind::Hole, FeatureKind::Pattern, FeatureKind::Mirror, FeatureKind::Plane,
                   FeatureKind::Combine, FeatureKind::Split, FeatureKind::Move, FeatureKind::Axis, FeatureKind::PressPull, FeatureKind::Point, FeatureKind::Import, FeatureKind::Remove, FeatureKind::Loft, FeatureKind::Sweep, FeatureKind::Pipe, FeatureKind::Coil, FeatureKind::Primitive, FeatureKind::Rib, FeatureKind::Emboss, FeatureKind::Draft, FeatureKind::Scale}) {
        if (s == toString(k)) {
            return k;
        }
    }
    return std::nullopt;
}

namespace
{

const char* paramKindName(ParamKind k)
{
    switch (k) {
        case ParamKind::Length:
            return "length";
        case ParamKind::Angle:
            return "angle";
        case ParamKind::Number:
            return "number";
    }
    return "length";
}

ParamKind paramKindFromString(const std::string& s)
{
    if (s == "angle") {
        return ParamKind::Angle;
    }
    if (s == "number") {
        return ParamKind::Number;
    }
    return ParamKind::Length;
}

Dim dimOf(ParamKind k)
{
    return k == ParamKind::Length ? Dim::len() : k == ParamKind::Angle ? Dim::ang() : Dim::none();
}

void collectStrings(const nlohmann::json& j, std::vector<std::string>& out)
{
    if (j.is_string()) {
        out.push_back(j.get<std::string>());
    }
    else if (j.is_array() || j.is_object()) {
        for (auto& v : j) {
            collectStrings(v, out);
        }
    }
}

// Upgrades older documents step by step. Index i migrates format i+1 -> i+2.
// (Format 1 is the first; add steps here, never edit old ones.)
const std::vector<std::function<void(nlohmann::json&)>>& migrations()
{
    static const std::vector<std::function<void(nlohmann::json&)>> m;
    return m;
}

}  // namespace

Document::Document()
    : empty_(std::make_shared<ModelState>())
{}

const Feature* Document::feature(const std::string& id) const
{
    for (auto& f : model_.features) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

int Document::featureIndex(const std::string& id) const
{
    for (size_t i = 0; i < model_.features.size(); ++i) {
        if (model_.features[i].id == id) {
            return int(i);
        }
    }
    return -1;
}

int Document::activeCount() const
{
    int n = int(model_.features.size());
    return model_.rollback < 0 ? n : std::min(model_.rollback, n);
}

// ---------------------------------------------------------------- parameters

namespace
{
Result<Quantity> evalParamImpl(const Model& m, const std::string& name, std::set<std::string>& visiting)
{
    const Parameter* p = nullptr;
    for (auto& q : m.parameters) {
        if (q.name == name) {
            p = &q;
        }
    }
    if (!p) {
        return makeError("param.unknown", fmt("There is no parameter called '%1'.", {name}));
    }
    if (!visiting.insert(name).second) {
        return makeError("param.cycle",
                         fmt("Parameter '%1' depends on itself.", {name}),
                         {"Change one of the expressions so they don't refer to each other in a loop."});
    }
    std::optional<Error> innerError;  // the real reason a referenced parameter failed
    ParamLookup inner = [&](std::string_view n) -> std::optional<Quantity> {
        bool exists = false;
        for (auto& q : m.parameters) {
            exists = exists || q.name == n;
        }
        if (!exists) {
            return std::nullopt;
        }
        auto r = evalParamImpl(m, std::string(n), visiting);
        if (!r) {
            if (!innerError) {
                innerError = r.error();
            }
            return std::nullopt;
        }
        return r.value();
    };
    auto r = evaluate(p->expr, inner, dimOf(p->kind));
    visiting.erase(name);
    if (!r && innerError) {
        return *innerError;
    }
    if (!r) {
        Error e = r.error();
        e.message = "Parameter '" + name + "': " + e.message;
        return e;
    }
    return r;
}
}  // namespace

Result<Quantity> Document::evalParam(const std::string& name) const
{
    return evalParamIn(model_, name);
}

Result<Quantity> Document::evalParamIn(const Model& m, const std::string& name)
{
    std::set<std::string> visiting;
    return evalParamImpl(m, name, visiting);
}

ParamLookup Document::lookup() const
{
    return [this](std::string_view n) -> std::optional<Quantity> {
        auto r = evalParam(std::string(n));
        if (!r) {
            return std::nullopt;
        }
        return r.value();
    };
}

Result<double> Document::evalLength(const std::string& expr) const
{
    auto r = evaluate(expr, lookup(), Dim::len());
    if (!r) {
        return r.error();
    }
    return r->value;
}

std::string Document::lengthInput(const std::string& expr) const
{
    return model_.units == LengthUnit::Millimetre ? expr : withLengthUnit(expr, model_.units, lookup());
}

std::string Document::defaultLength(double mm, double inches) const
{
    switch (model_.units) {
        case LengthUnit::Inch:
            return formatLength(inches * 25.4, LengthUnit::Inch);
        case LengthUnit::Foot:
            return formatLength(inches * 25.4, LengthUnit::Inch);  // feet are too coarse for part sizes
        case LengthUnit::Centimetre:
            return formatLength(mm, LengthUnit::Centimetre);
        default:
            return formatLength(mm, LengthUnit::Millimetre);
    }
}

Result<double> Document::evalAngle(const std::string& expr) const
{
    auto r = evaluate(expr, lookup(), Dim::ang());
    if (!r) {
        return r.error();
    }
    return r->value;
}

// ---------------------------------------------------------------- transactions

std::string Document::newId(Model& m, const std::string& prefix)
{
    int& n = m.counters[prefix];
    if (n < 1) {
        n = 1;
    }
    return prefix + std::to_string(n++);
}

void Document::insertAtMarker(Model& m, Feature f)
{
    const int n = int(m.features.size());
    const int at = m.rollback < 0 ? n : std::min(m.rollback, n);
    m.features.insert(m.features.begin() + at, std::move(f));
    if (m.rollback >= 0) {
        m.rollback = at + 1;
    }
}

Feature* Document::findFeature(Model& m, const std::string& id)
{
    for (auto& f : m.features) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

namespace
{
// True when a feature's parameters refer to `id`: by id, or through names of faces and
// edges it made ("e1/end", "E[e1/end|…]", "f1/fillet:…").
bool mentions(const Feature& f, const std::string& id)
{
    const std::string d = f.params.dump();
    const std::string quoted = "\"" + id + "\"", prefix = "\"" + id + "/", inName = "|" + id + "/",
                      inEdge = "[" + id + "/", inFillet = ":" + id + "/";
    return d.find(quoted) != std::string::npos || d.find(prefix) != std::string::npos ||
           d.find(inName) != std::string::npos || d.find(inEdge) != std::string::npos ||
           d.find(inFillet) != std::string::npos;
}
}  // namespace

std::vector<std::string> Document::dependents(const std::string& id) const
{
    std::vector<std::string> out;
    const int i = featureIndex(id);
    if (i < 0) {
        return out;
    }
    for (size_t k = size_t(i) + 1; k < model_.features.size(); ++k) {
        if (mentions(model_.features[k], id)) {
            out.push_back(model_.features[k].id);
        }
    }
    return out;
}

Result<bool> Document::moveFeature(const std::string& id, int to)
{
    const int from = featureIndex(id);
    const int n = int(model_.features.size());
    if (from < 0) {
        return makeError("move.missing", "That step no longer exists.");
    }
    to = std::clamp(to, 0, n - 1);
    if (to == from) {
        return true;
    }
    const Feature& f = model_.features[size_t(from)];
    if (to < from) {
        for (int k = to; k < from; ++k) {
            const Feature& other = model_.features[size_t(k)];
            if (mentions(f, other.id)) {
                return makeError("move.order", fmt("“%1” uses “%2”, so it must stay after it.", {f.name, other.name}),
                                 {fmt("Move it to a place after “%1”.", {other.name})});
            }
        }
    }
    else {
        for (int k = from + 1; k <= to; ++k) {
            const Feature& other = model_.features[size_t(k)];
            if (mentions(other, f.id)) {
                return makeError("move.order", fmt("“%1” uses “%2”, so “%2” must stay before it.", {other.name, f.name}),
                                 {fmt("Move it to a place before “%1”.", {other.name})});
            }
        }
    }
    // remember which steps work now, so a move that breaks one can be refused
    std::set<std::string> okBefore;
    for (int k = 0; k < n; ++k) {
        if (status(k).level != StatusLevel::Error) {
            okBefore.insert(model_.features[size_t(k)].id);
        }
    }
    const std::string name = f.name;
    auto r = transact("Move " + name, [&](Model& m) -> Result<bool> {
        Feature moved = m.features[size_t(from)];
        m.features.erase(m.features.begin() + from);
        m.features.insert(m.features.begin() + to, std::move(moved));
        return true;
    });
    if (!r) {
        return r;
    }
    std::vector<std::string> broken;
    for (int k = 0; k < n; ++k) {
        const Feature& g = model_.features[size_t(k)];
        if (status(k).level == StatusLevel::Error && okBefore.count(g.id)) {
            broken.push_back(g.name);
        }
    }
    if (!broken.empty()) {
        undo();
        redo_.clear();  // the refused move is not something to redo
        std::string list;
        for (auto& b : broken) {
            list += (list.empty() ? "“" : ", “") + b + "”";
        }
        return makeError("move.breaks", fmt("Moving “%1” there would stop %2 from working.", {name, list}),
                         {"Try another place in the timeline."});
    }
    return true;
}

Result<bool> Document::transact(const std::string& label, const Edit& edit)
{
    if (busy_) {
        return makeError("document.busy", "The design is still being rebuilt. Try again in a moment.");
    }
    Model next = model_;
    auto r = edit(next);
    if (!r) {
        return r;
    }
    undo_.push_back({label, std::move(model_)});
    redo_.clear();
    model_ = std::move(next);
    ++revision_;
    recompute();
    return true;
}

std::string Document::undoLabel() const
{
    return undo_.empty() ? std::string {} : undo_.back().label;
}

std::string Document::redoLabel() const
{
    return redo_.empty() ? std::string {} : redo_.back().label;
}

bool Document::undo()
{
    if (undo_.empty()) {
        return false;
    }
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back({s.label, std::move(model_)});
    model_ = std::move(s.model);
    ++revision_;
    recompute();
    return true;
}

bool Document::redo()
{
    if (redo_.empty()) {
        return false;
    }
    Snapshot s = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back({s.label, std::move(model_)});
    model_ = std::move(s.model);
    ++revision_;
    recompute();
    return true;
}

void Document::resetModel(Model m)
{
    model_ = std::move(m);
    undo_.clear();
    redo_.clear();
    cache_.clear();
    ++revision_;
    recompute();
}

// ---------------------------------------------------------------- recompute

std::string Document::cacheKey(const Feature& f, const std::string& prevKey) const
{
    std::string k = prevKey;
    k += '\x1f';
    k += f.id;
    k += '\x1f';
    k += f.params.dump();
    std::vector<std::string> strings;
    collectStrings(f.params, strings);
    std::set<std::string> names;
    for (auto& s : strings) {
        for (auto& n : referencedNames(s)) {
            names.insert(n);
        }
    }
    for (auto& n : names) {
        auto q = evalParam(n);
        char buf[96];
        if (q) {
            std::snprintf(buf, sizeof buf, "%s=%.17g/%d/%d;", n.c_str(), q->value, q->dim.length,
                          q->dim.angle);
        }
        else {
            std::snprintf(buf, sizeof buf, "%s=!;", n.c_str());
        }
        k += buf;
    }
    return std::to_string(std::hash<std::string> {}(k));
}

namespace
{
Document::Runner& recomputeRunner()
{
    static Document::Runner r;
    return r;
}
}  // namespace

void Document::setRecomputeRunner(Runner r)
{
    recomputeRunner() = std::move(r);
}

void Document::recompute()
{
    auto t0 = std::chrono::steady_clock::now();
    busy_ = true;
    const int n = int(model_.features.size());
    const int active = activeCount();
    // worked out into these, then put in place: anything that looks at the document while
    // the work runs elsewhere sees the previous results, never half of the new ones
    std::vector<std::shared_ptr<const ModelState>> states;
    std::vector<FeatureStatus> statuses(static_cast<size_t>(n));
    int recomputed = 0;
    progressDone_ = 0;
    progressTotal_ = n;
    auto work = [&] {
        std::shared_ptr<const ModelState> prev = empty_;
        std::string prevKey = "root";
        std::set<std::string> live;
        for (int i = 0; i < n; ++i) {
            const Feature& f = model_.features[size_t(i)];
            live.insert(f.id);
            progressDone_ = i;
            if (i >= active) {
                statuses[size_t(i)].level = StatusLevel::RolledBack;
                continue;
            }
            if (f.suppressed) {
                statuses[size_t(i)].level = StatusLevel::Suppressed;
                statuses[size_t(i)].message = "Suppressed: this step is skipped.";
                states.push_back(prev);
                continue;
            }
            std::string key = cacheKey(f, prevKey);
            auto it = cache_.find(f.id);
            if (it == cache_.end() || it->second.key != key) {
                FeatureOutcome o = applyFeature(*this, f, prev);
                if (!o.after) {
                    o.after = prev;  // failed features change nothing (rollback)
                }
                cache_[f.id] = {key, o.after, o.status};
                it = cache_.find(f.id);
                ++recomputed;
            }
            statuses[size_t(i)] = it->second.status;
            states.push_back(it->second.after);
            prev = it->second.after;
            prevKey = key;
        }
        std::erase_if(cache_, [&](const auto& kv) { return !live.count(kv.first); });
        progressDone_ = n;
    };
    if (recomputeRunner()) {
        recomputeRunner()(*this, work);
    }
    else {
        work();
    }
    states_ = std::move(states);
    statuses_ = std::move(statuses);
    recomputed_ = recomputed;
    // A sketch of open curves is not a problem when a later step builds a wall along them.
    for (int i = 0; i < n; ++i) {
        const Feature& f = model_.features[size_t(i)];
        if (f.kind != FeatureKind::Sketch || statuses_[size_t(i)].level != StatusLevel::Warning ||
            statuses_[size_t(i)].message.rfind("This sketch has no closed area yet", 0) != 0) {
            continue;
        }
        for (int j = i + 1; j < n; ++j) {
            const auto& g = model_.features[size_t(j)];
            const auto prof = g.params.value("profiles", nlohmann::json("all"));
            if (g.kind == FeatureKind::Rib && g.params.value("sketch", std::string {}) == f.id &&
                statuses_[size_t(j)].level != StatusLevel::Error) {
                statuses_[size_t(i)] = {};
                statuses_[size_t(i)].message = fmt("Open curves, the edge of %1.", {g.name});
                break;
            }
            if (g.kind == FeatureKind::Extrude && g.params.value("sketch", std::string {}) == f.id &&
                (prof == nlohmann::json("open") || prof == nlohmann::json::array({"open"})) &&
                statuses_[size_t(j)].level == StatusLevel::Ok) {
                statuses_[size_t(i)] = {};
                statuses_[size_t(i)].message = fmt("Open curves, used as a wall by %1.", {g.name});
                break;
            }
            // a path for a sweep or a pattern is meant to be open
            const nlohmann::json path = g.params.value("path", nlohmann::json::object());
            if ((g.kind == FeatureKind::Sweep || g.kind == FeatureKind::Pipe || g.kind == FeatureKind::Pattern ||
                 g.kind == FeatureKind::Plane) && path.is_object() &&
                path.value("sketch", std::string {}) == f.id && statuses_[size_t(j)].level != StatusLevel::Error) {
                statuses_[size_t(i)] = {};
                statuses_[size_t(i)].message = fmt("Open curves, used as a path by %1.", {g.name});
                break;
            }
        }
    }
    progressDone_ = 0;
    progressTotal_ = 0;
    busy_ = false;
    lastMs_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

const ModelState& Document::state() const
{
    return states_.empty() ? *empty_ : *states_.back();
}

const ModelState& Document::stateBefore(int featureIndex) const
{
    int k = std::min<int>(featureIndex, int(states_.size()));
    return k <= 0 ? *empty_ : *states_[k - 1];
}

const FeatureStatus& Document::status(int featureIndex) const
{
    static const FeatureStatus none;
    if (featureIndex < 0 || featureIndex >= int(statuses_.size())) {
        return none;
    }
    return statuses_[featureIndex];
}

TopoDS_Shape Document::combinedShape() const
{
    const auto& bodies = state().bodies;
    if (bodies.size() == 1) {
        return bodies.begin()->second->shape();
    }
    TopoDS_Compound c;
    BRep_Builder b;
    b.MakeCompound(c);
    for (auto& [id, body] : bodies) {
        b.Add(c, body->shape());
    }
    return c;
}

// ---------------------------------------------------------------- view

void Document::setHiddenBodies(std::set<std::string> ids)
{
    if (ids != hidden_) {
        hidden_ = std::move(ids);
        ++revision_;
    }
}

void Document::setShownSketches(std::set<std::string> ids)
{
    if (ids != shownSketches_) {
        shownSketches_ = std::move(ids);
        ++revision_;
    }
}

void Document::setFoldedGroups(std::set<std::string> groups)
{
    if (groups != folded_) {
        folded_ = std::move(groups);
        ++revision_;
    }
}

void Document::setNamedView(const std::string& name, nlohmann::json camera)
{
    if (camera.is_null()) {
        if (views_.erase(name)) {
            ++revision_;
        }
        return;
    }
    views_[name] = std::move(camera);
    ++revision_;
}

// ---------------------------------------------------------------- json

nlohmann::json Document::toJson() const
{
    nlohmann::json j = model_.extra;
    j["format_version"] = kFormatVersion;
    j["units"] = unitSymbol(model_.units);
    j["up_axis"] = "Z";
    nlohmann::json params = nlohmann::json::array();
    for (auto& p : model_.parameters) {
        nlohmann::json jp {{"name", p.name}, {"expr", p.expr}, {"kind", paramKindName(p.kind)}};
        if (!p.comment.empty()) {
            jp["comment"] = p.comment;
        }
        params.push_back(jp);
    }
    j["parameters"] = params;
    nlohmann::json feats = nlohmann::json::array();
    for (auto& f : model_.features) {
        nlohmann::json jf = f.extra;
        jf["id"] = f.id;
        jf["name"] = f.name;
        jf["kind"] = toString(f.kind);
        if (f.suppressed) {
            jf["suppressed"] = true;
        }
        if (!f.group.empty()) {
            jf["group"] = f.group;
        }
        jf["params"] = f.params;
        feats.push_back(jf);
    }
    j["features"] = feats;
    j["rollback"] = model_.rollback;
    j["counters"] = model_.counters;
    if (!hidden_.empty() || !folded_.empty() || !views_.empty() || !shownSketches_.empty()) {
        j["view"] = {{"hidden_bodies", hidden_}, {"folded_groups", folded_}};
        if (!shownSketches_.empty()) {
            j["view"]["shown_sketches"] = shownSketches_;
        }
        if (!views_.empty()) {
            j["view"]["named_views"] = views_;
        }
    }
    return j;
}

Result<std::unique_ptr<Document>> Document::fromJson(const nlohmann::json& input)
{
    nlohmann::json j = input;
    try {
        int v = j.value("format_version", 0);
        if (v < 1) {
            return makeError("file.version", "This is not a Jigsmith design file.");
        }
        if (v > kFormatVersion) {
            return makeError("file.too_new",
                             "This design was saved by a newer version of Jigsmith.",
                             {"Update Jigsmith to open it."});
        }
        for (int step = v; step < kFormatVersion; ++step) {
            migrations()[step - 1](j);
        }
        Model m;
        static const std::set<std::string> known {"format_version", "units", "up_axis", "parameters",
                                                  "features", "rollback", "counters", "view"};
        for (auto& [k, val] : j.items()) {
            if (!known.count(k)) {
                m.extra[k] = val;
            }
        }
        for (auto& jp : j.at("parameters")) {
            Parameter p;
            p.name = jp.at("name").get<std::string>();
            p.expr = jp.at("expr").get<std::string>();
            p.kind = paramKindFromString(jp.value("kind", std::string("length")));
            p.comment = jp.value("comment", std::string {});
            m.parameters.push_back(p);
        }
        static const std::set<std::string> fknown {"id", "name", "kind", "suppressed", "group", "params"};
        for (auto& jf : j.at("features")) {
            Feature f;
            f.id = jf.at("id").get<std::string>();
            f.name = jf.value("name", f.id);
            auto k = featureKindFromString(jf.at("kind").get<std::string>());
            if (!k) {
                return makeError("file.feature",
                                 fmt("This design uses a feature this version of Jigsmith doesn't know (%1).",
                                     {jf.at("kind").get<std::string>()}),
                                 {"Update Jigsmith to open it."});
            }
            f.kind = *k;
            f.suppressed = jf.value("suppressed", false);
            f.group = jf.value("group", std::string {});
            f.params = jf.value("params", nlohmann::json::object());
            for (auto& [key, val] : jf.items()) {
                if (!fknown.count(key)) {
                    f.extra[key] = val;
                }
            }
            m.features.push_back(std::move(f));
        }
        if (auto u = lengthUnitFromString(j.value("units", std::string("mm")))) {
            m.units = *u;
        }
        m.rollback = j.value("rollback", -1);
        m.counters = j.value("counters", std::map<std::string, int> {});
        for (auto& [prefix, n] : m.counters) {
            n = std::clamp(n, 1, 1'000'000'000);  // a damaged count must not overflow when it grows
        }
        auto doc = std::make_unique<Document>();
        doc->resetModel(std::move(m));
        if (j.contains("view") && j["view"].is_object()) {
            doc->hidden_ = j["view"].value("hidden_bodies", std::set<std::string> {});
            doc->folded_ = j["view"].value("folded_groups", std::set<std::string> {});
            doc->shownSketches_ = j["view"].value("shown_sketches", std::set<std::string> {});
            if (j["view"].contains("named_views") && j["view"]["named_views"].is_object()) {
                for (auto& [k, v] : j["view"]["named_views"].items()) {
                    doc->views_[k] = v;
                }
            }
        }
        return doc;
    }
    catch (const nlohmann::json::exception& ex) {
        return makeError("file.damaged", "This design file is damaged and could not be read.", {},
                         ex.what());
    }
}

}  // namespace jig
