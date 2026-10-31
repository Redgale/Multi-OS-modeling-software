// SPDX-License-Identifier: GPL-3.0-or-later
// Transactions, undo/redo, incremental recompute and the .jig file format.
#include <cmath>
#include <random>

#include <gtest/gtest.h>

#include "helpers.hpp"
#include "jig/io.hpp"

using namespace jig;
using namespace jigtest;

TEST(Document, TransactionErrorLeavesModelUntouched)
{
    Document doc;
    auto before = doc.toJson();
    auto r = doc.transact("bad", [](Model& m) -> Result<bool> {
        m.parameters.push_back({"x", "1 mm", ParamKind::Length, {}});
        return makeError("test", "no");
    });
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(doc.toJson(), before);
    EXPECT_FALSE(doc.canUndo());
}

TEST(Document, ParameterCyclesAreExplained)
{
    auto doc = build("param a = 5 mm\nparam b = a * 2\n");
    const auto before = doc->toJson();
    ScriptRunner r(*doc);
    // An unknown name is refused, and nothing is committed.
    auto unknown = r.runLine("param c = d + 1 mm");
    ASSERT_FALSE(unknown.ok());
    EXPECT_NE(unknown.error().message.find("'d'"), std::string::npos) << unknown.error().message;
    // A cycle is refused with a plain explanation, and nothing is committed.
    auto cyc = r.runLine("param a = b + 1 mm");
    ASSERT_FALSE(cyc.ok());
    EXPECT_NE(cyc.error().message.find("itself"), std::string::npos) << cyc.error().message;
    EXPECT_EQ(doc->toJson(), before);
    EXPECT_NEAR(doc->evalParam("b")->value, 10, 1e-12);
}

TEST(Document, ParameterKindIsKeptAndInferred)
{
    auto doc = build("param lean = 75 deg\nparam n = 4 kind=number\n");
    EXPECT_EQ(int(doc->model().parameters[0].kind), int(ParamKind::Angle));
    run(*doc, "param lean = 70 deg");  // updating keeps it an angle
    EXPECT_EQ(int(doc->model().parameters[0].kind), int(ParamKind::Angle));
    run(*doc, "param lean = 60");      // a bare number in an angle parameter means degrees
    EXPECT_NEAR(doc->evalParam("lean")->value, 60 * M_PI / 180, 1e-12);
}

TEST(Document, UndoRedoFiftyStepsIsIdentical)
{
    // Scenario A6 (second half): undo 50 steps and redo 50 steps with identical results.
    auto doc = buildSample("phone_stand.jigs");
    const auto jEnd = doc->toJson();
    const double vEnd = props(*doc).volume;
    for (int i = 0; i < 40; ++i) {
        run(*doc, "param base_w = " + std::to_string(80 + i % 7) + " mm");
    }
    ASSERT_GE(doc->undoDepth(), 50u);
    const auto jTop = doc->toJson();
    const double vTop = props(*doc).volume;
    for (int i = 0; i < 50; ++i) {
        ASSERT_TRUE(doc->undo());
    }
    for (int i = 0; i < 50; ++i) {
        ASSERT_TRUE(doc->redo());
    }
    EXPECT_EQ(doc->toJson(), jTop);
    EXPECT_NEAR(props(*doc).volume, vTop, 1e-6);
    for (int i = 0; i < 40; ++i) {
        ASSERT_TRUE(doc->undo());
    }
    EXPECT_EQ(doc->toJson(), jEnd);
    EXPECT_NEAR(props(*doc).volume, vEnd, 1e-6);
    expectAllOk(*doc);
}

TEST(Document, RecomputeIsIncremental)
{
    auto doc = buildSample("phone_stand.jigs");
    const int n = int(doc->model().features.size());
    // Editing the last feature recomputes only that feature.
    run(*doc, "set f2 radius=1mm");
    EXPECT_EQ(doc->recomputedLastTime(), 1);
    // Changing a parameter used only by the slot recomputes from the slot sketch on.
    run(*doc, "param slot_w = 12 mm");
    EXPECT_EQ(doc->recomputedLastTime(), n - doc->featureIndex("s4"));
    // Undo hits the cache: nothing upstream of the change is rebuilt.
    doc->undo();
    EXPECT_LE(doc->recomputedLastTime(), n - doc->featureIndex("s4"));
}

TEST(Document, SaveLoadRecomputesIdentically)
{
    auto doc = buildSample("phone_stand.jigs");
    const auto path = (tempDir() / "roundtrip.jig").string();
    ASSERT_TRUE(saveDocument(*doc, path).ok());
    auto back = loadDocument(path);
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ((*back)->toJson(), doc->toJson());
    auto a = props(*doc), b = props(**back);
    EXPECT_NEAR(a.volume, b.volume, 1e-6);
    EXPECT_NEAR(a.area, b.area, 1e-6);
    EXPECT_EQ(a.faces, b.faces);
    EXPECT_EQ(a.edges, b.edges);
    expectAllOk(**back);
}

TEST(Document, UnknownFieldsArePreserved)
{
    auto doc = build("param w = 10 mm\nsketch id=s1\nrect center=0,0 w=w h=5\n");
    auto j = doc->toJson();
    j["future_top_level"] = {{"x", 1}};
    j["features"][0]["future_feature_field"] = "keep me";
    auto back = Document::fromJson(j);
    ASSERT_TRUE(back.ok());
    auto j2 = (*back)->toJson();
    EXPECT_EQ(j2["future_top_level"]["x"], 1);
    EXPECT_EQ(j2["features"][0]["future_feature_field"], "keep me");
}

TEST(Document, NewerFormatIsRefusedPolitely)
{
    nlohmann::json j = {{"format_version", kFormatVersion + 1}, {"parameters", nlohmann::json::array()},
                        {"features", nlohmann::json::array()}};
    auto r = Document::fromJson(j);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error().code, "file.too_new");
}

TEST(Document, DocumentJsonIsCanonical)
{
    // Stable key order: saving twice gives byte-identical document.json, so designs diff cleanly.
    auto doc = buildSample("phone_stand.jigs");
    EXPECT_EQ(doc->toJson().dump(2), Document::fromJson(doc->toJson()).value()->toJson().dump(2));
}

TEST(Document, DamagedFilesNeverCrash)
{
    auto doc = buildSample("phone_stand.jigs");
    const auto path = (tempDir() / "fuzz.jig").string();
    ASSERT_TRUE(saveDocument(*doc, path).ok());
    const std::string good = readFile(path);
    std::mt19937 rng(99);
    int opened = 0;
    for (int i = 0; i < 400; ++i) {
        std::string bad = good;
        if (i % 3 == 0) {
            bad.resize(rng() % good.size());  // truncation
        }
        else {
            for (int k = 0; k < 1 + int(rng() % 8); ++k) {
                bad[rng() % bad.size()] = char(rng());  // bit rot
            }
        }
        auto r = loadDocumentFromMemory(bad.data(), bad.size());
        if (r.ok()) {
            ++opened;  // corruption in compressed padding can be harmless
        }
        else {
            EXPECT_FALSE(r.error().message.empty());
        }
    }
    SUCCEED() << opened << " of 400 corrupted files still opened";
    // Not a zip at all
    std::string junk = "hello";
    EXPECT_FALSE(loadDocumentFromMemory(junk.data(), junk.size()).ok());
}

TEST(Document, AtomicSaveLeavesNoPartialFile)
{
    auto doc = build("param w = 10 mm\n");
    const auto path = tempDir() / "atomic.jig";
    ASSERT_TRUE(saveDocument(*doc, path.string()).ok());
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".partial"));
    // saving into a folder that does not exist fails with an explanation, no crash
    auto bad = saveDocument(*doc, (tempDir() / "no" / "such" / "dir" / "x.jig").string());
    EXPECT_FALSE(bad.ok());
}

TEST(Document, DependentsAreFound)
{
    auto doc = buildSample("phone_stand.jigs");
    auto d = doc->dependents("s1");
    ASSERT_EQ(d.size(), 1u);
    EXPECT_EQ(d[0], "e1");
    auto e1 = doc->dependents("e1");  // the lip sketch sits on e1's face; fillets use e1 edges
    EXPECT_NE(std::find(e1.begin(), e1.end(), "s3"), e1.end());
    EXPECT_NE(std::find(e1.begin(), e1.end(), "f1"), e1.end());
    EXPECT_TRUE(doc->dependents("f2").empty());
}

TEST(Document, InchDesignsReadPlainNumbersAsInches)
{
    auto doc = build("units in\nparam wall = 0.125\n"
                     "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=2 h=1\nextrude id=e1 sketch=s1 distance=0.5\n"
                     "shell id=sh1 faces=face:e1/end thickness=wall\n");
    expectAllOk(*doc);
    EXPECT_EQ(doc->units(), LengthUnit::Inch);
    const double in = 25.4;
    const double t = 0.125 * in;
    const double expected = 2 * in * 1 * in * 0.5 * in - (2 * in - 2 * t) * (1 * in - 2 * t) * (0.5 * in - t);
    EXPECT_NEAR(props(*doc).volume, expected, 1e-6);
    // the typed values were stored with their unit written out
    EXPECT_EQ(doc->model().parameters[0].expr, "0.125 in");
    EXPECT_EQ(doc->feature("e1")->params.value("distance", std::string {}), "0.5 in");
    // the unit is saved, and switching back to millimetres changes the display only
    const auto path = (tempDir() / "inch.jig").string();
    ASSERT_TRUE(saveDocument(*doc, path).ok());
    auto back = loadDocument(path);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ((*back)->units(), LengthUnit::Inch);
    run(*doc, "units mm");
    EXPECT_NEAR(props(*doc).volume, expected, 1e-6);
    EXPECT_EQ(doc->fmtLength(25.4), "25.4 mm");
    EXPECT_EQ(doc->defaultLength(10, 0.5), "10 mm");
    run(*doc, "units in");
    EXPECT_EQ(doc->defaultLength(10, 0.5), "0.5 in");
}

TEST(Document, ReorderStepsSafely)
{
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                     "sketch id=s2 plane=XY\ncircle sketch=s2 center=0,0 d=6\nextrude id=e2 sketch=s2 extent=all op=cut\n"
                     "fillet id=f1 edges=\"name:E[e1/end|e1/side:5]\" radius=1mm\n");
    expectAllOk(*doc);
    const double v = props(*doc).volume;
    auto order = [&] {
        std::string o;
        for (auto& f : doc->model().features) {
            o += f.id + " ";
        }
        return o;
    };
    // a step cannot move before what it uses
    auto bad = doc->moveFeature("e1", 0);
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.error().message.find("uses"), std::string::npos) << bad.error().message;
    EXPECT_EQ(order(), "s1 e1 s2 e2 f1 ");
    // an independent sketch can move to the front; the fillet can go before the cut
    ASSERT_TRUE(doc->moveFeature("s2", 0).ok());
    ASSERT_TRUE(doc->moveFeature("f1", 3).ok());
    EXPECT_EQ(order(), "s2 s1 e1 f1 e2 ");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).volume, v, 1e-6);
    // the cut uses no name of the block, but before the block there is nothing to cut:
    // the move is refused and the timeline stays as it was
    auto breaks = doc->moveFeature("e2", 1);
    ASSERT_FALSE(breaks.ok());
    EXPECT_NE(breaks.error().message.find("stop"), std::string::npos) << breaks.error().message;
    EXPECT_EQ(order(), "s2 s1 e1 f1 e2 ");
    expectAllOk(*doc);
    // moves are undoable
    ASSERT_TRUE(doc->undo());
    EXPECT_EQ(order(), "s2 s1 e1 e2 f1 ");  // the last successful move is taken back
    ScriptRunner r(*doc);
    ASSERT_TRUE(r.runLine("reorder s2 after=e1").ok());
}

TEST(Document, VersionsTravelInsideTheFile)
{
    auto doc = build("param w = 40 mm\nsketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=w h=20\n"
                     "extrude id=e1 sketch=s1 distance=10\n");
    const auto path = (tempDir() / "versions.jig").string();
    std::filesystem::remove(path);
    auto v1 = saveVersion(*doc, path, "first try");
    ASSERT_TRUE(v1.ok()) << v1.error().message;
    EXPECT_EQ(v1.value(), 1);
    run(*doc, "param w = 60 mm");
    ASSERT_TRUE(saveDocument(*doc, path).ok());  // an ordinary save keeps the versions
    auto v2 = saveVersion(*doc, path, "wider");
    ASSERT_TRUE(v2.ok());
    EXPECT_EQ(v2.value(), 2);
    auto list = listVersions(path);
    ASSERT_TRUE(list.ok());
    ASSERT_EQ(list->size(), 2u);
    EXPECT_EQ((*list)[0].label, "first try");
    EXPECT_EQ((*list)[1].label, "wider");
    EXPECT_EQ((*list)[0].saved.size(), 20u);  // 2026-10-31T04:00:00Z
    auto first = loadVersion(path, 1);
    ASSERT_TRUE(first.ok());
    EXPECT_NEAR(props(**first).volume, 40 * 20 * 10, 1e-6);
    auto latest = loadDocument(path);
    ASSERT_TRUE(latest.ok());
    EXPECT_NEAR(props(**latest).volume, 60 * 20 * 10, 1e-6);
    EXPECT_FALSE(loadVersion(path, 7).ok());
    // a file without versions simply has none
    const auto plain = (tempDir() / "plain.jig").string();
    ASSERT_TRUE(saveDocument(*doc, plain).ok());
    ASSERT_TRUE(listVersions(plain).ok());
    EXPECT_TRUE(listVersions(plain)->empty());
}

TEST(Document, TimelineGroupsAreSaved)
{
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=5\n"
                     "group name=\"Base\" steps=s1;e1\n");
    EXPECT_EQ(doc->model().features[0].group, "Base");
    EXPECT_EQ(doc->model().features[1].group, "Base");
    const auto path = (tempDir() / "groups.jig").string();
    ASSERT_TRUE(saveDocument(*doc, path).ok());
    auto back = loadDocument(path);
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ((*back)->model().features[1].group, "Base");
    run(*doc, "group name=\"\" steps=e1");
    EXPECT_TRUE(doc->model().features[1].group.empty());
}
