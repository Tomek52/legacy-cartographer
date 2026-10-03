# targets/

Third-party code bases analysed by `lcart`, as pinned git submodules.

Rules ([NFR-008](../docs/specs/requirements.md), [ADR-0003](../docs/adr/0003-licensing.md)):

- Never modify anything inside a submodule. Build directories, harnesses,
  tests and wrappers live outside `targets/`.
- Each submodule is pinned to a tagged upstream commit, recorded in
  `docs/targets/<name>.md`.
- The code here is under its own licence, not Apache-2.0 (see `NOTICE`).

Content:

```text
targets/
└── libmodbus/   # https://github.com/stephane/libmodbus, tag v3.2.0
```

After cloning this repository:

```bash
git submodule update --init targets/libmodbus
scripts/check-targets.sh      # pinned commit, no local changes (also in CI)
```

To move a target to another version: check out the new tag inside the
submodule, update the commit in `docs/targets/<name>.md`, run
`scripts/check-targets.sh`, and commit both together.
