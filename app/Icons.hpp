// SPDX-License-Identifier: GPL-3.0-or-later
// Jigsmith's tool icons: an original set drawn from compact vector descriptions on a
// 24x24 grid, painted in the theme's ink colour plus one accent colour. No image
// files and no SVG module are needed, and every icon follows the theme (light, dark,
// high contrast). See docs/ASSETS.md.
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace jigapp
{

// Icon for a command id from the registry ("solid.extrude"), a feature kind
// ("feature.sketch") or a status ("status.error"). Unknown ids get a neutral dot.
QIcon toolIcon(const QString& id, const QColor& ink, const QColor& accent);

// True when a dedicated drawing exists for the id (tests check every command has one).
bool hasToolIcon(const QString& id);

}  // namespace jigapp
