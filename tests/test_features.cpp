// SPDX-License-Identifier: GPL-3.0-or-later
// Revolve, Hole, Pattern and Mirror (acceptance scenario A2, the knob), checked against
// hand-computed volumes.
#include <cmath>
#include <numbers>

#include "helpers.hpp"
#include "jig/io.hpp"

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
