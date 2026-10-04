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

With `--trace JUNIT [--write]` it also checks the traceability of
requirements to tests (ADR-0010, B-15) against a CTest JUnit file:

5. every anchor cited after `Verifies:` (in tests/, scripts/, cmake/, src/,
   fuzz/, .github/) exists, so a citation of an old version fails;
6. every test citing a requirement passed in the JUnit file;
7. every FR/NFR of a milestone from M1 on whose roadmap status is "done"
   has a citing test that passed (skipped ones do not count);
8. every `Verifies:` names at least one versioned anchor.

It prints the matrix; `--write` also writes docs/plan/traceability.md and
build/traceability.csv.

Exit status 1 with one `file:line: message` per problem.
"""

from __future__ import annotations

import argparse
import csv
import re
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DOCS = REPO / "docs"
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


# ---- traceability (ADR-0010, B-15) -----------------------------------------

ANCHOR_TOKEN = re.compile(
    r"\b(?:fr\d{3}|nfr\d{3}|b\d{2}|m\d|adr\d{4}|arch|claim|testing|security)"
    r"-[a-z0-9]+(?:-[a-z0-9]+){0,5}-v[1-9]\d*\b"
)
REQUIREMENT_HEADING = re.compile(r'^#+ <a id="((n?fr)(\d{3})-[a-z0-9-]+-v(\d+))"></a>\*\*\[[A-Z]+-\d+ v\d+\]\*\* (.*)$')
MILESTONE_HEADING = re.compile(r'^## <a id="(m(\d)-[a-z0-9-]+-v\d+)"></a>')
REQUIREMENT_LINK = re.compile(r"requirements\.md#((n?fr)(\d{3})-[a-z0-9-]+-v\d+)\)")
GTEST = re.compile(r"^\s*TEST(?:_F|_P)?\(\s*(\w+)\s*,\s*(\w+)\s*\)")
ADD_TEST = re.compile(r"add_test\(\s*NAME\s+([^\s)]+)")
CITATION_ROOTS = ("tests", "scripts", "cmake", "src", "fuzz", ".github")
CITATION_SUFFIXES = {".cpp", ".hpp", ".h", ".c", ".py", ".sh", ".cmake", ".txt", ".yml", ".toml"}
# Python unittest files (tests/docs) do not run under CTest.
NOT_IN_CTEST = "not in CTest"


@dataclass
class Requirement:
    anchor: str
    label: str  # FR-001
    version: int
    title: str
    milestones: list[str] = field(default_factory=list)
    tests: dict[str, str] = field(default_factory=dict)  # test -> result


def requirements(docs: Path) -> dict[str, Requirement]:
    """FR/NFR headings of specs/requirements.md, by anchor."""
    found: dict[str, Requirement] = {}
    for line in (docs / "specs" / "requirements.md").read_text(encoding="utf-8").splitlines():
        match = REQUIREMENT_HEADING.match(line)
        if match:
            anchor, kind, number, version, title = match.groups()
            found[anchor] = Requirement(anchor, f"{kind.upper()}-{number}", int(version), title.strip())
    return found


def milestones(docs: Path) -> dict[str, tuple[int, bool, list[str]]]:
    """Per milestone label (M1): number, status done, cited requirement labels.

    A `**Requirements:**` paragraph lists links; two links joined by an en
    dash are a range (FR-001–FR-007)."""
    result: dict[str, tuple[int, bool, list[str]]] = {}
    current: str | None = None
    in_requirements = False
    paragraph: dict[str, list[str]] = {}
    status: dict[str, bool] = {}
    numbers: dict[str, int] = {}
    for line in (docs / "plan" / "roadmap.md").read_text(encoding="utf-8").splitlines():
        heading = MILESTONE_HEADING.match(line)
        if heading or line.startswith("## "):
            current = f"M{heading.group(2)}" if heading else None
            if heading:
                numbers[current] = int(heading.group(2))
                paragraph[current] = []
                status[current] = False
            in_requirements = False
            continue
        if current is None:
            continue
        if line.startswith("**Status:**"):
            value = line.removeprefix("**Status:**").strip().lower()
            if not value.startswith(("done", "open")):
                raise ValueError(f"roadmap.md: {current} status must start with 'done' or 'open': {line}")
            status[current] = value.startswith("done")
        if line.startswith("**Requirements:**"):
            in_requirements = True
        elif not line.strip():
            in_requirements = False
        if in_requirements:
            paragraph[current].append(line)
    for label, lines in paragraph.items():
        text = " ".join(lines)
        links = list(REQUIREMENT_LINK.finditer(text))
        labels: list[str] = []
        for index, link in enumerate(links):
            labels.append(f"{link.group(2).upper()}-{link.group(3)}")
            if index + 1 < len(links):
                between = text[link.end():links[index + 1].start()]
                following = links[index + 1]
                if between.strip().startswith("–") and following.group(2) == link.group(2):
                    for number in range(int(link.group(3)) + 1, int(following.group(3))):
                        labels.append(f"{link.group(2).upper()}-{number:03d}")
        result[label] = (numbers[label], status[label], labels)
    return result


def comment_text(line: str) -> str | None:
    """The text of a `//` or `#` comment line, else None."""
    stripped = line.strip()
    for prefix in ("//", "#"):
        if stripped.startswith(prefix):
            return stripped[len(prefix):]
    return None


def cited_anchors(lines: list[str], start: int) -> list[str]:
    """Anchors on the `Verifies:` line at `start` and the lines after it up
    to a blank line or a line of code."""
    anchors = ANCHOR_TOKEN.findall(lines[start].split("Verifies:", 1)[1])
    is_comment = comment_text(lines[start]) is not None
    for line in lines[start + 1:]:
        if not line.strip() or (is_comment and comment_text(line) is None):
            break
        anchors += ANCHOR_TOKEN.findall(line)
    return anchors


@dataclass
class Citation:
    where: str  # file:line
    anchor: str
    tests: list[str]  # CTest names, or NOT_IN_CTEST markers


def citations(repo: Path) -> tuple[list[Citation], list[str]]:
    """Every `Verifies:` citation in the repository and the tests it covers.

    In a C++ test file, a citation in the comment block that opens the file
    covers every TEST in it; any other citation covers the TEST right after
    its comment block. In tests/CMakeLists.txt a citation covers each
    add_test() between it and the next blank line. A citation in a Python
    test covers that file, which runs outside CTest. Elsewhere (scripts,
    CI) a citation names no CTest test; its anchor is still checked."""
    found: list[Citation] = []
    problems: list[str] = []
    for root in CITATION_ROOTS:
        base = repo / root
        if not base.is_dir():
            continue
        for path in sorted(p for p in base.rglob("*") if p.is_file() and (p.suffix in CITATION_SUFFIXES or p.name == "CMakeLists.txt")):
            # Fixtures are analysed code; this script describes the syntax.
            if "fixtures" in path.relative_to(repo).parts or path == Path(__file__).resolve():
                continue
            try:
                lines = path.read_text(encoding="utf-8").splitlines()
            except UnicodeDecodeError:
                continue
            rel = path.relative_to(repo).as_posix()
            is_test_source = rel.startswith("tests/") and path.suffix == ".cpp"
            tests_in_file = [f"{m.group(1)}.{m.group(2)}" for line in lines if (m := GTEST.match(line))]
            header_end = 0
            while header_end < len(lines) and comment_text(lines[header_end]) is not None:
                header_end += 1
            for index, line in enumerate(lines):
                if "Verifies:" not in line:
                    continue
                anchors = cited_anchors(lines, index)
                where = f"{rel}:{index + 1}"
                if not anchors:
                    problems.append(f"{where}: Verifies: names no versioned anchor (<doc>-<slug>-v<N>)")
                    continue
                tests: list[str] = []
                if is_test_source:
                    if index < header_end:
                        tests = tests_in_file
                    else:
                        after = index
                        while after < len(lines) and comment_text(lines[after]) is not None:
                            after += 1
                        match = GTEST.match(lines[after]) if after < len(lines) else None
                        if match:
                            tests = [f"{match.group(1)}.{match.group(2)}"]
                        else:
                            problems.append(f"{where}: Verifies: is neither in the file header nor right above a TEST")
                elif rel.startswith("tests/") and path.name == "CMakeLists.txt":
                    after = index
                    while after < len(lines) and lines[after].strip():
                        tests += ADD_TEST.findall(lines[after])
                        after += 1
                elif rel.startswith("tests/") and path.suffix == ".py":
                    tests = [f"{rel} ({NOT_IN_CTEST})"]
                for anchor in anchors:
                    found.append(Citation(where, anchor, tests))
    return found, problems


def junit_results(junit: Path) -> dict[str, str]:
    """CTest JUnit file: test name -> passed, failed or skipped."""
    results: dict[str, str] = {}
    for case in ET.parse(junit).getroot().iter("testcase"):
        status = case.get("status", "")
        if case.find("failure") is not None or case.find("error") is not None or status == "fail":
            results[case.get("name", "")] = "failed"
        elif case.find("skipped") is not None or status in {"notrun", "disabled"}:
            results[case.get("name", "")] = "skipped"
        else:
            results[case.get("name", "")] = "passed"
    return results


def trace(docs: Path, repo: Path, junit: Path) -> tuple[list[Requirement], list[str]]:
    """The traceability matrix and one message per gate failure."""
    problems: list[str] = []
    anchors = all_anchor_ids(docs)
    reqs = requirements(docs)
    by_label = {r.label: r for r in reqs.values()}
    for label, (number, done, labels) in sorted(milestones(docs).items()):
        for req_label in labels:
            if req_label in by_label and label not in by_label[req_label].milestones:
                by_label[req_label].milestones.append(label)
    results = junit_results(junit)
    found, citation_problems = citations(repo)
    problems += citation_problems
    for citation in found:
        if citation.anchor not in anchors:
            problems.append(f"{citation.where}: cites '{citation.anchor}', which does not exist (an old version?)")
            continue
        requirement = reqs.get(citation.anchor)
        if requirement is None:
            continue
        for test in citation.tests:
            if NOT_IN_CTEST in test:
                requirement.tests[test] = NOT_IN_CTEST
                continue
            result = results.get(test, "not run")
            requirement.tests[test] = result
            # A skipped test (for example one that cannot run as root) is
            # shown, not a failure; a test missing from the results is.
            if result not in {"passed", "skipped"}:
                problems.append(f"{citation.where}: {test}, citing {citation.anchor}, {result}")
    # From M1 on (NFR-012 criterion 2): M0's requirements are checked by CI
    # jobs, not by tests.
    gated = {label for label, (number, done, _) in milestones(docs).items() if done and number >= 1}
    for requirement in reqs.values():
        done = gated.intersection(requirement.milestones)
        if done and requirement_result(requirement) not in {"passed", NOT_IN_CTEST}:
            problems.append(
                f"docs/specs/requirements.md: {requirement.label} v{requirement.version} belongs to done "
                f"milestone {', '.join(sorted(done))} and no test that ran cites it"
            )
    ordered = sorted(reqs.values(), key=lambda r: (r.label.startswith("NFR"), r.label))
    return ordered, sorted(set(problems))


def requirement_result(requirement: Requirement) -> str:
    """One result per requirement: failed if a citing test failed or did not
    run; passed if one passed; skipped if all were skipped (a skipped test
    proves nothing); "not in CTest" if only Python tests cite it (their
    results are in the docs job); "no test" without citations."""
    results = set(requirement.tests.values())
    if not results:
        return "no test"
    if results - {"passed", "skipped", NOT_IN_CTEST}:
        return "failed"
    if "passed" in results:
        return "passed"
    if "skipped" in results:
        return "skipped"
    return NOT_IN_CTEST


def all_anchor_ids(docs: Path) -> set[str]:
    ids: set[str] = set()
    for path in markdown_files(docs):
        ids.update(ANCHOR.findall(path.read_text(encoding="utf-8")))
    return ids


def matrix_markdown(matrix: list[Requirement]) -> str:
    lines = [
        "# Traceability matrix",
        "",
        "<!-- Generated by scripts/check-doc-anchors.py --trace --write; do not edit. -->",
        "",
        "Generated from the requirement anchors, the `Verifies:` citations in the",
        "tests and the CTest results of the `debug-system` preset",
        "([ADR-0010](../adr/0010-traceability-matrix.md)). Do not edit: CI",
        "regenerates it and fails if it differs.",
        "",
        "Result is per requirement: passed if a citing test passed and none",
        "failed; skipped if every citing test was skipped; per-test results are",
        "in `build/traceability.csv`, kept by the CI job.",
        "",
        "| Requirement | Version | Milestones | Tests | Result |",
        "|---|---|---|---|---|",
    ]
    for r in matrix:
        tests = sorted(r.tests)
        result = requirement_result(r)
        names = "<br>".join(f"`{t}`" for t in tests) if tests else "–"
        link = f"[{r.label}](../specs/requirements.md#{r.anchor})"
        lines.append(f"| {link} {r.title} | v{r.version} | {', '.join(r.milestones) or '–'} | {names} | {result} |")
    return "\n".join(lines) + "\n"


def write_csv(matrix: list[Requirement], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.writer(out)
        writer.writerow(["requirement", "version", "milestones", "test", "result"])
        for r in matrix:
            for test in sorted(r.tests) or [""]:
                writer.writerow([r.label, r.version, " ".join(r.milestones), test, r.tests.get(test, "no test")])


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--trace", metavar="JUNIT", type=Path, help="CTest JUnit file (ctest --output-junit)")
    parser.add_argument("--write", action="store_true", help="write docs/plan/traceability.md and build/traceability.csv")
    args = parser.parse_args(argv)

    problems = check(DOCS)
    if args.trace:
        try:
            matrix, trace_problems = trace(DOCS, REPO, args.trace)
        except ValueError as error:
            print(error)
            return 1
        problems += trace_problems
        text = matrix_markdown(matrix)
        if args.write:
            (DOCS / "plan" / "traceability.md").write_text(text, encoding="utf-8")
            write_csv(matrix, REPO / "build" / "traceability.csv")
        else:
            print(text, end="")
    for problem in problems:
        print(problem)
    if problems:
        return 1
    print(f"check-doc-anchors: OK ({len(markdown_files(DOCS))} files)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
