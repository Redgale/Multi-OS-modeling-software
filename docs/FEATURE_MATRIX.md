# Feature matrix

Every tool and feature described in the reference guide (`Fusion_Hobbyist_Guide.pdf`,
108 pages, read in full on 2026-10-30), mapped to a Jigsmith release. Names are the
generic CAD names so knowledge transfers. Our own UI labels may be plainer (see the
command registry, `core/src/commands.cpp`).

Release key: **v0.1** = release gate, **v0.2**, **v0.3+** = later, **never** = out of
scope (with reason). Milestones (M1 to M8) are the v0.1 build order from the brief.
"Now" means it works in this build, and how.

## Interface and workflow (PDF ch. 1, 3 to 6, 42)

| Feature | Ch. | Release | Now | Rationale |
|---|---|---|---|---|
| Sketch, Feature, Refine loop shown in UI | 1, 6 | v0.1 | yes (workflow strip + next-step hint) | Easy Contract principle 1 |
| Application bar: file menu, save, undo/redo, document tabs | 4 | v0.1 (tabs M8) | menu, save, undo/redo | Core habit |
| Workspace selector (Design, Render, Animation, Simulation, Manufacture, Drawing) | 4 | Design only in v0.1 | — | Other workspaces are later releases |
| Toolbar tabs (Solid, Surface, Mesh, Sheet Metal, Plastic, Utilities) | 4 | Solid v0.1; Mesh v0.2; Surface, Sheet v0.3+; Plastic never | Solid panels | Scope by release |
| Panels: Create, Modify, Construct, Inspect, Insert, Select | 4, 13 | v0.1 | yes, from registry, unbuilt tools disabled | Principle 8 |
| Contextual sketch tab | 4, 9 | v0.1 M1 | **app**: a sketch toolbar replaces the 3D tools while a sketch is open | Behaviour "implement exactly" |
| Browser tree (settings, named views, origin, bodies, sketches) | 4, 7 | v0.1 | settings, origin, sketches, bodies, parameters | Named views in M8 |
| Browser visibility, isolate, rename, find in timeline | 7 | v0.1 M2 | **app**: body visibility (tick box + word), isolate, show all, rename; choosing a sketch or body finds its step in the timeline | Visibility and folded groups are saved with the design (not undo steps) |
| Timeline with icons, edit, rollback, reorder, group, playback | 4 | v0.1 (playback v0.2) | **app**: icons, edit, rename, suppress, roll back, delete, reorder (refused with a reason when it would break a step), named groups that fold | Principle 6 |
| Marking menu | 4 | v0.1 SHOULD (M7), v0.2 MUST | **app**: right click (no drag) in the view: eight commands in a ring, for modelling or sketching; click or number key 1–8; right drag still orbits | Brief |
| Quick-access search (S) | 4 | v0.1 | yes: Ctrl/Cmd+K and S, alias search | Principle 9 |
| Data panel, hubs, projects (cloud) | 3 | never | — | Local-first; replaced by files + local versions |
| Version history and restore | 3 | v0.1 M6 | **app**: named versions inside the .jig, list, restore (undoable) | Local snapshots (PREFLIGHT A-6) |
| Home start page | 3 | v0.1 | welcome dialog | First-run requirement |
| Interface languages | — | v0.1 M8 | Qt Linguist template, Settings choice, pseudo-language for testing; no reviewed language yet | docs/TRANSLATING.md |
| Navigation: pan, orbit, zoom to cursor, fit | 5 | v0.1 | yes, four presets; Settings page with a plain description of each, zoom direction and orbit speed | Principle 12 |
| ViewCube (faces, edges, corners, home, drag) | 5 | v0.1 | axis gizmo: 6 views + Home + Fit | Original design, not a cube clone |
| Visual styles (shaded, edges, wireframe variants) | 5 | v0.1 (two styles) M8 | shaded with edges | |
| Environment effects (shadows, AO) | 5 | v0.2 | — | Rendering is v0.2 |
| Orthographic/perspective | 5 | v0.1 | yes | |
| Grid and snaps display | 5 | v0.1 | grid | Snaps with sketching M1 |
| Objects visibility toggles | 5 | v0.1 M2 | **app**: per body in the Browser (saved with the design); construction geometry always shown | |
| Named views | 5 | v0.1 M8 | **app**: View ▸ Save view…; listed under Named views in the Browser (activate to go there; replace or delete from its menu); saved with the design | |
| Document settings: units, up axis | 8 | v0.1 (mm, Z fixed) | shown in browser | Safe defaults |
| Capture design history on/off | 8, 24 | v0.3+ | — | Direct modelling is later |
| Appearances and physical material | 8, 34 | v0.2 (density in v0.1 Properties M6) | density chooser in Properties (not stored in the design yet) | Mass needs density |

## Sketching (PDF ch. 9 to 12)

| Feature | Ch. | Release | Now | Rationale |
|---|---|---|---|---|
| Create sketch on plane | 9 | v0.1 M1 | **app** (click a plane) + scripts | |
| Sketch on planar face (parametrically attached) | 9 | v0.1 M1 | **app** (click a flat face) + scripts; stays attached in tests | Naming spike |
| Finish sketch | 9 | v0.1 M1 | **app** | |
| Look At | 9 | v0.1 M1 | **app** (automatic on entering a sketch, and a button) | |
| Slice | 9 | v0.1 M1 | **app**: a tick box in the sketch palette cuts the view at the sketch plane (the model is unchanged); finishing the sketch takes it away | |
| Show profile, points, dimensions, constraints | 9 | v0.1 M1 | **app** (toggles in the sketch panel) | |
| 3D sketch | 9 | v0.3+ | — | Sweep paths are v0.2 |
| Typed values with Tab while drawing | 9 | v0.1 M1 | **app** | Principle 3 |
| Line (with drag-to-tangent-arc) | 10 | v0.1 M1 | **app** (chains; press on the chain's end and drag for a tangent arc, then lines go on) | |
| Rectangle 2-point, 3-point, center | 10 | v0.1 M1 | **app** (corner, centre, 3-point at any angle) | |
| Circle center, 2-point, 3-point, 2/3-tangent | 10 | v0.1 M1 (3-tangent v0.2) | **app** (centre, 2-point, 3-point, tangent to two lines with a typed radius); 3-tangent v0.2 | Brief lists "tangent" |
| Arc 3-point, tangent, center | 10 | v0.1 M1 | **app**: center arc (typed radius and angle), 3-point arc, tangent arc from a loose end; scripts: `arc` | |
| Polygon (inscribed, circumscribed, edge) | 10 | v0.1 M1 | **app** + scripts: inscribed (corners on the circle) | |
| Ellipse | 10 | v0.1 M1 | **app** + scripts (`ellipse`): radii as dimensions; no point-on or tangent constraints along it yet | Solver sees its centre and axis end only (D-015) |
| Slot (center-to-center, overall, arc slots) | 10 | v0.1 M1 (arc slots v0.2) | **app** + scripts: center-to-center | |
| Fit-point spline | 10 | v0.1 M1 | **app** + scripts (`spline`): open or closed, drag points to reshape; no tangent handles or constraints along it yet | |
| Control-point spline | 10 | v0.2 | — | Not in brief's v0.1 list |
| Conic curve | 10 | v0.2 | — | Not in brief's v0.1 list |
| Point | 10 | v0.1 M1 | **app** | |
| Text (font, height, alignment) | 10 | v0.1 M5 | **app** + scripts: built-in Inter or a font file, size as an expression, angle, centred on a draggable point; edit by double-click (D-014). Installed fonts by family name and style (e.g. "DejaVu Sans Bold"; a Font choice in the Text tool); several lines with `\n`, each centred (B-020) | Fonts spike (5.4f) |
| Text on path | 10 | v0.2 | — | |
| Sketch mirror, circular and rectangular pattern | 10 | v0.1 M1 | **app** + scripts: Mirror (held symmetric); Grid and Ring copy (each copy keeps its shape; not linked) | |
| Project / Include, Intersect | 10 | v0.1 M5 (Project); Intersect v0.2 | **app** + scripts (`project`): an edge, or a face's whole outline, of an earlier body, as fixed lines, circles and arcs that follow the body when it changes (`linked=no` for a plain copy); slanted circles refused | Brief lists Project/Include |
| Include 3D geometry, intersection curve, project to surface | 10 | v0.2 | — | |
| Fillet (sketch) | 11 | v0.1 M1 | **app** ("Round corner") + scripts (`round`): line-line corners | |
| Trim, Extend, Break | 11 | v0.1 M1 | **app** + scripts: Trim (lines, circles, arcs); Extend (loose ends of lines and arcs); Break (at crossings) | |
| Sketch scale | 11 | v0.2 | — | Not in brief's list |
| Offset (chains) | 11 | v0.1 M1 | **app** + scripts: closed outlines (sharp corners) and single curves; not associative yet | |
| Move/Copy (sketch) | 11 | v0.1 M1 | **app** (click or typed shift and turn about the base point, Ctrl copies) + scripts (`angle= about=`) | |
| Stretch | 11 | v0.2 | — | |
| Delete | 11 | v0.1 M1 | **app** | |
| Change parameters from sketch | 11 | v0.1 M5 | **app**: Parameters on the sketch toolbar; the open sketch follows the change | |
| Constraints: coincident, collinear, concentric, midpoint, fix, parallel, perpendicular, horizontal/vertical, tangent, symmetry, equal | 12 | v0.1 M1 | **app**: all (collinear and concentric built from point-on-curve and coincident) | |
| Smooth (G2) | 12 | v0.2 | — | Needs splines |
| Auto-constraints with glyphs, override with modifier | 12 | v0.1 M1 | **app**: coincident (snap) and H/V, Ctrl overrides | Principle 5 |
| Sketch dimension (length, angle, diameter, radius, point-line) | 12 | v0.1 M1 | **app**: all but angle | |
| Driven (reference) dimensions | 12 | v0.1 M1 | model field exists | |
| Under/fully constrained display, not by colour alone | 12 | v0.1 M1 | **app** (marker shapes + DOF in words) | Principle 4 |
| Over-constrained diagnosis (which constraints) | 12 | v0.1 M1 | **app** refuses conflicting constraints with a message | 5.4c |
| Gap finder | 12, 46 | v0.1 M1 | **app** (red rings with gap size) | Beginner trap |

## Solid modelling (PDF ch. 13 to 23)

| Feature | Ch. | Release | Now | Rationale |
|---|---|---|---|---|
| New component | 13 | v0.2 | — | Parts model (PREFLIGHT A-3) |
| Extrude: one side, two sides, symmetric | 14 | v0.1 M2 | **app** + scripts: one side, other side, symmetric, two sides | |
| Extrude extents: distance, to object, all | 14 | v0.1 M2 | **app** + scripts: distance, all, up to a parallel plane or flat face (with an offset beyond it), up to a slanted plane or any curved face (cut off where it meets it) | |
| Extrude start: profile plane, offset, object | 14 | v0.1 M2 | profile plane | |
| Extrude taper | 14 | v0.1 M2 | **app** + scripts | |
| Extrude thin | 14 | v0.1 M2 | **app** + scripts: closed areas (inside/centred/outside) and open curves (lines, arcs, smooth curves; centred walls with square ends and mitred corners) | |
| Extrude ops: join, cut, new body, intersect | 14 | v0.1 M2 | **app**: all four | |
| Extrude new component | 14 | v0.2 | — | Components |
| Auto join/cut from drag direction | 14 | v0.1 M2 | **app**: Auto picks from the direction, typed or dragged with the arrow handle on the canvas (snaps to 0.5 mm or 1/32 in) | Principle 10 |
| Extrude from selected face | 14 | v0.1 M3 (via Press Pull) | **app** + scripts via Press Pull (flat faces, straight out or in) | |
| Press Pull | 14, 17 | v0.1 M3 | **app** + scripts (`presspull`): flat faces out (join) or in (cut); whole round faces (a hole's wall, a round boss) change radius; with Q, a fillet, chamfer or hole face opens that step; other curved faces not yet | |
| Revolve (full, angle, to object) | 15 | v0.1 M4 | **app** + scripts: full, angle, up to a plane or flat face the axis lies in, or up to a curved face in its way; origin axis, sketch line or construction axis | |
| Sweep, Loft, Rib, Web, Emboss, Coil, Pipe | 15 | v0.2 (Web v0.3+) | — | Brief v0.2 list |
| Thicken, Boundary fill | 15 | v0.3+ | — | Surface workflows |
| Derive, Insert into design | 13, 22 | v0.3+ | — | Multi-file projects |
| Primitives (box, cylinder, sphere, torus) | 13 | v0.2 | — | Sketch+extrude teaches better; low cost later |
| Create Form (T-splines) | 13, 26 | v0.3+ | — | |
| Create base feature | 13, 24 | v0.3+ | — | |
| Create PCB | 13 | never | — | Electronics out of scope |
| Hole: simple, counterbore, countersink; distance/all; drill point | 16 | v0.1 M3 | **app** + scripts: all three types, depth or through; flat bottom or 118° drill point; screw-size clearance table (ISO 273, US drill chart) with close/normal/loose fit | |
| Hole from sketch points | 16 | v0.1 M3 | **app** + scripts | |
| Clearance hole sizes | 16 | v0.1 M3 | **app** + scripts (`hole size= fit=`): metric (ISO 273) and US screw sizes, close/normal/loose fit, counterbore and countersink for the head | Hobby tip table |
| Tapped holes, Thread (modelled/cosmetic) | 16 | v0.2 | — | |
| Fillet constant | 18 | v0.1 M3 | **app** (edge picking, preview, one-click fix) + scripts | |
| Fillet variable | 18 | v0.1 M3 | **app** + scripts: one radius at each end of an edge | |
| Fillet chord length, setback corners, G2 | 18 | v0.2 | — | |
| Tangent chain selection | 18, 23 | v0.1 M3 | **app** | |
| Chamfer: equal, two distances, distance+angle | 18 | v0.1 M3 | **app** + scripts: all three | |
| Shell (inside/outside, openings) | 18 | v0.1 M3 | **app** + scripts (`direction=outside`): inside or outside, openings or closed hollow | |
| Draft | 18 | v0.2 | — | |
| Scale | 17, 18 | v0.2 | — | |
| Combine join/cut/intersect, keep tools | 17 | v0.1 M4 | **app** + scripts (`combine`) | |
| Offset face, replace face, split face, silhouette split | 17 | v0.3+ | — | Direct modelling |
| Split body (plane) | 17, 18 | v0.1 M4 | **app** + scripts (`split`): origin plane, construction plane, flat face, or a curved face that reaches through the body | |
| Move/Copy bodies (free, translate, rotate, point-to-point) | 17 | v0.1 M4 | **app** + scripts (`move`): typed X/Y/Z shift and turn about an origin or construction axis, point to point (construction points), copy, dragging X/Y/Z arrows in the view | |
| Align | 17 | v0.2 | — | |
| Delete body/feature | 17 | v0.1 M2 | **app** + scripts (`remove`): a step is deleted from the timeline (warning when later steps use it); a body is deleted from the Browser as a Remove step, so undo and rolling back bring it back | |
| Compute all | 17 | v0.1 | automatic (incremental recompute) | |
| Patterns: rectangular, circular (features, bodies) | 19 | v0.1 M3 | **app** + scripts: features (Extrude, Revolve, Hole) and whole bodies (separate or joined); axes and directions may be construction axes | |
| Pattern on path | 19 | v0.2 (done early) | **app** + scripts (`kind=path`): along a sketch's curves, spread or spaced, same direction or turning with the path | |
| Pattern compute options (optimised/identical/adjust) | 19 | v0.2 | — | Adjust only in v0.1 |
| Mirror (features, bodies) | 19 | v0.1 M3 | **app** + scripts: bodies and single steps, across origin planes, construction planes or a clicked flat face | |
| Construct: offset plane, plane at angle, midplane | 20 | v0.1 M4 | **app** + scripts (`plane`): from origin planes, construction planes or flat faces; angle around an origin or construction axis or straight edge | |
| Tangent plane, plane through edges/points, along path | 20 | v0.2 | — | Needed for sweep and pen cup emboss |
| Axis (through cylinder, two points, perpendicular) | 20 | v0.1 M4 | **app** + scripts (`axis`): along a straight edge, through a round edge or face, where two planes meet, through two points (or a point and the origin), square to a plane or flat face through a point | |
| Point (vertex, edges, planes, center) | 20 | v0.1 M4 | **app** + scripts (`point … at=x,y,z` or `through=`): typed coordinates (parameters allowed), start, end, middle or centre of an edge, centre of a face, where an edge crosses a plane | |
| Pattern construction geometry, user coordinate system | 20 | v0.3+ | — | |
| Measure | 21 | v0.1 M6 | **app** + scripts: size of a face/edge, distance, angle, centre-to-centre, X/Y/Z offsets | |
| Section analysis | 21 | v0.1 M6 | **app**: view-only cut along origin or construction planes, movable, flippable; cut faces filled in (capped) with the cut area shown | |
| Properties: volume, area, mass, center of mass | 21 | v0.1 M6 | **app**: volume, weight by material or density, filament length, area, size, centre of mass; CLI `report` | |
| Interference | 21 | v0.2 | — | Needs parts |
| Curvature comb, zebra, draft, curvature map, isocurve, accessibility, min radius | 21 | v0.3+ | — | Surface-quality tools |
| Component colours | 21 | v0.2 | — | |
| Insert SVG, DXF | 22 | v0.1 M5 | **app** + scripts: DXF (lines, arcs, circles, polylines with bulges; unit detection); SVG (filled paths and shapes, transforms, viewBox; as scalable artwork) | |
| Insert STEP | 22 | v0.1 M6 | **app** + scripts (`import step`): an Import step with one body per solid, kept inside the design; faces named `<step>/face:<n>` for later steps | |
| Insert mesh (STL/OBJ/3MF) | 22, 27 | v0.2 | — | Brief v0.2 |
| Attached canvas (image tracing) | 22 | v0.2 | — | Hobby-valuable; small |
| Decal | 22, 34 | v0.3+ | — | Rendering |
| McMaster-Carr, manufacturer parts | 22, 32 | never | — | Requires network and third-party catalogs |
| Insert fastener (standard parts) | 22, 32 | v0.2 | — | Brief v0.2: offline library |
| Select, selection priority, filters | 23 | v0.1 M2 | **app**: filter in the status bar (faces, edges, bodies) with details for each; Select Other (long press: every face under the cursor, nearest first) | |
| Window and crossing selection | 23 | v0.1 M2 | **app** in sketches (window left to right, crossing right to left); in 3D for edges while picking them (Fillet, Chamfer) faces (Shell), visible ones only, and bodies (Move) | |
| Freeform, paint selection, selection sets | 23 | v0.2 | — | |
| Select Other (overlapping) | 23 | v0.1 M2 | **app**: long press lists every face under the cursor, nearest first | 5.4d |

## Beyond solids, assemblies, presentation, making (PDF ch. 24 to 41)

| Feature | Ch. | Release | Rationale |
|---|---|---|---|
| Direct modelling mode | 24 | v0.3+ | History-based first |
| Surface tools (patch, ruled, trim, stitch, thicken…) | 25 | v0.3+ | Brief |
| Form / T-spline sculpting | 26 | v0.3+ (as subdivision) | Brief |
| Mesh: insert, repair, reduce, remesh, plane cut, convert to solid | 27 | v0.2 | Brief |
| Sheet metal with flat-pattern DXF | 28 | v0.3+ | Brief |
| Plastic tab (boss, snap fit, grill, lip and groove) | 29 | never as a tab; v0.3+ as plugins | Niche; plugin ecosystem |
| 3D print utility (send to slicer) | 29, 40 | v0.1 export (**done**: STL, STEP and 3MF with one named object per body); "open in slicer" v0.2 | Export is the core need |
| Scripts and add-ins (Python API) | 29 | v0.2 | Core designed for it now (commands are data) |
| AI assistant | 29, 42 | never (for now) | Requires network/cloud; violates "free means free" defaults |
| Components, joints (7 types), limits, drive, as-built, rigid group | 7, 30, 31 | v0.2 | Brief |
| Motion link, contact sets, motion study | 31 | v0.3+ | |
| Configurations | 33 | v0.2 as parameter sets ("variants") | Brief |
| Render workspace, appearances library | 34 | v0.2 (basic viewport rendering) | |
| Animation, exploded views | 35 | v0.3+ | |
| 2D drawings | 36 | v0.3+ | |
| Simulation (FEA) | 37 | v0.3+ via an existing open solver | Brief |
| Generative design | 38 | never | Out of scope (brief) |
| CAM | 39 | never from scratch; slicer/CAM hand-off v0.3+ | Brief |
| Electronics/PCB | 41 | never | Out of scope (brief) |

## Acceptance projects (PDF ch. 43)

| Project | Release | Tools needed | Now |
|---|---|---|---|
| 1. Phone stand | v0.1 (A1, A4, A7) | sketch, rectangle, polyline, slot, extrude join/cut/symmetric, sketch on face, fillet, export | **Builds headlessly** (`samples/phone_stand.jigs`); golden test passes; edit-early regression passes |
| 2. Hinged box | v0.2 | components, shell, project/offset, patterns, revolute joint, interference | — |
| 3. Knob | v0.1 (A2) | revolve, arc, trim, axis, circular pattern, cut, fillet, text | **builds completely** (`samples/knob.jigs`: revolve, D-bore from an arc, 12 scallops, top round, engraved "VOL"; golden and parameter tests) |
| 4. Pen cup | v0.1 (A3) | parameters, circle, extrude, shell, fillets, text + cut | **builds completely** (`samples/pen_cup.jigs`: maker's mark cut into the underside, sized from `dia`; golden test, parameter changes). Text wrapped around the wall needs Emboss (v0.2) |

## Shortcuts (PDF ch. 45)

All defaults from chapter 45 and the brief are in the command registry and covered by
the `DefaultKeysMatchTheBrief` test: L, R, C, D, T, O, P, X, M, E, Q, F, H, J (v0.2), I,
A (v0.2), Esc, Ctrl/Cmd+Z, Ctrl/Cmd+Shift+Z (Ctrl+Y also works on Windows and Linux),
Ctrl/Cmd+S, S and Ctrl/Cmd+K. Navigation defaults match chapter 5 (middle-drag pan,
Shift+middle-drag orbit, wheel zoom toward cursor, double-middle-click fit).
