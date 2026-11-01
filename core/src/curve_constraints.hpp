// SPDX-License-Identifier: GPL-3.0-or-later
// Solver constraints along ellipses and smooth curves, which planegcs has no form for in
// the way Jigsmith stores them (an ellipse by its centre, the end of one axis and the other
// radius; a smooth curve by the points it passes through). Internal to the core.
#pragma once

#include <array>
#include <functional>
#include <utility>
#include <vector>

#include <planegcs/GCS.h>

namespace jig::detail
{
using Pt = std::pair<double, double>;
using Cubic = std::array<Pt, 4>;

// The cubic pieces of a smooth curve through P (Catmull-Rom; open ends mirror their neighbour).
std::vector<Cubic> catmullRom(const std::vector<Pt>& P, bool closed);
// The cubic pieces of a uniform cubic B-spline with control points P (open: ends reflected so
// the curve starts at the first point and ends at the last; closed: wraps round).
std::vector<Cubic> bsplinePieces(const std::vector<Pt>& P, bool closed);

// A constraint whose error is any function of its parameters' values; the gradient is
// worked out by small steps, so the function only has to be smooth where it is zero.
class FuncConstraint : public GCS::Constraint
{
public:
    using Fn = std::function<double(const std::vector<double>&)>;
    FuncConstraint(std::vector<double*> params, Fn fn);
    void errorgrad(double* err, double* grad, double* param) override;

private:
    double eval();
    Fn fn_;
};

// Signed distance-like errors (zero on the curve). Value layouts are in the .cpp.
double pointOnEllipse(const std::vector<double>& v, double minor);  // px py cx cy ax ay
double lineTangentToEllipse(const std::vector<double>& v, double minor);  // l1 l2 c a
double circleTangentToEllipse(const std::vector<double>& v, double minor);  // ccx ccy r c a
double pointOnSmooth(const std::vector<double>& v, bool closed, bool control = false);  // px py, then the points
// Two directions at a shared point running the same way (or straight back):
// each is 4 values (from, to); `perp` turns one a quarter turn (an arc: its radius).
double sameDirection(const std::vector<double>& v, bool perpA, bool perpB);
}  // namespace jig::detail
