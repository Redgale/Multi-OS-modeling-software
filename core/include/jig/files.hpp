// SPDX-License-Identifier: GPL-3.0-or-later
// File paths are UTF-8 strings everywhere in Jigsmith (the app hands Qt's paths over as
// UTF-8). On Windows a std::string path is read in the system code page instead, so a design
// in "C:\Users\José\…" could not be opened or saved; these helpers make the path from its
// UTF-8 text on every system, and read and write whole files through such paths.
#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace jig
{
inline std::filesystem::path pathFromUtf8(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

inline std::string utf8FromPath(const std::filesystem::path& p)
{
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

// The whole file, or nothing when it cannot be read or is larger than `maxBytes`.
inline std::optional<std::string> readWholeFile(const std::string& utf8Path, std::size_t maxBytes)
{
    std::error_code ec;
    const auto p = pathFromUtf8(utf8Path);
    const auto size = std::filesystem::file_size(p, ec);
    if (ec || size > maxBytes) {
        return std::nullopt;
    }
    std::ifstream f(p, std::ios::binary);
    if (!f) {
        return std::nullopt;
    }
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return data;
}

inline bool writeWholeFile(const std::filesystem::path& p, const void* data, std::size_t size)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    if (!f) {
        return false;
    }
    f.write(static_cast<const char*>(data), std::streamsize(size));
    f.close();
    return !f.fail();
}
}  // namespace jig
