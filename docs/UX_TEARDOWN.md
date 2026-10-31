# UX teardown

*Phase 0, step 3. What to learn from, and what to avoid in, the tools hobbyists
compare us with. Sources: public documentation and the projects' own manuals, release
notes and forums, from general knowledge current to late 2026 (no hands-on session in
this cloud environment, which has no display for third-party GUI apps). No code or
assets were copied from any of them.*

## Summary: the five lessons that shape Jigsmith

1. **One obvious loop beats many workbenches.** Fusion and Onshape win beginners
   because "sketch, extrude, refine" is always the visible path. FreeCAD's workbench
   switching is the most-cited reason people give up. Jigsmith has one modelling
   environment plus a contextual sketch mode, and the loop is drawn on screen.
2. **Errors must say what to do.** Every tool studied produces failures; the friendly
   ones (Onshape) highlight the failing feature and say why. FreeCAD and SolveSpace
   often show solver or kernel language. Jigsmith's rule: plain sentence, cause, action
   (D-009).
3. **Typed values while drawing remove a whole dimensioning step.** Fusion's
   Tab-between-fields habit is the biggest speed-up for beginners.
4. **Persistent naming is a UX feature, not just an engineering one.** The "my fillet
   moved" experience in FreeCAD before 1.0 drove people away. It must be invisible when
   it works and loud and specific when it cannot decide.
5. **Free has to mean free.** Fusion's personal-use limits (PDF ch. 2: cloud lock-in,
   format limits, the reported 10-document cap) and Onshape's public-documents-only free
   plan are the gap Jigsmith fills. Local files, no account, everything included.

## FreeCAD 1.x

- **Learn:** The open-source kernel stack works (OCCT plus planegcs, which we reuse).
  The Sketcher's solver diagnostics (which constraints conflict, redundant, DOF count)
  are best in class. The 1.0 topological-naming work (element maps carried through
  history) shows the approach: names derived from history, not indices. The integrated
  Assembly workbench in 1.0 shows the value of shipping assemblies in the box. The
  Python console and macro recording make everything scriptable.
- **Avoid:** Workbench overload: Part, Part Design, Sketcher, Draft, Assembly and more,
  with overlapping tools (three ways to make a cylinder) and a "Body" concept that
  confuses newcomers ("why can't I extrude here? activate the body"). Task panels that
  are modal in practice, dense property editors exposing every attribute, and default
  views that are not Z-up-hobby friendly. Raw kernel messages ("BRep_API: command not
  done") and red timeline icons without repairs. Settings sprawl.
- **Why it repels beginners:** It exposes the *implementation* structure (document
  objects, links, placement matrices) instead of a *task* structure. Every concept is
  available, and none is privileged. Jigsmith keeps FreeCAD's engine ideas and hides
  its object model behind a single task flow.

## SolveSpace (and libslvs)

- **Learn:** Speed and directness. Sketches are tiny, the constraint solver is fast
  and robust while dragging, and failures highlight the offending constraints. A
  single small executable, and keyboard-first with a letter key for almost every tool.
  Its solver library is a credible alternative to planegcs (spike in D-004).
- **Avoid:** Non-standard terminology and UI conventions ("groups", "workplanes",
  text-window property editing), no fillets/chamfers on solids (its own NURBS kernel
  is limited: booleans sometimes fail on curved geometry), and a look that intimidates
  hobbyists. GPL-3.0 libslvs is compatible with us but its kernel is not reusable.

## Dune 3D

- **Learn:** It proves the stack Jigsmith also uses (OCCT for solids plus a
  SolveSpace-derived solver) can feel modern, with GTK4 and a clean, keyboard-driven UI.
  It has good in-canvas constraint glyphs, a command palette-style action bar, and a
  sane document model (groups in order). It is honest about limited scope.
- **Avoid:** Still expert-oriented. It assumes you know the SolveSpace mental model
  (groups, workplanes, entities), there is no guided first run, and tools are found
  by key rather than by intent. Linux-first; Windows is secondary and macOS less
  mature. A GTK stack is harder to make native on macOS than Qt (D-003).

## CadQuery / build123d

- **Learn:** Code-CAD on OCCT: a clean, documented API with *selectors* ("the top face",
  `faces(">Z")`), which are a persistent-naming strategy users can read. build123d's
  builder contexts and named locations are elegant. It is excellent for parametric part
  families and testable geometry. Jigsmith's script format and selectors
  (`face:e1/end`, `feature:e3`) borrow the idea of human-readable, semantic references
  (no code copied).
- **Avoid:** Code as the *only* interface excludes most hobbyists. Selector strings like
  `">Z"` silently pick a different face when geometry changes, which is the very
  failure Jigsmith must flag rather than hide.

## Onshape

- **Learn:** Browser-native and fast, with excellent feature-failure UX: the failing
  feature turns red, a tooltip says why, and later features show they depend on it.
  Rollback bar and feature list behave predictably. FeatureScript shows the value of
  features defined as code over a stable API (our registry and v0.2 Python API).
  Version graph and branches are first-class. Variable studios and configurations
  (parameter sets).
- **Avoid:** Cloud-only: no offline use, account required, and free-plan documents
  are public. Multi-part-studio vs. assembly concepts take time to learn. Jigsmith
  offers local version history instead of cloud versions.

## Autodesk Fusion (from public docs and the reference guide)

- **Learn (functionally, not visually):** The guide's chapter 42 list is right: timeline
  with rollback, live previews with OK/Cancel, colour-coded sketches, automatic
  constraints, typed values with Tab, the marking menu and command search, contextual
  tabs, and recoverable versions. Defaults transfer: E extrude, F fillet, D dimension,
  L line, S search. These keys and generic tool names are kept so Fusion knowledge
  transfers.
- **Avoid (the guide's own "watch out" boxes and chapter 46):** everything landing in
  one component; sketches that look closed but are not; two sketches on one plane that
  look joined; cryptic red timeline errors; Y-up vs Z-up and inch vs mm surprises;
  fillet and shell failures without a suggested value; tools that cannot be found. The
  Easy Contract table in the brief answers each one. Also avoid: cloud dependence,
  sign-in, licensing limits, and feature paywalls.
- **Not copied:** product name, icons, colours, layout specifics, the ViewCube's cube
  shape (Jigsmith uses an axis gizmo), copy text and screenshots.

## Tinkercad

- **Learn:** The fastest path from zero to a printable object for children and adults:
  drag shapes, make holes by changing a shape to "hole", group to combine. No
  vocabulary needed, an always-visible workplane with a ruler, and lessons inside the
  app that check your work. Jigsmith's first-run tutorial (M7) takes this "learn by doing
  in the real app" idea.
- **Avoid:** No history, no parameters, no constraints. Hobbyists outgrow it in weeks.
  Jigsmith aims to be the tool you move to, so the first 15 minutes must feel this easy
  without giving up parametrics.

## What this means for Jigsmith's UI (decisions already taken in the code)

| Lesson | Implemented now | Planned |
|---|---|---|
| Visible loop | workflow strip with next step | tutorial drives it (M7) |
| Find by intent | palette with plain-language aliases, standard CAD name always shown | intent tests per release |
| Progressive disclosure | toolbar shows essential tools only; "All tools…" | collapse unused panels (M8) |
| Honest availability | unbuilt tools disabled with "coming in Mx" | — |
| Status not by colour alone | ✓ ⚠ ✕ ⏸ glyphs plus words | sketch constraint glyphs (M1) |
| Failures explained | plain messages and tested suggestions (fillet radius search) | "fix this" one-click actions (M7) |
| Navigation for everyone | four presets; right-drag orbit fallback; pinch zoom | settings page, gesture tests on macOS |
| Local-first | `.jig` files, autosave, recovery prompt | local versions (M6) |
