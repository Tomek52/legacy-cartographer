# Fixture: fn_pointers

Direct and indirect calls (B-10,
FR-004 criteria 1 and 2). `backend.h`
models libmodbus's backend table (`modbus_backend_t` in
`targets/libmodbus/src/modbus-private.h`): a struct of function pointers
reached through `ctx->backend`, and `rtu.c` fills one with a positional
static initializer, as `modbus-rtu.c` does. FR-015 will resolve calls
through it.

| Caller | Calls (file:line:column) |
|---|---|
| `rtu_send` (`rtu.c`) | direct `checksum` 5:12 |
| `rtu_recv` (`rtu.c`) | none |
| `checksum` (`core.c`) | none |
| `transfer` (`core.c`) | indirect `backend.send` 10:16, indirect `backend.recv` 11:19 |
| `apply` (`core.c`) | indirect (plain pointer, no member) 15:12 and 15:29 |
| `run` (`core.c`) | direct `apply` 20:12, direct `transfer` 20:28, indirect `local.recv` 20:48 |

`run` passes `checksum` to `apply` without calling it: not an edge.
