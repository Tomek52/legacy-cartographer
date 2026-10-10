#!/usr/bin/env python3
"""Measure `lcart analyze` on generated projects (B-16, B-19; NFR-005 criteria 2 and 5).

Generates projects with scripts/gen-many-tus.py in a temporary directory and
reports, as Markdown:

- throughput (translation units per minute) with 1 and with 4 workers on
  the same project, their ratio, and whether the two exports are identical;
- peak memory (maximum resident set size of the lcart process) with 4
  workers on a project and on one twice its size: with the bounded dispatch
  window of ADR-0019 it must not grow with the number of translation units.

Usage: scripts/measure-analysis.py [--lcart PATH] [--units N] [--runs R]
"""

import argparse
import os
import pathlib
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
GENERATOR = ROOT / "scripts" / "gen-many-tus.py"


def generate(directory: pathlib.Path, units: int) -> pathlib.Path:
    subprocess.run([sys.executable, str(GENERATOR), str(directory), "--units", str(units)],
                   check=True)
    return directory


def analyze(lcart: pathlib.Path, project: pathlib.Path, database: pathlib.Path,
            jobs: int) -> tuple[float, int]:
    """Runs one analysis; returns wall seconds and peak RSS in KiB."""
    started = time.monotonic()
    process = subprocess.Popen(
        [str(lcart), "analyze", "--root", str(project), "--db", str(database), "--force",
         "--jobs", str(jobs)],
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _, status, usage = os.wait4(process.pid, 0)
    elapsed = time.monotonic() - started
    process.returncode = os.waitstatus_to_exitcode(status)
    if process.returncode != 0:
        sys.exit(f"lcart analyze --jobs {jobs} on {project} exited {process.returncode}")
    return elapsed, usage.ru_maxrss


def export(lcart: pathlib.Path, database: pathlib.Path) -> bytes:
    return subprocess.run([str(lcart), "query", "export", "--db", str(database)],
                          check=True, capture_output=True).stdout


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--lcart", type=pathlib.Path,
                        default=ROOT / "build" / "debug-system" / "src" / "cli" / "lcart")
    parser.add_argument("--units", type=int, default=800)
    parser.add_argument("--runs", type=int, default=3,
                        help="runs per measurement; the fastest counts (default 3)")
    args = parser.parse_args()
    lcart = args.lcart.resolve()
    if not lcart.is_file():
        parser.error(f"no lcart executable at {lcart}; build it first")

    with tempfile.TemporaryDirectory(prefix="lcart-measure-") as temp:
        work = pathlib.Path(temp)
        project = generate(work / "project", args.units)
        double = generate(work / "double", 2 * args.units)

        seconds = {}
        peak = {}
        for jobs in (1, 4):
            runs = [analyze(lcart, project, work / f"jobs{jobs}.lcart.db", jobs)
                    for _ in range(args.runs)]
            seconds[jobs] = min(run[0] for run in runs)
            peak[(args.units, jobs)] = max(run[1] for run in runs)
        identical = export(lcart, work / "jobs1.lcart.db") == export(lcart, work / "jobs4.lcart.db")
        peak[(2 * args.units, 4)] = max(
            analyze(lcart, double, work / "double.lcart.db", 4)[1] for _ in range(args.runs))

    per_minute = {jobs: args.units / seconds[jobs] * 60 for jobs in seconds}
    print(f"lcart: {lcart}")
    print(f"cores: {os.cpu_count()}, runs per measurement: {args.runs} (fastest wall time,"
          " highest peak memory)")
    print()
    print("| Project | Workers | Wall time | TUs per minute | Peak RSS |")
    print("|---|---|---|---|---|")
    for jobs in (1, 4):
        print(f"| {args.units} TUs | {jobs} | {seconds[jobs]:.2f} s | {per_minute[jobs]:.0f} |"
              f" {peak[(args.units, jobs)] / 1024:.0f} MiB |")
    print(f"| {2 * args.units} TUs | 4 | | | {peak[(2 * args.units, 4)] / 1024:.0f} MiB |")
    print()
    print(f"Throughput ratio, 4 workers / 1 worker: {per_minute[4] / per_minute[1]:.2f}")
    print(f"Exports with 1 and 4 workers identical: {'yes' if identical else 'NO'}")
    print(f"Peak RSS, {2 * args.units} / {args.units} TUs with 4 workers:"
          f" {peak[(2 * args.units, 4)] / peak[(args.units, 4)]:.2f}")
    return 0 if identical else 1


if __name__ == "__main__":
    sys.exit(main())
