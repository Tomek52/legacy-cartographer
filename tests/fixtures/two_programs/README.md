# Fixture: two_programs

Two programs in one compilation database, as libmodbus's tests are (B-10,
ADR-0013). Translation units, in the order they are analysed:
`one/main.c`, `one/report.c`, `three/use.c`, `two/extra.c`, `two/main.c`.

| Function | Stored USR | Defined |
|---|---|---|
| `main` | `c:@F@main` | `one/main.c` 7–9 |
| `main` | `two/main.c#c:@F@main` | `two/main.c` 7–9 |
| `work` | `c:@F@work` | `one/main.c` 3–5 |
| `work` | `two/main.c#c:@F@work` | `two/main.c` 3–5 |
| `twice` | `c:@F@twice` | `lib.h` 6–8 (C99 `inline`, external) |
| `report` | `c:@F@report` | `one/report.c` 1 |
| `report` | `two/report.h#c:@F@report` | `two/report.h` 4 (included by `two/extra.c` and `two/main.c`) |
| `use` | `c:@F@use` | `three/use.c` 3–5 |
| `extra` | `c:@F@extra` | `two/extra.c` 3 |

- Each `main` calls the `work` of its own file (line 8, column 12).
- `twice` is reached from three translation units and is one function; its
  two calls (`lib.h` 7:12 and 7:26) are recorded once, from `one/main.c`,
  and name the first `work`.
- `use` only declares `work` and calls the first one (`three/use.c` 4:12).
- The analysis warns once each for `report` (`two/report.h`), `work` and
  `main` (`two/main.c`); `report` is not warned about twice.
