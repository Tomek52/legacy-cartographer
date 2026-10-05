#!/usr/bin/env python3
"""Generate a C project with many translation units and its compile_commands.json.

B-16 / NFR-005: the throughput, memory and 1-vs-4-worker determinism checks
run on a fixture of at least 200 translation units, generated on the fly
and never committed.

The project exercises the cross-TU decisions of the analysis (ADR-0012,
ADR-0013, ADR-0019): `static` functions with one name in every unit, a
`static inline` function and a macro in a shared header, calls across
modules, calls through a function pointer, several programs with their own
`main`, and one unit that does not parse.

Usage: scripts/gen-many-tus.py OUT_DIR [--units N] [--functions K]
                               [--extra-argument ARG ...]
"""

import argparse
import json
import pathlib
import sys

MODULES = 8
BROKEN_UNIT = 5
MAIN_EVERY = 10


def module_of(unit: int) -> str:
    return f"mod{unit % MODULES}"


def unit_path(unit: int) -> str:
    return f"{module_of(unit)}/unit{unit}.c"


def shared_header() -> str:
    return """#ifndef SHARED_H
#define SHARED_H

struct shared_ops {
    int (*apply)(int value);
};

#define SHARED_ADD(a, b) ((a) + (b))

static inline int shared_twice(int value) {
    return 2 * value;
}

#endif
"""


def module_header(module: int, units: int) -> str:
    lines = [f"#ifndef MOD{module}_H", f"#define MOD{module}_H", "",
             '#include "common/shared.h"', ""]
    for unit in range(module, units, MODULES):
        lines.append(f"int unit{unit}_step(int value);")
        lines.append(f"int unit{unit}_run(const struct shared_ops *ops, int value);")
    lines += ["", "#endif", ""]
    return "\n".join(lines)


def unit_source(unit: int, units: int, functions: int) -> str:
    previous = (unit - 1) % units
    lines = [
        "#include <stdio.h>",
        "#include <stdlib.h>",
        "#include <string.h>",
        "",
        '#include "common/shared.h"',
        f'#include "{module_of(unit)}/mod.h"',
        f'#include "{module_of(previous)}/mod.h"',
        "",
        "static int helper(int value) {",
        f"    return value * {unit + 1} + shared_twice(value);",
        "}",
        "",
        f"static int unit{unit}_f0(int value) {{",
        "    return helper(value);",
        "}",
    ]
    for k in range(1, functions):
        lines += [
            "",
            f"static int unit{unit}_f{k}(int value) {{",
            f"    return unit{unit}_f{k - 1}(value) + SHARED_ADD(value, {k});",
            "}",
        ]
    lines += [
        "",
        f"int unit{unit}_step(int value) {{",
        "    char buffer[32];",
        '    snprintf(buffer, sizeof buffer, "%d", value);',
        f"    return unit{unit}_f{functions - 1}((int)strlen(buffer));",
        "}",
        "",
        f"int unit{unit}_run(const struct shared_ops *ops, int value) {{",
        f"    int total = unit{unit}_step(value);",
        "    if (ops != NULL && ops->apply != NULL) {",
        "        total += ops->apply(total);",
        "    }",
        f"    return total + unit{previous}_step(value);",
        "}",
    ]
    if unit % MAIN_EVERY == MAIN_EVERY - 1:
        lines += [
            "",
            "int main(void) {",
            f"    return unit{unit}_run(NULL, 1) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;",
            "}",
        ]
    if unit == BROKEN_UNIT:
        lines += ["", "int broken(void) { return 1 +; }"]
    lines.append("")
    return "\n".join(lines)


def generate(out: pathlib.Path, units: int, functions: int, extra: list[str]) -> None:
    out.mkdir(parents=True, exist_ok=True)
    (out / "common").mkdir(exist_ok=True)
    (out / "common" / "shared.h").write_text(shared_header())
    for module in range(MODULES):
        (out / f"mod{module}").mkdir(exist_ok=True)
        (out / f"mod{module}" / "mod.h").write_text(module_header(module, units))
    commands = []
    for unit in range(units):
        (out / unit_path(unit)).write_text(unit_source(unit, units, functions))
        commands.append({
            "directory": str(out),
            "arguments": ["cc", "-c", "-I.", *extra, unit_path(unit)],
            "file": unit_path(unit),
        })
    (out / "compile_commands.json").write_text(json.dumps(commands, indent=1) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("out_dir", type=pathlib.Path)
    parser.add_argument("--units", type=int, default=200)
    parser.add_argument("--functions", type=int, default=20,
                        help="functions per translation unit (default 20)")
    parser.add_argument("--extra-argument", action="append", default=[], metavar="ARG",
                        help="add ARG to every compile command (repeatable)")
    args = parser.parse_args()
    if args.units < 1 or args.functions < 1:
        parser.error("--units and --functions must be at least 1")
    generate(args.out_dir.resolve(), args.units, args.functions, args.extra_argument)
    return 0


if __name__ == "__main__":
    sys.exit(main())
