# Claim schema and verification rules

The agent or LLM writes prose, but every **structural** statement in it must
also be emitted as a claim: a small JSON object that the verifier can check against
the fact database ([FR-009](requirements.md#fr009-structured-claims-v2),
[FR-010](requirements.md#fr010-verify-claims-v1)). The C++ model is in
`src/claims/claim.hpp` and `src/verify/verdict.hpp`.

## <a id="claim-kinds-v1"></a>Claim kinds

| Kind | Subject | Object | Meaning (affirmed) |
|---|---|---|---|
| `calls` | function | function | subject's body contains a direct call to object |
| `includes` | file | file | subject has an `#include` that resolves to object |
| `depends_on` | module | module | some file of subject includes or calls into object |
| `defines` | file | function | subject contains object's definition |
| `belongs_to` | file or function | module | subject is assigned to object |

A claim can be **negated** ("`modbus-tcp.c` does not include
`modbus-rtu.h`"). Statements about behaviour, intent, performance or
correctness are not claims: they stay prose and the report counts them as
"not checkable" only if the LLM marks them (out of scope for v1).

## <a id="claim-json-schema-v2"></a>JSON Schema (v1)

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "lcart:claim:v1",
  "title": "Claims emitted for one generated section",
  "type": "object",
  "required": ["schema", "claims"],
  "additionalProperties": false,
  "properties": {
    "schema": { "const": "lcart:claim:v1" },
    "claims": {
      "type": "array",
      "maxItems": 500,
      "items": { "$ref": "#/$defs/claim" }
    }
  },
  "$defs": {
    "entity": {
      "type": "object",
      "required": ["type", "name"],
      "additionalProperties": false,
      "properties": {
        "type": { "enum": ["function", "file", "module"] },
        "name": { "type": "string", "minLength": 1, "maxLength": 512 },
        "file": { "type": "string", "maxLength": 1024 }
      }
    },
    "claim": {
      "type": "object",
      "required": ["id", "kind", "subject", "object", "polarity", "text"],
      "additionalProperties": false,
      "properties": {
        "id": { "type": "string", "pattern": "^c-[0-9]{4,6}$" },
        "kind": { "enum": ["calls", "includes", "depends_on", "defines", "belongs_to"] },
        "subject": { "$ref": "#/$defs/entity" },
        "object": { "$ref": "#/$defs/entity" },
        "polarity": { "enum": ["affirmed", "negated"] },
        "text": { "type": "string", "maxLength": 2000 },
        "source": {
          "type": "object",
          "additionalProperties": false,
          "properties": {
            "document": { "type": "string" },
            "section": { "type": "string" }
          }
        }
      }
    }
  }
}
```

Entity types per kind (for example `calls` needs two functions) are checked
by the parser after schema validation; a mismatch is a `schema_violation`.
So is a claim ID used twice in one document: claim markers (`[c-NNNN]`) and
verdicts refer to a claim by its ID. The parser reports every problem it
finds, each with its JSON pointer (`/claims/3/kind`), and returns no claims
from a document that has one.

The limits (`maxItems`, `maxLength`) bound the work an adversarial or broken
model output can cause ([security](../security.md)). Before building a
document, the parser also rejects input larger than 4 MiB and nesting deeper
than 4 levels (document, `claims`, claim, entity or `source`: the most the
schema has), so its recursion is bounded by a constant and its memory by a
fixed multiple of the size limit.

Example:

```json
{
  "schema": "lcart:claim:v1",
  "claims": [
    {
      "id": "c-0001",
      "kind": "calls",
      "subject": { "type": "function", "name": "modbus_receive" },
      "object": { "type": "function", "name": "_modbus_receive_msg" },
      "polarity": "affirmed",
      "text": "modbus_receive delegates frame reception to _modbus_receive_msg.",
      "source": { "document": "modules/core.md", "section": "Receiving requests" }
    }
  ]
}
```

(Function names taken from libmodbus v3.2.0 `src/modbus.c`; whether the call
is direct is for the verifier to decide.)

## <a id="claim-verification-rules-v2"></a>Verification rules

Verification is a pure function of the claim and the fact database. It is
deterministic and needs no LLM.

### <a id="claim-entity-resolution-v2"></a>Step 1: resolve entities

The subject is resolved first, then the object; the first entity that does
not resolve decides the verdict.

- **file:** by path as the fact database records it (relative to the
  analysis root; absolute for a file outside it). An exact match wins;
  otherwise the files whose path ends with `/` and the name
  (`modbus.c` → `src/modbus.c`, but not `libmodbus.c`). Several → `unverified` /
  `entity_ambiguous`.
- **function:** by (unqualified) name. The fact database locates each
  function at one file only: its definition, else its first declaration
  ([function facts](../architecture/data-model.md#arch-data-model-function-facts-v2)).
  If `file` is given, it keeps the functions of that name located in a file
  that `file` matches as a file entity would (exact, else suffix, every
  match). If none is located there, `file` keeps all of them, because the
  declaration it may name (a header) is not recorded: `file` only
  disambiguates. Several → `unverified` / `entity_ambiguous`.
- **module:** by exact name.
- **Not found:** if analysis coverage is complete, `contradicted` /
  `entity_not_found` (the LLM named something that does not exist, a typical
  hallucination). If coverage is incomplete, `unverified` /
  `coverage_incomplete`. Coverage is **complete** when at least one
  translation unit was analysed and every one was parsed; a database without
  coverage counts as incomplete.
- These outcomes are the same for both polarities ([negation](#claim-negation-rule-v2)
  does not apply to them): a claim that names something that does not exist
  is wrong whichever way it is put.

Evidence: for `entity_ambiguous`, the candidates; for `entity_not_found`,
no fact, since there is none to cite: the coverage the report shows is the
evidence; for `coverage_incomplete`, none.

### <a id="claim-relation-rules-v2"></a>Step 2: check the relation (affirmed claims)

A file **was parsed** when it is under the analysis root and no
diagnostic of the analysis is an error or a fatal error. An error is not
local to where Clang reports it: Clang suppresses follow-on errors at the
uses of an invalid declaration, so a call in another file of the same unit
becomes an expression with errors, is not recorded, and no error names that
file (a parameter of an unknown type in one header silently drops the calls
to that function in the next). After a fatal error (a missing `#include`,
too many errors) Clang goes on parsing the unit but reports no further
errors at all. So error recovery can drop facts silently in any file a unit
with an error reached, and the recorded include edges cannot bound that
set: a unit whose main file is outside the root has none, and the
directives of headers outside the root are not recorded, so a header they
include is reached without an edge. Therefore any error in the analysis means no
file counts as parsed, and absent `calls`, `includes` and `defines`
relations are `unverified` until the analysis is clean (narrowing this is
an [open question](../open-questions.md#open-technical-questions)). A
function's **definition was parsed** when the function is recorded
as a definition and its file was parsed. A function belongs to the module of
its file; a function whose file is not recorded is an inconsistent fact
database, an error rather than a verdict.

| Kind | Relation found | Relation absent |
|---|---|---|
| `calls` | a direct call from subject to object: `confirmed` (evidence: those calls) | `unverified` / `coverage_incomplete` if the subject's definition was not parsed; else `unverified` / `indirect_calls_present` if it has indirect calls (incl. `may_call` edges, [FR-015](requirements.md#fr015-resolve-function-pointer-tables-v1); evidence: its indirect calls and its `may_call` edges to the object); else `contradicted` (evidence: the subject) |
| `includes` | an include directive in subject resolving to object: `confirmed` (evidence: those directives) | `contradicted` if subject was parsed (evidence: the subject); else `unverified` / `coverage_incomplete` |
| `depends_on` | the module dependency: `confirmed` (evidence: the include directives and calls that justify it) | `unverified` / `indirect_calls_present` if a function of subject has a `may_call` edge to a function of object (evidence: those edges); else `contradicted` if coverage is complete (evidence: the subject); else `unverified` / `coverage_incomplete` |
| `defines` | object recorded as a definition located in subject: `confirmed` (evidence: the object) | `contradicted` if subject was parsed (evidence: the subject); else `unverified` / `coverage_incomplete` |
| `belongs_to` | subject's module is object: `confirmed` | `contradicted`; evidence either way: the subject, and for a function also its file |

The reason is `relation_found` or `relation_absent` unless the table names
another. `may_call` edges never make a `calls` or `depends_on` relation
found (they are no module dependencies,
[ADR-0026](../adr/0026-may-call-edges-from-function-pointer-tables.md)). Evidence
names rows of the fact database, each with its table
([ADR-0029](../adr/0029-verdict-evidence-names-its-fact-table.md)).

### <a id="claim-negation-rule-v2"></a>Step 3: negation

For a negated claim, the `confirmed` and `contradicted` of step 2 swap,
with the same reason and evidence; `unverified` stays. So a negated `calls`
whose relation is absent but the subject has indirect calls is
`unverified`, because the call may still happen through a pointer. Step 1's
outcomes do not swap.

### <a id="claim-status-semantics-v1"></a>What the statuses promise

- **confirmed:** the fact database contains the relation. It does not prove
  the surrounding prose is right, only this structural statement.
- **contradicted:** the fact database, under complete coverage, shows the
  statement is false. These must be fixed or flagged before the documentation
  is used.
- **unverified:** the tool cannot tell. The report gives the reason; a human
  decides.

The worst verifier error is a false `confirmed` (it lends authority to a
wrong statement), so the rules prefer `unverified` whenever the evidence is
incomplete. Accuracy is measured as described in the
[testing strategy](../testing/strategy.md#testing-verifier-accuracy-v1).

## <a id="claim-verification-report-format-v1"></a>Verification report (format 1)

`lcart verify` (and `lcart ingest`, B-33) writes the verdicts as
`verification-report.json` and `verification-report.md`
([FR-011](requirements.md#fr011-verification-report-v2),
[ADR-0030](../adr/0030-verification-report-and-lcart-verify.md#adr0030-report-format-v1)).
Neither has a generation time: identical input gives identical bytes.

The JSON report has these keys, in this order, every key always present:

| Key | Content |
|---|---|
| `format` | `"lcart-verification-report"` |
| `format_version` | `1`; any change to keys, their meaning or their order increments it |
| `tool_version` | lcart's version |
| `fact_schema_version` | the fact database's schema version |
| `handoff_contract_version` | the contract version of the bundle the claims answer; `null` for `lcart verify` |
| `agent` | the agent and model as the user stated them (`lcart ingest --agent`); `""` for `lcart verify` |
| `coverage` | `translation_units_total`, `translation_units_parsed`, `complete` (at least one unit, every one parsed: [entity resolution](#claim-entity-resolution-v2)), `error_diagnostics` (errors and fatal errors: with any, no file [was parsed](#claim-relation-rules-v2)) |
| `counts` | `by_status`: `confirmed`, `unverified`, `contradicted` and their counts; `by_kind`: the five claim kinds in the order of [claim kinds](#claim-kinds-v1), each with the three counts |
| `claims` | every claim, in the order of the claims file: `id`, `kind`, `polarity`, `subject` and `object` (`type`, `name`, `file` or `null`), `text`, `source` (`document`, `section`, `""` if not given), then the verdict: `status`, `reason`, `evidence` |

`evidence` lists the facts a verdict rests on, in the verdict's order
([ADR-0029](../adr/0029-verdict-evidence-names-its-fact-table.md#adr0029-fact-references-v1)),
each as the fact itself rather than a row id. `table` comes first, then:

| `table` | Keys |
|---|---|
| `module` | `module` (its name) |
| `file` | `file` |
| `function` | `function` (its name), `file`, `line` (first line), `definition` |
| `include_edge` | `file` (the including file), `line`, `target` (`null` if unresolved) |
| `call_edge` | `kind` (`direct`, `indirect`, `may_call`), `caller`, `callee` (`null` for an indirect call), `via`, `file`, `line`, `column` (the call site) |

Files are named as in a handoff bundle
([bundle paths](handoff-contract.md#claim-handoff-bundle-paths-v1)): no
absolute path appears. `entity_not_found` has no evidence; the coverage is
its evidence.

The Markdown report starts with `# Verification report` and a table of
the versions and the agent, then `## Counts` (per kind and status, and a
row `all`), `## Contradicted claims` and `## Unverified claims` (each claim
with its statement, text, source, reason and evidence as `file:line`, or
`file:line:column` for a call), and `## Coverage`. Confirmed claims are
only counted. Every name, path and text is a code span, and a `|` in a
table cell is written `\|`, so a claim's text cannot add Markdown or HTML.

## Out of scope for v1

- Transitive claims ("A eventually calls B"). Candidate for v2 with a path as
  evidence.
- Data-flow claims ("A writes field X"), claims about macros, types and
  globals.
- Claims about behaviour. Characterization tests, not the verifier, are the
  tool for those.
