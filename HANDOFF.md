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

Read STATUS.md for the current installed APK and active owners. The user drives
manual device play; main alone builds/installs and captures passively. Full
non-incremental user0 installs plus installed SHA verification are mandatory.
Do not overwrite an APK during installation. Build success is separate from play.
Asset staging now starts in a fresh temporary directory, validates the entire
selected closure, then publishes it. Do not copy arbitrary proof sidecars into
the published assets: weapon effects are embedded in weapons.bin; effects.lua
is the original world spawner catalog. Coordinate readers before publishing.

Source now covers actual RoomOpening exit -> RoomSimple01, original forced enemy
spawn, hit resources, clocks and cast projectile/ammo lifecycle. Current selected
packs: opening-actors-v13, room-packs-v6/RoomSimple01 (actor-simple-v4),
actor-actions-v8, actor-markers-v8, actor-properties-v13 (HAP6),
player-weapons-v24, action-effects-v20, screen-opening-v12, original-text-v7, audio-events-v26.
All packs are ignored and must be regenerated from original assets; never publish.

Critical prior direction-lock causes: hit consumers interrupted firing before
request cleanup, leaving held=[] but queued requests/Strafe. Metadata serialization
omitted proven RemoveEffects closure; defense queried EffectData.Reflection when
native GetReflection actually adds ReflectAddition. Both are repaired. Do not
replace the original input/trigger-lock policy with blanket request clearing.
Same-name victim effects now reapply across source weapons while preserving the
original EffectData/factory/source identity, matching native Reapply. This removed
the actual iteration9 repeated-hit loop; mixed sword/special/sword replay logs the
cross-source OnHitStun and recovers all four directions. No request clearing hack.
HERO_TRACE now reports held controls, requests, strafe, effects, timers, velocity,
body/goal/cosmetic facing and animation state. New AI diagnostics preserve actual
coroutine waits/stacks and dynamic enemy arsenals.

## Immediate iteration

Current first-enemy replay advances through actual hero damage/recovery, enemy
death/cleanup and money pickup. Weapon24 preserves all old records and derives
the inherited obstacle OnTouchdown weapon class. Actual next first-combat stop is
RubbleFall INSTANT6 initialization at2599; do not admit it as a base projectile.
The child-movie wave boundary is separate. The candidate has not been installed.


1. Preserve the installed iteration10a baseline: user confirms improved controls.
   The candidate must prove actual enemy hits, repeated attacks and cardinal
   recovery before replacing it. Three agents work in parallel; main alone deploys.
2. Actual AI activation, state notifies, colliding MoveAI path/repath, live-target
   FireWeaponFromUnit and native missing-marker fallback are reconstructed. Fresh
   replays now reach real enemy fire and damage. Native authored draw offsets
   must not be confused with the primary Animation attach offset for markers.
3. Actual repeated sword presses reduce original enemy Health160 to0. The former
   apparent immortal enemy was an aborted original Kill script: its money-drop
   primary was rejected before RequiredKillEnemies cleanup. Shared action primary
   completion now retains owner/state through native ChainTo/Graphic replacement.
   Kill cleanup and actual money pickup now pass strict. Next-wave activation
   reaches primary flags and a child-owned movie dependency; its real movie
   ownership/presentation bridge remains under reconstruction.
4. Hitstop scales world time once; component own fractions and actual owner links
   are separate. Hit resource/audio closure follows selected source definitions,
   including FxStyles maps. Packaging rejects audio closure stale against selected
   actor/action manifests. Missing sound coverage is checked before FX allocation;
   original failure detail survives later initialized-registry guards.
5. Styled text resolves original format widgets/Graft/PreviousFormat with mixed
   fonts/colors and native owner Z policy. Verify reached disabled-use prompt.
6. Finish all forced encounter waves and Athena reward/boon before choosing the
   next actual map. RoomSimple01 has no fixed linked third room. Later biome/map
   coverage, full encounter progression and device save/reload remain unproven.

Favor rapid fixes, builds and device play. Use short existing input reproductions
only to resolve concrete failures; no broad new test scaffolds. Save screenshots,
video and complete logs under .local/reports. Passive captures require the game
foreground. Root's scratch build dirs are temporary and must be deleted at final
cleanup; keep active agent scratch dirs until their assigned verification ends.

The sibling REA checkout has a pre-existing bridge modification; leave it alone.
Use scripts/rea.py, one decompile at a time, recover actual behavior from the
matching EngineWin64s DLL/PDB. The current shipped fmod DLL does NOT match the
adjacent old fmod PDB; use the documented descriptor/static evidence for Chorus.
