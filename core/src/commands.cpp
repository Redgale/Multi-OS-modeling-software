// SPDX-License-Identifier: GPL-3.0-or-later
#include "jig/commands.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <set>
#include <numbers>
#include <sstream>
#include <tuple>

#include "jig/io.hpp"

namespace jig
{
namespace
{

std::string lower(std::string_view s)
{
    std::string r(s);
    for (auto& c : r) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return r;
}

std::vector<std::string> words(std::string_view s)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            cur += char(std::tolower(static_cast<unsigned char>(c)));
        }
        else if (!cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------------- registry

CommandRegistry::CommandRegistry()
{
    // clang-format off
    // id, label, standard name, panel, tooltip, shortcut, aliases, essential, available(UI), scripted, comingIn
    auto add = [&](CommandInfo c) { commands_.push_back(std::move(c)); };
    const std::string m1 = "M1 (v0.1)", m2 = "M2 (v0.1)", m3 = "M3 (v0.1)", m4 = "M4 (v0.1)",
                      m5 = "M5 (v0.1)", m6 = "M6 (v0.1)", m7 = "M7 (v0.1)";

    // File / edit / view: work in the app today.
    add({"file.new", "New design", "New", "FILE", "Start an empty design with the origin planes ready to sketch on.", "Ctrl+N", {"blank", "start over"}, true, true, false, ""});
    add({"file.open", "Open…", "Open", "FILE", "Open a Jigsmith design (.jig) from your computer.", "Ctrl+O", {"load"}, true, true, false, ""});
    add({"file.save", "Save", "Save", "FILE", "Save your design to a .jig file on your computer.", "Ctrl+S", {"store", "keep"}, true, true, true, ""});
    add({"file.save_as", "Save as…", "Save As", "FILE", "Save a copy under a new name, e.g. for a size variant.", "Ctrl+Shift+S", {"copy", "variant"}, false, true, false, ""});
    add({"file.export_stl", "Export STL…", "Export STL", "FILE", "Save a triangle mesh for your 3D-printer slicer.", "", {"3d print", "slicer", "mesh", "print"}, true, true, true, ""});
    add({"file.export_step", "Export STEP…", "Export STEP", "FILE", "Save an exact solid that other CAD programs can open.", "", {"cad exchange", "stp", "share"}, false, true, true, ""});
    add({"file.export_3mf", "Export 3MF…", "Export 3MF", "FILE", "Save a modern print file that remembers units.", "", {"3d print", "slicer", "print"}, false, false, false, m6});
    add({"edit.undo", "Undo", "Undo", "EDIT", "Take back the last change. Nothing is ever lost: you can redo it.", "Ctrl+Z", {"back", "oops", "revert"}, true, true, true, ""});
    add({"edit.redo", "Redo", "Redo", "EDIT", "Bring back a change you undid.", "Ctrl+Shift+Z", {"again", "forward"}, true, true, true, ""});
    add({"edit.palette", "Find a tool…", "Command Search", "EDIT", "Type what you want to do and pick the tool.", "Ctrl+K", {"search", "find", "command", "help"}, true, true, false, ""});
    add({"view.fit", "Fit to view", "Fit", "VIEW", "Zoom so the whole design fits on screen. Use it whenever you feel lost.", "F6", {"zoom all", "lost", "see everything"}, true, true, false, ""});
    add({"view.home", "Home view", "Home", "VIEW", "Return to the default angled view.", "Home", {"reset view", "isometric"}, true, true, false, ""});
    add({"view.projection", "Perspective / orthographic", "Projection", "VIEW", "Switch between a natural view and a measured, parallel view.", "", {"ortho", "perspective", "camera"}, false, true, false, ""});

    // Sketch.
    add({"sketch.create", "Sketch", "Create Sketch", "CREATE", "Start a flat drawing on a plane or flat face. Most parts start here.", "", {"draw", "2d", "outline"}, true, false, true, m1});
    add({"sketch.finish", "Finish sketch", "Finish Sketch", "SKETCH", "Leave the sketch and go back to 3D tools.", "", {"done", "exit sketch"}, true, false, false, m1});
    add({"sketch.line", "Line", "Line", "SKETCH CREATE", "Draw straight lines; keep clicking to chain them.", "L", {"draw line", "polyline", "segment"}, true, false, true, m1});
    add({"sketch.rectangle", "Rectangle", "Rectangle", "SKETCH CREATE", "Draw a rectangle from corner to corner or from its center.", "R", {"box", "square"}, true, false, true, m1});
    add({"sketch.circle", "Circle", "Circle", "SKETCH CREATE", "Draw a circle from its center and diameter.", "C", {"round", "hole outline", "disc"}, true, false, true, m1});
    add({"sketch.arc", "Arc", "Arc", "SKETCH CREATE", "Draw part of a circle from its center, start and end.", "", {"curve", "bend"}, false, false, false, m1});
    add({"sketch.polygon", "Polygon", "Polygon", "SKETCH CREATE", "Draw a shape with equal sides, like a hex nut.", "", {"hexagon", "nut"}, false, false, false, m1});
    add({"sketch.ellipse", "Ellipse", "Ellipse", "SKETCH CREATE", "Draw an oval from its center and two axes.", "", {"oval"}, false, false, false, m1});
    add({"sketch.slot", "Slot", "Slot", "SKETCH CREATE", "Draw a rounded-end slot, ideal for adjustable screws.", "", {"oblong", "obround"}, false, false, true, m1});
    add({"sketch.spline", "Smooth curve", "Fit Point Spline", "SKETCH CREATE", "Draw a smooth curve through the points you click.", "", {"spline", "freeform", "organic"}, false, false, false, m1});
    add({"sketch.point", "Point", "Point", "SKETCH CREATE", "Place a reference point, e.g. for a hole center.", "", {"marker"}, false, false, false, m1});
    add({"sketch.text", "Text", "Text", "SKETCH CREATE", "Add lettering you can extrude or cut, e.g. a name plate.", "", {"label", "letters", "engrave", "name"}, false, false, false, m5});
    add({"sketch.mirror", "Mirror (sketch)", "Sketch Mirror", "SKETCH CREATE", "Copy sketch shapes across a line to make them symmetric.", "", {"symmetric copy", "flip"}, false, false, false, m1});
    add({"sketch.pattern_rect", "Grid copy (sketch)", "Sketch Rectangular Pattern", "SKETCH CREATE", "Repeat sketch shapes in rows and columns.", "", {"array", "grid", "repeat"}, false, false, false, m1});
    add({"sketch.pattern_circ", "Ring copy (sketch)", "Sketch Circular Pattern", "SKETCH CREATE", "Repeat sketch shapes around a center, e.g. a bolt circle.", "", {"array", "bolt circle", "repeat around"}, false, false, false, m1});
    add({"sketch.project", "Project / include", "Project", "SKETCH CREATE", "Bring edges of a body into this sketch as linked references.", "P", {"reference", "copy edges", "include"}, false, false, false, m5});
    add({"sketch.dimension", "Dimension", "Sketch Dimension", "SKETCH CREATE", "Set an exact size: click a line, circle or two items, then type.", "D", {"measure", "size", "length", "distance"}, true, false, true, m1});
    add({"sketch.trim", "Trim", "Trim", "SKETCH MODIFY", "Delete the piece of a curve between its nearest crossings.", "T", {"cut line", "delete part", "clean up"}, true, false, false, m1});
    add({"sketch.extend", "Extend", "Extend", "SKETCH MODIFY", "Lengthen a curve until it meets the next one.", "", {"lengthen", "stretch"}, false, false, false, m1});
    add({"sketch.break", "Break", "Break", "SKETCH MODIFY", "Split a curve where other curves cross it.", "", {"split curve"}, false, false, false, m1});
    add({"sketch.offset", "Offset", "Offset", "SKETCH MODIFY", "Make a parallel copy at a set distance, e.g. a wall or clearance.", "O", {"parallel copy", "wall thickness", "clearance"}, true, false, false, m1});
    add({"sketch.fillet", "Round corner (sketch)", "Sketch Fillet", "SKETCH MODIFY", "Round a sharp corner between two sketch curves.", "", {"round corner", "radius"}, false, false, false, m1});
    add({"sketch.move", "Move / copy (sketch)", "Sketch Move/Copy", "SKETCH MODIFY", "Move or copy sketch shapes by a distance or angle.", "M", {"translate", "rotate", "duplicate"}, false, false, false, m1});
    add({"sketch.construction", "Construction line", "Construction", "SKETCH MODIFY", "Turn a curve into a guide that is ignored when extruding.", "X", {"guide", "helper line", "centerline"}, true, false, false, m1});
    const char* cons[][3] = {
        {"coincident", "Coincident", "make points touch"}, {"collinear", "Collinear", "same line"},
        {"concentric", "Concentric", "same center"}, {"midpoint", "Midpoint", "middle"},
        {"fix", "Fix", "lock in place"}, {"parallel", "Parallel", "same direction"},
        {"perpendicular", "Perpendicular", "right angle 90"}, {"horizontal", "Horizontal / vertical", "level straight"},
        {"tangent", "Tangent", "touch smoothly"}, {"symmetric", "Symmetry", "mirror equal sides"},
        {"equal", "Equal", "same size"}};
    for (auto& c : cons) {
        bool scripted = std::string(c[0]) != "collinear" && std::string(c[0]) != "concentric";
        add({std::string("constraint.") + c[0], c[1], c[1], "CONSTRAINTS", std::string("Constraint: ") + c[2] + ".", "", {c[2]}, false, false, scripted, m1});
    }

    // Solid.
    add({"solid.extrude", "Extrude", "Extrude", "CREATE", "Push a closed sketch area into 3D, adding or cutting material.", "E", {"pull up", "thicken", "make 3d", "pocket", "cut through"}, true, false, true, m2});
    add({"solid.revolve", "Revolve", "Revolve", "CREATE", "Spin a profile around an axis, like a lathe: knobs, cups, wheels.", "", {"lathe", "spin", "turn"}, true, false, false, m4});
    add({"solid.hole", "Hole", "Hole", "CREATE", "Drill a plain, counterbored or countersunk hole for a screw.", "H", {"drill", "screw hole", "bore", "countersink", "counterbore"}, true, false, false, m3});
    add({"solid.mirror", "Mirror", "Mirror", "CREATE", "Copy a body across a plane, e.g. to make a symmetric part.", "", {"symmetric", "flip copy"}, false, false, false, m3});
    add({"solid.pattern_rect", "Grid copy", "Rectangular Pattern", "CREATE", "Repeat a feature in rows and columns.", "", {"array", "grid", "repeat"}, false, false, false, m3});
    add({"solid.pattern_circ", "Ring copy", "Circular Pattern", "CREATE", "Repeat a feature around an axis.", "", {"array", "repeat around", "spokes"}, true, false, false, m3});
    add({"solid.presspull", "Press pull", "Press Pull", "MODIFY", "Drag a face to push or pull it; adapts to what you pick.", "Q", {"push", "pull", "drag face"}, false, false, false, m3});
    add({"solid.fillet", "Fillet", "Fillet", "MODIFY", "Round off edges so parts look finished and are comfortable to hold.", "F", {"round the edges", "round", "soften", "radius"}, true, false, true, m3});
    add({"solid.chamfer", "Chamfer", "Chamfer", "MODIFY", "Cut a flat bevel on edges; great for the bottom of 3D prints.", "", {"bevel", "angled edge", "elephant foot"}, true, false, true, m3});
    add({"solid.shell", "Shell", "Shell", "MODIFY", "Hollow out a solid, leaving walls of a set thickness.", "", {"hollow", "wall", "empty inside", "cup"}, true, false, true, m3});
    add({"solid.combine", "Combine", "Combine", "MODIFY", "Join, cut or intersect two bodies.", "", {"boolean", "merge", "subtract", "union"}, false, false, false, m4});
    add({"solid.split", "Split body", "Split Body", "MODIFY", "Cut a body into pieces with a plane, e.g. to fit a small printer.", "", {"slice", "cut in half", "divide"}, false, false, false, m4});
    add({"solid.move", "Move / copy", "Move/Copy", "MODIFY", "Move, rotate or copy bodies.", "M", {"translate", "rotate", "position", "duplicate"}, false, false, false, m4});
    add({"construct.offset_plane", "Offset plane", "Offset Plane", "CONSTRUCT", "A plane parallel to a face or plane at a set distance.", "", {"parallel plane", "work plane"}, true, false, true, m4});
    add({"construct.angle_plane", "Plane at angle", "Plane at Angle", "CONSTRUCT", "A plane tilted around an edge or axis.", "", {"tilted plane", "slanted"}, false, false, false, m4});
    add({"construct.midplane", "Midplane", "Midplane", "CONSTRUCT", "A plane halfway between two faces or planes.", "", {"middle plane", "center plane"}, false, false, false, m4});
    add({"construct.axis", "Axis", "Axis", "CONSTRUCT", "A reference axis for revolves and patterns.", "", {"center line", "rotation axis"}, false, false, false, m4});
    add({"construct.point", "Reference point", "Construction Point", "CONSTRUCT", "A reference point in 3D.", "", {"work point"}, false, false, false, m4});
    add({"inspect.measure", "Measure", "Measure", "INSPECT", "Read distances, angles, lengths and areas.", "I", {"distance", "how big", "ruler", "size"}, true, false, false, m6});
    add({"inspect.section", "Section view", "Section Analysis", "INSPECT", "Slice the view to look inside walls and holes.", "", {"cut view", "cross section", "inside"}, false, false, false, m6});
    add({"inspect.properties", "Properties", "Properties", "INSPECT", "Volume, area, weight and center of mass of a body.", "", {"volume", "weight", "mass", "filament"}, true, false, true, m6});
    add({"insert.step", "Import STEP…", "Insert STEP", "INSERT", "Bring in a solid from another CAD program.", "", {"stp", "import solid"}, false, false, true, m6});
    add({"insert.svg", "Import SVG…", "Insert SVG", "INSERT", "Bring vector artwork into a sketch, e.g. a logo.", "", {"logo", "vector", "artwork"}, false, false, false, m5});
    add({"insert.dxf", "Import DXF…", "Insert DXF", "INSERT", "Bring a 2D drawing or laser-cut file into a sketch.", "", {"laser", "2d drawing"}, false, false, false, m5});
    add({"modify.parameters", "Parameters", "Change Parameters", "MODIFY", "Name sizes like width or wall and use them in any field.", "", {"variables", "named values", "change size"}, true, false, true, m5});
    add({"help.tutorial", "Your first part (15 min)", "Tutorial", "HELP", "A guided tour that builds a phone stand with you.", "", {"learn", "tutorial", "getting started", "help"}, true, false, false, m7});
    // clang-format on

    // Usable from the desktop app as of milestone M1/M2/M3 (sketching, extrude, fillet,
    // shell, revolve, hole, patterns, mirror, parameters). Everything else above keeps its "coming in" note.
    static const std::set<std::string> inApp {
        "sketch.create", "sketch.finish", "sketch.line", "sketch.rectangle", "sketch.circle", "sketch.point",
        "sketch.arc", "sketch.slot", "sketch.polygon", "sketch.text", "sketch.trim",
        "sketch.dimension", "sketch.construction", "constraint.coincident", "constraint.midpoint", "constraint.fix",
        "constraint.parallel", "constraint.perpendicular", "constraint.horizontal", "constraint.tangent",
        "constraint.symmetric", "constraint.equal", "solid.extrude", "solid.fillet", "solid.chamfer", "solid.shell",
        "solid.revolve", "solid.hole", "solid.mirror", "solid.pattern_rect", "solid.pattern_circ", "modify.parameters",
        "inspect.measure", "inspect.properties", "file.export_3mf", "construct.offset_plane", "construct.angle_plane",
        "construct.midplane", "solid.combine", "solid.split", "solid.move",
        "inspect.section"};
    for (auto& c : commands_) {
        if (inApp.count(c.id)) {
            c.available = true;
            c.comingIn.clear();
        }
    }
}

const CommandRegistry& CommandRegistry::instance()
{
    static const CommandRegistry r;
    return r;
}

const CommandInfo* CommandRegistry::find(std::string_view id) const
{
    for (auto& c : commands_) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

std::vector<CommandRegistry::Match> CommandRegistry::search(std::string_view query,
                                                            bool includeUnavailable) const
{
    std::vector<Match> out;
    const std::string q = lower(query);
    const auto qw = words(query);
    for (auto& c : commands_) {
        if (!includeUnavailable && !c.available) {
            continue;
        }
        int score = 0;
        if (q.empty()) {
            score = c.essential ? 2 : 1;
        }
        else {
            const std::string label = lower(c.label), std = lower(c.standardName);
            if (label == q || std == q) {
                score = 100;
            }
            else if (label.rfind(q, 0) == 0 || std.rfind(q, 0) == 0) {
                score = 80;
            }
            for (auto& a : c.aliases) {
                const std::string al = lower(a);
                if (al == q) {
                    score = std::max(score, 75);
                }
                else if (al.find(q) != std::string::npos) {
                    score = std::max(score, 55);
                }
            }
            if (score == 0 && (label.find(q) != std::string::npos || std.find(q) != std::string::npos)) {
                score = 50;
            }
            if (score == 0 && !qw.empty()) {
                // word overlap across label, name, aliases and tooltip
                std::vector<std::string> hay = words(c.label + " " + c.standardName);
                for (auto& a : c.aliases) {
                    auto w = words(a);
                    hay.insert(hay.end(), w.begin(), w.end());
                }
                auto tip = words(c.tooltip);
                int hits = 0, tipHits = 0;
                for (auto& w : qw) {
                    if (w.size() < 2 || w == "the" || w == "a" || w == "an" || w == "of" || w == "to") {
                        continue;
                    }
                    for (auto& h : hay) {
                        if (h == w || (w.size() >= 4 && h.rfind(w, 0) == 0)) {
                            ++hits;
                            break;
                        }
                    }
                    for (auto& h : tip) {
                        if (h == w) {
                            ++tipHits;
                            break;
                        }
                    }
                }
                score = hits * 20 + tipHits * 5;
            }
            if (score > 0 && c.essential) {
                score += 1;
            }
            if (score > 0 && c.available) {
                score += 1;
            }
        }
        if (score > 0) {
            out.push_back({&c, score});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.score > b.score; });
    return out;
}

// ---------------------------------------------------------------- script runner

namespace
{

struct Args
{
    std::string verb;
    std::vector<std::string> positional;
    std::map<std::string, std::string> kv;

    bool has(const std::string& k) const
    {
        return kv.count(k) > 0;
    }
    std::string get(const std::string& k, const std::string& def = {}) const
    {
        auto it = kv.find(k);
        return it == kv.end() ? def : it->second;
    }
};

Result<Args> tokenize(std::string_view line)
{
    Args a;
    std::vector<std::string> toks;
    std::string cur;
    bool inQ = false, any = false;
    for (char c : line) {
        if (c == '"') {
            inQ = !inQ;
            any = true;
            continue;
        }
        if (!inQ && std::isspace(static_cast<unsigned char>(c))) {
            if (any) {
                toks.push_back(cur);
            }
            cur.clear();
            any = false;
            continue;
        }
        cur += c;
        any = true;
    }
    if (inQ) {
        return makeError("script.syntax", "A closing quote (\") is missing.");
    }
    if (any) {
        toks.push_back(cur);
    }
    if (toks.empty()) {
        return a;
    }
    a.verb = toks[0];
    for (size_t i = 1; i < toks.size(); ++i) {
        auto eq = toks[i].find('=');
        if (eq == std::string::npos || eq == 0) {
            a.positional.push_back(toks[i]);
        }
        else {
            a.kv[toks[i].substr(0, eq)] = toks[i].substr(eq + 1);
        }
    }
    return a;
}

std::vector<std::string> split(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(') {
            ++depth;
        }
        if (c == ')') {
            --depth;
        }
        if (c == sep && depth == 0) {
            out.push_back(cur);
            cur.clear();
        }
        else {
            cur += c;
        }
    }
    out.push_back(cur);
    for (auto& o : out) {
        while (!o.empty() && std::isspace(static_cast<unsigned char>(o.front()))) {
            o.erase(o.begin());
        }
        while (!o.empty() && std::isspace(static_cast<unsigned char>(o.back()))) {
            o.pop_back();
        }
    }
    return out;
}

Result<std::pair<std::string, std::string>> parsePairExpr(const std::string& s)
{
    auto parts = split(s, ',');
    if (parts.size() != 2) {
        return makeError("script.args", "Expected two values separated by a comma, got '" + s + "'.");
    }
    return std::pair {parts[0], parts[1]};
}

Feature* findFeature(Model& m, const std::string& id)
{
    return Document::findFeature(m, id);
}

void insertFeature(Model& m, Feature f)
{
    Document::insertAtMarker(m, std::move(f));
}

}  // namespace

nlohmann::json reportJson(const Document& doc)
{
    nlohmann::json bodies = nlohmann::json::object();
    for (auto& [id, b] : doc.state().bodies) {
        MassProperties p = massProperties(b->shape());
        bodies[id] = {{"volume", p.volume},
                      {"area", p.area},
                      {"bbox", {p.xmin, p.ymin, p.zmin, p.xmax, p.ymax, p.zmax}},
                      {"faces", p.faces},
                      {"edges", p.edges},
                      {"solids", p.solids},
                      {"valid", p.valid}};
    }
    nlohmann::json feats = nlohmann::json::array();
    const auto& fs = doc.model().features;
    for (size_t i = 0; i < fs.size(); ++i) {
        const auto& st = doc.status(int(i));
        const char* lvl = st.level == StatusLevel::Ok        ? "ok"
                        : st.level == StatusLevel::Warning   ? "warning"
                        : st.level == StatusLevel::Error     ? "error"
                        : st.level == StatusLevel::Suppressed ? "suppressed"
                                                             : "rolled_back";
        nlohmann::json jf {{"id", fs[i].id}, {"kind", toString(fs[i].kind)}, {"status", lvl}};
        if (!st.message.empty()) {
            jf["message"] = st.message;
        }
        if (!st.suggestions.empty()) {
            jf["suggestions"] = st.suggestions;
        }
        feats.push_back(jf);
    }
    return {{"bodies", bodies}, {"features", feats}};
}

Result<bool> ScriptRunner::runText(std::string_view text)
{
    std::istringstream in {std::string(text)};
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
        ++n;
        auto r = runLine(line);
        if (!r) {
            Error e = r.error();
            e.message = "Line " + std::to_string(n) + ": " + e.message;
            return e;
        }
    }
    return true;
}

Result<bool> ScriptRunner::runFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return makeError("script.open", "The script file '" + path + "' could not be opened.");
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return runText(ss.str());
}

Result<bool> ScriptRunner::runLine(std::string_view rawLine)
{
    std::string line(rawLine);
    if (auto h = line.find('#'); h != std::string::npos) {
        line.erase(h);
    }
    auto ta = tokenize(line);
    if (!ta) {
        return ta.error();
    }
    Args& a = ta.value();
    if (a.verb.empty()) {
        return true;
    }
    Document& doc = doc_;
    // In a design that is not in millimetres, plain numbers in size and position arguments
    // are in the design's unit ("rect w=2" in an inch design is 2 in). They are stored with
    // the unit written out, so changing the design's unit later never rescales anything.
    if (doc.units() != LengthUnit::Millimetre) {
        static const std::set<std::string> lengthKeys {
            "w", "h", "d", "r", "width", "height", "distance", "depth", "diameter", "thickness", "radius",
            "offset", "dx", "dy", "dz", "spacing1", "spacing2", "cbore_diameter", "cbore_depth", "csink_diameter",
            "center", "corner", "points", "c1", "c2", "p1", "p2", "at"};
        for (auto& [k, v] : a.kv) {
            if (!lengthKeys.count(k)) {
                continue;
            }
            std::string out, tok;
            auto flush = [&] {
                std::string t = tok;
                const auto b = t.find_first_not_of(' '), e = t.find_last_not_of(' ');
                t = b == std::string::npos ? "" : t.substr(b, e - b + 1);
                out += t.empty() ? tok : withLengthUnit(t, doc.units(), doc.lookup());
                tok.clear();
            };
            for (char c : v) {
                if (c == ',' || c == ';') {
                    flush();
                    out += c;
                }
                else {
                    tok += c;
                }
            }
            flush();
            v = out;
        }
    }

    // After a feature-creating edit: fail when the new/edited feature errors (strict mode).
    auto checkFeature = [&](const std::string& id) -> Result<bool> {
        last_ = id;
        int i = doc.featureIndex(id);
        if (strict && i >= 0 && doc.status(i).level == StatusLevel::Error) {
            const auto& st = doc.status(i);
            return makeError("script.feature_failed", id + ": " + st.message, st.suggestions, st.detail);
        }
        return true;
    };

    // Edits the geometry JSON of a sketch feature.
    auto editSketch = [&](const std::string& label, const std::string& sid,
                          const std::function<Result<bool>(Sketch&, const Document&)>& fn) -> Result<bool> {
        auto r = doc.transact(label, [&](Model& m) -> Result<bool> {
            Feature* f = findFeature(m, sid);
            if (!f || f->kind != FeatureKind::Sketch) {
                return makeError("script.args", "There is no sketch called '" + sid + "'.");
            }
            auto sk = Sketch::fromJson(f->params.value(
                "geometry",
                nlohmann::json {{"entities", nlohmann::json::array()}, {"constraints", nlohmann::json::array()}}));
            if (!sk) {
                return sk.error();
            }
            auto ok = fn(sk.value(), doc);
            if (!ok) {
                return ok;
            }
            f->params["geometry"] = sk->toJson();
            return true;
        });
        if (!r) {
            return r;
        }
        return checkFeature(sid);
    };
    auto len = [&](const std::string& e) { return doc.evalLength(e); };

    // ---- parameters
    if (a.verb == "units") {
        // units mm|cm|m|in|ft : how this design shows sizes and reads plain numbers
        auto u = lengthUnitFromString(a.positional.empty() ? "" : a.positional[0]);
        if (!u) {
            return makeError("script.args", "Use: units mm|cm|m|in|ft");
        }
        return doc.transact("Units", [&](Model& m) -> Result<bool> {
            m.units = *u;
            return true;
        });
    }
    if (a.verb == "param") {
        // param name = expr [kind=angle|number] [comment="..."]
        std::string rest = line.substr(line.find("param") + 5);
        auto eq = rest.find('=');
        if (eq == std::string::npos) {
            return makeError("script.args", "Use: param name = value");
        }
        auto name = split(rest.substr(0, eq), '\x01')[0];  // trimmed
        std::string expr = rest.substr(eq + 1);
        std::optional<ParamKind> kind;
        for (auto opt : {std::string(" kind=angle"), std::string(" kind=number"), std::string(" kind=length")}) {
            if (auto p = expr.find(opt); p != std::string::npos) {
                kind = opt == " kind=angle" ? ParamKind::Angle : opt == " kind=number" ? ParamKind::Number : ParamKind::Length;
                expr.erase(p, opt.size());
            }
        }
        expr = split(expr, '\x01')[0];  // trim
        if (!isValidParameterName(name)) {
            return makeError("param.name", "'" + name + "' can't be used as a parameter name.",
                             {"Use letters, digits and _, starting with a letter, e.g. wall_t."});
        }
        auto r = doc.transact("Set " + name, [&](Model& m) -> Result<bool> {
            Parameter* target = nullptr;
            for (auto& p : m.parameters) {
                if (p.name == name) {
                    target = &p;
                }
            }
            if (!target) {
                m.parameters.push_back({name, expr, ParamKind::Length, {}});
                target = &m.parameters.back();
                // A new parameter takes its kind from the value typed: "75 deg" is an angle.
                ParamLookup inModel = [&](std::string_view n) -> std::optional<Quantity> {
                    auto q = Document::evalParamIn(m, std::string(n));
                    return q ? std::optional<Quantity>(q.value()) : std::nullopt;
                };
                if (auto q = evaluate(expr, inModel); q && q->dim == Dim::ang()) {
                    target->kind = ParamKind::Angle;
                }
            }
            target->expr = expr;
            if (kind) {
                target->kind = *kind;  // an existing parameter keeps its kind unless told otherwise
            }
            if (target->kind == ParamKind::Length && m.units != LengthUnit::Millimetre) {
                target->expr = withLengthUnit(expr, m.units);  // "2" in an inch design is 2 in
            }
            // Reject the change (nothing is committed) if the value does not work.
            auto v = Document::evalParamIn(m, name);
            if (!v) {
                return v.error();
            }
            return true;
        });
        if (!r) {
            return r;
        }
        // a parameter change may break features
        for (size_t i = 0; i < doc.model().features.size(); ++i) {
            if (strict && doc.status(int(i)).level == StatusLevel::Error) {
                return makeError("script.feature_failed",
                                 doc.model().features[i].id + ": " + doc.status(int(i)).message,
                                 doc.status(int(i)).suggestions);
            }
        }
        return true;
    }

    // ---- sketches
    if (a.verb == "sketch") {
        std::string id;
        auto r = doc.transact("New sketch", [&](Model& m) -> Result<bool> {
            Feature f;
            f.id = a.get("id", Document::newId(m, "s"));
            if (findFeature(m, f.id)) {
                return makeError("script.args", "The id '" + f.id + "' is already used.");
            }
            f.name = a.get("name", "Sketch " + f.id.substr(1));
            f.kind = FeatureKind::Sketch;
            if (a.has("on")) {
                // on=<face name> : sketch on a named face of a body
                const std::string fname = a.get("on");
                const ModelState& st = doc.state();
                for (auto& [bid, body] : st.bodies) {
                    auto hits = body->findFaces(fname);
                    if (hits.size() == 1) {
                        f.params["face"] = body->faceRef(bid, hits[0]).toJson();
                    }
                }
                if (!f.params.contains("face")) {
                    return makeError("script.args", "There is no single face called '" + fname + "'.");
                }
            }
            else {
                f.params["plane"] = a.get("plane", "XY");
                if (a.has("offset")) {
                    f.params["offset"] = a.get("offset");
                }
            }
            f.params["geometry"] = Sketch().toJson();
            id = f.id;
            insertFeature(m, std::move(f));
            return true;
        });
        if (!r) {
            return r;
        }
        return checkFeature(id);
    }
    if (a.verb == "rect") {
        const std::string sid = a.get("sketch", last_);
        return editSketch("Rectangle", sid, [&](Sketch& s, const Document&) -> Result<bool> {
            const std::string we = a.get("w"), he = a.get("h");
            auto w = len(we), h = len(he);
            if (!w) {
                return w.error();
            }
            if (!h) {
                return h.error();
            }
            bool centered = a.has("center");
            auto pe = parsePairExpr(a.get(centered ? "center" : "corner", "0,0"));
            if (!pe) {
                return pe.error();
            }
            auto px = len(pe->first), py = len(pe->second);
            if (!px || !py) {
                return !px ? px.error() : py.error();
            }
            double x0 = centered ? px.value() - w.value() / 2 : px.value();
            double y0 = centered ? py.value() - h.value() / 2 : py.value();
            int A = s.addPoint(x0, y0, true), B = s.addPoint(x0 + w.value(), y0, true),
                C = s.addPoint(x0 + w.value(), y0 + h.value(), true), D = s.addPoint(x0, y0 + h.value(), true);
            int ab = s.addLine(A, B), bc = s.addLine(B, C), cd = s.addLine(C, D), da = s.addLine(D, A);
            s.addConstraint(ConstraintType::Horizontal, ab);
            s.addConstraint(ConstraintType::Horizontal, cd);
            s.addConstraint(ConstraintType::Vertical, bc);
            s.addConstraint(ConstraintType::Vertical, da);
            s.addConstraint(ConstraintType::Distance, ab, 0, 0, we);
            s.addConstraint(ConstraintType::Distance, bc, 0, 0, he);
            if (centered) {
                int diag = s.addLine(A, C);
                s.setConstruction(diag, true);
                int M = s.addPoint(px.value(), py.value());
                s.addConstraint(ConstraintType::Midpoint, M, diag);
                s.addConstraint(ConstraintType::DistanceX, M, 0, 0, pe->first);
                s.addConstraint(ConstraintType::DistanceY, M, 0, 0, pe->second);
            }
            else {
                s.addConstraint(ConstraintType::DistanceX, A, 0, 0, pe->first);
                s.addConstraint(ConstraintType::DistanceY, A, 0, 0, pe->second);
            }
            return true;
        });
    }
    if (a.verb == "circle") {
        const std::string sid = a.get("sketch", last_);
        return editSketch("Circle", sid, [&](Sketch& s, const Document&) -> Result<bool> {
            const std::string de = a.get("d");
            auto d = len(de);
            if (!d) {
                return d.error();
            }
            auto pe = parsePairExpr(a.get("center", "0,0"));
            if (!pe) {
                return pe.error();
            }
            auto px = len(pe->first), py = len(pe->second);
            if (!px || !py) {
                return !px ? px.error() : py.error();
            }
            int c = s.addCircle(px.value(), py.value(), d.value() / 2);
            int cp = s.entity(c)->center;
            s.addConstraint(ConstraintType::Diameter, c, 0, 0, de);
            s.addConstraint(ConstraintType::DistanceX, cp, 0, 0, pe->first);
            s.addConstraint(ConstraintType::DistanceY, cp, 0, 0, pe->second);
            return true;
        });
    }
    if (a.verb == "polyline") {
        // polyline points="x1,y1; x2,y2; ..." [closed=yes] [dims=yes]
        const std::string sid = a.get("sketch", last_);
        return editSketch("Lines", sid, [&](Sketch& s, const Document&) -> Result<bool> {
            auto pts = split(a.get("points"), ';');
            if (pts.size() < 2) {
                return makeError("script.args", "A polyline needs at least two points.");
            }
            std::vector<int> ids;
            for (auto& p : pts) {
                auto pe = parsePairExpr(p);
                if (!pe) {
                    return pe.error();
                }
                auto px = len(pe->first), py = len(pe->second);
                if (!px || !py) {
                    return !px ? px.error() : py.error();
                }
                int id = s.addPoint(px.value(), py.value(), true);
                if (a.get("dims", "yes") == "yes") {
                    s.addConstraint(ConstraintType::DistanceX, id, 0, 0, pe->first);
                    s.addConstraint(ConstraintType::DistanceY, id, 0, 0, pe->second);
                }
                ids.push_back(id);
            }
            for (size_t i = 0; i + 1 < ids.size(); ++i) {
                s.addLine(ids[i], ids[i + 1]);
            }
            if (a.get("closed", "yes") == "yes" && ids.size() > 2) {
                s.addLine(ids.back(), ids.front());
            }
            return true;
        });
    }
    if (a.verb == "slot") {
        // slot c1=x,y c2=x,y width=w  (center-to-center slot)
        const std::string sid = a.get("sketch", last_);
        return editSketch("Slot", sid, [&](Sketch& s, const Document&) -> Result<bool> {
            auto c1 = parsePairExpr(a.get("c1")), c2 = parsePairExpr(a.get("c2"));
            if (!c1 || !c2) {
                return !c1 ? c1.error() : c2.error();
            }
            const std::string we = a.get("width");
            auto w = len(we);
            auto x1 = len(c1->first), y1 = len(c1->second), x2 = len(c2->first), y2 = len(c2->second);
            if (!w || !x1 || !y1 || !x2 || !y2) {
                return makeError("script.args", "Slot needs c1=x,y c2=x,y width=w with valid values.");
            }
            if (std::hypot(x2.value() - x1.value(), y2.value() - y1.value()) < 1e-9) {
                return makeError("script.args", "The two slot centers must be different points.");
            }
            const auto ids = s.addSlot(x1.value(), y1.value(), x2.value(), y2.value(), w.value() / 2);
            const int a1 = ids.arc1;
            const SketchEntity A1 = *s.entity(ids.arc1), A2 = *s.entity(ids.arc2);
            s.addConstraint(ConstraintType::Radius, a1, 0, 0, "(" + we + ") / 2");
            s.addConstraint(ConstraintType::DistanceX, A1.center, 0, 0, c2->first);
            s.addConstraint(ConstraintType::DistanceY, A1.center, 0, 0, c2->second);
            s.addConstraint(ConstraintType::DistanceX, A2.center, 0, 0, c1->first);
            s.addConstraint(ConstraintType::DistanceY, A2.center, 0, 0, c1->second);
            return true;
        });
    }
    if (a.verb == "line" || a.verb == "point" || a.verb == "arc") {
        // line p1=x,y p2=x,y [construction=yes] [name=axis]
        // point at=x,y [name=..]                       (hole centres)
        // arc center=x,y r=<len> from=<angle> to=<angle> [chord=yes] [name=..]   (counter-clockwise)
        const std::string sid = a.get("sketch", last_);
        int made = 0;
        auto res = editSketch(a.verb == "line" ? "Line" : a.verb == "point" ? "Point" : "Arc", sid,
                              [&](Sketch& s, const Document&) -> Result<bool> {
            auto pos = [&](const std::string& key) -> Result<std::pair<std::pair<double, double>, std::pair<std::string, std::string>>> {
                auto pe = parsePairExpr(a.get(key, "0,0"));
                if (!pe) {
                    return pe.error();
                }
                auto x = len(pe->first), y = len(pe->second);
                if (!x || !y) {
                    return !x ? x.error() : y.error();
                }
                return std::pair {std::pair {x.value(), y.value()}, *pe};
            };
            auto fixPoint = [&](int pid, const std::pair<std::string, std::string>& e) {
                s.addConstraint(ConstraintType::DistanceX, pid, 0, 0, e.first);
                s.addConstraint(ConstraintType::DistanceY, pid, 0, 0, e.second);
            };
            if (a.verb == "point") {
                auto at = pos("at");
                if (!at) {
                    return at.error();
                }
                made = s.addPoint(at->first.first, at->first.second);
                fixPoint(made, at->second);
            }
            else if (a.verb == "line") {
                auto p1 = pos("p1"), p2 = pos("p2");
                if (!p1 || !p2) {
                    return !p1 ? p1.error() : p2.error();
                }
                made = s.addLine(p1->first.first, p1->first.second, p2->first.first, p2->first.second);
                fixPoint(s.entity(made)->p1, p1->second);
                fixPoint(s.entity(made)->p2, p2->second);
            }
            else {
                auto c = pos("center");
                const std::string re = a.get("r");
                auto r = len(re);
                auto a0 = doc.evalAngle(a.get("from", "0 deg")), a1 = doc.evalAngle(a.get("to", "90 deg"));
                if (!c || !r || !a0 || !a1) {
                    return makeError("script.args", "Arc needs center=x,y r=<len> from=<angle> to=<angle> with valid values.");
                }
                made = s.addArc(c->first.first, c->first.second, r.value(), a0.value(), a1.value());
                const SketchEntity A = *s.entity(made);
                fixPoint(A.center, c->second);
                s.addConstraint(ConstraintType::Radius, made, 0, 0, re);
                // the end angles stay free (the solver keeps them where they are)
                if (a.get("chord", "no") == "yes") {
                    s.addLine(A.p1, A.p2);  // closes the arc into a "D"
                }
            }
            if (a.get("construction", "no") == "yes") {
                s.setConstruction(made, true);
            }
            return true;
        });
        if (res && a.has("name")) {
            names_[sid + "." + a.get("name")] = made;
        }
        return res;
    }
    if (a.verb == "text") {
        // text center=x,y height=<len> text="ABC" [font=<file or name>] [angle=<angle>]
        const std::string sid = a.get("sketch", last_);
        return editSketch("Text", sid, [&](Sketch& s, const Document&) -> Result<bool> {
            auto pe = parsePairExpr(a.get("center", "0,0"));
            if (!pe) {
                return pe.error();
            }
            auto px = len(pe->first), py = len(pe->second);
            const std::string he = a.get("height", "5 mm");
            auto h = len(he);
            auto ang = doc.evalAngle(a.get("angle", "0 deg"));
            if (!px || !py || !h || !ang) {
                return makeError("script.args", "Text needs center=x,y height=<len> text=\"…\" with valid values.");
            }
            if (a.get("text").empty()) {
                return makeError("script.args", "Say what the text reads: text=\"…\".");
            }
            const int id = s.addText(px.value(), py.value(), a.get("text"), he, h.value(), a.get("font"), ang.value());
            const int anchor = s.texts().at(id).anchor;
            s.addConstraint(ConstraintType::DistanceX, anchor, 0, 0, pe->first);
            s.addConstraint(ConstraintType::DistanceY, anchor, 0, 0, pe->second);
            return true;
        });
    }
    if (a.verb == "trim") {
        // trim [sketch=s1] at=x,y : remove the piece of the curve nearest (x, y) between its crossings
        const std::string sid = a.get("sketch", last_);
        return editSketch("Trim", sid, [&](Sketch& s, const Document&) -> Result<bool> {
            auto pe = parsePairExpr(a.get("at", "0,0"));
            if (!pe) {
                return pe.error();
            }
            auto px = len(pe->first), py = len(pe->second);
            if (!px || !py) {
                return !px ? px.error() : py.error();
            }
            auto near = s.curveNear(px.value(), py.value());
            if (!near || near->second > 1.0) {
                return makeError("script.args", "There is no curve near that point to trim.");
            }
            s.trim(near->first, px.value(), py.value());
            return true;
        });
    }
    if (a.verb == "polygon") {
        // polygon center=x,y d=<len> sides=<n>   (d = diameter of the circle through the corners)
        const std::string sid = a.get("sketch", last_);
        return editSketch("Polygon", sid, [&](Sketch& s, const Document&) -> Result<bool> {
            auto pe = parsePairExpr(a.get("center", "0,0"));
            if (!pe) {
                return pe.error();
            }
            const std::string de = a.get("d");
            auto d = len(de);
            auto px = len(pe->first), py = len(pe->second);
            auto n = evaluate(a.get("sides", "6"), doc.lookup());
            if (!d || !px || !py) {
                return makeError("script.args", "Polygon needs center=x,y d=<len> with valid values.");
            }
            if (!n || n->value < 3 || n->value > 64) {
                return makeError("script.args", "A polygon needs 3 to 64 sides.");
            }
            const auto ids = s.addPolygon(px.value(), py.value(), d.value() / 2, int(std::lround(n->value)), std::numbers::pi / 2);
            const int c = s.entity(ids.circle)->center;
            s.addConstraint(ConstraintType::Diameter, ids.circle, 0, 0, de);
            s.addConstraint(ConstraintType::DistanceX, c, 0, 0, pe->first);
            s.addConstraint(ConstraintType::DistanceY, c, 0, 0, pe->second);
            // first corner straight above the centre: fixes the rotation
            s.addConstraint(ConstraintType::Vertical, c, s.entity(ids.sides[0])->p1);
            return true;
        });
    }
    if (a.verb == "constrain") {
        // constrain sketch=s1 type=parallel a=3 b=4 [c=..] [value="..."]
        const std::string sid = a.get("sketch", last_);
        return editSketch("Constraint", sid, [&](Sketch& s, const Document&) -> Result<bool> {
            auto t = constraintTypeFromString(a.get("type"));
            if (!t) {
                return makeError("script.args", "Unknown constraint type '" + a.get("type") + "'.");
            }
            s.addConstraint(*t, std::stoi(a.get("a", "0")), std::stoi(a.get("b", "0")),
                            std::stoi(a.get("c", "0")), a.get("value"));
            return true;
        });
    }

    // ---- solid features
    if (a.verb == "extrude") {
        std::string id;
        auto r = doc.transact("Extrude", [&](Model& m) -> Result<bool> {
            Feature f;
            f.id = a.get("id", Document::newId(m, "e"));
            if (findFeature(m, f.id)) {
                return makeError("script.args", "The id '" + f.id + "' is already used.");
            }
            f.name = a.get("name", "Extrude " + f.id.substr(1));
            f.kind = FeatureKind::Extrude;
            f.params["sketch"] = a.get("sketch");
            if (a.has("profiles") && a.get("profiles") != "all") {
                f.params["profiles"] = split(a.get("profiles"), ';');
            }
            else {
                f.params["profiles"] = "all";
            }
            f.params["extent"] = a.get("extent", "distance");
            f.params["distance"] = a.get("distance", "10 mm");
            f.params["direction"] = a.get("direction", "one");
            f.params["op"] = a.get("op", "new");
            if (a.has("target")) {
                f.params["target"] = a.get("target");
            }
            id = f.id;
            insertFeature(m, std::move(f));
            return true;
        });
        if (!r) {
            return r;
        }
        return checkFeature(id);
    }
    if (a.verb == "fillet" || a.verb == "chamfer") {
        // fillet edges="<sel>;<sel>" radius=2mm [body=e1]
        // chamfer edges="<sel>;<sel>" distance=1mm [body=e1]
        const bool isChamfer = a.verb == "chamfer";
        // selectors: all | face:<face name> | feature:<feature id> | name:<edge name>
        const ModelState& st = doc.state();
        std::string bodyId = a.get("body");
        if (bodyId.empty()) {
            if (st.bodies.size() != 1) {
                return makeError("script.args", "Say which body to fillet with body=<id>.");
            }
            bodyId = st.bodies.begin()->first;
        }
        auto bit = st.bodies.find(bodyId);
        if (bit == st.bodies.end()) {
            return makeError("script.args", "There is no body called '" + bodyId + "'.");
        }
        const NamedShape& body = *bit->second;
        std::set<int> picked;
        for (auto& sel : split(a.get("edges", "all"), ';')) {
            if (sel == "all") {
                for (int i = 0; i < body.edgeCount(); ++i) {
                    picked.insert(i);
                }
            }
            else if (sel.rfind("face:", 0) == 0) {
                const std::string fname = sel.substr(5);
                std::vector<int> hits = body.findFaces(fname);
                for (int fi = 0; fi < body.faceCount() && hits.empty(); ++fi) {
                    // also accept the pieces of a face split by a later step: name~0, name~1
                    if (body.faceName(fi).rfind(fname + "~", 0) == 0) {
                        hits.push_back(fi);
                    }
                }
                for (int fi = 0; fi < body.faceCount(); ++fi) {
                    if (!hits.empty() && body.faceName(fi).rfind(fname + "~", 0) == 0
                        && std::find(hits.begin(), hits.end(), fi) == hits.end()) {
                        hits.push_back(fi);
                    }
                }
                if (hits.empty()) {
                    return makeError("script.args", "No face called '" + sel.substr(5) + "'.");
                }
                for (int fi : hits) {
                    for (int e : body.edgesOfFace(fi)) {
                        picked.insert(e);
                    }
                }
            }
            else if (sel.rfind("feature:", 0) == 0) {
                const std::string pre = sel.substr(8) + "/";
                for (int i = 0; i < body.edgeCount(); ++i) {
                    for (int fi : body.facesOfEdge(i)) {
                        if (body.faceName(fi).rfind(pre, 0) == 0) {
                            picked.insert(i);
                        }
                    }
                }
            }
            else if (sel.rfind("name:", 0) == 0) {
                auto hits = body.findEdges(sel.substr(5));
                if (hits.size() != 1) {
                    return makeError("script.args", "No single edge called '" + sel.substr(5) + "'.");
                }
                picked.insert(hits[0]);
            }
            else {
                return makeError("script.args", "Unknown edge selector '" + sel + "'.");
            }
        }
        std::string id;
        auto r = doc.transact(isChamfer ? "Chamfer" : "Fillet", [&](Model& m) -> Result<bool> {
            Feature f;
            f.id = a.get("id", Document::newId(m, isChamfer ? "c" : "f"));
            if (findFeature(m, f.id)) {
                return makeError("script.args", "The id '" + f.id + "' is already used.");
            }
            f.name = a.get("name", (isChamfer ? "Chamfer " : "Fillet ") + f.id.substr(1));
            f.kind = isChamfer ? FeatureKind::Chamfer : FeatureKind::Fillet;
            if (isChamfer) {
                f.params["distance"] = a.get("distance", "1 mm");
            }
            else {
                f.params["radius"] = a.get("radius", "1 mm");
            }
            nlohmann::json refs = nlohmann::json::array();
            for (int e : picked) {
                refs.push_back(body.edgeRef(bodyId, e).toJson());
            }
            f.params["edges"] = refs;
            id = f.id;
            insertFeature(m, std::move(f));
            return true;
        });
        if (!r) {
            return r;
        }
        return checkFeature(id);
    }
    if (a.verb == "shell") {
        // shell faces="face:<name>;face:<name>" thickness=2mm [body=e1]   (faces=none: closed hollow)
        const ModelState& st = doc.state();
        std::string bodyId = a.get("body");
        if (bodyId.empty()) {
            if (st.bodies.size() != 1) {
                return makeError("script.args", "Say which body to shell with body=<id>.");
            }
            bodyId = st.bodies.begin()->first;
        }
        auto bit = st.bodies.find(bodyId);
        if (bit == st.bodies.end()) {
            return makeError("script.args", "There is no body called '" + bodyId + "'.");
        }
        const NamedShape& body = *bit->second;
        nlohmann::json refs = nlohmann::json::array();
        const std::string sel = a.get("faces", "none");
        if (sel != "none") {
            for (auto& one : split(sel, ';')) {
                if (one.rfind("face:", 0) != 0) {
                    return makeError("script.args", "Shell faces are given as face:<face name>.");
                }
                auto hits = body.findFaces(one.substr(5));
                if (hits.size() != 1) {
                    return makeError("script.args", "No single face called '" + one.substr(5) + "'.");
                }
                refs.push_back(body.faceRef(bodyId, hits[0]).toJson());
            }
        }
        std::string id;
        auto r = doc.transact("Shell", [&](Model& m) -> Result<bool> {
            Feature f;
            f.id = a.get("id", Document::newId(m, "sh"));
            if (findFeature(m, f.id)) {
                return makeError("script.args", "The id '" + f.id + "' is already used.");
            }
            f.name = a.get("name", "Shell " + f.id.substr(2));
            f.kind = FeatureKind::Shell;
            f.params = {{"faces", refs}, {"thickness", a.get("thickness", "2 mm")}, {"body", bodyId}};
            id = f.id;
            insertFeature(m, std::move(f));
            return true;
        });
        if (!r) {
            return r;
        }
        return checkFeature(id);
    }
    if (a.verb == "combine" || a.verb == "split" || a.verb == "move") {
        // combine target=<body> tools="<body>;<body>" [op=join|cut|intersect] [keep=yes]
        // split body=<body> plane=XY|XZ|YZ|<plane id>|face:<name>
        // move bodies="<body>;…" [dx= dy= dz=] [axis=X|Y|Z angle=<angle>] [copy=yes]
        nlohmann::json params = nlohmann::json::object();
        FeatureKind kind = FeatureKind::Combine;
        std::string prefix = "cb";
        if (a.verb == "combine") {
            params = {{"target", a.get("target")}, {"tools", split(a.get("tools"), ';')}, {"op", a.get("op", "join")},
                      {"keep_tools", a.get("keep", "no") == "yes"}};
        }
        else if (a.verb == "split") {
            kind = FeatureKind::Split;
            prefix = "sp";
            const std::string pl = a.get("plane", "YZ");
            nlohmann::json ref = pl;
            if (pl.rfind("face:", 0) == 0) {
                ref = nullptr;
                for (auto& [bid, body] : doc.state().bodies) {
                    auto hits = body->findFaces(pl.substr(5));
                    if (hits.size() == 1) {
                        ref = {{"face", body->faceRef(bid, hits[0]).toJson()}};
                    }
                }
                if (ref.is_null()) {
                    return makeError("script.args", "There is no single face called '" + pl.substr(5) + "'.");
                }
            }
            params = {{"body", a.get("body")}, {"plane", ref}};
        }
        else {
            kind = FeatureKind::Move;
            prefix = "mv";
            params = {{"bodies", split(a.get("bodies"), ';')}, {"copy", a.get("copy", "no") == "yes"}};
            for (const char* key : {"dx", "dy", "dz", "axis", "angle"}) {
                if (a.has(key)) {
                    params[key] = a.get(key);
                }
            }
        }
        std::string label = a.verb;
        label[0] = char(std::toupper(label[0]));
        std::string id;
        auto r = doc.transact(label, [&](Model& m) -> Result<bool> {
            Feature f;
            f.id = a.get("id", Document::newId(m, prefix));
            if (findFeature(m, f.id)) {
                return makeError("script.args", "The id '" + f.id + "' is already used.");
            }
            f.name = a.get("name", label + " " + f.id.substr(prefix.size()));
            f.kind = kind;
            f.params = params;
            id = f.id;
            insertFeature(m, std::move(f));
            return true;
        });
        if (!r) {
            return r;
        }
        return checkFeature(id);
    }
    if (a.verb == "plane") {
        // plane id=pl1 [kind=offset|angle|midplane] base=XY|<plane id>|face:<name>
        //       [distance=<len>] [axis=X|Y|Z|edge:<name> angle=<angle>] [base2=…]
        const ModelState& st = doc.state();
        auto ref = [&](const std::string& v) -> Result<nlohmann::json> {
            for (const char* pre : {"face:", "edge:"}) {
                if (v.rfind(pre, 0) != 0) {
                    continue;
                }
                const bool face = pre[0] == 'f';
                const std::string name = v.substr(5);
                for (auto& [bid, body] : st.bodies) {
                    auto hits = face ? body->findFaces(name) : body->findEdges(name);
                    if (hits.size() == 1) {
                        return nlohmann::json {{face ? "face" : "edge",
                                                (face ? body->faceRef(bid, hits[0]) : body->edgeRef(bid, hits[0])).toJson()}};
                    }
                }
                return makeError("script.args", "There is no single " + std::string(face ? "face" : "edge") + " called '" + name + "'.");
            }
            return nlohmann::json(v);
        };
        nlohmann::json params {{"kind", a.get("kind", "offset")}};
        for (const char* key : {"base", "base2", "axis"}) {
            if (a.has(key)) {
                auto r = ref(a.get(key));
                if (!r) {
                    return r.error();
                }
                params[key] = r.value();
            }
        }
        for (const char* key : {"distance", "angle"}) {
            if (a.has(key)) {
                params[key] = a.get(key);
            }
        }
        std::string id;
        auto r = doc.transact("Construction plane", [&](Model& m) -> Result<bool> {
            Feature f;
            f.id = a.get("id", Document::newId(m, "pl"));
            if (findFeature(m, f.id)) {
                return makeError("script.args", "The id '" + f.id + "' is already used.");
            }
            f.name = a.get("name", "Plane " + f.id.substr(2));
            f.kind = FeatureKind::Plane;
            f.params = params;
            id = f.id;
            insertFeature(m, std::move(f));
            return true;
        });
        if (!r) {
            return r;
        }
        return checkFeature(id);
    }
    if (a.verb == "revolve" || a.verb == "hole" || a.verb == "pattern" || a.verb == "mirror") {
        // revolve sketch=s1 axis=X|Y|Z|line:<name> [angle=360deg] [op=new|join|cut|intersect] [profiles=…]
        // hole sketch=s2 diameter=3.4mm (depth=10mm | extent=all) [type=simple|counterbore|countersink]
        //      [cbore_diameter= cbore_depth= | csink_diameter= csink_angle=]
        // pattern feature=e2 kind=circular axis=Z count=12 [angle=360deg]
        // pattern feature=h1 kind=rectangular dir1=X count1=3 spacing1=20mm [dir2=Y count2=2 spacing2=15mm]
        // mirror [body=e1] plane=YZ|XZ|XY [op=join|new]
        static const std::map<std::string, std::pair<FeatureKind, std::string>> kinds {
            {"revolve", {FeatureKind::Revolve, "r"}}, {"hole", {FeatureKind::Hole, "h"}},
            {"pattern", {FeatureKind::Pattern, "p"}}, {"mirror", {FeatureKind::Mirror, "m"}}};
        const auto& [kind, prefix] = kinds.at(a.verb);
        nlohmann::json params = nlohmann::json::object();
        for (auto& [k, v] : a.kv) {
            if (k != "id" && k != "name") {
                params[k] = v;
            }
        }
        if (a.verb == "revolve") {
            const std::string ax = a.get("axis", "Z");
            if (ax.rfind("line:", 0) == 0) {
                auto it = names_.find(a.get("sketch") + "." + ax.substr(5));
                if (it == names_.end()) {
                    return makeError("script.args", "No line called '" + ax.substr(5) + "' in sketch '" + a.get("sketch") + "'.",
                                     {"Name the axis line: line … name=axis"});
                }
                params["axis"] = {{"line", it->second}};
            }
            else {
                params["axis"] = {{"origin", ax}};
            }
            params["profiles"] = a.has("profiles") && a.get("profiles") != "all" ? nlohmann::json(split(a.get("profiles"), ';'))
                                                                                  : nlohmann::json("all");
        }
        std::string label = a.verb;
        label[0] = char(std::toupper(label[0]));
        std::string id;
        auto r = doc.transact(label, [&](Model& m) -> Result<bool> {
            Feature f;
            f.id = a.get("id", Document::newId(m, prefix));
            if (findFeature(m, f.id)) {
                return makeError("script.args", "The id '" + f.id + "' is already used.");
            }
            f.name = a.get("name", label + " " + f.id.substr(prefix.size()));
            f.kind = kind;
            f.params = params;
            id = f.id;
            insertFeature(m, std::move(f));
            return true;
        });
        if (!r) {
            return r;
        }
        return checkFeature(id);
    }
    if (a.verb == "set") {
        // set <feature> key=value ...  (edit feature parameters)
        if (a.positional.empty()) {
            return makeError("script.args", "Use: set <feature id> key=value");
        }
        const std::string fid = a.positional[0];
        auto r = doc.transact("Edit " + fid, [&](Model& m) -> Result<bool> {
            Feature* f = findFeature(m, fid);
            if (!f) {
                return makeError("script.args", "There is no feature called '" + fid + "'.");
            }
            for (auto& [k, v] : a.kv) {
                f->params[k] = v;
            }
            return true;
        });
        if (!r) {
            return r;
        }
        return checkFeature(fid);
    }
    if (a.verb == "suppress" || a.verb == "unsuppress") {
        const std::string fid = a.positional.empty() ? "" : a.positional[0];
        return doc.transact(a.verb + " " + fid, [&](Model& m) -> Result<bool> {
            Feature* f = findFeature(m, fid);
            if (!f) {
                return makeError("script.args", "There is no feature called '" + fid + "'.");
            }
            f->suppressed = a.verb == "suppress";
            return true;
        });
    }
    if (a.verb == "rollback") {
        // rollback <n> | rollback end | rollback before=<feature id>
        return doc.transact("Move rollback marker", [&](Model& m) -> Result<bool> {
            if (a.has("before")) {
                int i = -1;
                for (size_t k = 0; k < m.features.size(); ++k) {
                    if (m.features[k].id == a.get("before")) {
                        i = int(k);
                    }
                }
                if (i < 0) {
                    return makeError("script.args", "There is no feature called '" + a.get("before") + "'.");
                }
                m.rollback = i;
            }
            else if (!a.positional.empty() && a.positional[0] != "end") {
                m.rollback = std::stoi(a.positional[0]);
            }
            else {
                m.rollback = -1;
            }
            return true;
        });
    }
    if (a.verb == "undo") {
        int n = a.positional.empty() ? 1 : std::stoi(a.positional[0]);
        for (int i = 0; i < n; ++i) {
            if (!doc.undo()) {
                return makeError("script.undo", "There is nothing more to undo.");
            }
        }
        return true;
    }
    if (a.verb == "redo") {
        int n = a.positional.empty() ? 1 : std::stoi(a.positional[0]);
        for (int i = 0; i < n; ++i) {
            if (!doc.redo()) {
                return makeError("script.redo", "There is nothing more to redo.");
            }
        }
        return true;
    }

    // ---- output
    if (a.verb == "measure") {
        // measure [body=<id>] a=face:<name>|edge:<name> [b=face:<name>|edge:<name>]  -> JSON
        const ModelState& st = doc.state();
        std::string bodyId = a.get("body");
        if (bodyId.empty() && st.bodies.size() == 1) {
            bodyId = st.bodies.begin()->first;
        }
        auto bit = st.bodies.find(bodyId);
        if (bit == st.bodies.end()) {
            return makeError("script.args", "Say which body to measure with body=<id>.");
        }
        const NamedShape& body = *bit->second;
        auto pick = [&](const std::string& sel) -> Result<TopoDS_Shape> {
            if (sel.rfind("face:", 0) == 0) {
                auto hits = body.findFaces(sel.substr(5));
                if (hits.size() == 1) {
                    return TopoDS_Shape(body.face(hits[0]));
                }
            }
            else if (sel.rfind("edge:", 0) == 0) {
                auto hits = body.findEdges(sel.substr(5));
                if (hits.size() == 1) {
                    return TopoDS_Shape(body.edge(hits[0]));
                }
            }
            return makeError("script.args", "No single face or edge called '" + sel + "'.",
                             {"Use face:<name> or edge:<name>; the faces command lists face names."});
        };
        auto sa = pick(a.get("a"));
        if (!sa) {
            return sa.error();
        }
        auto info = [](const MeasureItem& m) {
            nlohmann::json j {{"kind", m.kind}, {m.kind.find("face") != std::string::npos || m.kind == "cone" ? "area" : "length", m.size}};
            if (m.radius) {
                j["radius"] = *m.radius;
            }
            return j;
        };
        nlohmann::json out {{"a", info(measureItem(sa.value()))}};
        if (a.has("b")) {
            auto sb = pick(a.get("b"));
            if (!sb) {
                return sb.error();
            }
            out["b"] = info(measureItem(sb.value()));
            auto mb = measureBetween(sa.value(), sb.value());
            if (!mb) {
                return mb.error();
            }
            out["distance"] = mb->distance;
            if (mb->angle) {
                out["angle_deg"] = *mb->angle * 180 / std::numbers::pi;
            }
            if (mb->centres) {
                out["centre_distance"] = *mb->centres;
            }
        }
        output_.push_back(out.dump(2));
        return true;
    }
    if (a.verb == "faces") {
        // faces [body=<id>]: prints the body's face names, for writing face:<name> selectors
        const ModelState& st = doc.state();
        nlohmann::json out = nlohmann::json::object();
        for (auto& [id, b] : st.bodies) {
            if (a.has("body") && a.get("body") != id) {
                continue;
            }
            auto& list = out[id] = nlohmann::json::array();
            for (int i = 0; i < b->faceCount(); ++i) {
                list.push_back(b->faceName(i));
            }
        }
        output_.push_back(out.dump(2));
        return true;
    }
    if (a.verb == "report") {
        output_.push_back(reportJson(doc).dump(2));
        return true;
    }
    if (a.verb == "export") {
        if (a.positional.size() < 2) {
            return makeError("script.args", "Use: export stl|step|3mf <path>");
        }
        const std::string fmt = lower(a.positional[0]), path = a.positional[1];
        if (fmt == "3mf") {
            std::vector<std::pair<std::string, TopoDS_Shape>> bodies;
            for (auto& [id, b] : doc.state().bodies) {
                const Feature* f = doc.feature(id);
                bodies.emplace_back(f ? f->name : id, b->shape());
            }
            return export3mf(bodies, path);
        }
        TopoDS_Shape s = doc.combinedShape();
        Result<bool> r = fmt == "stl"    ? exportStl(s, path)
                       : fmt == "step"   ? exportStep(s, path)
                                         : Result<bool>(makeError("script.args", "Unknown export format '" + fmt + "'."));
        return r;
    }
    if (a.verb == "save") {
        if (a.positional.empty()) {
            return makeError("script.args", "Use: save <path.jig>");
        }
        return saveDocument(doc, a.positional[0]);
    }
    return makeError("script.unknown", "Unknown command '" + a.verb + "'.",
                     {"See docs/SCRIPTING.md for the list of script commands."});
}

}  // namespace jig
