# targets/

Third-party code bases analysed by `lcart`, as pinned git submodules.

Rules ([NFR-008](../docs/specs/requirements.md), [ADR-0003](../docs/adr/0003-licensing.md)):

- Never modify anything inside a submodule. Build directories, harnesses,
  tests and wrappers live outside `targets/`.
- Each submodule is pinned to a tagged upstream commit, recorded in
  `docs/targets/<name>.md`.
- The code here is under its own licence, not Apache-2.0 (see `NOTICE`).

Expected content after setup:

```text
targets/
└── libmodbus/   # https://github.com/stephane/libmodbus, tag v3.2.0
```

Add it with:

```bash
git submodule add https://github.com/stephane/libmodbus targets/libmodbus
git -C targets/libmodbus checkout v3.2.0
git add .gitmodules targets/libmodbus
git commit -m "build(targets): add libmodbus v3.2.0 as submodule"
```
