// SPDX-License-Identifier: GPL-3.0-or-later
// Sketch model and the planegcs solver wrapper (brief 5.4c, D-004).
//
// Entities and constraints carry stable integer ids that are never reused inside
// a sketch; 3D features refer to sketch geometry through these ids, which is the
// root of persistent naming (D-005).
#pragma once

#include <array>
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
    Arc,
    Ellipse,  // center, p1 = end of the major axis (sets its length and direction), r = minor radius
    Spline    // a smooth curve through the fit points `pts` (closed: back round to the first)
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
    std::vector<int> pts;   // spline fit points, in order
    bool closed = false;    // spline: runs back to its first point
};

// A spline's shape as cubic Bézier pieces (each: start, two handles, end), one per pair of
// neighbouring fit points: a smooth curve through every fit point (Catmull-Rom).
using Bezier2 = std::array<std::pair<double, double>, 4>;

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

// Lettering placed in a sketch. The text is centred on its anchor point (a normal,
// draggable and dimensionable sketch point); its glyph outlines become closed areas.
struct SketchText
{
    int id = 0;
    int anchor = 0;          // point entity id
    std::string text;        // UTF-8
    std::string font;        // family name; empty = the bundled font (Inter)
    std::string height = "5 mm";  // expression: the font size (about 1.4 x capital height)
    double heightValue = 5;  // evaluated on solve
    double angle = 0;        // radians, counter-clockwise
    int path = 0;            // a curve the text follows (0: straight)
    // that curve as points, filled in before the outlines are made (not saved)
    std::vector<std::pair<double, double>> pathPts;
};

// Imported artwork (an SVG logo): closed outlines of straight segments and Béziers,
// centred on an anchor point and scaled to a width. Like text, its outlines become
// closed areas; the curves themselves are not editable in the sketch.
using ArtSegment = std::vector<std::array<double, 2>>;  // 2 points: line; 3: quadratic; 4: cubic
using ArtContour = std::vector<ArtSegment>;
struct SketchArtwork
{
    int id = 0;
    int anchor = 0;                    // point entity id (the artwork's centre)
    std::vector<ArtContour> contours;  // at natural size in mm, centred on (0, 0)
    double naturalWidth = 1;           // mm
    std::string width;                 // expression; empty = natural size
    double widthValue = 0;             // evaluated on solve
    std::string name;
    double scale() const
    {
        return width.empty() || naturalWidth <= 0 ? 1.0 : widthValue / naturalWidth;
    }
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
    // Ellipse from its centre, the end of its major axis and the minor radius.
    int addEllipse(double cx, double cy, double ax, double ay, double minor);
    // Smooth curve through the given points (at least two; three for a closed one).
    int addSpline(const std::vector<std::pair<double, double>>& points, bool closed = false);
    std::vector<Bezier2> splinePieces(int splineId) const;
    // Points along any curve, for drawing and picking (a line gives its two ends).
    std::vector<std::pair<double, double>> curvePoints(int entityId, int perPiece = 24) const;
    // Composite shapes with the constraints that keep them in shape (but no sizes).
    struct SlotIds
    {
        int arc1 = 0, arc2 = 0, side1 = 0, side2 = 0;  // arc1 at (x2, y2), arc2 at (x1, y1)
    };
    // Centre-to-centre slot of radius r: two equal arcs joined by tangent lines.
    SlotIds addSlot(double x1, double y1, double x2, double y2, double r);
    struct PolygonIds
    {
        int circle = 0;              // construction circle through the corners
        std::vector<int> sides;      // lines, all equal
    };
    // Regular polygon with n corners on a circle of radius r; first corner at angle a0.
    PolygonIds addPolygon(double cx, double cy, double r, int n, double a0);
    // Text centred at (x, y); returns the text id (its anchor is a new point).
    int addText(double x, double y, std::string text, std::string heightExpr, double heightValue,
                std::string font = {}, double angle = 0);
    // Artwork centred at (x, y); returns its id (its anchor is a new point).
    int addArtwork(double x, double y, std::vector<ArtContour> contours, double naturalWidth, std::string widthExpr,
                   double widthValue, std::string name);
    const std::map<int, SketchArtwork>& artworks() const
    {
        return artworks_;
    }
    SketchArtwork* artwork(int id)
    {
        auto it = artworks_.find(id);
        return it == artworks_.end() ? nullptr : &it->second;
    }
    // Removes a curve, point or text. Also removes dependent constraints, internal
    // points, and text anchored to a removed point.
    bool remove(int entityId);
    void setConstruction(int entityId, bool on);

    // ---- trim -------------------------------------------------------------
    // The piece of a curve between its nearest crossings with other curves, around the
    // point (x, y) on it. Lines use the parameter 0..1 from p1 to p2; circles and arcs use
    // angles (radians), the piece running counter-clockwise from `from` to `to`.
    struct TrimPiece
    {
        int entity = 0;
        bool whole = false;  // nothing crosses it there: the whole curve goes
        double from = 0, to = 0;
    };
    std::optional<TrimPiece> trimPiece(int entityId, double x, double y) const;
    // Removes that piece. New ends stay attached to the curves that cut them (a
    // point-on-curve constraint, or the existing point when they meet at one). Length
    // and diameter dimensions of the trimmed curve that no longer apply are removed.
    bool trim(int entityId, double x, double y);
    // Lengthens the loose end of a line or arc nearest (x, y) until it meets another curve.
    // Returns the moved end's point id, or why it cannot.
    Result<int> extend(int entityId, double x, double y);
    // Splits a curve where other curves cross it, either side of (x, y): the pieces share
    // their new ends. Returns the ids of the curves that make up the old one.
    Result<std::vector<int>> breakAt(int entityId, double x, double y);
    // Mirror copies of curves and points across a line, held mirrored by symmetric
    // constraints (points on the line are shared). Returns the new ids.
    Result<std::vector<int>> mirror(const std::vector<int>& ids, int axisLine);
    // Copies curves and points turned by `angle` (radians) around (cx, cy), then shifted by
    // (dx, dy). Constraints among the copied items come along (horizontal/vertical only
    // when the turn keeps them); the copy is not linked to the original. Returns new ids.
    Result<std::vector<int>> copyCurves(const std::vector<int>& ids, double dx, double dy, double angle = 0,
                                        double cx = 0, double cy = 0);
    // Moves them instead (their end points move, so joined curves stretch to follow).
    Result<bool> moveCurves(const std::vector<int>& ids, double dx, double dy, double angle = 0, double cx = 0,
                            double cy = 0);
    // Scales curves and points by `factor` about (cx, cy): positions, radii and the lengths of
    // dimensions among them (a dimension's value becomes "(value) * factor"). Fixed points
    // move with them.
    Result<bool> scaleCurves(const std::vector<int>& ids, double factor, double cx = 0, double cy = 0);
    // Copies in rows and columns: n1 along (dx1, dy1), n2 along (dx2, dy2); counts include
    // the original. Copies around (cx, cy): n in all, spread over `total` (a full turn
    // spaces them evenly).
    Result<std::vector<int>> patternRect(const std::vector<int>& ids, int n1, double dx1, double dy1, int n2,
                                         double dx2, double dy2);
    Result<std::vector<int>> patternCircular(const std::vector<int>& ids, double cx, double cy, int n, double total);
    // Rounds the corner where two lines meet nearest (x, y) with a tangent arc of radius r
    // (`radiusExpr` is stored as its dimension). Returns the arc id, or an error message.
    Result<int> filletCorner(double x, double y, double r, const std::string& radiusExpr);
    // Curves connected end to end with `curve` (sharing an end point), including itself.
    std::vector<int> chainOf(int curve) const;
    // Counter-clockwise arc on existing points (used when building offsets).
    int addArcOn(int center, int p1, int p2, double r, double a0, double a1);
    // The curve nearest to (x, y) (not points; construction only when asked), and its distance.
    std::optional<std::pair<int, double>> curveNear(double x, double y, bool construction = false) const;

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
    const std::map<int, SketchText>& texts() const
    {
        return texts_;
    }
    SketchText* text(int id);
    const SketchEntity* entity(int id) const;
    SketchEntity* entity(int id);
    // Point coordinates by point id (curves' endpoints are kept in sync on solve).
    std::pair<double, double> pointXY(int pointId) const;

    // Projected edges that follow the model: each link holds the edge's persistent reference
    // ("ref"), what was made ("kind": line, circle, arc), the fixed points it moves ("points")
    // and, for a circle, its radius constraint ("radius"). Kept with the sketch.
    const std::vector<nlohmann::json>& links() const
    {
        return links_;
    }
    void addLink(nlohmann::json link)
    {
        links_.push_back(std::move(link));
    }
    // An offset copy that follows its source: rebuilt after every solve from the source curves
    // and the distance expression, held by fixed points (it adds no freedom). Returns the copy.
    Result<std::vector<int>> offsetLinked(const std::vector<int>& curves, const std::string& distanceExpr,
                                          const ParamLookup& lookup);
    // Grid or ring copies that follow their source, rebuilt after every solve like a linked
    // offset. `spec`: {"kind": "grid", "n1", "s1" (spacing along X), "n2", "s2" (along Y)} or
    // {"kind": "ring", "n", "angle", "center": [x, y], optional "center_point": a point id
    // the centre follows}; sizes and angles are expressions. Returns the copies.
    Result<std::vector<int>> patternLinked(const std::vector<int>& curves, const nlohmann::json& spec,
                                           const ParamLookup& lookup);
    // Moves a fixed point (its Fix constraint) to (x, y); false if it has none.
    bool moveFixedPoint(int pointId, double x, double y);
    // Changes a dimension's value text (a projected circle's radius).
    bool setConstraintExpr(int constraintId, std::string expr);

    nlohmann::json toJson() const;
    static Result<Sketch> fromJson(const nlohmann::json& j);

private:
    std::vector<nlohmann::json> links_;
    // Rebuilds linked offsets from their sources' solved places; true if any was rebuilt.
    bool rebuildOffsets(const ParamLookup& lookup);
    SolveReport runSolver(const ParamLookup& lookup, const int* dragPoint, double dx, double dy,
                          const std::vector<int>& skip = {});
    void syncCurvePoints();
    void dropUnusedPoint(int pointId);
    int attachedPoint(int skipEntity, double px, double py);
    // Cuts an ellipse or smooth curve where other curves cross it: drops the piece nearest
    // (x, y) (trim), or keeps them all (break). The kept pieces are smooth curves through
    // points along the original; its own end points stay.
    bool splitFreeForm(int entityId, double x, double y, bool keepAll, std::vector<int>* made);
    int crossingCurveOf(int pointId) const;
    void dropSizeConstraints(int entityId);

    std::map<int, SketchEntity> entities_;
    std::map<int, SketchConstraint> constraints_;
    std::map<int, SketchText> texts_;
    std::map<int, SketchArtwork> artworks_;
    int nextId_ = 1;
};

const char* toString(ConstraintType t);
std::optional<ConstraintType> constraintTypeFromString(std::string_view s);

}  // namespace jig
