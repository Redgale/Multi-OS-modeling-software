// SPDX-License-Identifier: GPL-3.0-or-later
#include "jig/picture.hpp"

#include <cstdint>

#include "jig/document.hpp"
#include "jig/files.hpp"

namespace jig
{
std::string base64Encode(std::string_view in)
{
    static const char* abc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const uint32_t v = uint32_t(uint8_t(in[i])) << 16 | uint32_t(uint8_t(in[i + 1])) << 8 | uint8_t(in[i + 2]);
        out += abc[v >> 18 & 63];
        out += abc[v >> 12 & 63];
        out += abc[v >> 6 & 63];
        out += abc[v & 63];
    }
    if (i < in.size()) {
        const bool two = i + 1 < in.size();
        const uint32_t v = uint32_t(uint8_t(in[i])) << 16 | (two ? uint32_t(uint8_t(in[i + 1])) << 8 : 0u);
        out += abc[v >> 18 & 63];
        out += abc[v >> 12 & 63];
        out += two ? abc[v >> 6 & 63] : '=';
        out += '=';
    }
    return out;
}

std::optional<std::pair<int, int>> pictureSize(std::string_view b)
{
    auto u8 = [&](size_t i) { return uint32_t(uint8_t(b[i])); };
    // PNG: signature, then the IHDR chunk with width and height (big-endian)
    if (b.size() >= 24 && b.substr(0, 8) == std::string_view("\x89PNG\r\n\x1a\n", 8) && b.substr(12, 4) == "IHDR") {
        const uint32_t w = u8(16) << 24 | u8(17) << 16 | u8(18) << 8 | u8(19);
        const uint32_t h = u8(20) << 24 | u8(21) << 16 | u8(22) << 8 | u8(23);
        if (w > 0 && h > 0 && w < 100000 && h < 100000) {
            return std::pair {int(w), int(h)};
        }
        return std::nullopt;
    }
    // JPEG: walk the markers to a start-of-frame (SOF0..SOF15 except DHT, JPG and DAC)
    if (b.size() >= 4 && u8(0) == 0xFF && u8(1) == 0xD8) {
        size_t i = 2;
        while (i + 9 < b.size()) {
            if (u8(i) != 0xFF) {
                return std::nullopt;
            }
            const uint32_t m = u8(i + 1);
            if (m == 0xFF) {
                ++i;
                continue;
            }
            const uint32_t len = u8(i + 2) << 8 | u8(i + 3);
            if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                const uint32_t h = u8(i + 5) << 8 | u8(i + 6), w = u8(i + 7) << 8 | u8(i + 8);
                if (w > 0 && h > 0) {
                    return std::pair {int(w), int(h)};
                }
                return std::nullopt;
            }
            if (len < 2) {
                return std::nullopt;
            }
            i += 2 + len;
        }
    }
    return std::nullopt;
}

Result<std::string> insertCanvas(Document& doc, const std::string& path, const std::string& plane, const std::string& x,
                                 const std::string& y, const std::string& width, const std::string& angle, double opacity,
                                 std::string id)
{
    const auto bytes = readWholeFile(path, 20u << 20);
    if (!bytes) {
        return makeError("canvas.read", "The picture could not be read (it is missing, unreadable or larger than 20 MB).");
    }
    const auto size = pictureSize(*bytes);
    if (!size) {
        return makeError("canvas.kind", "Jigsmith places PNG and JPEG pictures.",
                         {"Save the picture as PNG or JPEG in an image editor, then place it again."});
    }
    const std::string file = utf8FromPath(pathFromUtf8(path).filename());
    const std::string image = base64Encode(*bytes);
    auto r = doc.transact("Canvas " + file, [&](Model& m) -> Result<bool> {
        Feature f;
        f.id = id.empty() ? Document::newId(m, "cv") : id;
        if (Document::findFeature(m, f.id)) {
            return makeError("script.args", fmt("The id '%1' is already used.", {f.id}));
        }
        f.name = file;
        f.kind = FeatureKind::Canvas;
        f.params = {{"file", file},
                    {"plane", plane},
                    {"center", {x, y}},
                    {"width", width},
                    {"angle", angle},
                    {"opacity", opacity},
                    {"aspect", double(size->second) / double(size->first)},
                    {"image", image}};
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
