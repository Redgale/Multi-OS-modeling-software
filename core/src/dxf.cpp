// SPDX-License-Identifier: GPL-3.0-or-later
// DXF import (brief M5): LINE, CIRCLE, ARC, LWPOLYLINE and POLYLINE (with bulge arcs),
// SPLINE, ELLIPSE, TEXT and MTEXT from the ENTITIES section of an ASCII DXF, and the blocks
// INSERTs place (moved, turned, scaled, mirrored, nested), become editable sketch curves. Units come
// from the $INSUNITS header variable when it is set.
#include <array>
#include <cmath>
#include <locale>
#include <fstream>
#include <functional>
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
    // blocks: name -> the pairs of the entities inside it (an INSERT draws them, moved)
    std::map<std::string, std::pair<size_t, size_t>> blocks;
    {
        size_t b = 0;
        for (; b + 1 < pairs.size(); ++b) {
            if (pairs[b].code == 2 && pairs[b].value == "BLOCKS") {
                break;
            }
        }
        std::string name;
        size_t first = 0;
        bool header = false;
        for (; b < pairs.size(); ++b) {
            const Pair& p = pairs[b];
            if (p.code == 0 && p.value == "ENDSEC") {
                break;
            }
            if (p.code == 0 && p.value == "BLOCK") {
                name.clear();
                header = true;
                continue;
            }
            if (header && p.code == 2) {
                name = p.value;
            }
            if (header && p.code == 0) {  // the first entity of the block
                header = false;
                first = b;
            }
            if (p.code == 0 && p.value == "ENDBLK") {
                if (!name.empty() && first && first < b) {
                    blocks[name] = {first, b};
                }
                header = false;
                first = 0;
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
    // where a block's drawing goes: turned, scaled (the same both ways, maybe mirrored), moved
    struct Xf
    {
        double s = 1, rot = 0, tx = 0, ty = 0;
        bool mirror = false;
        std::pair<double, double> at(double x, double y) const
        {
            if (mirror) {
                x = -x;
            }
            const double c = std::cos(rot), sn = std::sin(rot);
            return {tx + s * (x * c - y * sn), ty + s * (x * sn + y * c)};
        }
        double angle(double deg) const  // a direction in degrees
        {
            return (mirror ? 180 - deg : deg) + rot * 180 / kPi;
        }
    };
    std::function<void(size_t, size_t, const Xf&, int)> parse;
    parse = [&](size_t from, size_t to, const Xf& xf, int depth) {
        auto addBulgeSegment = [&](double x1, double y1, double x2, double y2, double bulge) {
            auto [ax, ay] = xf.at(x1, y1);
            auto [bx, by] = xf.at(x2, y2);
            if (xf.mirror) {
                bulge = -bulge;
            }
            if (std::abs(bulge) < 1e-12) {
                d.lines.push_back({ax, ay, bx, by});
                return;
            }
            // bulge = tan(theta / 4); positive: counter-clockwise from the first point
            const double theta = 4 * std::atan(bulge);
            const double chord = std::hypot(bx - ax, by - ay);
            const double r = chord / (2 * std::sin(std::abs(theta) / 2));
            const double mx = (ax + bx) / 2, my = (ay + by) / 2;
            const double h = std::sqrt(std::max(0.0, r * r - chord * chord / 4));
            double nx = -(by - ay) / chord, ny = (bx - ax) / chord;  // left of the chord
            const bool small = std::abs(theta) < kPi;
            const double side = ((bulge > 0) == small) ? 1 : -1;
            const double cx = mx + nx * h * side, cy = my + ny * h * side;
            double a1 = std::atan2(ay - cy, ax - cx), a2 = std::atan2(by - cy, bx - cx);
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
            auto P = [&](double x, double y) { return xf.at(x, y); };
            if (type == "LINE") {
                auto [x1, y1] = P(f(10), f(20));
                auto [x2, y2] = P(f(11), f(21));
                d.lines.push_back({x1, y1, x2, y2});
            }
            else if (type == "CIRCLE") {
                auto [cx, cy] = P(f(10), f(20));
                d.circles.push_back({cx, cy, f(40) * xf.s});
            }
            else if (type == "ARC") {
                auto [cx, cy] = P(f(10), f(20));
                double a0 = xf.angle(f(50)), a1 = xf.angle(f(51));
                if (xf.mirror) {
                    std::swap(a0, a1);  // still counter-clockwise
                }
                d.arcs.push_back({cx, cy, f(40) * xf.s, a0, a1});
            }
            else if (type == "LWPOLYLINE") {
                const auto& xs = g[10];
                const auto& ys = g[20];
                const size_t n = std::min(xs.size(), ys.size());
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
                for (size_t k = 0; n > 0 && k + 1 < n + (closed ? 1 : 0); ++k) {
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
                for (size_t k = 0; !polyVerts.empty() && k + 1 < polyVerts.size() + (polyClosed ? 1 : 0); ++k) {
                    const auto& a = polyVerts[k];
                    const auto& b = polyVerts[(k + 1) % polyVerts.size()];
                    addBulgeSegment(a[0], a[1], b[0], b[1], a[2]);
                }
                inPolyline = false;
            }
            else if (type == "SPLINE") {
                // fit points when the file has them; otherwise points along the B-spline from
                // its control points and knots
                DxfDrawing::Spline sp;
                sp.closed = (int(f(70)) & 1) != 0;
                const auto& fx = g[11];
                const auto& fy = g[21];
                if (fx.size() >= 2 && fx.size() == fy.size()) {
                    for (size_t k = 0; k < fx.size(); ++k) {
                        sp.points.push_back(P(num(fx[k]), num(fy[k])));
                    }
                }
                else {
                    const auto& cx = g[10];
                    const auto& cy = g[20];
                    const int deg = std::max(1, int(f(71, 3)));
                    std::vector<double> knots;
                    for (auto& k : g[40]) {
                        knots.push_back(num(k));
                    }
                    const size_t n = std::min(cx.size(), cy.size());
                    if (n >= 2 && knots.size() == n + size_t(deg) + 1) {
                        std::vector<std::pair<double, double>> ctrl;
                        for (size_t k = 0; k < n; ++k) {
                            ctrl.push_back({num(cx[k]), num(cy[k])});
                        }
                        // de Boor at evenly spread parameters
                        const double u0 = knots[size_t(deg)], u1 = knots[n];
                        const int samples = int(std::min<size_t>(200, n * 4));
                        for (int si = 0; si <= samples; ++si) {
                            const double u = std::min(u1 - 1e-12, u0 + (u1 - u0) * si / samples);
                            size_t span = size_t(deg);
                            while (span + 1 < n && knots[span + 1] <= u) {
                                ++span;
                            }
                            std::vector<std::pair<double, double>> dd(ctrl.begin() + long(span) - deg, ctrl.begin() + long(span) + 1);
                            for (int r = 1; r <= deg; ++r) {
                                for (int j = deg; j >= r; --j) {
                                    const size_t ki = span - size_t(deg) + size_t(j);
                                    const double den = knots[ki + size_t(deg) - size_t(r) + 1] - knots[ki];
                                    const double al = den > 0 ? (u - knots[ki]) / den : 0;
                                    dd[size_t(j)].first = (1 - al) * dd[size_t(j) - 1].first + al * dd[size_t(j)].first;
                                    dd[size_t(j)].second = (1 - al) * dd[size_t(j) - 1].second + al * dd[size_t(j)].second;
                                }
                            }
                            sp.points.push_back(P(dd[size_t(deg)].first, dd[size_t(deg)].second));
                        }
                        if (sp.closed && sp.points.size() > 2 &&
                            std::hypot(sp.points.front().first - sp.points.back().first,
                                       sp.points.front().second - sp.points.back().second) < 1e-9) {
                            sp.points.pop_back();  // a closed curve does not repeat its start
                        }
                    }
                }
                if (sp.points.size() >= 2) {
                    d.splines.push_back(std::move(sp));
                }
                else {
                    ++d.skipped[type];
                }
            }
            else if (type == "ELLIPSE") {
                auto [cx, cy] = P(f(10), f(20));
                auto [ex, ey] = P(f(10) + f(11), f(20) + f(21));
                const double ratio = f(40, 1), t0 = f(41, 0), t1 = f(42, 2 * kPi);
                if (std::abs(std::fmod(t1 - t0, 2 * kPi)) < 1e-9 || std::abs(t1 - t0 - 2 * kPi) < 1e-9) {
                    d.ellipses.push_back({cx, cy, ex - cx, ey - cy, ratio});
                }
                else {
                    // part of an ellipse: points along it, as a smooth curve
                    DxfDrawing::Spline sp;
                    const double mx = f(11), my = f(21);
                    const double a = std::hypot(mx, my), ang = std::atan2(my, mx);
                    double end = t1;
                    while (end <= t0) {
                        end += 2 * kPi;
                    }
                    const int m = std::max(4, int(24 * (end - t0) / (2 * kPi)));
                    for (int k = 0; k <= m; ++k) {
                        const double t = t0 + (end - t0) * k / m;
                        const double lx = a * std::cos(t), ly = a * ratio * std::sin(t);
                        sp.points.push_back(P(f(10) + lx * std::cos(ang) - ly * std::sin(ang),
                                              f(20) + lx * std::sin(ang) + ly * std::cos(ang)));
                    }
                    d.splines.push_back(std::move(sp));
                }
            }
            else if ((type == "TEXT" || type == "MTEXT") && g.count(1)) {
                std::string t;
                for (auto& part : g[3]) {  // MTEXT: long text comes in pieces, the last in code 1
                    t += part;
                }
                t += g[1][0];
                if (type == "MTEXT") {
                    // formatting codes out: \\P is a new line; {\\fArial;...} and \\H2; go
                    std::string out;
                    for (size_t k = 0; k < t.size(); ++k) {
                        if (t[k] == '\\' && k + 1 < t.size()) {
                            const char c = t[k + 1];
                            if (c == 'P') {
                                out += "\\n";
                                ++k;
                                continue;
                            }
                            if (c == '\\' || c == '{' || c == '}') {
                                out += c;
                                ++k;
                                continue;
                            }
                            const auto semi = t.find(';', k);
                            k = semi == std::string::npos ? k + 1 : semi;
                            continue;
                        }
                        if (t[k] != '{' && t[k] != '}') {
                            out += t[k];
                        }
                    }
                    t = out;
                }
                const double h = f(40, 2.5) * xf.s;
                const double angDeg = xf.angle(type == "MTEXT" ? f(50) : f(50));
                // the file places text by its start (or a chosen point); we place it by its middle
                const bool aligned = type == "TEXT" && (g.count(72) || g.count(73)) && g.count(11);
                double x = aligned ? f(11) : f(10), y = aligned ? f(21) : f(20);
                auto [px, py] = P(x, y);
                if (!aligned) {
                    const double guessW = 0.6 * h * double(t.size());  // about 0.6 em per letter
                    const double a = angDeg * kPi / 180;
                    const double ux = type == "MTEXT" ? 0.5 * guessW : 0.5 * guessW;
                    const double uy = type == "MTEXT" ? -0.5 * h : 0.35 * h;
                    px += ux * std::cos(a) - uy * std::sin(a);
                    py += ux * std::sin(a) + uy * std::cos(a);
                }
                d.texts.push_back({px, py, h, angDeg, t});
            }
            else if (type == "INSERT") {
                const std::string name = g.count(2) ? g[2][0] : std::string {};
                auto it = blocks.find(name);
                const double sx = f(41, 1), sy = f(42, 1);
                if (it == blocks.end() || depth > 8) {
                    ++d.skipped[type];
                }
                else if (std::abs(std::abs(sx) - std::abs(sy)) > 1e-9 * std::max(1.0, std::abs(sx))) {
                    ++d.skipped["INSERT (stretched)"];
                }
                else {
                    // the block's drawing: its own transform, then this one
                    Xf inner;
                    inner.mirror = (sx < 0) != (sy < 0);
                    inner.s = std::abs(sx);
                    inner.rot = f(50) * kPi / 180 + (sy < 0 ? kPi : 0);  // (x, -y) is a mirror and a half turn
                    inner.tx = f(10);
                    inner.ty = f(20);
                    Xf both;  // xf applied after inner
                    both.mirror = xf.mirror != inner.mirror;
                    both.s = xf.s * inner.s;
                    both.rot = xf.rot + (xf.mirror ? -inner.rot : inner.rot);
                    auto [tx, ty] = xf.at(inner.tx, inner.ty);
                    both.tx = tx;
                    both.ty = ty;
                    if (xf.mirror) {
                        both.rot = xf.rot - inner.rot;
                    }
                    parse(it->second.first, it->second.second, both, depth + 1);
                }
            }
            else if (!type.empty() && type != "VERTEX" && type != "SEQEND" && type != "ATTRIB" && type != "ENDBLK") {
                ++d.skipped[type];
            }
            g.clear();
        };
        int lwVertex = -1;
        for (size_t k = from; k < to; ++k) {
            const Pair& p = pairs[k];
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
    };
    parse(i, pairs.size(), Xf {}, 0);
    if (d.lines.empty() && d.circles.empty() && d.arcs.empty() && d.splines.empty() && d.ellipses.empty() && d.texts.empty()) {
        return makeError("dxf.empty", "This DXF has no lines, circles, arcs, curves, text or blocks to bring in.",
                         {"Hatches and dimensions are not imported: explode them to lines first."});
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
    for (auto& sp : d.splines) {
        std::vector<std::pair<double, double>> pts;
        for (auto [x, y] : sp.points) {
            pts.push_back({x * scale + dx, y * scale + dy});
        }
        const int id = s.addSpline(pts, sp.closed);
        // its ends join the lines and arcs that end there, so outlines close
        if (!sp.closed && s.entity(id) && !s.entity(id)->pts.empty()) {
            const std::vector<int> fit = s.entity(id)->pts;
            for (int pid : {fit.front(), fit.back()}) {
                auto [x, y] = s.pointXY(pid);
                auto it = points.find({std::llround(x * 1e5), std::llround(y * 1e5)});
                if (it != points.end()) {
                    s.addConstraint(ConstraintType::Coincident, pid, it->second);
                }
                else {
                    points[{std::llround(x * 1e5), std::llround(y * 1e5)}] = pid;
                }
            }
        }
        ++n;
    }
    for (auto& e : d.ellipses) {
        const double major = std::hypot(e.ax, e.ay) * scale;
        s.addEllipse(e.cx * scale + dx, e.cy * scale + dy, e.cx * scale + dx + e.ax * scale, e.cy * scale + dy + e.ay * scale,
                     major * e.ratio);
        ++n;
    }
    for (auto& t : d.texts) {
        const double h = t.height * scale;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.6g mm", h);
        s.addText(t.x * scale + dx, t.y * scale + dy, t.text, buf, h, {}, t.angleDeg * kPi / 180);
        ++n;
    }
    return n;
}

}  // namespace jig
