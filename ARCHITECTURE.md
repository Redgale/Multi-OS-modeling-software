# Architecture

Jigsmith is a **headless core with a thin UI** (brief 5.3). Everything a user can do
must be possible through `jig_core` without a window, so it can be tested on every OS
in CI.

```
 ┌──────────────── app/ (Qt 6 Widgets) ─────────────────┐   ┌── cli/ ──────────┐
 │ MainWindow: workflow strip, panels, browser,          │   │ jigsmith-cli      │
 │ timeline, properties, palette, autosave               │   │ --run script.jigs │
 │ Viewport: GL 3.3 renderer, ID-buffer picking, gizmo   │   └────────┬─────────┘
 └───────────────┬───────────────────────────────────────┘            │
                 │  CommandRegistry  ·  ScriptRunner  ·  Document     │
 ┌───────────────▼────────────────────────────────────────────────────▼──────┐
 │ core/ (jig_core, no Qt)                                                    │
 │  commands.*  registry of every tool (labels, aliases, keys, availability)  │
 │              + script runner (the CLI and tests use it)                    │
 │  document.*  Model (parameters, features, rollback) · transactions · undo  │
 │              incremental recompute with per-feature cache · JSON           │
 │  features.*  Sketch / Extrude / Fillet recompute, status + plain messages  │
 │  sketch.*    sketch entities & constraints · planegcs wrapper · gap finder │
 │  kernel.*    the only OCCT user: naming, profiles, extrude, booleans,      │
 │              fillet + failure diagnosis, mass props, tessellation, STEP/STL│
 │  expr.*      units and expressions                                         │
 │  io.*        .jig ZIP container (manifest.json + document.json)            │
 └───────────────┬───────────────────────────┬────────────────────────────────┘
                 │                           │
      third_party/planegcs (LGPL)     Open CASCADE Technology (LGPL+exception)
```

## Data flow

1. A UI action or script line is a **command**. It calls
   `Document::transact(label, edit)` with a function that edits a *copy* of the
   **Model** (plain data: parameters, features with JSON params, rollback marker).
2. If the edit returns an error, nothing changes. Otherwise the old model goes on the
   undo stack, and the document **recomputes**.
3. **Recompute** walks the active features. Each feature's cache key is (previous key,
   the feature's JSON, the values of the parameters it mentions); unchanged prefixes are
   reused. `applyFeature` turns (previous `ModelState`, feature) into (new state, status).
   States are immutable and shared, so undo and rollback are cheap.
4. A failing feature returns no state: geometry stays as it was before that step, the
   step gets an Error status with a plain message and suggestions, and later steps run
   against the earlier geometry.
5. The UI reads `Document::state()` and statuses, and the viewport tessellates bodies on
   worker threads.

## Persistent naming (D-005)

Faces carry names like `e1/side:7`, `e1/end`, `f1/fillet:E[...]`, made by the operation
that created them and carried through later operations with OCCT's history (composed
with `UnifySameDomain`'s history after booleans). Edges are named after their two
faces. References (sketch-on-face, fillet edges) store the name plus a geometric
fingerprint. `resolve()` returns Exact, Rebound (warning), Ambiguous or Missing
(errors). It never silently guesses.

**Rule for new features:** every operation in `kernel.cpp` must name every face it
creates. Leftovers get `<feature>/new~k`, which tests treat as a smell.

## Threading

The document is single-threaded. Tessellation for display uses `std::async` per body;
the viewport collects results on the UI thread (no cross-thread callbacks into
widgets). Meshing works on a copy of the shape because OCCT stores triangulations
inside shared shape data.

## Rules (from the brief, enforced in review)

1. No Qt in `core/`. 2. Every document change is a transaction. 3. Recompute is
deterministic; caches are never required. 4. No OCCT exception leaves `kernel.cpp`.
5. Every user-visible message is a plain sentence; technical text goes in `detail`.
6. Commands are data in the registry; UI buttons, palette, keys and scripts all use it.
7. Unfinished tools stay disabled with a "coming in" note.
