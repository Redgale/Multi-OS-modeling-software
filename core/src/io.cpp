// SPDX-License-Identifier: GPL-3.0-or-later
#include "jig/io.hpp"

#include <cstring>
#include <filesystem>
#include <memory>

#include <miniz.h>

#include "jig/version.hpp"

namespace jig
{
namespace
{

constexpr size_t kMaxJsonBytes = 64u * 1024u * 1024u;
constexpr const char* kFormatName = "jigsmith-design";

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

Result<bool> saveDocument(const Document& doc, const std::string& path, const std::string& thumbnailPng)
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

}  // namespace jig
