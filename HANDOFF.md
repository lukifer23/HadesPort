# Handoff guide

Read `AGENTS.md`, `STATUS.md` and `AUDIT.md`. The goal is a complete offline native
ARM64 Android Hades engine using the owner's original scripts/assets. It remains
an incomplete prototype. Work on `main`; keep original/derived game material and
session evidence under ignored `.local/`.

## Current continuation

Implementation resumed on 2026-11-07. Source `1028a6b` retains the completed original
boon input/tooltip/teardown path and reconstructs native nearby-danger queries,
callback owner IDs and signed missing-optional-target selection. Native MenuScreen
slots use authored text13 templates/pixels, parsed source keywords, actual timers
and retained fades.
ClearUseTarget has its recovered callback/primary/cache lifecycle. Config defaults
come from their shared recovered specification; Gaussian state and math.random
registration follow the matching DLL/PDB. Normal Android execution is strict.

Iteration21 is installed over USB from source
`1028a6b8296543239c54f9ce900c035216bc847c`: actual SHA-256
`ff502b78fcf20b2ab17980ee1b9b58f4d6cf82a286a78ad09ca1c9f11c16f870`,
2,613,850,469 bytes, unchanged signing identity and all four actual app files
unchanged before launch. Bounded physical play has completed and Hades is deliberately
stopped. Phone boon/reward acceptance remains open. Preserve APK21, APK20/19 and
rollback18/17, signing key, original exports and before/after app archives.
The installation receipt is
`.local/reports/device-2026-11-07/iteration21/installed.json`; STATUS records the
current device acceptance boundary.

Iteration21's real Continue and genuine controls defeated six enemies naturally.
Dash with live ranged enemies returned zero danger. A live projectile candidate
at that query was not established; positive projectile geometry/owner callback
remains host evidence and a separate hardware gate. At the final wave, original
`Scripts/RoomEvents.lua:620` stopped strict execution at
`SpawnUnit ForceToValidLocation relocation search not reconstructed: LightRanged`.
Player health was 25. After deliberate stop, all four actual app files remained
byte-identical before installation versus after play, and final thermal status was
0. No callbacks/progression were injected and fault containment was off. The receipt
is `.local/reports/device-2026-11-07/iteration21/acceptance.json`. Physical boon
completion remains blocked on native Unit relocation.

Iteration20's real Continue and genuine controls defeated six enemies naturally,
then a dash with a live ranged enemy hit the explicit GetNearbyDanger geometry gap
at 20 HP. Hades was deliberately stopped; all four actual app files remained
unchanged before installation versus after play, and final thermal status was 0.
That historical result is in `iteration20-acceptance.json` in the device report
directory. Its source repair has host coverage and the bounded iteration21 physical
bounded control observations above; positive projectile geometry/owner return is
host-only evidence.

Final same-source ASan/UBSan routes complete3600 for real danger/input, complete6220
for original boon selection/tooltip show/hide/fade and owner/movie teardown, and
complete2950 for the five exact historical held-combat hits. The danger route passes
actual projectile owner1000003 into original OnWeaponFired, clears the reached
optional-rubble selector failure and reaches a later natural ranged wave. All
completed frames are presentation-validated with zero contained faults; boon world
samples match the prior accepted route. Leak detection was disabled. Receipts are
under `.local/reports/boon-completion-2026-11-07/danger-input-review/`, indexed by
`final-verification.json`. Real save copies pass durable-publication,
recovery, kernel-write-failure and killed-writer checks. Source hashes were unchanged
through validation. Windows/platform libm last-bit parity and physical power-loss
acceptance are not claimed.

Next reconstruct the actual native blocked-Unit spawn relocation search reached by
ForceToValidLocation, then repeat bounded physical reward/boon acceptance. After
that, deliver actual script-selected room/death/House resource/runtime closure.
The extended host route stops at `A_Combat04/scene.bin`; DeathArea is also absent.
For map expansion, remove opening-specific room initialization assumptions from
real native/map facts. Full-game delivery needs an offline content store/importer behind AssetStore;
package reads/residency need bounded owned I/O and measured frame behavior before
broad expansion. Keep missing-room preflight strict; never substitute a room.

Recovered native resource/runtime contracts are
`native/analysis/menu-tooltip-runtime-contract.json`,
`native/analysis/clear-use-target-contract.json`,
`native/analysis/nearby-danger-contract.json`,
`native/analysis/object-id-selection-contract.json`, `save-contract.json` and
`recovered-runtime-contract.json`. The older screen-tooltip contract is an immutable
source binding of text13; current consumers/new runtime contract take precedence
for implementation status. Do not change a bound contract and silently bless an
old export. Re-export to a fresh version when its input identity changes.

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

Build defaults select packages2/text13/screen17/action27/audio32/actor9/weapons25/
properties14 and RoomSimple01 in addition to RoomOpening. Source and canonical host
staging agree. Installed APK21 uses source1028a6b and the selected current packs.
APK20/source03e5770 is preserved with the same packs; APK19/source5b54c0d retains
screen16/action26/audio31 as a protected rollback.

## Storage cleanup and restoration

The 2026-11-06 storage audit consolidated 1,327 exact binary/capture groups while
preserving all 3,938 paths. Shared export/capture payloads are read-only. Do not
edit them in place or make a shared inode writable; export a fresh version or use
an independent copy for changes. Metadata before linking is retained privately.

The installed iteration21 APK, APK20/19 and rollback18/17 remain preserved locally.
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
  --activate-room --world-frames=6220 --autoplay --audit-screen-input --validate-live-presentation
# Direct registered-action diagnostic; separate from world progression:
nice -n 15 .local/host-build-next/hades-debug .local/native-build/assets .local/reports/NEW-PACKAGES.json \
  --probe-packages
# Isolated genuine screen actions/resources; no progression or device claim:
nice -n 15 .local/host-build-next/hades-debug .local/native-build/assets .local/reports/NEW-SCREEN.json \
  --probe-screen-animations
# The verified input-audit route completes original close/teardown and next-room
# request through6,220. Frame6,221 attempts the absent A_Combat04 room.
# Slower real ContinueText input uses --autoplay-continue-period=600.
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
