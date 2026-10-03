#!/usr/bin/env bash
# Documentation gate: versioned-anchor check, then a strict MkDocs build.
# First time: python3 -m venv .venv-docs && .venv-docs/bin/pip install -r requirements-docs.txt
set -euo pipefail
cd "$(dirname "$0")/.."

python3 scripts/check-doc-anchors.py

mkdocs_bin="mkdocs"
[[ -x .venv-docs/bin/mkdocs ]] && mkdocs_bin=".venv-docs/bin/mkdocs"
"$mkdocs_bin" build --strict --site-dir build/site
echo "docs-check: OK"
