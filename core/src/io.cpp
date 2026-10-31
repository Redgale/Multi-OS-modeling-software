// SPDX-License-Identifier: GPL-3.0-or-later
#include "jig/io.hpp"

#include <cstring>
#include <filesystem>
#include <memory>

#include <array>
#include <map>
#include <sstream>
#include <locale>
#include <cmath>
#include <ctime>
#include <miniz.h>
#include <TopoDS_Face.hxx>
#include <TopoDS.hxx>
#include <TopExp_Explorer.hxx>
#include <Poly_Triangulation.hxx>
#include <BRep_Tool.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepBuilderAPI_Copy.hxx>

#include "jig/version.hpp"

namespace jig
{
namespace
{

constexpr size_t kMaxJsonBytes = 64u * 1024u * 1024u;
constexpr const char* kFormatName = "jigsmith-design";

// Every "versions/…" entry of an existing design file (empty when there is none).
std::map<std::string, std::string> readVersionEntries(const std::string& path)
{
    std::map<std::string, std::string> out;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        return out;
    }
    const mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, i, &st) || st.m_uncomp_size > kMaxJsonBytes) {
            continue;
        }
        const std::string name = st.m_filename;
        if (name.rfind("versions/", 0) != 0) {
            continue;
        }
        std::string data(size_t(st.m_uncomp_size), '\0');
        if (mz_zip_reader_extract_to_mem(&zip, i, data.data(), data.size(), 0)) {
            out[name] = std::move(data);
        }
    }
    mz_zip_reader_end(&zip);
    return out;
}

Result<std::unique_ptr<Document>> readArchive(mz_zip_archive& zip)
{
    auto extract = [&](const char* name, std::string& out) -> bool {
        int idx = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
        if (idx < 0) {
            return false;
        }
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&zip, mz_uint(idx), &st) || st.m_uncomp_size > kMaxJsonBytes) {
            return false;
        }
        out.resize(size_t(st.m_uncomp_size));
        return mz_zip_reader_extract_to_mem(&zip, mz_uint(idx), out.data(), out.size(), 0) != 0;
    };
    std::string manifestText, docText;
    if (!extract("manifest.json", manifestText) || !extract("document.json", docText)) {
        return makeError("file.damaged",
                         "This file is not a Jigsmith design, or it is damaged.",
                         {"If you have a backup or an earlier version, open that instead."});
    }
    auto manifest = nlohmann::json::parse(manifestText, nullptr, false);
    if (manifest.is_discarded() || !manifest.is_object()
        || manifest.value("format", std::string {}) != kFormatName) {
        return makeError("file.damaged", "This file is not a Jigsmith design, or it is damaged.");
    }
    auto doc = nlohmann::json::parse(docText, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return makeError("file.damaged", "The design inside this file is damaged.");
    }
    return Document::fromJson(doc);
}

}  // namespace

std::string appVersion()
{
    return JIG_VERSION_STRING;
}

Result<bool> writeDesign(const Document& doc, const std::string& path, const std::string& thumbnailPng,
                         const std::map<std::string, std::string>& versions);

Result<bool> saveDocument(const Document& doc, const std::string& path, const std::string& thumbnailPng)
{
    // versions saved earlier in this file are kept
    return writeDesign(doc, path, thumbnailPng, readVersionEntries(path));
}

namespace
{
std::string nowUtc()
{
    const std::time_t t = std::time(nullptr);
    std::tm tm {};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}
}  // namespace

Result<int> saveVersion(const Document& doc, const std::string& path, const std::string& label)
{
    auto versions = readVersionEntries(path);
    nlohmann::json index = nlohmann::json::array();
    if (auto it = versions.find("versions/index.json"); it != versions.end()) {
        index = nlohmann::json::parse(it->second, nullptr, false);
        if (!index.is_array()) {
            index = nlohmann::json::array();
        }
    }
    int next = 1;
    for (auto& v : index) {
        next = std::max(next, v.value("number", 0) + 1);
    }
    const std::string body = doc.toJson().dump(2) + "\n";
    versions["versions/" + std::to_string(next) + ".json"] = body;
    index.push_back({{"number", next}, {"label", label}, {"saved", nowUtc()}, {"steps", doc.model().features.size()}});
    versions["versions/index.json"] = index.dump(2) + "\n";
    auto r = writeDesign(doc, path, {}, versions);  // saving a version also saves the design
    if (!r) {
        return r.error();
    }
    return next;
}

Result<std::vector<VersionInfo>> listVersions(const std::string& path)
{
    std::vector<VersionInfo> out;
    auto versions = readVersionEntries(path);
    auto it = versions.find("versions/index.json");
    if (it == versions.end()) {
        return out;
    }
    auto index = nlohmann::json::parse(it->second, nullptr, false);
    if (!index.is_array()) {
        return makeError("file.damaged", "The list of versions in this file is damaged.");
    }
    for (auto& v : index) {
        if (!v.is_object()) {
            continue;
        }
        out.push_back({v.value("number", 0), v.value("label", std::string {}), v.value("saved", std::string {}),
                       v.value("steps", 0)});
    }
    return out;
}

Result<std::unique_ptr<Document>> loadVersion(const std::string& path, int number)
{
    auto versions = readVersionEntries(path);
    auto it = versions.find("versions/" + std::to_string(number) + ".json");
    if (it == versions.end()) {
        return makeError("file.version_missing", "That version is not in this file.");
    }
    auto j = nlohmann::json::parse(it->second, nullptr, false);
    if (j.is_discarded()) {
        return makeError("file.damaged", "That version is damaged.");
    }
    return Document::fromJson(j);
}

Result<bool> writeDesign(const Document& doc, const std::string& path, const std::string& thumbnailPng,
                         const std::map<std::string, std::string>& versions)
{
    namespace fs = std::filesystem;
    nlohmann::json manifest {{"format", kFormatName},
                             {"format_version", kFormatVersion},
                             {"app", "Jigsmith"},
                             {"app_version", appVersion()}};
    const std::string m = manifest.dump(2) + "\n";
    const std::string d = doc.toJson().dump(2) + "\n";

    fs::path target(path);
    fs::path tmp = target;
    tmp += ".partial";
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_file(&zip, tmp.string().c_str(), 0)) {
        return makeError("file.write", "The design could not be saved here.",
                         {"Check that the folder exists and you can write to it."});
    }
    bool ok = mz_zip_writer_add_mem(&zip, "manifest.json", m.data(), m.size(), MZ_BEST_COMPRESSION)
           && mz_zip_writer_add_mem(&zip, "document.json", d.data(), d.size(), MZ_BEST_COMPRESSION);
    if (ok && !thumbnailPng.empty()) {
        ok = mz_zip_writer_add_mem(&zip, "thumbnail.png", thumbnailPng.data(), thumbnailPng.size(),
                                   MZ_NO_COMPRESSION);
    }
    for (auto& [name, data] : versions) {
        ok = ok && mz_zip_writer_add_mem(&zip, name.c_str(), data.data(), data.size(), MZ_BEST_COMPRESSION);
    }
    ok = ok && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    std::error_code ec;
    if (!ok) {
        fs::remove(tmp, ec);
        return makeError("file.write", "The design could not be saved (the disk may be full).");
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return makeError("file.write", "The design could not be saved here.", {}, ec.message());
    }
    return true;
}

Result<std::unique_ptr<Document>> loadDocument(const std::string& path)
{
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        return makeError("file.open", "This file could not be opened. It may be damaged or not a "
                                      "Jigsmith design.");
    }
    auto r = readArchive(zip);
    mz_zip_reader_end(&zip);
    return r;
}

Result<std::unique_ptr<Document>> loadDocumentFromMemory(const void* data, size_t size)
{
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_reader_init_mem(&zip, data, size, 0)) {
        return makeError("file.open", "This file could not be opened. It may be damaged or not a "
                                      "Jigsmith design.");
    }
    auto r = readArchive(zip);
    mz_zip_reader_end(&zip);
    return r;
}

// ---------------------------------------------------------------- 3MF

Mesh meshOf(const TopoDS_Shape& original, double deflection)
{
    Mesh m;
    if (original.IsNull()) {
        return m;
    }
    TopoDS_Shape s = BRepBuilderAPI_Copy(original, true, false).Shape();
    BRepMesh_IncrementalMesh mesher(s, deflection, false, 0.2, true);
    // Weld vertices shared by neighbouring faces: the mesher puts the same edge points
    // on both faces, so quantising to 1e-6 mm joins them exactly.
    std::map<std::array<long long, 3>, uint32_t> index;
    auto vertex = [&](const gp_Pnt& p) {
        const std::array<long long, 3> k {std::llround(p.X() * 1e6), std::llround(p.Y() * 1e6), std::llround(p.Z() * 1e6)};
        auto [it, added] = index.emplace(k, uint32_t(m.xyz.size() / 3));
        if (added) {
            m.xyz.insert(m.xyz.end(), {p.X(), p.Y(), p.Z()});
        }
        return it->second;
    };
    for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face& f = TopoDS::Face(ex.Current());
        TopLoc_Location loc;
        Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(f, loc);
        if (tri.IsNull()) {
            continue;
        }
        const gp_Trsf t = loc.Transformation();
        std::vector<uint32_t> ids(size_t(tri->NbNodes()) + 1);
        for (int i = 1; i <= tri->NbNodes(); ++i) {
            ids[size_t(i)] = vertex(tri->Node(i).Transformed(t));
        }
        const bool flip = f.Orientation() == TopAbs_REVERSED;
        for (int i = 1; i <= tri->NbTriangles(); ++i) {
            int a, b, c;
            tri->Triangle(i).Get(a, b, c);
            if (flip) {
                std::swap(b, c);
            }
            const uint32_t A = ids[size_t(a)], B = ids[size_t(b)], C = ids[size_t(c)];
            if (A != B && B != C && A != C) {  // drop triangles that welding collapsed
                m.triangles.insert(m.triangles.end(), {A, B, C});
            }
        }
    }
    return m;
}

Result<bool> export3mf(const std::vector<std::pair<std::string, TopoDS_Shape>>& bodies, const std::string& path,
                       double deflection)
{
    namespace fs = std::filesystem;
    if (bodies.empty()) {
        return makeError("export.empty", "There is nothing to export yet.");
    }
    auto xmlEscape = [](const std::string& in) {
        std::string out;
        for (char c : in) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                default: out += c;
            }
        }
        return out;
    };
    std::ostringstream x;
    x.imbue(std::locale::classic());  // always a decimal point, whatever the user's locale
    x.precision(9);
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      << "<model unit=\"millimeter\" xml:lang=\"en-US\" "
         "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
      << " <metadata name=\"Application\">Jigsmith " << appVersion() << "</metadata>\n"
      << " <resources>\n";
    int id = 0;
    try {
        for (auto& [name, shape] : bodies) {
            const Mesh m = meshOf(shape, deflection);
            if (m.triangles.empty()) {
                continue;
            }
            x << "  <object id=\"" << ++id << "\" type=\"model\" name=\"" << xmlEscape(name) << "\">\n   <mesh>\n"
              << "    <vertices>\n";
            for (size_t i = 0; i < m.xyz.size(); i += 3) {
                x << "     <vertex x=\"" << m.xyz[i] << "\" y=\"" << m.xyz[i + 1] << "\" z=\"" << m.xyz[i + 2] << "\"/>\n";
            }
            x << "    </vertices>\n    <triangles>\n";
            for (size_t i = 0; i < m.triangles.size(); i += 3) {
                x << "     <triangle v1=\"" << m.triangles[i] << "\" v2=\"" << m.triangles[i + 1] << "\" v3=\""
                  << m.triangles[i + 2] << "\"/>\n";
            }
            x << "    </triangles>\n   </mesh>\n  </object>\n";
        }
    }
    catch (const Standard_Failure&) {
        return makeError("export.3mf", "The 3MF file could not be created.");
    }
    if (id == 0) {
        return makeError("export.empty", "There is nothing to export yet.");
    }
    x << " </resources>\n <build>\n";
    for (int i = 1; i <= id; ++i) {
        x << "  <item objectid=\"" << i << "\"/>\n";
    }
    x << " </build>\n</model>\n";
    const std::string model = x.str();
    const std::string types =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
        " <Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
        " <Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
        "</Types>\n";
    const std::string rels =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
        " <Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
        "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
        "</Relationships>\n";

    fs::path target(path);
    fs::path tmp = target;
    tmp += ".partial";
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof zip);
    if (!mz_zip_writer_init_file(&zip, tmp.string().c_str(), 0)) {
        return makeError("export.write", "The file could not be written.",
                         {"Check that the folder exists and you can write to it."});
    }
    bool ok = mz_zip_writer_add_mem(&zip, "[Content_Types].xml", types.data(), types.size(), MZ_DEFAULT_COMPRESSION)
           && mz_zip_writer_add_mem(&zip, "_rels/.rels", rels.data(), rels.size(), MZ_DEFAULT_COMPRESSION)
           && mz_zip_writer_add_mem(&zip, "3D/3dmodel.model", model.data(), model.size(), MZ_DEFAULT_COMPRESSION)
           && mz_zip_writer_finalize_archive(&zip);
    mz_zip_writer_end(&zip);
    std::error_code ec;
    if (!ok) {
        fs::remove(tmp, ec);
        return makeError("export.write", "The file could not be written (the disk may be full).");
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return makeError("export.write", "The file could not be written.", {}, ec.message());
    }
    return true;
}

}  // namespace jig
