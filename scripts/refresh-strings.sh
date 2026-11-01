#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Refreshes every generated string list after changing commands or core messages:
# the command strings, the core's messages, the translation template and the
# pseudo-translation. Run from the source folder with a configured build/ folder.
set -e
python3 tools/extract_registry_strings.py core/src/commands.cpp app/registry_strings.h
python3 tools/extract_core_messages.py core/src app/core_strings.h
cmake --build "${1:-build}" --target jigsmith_ui_lupdate
python3 tools/pseudo_translate.py translations/jigsmith.ts translations/jigsmith_qps.ts
