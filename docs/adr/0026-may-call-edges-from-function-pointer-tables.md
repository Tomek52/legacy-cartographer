# 0026. may_call edges from function-pointer tables

- **Status:** Accepted
- **Date:** 2026-10-18
- **Requirements:** [FR-015](../specs/requirements.md#fr015-resolve-function-pointer-tables-v1),
  [NFR-004](../specs/requirements.md#nfr004-deterministic-output-v1),
  [B-26](../plan/backlog.md#b26-function-pointer-tables-v1)
- **Supersedes / superseded by:** — (amends the export format of
  [ADR-0016](0016-facts-export-and-command-line.md#adr0016-export-format-v1):
  a `may_call` record kind and `format_version` 2)

## Context

libmodbus reaches its transports through a struct of function pointers,
`modbus_backend_t` (`src/modbus-private.h`, v3.2.0), filled by positional
static initializers in `src/modbus-rtu.c` and `src/modbus-tcp.c`. Since
B-10 every call through it is an `indirect` edge with a `via` such as
`backend.check_integrity` and no callee, so a `calls` claim about a
transport can never be confirmed or contradicted. FR-015 asks for
`may_call` edges from such a call to every function assigned to the field
in static initializers; the claim schema already treats them as
"indirect calls present", never as a confirmation. The fact database's
`CALL_EDGE.kind` already allows `may_call`, but nothing records it.

The table and the calls through it are usually in different translation
units (TUs). The analysis replays TUs in path order through one writer,
commits each TU on its own and may be resumed after an interruption
([ADR-0019](0019-parallel-resumable-analysis.md#adr0019-resume-v1)), so
an edge cannot be decided while one TU is replayed, and the writer's
memory does not survive a resume. Four things are open: what counts as
"assigned in a static initializer", how a field is identified across TUs,
when the edges are resolved, and how they appear in queries and the
export.

## Options considered

Identity of a field across TUs:

1. **Clang's USR of the field** (`clang::index::generateUSRForDecl` on the
   `FieldDecl`, `clang/Index/USRGeneration.h`): `c:@S@_modbus_backend@FI@check_integrity`;
   a field of an anonymous struct named by a `typedef` gets the typedef's
   name (`@SA@`). The same for every TU that includes the header. Two
   different structs with one tag, each defined in its own source file,
   share it.
2. **Struct and field name as spelled** (`modbus_backend_t.check_integrity`).
   A struct is named by its tag in one place and by a typedef in another,
   so one field gets several keys.
3. **The USR, prefixed with the path of a source file that defines the
   struct**, as ADR-0012 does for `static` functions. Keeps same-tag
   structs of different source files apart; more code, and a struct
   defined in a header (the libmodbus case) would still need option 1.

When the edges are resolved:

1. **In the writer's memory while TUs are replayed.** A table seen after
   the call needs the call kept anyway, and a resumed run would have to
   rebuild the state from the database.
2. **Field assignments stored as facts with each TU, edges derived after
   the last TU**, like `MODULE_DEPENDENCY` (ADR-0019 commit per TU). The
   edges cannot depend on the order of TUs, and resume needs nothing new.

What counts as an assignment:

1. **Initializers of variables with static storage duration** only: what
   FR-015 names, decidable from the AST of one TU, no data flow.
2. **Also automatic variables and run-time assignments** (`b.send = f`).
   Closer to what may run; needs data flow or over-approximates in ways
   FR-015 does not ask for.

## Decision

<a id="adr0026-field-assignments-v1"></a>**Field assignments.** A field
assignment is a function named, as `f` or `&f` (in parentheses or cast),
by the initializer of a variable with static storage duration (at file
scope, `static` or not, or a `static` local) for a struct field of
function-pointer type, positional or designated: Clang's semantic form of
the initializer list decides which field a value initializes. Nested
structs, unions (their initialized member), base classes and array
elements count. Not counted: initializers of automatic variables,
assignments in code, compound literals, a table copied from another
variable, initializers that depend on a template parameter, functions
that are not recorded (outside the root) and initializers located
outside the root. The field is identified by **Clang's USR of the field**
(option 1); two structs with the same tag in different source files share
their fields' keys, which can only add `may_call` edges, never hide one.

<a id="adr0026-indirect-calls-through-fields-v1"></a>**Calls through a
field.** An indirect call whose callee, after parentheses, implicit casts
and `*`, is a member access of a field of function-pointer type
(`p->f(...)`, `s.f(...)`, `(*p->q->f)(...)`) keeps that field's USR with
the call edge. The field, so the struct type, is the one Clang resolved
for the member access. A call through a local pointer, a parameter, an
array element (`t[i](...)`) or a call's result has no field and gets no
`may_call` edge, even if its value was read from a table.

<a id="adr0026-resolution-after-last-tu-v1"></a>**Resolution after the
last translation unit.** Each TU's field assignments are written as facts
in its transaction (`FIELD_ASSIGNMENT`: field USR and function, once per
pair), so `--resume` keeps them. After the last TU, before module
dependencies, the `may_call` edges are replaced in one transaction: one
per indirect call with a field and per distinct function assigned to that
field in any TU, at the call's site, with its `via` and field and the
function as callee. They are not module dependencies: `MODULE_DEPENDENCY`
stays derived from include edges and direct calls only, so
[ADR-0017](0017-module-map-format-and-command-line.md)'s module map does
not change. The fact database's `schema_version` becomes 3.

<a id="adr0026-queries-and-export-v1"></a>**Queries and export.**
`FactQuery::may_calls_from(caller)` and `may_calls()` return them (kind
`may_call`), by call site, then callee USR; `calls_from`, `calls_to` and
`calls` never do, and `lcart query callees` keeps listing direct and
indirect calls only. The facts export of ADR-0016 gets a record kind
`may_call` right after the `call` records: keys `caller`, `file`, `line`,
`column`, `callee`, `via`, sorted by file, line, column, caller, callee,
via. Field USRs and field assignments are not exported. The export's
`format_version` becomes 2; everything else in ADR-0016's format stays.

## Consequences

- On libmodbus v3.2.0 the 36 indirect calls through `ctx->backend` (all in
  `src/modbus.c`) get 74 `may_call` edges to the RTU and TCP
  implementations; the three calls through `ctx_rtu->set_rts`, which
  `src/modbus-rtu.c` assigns in code (lines 1098 and 1390), get none.
  B-27 can answer `calls` claims about the backends with `unverified` /
  `indirect_calls_present`, and handoff bundles (B-30) can list the
  edges.
- The edges do not depend on TU order, thread count or resume (tests with
  every order of the `fn_pointers` fixture, renamed TUs and an
  interrupted run).
- Tables filled at run time (`ctx->backend = &custom`) or by a function
  that returns a table are not seen; such code keeps plain indirect
  calls. Revisit with data flow if a target needs it.
- A C++ field of reference-to-function type or a pointer to member
  function is not a function-pointer field here; C++ virtual calls stay
  `direct` (data model).
- Exports of format 1 differ only in the header line where no table
  exists; consumers that check `format_version` must accept 2.
- Fact databases of schema 2 are refused; re-run the analysis.
