# Backlog

Ordered by priority within each group. Items carry their origin (brief section,
decision or finding). Move items to STATUS.md when started.

## Owed from Phase 1 (do before or alongside M1)

- **B-001 libslvs spike** (D-004): build SolveSpace's libslvs and run the same
  `tests/test_solver.cpp` contract plus the drag bench against it. Blocked this session by
  the environment's policy on building external code.
- **B-002 Reorder regression tests** (5.4a): move a feature earlier/later (validated: a
  feature can't move before what it references), assert names still resolve.
- **B-003 Windows and macOS GUI evidence**: the CI GUI smoke test runs only on Linux
  (xvfb). Add a Windows screenshot run (the runner has a desktop session) and a macOS
  offscreen run; manual check of trackpad pinch on a real Mac.
- **B-004 macOS renderer path**: verify GL 4.1 core on Apple silicon; spike a Qt RHI
  (Metal) backend for `Viewport`.
- **B-005 Universal macOS binary**: CI builds arm64 and Intel separately; add a
  `lipo`-merged universal .dmg and document signing/notarisation (secrets never in repo).
- **B-006 Windows installer** (brief: installer + portable zip): WiX or Inno Setup.
- **B-007 Flatpak manifest** (brief: AppImage and Flatpak).
- **B-008 Endpoint tangency** in the solver wrapper via angle-via-point (finding in
  D-004), and collinear/concentric constraints.
- **B-010 Windows CI result**: first green Windows run with conda-forge OCCT 8.0.1 (D-011
  revised); fix any MSVC/OCCT 8 issues.
- **B-012 OCCT 8 deprecations**: `TopTools_*`/`TColStd_*` typedef headers are deprecated in
  8.0 (warnings on Windows). Switch to `NCollection_*` types once 7.6 support is dropped, or
  wrap them in one compatibility header.
- **B-011 Windows package DLL audit**: ship only the DLLs Jigsmith loads; list each licence.
- **B-009 Recompute off the UI thread** for long timelines with a progress indicator
  (5.3.6), once any real design exceeds 100 ms.

## M1 Sketch core and solver (next)

- Sketch mode (contextual toolbar, look-at, finish), entity drawing tools with typed
  values and Tab, auto-constraints with glyphs and a modifier override, Dimension tool,
  constraint tools, drag with live solve, DOF display and free-geometry styling (not by
  colour alone), conflict panel with "remove this constraint", gap finder overlay,
  Trim/Extend/Break/Offset/Fillet/Mirror/patterns, construction toggle, undo per action.

## M2 Extrude and the timeline

- Extrude dialog: live preview, drag handle, OK/Cancel, values remembered per session,
  two-sides, to-object, taper, thin, auto join/cut from drag direction.
- Timeline editing: double-click edit, rename, group, delete with dependency warning,
  reorder with validation, "fix this" popover with one-click actions.
- Browser: visibility, isolate, rename, find in timeline. Selection filters, window
  selection, Select Other.

## Later v0.1 milestones

- M3: Fillet (dialog, variable, tangent chain), Chamfer, Shell, Hole (+clearance table),
  Mirror, Patterns, Press Pull.
- M4: Revolve, construction planes (offset/angle/midplane), axis, point, Combine,
  Split, Move/Copy. Knob acceptance test (A2).
- M5: Parameters table UI, Project/Include, Text (FreeType/HarfBuzz spike, 5.4f), SVG and
  DXF import with unit detection. Pen cup acceptance (A3).
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
