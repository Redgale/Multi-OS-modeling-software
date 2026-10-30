# Contributing to Jigsmith

Thank you! Jigsmith is built for hobbyists, so contributions that make it easier,
calmer or more reliable are as welcome as new tools.

## Before you start

- Read [ARCHITECTURE.md](ARCHITECTURE.md) and the decision log
  [docs/DECISIONS.md](docs/DECISIONS.md).
- Look for issues labelled **good first issue**. Also see [docs/BACKLOG.md](docs/BACKLOG.md).
- Build following the [README](README.md). Run `ctest --test-dir build` before every push.

## The rules a pull request is checked against

1. **Headless first.** New behaviour lives in `core/` with a test that runs without a
   window. The UI only calls into the core.
2. **Every change is a transaction** (`Document::transact`), so undo works.
3. **Name your faces.** Any new geometry operation must give every face it creates a
   persistent name (see ARCHITECTURE.md), and add a case to `tests/test_naming.cpp`.
4. **Explain failures.** User-facing errors are one plain sentence plus suggestions.
   No kernel or exception text in `message`.
5. **No fake UI.** A tool appears enabled only when it works and has a test, a tooltip
   and a docs entry. Otherwise leave it disabled in the registry with `comingIn`.
6. **Never colour alone.** Every state shown with colour also has a symbol or word.
7. **Licences.** New dependencies must be added to `tools/dependencies.json` and
   `THIRD_PARTY_LICENSES.md`; CI fails otherwise. Never copy code, icons or text from
   proprietary software. Code from other open-source projects only with a compatible
   licence and attribution.
8. **Decisions.** Significant choices get a record in `docs/DECISIONS.md` (context,
   at least two options, decision, evidence, consequences, how to reverse).

## Style

C++20, 4-space indent, braces on their own line for functions and types, `camelCase`
functions, `PascalCase` types, trailing underscore for members. Keep comments for the
*why*. Match the surrounding file.

## Reporting bugs

Use the bug template. Attach the `.jig` file if you can; it contains only your design
(parameters and steps), no personal data.
