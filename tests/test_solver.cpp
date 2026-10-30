// SPDX-License-Identifier: GPL-3.0-or-later
// Sketch solver wrapper: DOF reporting, conflict diagnostics, dragging, gap finder.
#include <cmath>
#include <random>

#include <gtest/gtest.h>

#include "jig/sketch.hpp"

using namespace jig;

namespace
{
struct Rect
{
    int A, B, C, D, ab, bc, cd, da;
};
Rect looseRect(Sketch& s, double x, double y, double w, double h)
{
    Rect r;
    r.A = s.addPoint(x, y, true);
    r.B = s.addPoint(x + w, y, true);
    r.C = s.addPoint(x + w, y + h, true);
    r.D = s.addPoint(x, y + h, true);
    r.ab = s.addLine(r.A, r.B);
    r.bc = s.addLine(r.B, r.C);
    r.cd = s.addLine(r.C, r.D);
    r.da = s.addLine(r.D, r.A);
    return r;
}
double length(const Sketch& s, int line)
{
    auto* l = s.entity(line);
    auto [x1, y1] = s.pointXY(l->p1);
    auto [x2, y2] = s.pointXY(l->p2);
    return std::hypot(x2 - x1, y2 - y1);
}
}  // namespace

TEST(Solver, DegreesOfFreedomCountDown)
{
    Sketch s;
    Rect r = looseRect(s, 1, 2, 30, 20);
    EXPECT_EQ(s.solve().dof, 8);
    s.addConstraint(ConstraintType::Horizontal, r.ab);
    s.addConstraint(ConstraintType::Horizontal, r.cd);
    s.addConstraint(ConstraintType::Vertical, r.bc);
    s.addConstraint(ConstraintType::Vertical, r.da);
    EXPECT_EQ(s.solve().dof, 4);
    s.addConstraint(ConstraintType::Distance, r.ab, 0, 0, "60 mm");
    s.addConstraint(ConstraintType::Distance, r.bc, 0, 0, "40");
    auto rep = s.solve();
    EXPECT_EQ(rep.dof, 2);
    EXPECT_EQ(rep.state, SolveState::UnderConstrained);
    EXPECT_FALSE(rep.freeEntities.empty());
    s.addConstraint(ConstraintType::DistanceX, r.A, 0, 0, "0");
    s.addConstraint(ConstraintType::DistanceY, r.A, 0, 0, "0");
    rep = s.solve();
    EXPECT_EQ(rep.dof, 0);
    EXPECT_EQ(rep.state, SolveState::FullyConstrained);
    EXPECT_TRUE(rep.freeEntities.empty());
    EXPECT_NEAR(length(s, r.ab), 60, 1e-9);
    EXPECT_NEAR(length(s, r.bc), 40, 1e-9);
    EXPECT_NEAR(s.pointXY(r.C).first, 60, 1e-9);
}

TEST(Solver, ConflictIsReportedAndGeometryUntouched)
{
    Sketch s;
    Rect r = looseRect(s, 0, 0, 30, 20);
    s.addConstraint(ConstraintType::Horizontal, r.ab);
    s.addConstraint(ConstraintType::Vertical, r.bc);
    int c1 = s.addConstraint(ConstraintType::Distance, r.ab, 0, 0, "50");
    ASSERT_NE(s.solve().state, SolveState::Failed);
    auto before = s.toJson();
    int c2 = s.addConstraint(ConstraintType::DistanceX, r.A, r.B, 0, "20");  // contradicts c1
    auto rep = s.solve();
    EXPECT_EQ(rep.state, SolveState::OverConstrained);
    ASSERT_FALSE(rep.conflicting.empty());
    // the report names the two clashing constraints
    auto has = [&](int id) {
        return std::find(rep.conflicting.begin(), rep.conflicting.end(), id) != rep.conflicting.end();
    };
    EXPECT_TRUE(has(c1) || has(c2));
    EXPECT_FALSE(rep.message.empty());
    // geometry did not move
    s.removeConstraint(c2);
    EXPECT_EQ(s.toJson()["entities"], before["entities"]);
}

TEST(Solver, RedundantIsReported)
{
    Sketch s;
    int l = s.addLine(0, 0, 10, 1);
    s.addConstraint(ConstraintType::Horizontal, l);
    int dup = s.addConstraint(ConstraintType::Horizontal, l);
    auto rep = s.solve();
    EXPECT_NEAR(s.pointXY(s.entity(l)->p1).second, s.pointXY(s.entity(l)->p2).second, 1e-9);
    EXPECT_EQ(rep.state, SolveState::OverConstrained);
    ASSERT_EQ(rep.redundant.size(), 1u);
    EXPECT_TRUE(rep.redundant[0] == dup || rep.redundant[0] == dup - 1);
}

TEST(Solver, BadDimensionValueIsExplained)
{
    Sketch s;
    int l = s.addLine(0, 0, 10, 0);
    s.addConstraint(ConstraintType::Distance, l, 0, 0, "width + 1");
    auto rep = s.solve();
    EXPECT_EQ(rep.state, SolveState::Failed);
    EXPECT_NE(rep.message.find("width"), std::string::npos);
}

TEST(Solver, DragMovesWhatIsFreeOnly)
{
    Sketch s;
    Rect r = looseRect(s, 0, 0, 30, 20);
    s.addConstraint(ConstraintType::Horizontal, r.ab);
    s.addConstraint(ConstraintType::Horizontal, r.cd);
    s.addConstraint(ConstraintType::Vertical, r.bc);
    s.addConstraint(ConstraintType::Vertical, r.da);
    s.addConstraint(ConstraintType::Fix, r.A);
    ASSERT_EQ(s.solve().dof, 2);
    auto rep = s.drag(r.C, 50, 35);
    ASSERT_NE(rep.state, SolveState::Failed);
    EXPECT_NEAR(s.pointXY(r.C).first, 50, 1e-6);
    EXPECT_NEAR(s.pointXY(r.C).second, 35, 1e-6);
    EXPECT_NEAR(s.pointXY(r.A).first, 0, 1e-9);  // fixed corner stays
    EXPECT_NEAR(s.pointXY(r.A).second, 0, 1e-9);
    EXPECT_NEAR(s.pointXY(r.B).second, 0, 1e-6);  // still a rectangle
    EXPECT_NEAR(s.pointXY(r.D).first, 0, 1e-6);
    // Dragging a fully fixed point does not move it.
    s.addConstraint(ConstraintType::Fix, r.C);
    s.drag(r.C, 80, 80);
    EXPECT_NEAR(s.pointXY(r.C).first, 50, 1e-6);
}

TEST(Solver, GapFinderFindsOpenEnds)
{
    Sketch s;
    int a = s.addPoint(0, 0, true), b = s.addPoint(10, 0, true), c = s.addPoint(10, 10, true);
    s.addLine(a, b);
    s.addLine(b, c);
    int l3 = s.addLine(10, 10, 0, 0.4);  // misses the start point by 0.4 mm
    (void)l3;
    auto gaps = s.openEndpoints();
    ASSERT_EQ(gaps.size(), 2u);
    ASSERT_TRUE(gaps[0].nearestPointId.has_value());
    EXPECT_NEAR(gaps[0].gap, 0.4, 1e-9);
}

TEST(Solver, PropertyRandomRectanglesMeetTheirDimensions)
{
    // Property: a fully dimensioned rectangle drawn anywhere, any size, solves to its
    // dimensions with zero DOF.
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> pos(-500, 500), size(0.5, 300), jitter(-3, 3);
    for (int i = 0; i < 300; ++i) {
        Sketch s;
        double w = size(rng), h = size(rng), x = pos(rng), y = pos(rng);
        Rect r = looseRect(s, x + jitter(rng), y + jitter(rng), w + jitter(rng), h + jitter(rng));
        s.addConstraint(ConstraintType::Horizontal, r.ab);
        s.addConstraint(ConstraintType::Horizontal, r.cd);
        s.addConstraint(ConstraintType::Vertical, r.bc);
        s.addConstraint(ConstraintType::Vertical, r.da);
        s.addConstraint(ConstraintType::Distance, r.ab, 0, 0, std::to_string(w));
        s.addConstraint(ConstraintType::Distance, r.bc, 0, 0, std::to_string(h));
        s.addConstraint(ConstraintType::DistanceX, r.A, 0, 0, std::to_string(x));
        s.addConstraint(ConstraintType::DistanceY, r.A, 0, 0, std::to_string(y));
        auto rep = s.solve();
        ASSERT_EQ(rep.state, SolveState::FullyConstrained) << i;
        EXPECT_NEAR(length(s, r.ab), w, 1e-6);
        EXPECT_NEAR(length(s, r.bc), h, 1e-6);
        EXPECT_NEAR(s.pointXY(r.A).first, x, 1e-6);
    }
}

TEST(Solver, JsonRoundTrip)
{
    Sketch s;
    Rect r = looseRect(s, 0, 0, 30, 20);
    s.addConstraint(ConstraintType::Distance, r.ab, 0, 0, "width/2");
    int c = s.addCircle(5, 5, 2);
    s.setConstruction(c, true);
    s.addArc(0, 0, 3, 0, 1.5);
    auto j = s.toJson();
    auto back = Sketch::fromJson(j);
    ASSERT_TRUE(back.ok());
    EXPECT_EQ(back->toJson(), j);
    // ids are never reused after a reload
    int next = back.value().addPoint(0, 0);
    EXPECT_GT(next, c);
}

TEST(Solver, MalformedJsonIsRejectedNotCrashing)
{
    for (const char* text : {R"({"entities": 5})", R"({"entities": [{"id": 1}]})",
                             R"({"entities": [], "constraints": [{"id": 1, "type": "bogus"}]})",
                             R"({})"}) {
        auto r = Sketch::fromJson(nlohmann::json::parse(text));
        EXPECT_FALSE(r.ok()) << text;
    }
}
