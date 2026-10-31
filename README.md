# Jigsmith

**Free, open-source parametric 3D CAD for hobbyists and makers**, on Linux, Windows and macOS.
Sketch, extrude, refine, with a timeline of editable steps. No account, no cloud, no
telemetry, no paywall. Your designs are files on your computer.

![The phone stand sample in Jigsmith](docs/images/phone_stand_light.png)

> **Status: early development (pre-v0.1, "walking skeleton").** The modelling engine,
> file format and a viewer-style app work; interactive sketching and feature dialogs
> are being built milestone by milestone. See [docs/STATUS.md](docs/STATUS.md) for
> exactly what works today and how it was verified. Tools that are not ready are shown
> disabled with the milestone they arrive in.

## What works today

- Open the built-in **phone stand** sample (File ▸ Open a sample): parameters, a
  10-step timeline, rollback marker, undo/redo, face picking with persistent face names.
- Save and open `.jig` designs; export **STL** (binary) and **STEP**.
- Find any tool by intent: **Ctrl+K** (⌘K on macOS) or **S**, then type “round the edges”.
- Headless modelling with scripts: `jigsmith-cli --run samples/phone_stand.jigs --report -`.
- Change a parameter early in the timeline and later fillets and sketches stay attached
  (tested in CI).

## Build from source

You need a C++20 compiler, CMake ≥ 3.24, Ninja, and the dependencies below. Every platform
uses prebuilt dependencies, so a typical machine builds Jigsmith in 5 to 15 minutes.

### Linux (Ubuntu 24.04 / Debian 13)

```bash
sudo apt install build-essential cmake ninja-build libocct-modeling-algorithms-dev libocct-modeling-data-dev libocct-data-exchange-dev libocct-foundation-dev libtbb-dev libeigen3-dev libboost-dev nlohmann-json3-dev libgtest-dev qt6-base-dev libgl-dev
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build && ctest --test-dir build
```

```bash
./build/app/jigsmith
```

### macOS (Apple silicon or Intel, Homebrew)

```bash
brew install cmake ninja opencascade qt eigen boost nlohmann-json googletest
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix)"
```

```bash
cmake --build build && ctest --test-dir build
```

```bash
open build/app/jigsmith.app
```

### Windows 10/11 (Visual Studio 2022, conda-forge, Qt)

1. Install Visual Studio 2022 with “Desktop development with C++”,
   [Miniforge](https://github.com/conda-forge/miniforge) (for prebuilt Open CASCADE), and
   Qt 6.8 (open-source edition, “MSVC 2022 64-bit”) from the Qt online installer.
2. Open the “Miniforge Prompt”, create the dependency environment once (about two minutes):

```bash
conda env create -f packaging/windows/environment.yml
```

3. In the same prompt, activate it and load the compiler (adjust the VS path if needed):

```bash
conda activate jigsmith && "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat"
```

4. Configure, build and test (adjust the Qt path to your install):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%CONDA_PREFIX%/Library;C:/Qt/6.8.3/msvc2022_64"
```

```bash
cmake --build build && ctest --test-dir build
```

### Build options

| Option | Default | Meaning |
|---|---|---|
| `JIG_BUILD_GUI` | ON | Build the Qt app (turn off for a headless core + CLI) |
| `JIG_BUILD_TESTS` | ON | Unit, geometry, naming and file-format tests |
| `JIG_BUILD_BENCH` | ON | `jigsmith-bench` performance budgets |
| `JIG_SANITIZE` | empty | e.g. `address,undefined` for the core and tests |

## Project layout

| Path | What |
|---|---|
| `core/` | `jig_core`: the headless library (document, sketches, features, kernel, files) |
| `cli/` | `jigsmith-cli`: run scripts, open designs, print reports |
| `app/` | The Qt desktop application (a thin layer over `jig_core`) |
| `tests/`, `bench/` | GoogleTest suites and performance budgets |
| `samples/` | Sample designs as scripts (`.jigs`) |
| `third_party/planegcs/` | FreeCAD's sketch solver, vendored |
| `docs/` | Status, decisions, architecture, plans |

Read [ARCHITECTURE.md](ARCHITECTURE.md) before changing the core, and
[CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request.

## License

Jigsmith is free software under the **GNU GPL v3 or later** ([LICENSE](LICENSE)).

**Jigsmith uses Open CASCADE Technology** (https://dev.opencascade.org), licensed
under the LGPL 2.1 with an additional exception. It also uses planegcs from FreeCAD, Qt,
Eigen, Boost, nlohmann/json and miniz; see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

Jigsmith is an independent project. It is not affiliated with or endorsed by Autodesk;
tool names such as Extrude and Fillet are generic CAD terms.
