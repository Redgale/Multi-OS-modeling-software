# SPDX-License-Identifier: GPL-3.0-or-later
"""The jigsmith Python module: build a part, drive it with a parameter, measure, export,
save and open again; errors come back as JigsmithError with the core's explanation."""
import math
import os
import sys
import tempfile

import jigsmith


def near(a, b, tol=1e-6):
    assert abs(a - b) <= tol, f"{a} != {b}"


d = jigsmith.Design()
assert jigsmith.__version__
d.set_param("wall", "3 mm")
d.run("box id=bx1 length=30mm width=20mm height=10mm\ncylinder id=cy1 diameter=6mm height=10mm op=cut\n")
assert d.bodies == ["bx1"], d.bodies
near(d.properties()["volume"], 6000 - math.pi * 9 * 10, 1e-6)
assert "bx1/top" in d.faces("bx1")
assert [s["status"] for s in d.steps] == ["ok", "ok"], d.steps
near(d.param("wall"), 3)
assert d.params["wall"] == "3 mm"
# a parameter drives the model
d.run("fillet id=f1 edges=face:bx1/top radius=wall/3")
before = d.properties()["volume"]
d.set_param("wall", "6 mm")
assert d.properties()["volume"] < before
# undo and redo
assert d.undo()
near(d.param("wall"), 3)
assert d.redo()
# errors explain themselves
try:
    d.run("box id=bx1 length=1mm width=1mm height=1mm")
    raise SystemExit("an id used twice should fail")
except jigsmith.JigsmithError as e:
    assert "already used" in str(e), str(e)
try:
    d.properties("nothing")
    raise SystemExit("a missing body should fail")
except jigsmith.JigsmithError as e:
    assert "no body" in str(e), str(e)
# export, save, open
with tempfile.TemporaryDirectory() as tmp:
    for ext in ("stl", "step", "3mf"):
        path = os.path.join(tmp, "part." + ext)
        d.export(path)
        assert os.path.getsize(path) > 100, ext
    path = os.path.join(tmp, "part.jig")
    d.save(path)
    e = jigsmith.Design.open(path)
    near(e.properties()["volume"], d.properties()["volume"], 1e-6)
    assert repr(e).startswith("<jigsmith.Design: 3 steps")
print("python api ok")
sys.exit(0)
