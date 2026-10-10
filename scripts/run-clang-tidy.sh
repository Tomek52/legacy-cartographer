#!/usr/bin/env bash
# Runs clang-tidy over all owned C++ translation units, using the compilation
# database of a configured preset (default: debug).
# Usage: scripts/run-clang-tidy.sh [preset]
set -euo pipefail
cd "$(dirname "$0")/.."

preset="${1:-debug}"
build_dir="build/$preset"
clang_tidy="${CLANG_TIDY:-clang-tidy-20}"

if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    echo "run-clang-tidy: $build_dir/compile_commands.json missing; run cmake --preset $preset first" >&2
    exit 2
fi

# The header filter is passed explicitly: clang-tidy 20 did not apply
# HeaderFilterRegex from .clang-tidy in every checkout (it did in a git
# worktree, not in the main clone), so findings in the project's own headers
# were dropped as "non-user code" (B-24). Keep it equal to .clang-tidy's.
header_filter='.*/(src|tests|fuzz)/.*'
grep -qxF "HeaderFilterRegex: '$header_filter'" .clang-tidy ||
    { echo "run-clang-tidy: header filter differs from .clang-tidy" >&2; exit 2; }

git ls-files -- 'src/*.cpp' 'tests/*.cpp' 'fuzz/*.cpp' \
    | xargs --no-run-if-empty -n 1 -P "$(nproc)" "$clang_tidy" -p "$build_dir" --quiet \
        --header-filter="$header_filter"
echo "run-clang-tidy: OK"
