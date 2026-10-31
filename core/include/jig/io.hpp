// SPDX-License-Identifier: GPL-3.0-or-later
// The .jig design file (brief 5.5): a ZIP container holding
//   manifest.json   format name, format version, app version
//   document.json   parameters + features, canonical key order (diffs cleanly)
//   thumbnail.png   optional
// Geometry is never stored as the source of truth; it is recomputed on open.
// Writes are atomic (temporary file, then rename).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <TopoDS_Shape.hxx>

#include "jig/document.hpp"
#include "jig/result.hpp"

namespace jig
{

std::string appVersion();

Result<bool> saveDocument(const Document& doc,
                          const std::string& path,
                          const std::string& thumbnailPng = {});
Result<std::unique_ptr<Document>> loadDocument(const std::string& path);

// For tests and fuzzing: parse a container already in memory.
Result<std::unique_ptr<Document>> loadDocumentFromMemory(const void* data, size_t size);

// ---------------------------------------------------------------- 3MF

// A triangle mesh with shared (welded) vertices, oriented outward.
struct Mesh
{
    std::vector<double> xyz;          // 3 per vertex, millimetres
    std::vector<uint32_t> triangles;  // 3 vertex indices per triangle
};
// Tessellates a solid into a closed mesh suitable for slicers.
Mesh meshOf(const TopoDS_Shape& s, double deflection = 0.02);

// 3MF (3D Manufacturing Format) with one object per body, units in millimetres.
Result<bool> export3mf(const std::vector<std::pair<std::string, TopoDS_Shape>>& bodies, const std::string& path,
                       double deflection = 0.02);

}  // namespace jig
