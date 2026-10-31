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
#include <optional>
#include <string>
#include <vector>

#include <TopTools_DataMapOfShapeInteger.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax3.hxx>
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
};

// Finds every closed region of the sketch's non-construction curves.
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
    Symmetric
};

struct ExtrudeInput
{
    std::string featureId;
    std::vector<const Profile*> profiles;
    gp_Dir normal;
    double distance = 10;  // mm, >0
    ExtrudeDirection direction = ExtrudeDirection::OneSide;
};

// Builds the extruded tool body and names its faces.
Result<NamedShape> makeExtrusion(const ExtrudeInput& in);

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
