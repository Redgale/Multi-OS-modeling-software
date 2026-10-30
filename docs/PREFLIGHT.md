# Preflight: critique of the master brief

*Phase 0, step 1. Written 2026-10-30 at the start of session 1. This is the working
interpretation of `CAD_Build_Prompt.md`: where the brief is ambiguous, this document
says how it is read. LOCKED items are not changed; DEFAULT and OPEN items are
resolved here or in `docs/DECISIONS.md`.*

## 1. What the brief gets right (keep)

- Integrate rather than invent: OCCT plus an existing solver, with the value in
  workflow, document model and UX.
- Hard problems first, via spikes: persistent naming, kernel failures and packaging
  are where CAD projects die.
- An honesty contract (section 8.7), plus a long-running state discipline
  (`STATUS.md`, `DECISIONS.md`, `BACKLOG.md`) that makes many-session work possible.
- Concrete acceptance scenarios (A1 to A8) rather than adjectives.

## 2. Ambiguities and how they are resolved

| # | Ambiguity | Resolution |
|---|---|---|
| A-1 | "~25 essential tools" (principle 8): which 25? | The command registry marks `essential` per command. Today 27 modelling/sketch tools plus file/edit/view basics are essential; a test keeps the count between 20 and 40. Revisit after the usability protocol (USABILITY_TEST.md). |
| A-2 | "Under 15 minutes for a novice" (A1) cannot be measured by an AI agent. | Measured by the human protocol in `docs/USABILITY_TEST.md`. Automated proxy: the tutorial script must complete in under 60 interaction steps, and a scripted run of the tutorial must succeed on all three OSes. |
| A-3 | "Parts model by default" vs. v0.1 scope (Components/joints are v0.2). | v0.1 has **bodies only**; each body is the future part. The document model reserves a `parts` layer (D-006) so v0.2 can promote bodies to parts without a format break. The "turn these into parts" offer ships with joints in v0.2. |
| A-4 | Undo vs. "undo across rollback" (Phase 1 spike 4). | Moving the rollback marker is itself an undoable transaction; undo restores the full model snapshot, including the marker position. Proven by `UndoRedoFiftyStepsIsIdentical` and the rollback tests. |
| A-5 | "Autosave at a fixed interval" with no interval given. | 60 s while there are unsaved changes, to a per-user recovery folder, written atomically. The interval becomes a preference in M6. |
| A-6 | "Versions" (local version history) vs. git-like history. | Named and automatic snapshots of the `.jig` (each a full model, since models are small) stored beside the design in a `<name>.jig.versions/` folder. M6. |
| A-7 | "Embossed or cut text" for the pen cup (A3) but Emboss is v0.2. | A3 is met in v0.1 with **Text + Extrude Cut** on a plane tangent to the cylinder (the brief allows "embossed or cut"). Emboss-on-curved-face is v0.2. |
| A-8 | Knob (A2) needs "a slightly curved side" and a D-shaft, both v0.1 sketch tools, plus Circular Pattern of features. | All are in v0.1 scope (Arc, Trim, Circular Pattern of features). Listed as the M4 acceptance test. |
| A-9 | "Typed values everywhere … locale-aware number parsing". The expression language uses `,` to separate function arguments. | Expressions always use `.` as the decimal point inside the expression engine. The UI layer converts a locale decimal comma to `.` in simple numeric input (no operators) before evaluation; mixed cases show a hint. Recorded as D-008. |
| A-10 | Fit-point spline and Text are in the v0.1 sketch list, but "Conic curve" and "Control point spline" (PDF ch. 10) are not. | Follow the brief: those two are v0.2. |
| A-11 | Hole "tap types" (clearance, tapped) and threads. | v0.1 Hole: simple/counterbore/countersink with typed sizes plus an ISO metric *clearance* table (M3 = 3.4 mm, etc., matching the PDF hobby tips). Tapped/modelled threads ship with Thread in v0.2. |
| A-12 | "Mirror" and "Patterns" of what object types? | v0.1: features and bodies. Faces and components later. |

## 3. Contradictions found

1. **Phone stand without Plane-at-Angle.** PDF Project 1 says "Angle via Construct > Plane at
   Angle", and its step 3 sketches the slanted back on the side plane. Both work. The
   sample (`samples/phone_stand.jigs`) uses the side-plane profile, because that keeps
   the 75° angle as a single named parameter. The tutorial (M7) will use the same route.
   Plane at Angle stays in v0.1 scope (M4).
2. **"Shift+middle drag orbit" vs. "trackpad essential on macOS".** A Fusion-like
   default is useless without a middle button. Resolution: every preset also orbits on
   **right-drag** (Shift+right-drag pans), and the Trackpad preset maps two-finger
   scroll to orbit and pinch to zoom. The marking menu will open on a right-*click*
   (no drag) once it exists, which is how other apps reconcile the two.
3. **C++20 vs. planegcs.** Upstream planegcs (October 2026) uses `std::unreachable`
   (C++23). The project stays C++20; only the vendored planegcs target compiles as
   C++23 (D-004). All compilers in the CI matrix support this.
4. **OCCT 8.x default vs. available packages.** Ubuntu 24.04 ships OCCT 7.6.3, Homebrew
   7.9.3 and vcpkg 8.0.1 (checked 2026-10-30). Requiring 8.x would mean building OCCT
   from source on Linux and macOS for every contributor, which breaks the
   30-minute build promise (success measure 4). Resolution: **support OCCT 7.6 to 8.x**
   and record the differences in one place (`cmake/Dependencies.cmake`,
   `core/src/kernel.cpp`). D-002.
5. **"Do not use OCCT visualization" vs. the effort of a custom renderer.** Kept: the
   custom renderer is about 700 lines and already picks faces on the GPU. OCCT's AIS/V3d
   would tie the UI to OCCT's OpenGL path and make the macOS Metal question harder.

## 4. Hidden assumptions made explicit

- **Single-threaded document.** Recompute runs on the calling thread; only
  tessellation for display runs on workers. Long recomputes (over 100 ms) will move to
  a worker with a progress indicator in M8, and the document API is already
  value-based (immutable `ModelState`) to allow it.
- **One body per fillet.** Fillets act on one body at a time (error with a plain
  explanation otherwise).
- **Z-up, millimetres** are fixed for v0.1 documents. Inch documents are a v0.2
  preference; typed inch values (`2 in`) already work everywhere.
- **No network access at all** in the app. Nothing in the core or UI opens a socket.
  Crash reporting, when added, is opt-in and goes through a separate, audited module.
- **Determinism.** Recompute must give identical results on the same OCCT version.
  Across OCCT versions small floating-point differences are expected, so golden tests
  use tolerances (1e-4 relative on volume) and exact face/edge counts.

## 5. Missing requirements added

| Added requirement | Why |
|---|---|
| **Script format and CLI** (`jigsmith-cli --run`, docs/SCRIPTING.md) | The brief requires a CLI but does not define the script. It is a one-command-per-line format driven by the same command registry as the UI, so every UI feature is scriptable and testable. The Python API (v0.2) will wrap the same commands. |
| **Recovery lock file** | Crash recovery needs to know a session ended uncleanly. The app writes a lock file and removes it on clean exit. |
| **Kernel-version matrix in golden tests** | Golden numbers may differ slightly between OCCT 7.6, 7.9 and 8.0; tolerances and exact topology counts are specified per test. |
| **File-format fuzzing on every CI run** | `DamagedFilesNeverCrash` mutates real `.jig` files 400 ways per run, and the expression engine gets 20,000 random inputs. |
| **Sample files are scripts** | Samples are stored as `.jigs` scripts, not binary `.jig` files, so they diff cleanly and double as tests. |

## 6. Risks (ranked) and planned mitigations

| Rank | Risk | Mitigation | Status after session 1 |
|---|---|---|---|
| 1 | **Persistent naming breaks on real edits** (fillets jump to other edges) | History-based face names, edge names from adjacent faces, fingerprint fallback that *reports* instead of guessing; regression suite. | Spike passes all 7 regression scenarios on the phone stand. Reordering is not yet implemented (BACKLOG). |
| 2 | **macOS OpenGL deprecation** | Renderer isolated behind `Viewport`; plan a Qt RHI (Metal) backend. | GL 3.3 core works on Linux/Mesa. macOS path compiles in CI; not yet run on a Mac display (no GUI test on macOS runners). |
| 3 | **Windows build time** (vcpkg builds OCCT from source) | Measured: 89+ min, cancelled. Switched to prebuilt conda-forge OCCT (D-013); official Qt binaries. | See STATUS.md. |
| 4 | **Solver robustness while dragging** | planegcs with DogLeg then Levenberg-Marquardt; redundant-constraint re-solve; temporary drag constraints. | Drag on 100 entities: 3.4 ms median (budget 16 ms). Interactive drag UI is M1. |
| 5 | **Kernel failures on fillet/shell** | Validate with BRepCheck, roll back, explain, and search for a working value. | Fillet failure grace passes A5 automatically. Shell is M3. |
| 6 | **Scope**: v0.1 is large (about 80 tools) | Vertical slices M1 to M8; tools hidden or disabled until they pass their tests. | Registry shows every v0.1 tool with "coming in Mx". |
| 7 | **Trademark of the chosen name** | Registry and web checks done; a formal trademark search is the owner's call. | See D-001; flagged to the owner. |
| 8 | **Text tool fonts** across OSes | FreeType/HarfBuzz plus platform font enumeration (M5 spike). | Not started. |

## 7. The improved working spec, in one paragraph

Jigsmith v0.1 is a body-based (not yet assembly) parametric modeller with a strict
headless core (`jig_core`), a Qt 6 Widgets UI with a custom GL renderer, planegcs for
sketches and OCCT 7.6 to 8.x for solids. Every user action is a registry command that
also works from a script; every document change is a transaction on a small, plain-data
model that recomputes deterministically and incrementally; every kernel failure becomes
a plain-language message with a tested repair suggestion. Release gates are the
acceptance scenarios A1 to A8, run in CI on Linux, Windows and macOS wherever they can be
automated, and by the human protocol otherwise.
