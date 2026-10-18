# Verification report

| Item | Value |
|---|---|
| Tool version | `0.0.1` |
| Fact schema version | 3 |
| Handoff contract version | none |
| Agent | none |

## Counts

| Kind | confirmed | unverified | contradicted | total |
|---|---|---|---|---|
| `calls` | 2 | 1 | 2 | 5 |
| `includes` | 1 | 0 | 0 | 1 |
| `depends_on` | 0 | 0 | 0 | 0 |
| `defines` | 0 | 0 | 1 | 1 |
| `belongs_to` | 1 | 0 | 0 | 1 |
| all | 4 | 1 | 3 | 8 |

## Contradicted claims

### `c-0003`

- Claim (`calls`, negated): function `rtu_send` in `rtu.c` → function `checksum`
- Text: `rtu_send does not compute a checksum itself.`
- Source: `modules/root.md`, section `Backends`
- Reason: `relation_found`
- Evidence:
  - `rtu.c:5:12` direct call from `rtu_send` to `checksum`

### `c-0005`

- Claim (`defines`, affirmed): file `core.c` → function `rtu_send`
- Text: `core.c defines rtu_send.`
- Source: `modules/root.md`, section `Backends`
- Reason: `relation_absent`
- Evidence:
  - file `core.c`

### `c-0006`

- Claim (`calls`, affirmed): function `run` → function `modbus_flush`
- Text: `run flushes the context with modbus_flush.`
- Reason: `entity_not_found`
- Evidence: no fact to cite; the coverage below is the evidence

## Unverified claims

### `c-0002`

- Claim (`calls`, affirmed): function `transfer` → function `tcp_send`
- Text: `transfer sends through tcp_send.`
- Source: `modules/root.md`, section `Main functions`
- Reason: `indirect_calls_present`
- Evidence:
  - `core.c:10:16` indirect call from `transfer` via `backend.send`
  - `core.c:11:19` indirect call from `transfer` via `backend.recv`
  - `core.c:10:16` `may_call` edge from `transfer` to `tcp_send` via `backend.send`

## Coverage

3 of 3 translation units parsed: coverage complete. 0 error diagnostics; with any, an absent `calls`, `includes` or `defines` relation is unverified.
