# Backlog

Ordered by priority within each group. Items carry their origin (brief section,
decision or finding). Move items to STATUS.md when started.

## Owed from Phase 1 (do before or alongside M1)

- **B-001 libslvs spike** (D-004): build SolveSpace's libslvs and run the same
  `tests/test_solver.cpp` contract plus the drag bench against it. Blocked this session by
  the environment's policy on building external code.
- ~~B-002 Reorder regression tests~~ done in session 2 (`Document.ReorderStepsSafely`, UI
  `reorderTimeline`). Remaining: name-resolution checks for reorders inside the phone stand.
- **B-003 Windows and macOS GUI evidence**: screenshot runs work in CI on macOS and on Windows
  (Mesa llvmpipe as Qt's software OpenGL, run 41225131582). Left: a Windows machine with a real GPU
  driver; manual check of trackpad pinch on a real Mac.
- **B-004 macOS renderer path**: verify GL 4.1 core on Apple silicon; spike a Qt RHI
  (Metal) backend for `Viewport`.
- **B-005 Universal macOS binary**: the "macOS universal" CI job builds for both architectures at
  once (official Qt, OCCT and FreeType from source; joining two Homebrew apps failed because their
  library versions differ). Green (run 41304373107). Left: HarfBuzz in that build. No signing or notarisation: the owner decided against a
  paid Apple developer account (2026-11-01); README says how to open the unsigned app.
- ~~B-006 Windows installer~~ done (Inno Setup, tested in CI by silent install, run, uninstall); try
  on a clean machine by hand.
- **B-007 Flatpak manifest** (brief: AppImage and Flatpak): manifest and AppStream metainfo in
  `packaging/flatpak`, built by the "Flatpak" CI job on [full ci] (green: run 41299218517, CLI runs
  inside the sandbox). Left: opening the window on a real desktop, pinning OCCT's git commit and submitting to Flathub: the owner will do that once the app is complete and bug-tested (2026-11-01).
- ~~B-008 Endpoint tangency~~ done (angle via the shared point); collinear/concentric done.
- **B-012 OCCT 8 deprecations**: `TopTools_*`/`TColStd_*` typedef headers are deprecated in
  8.0 (warnings on Windows). Switch to `NCollection_*` types once 7.6 support is dropped, or
  wrap them in one compatibility header.
- **B-011 Windows package DLL audit**: `packaging/windows/trim-dlls.ps1` (CI) keeps only DLLs reachable
  from the programs and Qt plugins and writes THIRD_PARTY_DLLS.txt (conda package + licence); the
  trimmed package renders, installs and runs in CI (run 41225131582). Review that list before the
  first release.
- ~~B-009 Recompute off the UI thread~~ done (worker thread, progress dialog after 0.4 s, input held
  back meanwhile; D-017). Left: a Cancel button (would need the feature code to stop between steps).

## M1 Sketch core and solver (in progress: core done in session 2)

Done: sketch mode, plane/face picker, look-at, Line/Rectangle/Centre rectangle/Circle/Point,
Dimension, typed values, snapping, automatic H/V, constraint commands, drag, DOF display,
conflict refusal, gap rings, profiles, construction toggle, delete.

- B-013 done (all v0.1 drawing tools, incl. tangent arc, 2/3-point circle, 3-point rectangle; point-on and
  tangent constraints along ellipses and smooth curves; trim and break of them, extending lines to them).
  Open smooth curves extend straight on from their ends (ellipses are closed).
- B-014 done (Trim, Extend, Break, Offset (linked), sketch Fillet, Mirror, Move/Copy with a turn,
  Grid/Ring copy (linked)).
- B-016 Select and delete individual constraints (click a glyph); window selection; collinear,
  concentric; point-on-curve snapping while drawing; endpoint tangency via angle-via-point.

## M2 Extrude and the timeline (in progress: core done in session 2)

Done: Extrude and Fillet panels with live preview, Auto join/cut, edge picking, tangent chain,
timeline edit/rename/suppress/roll back/delete with dependents warning, Parameters dialog.

- ~~B-015~~ done: UI test `parametersAndTimelineMenu` (Parameters dialog as one undoable step,
  a bad value explained in the dialog; Suppress/Unsuppress from the timeline menu).
- ~~B-017~~ done: drag handle, two sides, up to a face, taper, thin (incl. open curves).
- B-018 done (reorder; Browser visibility, isolate, rename, find in timeline, timeline groups,
  visibility and folded groups saved with the design).
- B-019 (sketch window selection, selection filter, Select Other, 3D box selection of edges, faces and bodies done).

- B-020 Text (installed fonts by family and style, several lines, along any sketch curve, HarfBuzz shaping
  done; a simplified bidirectional layout done): the full Unicode bidirectional algorithm (embeddings,
  isolates, mirrored brackets; e.g. FriBidi) if users need it.

## Later v0.1 milestones

- M3 (done in session 2: Fillet, Chamfer, Shell, Hole, Mirror of bodies, Patterns of
  features; hole clearance table and drill point; pattern of bodies; mirror of steps):
  variable fillet, press pull of curved faces (offset face).
- M4 (Revolve, construction planes and axes, Combine, Split, Move/Copy done): revolve to
  object, construction point.
- M5 (Parameters, Text, Project, SVG and DXF import done): linked (associative) Project.
- M6: Measure, Section, Properties with density; 3MF export; local versions; autosave
  preferences; STEP import UI with unit correction; parser fuzzing (SVG, DXF, STEP).
- M7 (tutorial, marking menu, navigation settings, one-click fixes, tooltips audit done): more fixes
  (sketch gaps, hole sizes).
- M8 (screen-reader names, keyboard-only modelling done): accessibility (scaling checks),
  localisation (set up, pseudo-language done; core messages and real languages left),
  performance, installers, v0.1.

## Ideas (unscheduled)

- Show the face name and "what made this face" when hovering a face, linking to the
  timeline step.
- "Open in slicer" (PrusaSlicer, OrcaSlicer, Cura) once STL/3MF export is in the UI.

## Robustness ideas (from the 2026-11-01 bug hunt)

- **B-030 Kernel signals**: OCCT can turn access violations inside its algorithms into catchable
  exceptions (`OSD::SetSignal` with `OCC_CATCH_SIGNALS`). Jigsmith catches OCCT's exceptions
  per step, but a hard crash inside OCCT would still end the program (autosave limits the loss).
  Not enabled yet: it installs process-wide signal handlers that can clash with Qt, the
  sanitizers and the rebuild thread; try it behind a setting with the fuzzers first.
- ~~B-031 Real crash test of recovery~~ done on Linux (`realCrashLeavesARecoverableAutosave`); macOS
  and Windows by hand.


## v0.2 leftovers

- **B-040 Mesh reduce**: inserting a mesh over 20,000 triangles is refused with a suggestion to
  simplify it elsewhere. Tried (2026-11-01): vertex clustering on a grid before sewing. A 40 mm ball
  came out 5 % small and not a valid solid (clustering makes edges shared by more than two
  triangles), and it took 20 s, so it was not kept. Next attempt: quadric edge collapse that
  refuses non-manifold collapses, or sewing large meshes in parallel chunks.
- **B-041 In-app Python console** (D-018): needs a Python runtime in every package.
- **B-042 Rigid groups, nested components** (as-built joints done); conic curves; mesh plane cut (Split
  with a plane already cuts solids made from meshes).
