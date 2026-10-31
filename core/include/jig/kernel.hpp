// SPDX-License-Identifier: GPL-3.0-or-later
// The only place Jigsmith talks to Open CASCADE. Every kernel call is wrapped:
// OCCT exceptions are caught here and turned into jig::Error with plain-language
// text (Easy Contract principle 11, architecture rule 5).
//
// Persistent naming (brief 5.4a, docs/DECISIONS.md D-005):
//   * every face of every body carries a name derived from the feature and role
//     that created it, e.g. "e1/side:7" (side wall made by extrude e1 from sketch
//     curve 7) or "e1/end" (the far cap of e1);
//   * names flow through later operations using OCCT's own history
//     (Modified / Generated / IsDeleted);
//   * edge names are derived from the names of the faces they separate;
//   * references store the name plus a geometric fingerprint, and are re-resolved
//     by fingerprint only when the name no longer matches, never silently.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <TopTools_DataMapOfShapeInteger.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax1.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <gp_XY.hxx>
#include <gp_Ax3.hxx>
#include <gp_Trsf.hxx>
#include <nlohmann/json.hpp>

#include "jig/result.hpp"
#include "jig/sketch.hpp"

namespace jig
{

// ---------------------------------------------------------------- naming

struct Fingerprint
{
    std::string type;          // "plane", "cylinder", "line", "circle", ...
    double cx = 0, cy = 0, cz = 0;  // centroid (face) or midpoint (edge)
    double size = 0;           // area (face) or length (edge)
    double nx = 0, ny = 0, nz = 0;  // outward normal (planar face) or direction (line edge)

    nlohmann::json toJson() const;
    static Fingerprint fromJson(const nlohmann::json& j);
};

enum class SubShape
{
    Face,
    Edge
};

// A stored reference to a face or edge of a body.
struct ShapeRef
{
    SubShape kind = SubShape::Edge;
    std::string body;  // body id
    std::string name;  // persistent name
    Fingerprint fp;

    nlohmann::json toJson() const;
    static ShapeRef fromJson(const nlohmann::json& j);
};

class NamedShape
{
public:
    NamedShape() = default;
    explicit NamedShape(TopoDS_Shape s);

    const TopoDS_Shape& shape() const
    {
        return shape_;
    }
    int faceCount() const
    {
        return faces_.Extent();
    }
    int edgeCount() const
    {
        return edges_.Extent();
    }
    // 0-based indices into the face / edge lists
    const TopoDS_Shape& face(int i) const
    {
        return faces_(i + 1);
    }
    const TopoDS_Shape& edge(int i) const
    {
        return edges_(i + 1);
    }
    const std::string& faceName(int i) const
    {
        return faceNames_[i];
    }
    const std::string& edgeName(int i) const
    {
        return edgeNames_[i];
    }
    int faceIndex(const TopoDS_Shape& f) const
    {
        return faces_.FindIndex(f) - 1;
    }
    int edgeIndex(const TopoDS_Shape& e) const
    {
        return edges_.FindIndex(e) - 1;
    }
    std::vector<int> findFaces(const std::string& name) const;
    std::vector<int> findEdges(const std::string& name) const;
    std::vector<int> edgesOfFace(int faceIdx) const;
    std::vector<int> facesOfEdge(int edgeIdx) const;

    Fingerprint faceFingerprint(int i) const;
    Fingerprint edgeFingerprint(int i) const;
    ShapeRef faceRef(const std::string& body, int i) const;
    ShapeRef edgeRef(const std::string& body, int i) const;

    // Set by the operations in this file.
    void setFaceName(int i, std::string n)
    {
        faceNames_[i] = std::move(n);
    }
    // Derive edge names from adjacent face names; must be called after all faces are named.
    void deriveEdgeNames();

private:
    TopoDS_Shape shape_;
    TopTools_IndexedMapOfShape faces_, edges_;
    std::vector<std::string> faceNames_, edgeNames_;
};

enum class ResolveStatus
{
    Exact,      // the persistent name matched exactly one sub-shape
    Rebound,    // the name was gone; re-attached by shape similarity (reported as a warning)
    Ambiguous,  // several candidates fit equally well: the user must choose
    Missing     // nothing suitable exists any more
};

struct Resolution
{
    ResolveStatus status = ResolveStatus::Missing;
    int index = -1;               // face or edge index in the body
    std::vector<int> candidates;  // for Ambiguous
};

Resolution resolve(const NamedShape& body, const ShapeRef& ref);

// ---------------------------------------------------------------- sketches

enum class OriginPlane
{
    XY,
    XZ,
    YZ
};

// The 3D placement of a sketch. Origin planes in a Z-up document:
// XY = floor ("Top"), XZ = front, YZ = right side.
gp_Ax3 originPlaneFrame(OriginPlane p, double offset);
// A frame on a planar face: normal = outward face normal, origin = projection of
// the world origin, x axis = world X projected (or world Y when X is normal).
Result<gp_Ax3> faceFrame(const TopoDS_Shape& face);

struct Profile
{
    std::string key;  // sorted ids of the sketch curves bounding the region, e.g. "3,4,5,6"
    TopoDS_Face face;  // in 3D, on the sketch plane
    double area = 0;
    TopTools_DataMapOfShapeInteger edgeEntity;  // profile edge -> sketch entity id
};

struct ProfileSet
{
    std::vector<Profile> regions;
    std::vector<std::string> notes;  // e.g. a missing font was replaced
};

// ---------------------------------------------------------------- SVG import

// Filled shapes of an SVG file as closed outlines, in millimetres, y up, centred on (0, 0).
struct SvgArtwork
{
    std::vector<ArtContour> contours;
    double width = 0, height = 0;  // mm
    bool unitsKnown = false;       // the file gives a physical size (mm, in, …)
    std::map<std::string, int> skipped;
};
Result<SvgArtwork> parseSvg(const std::string& text);
Result<SvgArtwork> readSvg(const std::string& path);
// The artwork's outlines as faces in the sketch plane, centred on (x, y).
Result<std::vector<TopoDS_Face>> artworkFaces(const SketchArtwork& a, double x, double y, const gp_Ax3& frame);

// ---------------------------------------------------------------- DXF import

// Curves read from a DXF file, in the file's own units (see unitScale).
struct DxfDrawing
{
    struct Line
    {
        double x1, y1, x2, y2;
    };
    struct Circle
    {
        double cx, cy, r;
    };
    struct Arc
    {
        double cx, cy, r, startDeg, endDeg;  // counter-clockwise, as DXF stores them
    };
    std::vector<Line> lines;
    std::vector<Circle> circles;
    std::vector<Arc> arcs;
    double unitScale = 1;     // millimetres per drawing unit
    bool unitsKnown = false;  // false when the file does not say ($INSUNITS unset or 0)
    std::map<std::string, int> skipped;  // entity types not imported, with counts
};
Result<DxfDrawing> parseDxf(const std::string& text);
Result<DxfDrawing> readDxf(const std::string& path);
// Adds the drawing's curves to a sketch, scaled (mm per drawing unit) and shifted; ends
// that meet share a point, so outlines close. Returns the number of curves added.
int addDxfToSketch(Sketch& s, const DxfDrawing& d, double scale, double dx = 0, double dy = 0);

// Offset copy of sketch curves: a single line (to its left for a positive distance), a
// single circle or arc (bigger radius for positive), or one closed outline (outward for
// positive, sharp corners). Adds the new curves to the sketch and returns their ids.
Result<std::vector<int>> offsetCurves(Sketch& sketch, const std::vector<int>& curves, double distance);

// Closed outlines made of straight segments (2 points) and quadratic / cubic Béziers
// (3 / 4 points) turned into flat faces; holes are found by nesting, whatever the winding.
using OutlineSegment = std::vector<gp_XY>;
using OutlineContour = std::vector<OutlineSegment>;
std::vector<TopoDS_Face> facesFromOutlines(const std::vector<OutlineContour>& contours,
                                           const std::function<gp_Pnt(const gp_XY&)>& to3d, const gp_Pln& plane);

// Glyph outlines of a sketch text as flat faces in the sketch plane, centred on (x, y).
struct TextOutline
{
    std::vector<TopoDS_Face> faces;
    std::string note;  // set when the requested font was not available
};
Result<TextOutline> textFaces(const SketchText& t, double x, double y, const gp_Ax3& frame);

// Finds every closed region of the sketch's non-construction curves. Each glyph
// outline of a sketch text is a closed region too (key "T<text id>.<n>"); text is
// not split by the other curves.
Result<ProfileSet> buildProfiles(const Sketch& sketch, const gp_Ax3& frame);

// ---------------------------------------------------------------- features

enum class BoolOp
{
    NewBody,
    Join,
    Cut,
    Intersect
};

enum class ExtrudeDirection
{
    OneSide,
    Reverse,
    Symmetric,
    TwoSides  // `distance` one way, `distance2` the other
};

struct ExtrudeInput
{
    std::string featureId;
    std::vector<const Profile*> profiles;
    gp_Dir normal;
    double distance = 10;  // mm, >0
    ExtrudeDirection direction = ExtrudeDirection::OneSide;
    double distance2 = 0;  // TwoSides: how far it goes the other way
    double taper = 0;      // draft angle in radians; positive narrows the shape as it goes
    double thin = 0;       // > 0: a wall of this thickness along the outline instead of a solid
    int thinSide = 0;      // 0 centred on the outline, -1 inside it, +1 outside it
};

// Builds the extruded tool body and names its faces.
Result<NamedShape> makeExtrusion(const ExtrudeInput& in);

// Spins profiles around an axis. angle in radians (2*pi = full turn). Faces are named
// "<feature>/side:<sketch curve>", plus "start"/"end" caps for partial turns.
Result<NamedShape> makeRevolution(const std::string& featureId,
                                  const std::vector<const Profile*>& profiles,
                                  const gp_Ax1& axis,
                                  double angle);

struct HoleSpec
{
    enum class Type
    {
        Simple,
        Counterbore,
        Countersink
    } type = Type::Simple;
    double diameter = 3;     // mm
    double depth = 10;       // mm, ignored when through
    bool through = false;
    double cboreDiameter = 6, cboreDepth = 3;
    double csinkDiameter = 6, csinkAngle = 1.5707963267948966;  // 90 degrees
};

struct HolePoint
{
    int id;       // sketch point id (stable): faces are named "<feature>/wall:p<id>" etc.
    double x, y;  // in the sketch frame
};

// The cutting tool for holes at sketch points, drilled along -normal of the frame.
Result<NamedShape> makeHoleTool(const std::string& featureId,
                                const gp_Ax3& frame,
                                const std::vector<HolePoint>& points,
                                const HoleSpec& spec,
                                double throughDepth);

// A moved/rotated/mirrored copy whose faces are renamed "<prefix><original name>".
NamedShape transformedCopy(const NamedShape& src, const gp_Trsf& t, const std::string& prefix);

// Several named shapes as one compound (names kept), e.g. pattern copies.
NamedShape namedCompound(const std::vector<NamedShape>& parts);

// Boolean between a named target and a named tool; names propagate from both.
Result<NamedShape> booleanOp(const std::string& featureId,
                             BoolOp op,
                             const NamedShape& target,
                             const NamedShape& tool);

// Fillet of resolved edges with a constant radius. On failure the Error carries a
// probable cause and, when one can be found, the largest radius that does work.
Result<NamedShape> filletEdges(const std::string& featureId,
                               const NamedShape& body,
                               const std::vector<int>& edgeIndices,
                               double radius);

// Equal-distance chamfer (bevel) of resolved edges; failures are diagnosed like fillets.
Result<NamedShape> chamferEdges(const std::string& featureId,
                                const NamedShape& body,
                                const std::vector<int>& edgeIndices,
                                double distance);

// Hollow the body with walls of `thickness` (inward), removing `openFaces` (may be
// empty: a closed hollow). Inner walls are named "<feature>/inner:<outer face>".
Result<NamedShape> shellSolid(const std::string& featureId,
                              const NamedShape& body,
                              const std::vector<int>& openFaces,
                              double thickness);

// True when the point lies strictly inside the solid (used to choose Join or Cut).
bool pointInside(const TopoDS_Shape& solid, double x, double y, double z);

// Edges connected to `edge` through smooth (tangent) joins, including `edge` itself.
std::vector<int> tangentChain(const NamedShape& body, int edge, double angleTolDeg = 2.0);

// ---------------------------------------------------------------- inspect

struct MassProperties
{
    double volume = 0;  // mm^3
    double area = 0;    // mm^2
    double cx = 0, cy = 0, cz = 0;
    double xmin = 0, ymin = 0, zmin = 0, xmax = 0, ymax = 0, zmax = 0;
    int faces = 0, edges = 0, solids = 0;
    bool valid = false;  // BRepCheck_Analyzer result
};

MassProperties massProperties(const TopoDS_Shape& s);

// ---------------------------------------------------------------- measure

// What one picked face or edge is, in plain words, with its size.
struct MeasureItem
{
    std::string kind;       // "flat face", "round face", "straight edge", "circle", "arc", "curved face", …
    double size = 0;        // area (faces, mm^2) or length (edges, mm)
    std::optional<double> radius;            // circles, arcs, cylinders, cones at their base
    std::optional<gp_Ax1> axis;              // circles and cylinders: centre and axis; lines: direction
    std::optional<gp_Dir> normal;            // flat faces
};
MeasureItem measureItem(const TopoDS_Shape& faceOrEdge);

struct MeasureBetween
{
    double distance = 0;            // shortest distance (0 when they touch)
    gp_Pnt p1, p2;                  // where it is measured
    std::optional<double> angle;    // radians, 0..pi/2, for flat faces and straight edges
    std::optional<double> centres;  // centre-to-centre distance of two round things with parallel axes
};
Result<MeasureBetween> measureBetween(const TopoDS_Shape& a, const TopoDS_Shape& b);

// ---------------------------------------------------------------- display

struct DisplayMesh
{
    std::vector<float> positions;  // xyz per vertex
    std::vector<float> normals;    // xyz per vertex
    std::vector<uint32_t> indices; // 3 per triangle
    std::vector<uint32_t> triFace; // face index per triangle
    std::vector<float> edgeLines;  // xyz pairs, line segments
    std::vector<uint32_t> segEdge; // edge index per segment
};

// Thread-safe for distinct shapes; used off the UI thread.
DisplayMesh tessellate(const TopoDS_Shape& s, double linearDeflection = 0.05);

// ---------------------------------------------------------------- interchange

Result<bool> exportStl(const TopoDS_Shape& s, const std::string& path, double deflection = 0.02);
Result<bool> exportStep(const TopoDS_Shape& s, const std::string& path);
struct ImportedShape
{
    TopoDS_Shape shape;
    std::string fileUnit;  // unit declared by the file, normalised: "mm", "cm", "m", "in", "ft"
};
Result<ImportedShape> importStep(const std::string& path);

}  // namespace jig
