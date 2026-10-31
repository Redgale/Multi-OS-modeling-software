// SPDX-License-Identifier: GPL-3.0-or-later
// The .jig design file (brief 5.5): a ZIP container holding
//   manifest.json   format name, format version, app version
//   document.json   parameters + features, canonical key order (diffs cleanly)
//   thumbnail.png   optional
// Geometry is never stored as the source of truth; it is recomputed on open.
// Writes are atomic (temporary file, then rename).
#pragma once

#include <memory>
#include <string>

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

}  // namespace jig
