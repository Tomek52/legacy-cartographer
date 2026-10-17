#!/usr/bin/env bash
# Runs every libFuzzer target of a build for 60 s each, the minimum of
# NFR-010 v1 criterion 1, with its dictionary and seed corpus when it has
# them. Used by CI and scripts/dod.sh.
# Usage: scripts/fuzz-smoke.sh <build dir> [seconds per target, default 60]
set -euo pipefail

build="${1:?usage: $0 <build dir> [seconds]}"
seconds="${2:-60}"
repo="$(cd "$(dirname "$0")/.." && pwd)"

# Seed corpora, read only: libFuzzer writes new inputs into the first corpus
# directory it is given, which is a temporary one.
declare -A seeds=(
    [claim_json_fuzzer]="$repo/tests/claims/data/valid $repo/tests/claims/data/invalid"
)
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

for target in claim_json_fuzzer project_config_fuzzer; do
    fuzzer="$build/fuzz/$target"
    if [[ ! -x "$fuzzer" ]]; then
        echo "fuzz-smoke: $fuzzer not built (LCART_BUILD_FUZZERS=ON with Clang)" >&2
        exit 1
    fi
    dict="$repo/fuzz/${target%_fuzzer}.dict"
    # Inputs up to 64 KiB: libFuzzer's default of 4 KiB is too small for
    # bugs that need depth, such as deeply dotted TOML keys (ADR-0015).
    args=(-max_total_time="$seconds" -max_len=65536)
    [[ -f "$dict" ]] && args+=(-dict="$dict")
    mkdir "$work/$target"
    read -r -a corpus <<<"${seeds[$target]:-}"
    args+=("$work/$target" "${corpus[@]}")
    echo "fuzz-smoke: $target"
    "$fuzzer" "${args[@]}"
done
