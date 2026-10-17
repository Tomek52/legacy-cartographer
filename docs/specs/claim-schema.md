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

## <a id="claim-verification-rules-v1"></a>Verification rules

Verification is a pure function of the claim and the fact database. It is
deterministic and needs no LLM.

### <a id="claim-entity-resolution-v1"></a>Step 1: resolve entities

- **function:** by name; if `file` is given, only functions defined or
  declared in that file. Several matches → `unverified` / `entity_ambiguous`.
- **file:** by path relative to the analysis root; a unique path suffix
  match is accepted (`modbus.c` → `src/modbus.c`). Several → ambiguous.
- **module:** by exact name.
- **Not found:** if analysis coverage is complete, `contradicted` /
  `entity_not_found` (the LLM named something that does not exist, a typical
  hallucination). If coverage is incomplete, `unverified` /
  `coverage_incomplete`.

### <a id="claim-relation-rules-v1"></a>Step 2: check the relation (affirmed claims)

| Kind | Relation found | Relation absent |
|---|---|---|
| `calls` | `confirmed` | `contradicted` if subject's definition was parsed and has no indirect calls; `unverified` / `indirect_calls_present` if it has indirect calls (incl. `may_call` edges, [FR-015](requirements.md#fr015-resolve-function-pointer-tables-v1)); `unverified` / `coverage_incomplete` if the definition was not parsed |
| `includes` | `confirmed` | `contradicted` if subject was parsed; else `unverified` |
| `depends_on` | `confirmed` (evidence: the edges) | `contradicted` if coverage is complete; else `unverified` |
| `defines` | `confirmed` | `contradicted` |
| `belongs_to` | `confirmed` | `contradicted` |

### <a id="claim-negation-rule-v1"></a>Step 3: negation

For a negated claim, `confirmed` and `contradicted` swap; `unverified` stays.
Exception: a negated `calls` whose relation is absent but the subject has
indirect calls is `unverified`, because the call may still happen through a
pointer.

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

## Out of scope for v1

- Transitive claims ("A eventually calls B"). Candidate for v2 with a path as
  evidence.
- Data-flow claims ("A writes field X"), claims about macros, types and
  globals.
- Claims about behaviour. Characterization tests, not the verifier, are the
  tool for those.
