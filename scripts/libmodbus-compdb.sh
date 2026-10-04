#!/usr/bin/env bash
# Builds libmodbus under bear and writes its compilation database (B-05):
#   build/targets/libmodbus/compile_commands.json
#
# Requires: git, autoconf, automake, libtool (libtoolize), make, bear and a
# C compiler ($CC, default clang). On Ubuntu 24.04:
#   sudo apt-get install autoconf automake libtool make bear clang
#
# targets/libmodbus is never touched (CLAUDE.md): autogen.sh writes its
# generated files into the source tree, so the pinned commit is exported
# with `git archive` to build/targets/libmodbus/source and configured and
# built there. The compilation database therefore names files under that
# copy, which is byte-identical to the pinned commit plus what configure
# generates (config.h, src/modbus-version.h, tests/unit-test.h); it is the
# analysis root for libmodbus (docs/targets/libmodbus.md).
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
submodule="$repo_root/targets/libmodbus"
out_dir="$repo_root/build/targets/libmodbus"
source_dir="$out_dir/source"
database="$out_dir/compile_commands.json"
cc=${CC:-clang}

die() {
    echo "libmodbus-compdb: $*" >&2
    exit 1
}

missing=()
for tool in git autoreconf automake libtoolize make bear "$cc"; do
    command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
if ((${#missing[@]} > 0)); then
    die "missing tools: ${missing[*]} (Ubuntu: sudo apt-get install autoconf automake libtool make bear clang)"
fi
if ! git -C "$submodule" rev-parse --verify -q HEAD >/dev/null; then
    die "targets/libmodbus is not checked out (git submodule update --init targets/libmodbus)"
fi

# A fresh copy every run, so no stale generated file survives.
rm -rf "$source_dir"
mkdir -p "$source_dir"
git -C "$submodule" archive --format=tar HEAD | tar -x -C "$source_dir"

log="$out_dir/build.log"
{
    cd "$source_dir"
    ./autogen.sh
    CC="$cc" ./configure --enable-tests
    bear --output "$database" -- make -j"$(nproc)"
} >"$log" 2>&1 || die "build failed; see $log"

for file in modbus.c modbus-data.c modbus-rtu.c modbus-tcp.c; do
    grep -q "\"file\": \"$source_dir/src/$file\"" "$database" ||
        die "no entry for src/$file in $database"
done
if [[ -n $(git -C "$submodule" status --porcelain) ]]; then
    die "targets/libmodbus was modified; it must stay clean"
fi
entries=$(grep -c '"file":' "$database")
echo "libmodbus-compdb: $entries entries in ${database#"$repo_root"/}"
