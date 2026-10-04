#!/usr/bin/env bash
# libmodbus baseline (B-14): the compilation database (B-05), then
# `lcart analyze` and `lcart map` with scripts/libmodbus/lcart.toml.
# Writes the map to docs/targets/libmodbus-map.md (committed; CI fails if
# a run changes it), prints the analysis time (NFR-005 baseline) and checks
# the facts the libmodbus plan expects (M1 step 3).
#
# Usage: scripts/libmodbus-baseline.sh [LCART]
#   LCART defaults to build/debug-system/src/cli/lcart. Needs no network
#   once the tools are installed; CI runs it under `unshare -n`.
#
# Verifies: b14-libmodbus-baseline-v1 (criteria 1-4),
# nfr001-works-without-llm-v2 (criterion 2).
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
lcart=${1:-$repo_root/build/debug-system/src/cli/lcart}
config="$repo_root/scripts/libmodbus/lcart.toml"
database="$repo_root/build/targets/libmodbus/facts.lcart.db"
map="$repo_root/docs/targets/libmodbus-map.md"

die() {
    echo "libmodbus-baseline: $*" >&2
    exit 1
}

[[ -x $lcart ]] || die "no lcart at $lcart (build it first, or pass its path)"
# The compiler CI uses, so a local run regenerates the same map.
export CC=${CC:-clang-20}

"$repo_root/scripts/libmodbus-compdb.sh"

start=$(date +%s.%N)
"$lcart" analyze --config "$config" --db "$database" --force >/dev/null
end=$(date +%s.%N)
seconds=$(awk -v s="$start" -v e="$end" 'BEGIN { printf "%.2f", e - s }')
echo "libmodbus-baseline: lcart analyze took $seconds s"

"$lcart" map --db "$database" --output "$map" >/dev/null
echo "libmodbus-baseline: module map in ${map#"$repo_root"/}"

# M1 step 3 of docs/targets/libmodbus.md: each function is defined in the
# expected file.
export_file="$repo_root/build/targets/libmodbus/facts.jsonl"
"$lcart" query export --db "$database" --output "$export_file"
while read -r name file; do
    grep -F "\"name\":\"$name\",\"qualified_name\":\"$name\"" "$export_file" |
        grep -F "\"file\":\"$file\"" >/dev/null ||
        die "$name is not defined in $file"
done <<'FUNCTIONS'
_modbus_receive_msg src/modbus.c
modbus_receive src/modbus.c
compute_meta_length_after_function src/modbus.c
compute_data_length_after_meta src/modbus.c
check_confirmation src/modbus.c
modbus_reply src/modbus.c
crc16 src/modbus-rtu.c
_modbus_rtu_check_integrity src/modbus-rtu.c
_modbus_tcp_check_integrity src/modbus-tcp.c
FUNCTIONS

# Core reaches the backends through the function-pointer table: those
# calls are indirect (via a `backend` member), and no direct call from core
# into rtu or tcp exists (only include edges).
callees=$("$lcart" query callees _modbus_receive_msg --db "$database")
grep -qP '\tindirect\tbackend\.recv$' <<<"$callees" ||
    die "_modbus_receive_msg has no indirect call through backend.recv"
grep -qP '\tindirect\tbackend\.check_integrity$' <<<"$callees" ||
    die "_modbus_receive_msg has no indirect call through backend.check_integrity"
if grep -E '"record":"module_dependency","from":"core","to":"(rtu|tcp)","via":"call"' \
    "$export_file"; then
    die "core calls a backend directly"
fi
echo "libmodbus-baseline: expected functions and indirect backend calls present"
