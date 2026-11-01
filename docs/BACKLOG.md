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
- **B-005 Universal macOS binary**: CI builds arm64 and Intel separately; add a
  `lipo`-merged universal .dmg and document signing/notarisation (secrets never in repo).
- ~~B-006 Windows installer~~ done (Inno Setup, tested in CI by silent install, run, uninstall); try
  on a clean machine by hand.
- **B-007 Flatpak manifest** (brief: AppImage and Flatpak).
- ~~B-008 Endpoint tangency~~ done (angle via the shared point); collinear/concentric done.
- **B-012 OCCT 8 deprecations**: `TopTools_*`/`TColStd_*` typedef headers are deprecated in
  8.0 (warnings on Windows). Switch to `NCollection_*` types once 7.6 support is dropped, or
  wrap them in one compatibility header.
- **B-011 Windows package DLL audit**: `packaging/windows/trim-dlls.ps1` (CI) keeps only DLLs reachable
  from the programs and Qt plugins and writes THIRD_PARTY_DLLS.txt (conda package + licence); the
  trimmed package renders, installs and runs in CI (run 41225131582). Review that list before the
  first release.
- **B-009 Recompute off the UI thread** for long timelines with a progress indicator
  (5.3.6), once any real design exceeds 100 ms.

## M1 Sketch core and solver (in progress: core done in session 2)

Done: sketch mode, plane/face picker, look-at, Line/Rectangle/Centre rectangle/Circle/Point,
Dimension, typed values, snapping, automatic H/V, constraint commands, drag, DOF display,
conflict refusal, gap rings, profiles, construction toggle, delete.

- B-013 done (all v0.1 drawing tools, incl. tangent arc, 2/3-point circle, 3-point rectangle; point-on and
  tangent constraints along ellipses and smooth curves; trim and break of them, extending lines to them).
  Left: circles and arcs tangent to ellipses; extending the curves themselves.
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

- B-020 Text (installed fonts by family and style, several lines, along any sketch curve done): HarfBuzz shaping for
  complex scripts, text along a path.

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
