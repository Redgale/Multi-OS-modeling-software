// SPDX-License-Identifier: GPL-3.0-or-later
// Performance budgets from the brief, section 7.2. Prints one JSON object; CI
// stores it as an artifact so results can be tracked over time (bench/README.md).
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "jig/commands.hpp"
#include "jig/io.hpp"
#include "jig/version.hpp"

using namespace jig;
using Clock = std::chrono::steady_clock;

namespace
{
double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}
double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : v[v.size() / 2];
}
std::string readFile(const std::string& p)
{
    std::ifstream f(p);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
}  // namespace

int main()
{
    nlohmann::json out;
    out["version"] = JIG_VERSION_STRING;
    out["occt"] = JIG_OCCT_VERSION_STRING;

    // 1. Sketch drag with ~100 entities: 25 rectangles (100 lines), each with H/V constraints.
    {
        Sketch s;
        std::vector<int> corners;
        for (int i = 0; i < 25; ++i) {
            double x = (i % 5) * 30.0, y = (i / 5) * 30.0;
            int A = s.addPoint(x, y, true), B = s.addPoint(x + 20, y, true), C = s.addPoint(x + 20, y + 15, true),
                D = s.addPoint(x, y + 15, true);
            int ab = s.addLine(A, B), bc = s.addLine(B, C), cd = s.addLine(C, D), da = s.addLine(D, A);
            s.addConstraint(ConstraintType::Horizontal, ab);
            s.addConstraint(ConstraintType::Horizontal, cd);
            s.addConstraint(ConstraintType::Vertical, bc);
            s.addConstraint(ConstraintType::Vertical, da);
            s.addConstraint(ConstraintType::Distance, ab, 0, 0, "20");
            corners.push_back(C);
        }
        std::vector<double> t;
        for (int k = 0; k < 60; ++k) {
            int p = corners[k % corners.size()];
            auto [x, y] = s.pointXY(p);
            auto rep = s.drag(p, x + 0.5, y + 0.3);
            t.push_back(rep.solveMs);
        }
        out["sketch_drag_100_entities_ms"] = {{"median", median(t)}, {"max", *std::max_element(t.begin(), t.end())},
                                               {"budget", 16}};
    }

    // 2. Recompute of a 50-feature timeline: one plate + 24 sketch/cut pairs + one fillet.
    {
        Document doc;
        ScriptRunner run(doc);
        std::string script = "param hole_d = 3 mm\nsketch id=s0\nrect center=0,0 w=200 h=150\nextrude id=e0 sketch=s0 distance=8\n";
        int n = 0;
        for (int i = 0; i < 24; ++i) {
            double x = -90 + (i % 8) * 25.0, y = -50 + (i / 8) * 50.0;
            script += "sketch id=h" + std::to_string(i) + "\ncircle center=\"" + std::to_string(x) + "," +
                      std::to_string(y) + "\" d=hole_d\nextrude sketch=h" + std::to_string(i) +
                      " extent=all op=cut\n";
            ++n;
        }
        script += "fillet id=fl edges=face:e0/end radius=1mm\n";
        auto t0 = Clock::now();
        auto r = run.runText(script);
        auto t1 = Clock::now();
        if (!r) {
            std::cerr << r.error().message << "\n";
            return 1;
        }
        const int features = int(doc.model().features.size());
        run.runLine("set fl radius=0.8mm");
        double last = doc.lastRecomputeMs();
        int lastCount = doc.recomputedLastTime();
        run.runLine("set e0 distance=9");
        double first = doc.lastRecomputeMs();
        int firstCount = doc.recomputedLastTime();
        out["recompute_50_features"] = {{"features", features},
                                        {"edit_last_ms", last},
                                        {"edit_last_features_rebuilt", lastCount},
                                        {"edit_first_ms", first},
                                        {"edit_first_features_rebuilt", firstCount},
                                        {"build_from_script_ms", ms(t0, t1)},
                                        {"budget_last_ms", 100},
                                        {"budget_first_ms", 1000}};
    }

    // 3. Open a typical hobby file (the phone stand) including full recompute.
    {
        Document doc;
        ScriptRunner run(doc);
        run.runText(readFile(std::string(JIG_SOURCE_DIR) + "/samples/phone_stand.jigs"));
        auto path = (std::filesystem::temp_directory_path() / "jig-bench.jig").string();
        saveDocument(doc, path);
        std::vector<double> t;
        for (int i = 0; i < 5; ++i) {
            auto a = Clock::now();
            auto d = loadDocument(path);
            t.push_back(ms(a, Clock::now()));
        }
        out["open_phone_stand_ms"] = {{"median", median(t)}, {"budget", 2000}};

        auto a = Clock::now();
        DisplayMesh m = tessellate(doc.combinedShape());
        out["tessellate_phone_stand"] = {{"ms", ms(a, Clock::now())}, {"triangles", m.indices.size() / 3}};
    }
    std::cout << out.dump(2) << "\n";
    return 0;
}
