# Handoff guide

Read `AGENTS.md`, `STATUS.md` and `AUDIT.md`. The goal is a complete offline native
ARM64 Android Hades engine using the owner's original scripts/assets. It remains
an incomplete prototype. Work on `main`; keep original/derived game material and
session evidence under ignored `.local/`.

## Current continuation

**Iteration18 is now installed and verified over wireless ADB.** USB debugging
transport was not enumerated. Fresh app-file backup and installed APK verification
passed; all four existing save files were unchanged before launch. Real Continue
loaded RoomOpening. Three visible-stick press/move/release/repress cycles passed on
the inner display at the observed 60 FPS; the control stayed in the lower screen.
Thermal status stayed 0; no new crash/ANR was found in scoped logs/exit history.
About 4.2 GiB memory was available; memory PSI avg10 was 0.11%, so this is a bounded
health observation, not sustained stability. Hades was deliberately stopped after
the check. Cover display, fold transitions, controllers and longer play remain open.
See STATUS for installed identity and private receipts. Preserve the current APK,
rollback17, saves and backups before any new gameplay APK publication.

Strict host progression passes package/config setup and the original boon-shell
movies, symbol graph and lighting. The current default 8,000-frame request completes
5,452 frames before SetInteractProperty requires actual screen geometry/lifecycle
for BoonSlot1. Native loop randomization now honors its default true and consumes
the genuine shared engine RNG; later path timing differs from the older 4,837-frame
route. The held 2,950-frame route still matches all five historical hits exactly.
The isolated --probe-screen-animations diagnostic exercises all 54 new movie frames,
chain/fallback/hold, ten symbol roots, repeated random lighting and registered colors.
It is separate from progression. Next: actual boon geometry/text areas/cursor/
highlights/Interact, then death/DeathArea/House and script-reached save/Continue.
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

Build defaults select packages2/text12/screen16/action26/audio31/actor9/weapons25/properties14
and RoomSimple01 in addition to root RoomOpening. `scripts/build_native.py` is the
selection authority; selected source, staged assets and an installed APK are
separate identities. Source/canonical assets use packages2/screen16; the preserved
iteration18 joystick candidate uses packages1/screen15. Installed iteration18 remains
a separate recorded identity. No new gameplay APK is published for this host milestone.
Preserve iteration18 before any future APK publication. See STATUS for receipts.

## Storage cleanup and restoration

The 2026-11-06 storage audit consolidated 1,327 exact binary/capture groups while
preserving all 3,938 paths. Shared export/capture payloads are read-only. Do not
edit them in place or make a shared inode writable; export a fresh version or use
an independent copy for changes. Metadata before linking is retained privately.

The installed iteration18 APK and rollback17 remain preserved locally.
Older iteration16, 17 retired standalone probes, and historical bulk logs/raw
renders are preserved in 108 SHA-verified lossless gzip archives. Original game/
installer files, all versioned export paths, saves/backups, signing key, pinned
decoder/toolchains and unique device media remain protected. Rust analysis caches
were retained because complete offline rebuild dependencies were not established.

List archived paths or restore one exact original (including mode/mtime):

```sh
python3 .local/maintenance/storage-audit-20261106/restore_archives.py
python3 .local/maintenance/storage-audit-20261106/restore_archives.py \
  --path build/native/baseline-iteration16-b1bdc3a0.apk
# Use --path reports/... for an archived historical trace/render.
# For a legacy executable, use e.g. --path renderer-perf-hulls.
# --all restores every archived original and retains the gzip backups.
```

The restore tool verifies archive and original hashes and refuses to overwrite a
differing existing file. Restore an archived historical sidecar before rerunning
an analysis that needs it. Audit inventory, links, prior metadata, archive index,
retention reasons and verification are under
`.local/maintenance/storage-audit-20261106/`. No device operations occurred.
Directory accounting fell from about 44.2 to 35.5 GiB; physical APFS free-space gains
are unverified and must not be inferred from that directory metric.

## Host commands

```sh
python3 scripts/prepare_native_dependencies.py
PYTHONDONTWRITEBYTECODE=1 python3 native/analysis/dependency_integrity_check.py
PYTHONDONTWRITEBYTECODE=1 python3 native/analysis/package_catalog_check.py \
  --manifest .local/native-assets/player-packages-v2/manifest.json \
  --legacy-manifest .local/native-assets/player-packages-v1/manifest.json
PYTHONDONTWRITEBYTECODE=1 python3 native/analysis/smoke_recovered_runtime.py
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
  --activate-room --world-frames=8000 --autoplay --validate-live-presentation
# Direct registered-action diagnostic; separate from world progression:
nice -n 15 .local/host-build-next/hades-debug .local/native-build/assets .local/reports/NEW-PACKAGES.json \
  --probe-packages
# Isolated genuine screen actions/resources; no progression or device claim:
nice -n 15 .local/host-build-next/hades-debug .local/native-build/assets .local/reports/NEW-SCREEN.json \
  --probe-screen-animations
# The current default route stops on BoonSlot1 interaction geometry at 5452.
# Slow ContinueText600 reaches the same blocker at 6,052; request 8,500 frames.
# HADES_TRACE_SLIDES=1 records actual random-slide entry.
```

`--survey-blockers` / `--contain-faults` are diagnostic, never strict acceptance.
The held route has five hits; release/repress inputs have different results.
CreateScreenObstacle makes native World obstacles: draw coordinates do not change
manager ownership. Keep per-animation time fractions and real owner links separate
from Body/Move timing. Source contracts record these distinct clock consumers.
Host autoplay supplies actual touch/control input and ContinueText pulses only for
live original notifications. It must not mutate game progression or call callbacks.
`--probe-save` is a direct registered-action diagnostic, not a script-reached save.
`--probe-packages` isolates real registered actions, worker reads and original Lua
dispatch without advancing the world. Both registered-action diagnostics are mutually
exclusive with progression flags; --probe-screen-animations consumes the genuine
shared RNG/audio streams in its own process. Neither establishes GPU/device parity.
The config smoke uses real native APIs and supplies no substitute importer/clock.
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
