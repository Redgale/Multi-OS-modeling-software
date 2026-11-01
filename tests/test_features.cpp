// SPDX-License-Identifier: GPL-3.0-or-later
// Revolve, Hole, Pattern and Mirror (acceptance scenario A2, the knob), checked against
// hand-computed volumes.
#include <fstream>
#include <cmath>
#include <numbers>
#include <random>

#include "helpers.hpp"
#include "jig/files.hpp"
#include "jig/io.hpp"
#include "jig/picture.hpp"
#include "jig/screws.hpp"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <TopoDS.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

using namespace jig;
using namespace jigtest;

namespace
{
constexpr double pi = std::numbers::pi;

const char* kProfile = "sketch id=s1 plane=XZ\n"
                       "polyline sketch=s1 points=\"0,0; 15,0; 15,13; 12,16; 0,16\" closed=yes dims=yes\n";
// cylinder r15 h13 plus a frustum r15->12 over 3 mm
const double kTurned = pi * 225 * 13 + pi * 3 / 3 * (225 + 180 + 144);

// Area of the overlap of two circles (radii R, r, centres d apart).
double lens(double R, double r, double d)
{
    if (d >= R + r) {
        return 0;
    }
    const double a = r * r * std::acos((d * d + r * r - R * R) / (2 * d * r));
    const double b = R * R * std::acos((d * d + R * R - r * r) / (2 * d * R));
    const double c = 0.5 * std::sqrt((-d + r + R) * (d + r - R) * (d - r + R) * (d + r + R));
    return a + b - c;
}

// Volume one knob scallop (circle r4 centred 17 mm out, cut through) removes.
double scallop()
{
    const int n = 20000;
    double v = 0;
    for (int i = 0; i < n; ++i) {
        const double z = 16.0 * (i + 0.5) / n;
        v += lens(z <= 13 ? 15 : 15 - (z - 13), 4, 17) * 16.0 / n;
    }
    return v;
}

const double kBore = 12 * (6 * pi + 0.5 * 9 * std::sin(2 * pi / 3));  // 240-degree D, 12 deep

bool hasFace(const Document& doc, const std::string& body, const std::string& name)
{
    return !doc.state().bodies.at(body)->findFaces(name).empty();
}

int facesStartingWith(const Document& doc, const std::string& body, const std::string& prefix)
{
    const auto& b = *doc.state().bodies.at(body);
    int n = 0;
    for (int i = 0; i < b.faceCount(); ++i) {
        n += b.faceName(i).rfind(prefix, 0) == 0;
    }
    return n;
}
double regionArea(const Document& doc, const std::string& sketch)
{
    double a = 0;
    for (auto& r : doc.state().sketches.at(sketch)->profiles.regions) {
        a += r.area;
    }
    return a;
}
}  // namespace

TEST(Revolve, FullTurnMatchesTheTurnedVolume)
{
    auto doc = build(std::string(kProfile) + "revolve id=r1 sketch=s1 axis=Z\n");
    expectAllOk(*doc);
    auto p = props(*doc);
    EXPECT_NEAR(p.volume, kTurned, 1e-3);
    EXPECT_EQ(p.faces, 4);
    EXPECT_NEAR(p.zmax, 16, 1e-6);
    // every face is named after the sketch curve that swept it, including the flat
    // top and bottom whose curves touch the axis
    for (int i = 0; i < doc->state().bodies.at("r1")->faceCount(); ++i) {
        EXPECT_EQ(doc->state().bodies.at("r1")->faceName(i).rfind("r1/side:", 0), 0u);
    }
}

TEST(Revolve, PartialTurnHasStartAndEndFaces)
{
    auto doc = build(std::string(kProfile) + "revolve id=r1 sketch=s1 axis=Z angle=90deg\n");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).volume, kTurned / 4, 1e-3);
    EXPECT_TRUE(hasFace(*doc, "r1", "r1/start"));
    EXPECT_TRUE(hasFace(*doc, "r1", "r1/end"));
}

TEST(Revolve, AroundASketchLine)
{
    // A 10 x 4 rectangle 20 mm from a construction axis line: a washer.
    auto doc = build("sketch id=s1 plane=XY\n"
                     "line sketch=s1 p1=0,-50 p2=0,50 construction=yes name=axis\n"
                     "rect sketch=s1 corner=20,0 w=10 h=4\n"
                     "revolve id=r1 sketch=s1 axis=line:axis\n");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).volume, pi * (30 * 30 - 20 * 20) * 4, 1e-3);
}

TEST(Revolve, ProfileCrossingTheAxisIsExplained)
{
    auto doc = std::make_unique<Document>();
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runText("sketch id=s1 plane=XZ\nrect sketch=s1 center=0,5 w=10 h=10\n"
                          "revolve id=r1 sketch=s1 axis=Z\n")
                    .ok());
    const auto& st = doc->status(1);
    EXPECT_EQ(int(st.level), int(StatusLevel::Error));
    EXPECT_NE(st.message.find("axis"), std::string::npos) << st.message;
    EXPECT_TRUE(doc->state().bodies.empty());  // clean rollback
}

TEST(Golden, Knob)
{
    auto doc = buildSample("knob.jigs");
    expectAllOk(*doc);
    auto p = props(*doc);
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.solids, 1);
    // Without the top round: turned body - D bore - 12 scallops - the 0.5 mm deep label.
    const double label = regionArea(*doc, "s4") * 0.5;
    EXPECT_GT(label, 5);
    run(*doc, "suppress f1");
    EXPECT_NEAR(props(*doc).volume, kTurned - kBore - 12 * scallop() - label, 0.05);
    run(*doc, "unsuppress f1");
    // Golden value recorded 2026-10-31 (OCCT 7.6.3, FreeType 2.13): the 1 mm round
    // removes ~1.6 mm^3.
    EXPECT_NEAR(props(*doc).volume, 9143.107, 9143.107 * 1e-4);
    EXPECT_NEAR(p.zmin, 0, 1e-6);
    EXPECT_NEAR(p.zmax, 16, 1e-6);
    // every scallop copy has a stable name
    for (int k = 1; k <= 11; ++k) {
        EXPECT_TRUE(hasFace(*doc, "r1", "p1/" + std::to_string(k) + ":e2/side:2")) << k;
    }
}

TEST(Golden, KnobFollowsItsParameters)
{
    auto doc = buildSample("knob.jigs");
    run(*doc, "suppress f1");
    run(*doc, "param scallops = 8");
    for (int i = 0; i < int(doc->model().features.size()); ++i) {
        if (doc->model().features[size_t(i)].id != "f1") {
            EXPECT_EQ(int(doc->status(i).level), int(StatusLevel::Ok)) << doc->status(i).message;
        }
    }
    EXPECT_NEAR(props(*doc).volume, kTurned - kBore - 8 * scallop() - regionArea(*doc, "s4") * 0.5, 0.05);
}

namespace
{
// 40 x 30 x 10 block with a sketch on its top face and two hole points.
const char* kBlock = "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\n"
                     "extrude id=e1 sketch=s1 distance=10\n"
                     "sketch id=s2 on=e1/end\npoint sketch=s2 at=-10,0\npoint sketch=s2 at=10,0\n";
}

TEST(Hole, SimpleBlindAndThrough)
{
    auto blind = build(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm depth=6mm\n");
    expectAllOk(*blind);
    EXPECT_NEAR(props(*blind).volume, 12000 - 2 * pi * 4 * 6, 1e-3);
    auto through = build(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm extent=all\n");
    expectAllOk(*through);
    EXPECT_NEAR(props(*through).volume, 12000 - 2 * pi * 4 * 10, 1e-3);
    EXPECT_EQ(facesStartingWith(*blind, "e1", "h1/floor:p"), 2);
    EXPECT_EQ(facesStartingWith(*blind, "e1", "h1/wall:p"), 2);
}

TEST(Hole, CounterboreAndCountersink)
{
    auto cb = build(std::string(kBlock) +
                    "hole id=h1 sketch=s2 type=counterbore diameter=3.4mm extent=all cbore_diameter=6.5mm "
                    "cbore_depth=3mm\n");
    expectAllOk(*cb);
    const double r = 1.7, R = 3.25;
    EXPECT_NEAR(props(*cb).volume, 12000 - 2 * (pi * R * R * 3 + pi * r * r * 7), 1e-3);
    auto cs = build(std::string(kBlock) +
                    "hole id=h1 sketch=s2 type=countersink diameter=3.4mm extent=all csink_diameter=6.5mm "
                    "csink_angle=90deg\n");
    expectAllOk(*cs);
    const double h = R - r;  // 90-degree countersink: depth equals the radius difference
    const double cone = pi * h / 3 * (R * R + R * r + r * r);
    EXPECT_NEAR(props(*cs).volume, 12000 - 2 * (cone + pi * r * r * (10 - h)), 1e-3);
}

TEST(Pattern, RectangularPatternOfAHole)
{
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 corner=0,0 w=60 h=40\n"
                     "extrude id=e1 sketch=s1 distance=5\n"
                     "sketch id=s2 on=e1/end\npoint sketch=s2 at=10,10\n"
                     "hole id=h1 sketch=s2 diameter=4mm extent=all\n"
                     "pattern id=p1 feature=h1 kind=rectangular dir1=X count1=3 spacing1=20mm dir2=Y count2=2 "
                     "spacing2=20mm\n");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).volume, 60 * 40 * 5 - 6 * pi * 4 * 5, 1e-3);
    // 1000 by 1000 is a million solid operations: refused with a reason instead of never finishing
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runLine("set p1 count1=1000 count2=1000").ok());
    const auto& st = doc->status(doc->featureIndex("p1"));
    EXPECT_EQ(st.level, StatusLevel::Error);
    EXPECT_NE(st.message.find("at most 1000"), std::string::npos) << st.message;
}

TEST(Pattern, CopiesThatMissTheBodyAreExplained)
{
    auto doc = std::make_unique<Document>();
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\n"
                          "extrude id=e1 sketch=s1 distance=5\n"
                          "sketch id=s2 on=e1/end\nrect sketch=s2 center=0,0 w=4 h=4\n"
                          "extrude id=e2 sketch=s2 distance=3 op=join\n"
                          "pattern id=p1 feature=e2 kind=rectangular dir1=X count1=3 spacing1=50mm\n")
                    .ok());
    const auto& st = doc->status(4);
    EXPECT_EQ(int(st.level), int(StatusLevel::Error));
    EXPECT_NE(st.message.find("do not touch"), std::string::npos) << st.message;
}

TEST(Mirror, JoinAndNewBody)
{
    // A 20 x 10 x 5 block from x=0 to x=20, mirrored across the side (YZ) plane.
    const std::string half = "sketch id=s1 plane=XY\nrect sketch=s1 corner=0,0 w=20 h=10\n"
                             "extrude id=e1 sketch=s1 distance=5\n";
    auto joined = build(half + "mirror id=m1 plane=YZ\n");
    expectAllOk(*joined);
    auto p = props(*joined);
    EXPECT_NEAR(p.volume, 2000, 1e-3);
    EXPECT_EQ(p.solids, 1);
    EXPECT_NEAR(p.xmin, -20, 1e-6);
    auto separate = build(half + "mirror id=m1 plane=XZ op=new\n");
    expectAllOk(*separate);
    EXPECT_EQ(separate->state().bodies.size(), 2u);
    EXPECT_NEAR(props(*separate).volume, 2000, 1e-3);
}

TEST(SketchShapes, HexagonAndSlotAreFullyDefined)
{
    auto doc = build("sketch id=s1 plane=XY\npolygon sketch=s1 center=0,0 d=20 sides=6\n"
                     "extrude id=e1 sketch=s1 distance=10\n"
                     "sketch id=s2 plane=XY\nslot sketch=s2 c1=30,0 c2=50,0 width=8\n"
                     "extrude id=e2 sketch=s2 distance=10 op=new\n");
    expectAllOk(*doc);
    EXPECT_EQ(doc->state().sketches.at("s1")->report.dof, 0);
    EXPECT_EQ(doc->state().sketches.at("s2")->report.dof, 0);
    const double hex = 3 * std::sqrt(3.0) / 2 * 100;  // regular hexagon, corner radius 10
    const double slot = 20 * 8 + pi * 16;
    EXPECT_NEAR(props(*doc).volume, (hex + slot) * 10, 1e-3);
    auto p = props(*doc);
    EXPECT_NEAR(p.ymax, 10, 1e-6);  // a corner points straight up
}


TEST(Text, GlyphsBecomeClosedAreasCentredOnTheAnchor)
{
    auto doc = build("sketch id=s1 plane=XY\ntext sketch=s1 center=20,10 height=10mm text=\"Jig 8\"\n"
                     "extrude id=e1 sketch=s1 distance=2 op=new\n");
    expectAllOk(*doc);
    const auto& regions = doc->state().sketches.at("s1")->profiles.regions;
    ASSERT_EQ(regions.size(), 5u);  // J, i stem, i dot, g, 8 (the holes of g and 8 stay holes)
    EXPECT_EQ(regions.front().key, "T2.1");
    auto p = props(*doc);
    EXPECT_TRUE(p.valid);
    EXPECT_EQ(p.solids, 5);
    EXPECT_NEAR(p.volume, regionArea(*doc, "s1") * 2, 1e-6);
    EXPECT_NEAR((p.xmin + p.xmax) / 2, 20, 1e-6);
    EXPECT_NEAR((p.ymin + p.ymax) / 2, 10, 1e-6);
    // Inter at a 10 mm size: the outlines are about 22 x 9.7 mm
    EXPECT_NEAR(p.xmax - p.xmin, 22.18, 0.05);
    EXPECT_NEAR(p.ymax - p.ymin, 9.67, 0.05);
}

TEST(Text, CutLetteringFollowsAParameterAndSurvivesSaving)
{
    auto doc = build("param size = 8 mm\n"
                     "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=60 h=20\nextrude id=e1 sketch=s1 distance=5\n"
                     "sketch id=s2 on=e1/end\ntext sketch=s2 center=0,0 height=size text=\"ABC\"\n"
                     "extrude id=e2 sketch=s2 distance=-1mm op=cut\n");
    expectAllOk(*doc);
    const double a8 = regionArea(*doc, "s2");
    EXPECT_NEAR(props(*doc).volume, 6000 - a8, 1e-6);
    run(*doc, "param size = 12 mm");
    expectAllOk(*doc);
    const double a12 = regionArea(*doc, "s2");
    EXPECT_NEAR(a12, a8 * 12 * 12 / 64, a8 * 1e-6);  // area scales with the square of the size
    const double v = props(*doc).volume;
    const auto path = (tempDir() / "text.jig").string();
    ASSERT_TRUE(saveDocument(*doc, path).ok());
    auto back = loadDocument(path);
    ASSERT_TRUE(back.ok()) << back.error().message;
    EXPECT_EQ((*back)->toJson(), doc->toJson());
    EXPECT_NEAR(props(**back).volume, v, 1e-6);
}

TEST(Text, MissingFontFallsBackAndSaysSo)
{
    auto doc = build("sketch id=s1 plane=XY\ntext sketch=s1 center=0,0 height=5mm text=\"Hi\" font=\"No Such Font\"\n");
    const auto& st = doc->status(0);
    EXPECT_EQ(int(st.level), int(StatusLevel::Warning));
    EXPECT_NE(st.message.find("built-in font"), std::string::npos) << st.message;
    EXPECT_EQ(doc->state().sketches.at("s1")->profiles.regions.size(), 3u);  // H, i stem, i dot
}

TEST(Text, AnchorIsNotAHoleCentre)
{
    auto doc = std::make_unique<Document>();
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=5\n"
                          "sketch id=s2 on=e1/end\ntext sketch=s2 center=0,0 height=5mm text=\"X\"\n"
                          "hole id=h1 sketch=s2 diameter=3mm depth=2mm\n")
                    .ok());
    EXPECT_EQ(int(doc->status(3).level), int(StatusLevel::Error));
    EXPECT_NE(doc->status(3).message.find("no points"), std::string::npos) << doc->status(3).message;
}

TEST(Measure, SizesDistancesAnglesAndHoleSpacing)
{
    auto doc = build(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm extent=all\n");
    const auto& body = *doc->state().bodies.at("e1");
    auto face = [&](const std::string& n) {
        auto h = body.findFaces(n);
        EXPECT_EQ(h.size(), 1u) << n;
        return TopoDS_Shape(body.face(h.at(0)));
    };
    // single items
    const auto top = measureItem(face("e1/end"));
    EXPECT_EQ(top.kind, "flat face");
    EXPECT_NEAR(top.size, 40 * 30 - 2 * pi * 4, 1e-6);
    const auto wall = measureItem(face("h1/wall:p1"));
    EXPECT_EQ(wall.kind, "round face (cylinder)");
    ASSERT_TRUE(wall.radius.has_value());
    EXPECT_NEAR(*wall.radius, 2, 1e-9);
    // top to bottom: 10 mm apart, parallel
    auto tb = measureBetween(face("e1/end"), face("e1/start"));
    ASSERT_TRUE(tb.ok());
    EXPECT_NEAR(tb->distance, 10, 1e-9);
    ASSERT_TRUE(tb->angle.has_value());
    EXPECT_NEAR(*tb->angle, 0, 1e-9);
    // top to a side wall: they touch, at 90 degrees
    auto ts = measureBetween(face("e1/end"), face("e1/side:5"));
    ASSERT_TRUE(ts.ok());
    EXPECT_NEAR(ts->distance, 0, 1e-9);
    EXPECT_NEAR(*ts->angle, pi / 2, 1e-9);
    // the two holes: 20 mm centre to centre, 16 mm wall to wall
    auto hh = measureBetween(face("h1/wall:p1"), face("h1/wall:p4"));
    ASSERT_TRUE(hh.ok());
    ASSERT_TRUE(hh->centres.has_value());
    EXPECT_NEAR(*hh->centres, 20, 1e-9);
    EXPECT_NEAR(hh->distance, 16, 1e-6);
    // edge length, and the script command
    const int e = body.findEdges("E[e1/end|e1/side:5]").at(0);
    EXPECT_NEAR(measureItem(body.edge(e)).size, 40, 1e-9);
    ScriptRunner r(*doc);
    ASSERT_TRUE(r.runLine("measure a=face:h1/wall:p1 b=face:h1/wall:p4").ok());
    auto j = nlohmann::json::parse(r.output().back());
    EXPECT_NEAR(j["centre_distance"].get<double>(), 20, 1e-9);
}

TEST(Plane, OffsetAngleAndMidplane)
{
    auto doc = build("param lift = 15 mm\n"
                     "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
                     "plane id=pl1 base=face:e1/end distance=lift\n"
                     "sketch id=s2 plane=pl1\ncircle sketch=s2 center=0,0 d=10\n"
                     "extrude id=e2 sketch=s2 distance=-lift op=join\n"
                     "plane id=pl2 kind=angle base=XZ axis=X angle=30deg\n"
                     "plane id=pl3 kind=midplane base=face:e1/start base2=face:e1/end\n"
                     "sketch id=s3 plane=pl3\nrect sketch=s3 center=0,0 w=60 h=4\n"
                     "extrude id=e3 sketch=s3 distance=4 direction=symmetric op=join\n");
    expectAllOk(*doc);
    const auto& planes = doc->state().planes;
    EXPECT_NEAR(planes.at("pl1").Location().Z(), 25, 1e-9);
    EXPECT_NEAR(planes.at("pl3").Location().Z(), 5, 1e-9);
    // XZ (normal -Y) turned 30 degrees about X
    const gp_Dir n = planes.at("pl2").Direction();
    EXPECT_NEAR(n.Y(), -std::cos(pi / 6), 1e-9);
    EXPECT_NEAR(n.Z(), -std::sin(pi / 6), 1e-9);
    // block + post down to the top face + the part of the rib outside the block
    EXPECT_NEAR(props(*doc).volume, 12000 + pi * 25 * 15 + 20 * 4 * 4, 1e-6);
    // the post follows the plane's parameter
    run(*doc, "param lift = 20 mm");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).zmax, 30, 1e-9);
    EXPECT_NEAR(props(*doc).volume, 12000 + pi * 25 * 20 + 20 * 4 * 4, 1e-6);
}

TEST(Plane, ImpossiblePlanesAreExplained)
{
    auto doc = std::make_unique<Document>();
    ScriptRunner r(*doc);
    r.strict = false;
    ASSERT_TRUE(r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
                          "plane id=pl1 kind=midplane base=face:e1/end base2=YZ\n"
                          "plane id=pl2 kind=angle base=XY axis=Z angle=30deg\n")
                    .ok());
    EXPECT_EQ(int(doc->status(2).level), int(StatusLevel::Error));
    EXPECT_NE(doc->status(2).message.find("parallel"), std::string::npos) << doc->status(2).message;
    EXPECT_EQ(int(doc->status(3).level), int(StatusLevel::Error));
    EXPECT_NE(doc->status(3).message.find("axis"), std::string::npos) << doc->status(3).message;
}

TEST(Bodies, CombineSplitMoveAndCopy)
{
    const std::string two = "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                            "sketch id=s2 plane=XY\ncircle sketch=s2 center=20,0 d=10\n"
                            "extrude id=e2 sketch=s2 distance=30 op=new\n";
    {
        auto doc = build(two + "combine id=cb1 target=e1 tools=e2 op=cut\n");
        expectAllOk(*doc);
        EXPECT_EQ(doc->state().bodies.size(), 1u);  // the tool body is used up
        EXPECT_NEAR(props(*doc).volume, 8000 - pi * 25 * 10 / 2, 1e-6);  // half the post was inside
    }
    {
        auto doc = build(two + "combine id=cb1 target=e1 tools=e2 op=join keep=yes\n");
        expectAllOk(*doc);
        EXPECT_EQ(doc->state().bodies.size(), 2u);
        EXPECT_NEAR(massProperties(doc->state().bodies.at("e1")->shape()).volume, 8000 + pi * 25 * 30 - pi * 25 * 10 / 2, 1e-6);
    }
    {
        // split the block by the side plane: two equal halves, the cut faces named
        auto doc = build(two + "split id=sp1 body=e1 plane=YZ\n");
        expectAllOk(*doc);
        const double a = massProperties(doc->state().bodies.at("e1")->shape()).volume;
        const double b = massProperties(doc->state().bodies.at("sp1")->shape()).volume;
        EXPECT_NEAR(a, 4000, 1e-6);
        EXPECT_NEAR(b, 4000, 1e-6);
        EXPECT_EQ(doc->state().bodies.at("e1")->findFaces("sp1/split").size(), 1u);
        EXPECT_GT(massProperties(doc->state().bodies.at("e1")->shape()).xmin, -1e-6);  // e1 keeps the +X side
    }
    {
        auto doc = build(two + "move id=mv1 bodies=e2 dx=10mm dz=5mm\nmove id=mv2 bodies=e1 copy=yes axis=Z angle=90deg\n");
        expectAllOk(*doc);
        auto post = massProperties(doc->state().bodies.at("e2")->shape());
        EXPECT_NEAR(post.xmin, 25, 1e-6);
        EXPECT_NEAR(post.zmin, 5, 1e-6);
        auto turned = massProperties(doc->state().bodies.at("mv2")->shape());
        EXPECT_NEAR(turned.xmax - turned.xmin, 20, 1e-6);  // the copy is turned a quarter
        EXPECT_NEAR(turned.ymax - turned.ymin, 40, 1e-6);
        EXPECT_EQ(doc->state().bodies.size(), 3u);
        // moved faces keep their names, so later steps still find them
        EXPECT_EQ(doc->state().bodies.at("e2")->findFaces("e2/end").size(), 1u);
    }
    {
        auto doc = std::make_unique<Document>();
        ScriptRunner r(*doc);
        r.strict = false;
        ASSERT_TRUE(r.runText(two + "plane id=pl1 base=YZ distance=100mm\nsplit id=sp1 body=e1 plane=pl1\n").ok());
        EXPECT_EQ(int(doc->status(5).level), int(StatusLevel::Error));
        EXPECT_NE(doc->status(5).message.find("does not cross"), std::string::npos) << doc->status(5).message;
    }
}

TEST(SketchTrim, LinesCirclesAndLoneCurves)
{
    // An X of two lines: trimming the right arm leaves a 10 mm line that ends on the other.
    auto x = build("sketch id=s1 plane=XY\nline sketch=s1 p1=-10,0 p2=10,0\nline sketch=s1 p1=0,-10 p2=0,10\n");
    auto lineCount = [](const Document& d) {
        int n = 0;
        for (auto& [id, e] : d.state().sketches.at("s1")->solved.entities()) {
            n += e.type == EntityType::Line;
        }
        return n;
    };
    // the lines' ends are dimensioned: trimming must keep the sketch solvable
    auto notError = [](const Document& d) { return d.status(0).level != StatusLevel::Error; };
    run(*x, "trim sketch=s1 at=5,0");
    EXPECT_TRUE(notError(*x)) << x->status(0).message;  // an open sketch only warns
    const auto& sk = x->state().sketches.at("s1")->solved;
    double longest = 0;
    for (auto& [id, e] : sk.entities()) {
        if (e.type == EntityType::Line) {
            auto [ax, ay] = sk.pointXY(e.p1);
            auto [bx, by] = sk.pointXY(e.p2);
            longest = std::max(longest, std::hypot(bx - ax, by - ay));
            if (std::abs(ay) < 1e-9 && std::abs(by) < 1e-9) {
                EXPECT_NEAR(std::max(ax, bx), 0, 1e-9);  // the horizontal line now stops at x = 0
            }
        }
    }
    EXPECT_NEAR(longest, 20, 1e-9);  // the vertical line is untouched
    EXPECT_EQ(lineCount(*x), 2);

    // Trimming the middle of a line splits it in two.
    auto mid = build("sketch id=s1 plane=XY\nline sketch=s1 p1=-10,0 p2=10,0\nline sketch=s1 p1=-5,-5 p2=-5,5\n"
                     "line sketch=s1 p1=5,-5 p2=5,5\ntrim sketch=s1 at=0,0\n");
    EXPECT_TRUE(notError(*mid)) << mid->status(0).message;
    EXPECT_EQ(lineCount(*mid), 4);

    // A circle over a rectangle corner: trimming the outside part leaves a quarter arc that
    // splits the rectangle into two closed areas.
    auto rc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\ncircle sketch=s1 center=10,10 d=10\n");
    EXPECT_EQ(rc->state().sketches.at("s1")->profiles.regions.size(), 3u);
    run(*rc, "trim sketch=s1 at=14,14");
    expectAllOk(*rc);
    const auto& regions = rc->state().sketches.at("s1")->profiles.regions;
    ASSERT_EQ(regions.size(), 2u);
    std::vector<double> areas {regions[0].area, regions[1].area};
    std::sort(areas.begin(), areas.end());
    EXPECT_NEAR(areas[0], pi * 25 / 4, 1e-6);
    EXPECT_NEAR(areas[0] + areas[1], 400, 1e-6);
    // the circle's diameter dimension became a radius on the arc
    bool radius = false;
    for (auto& [id, c] : rc->state().sketches.at("s1")->solved.constraints()) {
        radius = radius || c.type == ConstraintType::Radius;
    }
    EXPECT_TRUE(radius);

    // A line that nothing crosses is removed entirely.
    auto lone = build("sketch id=s1 plane=XY\nline sketch=s1 p1=0,0 p2=10,0\ncircle sketch=s1 center=50,50 d=4\n"
                      "trim sketch=s1 at=5,0\n");
    EXPECT_EQ(lineCount(*lone), 0);
}

TEST(SketchOffset, OutlinesAndSingleCurves)
{
    auto areas = [](const Document& d) {
        std::vector<double> a;
        for (auto& r : d.state().sketches.at("s1")->profiles.regions) {
            a.push_back(r.area);
        }
        std::sort(a.begin(), a.end());
        return a;
    };
    // rectangle, outward 3 mm with sharp corners: a 46 x 26 frame around it
    auto out = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\noffset sketch=s1 at=20,0 distance=3\n");
    ASSERT_EQ(areas(*out).size(), 2u);
    EXPECT_NEAR(areas(*out)[0] + areas(*out)[1], 46 * 26, 1e-6);
    // inward: a 34 x 14 rectangle inside
    auto in = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\noffset sketch=s1 at=20,0 distance=-3\n");
    ASSERT_EQ(areas(*in).size(), 2u);
    EXPECT_NEAR(areas(*in)[1], 34 * 14, 1e-6);   // the inner rectangle
    EXPECT_NEAR(areas(*in)[0], 800 - 34 * 14, 1e-6);  // the ring between them
    // a slot (lines and arcs) grows into a wider slot
    auto slot = build("sketch id=s1 plane=XY\nslot sketch=s1 c1=0,0 c2=20,0 width=8\noffset sketch=s1 at=10,4 distance=2\n");
    ASSERT_EQ(areas(*slot).size(), 2u);
    EXPECT_NEAR(areas(*slot)[0] + areas(*slot)[1], 20 * 12 + pi * 36, 1e-6);
    // single curves: a circle grows (and stays concentric), a line moves to its left
    auto circ = build("sketch id=s1 plane=XY\ncircle sketch=s1 center=5,5 d=10\noffset sketch=s1 at=10,5 distance=2\n");
    ASSERT_EQ(areas(*circ).size(), 2u);
    EXPECT_NEAR(areas(*circ)[0], pi * 49 - pi * 25, 1e-6);  // the ring between them
    EXPECT_NEAR(areas(*circ)[1], pi * 25, 1e-6);            // the original disc
    auto line = build("sketch id=s1 plane=XY\nline sketch=s1 p1=0,0 p2=10,0\noffset sketch=s1 at=5,0 distance=4\n");
    int above = 0;
    for (auto& [id, e] : line->state().sketches.at("s1")->solved.entities()) {
        if (e.type == EntityType::Line) {
            above += std::abs(line->state().sketches.at("s1")->solved.pointXY(e.p1).second - 4) < 1e-9;
        }
    }
    EXPECT_EQ(above, 1);
    // an inward offset that cannot fit is explained
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    auto res = r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\noffset sketch=s1 at=20,0 distance=-12\n");
    ASSERT_FALSE(res.ok());
    EXPECT_NE(res.error().message.find("could not be offset"), std::string::npos) << res.error().message;
}

TEST(ExtrudeOptions, TwoSidesTaperAndThin)
{
    const std::string sq = "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\n";
    const double t5 = std::tan(5 * pi / 180);
    auto frustum = [&](double h) {  // a 20 x 20 square narrowing at 5 degrees over h
        const double c = 20 - 2 * h * t5;
        return h / 3 * (400 + 20 * c + c * c);
    };
    auto two = build(sq + "extrude id=e1 sketch=s1 distance=10 direction=two distance2=4\n");
    expectAllOk(*two);
    EXPECT_NEAR(props(*two).volume, 400 * 14, 1e-6);
    EXPECT_NEAR(props(*two).zmin, -4, 1e-9);
    auto taper = build(sq + "extrude id=e1 sketch=s1 distance=10 taper=5deg\n");
    expectAllOk(*taper);
    EXPECT_NEAR(props(*taper).volume, frustum(10), 1e-6);
    EXPECT_NEAR(props(*taper).zmax, 10, 1e-9);  // the height is the vertical distance
    EXPECT_EQ(taper->state().bodies.at("e1")->findFaces("e1/end").size(), 1u);
    EXPECT_EQ(taper->state().bodies.at("e1")->findFaces("e1/start").size(), 1u);
    // both sides narrow away from the sketch plane
    auto both = build(sq + "extrude id=e1 sketch=s1 distance=10 direction=two distance2=4 taper=5deg\n");
    expectAllOk(*both);
    EXPECT_NEAR(props(*both).volume, frustum(10) + frustum(4), 1e-6);
    EXPECT_NEAR(props(*both).xmax, 10, 1e-9);
    // a negative taper widens
    auto out = build(sq + "extrude id=e1 sketch=s1 distance=10 taper=-5deg\n");
    expectAllOk(*out);
    EXPECT_NEAR(props(*out).xmax, 10 + 10 * t5, 1e-9);
    // thin walls: centred, inside, outside
    auto thin = build(sq + "extrude id=e1 sketch=s1 distance=10 thin=2mm\n");
    expectAllOk(*thin);
    EXPECT_NEAR(props(*thin).volume, (22 * 22 - 18 * 18) * 10, 1e-6);
    auto inside = build(sq + "extrude id=e1 sketch=s1 distance=10 thin=2mm thin_side=inside\n");
    EXPECT_NEAR(props(*inside).volume, (400 - 16 * 16) * 10, 1e-6);
    auto outside = build(sq + "extrude id=e1 sketch=s1 distance=10 thin=2mm thin_side=outside\n");
    EXPECT_NEAR(props(*outside).volume, (24 * 24 - 400) * 10, 1e-6);
    // too steep is explained
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.strict = false;
    ASSERT_TRUE(r.runText(sq + "extrude id=e1 sketch=s1 distance=200 taper=10deg\n").ok());
    EXPECT_EQ(int(bad->status(1).level), int(StatusLevel::Error));
}

TEST(SketchRound, CornersBecomeTangentArcs)
{
    // round all four corners of a 40 x 20 rectangle with r = 5: area 800 - 4 (r^2 - pi r^2 / 4)
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\n"
                     "round sketch=s1 at=20,10 r=5\nround sketch=s1 at=-20,10 r=5\n"
                     "round sketch=s1 at=-20,-10 r=5\nround sketch=s1 at=20,-10 r=5\n"
                     "extrude id=e1 sketch=s1 distance=10\n");
    expectAllOk(*doc);
    const auto& regions = doc->state().sketches.at("s1")->profiles.regions;
    ASSERT_EQ(regions.size(), 1u);
    EXPECT_NEAR(regions[0].area, 800 - 4 * (25 - pi * 25 / 4), 1e-6);
    // the rounded block has 4 quarter-cylinder faces, and the size dimensions still hold
    auto p = props(*doc);
    EXPECT_EQ(p.faces, 10);
    EXPECT_NEAR(p.xmax - p.xmin, 40, 1e-9);
    EXPECT_NEAR(p.ymax - p.ymin, 20, 1e-9);
    // the radius follows a parameter
    auto prm = build("param rr = 3 mm\nsketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nround sketch=s1 at=20,10 r=rr\n");
    run(*prm, "param rr = 6 mm");
    expectAllOk(*prm);
    EXPECT_NEAR(prm->state().sketches.at("s1")->profiles.regions[0].area, 800 - (36 - pi * 36 / 4), 1e-6);
    // too big for the corner is explained
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    auto res = r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nround sketch=s1 at=20,10 r=25\n");
    ASSERT_FALSE(res.ok());
    EXPECT_NE(res.error().message.find("does not fit"), std::string::npos) << res.error().message;
}

TEST(DxfImport, InchDrawingWithPolylinesBulgesArcsAndCircles)
{
    const std::string path = std::string(JIG_SOURCE_DIR) + "/tests/data/plate_inch.dxf";
    auto d = readDxf(path);
    ASSERT_TRUE(d.ok()) << d.error().message;
    EXPECT_TRUE(d->unitsKnown);
    EXPECT_NEAR(d->unitScale, 25.4, 1e-12);
    EXPECT_EQ(d->texts.size(), 1u);  // its label comes in as sketch text now
    auto doc = build("sketch id=s1 plane=XY\nimport dxf \"" + path + "\" sketch=s1\n");
    const auto& regions = doc->state().sketches.at("s1")->profiles.regions;
    std::vector<double> a;
    for (auto& r : regions) {
        if (r.key.rfind("T", 0) != 0) {  // not the letters
            a.push_back(r.area / (25.4 * 25.4));  // in square inches
        }
    }
    std::sort(a.begin(), a.end());
    ASSERT_EQ(a.size(), 4u);  // circle, D, stadium, plate around the holes
    const double circle = pi / 16, dshape = pi * 0.25 / 2, stadium = 2 + pi * 0.25;
    EXPECT_NEAR(a[0], circle, 1e-6);
    EXPECT_NEAR(a[1], dshape, 1e-6);
    EXPECT_NEAR(a[2], stadium, 1e-6);
    EXPECT_NEAR(a[3], 8 - circle - dshape, 1e-6);
    // the unit can be overridden, and junk is refused politely
    auto mm = build("sketch id=s1 plane=XY\nimport dxf \"" + path + "\" sketch=s1 units=mm\n");
    double total = 0;
    for (auto& r : mm->state().sketches.at("s1")->profiles.regions) {
        if (r.key.rfind("T", 0) != 0) {
            total += r.area;
        }
    }
    EXPECT_NEAR(total, 8 + stadium, 1e-6);  // the same numbers, read as millimetres
    EXPECT_FALSE(parseDxf("this is not a dxf").ok());
    // group codes that are not numbers ("-", too long) and a units value that is not one:
    // refused or ignored, never an exception
    EXPECT_NO_THROW(EXPECT_FALSE(parseDxf("0\nSECTION\n-\nHEADER\n").ok()));
    EXPECT_NO_THROW(EXPECT_FALSE(parseDxf("0\nSECTION\n99999999999\nHEADER\n").ok()));
    EXPECT_NO_THROW(parseDxf("0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\nmm\n0\nENDSEC\n0\nEOF\n"));
    EXPECT_FALSE(parseDxf("0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n").ok());
}

TEST(DxfImport, BlocksSplinesEllipsesAndText)
{
    // a block (a 10 x 10 square with its corner at the block's base) placed twice: once at
    // (100, 0); once at (0, 50) turned 90 degrees and mirrored; a spline through fit points,
    // a whole ellipse and an MTEXT with a line break
    const std::string dxf =
        "0\nSECTION\n2\nHEADER\n9\n$INSUNITS\n70\n4\n0\nENDSEC\n"
        "0\nSECTION\n2\nBLOCKS\n0\nBLOCK\n2\nSQ\n10\n0\n20\n0\n"
        "0\nLWPOLYLINE\n90\n4\n70\n1\n10\n0\n20\n0\n10\n10\n20\n0\n10\n10\n20\n10\n10\n0\n20\n10\n"
        "0\nENDBLK\n0\nENDSEC\n"
        "0\nSECTION\n2\nENTITIES\n"
        "0\nINSERT\n2\nSQ\n10\n100\n20\n0\n"
        "0\nINSERT\n2\nSQ\n10\n0\n20\n50\n41\n-1\n42\n1\n50\n90\n"
        "0\nSPLINE\n70\n0\n71\n3\n11\n0\n21\n-30\n11\n10\n21\n-20\n11\n20\n21\n-30\n"
        "0\nELLIPSE\n10\n50\n20\n50\n11\n8\n21\n0\n40\n0.5\n41\n0\n42\n6.283185307179586\n"
        "0\nMTEXT\n10\n0\n20\n100\n40\n5\n1\n{\\fArial;HI}\\PTHERE\n"
        "0\nENDSEC\n0\nEOF\n";
    auto d = parseDxf(dxf);
    ASSERT_TRUE(d.ok()) << d.error().message;
    EXPECT_EQ(d->lines.size(), 8u);  // two squares
    // the plain copy at x 100..110; the turned, mirrored one: (x, y) -> mirror -> turn 90 -> +(0, 50)
    double minX = 1e9, maxX = -1e9;
    for (auto& l : d->lines) {
        if (l.x1 > 50) {
            minX = std::min({minX, l.x1, l.x2});
            maxX = std::max({maxX, l.x1, l.x2});
        }
    }
    EXPECT_NEAR(minX, 100, 1e-9);
    EXPECT_NEAR(maxX, 110, 1e-9);
    bool turned = false;  // the point (10, 0) of the block: mirrored (-10, 0), turned (0, -10), moved (0, 40)
    for (auto& l : d->lines) {
        turned = turned || (std::abs(l.x1) < 1e-9 && std::abs(l.y1 - 40) < 1e-9) || (std::abs(l.x2) < 1e-9 && std::abs(l.y2 - 40) < 1e-9);
    }
    EXPECT_TRUE(turned);
    ASSERT_EQ(d->splines.size(), 1u);
    EXPECT_EQ(d->splines[0].points.size(), 3u);
    ASSERT_EQ(d->ellipses.size(), 1u);
    ASSERT_EQ(d->texts.size(), 1u);
    EXPECT_EQ(d->texts[0].text, "HI\\nTHERE");  // formatting codes out, the break kept
    // into a sketch: the squares and the ellipse are closed areas (the ellipse: pi * 8 * 4)
    Sketch s;
    EXPECT_GT(addDxfToSketch(s, d.value(), 1.0), 0);
    auto prof = buildProfiles(s, originPlaneFrame(OriginPlane::XY, 0));
    ASSERT_TRUE(prof.ok());
    int squares = 0;
    bool ellipse = false;
    for (auto& r : prof->regions) {
        squares += std::abs(r.area - 100) < 1e-6;
        ellipse = ellipse || std::abs(r.area - pi * 32) < 1e-6;
    }
    EXPECT_EQ(squares, 2);
    EXPECT_TRUE(ellipse);
    // a control-point spline (no fit points) is followed from its knots
    auto cp = parseDxf("0\nSECTION\n2\nENTITIES\n0\nSPLINE\n70\n8\n71\n2\n40\n0\n40\n0\n40\n0\n40\n1\n40\n1\n40\n1\n"
                       "10\n0\n20\n0\n10\n5\n20\n10\n10\n10\n20\n0\n0\nENDSEC\n0\nEOF\n");
    ASSERT_TRUE(cp.ok()) << cp.error().message;
    ASSERT_EQ(cp->splines.size(), 1u);
    double peak = 0;
    for (auto [x, y] : cp->splines[0].points) {
        peak = std::max(peak, y);
    }
    EXPECT_NEAR(peak, 5, 1e-6);  // a quadratic Bezier from (0,0) over (5,10) to (10,0) peaks at 5
}

TEST(DxfImport, DamagedFilesNeverCrash)
{
    const std::string good = readFile(std::string(JIG_SOURCE_DIR) + "/tests/data/plate_inch.dxf");
    std::mt19937 rng(7);
    int parsed = 0;
    for (int i = 0; i < 3000; ++i) {
        std::string bad = good;
        const int edits = 1 + int(rng() % 6);
        for (int k = 0; k < edits; ++k) {
            const size_t at = rng() % bad.size();
            switch (rng() % 4) {
                case 0: bad[at] = char(rng() % 256); break;              // flip a byte
                case 1: bad.erase(at, rng() % 20); break;                 // drop some
                case 2: bad.insert(at, std::string(rng() % 12, '9')); break;  // huge numbers
                default: bad.insert(at, "\n0\nLWPOLYLINE\n42\n"); break;  // structure noise
            }
        }
        auto r = parseDxf(bad);  // must return, never throw or crash
        parsed += r.ok();
        if (r.ok()) {
            Sketch s;
            addDxfToSketch(s, r.value(), 1.0);
        }
    }
    EXPECT_GT(parsed, 0);  // many mutations still read; the rest are refused cleanly
}

TEST(SvgImport, StrokesAndText)
{
    auto areas = [](const SvgArtwork& art) {
        Sketch s;
        const int id = s.addArtwork(0, 0, art.contours, art.width, {}, art.width, "t");
        (void)id;
        auto prof = buildProfiles(s, originPlaneFrame(OriginPlane::XY, 0));
        std::vector<double> a;
        for (auto& r : prof->regions) {
            a.push_back(r.area);
        }
        std::sort(a.begin(), a.end());
        return a;
    };
    // a line 20 long drawn 2 wide: a band with round ends, 20 x 2 + a circle of radius 1
    auto line = parseSvg("<svg xmlns='http://www.w3.org/2000/svg' width='100mm' viewBox='0 0 100 100'>"
                         "<line x1='10' y1='10' x2='30' y2='10' stroke='black' stroke-width='2'/></svg>");
    ASSERT_TRUE(line.ok()) << line.error().message;
    auto la = areas(*line);
    ASSERT_EQ(la.size(), 1u);
    EXPECT_NEAR(la[0], 40 + pi, 0.05);  // round ends are made of short straight pieces
    // a circle of radius 10 drawn 2 wide (style syntax): a ring between radius 9 and 11
    auto ring = parseSvg("<svg xmlns='http://www.w3.org/2000/svg' width='100mm' viewBox='0 0 100 100'>"
                         "<circle cx='50' cy='50' r='10' style='fill:none;stroke:#000;stroke-width:2'/></svg>");
    ASSERT_TRUE(ring.ok()) << ring.error().message;
    auto ra = areas(*ring);
    double total = 0;
    for (double a : ra) {
        total += a;
    }
    EXPECT_NEAR(total, pi * (121 - 81), 0.5);
    // an open L-shaped polyline, a path left open, text, and a filled square side by side
    auto mixed = parseSvg("<svg xmlns='http://www.w3.org/2000/svg' width='200mm' viewBox='0 0 200 200'>"
                          "<polyline points='0,0 20,0 20,20' fill='none' stroke='black' stroke-width='1'/>"
                          "<path d='M40 0 L60 0' stroke='black'/>"
                          "<text x='80' y='40' font-size='20'>OK</text>"
                          "<rect x='150' y='0' width='10' height='10'/></svg>");
    ASSERT_TRUE(mixed.ok()) << mixed.error().message;
    auto ma = areas(*mixed);
    EXPECT_GE(ma.size(), 5u);  // the L, the open path drawn as a band, the O (a ring), the K and the square
    bool square = false;
    for (double a : ma) {
        square = square || std::abs(a - 100) < 1e-6;
    }
    EXPECT_TRUE(square);
    EXPECT_FALSE(mixed->skipped.count("text"));
}

TEST(SvgImport, ShapesHolesTransformsAndUnits)
{
    const std::string path = std::string(JIG_SOURCE_DIR) + "/tests/data/badge.svg";
    auto art = readSvg(path);
    ASSERT_TRUE(art.ok()) << art.error().message;
    EXPECT_TRUE(art->unitsKnown);
    EXPECT_NEAR(art->width, 72, 1e-9);  // from the plate's left edge to the square's right edge
    EXPECT_EQ(art->skipped.at("unfilled path"), 1);
    auto doc = build("sketch id=s1 plane=XY\nimport svg \"" + path + "\" sketch=s1\n"
                     "extrude id=e1 sketch=s1 distance=2\n");
    expectAllOk(*doc);
    std::vector<double> a;
    for (auto& r : doc->state().sketches.at("s1")->profiles.regions) {
        a.push_back(r.area);
    }
    std::sort(a.begin(), a.end());
    ASSERT_EQ(a.size(), 3u);  // triangle, square, the plate with two holes
    EXPECT_NEAR(a[0], 50, 1e-6);
    EXPECT_NEAR(a[1], 100, 1e-6);
    // the circle is four cubic arcs (exact to about 0.03 %), the half circle an SVG arc
    EXPECT_NEAR(a[2], 800 - pi * 25 - pi * 25 / 2, 0.05);
    // scaled to half its 72 mm width: every area a quarter
    auto half = build("sketch id=s1 plane=XY\nimport svg \"" + path + "\" sketch=s1 width=36mm\n");
    double total = 0;
    for (auto& r : half->state().sketches.at("s1")->profiles.regions) {
        total += r.area;
    }
    EXPECT_NEAR(total, (a[0] + a[1] + a[2]) / 4, 1e-6);
    // junk is refused, and mutated files never crash
    EXPECT_FALSE(parseSvg("not svg").ok());
    EXPECT_FALSE(parseSvg("<svg></svg>").ok());
    const std::string good = readFile(path);
    std::mt19937 rng(11);
    for (int i = 0; i < 1500; ++i) {
        std::string bad = good;
        for (int k = 0; k < 1 + int(rng() % 5); ++k) {
            const size_t at = rng() % bad.size();
            switch (rng() % 4) {
                case 0: bad[at] = char(rng() % 256); break;
                case 1: bad.erase(at, rng() % 15); break;
                case 2: bad.insert(at, "1e999 -.5.5,"); break;
                default: bad.insert(at, "<g transform=\"rotate(90 5 5) scale(0)\"><path d=\"M0 0 A0 0 0 1 1 5 5\"/>"); break;
            }
        }
        auto r = parseSvg(bad);
        if (r.ok()) {
            SketchArtwork sa;
            sa.contours = r->contours;
            sa.naturalWidth = r->width;
            (void)artworkFaces(sa, 0, 0, gp_Ax3());
        }
    }
}

TEST(EdgeOptions, ChamferTwoDistancesAngleAndVariableFillet)
{
    // one top edge of a 40 x 20 x 10 block, along X: the bevel removes a triangle prism
    const std::string block = "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n";
    const std::string edge = "name:E[e1/end|e1/side:5]";
    auto removed = [&](const std::string& line) {
        auto d = build(block + line + "\n");
        expectAllOk(*d);
        return 8000 - props(*d).volume;
    };
    EXPECT_NEAR(removed("chamfer id=c1 edges=\"" + edge + "\" distance=2mm"), 0.5 * 2 * 2 * 40, 1e-6);
    // 2 mm on one face, 4 mm on the other: same triangle area whichever way round
    const double two = removed("chamfer id=c1 edges=\"" + edge + "\" distance=2mm type=two distance2=4mm");
    EXPECT_NEAR(two, 0.5 * 2 * 4 * 40, 1e-6);
    EXPECT_NEAR(removed("chamfer id=c1 edges=\"" + edge + "\" distance=2mm type=two distance2=4mm flip=yes"), two, 1e-6);
    // 2 mm at 30 degrees: the other leg is 2 tan 30
    EXPECT_NEAR(removed("chamfer id=c1 edges=\"" + edge + "\" distance=2mm type=angle angle=30deg"),
                0.5 * 2 * 2 * std::tan(pi / 6) * 40, 1e-6);
    // variable fillet from 1 mm to 3 mm: removes more than a 1 mm round, less than 3 mm
    const double v = removed("fillet id=f1 edges=\"" + edge + "\" radius=1mm type=variable radius2=3mm");
    EXPECT_GT(v, (1 - pi / 4) * 1 * 40);
    EXPECT_LT(v, (1 - pi / 4) * 9 * 40);
}

TEST(SketchProject, FaceOutlinesAndRoundEdges)
{
    // a block with a round hole; project its top outline into a sketch 20 mm above
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                     "sketch id=s3 on=e1/end\ncircle sketch=s3 center=5,0 d=8\nextrude id=h sketch=s3 extent=all op=cut\n"
                     "sketch id=s2 plane=XY offset=20mm\nproject face:e1/end sketch=s2\n"
                     "extrude id=e2 sketch=s2 distance=3 op=new\n");
    const auto& sk = *doc->state().sketches.at("s2");
    EXPECT_EQ(sk.report.dof, 0);  // projected geometry is fixed
    double total = 0;
    for (auto& r : sk.profiles.regions) {
        total += r.area;
    }
    EXPECT_NEAR(total, 800, 1e-6);  // the plate area plus the hole's disc
    EXPECT_EQ(sk.profiles.regions.size(), 2u);
    // an edge seen edge-on projects to a point and is refused politely
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    auto res = r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                         "sketch id=s2 plane=XY offset=20mm\nproject edge:E[e1/side:5|e1/side:6] sketch=s2\n");
    ASSERT_FALSE(res.ok());
    EXPECT_NE(res.error().message.find("point"), std::string::npos) << res.error().message;
}

TEST(SketchExtend, LinesAndArcsReachTheNextCurve)
{
    Sketch s;
    const int h = s.addLine(0, 0, 10, 0);
    s.addLine(20, -5, 20, 5);
    // the loose right end runs on to the vertical line
    auto r = s.extend(h, 9, 0);
    ASSERT_TRUE(r.ok()) << r.error().message;
    auto [x, y] = s.pointXY(s.entity(h)->p2);
    EXPECT_NEAR(x, 20, 1e-9);
    EXPECT_NEAR(y, 0, 1e-9);
    EXPECT_NE(s.solve().state, SolveState::Failed);
    EXPECT_NEAR(s.pointXY(s.entity(h)->p2).first, 20, 1e-9);  // held on the line
    // nothing beyond the left end
    EXPECT_FALSE(s.extend(h, 1, 0).ok());
    // a joined end is refused
    const int v = s.addLine(s.entity(h)->p1, s.addPoint(0, 10));
    (void)v;
    EXPECT_FALSE(s.extend(h, 1, 0).ok());

    // an arc from 0° to 90° (r 10) grows counter-clockwise until x = -5: at 120°
    Sketch a;
    const int arc = a.addArc(0, 0, 10, 0, pi / 2);
    a.addLine(-5, -20, -5, 20);
    ASSERT_TRUE(a.extend(arc, 0, 10).ok());
    EXPECT_NEAR(a.entity(arc)->a1, 2 * pi / 3, 1e-9);
    // its start runs clockwise to the same line's other crossing, at 240°
    ASSERT_TRUE(a.extend(arc, 10, 0).ok());
    EXPECT_NEAR(std::cos(a.entity(arc)->a0), -0.5, 1e-9);
    EXPECT_NEAR(std::sin(a.entity(arc)->a0), -std::sqrt(3.0) / 2, 1e-9);
    EXPECT_NE(a.solve().state, SolveState::Failed);
}

TEST(SketchExtend, LinesRunOnToEllipsesAndSmoothCurves)
{
    // an ellipse centred at (30, 0), radii 10 and 5: the line y = 3 meets it at x = 22
    Sketch s;
    const int l = s.addLine(0, 3, 10, 3);
    const int el = s.addEllipse(30, 0, 40, 0, 5);
    auto r = s.extend(l, 9, 3);
    ASSERT_TRUE(r.ok()) << r.error().message;
    EXPECT_NEAR(s.pointXY(s.entity(l)->p2).first, 22, 0.05);
    EXPECT_NEAR(s.pointXY(s.entity(l)->p2).second, 3, 1e-9);
    // held on the ellipse: once solved it is exactly on it
    s.addConstraint(ConstraintType::Fix, s.entity(el)->center);
    s.addConstraint(ConstraintType::Fix, s.entity(el)->p1);
    ASSERT_NE(s.solve().state, SolveState::Failed);
    {
        auto [ex, ey] = s.pointXY(s.entity(l)->p2);
        EXPECT_NEAR((ex - 30) * (ex - 30) / 100 + ey * ey / 25, 1, 1e-9);
    }
    // a smooth curve through (50, -10), (50, 0), (50, 10): the line's other end runs to it
    Sketch c;
    const int m = c.addLine(30, 0, 40, 0);
    c.addSpline({{50, -10}, {52, 0}, {50, 10}});
    ASSERT_TRUE(c.extend(m, 39, 0).ok());
    EXPECT_NEAR(c.pointXY(c.entity(m)->p2).first, 52, 0.05);
    // an ellipse is closed: nothing to extend
    EXPECT_FALSE(s.extend(el, 40, 0).ok());
    // an open smooth curve runs on straight from its end, the way it leaves it: through
    // (0,0), (10,5), (20,0) it leaves along (10,-5) and meets the line x = 30 at (30,-5)
    Sketch o;
    const int sp = o.addSpline({{0, 0}, {10, 5}, {20, 0}});
    o.addLine(30, -20, 30, 20);
    auto e = o.extend(sp, 19, 0);
    ASSERT_TRUE(e.ok()) << e.error().message;
    ASSERT_EQ(o.entity(sp)->pts.size(), 4u);
    EXPECT_NEAR(o.pointXY(o.entity(sp)->pts.back()).first, 30, 1e-9);
    EXPECT_NEAR(o.pointXY(o.entity(sp)->pts.back()).second, -5, 1e-9);
    EXPECT_NE(o.solve().state, SolveState::Failed);
    EXPECT_NEAR(o.pointXY(o.entity(sp)->pts.back()).first, 30, 1e-9);  // held on the line
    // its other end: leaves along (-10,-5); nothing there
    EXPECT_FALSE(o.extend(sp, 1, 0).ok());
    // a closed one has no ends
    Sketch cl;
    EXPECT_FALSE(cl.extend(cl.addSpline({{0, 0}, {10, 5}, {20, 0}}, true), 20, 0).ok());
}

TEST(SketchBreak, CurvesSplitWhereOthersCross)
{
    Sketch s;
    const int l = s.addLine(0, 0, 30, 0);
    s.addLine(10, -5, 10, 5);
    s.addLine(20, -5, 20, 5);
    auto r = s.breakAt(l, 15, 0);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r->size(), 3u);
    double total = 0;
    for (int id : *r) {
        auto [ax, ay] = s.pointXY(s.entity(id)->p1);
        auto [bx, by] = s.pointXY(s.entity(id)->p2);
        total += std::hypot(bx - ax, by - ay);
    }
    EXPECT_NEAR(total, 30, 1e-9);
    EXPECT_NE(s.solve().state, SolveState::Failed);
    // a lone line has nothing to break at
    Sketch lone;
    EXPECT_FALSE(lone.breakAt(lone.addLine(0, 0, 5, 0), 2, 0).ok());

    // a circle cut by a line becomes two half arcs
    Sketch c;
    const int circ = c.addCircle(0, 0, 10);
    c.addLine(-20, 0, 20, 0);
    auto rc = c.breakAt(circ, 0, 10);
    ASSERT_TRUE(rc.ok()) << rc.error().message;
    ASSERT_EQ(rc->size(), 2u);
    double span = 0;
    for (int id : *rc) {
        EXPECT_EQ(c.entity(id)->type, EntityType::Arc);
        span += c.entity(id)->a1 - c.entity(id)->a0;
    }
    EXPECT_NEAR(span, 2 * pi, 1e-9);
    EXPECT_NE(c.solve().state, SolveState::Failed);

    // the arc left over, crossed twice more, becomes three arcs (by script; the four
    // quarters stay closed areas and nothing is defined twice)
    auto doc = build("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=20\nline sketch=s1 p1=-20,0 p2=20,0\n"
                     "line sketch=s1 p1=0,-20 p2=0,20\nbreak sketch=s1 at=7.07,7.07\nbreak sketch=s1 at=-7.07,-7.07\n");
    expectAllOk(*doc);
    int arcs = 0;
    for (auto& [id, e] : doc->state().sketches.at("s1")->solved.entities()) {
        arcs += e.type == EntityType::Arc;
    }
    EXPECT_EQ(arcs, 4);
    EXPECT_EQ(doc->state().sketches.at("s1")->profiles.regions.size(), 4u);
}

TEST(SketchMirror, CopiesStayMirrored)
{
    Sketch s;
    const int axis = s.addLine(0, -50, 0, 50);
    s.setConstruction(axis, true);
    s.addConstraint(ConstraintType::Fix, s.entity(axis)->p1);
    s.addConstraint(ConstraintType::Fix, s.entity(axis)->p2);
    const int l1 = s.addLine(5, 0, 10, 10);
    const int l2 = s.addLine(0, 20, 5, 25);  // one end on the mirror line
    const int c = s.addCircle(8, 3, 2);
    const int arc = s.addArc(20, 0, 4, 0, pi / 2);
    const int dofBefore = s.solve().dof;
    auto r = s.mirror({l1, l2, c, arc, axis}, axis);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(r->size(), 4u);
    auto rep = s.solve();
    ASSERT_NE(rep.state, SolveState::Failed) << rep.message;
    EXPECT_TRUE(rep.redundant.empty());
    EXPECT_EQ(rep.dof, dofBefore);  // the copies add no freedom
    auto [mx, my] = s.pointXY(s.entity((*r)[0])->p2);
    EXPECT_NEAR(mx, -10, 1e-9);
    EXPECT_NEAR(my, 10, 1e-9);
    EXPECT_EQ(s.entity((*r)[1])->p1, s.entity(l2)->p1);  // the shared point on the line
    const SketchEntity& ma = *s.entity((*r)[3]);
    EXPECT_NEAR(s.pointXY(ma.center).first, -20, 1e-9);
    EXPECT_NEAR(std::cos(ma.a0), std::cos(pi / 2), 1e-9);  // runs from 90° ... to 180°
    EXPECT_NEAR(std::cos(ma.a1), -1, 1e-9);
    // drag the original: the copy follows
    s.drag(s.entity(l1)->p2, 12, 14);
    auto [nx, ny] = s.pointXY(s.entity((*r)[0])->p2);
    auto [ox, oy] = s.pointXY(s.entity(l1)->p2);
    EXPECT_NEAR(nx, -ox, 1e-6);
    EXPECT_NEAR(ny, oy, 1e-6);
    // by script
    auto d2 = build("sketch id=s1 plane=XY\nline sketch=s1 p1=0,-50 p2=0,50 construction=yes\n"
                    "rect sketch=s1 center=10,0 w=6 h=4\nmirror sketch=s1 at=13,0;7,0;10,2;10,-2 axis=0,30\n");
    expectAllOk(*d2);
    EXPECT_EQ(d2->state().sketches.at("s1")->profiles.regions.size(), 2u);
}

TEST(SketchCopy, MoveCopyAndPatterns)
{
    // a dimensioned 10 x 6 rectangle; its copy keeps the shape (only its place is free)
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=6\n"
                     "copy sketch=s1 at=5,0;-5,0;0,3;0,-3 by=30,0\n");
    expectAllOk(*doc);
    const auto& st = *doc->state().sketches.at("s1");
    ASSERT_EQ(st.profiles.regions.size(), 2u);
    for (auto& r : st.profiles.regions) {
        EXPECT_NEAR(r.area, 60, 1e-6);
    }
    Sketch s = st.solved;
    const int dofOne = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=6\n")
                           ->state().sketches.at("s1")->report.dof;
    EXPECT_EQ(st.report.dof, dofOne + 2);  // the copy can only slide
    // turned a quarter: horizontal sides become vertical ones, still 10 x 6
    auto q = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=6\n"
                   "copy sketch=s1 at=5,0;-5,0;0,3;0,-3 angle=90 deg about=20,0\n");
    expectAllOk(*q);
    double ymin = 1e9, ymax = -1e9;
    for (auto& [id, e] : q->state().sketches.at("s1")->solved.entities()) {
        if (e.type == EntityType::Point && !e.construction) {
            auto [x, y] = q->state().sketches.at("s1")->solved.pointXY(id);
            if (x > 15) {
                ymin = std::min(ymin, y);
                ymax = std::max(ymax, y);
            }
        }
    }
    EXPECT_NEAR(ymax - ymin, 10, 1e-6);
    // move: the whole rectangle shifts (its centre dimensions are relative, so it stays)
    auto m = build("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=4\nmove sketch=s1 at=2,0 by=0,0\n");
    expectAllOk(*m);
    // grid: 3 columns 10 apart, 2 rows 15 apart
    auto g = build("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=4\ngrid sketch=s1 at=2,0 count=3,2 spacing=10,15\n");
    expectAllOk(*g);
    EXPECT_EQ(g->state().sketches.at("s1")->profiles.regions.size(), 6u);
    // ring: 6 round a centre, evenly
    auto r = build("sketch id=s1 plane=XY\ncircle sketch=s1 center=20,0 d=4\nring sketch=s1 at=22,0 center=0,0 count=6\n"
                   "extrude id=e1 sketch=s1 distance=2\n");
    expectAllOk(*r);
    EXPECT_EQ(r->state().sketches.at("s1")->profiles.regions.size(), 6u);
    EXPECT_NEAR(props(*r).volume, 6 * pi * 4 * 2, 1e-6);
    EXPECT_NEAR(props(*r).xmin, -22, 1e-6);  // one copy at 180°
    // limits are explained
    Sketch one;
    const int c = one.addCircle(0, 0, 1);
    EXPECT_FALSE(one.patternRect({c}, 1, 5, 0, 1, 0, 5).ok());
    EXPECT_FALSE(one.patternCircular({c}, 5, 0, 1, 2 * pi).ok());
    EXPECT_FALSE(one.copyCurves({}, 1, 0).ok());
    // move with a turn: an arc's angles turn with it
    const int arc = one.addArc(10, 0, 2, 0, pi / 2);
    ASSERT_TRUE(one.moveCurves({arc}, 0, 0, pi / 2, 0, 0).ok());
    EXPECT_NEAR(one.pointXY(one.entity(arc)->center).second, 10, 1e-9);
    EXPECT_NEAR(one.entity(arc)->a0, pi / 2, 1e-9);
}

TEST(SketchCurves, ConstraintsAlongEllipsesAndSmoothCurves)
{
    auto nearest = [](const Sketch& s, int curve, double x, double y) {
        double best = 1e300;  // to the nearest of the curve's short straight pieces
        const auto Q = s.curvePoints(curve, 4000);
        for (size_t k = 0; k + 1 < Q.size(); ++k) {
            const double ex = Q[k + 1].first - Q[k].first, ey = Q[k + 1].second - Q[k].second;
            const double u = std::clamp(((x - Q[k].first) * ex + (y - Q[k].second) * ey) / (ex * ex + ey * ey), 0.0, 1.0);
            best = std::min(best, std::hypot(Q[k].first + ex * u - x, Q[k].second + ey * u - y));
        }
        return best;
    };
    // a point pulled onto an ellipse (radii 20 and 10)
    Sketch s;
    const int el = s.addEllipse(0, 0, 20, 0, 10);
    const int p = s.addPoint(25, 3);
    s.addConstraint(ConstraintType::Fix, s.entity(el)->center);
    const int axFix = s.addConstraint(ConstraintType::Fix, s.entity(el)->p1);
    s.addConstraint(ConstraintType::PointOnCurve, p, el);
    ASSERT_NE(s.solve().state, SolveState::Failed);
    auto [x, y] = s.pointXY(p);
    EXPECT_NEAR(x * x / 400 + y * y / 100, 1, 1e-6);
    // a level line touching it: at y = 10 (or -10)
    const int l = s.addLine(-30, 14, 30, 15);
    s.addConstraint(ConstraintType::Horizontal, l);
    s.addConstraint(ConstraintType::Tangent, l, el);
    ASSERT_NE(s.solve().state, SolveState::Failed);
    EXPECT_NEAR(s.pointXY(s.entity(l)->p1).second, 10, 1e-6);
    EXPECT_NEAR(s.pointXY(s.entity(l)->p2).second, 10, 1e-6);
    // the ellipse turned 30 degrees: still touching (the line stays level)
    const int ax = s.entity(el)->p1;
    s.removeConstraint(axFix);
    auto rep = s.drag(ax, 20 * std::cos(pi / 6), 20 * std::sin(pi / 6));
    ASSERT_NE(rep.state, SolveState::Failed);
    s.solve();
    const double yl = s.pointXY(s.entity(l)->p1).second;
    // the top of an ellipse a = 20, b = 10 turned by t: sqrt(a² sin² t + b² cos² t)
    EXPECT_NEAR(std::abs(yl), std::sqrt(400 * 0.25 + 100 * 0.75), 1e-4);
    std::tie(x, y) = s.pointXY(p);
    EXPECT_LT(nearest(s, el, x, y), 1e-5);  // the pieces cut inside by < 1e-5

    // a smooth curve: a point held on it, a line and another curve joining its ends smoothly
    Sketch c;
    const int sp = c.addSpline({{0, 0}, {10, 5}, {20, 0}});
    const int q = c.addPoint(10, 9);
    c.addConstraint(ConstraintType::PointOnCurve, q, sp);
    const int start = c.entity(sp)->pts.front();
    const int ln = c.addLine(c.addPoint(-10, -8), start);
    c.addConstraint(ConstraintType::Tangent, ln, sp);
    const int sp2 = c.addSpline({{20.5, 0.5}, {30, 0}, {40, 4}});
    c.addConstraint(ConstraintType::Coincident, c.entity(sp2)->pts.front(), c.entity(sp)->pts.back());
    c.addConstraint(ConstraintType::Tangent, sp, sp2);
    auto r = c.solve();
    ASSERT_NE(r.state, SolveState::Failed) << r.message;
    std::tie(x, y) = c.pointXY(q);
    EXPECT_LT(nearest(c, sp, x, y), 1e-5);
    auto dir = [&](int a, int b) {
        auto [ax2, ay2] = c.pointXY(a);
        auto [bx, by] = c.pointXY(b);
        const double L = std::hypot(bx - ax2, by - ay2);
        return std::pair {(bx - ax2) / L, (by - ay2) / L};
    };
    auto cross = [](std::pair<double, double> u, std::pair<double, double> v) {
        return u.first * v.second - u.second * v.first;
    };
    const auto& P = c.entity(sp)->pts;
    const auto& Q = c.entity(sp2)->pts;
    EXPECT_NEAR(cross(dir(c.entity(ln)->p1, c.entity(ln)->p2), dir(P[0], P[1])), 0, 1e-7);
    EXPECT_NEAR(cross(dir(P[2], P[1]), dir(Q[0], Q[1])), 0, 1e-7);
    // a circle of radius 5 touching a fixed ellipse (radii 20 and 10): from outside on the right
    // its centre ends 25 from the ellipse's centre; one inside at the top sits 5 below the top
    Sketch ce;
    const int e2 = ce.addEllipse(0, 0, 20, 0, 10);
    ce.addConstraint(ConstraintType::Fix, ce.entity(e2)->center);
    ce.addConstraint(ConstraintType::Fix, ce.entity(e2)->p1);
    const int outside = ce.addCircle(27, 1, 5), inside = ce.addCircle(0, 3, 5);
    for (int k : {outside, inside}) {
        ce.addConstraint(ConstraintType::Radius, k, 0, 0, "5 mm");
    }
    ce.addConstraint(ConstraintType::Horizontal, ce.entity(outside)->center, ce.entity(e2)->center);
    ce.addConstraint(ConstraintType::Vertical, ce.entity(inside)->center, ce.entity(e2)->center);
    ce.addConstraint(ConstraintType::Tangent, outside, e2);
    ce.addConstraint(ConstraintType::Tangent, inside, e2);
    auto rc = ce.solve();
    ASSERT_NE(rc.state, SolveState::Failed) << rc.message;
    EXPECT_NEAR(ce.pointXY(ce.entity(outside)->center).first, 25, 1e-6);
    // the inside one stays on the vertical centre line and touches the top from below
    EXPECT_NEAR(ce.pointXY(ce.entity(inside)->center).first, 0, 1e-9);
    EXPECT_NEAR(ce.pointXY(ce.entity(inside)->center).second, 5, 1e-6);
    // refused: a smooth curve tangent to a line it does not meet
    Sketch bad2;
    const int s3 = bad2.addSpline({{0, 0}, {10, 5}, {20, 0}});
    bad2.addConstraint(ConstraintType::Tangent, bad2.addLine(50, 0, 60, 0), s3);
    EXPECT_EQ(bad2.solve().state, SolveState::Failed);
}

TEST(SketchCurves, EllipsesAndSmoothCurves)
{
    // an ellipse 20 x 10 (radii 10 and 5), turned 30°: area pi a b
    auto doc = build("sketch id=s1 plane=XY\nellipse sketch=s1 center=5,5 a=10 b=5 angle=30 deg\n"
                     "extrude id=e1 sketch=s1 distance=4\n");
    expectAllOk(*doc);
    const auto& st = *doc->state().sketches.at("s1");
    ASSERT_EQ(st.profiles.regions.size(), 1u);
    EXPECT_NEAR(st.profiles.regions[0].area, pi * 50, 1e-6);
    EXPECT_NEAR(props(*doc).volume, pi * 50 * 4, 1e-4);
    // the minor radius follows a parameter (also when it becomes the longer one)
    auto prm = build("param b = 5 mm\nsketch id=s1 plane=XY\nellipse sketch=s1 center=0,0 a=10 b=b\n");
    run(*prm, "param b = 12 mm");
    expectAllOk(*prm);
    EXPECT_NEAR(prm->state().sketches.at("s1")->profiles.regions[0].area, pi * 120, 1e-6);

    // a closed smooth curve through four points makes one region; an open one closed by a line
    auto sp = build("sketch id=s1 plane=XY\nspline sketch=s1 points=0,0;20,0;20,20;0,20 closed=yes\n");
    expectAllOk(*sp);
    const auto& sr = sp->state().sketches.at("s1")->profiles.regions;
    ASSERT_EQ(sr.size(), 1u);
    EXPECT_GT(sr[0].area, 400);  // bulges out past the square through its points
    EXPECT_LT(sr[0].area, 700);
    auto open = build("sketch id=s1 plane=XY\nspline sketch=s1 points=0,0;10,8;20,0\nline sketch=s1 p1=0,0 p2=20,0\n");
    EXPECT_EQ(open->state().sketches.at("s1")->profiles.regions.size(), 1u);
    // the curve passes through its fit points, and the drawing does too
    Sketch s;
    const int c = s.addSpline({{0, 0}, {10, 8}, {20, 0}, {30, 5}});
    auto pts = s.curvePoints(c, 16);
    bool through = false;
    for (auto [x, y] : pts) {
        through |= std::hypot(x - 10, y - 8) < 1e-9;
    }
    EXPECT_TRUE(through);
    EXPECT_EQ(s.openEndpoints().size(), 2u);
    // nothing holds its fit points: they are reported free (drawn hollow)
    auto rep = s.solve();
    EXPECT_TRUE(std::find(rep.freeEntities.begin(), rep.freeEntities.end(), c) != rep.freeEntities.end());
    EXPECT_EQ(rep.dof, 8);
    // moving a fit point reshapes it; the shape is saved and read back
    auto json = s.toJson();
    auto back = Sketch::fromJson(json);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back->entity(c)->pts.size(), 4u);
    // copies and mirrors carry them along
    const int e = s.addEllipse(50, 0, 60, 0, 3);
    auto cp = s.copyCurves({c, e}, 0, 40);
    ASSERT_TRUE(cp.ok());
    EXPECT_EQ(s.entity((*cp)[1])->type, EntityType::Ellipse);
    EXPECT_NEAR(s.pointXY(s.entity((*cp)[1])->center).second, 40, 1e-9);
    // trimming or breaking them is explained, not done wrongly
    EXPECT_FALSE(s.breakAt(c, 10, 8).ok());
    EXPECT_FALSE(s.trimPiece(e, 60, 0).has_value());
    // removing one removes its fit points
    const size_t before = s.entities().size();
    s.remove(c);
    EXPECT_EQ(s.entities().size(), before - 5);
}

TEST(SketchTangent, MeetingAtAnEndIsNotSaidTwice)
{
    // a line and an arc that share an end and are tangent there (B-008)
    Sketch s;
    const int l = s.addLine(-20, 0, 0, 0);
    const int arc = s.addArc(0, 10, 10, -pi / 2, pi / 2);  // starts at (0, 0), heading +x
    s.addConstraint(ConstraintType::Coincident, s.entity(arc)->p1, s.entity(l)->p2);
    s.addConstraint(ConstraintType::Tangent, l, arc);
    auto r = s.solve();
    ASSERT_NE(r.state, SolveState::Failed) << r.message;
    EXPECT_TRUE(r.redundant.empty());
    // drag the line's far end down: the arc turns with it and stays tangent
    s.drag(s.entity(l)->p1, -20, -8);
    auto [x1, y1] = s.pointXY(s.entity(l)->p1);
    auto [x2, y2] = s.pointXY(s.entity(l)->p2);
    auto [cx, cy] = s.pointXY(s.entity(arc)->center);
    const double dx = x2 - x1, dy = y2 - y1;
    // the radius at the shared end is perpendicular to the line
    EXPECT_NEAR(dx * (cx - x2) + dy * (cy - y2), 0, 1e-6);
}

TEST(Hole, ScrewSizesAndDrillPoint)
{
    // the clearance table: ISO 273 for M3, the drill chart for #6
    EXPECT_NEAR(clearanceDiameter("M3", HoleFit::Close).value(), 3.2, 1e-12);
    EXPECT_NEAR(clearanceDiameter("M3", HoleFit::Normal).value(), 3.4, 1e-12);
    EXPECT_NEAR(clearanceDiameter("m8", HoleFit::Loose).value(), 10.0, 1e-12);
    EXPECT_NEAR(clearanceDiameter("#6", HoleFit::Close).value(), 0.144 * 25.4, 1e-9);
    EXPECT_NEAR(clearanceDiameter("1/4", HoleFit::Normal).value(), 0.266 * 25.4, 1e-9);
    EXPECT_FALSE(clearanceDiameter("#6", HoleFit::Loose).ok());
    EXPECT_FALSE(clearanceDiameter("M7", HoleFit::Normal).ok());
    // a through M4 hole at normal fit is 4.5 mm across (two holes in a 10 mm block)
    auto m4 = build(std::string(kBlock) + "hole id=h1 sketch=s2 size=M4 extent=all\n");
    expectAllOk(*m4);
    EXPECT_NEAR(props(*m4).volume, 12000 - 2 * pi * 2.25 * 2.25 * 10, 1e-3);
    // a typed diameter still wins over the size
    auto typed = build(std::string(kBlock) + "hole id=h1 sketch=s2 size=M4 diameter=4mm extent=all\n");
    EXPECT_NEAR(props(*typed).volume, 12000 - 2 * pi * 4 * 10, 1e-3);
    // a counterbore from the table: M3 socket head (6.5 across, 3.5 deep)
    auto cb = build(std::string(kBlock) + "hole id=h1 sketch=s2 size=M3 type=counterbore extent=all\n");
    expectAllOk(*cb);
    EXPECT_NEAR(props(*cb).volume, 12000 - 2 * (pi * 3.25 * 3.25 * 3.5 + pi * 1.7 * 1.7 * 6.5), 1e-3);
    // a drill point: 6 deep to the shoulder plus a 118° cone
    auto tip = build(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm depth=6mm tip=118deg\n");
    expectAllOk(*tip);
    const double h = 2 / std::tan(59 * pi / 180);
    EXPECT_NEAR(props(*tip).volume, 12000 - 2 * (pi * 4 * 6 + pi * 4 * h / 3), 1e-3);
    EXPECT_EQ(facesStartingWith(*tip, "e1", "h1/tip:p"), 2);
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.runText(std::string(kBlock) + "hole id=h1 sketch=s2 size=M7 extent=all\n");
    EXPECT_NE(int(bad->status(int(bad->model().features.size()) - 1).level), int(StatusLevel::Ok));
}

TEST(Axis, EdgesRoundFacesAndCrossingPlanes)
{
    // where two origin planes meet: the Z axis; revolving around it is the same as around Z
    auto ring = build("axis id=a1 base=XZ base2=YZ\nsketch id=s1 plane=XZ\nrect sketch=s1 corner=10,0 w=10 h=5\n"
                      "revolve id=r1 sketch=s1 axis=a1\n");
    expectAllOk(*ring);
    const gp_Ax1 z = ring->state().axes.at("a1");
    EXPECT_TRUE(z.Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9));
    EXPECT_NEAR(z.Location().Distance(gp_Pnt(0, 0, 0)), 0, 1e-9);
    EXPECT_NEAR(props(*ring).volume, pi * (400 - 100) * 5, 1e-3);
    // along a block's corner edge; turning the block half a turn about it
    auto blk = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                     "axis id=a2 through=edge:E[e1/side:5|e1/side:6]\nmove id=m1 bodies=e1 angle=180deg axis=a2\n");
    expectAllOk(*blk);
    const gp_Ax1 c = blk->state().axes.at("a2");
    EXPECT_TRUE(c.Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9));
    EXPECT_NEAR(std::abs(c.Location().X()), 20, 1e-9);
    auto mp = props(*blk);
    EXPECT_NEAR(mp.xmin, 2 * c.Location().X() - 20, 1e-6);
    EXPECT_NEAR(mp.ymin, 2 * c.Location().Y() - 10, 1e-6);
    // through a hole's wall: its centre line; a ring pattern of a hole around another hole
    auto hole = build(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm extent=all\n");
    std::string wall;
    for (auto& [bid, b] : hole->state().bodies) {
        for (int i = 0; i < b->faceCount(); ++i) {
            if (b->faceName(i).rfind("h1/wall:", 0) == 0 && wall.empty()) {
                wall = b->faceName(i);
            }
        }
    }
    ASSERT_FALSE(wall.empty());
    run(*hole, "axis id=a3 through=face:" + wall);
    expectAllOk(*hole);
    const gp_Ax1 h = hole->state().axes.at("a3");
    EXPECT_NEAR(std::abs(h.Location().X()), 10, 1e-6);
    EXPECT_NEAR(h.Location().Y(), 0, 1e-6);
    // parallel planes never meet; a flat face has no axis
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.runText("axis id=a1 base=XY base2=XY\n");
    EXPECT_NE(int(bad->status(0).level), int(StatusLevel::Ok));
    // an axis that does not exist is explained
    auto none = std::make_unique<Document>();
    ScriptRunner r2(*none);
    r2.runText("sketch id=s1 plane=XZ\nrect sketch=s1 corner=10,0 w=10 h=5\nrevolve id=r1 sketch=s1 axis=a9\n");
    EXPECT_NE(none->status(1).message.find("a9"), std::string::npos);
}

TEST(Pattern, WholeBodiesAndMirroredSteps)
{
    // a 10 mm cube copied 3 times along X, 20 apart, as new bodies; then joined into one
    const std::string cube = "sketch id=s1 plane=XY\nrect sketch=s1 corner=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=10\n";
    auto sep = build(cube + "pattern id=p1 body=e1 kind=rectangular dir1=X count1=3 spacing1=20mm\n");
    expectAllOk(*sep);
    EXPECT_EQ(sep->state().bodies.size(), 3u);
    auto joined = build(cube + "pattern id=p1 body=e1 kind=rectangular dir1=X count1=3 spacing1=10mm op=join\n");
    expectAllOk(*joined);
    EXPECT_EQ(joined->state().bodies.size(), 1u);
    EXPECT_NEAR(props(*joined).volume, 3000, 1e-6);
    // a ring of bodies around a construction axis at x = 30
    auto ring = build(cube + "axis id=a1 base=YZ base2=XZ\npattern id=p1 body=e1 axis=a1 count=4\n");
    expectAllOk(*ring);
    EXPECT_EQ(ring->state().bodies.size(), 4u);
    EXPECT_NEAR(props(*ring).xmin, -10, 1e-6);  // the copy turned half a turn about Z
    // mirror one step (a hole) across a construction plane: the same op on the copy
    auto mf = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
                    "sketch id=s2 on=e1/end\npoint sketch=s2 at=10,5\nhole id=h1 sketch=s2 diameter=4mm extent=all\n"
                    "plane id=pl1 base=YZ distance=0mm\nmirror id=m1 feature=h1 plane=pl1\n");
    expectAllOk(*mf);
    EXPECT_NEAR(props(*mf).volume, 12000 - 2 * pi * 4 * 10, 1e-3);
    // a body mirrored across a flat face of its own: doubles it
    auto mb = build(cube + "mirror id=m1 body=e1 plane=face:e1/end\n");
    expectAllOk(*mb);
    EXPECT_NEAR(props(*mb).volume, 2000, 1e-6);
    EXPECT_NEAR(props(*mb).zmax, 20, 1e-9);
}

TEST(PressPull, FlatFacesOutAndIn)
{
    const std::string block = "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n";
    // pull the top up 5 mm: one taller block, still one body
    auto up = build(block + "presspull id=pp1 face=e1/end distance=5mm\n");
    expectAllOk(*up);
    EXPECT_EQ(up->state().bodies.size(), 1u);
    EXPECT_NEAR(props(*up).volume, 40 * 30 * 15, 1e-6);
    EXPECT_NEAR(props(*up).zmax, 15, 1e-9);
    // push a side in 4 mm (the bottom face pushed in works the same way)
    auto in = build(block + "presspull id=pp1 face=e1/start distance=-4mm\n");
    expectAllOk(*in);
    EXPECT_NEAR(props(*in).volume, 40 * 30 * 6, 1e-6);
    EXPECT_NEAR(props(*in).zmin, 4, 1e-9);
    // a distance from a parameter follows it
    auto prm = build("param lift = 2 mm\n" + block + "presspull id=pp1 face=e1/end distance=lift\n");
    run(*prm, "param lift = 7 mm");
    EXPECT_NEAR(props(*prm).zmax, 17, 1e-9);
    // a whole round face: a round boss pulled out 2 mm gets 2 mm wider all round
    auto boss = build("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=20\nextrude id=e1 sketch=s1 distance=10\n");
    std::string side;
    for (int i = 0; i < boss->state().bodies.at("e1")->faceCount(); ++i) {
        if (boss->state().bodies.at("e1")->faceName(i).find("side") != std::string::npos) {
            side = boss->state().bodies.at("e1")->faceName(i);
        }
    }
    run(*boss, "presspull id=pp1 face=" + side + " distance=2mm");
    expectAllOk(*boss);
    EXPECT_NEAR(props(*boss).volume, pi * 144 * 10, 1e-3);
    // a hole's wall: pushed in (negative) the hole gets bigger, pulled out it gets smaller
    const std::string holed = std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm extent=all\n";
    auto wallOf = [](const Document& d) {
        for (auto& [bid, b] : d.state().bodies) {
            for (int i = 0; i < b->faceCount(); ++i) {
                if (b->faceName(i).rfind("h1/wall:", 0) == 0) {
                    return b->faceName(i);
                }
            }
        }
        return std::string {};
    };
    auto bigger = build(holed);
    const std::string wall = wallOf(*bigger);
    ASSERT_FALSE(wall.empty());
    const double before = props(*bigger).volume;
    run(*bigger, "presspull id=pp1 face=" + wall + " distance=-1mm");
    expectAllOk(*bigger);
    EXPECT_NEAR(before - props(*bigger).volume, pi * (9 - 4) * 10, 1e-3);  // one hole 4 -> 6 mm across
    auto smaller = build(holed);
    run(*smaller, "presspull id=pp1 face=" + wall + " distance=0.5mm");
    expectAllOk(*smaller);
    EXPECT_NEAR(props(*smaller).volume - before, pi * (4 - 2.25) * 10, 1e-3);  // 4 -> 3 mm across
    // closing a hole completely, or a round that is only part of a circle, is explained
    auto shut = std::make_unique<Document>();
    ScriptRunner r(*shut);
    r.runText(holed);
    r.runText("presspull id=pp1 face=" + wallOf(*shut) + " distance=3mm\n");
    EXPECT_NE(shut->status(int(shut->model().features.size()) - 1).message.find("close the hole"), std::string::npos);
    auto rounded = std::make_unique<Document>();
    ScriptRunner rr(*rounded);
    rr.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
               "fillet id=f1 edges=face:e1/end radius=2mm body=e1\n");
    std::string fil;
    for (int i = 0; i < rounded->state().bodies.at("e1")->faceCount(); ++i) {
        const auto& b = *rounded->state().bodies.at("e1");
        BRepAdaptor_Surface fs(TopoDS::Face(b.face(i)));
        if (fs.GetType() == GeomAbs_Cylinder && fil.empty()) {
            fil = b.faceName(i);
        }
    }
    ASSERT_FALSE(fil.empty());
    rr.runText("presspull id=pp1 face=" + fil + " distance=1mm\n");
    EXPECT_NE(rounded->status(int(rounded->model().features.size()) - 1).message.find("part of the way"), std::string::npos)
        << rounded->status(int(rounded->model().features.size()) - 1).message;
}

TEST(Extrude, UpToAFaceOrPlane)
{
    // a 10 mm high block; a second sketch on the floor extruded up to the block's top follows it
    const std::string base = "param h = 10 mm\nsketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\n"
                             "extrude id=e1 sketch=s1 distance=h\n"
                             "sketch id=s2 plane=XY\ncircle sketch=s2 center=40,0 d=10\n";
    auto doc = build(base + "extrude id=e2 sketch=s2 extent=to to=face:e1/end op=new\n");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).zmax, 10, 1e-9);
    run(*doc, "param h = 25 mm");
    expectAllOk(*doc);
    EXPECT_NEAR(massProperties(doc->state().bodies.at("e2")->shape()).zmax, 25, 1e-9);
    // up to a construction plane, with an offset beyond it; and downwards (the plane below)
    auto pl = build("plane id=pl1 base=XY distance=12mm\nsketch id=s1 plane=XY offset=30mm\n"
                    "circle sketch=s1 center=0,0 d=10\nextrude id=e1 sketch=s1 extent=to to=pl1 to_offset=3mm\n");
    expectAllOk(*pl);
    EXPECT_NEAR(props(*pl).zmin, 9, 1e-9);
    EXPECT_NEAR(props(*pl).zmax, 30, 1e-9);
    // a plane square to the sketch, through the outline, gives no way to tell how far
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.runText("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=10\nextrude id=e1 sketch=s1 extent=to to=YZ\n");
    EXPECT_NE(int(bad->status(1).level), int(StatusLevel::Ok));
}

TEST(Extrude, UpToASlantedPlaneOrCurvedFace)
{
    // a 10 mm rod from 20 below the floor up to a plane tilted 30 degrees about X through the
    // origin: the cut is centred on the rod, so the volume is the area times 20
    auto slant = build("plane id=pl1 kind=angle base=XY axis=X angle=30deg\nsketch id=s1 plane=XY offset=-20mm\n"
                       "circle sketch=s1 center=0,0 d=10\nextrude id=e1 sketch=s1 extent=to to=pl1\n");
    expectAllOk(*slant);
    EXPECT_NEAR(props(*slant).volume, 25 * pi * 20, 1e-3);
    const auto& rod = *slant->state().bodies.at("e1");
    EXPECT_EQ(rod.findFaces("e1/end").size(), 1u);  // the slanted cut is the rod's end
    // up to the underside of a lying cylinder (radius 20, axis along Y at z = 30)
    auto curved = build("sketch id=c plane=XZ\ncircle sketch=c center=0,30 d=40\nextrude id=cyl sketch=c distance=30 direction=symmetric\n"
                        "sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=6\n");
    std::string round;
    for (auto& [bid, b] : curved->state().bodies) {
        for (int i = 0; i < b->faceCount(); ++i) {
            if (measureItem(b->face(i)).radius) {
                round = b->faceName(i);
            }
        }
    }
    ASSERT_FALSE(round.empty());
    run(*curved, "extrude id=e1 sketch=s1 extent=to to=face:" + round + " op=new");
    expectAllOk(*curved);
    const auto mp = massProperties(curved->state().bodies.at("e1")->shape());
    EXPECT_NEAR(mp.zmin, 0, 1e-6);
    EXPECT_NEAR(mp.zmax, 30 - std::sqrt(400.0 - 9), 1e-6);  // highest at the rod's sides
    // between the rod cut flat at the lowest and the highest point of the curve
    EXPECT_GT(mp.volume, 9 * pi * 10);
    EXPECT_LT(mp.volume, 9 * pi * (30 - std::sqrt(400.0 - 9)));
    // a face that does not cover the outline is explained: a thin upright pin inside a wide rod
    auto narrow = std::make_unique<Document>();
    ScriptRunner rn(*narrow);
    rn.runText("sketch id=b plane=XY\ncircle sketch=b center=0,0 d=4\nextrude id=e0 sketch=b distance=30\n"
               "sketch id=s1 plane=XY offset=-10mm\ncircle sketch=s1 center=0,0 d=20\n");
    std::string pin;
    const auto& b0 = *narrow->state().bodies.at("e0");
    for (int i = 0; i < b0.faceCount(); ++i) {
        if (measureItem(b0.face(i)).radius) {
            pin = b0.faceName(i);
        }
    }
    ASSERT_FALSE(pin.empty());
    rn.runText("extrude id=e1 sketch=s1 extent=to to=face:" + pin + " op=new\n");
    EXPECT_NE(narrow->status(3).message.find("does not cross"), std::string::npos) << narrow->status(3).message;
}

TEST(Extrude, ThinWallsAlongOpenCurves)
{
    // an L of two lines (30 + 20 long), a 2 mm wall 10 high, centred on the lines
    auto doc = build("sketch id=s1 plane=XY\nline sketch=s1 p1=0,0 p2=30,0\nline sketch=s1 p1=30,0 p2=30,20\n"
                     "extrude id=e1 sketch=s1 profiles=open thin=2mm distance=10\n");
    expectAllOk(*doc);
    // centre line 50 long; the outer corner adds 1x1 and the inner corner takes it back, the
    // two ends square: area = 50 * 2 exactly
    EXPECT_NEAR(props(*doc).volume, 50 * 2 * 10, 1e-6);
    auto mp = props(*doc);
    EXPECT_NEAR(mp.xmin, 0, 1e-9);  // square ends: no cap beyond the line's end
    EXPECT_NEAR(mp.ymin, -1, 1e-9);
    EXPECT_NEAR(mp.xmax, 31, 1e-9);
    // an arc on its own: a curved wall
    auto arc = build("sketch id=s1 plane=XY\narc sketch=s1 center=0,0 r=20 from=0deg to=90deg\n"
                     "extrude id=e1 sketch=s1 profiles=open thin=2mm distance=5\n");
    expectAllOk(*arc);
    EXPECT_NEAR(props(*arc).volume, (pi / 4) * (21 * 21 - 19 * 19) * 5, 1e-4);
    // a closed outline has no open curves: explained
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\nextrude id=e1 sketch=s1 profiles=open thin=2mm\n");
    EXPECT_NE(bad->status(1).message.find("open curves"), std::string::npos) << bad->status(1).message;
}

TEST(Fixes, StepsOfferOneClickRepairs)
{
    auto fixOf = [](const Document& d, int i, const std::string& param) -> std::string {
        for (auto& f : d.status(i).fixes) {
            if (f.param == param) {
                return f.value;
            }
        }
        return {};
    };
    // a fillet far too big for a 10 mm block: a smaller radius to use
    auto fil = std::make_unique<Document>();
    ScriptRunner r1(*fil);
    r1.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
               "fillet id=f1 edges=face:e1/end radius=30mm body=e1\n");
    const std::string r = fixOf(*fil, 2, "radius");
    ASSERT_FALSE(r.empty()) << fil->status(2).message;
    // and it works when applied
    auto ok = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
                    "fillet id=f1 edges=face:e1/end radius=" + r + " body=e1\n");
    expectAllOk(*ok);
    // a join that does not touch: make it a new body
    auto apart = std::make_unique<Document>();
    ScriptRunner r2(*apart);
    r2.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=10\n"
               "sketch id=s2 plane=XY\nrect sketch=s2 center=50,0 w=10 h=10\nextrude id=e2 sketch=s2 distance=10 op=join\n");
    EXPECT_EQ(fixOf(*apart, 3, "op"), "new");
    // a taper too steep: half the angle
    auto steep = std::make_unique<Document>();
    ScriptRunner r3(*steep);
    r3.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=50 taper=40deg\n");
    if (steep->status(1).level == StatusLevel::Error) {
        EXPECT_FALSE(fixOf(*steep, 1, "taper").empty());
    }
    // a counterbore narrower than its hole: a width in proportion, which then works
    auto cb = std::make_unique<Document>();
    ScriptRunner r4(*cb);
    r4.runText(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm depth=8mm type=counterbore cbore_diameter=3mm cbore_depth=2mm\n");
    const std::string w = fixOf(*cb, 3, "cbore_diameter");
    ASSERT_FALSE(w.empty()) << cb->status(3).message;
    expectAllOk(*build(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm depth=8mm type=counterbore cbore_diameter=" + w +
                       " cbore_depth=2mm\n"));
    // a counterbore deeper than the hole: a depth that fits
    auto deep = std::make_unique<Document>();
    ScriptRunner r5(*deep);
    r5.runText(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm depth=6mm type=counterbore cbore_diameter=7mm cbore_depth=9mm\n");
    const std::string h = fixOf(*deep, 3, "cbore_depth");
    ASSERT_FALSE(h.empty());
    expectAllOk(*build(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm depth=6mm type=counterbore cbore_diameter=7mm cbore_depth=" + h + "\n"));
    // a drill point that is not a drill point: 118 deg or flat
    auto tip = std::make_unique<Document>();
    ScriptRunner r6(*tip);
    r6.runText(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm depth=6mm tip=10deg\n");
    EXPECT_EQ(fixOf(*tip, 3, "tip"), "118 deg");
    // a countersink narrower than its hole
    auto cs = std::make_unique<Document>();
    ScriptRunner r7(*cs);
    r7.runText(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm extent=all type=countersink csink_diameter=3mm csink_angle=90deg\n");
    const std::string cw = fixOf(*cs, 3, "csink_diameter");
    ASSERT_FALSE(cw.empty());
    expectAllOk(*build(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm extent=all type=countersink csink_diameter=" + cw +
                       " csink_angle=90deg\n"));
}

TEST(Text, InstalledFontsByFamilyName)
{
    // a font folder of our own (the bundled Inter file), found by family and style
    const std::string dir = std::string(JIG_SOURCE_DIR) + "/assets/fonts";
#if defined(_WIN32)
    _putenv_s("JIG_FONT_DIRS", dir.c_str());
#else
    setenv("JIG_FONT_DIRS", dir.c_str(), 1);
#endif
    const auto& fonts = installedFonts(true);
    bool inter = false;
    for (auto& f : fonts) {
        inter |= f.family == "Inter" && f.path.find("Inter-Regular.otf") != std::string::npos;
    }
    EXPECT_TRUE(inter);
    auto f = findFont("inter regular");  // any case
    ASSERT_TRUE(f.has_value());
    EXPECT_EQ(f->family, "Inter");
    EXPECT_FALSE(findFont("No Such Typeface").has_value());
    // a sketch text in "Inter Regular" (by name, from the folder) matches the built-in Inter
    auto byName = build("sketch id=s1 plane=XY\ntext sketch=s1 at=0,0 text=\"Jig\" size=10mm font=\"Inter Regular\"\n");
    auto builtIn = build("sketch id=s1 plane=XY\ntext sketch=s1 at=0,0 text=\"Jig\" size=10mm\n");
    double a = 0, b = 0;
    for (auto& r : byName->state().sketches.at("s1")->profiles.regions) {
        a += r.area;
    }
    for (auto& r : builtIn->state().sketches.at("s1")->profiles.regions) {
        b += r.area;
    }
    EXPECT_GT(a, 0);
    EXPECT_NEAR(a, b, 1e-6);
    EXPECT_TRUE(byName->status(0).message.find("not available") == std::string::npos) << byName->status(0).message;
}

TEST(Text, JoinedScriptsAreShaped)
{
    // Arabic letters take joined forms, lam + alef become one ligature and the line reads
    // right to left; DejaVu Sans has these forms (skipped where it is not installed)
    const auto font = findFont("DejaVu Sans");
    if (!font) {
        GTEST_SKIP() << "DejaVu Sans is not installed";
    }
    auto shape = [&](const std::string& text) {
        SketchText t;
        t.text = text;
        t.font = "DejaVu Sans";
        t.heightValue = 10;
        auto r = textFaces(t, 0, 0, gp_Ax3());
        EXPECT_TRUE(r.ok());
        return r ? r.value() : TextOutline {};
    };
    const auto word = shape("\u0633\u0644\u0627\u0645");  // salaam: seen, lam, alef, meem
    if (!word.shaped) {
        GTEST_SKIP() << "built without HarfBuzz";
    }
    EXPECT_FALSE(word.faces.empty());
    const unsigned seen = shape("\u0633").glyphs.at(0), meem = shape("\u0645").glyphs.at(0);
    ASSERT_EQ(word.glyphs.size(), 3u);  // seen, the lam-alef ligature, meem
    EXPECT_NE(word.glyphs.back(), seen);  // the first letter, rightmost, in its joining form
    // the last letter is leftmost (right to left), on its own as Arabic writes it after alef
    EXPECT_EQ(word.glyphs.front(), meem);
    // mixed: an Arabic word inside an English line keeps its own order, the English around it
    // reads left to right; an Arabic line with a number puts the number (left to right) on the left
    auto concat = [](std::initializer_list<std::vector<unsigned>> parts) {
        std::vector<unsigned> out;
        for (auto& p : parts) {
            out.insert(out.end(), p.begin(), p.end());
        }
        return out;
    };
    const auto mixed = shape("abc \u0633\u0644\u0627\u0645 def");
    const auto abc = shape("abc ").glyphs, def = shape(" def").glyphs;
    EXPECT_EQ(mixed.glyphs, concat({abc, word.glyphs, def}));
    const auto withNumber = shape("\u0633\u0644\u0627\u0645 123");
    const auto num = shape("123").glyphs, space = shape(" ").glyphs;
    EXPECT_EQ(withNumber.glyphs, concat({num, space, word.glyphs}));
}

TEST(Text, SeveralLinesStackAndCentre)
{
    // two lines ("\n" as typed): taller than one, as wide as the wider line, centred
    auto one = build("sketch id=s1 plane=XY\ntext sketch=s1 at=0,0 text=\"HI\" size=10mm\nextrude id=e1 sketch=s1 distance=1\n");
    auto two = build("sketch id=s1 plane=XY\ntext sketch=s1 at=0,0 text=\"HI\\nHI\" size=10mm\nextrude id=e1 sketch=s1 distance=1\n");
    expectAllOk(*two);
    const auto a = props(*one), b = props(*two);
    EXPECT_NEAR(b.volume, 2 * a.volume, 1e-6);
    EXPECT_NEAR(b.xmax - b.xmin, a.xmax - a.xmin, 1e-6);     // same width
    EXPECT_GT(b.ymax - b.ymin, 1.8 * (a.ymax - a.ymin));    // two lines tall
    EXPECT_NEAR((b.ymax + b.ymin) / 2, 0, 1e-6);            // centred on the anchor
    EXPECT_NEAR((b.xmax + b.xmin) / 2, 0, 1e-6);
}

TEST(Point, TypedEdgeEndsAndFaceCentres)
{
    // typed coordinates may use parameters; following a parameter change
    auto doc = build("param h = 7 mm\npoint id=p1 at=1,2,h\n");
    expectAllOk(*doc);
    EXPECT_NEAR(doc->state().points.at("p1").Distance(gp_Pnt(1, 2, 7)), 0, 1e-9);
    // the centre of a block's top face, and the ends and middle of one of its edges
    auto blk = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                     "point id=p2 through=face:e1/end\n"
                     "point id=p3 through=edge:E[e1/side:5|e1/side:6] end=start\n"
                     "point id=p4 through=edge:E[e1/side:5|e1/side:6] end=end\n"
                     "point id=p5 through=edge:E[e1/side:5|e1/side:6] end=mid\n");
    expectAllOk(*blk);
    const auto& pts = blk->state().points;
    EXPECT_NEAR(pts.at("p2").Distance(gp_Pnt(0, 0, 10)), 0, 1e-9);
    EXPECT_NEAR(pts.at("p3").Distance(pts.at("p4")), 10, 1e-9);  // a vertical edge, 10 long
    EXPECT_NEAR(pts.at("p5").Z(), 5, 1e-9);
    EXPECT_NEAR(std::abs(pts.at("p5").X()), 20, 1e-9);
    // an axis through two points; revolving around it
    auto ring = build("point id=p1 at=0,0,10\naxis id=a1 from=O to=p1\nsketch id=s1 plane=XZ\n"
                      "rect sketch=s1 corner=10,0 w=10 h=5\nrevolve id=r1 sketch=s1 axis=a1\n");
    expectAllOk(*ring);
    EXPECT_TRUE(ring->state().axes.at("a1").Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9));
    EXPECT_NEAR(props(*ring).volume, pi * (400 - 100) * 5, 1e-3);
    // the centre of a hole's rim
    auto hole = build(std::string(kBlock) + "hole id=h1 sketch=s2 diameter=4mm extent=all\n");
    std::string rim;
    for (auto& [bid, b] : hole->state().bodies) {
        for (int i = 0; i < b->edgeCount() && rim.empty(); ++i) {
            if (measureItem(b->edge(i)).radius && b->edgeName(i).find("h1/wall") != std::string::npos) {
                rim = b->edgeName(i);
            }
        }
    }
    ASSERT_FALSE(rim.empty());
    run(*hole, "point id=p9 through=edge:" + rim + " end=center");
    expectAllOk(*hole);
    EXPECT_NEAR(std::abs(hole->state().points.at("p9").X()), 10, 1e-6);
    EXPECT_NEAR(hole->state().points.at("p9").Y(), 0, 1e-6);
    // a sketch point still works the old way (two coordinates)
    auto sk = build("sketch id=s1 plane=XY\npoint at=3,4\n");
    expectAllOk(*sk);
    EXPECT_TRUE(sk->state().points.empty());
    // failures are explained: a straight edge has no centre, two equal points make no axis
    auto bad = std::make_unique<Document>();
    ScriptRunner rb(*bad);
    rb.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
               "point id=p3 through=edge:E[e1/side:5|e1/side:6] end=center\n");
    EXPECT_NE(bad->status(2).message.find("centre"), std::string::npos);
    auto same = std::make_unique<Document>();
    ScriptRunner r(*same);
    r.runText("point id=p1 at=0,0,0\naxis id=a1 from=O to=p1\n");
    EXPECT_NE(same->status(1).message.find("same place"), std::string::npos);
}

TEST(Revolve, UpToAPlaneOrFaceThroughTheAxis)
{
    // the profile is in the XZ plane on +X; turning about Z it meets the YZ plane after a
    // quarter turn
    auto doc = build(std::string(kProfile) + "revolve id=r1 sketch=s1 axis=Z extent=to to=YZ\n");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).volume, kTurned / 4, 1e-3);
    // up to a construction plane at 30 degrees: a twelfth of the turn
    auto at30 = build(std::string(kProfile) + "plane id=pl1 kind=angle base=XZ axis=Z angle=30deg\n"
                                                "revolve id=r1 sketch=s1 axis=Z extent=to to=pl1\n");
    expectAllOk(*at30);
    const double v = props(*at30).volume;
    EXPECT_TRUE(std::abs(v - kTurned / 12) < 1e-3 || std::abs(v - kTurned * 5 / 12) < 1e-3) << v;
    // up to a flat face of a block on the -X side whose face at y=0 holds the axis: half a turn
    auto face = build("sketch id=b plane=XY\nrect sketch=b corner=-30,0 w=20 h=30\nextrude id=e1 sketch=b distance=16\n"
                      + std::string(kProfile));
    std::string onAxis;
    const auto& blk = *face->state().bodies.at("e1");
    for (int i = 0; i < blk.faceCount(); ++i) {
        const MeasureItem m = measureItem(blk.face(i));
        Bnd_Box bb;
        BRepBndLib::Add(blk.face(i), bb);
        double x0, y0, z0, x1, y1, z1;
        bb.Get(x0, y0, z0, x1, y1, z1);
        if (m.normal && std::abs(y1) < 1e-6 && std::abs(y0) < 1e-6) {
            onAxis = blk.faceName(i);
        }
    }
    ASSERT_FALSE(onAxis.empty());
    run(*face, "revolve id=r1 sketch=s1 axis=Z extent=to to=face:" + onAxis + " op=new");
    expectAllOk(*face);
    double turned = 0;
    for (auto& [id, b] : face->state().bodies) {
        if (id != "e1") {
            turned += massProperties(b->shape()).volume;
        }
    }
    EXPECT_NEAR(turned, kTurned / 2, 1e-3);
    // a plane the axis does not lie in is explained
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.runText(std::string(kProfile) + "revolve id=r1 sketch=s1 axis=Z extent=to to=XY\n");
    EXPECT_NE(bad->status(1).message.find("axis lies in"), std::string::npos) << bad->status(1).message;
}

TEST(Bodies, MovePointToPoint)
{
    // a block's top-face centre (0,0,10) moved onto a point at (50,20,0): the block now
    // hangs below that point
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                     "point id=p1 through=face:e1/end\npoint id=p2 at=50,20,0\n"
                     "move id=m1 bodies=e1 from=p1 to=p2\n");
    expectAllOk(*doc);
    auto mp = props(*doc);
    EXPECT_NEAR(mp.xmin, 30, 1e-6);
    EXPECT_NEAR(mp.ymin, 10, 1e-6);
    EXPECT_NEAR(mp.zmax, 0, 1e-6);
    // with a quarter turn about Z first, the turned point still lands on the target
    auto turned = build("sketch id=s1 plane=XY\nrect sketch=s1 corner=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                        "point id=p1 at=40,0,0\npoint id=p2 at=100,0,0\n"
                        "move id=m1 bodies=e1 axis=Z angle=90deg from=p1 to=p2\n");
    expectAllOk(*turned);
    auto tp = props(*turned);
    // turned: x -20..0, y 0..40 and the point at (0,40); shifted by (100,-40)
    EXPECT_NEAR(tp.xmin, 80, 1e-6);
    EXPECT_NEAR(tp.xmax, 100, 1e-6);
    EXPECT_NEAR(tp.ymin, -40, 1e-6);
    EXPECT_NEAR(tp.ymax, 0, 1e-6);
    // a missing point is explained
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
              "move id=m1 bodies=e1 from=p9 to=O\n");
    EXPECT_NE(bad->status(2).message.find("p9"), std::string::npos);
}

TEST(Point, WhereAnEdgeCrossesAPlaneAndSquareAxes)
{
    // a vertical corner edge of a 10 mm block crosses a plane 4 mm up at z = 4; a plane
    // above the block is still met by the edge's line
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
                     "plane id=pl1 base=XY distance=4mm\nplane id=pl2 base=XY distance=25mm\n"
                     "point id=p1 through=edge:E[e1/side:5|e1/side:6] plane=pl1\n"
                     "point id=p2 through=edge:E[e1/side:5|e1/side:6] plane=pl2\n");
    expectAllOk(*doc);
    EXPECT_NEAR(doc->state().points.at("p1").Z(), 4, 1e-9);
    EXPECT_NEAR(std::abs(doc->state().points.at("p1").X()), 20, 1e-9);
    EXPECT_NEAR(doc->state().points.at("p2").Z(), 25, 1e-9);
    // a circle crossing a vertical plane: the first crossing along it
    auto ring = build("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=20\nextrude id=e1 sketch=s1 distance=5\n");
    std::string rim;
    for (auto& [bid, b] : ring->state().bodies) {
        for (int i = 0; i < b->edgeCount() && rim.empty(); ++i) {
            if (measureItem(b->edge(i)).radius) {
                rim = b->edgeName(i);
            }
        }
    }
    ASSERT_FALSE(rim.empty());
    run(*ring, "point id=p3 through=edge:" + rim + " plane=YZ");
    expectAllOk(*ring);
    const gp_Pnt c = ring->state().points.at("p3");
    EXPECT_NEAR(std::abs(c.X()), 0, 1e-7);
    EXPECT_NEAR(std::abs(c.Y()), 10, 1e-7);
    // an axis square to the block's top, through that crossing point: parallel to Z
    run(*doc, "axis id=a1 normal=face:e1/end at=p1");
    expectAllOk(*doc);
    const gp_Ax1 a = doc->state().axes.at("a1");
    EXPECT_TRUE(a.Direction().IsParallel(gp_Dir(0, 0, 1), 1e-9));
    EXPECT_NEAR(a.Location().Distance(doc->state().points.at("p1")), 0, 1e-9);
    // an edge parallel to the plane never crosses it
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=20\nextrude id=e1 sketch=s1 distance=10\n"
              "point id=p1 through=edge:E[e1/side:5|e1/side:6] plane=XZ\n");
    EXPECT_NE(bad->status(2).message.find("parallel"), std::string::npos) << bad->status(2).message;
}

TEST(Bodies, RemoveTakesBodiesOutFromThatStep)
{
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=10\n"
                     "sketch id=s2 plane=XY\nrect sketch=s2 center=40,0 w=10 h=10\nextrude id=e2 sketch=s2 distance=5 op=new\n"
                     "remove id=rm1 bodies=e2\n");
    expectAllOk(*doc);
    ASSERT_EQ(doc->state().bodies.size(), 1u);
    EXPECT_TRUE(doc->state().bodies.count("e1"));
    // rolled back before the step, the body is there again
    EXPECT_EQ(doc->stateBefore(doc->featureIndex("rm1")).bodies.size(), 2u);
    // a later step that uses the removed body is explained
    auto later = std::make_unique<Document>();
    ScriptRunner r(*later);
    r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\nextrude id=e1 sketch=s1 distance=10\n"
              "remove id=rm1 bodies=e1\n");
    EXPECT_FALSE(r.runText("remove id=rm2 bodies=e1\n").ok());  // nothing called e1 any more
}

TEST(Revolve, UpToACurvedFace)
{
    // a ring section (radius 10 to 20, 5 high) turning about Z from +X towards +Y stops at
    // the wall of an upright cylinder standing in its way
    auto doc = build("sketch id=c plane=XY\ncircle sketch=c center=0,15 d=20\nextrude id=pin sketch=c distance=10\n"
                     "sketch id=s1 plane=XZ\nrect sketch=s1 corner=10,0 w=10 h=5\n");
    std::string wall;
    const auto& pin = *doc->state().bodies.at("pin");
    for (int i = 0; i < pin.faceCount(); ++i) {
        if (measureItem(pin.face(i)).radius) {
            wall = pin.faceName(i);
        }
    }
    ASSERT_FALSE(wall.empty());
    run(*doc, "revolve id=r1 sketch=s1 axis=Z extent=to to=face:" + wall + " op=new");
    expectAllOk(*doc);
    const TopoDS_Shape& turned = doc->state().bodies.at("r1")->shape();
    const double v = massProperties(turned).volume;
    const double quarter = pi * (400 - 100) * 5 / 4;
    EXPECT_GT(v, 0.2 * quarter);
    EXPECT_LT(v, quarter);  // it stops before a quarter turn
    BRepAlgoAPI_Common overlap(turned, pin.shape());
    EXPECT_NEAR(massProperties(overlap.Shape()).volume, 0, 1e-6);  // and does not go into the pin
    // the cut is the revolve's end (in two pieces here: it crosses the pin's seam)
    const auto& rb = *doc->state().bodies.at("r1");
    int ends = 0;
    for (int i = 0; i < rb.faceCount(); ++i) {
        ends += rb.faceName(i).rfind("r1/end", 0) == 0;
    }
    EXPECT_GE(ends, 1);
    // a pin that only reaches part of the way across the ring is refused
    auto part = std::make_unique<Document>();
    ScriptRunner r(*part);
    r.runText("sketch id=c plane=XY\ncircle sketch=c center=0,25 d=20\nextrude id=pin sketch=c distance=10\n"
              "sketch id=s1 plane=XZ\nrect sketch=s1 corner=10,0 w=10 h=5\n");
    std::string w2;
    const auto& p2 = *part->state().bodies.at("pin");
    for (int i = 0; i < p2.faceCount(); ++i) {
        if (measureItem(p2.face(i)).radius) {
            w2 = p2.faceName(i);
        }
    }
    r.runText("revolve id=r1 sketch=s1 axis=Z extent=to to=face:" + w2 + " op=new\n");
    EXPECT_NE(part->status(3).message.find("does not cross"), std::string::npos) << part->status(3).message;
}

TEST(Pattern, AlongAPath)
{
    // a 4 mm peg on a plate, repeated 5 times along a straight path 40 long: 10 apart
    const std::string base = "sketch id=s1 plane=XY\nrect sketch=s1 corner=-10,-10 w=70 h=40\nextrude id=e1 sketch=s1 distance=3\n"
                             "sketch id=s2 on=e1/end\ncircle sketch=s2 center=0,0 d=4\nextrude id=e2 sketch=s2 distance=5 op=join\n"
                             "sketch id=p plane=XY\nline sketch=p p1=0,0 p2=40,0 construction=yes\n";
    auto doc = build(base + "pattern id=pt1 feature=e2 kind=path path=p count=5\n");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).volume, 70 * 40 * 3 + 5 * 4 * pi * 5, 1e-3);
    // with a spacing: 3 copies 15 apart; too many for the path is explained
    auto spaced = build(base + "pattern id=pt1 feature=e2 kind=path path=p count=3 spacing=15mm\n");
    expectAllOk(*spaced);
    EXPECT_NEAR(props(*spaced).volume, 70 * 40 * 3 + 3 * 4 * pi * 5, 1e-3);
    auto toomany = std::make_unique<Document>();
    ScriptRunner r(*toomany);
    r.runText(base + "pattern id=pt1 feature=e2 kind=path path=p count=9 spacing=15mm\n");
    EXPECT_NE(toomany->status(int(toomany->model().features.size()) - 1).message.find("do not fit"), std::string::npos);
    // around a closed circle of radius 20, a block turning with it: copies keep their place
    // relative to the path, so each one's distance from the centre stays the same
    auto ring = build("sketch id=s1 plane=XY\nrect sketch=s1 center=20,0 w=4 h=2\nextrude id=e1 sketch=s1 distance=2\n"
                      "sketch id=p plane=XY\ncircle sketch=p center=0,0 d=40 construction=yes\n"
                      "pattern id=pt1 body=e1 kind=path path=p count=4 orient=follow op=new\n");
    expectAllOk(*ring);
    ASSERT_EQ(ring->state().bodies.size(), 4u);
    for (auto& [id, b] : ring->state().bodies) {
        GProp_GProps g;
        BRepGProp::VolumeProperties(b->shape(), g);
        const gp_Pnt c = g.CentreOfMass();
        EXPECT_NEAR(std::hypot(c.X(), c.Y()), 20, 1e-6) << id;
        // turned with the path: the long side (4 mm) lies along the radius at each copy
        const auto mp = massProperties(b->shape());
        const double along = std::abs(c.X()) > 1 ? mp.xmax - mp.xmin : mp.ymax - mp.ymin;
        EXPECT_NEAR(along, 4, 1e-6) << id;
    }
}

TEST(Bodies, SplitWithACurvedFace)
{
    // a 40 x 30 x 10 block cut by the underside of a lying cylinder (radius 25, axis along Y
    // at z = 30): a channel-shaped piece comes off the top
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
                     "sketch id=c plane=XZ\ncircle sketch=c center=0,30 d=50\nextrude id=cyl sketch=c distance=40 direction=symmetric op=new\n");
    std::string round;
    const auto& cyl = *doc->state().bodies.at("cyl");
    for (int i = 0; i < cyl.faceCount(); ++i) {
        if (measureItem(cyl.face(i)).radius) {
            round = cyl.faceName(i);
        }
    }
    ASSERT_FALSE(round.empty());
    run(*doc, "split id=sp1 body=e1 plane=face:" + round);
    expectAllOk(*doc);
    const double big = massProperties(doc->state().bodies.at("e1")->shape()).volume;
    const double cap = massProperties(doc->state().bodies.at("sp1")->shape()).volume;
    EXPECT_NEAR(big + cap, 12000, 1e-3);
    // the cap: 30 long, its section the part of the circle below z = 10 inside the block
    const double section = (300 + 625 * std::asin(0.6)) - 600;
    EXPECT_NEAR(cap, 30 * section, 1e-3);
    int cut = 0;
    const auto& b = *doc->state().bodies.at("sp1");
    for (int i = 0; i < b.faceCount(); ++i) {
        cut += b.faceName(i).rfind("sp1/split", 0) == 0;
    }
    EXPECT_GE(cut, 1);
    // a curved face that does not reach across is explained
    auto miss = std::make_unique<Document>();
    ScriptRunner r(*miss);
    r.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=40 h=30\nextrude id=e1 sketch=s1 distance=10\n"
              "sketch id=c plane=XY\ncircle sketch=c center=0,0 d=4\nextrude id=pin sketch=c distance=5 op=new\n");
    std::string pin;
    const auto& pb = *miss->state().bodies.at("pin");
    for (int i = 0; i < pb.faceCount(); ++i) {
        if (measureItem(pb.face(i)).radius) {
            pin = pb.faceName(i);
        }
    }
    r.runText("split id=sp1 body=e1 plane=face:" + pin + "\n");
    EXPECT_NE(miss->status(int(miss->model().features.size()) - 1).message.find("all the way"), std::string::npos)
        << miss->status(int(miss->model().features.size()) - 1).message;
}

TEST(SketchProject, CopiesFollowTheModel)
{
    // the top outline of a block (with a round hole) projected 20 mm above, extruded 3 mm;
    // making the block wider and the hole bigger moves and resizes the copy too
    auto doc = build("param w = 40 mm\nparam d = 8 mm\n"
                     "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=w h=20\nextrude id=e1 sketch=s1 distance=10\n"
                     "sketch id=s3 on=e1/end\ncircle sketch=s3 center=5,0 d=d\nextrude id=h sketch=s3 extent=all op=cut\n"
                     "sketch id=s2 plane=XY offset=20mm\nproject face:e1/end sketch=s2\n"
                     "extrude id=e2 sketch=s2 distance=3 op=new\n");
    auto lid = [&] { return massProperties(doc->state().bodies.at("e2")->shape()); };
    auto disk = [&] {  // the smallest closed area of the projected sketch: the hole's copy
        double a = 1e300;
        for (auto& r : doc->state().sketches.at("s2")->profiles.regions) {
            a = std::min(a, r.area);
        }
        return a;
    };
    EXPECT_NEAR(lid().volume, 40 * 20 * 3, 1e-3);  // all areas extruded: the disk too
    EXPECT_NEAR(disk(), pi * 16, 1e-6);
    run(*doc, "param w = 60 mm");
    expectAllOk(*doc);
    EXPECT_NEAR(lid().volume, 60 * 20 * 3, 1e-3);
    EXPECT_NEAR(lid().xmax, 30, 1e-6);
    run(*doc, "param d = 12 mm");
    expectAllOk(*doc);
    EXPECT_NEAR(disk(), pi * 36, 1e-6);
    EXPECT_EQ(doc->state().sketches.at("s2")->report.dof, 0);  // still fixed in place
    // the links are kept with the design
    const auto path = (tempDir() / "linked-project.jig").string();
    ASSERT_TRUE(saveDocument(*doc, path).ok());
    auto back = loadDocument(path);
    ASSERT_TRUE(back.ok());
    run(**back, "param w = 50 mm");
    EXPECT_NEAR(massProperties((*back)->state().bodies.at("e2")->shape()).volume, 50 * 20 * 3, 1e-3);
    // linked=no keeps a copy that stays put
    auto fixedCopy = build("param w = 40 mm\nsketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=w h=20\nextrude id=e1 sketch=s1 distance=10\n"
                           "sketch id=s2 plane=XY offset=20mm\nproject face:e1/end sketch=s2 linked=no\n"
                           "extrude id=e2 sketch=s2 distance=3 op=new\n");
    run(*fixedCopy, "param w = 60 mm");
    EXPECT_NEAR(massProperties(fixedCopy->state().bodies.at("e2")->shape()).volume, 40 * 20 * 3, 1e-3);
}

TEST(SketchOffset, CopiesFollowTheirSource)
{
    auto areas = [](const Document& d) {
        std::vector<double> a;
        for (auto& r : d.state().sketches.at("s1")->profiles.regions) {
            a.push_back(r.area);
        }
        std::sort(a.begin(), a.end());
        return a;
    };
    auto total = [&](const Document& d) {
        double t = 0;
        for (double a : areas(d)) {
            t += a;
        }
        return t;
    };
    // a frame 3 mm outside a rectangle; both the rectangle and the gap are parameters
    auto doc = build("param w = 40 mm\nparam gap = 3 mm\nsketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=w h=20\n"
                     "offset sketch=s1 at=20,0 distance=gap\n");
    EXPECT_NEAR(total(*doc), 46 * 26, 1e-6);
    const int dof = doc->state().sketches.at("s1")->report.dof;
    run(*doc, "param w = 60 mm");  // the source changes: the copy follows
    expectAllOk(*doc);
    EXPECT_NEAR(total(*doc), 66 * 26, 1e-6);
    run(*doc, "param gap = 5 mm");  // and its distance
    expectAllOk(*doc);
    EXPECT_NEAR(total(*doc), 70 * 30, 1e-6);
    EXPECT_EQ(doc->state().sketches.at("s1")->report.dof, dof);  // the copy adds no freedom
    // kept with the design
    const auto path = (tempDir() / "linked-offset.jig").string();
    ASSERT_TRUE(saveDocument(*doc, path).ok());
    auto back = loadDocument(path);
    ASSERT_TRUE(back.ok());
    run(**back, "param w = 50 mm");
    EXPECT_NEAR(total(**back), 60 * 30, 1e-6);
    // linked=no: a plain copy that stays where it was put
    auto plain = build("param w = 40 mm\nsketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=w h=20\n"
                       "offset sketch=s1 at=20,0 distance=3 linked=no\n");
    run(*plain, "param w = 60 mm");
    EXPECT_NE(total(*plain), 66 * 26);
}

TEST(SketchCopy, LinkedGridAndRingFollowTheOriginal)
{
    auto total = [](const Document& d) {
        double t = 0;
        for (auto& r : d.state().sketches.at("s1")->profiles.regions) {
            t += r.area;
        }
        return t;
    };
    // a 3 x 2 grid of 4 mm circles 10 apart; the circle and the spacing are parameters
    auto doc = build("param d = 4 mm\nparam pitch = 10 mm\nsketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=d\n"
                     "grid sketch=s1 at=2,0 count=3,2 spacing=pitch\n");
    EXPECT_NEAR(total(*doc), 6 * pi * 4, 1e-6);
    const int dof = doc->state().sketches.at("s1")->report.dof;
    run(*doc, "param d = 6 mm");  // every copy grows with the original
    expectAllOk(*doc);
    EXPECT_NEAR(total(*doc), 6 * pi * 9, 1e-6);
    run(*doc, "param d = 12 mm");  // 12 mm circles 10 apart overlap: the areas merge, so check the spread
    run(*doc, "param d = 4 mm");
    run(*doc, "param pitch = 15 mm");
    expectAllOk(*doc);
    double xmax = -1e9;
    for (auto& [id, e] : doc->state().sketches.at("s1")->solved.entities()) {
        if (e.type == EntityType::Circle) {
            xmax = std::max(xmax, doc->state().sketches.at("s1")->solved.pointXY(e.center).first);
        }
    }
    EXPECT_NEAR(xmax, 30, 1e-6);  // the third column at twice the new pitch
    EXPECT_EQ(doc->state().sketches.at("s1")->report.dof, dof);
    // a ring of 6 round the origin follows the original's size
    auto ring = build("param d = 4 mm\nsketch id=s1 plane=XY\ncircle sketch=s1 center=20,0 d=d\n"
                      "ring sketch=s1 at=22,0 center=0,0 count=6\n");
    EXPECT_NEAR(total(*ring), 6 * pi * 4, 1e-6);
    run(*ring, "param d = 8 mm");
    expectAllOk(*ring);
    EXPECT_NEAR(total(*ring), 6 * pi * 16, 1e-6);
}

TEST(Text, BendsAlongACircle)
{
    // "OOOOO" round the top of a 40 mm construction circle: every letter's middle stays
    // about the radius from the centre, and they spread round it instead of along a line
    auto doc = build("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=40 construction=yes\n"
                     "text sketch=s1 center=0,20 height=5 text=\"OOOOO\" along=20,0\n");
    const auto& regions = doc->state().sketches.at("s1")->profiles.regions;
    ASSERT_GE(regions.size(), 5u);
    double ymin = 1e300;
    for (auto& r : regions) {
        GProp_GProps g;
        BRepGProp::SurfaceProperties(r.face, g);
        const gp_Pnt c = g.CentreOfMass();
        EXPECT_NEAR(std::hypot(c.X(), c.Y()), 20, 1.5);
        ymin = std::min(ymin, c.Y());
    }
    EXPECT_LT(ymin, 19.5);  // the outer letters come down round the curve
    // the same text straight: every letter at the same height
    auto flat = build("sketch id=s1 plane=XY\ntext sketch=s1 center=0,20 height=5 text=\"OOOOO\"\n");
    for (auto& r : flat->state().sketches.at("s1")->profiles.regions) {
        GProp_GProps g;
        BRepGProp::SurfaceProperties(r.face, g);
        EXPECT_NEAR(g.CentreOfMass().Y(), 20, 0.5);
    }
    // along a slanted construction line: the letters line up along it, at its angle
    auto slant = build("sketch id=s1 plane=XY\nline sketch=s1 p1=0,0 p2=40,30 construction=yes\n"
                       "text sketch=s1 center=20,15 height=5 text=\"OOOO\" along=20,15\n");
    std::vector<gp_Pnt> mids;
    for (auto& r : slant->state().sketches.at("s1")->profiles.regions) {
        GProp_GProps g;
        BRepGProp::SurfaceProperties(r.face, g);
        mids.push_back(g.CentreOfMass());
    }
    ASSERT_GE(mids.size(), 4u);
    for (auto& m : mids) {
        EXPECT_NEAR(std::abs(3 * m.X() - 4 * m.Y()) / 5, 0, 0.3);  // on the line 3x = 4y
    }
    // along a smooth curve through three points: every letter near the curve
    auto curve = build("sketch id=s1 plane=XY\nspline sketch=s1 points=\"0,0; 30,15; 60,0\"\n"
                       "text sketch=s1 center=30,15 height=4 text=\"OOOOO\" along=30,15\n");
    double lowest = 1e9;
    for (auto& r : curve->state().sketches.at("s1")->profiles.regions) {
        if (r.key.rfind("T", 0) != 0) {
            continue;
        }
        GProp_GProps g;
        BRepGProp::SurfaceProperties(r.face, g);
        lowest = std::min(lowest, g.CentreOfMass().Y());
    }
    EXPECT_LT(lowest, 14);  // the outer letters follow the curve down from its top at 15
    // and it is kept with the design
    const auto path = (tempDir() / "bent-text.jig").string();
    ASSERT_TRUE(saveDocument(*doc, path).ok());
    auto back = loadDocument(path);
    ASSERT_TRUE(back.ok());
    EXPECT_NE((*back)->state().sketches.at("s1")->solved.texts().begin()->second.path, 0);
}

TEST(SketchTrim, EllipsesAndSmoothCurves)
{
    // a line trimmed back to where a smooth curve crosses it
    {
        Sketch s;
        const int line = s.addLine(0, -10, 0, 20);
        s.addSpline({{-20, 0}, {0, 5}, {20, 0}});
        ASSERT_TRUE(s.trim(line, 0, 15));  // the part above the curve goes
        auto [x2, y2] = s.pointXY(s.entity(line)->p2);
        auto [x1, y1] = s.pointXY(s.entity(line)->p1);
        EXPECT_NEAR(std::max(y1, y2), 5, 0.05);
    }
    // half an ellipse trimmed away at a line through its middle; the line's ends trimmed too:
    // a closed half ellipse (area half of pi a b). The line runs through two of the ellipse's
    // sample points; nudged by rounding-sized amounts either way it must still close (macOS on
    // Intel rounds cos(pi/2) differently and once found no closed area)
    for (double nudge : {0.0, 1e-15, -1e-15, 1e-13, -1e-13, 1e-11, -1e-11}) {
        SCOPED_TRACE(nudge);
        Sketch s;
        const int el = s.addEllipse(0, 0, 20, 0, 10);
        const int line = s.addLine(nudge, -15, nudge, 15);
        ASSERT_TRUE(s.trim(el, 20, 0));  // the right half goes
        int splines = 0;
        for (auto& [id, e] : s.entities()) {
            splines += e.type == EntityType::Spline;
        }
        EXPECT_EQ(splines, 1);
        ASSERT_TRUE(s.trim(line, nudge, 13));
        ASSERT_TRUE(s.trim(line, nudge, -13));
        auto prof = buildProfiles(s, originPlaneFrame(OriginPlane::XY, 0));
        ASSERT_TRUE(prof.ok());
        ASSERT_EQ(prof->regions.size(), 1u) << "the half ellipse closes";
        EXPECT_NEAR(prof->regions[0].area, pi * 20 * 10 / 2, pi * 100 * 0.01);
    }
    // a smooth curve broken where a line crosses it: two curves sharing the point between them
    {
        Sketch s;
        const int sp = s.addSpline({{0, 0}, {10, 10}, {20, 0}});
        s.addLine(10, -5, 10, 20);
        auto pieces = s.breakAt(sp, 5, 5);
        ASSERT_TRUE(pieces.ok()) << pieces.error().message;
        ASSERT_EQ(pieces->size(), 2u);
        const auto& a = s.entity((*pieces)[0])->pts;
        const auto& b = s.entity((*pieces)[1])->pts;
        EXPECT_TRUE(a.back() == b.front() || a.front() == b.back());
        auto [mx, my] = s.pointXY(a.back() == b.front() ? a.back() : a.front());
        EXPECT_NEAR(mx, 10, 1e-6);
    }
}

TEST(Loft, ThroughSquaresIsAFrustum)
{
    // 20 x 20 at z = 0 to 10 x 10 at z = 30: a frustum, h/3 (A1 + A2 + sqrt(A1 A2)) = 7000
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\n"
                     "sketch id=s2 plane=XY offset=30\nrect sketch=s2 center=0,0 w=10 h=10\n"
                     "loft id=lf1 sections=s1;s2\n");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).volume, 7000, 1e-3);
    const auto& body = *doc->state().bodies.at("lf1");
    for (const char* name : {"lf1/start", "lf1/end", "lf1/side1", "lf1/side4"}) {
        EXPECT_EQ(body.findFaces(name).size(), 1u) << name;
    }
    // through a smaller square in the middle and back out (ruled: straight between sections)
    auto waist = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\n"
                       "sketch id=s2 plane=XY offset=15\nrect sketch=s2 center=0,0 w=10 h=10\n"
                       "sketch id=s3 plane=XY offset=30\nrect sketch=s3 center=0,0 w=20 h=20\n"
                       "loft id=lf1 sections=s1;s2;s3 ruled=yes\n");
    expectAllOk(*waist);
    EXPECT_NEAR(props(*waist).volume, 7000, 1e-3);
    // a square base blending into a round top (4 edges into 1): a valid solid whose volume lies
    // between the circle's cylinder and the square's prism
    auto round = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\n"
                       "sketch id=s2 plane=XY offset=30\ncircle sketch=s2 center=0,0 d=16\n"
                       "loft id=lf1 sections=s1;s2\n");
    expectAllOk(*round);
    const auto rp = props(*round);
    EXPECT_GT(rp.volume, pi * 64 * 30);
    EXPECT_LT(rp.volume, 400 * 30);
    EXPECT_TRUE(jig::massProperties(round->state().bodies.at("lf1")->shape()).valid);
    // a loft cut out of a block: 30 x 30 x 30 minus the frustum
    auto cut = build("sketch id=b plane=XY\nrect sketch=b center=0,0 w=30 h=30\nextrude id=e1 sketch=b distance=30\n"
                     "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\n"
                     "sketch id=s2 plane=XY offset=30\nrect sketch=s2 center=0,0 w=10 h=10\n"
                     "loft id=lf1 sections=s1;s2 op=cut\n");
    expectAllOk(*cut);
    EXPECT_NEAR(props(*cut).volume, 27000 - 7000, 1e-3);
    // refused with reasons: one section; a sketch with two closed areas and none picked
    auto one = std::make_unique<Document>();
    ScriptRunner r1(*one);
    r1.strict = false;
    ASSERT_TRUE(r1.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\nloft id=lf1 sections=s1\n").ok());
    EXPECT_EQ(one->status(1).level, StatusLevel::Error);
    auto two = std::make_unique<Document>();
    ScriptRunner r2(*two);
    r2.strict = false;
    ASSERT_TRUE(r2.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=20 h=20\nrect sketch=s1 center=40,0 w=5 h=5\n"
                           "sketch id=s2 plane=XY offset=10\nrect sketch=s2 center=0,0 w=10 h=10\n"
                           "loft id=lf1 sections=s1;s2\n")
                    .ok());
    EXPECT_EQ(two->status(2).level, StatusLevel::Error);
    EXPECT_NE(two->status(2).message.find("several closed areas"), std::string::npos) << two->status(2).message;
}

TEST(Sweep, AlongLinesAndArcs)
{
    // a 4 mm circle on the front plane pushed 50 mm along Y: a rod, pi r^2 L
    auto rod = build("sketch id=s1 plane=XZ\ncircle sketch=s1 center=0,0 d=4\n"
                     "sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,50\n"
                     "sweep id=sw1 sketch=s1 path=s2\n");
    expectAllOk(*rod);
    EXPECT_NEAR(props(*rod).volume, pi * 4 * 50, 1e-3);
    const auto& body = *rod->state().bodies.at("sw1");
    for (const char* name : {"sw1/start", "sw1/end", "sw1/side1"}) {
        EXPECT_EQ(body.findFaces(name).size(), 1u) << name;
    }
    // a ring (the area between two circles, picked by its key) stays a tube all the way:
    // pi (R^2 - r^2) L
    auto rings = build("sketch id=s1 plane=XZ\ncircle sketch=s1 center=0,0 d=10\ncircle sketch=s1 center=0,0 d=6\n");
    std::string ringKey;
    for (const auto& rg : rings->state().sketches.at("s1")->profiles.regions) {
        if (std::abs(rg.area - pi * (25 - 9)) < 1e-6) {
            ringKey = rg.key;
        }
    }
    ASSERT_FALSE(ringKey.empty());
    auto tube = build("sketch id=s1 plane=XZ\ncircle sketch=s1 center=0,0 d=10\ncircle sketch=s1 center=0,0 d=6\n"
                      "sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,50\n"
                      "sweep id=sw1 sketch=s1 path=s2 profiles=" + ringKey + "\n");
    expectAllOk(*tube);
    EXPECT_NEAR(props(*tube).volume, pi * (25 - 9) * 50, 1e-2);
    // round a bend: 20 mm straight, then a quarter turn of radius 10 (Pappus: area x path length)
    auto bend = build("sketch id=s1 plane=XZ\ncircle sketch=s1 center=0,0 d=4\n"
                      "sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,20\n"
                      "arc sketch=s2 center=10,20 r=10 from=90deg to=180deg\n"
                      "sweep id=sw1 sketch=s1 path=s2\n");
    expectAllOk(*bend);
    EXPECT_NEAR(props(*bend).volume, pi * 4 * (20 + 5 * pi), pi * 4 * (20 + 5 * pi) * 1e-3);
    // cut a channel through a block
    auto cut = build("sketch id=b plane=XY\nrect sketch=b center=0,25 w=20 h=50\nextrude id=e1 sketch=b distance=10 "
                     "direction=symmetric\n"
                     "sketch id=s1 plane=XZ\ncircle sketch=s1 center=0,0 d=4\n"
                     "sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,50\n"
                     "sweep id=sw1 sketch=s1 path=s2 op=cut\n");
    expectAllOk(*cut);
    EXPECT_NEAR(props(*cut).volume, 20 * 50 * 10 - pi * 4 * 50, 1e-2);
    // the path in the shape's own sketch is refused with a reason
    auto self = std::make_unique<Document>();
    ScriptRunner r(*self);
    r.strict = false;
    ASSERT_TRUE(r.runText("sketch id=s1 plane=XZ\ncircle sketch=s1 center=0,0 d=4\nsweep id=sw1 sketch=s1 path=s1\n").ok());
    EXPECT_EQ(self->status(1).level, StatusLevel::Error);
}

TEST(Pipe, RodsTubesBendsRingsAndLimits)
{
    // a 6 mm rod 50 mm long, without a circle sketch: pi r^2 L
    auto rod = build("sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,50\npipe id=pi1 path=s2 diameter=6mm\n");
    expectAllOk(*rod);
    EXPECT_NEAR(props(*rod).volume, pi * 9 * 50, 1e-3);
    const auto& body = *rod->state().bodies.at("pi1");
    for (const char* name : {"pi1/start", "pi1/end", "pi1/outside"}) {
        EXPECT_GE(body.findFaces(name).size(), 1u) << name;
    }
    // a tube with a 1 mm wall around a bend (Pappus: ring area x path length)
    auto tube = build("sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,20\n"
                      "arc sketch=s2 center=10,20 r=10 from=90deg to=180deg\n"
                      "pipe id=pi1 path=s2 diameter=6mm wall=1mm\n");
    expectAllOk(*tube);
    const double ring = pi * (9 - 4), len = 20 + 5 * pi;
    EXPECT_NEAR(props(*tube).volume, ring * len, ring * len * 1e-3);
    EXPECT_GE(tube->state().bodies.at("pi1")->findFaces("pi1/inside").size(), 1u);
    // a closed path: a ring (torus) of 4 mm wire around a 20 mm circle, 2 pi^2 R r^2
    auto torus = build("sketch id=s2 plane=XY\ncircle sketch=s2 center=0,0 d=40\npipe id=pi1 path=s2 diameter=4mm\n");
    expectAllOk(*torus);
    EXPECT_NEAR(props(*torus).volume, 2 * pi * pi * 20 * 4, 2 * pi * pi * 20 * 4 * 1e-3);
    // follows a parameter
    auto param = build("param d = 8 mm\nsketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,10\n"
                       "pipe id=pi1 path=s2 diameter=d\n");
    expectAllOk(*param);
    EXPECT_NEAR(props(*param).volume, pi * 16 * 10, 1e-3);
    // cuts a channel
    auto cut = build("sketch id=b plane=XY\nrect sketch=b center=0,25 w=20 h=50\nextrude id=e1 sketch=b distance=10 "
                     "direction=symmetric\nsketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,50\n"
                     "pipe id=pi1 path=s2 diameter=4mm op=cut\n");
    expectAllOk(*cut);
    EXPECT_NEAR(props(*cut).volume, 20 * 50 * 10 - pi * 4 * 50, 1e-2);
    // a wall that fills the pipe, and a missing path, are explained
    for (const char* bad : {"sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,50\npipe id=pi1 path=s2 diameter=6mm wall=3mm\n",
                            "sketch id=s2 plane=XY\nline sketch=s2 p1=0,0 p2=0,50\npipe id=pi1 path=s2 diameter=0mm\n"}) {
        auto doc = std::make_unique<Document>();
        ScriptRunner r(*doc);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        EXPECT_EQ(doc->status(1).level, StatusLevel::Error) << bad;
        EXPECT_FALSE(doc->status(1).message.empty());
    }
    // saved and loaded
    auto back = Document::fromJson(rod->toJson());
    ASSERT_TRUE(back.ok());
    EXPECT_NEAR(props(**back).volume, pi * 9 * 50, 1e-3);
}

TEST(Coil, SpringsBothHandsSquareSectionsAndLimits)
{
    // a round 2 mm wire, 20 mm across (to the wire's middle), 4 turns at a 5 mm pitch: the
    // section screws around the axis, so the volume is its area times 2 pi R times the turns
    auto spring = build("coil id=co1 plane=XY diameter=20mm pitch=5mm turns=4 size=2mm\n");
    expectAllOk(*spring);
    const double want = pi * 1 * (2 * pi * 10) * 4;
    auto pr = props(*spring);
    EXPECT_TRUE(pr.valid);
    EXPECT_EQ(pr.solids, 1);
    EXPECT_NEAR(pr.volume, want, want * 1e-3);
    // it stands on the plane and climbs about 4 turns x 5 mm (plus the wire's thickness)
    EXPECT_NEAR(pr.zmax - pr.zmin, 4 * 5 + 2, 0.05);
    EXPECT_NEAR(pr.xmax - pr.xmin, 22, 0.05);
    const auto& body = *spring->state().bodies.at("co1");
    for (const char* name : {"co1/start", "co1/end", "co1/outside"}) {
        EXPECT_GE(body.findFaces(name).size(), 1u) << name;
    }
    // left-handed, flipped below the plane, at a centre, with half a turn: same volume rule
    auto left = build("coil id=co1 plane=XY center=30,0 diameter=20mm pitch=5mm turns=2.5 size=2mm hand=left flip=yes\n");
    expectAllOk(*left);
    auto pl = props(*left);
    EXPECT_NEAR(pl.volume, pi * (2 * pi * 10) * 2.5, pi * (2 * pi * 10) * 2.5 * 1e-3);
    EXPECT_LT(pl.zmax, 1.01);
    EXPECT_NEAR((pl.xmax + pl.xmin) / 2, 30, 0.5);
    // the two hands really turn opposite ways: the end of a quarter turn is on opposite sides
    auto qr = build("coil id=co1 plane=XY diameter=20mm pitch=5mm turns=0.25 size=2mm\n");
    auto ql = build("coil id=co1 plane=XY diameter=20mm pitch=5mm turns=0.25 size=2mm hand=left\n");
    EXPECT_GT(props(*qr).ymax, 9);
    EXPECT_LT(props(*ql).ymin, -9);
    // a square section (a flat-wire spring), on the side plane, following a parameter
    auto sq = build("param p = 6 mm\ncoil id=co1 plane=YZ diameter=30mm pitch=p turns=3 size=3mm section=square\n");
    expectAllOk(*sq);
    EXPECT_NEAR(props(*sq).volume, 9 * (2 * pi * 15) * 3, 9 * (2 * pi * 15) * 3 * 1e-3);
    EXPECT_NEAR(props(*sq).xmax - props(*sq).xmin, 3 * 6 + 3, 0.05);
    EXPECT_GE(sq->state().bodies.at("co1")->findFaces("co1/side1").size(), 1u);
    // turns that run into each other, a section reaching the middle and no turns are explained
    for (const char* bad : {"coil id=co1 plane=XY diameter=20mm pitch=2mm turns=3 size=2mm\n",
                            "coil id=co1 plane=XY diameter=2mm pitch=5mm turns=3 size=2mm\n",
                            "coil id=co1 plane=XY diameter=20mm pitch=5mm turns=0 size=2mm\n"}) {
        auto doc = std::make_unique<Document>();
        ScriptRunner r(*doc);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        EXPECT_EQ(doc->status(0).level, StatusLevel::Error) << bad;
        EXPECT_FALSE(doc->status(0).message.empty());
    }
    auto back = Document::fromJson(spring->toJson());
    ASSERT_TRUE(back.ok());
    EXPECT_NEAR(props(**back).volume, want, want * 1e-3);
}

TEST(Primitive, BoxCylinderSphereTorusPlacedAndNamed)
{
    // a box standing on the floor plane, centred at (10, 5): 30 x 20 x 10
    auto box = build("box id=bx1 center=10,5 length=30mm width=20mm height=10mm\n");
    expectAllOk(*box);
    auto pb = props(*box);
    EXPECT_NEAR(pb.volume, 30 * 20 * 10, 1e-6);
    EXPECT_NEAR(pb.xmin, -5, 1e-6);
    EXPECT_NEAR(pb.xmax, 25, 1e-6);
    EXPECT_NEAR(pb.ymin, -5, 1e-6);
    EXPECT_NEAR(pb.zmin, 0, 1e-6);
    EXPECT_NEAR(pb.zmax, 10, 1e-6);
    const auto& body = *box->state().bodies.at("bx1");
    for (const char* n : {"bx1/bottom", "bx1/top", "bx1/left", "bx1/right", "bx1/front", "bx1/back"}) {
        EXPECT_EQ(body.findFaces(n).size(), 1u) << n;
    }
    // the top face is where a fillet or a sketch can find it after the box changes size
    auto filleted = build("param h = 10 mm\nbox id=bx1 length=30mm width=20mm height=h\nfillet id=f1 edges=face:bx1/top radius=2mm\n"
                          "param h = 15 mm\n");
    expectAllOk(*filleted);
    EXPECT_LT(props(*filleted).volume, 30 * 20 * 15 - 1);  // the four top edges are rounded
    EXPECT_NEAR(props(*filleted).zmax, 15, 1e-6);
    // a cylinder on the side plane, flipped (grows towards -X)
    auto cyl = build("cylinder id=cy1 plane=YZ diameter=10mm height=20mm flip=yes\n");
    expectAllOk(*cyl);
    auto pc = props(*cyl);
    EXPECT_NEAR(pc.volume, pi * 25 * 20, 1e-3);
    EXPECT_NEAR(pc.xmin, -20, 1e-6);
    EXPECT_NEAR(pc.xmax, 0, 1e-6);
    for (const char* n : {"cy1/bottom", "cy1/top", "cy1/side"}) {
        EXPECT_GE(cyl->state().bodies.at("cy1")->findFaces(n).size(), 1u) << n;
    }
    // a sphere centred on the plane, and a torus lying on it
    auto sph = build("sphere id=sp1 diameter=12mm\n");
    expectAllOk(*sph);
    EXPECT_NEAR(props(*sph).volume, 4.0 / 3 * pi * 216, 1e-2);
    EXPECT_NEAR(props(*sph).zmin, -6, 1e-6);
    auto tor = build("torus id=to1 plane=XZ diameter=40mm tube=10mm\n");
    expectAllOk(*tor);
    EXPECT_NEAR(props(*tor).volume, 2 * pi * pi * 20 * 25, 1e-1);
    EXPECT_NEAR(props(*tor).ymax - props(*tor).ymin, 10, 1e-6);
    // a hole through the box with a cylinder cut
    auto cut = build("box id=bx1 length=30mm width=20mm height=10mm\ncylinder diameter=6mm height=10mm op=cut\n");
    expectAllOk(*cut);
    EXPECT_NEAR(props(*cut).volume, 6000 - pi * 9 * 10, 1e-3);
    // sizes that make no shape are explained
    for (const char* bad : {"box id=bx1 length=0mm width=20mm height=10mm\n", "torus id=to1 diameter=10mm tube=10mm\n"}) {
        auto doc = std::make_unique<Document>();
        ScriptRunner r(*doc);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        EXPECT_EQ(doc->status(0).level, StatusLevel::Error) << bad;
        EXPECT_FALSE(doc->status(0).message.empty());
    }
}

TEST(Rib, GussetFillsFromTheEdgeToTheWalls)
{
    // an L bracket on the front plane: a 40 x 5 base and a 5 x 40 wall, 20 deep (Y from -10
    // to 10); a rib edge from (35, 5) to (5, 35) on the front plane: the gusset is the
    // triangle between the edge and the two inner faces, 30 x 30 / 2, 3 mm thick
    const std::string bracket = "sketch id=s1 plane=XZ\nrect sketch=s1 corner=0,0 w=40 h=5\nrect sketch=s1 corner=0,0 w=5 h=40\n"
                                "extrude id=e1 sketch=s1 distance=20mm direction=symmetric\n";
    auto base = build(bracket);
    expectAllOk(*base);
    const double before = props(*base).volume;
    auto doc = build(bracket + "sketch id=s2 plane=XZ\nline sketch=s2 p1=35,5 p2=5,35\nrib id=rb1 sketch=s2 thickness=3mm\n");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).volume - before, 30 * 30 / 2.0 * 3, 1e-3);
    EXPECT_EQ(doc->state().bodies.size(), 1u);
    EXPECT_TRUE(props(*doc).valid);
    EXPECT_EQ(doc->status(3).level, StatusLevel::Ok);  // the rib's sketch: open on purpose
    EXPECT_GE(doc->state().bodies.begin()->second->findFaces("rb1/side").size(), 1u);
    // follows a thickness parameter and the bracket growing
    auto par = build("param t = 4 mm\n" + bracket + "sketch id=s2 plane=XZ\nline sketch=s2 p1=35,5 p2=5,35\n"
                     "rib id=rb1 sketch=s2 thickness=t\n");
    expectAllOk(*par);
    EXPECT_NEAR(props(*par).volume - before, 30 * 30 / 2.0 * 4, 1e-3);
    // an edge with nothing to meet on either side, and a closed shape, are explained
    for (const std::string& bad : {bracket + "sketch id=s2 plane=XZ\nline sketch=s2 p1=60,60 p2=80,80\nrib id=rb1 sketch=s2\n",
                                   bracket + "sketch id=s2 plane=XZ\nrect sketch=s2 corner=10,10 w=5 h=5\nrib id=rb1 sketch=s2\n"}) {
        auto d = std::make_unique<Document>();
        ScriptRunner r(*d);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        EXPECT_EQ(d->status(int(d->model().features.size()) - 1).level, StatusLevel::Error) << bad;
        EXPECT_FALSE(d->status(int(d->model().features.size()) - 1).message.empty());
    }
}

TEST(Emboss, WrapsAreasOntoRoundFacesRaisedOrSunk)
{
    // a 40 mm cylinder 30 high; a plane 25 mm in front of its axis; a 10 x 10 square on it
    // wraps 10 mm round (0.5 rad of the 20 mm radius) and 10 mm up
    const std::string cup = "cylinder id=cy1 diameter=40mm height=30mm\nplane id=pl1 base=XZ distance=-25mm\n"
                            "sketch id=s2 plane=pl1\nrect sketch=s2 center=0,15 w=10 h=10\n";
    auto base = build(cup);
    const double v0 = props(*base).volume;
    auto raised = build(cup + "emboss id=em1 sketch=s2 face=cy1/side depth=1mm\n");
    expectAllOk(*raised);
    auto pr = props(*raised);
    EXPECT_TRUE(pr.valid);
    EXPECT_EQ(raised->state().bodies.size(), 1u);
    EXPECT_NEAR(pr.volume - v0, 0.25 * (21 * 21 - 400) * 10, 0.2);
    // it is on the side facing the sketch's plane, between 10 and 20 mm up
    EXPECT_NEAR(pr.zmax, 30, 1e-6);
    auto sunk = build(cup + "emboss id=em1 sketch=s2 face=cy1/side depth=1mm op=cut\n");
    expectAllOk(*sunk);
    EXPECT_NEAR(v0 - props(*sunk).volume, 0.25 * (400 - 19 * 19) * 10, 0.2);
    // lettering: every letter raised, holes in letters kept
    auto text = build("cylinder id=cy1 diameter=40mm height=30mm\nplane id=pl1 base=XZ distance=-25mm\n"
                      "sketch id=s2 plane=pl1\ntext sketch=s2 center=0,15 height=8mm text=\"Jig 8\"\n"
                      "emboss id=em1 sketch=s2 face=cy1/side depth=0.8mm\n");
    expectAllOk(*text);
    EXPECT_TRUE(props(*text).valid);
    EXPECT_GT(props(*text).volume, v0 + 5);
    // a flat face and a plane across the axis are explained
    for (const std::string& bad : {cup + "emboss id=em1 sketch=s2 face=cy1/top depth=1mm\n",
                                   std::string("cylinder id=cy1 diameter=40mm height=30mm\nsketch id=s2 plane=XY\n"
                                               "rect sketch=s2 center=0,0 w=5 h=5\nemboss id=em1 sketch=s2 face=cy1/side\n")}) {
        auto d = std::make_unique<Document>();
        ScriptRunner r(*d);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        const int last = int(d->model().features.size()) - 1;
        EXPECT_EQ(d->status(last).level, StatusLevel::Error) << bad;
        EXPECT_FALSE(d->status(last).message.empty());
    }
}

TEST(Plane, TangentThroughPointsAndAcrossAPath)
{
    // tangent to an upright 40 mm cylinder: on the front (the front plane's side), then turned
    // 90 degrees around the axis; text sketched on it wraps with Emboss
    auto doc = build("cylinder id=cy1 diameter=40mm height=30mm\n"
                     "plane id=pl1 kind=tangent face=face:cy1/side\n"
                     "plane id=pl2 kind=tangent face=face:cy1/side angle=90deg\n"
                     "point id=p1 at=0,0,0\npoint id=p2 at=10,0,0\npoint id=p3 at=0,10,10\n"
                     "plane id=pl3 kind=points points=p1;p2;p3\n"
                     "sketch id=s1 plane=XY\nline sketch=s1 p1=0,0 p2=0,40\n"
                     "plane id=pl4 kind=path path=s1 position=0.25\n"
                     "sketch id=s2 plane=pl1\nrect sketch=s2 center=0,0 w=6 h=6\n"
                     "emboss id=em1 sketch=s2 face=cy1/side depth=1mm\n");
    expectAllOk(*doc);
    const auto& pl = doc->state().planes;
    const gp_Ax3 a = pl.at("pl1"), b = pl.at("pl2"), c = pl.at("pl3"), d = pl.at("pl4");
    // touches the cylinder 20 mm from the axis, halfway up, facing out
    const gp_Pnt la = a.Location(), lb = b.Location();
    EXPECT_NEAR(std::hypot(la.X(), la.Y()), 20, 1e-6);
    EXPECT_NEAR(la.Z(), 15, 1e-6);
    EXPECT_NEAR(gp_Vec(la.X(), la.Y(), 0).Normalized().Dot(gp_Vec(a.Direction())), 1, 1e-9);
    EXPECT_NEAR(std::abs(a.YDirection().Z()), 1, 1e-9);  // y runs up the axis
    EXPECT_NEAR(std::hypot(lb.X(), lb.Y()), 20, 1e-6);
    EXPECT_NEAR(gp_Vec(a.Direction()).Dot(gp_Vec(b.Direction())), 0, 1e-9);  // a quarter turn apart
    // the three points lie on pl3
    for (const gp_Pnt& q : {gp_Pnt(0, 0, 0), gp_Pnt(10, 0, 0), gp_Pnt(0, 10, 10)}) {
        EXPECT_NEAR(gp_Vec(c.Location(), q).Dot(gp_Vec(c.Direction())), 0, 1e-9);
    }
    // a quarter of the way along a 40 mm line up Y, square across it
    EXPECT_NEAR(d.Location().Y(), 10, 1e-6);
    EXPECT_NEAR(std::abs(d.Direction().Y()), 1, 1e-9);
    // the square wrapped onto the side through the tangent plane adds 6 x 6 wrapped, 1 mm out
    EXPECT_NEAR(props(*doc).volume - pi * 400 * 30, 6 / 20.0 / 2 * (21 * 21 - 400) * 6, 0.2);
    // points in a line, a flat face and a base along the axis are explained
    for (const char* bad : {"point id=p1 at=0,0,0\npoint id=p2 at=1,0,0\npoint id=p3 at=2,0,0\nplane id=pl kind=points points=p1;p2;p3\n",
                            "cylinder id=cy1 diameter=40mm height=30mm\nplane id=pl kind=tangent face=face:cy1/top\n",
                            "cylinder id=cy1 diameter=40mm height=30mm\nplane id=pl kind=tangent face=face:cy1/side base=XY\n"}) {
        auto e = std::make_unique<Document>();
        ScriptRunner r(*e);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        const int last = int(e->model().features.size()) - 1;
        EXPECT_EQ(e->status(last).level, StatusLevel::Error) << bad;
    }
}

TEST(Draft, SidesSlopeAroundTheNeutralPlane)
{
    // a 20 mm cube on the floor; its four sides sloped 5 degrees from the floor up: the top
    // shrinks by 20 tan 5 on each side, the bottom stays (a frustum)
    const std::string cube = "box id=bx1 length=20mm width=20mm height=20mm\n";
    auto doc = build(cube + "draft id=dr1 faces=\"face:bx1/left;face:bx1/right;face:bx1/front;face:bx1/back\" "
                            "plane=XY angle=5deg\n");
    expectAllOk(*doc);
    const double top = 20 - 2 * 20 * std::tan(5 * pi / 180);
    const double want = 20.0 / 3 * (400 + top * top + 20 * top);
    auto pr = props(*doc);
    EXPECT_TRUE(pr.valid);
    EXPECT_NEAR(pr.volume, want, 1e-3);
    EXPECT_NEAR(pr.xmax - pr.xmin, 20, 1e-6);  // the bottom keeps its size
    // the faces keep their names, so a fillet on the top edges still finds them
    const auto& body = *doc->state().bodies.begin()->second;
    for (const char* n : {"bx1/left", "bx1/top", "bx1/bottom"}) {
        EXPECT_EQ(body.findFaces(n).size(), 1u) << n;
    }
    auto more = build(cube + "draft id=dr1 faces=\"face:bx1/left;face:bx1/right\" plane=XY angle=5deg\n"
                             "fillet id=f1 edges=face:bx1/top radius=1mm\n");
    expectAllOk(*more);
    // flipped: the sides slope outwards going up
    auto flip = build(cube + "draft id=dr1 faces=\"face:bx1/left\" plane=XY angle=5deg flip=yes\n");
    expectAllOk(*flip);
    EXPECT_NEAR(props(*flip).volume, 8000 + 0.5 * 20 * 20 * std::tan(5 * pi / 180) * 20, 1e-3);
    // neutral plane halfway up (a construction plane): the middle keeps its size
    auto mid = build(cube + "plane id=pl1 base=XY distance=10mm\n"
                            "draft id=dr1 faces=\"face:bx1/left;face:bx1/right\" plane=pl1 angle=5deg\n");
    expectAllOk(*mid);
    EXPECT_NEAR(props(*mid).volume, 8000, 1e-3);
    // the top (across the pull) and a right angle are refused with a reason
    for (const std::string& bad : {cube + "draft id=dr1 faces=face:bx1/top plane=XY angle=5deg\n",
                                   cube + "draft id=dr1 faces=face:bx1/left plane=XY angle=90deg\n"}) {
        auto d = std::make_unique<Document>();
        ScriptRunner r(*d);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        EXPECT_EQ(d->status(1).level, StatusLevel::Error) << bad;
        EXPECT_FALSE(d->status(1).message.empty());
    }
}

TEST(Scale, EvenlyPerAxisAboutAPointAndCopies)
{
    const std::string cube = "box id=bx1 length=10mm width=10mm height=10mm\n";
    // twice as big about the origin: 8 times the volume; the bottom stays on the floor
    auto two = build(cube + "scale id=sc1 bodies=bx1 factor=2\n");
    expectAllOk(*two);
    EXPECT_NEAR(props(*two).volume, 8000, 1e-6);
    EXPECT_NEAR(props(*two).zmin, 0, 1e-9);
    EXPECT_NEAR(props(*two).xmax, 10, 1e-9);
    EXPECT_EQ(two->state().bodies.at("bx1")->findFaces("bx1/top").size(), 1u);  // names kept
    // halved about a construction point at the cube's top centre: the top stays put
    auto half = build(cube + "point id=p1 at=0,0,10\nscale id=sc1 bodies=bx1 factor=0.5 about=p1\n");
    expectAllOk(*half);
    EXPECT_NEAR(props(*half).volume, 125, 1e-6);
    EXPECT_NEAR(props(*half).zmax, 10, 1e-9);
    EXPECT_NEAR(props(*half).zmin, 5, 1e-9);
    // stretched along Z only, from a parameter; then a fillet on the stretched body's top
    auto tall = build("param k = 3 kind=number\n" + cube + "scale id=sc1 bodies=bx1 x=1 y=1 z=k\n"
                      "fillet id=f1 edges=face:bx1/top radius=1mm\n");
    expectAllOk(*tall);
    EXPECT_NEAR(props(*tall).zmax, 30, 1e-6);
    EXPECT_LT(props(*tall).volume, 3000);
    // a scaled copy is a new body; the original stays
    auto copy = build(cube + "scale id=sc1 bodies=bx1 factor=2 copy=yes\n");
    expectAllOk(*copy);
    EXPECT_EQ(copy->state().bodies.size(), 2u);
    EXPECT_NEAR(massProperties(copy->state().bodies.at("sc1")->shape()).volume, 8000, 1e-6);
    // zero, a length and a missing point are refused with a reason
    for (const std::string& bad : {cube + "scale id=sc1 bodies=bx1 factor=0\n", cube + "scale id=sc1 bodies=bx1 factor=2mm\n",
                                   cube + "scale id=sc1 bodies=bx1 factor=2 about=nowhere\n"}) {
        auto d = std::make_unique<Document>();
        ScriptRunner r(*d);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        EXPECT_EQ(d->status(1).level, StatusLevel::Error) << bad;
        EXPECT_FALSE(d->status(1).message.empty());
    }
}

TEST(Thread, ShaftsAndHolesGetAHelicalGroove)
{
    // a 6 mm shaft 20 long with a 1 mm pitch: the groove removes about its in-material
    // triangle (depth^2 tan 30) screwed round at its centroid radius, over the threaded length
    auto shaft = build("cylinder id=cy1 diameter=6mm height=20mm\nthread id=th1 face=cy1/side pitch=1mm\n");
    expectAllOk(*shaft);
    auto ps = props(*shaft);
    EXPECT_TRUE(ps.valid);
    EXPECT_EQ(ps.solids, 1);
    const double depth = 0.6134, R = 3;
    const double perTurn = depth * depth * std::tan(pi / 6) * 2 * pi * (R - depth / 3);
    const double removed = pi * 9 * 20 - ps.volume;
    EXPECT_GT(removed, perTurn * 17);
    EXPECT_LT(removed, perTurn * 20);
    EXPECT_NEAR(ps.xmax - ps.xmin, 6, 1e-3);  // the major diameter stays
    // the two hands differ
    auto left = build("cylinder id=cy1 diameter=6mm height=20mm\nthread id=th1 face=cy1/side pitch=1mm hand=left\n");
    expectAllOk(*left);
    EXPECT_NEAR(props(*left).volume, ps.volume, ps.volume * 1e-4);
    // a 5 mm hole through a block, threaded: material is cut outwards from the hole wall
    auto nut = build("box id=bx1 length=20mm width=20mm height=10mm\ncylinder id=cy2 diameter=5mm height=10mm op=cut\n"
                     "thread id=th1 face=cy2/side pitch=0.8mm\n");
    expectAllOk(*nut);
    auto pn = props(*nut);
    EXPECT_TRUE(pn.valid);
    EXPECT_LT(pn.volume, 4000 - pi * 6.25 * 10 - 1);
    // flat faces, a pitch larger than the radius and a face too short are refused
    for (const char* bad : {"cylinder id=cy1 diameter=6mm height=20mm\nthread id=th1 face=cy1/top pitch=1mm\n",
                            "cylinder id=cy1 diameter=6mm height=20mm\nthread id=th1 face=cy1/side pitch=4mm\n",
                            "cylinder id=cy1 diameter=6mm height=0.5mm\nthread id=th1 face=cy1/side pitch=1mm\n"}) {
        auto d = std::make_unique<Document>();
        ScriptRunner r(*d);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        EXPECT_EQ(d->status(1).level, StatusLevel::Error) << bad;
        EXPECT_FALSE(d->status(1).message.empty());
    }
}

TEST(MeshImport, StlObjAnd3mfBecomeSolids)
{
    // a box with a hole, out as STL and 3MF and back in as solids of the same volume
    auto part = build("box id=bx1 length=30mm width=20mm height=10mm\ncylinder diameter=6mm height=10mm op=cut\n");
    const double v = props(*part).volume;
    const TopoDS_Shape shape = part->state().bodies.begin()->second->shape();
    const auto dir = tempDir();
    const std::string stl = (dir / "part.stl").string(), mf = (dir / "part.3mf").string();
    ASSERT_TRUE(exportStl(shape, stl, 0.01).ok());
    ASSERT_TRUE(export3mf({{"part", shape}}, mf, 0.01).ok());
    for (const std::string& path : {stl, mf}) {
        auto doc = std::make_unique<Document>();
        auto id = insertMesh(*doc, path);
        ASSERT_TRUE(id.ok()) << id.error().message;
        expectAllOk(*doc);
        auto pr = props(*doc);
        EXPECT_TRUE(pr.valid) << path;
        EXPECT_EQ(pr.solids, 1) << path;
        EXPECT_NEAR(pr.volume, v, v * 0.01) << path;  // the hole is faceted
        EXPECT_LT(doc->state().bodies.begin()->second->faceCount(), 200) << path;  // flat sides merged
    }
    // a cube as text OBJ (quads), in inches
    const std::string obj = (dir / "cube.obj").string();
    {
        std::ofstream o(obj);
        o << "# cube\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0 0 1\nv 1 0 1\nv 1 1 1\nv 0 1 1\n"
             "f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 2 3 7 6\nf 3 4 8 7\nf 4 1 5 8\n";
    }
    auto cube = build("import mesh " + obj + " unit=in id=ms1\n");
    expectAllOk(*cube);
    EXPECT_NEAR(props(*cube).volume, 25.4 * 25.4 * 25.4, 1e-3);
    EXPECT_EQ(cube->state().bodies.at("ms1")->faceCount(), 6);
    // the inserted cube survives saving and loading (kept inside the design)
    auto back = Document::fromJson(cube->toJson());
    ASSERT_TRUE(back.ok());
    EXPECT_NEAR(props(**back).volume, 25.4 * 25.4 * 25.4, 1e-3);
    // an open mesh (one side missing), an unknown file kind and too many triangles are explained
    {
        std::ofstream o((dir / "open.obj").string());
        o << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nv 0 0 1\nv 1 0 1\nv 1 1 1\nv 0 1 1\n"
             "f 1 4 3 2\nf 5 6 7 8\nf 1 2 6 5\nf 2 3 7 6\nf 3 4 8 7\n";
    }
    auto d = std::make_unique<Document>();
    // without repair it is refused; with repair (the default) the missing side is closed
    auto openMesh = readMeshFile((dir / "open.obj").string());
    ASSERT_TRUE(openMesh.ok());
    auto refused = solidFromMesh(*openMesh, 20000, false);
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.error().message.find("holes"), std::string::npos) << refused.error().message;
    int holes = 0;
    auto mended = solidFromMesh(*openMesh, 20000, true, &holes);
    ASSERT_TRUE(mended.ok()) << mended.error().message;
    EXPECT_EQ(holes, 1);
    EXPECT_NEAR(massProperties(mended.value()).volume, 1, 1e-9);
    auto open = insertMesh(*d, (dir / "open.obj").string());
    ASSERT_TRUE(open.ok());
    EXPECT_NE(d->status(0).message.find("1 hole"), std::string::npos) << d->status(0).message;
    EXPECT_FALSE(insertMesh(*d, (dir / "part.step").string()).ok());
    auto mesh = readMeshFile(stl);
    ASSERT_TRUE(mesh.ok());
    auto big = solidFromMesh(*mesh, 10);
    ASSERT_FALSE(big.ok());
    EXPECT_FALSE(big.error().suggestions.empty());
    // damaged files never crash
    for (const char* junk : {"solid x\nfacet\nvertex 1 nan 2\n", "f 1 2 3\n", "PK\x03\x04garbage"}) {
        for (const char* ext : {".stl", ".obj", ".3mf"}) {
            const std::string p = (dir / (std::string("junk") + ext)).string();
            {
                std::ofstream o(p, std::ios::binary);
                o << junk;
            }
            EXPECT_FALSE(insertMesh(*d, p).ok()) << junk << ext;
        }
    }
}

TEST(Canvas, PicturesArePlacedOnPlanesAndKeptInTheDesign)
{
    EXPECT_EQ(base64Encode(""), "");
    EXPECT_EQ(base64Encode("f"), "Zg==");
    EXPECT_EQ(base64Encode("fo"), "Zm8=");
    EXPECT_EQ(base64Encode("foo"), "Zm9v");
    EXPECT_EQ(base64Encode("foobar"), "Zm9vYmFy");
    // a PNG header saying 200 x 100, and a JPEG with a start-of-frame saying 640 x 480
    std::string png("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR\0\0\0\xc8\0\0\0\x64\x08\x06\0\0\0", 29);
    std::string jpg("\xff\xd8\xff\xe0\0\x04xx\xff\xc0\0\x11\x08\x01\xe0\x02\x80\x03", 19);
    EXPECT_EQ(pictureSize(png), std::make_pair(200, 100));
    EXPECT_EQ(pictureSize(jpg), std::make_pair(640, 480));
    EXPECT_FALSE(pictureSize("GIF89a....").has_value());
    const auto dir = tempDir();
    const std::string path = (dir / "logo.png").string();
    {
        std::ofstream o(path, std::ios::binary);
        o << png;
    }
    // on the front plane, centred at (10, 20), 100 wide: 50 high; turned 90 degrees
    auto doc = build("canvas id=cv1 file=" + path + " plane=XZ center=10,20 width=100mm angle=90deg opacity=0.4\n");
    expectAllOk(*doc);
    const auto& cv = doc->state().canvases.at("cv1");
    EXPECT_NEAR(cv.width, 100, 1e-9);
    EXPECT_NEAR(cv.height, 50, 1e-9);
    EXPECT_NEAR(cv.opacity, 0.4, 1e-9);
    EXPECT_NEAR(cv.frame.Location().X(), 10, 1e-9);
    EXPECT_NEAR(cv.frame.Location().Z(), 20, 1e-9);
    EXPECT_NEAR(std::abs(cv.frame.XDirection().Z()), 1, 1e-9);  // its width now runs up
    EXPECT_TRUE(doc->state().bodies.empty());                   // it changes no geometry
    // the picture travels inside the design
    auto back = Document::fromJson(doc->toJson());
    ASSERT_TRUE(back.ok());
    EXPECT_EQ((*back)->model().features[0].params.value("image", std::string {}), base64Encode(png));
    EXPECT_NEAR((*back)->state().canvases.at("cv1").height, 50, 1e-9);
    // not a picture, and a missing file, are refused with a reason
    const std::string txt = (dir / "notes.png").string();
    {
        std::ofstream o(txt);
        o << "hello";
    }
    auto d = std::make_unique<Document>();
    EXPECT_FALSE(insertCanvas(*d, txt, "XY", "0", "0", "100 mm").ok());
    EXPECT_FALSE(insertCanvas(*d, (dir / "none.png").string(), "XY", "0", "0", "100 mm").ok());
}

TEST(Joint, PartsMeetTurnSlideStopAtLimitsAndInterfere)
{
    // two separate boxes; a rigid joint puts the small one's bottom on the big one's top,
    // centred and facing it
    const std::string parts = "box id=a length=40mm width=40mm height=10mm\n"
                              "box id=b center=100,0 length=10mm width=20mm height=6mm op=new\n";
    auto rigid = build(parts + "joint id=j1 kind=rigid from=face:b/bottom to=face:a/top\n");
    expectAllOk(*rigid);
    auto bb = massProperties(rigid->state().bodies.at("b")->shape());
    EXPECT_NEAR(bb.zmin, 10, 1e-6);
    EXPECT_NEAR(bb.zmax, 16, 1e-6);
    EXPECT_NEAR((bb.xmin + bb.xmax) / 2, 0, 1e-6);
    EXPECT_NEAR((bb.ymin + bb.ymax) / 2, 0, 1e-6);
    EXPECT_NEAR(massProperties(rigid->state().bodies.at("a")->shape()).zmax, 10, 1e-9);  // the other stays
    // revolute: turned 90 degrees about the top's normal, the small box's long side swaps axes
    auto turned = build(parts + "joint id=j1 kind=revolute from=face:b/bottom to=face:a/top angle=90deg\n");
    expectAllOk(*turned);
    auto bt = massProperties(turned->state().bodies.at("b")->shape());
    EXPECT_NEAR(bt.xmax - bt.xmin, 20, 1e-6);
    EXPECT_NEAR(bt.ymax - bt.ymin, 10, 1e-6);
    // the angle follows a parameter, and editing it moves the part
    auto param = build("param lid = 0 deg\n" + parts +
                       "joint id=j1 kind=revolute from=face:b/bottom to=face:a/top angle=lid\nparam lid = 90 deg\n");
    expectAllOk(*param);
    EXPECT_NEAR(massProperties(param->state().bodies.at("b")->shape()).xmax, 10, 1e-6);
    // slider with a limit: asked 50, stops at 20 and says so
    auto slid = build(parts + "joint id=j1 kind=slider from=face:b/bottom to=face:a/top distance=50mm distance_max=20mm\n");
    EXPECT_NEAR(massProperties(slid->state().bodies.at("b")->shape()).zmin, 30, 1e-6);
    EXPECT_EQ(slid->status(2).level, StatusLevel::Warning);
    EXPECT_NE(slid->status(2).message.find("limit"), std::string::npos);
    // a pin into a hole: a round edge to a round edge lines up their axes
    auto pin = build("box id=a length=40mm width=40mm height=10mm\ncylinder id=h diameter=6mm height=10mm op=cut\n"
                     "cylinder id=p center=80,30 diameter=6mm height=30mm op=new\n"
                     "joint id=j1 kind=revolute from=face:p/bottom to=face:a/top offset=-10mm\n");
    expectAllOk(*pin);
    auto pp = massProperties(pin->state().bodies.at("p")->shape());
    EXPECT_NEAR((pp.xmin + pp.xmax) / 2, 0, 1e-6);
    EXPECT_NEAR(pp.zmin, 0, 1e-6);
    // interference: the pin in its hole does not overlap; a box pushed into the plate does
    EXPECT_TRUE(interferences(pin->state()).empty());
    auto clash = build(parts + "joint id=j1 kind=rigid from=face:b/bottom to=face:a/top offset=-2mm\n");
    auto hits = interferences(clash->state());
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_NEAR(hits[0].volume, 10 * 20 * 2, 1e-6);
    // the same part twice, and a pick on nothing, are explained
    auto self = std::make_unique<Document>();
    ScriptRunner r(*self);
    r.strict = false;
    ASSERT_TRUE(r.runText(parts + "joint id=j1 kind=rigid from=face:b/bottom to=face:b/top\n").ok());
    EXPECT_EQ(self->status(2).level, StatusLevel::Error);
}

TEST(Appearance, ColoursAreCheckedSavedAndExported)
{
    auto doc = build("box id=bx1 length=10mm width=10mm height=10mm\nappearance body=bx1 color=3a7bd5 finish=metal\n");
    ASSERT_EQ(doc->appearances().size(), 1u);
    EXPECT_EQ(doc->appearances().at("bx1").finish, "metal");
    EXPECT_FALSE(doc->setAppearance("bx1", {"#12345", "satin"}).ok());
    EXPECT_FALSE(doc->setAppearance("bx1", {"#123456", "chrome"}).ok());
    auto back = Document::fromJson(doc->toJson());
    ASSERT_TRUE(back.ok());
    EXPECT_EQ((*back)->appearances().at("bx1").color, "#3a7bd5");
    // a damaged colour in a file is dropped, not kept
    auto j = doc->toJson();
    j["view"]["appearances"]["bx1"]["color"] = "javascript:";
    auto damaged = Document::fromJson(j);
    ASSERT_TRUE(damaged.ok());
    EXPECT_TRUE((*damaged)->appearances().empty());
    // the 3MF carries the colour as a base material the object refers to, and reads back
    const std::string mf = (tempDir() / "blue.3mf").string();
    ScriptRunner r(*doc);
    ASSERT_TRUE(r.runText("export 3mf " + mf + "\n").ok());
    auto mesh = readMeshFile(mf);
    ASSERT_TRUE(mesh.ok());
    auto bytes = readWholeFile(mf, 1 << 20);
    ASSERT_TRUE(bytes.has_value());
    auto d2 = std::make_unique<Document>();
    ASSERT_TRUE(insertMesh(*d2, mf).ok());
    EXPECT_NEAR(props(*d2).volume, 1000, 1e-6);
}

TEST(Variant, NamedSetsOfParameterValues)
{
    auto doc = build("param w = 30 mm\nparam h = 10 mm\nbox id=bx1 length=w width=20mm height=h\n"
                     "variant save name=Small\nparam w = 60 mm\nparam h = 20 mm\nvariant save name=Large\n");
    expectAllOk(*doc);
    EXPECT_NEAR(props(*doc).volume, 60 * 20 * 20, 1e-6);
    ScriptRunner r(*doc);
    ASSERT_TRUE(r.runText("variant use name=Small\n").ok());
    EXPECT_NEAR(props(*doc).volume, 30 * 20 * 10, 1e-6);
    // one undo step each
    ASSERT_TRUE(doc->undo());
    EXPECT_NEAR(props(*doc).volume, 60 * 20 * 20, 1e-6);
    // saved with the design
    auto back = Document::fromJson(doc->toJson());
    ASSERT_TRUE(back.ok());
    ASSERT_EQ((*back)->model().variants.size(), 2u);
    EXPECT_EQ((*back)->model().variants[0].values.at("w"), "30 mm");
    // saving under the same name updates it; a parameter the design no longer has is reported
    ASSERT_TRUE(r.runText("param w = 45 mm\nvariant save name=Small\n").ok());
    EXPECT_EQ(doc->model().variants.size(), 2u);
    auto left = useVariant(*doc, "Small");
    ASSERT_TRUE(left.ok());
    EXPECT_TRUE(left->empty());
    EXPECT_NEAR(props(*doc).volume, 45 * 20 * 20, 1e-6);
    // delete; unknown names and a design with no parameters are refused
    ASSERT_TRUE(deleteVariant(*doc, "Large").ok());
    EXPECT_FALSE(useVariant(*doc, "Large").ok());
    auto empty = std::make_unique<Document>();
    EXPECT_FALSE(saveVariant(*empty, "A").ok());
    EXPECT_FALSE(saveVariant(*doc, "").ok());
}

TEST(Fastener, ScrewsNutsAndWashersAtTheirStandardSizes)
{
    // an M3 x 10 socket head cap screw: head 5.5 across, 3 high, above the plane; shank 3 x 10 below
    auto cap = build("fastener id=fs1 kind=capscrew size=M3 length=10mm\n");
    expectAllOk(*cap);
    auto pc = props(*cap);
    EXPECT_TRUE(pc.valid);
    EXPECT_NEAR(pc.zmax, 3, 1e-6);
    EXPECT_NEAR(pc.zmin, -10, 1e-6);
    EXPECT_NEAR(pc.xmax - pc.xmin, 5.5, 1e-6);
    const double head = pi * 2.75 * 2.75 * 3, shank = pi * 1.5 * 1.5 * 10;
    EXPECT_LT(pc.volume, head + shank);           // the hex socket is cut out
    EXPECT_GT(pc.volume, head + shank - 15);
    for (const char* n : {"fs1/top", "fs1/shank", "fs1/under_head", "fs1/end"}) {
        EXPECT_GE(cap->state().bodies.at("fs1")->findFaces(n).size(), 1u) << n;
    }
    // an M6 nut: 10 across flats, 5.2 high, with a 6 mm hole
    auto nut = build("fastener id=n1 kind=nut size=M6\n");
    expectAllOk(*nut);
    const double hexArea = std::sqrt(3.0) / 2 * 10 * 10;
    EXPECT_NEAR(props(*nut).volume, (hexArea - pi * 9) * 5.2, 1e-3);
    // an M8 washer: 8.4 / 16 x 1.6
    auto washer = build("fastener id=w1 kind=washer size=M8\n");
    EXPECT_NEAR(props(*washer).volume, pi * (64 - 4.2 * 4.2) * 1.6, 1e-3);
    // a hex head screw sized by a parameter, flipped and placed off-centre
    auto hex = build("param L = 16 mm\nfastener id=fs1 kind=hexscrew size=M4 length=L center=20,0 flip=yes\n");
    expectAllOk(*hex);
    EXPECT_NEAR(props(*hex).zmax, 16, 1e-6);
    EXPECT_NEAR((props(*hex).xmin + props(*hex).xmax) / 2, 20, 1e-6);
    // a screw cut into a block leaves its shape as a hole
    auto cut = build("box id=bx1 length=20mm width=20mm height=10mm\nplane id=pl1 base=XY distance=10mm\n"
                     "fastener kind=capscrew size=M3 length=10mm plane=pl1 op=cut\n");
    expectAllOk(*cut);
    EXPECT_NEAR(props(*cut).volume, 4000 - pi * 1.5 * 1.5 * 10, 1e-3);
    // unknown sizes and kinds are explained
    for (const char* bad : {"fastener kind=capscrew size=M7\n", "fastener kind=rivet size=M3\n"}) {
        auto d = std::make_unique<Document>();
        ScriptRunner r(*d);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        EXPECT_EQ(d->status(0).level, StatusLevel::Error) << bad;
    }
}

TEST(Joint, PinInASlotTurnsAndSlides)
{
    const std::string parts = "box id=a length=40mm width=40mm height=10mm\n"
                              "box id=b center=100,0 length=10mm width=20mm height=6mm op=new\n";
    // slides 8 mm along the top's x and turns a quarter
    auto doc = build(parts + "joint id=j1 kind=pinslot from=face:b/bottom to=face:a/top distance=8mm angle=90deg\n");
    expectAllOk(*doc);
    auto bb = massProperties(doc->state().bodies.at("b")->shape());
    EXPECT_NEAR(std::abs((bb.xmin + bb.xmax) / 2) + std::abs((bb.ymin + bb.ymax) / 2), 8, 1e-6);
    EXPECT_NEAR(bb.zmin, 10, 1e-6);
    EXPECT_NEAR(std::max(bb.xmax - bb.xmin, bb.ymax - bb.ymin), 20, 1e-6);
}

TEST(SketchScale, CurvesAndTheirDimensionsScale)
{
    // a dimensioned 10 x 6 rectangle and a 4 mm circle, doubled about the origin: 20 x 12 and 8
    auto doc = build("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=6\ncircle sketch=s1 center=20,0 d=4\n"
                     "scale sketch=s1 at=5,0;-5,0;0,3;0,-3;22,0 factor=2\n");
    expectAllOk(*doc);
    const auto& st = *doc->state().sketches.at("s1");
    std::vector<double> areas;
    for (auto& r : st.profiles.regions) {
        areas.push_back(r.area);
    }
    std::sort(areas.begin(), areas.end());
    ASSERT_EQ(areas.size(), 2u);
    EXPECT_NEAR(areas[0], pi * 16, 1e-6);  // the circle, now 8 across
    EXPECT_NEAR(areas[1], 240, 1e-6);      // the rectangle, 20 x 12
    // the circle's centre moved away from the origin too
    double cxMax = 0;
    for (auto& [id, e] : st.solved.entities()) {
        if (e.type == EntityType::Circle) {
            cxMax = st.solved.pointXY(e.center).first;
        }
    }
    EXPECT_NEAR(cxMax, 40, 1e-6);
    // halved about a point, and refused for a zero factor or nothing near
    auto half = build("sketch id=s1 plane=XY\nrect sketch=s1 center=10,10 w=10 h=10\n"
                      "scale sketch=s1 at=15,10;5,10;10,15;10,5 factor=0.5 about=10,10\n");
    expectAllOk(*half);
    EXPECT_NEAR(half->state().sketches.at("s1")->profiles.regions[0].area, 25, 1e-6);
    for (const char* bad : {"sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=6\nscale sketch=s1 at=5,0 factor=0\n",
                            "sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=6\nscale sketch=s1 at=50,50 factor=2\n"}) {
        auto d = std::make_unique<Document>();
        ScriptRunner r(*d);
        EXPECT_FALSE(r.runText(bad).ok()) << bad;
    }
}

TEST(SketchCurves, ControlPointSplines)
{
    // a closed control-point spline on a square's corners: inside the square (it only touches
    // the control polygon's shape), symmetric, and an area that extrudes
    auto doc = build("sketch id=s1 plane=XY\nspline sketch=s1 points=-10,-10;10,-10;10,10;-10,10 closed=yes control=yes\n"
                     "extrude id=e1 sketch=s1 distance=5mm\n");
    expectAllOk(*doc);
    auto p = props(*doc);
    EXPECT_TRUE(p.valid);
    EXPECT_NEAR(p.xmax, -p.xmin, 1e-6);
    EXPECT_GT(p.volume, 5 * 200);  // still mostly the square
    // a uniform cubic B-spline is widest halfway along a side: (-10 + 23·10 + 23·10 - 10) / 48
    EXPECT_NEAR(p.xmax, 440.0 / 48, 1e-6);
    // an open one starts and ends on its end points, but misses the middle one
    auto open = build("sketch id=s1 plane=XY\nspline sketch=s1 points=0,0;10,10;20,0 control=yes\n");
    const Sketch& s = open->state().sketches.at("s1")->solved;
    int id = 0;
    for (auto& [eid, e] : s.entities()) {
        if (e.type == EntityType::Spline) {
            id = eid;
            EXPECT_TRUE(e.control);
        }
    }
    const auto pieces = s.splinePieces(id);
    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_NEAR(pieces.front()[0].first, 0, 1e-12);
    EXPECT_NEAR(pieces.back()[3].first, 20, 1e-12);
    EXPECT_LT(pieces.front()[3].second, 10 - 1);  // the curve passes below the middle control point
    // kept when saved, and a point can be held on it
    auto back = Document::fromJson(open->toJson());
    ASSERT_TRUE(back.ok());
    bool control = false;
    for (auto& [eid, e] : (*back)->state().sketches.at("s1")->solved.entities()) {
        control = control || (e.type == EntityType::Spline && e.control);
    }
    EXPECT_TRUE(control);
}

TEST(Joint, AsBuiltKeepsThePartWhereItIs)
{
    const std::string parts = "box id=a length=40mm width=40mm height=10mm\n"
                              "box id=b center=100,0 length=10mm width=20mm height=6mm op=new\n";
    // as built about the base's top: the small box stays at x = 100, then turns about the top's
    // middle (the origin's vertical): a quarter turn takes it to y = 100
    auto still = build(parts + "joint id=j1 kind=revolute from=face:b/bottom to=face:a/top as_built=yes\n");
    expectAllOk(*still);
    EXPECT_NEAR(massProperties(still->state().bodies.at("b")->shape()).xmin, 95, 1e-6);
    auto turned = build(parts + "joint id=j1 kind=revolute from=face:b/bottom to=face:a/top as_built=yes angle=90deg\n");
    auto bt = massProperties(turned->state().bodies.at("b")->shape());
    EXPECT_NEAR((bt.ymin + bt.ymax) / 2, 100, 1e-6);
    EXPECT_NEAR(bt.zmin, 0, 1e-6);
}

TEST(Joint, RigidGroupsMoveTogether)
{
    // a lid with a handle grouped to it: the hinge turns both
    const std::string parts = "box id=a length=40mm width=40mm height=10mm\n"
                              "box id=b center=100,0 length=10mm width=20mm height=6mm op=new\n"
                              "box id=c center=100,30 length=4mm width=4mm height=4mm op=new\n";
    auto doc = build(parts + "rigid id=rg1 bodies=b;c\njoint id=j1 kind=rigid from=face:b/bottom to=face:a/top\n");
    expectAllOk(*doc);
    auto cb = massProperties(doc->state().bodies.at("c")->shape());
    EXPECT_NEAR((cb.xmin + cb.xmax) / 2, 0, 1e-6);   // came along with b
    EXPECT_NEAR((cb.ymin + cb.ymax) / 2, 30, 1e-6);
    EXPECT_NEAR(cb.zmin, 10, 1e-6);
    // without the group the handle stays
    auto alone = build(parts + "joint id=j1 kind=rigid from=face:b/bottom to=face:a/top\n");
    EXPECT_NEAR(massProperties(alone->state().bodies.at("c")->shape()).xmin, 98, 1e-6);
    // a joint between two parts of one group is refused; one part is not a group
    for (const std::string& bad : {parts + "rigid id=rg1 bodies=a;b\njoint id=j1 kind=rigid from=face:b/bottom to=face:a/top\n",
                                   parts + "rigid id=rg1 bodies=a\n"}) {
        auto d = std::make_unique<Document>();
        ScriptRunner r(*d);
        r.strict = false;
        ASSERT_TRUE(r.runText(bad).ok());
        EXPECT_EQ(d->status(int(d->model().features.size()) - 1).level, StatusLevel::Error) << bad;
    }
}

TEST(SketchStretch, PointsInTheBoxMoveAndJoinedCurvesFollow)
{
    // a free 10 x 6 rectangle (no dimensions): its right side pulled 5 mm further right
    auto doc = build("sketch id=s1 plane=XY\nline sketch=s1 p1=0,0 p2=10,0\nline p1=10,0 p2=10,6\nline p1=10,6 p2=0,6\n"
                     "line p1=0,6 p2=0,0\nstretch sketch=s1 box=8,-1,12,7 by=5,0\n");
    expectAllOk(*doc);
    const auto& r = doc->state().sketches.at("s1")->profiles.regions;
    ASSERT_EQ(r.size(), 1u);
    EXPECT_NEAR(r[0].area, 15 * 6, 1e-6);
    // nothing inside the box is refused
    auto d = std::make_unique<Document>();
    ScriptRunner sr(*d);
    EXPECT_FALSE(sr.runText("sketch id=s1 plane=XY\nline sketch=s1 p1=0,0 p2=10,0\nstretch sketch=s1 box=50,50,60,60 by=1,0\n").ok());
}
