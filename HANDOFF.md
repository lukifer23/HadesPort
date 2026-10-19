# Handoff guide

Read this first, then `STATUS.md` (live state) and `AGENTS.md` (rules). Repository:
https://github.com/lukifer23/HadesPort (`main` only).

## Scope and end goal

A playable, offline, native Hades on Android: the full run loop (rooms, combat, boons,
enemies, bosses), the House, saves compatible with the original format, PC-equivalent
settings and controls (touch plus Bluetooth/wired controllers), and a sustained frame rate
at a correct aspect ratio. "Done" means proven on a device. Until then it is a prototype.

## Durability vs strict mode

On the device the engine *contains* unreconstructed behaviour: the failing script call
returns nothing, a failing world stage is skipped for that frame, each distinct gap is
logged once (`CONTAINED_FAULT` / `SURVEY_BLOCKER`, tag `HadesSurvey`) and counted on the
FPS line ("N engine gaps"). Nothing is substituted — the behaviour simply doesn't happen.
Host replays are strict by default; `--survey-blockers` lists every gap in one run.
Still fix every gap: containment keeps the game playable, it isn't a reconstruction.

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
adb -s "$HADES_DEVICE_SERIAL" install --no-incremental -r --user 0 .local/build/native/hades-native-engine-preview.apk
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
# Hero motion/camera trace: HADES_TRACE_HERO=1 hades-debug ... (host, every 10 frames);
#   on device the engine logs HERO_TRACE every ~0.5 s (logcat -s HadesNative)
# RE helpers
python3 native/analysis/inspect_engine.py 'sgg::Class::Method' --output .local/reports/NEW.json
python3 scripts/rea.py decompile .local/game/base/x64/EngineWin64s.dll 0x1800XXXXX --format json
```

Script action ids: `.local/reports/native-action-enum.json` is decoded from the engine's
action-name switch (regenerate it from the DLL if missing). Android SDK pieces:
`ndk;29.0.14206865`, `build-tools;37.0.0`, `platforms;android-36`.

## Current checkpoint (2026-10-18)

The installed build is recorded in `STATUS.md`. User reports are reproducible:
movement can stop after actions, cast stops at ShowAimLine, and Use previously
faded to black because LoadMapInternal was absent. A successful first-combat
prefix did not cover these failures. Preserve actual input sequences and logs.

The fresh-save route is **RoomOpening exit door → RoomSimple01** with forced
EnemyIntroFight01, followed by its encounter/reward and Athena flow.

Implemented in the current source:

- Persistent animation play-speed multiplier and move/stop/idle recovery after
  finite animations; native move-animation override and CancelOnMove gates.
- Unit collision candidate filters, polygon push, and native collision callbacks;
  unit detonation LOS/arc/dodge/reflection/immunity gates and shared impact path.
- Source-bound interaction resources, original control aliases rendered with live
  touch labels, retained detached FX owner state, and OnOutOfAmmo dispatch.
- SetObstacleProperty/GetUnitDataValue and native default Move using A*.
- Deferred LoadMapInternal; normal transition retains Lua, scheduler, clocks and
  RNG, resets map managers/player binding, loads actual map assets and switches
  the GLES renderer. No opening scene fallback for other rooms.
- Animated Book lights needed by RoomSimple01 and polygon camera clamp.
- Homing movement with original allegiance defaults in player-weapons-v22.
  Normal cast now reaches actual impact/death/ammo-drop; pickup sparkle is the
  next native default/type/resource gap.

Current pack defaults: action-effects-v12, screen-opening-v12, audio-events-v21,
actor-actions-v6 with matching actor-markers-v6/actor-properties-v9 (HAP4),
player-weapons-v22, room-packs-v3/RoomSimple01.
The room pack stages 26 map-bound resources under `rooms/RoomSimple01/`.
Other rooms must get their own validated packs and lifecycle coverage.

## Immediate iteration

1. Finish AmmoPackSparkle ConstantNoUpdate default/components/resource coverage;
   cast now fires/moves/hits/expires and runs original ammo-drop. Verify pickup
   and movement/attacks after repeated casts.
2. Continue the actual second-room OnAnyLoad/StartRoom path, then first enemies.
   Distinguish map instantiation from a functioning room/encounter.
3. Reproduce unit/breakable collisions and special attacks on the device;
   newly reached collision weapons, effect clears, death and hit branches still
   have explicit boundaries. Do not reset faults to claim recovery.
4. Continue animation direction/recovery checks after every action. Native run
   atlas cardinal directions were verified; do not blindly flip coordinates.
5. Save/restart/reload and subsequent progression after combat works.

User steering: favor rapid fixes, builds and device play. Avoid new test scaffolds
or broad repeat runs. Use a short existing reproduction when it resolves a concrete
blocker. Main session alone builds/installs; avoid mixing manual and scripted input.
Local `.local/tools/e2e-resume.sh` checks game foreground and preserves logcat.
Screenshots/video and all derived packs remain ignored under `.local/`.

At takeover there were no active prior build/export/REA processes. The sibling REA
checkout has a pre-existing bridge modification; leave it untouched. Tool use is via
`scripts/rea.py`, one decompile at a time. Historical agent owners in old tracker
rows are history, not live assignments.
