# Phase 1 go/no-go report

*Session 1, 2026-10-30. Brief 8.2: what is proven, what is risky, the revised plan.
Evidence is linked to tests, benchmarks or screenshots in this repository; anything
not run is labelled **unverified**. CI results for each platform are recorded in
`docs/STATUS.md`, which is updated as runs complete.*

## Verdict

**GO for M1 (sketch core), with three conditions:** (1) the libslvs spike (B-001) runs
early in M1, before the interactive sketch UI depends on solver specifics; (2) CI stays green on all four platforms (it is green on Linux, macOS arm64 and Intel,
and Windows as of the end of session 1); (3) the reorder regression
tests (B-002) land in M2 with timeline editing.

No LOCKED requirement failed a spike.

## Spike results

| # | Spike (brief 8.2) | Result | Evidence |
|---|---|---|---|
| 1 | OCCT tessellation into a custom viewport on all three OSes, with GPU picking | **Pass on Linux** (Mesa llvmpipe, GL 3.3 core). Windows and macOS: builds in CI; **rendering unverified** (no display on CI runners) | `docs/images/*.png` from the real app; `Display.TessellationHasFaceIds`; viewport picks faces by ID buffer and shows the persistent face name |
| 2 | Sketch solver with live dragging and conflict diagnostics (planegcs vs libslvs) | **planegcs passes** every measured criterion. **libslvs not run** (build blocked in this environment) | `tests/test_solver.cpp` (DOF, conflict, redundant, drag, gap finder, 300-case property test); drag 3.4 ms median on 100 entities |
| 3 | Persistent naming prototype with regression scenarios | **Pass** for edit-early (four parameters), insert mid-history (two scenarios), combined edit/insert/edit, and removed-geometry reporting. Reorder not yet tested | `tests/test_naming.cpp` (7 tests) |
| 4 | Feature DAG, incremental recompute and timeline editing, including undo across rollback | **Pass** | `Document.*` tests; editing the last of 51 features rebuilds 1 feature in 51 ms, editing the first rebuilds all in 539 ms; undo/redo 50 steps identical |
| 5 | Packaging a "hello CAD" app on all three OSes through CI | **Pass on Linux (AppImage) and macOS arm64 + Intel (.dmg)**; Windows portable zip (dev artifact) also green, after moving to prebuilt conda-forge OCCT (vcpkg took 89+ min, D-013) | CI runs 2 and 5; `.github/workflows/ci.yml` |

## What is proven (on Linux, in this container and in CI)

- The headless core builds the PDF's phone stand from a 30-line script, from named
  parameters, with a valid single solid (golden volume, bounding box, 26 faces,
  62 edges).
- Sketch-to-solid loop: closed-region detection (including nested regions and
  ignoring dangling lines), extrude new/join/cut/intersect, symmetric and through-all,
  sketch on a face that follows the face when it moves.
- Fillet failures are explained, roll back, and come with a radius that is verified to
  work (A5 automated).
- STEP export → import keeps volume to 1e-6 relative; binary STL layout verified (A7,
  automated half).
- `.jig` save/load recomputes identically; unknown fields survive; newer formats are
  refused politely; 400 corrupted files per CI run never crash.
- All performance budgets that can be measured headlessly are met with wide margins
  (table below).
- Address and undefined-behaviour sanitizers are clean on the core test suite (CI).

## Performance (brief 7.2), this container, RelWithDebInfo, OCCT 7.6.3

| Metric | Budget | Measured | Note |
|---|---|---|---|
| Sketch drag solve, 100 entities | < 16 ms | 3.4 ms median, 4.6 ms max | `jigsmith-bench` |
| Recompute after editing last of 51 features | < 100 ms | 51 ms | rebuilds 1 feature |
| Recompute after editing first of 51 features | < 1 s | 539 ms | rebuilds all 51 |
| Open a typical hobby file (phone stand) | < 2 s | 78 ms | includes full recompute |
| Cold start to usable window | < 3 s | 0.7 to 1.4 s | Xvfb + software GL; not a "mid-range laptop" |
| Orbit at 60 fps with 200k triangles | ≥ 60 fps | **unverified** | needs a real GPU; bench scene planned in M8 |
| Input-to-feedback latency in dialogs | < 100 ms | n/a | no dialogs yet (M2) |

## What is risky

1. **macOS rendering** (OpenGL deprecated): unverified on hardware. Mitigation: B-004
   (RHI/Metal spike) is scheduled before M2's polish.
2. **Windows build time**: vcpkg compiling OCCT took over 89 minutes and was abandoned.
   Now prebuilt OCCT 8.0.1 from conda-forge (D-013). Risk left: the Windows package must
   bundle and audit OCCT's runtime DLLs (B-011).
3. **OCCT version spread** (7.6 on Linux, 7.9 on macOS, 8.0 on Windows): golden numbers
   may differ slightly; topology counts might differ after `UnifySameDomain`. CI will
   show it.
4. **Solver choice is one-sided** until B-001 runs.
5. **QPainter on core-profile GL** drew nothing on Mesa; worked around with a raster
   overlay. Other driver quirks are likely on Windows Intel GPUs; needs real-hardware
   testing (usability protocol machines).

## Self-critique against the Easy Contract (brief 8.5)

Scored 1 to 5 for **this walking skeleton**, with a sentence of evidence each. Below 4
becomes work before the slice that owns it is called done (not before M1 starts, since
most items are by design in later milestones).

| # | Principle | Score | Evidence | Owner |
|---|---|---|---|---|
| 1 | One visible workflow | 4 | Workflow strip shows Sketch → Feature → Refine with ▶ marker and a next-step hint computed from the timeline (screenshots). Needs tutorial integration. | M7 |
| 2 | Preview everything, Esc cancels | 1 | No command dialogs yet. | M2 |
| 3 | Typed values everywhere | 3 | Expression engine with units, parameters and plain errors is done and tested; not yet wired into UI fields. | M1/M2 |
| 4 | Sketch state visible | 2 | DOF, free geometry, conflicts and gaps computed in the core; browser shows "fully defined" per sketch; no sketch view yet. | M1 |
| 5 | Auto-constraints with glyphs | 1 | Not started. | M1 |
| 6 | History is a feature | 3 | Timeline with status glyphs, details with fixes, working rollback marker; no edit/rename/reorder/group yet. | M2 |
| 7 | Nothing is ever lost | 3 | Unlimited undo, atomic saves, autosave every 60 s with crash-recovery prompt, failed operations roll back. Versions not yet. Recovery prompt not yet tested by a forced kill in CI. | M6 |
| 8 | Progressive disclosure | 4 | Toolbar shows only essential tools; "All tools…" palette lists all 80; unbuilt tools disabled with milestone. | M8 |
| 9 | Findable by intent | 4 | Palette (Ctrl/Cmd+K, S) finds Fillet from "round the edges", Shell from "hollow", Chamfer from "bevel" (tested), always shows the standard name. | M7 |
| 10 | Safe defaults | 4 | mm, Z-up, origin and grid visible, blank design shows a hint. Auto join/cut not yet. | M2 |
| 11 | Failures explained | 4 | Plain messages, suggestions, no kernel text (tested); fillet suggests a verified radius. One-click "fix" actions not yet. | M7 |
| 12 | Navigation for everyone | 3 | Four presets, right-drag fallback, pinch zoom, gizmo, Home, Fit, ortho/persp. Trackpad not tested on a Mac. | B-003 |
| 13 | Learn inside the app | 2 | Tooltips for every registry tool with keys; What's This on panels; welcome dialog. No tutorial. | M7 |
| 14 | Free means free | 5 | No account, no network code at all, no telemetry, everything local; GPL-3.0. | — |
| 15 | Feels fast | 4 | All headless budgets met with margin; GUI frame rate unmeasured on real GPUs. | M8 |

## Revised plan

1. **M1 Sketch core** (next session): B-001 libslvs spike first; then sketch mode,
   drawing tools with typed input, auto-constraints with glyphs, Dimension, drag, DOF
   display, conflict panel, gap overlay, Trim/Offset/Fillet. Definition of done: the
   phone stand's base and back sketches can be drawn by hand in the app; tests for
   every tool; tooltip and doc entry each; screenshots on all three OSes.
2. **M2 Extrude and timeline**: Extrude dialog with preview and handle; timeline
   edit/rename/reorder (B-002) and "fix this" actions; Browser operations; selection
   filters.
3. Keep CI green on all four runners; add Windows/macOS GUI screenshots (B-003).
