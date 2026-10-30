#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Fails (exit 1) when a dependency's license is unknown or not allowed.

Checks:
  * every dependency in tools/dependencies.json has a license on the allow-list;
  * every dependency named in vcpkg.json and every FetchContent/find_package in
    cmake/Dependencies.cmake is listed in tools/dependencies.json;
  * every dependency has a section in THIRD_PARTY_LICENSES.md.
Usage: python3 tools/license_audit.py [repo_root]
"""
import json
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[1])
data = json.loads((root / "tools/dependencies.json").read_text())
allowed = set(data["allowed_licenses"])
deps = {d["id"]: d for d in data["dependencies"]}
errors = []

for d in data["dependencies"]:
    if d["license"] not in allowed:
        errors.append(f"{d['name']}: license '{d['license']}' is not on the allow-list")

vcpkg = json.loads((root / "vcpkg.json").read_text())
vcpkg_map = {"opencascade": "opencascade", "eigen3": "eigen3", "boost-graph": "boost",
             "nlohmann-json": "nlohmann-json", "gtest": "gtest"}
for dep in vcpkg.get("dependencies", []):
    name = dep if isinstance(dep, str) else dep["name"]
    if vcpkg_map.get(name) not in deps:
        errors.append(f"vcpkg.json dependency '{name}' is not in tools/dependencies.json")

cmake = (root / "cmake/Dependencies.cmake").read_text()
cmake_map = {"OpenCASCADE": "opencascade", "Eigen3": "eigen3", "Boost": "boost",
             "nlohmann_json": "nlohmann-json", "miniz": "miniz", "googletest": "gtest",
             "GTest": "gtest", "Qt6": "qt6"}
for m in re.finditer(r"(?:find_package|FetchContent_Declare)\(\s*(\w+)", cmake):
    name = m.group(1)
    if cmake_map.get(name) not in deps:
        errors.append(f"cmake/Dependencies.cmake uses '{name}', which is not in tools/dependencies.json")

notices = (root / "THIRD_PARTY_LICENSES.md").read_text()
for d in data["dependencies"]:
    if f"## {d['name']}" not in notices:
        errors.append(f"THIRD_PARTY_LICENSES.md has no section '## {d['name']}'")

if errors:
    print("License audit FAILED:")
    for e in errors:
        print("  -", e)
    sys.exit(1)
print(f"License audit passed: {len(deps)} dependencies, all licenses allowed and documented.")
