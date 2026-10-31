# Instructions for Claude sessions working on Jigsmith

## Standing instruction from the owner (Red, 2026-10-31)

> Keep going through the list of things that need to be added to the program. Don't stop
> until everything is added. If you run out of credits or hit the usage limit, wait until
> the limit resets and continue where you left off until the CAD program is done.

So: work autonomously through the remaining v0.1 feature list without waiting for
confirmation between items. After each finished batch: build, run the full test suite
(`xvfb-run -a ctest --test-dir build -j8`), update the docs, commit, and push to the working
branch. Ask the owner only for decisions that are genuinely theirs (legal, money,
accounts).

Owner priorities stated so far: dark mode is the default theme (light and high contrast in
Settings); US units (inches, feet) are fully supported alongside metric.

## Where things are

- `CAD_Build_Prompt.md`: the master brief (requirements, constraints, release plan).
- `docs/STATUS.md`: read first. What works, how it was verified, CI results, next tasks.
- `docs/BACKLOG.md`, `docs/FEATURE_MATRIX.md`: what is left, by milestone.
- `docs/DECISIONS.md`: decision records (every significant choice, with alternatives).
- `docs/SCRIPTING.md`: the `.jigs` script commands that drive headless tests.
- Working branch: `claude/pensive-mayer-l4r19c` (do not open pull requests unless asked).

## Rules from the brief that always apply

- Never copy Autodesk text, icons or branding. No telemetry.
- Never claim untested things work; STATUS.md says how each item was verified.
- The core (`core/`) stays headless: no Qt, no window-system libraries.
- Unfinished tools stay disabled in the UI with a "coming in" note.
- Never convey state by colour alone. Keep the license audit passing.
- Commit messages end with the attribution lines the session provides; never put model
  identifiers in the repository.

## How to build and test

```bash
scripts/install-deps.sh          # once, per distribution
cmake -S . -B build -G Ninja && cmake --build build
xvfb-run -a ctest --test-dir build -j8
scripts/refresh-strings.sh       # after changing commands or core messages (a ctest checks it)
```

A push runs the Linux CI jobs; put `[full ci]` in the commit message (or dispatch the
workflow) to run macOS, Windows, the other distributions and the publish job.
