# Status

*Last updated: 2026-10-31, session 2. Read this first when resuming.*

## Summary

Session 1 did Phase 0 and the Phase 1 spikes (**GO for M1**, `docs/PHASE1_REPORT.md`).
Session 2 delivered the core of **M1 (interactive sketching)** and **M2 (Extrude, Fillet,
timeline editing, parameters)** in the desktop app, an original **icon set** for every
tool, build-from-source support for **Void, Fedora, Arch and openSUSE**, and a publish job
for downloadable builds. 53 tests, including a UI workflow test that drives the real
window.

## What works, and how it was verified

| Area | What works | Verified how |
|---|---|---|
| Core build | `jig_core`, `jigsmith-cli`, tests, bench | Linux local (GCC 13, OCCT 7.6.3); CI, see table below |
| Phone stand | Builds headlessly from `samples/phone_stand.jigs` (10 steps, 8 parameters) | `Golden.PhoneStand`: volume 87925.87 mm³ ±0.01%, bbox, 26 faces, 62 edges, valid solid |
| Sketch solver | planegcs: points, lines, circles, arcs; 15 constraint types; DOF, conflicts, redundancy, free geometry, drag, gap finder | `tests/test_solver.cpp` (9 tests incl. 300-case property test) |
| Expressions and units | mm/cm/m/in/ft, deg/rad, parameters, functions, plain errors | `tests/test_expr.cpp` (8 tests incl. 20,000-input fuzz) |
| Features | Sketch (plane, offset plane, on a face), Extrude (new/join/cut/intersect, one side/reverse/symmetric, distance/all), Fillet (constant) | golden, naming and failure tests |
| Persistent naming | Edit early, insert mid-history, sketch-on-face follows the face, removed edges reported | `tests/test_naming.cpp` (7 scenarios) |
| Failure grace (A5) | Impossible fillet rolls back, explains, suggests a radius that is verified to work | `FailureGrace.*` (4 tests) |
| Undo (A6, half) | Unlimited; 50 undo + 50 redo identical | `Document.UndoRedoFiftyStepsIsIdentical` |
| Files | `.jig` ZIP (manifest + canonical JSON), atomic save, unknown fields kept, version check, corrupt-file fuzz | `Document.*` (11 tests) |
| Interchange (A7, half) | STEP export/import (volume to 1e-6, unit detected), binary STL | `Interchange.*` |
| Command registry | ~80 v0.1 tools described; intent search; brief's default keys | `Commands.*` (4 tests) |
| Desktop app shell | Window, GL 3.3 viewport, GPU face and edge picking, gizmo, navigation presets, workflow strip, icon + label toolbar from the registry (unbuilt tools disabled with milestone), browser, timeline with icons, status glyphs and rollback marker, palette with icons, open/save/export, undo/redo, themes, welcome, autosave and recovery prompt | Linux under Xvfb + Mesa: screenshots in `docs/images/` |
| Sketching (M1) | Create sketch by clicking an origin plane or a flat face; look-at; contextual sketch toolbar; Line (chains, close by clicking the start), Rectangle (corner, centre), Circle, Point, Dimension (length, diameter, radius, point-point, point-line); typed values with Tab/Enter; snapping to points and grid; automatic horizontal/vertical (Ctrl suppresses); constraints from the toolbar on the selection; drag with live solve; delete; construction toggle; DOF in words; free/fixed shown by marker shape; constraint glyphs and dimension values (expressions shown); gap rings; shaded profiles; conflicting constraints refused with an explanation | `tests/test_ui.cpp` drives it (plane click, centre rectangle with typed 60 × 40, refusal message); screenshot `docs/images/sketch_mode.png` |
| Extrude panel (M2) | Live preview, sketch and profile choice, distance (expressions), through-all, one side/other side/symmetric, Join/Cut/New/Intersect and **Auto** (Cut when the extrusion goes into material), OK/Cancel/Esc/Enter, values remembered for the session | UI test: 24000 mm³, then edit from the timeline to 20 mm → 48000 mm³ |
| Fillet panel (M2) | Edge picking in the view (GPU, hidden edges rejected), tangent-chain selection, live preview on the rounded body (clicks map back by persistent name), failure message with one-click working radius | UI test: real edge picks, 4 edges, volume matches the analytic value; 30 mm radius refused with "too big" |
| Timeline editing (M2) | Double-click to edit, right-click: rename, suppress/unsuppress, roll back to here, delete with a dependents warning | `Document.DependentsAreFound`; UI test edits a step |
| Parameters (M5 part) | Table dialog: name, value with units/expressions, kind, live result, comment; refuses invalid names, duplicates, cycles | exercised manually under Xvfb only (no automated UI test yet: B-015) |
| Performance | All headless budgets met | `jigsmith-bench`, numbers in PHASE1_REPORT.md |

### CI results (GitHub Actions, branch `claude/pensive-mayer-l4r19c`)

| Job | Result |
|---|---|
| License audit | ✓ passed |
| Linux (Ubuntu 24.04, OCCT 7.6.3): build, tests (UI tests under Xvfb from session 2), bench, GUI screenshot, AppImage | ✓ passed (runs 2 and 5); session-2 run: see below |
| Linux ASan + UBSan (core) | ✓ passed (runs 1, 2, 5) |
| macOS 15 Apple silicon (Homebrew, OCCT 7.9.3, Eigen 5): build, 50 tests, bench, .dmg | ✓ passed (run 5), after two portability fixes (Eigen 5 version check; no floating `from_chars` in Apple libc++) |
| macOS 15 Intel: build, 50 tests, bench, .dmg | ✓ passed (run 5) |
| Windows 2022 (conda-forge OCCT 8.0.1, Qt 6.8.3, MSVC 14.44): build, 50 tests, bench, portable zip | ✓ passed (run 41146742253). First vcpkg attempt spent 89+ min compiling OCCT and was cancelled; switched to prebuilt conda-forge OCCT (D-013, dependencies install in about 3 min). Two fixes on the way: Boost headers without a CMake package; OCCT 8 removed `Standard_Failure::DynamicType()` |
| Void Linux glibc and musl, Fedora, Arch (official container images, `scripts/install-deps.sh` only): build + 50 tests | ✓ passed (run 41148659255) |
| openSUSE Tumbleweed | ✓ passed (run 41148826070) after replacing busybox-gawk in the minimal container |
| OCCT built from source (`scripts/build-occt.sh`) | first run failed: OCCT's data-exchange toolkits need X11 headers; headers added, re-run pending |
| Publish `continuous` pre-release | builds collected; **release creation refused (HTTP 403)** because the repository limits the workflow token to read-only. Owner action: Settings ▸ Actions ▸ General ▸ Workflow permissions ▸ "Read and write permissions", then re-run the job |

Also verified locally: the whole tree builds warning-clean and passes all tests with
Clang 18 as well as GCC 13.

**All six jobs green in a single run** ([run 41146742253](https://github.com/Redgale/Multi-OS-modeling-software/actions/runs/41146742253)): the same 50 tests pass on OCCT 7.6.3 (Linux), 7.9.3 (macOS) and 8.0.1 (Windows). "macOS and Windows unverified locally; CI result: as above" (brief 8.7). GUI rendering is still verified only on Linux.

## What is stubbed or not started

- M1 still missing: Arc, Polygon, Ellipse, Slot and Spline tools in the app (Slot works in
  scripts), Trim/Extend/Break/Offset/sketch Fillet/Mirror/patterns, selecting and deleting
  individual constraints, window selection, collinear/concentric, Project/Include.
- M2 still missing: extrude drag handle on the canvas, two-sides/to-object/taper/thin,
  Browser visibility/isolate/rename, selection filters, Select Other, timeline reorder and
  groups, one-click "fix this" beyond the fillet radius.
- Not built yet: Revolve, Hole, Chamfer, Shell, patterns, mirror, construction tools,
  Measure, Section, Text, SVG/DXF import, 3MF, local versions, tutorial, marking menu.
- Reorder of timeline steps (model supports it; no command or tests yet, B-002).
- libslvs comparison (B-001).

## What is broken or unverified

- Rendering on Windows and macOS: unverified (no display on CI runners). B-003.
- The crash-recovery prompt has not been exercised with a real forced kill.
- Code of Conduct contact: the owner should add a dedicated contact address.

## Decisions waiting for the owner

0. **Allow CI to publish releases**: enable read-and-write workflow permissions (see the CI table) so the `continuous` pre-release can be created.

1. **Trademark**: Jigsmith passed package-registry and web checks, but a formal
   trademark search (USPTO/EUIPO) is a legal decision (D-001).
2. **Code of Conduct contact address** (a project email or form; nothing personal is
   published now).

## Next three tasks

1. Finish M1's remaining sketch tools (Arc, Slot, Polygon, Trim, Offset) and constraint
   selection/deletion; then M3 core features (Chamfer, Shell, Hole) with panels.
2. **B-001 libslvs spike** in an environment where building SolveSpace is allowed.
3. Windows/macOS GUI screenshots in CI (B-003); Windows DLL audit (B-011).

## How to resume

```bash
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build
```

```bash
./build/cli/jigsmith-cli --run samples/phone_stand.jigs --report -
```

Read `docs/PHASE1_REPORT.md`, `docs/DECISIONS.md`, `docs/BACKLOG.md`, then pick up the
next three tasks above.
