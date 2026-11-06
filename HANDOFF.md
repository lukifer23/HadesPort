# Handoff guide

Read `AGENTS.md`, `STATUS.md` and `AUDIT.md`. The goal is a complete offline native
ARM64 Android Hades engine using the owner's original scripts/assets. It remains
an incomplete prototype. Work on `main`; keep original/derived game material and
session evidence under ignored `.local/`.

## Current continuation

**Iteration18 joystick/thermal repair is built; USB disconnected before install.**
Iteration17 remains the last verified installed APK and has an owner-reported
joystick acquisition regression during RoomOpening play. Hades was stopped after
Android reported severe throttling; app state was backed up, then temperature
cooled to light throttling. Source `4381770` fixes visible-stick acquisition/rest,
resize timing and thermal-aware pacing. See STATUS for candidate identity, checks
and limits. Reconnect, install non-incrementally, verify actual APK and unchanged
app files before launch, then perform short repeated release/repress checks and
health observations. Never count host controls checks as device acceptance.
Both iteration17/16 rollback APKs and actual app-state backups are retained.

Strict host progression now passes Athena's portrait graph and completes 4,837
frames before the original boon-menu request hits unimplemented LoadPackages.
The slower ContinueText cadence reaches the same boundary at 5,437 after multiple
real blink loops. Next: original package admission/name mapping/completion callback,
then boon geometry/text areas/cursor/highlights/Interact, death/DeathArea/House and
real save/restart/Continue. See AUDIT's acceptance table.
Do not force progression, skip original dialogue, substitute scenes or drop fields.

The dependency preparer and room pack builder/revision/stager now verify complete
sources/closures before publication. Native and Python room consumers share
`native/app/room_asset_names.inc`. Keep that single declaration authoritative.

## Layout and selected packs

| Path | Purpose |
|---|---|
| `native/app/` | Shared C++ engine, GLES renderer, Android entry and Java bridge |
| `native/host/` | Real-script host replay and capture drivers |
| `native/tools/` | Original-resource exporters and validated stagers |
| `native/analysis/` | Recovered contracts, provenance helpers and focused real-input checks |
| `scripts/` | Dependency preparation, build/packaging and REA launcher |
| `.local/game/base/` | Preserved owned original game files |
| `.local/native-assets/` | Immutable versioned exports; build defaults select current packs |
| `.local/native-build/assets/` | Canonical host/Android asset stage |
| `.local/build/native/` | Current local APK |
| `.local/reports/`, `.local/private/` | Evidence, saves/backups, cleanup receipts and local notes |

`.local` is the sole alias to `.local.nosync` so cloud sync does not rehash the game
and exports. Always resolve both sides of protected-path checks. Do not infer
storage waste from hardlinked file names. Preserve originals, exports, real saves,
current APK, signing key, pinned decoder libraries and evidence. Remove only owned
scratch/build intermediates after recording results.

Build defaults select text12/screen15/action26/audio31/actor9/weapons25/properties14
and RoomSimple01 in addition to root RoomOpening. `scripts/build_native.py` is the
selection authority; selected source, staged assets and an installed APK are
separate identities. Iteration17 and the iteration18 candidate bind the current
selected packs; see STATUS for immutable build/install receipts. No binary should be assumed fresh after restart.

## Host commands

```sh
python3 scripts/prepare_native_dependencies.py
PYTHONDONTWRITEBYTECODE=1 python3 native/analysis/dependency_integrity_check.py
PYTHONDONTWRITEBYTECODE=1 python3 native/analysis/room_pack_check.py \
  --manifest .local/native-assets/room-packs-v6/RoomSimple01/manifest.json \
  --actors .local/native-assets/actor-simple-v4/manifest.json \
  --source .local/native-assets/rooms-v1/RoomSimple01

nice -n 15 cmake -S native/host -B .local/host-build-next -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHADES_SANITIZE=OFF
nice -n 15 cmake --build .local/host-build-next --target hades-debug -j 2
# Create a fresh report directory and use NEW output names.
nice -n 15 .local/host-build-next/hades-debug .local/native-build/assets .local/reports/NEW.json \
  --activate-room --world-frames=2950 --validate-live-presentation \
  --input-replay=.local/reports/takeover-mixed-combat-hold.tsv
nice -n 15 .local/host-build-next/hades-debug .local/native-build/assets .local/reports/NEW-AUTO.json \
  --activate-room --world-frames=4900 --autoplay --validate-live-presentation
# The current default longer route fails on LoadPackages at 4837.
# To exercise genuine blink looping, use --autoplay-continue-period=600,
# --world-frames=5550 and HADES_TRACE_SLIDES=1 (expected boundary5437).
```

`--survey-blockers` / `--contain-faults` are diagnostic, never strict acceptance.
The held route has five hits; release/repress inputs have different results.
CreateScreenObstacle makes native World obstacles: draw coordinates do not change
manager ownership. Keep per-animation time fractions and real owner links separate
from Body/Move timing. Source contracts record these distinct clock consumers.
Host autoplay supplies actual touch/control input and ContinueText pulses only for
live original notifications. It must not mutate game progression or call callbacks.
`--probe-save` is a direct registered-action diagnostic, not a script-reached save.
Use fresh temporary build directories and remove them at completion. Run one
final ASan/UBSan check after runtime changes; leak detection is disabled on the
recorded macOS checks. No broad test scaffolding or copied source trees.

## Building and analysis

```sh
python3 scripts/build_native.py --scene .local/native-assets/opening-fx-v5/scene-affine.bin \
  --geometry .local/native-assets/opening-static-rgba-v3/room-geometry.json \
  --animations .local/native-assets/opening-fx-v5-animation/animations.bin
# --assets-only stages fresh validated assets; --compile-only builds Android .so.
python3 native/analysis/inspect_engine.py 'sgg::Class::Method' --output .local/reports/NEW-RE.json
python3 scripts/rea.py decompile .local/game/base/x64/EngineWin64s.dll 0x1800XXXXX --format json
```

Use matching EngineWin64s DLL/PDB provenance, one decompile at a time; close native
sessions you open. The sibling REA checkout has an existing bridge edit: leave it
untouched. The old FMOD PDB does not match its shipped DLL; use the established
static/descriptor evidence. No external source is needed to establish local state.

For each device session, rediscover the designated device and always use
`adb -s "$HADES_DEVICE_SERIAL"`; no historical serial/display/PID is current.
The owner authorized bounded real Android input while Hades is focused. Verify
foreground/keyguard and active physical display before any input/capture. No
network-setting changes, unrelated app capture, save reset or direct progression/
callback injection. Preserve APK/app files, install non-incrementally for user0,
verify actual installed hash/size, then record separate launch and play evidence.
Check actual crashes/exits, memory PSI and thermal status. Stop Hades and allow
cooldown on severe throttling; do not force system thermal status for acceptance.

Local details are in `.local/private/RESTART_HANDOFF.md`; historical public detail
is in git at `faee143`. STATUS indexes current receipts and proof limitations.
