#!/usr/bin/env bash
# Installs Conan dependencies for one CMake preset into build/<preset>/,
# where the preset's toolchainFile expects conan_toolchain.cmake.
# Usage: scripts/conan-install.sh <debug|clang-debug|release|asan-ubsan>
set -euo pipefail

preset="${1:?usage: $0 <preset>}"
repo="$(cd "$(dirname "$0")/.." && pwd)"

case "$preset" in
    debug)       profile=gcc;   build_type=Debug ;;
    clang-debug) profile=clang; build_type=Debug ;;
    release)     profile=gcc;   build_type=Release ;;
    asan-ubsan)  profile=clang; build_type=Debug ;;
    debug-system)
        echo "debug-system uses distribution packages; nothing to install with Conan." >&2
        exit 0 ;;
    *) echo "unknown preset: $preset" >&2; exit 2 ;;
esac

conan profile detect --exist-ok >/dev/null
conan install "$repo" \
    --output-folder="$repo/build/$preset" \
    --profile:all="$repo/conan/profiles/$profile" \
    --settings:all=build_type="$build_type" \
    --build=missing
