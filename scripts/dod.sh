#!/usr/bin/env bash
# Local Definition of Done (CONTRIBUTING.md): every check that runs without
# network, with one summary line per check. Logs: build/logs/dod-*.log.
# Not covered here, only in CI: the Conan presets (debug, clang-debug,
# release, asan-ubsan) and CodeQL. A pull request is done only when CI is
# green as well.
#
# Usage: [PRESETS="debug-system asan-ubsan-system tsan-system"]
#        [BASE=origin/main] [DOD_NO_ADD=1] scripts/dod.sh
# DOD_NO_ADD=1 leaves the git index alone (for read-only reviewers); new
# untracked files are then not format- or tidy-checked.
set -uo pipefail
cd "$(dirname "$0")/.."

presets="${PRESETS:-debug-system asan-ubsan-system tsan-system}"
base="${BASE:-origin/main}"
log_dir="build/logs"
mkdir -p "$log_dir"

if ! git rev-parse --verify -q "$base^{commit}" >/dev/null; then
    echo "dod: base '$base' not found; fetch it or set BASE=<ref>" >&2
    exit 2
fi
# Diff against the merge base, so commits that landed on the base after this
# branch started do not show up as changes of this branch.
merge_base="$(git merge-base "$base" HEAD)"

# The format and tidy scripts list files with `git ls-files`: an untracked
# new file would be skipped silently.
if [[ "${DOD_NO_ADD:-0}" != 1 ]]; then
    mapfile -d '' added < <(git ls-files -z --others --exclude-standard -- src tests fuzz scripts cmake)
    if ((${#added[@]})); then
        git add -N -- "${added[@]}"
        echo "dod: marked new files for the checks (git add -N): ${added[*]}"
    fi
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
if [[ " $presets " == *" asan-ubsan-system "* ]]; then
    run "fuzz (60 s per target)" scripts/fuzz-smoke.sh build/asan-ubsan-system
fi
run "check-format" scripts/check-format.sh
run "clang-tidy" bash -c 'cmake --preset debug-system >/dev/null && scripts/run-clang-tidy.sh debug-system'
run "docs-check" scripts/docs-check.sh
run "docs checker tests" python3 -m unittest discover -s tests/docs
if [[ " $presets " == *" debug-system "* && " ${summary[*]} " == *"PASS  build+test debug-system"* ]]; then
    # Rewrites docs/plan/traceability.md from this run's results; a change
    # is reported so it is committed with the rest (CI fails on a stale page).
    run "traceability" bash -c 'python3 scripts/check-doc-anchors.py \
        --trace build/debug-system/junit.xml --write &&
        { git diff --quiet -- docs/plan/traceability.md ||
          echo "dod: docs/plan/traceability.md changed; commit it"; }'
fi
if [[ -d targets/libmodbus/src ]]; then
    run "targets pinned" scripts/check-targets.sh
fi

# Added lines (committed or not) with a TODO lacking a B-NN or MN reference,
# or a NOLINT/NOLINTNEXTLINE/NOLINTBEGIN lacking a reason after it.
bad="$(git diff "$merge_base" -- src tests fuzz cmake CMakeLists.txt | grep -E '^\+' |
    grep -E 'TODO(\((B-[0-9]+|M[0-9]+)\))?' | grep -vE 'TODO\((B-[0-9]+|M[0-9]+)\)' || true)"
bad+="$(git diff "$merge_base" -- src tests fuzz cmake CMakeLists.txt | grep -E '^\+' |
    grep -E 'NOLINT(NEXTLINE|BEGIN)?(\([^)]*\))?[[:space:]]*(\*/)?[[:space:]]*$' || true)"
if [[ -n "$bad" ]]; then
    summary+=("FAIL  TODO/NOLINT references:"$'\n'"$bad")
    failed=1
else
    summary+=("PASS  TODO/NOLINT references")
fi

printf '%s\n' "${summary[@]}"
for line in "${summary[@]}"; do
    if [[ "$line" == FAIL*log:* ]]; then
        log="${line##*log: }"; log="${log%)}"
        echo "--- first errors in $log"
        errors="$(grep -E 'error:|FAILED|Failed|\*\*\*|ERROR|WARNING|runtime error|violation' "$log" | head -15)"
        if [[ -n "$errors" ]]; then echo "$errors"; else tail -15 "$log"; fi
    fi
done
exit $failed
