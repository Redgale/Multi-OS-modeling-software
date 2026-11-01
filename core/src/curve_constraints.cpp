// SPDX-License-Identifier: GPL-3.0-or-later
#include "curve_constraints.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace jig::detail
{
std::vector<Cubic> catmullRom(const std::vector<Pt>& P, bool closed)
{
    std::vector<Cubic> out;
    const int n = int(P.size());
    if (n < 2) {
        return out;
    }
    auto at = [&](int i) -> Pt {
        if (closed) {
            return P[size_t(((i % n) + n) % n)];
        }
        if (i < 0) {
            return {2 * P[0].first - P[1].first, 2 * P[0].second - P[1].second};
        }
        if (i >= n) {
            return {2 * P[size_t(n - 1)].first - P[size_t(n - 2)].first,
                    2 * P[size_t(n - 1)].second - P[size_t(n - 2)].second};
        }
        return P[size_t(i)];
    };
    const int pieces = closed ? n : n - 1;
    for (int i = 0; i < pieces; ++i) {
        auto p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
        out.push_back({p1,
                       Pt {p1.first + (p2.first - p0.first) / 6, p1.second + (p2.second - p0.second) / 6},
                       Pt {p2.first - (p3.first - p1.first) / 6, p2.second - (p3.second - p1.second) / 6},
                       p2});
    }
    return out;
}

FuncConstraint::FuncConstraint(std::vector<double*> params, Fn fn)
    : fn_(std::move(fn))
{
    pvec = std::move(params);
    origpvec = pvec;
    rescale();
}

double FuncConstraint::eval()
{
    std::vector<double> v;
    v.reserve(pvec.size());
    for (double* p : pvec) {
        v.push_back(*p);
    }
    return fn_(v);
}

void FuncConstraint::errorgrad(double* err, double* grad, double* param)
{
    if (err) {
        *err = eval();
    }
    if (grad) {
        *grad = 0;
        if (param && findParamInPvec(param) != -1) {
            const double old = *param, h = 1e-7 * std::max(1.0, std::abs(old));
            *param = old + h;
            const double up = eval();
            *param = old - h;
            const double down = eval();
            *param = old;
            *grad = (up - down) / (2 * h);
        }
    }
}

namespace
{
// (p - c) in the ellipse's own axes, scaled so the ellipse is the unit circle
Pt toUnit(double px, double py, double cx, double cy, double dx, double dy, double A, double b)
{
    const double rx = px - cx, ry = py - cy;
    return {(rx * dx + ry * dy) / A, (-rx * dy + ry * dx) / b};
}
}  // namespace

double pointOnEllipse(const std::vector<double>& v, double minor)
{
    const double A = std::hypot(v[4] - v[2], v[5] - v[3]);
    if (A < 1e-12 || minor < 1e-12) {
        return std::hypot(v[0] - v[2], v[1] - v[3]);
    }
    const double dx = (v[4] - v[2]) / A, dy = (v[5] - v[3]) / A;
    const auto [u, w] = toUnit(v[0], v[1], v[2], v[3], dx, dy, A, minor);
    return (std::hypot(u, w) - 1) * std::min(A, minor);
}

double lineTangentToEllipse(const std::vector<double>& v, double minor)
{
    const double A = std::hypot(v[6] - v[4], v[7] - v[5]);
    if (A < 1e-12 || minor < 1e-12) {
        return 0;
    }
    const double dx = (v[6] - v[4]) / A, dy = (v[7] - v[5]) / A;
    const auto q1 = toUnit(v[0], v[1], v[4], v[5], dx, dy, A, minor);
    const auto q2 = toUnit(v[2], v[3], v[4], v[5], dx, dy, A, minor);
    const double ex = q2.first - q1.first, ey = q2.second - q1.second, L = std::hypot(ex, ey);
    if (L < 1e-12) {
        return 0;
    }
    const double dist = std::abs(q1.first * ey - q1.second * ex) / L;  // centre to the line
    return (dist - 1) * std::min(A, minor);
}

double pointOnSmooth(const std::vector<double>& v, bool closed)
{
    std::vector<Pt> P;
    for (size_t i = 2; i + 1 < v.size(); i += 2) {
        P.emplace_back(v[i], v[i + 1]);
    }
    const auto pieces = catmullRom(P, closed);
    if (pieces.empty()) {
        return 0;
    }
    const double px = v[0], py = v[1];
    auto at = [](const Cubic& b, double t, int d) -> Pt {
        const double m = 1 - t;
        double w[4];
        if (d == 0) {
            w[0] = m * m * m, w[1] = 3 * m * m * t, w[2] = 3 * m * t * t, w[3] = t * t * t;
        }
        else if (d == 1) {
            w[0] = -3 * m * m, w[1] = 3 * m * m - 6 * m * t, w[2] = 6 * m * t - 3 * t * t, w[3] = 3 * t * t;
        }
        else {
            w[0] = 6 * m, w[1] = -12 * m + 6 * t, w[2] = 6 * m - 12 * t, w[3] = 6 * t;
        }
        return {w[0] * b[0].first + w[1] * b[1].first + w[2] * b[2].first + w[3] * b[3].first,
                w[0] * b[0].second + w[1] * b[1].second + w[2] * b[2].second + w[3] * b[3].second};
    };
    // the nearest sample, then Newton steps along the whole curve (T = piece + t), so the
    // foot can move on into the next piece
    const int n = int(pieces.size());
    auto split = [&](double T) {
        if (closed) {
            T = std::fmod(T, double(n));
            T = T < 0 ? T + n : T;
        }
        const int i = std::clamp(int(std::floor(T)), 0, n - 1);
        return std::pair {i, T - i};
    };
    double bT = 0, bd = 1e300;
    for (int i = 0; i < n; ++i) {
        for (int s = 0; s <= 16; ++s) {
            const auto q = at(pieces[size_t(i)], s / 16.0, 0);
            const double d = std::hypot(q.first - px, q.second - py);
            if (d < bd) {
                bd = d, bT = i + s / 16.0;
            }
        }
    }
    for (int it = 0; it < 12; ++it) {
        const auto [i, t] = split(bT);
        const Cubic& b = pieces[size_t(i)];
        const auto q = at(b, t, 0), d1 = at(b, t, 1), d2 = at(b, t, 2);
        const double rx = q.first - px, ry = q.second - py;
        const double f = rx * d1.first + ry * d1.second;
        const double fp = d1.first * d1.first + d1.second * d1.second + rx * d2.first + ry * d2.second;
        if (std::abs(fp) < 1e-15) {
            break;
        }
        const double step = std::clamp(-f / fp, -0.5, 0.5);
        bT = closed ? bT + step : std::clamp(bT + step, 0.0, double(n));
        if (std::abs(step) < 1e-14) {
            break;
        }
    }
    const auto [i, t] = split(bT);
    const auto q = at(pieces[size_t(i)], t, 0), d1 = at(pieces[size_t(i)], t, 1);
    const double L = std::hypot(d1.first, d1.second);
    const double rx = px - q.first, ry = py - q.second;
    const double normal = L > 1e-15 ? (-rx * d1.second + ry * d1.first) / L : 1;
    // the whole distance, on the side the point is: the normal part once the foot is found,
    // and still right past an open end
    return std::copysign(std::hypot(rx, ry), normal);
}

double sameDirection(const std::vector<double>& v, bool perpA, bool perpB)
{
    double ax = v[2] - v[0], ay = v[3] - v[1], bx = v[6] - v[4], by = v[7] - v[5];
    if (perpA) {
        std::tie(ax, ay) = std::pair {-ay, ax};
    }
    if (perpB) {
        std::tie(bx, by) = std::pair {-by, bx};
    }
    const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
    if (la < 1e-12 || lb < 1e-12) {
        return 0;
    }
    return (ax * by - ay * bx) / (la * lb);
}
}  // namespace jig::detail
