#!/usr/bin/env python3
"""Checks versioned documentation anchors (docs/adr/0005-versioned-requirement-anchors.md).

MkDocs (strict) already fails on links to missing anchors. This script checks
the anchors themselves:

1. every `<a id="...">` in docs/ (outside code blocks and inline code) matches
   `<doc>-<slug>-v<N>`;
2. every ID is unique across docs/;
3. a visible marker `**[<ID> v<N>]**` on the same line as an anchor names the
   same item and version (fr004 -> FR-004, nfr001 -> NFR-001, b07 -> B-07,
   m1 -> M1);
4. every FR/NFR heading in specs/requirements.md and every backlog item
   heading carries an anchor and a marker.

Exit status 1 with one `file:line: message` per problem.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

DOCS = Path(__file__).resolve().parent.parent / "docs"
# Relative to the docs root: the ADR template shows the anchor syntax with placeholders.
SKIP = {Path("adr") / "template.md"}

ID_FORMAT = re.compile(
    r"^(fr\d{3}|nfr\d{3}|b\d{2}|m\d|adr\d{4}|arch|claim|testing|security)"
    r"-[a-z0-9]+(?:-[a-z0-9]+){0,5}-v[1-9]\d*$"
)
ANCHOR = re.compile(r'<a id="([^"]+)"></a>')
MARKER = re.compile(r"\*\*\[([A-Z]+-?\d+) v(\d+)\]\*\*")
NEEDS_MARKER = re.compile(r"^#+ .*\b(FR|NFR)-\d{3}\b|^#+ .*\*\*\[B-\d{2}")
FENCE = re.compile(r"^\s*(```|~~~)")
INLINE_CODE = re.compile(r"`[^`]*`")


def expected_marker(anchor_id: str) -> tuple[str, str] | None:
    """Maps an anchor ID to the marker (label, version) it must carry, if any."""
    prefix, version = anchor_id.split("-", 1)[0], anchor_id.rsplit("-v", 1)[1]
    for doc, label in (("nfr", "NFR-"), ("fr", "FR-"), ("b", "B-"), ("m", "M")):
        if prefix.startswith(doc) and prefix[len(doc):].isdigit():
            return f"{label}{prefix[len(doc):]}", version
    return None


def markdown_files(docs: Path) -> list[Path]:
    return sorted(p for p in docs.rglob("*.md") if p.relative_to(docs) not in SKIP)


def check(docs: Path) -> list[str]:
    """Returns one `file:line: message` per problem in the Markdown under `docs`."""
    problems: list[str] = []
    seen: dict[str, str] = {}
    for path in markdown_files(docs):
        in_code = False
        rel = path.relative_to(docs.parent)
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            if FENCE.match(line):
                in_code = not in_code
                continue
            if in_code:
                continue
            where = f"{rel}:{number}"
            line = INLINE_CODE.sub("", line)
            anchors = ANCHOR.findall(line)
            for anchor_id in anchors:
                if not ID_FORMAT.match(anchor_id):
                    problems.append(f"{where}: anchor '{anchor_id}' does not match <doc>-<slug>-v<N>")
                    continue
                if anchor_id in seen:
                    problems.append(f"{where}: duplicate anchor '{anchor_id}' (first at {seen[anchor_id]})")
                seen.setdefault(anchor_id, where)
                expected = expected_marker(anchor_id)
                marker = MARKER.search(line)
                if expected and marker and (marker.group(1), marker.group(2)) != expected:
                    problems.append(
                        f"{where}: marker [{marker.group(1)} v{marker.group(2)}] does not match "
                        f"anchor '{anchor_id}' (expected [{expected[0]} v{expected[1]}])"
                    )
                if expected and line.lstrip().startswith("#") and not marker:
                    problems.append(f"{where}: heading anchor '{anchor_id}' needs a visible marker")
            if NEEDS_MARKER.search(line) and not anchors:
                problems.append(f"{where}: requirement or backlog heading without an anchor")
    return problems


def main() -> int:
    problems = check(DOCS)
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print(f"check-doc-anchors: OK ({len(markdown_files(DOCS))} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
