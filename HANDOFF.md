# Handoff guide

Read this first, then `STATUS.md` (live state) and `AGENTS.md` (rules). Repository:
https://github.com/lukifer23/HadesPort (`main` only).

## Scope and end goal

A playable, offline, native Hades on Android: the full run loop (rooms, combat, boons,
enemies, bosses), the House, saves compatible with the original format, PC-equivalent
settings and controls (touch plus Bluetooth/wired controllers), and a sustained frame rate
at a correct aspect ratio. "Done" means proven on a device. Until then it is a prototype.

## How progress is made

The original scripts drive the game. The engine fails loudly at the first unreconstructed
engine behaviour (`SCRIPT_STAGE_BLOCKED stage=… detail=<missing piece>`), and the device
shows that reason on its "unable to enter" screen. Each fix reconstructs that behaviour
from the binary and moves the stop later. The Mac host replays the same scripts with a
recorded input file, so most stops are found without the device. Unsanitized host replays
of 2400 frames take ~7 s; sanitized ones take minutes, so keep those for the final check.

## Layout

| Path | What |
|---|---|
| `native/app/` | Engine (C++/GLES; `HadesActivity.java` bridges vibration, controllers, game mode) |
| `native/host/` | Mac host debugger (`hades-debug`), capture (`hades-render`) |
| `native/tools/` | Asset exporters and stagers (fresh versioned output dirs) |
| `native/analysis/` | Recovered contracts, `engine-identity.json`, RE helpers (`inspect_engine.py`) |
| `.local/game/base/` | Your original game files (ignored) |
| `.local/native-assets/<name>-vN/` | Exported packs; `scripts/build_native.py` defaults pick current versions |
| `.local/reports/` | Evidence, replay logs, agent progress logs (ignored) |
| `.local/private/` | Local-only notes (`LOCAL.md`), retired/intake material (ignored) |

## Commands

```sh
# Stage assets + build APK
python3 scripts/build_native.py --scene .local/native-assets/opening-fx-v5/scene-affine.bin \
  --geometry .local/native-assets/opening-static-rgba-v3/room-geometry.json \
  --animations .local/native-assets/opening-fx-v5-animation/animations.bin
#   --assets-only  stage .local/native-build/assets for host runs;  --compile-only  Android .so only
# Device
adb -s "$HADES_DEVICE_SERIAL" install -r .local/build/native/hades-native-engine-preview.apk
adb -s "$HADES_DEVICE_SERIAL" logcat -d -s HadesNative HadesLua | grep -E "FAILED|BLOCKED|PERF"
adb -s "$HADES_DEVICE_SERIAL" exec-out screencap -d "$HADES_DISPLAY_ID" -p > .local/reports/shot.png
# Fast host replay (own build dir), first-combat input file
cmake -S native/host -B .local/native-host-build-X -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHADES_SANITIZE=OFF
cmake --build .local/native-host-build-X --target hades-debug
.local/native-host-build-X/hades-debug .local/native-build/assets .local/reports/NEW.json \
  --activate-room --world-frames=2400 --input-replay=.local/reports/native-first-combat-replay-v2.tsv
# Blocker survey (host-only diagnostic): records every failure instead of stopping
.local/native-host-build-X/hades-debug .local/native-build/assets .local/reports/SURVEY.json \
  --activate-room --world-frames=2400 --survey-blockers --input-replay=.local/reports/native-first-combat-replay-v2.tsv
# RE helpers
python3 native/analysis/inspect_engine.py 'sgg::Class::Method' --output .local/reports/NEW.json
python3 scripts/rea.py decompile .local/game/base/x64/EngineWin64s.dll 0x1800XXXXX --format json
```

Script action ids: `.local/reports/native-action-enum.json` is decoded from the engine's
action-name switch (regenerate it from the DLL if missing). Android SDK pieces:
`ndk;29.0.14206865`, `build-tools;37.0.0`, `platforms;android-36`.

## Where we are

See `STATUS.md` for the device run table, workstreams and known issues. Summary
(2026-10-18): the title, settings, intro, landing, HUD, control handoff, sword attacks,
ghost patrol, dash firing and most opening-room audio run. Current stops: a projectile
marker lookup during the first sword hit, and the dash wall-slide (`Unit::ApplyShift`).

## Next steps, in order

1. Keep moving the stop: install, read the reason, reconstruct, rebuild.
2. Dash wall-slide, then Athena's first boon (loot → USE → dialogue → boon screen → doors).
3. Room transition and multi-room assets (original BC7 via `GL_EXT_texture_compression_bptc`),
   which also shrinks the APK.
4. Enemies, then save/restart/reload on device.
5. Remaining settings once their features exist (Hero Occlusion, Gameplay Timer,
   Off-screen Indicators, 720p assets).
