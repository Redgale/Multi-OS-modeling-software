# planegcs (vendored)

The 2D geometric constraint solver from FreeCAD's Sketcher workbench.

- Upstream: https://github.com/FreeCAD/FreeCAD, `src/Mod/Sketcher/App/planegcs`
- Upstream commit: `bbc08916a0bab73fdb4b7f3dad7ff706a18924c9` (2026-10-30)
- License: LGPL-2.1-or-later (see `COPYING.LIB`). Original copyright: Konstantinos
  Poulios and the FreeCAD contributors (see each file header).
- Files under `Sketcher/App/planegcs/` are **unmodified** copies.
- `Sketcher/SketcherGlobal.h` and everything under `shim/` were written for
  Jigsmith. They replace the small set of FreeCAD headers planegcs includes
  (`Base/Console.h`, `Base/Tools.h`, `FCConfig.h`, the export macro, and a Boost
  wrapper header).

To update: copy the upstream directory over `Sketcher/App/planegcs/`, rebuild, run
`ctest -R solver`, and record the new commit hash here and in
`THIRD_PARTY_LICENSES.md`.
