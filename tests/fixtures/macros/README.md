# Fixture: macros

Calls written in macros (B-10, FR-004 criterion 3). One translation unit,
`main.c`; every call is located at the macro's use and belongs to the
function where the macro expands.

| Caller | Calls (main.c line:column) |
|---|---|
| `step` | `log_value` 10:32 (`LOG`) |
| `zero` (defined by `DEFINE_GETTER`, line 11) | `log_value` 11:1 |
| `main` | `step` 14:5 twice (`RUN_TWICE`); `log_value`, `fail`, `log_value` 15:12 (`CHECKED` expands its argument twice); `zero` 15:45 |

`strlen` is declared in `<string.h>`, outside the root, so its two calls
in `main` are not edges.
