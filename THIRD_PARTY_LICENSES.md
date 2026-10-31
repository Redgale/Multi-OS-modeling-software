# Third-party software in Jigsmith

Jigsmith itself is licensed under the **GNU General Public License v3.0 or later**
(see `LICENSE`). It is built on the following components. The machine-readable list
is `tools/dependencies.json`; CI runs `tools/license_audit.py`, which fails if a
dependency is missing from this file or has a license that is not on the allow-list.

| Component | License | How it is used |
|---|---|---|
| Open CASCADE Technology | LGPL-2.1 with the OCCT exception | dynamically linked geometry kernel |
| planegcs (from FreeCAD) | LGPL-2.1-or-later | vendored source, statically linked |
| Qt 6 | LGPL-3.0 | dynamically linked UI toolkit |
| Eigen | MPL-2.0 | header-only, used by planegcs |
| Boost (Graph and headers) | BSL-1.0 | header-only, used by planegcs |
| nlohmann/json | MIT | header-only, document format |
| miniz | MIT | ZIP container for `.jig` files |
| GoogleTest | BSD-3-Clause | tests only, not shipped |
| oneTBB | Apache-2.0 | shipped only where the OCCT build uses it |

All of these are compatible with GPL-3.0-or-later when combined as described.

## Open CASCADE Technology

**Jigsmith uses Open CASCADE Technology (OCCT)**, https://dev.opencascade.org,
Copyright © Open CASCADE SAS. OCCT is free software: you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public License version 2.1 as
published by the Free Software Foundation, with the special exception defined in
the file `OCCT_LGPL_EXCEPTION.txt` in the OCCT distribution. OCCT is linked
dynamically, so you can replace the OCCT libraries shipped with Jigsmith with a
compatible build of your own. OCCT sources: https://github.com/Open-Cascade-SAS/OCCT.
The notice also appears in Help ▸ About Jigsmith and in `jigsmith-cli --help`.

## planegcs (from FreeCAD)

Copyright © Konstantinos Poulios and the FreeCAD contributors. LGPL-2.1-or-later.
Source is in `third_party/planegcs` (unmodified files plus a small, separately
marked compatibility shim); the license text is `third_party/planegcs/COPYING.LIB`
and the upstream commit is recorded in `third_party/planegcs/README.md`.

## Qt 6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets)

Copyright © The Qt Company Ltd and other contributors. Used under the GNU Lesser
General Public License v3.0 (only LGPL-licensed modules are used; this is audited
in `cmake/Dependencies.cmake`). Qt is linked dynamically and can be replaced by the
user. Source: https://download.qt.io/official_releases/qt/. Help ▸ About Qt shows
Qt's own notice.

## Eigen

Mozilla Public License 2.0. https://gitlab.com/libeigen/eigen. Header-only, used
unmodified.

## Boost (Graph and headers)

Boost Software License 1.0. https://www.boost.org. Header-only, used unmodified.

## nlohmann/json

Copyright © 2013-2023 Niels Lohmann. MIT License. https://github.com/nlohmann/json.

## miniz

Copyright © 2013-2014 RAD Game Tools and Valve Software, Copyright © 2010-2014 Rich
Geldreich and Tenacious Software LLC. MIT License.
https://github.com/richgel999/miniz (version 3.0.2, fetched and hash-checked at build time).

## GoogleTest

Copyright © 2008 Google Inc. BSD 3-Clause License. https://github.com/google/googletest.
Used only to build the test suite; not shipped.

## oneTBB (pulled in by some OCCT builds)

Copyright © Intel Corporation and the UXL Foundation. Apache License 2.0.
https://github.com/uxlfoundation/oneTBB. Present only when the platform's OCCT build
was compiled with TBB (for example Debian/Ubuntu packages).

## FreeType

Portions of this software are copyright © The FreeType Project (www.freetype.org).
All rights reserved. Used under the FreeType License (FTL), dynamically linked, to read
glyph outlines for sketch text. https://freetype.org

## Inter font

Inter (`assets/fonts/Inter-Regular.otf`, version 4.0), copyright 2016-2023 The Inter
Project Authors, https://github.com/rsms/inter. Licensed under the SIL Open Font License
1.1; the full licence is in `assets/fonts/OFL.txt`. The font is embedded unmodified in
`jig_core` as the built-in font for sketch text. It is not sold by itself.

## OCCT runtime libraries on Windows (FreeType, FreeImage and others from conda-forge)

The Windows build takes a prebuilt OCCT 8.0.1 from conda-forge, together with the
libraries that OCCT build depends on (for example FreeType, FTL or GPL-2.0, used under the
FTL). **Before the first release** the Windows package must ship only the DLLs Jigsmith
actually loads, each listed here with its licence (BACKLOG B-011). Until then the CI zip is a
development artifact, not a release.

## Assets

The application icon (`packaging/linux/jigsmith.svg`) is an original placeholder
made for Jigsmith and dedicated to the public domain (CC0). The tool icons are original
drawings made for Jigsmith. The only third-party asset is the Inter font (above);
`docs/ASSETS.md` records the source and licence of every asset.
