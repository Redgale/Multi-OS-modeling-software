// SPDX-License-Identifier: GPL-3.0-or-later
// Clearance holes for common screws: the sizes a hobbyist reaches for (M2 to M12, #2 to
// 1/2"), with close / normal / loose fits and suggested counterbore and countersink sizes.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "jig/result.hpp"

namespace jig
{

struct ScrewSize
{
    std::string name;       // "M3", "#6", "1/4\""
    bool inch = false;      // US size: values below are still in mm
    double nominal = 0;     // thread diameter
    double close = 0;       // clearance hole diameters
    double normal = 0;
    double loose = 0;       // 0 = no loose fit in the table
    double cboreDiameter = 0, cboreDepth = 0;  // socket head cap screw sits flush
    double csinkDiameter = 0;                  // flat head screw
    double csinkAngleDeg = 90;                 // 90° metric, 82° US
};

enum class HoleFit
{
    Close,
    Normal,
    Loose
};

const std::vector<ScrewSize>& screwSizes();
std::optional<ScrewSize> screwSize(std::string_view name);
std::optional<HoleFit> holeFitFromString(std::string_view s);
// The clearance diameter for a size and fit, or why there is none.
Result<double> clearanceDiameter(std::string_view size, HoleFit fit);

}  // namespace jig
