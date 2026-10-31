// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry golden tests (brief 7.1) and failure grace (scenario A5) and
// interchange round trips (scenario A7, automated half).
#include <cmath>
#include <filesystem>

#include <gtest/gtest.h>

#include "helpers.hpp"

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
