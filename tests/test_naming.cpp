// SPDX-License-Identifier: GPL-3.0-or-later
// Persistent-naming regression suite (brief 5.4a, acceptance scenario A4).
// Edit early dimensions, insert features mid-history, and assert that later
// sketches and fillets stay on the intended geometry with no manual repair.
#include <gtest/gtest.h>

#include "helpers.hpp"

using namespace jig;
using namespace jigtest;

namespace
{
int countFaces(const Document& doc, const std::string& prefix)
{
    int n = 0;
    for (auto& [id, b] : doc.state().bodies) {
        for (int i = 0; i < b->faceCount(); ++i) {
            n += b->faceName(i).rfind(prefix, 0) == 0;
        }
    }
    return n;
}

struct Snapshot
{
    MassProperties p;
    int f1, f2;
};
Snapshot snap(const Document& d)
{
    return {props(d), countFaces(d, "f1/fillet:"), countFaces(d, "f2/fillet:")};
}
}  // namespace

class PhoneStandNaming : public ::testing::Test
{
protected:
    void SetUp() override
    {
        doc = buildSample("phone_stand.jigs");
        expectAllOk(*doc);
        base = snap(*doc);
        ASSERT_GT(base.f1, 0);
        ASSERT_GT(base.f2, 0);
    }
    std::unique_ptr<Document> doc;
    Snapshot base;
};

TEST_F(PhoneStandNaming, WiderBaseKeepsEverythingAttached)
{
    run(*doc, "param base_w = 110 mm");
    expectAllOk(*doc);
    Snapshot s = snap(*doc);
    EXPECT_EQ(s.f1, base.f1);
    EXPECT_EQ(s.f2, base.f2);
    EXPECT_EQ(s.p.faces, base.p.faces);
    EXPECT_EQ(s.p.edges, base.p.edges);
    EXPECT_NEAR(s.p.xmax - s.p.xmin, 110, 1e-6);
    EXPECT_GT(s.p.volume, base.p.volume);
}

TEST_F(PhoneStandNaming, DeeperBaseAndThickerPlate)
{
    run(*doc, "param base_d = 85 mm");
    run(*doc, "param base_t = 8 mm");
    expectAllOk(*doc);
    Snapshot s = snap(*doc);
    EXPECT_EQ(s.f1, base.f1);
    EXPECT_EQ(s.f2, base.f2);
    EXPECT_EQ(s.p.faces, base.p.faces);
    // the lip sketch followed the top face up to z = 8
    EXPECT_NEAR(s.p.zmin, 0, 1e-6);
}

TEST_F(PhoneStandNaming, NarrowerSlot)
{
    run(*doc, "param slot_w = 10 mm");
    expectAllOk(*doc);
    Snapshot s = snap(*doc);
    EXPECT_EQ(s.f1, base.f1);
    EXPECT_GT(s.p.volume, base.p.volume);
}

TEST_F(PhoneStandNaming, InsertHoleBeforeTheFillets)
{
    // Roll back to before the first fillet, add a cut, roll forward.
    run(*doc, "rollback before=f1");
    run(*doc, "sketch id=s9 plane=XY\ncircle sketch=s9 center=\"-30, 20\" d=6\n"
              "extrude id=e9 sketch=s9 extent=all op=cut");
    run(*doc, "rollback end");
    expectAllOk(*doc);
    Snapshot s = snap(*doc);
    EXPECT_EQ(s.f1, base.f1);  // the new hole's edges were not added to the fillet
    EXPECT_EQ(s.f2, base.f2);
    EXPECT_EQ(doc->featureIndex("e9"), doc->featureIndex("f1") - 1);
    EXPECT_LT(s.p.volume, base.p.volume);
}

TEST_F(PhoneStandNaming, InsertBossBeforeTheLipSketch)
{
    // The lip sketch sits on the base's top face; a boss added to that face first
    // must not knock the sketch off it.
    run(*doc, "rollback before=s3");
    run(*doc, "sketch id=s8 on=e1/end\ncircle sketch=s8 center=\"20, 0\" d=10\n"
              "extrude id=e8 sketch=s8 distance=4 op=join");
    run(*doc, "rollback end");
    expectAllOk(*doc);
    Snapshot s = snap(*doc);
    EXPECT_EQ(s.f1, base.f1);
    EXPECT_EQ(s.f2, base.f2);
    EXPECT_GT(s.p.volume, base.p.volume);
}

TEST_F(PhoneStandNaming, EditThenInsertThenEdit)
{
    run(*doc, "param base_w = 100 mm");
    run(*doc, "rollback before=f1");
    run(*doc, "sketch id=s9 plane=XY\ncircle sketch=s9 center=\"30, 20\" d=5\n"
              "extrude id=e9 sketch=s9 extent=all op=cut");
    run(*doc, "rollback end");
    run(*doc, "param lip_h = 18 mm");
    run(*doc, "param lean = 70 deg");
    expectAllOk(*doc);
    Snapshot s = snap(*doc);
    EXPECT_EQ(s.f1, base.f1);
    EXPECT_EQ(s.f2, base.f2);
}

TEST_F(PhoneStandNaming, RemovedGeometryIsReportedNotGuessed)
{
    // Suppressing the slot removes edges the base fillet was told to round. The
    // fillet must say so (never re-attach silently to something else).
    run(*doc, "suppress e4");
    const auto& st = doc->status(doc->featureIndex("f1"));
    EXPECT_EQ(int(st.level), int(StatusLevel::Error));
    EXPECT_NE(st.message.find("no longer exist"), std::string::npos) << st.message;
    EXPECT_FALSE(st.suggestions.empty());
    // ...and later features still compute from the state before it.
    EXPECT_EQ(int(doc->status(doc->featureIndex("f2")).level), int(StatusLevel::Ok));
    run(*doc, "unsuppress e4");
    expectAllOk(*doc);
}
