# Agent output (contract v1)

The agent writes, and only writes, into the output directory that
PROMPT.md names (`lcart-out/agent/<run-id>/` by default):

| File | Content |
|---|---|
| `overview.md` | purpose of the code base, module list with one paragraph each |
| `modules/<module>.md` | one file per module: responsibility, main functions, dependencies |
| `c4-context.md`, `c4-container.md` | Mermaid diagrams (flowchart syntax) with short explanations |
| `glossary.md` | domain terms found in names and facts |
| `claims.json` | every structural statement in the documents, as claims valid against `claim-schema.json` |

Each sentence that states a structural fact ends with the claim marker
`[c-NNNN]`, matching the claim `id`, so `lcart ingest` can annotate the
sentence with its verdict.

`lcart ingest` rejects the output (non-zero exit, list of problems) if a
required file is missing, `claims.json` is invalid, a marker has no claim,
a claim has no marker, or a file outside the list exists.
