#!/usr/bin/env bash
# Local Definition of Done (CONTRIBUTING.md): every check that runs without
# network, with one summary line per check. Logs: build/logs/dod-*.log.
# CI additionally builds the Conan presets (debug, clang-debug, release,
# asan-ubsan); a pull request is done only when CI is green as well.
#
# Usage: [PRESETS="debug-system asan-ubsan-system"] [BASE=origin/main] scripts/dod.sh
set -uo pipefail
cd "$(dirname "$0")/.."

presets="${PRESETS:-debug-system asan-ubsan-system}"
base="${BASE:-origin/main}"
log_dir="build/logs"
mkdir -p "$log_dir"

# The format and tidy scripts list files with `git ls-files`: an untracked
# new file would be skipped silently.
untracked="$(git ls-files --others --exclude-standard -- src tests fuzz scripts cmake)"
if [[ -n "$untracked" ]]; then
    # shellcheck disable=SC2086 # paths from git, no spaces in this repository
    git add -N $untracked
    echo "dod: marked new files for the checks (git add -N): $(echo $untracked | tr '\n' ' ')"
fi

failed=0
summary=()
run() { # name, command...
    local name="$1"; shift
    local log="$log_dir/dod-${name//[^a-zA-Z0-9_-]/_}.log"
    if "$@" >"$log" 2>&1; then
        summary+=("PASS  $name")
    else
        summary+=("FAIL  $name  (log: $log)")
        failed=1
    fi
}

for preset in $presets; do
    run "build+test $preset" cmake --workflow --preset "$preset"
done
run "check-format" scripts/check-format.sh
run "clang-tidy" bash -c 'cmake --preset debug-system >/dev/null && scripts/run-clang-tidy.sh debug-system'
run "docs-check" scripts/docs-check.sh
changed="$(git diff --name-only "$base"... 2>/dev/null; git diff --name-only HEAD)"
if grep -q '^scripts/check-doc-anchors.py\|^tests/docs/' <<<"$changed"; then
    run "docs checker tests" python3 -m unittest discover -s tests/docs
fi
if [[ -d targets/libmodbus/src ]]; then
    run "targets pinned" scripts/check-targets.sh
fi

# Added C++/CMake lines with a TODO lacking a reference (TODO(B-07)) or a
# NOLINT lacking a reason after it.
bad="$(git diff "$base" -- src tests fuzz cmake CMakeLists.txt | grep -E '^\+' |
    grep -E '(//|#) *TODO([^(]|$)|NOLINT[A-Z]*(\([^)]*\))? *$' || true)"
if [[ -n "$bad" ]]; then
    summary+=("FAIL  TODO/NOLINT references: $bad")
    failed=1
else
    summary+=("PASS  TODO/NOLINT references")
fi

printf '%s\n' "${summary[@]}"
for line in "${summary[@]}"; do
    if [[ "$line" == FAIL*log:* ]]; then
        log="${line##*log: }"; log="${log%)}"
        echo "--- first errors in $log"
        grep -E 'error:|FAILED|Failed|\*\*\*|ERROR|WARNING|runtime error|violation' "$log" | head -15 ||
            tail -15 "$log"
    fi
done
exit $failed
