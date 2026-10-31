# Translating Jigsmith

Jigsmith's interface uses Qt Linguist. Everything a person reads in the app goes through
`tr()`, including the command labels and tooltips that live in the core's command registry
(the app translates them; `app/registry_strings.h` lists them for the tools).

## What exists today

- `translations/jigsmith.ts`: the template with every interface string (about 980).
- `translations/jigsmith_qps.ts`: a **pseudo-translation** for testing, made by
  `tools/pseudo_translate.py`. Every string is accented, about a third longer and wrapped in
  `[ ]`, so plain text in the app is text that cannot be translated yet, and cramped layouts
  show up. Start the app with `jigsmith --lang qps` to see it, or pick it in Settings.
- No real languages yet. Machine translations are not shipped: a translation goes in when a
  person who speaks the language has reviewed it.

Not translated yet: messages that come from the core (error explanations, step status),
which are plain C++ strings; the standard CAD names shown beside the plain labels stay
English on purpose (they are what tutorials and searches use).

## Adding a language

```bash
cmake --build build --target jigsmith_ui_lupdate     # refresh translations/jigsmith.ts
cp translations/jigsmith.ts translations/jigsmith_de.ts
linguist translations/jigsmith_de.ts                 # set the language, translate, save
cmake -S . -B build && cmake --build build           # every jigsmith_*.ts is compiled in
```

Translations are built when Qt's LinguistTools are installed (Ubuntu: `qt6-tools-dev
qt6-l10n-tools`; Homebrew and the official Qt installers include them). Without them the app
builds in English only.

When commands change in `core/src/commands.cpp`, refresh their strings (a test checks this):

```bash
python3 tools/extract_registry_strings.py core/src/commands.cpp app/registry_strings.h
python3 tools/pseudo_translate.py translations/jigsmith.ts translations/jigsmith_qps.ts
```
