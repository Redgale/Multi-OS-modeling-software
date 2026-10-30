// SPDX-License-Identifier: GPL-3.0-or-later
// Sketch model and the planegcs solver wrapper (brief 5.4c, D-004).
//
// Entities and constraints carry stable integer ids that are never reused inside
// a sketch; 3D features refer to sketch geometry through these ids, which is the
// root of persistent naming (D-005).
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "jig/expr.hpp"
#include "jig/result.hpp"

namespace jig
{

enum class EntityType
{
    Point,
    Line,
    Circle,
    Arc
};

struct SketchEntity
{
    int id = 0;
    EntityType type = EntityType::Point;
    bool construction = false;
    // Point: x, y. Circle: center point id + r. Arc: center/start/end point ids + r.
    double x = 0, y = 0;
    int p1 = 0, p2 = 0, center = 0;  // point ids (line: p1,p2; arc: center,p1=start,p2=end)
    double r = 0;
    double a0 = 0, a1 = 0;  // arc start/end angle (rad), counter-clockwise
    bool internal = false;  // point owned by a curve (hidden in the UI)
};

enum class ConstraintType
{
    Coincident,     // a=point, b=point
    PointOnCurve,   // a=point, b=line/circle/arc
    Horizontal,     // a=line            (or a=point, b=point)
    Vertical,       // a=line            (or a=point, b=point)
    Parallel,       // a=line, b=line
    Perpendicular,  // a=line, b=line
    Equal,          // a,b = lines (length) or circles/arcs (radius)
    Tangent,        // a=line|circle|arc, b=circle|arc
    Midpoint,       // a=point, b=line
    Symmetric,      // a=point, b=point, c=line
    Fix,            // a=point (keeps its current position)
    Distance,       // a=point|line, b=point|0 : length of a line or distance between points
    DistanceX,      // a=point, b=point|0 (0 = sketch origin)
    DistanceY,      // a=point, b=point|0
    Radius,         // a=circle|arc
    Diameter,       // a=circle|arc
    Angle           // a=line, b=line
};

struct SketchConstraint
{
    int id = 0;
    ConstraintType type = ConstraintType::Coincident;
    int a = 0, b = 0, c = 0;
    std::string expr;     // value expression for dimensional constraints
    bool driving = true;  // false = driven (reference) dimension
    bool automatic = false;
    double fx = 0, fy = 0;  // Fix: the locked position
};

enum class SolveState
{
    UnderConstrained,  // solved, some freedom left
    FullyConstrained,
    OverConstrained,  // conflicting or redundant constraints
    Failed
};

struct SolveReport
{
    SolveState state = SolveState::Failed;
    int dof = 0;
    std::vector<int> conflicting;  // constraint ids
    std::vector<int> redundant;    // constraint ids
    std::vector<int> freeEntities; // entity ids that can still move (for the UI)
    std::string message;           // plain-language summary
    double solveMs = 0;
};

// An open endpoint: a point used by exactly one non-construction curve.
struct GapMarker
{
    int pointId = 0;
    double x = 0, y = 0;
    std::optional<int> nearestPointId;  // closest other open endpoint
    double gap = 0;                     // distance to it
};

class Sketch
{
public:
    // ---- geometry -------------------------------------------------------
    int addPoint(double x, double y, bool internal = false);
    int addLine(int p1, int p2);
    int addLine(double x1, double y1, double x2, double y2);
    int addCircle(double cx, double cy, double r);
    // Counter-clockwise arc from angle a0 to a1 around (cx, cy).
    int addArc(double cx, double cy, double r, double a0, double a1);
    bool remove(int entityId);  // also removes dependent constraints and internal points
    void setConstruction(int entityId, bool on);

    // ---- constraints ----------------------------------------------------
    int addConstraint(ConstraintType type, int a, int b = 0, int c = 0, std::string expr = {});
    bool removeConstraint(int constraintId);

    // ---- solving --------------------------------------------------------
    // Evaluates dimension expressions with `lookup` and solves. On failure the
    // geometry is left exactly as it was before the call.
    SolveReport solve(const ParamLookup& lookup = {});
    // Solves while pulling a point toward (x, y), the way a mouse drag does.
    SolveReport drag(int pointId, double x, double y, const ParamLookup& lookup = {});

    std::vector<GapMarker> openEndpoints(double nearTolerance = 2.0) const;

    // ---- access ---------------------------------------------------------
    const std::map<int, SketchEntity>& entities() const
    {
        return entities_;
    }
    const std::map<int, SketchConstraint>& constraints() const
    {
        return constraints_;
    }
    const SketchEntity* entity(int id) const;
    SketchEntity* entity(int id);
    // Point coordinates by point id (curves' endpoints are kept in sync on solve).
    std::pair<double, double> pointXY(int pointId) const;

    nlohmann::json toJson() const;
    static Result<Sketch> fromJson(const nlohmann::json& j);

private:
    SolveReport runSolver(const ParamLookup& lookup, const int* dragPoint, double dx, double dy,
                          const std::vector<int>& skip = {});
    void syncCurvePoints();

    std::map<int, SketchEntity> entities_;
    std::map<int, SketchConstraint> constraints_;
    int nextId_ = 1;
};

const char* toString(ConstraintType t);
std::optional<ConstraintType> constraintTypeFromString(std::string_view s);

}  // namespace jig
