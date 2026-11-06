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

## Reproduced progression boundary

A fresh strict host build against canonical assets completes 4,748 world frames,
with presentation sampled every frame and zero contained faults. The longer
4,900-frame request correctly fails at frame 4,748:

```
Original update: Scripts/Main.lua:419: Original live screen animation unsupported: Portrait_Athena_Default_01
```

The original flow clears the first encounter, picks up Athena and advances the first
ContinueText notification. Athena's created FX, looping/random slide children and
attached ray emitter remain unavailable. No completed Athena conversation, boon
selection, death/House loop or later-room progression is proven.

The exact held-combat 2,950-frame replay passes with every-frame presentation and
retains the historical five-hit sequence. Release/repress recordings are different
routes and must not be compared as the same baseline. Final ASan/UBSan also passes all 4,748 supported frames with every-frame
presentation and no findings (leak detection disabled). Android ARM64 compilation
also passes; the output was verified as an AArch64 ELF shared library. These checks
do not validate physical controls, lifecycle, performance or full GPU presentation.

## Implemented in this review

- Dependency preparation verifies every extracted file against the pinned Lua,
  Luabins and LZ4 archives. Incomplete, modified, extra or symlinked sources fail
  explicitly without replacement. New extractions are verified before publication.
- Room packs use the same 26-file declaration as the native transition consumer.
  Builders, revisions and staging enforce complete identities and runtime-compatible
  names. Invalid packs and duplicate selections cannot publish a partial room.
- Original-game output protection resolves both sides of the `.local` symlink;
  logical and physical paths into the preserved game are rejected.
- Approximately 373.7 MiB of unused generated probe binaries, objects and debug
  symbols were removed. Original assets, exports, saves, evidence, current APK,
  signing key and required decoder builds were preserved.
- Current documentation replaces accumulated historical checkpoints. The previous
  documents remain available in git at `faee143`; private evidence stays local.

Repeatable real-input checks: 24 dependency integrity checks and 49 room-pack checks, including a full producer run.
No simulated providers, replacement game assets or new runtime stubs were introduced.
Owned host, sanitizer and Android CMake scratch directories were removed after
recording source/library identities and results. The retained APK hash still matches
iteration16; no new APK was published.

## Selected assets and evidence

Build defaults in `scripts/build_native.py` remain authoritative. Canonical assets
use text12/screen14/action24/audio30/actor9/weapons25/properties14; only RoomOpening
and RoomSimple01 are staged. No versioned asset export or APK publication was needed
for this pipeline repair.

| Evidence | Local path |
|---|---|
| Current verification summary | `.local/reports/review-2026-11-06/final-verification.json` |
| Current review/build/replays | `.local/reports/review-2026-11-06/` |
| Dependency integrity | `.local/reports/dependency-integrity-2026-11-06.json` |
| Generated-file cleanup | `.local/private/cleanup/generated-probes-20261106.json` |
| Prior rollback retirement | `.local/maintenance/storage-cleanup-pass2-20261106-123724.json` |
| Iteration16 installation/source/pack receipts | `.local/reports/dialogue-continue-2026-10-26/` and `.local/reports/native-builds/` |
| Athena graph ordering evidence | `.local/reports/screen-created-fx-2026-10-26/` |
| Boon geometry/cursor plan | `.local/reports/gui-interact/screen-boon-next-boundary-plan-2026-10-26.json` |

## Next work

Implement Athena's complete initialization/FX/slide/emitter ownership graph first,
then actual boon geometry, individual text areas, cursor/highlights and original
Interact callbacks. Follow with death/DeathArea/House, save/restart/Continue and
Android acceptance. `AUDIT.md` records the concrete work and acceptance boundaries.
