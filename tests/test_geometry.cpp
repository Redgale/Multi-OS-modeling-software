// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry golden tests (brief 7.1) and failure grace (scenario A5) and
// interchange round trips (scenario A7, automated half).
#include <cmath>
#include <filesystem>

#include <gtest/gtest.h>

#include <map>

#include "helpers.hpp"
#include "jig/io.hpp"

using namespace jig;
using namespace jigtest;

TEST(Golden, PhoneStand)
{
    auto doc = buildSample("phone_stand.jigs");
    expectAllOk(*doc);
    auto p = props(*doc);
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.solids, 1);
    // Golden values recorded 2026-10-30 (OCCT 7.6.3); see docs/STATUS.md for the derivation.
    // Hand check: base 37800 + back 46512 + lip 7560 - slot 3527 = 88345 mm^3 before
    // fillets, which remove roughly 420 mm^3.
    EXPECT_NEAR(p.volume, 87925.867, 87925.867 * 1e-4);
    EXPECT_NEAR(p.xmin, -45, 1e-6);
    EXPECT_NEAR(p.xmax, 45, 1e-6);
    EXPECT_NEAR(p.ymin, -35, 1e-6);
    EXPECT_NEAR(p.ymax, 35 + 70 / std::tan(75 * M_PI / 180), 1e-6);
    EXPECT_NEAR(p.zmin, 0, 1e-6);
    EXPECT_NEAR(p.zmax, 70, 1e-6);
    EXPECT_EQ(p.faces, 26);
    EXPECT_EQ(p.edges, 62);
}

TEST(Golden, BlockWithHole)
{
    // PDF figure 13: a circle inside a rectangle extrudes to a block with a hole.
    auto doc = build("sketch id=s1\nrect center=0,0 w=60 h=40\ncircle center=0,0 d=10\n"
                     "extrude sketch=s1 profiles=all distance=20\n");
    auto p = props(*doc);
    EXPECT_NEAR(p.volume, 60 * 40 * 20, 1e-6);  // both regions extruded: solid block
    auto doc2 = build("sketch id=s1\nrect center=0,0 w=60 h=40\ncircle center=0,0 d=10\n");
    const auto& sk = *doc2->state().sketches.at("s1");
    ASSERT_EQ(sk.profiles.regions.size(), 2u);
    std::string ring;
    for (auto& r : sk.profiles.regions) {
        if (r.area > 1000) {
            ring = r.key;
        }
    }
    run(*doc2, "extrude sketch=s1 profiles=" + ring + " distance=20");
    EXPECT_NEAR(props(*doc2).volume, (60 * 40 - M_PI * 25) * 20, 1e-3);
}

TEST(Golden, FirstDesignFromChapter3)
{
    // "Your first design in five minutes": a 60 x 40 x 10 block.
    auto doc = build("sketch\nrect center=0,0 w=60 h=40\nextrude distance=10 sketch=s1\n");
    auto p = props(*doc);
    EXPECT_NEAR(p.volume, 24000, 1e-6);
    EXPECT_EQ(p.faces, 6);
    EXPECT_EQ(p.edges, 12);
}

TEST(FailureGrace, ImpossibleFilletIsExplainedAndRolledBack)
{
    // Scenario A5: ask the lip fillet for a radius the 6 mm lip can't take.
    auto doc = buildSample("phone_stand.jigs");
    const int f2 = doc->featureIndex("f2");
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runLine("set f2 radius=20mm").ok());
    const auto& st = doc->status(f2);
    ASSERT_EQ(int(st.level), int(StatusLevel::Error));
    EXPECT_NE(st.message.find("too big"), std::string::npos) << st.message;
    // no raw kernel text in what the user sees
    EXPECT_EQ(st.message.find("Standard_"), std::string::npos);
    // the model rolled back to the state before the failed fillet
    const auto& before = doc->stateBefore(f2).bodies;
    ASSERT_EQ(before.size(), 1u);
    EXPECT_NEAR(props(*doc).volume, massProperties(before.begin()->second->shape()).volume, 1e-9);
    // a concrete radius is suggested...
    ASSERT_FALSE(st.suggestions.empty());
    const std::string s0 = st.suggestions[0];
    ASSERT_EQ(s0.rfind("Try ", 0), 0u) << s0;
    double suggested = std::stod(s0.substr(4));
    EXPECT_GT(suggested, 0);
    EXPECT_LT(suggested, 6);
    // ...and it actually works
    ASSERT_TRUE(r.runLine("set f2 radius=\"" + std::to_string(suggested) + " mm\"").ok());
    EXPECT_EQ(int(doc->status(f2).level), int(StatusLevel::Ok)) << doc->status(f2).message;
}

TEST(FailureGrace, FilletOnSimpleBlock)
{
    auto doc = std::make_unique<Document>();
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runText("sketch id=s1\nrect center=0,0 w=60 h=40\nextrude sketch=s1 distance=10\n"
                          "fillet edges=all radius=8mm\n").ok());
    const auto& st = doc->status(2);
    ASSERT_EQ(int(st.level), int(StatusLevel::Error));
    ASSERT_FALSE(st.suggestions.empty());
    ASSERT_EQ(st.suggestions[0].rfind("Try ", 0), 0u) << st.suggestions[0];
    EXPECT_NEAR(props(*doc).volume, 24000, 1e-6);
}

TEST(FailureGrace, OpenSketchExplainsTheGap)
{
    auto doc = std::make_unique<Document>();
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runText("sketch id=s1\npolyline points=\"0,0; 40,0; 40,30; 0.3,30\" closed=no\n").ok());
    const auto& st = doc->status(0);
    EXPECT_EQ(int(st.level), int(StatusLevel::Warning));
    EXPECT_NE(st.message.find("no closed area"), std::string::npos);
    ASSERT_TRUE(r.runLine("extrude sketch=s1 distance=5").ok());
    EXPECT_EQ(int(doc->status(1).level), int(StatusLevel::Error));
    EXPECT_TRUE(doc->state().bodies.empty());
}

TEST(FailureGrace, JoinThatDoesNotTouch)
{
    auto doc = std::make_unique<Document>();
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runText("sketch id=s1\nrect center=0,0 w=10 h=10\nextrude sketch=s1 distance=5\n"
                          "sketch id=s2\nrect center=50,0 w=10 h=10\nextrude sketch=s2 distance=5 op=join\n")
                    .ok());
    const auto& st = doc->status(3);
    EXPECT_EQ(int(st.level), int(StatusLevel::Error));
    EXPECT_NE(st.message.find("does not touch"), std::string::npos);
}

TEST(Interchange, StepRoundTripKeepsVolume)
{
    auto doc = buildSample("phone_stand.jigs");
    const auto path = (tempDir() / "stand.step").string();
    ASSERT_TRUE(exportStep(doc->combinedShape(), path).ok());
    auto in = importStep(path);
    ASSERT_TRUE(in.ok()) << in.error().message;
    EXPECT_EQ(in->fileUnit, "mm");
    auto a = props(*doc);
    auto b = massProperties(in->shape);
    EXPECT_NEAR(b.volume, a.volume, a.volume * 1e-6);
    EXPECT_EQ(b.faces, a.faces);
}

TEST(Interchange, BinaryStl)
{
    auto doc = buildSample("phone_stand.jigs");
    const auto path = tempDir() / "stand.stl";
    ASSERT_TRUE(exportStl(doc->combinedShape(), path.string()).ok());
    const std::string data = readFile(path.string());
    ASSERT_GT(data.size(), 84u);
    uint32_t n = 0;
    std::memcpy(&n, data.data() + 80, 4);
    EXPECT_EQ(data.size(), 84u + 50u * n);  // binary STL layout
    EXPECT_GT(n, 500u);
}

TEST(Interchange, ThreeMfIsWatertightAndKeepsVolume)
{
    auto doc = buildSample("knob.jigs");
    const auto& body = *doc->state().bodies.begin()->second;
    const Mesh m = meshOf(body.shape());
    ASSERT_GT(m.triangles.size(), 300u);
    // every directed edge appears once, and its reverse once: closed and consistently oriented
    std::map<std::pair<uint32_t, uint32_t>, int> half;
    for (size_t i = 0; i < m.triangles.size(); i += 3) {
        for (int k = 0; k < 3; ++k) {
            ++half[{m.triangles[i + size_t(k)], m.triangles[i + size_t((k + 1) % 3)]}];
        }
    }
    int bad = 0;
    for (auto& [e, n] : half) {
        auto twin = half.find({e.second, e.first});
        bad += n != 1 || twin == half.end() || twin->second != 1;
    }
    EXPECT_EQ(bad, 0);
    double vol = 0;
    auto P = [&](uint32_t i) { return gp_XYZ(m.xyz[3 * i], m.xyz[3 * i + 1], m.xyz[3 * i + 2]); };
    for (size_t i = 0; i < m.triangles.size(); i += 3) {
        vol += P(m.triangles[i]).Dot(P(m.triangles[i + 1]).Crossed(P(m.triangles[i + 2]))) / 6;
    }
    EXPECT_NEAR(vol, props(*doc).volume, props(*doc).volume * 1e-3);
    const auto path = (tempDir() / "knob.3mf").string();
    ScriptRunner r(*doc);
    ASSERT_TRUE(r.runLine("export 3mf " + path).ok());
    const std::string bytes = readFile(path);
    EXPECT_EQ(bytes.substr(0, 2), "PK");
    EXPECT_NE(bytes.find("3D/3dmodel.model"), std::string::npos);
    EXPECT_NE(bytes.find("[Content_Types].xml"), std::string::npos);
}

TEST(Interchange, MalformedStepIsRejected)
{
    const auto path = (tempDir() / "junk.step").string();
    std::ofstream(path) << "ISO-10303-21;\nHEADER;\nthis is not step\n";
    auto r = importStep(path);
    EXPECT_FALSE(r.ok());
}

TEST(Display, TessellationHasFaceIds)
{
    auto doc = buildSample("phone_stand.jigs");
    DisplayMesh m = tessellate(doc->combinedShape());
    EXPECT_GT(m.indices.size(), 300u);
    EXPECT_EQ(m.triFace.size() * 3, m.indices.size());
    EXPECT_EQ(m.positions.size(), m.normals.size());
    uint32_t maxFace = 0;
    for (auto f : m.triFace) {
        maxFace = std::max(maxFace, f);
    }
    EXPECT_EQ(int(maxFace) + 1, props(*doc).faces);
    EXPECT_FALSE(m.edgeLines.empty());
}

TEST(Queries, PointInsideAndTangentChain)
{
    auto doc = build("sketch id=s1\nrect center=0,0 w=60 h=40\nextrude sketch=s1 distance=10\n"
                     "fillet edges=all radius=3mm\n");
    const auto& body = *doc->state().bodies.begin()->second;
    EXPECT_TRUE(pointInside(body.shape(), 0, 0, 5));
    EXPECT_FALSE(pointInside(body.shape(), 0, 0, 15));
    // with every edge rounded, the top face is a rounded rectangle: its outline is one
    // tangent-continuous loop, so the chain is at least that loop
    int topEdge = body.edgesOfFace(body.findFaces("e1/end")[0])[0];
    EXPECT_GE(tangentChain(body, topEdge).size(), body.edgesOfFace(body.findFaces("e1/end")[0]).size());
    // on the plain block, edges meet at right angles: no chain
    auto plain = build("sketch id=s1\nrect center=0,0 w=60 h=40\nextrude sketch=s1 distance=10\n");
    const auto& pb = *plain->state().bodies.begin()->second;
    EXPECT_EQ(tangentChain(pb, 0).size(), 1u);
}

TEST(Golden, PenCupWithParameters)
{
    // Scenario A3: height, dia and wall drive the cup; changing them keeps it valid.
    auto doc = buildSample("pen_cup.jigs");
    expectAllOk(*doc);
    auto p = props(*doc);
    ASSERT_TRUE(p.valid);
    // analytic shell volume, less the 2 mm base round and the two 1 mm rim rounds
    const double shell = M_PI * 40 * 40 * 95 - M_PI * 37.6 * 37.6 * (95 - 2.4);
    const double rounds = (4 - M_PI) * 4 / 4 * 2 * M_PI * 40 * 1.0          // base: (1 - pi/4) r^2 L, r = 2
                          + (1 - M_PI / 4) * 1 * (2 * M_PI * 40 + 2 * M_PI * 37.6);  // rim, r = 1
    double mark = 0;  // the maker's mark cut 0.6 mm into the underside
    for (auto& r : doc->state().sketches.at("s2")->profiles.regions) {
        mark += r.area * 0.6;
    }
    EXPECT_EQ(doc->state().sketches.at("s2")->profiles.regions.size(), 8u);  // J I G S M I T H
    EXPECT_NEAR(p.volume, shell - rounds - mark, 5.0);
    EXPECT_NEAR(p.zmax, 95, 1e-6);
    // a mini version, as the guide suggests
    run(*doc, "param height = 60 mm");
    run(*doc, "param dia = 50 mm");
    expectAllOk(*doc);
    auto q = props(*doc);
    EXPECT_TRUE(q.valid);
    EXPECT_NEAR(q.zmax, 60, 1e-6);
    EXPECT_NEAR(q.xmax - q.xmin, 50, 1e-6);
    EXPECT_EQ(q.faces, p.faces);
    // thicker walls also work
    run(*doc, "param wall = 3 mm");
    expectAllOk(*doc);
    EXPECT_EQ(props(*doc).solids, 1);
}

TEST(Chamfer, BevelsAndExplains)
{
    auto doc = build("sketch id=s1\nrect center=0,0 w=60 h=40\nextrude sketch=s1 distance=10\n"
                     "chamfer id=c1 edges=face:e1/start distance=1mm\n");
    expectAllOk(*doc);
    // an equal-distance chamfer along the 200 mm bottom outline removes d^2/2 per mm, and the
    // four corner mitres give back d^3/3 each: exactly 24000 - 100 + 4/3
    EXPECT_NEAR(props(*doc).volume, 24000 - 0.5 * 200 + 4.0 / 3.0, 1e-3);
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runLine("set c1 distance=12mm").ok());
    const auto& st = doc->status(doc->featureIndex("c1"));
    ASSERT_EQ(int(st.level), int(StatusLevel::Error));
    EXPECT_NE(st.message.find("too big"), std::string::npos) << st.message;
    ASSERT_FALSE(st.suggestions.empty());
    EXPECT_EQ(st.suggestions[0].rfind("Try ", 0), 0u) << st.suggestions[0];
}

TEST(Shell, TooThickIsExplainedWithAWorkingValue)
{
    auto doc = std::make_unique<Document>();
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runText("sketch id=s1\nrect center=0,0 w=60 h=40\nextrude sketch=s1 distance=10\n"
                          "shell id=sh1 faces=face:e1/end thickness=12mm\n").ok());  // taller than the 10 mm box
    const auto& st = doc->status(doc->featureIndex("sh1"));
    ASSERT_EQ(int(st.level), int(StatusLevel::Error));
    EXPECT_NE(st.message.find("does not fit"), std::string::npos) << st.message;
    ASSERT_FALSE(st.suggestions.empty());
    ASSERT_EQ(st.suggestions[0].rfind("Try ", 0), 0u) << st.suggestions[0];
    const double t = std::stod(st.suggestions[0].substr(4));
    ASSERT_TRUE(r.runLine("set sh1 thickness=\"" + std::to_string(t) + " mm\"").ok());
    EXPECT_EQ(int(doc->status(doc->featureIndex("sh1")).level), int(StatusLevel::Ok));
    EXPECT_NEAR(props(*doc).volume, 24000.0 - (60 - 2 * t) * (40 - 2 * t) * (10 - t), 1e-3);
}

TEST(Shell, InnerWallsHaveStableNames)
{
    // A fillet on an inside edge of the shell must survive resizing the box.
    auto doc = build("param w = 60 mm\nsketch id=s1\nrect center=0,0 w=w h=40\nextrude sketch=s1 distance=20\n"
                     "shell id=sh1 faces=face:e1/end thickness=2mm\n");
    const auto& body = *doc->state().bodies.begin()->second;
    int inner = 0;
    for (int i = 0; i < body.faceCount(); ++i) {
        inner += body.faceName(i).rfind("sh1/inner:", 0) == 0;
    }
    EXPECT_EQ(inner, 5);  // four inner walls and the inner floor
    const auto floorName = std::string("sh1/inner:e1/start");
    ASSERT_EQ(body.findFaces(floorName).size(), 1u);
    run(*doc, "fillet id=f1 edges=face:" + floorName + " radius=1mm");
    run(*doc, "param w = 80 mm");
    expectAllOk(*doc);
}

TEST(Shell, ClosedHollowKeepsOuterNames)
{
    auto doc = build("sketch id=s1\nrect center=0,0 w=60 h=40\nextrude sketch=s1 distance=20\n"
                     "shell id=sh1 faces=none thickness=2mm\n");
    const auto& body = *doc->state().bodies.begin()->second;
    EXPECT_EQ(body.findFaces("e1/end").size(), 1u);
    EXPECT_EQ(body.findFaces("e1/start").size(), 1u);
    EXPECT_EQ(body.findFaces("sh1/inner:e1/end").size(), 1u);
    EXPECT_NEAR(props(*doc).volume, 60 * 40 * 20 - 56 * 36 * 16, 1e-3);
}
