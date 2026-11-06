# Hades native port — current state

Updated 2026-11-06. Read `HANDOFF.md` for commands and `AUDIT.md` for the implementation
plan. This is an incomplete native ARM64 Android engine driven by the original game.

## Device and delivery

**No test device is connected.** No device inspection, installation or manual play
was performed during this review. Host results cannot establish device acceptance.

Last observed installation: iteration16, runtime source `f6339d9`, verified on
2026-10-26 by actual installed APK SHA-256 and size:
`b1bdc3a03969d45bd03d333a8558607fc9d6aa4d1f78ad366cbedf135c93ff19`,
2,571,651,037 bytes. Manual gameplay16 remains unverified. Its current local APK is
`.local/build/native/hades-native-engine-preview.apk`.

Earlier rollback APKs were retired in a separate authorized storage cleanup on
2026-11-06. They are **not available locally**. Historical installation receipts
and app-file backups remain; do not mistake those receipts for retained APKs.
Before any future replacement, preserve the current APK and actual app files.

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
library was verified as AArch64 ELF. Tested runtime source is `312f521`. No device
operations occurred. All owned host/ASan/Android scratch builds were removed.

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
and RoomSimple01 are staged. Source and staged resources are ahead of the retained
iteration16 APK; no new APK was published.

| Evidence | Local path |
|---|---|
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

Implement original LoadPackages admission, exact-case name mapping, request/completion
and original OnPackageLoaded callback over the real source package closure. Then
complete boon geometry, individual text areas, cursor/highlights and original
Interact callbacks. Death/DeathArea/House, save/restart/Continue and Android
acceptance follow. `AUDIT.md` records the concrete acceptance boundaries.
