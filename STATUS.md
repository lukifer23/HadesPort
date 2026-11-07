# Hades native port — current state

Updated 2026-11-06. Read `HANDOFF.md` for commands and `AUDIT.md` for the implementation
plan. This is an incomplete native ARM64 Android engine driven by the original game.

## Installed joystick/reward hotfix — iteration19 (2026-11-06)

Clean source `5b54c0d`; actual installed SHA-256
`2cf5659db7316a31bdf19cb9f6ef7ffb97595e458e0ce8ca1c042e188230796f`,
2,610,176,113 bytes. Installation used non-incremental user0 over wireless ADB;
USB transport was not enumerated. All four app files matched the fresh backup
before launch. Real Continue loaded the original saved run.

The owner correctly rejected iteration18's high layout despite its bounded input
check. The stick still used a 45 mm bottom offset: about y=1224 on the inner panel,
while ATTACK was y=1654. Iteration19 uses the same 19 mm bottom anchor as ATTACK,
retains side/user offsets and visible-ring acquisition, and restores rest immediately
on release/cancel. Actual captures verify the lower position. Four alternating
left/right cycles each produced real movement; released traces show zero input and
velocity. Cover/fold and longer play remain open.

The reported “Unable to enter” screen was a live engine failure, not a process
exit: AthenaUpgrade/weapon_2_TRACKING hit an unconditional non-HAA Book marker
rejection. Native no-dataset lookup follows the actual child chain and falls back
to animation location. The repair validates real owner/registry lifetimes and raw
attachment offsets, and rejects unrepresented nonempty datasets. No marker samples
or assets were invented. The actual original five-node reward chain passes across
multiple world and animation frames. ASan/UBSan controls, strict5500 marker replay,
held2950 (five exact historical hits) and component/input probes pass; leak detection
disabled. The repaired files/function match the clean APK source.

Phone reward verification remains open. During follow-up the character died, then
original death requested `DeathArea/scene.bin`, which is absent. This is an existing
content/transition gap, not closed by the marker repair. No new native process crash
or ANR was found. Thermal status stayed 0; post-control MemAvailable was 4,334,316 kB
(about 4.1 GiB), memory PSI avg10 some/full 0.00%. These are bounded observations.
Hades was deliberately stopped afterward. The original primary saves remain intact;
normal gameplay changed only Profile1.v.sav.bak. Before/after archives are retained.

APK19 includes committed packages2/text12/screen16/action26/audio31/actor9/weapons25/
properties14 and only RoomOpening/RoomSimple01. Unfinished boon-input work is excluded.
Current APK and rollback18/17 are preserved, with unchanged signing identity.
Receipts: `.local/reports/device-2026-11-06/joystick-bottom/`; immutable build receipt
under `.local/reports/native-builds/` at the APK hash above.

## Offline boon-shell milestone (2026-11-06)

This historical host milestone preceded device reconnection. Original boon-menu movies, symbol transforms,
lighting and title creation now execute. Strict default autoplay requested 8,000
frames and completed 5,452 with every-frame presentation and no contained faults:

```
Original update: Scripts/Main.lua:419: SetInteractProperty screen Interact creation requires actual screen geometry/lifecycle providers: BoonSlot1
```

Screen16 preserves all 1,967 prior frame records/payloads and 808 serialized clips
byte for byte, plus prior source metadata/provenance. Source-derived coverage adds
37 clips, seven actual atlas images and 54 original Bink frames across three movies.
All added pixels match independent original-source decode/crops. Fourteen damaged
source/manifest/bank/backend checks reject; publication refuses existing outputs
and a mismatched decoder. Originals and prior exports remain immutable.

The real registered-action diagnostic decodes all 54 movie frames, exercises
natural ChainTo/Graphic fallback/HoldLastFrame, ten symbol roots (40 actual screen
layers and 20 created action FX), three lighting loops and eight color cases. It
uses genuine shared RNG, audio PCM and providers; it does not advance progression
or establish GPU/device equivalence. Native scale/angle helpers are shared by screen
and action consumers. Book completion retains native cancellation and immutable
ThingData.Graphic fallback. Color parsing uses RGB-selected normalized/byte rules,
including fractional truncation, byte wrapping, indexed defaults and protected Lua
channel lookup.

Loop randomization now honors the native default true and native draw predicates/
order. This changes later engine RNG consumers, including MoveAI repath timing.
The held 2,950-frame replay still matches all five historical combat hits exactly;
old/new default world samples match through tick 3,180. The exact first changed RNG
draw was not traced. The former 4,837-frame route is historical, not current.

ASan/UBSan passes the screen diagnostic and slow ContinueText600 replay through
6,052 frames to the same geometry blocker, with no findings (leak detection
disabled). Android ARM64 compilation passes. Material/
Ambient/SortMode presentation hints, immediate portable ECS writes, exact native
command-buffer flush and cross-consumer child-update phases retain their limits.
Actual boon selection and device control/thermal acceptance remain open. No new
APK was published; candidate18 and rollback17 are preserved. Owned host, sanitized
and Android build directories and Python bytecode were removed after verification;
about 960 MiB of allocated scratch blocks, not a measured volume free-space gain.

Receipts: `.local/reports/boon-shell-2026-11-06/`; source/pixel preservation and
publication checks: `.local/reports/package-loader-2026-11-06/screen16-*.json`.

## Storage audit (2026-11-06)

Directory-accounted `.local` usage decreased from 44.2 to 35.5 GiB (about 8.7 GiB).
This is a directory metric: the shared APFS volume's free-space measurements
fluctuated, so physical free-space recovery is unverified.

- 1,327 exact duplicate binary/capture groups consolidated; 3,938 paths and bytes
  preserved, with shared payloads read-only. No export version was deleted.
- 108 older artifacts archived losslessly: iteration16 APK, 17 standalone probes,
  historical bulk logs and raw renders. Each gzip decoded to its original SHA256
  before the original path was removed. Actual render/executable restoration passed.
- Candidate18 and rollback17 hashes remain unchanged and ready to install.
  Iteration16 is archived, not present at its old APK path; see HANDOFF to restore.
- All 90 recorded build-manifest checks pass, along with 23 package and 49 room
  checks. Originals, installer sources, saves/backups, key, pinned tools and unique
  device videos/captures remain protected. Rebuildable Python bytecode was removed.
- Rust target caches remain: the offline dependency check required an unavailable
  crate. REA's source, existing bridge edit and dependencies were left unchanged.

Receipts, lossless archives and restore tool:
`.local/maintenance/storage-audit-20261106/`. No engine behavior or device state
was changed. No Codex transcript files were found in the workspace.

## Offline package-loading milestone (2026-11-06)

The device is unavailable. Source `f20c74c` now passes the original boon-menu
LoadPackages request and its reached config setters. Strict host replay completes
4,837 frames with zero contained faults and every-frame presentation before:

```
Original update: Scripts/Main.lua:419: Scripts/UIScripts.lua:2354: Original live screen animation unsupported: BoonSelectIn
```

Native admission resolves exact-case unit mappings, checks all selected manifests
before requests, retains distinct duplicate request identities and services actual
CPU atlas reads. Deferred OnPackageLoaded matches only explicit source names and
runs the original dispatch coroutine with an empty table. Callback-created requests
wait for later service. Names parsing follows native precedence, scalar/numeric,
first-nil sequence and parent-prefix rules. Nonstringable sequence entries that
would hang the original, and unsupported trigger filters, are explicitly rejected.

Packages2 preserves all prior package/texture/sprite metadata and 103,741,952 RGBA
bytes, adding nine independently decoded original Athena atlases. Three packages
contain 21 references backed by 19 unique textures (185,573,248 resident bytes),
retaining the existing 27,459,584-byte shared-atlas saving. Unit mappings derive
from 240 original records, three sources and 46 package filenames. Nineteen exporter
checks and 23 stage checks pass. Staging rederives mapping provenance from current
original inputs and rejects stale filename inventories, damaged payloads and proofs.
The stager does not parse Lua texture references; the native consumer exercises
all actual staged references. Original selected packages have no authored positive
include edge; source-copy include tests do not establish that missing coverage.

Real Lua5.2 registered-action checks pass 14 callbacks, exact/empty/wrong-case filters,
empty payload/global args, duplicate deferral, callback-created work, name forms
and complete-batch missing-package preflight. The real worker checks missing and
truncated files without false completion. Proven FreeFormSelectWrapY storage and
nil/missing-to-empty string assignments pass native API tests; actual GUI cursor
wrapping remains unimplemented. The old config driver no longer supplies fake
Import/GetTime functions and removes its own build scratch.

The 2,950-frame combat replay retains the exact historical five-hit sequence with
zero faults and every-frame presentation. Final ASan/UBSan passes the slower
5,437-frame route with zero findings (leak detection disabled); the package worker
and registered-action diagnostics also pass under sanitizers. Android ARM64
compilation passes; all 318 recorded code identities match across verification.
Removed 900.2 MiB of owned build/probe scratch and redundant preliminary snapshots;
584 tracked files have no exact duplicates or files over 1 MiB. No new APK or device
operations are part of this milestone; the signed joystick-only candidate remains
preserved for hardware acceptance. Boon selection is still unproven.

## Historical iteration18 repair and bounded check (2026-11-06)

**Superseded by iteration19 above.** Iteration18 was installed over wireless ADB.
USB ADB transport was not enumerated. The owner-reported acquisition/release defect
passed three real press/move/release/repress cycles on the inner display. Captures
show movement and the joystick remaining in its lower-screen position. The owner subsequently rejected its high placement; cover display, fold transitions,
controller behavior and sustained play remain unverified.

Before launch, all four actual save files were backed up and verified byte-identical
after installation. The actual installed base APK matches the SHA/size below. Real
Continue loaded RoomOpening, with observed 60 FPS and platform thermal status 0
through the short control check. Available memory was 4,405,772 kB (about 4.2 GiB);
memory PSI some/full avg10 was 0.11%, avg60 0.25%. Scoped PID logs and exit history
showed no new crash or ANR. This does not establish sustained health or thermal-cap
behavior under throttling. Hades was deliberately stopped after the bounded check.
Receipts/captures remain private under
`.local/reports/device-2026-11-06/boon-interaction/`.

Root cause: at 2520×1080 / density 420, the old 45 mm bottom offset placed the stick
at y=336 while floating acquisition required y>486. Bottom touches acquired it;
release returned the visible control to the unreachable rest. The repair bounds
the default rest within the lower movement area and makes the visible stick an
acquisition target even with an edited position. Original raw Android coordinates,
pointer-ID ownership, release, dead zone and combat controls remain authoritative.
Surface configuration now releases stale movement before simulation after resize.

The production controls check reproduces the original failure and passes with the
repair: 96 repeated press–move–release cycles across cover/inner geometry, handedness,
fixed/floating modes and three scales, plus edited rests, cancel/repress, unrelated
releases and existing pinch/action/dialogue/lifecycle checks. Final controls
ASan/UBSan passes without findings (leak detection disabled). Android compilation,
APK signing and the no-network-permission check pass. Source is `4381770`, built
from a clean detached worktree excluding unfinished package work. All 30 selected
resource manifests match iteration17. Candidate SHA-256 / size:
`4265c2a52f6014c114bfb35babc62005a2161a913ab915d5ce3549e8b1a53f0c`,
2,579,975,054 bytes.

### Device health

During the earlier pre-repair inspection, the crash buffer was empty and app exit
history contained no Hades crash or ANR from that session. An older background
LOW_MEMORY exit on October 27 predates this session.
At inspection, about 4.2 GiB memory was available, app PSS was about 1.65 GiB and
memory PSI averages were low. **Thermal throttling was present:** Android status 3
(severe), skin 45.8°C, battery 35.9°C. Hades was stopped, actual app state backed up,
and the device cooled to status 1 (light), skin 39.4°C. Boot reason was user-requested;
no reboot occurred during this inspection. These snapshots do not establish long-run
stability or attribute all heat to this app.

The candidate uses the real API29+ [PowerManager thermal status](https://developer.android.com/reference/android/os/PowerManager#getCurrentThermalStatus()), polled every two
active seconds and refreshed on resume. Rendering caps at 60/30/15 FPS for moderate/
severe/critical-or-higher states, preserving lower user limits and the original
fixed 60 Hz simulation. Menus cap at 30; fresh settings default to 60. Existing saved
settings are preserved; saved display-rate mode remains available while cool.
No thermal override, phone-wide setting changes, save reset or simulated device
health was used. Actual platform throttling response still needs device observation.

### Delivery history and next gate

Iteration17 was installed non-incrementally for user0, with actual APK identity
verified as `b913b9681a4924203eeb5fb3f81d7aac1e24de8168e97b1d00c6807714be21fa`
(2,579,970,958 bytes; build `f635114`, Athena runtime `312f521`). All backed-up app
file contents were identical after installation before launch. Title/first frame
and later actual RoomOpening rendering were observed. The joystick report means
controls acceptance failed; combat/Athena, physical controllers, lifecycle and
sustained play remain unverified. Real foreground Android Continue input was used
under the owner's full authorization; no progression/callback state was injected.

Current APK19: `.local/build/native/hades-native-engine-preview.apk`.
Preserved APK18: `.local/build/native/baseline-iteration18-4265c2a5.apk`.
Iteration17 rollback is ready; iteration16 is retained as a lossless archive
(restore instructions in HANDOFF). The signing key, canonical assets and actual
app-state backups are retained privately. The fresh install, identity, save-file
preservation and short inner-display control checks have passed. Preserve the current APK
before publishing another APK; keep future play bounded and stop on severe thermal
status. The earlier host/build cleanup removed only rebuildable intermediates;
the candidate, rollbacks, app backups, canonical assets and signing key remain.

## Earlier Athena portrait milestone (2026-11-06)

The earlier `312f521` host milestone passed the former Athena failure at frame 4,748
and completed 4,837 frames through original dialogue advancement to the boon-menu
package request. Its then-current default route stopped explicitly on:

```
Original update: Scripts/Main.lua:419: Scripts/Interactables.lua:826: Native engine action not reconstructed yet: LoadPackages
```

There are zero contained faults before that boundary and presentation is sampled
every frame. A slower real ContinueText input cadence exercises AthenaBlink through
multiple loops and resamples its authored random duration before reaching the same
LoadPackages boundary at frame 5,437. These are different input routes; neither
proves a boon choice or later-room progression.

The final held-combat 2,950-frame route retains the exact historical five-hit
sequence with every-frame presentation. Final ASan/UBSan passes 5,437 frames on the
slower input route with zero findings (leak detection disabled), including three
entries into the blink loop's first frame. Android ARM64 compilation passes; the
library was verified as AArch64 ELF. Tested runtime source is `312f521`. That host
milestone performed no device operations; iteration17 delivery and the iteration18
candidate are recorded above.
All owned host/ASan/Android scratch builds were removed.

Implemented behavior:

- Screen-layer initialization follows Attach, Child, singleton Create, ordered
  Creates and Sound, with child completion before parent initialization returns.
  Delegated children retain actual registry parent ownership; created roots remain
  independent manager allocations. Entire reached graphs preflight before mutation.
- AthenaBlink uses original per-slide images, durations, Loop and shared engine
  bounded PCG. Equal random bounds consume no draw; later loops resample real
  per-instance durations while preserving elapsed remainder.
- Original CreateScreenObstacle is a World SpawnObstacle followed by a coordinate
  flag. Portrait primaries/FX remain in World manager0. Body helpers, movement and
  animation time now use their distinct recovered clock equations; screen drawing
  does not choose manager ownership. Pending disposal precedes FX emission.
- Action FX read actual screen body, fractions, HSV, group, lifetime and live sound/
  elapsed fields. VFX offsets, nested creation, audio admission/fades and pre-dispose
  consumers execute. Nonspatial screen audio remains a documented Android adaptation.
- Audio export accepts proven same-file bank aliases without overriding retained
  sample evidence. Warm-cache verification includes ctime. Seventeen real-input
  checks pass, including same-size tampering with restored mtime.

Resource preservation was independently checked: all 1,942 prior screen frames and
787 clips remain byte-identical; 25 frames/21 clips are added. Action26 preserves
1,913 prior definitions, nine movie definitions, 13,615 frame payloads and 1,861
clip schedules/image bindings after packet-index normalization. Audio31 preserves
all 2,190 prior event semantics, 3,635 exact sample rows, four closure proofs and
24 stream identities, adding nine events/seven samples. Historical locator spelling
and one uniquely named parameter-array order can differ; original payloads and
mixing values remain unchanged. Three unrelated playlist cues remain unsupported.

The preceding review repaired dependency/source integrity and complete room-pack
publication (73 real-input checks), removed about 373.7 MiB of generated probes and
consolidated historical documentation. Its receipts remain indexed below.
No mocks, replacement game art, skipped dialogue or forced rewards were introduced.

## Selected assets and evidence

Build defaults in `scripts/build_native.py` remain authoritative. Canonical assets
use packages2/text12/screen16/action26/audio31/actor9/weapons25/properties14; only
RoomOpening and RoomSimple01 are staged. Source/canonical packages2/screen16 are ahead
of historical iteration18 (packages1/screen15). APK19 uses the committed default
selection. The active local menu candidate has screen17/action27/audio32; its
implementation and host staging remain separate from APK19. Device gameplay acceptance is separate.

| Evidence | Local path |
|---|---|
| Boon shell final verification | `.local/reports/boon-shell-2026-11-06/final-verification.json` |
| Boon shell tested source | `.local/reports/boon-shell-2026-11-06/tested-source.json` |
| Screen16 preservation | `.local/reports/package-loader-2026-11-06/screen16-preservation.json` |
| Package final verification | `.local/reports/package-loader-2026-11-06/final-verification.json` |
| Package sanitizer checks | `.local/reports/package-loader-2026-11-06/sanitizer-verification.json` |
| Android ARM64 identity | `.local/reports/package-loader-2026-11-06/android-native.json` |
| Current storage audit | `.local/maintenance/storage-audit-20261106/summary.json` |
| Package-build scratch cleanup | `.local/reports/package-loader-2026-11-06/cleanup.json` |
| Package source identity | `.local/reports/package-loader-2026-11-06/tested-source.json` |
| Package export preservation | `.local/reports/package-loader-2026-11-06/export-v2-verification.json` |
| Package stage integrity | `.local/reports/package-loader-2026-11-06/package-catalog-stage-checks.json` |
| Iteration18 immutable build | `.local/reports/native-builds/4265c2a52f6014c114bfb35babc62005a2161a913ab915d5ce3549e8b1a53f0c.json` |
| Joystick host verification | `.local/reports/joystick-2026-11-06/host-verification.json` |
| Device health and fresh app backup | `.local/reports/device-2026-11-06/joystick-health/` |
| Iteration19 hotfix verification | `.local/reports/device-2026-11-06/joystick-bottom/` |
| Iteration18 installation/control/health | `.local/reports/device-2026-11-06/boon-interaction/` |
| Iteration17 installation/launch | `.local/reports/device-2026-11-06/iteration17-installed.json` |
| Immutable iteration17 build | `.local/reports/native-builds/b913b9681a4924203eeb5fb3f81d7aac1e24de8168e97b1d00c6807714be21fa.json` |
| Athena milestone | `.local/reports/athena-graph-2026-11-06/final-verification.json` |
| Screen/action resource preservation | `.local/reports/screen-slide-2026-11-06/` |
| Audio preservation and alias checks | `.local/reports/screen-action-child-2026-11-06/` |
| Prior review verification | `.local/reports/review-2026-11-06/final-verification.json` |
| Current review/build/replays | `.local/reports/review-2026-11-06/` |
| Dependency integrity | `.local/reports/dependency-integrity-2026-11-06.json` |
| Generated-file cleanup | `.local/private/cleanup/generated-probes-20261106.json` |
| Prior rollback retirement | `.local/maintenance/storage-cleanup-pass2-20261106-123724.json` |
| Iteration16 installation/source/pack receipts | `.local/reports/dialogue-continue-2026-10-26/` and `.local/reports/native-builds/` |
| Athena graph ordering evidence | `.local/reports/screen-created-fx-2026-10-26/` |
| Boon geometry/cursor plan | `.local/reports/gui-interact/screen-boon-next-boundary-plan-2026-10-26.json` |

## Next work

Continue on the host with actual BoonSlot1 screen geometry and lifecycle providers,
then individual text areas, cursor/highlights and original Interact
callbacks. Death/DeathArea/House, save/restart/Continue and Android acceptance
follow. Iteration19 lower placement and alternating controls passed on the phone; cover/fold
and sustained play acceptance remain open. `AUDIT.md` records the
concrete acceptance boundaries.
