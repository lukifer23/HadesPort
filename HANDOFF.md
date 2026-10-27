# Handoff guide

Read this first, then `STATUS.md` (live state) and `AGENTS.md` (rules). Repository:
https://github.com/lukifer23/HadesPort (`main` only).
The detailed machine/session restart checkpoint, when present, is
`.local/private/RESTART_HANDOFF.md`; read it together with `.local/private/LOCAL.md`.
Both are ignored local files and must never be committed.

The [2026-10-26 review](AUDIT.md) maps current implementation, verified repairs,
remaining gaps and ordered acceptance milestones. The Fold is connected again;
iteration16 (source `f6339d9`) is installed and hash/size-verified. Iteration15 is
retained as a rollback APK. See STATUS's latest entry for exact source/pack/build/
device identities and host verification. Only source-reviewed, separately verified
areas of the preserved workstream branch have been integrated. Bisect or probe old
sources in a separate `git worktree`, never stash/checkout in the live tree.

## Current continuation (2026-10-26 evening)

Read the latest STATUS entry and AUDIT before historical checkpoints below.
Iteration16 is installed and re-verified; current source includes the
reviewed native continuation, touch NEXT and portrait-exit batch. Preserve iteration15
and app-file backups. Manual gameplay16 remains unverified.
The canonical stage and a selected host candidate can use different packs.

Already repaired: transient FX owner draw groups, autoplay navigation through the
engine's A*, ShiftFraction reward animation, radial blur state/update/pass and camera
frame state. Strict host continuation and final ASan/UBSan pass 4,748 frames through
original first dialogue advancement after Athena pickup. The next frame rejects
Athena's unsupported portrait creation graph; older 5,600-frame results waited at
dialogue. The exact held-input baseline remains five hits; the earlier separate
release/repress replay has seven. Visible vignette Shroud rendering remains
explicitly unsupported; frame state is not GPU proof.

Fresh device logs establish death-path gaps and `DeathArea/scene.bin` unavailable.
Next milestones are the complete Athena portrait FX/slide/emitter graph, original
boon interaction (geometry, text areas, cursor and floating highlights), and death/House recovery,
then the script-selected next map and real save/Continue. Host autoplay now
supplies ContinueText pulses only for actual queued original notifications. The
first continuation reaches an explicit Athena portrait graph gap; no boon selection
is proven. The former idle dialogue route remains entry evidence only.
Do not skip dialogue, inject damage, force rewards or invent a next room.

Build in an isolated host stage, run the exact mixed replay and strict progression
with every-frame presentation sampling, then one final sanitized check. Only main
publishes assets/builds/installs. Keep immutable source/pack/build/device identities
and explicit limitations. Reconcile fresh results in STATUS and the private handoff.

## Storage and process hygiene

Keep original game data, immutable versioned exports/manifests, pinned decoder
libraries, signing keys, real saves, evidence and the installed/rollback APKs.
Remove completed CMake scratch trees, generated probe binaries/objects/debug symbols,
Python bytecode caches and empty duplicate folders after retaining result receipts.
`.local` is the sole alias to `.local.nosync`; a duplicate alias is unnecessary.
Hardlinked pack/stage files share storage and are not independent wasted copies.

Do not delete versioned packs merely because their names look old: receipts and
rollback builds can reference them. Rebuildable CMake/Java/resource intermediates
may be removed while keeping canonical `assets/` and `debug.keystore`. Recreate
host builds from `native/host`, and use the normal build script for Android.
Inspect exact process IDs and stop project-owned jobs; never use broad kill patterns
that could affect unrelated workers. Only HadesPort is maintained by this task;
the parent workspace and sibling REA checkout are outside the cleanup scope.

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
recorded input file, so many stops are found without the device. Use bounded
unsanitized replays while iterating and reserve sanitizers for the final check.

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

If the checkout sits in a cloud-synced folder (e.g. macOS iCloud "Desktop & Documents"),
keep the tens of GB under `.local` out of sync: store it as `.local.nosync/` with a
`.local -> .local.nosync` symlink (iCloud skips `*.nosync`; all paths keep working;
`.gitignore` covers both). Otherwise the sync daemons rehash every export and build.

## Commands

```sh
# Stage assets + build APK
python3 scripts/build_native.py --scene .local/native-assets/opening-fx-v5/scene-affine.bin \
  --geometry .local/native-assets/opening-static-rgba-v3/room-geometry.json \
  --animations .local/native-assets/opening-fx-v5-animation/animations.bin
#   --assets-only  stage .local/native-build/assets for host runs;  --compile-only  Android .so only
# Device
adb -s "$HADES_DEVICE_SERIAL" install --no-incremental -r --user 0 .local/build/native/hades-native-engine-preview.apk
adb -s "$HADES_DEVICE_SERIAL" logcat -d -v threadtime -s HadesNative HadesLua HadesSurvey AndroidRuntime
# Save complete logs before filtering; retained faults can precede the visible symptom.
adb -s "$HADES_DEVICE_SERIAL" exec-out screencap -d "$HADES_DISPLAY_ID" -p > .local/reports/shot.png
# Fast host replay (own build dir), mixed held-attack baseline (five hits)
nice -n 15 cmake -S native/host -B .local/native-host-build-X -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHADES_SANITIZE=OFF
nice -n 15 cmake --build .local/native-host-build-X --target hades-debug -j 2
nice -n 15 .local/native-host-build-X/hades-debug .local/native-build/assets .local/reports/NEW.json \
  --activate-room --world-frames=2950 --input-replay=.local/reports/takeover-mixed-combat-hold.tsv
# Blocker survey (host-only diagnostic): records every failure instead of stopping
nice -n 15 .local/native-host-build-X/hades-debug .local/native-build/assets .local/reports/SURVEY.json \
  --activate-room --world-frames=2950 --survey-blockers --input-replay=.local/reports/takeover-mixed-combat-hold.tsv
# Autoplay (host-only input source): plays the real encounter/reward flow from the
#   original RequiredKillEnemies + live Interact owners via the touch input path.
#   --contain-faults = device durability mode, prints SURVEY_SUMMARY of reached gaps.
nice -n 15 .local/native-host-build-X/hades-debug .local/native-build/assets .local/reports/AUTO.json \
  --activate-room --world-frames=9000 --autoplay --contain-faults
# Save/Continue: --probe-save=NEW_DIR (checkpoint + fresh reload compare), then
#   --load-save=COPY_OF_DIR --autoplay to continue from it.
# Hero motion/camera trace: HADES_TRACE_HERO=1 hades-debug ... (host, every 10 frames);
#   on device the engine logs HERO_TRACE every ~0.5 s (logcat -s HadesNative)
# RE helpers
python3 native/analysis/inspect_engine.py 'sgg::Class::Method' --output .local/reports/NEW.json
python3 scripts/rea.py decompile .local/game/base/x64/EngineWin64s.dll 0x1800XXXXX --format json
```

Script action ids: `.local/reports/native-action-enum.json` is decoded from the engine's
action-name switch (regenerate it from the DLL if missing). Android SDK pieces:
`ndk;29.0.14206865`, `build-tools;37.0.0`, `platforms;android-36`.

## Historical checkpoint (reviewed 2026-10-20)

Read STATUS.md for the current installed APK and verification boundaries.
Previous repair workstreams are complete; agent processes do not survive a host
restart. Recreate assignments only when new work requires them. The user drives
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
actor-actions-v9, actor-markers-v9, actor-properties-v14 (HAP6),
player-weapons-v25, action-effects-v22, screen-opening-v12, original-text-v7, audio-events-v29.
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

## Immediate iteration (2026-10-20, code-only)

The user asked for code work only for now: no device installs or tests. Iteration12
remains installed and hash-verified. Work proceeds on the host with `--autoplay`,
which now clears the whole RoomSimple01 encounter; the reached gaps on the reward
path are listed in STATUS.md "Workstream checkpoint", with their owners. Process
rules: one build/export/replay per worker at a time, `nice -n 15`, `-j 2`, no copied
source trees, no lingering background processes, no broad `pkill -f` patterns.
All agents were stopped at 21:05; their unverified work is on branch
`wip/agents-2026-10-20` and described in STATUS.md "Workstream checkpoint". Agents edit; main verifies (build + strict
firstcombat2950 + autoplay), commits, pushes and keeps these docs current.

Root-cause rules still hold: never clear legitimate requests or skip a missing
native consumer to claim recovery, no weapon/room/god-name switches, no fallback
scene, and no invented next room — the original scripts choose it after the reward.

The final host runs sample presentation; they do not validate every rendered
frame. Read the private restart inventory before assuming any scratch directory
was removed.

Favor rapid fixes, builds and device play. Use short existing input reproductions
only to resolve concrete failures; no broad new test scaffolds. Save screenshots,
video and complete logs under .local/reports. Passive captures require the game
foreground. Root's scratch build dirs are temporary and must be deleted at final
cleanup; keep active agent scratch dirs until their assigned verification ends.

The sibling REA checkout has a pre-existing bridge modification; leave it alone.
Use scripts/rea.py, one decompile at a time, recover actual behavior from the
matching EngineWin64s DLL/PDB. The current shipped fmod DLL does NOT match the
adjacent old fmod PDB; use the documented descriptor/static evidence for Chorus.
