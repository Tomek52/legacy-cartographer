#!/usr/bin/env bash
# Checks that every analysed target under targets/ is a submodule at the
# commit recorded in docs/targets/<name>.md, with no local changes
# (NFR-008, ADR-0003). Run after `git submodule update --init`.
set -euo pipefail
cd "$(dirname "$0")/.."

status=0
while IFS= read -r line; do
    prefix="${line:0:1}"
    read -r sha path _ <<<"${line:1}"
    name="$(basename "$path")"
    doc="docs/targets/$name.md"

    if [[ "$prefix" == "-" ]]; then
        echo "check-targets: $path is not initialized (git submodule update --init $path)" >&2
        status=1; continue
    fi
    if [[ ! -f "$doc" ]]; then
        echo "check-targets: $path has no $doc recording its pinned commit" >&2
        status=1; continue
    fi
    pinned="$(sed -n 's/^| Commit | `\([0-9a-f]\{40\}\)` |$/\1/p' "$doc")"
    if [[ -z "$pinned" ]]; then
        echo "check-targets: $doc has no '| Commit | \`<sha>\` |' row" >&2
        status=1; continue
    fi
    if [[ "$sha" != "$pinned" ]]; then
        echo "check-targets: $path is at $sha, $doc pins $pinned" >&2
        status=1
    fi
    if [[ "$prefix" == "+" ]]; then
        echo "check-targets: $path checkout differs from the commit recorded in the superproject" >&2
        status=1
    fi
    if [[ -n "$(git -C "$path" status --porcelain --untracked-files=all)" ]]; then
        echo "check-targets: $path has local changes; targets must stay unmodified" >&2
        status=1
    fi
done < <(git submodule status -- targets)

[[ $status -eq 0 ]] && echo "check-targets: OK"
exit $status
