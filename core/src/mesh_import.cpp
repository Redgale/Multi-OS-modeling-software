// SPDX-License-Identifier: GPL-3.0-or-later
// Meshes in (STL, OBJ, 3MF): read into a triangle list, turned into a solid by sewing the
// triangles together, and kept inside the design as an Import step.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <tuple>

#include <miniz.h>

#include <BRep_Tool.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <ShapeFix_Solid.hxx>
#include <TopoDS_Wire.hxx>
#include <BRepFill_Filling.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Shell.hxx>
#include <gp_Pnt.hxx>

#include "jig/document.hpp"
#include "jig/files.hpp"
#include "jig/io.hpp"
#include "jig/kernel.hpp"
#include "jig/result.hpp"

namespace jig
{
namespace
{
constexpr size_t kMaxMeshBytes = 256u << 20;

std::string lowerExt(const std::string& path)
{
    std::string e = utf8FromPath(pathFromUtf8(path).extension());
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return e;
}

// Welds equal corners so the mesh shares its vertices.
struct Welder
{
    Mesh mesh;
    std::map<std::tuple<double, double, double>, uint32_t> index;
    uint32_t add(double x, double y, double z)
    {
        auto [it, fresh] = index.try_emplace({x, y, z}, uint32_t(mesh.xyz.size() / 3));
        if (fresh) {
            mesh.xyz.insert(mesh.xyz.end(), {x, y, z});
        }
        return it->second;
    }
    void triangle(uint32_t a, uint32_t b, uint32_t c)
    {
        if (a != b && b != c && a != c) {
            mesh.triangles.insert(mesh.triangles.end(), {a, b, c});
        }
    }
};

Result<Mesh> readStl(const std::string& data)
{
    Welder w;
    // binary: an 80-byte header, a count, then 50 bytes a triangle; anything else is text
    if (data.size() >= 84) {
        uint32_t n = 0;
        std::memcpy(&n, data.data() + 80, 4);
        if (size_t(n) * 50 + 84 == data.size()) {
            for (uint32_t t = 0; t < n; ++t) {
                const char* p = data.data() + 84 + size_t(t) * 50 + 12;
                uint32_t v[3];
                for (int k = 0; k < 3; ++k) {
                    float xyz[3];
                    std::memcpy(xyz, p + k * 12, 12);
                    if (!std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2])) {
                        return makeError("mesh.read", "This STL file has damaged numbers in it.");
                    }
                    v[k] = w.add(xyz[0], xyz[1], xyz[2]);
                }
                w.triangle(v[0], v[1], v[2]);
            }
            return w.mesh;
        }
    }
    std::istringstream in(data);
    in.imbue(std::locale::classic());
    std::string word;
    std::vector<uint32_t> corner;
    while (in >> word) {
        if (word == "vertex") {
            double x, y, z;
            if (!(in >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                return makeError("mesh.read", "This STL file could not be read: a corner has no proper numbers.");
            }
            corner.push_back(w.add(x, y, z));
        }
        else if (word == "endloop") {
            for (size_t k = 2; k < corner.size(); ++k) {
                w.triangle(corner[0], corner[k - 1], corner[k]);
            }
            corner.clear();
        }
    }
    if (w.mesh.triangles.empty()) {
        return makeError("mesh.read", "This file has no triangles, or is not an STL file.");
    }
    return w.mesh;
}

Result<Mesh> readObj(const std::string& data)
{
    Welder w;
    std::vector<uint32_t> verts;
    std::istringstream in(data);
    in.imbue(std::locale::classic());
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        ls.imbue(std::locale::classic());
        std::string tag;
        ls >> tag;
        if (tag == "v") {
            double x, y, z;
            if (!(ls >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                return makeError("mesh.read", "This OBJ file could not be read: a corner has no proper numbers.");
            }
            verts.push_back(w.add(x, y, z));
        }
        else if (tag == "f") {
            std::vector<uint32_t> face;
            std::string item;
            while (ls >> item) {
                const long i = std::strtol(item.c_str(), nullptr, 10);  // "7", "7/2", "7//3"
                const long k = i > 0 ? i - 1 : long(verts.size()) + i;   // negative: from the end
                if (i == 0 || k < 0 || k >= long(verts.size())) {
                    return makeError("mesh.read", "This OBJ file refers to a corner that is not there.");
                }
                face.push_back(verts[size_t(k)]);
            }
            for (size_t k = 2; k < face.size(); ++k) {
                w.triangle(face[0], face[k - 1], face[k]);
            }
        }
    }
    if (w.mesh.triangles.empty()) {
        return makeError("mesh.read", "This file has no faces, or is not an OBJ file.");
    }
    return w.mesh;
}

// The value of attribute `name` in an XML start tag.
bool attribute(const std::string& tag, const char* name, std::string& out)
{
    const std::string key = std::string(" ") + name + "=";
    size_t p = tag.find(key);
    if (p == std::string::npos) {
        return false;
    }
    p += key.size();
    if (p >= tag.size() || (tag[p] != '"' && tag[p] != '\'')) {
        return false;
    }
    const char q = tag[p];
    const size_t e = tag.find(q, p + 1);
    if (e == std::string::npos) {
        return false;
    }
    out = tag.substr(p + 1, e - p - 1);
    return true;
}

Result<Mesh> read3mf(const std::string& data)
{
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem(&zip, data.data(), data.size(), 0)) {
        return makeError("mesh.read", "This 3MF file could not be opened (it is not a zip archive).");
    }
    std::string model;
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_uncomp_size > kMaxMeshBytes) {
            continue;
        }
        std::string name = st.m_filename;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (name.size() > 6 && name.compare(name.size() - 6, 6, ".model") == 0) {
            model.assign(size_t(st.m_uncomp_size), '\0');
            if (!mz_zip_reader_extract_to_mem(&zip, i, model.data(), model.size(), 0)) {
                model.clear();
            }
            break;
        }
    }
    mz_zip_reader_end(&zip);
    if (model.empty()) {
        return makeError("mesh.read", "This 3MF file has no model in it.");
    }
    double scale = 1;  // the model's unit, in millimetres
    if (size_t m = model.find("<model"); m != std::string::npos) {
        std::string unit;
        if (attribute(model.substr(m, model.find('>', m) - m), "unit", unit)) {
            scale = unit == "inch" ? 25.4 : unit == "centimeter" ? 10 : unit == "meter" ? 1000 : unit == "micron" ? 0.001
                    : unit == "foot"                                                    ? 304.8
                                                                                         : 1;
        }
    }
    Welder w;
    // each <mesh> block: its <vertex x= y= z=/> list, then its <triangle v1= v2= v3=/> list
    size_t pos = 0;
    while ((pos = model.find("<mesh", pos)) != std::string::npos) {
        const size_t end = model.find("</mesh>", pos);
        if (end == std::string::npos) {
            break;
        }
        std::vector<uint32_t> verts;
        for (size_t t = model.find('<', pos + 1); t != std::string::npos && t < end; t = model.find('<', t + 1)) {
            const size_t close = model.find('>', t);
            const std::string tag = model.substr(t, close - t);
            std::string a, b, c;
            if (tag.rfind("<vertex", 0) == 0 && attribute(tag, "x", a) && attribute(tag, "y", b) && attribute(tag, "z", c)) {
                const double x = std::strtod(a.c_str(), nullptr), y = std::strtod(b.c_str(), nullptr),
                             z = std::strtod(c.c_str(), nullptr);
                if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
                    return makeError("mesh.read", "This 3MF file has damaged numbers in it.");
                }
                verts.push_back(w.add(x * scale, y * scale, z * scale));
            }
            else if (tag.rfind("<triangle", 0) == 0 && attribute(tag, "v1", a) && attribute(tag, "v2", b) &&
                     attribute(tag, "v3", c)) {
                const long i = std::strtol(a.c_str(), nullptr, 10), j = std::strtol(b.c_str(), nullptr, 10),
                           k = std::strtol(c.c_str(), nullptr, 10);
                const long nv = long(verts.size());
                if (i < 0 || j < 0 || k < 0 || i >= nv || j >= nv || k >= nv) {
                    return makeError("mesh.read", "This 3MF file refers to a corner that is not there.");
                }
                w.triangle(verts[size_t(i)], verts[size_t(j)], verts[size_t(k)]);
            }
        }
        pos = end;
    }
    if (w.mesh.triangles.empty()) {
        return makeError("mesh.read", "This 3MF file has no triangles in it.");
    }
    return w.mesh;
}
}  // namespace

Result<Mesh> readMeshFile(const std::string& path)
{
    const std::string ext = lowerExt(path);
    if (ext != ".stl" && ext != ".obj" && ext != ".3mf") {
        return makeError("mesh.kind", "Jigsmith reads meshes from STL, OBJ and 3MF files.");
    }
    const auto data = readWholeFile(path, kMaxMeshBytes);
    if (!data) {
        return makeError("mesh.read", "The file could not be read (it is missing, unreadable or larger than 256 MB).");
    }
    return ext == ".stl" ? readStl(*data) : ext == ".obj" ? readObj(*data) : read3mf(*data);
}

Result<TopoDS_Shape> solidFromMesh(const Mesh& mesh, size_t maxTriangles, bool repair, int* holesClosed)
{
    const size_t n = mesh.triangles.size() / 3;
    if (n == 0) {
        return makeError("mesh.empty", "This mesh has no triangles.");
    }
    if (n > maxTriangles) {
        return makeError("mesh.big",
                         fmt("This mesh has %1 triangles; Jigsmith turns meshes of up to %2 into solids.",
                             {std::to_string(n), std::to_string(maxTriangles)}),
                         {"Reduce the mesh in another program first (for example, decimate it in a slicer or "
                          "mesh editor), then insert it again."});
    }
    try {
        auto pt = [&](uint32_t i) { return gp_Pnt(mesh.xyz[3 * i], mesh.xyz[3 * i + 1], mesh.xyz[3 * i + 2]); };
        // sewing tolerance: a little of the mesh's size, so slightly open seams still close
        double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
        for (size_t i = 0; i < mesh.xyz.size(); ++i) {
            lo[i % 3] = std::min(lo[i % 3], mesh.xyz[i]);
            hi[i % 3] = std::max(hi[i % 3], mesh.xyz[i]);
        }
        const double size = std::hypot(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
        BRepBuilderAPI_Sewing sew(std::max(1e-6, size * 1e-6));
        size_t made = 0;
        for (size_t t = 0; t < n; ++t) {
            const gp_Pnt a = pt(mesh.triangles[3 * t]), b = pt(mesh.triangles[3 * t + 1]), c = pt(mesh.triangles[3 * t + 2]);
            if (gp_Vec(a, b).Crossed(gp_Vec(a, c)).Magnitude() < 1e-12 * std::max(1.0, size * size)) {
                continue;  // a sliver with no area
            }
            BRepBuilderAPI_MakePolygon poly(a, b, c, true);
            BRepBuilderAPI_MakeFace face(poly.Wire(), true);
            if (face.IsDone()) {
                sew.Add(face.Face());
                ++made;
            }
        }
        if (made < 4) {
            return makeError("mesh.open", "This mesh is too small to close into a solid.");
        }
        sew.Perform();
        TopoDS_Shape sewn = sew.SewedShape();
        // repair: each hole (a closed loop of free edges) gets a face across it, then sew again
        if (holesClosed) {
            *holesClosed = 0;
        }
        bool open = false;
        for (TopExp_Explorer ex(sewn, TopAbs_SHELL); ex.More(); ex.Next()) {
            open = open || !(ex.Current().Closed() || BRep_Tool::IsClosed(ex.Current()));
        }
        if (open && repair) {
            ShapeAnalysis_FreeBounds bounds(sewn, std::max(1e-6, size * 1e-6), false, false);
            BRepBuilderAPI_Sewing again(std::max(1e-6, size * 1e-6));
            again.Add(sewn);
            int patched = 0;
            for (TopExp_Explorer w(bounds.GetClosedWires(), TopAbs_WIRE); w.More(); w.Next()) {
                const TopoDS_Wire loop = TopoDS::Wire(w.Current());
                BRepBuilderAPI_MakeFace flat(loop, true);
                if (flat.IsDone()) {
                    again.Add(flat.Face());
                    ++patched;
                    continue;
                }
                BRepFill_Filling fill;  // a hole that is not flat: a smooth patch across it
                for (TopExp_Explorer e(loop, TopAbs_EDGE); e.More(); e.Next()) {
                    fill.Add(TopoDS::Edge(e.Current()), GeomAbs_C0);
                }
                fill.Build();
                if (fill.IsDone()) {
                    again.Add(fill.Face());
                    ++patched;
                }
            }
            if (patched > 0) {
                again.Perform();
                sewn = again.SewedShape();
                if (holesClosed) {
                    *holesClosed = patched;
                }
            }
        }
        TopoDS_Shape result;
        BRepBuilderAPI_MakeSolid solid;
        int shells = 0;
        for (TopExp_Explorer ex(sewn, TopAbs_SHELL); ex.More(); ex.Next()) {
            if (!ex.Current().Closed() && !BRep_Tool::IsClosed(ex.Current())) {
                return makeError("mesh.open", repair ? "This mesh has holes in it that could not be closed, so it does not enclose a solid."
                                                     : "This mesh has holes in it, so it does not enclose a solid.",
                                 {"Repair the mesh in another program (close its holes), then insert it again."});
            }
            solid.Add(TopoDS::Shell(ex.Current()));
            ++shells;
        }
        if (shells == 0) {
            return makeError("mesh.open", "This mesh's triangles do not join into a closed skin.",
                             {"Repair the mesh in another program, then insert it again."});
        }
        ShapeFix_Solid fix(solid.Solid());
        fix.Perform();
        result = fix.Solid();
        GProp_GProps gp;
        BRepGProp::VolumeProperties(result, gp);
        if (gp.Mass() < 0) {
            result.Reverse();
        }
        // flat neighbours become one face, so a box comes in with six faces
        ShapeUpgrade_UnifySameDomain unify(result, true, true, false);
        unify.Build();
        if (!unify.Shape().IsNull()) {
            result = unify.Shape();
        }
        return result;
    }
    catch (const Standard_Failure& f) {
        const char* m = f.GetMessageString();
        return makeError("mesh.solid", "This mesh could not be turned into a solid.", {}, m ? m : "");
    }
}

Result<std::string> insertMesh(Document& doc, const std::string& path, double unitMm, std::string id)
{
    auto mesh = readMeshFile(path);
    if (!mesh) {
        return mesh.error();
    }
    if (!(unitMm > 0)) {
        return makeError("mesh.unit", "The mesh's unit must be more than zero.");
    }
    for (double& v : mesh->xyz) {
        v *= unitMm;
    }
    int closed = 0;
    auto solid = solidFromMesh(*mesh, 20000, true, &closed);
    if (!solid) {
        return solid.error();
    }
    auto bodies = importedBodies("check", solid.value());
    if (!bodies) {
        return bodies.error();
    }
    const std::string file = utf8FromPath(pathFromUtf8(path).filename());
    const std::string brep = shapeToText(solid.value());
    auto r = doc.transact("Insert " + file, [&](Model& m) -> Result<bool> {
        Feature f;
        f.id = id.empty() ? Document::newId(m, "ms") : id;
        if (Document::findFeature(m, f.id)) {
            return makeError("script.args", fmt("The id '%1' is already used.", {f.id}));
        }
        f.name = file;
        f.kind = FeatureKind::Import;
        f.params = {{"file", file}, {"from", "mesh"}, {"triangles", mesh->triangles.size() / 3}, {"brep", brep}};
        if (closed > 0) {
            f.params["holes_closed"] = closed;  // said in the step's status
        }
        id = f.id;
        Document::insertAtMarker(m, std::move(f));
        return true;
    });
    if (!r) {
        return r.error();
    }
    return id;
}
}  // namespace jig
