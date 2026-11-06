# Hades native port — current state

Updated 2026-11-06. Read `HANDOFF.md` for commands and `AUDIT.md` for the implementation
plan. This is an incomplete native ARM64 Android engine driven by the original game.

## Joystick and thermal repair — iteration18 candidate (2026-11-06)

**The device is currently unavailable. Iteration18 is built and signed but
uninstalled; iteration17 remains the last verified installed build.** The owner
reported the visible joystick jumping upward and becoming unresponsive after
release during actual RoomOpening play. This is an open device regression until
installation and repeated physical-input checks pass.

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

The current crash buffer was empty and app exit history contained no Hades crash or
ANR today. An older background LOW_MEMORY exit on October 27 predates this session.
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

Current candidate: `.local/build/native/hades-native-engine-preview.apk`. Both
iteration17 and iteration16 rollback APKs, the signing key, canonical assets and
actual app-state backups are retained privately. Reconnect the designated device,
verify previous identity, install iteration18 non-incrementally, verify installed
hash/size and unchanged app files before launch, then exercise repeated visible-stick
press/move/release/repress and check faults, memory and thermals. Keep tests bounded;
stop Hades if severe throttling recurs. USB disappeared before installation began;
no iteration18 device success is claimed. Build/probe intermediates and the clean
worktree were removed after recording results (5.08 GiB allocated); the candidate,
rollbacks, app backups, canonical assets and signing key remain preserved.

## Athena portrait milestone (2026-11-06)

Strict host progression now passes the former Athena failure at frame 4,748 and
completes 4,837 frames through original dialogue advancement to the boon-menu
package request. The longer default route stops explicitly on:

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
use text12/screen15/action26/audio31/actor9/weapons25/properties14; only RoomOpening
and RoomSimple01 are staged. Source, canonical assets, iteration17 and the iteration18
candidate select the same resource versions. Device gameplay acceptance is separate.

| Evidence | Local path |
|---|---|
| Iteration18 immutable build | `.local/reports/native-builds/4265c2a52f6014c114bfb35babc62005a2161a913ab915d5ce3549e8b1a53f0c.json` |
| Joystick host verification | `.local/reports/joystick-2026-11-06/host-verification.json` |
| Device health and fresh app backup | `.local/reports/device-2026-11-06/joystick-health/` |
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

The owner authorized continued offline work while the device is unavailable.
Implement and verify original LoadPackages admission, exact-case name mapping,
request/completion
and original OnPackageLoaded callback over the real source package closure. Then
complete boon geometry, individual text areas, cursor/highlights and original
Interact callbacks. Death/DeathArea/House, save/restart/Continue and Android
acceptance follow. Install/verify the preserved joystick and thermal candidate
when hardware returns; host package progress does not close that device gate.
`AUDIT.md` records the concrete acceptance boundaries.
