# Asset register

Every non-code asset shipped with Jigsmith, with its source and licence (brief 5.6).

| Asset | Path | Source | Licence |
|---|---|---|---|
| Application icon (placeholder) | `packaging/linux/jigsmith.svg` | Original, drawn for Jigsmith in session 1 | CC0-1.0 |
| Tool icon set (about 80 icons) | `app/Icons.cpp` | Original vector drawings made for Jigsmith (session 2), painted in code in the theme colours; not derived from any icon set | GPL-3.0-or-later |
| Phone stand sample | `samples/phone_stand.jigs` | Original script following the generic "phone stand" exercise in the reference guide's ch. 43 (dimensions are ordinary hobby values) | GPL-3.0-or-later |
| Inter font, Regular, v4.0 | `assets/fonts/Inter-Regular.otf` (embedded in `jig_core`), licence in `assets/fonts/OFL.txt` | https://github.com/rsms/inter (copied unmodified from the Debian/Ubuntu `fonts-inter` package, SHA-256 be6d709d…3521b) | SIL OFL 1.1 |
| Knob and pen cup samples | `samples/knob.jigs`, `samples/pen_cup.jigs` | Original scripts following the generic exercises in the reference guide's ch. 43 | GPL-3.0-or-later |
| Screenshots | `docs/images/*.png` | Generated from Jigsmith itself with `--screenshot` | GPL-3.0-or-later |

The UI itself uses the system font; Inter is used for sketch text. Nothing from Autodesk or
other proprietary software is used.
