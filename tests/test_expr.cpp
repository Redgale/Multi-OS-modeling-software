// SPDX-License-Identifier: GPL-3.0-or-later
#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include "jig/expr.hpp"

using namespace jig;

namespace
{
double mm(std::string_view s, const ParamLookup& l = {})
{
    auto r = evaluate(s, l, Dim::len());
    EXPECT_TRUE(r.ok()) << s << ": " << (r.ok() ? "" : r.error().message);
    return r.ok() ? r->value : NAN;
}
}  // namespace

TEST(Expr, BareNumbersInLengthFieldsAreMillimetres)
{
    EXPECT_DOUBLE_EQ(mm("10"), 10);
    EXPECT_DOUBLE_EQ(mm("2.5"), 2.5);
    EXPECT_DOUBLE_EQ(mm("1e2"), 100);
}

TEST(Expr, Units)
{
    EXPECT_DOUBLE_EQ(mm("2 in"), 50.8);
    EXPECT_DOUBLE_EQ(mm("5 cm"), 50);
    EXPECT_DOUBLE_EQ(mm("5cm"), 50);
    EXPECT_DOUBLE_EQ(mm("1 ft"), 304.8);
    EXPECT_DOUBLE_EQ(mm("0.1 m"), 100);
    EXPECT_DOUBLE_EQ(mm("1 in + 1 mm"), 26.4);
    EXPECT_DOUBLE_EQ(mm("(1 + 1) in"), 50.8);
    EXPECT_DOUBLE_EQ(mm("10 + 2 mm"), 12);  // bare number adopts the other side's unit
}

TEST(Expr, Angles)
{
    auto r = evaluate("90 deg", {}, Dim::ang());
    ASSERT_TRUE(r.ok());
    EXPECT_NEAR(r->value, std::numbers::pi / 2, 1e-12);
    auto bare = evaluate("45", {}, Dim::ang());
    ASSERT_TRUE(bare.ok());
    EXPECT_NEAR(bare->value, std::numbers::pi / 4, 1e-12);  // bare numbers in angle fields are degrees
    auto sym = evaluate("30\xC2\xB0", {}, Dim::ang());
    ASSERT_TRUE(sym.ok());
    EXPECT_NEAR(sym->value, std::numbers::pi / 6, 1e-12);
    EXPECT_NEAR(mm("10 mm * cos(60 deg)"), 5, 1e-12);
    EXPECT_NEAR(mm("70 / tan(45)"), 70, 1e-9);
}

TEST(Expr, ArithmeticAndFunctions)
{
    EXPECT_DOUBLE_EQ(mm("2 * 3 + 4"), 10);
    EXPECT_DOUBLE_EQ(mm("2 * (3 + 4)"), 14);
    EXPECT_DOUBLE_EQ(mm("-3 + 5"), 2);
    EXPECT_DOUBLE_EQ(mm("sqrt(3 mm * 3 mm + 4 mm * 4 mm)"), 5);
    EXPECT_DOUBLE_EQ(mm("sqrt((3 mm)^2 + (4 mm)^2)"), 5);
    EXPECT_DOUBLE_EQ(mm("max(2 mm, 7 mm, 3 mm)"), 7);
    EXPECT_DOUBLE_EQ(mm("abs(-4 mm)"), 4);
}

TEST(Expr, Parameters)
{
    ParamLookup l = [](std::string_view n) -> std::optional<Quantity> {
        if (n == "width") {
            return Quantity {60, Dim::len()};
        }
        if (n == "count") {
            return Quantity {4, Dim::none()};
        }
        return std::nullopt;
    };
    EXPECT_DOUBLE_EQ(mm("width/2", l), 30);
    EXPECT_DOUBLE_EQ(mm("width / count", l), 15);
    EXPECT_DOUBLE_EQ(mm("width/10 + 1 mm", l), 7);
    auto names = referencedNames("width/2 + wall * 3 + 2 in");
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "width");
    EXPECT_EQ(names[1], "wall");
}

TEST(Expr, ErrorsAreExplainedNotThrown)
{
    auto bad = [](std::string_view s, Dim d = Dim::len()) {
        auto r = evaluate(s, {}, d);
        EXPECT_FALSE(r.ok()) << s;
        if (!r.ok()) {
            EXPECT_FALSE(r.error().message.empty());
        }
        return r.ok() ? std::string {} : r.error().code;
    };
    EXPECT_EQ(bad("2 mm + 30 deg"), "expr.units");
    EXPECT_EQ(bad("nope + 1"), "expr.unknown_name");
    EXPECT_EQ(bad("1 / 0"), "expr.div0");
    EXPECT_EQ(bad("(1 + 2"), "expr.syntax");
    EXPECT_EQ(bad("3 *"), "expr.syntax");
    EXPECT_EQ(bad("4 mm * 2 mm"), "expr.units");  // an area in a length field
    EXPECT_EQ(bad("90 deg"), "expr.units");
    EXPECT_EQ(bad(""), "expr.syntax");
    EXPECT_EQ(bad("sqrt(-4)"), "expr.domain");
}

TEST(Expr, ParameterNames)
{
    EXPECT_TRUE(isValidParameterName("wall_t"));
    EXPECT_TRUE(isValidParameterName("_x2"));
    EXPECT_FALSE(isValidParameterName("2x"));
    EXPECT_FALSE(isValidParameterName("mm"));
    EXPECT_FALSE(isValidParameterName("sin"));
    EXPECT_FALSE(isValidParameterName("a-b"));
    EXPECT_FALSE(isValidParameterName(""));
}

TEST(Expr, FuzzNeverCrashes)
{
    // Property: any byte soup either evaluates to a finite value or returns an error.
    const std::string alphabet = "0123456789.+-*/^() ,abcdemintgsqrx_\xC2\xB0";
    uint32_t seed = 12345;
    auto rnd = [&]() { return seed = seed * 1664525u + 1013904223u; };
    for (int i = 0; i < 20000; ++i) {
        std::string s;
        int n = int(rnd() % 24);
        for (int k = 0; k < n; ++k) {
            s += alphabet[rnd() % alphabet.size()];
        }
        auto r = evaluate(s, {}, (i % 2) ? Dim::len() : Dim::none());
        if (r.ok()) {
            EXPECT_TRUE(std::isfinite(r->value)) << s;
        }
    }
}

TEST(Units, FormattingAndTypedInput)
{
    EXPECT_EQ(formatLength(25.4, LengthUnit::Inch), "1 in");
    EXPECT_EQ(formatLength(12.7, LengthUnit::Inch), "0.5 in");
    EXPECT_EQ(formatLength(3.175, LengthUnit::Inch), "0.125 in");
    EXPECT_EQ(formatLength(10, LengthUnit::Millimetre), "10 mm");
    EXPECT_EQ(formatLength(304.8, LengthUnit::Foot), "1 ft");
    EXPECT_EQ(formatArea(645.16, LengthUnit::Inch), "1 in²");
    EXPECT_EQ(formatVolume(25.4 * 25.4 * 25.4, LengthUnit::Inch), "1 in³");
    EXPECT_EQ(formatVolume(48000, LengthUnit::Millimetre), "48 cm³");
    EXPECT_EQ(withLengthUnit("2", LengthUnit::Inch), "2 in");
    EXPECT_EQ(withLengthUnit("3/4", LengthUnit::Inch), "(3/4) in");
    EXPECT_EQ(withLengthUnit("5 mm", LengthUnit::Inch), "5 mm");  // explicit units stay
    EXPECT_EQ(withLengthUnit("", LengthUnit::Inch), "");
    EXPECT_NEAR(evaluate("2 yd")->value, 1828.8, 1e-9);
    EXPECT_NEAR(evaluate("10 thou")->value, 0.254, 1e-12);
    EXPECT_NEAR(evaluate("1 ft + 3 in")->value, 381, 1e-9);
    EXPECT_EQ(*lengthUnitFromString("inch"), LengthUnit::Inch);
    EXPECT_FALSE(lengthUnitFromString("furlong").has_value());
}
