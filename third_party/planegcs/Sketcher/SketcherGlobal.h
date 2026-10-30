// SPDX-License-Identifier: LGPL-2.1-or-later
// Jigsmith shim: replaces FreeCAD's Sketcher export macro header. planegcs is
// built as a static library inside Jigsmith, so the export macro is empty.
#pragma once
#ifndef SketcherExport
#define SketcherExport
#endif
