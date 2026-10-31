# Decision records

Format (brief 8.3): context, options considered (at least two), decision, evidence,
consequences, how to reverse. Newest decisions are appended at the end. Status is one
of: **accepted**, **provisional** (needs more evidence or an owner decision),
**superseded**.

---

## D-001 Product name: Jigsmith (provisional: owner sign-off on trademark)

**Context.** The brief (section 2) requires an original name with no "Fusion",
"Autodesk" or "360", five candidates checked for trademark and package-registry
conflicts.

**Options and checks (2026-10-30).** Registry checks are exact-name lookups on PyPI,
npm and crates.io. "Web" is a general search for software with that name.

| Candidate | Meaning | PyPI | npm | crates | Web / similar marks | Verdict |
|---|---|---|---|---|---|---|
| **Jigsmith** | A smith of jigs: makers' word, workshop feel | free | free | free | No software of that name. Nearest is JigSpace (AR presentations, "Jig" brand); different word and market | **Chosen** |
| Plinth | A base you build on | taken | taken | taken | Generic word, many uses | Rejected (registries) |
| Tinmark | Tinkering + mark | free | free | free | No conflicts found | Runner-up: clean but meaningless to users |
| Benchwright | A wright at the bench | free | **taken** | free | — | Rejected (npm) |
| Kerfwright | Kerf (cut width) + wright | free | free | free | Close to CarveWright (CNC carving machines, woodworking market) | Rejected (confusing similarity) |

**Decision.** Jigsmith. Binary names `jigsmith` and `jigsmith-cli`, file extension
`.jig`, script extension `.jigs`, C++ namespace `jig`.

**Evidence.** Registry lookups and web searches above. **Not done:** a formal
trademark search (USPTO, EUIPO, WIPO). That is a legal decision for the owner (brief
section 0: block only on what only the owner can supply). Flagged in STATUS.md.

**Consequences.** The name appears in about 40 places (CMake project, window titles,
file format id `jigsmith-design`, docs).

**How to reverse.** Rename before v0.1 is public: `git grep -il jigsmith` plus the
format id in `core/src/io.cpp` (add a migration that accepts the old id).

---

## D-002 Geometry kernel: OCCT, supporting versions 7.6 to 8.x (accepted)

**Context.** The brief defaults to OCCT 8.x. Checked 2026-10-30: Ubuntu 24.04 ships
7.6.3, Homebrew 7.9.3, vcpkg 8.0.1. OCCT 7.8 renamed data-exchange toolkits (TKSTEP to
TKDESTEP, and so on), and 7.6 lacks some newer helpers (e.g. `BRepLib_ToolTriangulatedShape`).

**Options.** (a) Require 8.x everywhere and build OCCT from source on Linux and macOS.
(b) Support 7.6 to 8.x using only APIs present across the range. (c) Vendor and build
OCCT as part of our build.

**Decision.** (b). `cmake/Dependencies.cmake` picks toolkit names by version;
`core/src/kernel.cpp` uses only APIs present from 7.6 through 8.0 (for example, display
normals are computed with `BRepLProp_SLProps` instead of a 7.7+ helper).

**Evidence.** Builds and all 50 tests pass on 7.6.3 (Linux). CI builds against 7.9.3
(Homebrew) and 8.0.1 (vcpkg); see STATUS.md for results.

**Consequences.** Contributors get OCCT from their package manager in minutes (needed
for success measure 4). Golden tests use tolerances because results can differ slightly
between OCCT versions. Release builds should pin one version per platform (M8).

**How to reverse.** Raise `OpenCASCADE_VERSION` minimum in `cmake/Dependencies.cmake`
and delete the compatibility branches.

---

## D-003 UI toolkit and renderer: Qt 6 Widgets plus a custom OpenGL 3.3 core renderer (accepted for v0.1; macOS backend open)

**Context.** The brief's default: Qt 6 (LGPL modules only), custom renderer, beware
macOS OpenGL deprecation.

**Options.** (a) Qt Widgets + QOpenGLWidget (GL 3.3 core). (b) Qt Quick/QML + Qt RHI
(Metal/Vulkan/D3D). (c) GTK4 (as in Dune 3D). (d) OCCT's AIS/V3d viewer inside Qt.

**Decision.** (a) now, with the renderer isolated in `app/Viewport.*` (one class owns
all GL) so that a Qt RHI backend (b) can replace it when macOS drops OpenGL or
performance demands it. Docking panels, trees and tables are much faster to build with
Widgets. (c) gives a less native result on macOS and Windows. (d) is excluded by the
brief unless a spike proves it better, and it would tie the UI to OCCT's GL path.

**Evidence.** The walking skeleton renders the phone stand under Xvfb with Mesa
llvmpipe (screenshots in `docs/images/`): shaded faces with edges, MSAA, grid, axis
gizmo, GPU ID-buffer face picking (hover and click), four navigation presets. Cold
start to window with model: 0.7 to 1.4 s under software GL. Finding: **QPainter drawing
directly on the core-profile GL surface produced nothing on Mesa**, so the overlay (gizmo,
hints) is a raster child widget, which is also safer for a future RHI backend.

**Consequences.** macOS is unverified on real hardware (no display on CI runners). On
macOS, GL 4.1 core is available and deprecated, not removed. Track Apple's status each
release.

**How to reverse.** Implement `Viewport` on QRhi (Qt 6.6+ public RHI API) with the same
public interface.

---

## D-004 2D constraint solver: planegcs (provisional: libslvs spike owed)

**Context.** The brief asks for a spike of planegcs (FreeCAD, LGPL-2.1+) against libslvs
(SolveSpace, GPL-3.0) on dragging robustness, redundancy and conflict diagnostics,
constraint coverage, and speed.

**Options.** (a) planegcs, vendored. (b) libslvs. (c) Write our own (forbidden by 5.1).

**Evidence gathered.**
- planegcs vendored **unmodified** (upstream commit `bbc0891`, 2026-10-30) with a
  70-line shim for its four FreeCAD includes. Upstream now needs C++23 for
  `std::unreachable`, so only that target compiles as C++23.
- Diagnostics: reports DOF, *conflicting* and *redundant* constraints by our
  constraint ids (tests `ConflictIsReportedAndGeometryUntouched`,
  `RedundantIsReported`). It also returns "dependent parameters", which give the list of
  still-free geometry for the "what can still move" display.
- Coverage: everything in the v0.1 list maps to native planegcs constraints except
  collinear and concentric (one-line compositions, M1), plus ellipses, B-splines and
  angle-via-point tangency that FreeCAD already uses.
- Speed: drag on a 100-entity sketch: **3.4 ms median, 4.6 ms max** per solve
  (budget 16 ms), `bench/`.
- Robustness: 300 random fully-dimensioned rectangles solve exactly (property test).
- Two traps found and handled: (1) with redundant constraints planegcs returns
  "converged" rather than "success", so redundant ones are dropped and the sketch is
  re-solved, then reported; (2) distance-style line-arc tangency combined with a shared
  endpoint is rank-deficient (reported as redundant); endpoint tangency must use
  "perpendicular to radius" or angle-via-point.
- **libslvs: not spiked in this session.** An attempt to build SolveSpace in this cloud
  container was blocked by the environment's safety policy on building external code.
  The comparison above is one-sided. What is known from its documentation: libslvs is
  GPL-3.0 (compatible with our GPL-3.0-or-later), fast, and robust while dragging, with
  "which constraints fail" output, but no redundant-vs-conflicting distinction and a
  smaller constraint set (no B-splines).

**Decision.** planegcs, *provisionally*. It meets every measured requirement today.

**Consequences.** We carry FreeCAD's LGPL source and track upstream changes
(update procedure in `third_party/planegcs/README.md`). The solver is wrapped behind
`jig::Sketch` (`core/src/sketch.cpp`); nothing else includes `GCS.h`.

**How to reverse.** Implement `Sketch::runSolver` on libslvs; the tests in
`tests/test_solver.cpp` are solver-agnostic and define the contract. **Action:** run the
libslvs spike in a session where building SolveSpace is permitted (BACKLOG B-001).

---

## D-005 Persistent naming strategy (accepted; reorder pending)

**Context.** Brief 5.4a, the hardest problem.

**Options.** (a) Index-based references (FreeCAD before 1.0): breaks on any topology
change. (b) Geometric-only matching (selector-style): silently picks wrong faces. (c)
**History-based names from the kernel's Generated/Modified records plus a
fingerprint fallback that never guesses silently** (FreeCAD 1.0 and Onshape-like in
spirit; our own implementation). (d) OCAF's TNaming: heavy, OCAF document model,
weak diagnostics.

**Decision.** (c), implemented in `core/src/kernel.cpp`:
- Faces are named by their creator and role: `e1/side:7` (extrude e1, side wall from
  sketch curve 7), `e1/start`, `e1/end`, `f1/fillet:<edge name>`, `f1/corner`.
  Sketch curve ids are stable integers never reused inside a sketch.
- Names flow through booleans by composing the boolean's history with
  `ShapeUpgrade_UnifySameDomain`'s history, so a face split and re-merged keeps its name,
  and coplanar seams do not survive to break fillets.
- Split faces get suffixes `~0`, `~1`, ordered by centroid. Edges are named from the sorted
  names of their two faces, `E[a|b]`, with `#k` when needed.
- References store body, name and a fingerprint (surface type, centroid, area, normal).
  Resolution: exact name, then split family (`name~k`) by fingerprint, then global
  fingerprint match. A match found any way other than exact is a **warning** on the
  feature; close candidates are **ambiguous** (error, user picks). Nothing is
  re-attached silently.

**Evidence.** `tests/test_naming.cpp` on the phone stand: wider base; deeper base and
thicker plate (the lip sketch on the top face follows it); narrower slot; hole
inserted before the fillets; boss inserted on the face the lip sketch sits on;
edit + insert + edit (three parameters including an angle); and slot suppressed
(fillet reports "N edges no longer exist" instead of guessing). All pass with every
feature OK (no warnings).

**Consequences.** Names depend on our own operation code, so every new feature must
name its faces (a review checklist item in CONTRIBUTING). Reordering features is
implemented by moving entries in the model; its regression tests are BACKLOG B-002.

**How to reverse.** Names are strings in the file; a different scheme can be added
with a format migration.

---

## D-006 Document model: bodies now, parts reserved (accepted)

**Context.** The brief wants a simple Parts model by default, but Components and joints
are v0.2.

**Options.** (a) Ship components in v0.1. (b) Bodies only, with a reserved `parts`
layer so v0.2 can promote bodies to parts.

**Decision.** (b). Each body is keyed by the feature that created it.

**Consequences.** "Turn these into parts" ships with joints (v0.2).

**How to reverse.** Not needed; v0.2 adds parts on top.

---

## D-007 Undo by model snapshots on top of commands (accepted)

**Context.** Brief 5.3.2: command pattern with transactions for every change; undo
must be built in.

**Options.** (a) Per-command inverse operations. (b) Each transaction records the
*model* (parameters + features + rollback marker: a few KB) before and after; geometry is
derived. (c) Full geometry snapshots.

**Decision.** (b). `Document::transact(label, edit)` runs an edit on a copy and commits
it atomically or not at all; undo/redo swap model snapshots, and recompute reuses
cached feature results keyed by (previous key, feature JSON, parameter values used).

**Evidence.** 50 undo + 50 redo give byte-identical JSON and identical volume
(`UndoRedoFiftyStepsIsIdentical`). A failed edit leaves the model untouched. Editing
the last feature rebuilds 1 feature; undo re-uses the cache.

**Consequences.** Memory grows with the number of steps × model size (small). Undo is
unlimited. Sketch-drag previews will be transient (not transactions) and commit once on
mouse release (M1).

**How to reverse.** Swap snapshots for diffs if models ever grow large.

---

## D-008 Expression and units engine: hand-written (accepted)

**Context.** Brief 5.4e: an existing library or a minimal one.

**Options.** (a) muParser (MIT): no units. (b) ExprTk (MIT): large, no units. (c) A
units library (mp-units) plus a parser: heavy templates, compile time. (d) A small
recursive-descent parser with dimension tracking (about 500 lines).

**Decision.** (d), `core/src/expr.cpp`: + − × ÷ ^, parentheses, mm/cm/m/µm/in/ft,
deg/rad/turn and `°`, sqrt/abs/min/max/round/floor/ceil/trig, named parameters,
dimension checking with plain-language errors. A bare number takes the field's unit
(mm, or degrees in angle fields). Parameters have a kind (length, angle, number),
inferred from the value typed.

**Evidence.** `tests/test_expr.cpp`, including 20,000 random-input fuzz cases.

**Consequences.** We own the grammar. Locale: the engine uses `.` decimals; the UI
converts a locale decimal comma in simple numbers (PREFLIGHT A-9).

**How to reverse.** Replace `jig::evaluate` behind the same interface.

---

## D-009 Error model: `jig::Result` and plain-language `jig::Error` (accepted)

**Decision.** No exceptions cross the core's public API. Each `Error` has a stable code,
one plain sentence, suggestions, and technical detail kept apart. OCCT
`Standard_Failure` is caught only inside `kernel.cpp`. Failing features keep the
previous geometry. Tests assert that no `Standard_` text reaches messages.

**Options considered.** Exceptions throughout; `std::expected` (C++23, not available on
all our compilers in C++20 mode).

---

## D-010 Script format and CLI (accepted)

**Decision.** `.jigs`: one command per line, `verb key=value`, `#` comments; the same
command registry as the UI. Samples are scripts, so they diff cleanly and are tests.
The Python API (v0.2) will expose the same commands.

**Options considered.** JSON command lists (unreadable by hand); embedding Python now
(v0.2 scope, adds a large dependency before it's needed).

---

## D-011 Build and dependency management (accepted; Windows route revised in D-013)

**Decision.** CMake ≥ 3.24 with Ninja. Linux: distribution packages (apt). macOS:
Homebrew. Windows: vcpkg manifest (`vcpkg.json`) plus Qt's official LGPL binaries
(`install-qt-action`). Small header/source libraries (miniz, plus fallbacks for
nlohmann/json and GoogleTest) are fetched by CMake with SHA-256 pins.

**Options considered.** vcpkg everywhere: one recipe, but building OCCT and Qt from
source takes over an hour per cold machine, which breaks the 30-minute contributor
build. Conan: similar cost, and an extra tool for contributors.

**Consequences.** Three recipes to maintain (documented in README). Windows cold
builds are slow until the vcpkg binary cache is warm.

---

## D-012 License: GPL-3.0-or-later (accepted)

**Decision.** As the brief's default. Compatible with OCCT (LGPL-2.1 + exception), Qt
(LGPL-3), planegcs (LGPL-2.1+), Eigen (MPL-2.0), Boost (BSL), MIT and BSD components.
CI enforces the allow-list (`tools/license_audit.py`).

**Options considered.** LGPL or MPL for the core (would allow proprietary forks; the
brief prefers keeping improvements free).

---

## D-013 Windows dependencies from conda-forge instead of vcpkg (accepted, supersedes part of D-011)

**Context.** The first Windows CI run with vcpkg spent **over 89 minutes** in the
configure step still compiling OCCT 8.0.1 and its dependencies from source (vcpkg builds
debug and release by default on a 4-core runner), and was cancelled. The owner noticed it
first. A contributor on Windows would face the same wait, which breaks the brief's
30-minute cold build (success measure 4, scenario A8).

**Options.** (a) Keep vcpkg with a binary cache: fast once warm, but every cache miss
(new OCCT version, new runner image) costs hours, and contributors start cold. (b) vcpkg
with a release-only triplet and minimal OCCT features: roughly halves the time, still
far over budget. (c) **conda-forge**: prebuilt OCCT 8.0.1 for win-64 (`novtk` variant),
plus Eigen, Boost headers, nlohmann/json and GoogleTest, in one environment file;
installs in minutes. (d) Official OCCT Windows binary archives: no matching Eigen/Boost/
GTest, more manual steps.

**Decision.** (c). `packaging/windows/environment.yml` is the single source for Windows
dependencies (CI and README). Qt still comes from the official LGPL binaries. `vcpkg.json`
is removed. The license audit reads the conda environment file.

**Evidence.** conda-forge's OCCT feedstock recipe (checked 2026-10-30) builds 8.0.1 with a
`novtk` variant. The first conda-based Windows CI result is recorded in STATUS.md.

**Consequences.** The Windows package must bundle OCCT's runtime DLLs from the conda
environment; until a per-DLL audit (B-011) the Windows zip is a development artifact
only. conda-forge's OCCT is built with its own toolchain (MSVC-compatible C++ ABI).

**How to reverse.** Restore a `vcpkg.json` (git history) and the previous CI job; consider
a prebuilt-binary cache hosted by the project.

## D-014 Sketch text: FreeType outlines in the core, Inter built in (accepted)

**Context.** Brief 5.4f: the Text tool needs cross-platform fonts, "the platform's font
enumeration plus FreeType/HarfBuzz", glyphs turned into sketch geometry, tested on all
three OSes. Acceptance scenarios A2 (knob) and A3 (pen cup) need engraved lettering, and
the core must stay headless (architecture rule 1).

**Options.** (a) OCCT's `Font_BRepFont`/`Font_BRepTextBuilder`: ready-made, but they live
in TKV3d/TKService, the visualisation toolkits, which pull X11/OpenGL libraries into the
headless core and CLI; checked on Ubuntu's OCCT 7.6.3 (`nm`: `StdPrs_BRepFont` is in
libTKV3d). (b) **FreeType directly**: load unscaled outlines, convert lines and quadratic/
cubic Béziers to OCCT edges, classify outer contours and holes by nesting depth (works
for TrueType and CFF winding), make planar faces. About 300 lines, no window system.
(c) Qt's `QPainterPath::addText`: easy, but Qt is not allowed in the core.
(d) HarfBuzz shaping on top of (b): needed for Arabic, Indic and other complex scripts;
another dependency and more testing.

**Decision.** (b) for v0.1, without HarfBuzz: one glyph per character with FreeType
kerning (Latin, Greek, Cyrillic and other simple scripts). A sketch text is a block
anchored to a normal sketch point (so it can be dragged and dimensioned); its glyph
outlines become closed areas (`T<id>.<n>` profile keys) that any feature can use. The
built-in font is Inter Regular (SIL OFL 1.1, the brief's suggestion), embedded in
`jig_core` so results do not depend on the computer. A font given by file path is used
when present; anything else falls back to Inter with a visible warning.

**Evidence.** `tests/test_features.cpp` `Text.*`: 5 valid solids for "Jig 8" with holes
kept as holes, the cut volume equals the outline area times the depth, area scales with
the square of the size, save/load round trip, font fallback warning. Cross-check
against a prototype using OCCT's builder (option a) on "Jigsmith 8" at size 10 with Inter:
both give 11 faces and 106.146 mm² of area. Inter's CFF outlines and DejaVu's TrueType outlines both produce valid faces.

**Consequences.** New dependency FreeType (FTL, dynamic; every target platform packages
it). Text is not split by other sketch curves, and text does not wrap around curved faces
(Emboss, v0.2). System font enumeration by family name is not implemented yet (B-020).
Results with Inter are identical everywhere; a different FreeType version could in
principle change outlines, so golden tests record the FreeType version.

**How to reverse.** Replace `core/src/text.cpp`; the sketch format (`texts` with text,
font, height expression, angle, anchor) does not depend on the engine.

## D-015 Ellipses and smooth curves outside the constraint solver (accepted)

**Context.** The brief lists Ellipse and Fit Point Spline for v0.1 M1. planegcs has ellipse
and B-spline entities, but each needs internal-alignment constraints, its own glyph and
dimension handling, and new code in every tool that walks curves (trim, offset, project).

**Options.** (a) Full planegcs ellipses and B-splines. (b) Keep the solver to points: an
ellipse is its centre point, an axis-end point and a minor radius set before solving (from
its radius dimension); a smooth curve is an ordered list of fit points with the curve
computed from them. (c) Approximate both with many short arcs or lines.

**Decision.** (b). The fit points and the ellipse's two points are ordinary sketch points,
so they can be dragged, dimensioned, fixed and made coincident with other geometry. The
smooth curve is a uniform Catmull-Rom curve (open ends mirror their neighbour, closed
curves wrap round), stored as cubic Bézier pieces; the solid gets the same pieces as one
cubic B-spline (inner knots of multiplicity 3), and the screen draws the same pieces, so
what you see is what is extruded.

**Evidence.** `SketchCurves.EllipsesAndSmoothCurves`: area of a turned ellipse equals
pi·a·b; the curve passes exactly through its fit points; extrudes to the expected volume;
save/load keeps the points. UI test draws, closes, ends and drags them.

**Consequences.** No tangent, point-on-curve or equal constraints along an ellipse or a
smooth curve yet; trim, extend, break and offset refuse them with a reason; other curves
are not split where they cross them. The ellipse's minor radius has no freedom in the
solver (it changes only through its dimension or by editing).

**How to reverse.** Map the same entities onto planegcs ellipses and B-splines later; the
file format (centre, axis end and minor radius; ordered fit points and a closed flag) can
stay.
