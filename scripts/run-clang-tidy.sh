#!/usr/bin/env bash
# Runs clang-tidy over all owned C++ translation units, using the compilation
# database of a configured preset (default: debug).
# Usage: scripts/run-clang-tidy.sh [preset]
set -euo pipefail
cd "$(dirname "$0")/.."

preset="${1:-debug}"
build_dir="build/$preset"
clang_tidy="${CLANG_TIDY:-clang-tidy-18}"

if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    echo "run-clang-tidy: $build_dir/compile_commands.json missing; run cmake --preset $preset first" >&2
    exit 2
fi

git ls-files -- 'src/*.cpp' 'tests/*.cpp' 'fuzz/*.cpp' \
    | xargs --no-run-if-empty -n 1 -P "$(nproc)" "$clang_tidy" -p "$build_dir" --quiet
echo "run-clang-tidy: OK"
