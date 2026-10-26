# HadesPort

An unofficial, work-in-progress **native ARM64 Android engine** for **Hades (Supergiant Games,
2020)**. It runs the game's *original* Lua scripts and assets — taken from **your own legally
purchased PC copy** — on a C++/OpenGL ES engine reconstructed from the PC engine binary.
No emulation, Wine, CPU translation, ads, accounts or network access.

> **Status: prototype; full gameplay remains incomplete** (2026-10-26). Iteration12
> is installed and hash verified on the device. On the host, a new autoplay driver
> plays the original scripts through the opening room and the whole first encounter
> (four waves) and reaches the reward spawn; save and Continue round-trip. Athena's
> boon, the next room and later biomes are in progress. Missing engine behaviour is
> logged as engine gaps. The code audit and current priorities are in
> [`AUDIT.md`](AUDIT.md); verification boundaries are in [`STATUS.md`](STATUS.md).

**This repository contains no game files.** You must own Hades (e.g. on Steam or the Epic
Games Store) and supply its files yourself. Read [`DISCLAIMER.md`](DISCLAIMER.md).

## How it works

- **The original scripts drive everything.** Hades' gameplay is mostly Lua. The engine
  exposes the same script API (`ScriptManager` actions, triggers, notifies) the PC engine
  does, reconstructed from `EngineWin64s.dll` and its matching PDB.
- **Reconstruct, don't invent.** Each engine behaviour is rebuilt from the binary, and the
  source comments cite the virtual address (VA/RVA) it came from. Anything not yet rebuilt
  fails loudly (`SCRIPT_STAGE_BLOCKED … detail=<missing piece>`) instead of being faked.
  Approximations are labelled in the code.
- **Original assets, exported locally.** Bounded exporters in `native/tools/` read your game
  files (maps, animations, textures, FMOD banks, text) and write versioned packs under
  `.local/`, which are bundled into a locally built APK. Nothing is uploaded or shared.
- **Original settings and saves.** Settings mirror the PC game's `ConfigOptions` and are
  stored in the original `Profile1.sjson` / `Profile1.ctrls` formats. Saves use the original
  SGB1 format (Luabins + LZ4).

## Layout

| Path | Contents |
|---|---|
| `native/app/` | Engine: C++20, GLES 3, Android NativeActivity + a small Java bridge (vibration, controllers, game mode) |
| `native/host/` | macOS host debugger (`hades-debug`) and capture tools for fast replays without a device |
| `native/tools/` | Asset exporters and stagers (write fresh, versioned output dirs under `.local/`) |
| `native/analysis/` | Recovered contracts (`*-contract.json`), engine identity, RE helper scripts |
| `scripts/` | `build_native.py` (stage assets + build APK), dependency fetcher, REA launcher |
| `AUDIT.md` · `STATUS.md` · `HANDOFF.md` · `AGENTS.md` | Code audit/priorities, live progress, resume guide, contributor rules |

## Requirements

- macOS (arm64) host; Python 3.13 with Pillow; CMake + Ninja; Node.js (for REA).
- Android SDK with `ndk;29.0.14206865`, `build-tools;37.0.0`, `platforms;android-36`
  (`ANDROID_HOME`, default `~/Library/Android/sdk`), JDK 21 (`JAVA_HOME`).
- An arm64 Android device (developed on a Galaxy Z Fold with Snapdragon 8 Elite).
- Your own copy of Hades for Windows. The engine targets the build whose
  `EngineWin64s.dll` SHA-256 is `d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254`
  (see `native/analysis/engine-identity.json`); other builds are untested.
- For reverse-engineering work only: [REA](https://github.com/morluto/rea) checked out next to
  this repo (`../rea`, or set `REA_HOME`).

## Quick start

```sh
# 1. Copy your installed game (the folder containing Content/ and x64/) into the ignored game dir
mkdir -p .local/game && cp -R "/path/to/Hades/." .local/game/base/

# 2. Fetch the pinned native dependencies (Lua 5.2.2, Luabins 0.3, LZ4 1.9.4; SHA-256 checked)
python3 scripts/prepare_native_dependencies.py

# 3. Export asset packs with native/tools/* (see HANDOFF.md for the current set), then build
python3 scripts/build_native.py --scene <scene.bin> --geometry <room-geometry.json> --animations <animations.bin>

# 4. Install on your device
adb -s "$HADES_DEVICE_SERIAL" install --no-incremental -r --user 0 .local/build/native/hades-native-engine-preview.apk
```

The asset pipeline is still evolving; `HANDOFF.md` lists the exact commands and current
asset-pack versions. The resulting APK contains game assets — keep it for yourself.

## Contributing

Read [`AGENTS.md`](AGENTS.md) (rules) and [`HANDOFF.md`](HANDOFF.md) (how to resume work).
In short: reconstruct from the binary with provenance, fail loudly on gaps, never commit
game files or anything derived from them, and test sparingly on fast host builds.

## Credits

- **Hades** © Supergiant Games. This project is not affiliated with or endorsed by
  Supergiant Games. All game content belongs to its owners.
- **[REA (Reverse Engineer Anything)](https://github.com/morluto/rea)** by morluto (MIT) —
  the reverse-engineering toolkit used for binary/archive analysis and decompilation.
- [Lua 5.2.2](https://www.lua.org/) (MIT), [Luabins 0.3](https://github.com/agladysh/luabins)
  (MIT), [LZ4 1.9.4](https://github.com/lz4/lz4) (BSD-2-Clause) — fetched at build time,
  matching the versions embedded in the original engine. See [`NOTICE.md`](NOTICE.md).

## License

The code in this repository is released under the [MIT License](LICENSE). The license covers
only this project's own code and documentation — not Hades or any of its content.
