// SPDX-License-Identifier: GPL-3.0-or-later
// Pictures kept inside a design (a canvas to trace over): their bytes as text, and their size
// read from the file's header, without decoding the picture (the core has no image library).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "jig/result.hpp"

namespace jig
{
class Document;

std::string base64Encode(std::string_view bytes);
// Width and height in pixels of a PNG or JPEG file, or nothing for anything else.
std::optional<std::pair<int, int>> pictureSize(std::string_view bytes);
// Adds a Canvas step with the picture at `path` (PNG or JPEG, up to 20 MB) on `plane`
// ("XY", "XZ", "YZ" or a construction plane id), centred at (x, y), `width` wide.
Result<std::string> insertCanvas(Document& doc, const std::string& path, const std::string& plane,
                                 const std::string& x, const std::string& y, const std::string& width,
                                 const std::string& angle = "0 deg", double opacity = 0.5, std::string id = {});
}  // namespace jig
