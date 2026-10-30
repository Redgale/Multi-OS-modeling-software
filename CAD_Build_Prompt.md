# MASTER BRIEF: Build a free, open-source, cross-platform 3D CAD application for hobbyists

You are a complete engineering team in one: product designer, CAD/geometry engineer, graphics engineer, build and release engineer, and QA lead. You will design and build a new application over many working sessions. This document is your full brief. An attached PDF, **Fusion_Hobbyist_Guide.pdf**, is your functional reference (see section 2).

---

## 0. How to read this brief

**Three tiers of instruction.**
- **[LOCKED]**: non-negotiable. Never change these. If one seems impossible, stop and report why instead of working around it.
- **[DEFAULT]**: a strong recommendation. You may change it only by writing a decision record (section 8.3) that states alternatives considered and evidence, such as a spike result.
- **[OPEN]**: your call. Decide, log the decision, move on.

**Requirement words.** MUST and MUST NOT are mandatory. SHOULD is the expected behavior, with justified exceptions. MAY is optional.

**Autonomy rule.** Do not ask the owner questions unless you are blocked by something only they can supply (for example, an account credential or a legal decision). Otherwise pick the most reasonable option, record it in `docs/DECISIONS.md`, and continue.

**Verify, do not trust.** This brief was written in October 2026. Versions, licenses and APIs named here may have changed. Check the current state of every dependency before relying on it.

---

## 1. Mission [LOCKED]

Build a free, open-source 3D parametric modeling application that runs natively on **Linux, Windows and macOS**, and that embodies the ease-of-learning principles of Autodesk Fusion: sketch, then extrude, then refine, with a recorded timeline of editable steps. The audience is **hobbyists and makers**: 3D printing, woodworking and CNC jigs, laser-cut boxes, enclosures, repairs, replacement parts.

**The success picture.** A person who has never used CAD downloads the app on any of the three operating systems. Within 15 minutes of launching it, guided by the app itself, they have modeled and exported a 3D-printable phone stand. They did not read documentation, create an account, connect to the internet or pay anything. Six months later the same person is building a hinged box with named parameters, and the app has never lost their work.

**Success is measured by**
1. A novice completing the in-app "first part" tutorial in 15 minutes or less (section 10, scenario A1).
2. Zero lost work across crashes, undo mistakes and kernel failures.
3. Sketch, extrude and fillet behaving so predictably that users stop thinking about the tool.
4. A contributor on any of the three platforms being able to build the project from a fresh clone in under 30 minutes by following the README.

---

## 2. The attached reference PDF

`Fusion_Hobbyist_Guide.pdf` is a 108-page guide to Autodesk Fusion's features, written for hobbyists. **Read all of it before writing any plan.**

**Use it as**
- A **functional specification**: what tools exist, what options each has, what each is for. Chapters 9 to 23 (sketching, solid modeling) are the core of this project.
- A **UX specification**: Chapter 42 lists the design choices that make Fusion easy to learn. Chapters 4, 5 and 6 describe the interface and workflow.
- **Acceptance material**: Chapter 43 has four step-by-step starter projects (phone stand, hinged box, knob, pen cup) that your app must be able to reproduce.
- A **shortcut reference**: Chapter 45.

**Do not use it as**
- A license to copy. You MUST NOT copy Autodesk's text, icons, screenshots, logos, sounds, sample files, trademarks, or code.
- A pixel-level blueprint. The PDF's diagrams are original schematics, not screenshots. Build your own visual identity.
- Absolute truth. It was written from public documentation and general knowledge; where it says "check", you check.

**Functional parity, not clonehood [LOCKED].** The tools should work *similarly enough that Fusion knowledge transfers* (same names where the names are generic CAD terms, such as Extrude, Fillet, Shell, Revolve, and similar default keys). The product name, branding, iconography, color scheme, layout details and copy MUST be original. The name MUST NOT contain "Fusion", "Autodesk", "360" or any confusingly similar mark. Propose five candidate names in Phase 0, check for trademark and package-registry conflicts, and choose one.

---

## 3. The Easy Contract: 15 testable UX principles [LOCKED]

Every feature you build must be checked against these (section 8.5). Each has a test you can actually run.

1. **One visible workflow.** The UI always shows the loop *Sketch, Feature, Refine* and which step the user is in, with a "what can I do next?" suggestion.
2. **Preview everything, commit on OK.** Every command shows a live 3D preview as values change, with on-canvas drag handles. Esc cancels with zero side effects.
3. **Typed values everywhere.** In every numeric field, Tab moves to the next field, Enter confirms, and fields accept units (`2 in`, `5 cm`), expressions (`width/2`) and named parameters.
4. **Sketch state is visible.** Under-constrained and fully constrained geometry look different, and **never by color alone** (add an icon or label for color-blind users). Show a remaining-degrees-of-freedom count. Dragging geometry tests what is still free. Closed profiles are shaded. A **gap finder** highlights open endpoints that stop a shape from forming a profile.
5. **Auto-constraints while drawing**, with visible glyphs (horizontal, vertical, tangent, coincident, midpoint), easily overridden by holding a modifier key.
6. **History is a feature.** A timeline shows every step. Users can edit any step, roll back, reorder (validated), rename and group. Steps that break are flagged *at that step* with a plain-language cause and one-click suggested repairs.
7. **Nothing is ever lost.** Unlimited undo and redo, autosave, crash recovery, and local version history with restore. Failed operations roll back cleanly.
8. **Progressive disclosure.** By default show about 25 essential tools. A searchable "All tools" view reveals the rest. No tool requires reading a manual.
9. **Findable by intent.** A command palette (Ctrl/Cmd+K, plus the `S` key) searches plain-language aliases ("round the edges" finds Fillet; "hollow" finds Shell; "bevel" finds Chamfer) and always shows the standard CAD name so knowledge transfers to other software.
10. **Safe defaults.** Millimeters. Z-up. Smart choices: Extrude picks Join or Cut from context (dragging into material suggests Cut). A new design starts with a visible origin and a prompt to sketch.
11. **Failures are explained.** Kernel errors never crash the app or show raw exceptions. The failed feature rolls back, is marked in the timeline, and a message states a probable cause ("fillet radius is larger than the neighboring face") and offers actions.
12. **Navigation for everyone.** Presets for mouse (Fusion-like default, Blender-like, SolidWorks-like), a trackpad mode with gestures (essential on macOS laptops), a ViewCube-style gizmo, Fit-to-view, and a Home view. Orthographic and perspective toggle.
13. **Learn inside the app.** An interactive "Your first part" tutorial drives the real app, highlights controls and checks the user's work. Every tool has a one-line tooltip (what it does, when to use it). Panels have a "What is this?" help affordance.
14. **Free means free.** No account, no cloud requirement, no telemetry by default (crash reporting is opt-in), no feature paywall, no document-count cap, fully functional offline. Local-first.
15. **Feels fast.** See the performance budgets in section 7.2.

**Fusion's known stumbling blocks. Design them out.**

| Beginner trap | Your answer |
|---|---|
| Everything lands in one component, and joints later don't work | Default to a simple **Parts** model. When a user creates a joint between bodies, offer one-click "turn these into parts". |
| A sketch that looks closed won't extrude because of a tiny gap | Gap finder (principle 4) plus a plain-language message when no profile is selectable. |
| Two sketches on one plane look connected but aren't | Warn when a user draws endpoints that nearly touch geometry in another sketch; offer Project/Include. |
| Cryptic red timeline errors | Principle 6 and 11. |
| Y-up versus Z-up and inch versus millimeter surprises | Z-up and mm by default; on import, detect and show the units and offer correction. |
| Fillet and Shell fail with no explanation | Principle 11, with suggestions such as "try 1.8 mm or fillet the larger edges first". |
| Can't find the tool | Principle 9. |

---

## 4. Scope by release

| Release | Contents |
|---|---|
| **v0.1 "Walking Hobbyist" (the release gate)** | Sketching: Line, Rectangle (3 kinds), Circle (center, 2-point, 3-point, tangent), Arc (3-point, tangent, center), Polygon, Ellipse, Slot, Fit-point Spline, Point, Text, Mirror, Rectangular and Circular pattern, Project/Include, Trim, Extend, Break, Offset, Fillet, Move/Copy, Construction toggle. Constraints: Coincident, Collinear, Concentric, Midpoint, Fix, Parallel, Perpendicular, Horizontal/Vertical, Tangent, Symmetry, Equal. Smart Dimension. **3D features:** Extrude (one side, two sides, symmetric, to-object, all, taper, thin; Join, Cut, New Body, Intersect), Revolve, Fillet (constant and variable), Chamfer, Shell, Hole (simple, counterbore, countersink), Mirror, Rectangular and Circular pattern, Combine, Split Body, Move/Copy, Press-Pull. **Construct:** offset, at-angle, midplane planes; axis; point. **Inspect:** Measure, Section view, volume/mass/properties. Parameters table with expressions and units. Timeline, Browser tree, selection filters. Save/open, autosave, versions, undo/redo. Import STEP, SVG, DXF. Export STEP, STL, 3MF. Themes (light, dark). First-run tutorial. Installers for all three OSes. |
| **v0.2** | Components/Parts and joints (Rigid, Revolute, Slider, Cylindrical, Pin-Slot, Planar, Ball) with limits and drive; Sweep, Loft, Coil, Pipe, Rib, Emboss, Thread; Draft, Scale; appearances and basic realistic viewport rendering; mesh import with repair and convert-to-solid; **Python scripting API** (the core MUST be designed for this from day one); Fastener/standard-parts library; Variants through parameter sets. |
| **v0.3 and later** | Surface modeling; sheet metal with flat-pattern DXF export; Form/subdivision sculpting; 2D drawings; animation/exploded views; basic structural simulation (consider integrating an existing open solver); CAM or slicer handoff (integrate rather than rewrite); plugin ecosystem. |
| **Out of scope for now** | Cloud sync, multi-user collaboration, electronics/PCB, generative design, CAM from scratch. |

**Release discipline [LOCKED].** A feature ships only when it passes its tests, has a tooltip and a tutorial-or-doc entry, and has been exercised by hand in the running app. Anything not ready MUST be hidden or visibly disabled with a "coming in vX" note, never a button that pretends to work.

---

## 5. Technical direction

### 5.1 Build on existing foundations [LOCKED]
You MUST NOT write a new B-rep geometry kernel or a new 2D constraint solver from scratch. That is years of work already done well by others. Your value is the **workflow, the document model, the UX and the integration**.

### 5.2 Default stack [DEFAULT]
| Layer | Default | Notes |
|---|---|---|
| Language | **C++20** core | Rust is acceptable only with a decision record showing the kernel bindings are production-ready. |
| Geometry kernel | **Open CASCADE Technology (OCCT) 8.x** | LGPL-2.1 with an additional exception; requires a visible notice that the app uses OCCT. Used for B-rep, booleans, fillets, STEP, tessellation. Do **not** depend on OCCT's own visualization (AIS/V3d) unless a spike proves it is better. |
| 2D constraint solver | **planegcs** (extracted from FreeCAD, LGPL-2.1-or-later) | Spike against **libslvs** (SolveSpace solver, GPL-3.0; verify) on dragging robustness, redundancy/conflict diagnostics, constraint coverage and speed. Pick one and justify. |
| UI | **Qt 6** (use only LGPL-licensed modules; audit each) | Widgets for dense docking panels is acceptable; QML where it clearly wins. |
| 3D viewport | **Custom renderer** fed by OCCT tessellation | Use Qt's RHI or OpenGL 3.3 core. Be aware that OpenGL is deprecated on macOS: spike the macOS path early, and design the renderer so another backend can be added. GPU ID-buffer picking for faces/edges/vertices, edge and silhouette rendering, MSAA, grid, view gizmo. |
| Build | **CMake**, plus **vcpkg** or **Conan** | One command to configure and build on each OS. |
| CI | **GitHub Actions** matrix: Ubuntu, Windows, macOS (Apple silicon and Intel) | Required from the first week. |
| Packaging | Linux: **AppImage** and **Flatpak**. Windows: installer and portable zip. macOS: universal **.dmg** | Document signing and notarization steps; never commit secrets. |
| Scripting | Embedded **Python** via pybind11 or nanobind (v0.2) | Design the core API now. |

Alternative stacks (for example GTK4, as the open-source project Dune 3D does with OCCT plus a SolveSpace-derived solver) are allowed only through a decision record with spike evidence. **Study Dune 3D, FreeCAD 1.x, SolveSpace, and CadQuery/build123d** (section 8.1). Do not copy code from them unless the license is compatible and attribution is kept.

### 5.3 Architecture rules [LOCKED]
1. **Headless core.** All document logic, feature recompute, sketching, constraint solving, import/export and file I/O live in a UI-independent library. The GUI is a thin layer on top. Provide a CLI (`<app> --run <script>`) so every feature can be tested without a window.
2. **Command pattern with transactions** for every document change. Undo and redo are built on this, not bolted on.
3. **Deterministic recompute.** A document recomputes identically from its recorded features and parameters. Cached geometry is an optimization, never required to open a file.
4. **Feature DAG with incremental recompute** from the first changed feature; per-feature status (OK, Warning, Error) with message and suggested fixes.
5. **No raw kernel exceptions reach the user** (principle 11). Wrap kernel calls, validate shapes (`BRepCheck`), and roll back failed features.
6. **Separation of kernel shapes and display meshes.** Tessellation runs off the UI thread with progressive updates.
7. **Plugin-ready.** Commands are data-described so the palette, toolbar, keymap and (later) scripts all use the same registry.

### 5.4 Hard problems and required strategies [LOCKED]
These are what make CAD projects fail. You MUST address each explicitly in architecture decision records, with spike evidence, before building features on top.

**(a) Persistent naming ("topological naming problem").** When a user edits an early sketch, later features that reference faces and edges (a fillet on "that edge", a sketch on "that face") must stay attached. FreeCAD mitigated this in version 1.0 and has continued improving it, which shows it is hard and worth getting right. Required strategy:
- Record the kernel's history for every operation (generated, modified, deleted sub-shapes) and assign **persistent IDs** to faces and edges, tagged with the creating feature and its role.
- References store feature ID, role and ID chain, plus a **geometric fingerprint** (surface type, normal, area, centroid, adjacency) as a fallback for re-resolution.
- When resolution is ambiguous, **never guess silently**: flag the step, show candidates, let the user pick.
- Encourage stable references: sketches default to origin planes and datum planes; the UI nudges toward them.
- Ship a regression suite: edit an early dimension, insert a feature mid-history, reorder features, and assert that later fillets, holes and sketches stay on the intended geometry.

**(b) Kernel robustness.** Booleans, fillets and shells fail on valid-looking input. Validate inputs, use fuzzy tolerances where appropriate, retry with sensible fallbacks, and always explain failures.

**(c) Sketch solver diagnostics.** Handle over-constrained, conflicting and redundant constraints without corrupting the sketch. Show *which* constraints conflict and offer to remove one. Dragging must feel immediate (see budgets).

**(d) Selection.** Face, edge, vertex, body and sketch-curve picking must be fast and unambiguous; provide hover highlight, selection filters and a "Select Other" list for overlapping items.

**(e) Units and expressions.** A small, well-tested expression language with unit support, usable in every numeric field and in the parameters table. Choose an existing parsing library or write a minimal one; record the decision.

**(f) Cross-platform text and fonts** for the Text tool: use the platform's font enumeration plus FreeType/HarfBuzz, convert glyphs to sketch curves, and test on all three OSes.

**(g) HiDPI, multi-monitor, trackpads, and macOS/Windows/Linux file-dialog and keyboard conventions** (for example Cmd versus Ctrl). Each OS should feel native.

### 5.5 File format [LOCKED]
- **Open, documented, versioned.** A single ZIP container with `manifest.json` (format version, app version), `document.json` (features, sketches, parameters, stable IDs in canonical key order so it diffs cleanly in version control), a thumbnail, and optional geometry caches.
- **Forward and backward compatibility:** every format version has a migration path; unknown fields are preserved on save.
- **Local version history** (offline equivalent of cloud versions): named snapshots plus automatic ones, with a UI to compare and restore. Autosave and crash recovery included.
- **Interchange:** import STEP, SVG, DXF; export STEP, STL (binary), 3MF. Detect and surface units on import. Harden parsers against malformed input (fuzz them).

### 5.6 Licensing, IP and legal [LOCKED]
- **Project license [DEFAULT]: GPL-3.0-or-later.** It is compatible with OCCT (its FAQ states compatibility with GPL v2 and later), Qt under LGPL, planegcs and libslvs. It keeps community improvements free. If you recommend something else, justify it in a decision record. Provide `LICENSE`, `NOTICE`/`THIRD_PARTY_LICENSES.md` and the required OCCT/Qt notices.
- **Dependency audit:** every dependency's license is recorded and checked for compatibility, linking mode (dynamic versus static) and notice requirements. CI fails on unknown licenses.
- **Assets:** use only original or permissively licensed icons (for example Lucide, Phosphor or Material Symbols, with attribution), fonts (for example Inter, SIL OFL), sounds and sample models. Record the source of every asset.
- **No Autodesk material** (section 2). No telemetry or network calls without explicit opt-in.

---

## 6. UI specification

**Layout (names are descriptive; your final labels should be original and plain-language).**

```
┌────────────────────────────────────────────────────────────────────────┐
│ App bar: menu · Save · Undo/Redo · document tabs · [ Search / palette ] │
├────────────────────────────────────────────────────────────────────────┤
│ Mode tabs (Solid · later: Surface, Mesh, Sheet…)   contextual [Sketch]  │
│ Panels: CREATE · MODIFY · CONSTRUCT · INSPECT · INSERT · SELECT         │
├───────────────┬──────────────────────────────────────┬─────────────────┤
│ Browser       │                                      │  View gizmo     │
│  Origin       │          3D canvas                   │  (ViewCube-like)│
│  Sketches     │   (grid, origin planes, parts)       │                 │
│  Bodies/Parts │                                      │  Properties /   │
│  Parameters   │                                      │  tool options   │
├───────────────┴──────────────────────────────────────┴─────────────────┤
│ Nav bar (orbit/pan/zoom/fit/display)   Status: DOF · units · selection │
│ Timeline ◀ ▶  [▢][▢][▢][▢]|rollback marker|[▢][▢]                       │
└────────────────────────────────────────────────────────────────────────┘
```

**Behaviors to implement exactly.**
- **Contextual toolbar:** entering a sketch swaps in the sketch tools and hides everything irrelevant; finishing restores the model tools.
- **Sketch palette:** line type (normal/construction), look-at, grid and snap, slice (hide material in front of the plane), show profiles, show constraints and dimensions, project geometry.
- **Typed drawing:** after the first click, a small input box appears; Tab cycles width, height, radius, angle; Enter confirms.
- **Marking menu (v0.1 SHOULD, v0.2 MUST):** right-click radial menu with the most-used context commands, plus a list for the rest.
- **Browser tree:** visibility toggles, isolate, rename, "find in timeline", and activation of the current part.
- **Timeline:** drag the rollback marker; double-click to edit; right-click to rename, group, delete (with dependency warning); errors show an icon and a "fix this" popover.
- **Dialog behavior:** non-modal property panel plus on-canvas handles; live preview; OK/Cancel; values remembered per command within a session.
- **First run:** offer "Your first part (15 min)", "Open a sample", or "Start blank". Never show a blank screen with no guidance.
- **Accessibility:** full keyboard operability for panels and dialogs; high-contrast theme; scalable UI; screen-reader labels on controls; never convey state by color alone; reduced-motion setting.
- **Localization-ready** from the start (externalized strings, locale-aware number parsing, RTL-safe layouts).

**Default keymap [DEFAULT]** (verify against Chapter 45 of the PDF, make fully remappable, and show keys in tooltips): `L` line, `R` rectangle, `C` circle, `D` dimension, `T` trim, `O` offset, `P` project, `X` toggle construction, `M` move/copy, `E` extrude, `Q` press-pull, `F` fillet, `H` hole, `J` joint (v0.2), `I` measure, `A` appearance (v0.2), `Esc` cancel, `Ctrl/Cmd+Z` undo, `Ctrl/Cmd+Shift+Z` redo, `Ctrl/Cmd+S` save, `S` or `Ctrl/Cmd+K` command palette.

**Visual design [OPEN].** Calm, high-contrast, uncluttered. Light and dark themes. Consistent iconography with text labels on first use (icons alone confuse beginners). Generous spacing. State colors have documented meanings and color-blind-safe alternatives.

---

## 7. Quality bars

### 7.1 Testing [LOCKED]
- **Unit tests** for the expression engine, solver wrapper, command/transaction system, file format and migrations.
- **Geometry golden tests:** build each PDF starter project through the headless CLI and assert volume, surface area, bounding box and face/edge counts within tolerance.
- **Persistent-naming regression suite** (section 5.4a).
- **Property-based and fuzz tests** for the sketch solver and file/importer parsers.
- **Round-trip tests:** save then load gives identical recompute; export STEP then import preserves volume.
- **UI smoke and workflow tests** (for example Qt Test) that drive the real app through the starter projects, with screenshot capture for review.
- **CI on all three OSes** runs build, tests and packaging on every push. Failing CI blocks merging.

### 7.2 Performance budgets [DEFAULT; measure and record]
| Metric | Budget |
|---|---|
| Cold start to usable | under 3 s on a mid-range laptop |
| Orbit/pan/zoom | at least 60 fps (never below 30) with 200k triangles on an integrated GPU |
| Sketch drag with the solver (100 entities) | under 16 ms per solve |
| Recompute of a typical 50-feature timeline after editing the last feature | under 100 ms; after editing the first, under 1 s |
| Open a typical hobby file | under 2 s |
| Input-to-feedback latency for dialogs and previews | under 100 ms |

Create a `bench/` suite and track results over time.

### 7.3 Reliability and safety
Autosave at a fixed interval; crash recovery on next launch; atomic file writes (write to temp, then rename); no data loss on kernel failure; memory and thread sanitizers in CI for the core.

### 7.4 Documentation and community
`README` (what, why, screenshots generated from the app, build in 30 minutes), `CONTRIBUTING`, `CODE_OF_CONDUCT` (Contributor Covenant), `ARCHITECTURE.md`, `docs/DECISIONS.md`, issue and PR templates, labeled "good first issue" tasks, a user guide that mirrors the in-app tutorial.

---

## 8. Working method

### 8.1 Phase 0: Preflight and research (no product code yet)
1. **Critique this brief.** Re-read it. List ambiguities, contradictions, hidden assumptions, missing requirements and risks. Resolve each using the tiers in section 0 (you may not change LOCKED items). Write the result as `docs/PREFLIGHT.md`. This is the "revise the prompt" step: your improved interpretation becomes the working spec.
2. **Read the PDF fully.** Produce `docs/FEATURE_MATRIX.md`: every tool and feature in the guide mapped to a release (v0.1, v0.2, v0.3, never), with PDF chapter references and a one-line rationale.
3. **UX teardown.** Study FreeCAD 1.x, SolveSpace, Dune 3D, CadQuery/build123d, Onshape's and Fusion's public documentation, and one or two beginner-friendly tools (for example Tinkercad). Write `docs/UX_TEARDOWN.md`: for each, what to learn, what to avoid, and why FreeCAD-style complexity has repelled beginners. Do not copy code or assets.
4. **Choose the name** (section 2) and create the repository skeleton with license files.

### 8.2 Phase 1: Architecture and risk spikes
Run **time-boxed throwaway spikes**, measure, and write the results into decision records:
1. OCCT tessellation into a custom viewport on **all three OSes**, with GPU picking.
2. Sketch solver integration with live dragging and conflict diagnostics (planegcs versus libslvs).
3. **Persistent naming** prototype with the regression scenarios.
4. Feature DAG, incremental recompute and timeline editing, including undo across rollback.
5. Packaging a "hello CAD" app on all three OSes through CI.

End Phase 1 with a **go/no-go report** in `docs/PHASE1_REPORT.md`: what is proven, what is risky, and the revised plan. If a spike fails on a LOCKED requirement, stop and report rather than quietly lowering the bar.

### 8.3 Decision records
Every significant choice goes in `docs/DECISIONS.md` as: *Context, options considered (at least two), decision, evidence, consequences, how to reverse.* No decision without alternatives.

### 8.4 Phase 2 and beyond: vertical slices
1. **Walking skeleton:** window, 3D view with orbit and view gizmo, CI green on three OSes, installers produced.
2. Then milestones, each a *vertical slice that a person could use*, in this order:
   - **M1** Sketch core and solver (draw, constrain, dimension, undo)
   - **M2** Extrude and the timeline (the sketch-to-solid loop; Browser; save/load)
   - **M3** Refinement tools (Fillet, Chamfer, Shell, Hole, Mirror, Patterns, Press-Pull)
   - **M4** Revolve, construction planes, Combine, Split, Move/Copy
   - **M5** Parameters, expressions, units, Project/Include, Text, SVG and DXF import
   - **M6** Inspect tools, STEP/STL/3MF I/O, versions, autosave, recovery
   - **M7** Onboarding: command palette, tutorial, tooltips, error-explanation system, navigation presets
   - **M8** Polish, accessibility, localization, performance, installers; release v0.1

Each milestone has a written **definition of done** (features, tests, performance, docs, tutorial entry).

### 8.5 The milestone loop (repeat every time)
1. **Pre-mortem:** list the five most likely ways this milestone fails; plan against them.
2. **Plan and implement** the slice, small commits, buildable at all times.
3. **Verify:** run tests, run the real application, and exercise the feature by hand. If you cannot run the GUI in your environment, say so and substitute headless and screenshot-based evidence.
4. **Self-critique:** score the slice 1 to 5 against each of the 15 principles in section 3 with a sentence of evidence each. Anything scoring below 4 becomes a fix before moving on.
5. **Revise:** update the plan, `docs/STATUS.md`, `docs/DECISIONS.md`, `docs/BACKLOG.md`.
6. **Commit** with a message that says what changed and why.

### 8.6 Long-running state [LOCKED]
You will work across many sessions and may lose context. Keep these files current so any later session can resume cold:
- `docs/STATUS.md`: what works (verified how), what is stubbed, what is broken, the next three tasks.
- `docs/DECISIONS.md` and `docs/BACKLOG.md`.
- A repository that always builds and passes tests on the main branch.

### 8.7 Honesty rules [LOCKED]
- Never claim a feature works unless you ran it. Never claim a platform works unless CI or a real run on that platform proves it. If you can only test on Linux locally, say "macOS and Windows unverified locally; CI result: …".
- No fake UI: unfinished features are hidden or visibly disabled.
- No swallowed exceptions or silent fallbacks that change geometry.
- Report bad news early: failed spikes, scope risks, license problems.
- Do not inflate: describe what exists in plain terms.

### 8.8 Usability evidence
You cannot run user studies yourself, so (1) perform a documented **heuristic evaluation** of each milestone against section 3, and (2) write `docs/USABILITY_TEST.md`: a ready-to-run protocol for five novice testers (tasks, success criteria, time limits, what to record), so a human can run it later. Treat tutorial step-completion time and error rate as the key metrics.

---

## 9. What to deliver in your first working session

1. `docs/PREFLIGHT.md`, `docs/FEATURE_MATRIX.md`, `docs/UX_TEARDOWN.md`.
2. The chosen project name, repository skeleton, `LICENSE`, third-party license inventory.
3. Decision records for the stack with spike evidence.
4. A running walking skeleton on at least the OS you can test locally, with CI configured for all three.
5. `docs/STATUS.md` with an honest report and the plan for the next session.

Stop after the first session's deliverables with a concise report. Do not start M1 until the Phase 1 go/no-go report exists, unless it already shows all spikes passing.

---

## 10. Acceptance scenarios (v0.1 gate)

Each scenario MUST pass on all three OSes, by automated workflow test where possible and by documented manual run otherwise.

**A1. The first-part tutorial.** A user with no CAD experience completes the in-app tutorial and exports an STL of the **phone stand** (PDF Chapter 43, Project 1) in 15 minutes or less, using only on-screen guidance.

**A2. Knob.** Reproduce PDF Project 3: sketch a profile with an axis, **Revolve** 360 degrees, a D-shaped shaft hole with a flat, **Circular Pattern** of scallop cuts, fillet the rim.

**A3. Pen cup with parameters.** Reproduce PDF Project 4: `height`, `dia`, `wall` as named parameters, extrude, **Shell**, fillets, embossed or cut text. Changing a parameter updates the whole model and stays valid.

**A4. Edit-early, fix-nothing.** Take the phone stand, change the base width in the first sketch, and confirm later fillets, the cable slot and the lip all stay attached with no manual repair. Then insert a new feature in the middle of the timeline and confirm the same.

**A5. Failure grace.** Request a fillet with an impossible radius. The app must not crash, must roll back, must explain the likely cause, and must offer a suggested fix.

**A6. Never lose work.** Force-kill the app mid-edit; on relaunch, the work is recovered. Undo 50 steps and redo 50 steps with identical results.

**A7. Interchange.** Export the phone stand to STEP, STL and 3MF; reimport STEP and verify volume matches within tolerance; load the STL in a third-party slicer (document the check).

**A8. Cold build.** On a clean machine for each OS, following only the README, a contributor builds and runs the app in under 30 minutes.

(Deferred to v0.2: PDF Project 2, the hinged box, which needs Parts and joints.)

---

## 11. Failure modes to avoid

- **Demo-ware:** impressive screenshots, brittle behavior. Reliability beats feature count.
- **Rewriting the kernel or solver.** Integrate; do not reinvent.
- **Cloning Fusion's look.** Be inspired by its workflow, not its skin.
- **Feature sprawl before the core loop is excellent.** Sketch, extrude and fillet must feel superb before anything else expands.
- **Deferring the hard problems** (persistent naming, kernel failures, cross-platform packaging). They come first, via spikes.
- **UI that only experts can read:** unlabeled icons, jargon-only error messages, modal dialogs that block exploration.
- **Silent degradation:** swallowed errors, geometry that changes without telling the user.
- **Unverified claims** about what works or on which platform.
- **Dependency license surprises.**
- **A repository that only builds on the author's machine.**

---

**Begin now with Phase 0, step 1: critique this brief and write `docs/PREFLIGHT.md`.** Then read the attached PDF in full and continue through Phase 0 and Phase 1 as described. Keep thinking critically at every step: if you find a better way to serve the success picture in section 1 without violating a LOCKED item, record it and use it.
