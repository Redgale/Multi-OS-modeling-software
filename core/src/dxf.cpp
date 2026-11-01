// SPDX-License-Identifier: GPL-3.0-or-later
// DXF import (brief M5): LINE, CIRCLE, ARC, LWPOLYLINE and POLYLINE (with bulge arcs)
// from the ENTITIES section of an ASCII DXF become editable sketch curves. Units come
// from the $INSUNITS header variable when it is set.
#include <array>
#include <cmath>
#include <locale>
#include <fstream>
#include <map>
#include <numbers>
#include <sstream>

#include "jig/kernel.hpp"

namespace jig
{
namespace
{
constexpr double kPi = std::numbers::pi;

struct Pair
{
    int code;
    std::string value;
};

std::string trim(std::string s)
{
    const auto b = s.find_first_not_of(" \t\r\n");
    const auto e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string {} : s.substr(b, e - b + 1);
}

double num(const std::string& s)
{
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    double v = 0;
    is >> v;
    return v;
}

// mm per drawing unit for $INSUNITS values
std::optional<double> unitScale(int insunits)
{
    switch (insunits) {
        case 1: return 25.4;    // inches
        case 2: return 304.8;   // feet
        case 4: return 1.0;     // millimetres
        case 5: return 10.0;    // centimetres
        case 6: return 1000.0;  // metres
        case 8: return 0.0000254;  // microinches
        case 9: return 0.0254;     // mils
        case 10: return 914.4;     // yards
        default: return std::nullopt;  // 0 = unitless, others are rare
    }
}
}  // namespace

Result<DxfDrawing> parseDxfImpl(const std::string& text);

Result<DxfDrawing> parseDxf(const std::string& text)
{
    try {
        return parseDxfImpl(text);
    }
    catch (const std::exception&) {  // a number too large or otherwise unreadable
        return makeError("dxf.format", "This DXF file is damaged and could not be read.");
    }
}

Result<DxfDrawing> parseDxfImpl(const std::string& text)
{
    std::istringstream in(text);
    std::vector<Pair> pairs;
    std::string codeLine, valueLine;
    while (std::getline(in, codeLine) && std::getline(in, valueLine)) {
        const std::string c = trim(codeLine);
        if (c.empty() || c.find_first_not_of("-0123456789") != std::string::npos) {
            return makeError("dxf.format", "This is not a text DXF file (or it is damaged).",
                             {"Save it from your drawing program as ASCII DXF (R12 or newer)."});
        }
        pairs.push_back({std::stoi(c), trim(valueLine)});
    }
    if (pairs.empty()) {
        return makeError("dxf.format", "This DXF file is empty.");
    }
    DxfDrawing d;
    // header: $INSUNITS
    for (size_t i = 0; i + 1 < pairs.size(); ++i) {
        if (pairs[i].code == 9 && pairs[i].value == "$INSUNITS" && pairs[i + 1].code == 70) {
            if (auto s = unitScale(std::stoi(pairs[i + 1].value))) {
                d.unitScale = *s;
                d.unitsKnown = true;
            }
        }
    }
    // entities section
    size_t i = 0;
    for (; i + 1 < pairs.size(); ++i) {
        if (pairs[i].code == 2 && pairs[i].value == "ENTITIES") {
            break;
        }
    }
    auto addBulgeSegment = [&](double x1, double y1, double x2, double y2, double bulge) {
        if (std::abs(bulge) < 1e-12) {
            d.lines.push_back({x1, y1, x2, y2});
            return;
        }
        // bulge = tan(theta / 4); positive: counter-clockwise from the first point
        const double theta = 4 * std::atan(bulge);
        const double chord = std::hypot(x2 - x1, y2 - y1);
        const double r = chord / (2 * std::sin(std::abs(theta) / 2));
        const double mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
        const double h = std::sqrt(std::max(0.0, r * r - chord * chord / 4));
        double nx = -(y2 - y1) / chord, ny = (x2 - x1) / chord;  // left of the chord
        const bool small = std::abs(theta) < kPi;
        const double side = ((bulge > 0) == small) ? 1 : -1;
        const double cx = mx + nx * h * side, cy = my + ny * h * side;
        double a1 = std::atan2(y1 - cy, x1 - cx), a2 = std::atan2(y2 - cy, x2 - cx);
        if (bulge < 0) {
            std::swap(a1, a2);  // arcs are stored counter-clockwise
        }
        d.arcs.push_back({cx, cy, r, a1 * 180 / kPi, a2 * 180 / kPi});
    };
    std::string type;
    std::map<int, std::vector<std::string>> g;  // group code -> values, for the current entity
    std::vector<std::array<double, 3>> polyVerts;  // old-style POLYLINE vertices (x, y, bulge)
    bool polyClosed = false, inPolyline = false;
    auto flush = [&]() {
        auto f = [&](int code, double def = 0) { return g.count(code) ? num(g[code][0]) : def; };
        if (type == "LINE") {
            d.lines.push_back({f(10), f(20), f(11), f(21)});
        }
        else if (type == "CIRCLE") {
            d.circles.push_back({f(10), f(20), f(40)});
        }
        else if (type == "ARC") {
            d.arcs.push_back({f(10), f(20), f(40), f(50), f(51)});
        }
        else if (type == "LWPOLYLINE") {
            const auto& xs = g[10];
            const auto& ys = g[20];
            const size_t n = std::min(xs.size(), ys.size());
            // bulges (42) follow their vertex; collect them in order from the raw pairs
            std::vector<double> bulge(n, 0.0);
            for (size_t k = 0; k < g[42].size() && k < n; ++k) {
                bulge[k] = num(g[42][k]);
            }
            if (g.count(-42)) {  // exact vertex indices for bulges, when recorded
                bulge.assign(n, 0.0);
                for (auto& s : g[-42]) {
                    const auto colon = s.find(':');
                    const size_t k = std::stoul(s.substr(0, colon));
                    if (k < n) {
                        bulge[k] = num(s.substr(colon + 1));
                    }
                }
            }
            const bool closed = (int(f(70)) & 1) != 0;
            for (size_t k = 0; k + 1 < n + (closed ? 1 : 0); ++k) {
                const size_t j = (k + 1) % n;
                addBulgeSegment(num(xs[k]), num(ys[k]), num(xs[j]), num(ys[j]), bulge[k]);
            }
        }
        else if (type == "POLYLINE") {
            inPolyline = true;
            polyVerts.clear();
            polyClosed = (int(f(70)) & 1) != 0;
        }
        else if (type == "VERTEX" && inPolyline) {
            polyVerts.push_back({f(10), f(20), f(42)});
        }
        else if (type == "SEQEND" && inPolyline) {
            for (size_t k = 0; k + 1 < polyVerts.size() + (polyClosed ? 1 : 0); ++k) {
                const auto& a = polyVerts[k];
                const auto& b = polyVerts[(k + 1) % polyVerts.size()];
                addBulgeSegment(a[0], a[1], b[0], b[1], a[2]);
            }
            inPolyline = false;
        }
        else if (!type.empty() && type != "VERTEX" && type != "SEQEND") {
            ++d.skipped[type];
        }
        g.clear();
    };
    int lwVertex = -1;
    for (; i < pairs.size(); ++i) {
        const Pair& p = pairs[i];
        if (p.code == 0) {
            flush();
            type = p.value;
            lwVertex = -1;
            if (type == "ENDSEC") {
                break;
            }
            continue;
        }
        if (type == "LWPOLYLINE" && p.code == 10) {
            ++lwVertex;
        }
        if (type == "LWPOLYLINE" && p.code == 42) {
            g[-42].push_back(std::to_string(std::max(0, lwVertex)) + ":" + p.value);
        }
        g[p.code].push_back(p.value);
    }
    flush();
    if (d.lines.empty() && d.circles.empty() && d.arcs.empty()) {
        return makeError("dxf.empty", "This DXF has no lines, circles, arcs or polylines to bring in.",
                         {"Text, hatches, splines and blocks are not imported yet: explode them to lines first."});
    }
    return d;
}

Result<DxfDrawing> readDxf(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return makeError("dxf.open", "This file could not be opened.");
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return parseDxf(ss.str());
}

int addDxfToSketch(Sketch& s, const DxfDrawing& d, double scale, double dx, double dy)
{
    // share points between curve ends that meet, so outlines close into areas
    std::map<std::pair<long long, long long>, int> points;
    auto pt = [&](double x, double y) {
        x = x * scale + dx;
        y = y * scale + dy;
        const std::pair<long long, long long> k {std::llround(x * 1e5), std::llround(y * 1e5)};
        auto it = points.find(k);
        if (it != points.end()) {
            return it->second;
        }
        return points[k] = s.addPoint(x, y, true);
    };
    int n = 0;
    for (auto& l : d.lines) {
        const int a = pt(l.x1, l.y1), b = pt(l.x2, l.y2);
        if (a != b) {
            s.addLine(a, b);
            ++n;
        }
    }
    for (auto& c : d.circles) {
        s.addCircle(c.cx * scale + dx, c.cy * scale + dy, c.r * scale);
        ++n;
    }
    for (auto& a : d.arcs) {
        const double a0 = a.startDeg * kPi / 180;
        double a1 = a.endDeg * kPi / 180;
        while (a1 <= a0) {
            a1 += 2 * kPi;
        }
        const int c = s.addPoint(a.cx * scale + dx, a.cy * scale + dy, true);
        const int p1 = pt(a.cx + a.r * std::cos(a0), a.cy + a.r * std::sin(a0));
        const int p2 = pt(a.cx + a.r * std::cos(a1), a.cy + a.r * std::sin(a1));
        s.addArcOn(c, p1, p2, a.r * scale, a0, a1);
        ++n;
    }
    return n;
}

}  // namespace jig
