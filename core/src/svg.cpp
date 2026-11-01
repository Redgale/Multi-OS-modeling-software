// SPDX-License-Identifier: GPL-3.0-or-later
// SVG import (brief M5): filled shapes, drawn outlines (strokes, as the band the pen
// leaves) and text of an SVG file (path, rect, circle, ellipse, line, polyline, text,
// polygon) become closed outlines of lines and Béziers, in millimetres. A small reader of
// our own: the core takes no XML or Qt dependency, and only the geometry is needed.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <locale>
#include <map>
#include <numbers>
#include <set>
#include <sstream>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <BRep_Builder.hxx>
#include <gp_Ax3.hxx>
#include <gp_Circ.hxx>
#include <gp_Pln.hxx>

#include "jig/files.hpp"
#include "jig/kernel.hpp"

namespace jig
{
namespace
{
constexpr double kPi = std::numbers::pi;

// 2D affine transform [a c e; b d f]
struct Affine
{
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    Affine operator*(const Affine& o) const  // this after o
    {
        return {a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.e + c * o.f + e,
                b * o.e + d * o.f + f};
    }
    std::array<double, 2> apply(double x, double y) const
    {
        return {a * x + c * y + e, b * x + d * y + f};
    }
};

struct Element
{
    std::string name;
    std::map<std::string, std::string> attrs;
    bool selfClosing = false, closing = false;
};

// Minimal XML tag reader: yields start / end / empty tags with their attributes; skips
// comments, processing instructions, doctype and CDATA. Text content is ignored.
std::vector<Element> tags(const std::string& x)
{
    std::vector<Element> out;
    size_t i = 0;
    while ((i = x.find('<', i)) != std::string::npos) {
        if (x.compare(i, 4, "<!--") == 0) {
            const size_t e = x.find("-->", i + 4);
            i = e == std::string::npos ? x.size() : e + 3;
            continue;
        }
        if (x.compare(i, 9, "<![CDATA[") == 0) {
            const size_t e = x.find("]]>", i + 9);
            i = e == std::string::npos ? x.size() : e + 3;
            continue;
        }
        if (x.compare(i, 2, "<?") == 0 || x.compare(i, 2, "<!") == 0) {
            const size_t e = x.find('>', i + 2);
            i = e == std::string::npos ? x.size() : e + 1;
            continue;
        }
        Element el;
        size_t j = i + 1;
        if (j < x.size() && x[j] == '/') {
            el.closing = true;
            ++j;
        }
        const size_t nameEnd = x.find_first_of(" \t\r\n/>", j);
        if (nameEnd == std::string::npos) {
            break;
        }
        el.name = x.substr(j, nameEnd - j);
        if (const auto colon = el.name.find(':'); colon != std::string::npos) {
            el.name = el.name.substr(colon + 1);  // svg:path -> path
        }
        j = nameEnd;
        while (j < x.size() && x[j] != '>') {
            if (x[j] == '/' && j + 1 < x.size() && x[j + 1] == '>') {
                el.selfClosing = true;
                ++j;
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(x[j]))) {
                ++j;
                continue;
            }
            const size_t eq = x.find('=', j);
            const size_t stop = x.find_first_of(" \t\r\n/>", j);
            if (eq == std::string::npos || (stop != std::string::npos && stop < eq)) {
                // an attribute without a value, or stray characters: always move on
                j = stop == std::string::npos ? x.size() : std::max(stop, j + 1);
                continue;
            }
            std::string key = x.substr(j, eq - j);
            while (!key.empty() && std::isspace(static_cast<unsigned char>(key.back()))) {
                key.pop_back();
            }
            size_t q = eq + 1;
            while (q < x.size() && std::isspace(static_cast<unsigned char>(x[q]))) {
                ++q;
            }
            if (q >= x.size() || (x[q] != '"' && x[q] != '\'')) {
                j = q;
                continue;
            }
            const size_t endq = x.find(x[q], q + 1);
            if (endq == std::string::npos) {
                return out;
            }
            el.attrs[key] = x.substr(q + 1, endq - q - 1);
            j = endq + 1;
        }
        if (!el.closing && !el.selfClosing && (el.name == "text" || el.name == "tspan")) {
            // the words of a text element, up to the next tag, with the usual entities
            const size_t next = x.find('<', j + 1);
            std::string t = x.substr(j + 1, (next == std::string::npos ? x.size() : next) - j - 1);
            for (auto [ent, ch] : {std::pair {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}}) {
                for (size_t at = t.find(ent); at != std::string::npos; at = t.find(ent, at + 1)) {
                    t.replace(at, std::strlen(ent), ch);
                }
            }
            const auto b = t.find_first_not_of(" \t\r\n"), e = t.find_last_not_of(" \t\r\n");
            el.attrs["#text"] = b == std::string::npos ? std::string {} : t.substr(b, e - b + 1);
        }
        out.push_back(std::move(el));
        i = j + 1;
    }
    return out;
}

// numbers in SVG syntax: "1.5-2", ".5.5", "1e-3", separated by spaces or commas
class Numbers
{
public:
    explicit Numbers(const std::string& s)
        : s_(s)
    {}
    bool more()
    {
        skip();
        return p_ < s_.size() && (std::isdigit(static_cast<unsigned char>(s_[p_])) || s_[p_] == '-' || s_[p_] == '+' || s_[p_] == '.');
    }
    std::optional<double> next()
    {
        skip();
        const size_t start = p_;
        if (p_ < s_.size() && (s_[p_] == '-' || s_[p_] == '+')) {
            ++p_;
        }
        bool dot = false, digits = false;
        while (p_ < s_.size()) {
            const char c = s_[p_];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                digits = true;
                ++p_;
            }
            else if (c == '.' && !dot) {
                dot = true;
                ++p_;
            }
            else {
                break;
            }
        }
        if (digits && p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
            size_t q = p_ + 1;
            if (q < s_.size() && (s_[q] == '-' || s_[q] == '+')) {
                ++q;
            }
            if (q < s_.size() && std::isdigit(static_cast<unsigned char>(s_[q]))) {
                p_ = q;
                while (p_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[p_]))) {
                    ++p_;
                }
            }
        }
        if (!digits) {
            p_ = start;
            return std::nullopt;
        }
        std::istringstream is(s_.substr(start, p_ - start));
        is.imbue(std::locale::classic());
        double v = 0;
        is >> v;
        if (!std::isfinite(v)) {
            return std::nullopt;
        }
        return v;
    }
    // a single 0/1 arc flag, which may be written without a separator ("a1 1 0 01 5 5")
    std::optional<int> flag()
    {
        skip();
        if (p_ < s_.size() && (s_[p_] == '0' || s_[p_] == '1')) {
            return s_[p_++] - '0';
        }
        return std::nullopt;
    }
    size_t pos() const
    {
        return p_;
    }
    void seek(size_t p)
    {
        p_ = p;
    }
    void skip()
    {
        while (p_ < s_.size() && (std::isspace(static_cast<unsigned char>(s_[p_])) || s_[p_] == ',')) {
            ++p_;
        }
    }

private:
    const std::string& s_;
    size_t p_ = 0;
};

Affine parseTransform(const std::string& t)
{
    Affine m;
    size_t i = 0;
    while (i < t.size()) {
        const size_t open = t.find('(', i);
        if (open == std::string::npos) {
            break;
        }
        const size_t close = t.find(')', open);
        if (close == std::string::npos) {
            break;
        }
        std::string name = t.substr(i, open - i);
        name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) || c == ','; }),
                   name.end());
        const std::string args = t.substr(open + 1, close - open - 1);
        Numbers n(args);
        std::vector<double> v;
        while (auto x = n.next()) {
            v.push_back(*x);
        }
        Affine k;
        if (name == "matrix" && v.size() == 6) {
            k = {v[0], v[1], v[2], v[3], v[4], v[5]};
        }
        else if (name == "translate" && !v.empty()) {
            k.e = v[0];
            k.f = v.size() > 1 ? v[1] : 0;
        }
        else if (name == "scale" && !v.empty()) {
            k.a = v[0];
            k.d = v.size() > 1 ? v[1] : v[0];
        }
        else if (name == "rotate" && !v.empty()) {
            const double r = v[0] * kPi / 180, cs = std::cos(r), sn = std::sin(r);
            k = {cs, sn, -sn, cs, 0, 0};
            if (v.size() == 3) {
                Affine to {1, 0, 0, 1, v[1], v[2]}, back {1, 0, 0, 1, -v[1], -v[2]};
                k = to * k * back;
            }
        }
        else if (name == "skewX" && !v.empty()) {
            k.c = std::tan(v[0] * kPi / 180);
        }
        else if (name == "skewY" && !v.empty()) {
            k.b = std::tan(v[0] * kPi / 180);
        }
        m = m * k;
        i = close + 1;
    }
    return m;
}

double attrNum(const Element& el, const char* key, double def = 0)
{
    auto it = el.attrs.find(key);
    if (it == el.attrs.end()) {
        return def;
    }
    Numbers n(it->second);
    auto v = n.next();
    return v ? *v : def;
}

// length with a unit, in mm (px at 96 per inch)
std::optional<double> lengthMm(const std::string& s)
{
    Numbers n(s);
    auto v = n.next();
    if (!v) {
        return std::nullopt;
    }
    std::string unit = s.substr(n.pos());
    unit.erase(std::remove_if(unit.begin(), unit.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); }),
               unit.end());
    if (unit == "mm") return *v;
    if (unit == "cm") return *v * 10;
    if (unit == "in") return *v * 25.4;
    if (unit == "pt") return *v * 25.4 / 72;
    if (unit == "pc") return *v * 25.4 / 6;
    if (unit.empty() || unit == "px") return *v * 25.4 / 96;
    return std::nullopt;  // %, em: unknown
}

using Contour = std::vector<std::vector<std::array<double, 2>>>;

// SVG elliptical arc -> cubic Béziers (endpoint to centre parameterisation, SVG 1.1 F.6)
void arcToCubics(double x1, double y1, double rx, double ry, double phiDeg, int large, int sweep, double x2, double y2,
                 std::vector<std::array<double, 2>>& pts)
{
    if (std::abs(x1 - x2) < 1e-12 && std::abs(y1 - y2) < 1e-12) {
        return;
    }
    rx = std::abs(rx);
    ry = std::abs(ry);
    if (rx < 1e-12 || ry < 1e-12) {
        pts.insert(pts.end(), {{x1, y1}, {x2, y2}});  // a straight line
        return;
    }
    const double phi = phiDeg * kPi / 180, cp = std::cos(phi), sp = std::sin(phi);
    const double dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    const double x1p = cp * dx + sp * dy, y1p = -sp * dx + cp * dy;
    const double lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
    if (lam > 1) {
        rx *= std::sqrt(lam);
        ry *= std::sqrt(lam);
    }
    double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    const double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    double coef = std::sqrt(std::max(0.0, num / den));
    if (large == sweep) {
        coef = -coef;
    }
    const double cxp = coef * rx * y1p / ry, cyp = -coef * ry * x1p / rx;
    const double cx = cp * cxp - sp * cyp + (x1 + x2) / 2, cy = sp * cxp + cp * cyp + (y1 + y2) / 2;
    auto angle = [](double ux, double uy, double vx, double vy) {
        return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    };
    const double t1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    double dt = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dt > 0) {
        dt -= 2 * kPi;
    }
    else if (sweep && dt < 0) {
        dt += 2 * kPi;
    }
    const int n = std::max(1, int(std::ceil(std::abs(dt) / (kPi / 2) - 1e-9)));
    const double step = dt / n, k = 4.0 / 3 * std::tan(step / 4);
    auto at = [&](double t, double ox, double oy) -> std::array<double, 2> {
        const double ex = rx * std::cos(t) + ox, ey = ry * std::sin(t) + oy;
        return {cp * ex - sp * ey + cx, sp * ex + cp * ey + cy};
    };
    for (int i = 0; i < n; ++i) {
        const double a = t1 + step * i, b = a + step;
        // control points: P + k * derivative
        const std::array<double, 2> p0 = at(a, 0, 0), p3 = at(b, 0, 0);
        const double d0x = -rx * std::sin(a), d0y = ry * std::cos(a), d1x = -rx * std::sin(b), d1y = ry * std::cos(b);
        const std::array<double, 2> p1 {p0[0] + k * (cp * d0x - sp * d0y), p0[1] + k * (sp * d0x + cp * d0y)};
        const std::array<double, 2> p2 {p3[0] - k * (cp * d1x - sp * d1y), p3[1] - k * (sp * d1x + cp * d1y)};
        pts.insert(pts.end(), {p0, p1, p2, p3});
    }
}

// path data -> contours (in user units, untransformed)
// With `closed`, subpaths without Z stay open (for strokes) and each one's closedness is
// recorded; without it every subpath is closed (for fills).
std::vector<Contour> parsePath(const std::string& d, std::vector<bool>* closed = nullptr)
{
    std::vector<Contour> out;
    Contour cur;
    double x = 0, y = 0, sx = 0, sy = 0;     // current point, subpath start
    double lcx = 0, lcy = 0;                  // last control point (S, T)
    char last = 0;
    bool byZ = false;
    auto close = [&]() {
        if (cur.empty()) {
            return;
        }
        const bool shut = byZ || !closed;
        if (shut && std::hypot(x - sx, y - sy) > 1e-9) {
            cur.push_back({{x, y}, {sx, sy}});
        }
        if (closed) {
            closed->push_back(byZ);
        }
        byZ = false;
        out.push_back(std::move(cur));
        cur.clear();
        x = sx;
        y = sy;
    };
    Numbers n(d);
    size_t i = 0;
    char cmd = 0;
    while (true) {
        n.skip();
        i = n.pos();
        if (i >= d.size()) {
            break;
        }
        if (std::isalpha(static_cast<unsigned char>(d[i]))) {
            cmd = d[i];
            n.seek(i + 1);
            if (cmd == 'Z' || cmd == 'z') {
                byZ = true;
                close();
                last = cmd;
                continue;
            }
        }
        else if (!cmd) {
            break;
        }
        const bool rel = std::islower(static_cast<unsigned char>(cmd));
        const char up = char(std::toupper(cmd));
        auto num = [&]() { return n.next(); };
        const double ox = rel ? x : 0, oy = rel ? y : 0;
        if (up == 'M') {
            auto a = num(), b = num();
            if (!a || !b) {
                break;
            }
            close();
            x = sx = *a + ox;
            y = sy = *b + oy;
            cmd = rel ? 'l' : 'L';  // further pairs are lines
        }
        else if (up == 'L' || up == 'H' || up == 'V') {
            double nx = x, ny = y;
            if (up == 'L') {
                auto a = num(), b = num();
                if (!a || !b) {
                    break;
                }
                nx = *a + ox;
                ny = *b + oy;
            }
            else if (up == 'H') {
                auto a = num();
                if (!a) {
                    break;
                }
                nx = *a + (rel ? x : 0);
            }
            else {
                auto a = num();
                if (!a) {
                    break;
                }
                ny = *a + (rel ? y : 0);
            }
            if (std::hypot(nx - x, ny - y) > 1e-12) {
                cur.push_back({{x, y}, {nx, ny}});
            }
            x = nx;
            y = ny;
        }
        else if (up == 'C' || up == 'S') {
            double c1x, c1y;
            if (up == 'C') {
                auto a = num(), b = num();
                if (!a || !b) {
                    break;
                }
                c1x = *a + ox;
                c1y = *b + oy;
            }
            else {
                const bool smooth = last == 'C' || last == 'c' || last == 'S' || last == 's';
                c1x = smooth ? 2 * x - lcx : x;
                c1y = smooth ? 2 * y - lcy : y;
            }
            auto c = num(), dd = num(), e = num(), f = num();
            if (!c || !dd || !e || !f) {
                break;
            }
            const double c2x = *c + ox, c2y = *dd + oy, ex = *e + ox, ey = *f + oy;
            cur.push_back({{x, y}, {c1x, c1y}, {c2x, c2y}, {ex, ey}});
            lcx = c2x;
            lcy = c2y;
            x = ex;
            y = ey;
        }
        else if (up == 'Q' || up == 'T') {
            double qx, qy;
            if (up == 'Q') {
                auto a = num(), b = num();
                if (!a || !b) {
                    break;
                }
                qx = *a + ox;
                qy = *b + oy;
            }
            else {
                const bool smooth = last == 'Q' || last == 'q' || last == 'T' || last == 't';
                qx = smooth ? 2 * x - lcx : x;
                qy = smooth ? 2 * y - lcy : y;
            }
            auto e = num(), f = num();
            if (!e || !f) {
                break;
            }
            const double ex = *e + ox, ey = *f + oy;
            cur.push_back({{x, y}, {qx, qy}, {ex, ey}});
            lcx = qx;
            lcy = qy;
            x = ex;
            y = ey;
        }
        else if (up == 'A') {
            auto rx = num(), ry = num(), rot = num();
            auto large = n.flag(), sweep = n.flag();
            auto e = num(), f = num();
            if (!rx || !ry || !rot || !large || !sweep || !e || !f) {
                break;
            }
            const double ex = *e + ox, ey = *f + oy;
            std::vector<std::array<double, 2>> pts;
            arcToCubics(x, y, *rx, *ry, *rot, *large, *sweep, ex, ey, pts);
            for (size_t k = 0; k + 1 < pts.size(); k += (pts.size() == 2 ? 2 : 4)) {
                if (pts.size() == 2) {
                    cur.push_back({pts[0], pts[1]});
                }
                else {
                    cur.push_back({pts[k], pts[k + 1], pts[k + 2], pts[k + 3]});
                }
            }
            x = ex;
            y = ey;
        }
        else {
            break;  // unknown command: stop reading this path
        }
        last = cmd;
    }
    close();
    return out;
}

// A point on a segment (2 points: line, 3: quadratic, 4: cubic Bézier) at t in [0, 1].
std::array<double, 2> segAt(const std::vector<std::array<double, 2>>& s, double t)
{
    std::vector<std::array<double, 2>> p(s);
    for (size_t r = 1; r < p.size(); ++r) {
        for (size_t k = 0; k + r < p.size(); ++k) {
            p[k] = {p[k][0] + (p[k + 1][0] - p[k][0]) * t, p[k][1] + (p[k + 1][1] - p[k][1]) * t};
        }
    }
    return p[0];
}

// The closed outlines of OCCT faces in the XY plane, as straight segments (round edges in
// short pieces), outer and inner (hole) wires alike.
std::vector<Contour> faceContours(const TopoDS_Shape& faces)
{
    std::vector<Contour> out;
    for (TopExp_Explorer fx(faces, TopAbs_FACE); fx.More(); fx.Next()) {
        for (TopExp_Explorer wx(fx.Current(), TopAbs_WIRE); wx.More(); wx.Next()) {
            std::vector<std::array<double, 2>> pts;
            for (BRepTools_WireExplorer ex(TopoDS::Wire(wx.Current()), TopoDS::Face(fx.Current())); ex.More(); ex.Next()) {
                BRepAdaptor_Curve c(ex.Current());
                const bool rev = ex.Current().Orientation() == TopAbs_REVERSED;
                const int n = c.GetType() == GeomAbs_Line ? 1 : 12;
                for (int k = 0; k < n; ++k) {
                    double t = double(k) / n;
                    if (rev) {
                        t = 1 - t;
                    }
                    const gp_Pnt p = c.Value(c.FirstParameter() + (c.LastParameter() - c.FirstParameter()) * t);
                    pts.push_back({p.X(), p.Y()});
                }
            }
            if (pts.size() < 3) {
                continue;
            }
            Contour c;
            for (size_t k = 0; k < pts.size(); ++k) {
                c.push_back({pts[k], pts[(k + 1) % pts.size()]});
            }
            out.push_back(std::move(c));
        }
    }
    return out;
}

// What a pen of width w draws along these paths: a band round each piece with round joins
// and ends, all joined into one outline (with its holes).
std::vector<Contour> strokeOutline(const std::vector<Contour>& paths, const std::vector<bool>& closed, double w)
{
    if (!(w > 1e-9)) {
        return {};
    }
    TopTools_ListOfShape parts;
    const double r = w / 2;
    auto disc = [&](const std::array<double, 2>& p) {
        parts.Append(BRepBuilderAPI_MakeFace(BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(
                                                 gp_Circ(gp_Ax2(gp_Pnt(p[0], p[1], 0), gp_Dir(0, 0, 1)), r)).Edge()).Wire())
                         .Face());
    };
    for (size_t k = 0; k < paths.size(); ++k) {
        std::vector<std::array<double, 2>> pts;
        for (auto& seg : paths[k]) {
            const int n = seg.size() <= 2 ? 1 : 16;  // curves in short straight pieces
            for (int i = 0; i < n; ++i) {
                pts.push_back(segAt(seg, double(i) / n));
            }
        }
        if (!paths[k].empty()) {
            pts.push_back(paths[k].back().back());
        }
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const double dx = pts[i + 1][0] - pts[i][0], dy = pts[i + 1][1] - pts[i][1];
            const double L = std::hypot(dx, dy);
            if (L < 1e-9) {
                continue;
            }
            const double nx = -dy / L * r, ny = dx / L * r;
            // counter-clockwise, so every piece faces +Z like the round joins (or they will not merge)
            BRepBuilderAPI_MakePolygon poly(gp_Pnt(pts[i][0] - nx, pts[i][1] - ny, 0), gp_Pnt(pts[i + 1][0] - nx, pts[i + 1][1] - ny, 0),
                                            gp_Pnt(pts[i + 1][0] + nx, pts[i + 1][1] + ny, 0), gp_Pnt(pts[i][0] + nx, pts[i][1] + ny, 0), true);
            parts.Append(BRepBuilderAPI_MakeFace(poly.Wire()).Face());
            // a round start, and a round join where the path turns (not between the short
            // pieces of a smooth curve)
            bool turns = i == 0;
            if (i > 0) {
                const double px = pts[i][0] - pts[i - 1][0], py = pts[i][1] - pts[i - 1][1];
                const double lp = std::hypot(px, py);
                turns = lp < 1e-9 || (px * dx + py * dy) / (lp * L) < std::cos(2 * kPi / 180);
            }
            if (turns) {
                disc(pts[i]);
            }
        }
        if (!pts.empty() && !(k < closed.size() && closed[k])) {
            disc(pts.back());  // a round end
        }
    }
    if (parts.IsEmpty()) {
        return {};
    }
    if (parts.Extent() == 1) {
        return faceContours(parts.First());
    }
    TopTools_ListOfShape first, rest;
    first.Append(parts.First());
    for (auto it = TopTools_ListIteratorOfListOfShape(parts); it.More(); it.Next()) {
        if (!it.Value().IsSame(parts.First())) {
            rest.Append(it.Value());
        }
    }
    BRepAlgoAPI_Fuse fuse;
    fuse.SetArguments(first);
    fuse.SetTools(rest);
    fuse.Build();
    if (!fuse.IsDone()) {
        return {};
    }
    ShapeUpgrade_UnifySameDomain unify(fuse.Shape(), true, true, false);
    unify.Build();
    return faceContours(unify.Shape());
}

Contour ellipse(double cx, double cy, double rx, double ry)
{
    const double k = 0.5522847498307936;  // 4/3 (sqrt(2) - 1): quarter circle as a cubic
    Contour c;
    const std::array<double, 2> pts[4] = {{cx + rx, cy}, {cx, cy + ry}, {cx - rx, cy}, {cx, cy - ry}};
    for (int i = 0; i < 4; ++i) {
        const auto& p = pts[i];
        const auto& q = pts[(i + 1) % 4];
        const double tpx = i == 0 ? 0 : i == 1 ? -rx : i == 2 ? 0 : rx;  // tangent at p (scaled)
        const double tpy = i == 0 ? ry : i == 1 ? 0 : i == 2 ? -ry : 0;
        const double tqx = i == 0 ? -rx : i == 1 ? 0 : i == 2 ? rx : 0;
        const double tqy = i == 0 ? 0 : i == 1 ? -ry : i == 2 ? 0 : ry;
        c.push_back({p, {p[0] + k * tpx, p[1] + k * tpy}, {q[0] - k * tqx, q[1] - k * tqy}, q});
    }
    return c;
}
}  // namespace

Result<SvgArtwork> parseSvg(const std::string& text)
{
    try {
        const auto els = tags(text);
        SvgArtwork art;
        // root <svg>: size and viewBox give millimetres per user unit
        double mmPerUnit = 25.4 / 96;
        bool sawRoot = false;
        std::vector<Affine> stack {Affine {}};
        std::vector<int> skipDepth;  // inside <defs>, <clipPath>, … nothing is drawn
        static const std::set<std::string> hidden {"defs", "clipPath", "mask", "pattern", "marker", "symbol", "metadata", "title", "desc", "style"};
        std::vector<Contour> raw;
        for (const Element& el : els) {
            if (el.closing) {
                if (!skipDepth.empty()) {  // inside <defs> and friends nothing was pushed
                    if (hidden.count(el.name) && --skipDepth.back() == 0) {
                        skipDepth.pop_back();
                    }
                    continue;
                }
                if (el.name == "g" || el.name == "svg" || el.name == "a") {
                    if (stack.size() > 1) {
                        stack.pop_back();
                    }
                }
                continue;
            }
            if (hidden.count(el.name)) {
                if (!el.selfClosing) {
                    if (!skipDepth.empty()) {
                        ++skipDepth.back();
                    }
                    else {
                        skipDepth.push_back(1);
                    }
                }
                continue;
            }
            if (!skipDepth.empty()) {
                continue;
            }
            Affine m = stack.back();
            if (auto t = el.attrs.find("transform"); t != el.attrs.end()) {
                m = m * parseTransform(t->second);
            }
            if (el.name == "svg" && !sawRoot) {
                sawRoot = true;
                std::array<double, 4> vb {0, 0, 0, 0};
                bool hasVb = false;
                if (auto v = el.attrs.find("viewBox"); v != el.attrs.end()) {
                    Numbers n(v->second);
                    for (auto& x : vb) {
                        auto q = n.next();
                        x = q ? *q : 0;
                    }
                    hasVb = vb[2] > 0 && vb[3] > 0;
                }
                auto w = el.attrs.count("width") ? lengthMm(el.attrs.at("width")) : std::nullopt;
                if (hasVb && w) {
                    mmPerUnit = *w / vb[2];
                    art.unitsKnown = true;
                }
                else if (w) {
                    art.unitsKnown = el.attrs.at("width").find_first_of("abcdefghijklmnopqrstuvwxyz") != std::string::npos;
                }
                if (hasVb) {
                    m = m * Affine {1, 0, 0, 1, -vb[0], -vb[1]};
                }
                stack.push_back(m);
                continue;
            }
            if (el.name == "g" || el.name == "a" || el.name == "svg") {
                if (!el.selfClosing) {
                    stack.push_back(m);
                }
                continue;
            }
            if (auto st = el.attrs.find("style"); st != el.attrs.end() && st->second.find("display:none") != std::string::npos) {
                continue;
            }
            // a style property from the attribute or the style="" list
            auto prop = [&](const std::string& name) -> std::string {
                if (auto st = el.attrs.find("style"); st != el.attrs.end()) {
                    const std::string& css = st->second;
                    for (size_t at = css.find(name); at != std::string::npos; at = css.find(name, at + 1)) {
                        if ((at == 0 || css[at - 1] == ';' || std::isspace(static_cast<unsigned char>(css[at - 1]))) &&
                            css.compare(at + name.size(), 1, ":") == 0) {
                            const size_t v = at + name.size() + 1, e = css.find(';', v);
                            std::string val = css.substr(v, e == std::string::npos ? std::string::npos : e - v);
                            const auto b = val.find_first_not_of(" \t"), z = val.find_last_not_of(" \t");
                            return b == std::string::npos ? std::string {} : val.substr(b, z - b + 1);
                        }
                    }
                }
                auto a = el.attrs.find(name);
                return a == el.attrs.end() ? std::string {} : a->second;
            };
            const bool lineLike = el.name == "line" || el.name == "polyline";
            const bool filled = !lineLike && prop("fill") != "none";
            const std::string strokeColour = prop("stroke");
            const bool wantStroke = !strokeColour.empty() && strokeColour != "none";
            bool stroked = wantStroke && (!filled || lineLike);  // a path left open also counts (below)
            const double unitScale = std::sqrt(std::abs(m.a * m.d - m.b * m.c));  // how much the transform enlarges
            auto toUser = [&](Contour c) {
                for (auto& seg : c) {
                    for (auto& p : seg) {
                        p = m.apply(p[0], p[1]);
                    }
                }
                return c;
            };
            if (el.name == "text" || el.name == "tspan") {
                // the words, drawn with the font (outlines, like everything else here)
                const std::string words = el.attrs.count("#text") ? el.attrs.at("#text") : std::string {};
                if (words.empty()) {
                    continue;
                }
                std::string size = prop("font-size");
                const double fs = size.empty() ? 16 : std::max(0.1, std::atof(size.c_str()));
                std::string family = prop("font-family");
                if (const auto comma = family.find(','); comma != std::string::npos) {
                    family = family.substr(0, comma);
                }
                family.erase(std::remove(family.begin(), family.end(), '\''), family.end());
                family.erase(std::remove(family.begin(), family.end(), '"'), family.end());
                SketchText t;
                t.text = words;
                t.font = family == "sans-serif" || family == "serif" || family == "monospace" ? std::string {} : family;
                t.heightValue = fs;
                auto faces = textFaces(t, 0, 0, gp_Ax3());
                if (!faces || faces->faces.empty()) {
                    ++art.skipped[el.name];
                    continue;
                }
                TopoDS_Compound all;
                BRep_Builder bb;
                bb.MakeCompound(all);
                for (auto& f : faces->faces) {
                    bb.Add(all, f);
                }
                auto cs = faceContours(all);
                // the text was made centred on (0, 0) with y up: put its baseline start (or its
                // middle / end, by text-anchor) at x, y, in the drawing's y-down units
                double x0 = 1e300, x1 = -1e300, y0 = 1e300;
                for (auto& c : cs) {
                    for (auto& seg : c) {
                        for (auto& pt : seg) {
                            x0 = std::min(x0, pt[0]);
                            x1 = std::max(x1, pt[0]);
                            y0 = std::min(y0, pt[1]);
                        }
                    }
                }
                const std::string anchor = prop("text-anchor");
                const double ax = anchor == "middle" ? (x0 + x1) / 2 : anchor == "end" ? x1 : x0;
                const double px = attrNum(el, "x"), py = attrNum(el, "y");
                for (auto& c : cs) {
                    for (auto& seg : c) {
                        for (auto& pt : seg) {
                            pt = {px + (pt[0] - ax), py - (pt[1] - y0)};  // the lowest point on the baseline
                        }
                    }
                    raw.push_back(toUser(std::move(c)));
                }
                continue;
            }
            if (el.name == "image" || el.name == "use") {
                ++art.skipped[el.name];
                continue;
            }
            std::vector<Contour> cs;
            std::vector<bool> closedFlags;
            if (el.name == "path" && el.attrs.count("d")) {
                cs = parsePath(el.attrs.at("d"), wantStroke ? &closedFlags : nullptr);
                // a drawn path left open (no Z) is a line, even with the default black fill
                stroked = stroked || (wantStroke && std::find(closedFlags.begin(), closedFlags.end(), false) != closedFlags.end());
            }
            else if (el.name == "rect") {
                const double x = attrNum(el, "x"), y = attrNum(el, "y"), w = attrNum(el, "width"), h = attrNum(el, "height");
                if (w > 0 && h > 0) {
                    cs.push_back({{{x, y}, {x + w, y}}, {{x + w, y}, {x + w, y + h}}, {{x + w, y + h}, {x, y + h}}, {{x, y + h}, {x, y}}});
                }
            }
            else if (el.name == "circle") {
                const double r = attrNum(el, "r");
                if (r > 0) {
                    cs.push_back(ellipse(attrNum(el, "cx"), attrNum(el, "cy"), r, r));
                }
            }
            else if (el.name == "ellipse") {
                const double rx = attrNum(el, "rx"), ry = attrNum(el, "ry");
                if (rx > 0 && ry > 0) {
                    cs.push_back(ellipse(attrNum(el, "cx"), attrNum(el, "cy"), rx, ry));
                }
            }
            else if (el.name == "line") {
                cs.push_back({{{attrNum(el, "x1"), attrNum(el, "y1")}, {attrNum(el, "x2"), attrNum(el, "y2")}}});
                closedFlags.push_back(false);
            }
            else if ((el.name == "polygon" || el.name == "polyline") && el.attrs.count("points")) {
                Numbers n(el.attrs.at("points"));
                std::vector<std::array<double, 2>> pts;
                while (true) {
                    auto a = n.next(), b = n.next();
                    if (!a || !b) {
                        break;
                    }
                    pts.push_back({*a, *b});
                }
                const bool shut = el.name == "polygon";
                if (pts.size() >= (shut ? 3u : 2u)) {
                    Contour c;
                    for (size_t k = 0; k + 1 < pts.size() + (shut ? 1 : 0); ++k) {
                        c.push_back({pts[k], pts[(k + 1) % pts.size()]});
                    }
                    cs.push_back(c);
                    closedFlags.push_back(shut);
                }
            }
            else {
                continue;
            }
            if (closedFlags.size() != cs.size()) {
                closedFlags.assign(cs.size(), true);  // rectangles, circles, ellipses
            }
            if (filled && !stroked) {
                for (auto& c : cs) {
                    raw.push_back(toUser(std::move(c)));
                }
            }
            else if (stroked) {
                // a drawn outline: the band the pen leaves, as a shape of its own
                std::string sw = prop("stroke-width");
                const double w = (sw.empty() ? 1.0 : std::atof(sw.c_str())) * unitScale;
                std::vector<Contour> user;
                for (auto& c : cs) {
                    user.push_back(toUser(std::move(c)));
                }
                for (auto& c : strokeOutline(user, closedFlags, w)) {
                    raw.push_back(std::move(c));
                }
            }
            else {
                ++art.skipped["unfilled " + el.name];  // neither filled nor drawn: nothing to cut or extrude
            }
        }
        if (!sawRoot) {
            return makeError("svg.format", "This is not an SVG file.");
        }
        if (raw.empty()) {
            return makeError("svg.empty", "This SVG has no shapes, outlines or text to bring in.");
        }
        // millimetres, y up, centred on the bounding box of the control points
        double xmin = 1e300, xmax = -1e300, ymin = 1e300, ymax = -1e300;
        for (auto& c : raw) {
            for (auto& seg : c) {
                for (auto& p : seg) {
                    p = {p[0] * mmPerUnit, -p[1] * mmPerUnit};
                    xmin = std::min(xmin, p[0]);
                    xmax = std::max(xmax, p[0]);
                    ymin = std::min(ymin, p[1]);
                    ymax = std::max(ymax, p[1]);
                }
            }
        }
        const double cx = (xmin + xmax) / 2, cy = (ymin + ymax) / 2;
        for (auto& c : raw) {
            for (auto& seg : c) {
                for (auto& p : seg) {
                    p = {p[0] - cx, p[1] - cy};
                }
            }
        }
        art.contours = std::move(raw);
        art.width = xmax - xmin;
        art.height = ymax - ymin;
        if (!(art.width > 1e-9) || !std::isfinite(art.width) || !std::isfinite(art.height)) {
            return makeError("svg.empty", "The shapes in this SVG have no size.");
        }
        return art;
    }
    catch (const std::exception&) {
        return makeError("svg.format", "This SVG file is damaged and could not be read.");
    }
}

Result<SvgArtwork> readSvg(const std::string& path)
{
    const auto text = readWholeFile(path, 64u * 1024u * 1024u);
    if (!text) {
        return makeError("svg.open", "This file could not be opened, or it is larger than 64 MB.");
    }
    return parseSvg(*text);
}

Result<std::vector<TopoDS_Face>> artworkFaces(const SketchArtwork& a, double x, double y, const gp_Ax3& frame)
{
    std::vector<OutlineContour> cs;
    for (auto& c : a.contours) {
        OutlineContour oc;
        for (auto& seg : c) {
            OutlineSegment s;
            for (auto& p : seg) {
                s.emplace_back(p[0], p[1]);
            }
            oc.push_back(std::move(s));
        }
        cs.push_back(std::move(oc));
    }
    const double k = a.scale();
    try {
        return facesFromOutlines(
            cs,
            [&](const gp_XY& p) {
                const double lx = x + p.X() * k, ly = y + p.Y() * k;
                return gp_Pnt(frame.Location().XYZ() + frame.XDirection().XYZ() * lx + frame.YDirection().XYZ() * ly);
            },
            gp_Pln(frame));
    }
    catch (const Standard_Failure&) {
        return makeError("svg.outline", "This artwork could not be turned into shapes.");
    }
}

}  // namespace jig
