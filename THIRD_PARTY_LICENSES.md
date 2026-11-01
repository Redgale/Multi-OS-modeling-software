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

## HarfBuzz

Copyright © the HarfBuzz authors (Behdad Esfahbod, Google, Red Hat, Mozilla Foundation and
others; see its COPYING file). Used under HarfBuzz's "Old MIT" licence, dynamically linked,
to shape sketch text (letters that join, read right to left or form ligatures). The licence:
permission is hereby granted, without written agreement and without license or royalty fees,
to use, copy, modify, and distribute this software and its documentation for any purpose,
provided that the above copyright notice and the following two paragraphs appear in all
copies of this software. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS
SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE. THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE COPYRIGHT
HOLDER HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
MODIFICATIONS. https://harfbuzz.github.io

## Inter font

Inter (`assets/fonts/Inter-Regular.otf`, version 4.0), copyright 2016-2023 The Inter
Project Authors, https://github.com/rsms/inter. Licensed under the SIL Open Font License
1.1; the full licence is in `assets/fonts/OFL.txt`. The font is embedded unmodified in
`jig_core` as the built-in font for sketch text. It is not sold by itself.

## OCCT runtime libraries on Windows (FreeType, FreeImage and others from conda-forge)

The Windows build takes a prebuilt OCCT 8.0.1 from conda-forge, together with the
libraries that OCCT build depends on (for example FreeType, FTL or GPL-2.0, used under the
FTL). **Before the first release** the Windows package must ship only the DLLs Jigsmith
actually loads (BACKLOG B-011). The CI packaging now removes every DLL nothing loads and writes
`THIRD_PARTY_DLLS.txt` into the package: each remaining conda-forge DLL with its package, version
and licence. That list must be reviewed before the first release; until then the Windows
packages are development builds.

## Inno Setup (Windows installer only)

`Jigsmith-windows-x64-setup.exe` is made with Inno Setup 6 (Jordan Russell and Martijn
Laan), whose licence allows using it and distributing the installers it makes, including
commercially. The installer's own setup program comes from Inno Setup; nothing of it is
linked into Jigsmith. https://jrsoftware.org/files/is/license.txt

## Assets

The application icon (`packaging/linux/jigsmith.svg`) is an original placeholder
made for Jigsmith and dedicated to the public domain (CC0). The tool icons are original
drawings made for Jigsmith. The only third-party asset is the Inter font (above);
`docs/ASSETS.md` records the source and licence of every asset.

## pybind11

Copyright © 2016 Wenzel Jakob and the pybind11 contributors. Used under the BSD 3-Clause
licence, header-only, to build the optional `jigsmith` Python module. Redistribution and use in
source and binary forms, with or without modification, are permitted provided that the
following conditions are met: (1) redistributions of source code must retain the copyright
notice, this list of conditions and the following disclaimer; (2) redistributions in binary
form must reproduce the copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the distribution; (3) neither the
name of the copyright holder nor the names of its contributors may be used to endorse or
promote products derived from this software without specific prior written permission. THIS
SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES ARE DISCLAIMED; IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DAMAGES ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE.
