#!/usr/bin/env bash
# Builds the mod, loads it with the smoke host under Wine, and runs the checks: what CI runs on
# every push, and what a release runs before it packs anything. Works locally on Linux too
# (MinGW-w64, CMake, Ninja, Wine, Python 3).
#
#   tools/ci-build.sh [tag]     with a tag (v1.2.3), the tag must match echo-mod.json's version
set -euo pipefail
cd "$(dirname "$0")/.."

file=$(python3 -c 'import json; print(json.load(open("echo-mod.json"))["file"])')
cmake --preset mingw
cmake --build --preset mingw

# The DLL alone in a folder of its own, as in the game's plugins folder: anything it needs
# beside it would be missing here too.
rm -rf build/smoke && mkdir -p build/smoke
cp "build/mingw/$file" build/smoke/
cp build/mingw/smoke_host.exe build/smoke/
export WINEDEBUG=-all WINEPREFIX="${WINEPREFIX:-$PWD/build/wineprefix}"
wine=$(command -v wine || command -v wine64)
# A crash would start Wine's debugger and wait: the timeout ends that.
(cd build/smoke && timeout 300 "$wine" smoke_host.exe "$file" > smoke.json)
cat build/smoke/smoke.json

tag_args=()
[[ $# -gt 0 ]] && tag_args=(--tag "$1")
python3 tools/check_mod.py --dll "build/mingw/$file" --smoke build/smoke/smoke.json "${tag_args[@]}"
