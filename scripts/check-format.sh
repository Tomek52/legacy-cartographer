#!/usr/bin/env bash
# Checks (or with --fix, applies) clang-format on all owned C++ sources.
# targets/ (analysed upstream code) is never formatted.
set -euo pipefail
cd "$(dirname "$0")/.."

clang_format="${CLANG_FORMAT:-clang-format-18}"
mode=(--dry-run --Werror)
[[ "${1:-}" == "--fix" ]] && mode=(-i)

git ls-files -- 'src/*.cpp' 'src/*.hpp' 'tests/*.cpp' 'tests/*.hpp' 'fuzz/*.cpp' \
    | xargs --no-run-if-empty "$clang_format" "${mode[@]}"
echo "check-format: OK"
