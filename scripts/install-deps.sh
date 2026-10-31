#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Installs everything needed to build Jigsmith from source, using your system's
# package manager. Supported (and tested in CI, see .github/workflows/ci.yml):
#
#   Debian / Ubuntu (apt)     Void Linux, glibc and musl (xbps)
#   Fedora (dnf)              Arch Linux (pacman)
#   openSUSE Tumbleweed (zypper)
#   macOS (Homebrew)
#
# Usage:  scripts/install-deps.sh          show the command, ask, then run it
#         scripts/install-deps.sh --yes    run without asking (CI, containers)
#         scripts/install-deps.sh --print  only print the command
#
# Another distribution? Install the equivalents of: a C++20 compiler, CMake >= 3.24,
# Ninja, Open CASCADE (OCCT) >= 7.6 development files, Qt 6 base development files
# (Widgets + OpenGL), Eigen 3, Boost headers, nlohmann/json, GoogleTest and OpenGL
# headers. If your distribution has no OCCT package (or one older than 7.6), build it
# with scripts/build-occt.sh. Then follow "Build" in README.md.
set -eu

mode=ask
case "${1:-}" in
    --yes|-y) mode=yes ;;
    --print) mode=print ;;
    "") ;;
    *) echo "usage: $0 [--yes|--print]" >&2; exit 2 ;;
esac

if [ "$(id -u)" -eq 0 ]; then SUDO=""; else SUDO="sudo"; fi

if [ "$(uname -s)" = Darwin ]; then
    cmd="brew install cmake ninja opencascade qt eigen boost nlohmann-json googletest"
    SUDO=""
elif command -v xbps-install >/dev/null 2>&1; then
    # Void Linux (glibc or musl)
    # xbps must be current before it installs anything else (fresh containers often are not)
    [ "$mode" = print ] || $SUDO xbps-install -Syu xbps
    cmd="xbps-install -Sy base-devel cmake ninja pkg-config occt-devel qt6-base-devel eigen boost-headers json-c++ gtest-devel tbb-devel freetype-devel MesaLib-devel"
elif command -v apt-get >/dev/null 2>&1; then
    cmd="apt-get install -y build-essential cmake ninja-build libocct-modeling-algorithms-dev libocct-modeling-data-dev libocct-data-exchange-dev libocct-foundation-dev libtbb-dev libeigen3-dev libboost-dev nlohmann-json3-dev libgtest-dev qt6-base-dev qt6-tools-dev qt6-l10n-tools libgl-dev libfreetype-dev"
    [ "$mode" = print ] || $SUDO apt-get update
elif command -v dnf >/dev/null 2>&1; then
    cmd="dnf install -y gcc-c++ cmake ninja-build opencascade-devel qt6-qtbase-devel eigen3-devel boost-devel json-devel gtest-devel tbb-devel freetype-devel mesa-libGL-devel"
elif command -v pacman >/dev/null 2>&1; then
    cmd="pacman -S --needed --noconfirm base-devel cmake ninja opencascade qt6-base eigen boost nlohmann-json gtest onetbb freetype2"
elif command -v zypper >/dev/null 2>&1; then
    cmd="zypper --non-interactive install gcc-c++ cmake ninja occt-devel qt6-base-devel qt6-opengl-devel qt6-openglwidgets-devel qt6-test-devel eigen3-devel boost-devel nlohmann_json-devel googletest-devel tbb-devel freetype2-devel Mesa-libGL-devel"
else
    echo "Sorry, this script does not know your package manager." >&2
    echo "See the comment at the top of $0 for what to install by hand." >&2
    exit 1
fi

full="${SUDO:+$SUDO }$cmd"
echo "$full"
case "$mode" in
    print) exit 0 ;;
    ask)
        printf "Run this now? [y/N] "
        read -r answer
        case "$answer" in y|Y|yes) ;; *) echo "Nothing installed."; exit 0 ;; esac
        ;;
esac
# shellcheck disable=SC2086
$full
echo
echo "Done. Next:  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build"
