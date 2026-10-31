// SPDX-License-Identifier: GPL-3.0-or-later
// Revolve, Hole, Pattern and Mirror (acceptance scenario A2, the knob), checked against
// hand-computed volumes.
#include <cmath>
#include <numbers>
#include <random>

#include "helpers.hpp"
#include "jig/io.hpp"
#include "jig/screws.hpp"
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
    EXPECT_EQ(d->skipped.at("TEXT"), 1);
    auto doc = build("sketch id=s1 plane=XY\nimport dxf \"" + path + "\" sketch=s1\n");
    const auto& regions = doc->state().sketches.at("s1")->profiles.regions;
    std::vector<double> a;
    for (auto& r : regions) {
        a.push_back(r.area / (25.4 * 25.4));  // in square inches
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
        total += r.area;
    }
    EXPECT_NEAR(total, 8 + stadium, 1e-6);  // the same numbers, read as millimetres
    EXPECT_FALSE(parseDxf("this is not a dxf").ok());
    EXPECT_FALSE(parseDxf("0\nSECTION\n2\nENTITIES\n0\nENDSEC\n0\nEOF\n").ok());
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
    // curved faces are explained (edit the fillet instead)
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.runText("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=20\nextrude id=e1 sketch=s1 distance=10\n");
    std::string side;
    for (int i = 0; i < bad->state().bodies.at("e1")->faceCount(); ++i) {
        if (bad->state().bodies.at("e1")->faceName(i).find("side") != std::string::npos) {
            side = bad->state().bodies.at("e1")->faceName(i);
        }
    }
    auto res = r.runText("presspull id=pp1 face=" + side + " distance=2mm\n");
    EXPECT_FALSE(res.ok()) << side;
    ASSERT_EQ(bad->model().features.size(), 3u) << (res.ok() ? "" : res.error().message);
    EXPECT_NE(bad->status(2).message.find("flat"), std::string::npos) << bad->status(2).message;
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
    // a face that is not parallel is explained
    auto bad = std::make_unique<Document>();
    ScriptRunner r(*bad);
    r.runText("sketch id=s1 plane=XY\ncircle sketch=s1 center=0,0 d=10\nextrude id=e1 sketch=s1 extent=to to=YZ\n");
    EXPECT_NE(bad->status(1).message.find("parallel"), std::string::npos) << bad->status(1).message;
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
