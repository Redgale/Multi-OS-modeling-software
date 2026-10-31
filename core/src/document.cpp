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
    for (auto k : {FeatureKind::Revolve, FeatureKind::Hole, FeatureKind::Pattern, FeatureKind::Mirror}) {
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
        return makeError("param.unknown", "There is no parameter called '" + name + "'.");
    }
    if (!visiting.insert(name).second) {
        return makeError("param.cycle",
                         "Parameter '" + name + "' depends on itself.",
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

std::vector<std::string> Document::dependents(const std::string& id) const
{
    std::vector<std::string> out;
    const int i = featureIndex(id);
    if (i < 0) {
        return out;
    }
    const std::string quoted = "\"" + id + "\"", prefix = "\"" + id + "/", inName = "|" + id + "/",
                      inEdge = "[" + id + "/", inFillet = ":" + id + "/";
    for (size_t k = size_t(i) + 1; k < model_.features.size(); ++k) {
        const std::string d = model_.features[k].params.dump();
        if (d.find(quoted) != std::string::npos || d.find(prefix) != std::string::npos
            || d.find(inName) != std::string::npos || d.find(inEdge) != std::string::npos
            || d.find(inFillet) != std::string::npos) {
            out.push_back(model_.features[k].id);
        }
    }
    return out;
}

Result<bool> Document::transact(const std::string& label, const Edit& edit)
{
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

void Document::recompute()
{
    auto t0 = std::chrono::steady_clock::now();
    recomputed_ = 0;
    const int n = int(model_.features.size());
    const int active = activeCount();
    states_.clear();
    statuses_.assign(n, {});
    std::shared_ptr<const ModelState> prev = empty_;
    std::string prevKey = "root";
    std::set<std::string> live;
    for (int i = 0; i < n; ++i) {
        const Feature& f = model_.features[i];
        live.insert(f.id);
        if (i >= active) {
            statuses_[i].level = StatusLevel::RolledBack;
            continue;
        }
        if (f.suppressed) {
            statuses_[i].level = StatusLevel::Suppressed;
            statuses_[i].message = "Suppressed: this step is skipped.";
            states_.push_back(prev);
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
            ++recomputed_;
        }
        statuses_[i] = it->second.status;
        states_.push_back(it->second.after);
        prev = it->second.after;
        prevKey = key;
    }
    std::erase_if(cache_, [&](const auto& kv) { return !live.count(kv.first); });
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

// ---------------------------------------------------------------- json

nlohmann::json Document::toJson() const
{
    nlohmann::json j = model_.extra;
    j["format_version"] = kFormatVersion;
    j["units"] = "mm";
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
        jf["params"] = f.params;
        feats.push_back(jf);
    }
    j["features"] = feats;
    j["rollback"] = model_.rollback;
    j["counters"] = model_.counters;
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
                                                  "features", "rollback", "counters"};
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
        static const std::set<std::string> fknown {"id", "name", "kind", "suppressed", "params"};
        for (auto& jf : j.at("features")) {
            Feature f;
            f.id = jf.at("id").get<std::string>();
            f.name = jf.value("name", f.id);
            auto k = featureKindFromString(jf.at("kind").get<std::string>());
            if (!k) {
                return makeError("file.feature",
                                 "This design uses a feature this version of Jigsmith doesn't know ("
                                     + jf.at("kind").get<std::string>() + ").",
                                 {"Update Jigsmith to open it."});
            }
            f.kind = *k;
            f.suppressed = jf.value("suppressed", false);
            f.params = jf.value("params", nlohmann::json::object());
            for (auto& [key, val] : jf.items()) {
                if (!fknown.count(key)) {
                    f.extra[key] = val;
                }
            }
            m.features.push_back(std::move(f));
        }
        m.rollback = j.value("rollback", -1);
        m.counters = j.value("counters", std::map<std::string, int> {});
        auto doc = std::make_unique<Document>();
        doc->resetModel(std::move(m));
        return doc;
    }
    catch (const nlohmann::json::exception& ex) {
        return makeError("file.damaged", "This design file is damaged and could not be read.", {},
                         ex.what());
    }
}

}  // namespace jig
