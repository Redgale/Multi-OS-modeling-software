# Changelog

## Unreleased

- Pipe: a round rod or hollow tube along sketch curves, with no circle to draw.
- Coil: springs with round or square wire, either hand, on any plane.
- Basic shapes: box, cylinder, sphere and torus without drawing a sketch.
- Rib: a gusset from an open sketch edge to the body.
- Emboss: text or shapes wrapped onto a round face, raised or sunk.
- Construction planes: tangent to a round face, through three points, across a path.
- Draft: slope faces around a neutral plane for moulding and printing.
- Scale: bodies bigger or smaller about a point, evenly or per axis.
- Thread: real screw threads on shafts and in holes, metric pitches offered.
- Insert mesh: closed STL, OBJ and 3MF meshes become solids you can change.
- Picture to trace: a see-through photo or drawing on a plane.
- Joints: rigid, revolute, slider, cylindrical, ball and planar, with limits; Interference check.
- Tool panels scroll when taller than the window.
- Appearances: colour and finish per body, shown in the view and carried into 3MF.
- Python API: `import jigsmith` to build, measure, export and save designs (docs/PYTHON.md).
- Variants: named sets of parameter values, switched in one step.
- Fastener library: metric screws, nuts and washers (M2–M12), offline.
- Sample: the hinged box (parts, knuckles, pin and a hinge joint you open with a parameter).
- Pin-slot joints and an Animate button for joints; inserted meshes have their holes closed.
- Realistic look: softer light, a floor shadow, no drawn edges.
- Fixed: a fillet or chamfer on the edges of a loft or sweep could lose its edges when an earlier step changed.

## 0.1.0 (2026-11-01)

The first release of Jigsmith: free, open-source parametric CAD for hobbyists and makers.
Everything below is tested (see `docs/STATUS.md` for how each item was verified).

**Sketching**
- Line, rectangle (corner, centre, three-point), circle (centre, two- and three-point, tangent to
  two lines), arc (centre, three-point, tangent), slot, polygon, ellipse, smooth curve (spline),
  point, text (built-in or installed fonts, several lines, along any curve, joined and
  right-to-left scripts shaped by HarfBuzz).
- Constraints: coincident, point on curve, horizontal/vertical, parallel, perpendicular, tangent,
  equal, midpoint, symmetry, fix, collinear, concentric; also along ellipses and smooth curves.
  Dimensions with expressions and parameters; degrees-of-freedom display; conflicts explained.
- Trim, extend, break, offset (linked), round corner, mirror, move/copy, grid and ring copies
  (linked), project edges of bodies (following the model).
- Snapping to ends, middles and the grid; type sizes while drawing; metric and US units.

**Solids**
- Extrude (distance, through all, up to a face or plane, two sides, taper, thin walls, walls along
  open curves), revolve, hole (simple, counterbore, countersink, standard screw sizes, drill
  point), fillet (variable), chamfer, shell, press pull.
- Patterns (grid, ring, along a path), mirror, combine, split, move/copy (arrows, free drag in the
  view, point to point), remove bodies.
- Construction planes, axes and points.
- Click the closed areas of a sketch in the 3D view to choose what to extrude, revolve or sweep.

**From the v0.2 list, already in**
- Loft through two or more sketches; sweep along a path of sketch curves.

**Working with designs**
- Timeline with editing, reordering, suppression, rollback and groups; undo/redo of everything.
- Steps keep referring to the right faces and edges when earlier steps change.
- Rebuilds run in the background with a progress dialog; autosave per window and crash recovery.
- Files: `.jig` designs with named versions inside; STEP import and export; STL and 3MF export;
  SVG and DXF import; headless command-line tool and `.jigs` scripts (`docs/SCRIPTING.md`).
- Measure, properties (volume, weight, filament length), section view, named views.
- Dark (default), light and high-contrast themes; marking menu; find-a-tool search; guided first
  part tutorial; translatable (no translations shipped yet).

**Downloads** (all unsigned)
- Linux: AppImage (x86_64, glibc). Flatpak builds in CI but is not on Flathub yet.
- macOS: Apple silicon, Intel, and a universal build. To open the unsigned app, right-click it and
  choose Open (or System Settings ▸ Privacy & Security ▸ Open Anyway).
- Windows: installer (no administrator rights needed) and portable zip.

**Known limits**
- Tested in CI on every platform, but by hand mostly on Linux; Windows and macOS have not been
  tried on real machines with a GPU.
- Not yet: components and joints, threads, sheet metal, rendering, drawings; see
  `docs/BACKLOG.md` and `docs/FEATURE_MATRIX.md`.
