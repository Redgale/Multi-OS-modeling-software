# Jigsmith from Python

The `jigsmith` Python module builds, measures and exports designs from your own Python programs,
without the app. It uses the same commands as `.jigs` scripts (see `SCRIPTING.md`), so anything a
script can do, Python can do, and the app opens the `.jig` files it saves.

## Getting it

The module is built with Jigsmith when Python's development files and pybind11 are installed
(`sudo apt install python3-dev pybind11-dev` on Debian and Ubuntu). After `cmake --build build`
it is in `build/python/`:

```bash
PYTHONPATH=build/python python3 my_part.py
```

## A first part

```python
import jigsmith

d = jigsmith.Design()
d.set_param("wall", "2.4 mm")
d.run("""
box id=base length=60mm width=40mm height=wall
cylinder diameter=5mm height=wall op=cut
""")
print(d.properties()["volume"])    # mm³
d.set_param("wall", "4 mm")        # the part follows its parameters
d.export("base.stl")               # or .step, .3mf
d.save("base.jig")                 # opens in the app
```

## What a Design offers

| | |
|---|---|
| `Design()`, `Design.open(path)` | a new design, or a saved `.jig` |
| `run(commands)` | one or more script commands (several lines are fine) |
| `set_param(name, expression)`, `param(name)`, `params` | named parameters (values in mm, radians or plain numbers) |
| `steps` | the timeline: id, name, kind, status (`ok`, `warning`, `error`, `skipped`) and message |
| `bodies`, `faces(body)` | the parts, and their faces' names (for fillets, holes, sketches on faces) |
| `properties(body="")` | volume, area, centre, bounding box, face and solid counts, validity |
| `export(path)`, `save(path)` | STL, STEP or 3MF by extension; `.jig` |
| `undo()`, `redo()` | step back and forward |

A command that fails raises `jigsmith.JigsmithError` with Jigsmith's explanation and suggestions;
the lines before it stay done.
