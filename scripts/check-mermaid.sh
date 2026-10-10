#!/usr/bin/env bash
# Mermaid gate (backlog B-22): parses every ```mermaid block of the committed
# module maps with the pinned Mermaid in tools/mermaid-check, without a
# browser and without network access. Prints file:line and Mermaid's error
# for each block that does not parse, and exits 1.
#
# Verifies: fr007-module-map-without-llm-v1 (criterion 1, run in CI)
#
# First time (the only step that uses the network):
#   npm ci --ignore-scripts --prefix tools/mermaid-check
# Usage: scripts/check-mermaid.sh [file.md...]
#   Without arguments: the libmodbus map and the module-map golden files.
set -euo pipefail
cd "$(dirname "$0")/.."

tool_dir="tools/mermaid-check"
if ! command -v node >/dev/null; then
    echo "check-mermaid: node not found (Node.js 22.13 or later is needed, as jsdom 29.1.1 requires)" >&2
    exit 2
fi
if [[ ! -d "$tool_dir/node_modules/mermaid" ]]; then
    echo "check-mermaid: pinned packages missing; run: npm ci --ignore-scripts --prefix $tool_dir" >&2
    exit 2
fi

if (($# == 0)); then
    shopt -s nullglob
    set -- docs/targets/libmodbus-map.md tests/golden/*.map.md
fi
exec node "$tool_dir/check-mermaid.mjs" "$@"
