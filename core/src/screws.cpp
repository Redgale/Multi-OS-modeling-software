// SPDX-License-Identifier: GPL-3.0-or-later
#include "jig/screws.hpp"

namespace jig
{

const std::vector<ScrewSize>& screwSizes()
{
    // Metric clearance holes: ISO 273 fine / medium / coarse series. Counterbores fit
    // ISO 4762 socket head cap screws (head diameter + 1 mm, head height + 0.5 mm);
    // countersinks fit ISO 10642 flat heads (largest head diameter + 0.4 mm, 90°; M2 and
    // M2.5 from ISO 7046 heads). US clearance holes: the close and free fits of the common
    // drill chart after ASME B18.2.8 (#32/#30 drills for #4 and so on; sources differ for
    // #8, #18/#16 used here); there is no loose fit for them. US counterbores fit socket
    // head cap screws (the usual counterbore chart); countersinks fit 82° socket flat heads
    // (head diameter + 0.01 in).
    constexpr double in = 25.4;
    static const std::vector<ScrewSize> sizes {
        {"M2", false, 2, 2.2, 2.4, 2.6, 4.8, 2.5, 4.2},
        {"M2.5", false, 2.5, 2.7, 2.9, 3.1, 5.5, 3.0, 5.1},
        {"M3", false, 3, 3.2, 3.4, 3.6, 6.5, 3.5, 7.1},
        {"M4", false, 4, 4.3, 4.5, 4.8, 8.0, 4.5, 9.4},
        {"M5", false, 5, 5.3, 5.5, 5.8, 9.5, 5.5, 11.6},
        {"M6", false, 6, 6.4, 6.6, 7.0, 11.0, 6.5, 13.8},
        {"M8", false, 8, 8.4, 9.0, 10.0, 14.0, 8.5, 18.3},
        {"M10", false, 10, 10.5, 11.0, 12.0, 17.0, 10.5, 22.8},
        {"M12", false, 12, 13.0, 13.5, 14.5, 19.0, 12.5, 27.3},
        {"#4", true, 0.112 * in, 0.1160 * in, 0.1285 * in, 0, 0.2188 * in, 0.122 * in, 0.265 * in, 82},
        {"#6", true, 0.138 * in, 0.1440 * in, 0.1495 * in, 0, 0.2812 * in, 0.148 * in, 0.317 * in, 82},
        {"#8", true, 0.164 * in, 0.1695 * in, 0.1770 * in, 0, 0.3125 * in, 0.174 * in, 0.369 * in, 82},
        {"#10", true, 0.190 * in, 0.1960 * in, 0.2010 * in, 0, 0.3750 * in, 0.200 * in, 0.421 * in, 82},
        {"1/4\"", true, 0.250 * in, 0.2570 * in, 0.2660 * in, 0, 0.4375 * in, 0.260 * in, 0.541 * in, 82},
        {"5/16\"", true, 0.3125 * in, 0.3230 * in, 0.3320 * in, 0, 0.5312 * in, 0.322 * in, 0.666 * in, 82},
        {"3/8\"", true, 0.375 * in, 0.3860 * in, 0.3970 * in, 0, 0.6250 * in, 0.385 * in, 0.791 * in, 82},
        {"1/2\"", true, 0.500 * in, 0.5156 * in, 0.5312 * in, 0, 0.8125 * in, 0.510 * in, 1.041 * in, 82},
    };
    return sizes;
}

std::optional<ScrewSize> screwSize(std::string_view name)
{
    std::string n(name);
    if (!n.empty() && (n[0] == 'm')) {
        n[0] = 'M';
    }
    for (auto& s : screwSizes()) {
        if (s.name == n || (s.inch && s.name.back() == '"' && s.name.substr(0, s.name.size() - 1) == n)) {
            return s;
        }
    }
    return std::nullopt;
}

std::optional<HoleFit> holeFitFromString(std::string_view s)
{
    if (s == "close") {
        return HoleFit::Close;
    }
    if (s == "normal" || s == "free" || s.empty()) {
        return HoleFit::Normal;
    }
    if (s == "loose") {
        return HoleFit::Loose;
    }
    return std::nullopt;
}

Result<double> clearanceDiameter(std::string_view size, HoleFit fit)
{
    auto s = screwSize(size);
    if (!s) {
        Error e;
        e.code = "hole.size";
        e.message = "There is no screw size called '" + std::string(size) + "' in the clearance table.";
        e.suggestions = {"Use M2 to M12, or #4, #6, #8, #10, 1/4\", 5/16\", 3/8\", 1/2\".", "Or type the diameter."};
        return e;
    }
    const double d = fit == HoleFit::Close ? s->close : fit == HoleFit::Normal ? s->normal : s->loose;
    if (!(d > 0)) {
        Error e;
        e.code = "hole.fit";
        e.message = "The table has close and free fits for US screws, not a loose one.";
        e.suggestions = {"Pick the normal (free) fit, or type the diameter."};
        return e;
    }
    return d;
}

}  // namespace jig
