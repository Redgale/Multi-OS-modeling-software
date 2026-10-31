# Backlog

Ordered by priority within each group. Items carry their origin (brief section,
decision or finding). Move items to STATUS.md when started.

## Owed from Phase 1 (do before or alongside M1)

- **B-001 libslvs spike** (D-004): build SolveSpace's libslvs and run the same
  `tests/test_solver.cpp` contract plus the drag bench against it. Blocked this session by
  the environment's policy on building external code.
- ~~B-002 Reorder regression tests~~ done in session 2 (`Document.ReorderStepsSafely`, UI
  `reorderTimeline`). Remaining: name-resolution checks for reorders inside the phone stand.
- **B-003 Windows and macOS GUI evidence**: the CI GUI smoke test runs only on Linux
  (xvfb). Add a Windows screenshot run (the runner has a desktop session) and a macOS
  offscreen run; manual check of trackpad pinch on a real Mac.
- **B-004 macOS renderer path**: verify GL 4.1 core on Apple silicon; spike a Qt RHI
  (Metal) backend for `Viewport`.
- **B-005 Universal macOS binary**: CI builds arm64 and Intel separately; add a
  `lipo`-merged universal .dmg and document signing/notarisation (secrets never in repo).
- **B-006 Windows installer** (brief: installer + portable zip): WiX or Inno Setup.
- **B-007 Flatpak manifest** (brief: AppImage and Flatpak).
- ~~B-008 Endpoint tangency~~ done (angle via the shared point); collinear/concentric done.
- **B-012 OCCT 8 deprecations**: `TopTools_*`/`TColStd_*` typedef headers are deprecated in
  8.0 (warnings on Windows). Switch to `NCollection_*` types once 7.6 support is dropped, or
  wrap them in one compatibility header.
- **B-011 Windows package DLL audit**: ship only the DLLs Jigsmith loads; list each licence.
- **B-009 Recompute off the UI thread** for long timelines with a progress indicator
  (5.3.6), once any real design exceeds 100 ms.

## M1 Sketch core and solver (in progress: core done in session 2)

Done: sketch mode, plane/face picker, look-at, Line/Rectangle/Centre rectangle/Circle/Point,
Dimension, typed values, snapping, automatic H/V, constraint commands, drag, DOF display,
conflict refusal, gap rings, profiles, construction toggle, delete.

- B-013 (all v0.1 drawing tools done, incl. tangent arc, 2/3-point circle, 3-point rectangle) constraints along
  ellipses and splines; trim/extend/break of them.
- B-014 (Trim, Extend, Break, Offset, sketch Fillet, Mirror, Move/Copy, Grid/Ring copy done) linked sketch
  patterns, rotate in the Move tool.
- B-016 Select and delete individual constraints (click a glyph); window selection; collinear,
  concentric; point-on-curve snapping while drawing; endpoint tangency via angle-via-point.

## M2 Extrude and the timeline (in progress: core done in session 2)

Done: Extrude and Fillet panels with live preview, Auto join/cut, edge picking, tangent chain,
timeline edit/rename/suppress/roll back/delete with dependents warning, Parameters dialog.

- B-015 UI test for the Parameters dialog and timeline context menu.
- B-017 Extrude drag handle on the canvas; two sides, to object, taper, thin.
- B-018 (reorder done in session 2) Timeline groups; Browser visibility/isolate/rename/find in timeline.
- B-019 (sketch window selection done) Selection filters, Select Other.

- B-020 Text: pick installed fonts by family name (fontconfig / DirectWrite / CoreText),
  bold/italic, multi-line, HarfBuzz shaping for complex scripts, text along a path.

## Later v0.1 milestones

- M3 (done in session 2: Fillet, Chamfer, Shell, Hole, Mirror of bodies, Patterns of
  features; hole clearance table and drill point): variable fillet, pattern of bodies,
  mirror of features, Press Pull.
- M4 (Revolve and the knob, A2, done in session 2): revolve to object, construction planes
  (offset/angle/midplane), axis, point, Combine, Split, Move/Copy.
- M5 (Parameters, Text, Project, SVG and DXF import done): linked (associative) Project.
- M6: Measure, Section, Properties with density; 3MF export; local versions; autosave
  preferences; STEP import UI with unit correction; parser fuzzing (SVG, DXF, STEP).
- M7: tutorial, tooltips audit, error-explanation actions, marking menu, navigation
  settings page.
- M8: accessibility pass (keyboard, screen-reader labels, scaling, reduced motion),
  localisation (Qt Linguist), performance, installers, v0.1.

## Ideas (unscheduled)

- Show the face name and "what made this face" when hovering a face, linking to the
  timeline step.
- "Open in slicer" (PrusaSlicer, OrcaSlicer, Cura) once STL/3MF export is in the UI.
