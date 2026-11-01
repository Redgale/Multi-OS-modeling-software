// SPDX-License-Identifier: GPL-3.0-or-later
// Command registry: findable by intent (Easy Contract principle 9).
#include <gtest/gtest.h>
#include <cctype>
#include <set>

#include "jig/commands.hpp"

using namespace jig;

namespace
{
std::string top(std::string_view q)
{
    auto m = CommandRegistry::instance().search(q);
    return m.empty() ? std::string {} : m[0].info->standardName;
}
}  // namespace

TEST(Commands, FindableByPlainLanguage)
{
    EXPECT_EQ(top("round the edges"), "Fillet");
    EXPECT_EQ(top("hollow"), "Shell");
    EXPECT_EQ(top("bevel"), "Chamfer");
    EXPECT_EQ(top("extrude"), "Extrude");
    EXPECT_EQ(top("drill"), "Hole");
    EXPECT_EQ(top("lathe"), "Revolve");
    EXPECT_EQ(top("3d print"), "Export STL");
    EXPECT_EQ(top("oops"), "Undo");
}

TEST(Commands, EveryCommandIsDescribed)
{
    const auto& all = CommandRegistry::instance().all();
    EXPECT_GT(all.size(), 60u);
    int essential = 0;
    for (auto& c : all) {
        EXPECT_FALSE(c.label.empty()) << c.id;
        EXPECT_FALSE(c.standardName.empty()) << c.id;
        EXPECT_FALSE(c.tooltip.empty()) << c.id;
        EXPECT_LT(c.tooltip.size(), 120u) << c.id << ": tooltips are one line";
        // tooltips audit (M7): a whole sentence, in our own words
        EXPECT_TRUE(!c.tooltip.empty() && std::isupper(static_cast<unsigned char>(c.tooltip.front())))
            << c.id << ": starts with a capital";
        EXPECT_TRUE(!c.tooltip.empty() && (c.tooltip.back() == '.' || c.tooltip.back() == '?')) << c.id << ": ends a sentence";
        for (const char* brand : {"Fusion", "Autodesk", "SolidWorks", "Onshape"}) {
            EXPECT_EQ(c.tooltip.find(brand), std::string::npos) << c.id << ": no other product's name";
            EXPECT_EQ(c.label.find(brand), std::string::npos) << c.id;
        }
        if (!c.available) {
            EXPECT_FALSE(c.comingIn.empty() && !c.scripted && c.panel != "FILE" && c.panel != "EDIT")
                << c.id << " must say when it is coming";
        }
        essential += c.essential;
    }
    std::set<std::string> tips;
    for (auto& c : all) {
        EXPECT_TRUE(tips.insert(c.tooltip).second || c.id.rfind("constraint.", 0) == 0) << c.id << ": tooltip repeats another";
    }
    // Progressive disclosure: about 25 essential tools (brief principle 8), not counting file/edit/view.
    EXPECT_GE(essential, 20);
    EXPECT_LE(essential, 40);
}

TEST(Commands, DefaultKeysMatchTheBrief)
{
    auto key = [](const char* id) { return CommandRegistry::instance().find(id)->shortcut; };
    EXPECT_EQ(key("sketch.line"), "L");
    EXPECT_EQ(key("sketch.rectangle"), "R");
    EXPECT_EQ(key("sketch.circle"), "C");
    EXPECT_EQ(key("sketch.dimension"), "D");
    EXPECT_EQ(key("sketch.trim"), "T");
    EXPECT_EQ(key("sketch.offset"), "O");
    EXPECT_EQ(key("sketch.project"), "P");
    EXPECT_EQ(key("sketch.construction"), "X");
    EXPECT_EQ(key("solid.extrude"), "E");
    EXPECT_EQ(key("solid.presspull"), "Q");
    EXPECT_EQ(key("solid.fillet"), "F");
    EXPECT_EQ(key("solid.hole"), "H");
    EXPECT_EQ(key("inspect.measure"), "I");
    EXPECT_EQ(key("edit.undo"), "Ctrl+Z");
    EXPECT_EQ(key("edit.redo"), "Ctrl+Shift+Z");
    EXPECT_EQ(key("file.save"), "Ctrl+S");
    EXPECT_EQ(key("edit.palette"), "Ctrl+K");
}

TEST(Commands, ScriptErrorsNameTheLine)
{
    Document doc;
    ScriptRunner run(doc);
    auto r = run.runText("param a = 5 mm\n\nfrobnicate x=1\n");
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.error().message.find("Line 3"), std::string::npos);
    // a word where a number belongs is that line's error, not a crash (this used to abort)
    Document d2;
    ScriptRunner run2(d2);
    auto bad = run2.runText("sketch id=s1 plane=XY\nrect sketch=s1 center=0,0 w=10 h=10\n"
                            "extrude id=e1 sketch=s1 distance=10\nreorder e1 to=abc\n");
    ASSERT_FALSE(bad.ok());
    EXPECT_NE(bad.error().message.find("Line 4"), std::string::npos);
    for (const char* line : {"undo many", "redo x", "rollback y", "constrain sketch=s1 type=fix a=q"}) {
        EXPECT_NO_THROW((void)run2.runLine(line)) << line;
    }
}
