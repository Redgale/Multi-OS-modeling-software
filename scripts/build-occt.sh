#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds Open CASCADE Technology from source into ./deps/occt, for distributions
# that do not package OCCT (or package one older than 7.6). Only the modules
# Jigsmith uses are built (no Draw test harness, no OpenGL viewer, no VTK), release only,
# so it needs a C++ compiler, CMake, Ninja, and the X11 and OpenGL development
# headers (OCCT's data-exchange toolkits pull in its basic visualization layer):
#   Debian/Ubuntu: libx11-dev libgl-dev   Void: libX11-devel MesaLib-devel
#   Fedora: libX11-devel mesa-libGL-devel  Arch: libx11 mesa
#
# Usage:  scripts/build-occt.sh [version]      default version: 7.9.3
# Then:   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$PWD/deps/occt"
#
# Expect roughly 15-40 minutes depending on the machine (it uses all cores).
set -eu

version="${1:-7.9.3}"
tag="V$(echo "$version" | tr . _)"
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$root/deps/occt-src"
prefix="$root/deps/occt"
jobs="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

mkdir -p "$work"
if [ ! -f "$work/$tag/CMakeLists.txt" ]; then
    echo "Downloading OCCT $version ..."
    curl -fL "https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/$tag.tar.gz" -o "$work/$tag.tar.gz"
    mkdir -p "$work/$tag"
    tar -xzf "$work/$tag.tar.gz" -C "$work/$tag" --strip-components=1
fi

cmake -S "$work/$tag" -B "$work/build-$version" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DBUILD_LIBRARY_TYPE=Shared \
    -DBUILD_MODULE_ApplicationFramework=OFF \
    -DBUILD_MODULE_Draw=OFF \
    -DBUILD_MODULE_Visualization=OFF \
    -DBUILD_MODULE_DETools=OFF \
    -DBUILD_MODULE_FoundationClasses=ON \
    -DBUILD_MODULE_ModelingData=ON \
    -DBUILD_MODULE_ModelingAlgorithms=ON \
    -DBUILD_MODULE_DataExchange=ON \
    -DUSE_FREETYPE=OFF -DUSE_FREEIMAGE=OFF -DUSE_TBB=OFF -DUSE_VTK=OFF -DUSE_RAPIDJSON=OFF \
    -DUSE_TK=OFF -DUSE_OPENGL=OFF -DUSE_GLES2=OFF -DUSE_D3D=OFF \
    -DBUILD_DOC_Overview=OFF -DINSTALL_SAMPLES=OFF -DINSTALL_TEST_CASES=OFF
cmake --build "$work/build-$version" -j "$jobs"
cmake --install "$work/build-$version"

echo
echo "OCCT $version installed in $prefix"
echo "Configure Jigsmith with:"
echo "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=\"$prefix\""
echo "and run the app with LD_LIBRARY_PATH=\"$prefix/lib\" if it is not found automatically."
