# Fixture: two_programs

Two programs in one compilation database, as libmodbus's tests are (B-10,
ADR-0013). `one/main.c` and `two/main.c` each define `main` and `work`,
both with external linkage; `work` is declared in `lib.h`.

| Function | Stored USR | Defined |
|---|---|---|
| `main` | `c:@F@main` | `one/main.c` 7–9 |
| `main` | `two/main.c#c:@F@main` | `two/main.c` 7–9 |
| `work` | `c:@F@work` | `one/main.c` 3–5 |
| `work` | `two/main.c#c:@F@work` | `two/main.c` 3–5 |

Each `main` calls the `work` of its own file (line 8, column 12). The
analysis warns once for each of the two later definitions.
