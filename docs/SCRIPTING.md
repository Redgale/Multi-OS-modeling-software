# Jigsmith scripts (`.jigs`)

A `.jigs` file is a list of commands, one per line. Scripts drive the same document
and commands as the app, so they are how features are tested headlessly on every OS,
and how samples are stored. The Python API (v0.2) will wrap the same commands.

```bash
jigsmith-cli --run samples/phone_stand.jigs --report - --save stand.jig
```

Syntax: `verb key=value key="value with spaces"`. `#` starts a comment. Any length,
angle or position accepts an expression with units and parameter names (`width/2`,
`2 in`, `75 deg`). A bare number in a length means millimetres; in an angle, degrees.
The script stops at the first failing line, and the error names the line number.

## Parameters

| Command | Example |
|---|---|
| `param <name> = <expr> [kind=length\|angle\|number]` | `param wall = 2.4 mm` · `param lean = 75 deg` · `param n = 6 kind=number` |

A new parameter's kind is inferred from its value (`75 deg` is an angle). A change that
would make a parameter invalid (unknown name, or depending on itself) is refused and
nothing is committed.

## Sketches

| Command | Meaning |
|---|---|
| `sketch id=s1 plane=XY\|XZ\|YZ [offset=<len>] [name="…"]` | New sketch on an origin plane (XY = floor, XZ = front, YZ = side), optionally offset |
| `sketch id=s3 on=<face name>` | New sketch on a named flat face, e.g. `on=e1/end` (top of extrude e1). Stays attached when earlier steps change |
| `rect [sketch=s1] center=x,y w=<len> h=<len>` | Centre rectangle, fully dimensioned |
| `rect [sketch=s1] corner=x,y w=<len> h=<len>` | Corner rectangle |
| `circle [sketch=s1] center=x,y d=<len>` | Circle by centre and diameter |
| `polyline [sketch=s1] points="x1,y1; x2,y2; …" [closed=yes\|no] [dims=yes\|no]` | Connected lines; with `dims=yes` every point is dimensioned from the origin |
| `slot [sketch=s1] c1=x,y c2=x,y width=<len>` | Centre-to-centre slot |
| `line [sketch=s1] p1=x,y p2=x,y [construction=yes] [name=<n>]` | One line, ends dimensioned. Name it to use it as a revolve axis (`axis=line:<n>`) |
| `point [sketch=s1] at=x,y` | A point (hole centres) |
| `polygon [sketch=s1] center=x,y d=<len> [sides=6]` | Regular polygon; `d` is the diameter of the circle through the corners. One corner points straight up |
| `arc [sketch=s1] center=x,y r=<len> from=<angle> to=<angle> [chord=yes]` | Counter-clockwise arc; `chord=yes` closes it with a straight line (a "D" shape) |
| `constrain [sketch=s1] type=<t> a=<id> [b=<id>] [c=<id>] [value=<expr>]` | Raw constraint by entity id (for tests) |

`sketch=` defaults to the most recently created feature.

## Features

| Command | Meaning |
|---|---|
| `extrude id=e1 sketch=s1 distance=<len> [direction=one\|reverse\|symmetric] [extent=distance\|all] [op=new\|join\|cut\|intersect] [profiles=all\|<key>;<key>] [target=<body id>]` | Extrude closed areas of a sketch. `extent=all` goes through everything |
| `fillet id=f1 edges="<selector>;<selector>" radius=<len> [body=<id>]` | Round edges. Selectors: `all`, `face:<face name>` (all edges of that face), `feature:<id>` (edges touching faces made by that feature), `name:<edge name>` |
| `chamfer id=c1 edges="<selector>;…" distance=<len> [body=<id>]` | Bevel edges (equal distance); same selectors as fillet |
| `shell id=sh1 faces="face:<name>;…" thickness=<len> [body=<id>]` | Hollow a body leaving walls; the listed faces are removed (left open). `faces=none` makes a closed hollow |
| `revolve id=r1 sketch=s1 axis=X\|Y\|Z\|line:<name> [angle=<angle>] [op=new\|join\|cut\|intersect] [profiles=…]` | Spin closed areas around an axis (default a full turn). The outline must stay on one side of the axis |
| `hole id=h1 sketch=s2 diameter=<len> (depth=<len> \| extent=all) [type=simple\|counterbore\|countersink]` | A hole at every free point of the sketch, drilled into the face the sketch sits on. Counterbore: `cbore_diameter= cbore_depth=`; countersink: `csink_diameter= csink_angle=` |
| `pattern id=p1 feature=<id> kind=circular axis=X\|Y\|Z count=<n> [angle=<angle>]` | Repeat an Extrude, Revolve or Hole around an origin axis. A full turn spaces the copies evenly |
| `pattern id=p1 feature=<id> kind=rectangular dir1=X count1=<n> spacing1=<len> [dir2=Y count2=<n> spacing2=<len>]` | Repeat it in rows and columns |
| `mirror id=m1 [body=<id>] plane=YZ\|XZ\|XY [op=join\|new]` | Mirror a body across an origin plane, joined to the original or as a new body |

Profile keys are the sorted ids of the sketch curves around a closed area, e.g. `3,4,5,6`.

## Editing and history

| Command | Meaning |
|---|---|
| `set <feature id> key=value …` | Change a feature's values, e.g. `set f2 radius=1mm` |
| `suppress <id>` / `unsuppress <id>` | Skip or restore a step |
| `rollback before=<id>` · `rollback <n>` · `rollback end` | Move the rollback marker; new steps are inserted at the marker |
| `undo [n]` / `redo [n]` | Undo or redo whole commands |

## Output

| Command | Meaning |
|---|---|
| `faces [body=<id>]` | Print every face name of the bodies (to write `face:<name>` selectors) |
| `report` | Print JSON: per-body volume, area, bounding box, face/edge counts, validity, and every step's status and messages |
| `export stl <path>` · `export step <path>` | Export all bodies (millimetres) |
| `save <path.jig>` | Save the design |

## Face names

Faces are named after the step and role that made them, for example `e1/end` (far cap
of extrude e1), `e1/start` (the cap on the sketch plane), `e1/side:7` (the side wall
made from sketch curve 7), `f1/fillet:…`, `r1/side:19` (the face revolve r1 swept from
curve 19), `h1/wall:p5` (the wall of hole h1 at sketch point 5), `p1/3:e2/side:2` (that
face on the third copy of pattern p1). The app shows a face's name when you click it.
