// SPDX-License-Identifier: GPL-3.0-or-later
// Units and expressions (brief 5.4e). A small hand-written recursive-descent
// parser; see docs/DECISIONS.md D-008. Values are stored in base units:
// millimetres for length and radians for angle.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "jig/result.hpp"

namespace jig
{

// Physical dimension as integer exponents of length and angle.
// length=1 -> mm, length=2 -> mm^2, angle=1 -> rad, both 0 -> plain number.
struct Dim
{
    int length = 0;
    int angle = 0;
    bool operator==(const Dim&) const = default;

    static Dim none()
    {
        return {};
    }
    static Dim len()
    {
        return {1, 0};
    }
    static Dim ang()
    {
        return {0, 1};
    }
};

struct Quantity
{
    double value = 0.0;  // in base units (mm, rad)
    Dim dim;
};

// Looks up a named parameter; returns nullopt when it does not exist.
using ParamLookup = std::function<std::optional<Quantity>(std::string_view name)>;

// Evaluates an expression such as "2 in", "width/2 + 3 mm", "75 deg" or
// "sqrt(a^2 + b^2)". A bare number takes `defaultUnit` (so typing "10" into a
// length field means 10 mm) only when the whole expression is unitless.
Result<Quantity> evaluate(std::string_view text,
                          const ParamLookup& lookup = {},
                          Dim defaultUnit = Dim::none());

// Collects identifiers (parameter names) referenced by an expression.
std::vector<std::string> referencedNames(std::string_view text);

// Formats a value for display, e.g. 12.5 mm, 75 deg.
std::string formatQuantity(const Quantity& q);

// ---------------------------------------------------------------- display units

// The unit a design is shown in. Geometry is always stored in millimetres; the unit
// only changes what bare numbers typed into size fields mean and how sizes are shown.
enum class LengthUnit
{
    Millimetre,
    Centimetre,
    Metre,
    Inch,
    Foot
};
std::string unitSymbol(LengthUnit u);                         // "mm", "in", …
std::optional<LengthUnit> lengthUnitFromString(std::string_view s);  // "mm", "in", "inch", …
double millimetresPer(LengthUnit u);
// "12.7 mm", "0.5 in": up to 3 decimals (4 for in, ft and m), trailing zeros removed.
std::string formatLength(double mm, LengthUnit u, bool withSymbol = true);
std::string formatArea(double mm2, LengthUnit u);    // mm² / cm² / m² / in² / ft²
std::string formatVolume(double mm3, LengthUnit u);  // cm³ for metric designs, in³ / ft³ otherwise
// What a user typed into a length field, made explicit: a unitless expression gets the
// design's unit ("2" -> "2 in", "a*2" stays if a is a length, "3/4" -> "(3/4) in").
// Expressions that already have units, or are invalid, are returned unchanged.
std::string withLengthUnit(std::string_view expr, LengthUnit u, const ParamLookup& lookup = {});

// True for names a user may give a parameter (letter or _ first, then
// letters, digits or _), excluding unit and function names.
bool isValidParameterName(std::string_view name);

}  // namespace jig
