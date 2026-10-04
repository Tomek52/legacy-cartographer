#!/usr/bin/env bash
# Inner loop: configure, build and test one preset; print one line when
# green and only the errors when red. The full log stays in build/logs/.
# Usage: [PRESET=debug-system] scripts/quick-check.sh [ctest args, e.g. -R Sqlite]
set -uo pipefail
cd "$(dirname "$0")/.."

preset="${PRESET:-debug-system}"
log_dir="build/logs"
log="$log_dir/quick-check-$preset.log"
mkdir -p "$log_dir"

{
    cmake --preset "$preset" &&
        cmake --build --preset "$preset" &&
        ctest --preset "$preset" "$@"
} >"$log" 2>&1
status=$?

if [[ $status -eq 0 ]]; then
    echo "quick-check $preset: OK, $(grep -Eo 'out of [0-9]+' "$log" | tail -1 | cut -d' ' -f3) tests passed; log: $log"
else
    echo "quick-check $preset: FAILED (exit $status); log: $log"
    grep -E 'error:|FAILED|Failed|\*\*\*|ERROR:|Assertion|Expected|Which is|Value of|runtime error' "$log" | head -40
fi
exit $status
