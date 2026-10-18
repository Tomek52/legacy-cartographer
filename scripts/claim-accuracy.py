#!/usr/bin/env python3
"""Measures the verifier's accuracy on the labelled claim set (B-29, NFR-009).

Reads the labelled sets in tests/claims/labelled/*.jsonl (format: ADR-0031),
builds a fact database for each fixture set with `lcart analyze`, verifies
every set's claims with `lcart verify`, and compares each verdict with its
label. Prints the confusion matrix (expected status by actual status) per
claim kind and overall, the precision of `confirmed` and `contradicted`, the
recall of `contradicted` and the unverified rate, and every claim whose
verdict differs from its label.

The libmodbus set needs the fact database of scripts/libmodbus-baseline.sh
(`--libmodbus-db`); without it the set is skipped and only the fixture sets
count. The gate fails below the NFR-009 targets; with the libmodbus set it
also checks the size of the set (B-29 criterion 1). `--page FILE` writes the
result page (docs/testing/verifier-accuracy.md).

Exit status: 0 gate passed, 1 gate failed, 2 invalid labels or a failed run.
Needs no network; the output depends only on the labels and the facts.

Verifies: b29-labelled-claim-set-v1 (criteria 1 to 3),
nfr009-verifier-accuracy-measured-v1 (criteria 1 and 2).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FORMAT = "lcart-labelled-claims"
FORMAT_VERSION = 1
KINDS = ("calls", "includes", "depends_on", "defines", "belongs_to")
STATUSES = ("confirmed", "unverified", "contradicted")
ORIGINS = ("true", "mutated", "hard")
LABEL_KEYS = ("id", "kind", "polarity", "subject", "object", "expected", "origin", "at", "why")
AT_PATTERN = re.compile(r"^(?P<path>[^:]+):(?P<first>[1-9][0-9]*)(?:-(?P<last>[1-9][0-9]*))?$")

# NFR-009 criterion 2.
PRECISION_CONFIRMED_TARGET = 0.99
PRECISION_CONTRADICTED_TARGET = 0.95
# B-29 criterion 1.
MIN_CLAIMS = 200
MIN_PER_KIND = 30
MIN_FALSE = 60

# `lcart verify` exit statuses (ADR-0030): 0, or 6 when a claim is
# contradicted; both mean the report was written.
VERIFY_WROTE_REPORT = (0, 6)


class LabelError(Exception):
    """A labelled set does not follow the format."""


@dataclass
class Result:
    set_name: str
    claim_id: str
    kind: str
    polarity: str
    expected: str
    actual: str
    reason: str
    at: str
    statement: str


def entity_text(entity: dict) -> str:
    name = entity["name"]
    return f"{name} ({entity['file']})" if "file" in entity else name


def statement(label: dict) -> str:
    negation = "not " if label["polarity"] == "negated" else ""
    return f"{entity_text(label['subject'])} {negation}{label['kind']} {entity_text(label['object'])}"


def check_at(at: str, where: str) -> None:
    match = AT_PATTERN.match(at)
    if not match:
        raise LabelError(f"{where}: 'at' must be <path>:<line> or <path>:<first>-<last>, got {at!r}")
    path = REPO / match["path"]
    last = int(match["last"] or match["first"])
    if int(match["first"]) > last:
        raise LabelError(f"{where}: line range {at!r} is reversed")
    if not path.is_file():
        # The submodule may be missing outside CI; the libmodbus set is
        # then skipped anyway.
        if match["path"].startswith("targets/"):
            return
        raise LabelError(f"{where}: {match['path']} does not exist")
    with path.open(encoding="utf-8", errors="replace") as source:
        lines = sum(1 for _ in source)
    if last > lines:
        raise LabelError(f"{where}: {match['path']} has {lines} lines, label cites {at}")


def load_set(path: Path) -> tuple[dict, list[dict]]:
    lines = path.read_text(encoding="utf-8").splitlines()
    if not lines:
        raise LabelError(f"{path}: empty")
    header = json.loads(lines[0])
    if header.get("format") != FORMAT or header.get("format_version") != FORMAT_VERSION:
        raise LabelError(f"{path}:1: header must name format {FORMAT!r} version {FORMAT_VERSION}")
    if header.get("target") == "fixture":
        if not (REPO / "tests" / "fixtures" / str(header.get("fixture"))).is_dir():
            raise LabelError(f"{path}:1: no fixture {header.get('fixture')!r}")
        if not header.get("units"):
            raise LabelError(f"{path}:1: a fixture set needs its units")
    elif header.get("target") != "libmodbus":
        raise LabelError(f"{path}:1: target must be 'fixture' or 'libmodbus'")
    labels = []
    seen = set()
    for number, line in enumerate(lines[1:], start=2):
        where = f"{path.name}:{number}"
        label = json.loads(line)
        if tuple(label) != LABEL_KEYS:
            raise LabelError(f"{where}: keys must be {', '.join(LABEL_KEYS)} in this order")
        if label["id"] in seen:
            raise LabelError(f"{where}: claim ID {label['id']} used twice")
        seen.add(label["id"])
        if label["expected"] not in STATUSES:
            raise LabelError(f"{where}: expected must be one of {', '.join(STATUSES)}")
        if label["origin"] not in ORIGINS:
            raise LabelError(f"{where}: origin must be one of {', '.join(ORIGINS)}")
        if not label["why"]:
            raise LabelError(f"{where}: every label needs a one-sentence justification")
        check_at(label["at"], where)
        labels.append(label)
    return header, labels


def run(command: list[str], accepted: tuple[int, ...] = (0,)) -> None:
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    if completed.returncode not in accepted:
        sys.exit(
            f"claim-accuracy: {' '.join(command)} exited {completed.returncode}\n{completed.stderr}"
        )


def analyse_fixture(lcart: Path, header: dict, work: Path) -> Path:
    root = REPO / "tests" / "fixtures" / header["fixture"]
    entries = [
        {"directory": str(root), "arguments": ["clang", "-c", *header["flags"], unit], "file": unit}
        for unit in header["units"]
    ]
    database = work / "compile_commands.json"
    database.write_text(json.dumps(entries, indent=1) + "\n", encoding="utf-8")
    facts = work / "facts.lcart.db"
    # A unit that fails to parse (broken_tu) makes analyze exit 1 with its
    # facts kept: that is the case the set measures.
    run(
        [str(lcart), "analyze", "--root", str(root), "--compilation-database", str(database),
         "--db", str(facts), "--force", "--jobs", "1"],
        accepted=(0, 1),
    )
    return facts


def verify_set(lcart: Path, name: str, labels: list[dict], facts: Path, work: Path) -> tuple[list[Result], dict]:
    claims = [
        {
            "id": label["id"],
            "kind": label["kind"],
            "subject": label["subject"],
            "object": label["object"],
            "polarity": label["polarity"],
            "text": label["why"],
            "source": {"document": name, "section": label["at"]},
        }
        for label in labels
    ]
    claims_file = work / "claims.json"
    claims_file.write_text(
        json.dumps({"schema": "lcart:claim:v1", "claims": claims}, indent=1) + "\n", encoding="utf-8"
    )
    out = work / "report"
    run([str(lcart), "verify", str(claims_file), "--db", str(facts), "--out", str(out)], accepted=VERIFY_WROTE_REPORT)
    report = json.loads((out / "verification-report.json").read_text(encoding="utf-8"))
    verdicts = {claim["id"]: claim for claim in report["claims"]}
    results = [
        Result(
            set_name=name,
            claim_id=label["id"],
            kind=label["kind"],
            polarity=label["polarity"],
            expected=label["expected"],
            actual=verdicts[label["id"]]["status"],
            reason=verdicts[label["id"]]["reason"],
            at=label["at"],
            statement=statement(label),
        )
        for label in labels
    ]
    return results, report["coverage"]


Matrix = dict[str, dict[str, int]]


def matrix(results: list[Result]) -> Matrix:
    counts = {expected: dict.fromkeys(STATUSES, 0) for expected in STATUSES}
    for result in results:
        counts[result.expected][result.actual] += 1
    return counts


def ratio(numerator: int, denominator: int) -> float | None:
    return numerator / denominator if denominator else None


def metrics(counts: Matrix) -> dict[str, float | None]:
    total = sum(sum(row.values()) for row in counts.values())
    column = {status: sum(counts[expected][status] for expected in STATUSES) for status in STATUSES}
    return {
        "precision_confirmed": ratio(counts["confirmed"]["confirmed"], column["confirmed"]),
        "precision_contradicted": ratio(counts["contradicted"]["contradicted"], column["contradicted"]),
        "recall_contradicted": ratio(counts["contradicted"]["contradicted"], sum(counts["contradicted"].values())),
        "unverified_rate": ratio(column["unverified"], total),
    }


def fmt(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2f}"


def gate(results: list[Result], with_libmodbus: bool) -> list[str]:
    failures = []
    overall = metrics(matrix(results))
    for key, target, label in (
        ("precision_confirmed", PRECISION_CONFIRMED_TARGET, "precision of confirmed"),
        ("precision_contradicted", PRECISION_CONTRADICTED_TARGET, "precision of contradicted"),
    ):
        value = overall[key]
        # No verdict of that status: nothing was wrongly given it.
        if value is not None and value < target:
            failures.append(f"{label} {fmt(value)} < {target:.2f}")
    if with_libmodbus:
        if len(results) < MIN_CLAIMS:
            failures.append(f"{len(results)} claims < {MIN_CLAIMS}")
        for kind in KINDS:
            count = sum(1 for result in results if result.kind == kind)
            if count < MIN_PER_KIND:
                failures.append(f"{count} {kind} claims < {MIN_PER_KIND}")
        false_claims = sum(1 for result in results if result.expected == "contradicted")
        if false_claims < MIN_FALSE:
            failures.append(f"{false_claims} false claims < {MIN_FALSE}")
    return failures


def matrix_rows(counts: Matrix) -> list[str]:
    rows = ["| expected \\ actual | " + " | ".join(STATUSES) + " |", "|---|" + "---:|" * len(STATUSES)]
    rows += [f"| {expected} | " + " | ".join(str(counts[expected][s]) for s in STATUSES) + " |" for expected in STATUSES]
    return rows


def metric_rows(results: list[Result]) -> list[str]:
    rows = [
        "| Kind | Claims | Precision of confirmed | Precision of contradicted "
        "| Recall of contradicted | Unverified rate |",
        "|---|---:|---:|---:|---:|---:|",
    ]
    for kind in (*KINDS, "all"):
        subset = results if kind == "all" else [r for r in results if r.kind == kind]
        values = metrics(matrix(subset))
        name = "**all**" if kind == "all" else f"`{kind}`"
        rows.append(
            f"| {name} | {len(subset)} | {fmt(values['precision_confirmed'])} "
            f"| {fmt(values['precision_contradicted'])} | {fmt(values['recall_contradicted'])} "
            f"| {fmt(values['unverified_rate'])} |"
        )
    return rows


def disagreements(results: list[Result]) -> list[Result]:
    return [result for result in results if result.expected != result.actual]


def page(results: list[Result], sets: list[tuple[str, dict, int]], failures: list[str]) -> str:
    lines = [
        "<!-- Generated by scripts/claim-accuracy.py --page; do not edit. -->",
        "",
        "# Verifier accuracy",
        "",
        '<a id="testing-verifier-accuracy-results-v1"></a>The verifier measured on the labelled claim set',
        "([testing strategy](strategy.md#testing-verifier-accuracy-v2),",
        "[NFR-009](../specs/requirements.md#nfr009-verifier-accuracy-measured-v1)).",
        "Regenerated by `scripts/claim-accuracy.py` in CI's `libmodbus baseline` job,",
        "which fails if this page changes.",
        "",
        f"Gate: {'passed' if not failures else 'FAILED: ' + '; '.join(failures)}.",
        "",
        "## Sets",
        "",
        "| Set | Claims | Translation units parsed | Error diagnostics |",
        "|---|---:|---:|---:|",
    ]
    for name, coverage, count in sets:
        lines.append(
            f"| `{name}` | {count} | {coverage['translation_units_parsed']} of "
            f"{coverage['translation_units_total']} | {coverage['error_diagnostics']} |"
        )
    lines += ["", "## Metrics", "", *metric_rows(results), "", "## Confusion matrices", ""]
    for kind in (*KINDS, "all"):
        subset = results if kind == "all" else [r for r in results if r.kind == kind]
        lines += [f"### {'All kinds' if kind == 'all' else '`' + kind + '`'}", "", *matrix_rows(matrix(subset)), ""]
    lines += [
        "## Claims whose verdict differs from the label",
        "",
        "Each is resolved by reading the code again (B-29 criterion 4); those",
        "left here are what the verifier cannot decide or decides wrongly by design.",
        "",
    ]
    differing = disagreements(results)
    if differing:
        lines += ["| Set | Claim | Statement | Expected | Actual | Reason | Read at |", "|---|---|---|---|---|---|---|"]
        for result in differing:
            lines.append(
                f"| `{result.set_name}` | {result.claim_id} | `{result.statement}` | {result.expected} "
                f"| {result.actual} | `{result.reason}` | `{result.at}` |"
            )
    else:
        lines.append("None.")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--lcart", type=Path, required=True, help="the lcart executable")
    parser.add_argument("--work", type=Path, required=True, help="directory for databases and reports")
    parser.add_argument("--labelled", type=Path, default=REPO / "tests" / "claims" / "labelled")
    parser.add_argument("--libmodbus-db", type=Path, help="fact database of scripts/libmodbus-baseline.sh")
    parser.add_argument("--page", type=Path, help="write the result page here")
    args = parser.parse_args()

    results: list[Result] = []
    sets: list[tuple[str, dict, int]] = []
    with_libmodbus = False
    try:
        loaded = [(path, *load_set(path)) for path in sorted(args.labelled.glob("*.jsonl"))]
    except (LabelError, json.JSONDecodeError, KeyError) as error:
        print(f"claim-accuracy: invalid labels: {error}", file=sys.stderr)
        return 2
    if not loaded:
        print(f"claim-accuracy: no labelled sets in {args.labelled}", file=sys.stderr)
        return 2
    for path, header, labels in loaded:
        name = path.stem
        work = args.work.resolve() / name
        work.mkdir(parents=True, exist_ok=True)
        if header["target"] == "libmodbus":
            if args.libmodbus_db is None:
                print(f"{name}: skipped (no --libmodbus-db)")
                continue
            facts = args.libmodbus_db.resolve()
            with_libmodbus = True
        else:
            facts = analyse_fixture(args.lcart, header, work)
        set_results, coverage = verify_set(args.lcart, name, labels, facts, work)
        results += set_results
        sets.append((name, coverage, len(labels)))
        print(f"{name}: {len(labels)} claims, {len(disagreements(set_results))} differ from their label")

    print()
    for kind in (*KINDS, "all"):
        subset = results if kind == "all" else [r for r in results if r.kind == kind]
        print(f"{kind}: expected (rows) by actual (columns)")
        print("\n".join(matrix_rows(matrix(subset))))
        print()
    print("\n".join(metric_rows(results)))
    print()
    for result in disagreements(results):
        print(
            f"differs: {result.set_name} {result.claim_id} {result.statement}: expected {result.expected}, "
            f"got {result.actual} ({result.reason}); read at {result.at}"
        )
    failures = gate(results, with_libmodbus)
    if args.page:
        args.page.write_text(page(results, sets, failures), encoding="utf-8")
    if failures:
        print(f"accuracy gate: FAIL: {'; '.join(failures)}")
        return 1
    print(f"accuracy gate: passed ({len(results)} claims{'' if with_libmodbus else ', fixture sets only'})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
