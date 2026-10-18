# Fixture: fn_pointers

Direct and indirect calls (B-10,
FR-004 criteria 1 and 2), and `may_call` edges through function-pointer
tables (B-26, FR-015). `backend.h`
models libmodbus's backend table (`modbus_backend_t` in
`targets/libmodbus/src/modbus-private.h`): a struct of function pointers
reached through `ctx->backend`. `rtu.c` fills one with a positional
static initializer, as `modbus-rtu.c` does; `tcp.c` fills another with a
designated initializer that assigns only `send`.

| Caller | Calls (file:line:column) |
|---|---|
| `rtu_send` (`rtu.c`) | direct `checksum` 5:12 |
| `rtu_recv` (`rtu.c`) | none |
| `tcp_send` (`tcp.c`) | none |
| `checksum` (`core.c`) | none |
| `transfer` (`core.c`) | indirect `backend.send` 10:16, indirect `backend.recv` 11:19 |
| `apply` (`core.c`) | indirect (plain pointer, no member) 15:12 and 15:29 |
| `run` (`core.c`) | direct `apply` 20:12, direct `transfer` 20:28, indirect `local.recv` 20:48 |
| `size_only` (`core.c`) | none: `checksum(1)` is the operand of `sizeof` |
| `local_pointer` (`core.c`) | indirect (local pointer, no member) 29:12 |

`run` passes `checksum` to `apply` without calling it: not an edge.

Functions assigned to the fields of `backend_t` in static initializers:
`send` gets `rtu_send` (`rtu.c`, positional) and `tcp_send` (`tcp.c`,
designated); `recv` gets `rtu_recv` (`rtu.c`, positional) only.
`local_pointer`'s `local_handler = checksum` is not a field.

| Caller | `may_call` edges (file:line:column, via) |
|---|---|
| `transfer` | `rtu_send` and `tcp_send` at 10:16 (`backend.send`); `rtu_recv` at 11:19 (`backend.recv`) |
| `run` | `rtu_recv` at 20:48 (`local.recv`: a local of type `backend_t`) |
| `apply`, `local_pointer` | none: the pointer is a parameter or a local, not a field |
