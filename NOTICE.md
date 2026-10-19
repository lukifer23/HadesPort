# Third-party notices

These are **not vendored**. `scripts/prepare_native_dependencies.py` downloads the exact
releases (SHA-256 verified) into `.local/native-deps/` at build time. Versions match the ones
embedded in the original engine (identified from strings and symbols in `EngineWin64s.dll`).

| Component | Version | License | Source |
|---|---|---|---|
| Lua | 5.2.2 | MIT | https://www.lua.org/ |
| Luabins | 0.3 | MIT | https://github.com/agladysh/luabins |
| LZ4 | 1.9.4 | BSD-2-Clause | https://github.com/lz4/lz4 |
| zlib | system (Android NDK / macOS) | zlib | https://zlib.net/ |

Tooling (not linked into the app):

| Tool | License | Use |
|---|---|---|
| [REA](https://github.com/morluto/rea) by morluto | MIT | Binary/archive analysis and decompilation (`scripts/rea.py` launches a sibling checkout) |
| LLVM `llvm-objdump` (Android NDK) | Apache-2.0 with LLVM exception | Disassembly in `native/analysis/inspect_engine.py` |
| Rust `pdb` crate (via `native/analysis/pdb_contract`) | MIT / Apache-2.0 | PDB symbol/type reading |

Hades and its content © Supergiant Games — see `DISCLAIMER.md`.
