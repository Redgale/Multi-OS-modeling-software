#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Merges an Apple-silicon and an Intel build of jigsmith.app (each already made
# self-contained by macdeployqt) into one universal app (B-005). Every Mach-O file is
# joined with lipo; everything else is taken from the Apple-silicon copy. The two copies
# must hold the same files (same Homebrew versions on both machines); any file found in
# only one of them, or a Mach-O file without its other half, stops the merge with its name.
# lipo breaks code signatures, so the result is signed again ad hoc (not notarised).
#
# Usage: packaging/macos/make-universal.sh arm64/jigsmith.app x86_64/jigsmith.app out/jigsmith.app
set -eu

arm="$1"
intel="$2"
out="$3"

rm -rf "$out"
mkdir -p "$(dirname "$out")"
cp -R "$arm" "$out"

status=0
# files in one copy only
(cd "$arm" && find . -type f | sort) > /tmp/jig-arm-files.txt
(cd "$intel" && find . -type f | sort) > /tmp/jig-intel-files.txt
if ! diff -u /tmp/jig-arm-files.txt /tmp/jig-intel-files.txt; then
    echo "The two apps do not hold the same files (lines above: - only in Apple silicon, + only in Intel)."
    status=1
fi

merged=0
while IFS= read -r f; do
    case "$(file -b "$arm/$f")" in
        *Mach-O*)
            if [ ! -f "$intel/$f" ]; then
                continue  # reported above
            fi
            if lipo -info "$arm/$f" | grep -q x86_64; then
                continue  # already universal (some system-style dylibs are)
            fi
            lipo -create "$arm/$f" "$intel/$f" -output "$out/$f"
            merged=$((merged + 1))
            ;;
    esac
done < /tmp/jig-arm-files.txt
echo "Joined $merged Mach-O files."

codesign --force --deep --sign - "$out"
lipo -archs "$out/Contents/MacOS/jigsmith"
exit $status
