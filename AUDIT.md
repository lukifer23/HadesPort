# Hades port review and implementation plan

Updated 2026-11-07 for source `1028a6b` and installed iteration21. The goal is a
complete offline native ARM64 Android engine running the original Hades scripts
and owned assets. This remains an incomplete two-room prototype. The review covers
source, recovered contracts, resource pipelines, host replay and bounded device
observations; it does not establish execution of every original-engine branch.
The sibling REA checkout supplies analysis tooling and retains its existing bridge
edit. `STATUS.md` records live evidence; `HANDOFF.md` gives continuation commands.

## Current result

Iteration21, source `1028a6b8296543239c54f9ce900c035216bc847c`, is installed over USB
with actual SHA-256
`ff502b78fcf20b2ab17980ee1b9b58f4d6cf82a286a78ad09ca1c9f11c16f870`,
2,613,850,469 bytes and the unchanged signing certificate. All four actual app files
were verified unchanged after installation, before launch. APK20/19 and rollback18/17
remain preserved.

Iteration21's real Continue and genuine controls defeated six enemies naturally.
Dash with live ranged enemies returned zero danger. The logs do not establish a
live projectile candidate at the query; positive projectile geometry and the owner
callback are host-verified and remain a separate hardware acceptance gate.
At the final wave, original `Scripts/RoomEvents.lua:620` stopped strict execution at
`SpawnUnit ForceToValidLocation relocation search not reconstructed: LightRanged`.
The player had 25 HP. Hades was deliberately stopped; all four actual app files
remained byte-identical before installation versus after play, and final thermal
status was 0. No callbacks/progression were injected and fault containment was off.
Physical reward/boon completion remains open behind native blocked-spawn relocation.
The receipt is `.local/reports/device-2026-11-07/iteration21/acceptance.json`.

On iteration20, original Continue restored the version16 Temp save into RoomSimple01
and genuine movement/attack/special/dash input defeated six enemies naturally.
A real dash with a live ranged enemy then stopped strict execution at
`Weapon::GetNearbyDanger projectile danger geometry not reconstructed`. The player
had 20 HP. Hades was deliberately stopped; all four app files remained byte-identical
before installation versus after play, and final thermal status was 0. Iteration21
includes the source-derived repair and its bounded device evidence is recorded above.
These bounded observations do not establish sustained performance or later-room
acceptance.

The strict host input route completes all 6,220 requested frames with zero
contained faults and live presentation validated for every completed frame. It
hovers the three actual boon choices, observes native tooltip creation and fade,
and selects through real pointer input and original Interact/Notify. Trait changes,
BoonGet creation, source-owner disposal, movie detachment/retirement and menu
teardown execute. Final same-source ASan/UBSan checks pass this route, a 3,600-frame
danger/input route and the held 2,950-frame combat replay. The danger route passes
the actual projectile owner 1000003 into original OnWeaponFired, has no optional
rubble-selector blocker and reaches a later natural ranged wave. All three runs
validate every completed frame with zero contained faults. Boon world samples match
the prior accepted route; held combat retains its five exact historical hits.
Leak detection was disabled.

The recorded extended strict request for 6,500 frames completes 6,220, then stops at:

```
Actual room asset unavailable: A_Combat04/scene.bin
```

Original Gaussian room selection now executes and selects that actual next room.
Only RoomOpening and RoomSimple01 are bundled. DeathArea is also absent and was
reproduced as a transition failure on an earlier device build. Passing the
boon checkpoint therefore establishes neither a complete run nor death/House recovery.

## Implemented foundations

**Strict execution:** Android preserves original action errors and assertions and
stops failed world/render work. Survey and fault containment are explicit host
diagnostics. They must never be counted as gameplay acceptance or used to bypass
a missing action. The previous unconditional Android suppression is removed.

**Native tooltip and target lifecycle:** InGameUI/MenuScreen owns six retained
slots separately from World Things. Parsed text keys, actual Keywords, BlockTooltip,
source text parameters, native queue/show/hide timing, authored GUI styles, Clear
and fade behavior have real consumers. ClearUseTarget retains the cached entity
through original Lost callbacks and restores immutable Graphic only when its
primary matches; cursor/highlight ownership remains separate. Text components own
cached glyph geometry while placement and fades remain live.

**Original resources and safe publication:** defaults, canonical host assets and
APK21 select packages2/text13/screen17/action27/audio32/actor9/weapons25/properties14.
Text13 preserves all 177 previous font/icon payloads and adds the actual AutoTooltip
crop and complete authored templates. Independent BC7 pixel comparison, source and
semantic preservation, 16 damaged-input/publication cases and six ownership cases
pass. Text staging verifies unique temporary copies, preserves identical existing
inodes and publishes the catalog last. Old exports remain immutable.

**Shared config and recovered RNG:** the Lua config API consumes the same recovered
registered-field specification as native configuration, replacing its second
whitelist. Unsupported non-scalar/enum conversions still reject explicitly. Native
Gaussian streams have independent PCG state and cached spares, follow recovered
rejection/operation order and retain spares across randomseed. Native math.random
dispatch is restored. Source vector, type/float-bound, cached-spare, stream-isolation,
config and dispatch checks pass. Last-bit equivalence between Windows CRT and the
host/Android log/sqrt implementations is not established.

**Native danger and signed selection:** source `1028a6b` reconstructs the actual
projectile allegiance, attachment-aware point/ellipse/hollow tests, first-owner
return and enemy primary-animation scan. The result passes as an integer through
the weapon continuations into original OnWeaponFired; a mutable actor cache is
removed and existing team-provider copies are consolidated. Source signed object
selectors are separated from allocation identity and actual membership lookup.
Missing optional rubble targets follow recovered AddToGroup/motion selection,
including native group creation order. Screen and world motion share one validated
decoder with plural precedence and empty-table handling. Native pending-hash
ordering, switched allegiance and unexecuted geometry cases retain explicit limits.
Recovered facts and boundaries are recorded in
`native/analysis/nearby-danger-contract.json` and
`native/analysis/object-id-selection-contract.json`.

**Durable persistence:** saves, profile configuration/controls and UI settings share
same-filesystem temporary writes, file synchronization, atomic replacement and
directory synchronization. A validated previous save is backed up before primary
replacement; corresponding valid backups can recover corrupt/unreadable primaries.
Original main/Temp timestamp and validity selection is retained; stale validity
marker backups are not restored. Eleven actual-save/file/subprocess checks pass
ASan/UBSan, including kernel write limits, truncation and SIGKILL. Device Continue
loads the real existing save. Physical power loss and complete script-reached
checkpoint/restart acceptance remain separate gates.

Dependency preparation already verifies hash-pinned extraction completeness before
publication. Room builders/stagers share the native 26-file declaration, validate
complete manifests and source identities, and publish fresh directories. These
foundations should be extended through their existing owners rather than duplicated.

## Remaining architecture boundaries

| Area | Confirmed boundary and required work |
|---|---|
| Maps and original progression | A_Combat04 and DeathArea are absent. Derive each requested map's actual scripts, geometry, groups, anchors and complete resource closure. Existing generic staging assumes HeroEnd and a unique CameraIntroEnd; validate House geometry before reuse. |
| Combat and animation | The physical final-wave Unit spawn needs Thing::ForceToValidLocation (RVA0x2e1440) after actual hull/StopsUnits overlap. Recover its search; the obstacle point-search/RNG policy cannot substitute. Initial encounter systems execute. Specialized weapon/projectile branches, external child transfers and unreconstructed ownership paths remain explicit gaps. Keep shared hit and projectile consumers aligned; reached tests do not prove all weapons/enemies. |
| Package I/O and residency | Original package admission and deferred callbacks exist, but the provider performs atlas reads synchronously from pump. One texture per service call is not a byte/time budget. CPU atlases remain retained without an aggregate eviction budget. GPU readiness is a separate owner and acceptance boundary. |
| Rendering | Visible Shroud still fails explicitly. Material/Ambient/SortMode and other admitted presentation fields retain documented approximations. Exact native ECS command-buffer flush and cross-consumer phase parity remain unresolved. Zero faults on a supported route does not establish visual parity. |
| Content delivery | Original payloads are still bundled into a roughly 2.61 GB APK using the current ZIP32 publication path. Full-game expansion requires a verified offline local-content store/import path independent of single-APK size limits. |
| Save acceptance | Durable publication and real-file recovery work; verify original script-reached checkpoints across actual map transitions and device process death/relaunch without resetting saves. |
| Platform and performance | Physical boon acceptance, cover/fold transitions, pause/resume, surface recreation, controller connection/remap/loss, audio routes and sustained frame/audio/thermal/memory behavior remain open. Cache capacities are measurement targets, not measured defects. |

## Ordered next work

| Order | Concrete deliverable | Required acceptance |
|---|---|---|
| 1 | Reconstruct native blocked-Unit spawn relocation, then repeat bounded physical reward/boon acceptance. | Recover Thing::ForceToValidLocation RVA0x2e1440 and its actual hull predicate/RNG order; reuse existing Unit validity/location owners. Record an actual blocked candidate and real relocation before original RoomEvents final-wave creation completes. The obstacle point search cannot substitute. Preserve APK/app files, use genuine input with strict execution, then observe the original trait change, menu teardown and resumed control. Physical boon acceptance remains open; record results separately from host proof. |
| 2 | Export and integrate the actual A_Combat04 closure, then reconstruct death/House and subsequent script-selected transitions. | Strict original progression enters the selected map with correct resources and normal control. Actual death reaches the real House/DeathArea. Recover reached engine gaps from matching DLL/PDB evidence; never substitute rooms or force encounter results. |
| 3 | Complete script-reached checkpoint/restart acceptance across those transitions. | Actual checkpoint publication, process death/relaunch and Continue preserve original run state. Keep real saves/backups protected; exercise destructive failures on independent copies. |
| 4 | Implement full-game offline content delivery before broad room expansion. | Verified owned-file import, complete identity checks, bounded reads, atomic publication and explicit missing-content errors. Preserve immutable payload identity and offline operation. |
| 5 | Move package reads off the frame thread and implement bounded residency through the existing package owner. | Preserve original request/completion order and ownership, cancellation and error propagation. Measure real cold/warm load latency, frame time and memory; verify CPU/GPU readiness independently and avoid duplicate caches. |
| 6 | Close rendering and native scheduling gaps required by expanded play. | Reconstruct visible Shroud and reached material/sort/attachment/phase semantics from provenance. Validate actual pixels, owner lifetimes and device rendering; keep any remaining approximation explicit. |
| 7 | Qualify Android lifecycle and sustained play, then expand original encounters/maps toward complete runs. | Physical fold/cover, controls/controller loss, pause/resume, surface/audio route changes and sustained frame-time/thermal/memory evidence. Stop on severe throttling; successful short input cycles do not establish sustained acceptance. |

Platform checks can proceed alongside the next bounded milestone. Map delivery and
package performance become prerequisites before large-scale content expansion.
Source consumers and specialized contracts take precedence over older broad
summaries; do not reconstruct completed behavior again because historical prose
still lists it as missing.

## Evidence and verification discipline

Current host receipts are under `.local/reports/boon-completion-2026-11-07/`;
the final same-source danger/boon/combat acceptance is indexed by its
`danger-input-review/final-verification.json`.
Resource/pixel/publication receipts are under
`.local/reports/native-tooltip-2026-11-07/`; device receipts are under
`.local/reports/device-2026-11-07/`. Immutable APK receipts remain under
`.local/reports/native-builds/`. `STATUS.md` separates current acceptance from history.

Use the shortest real route that reaches a change, retain the held-combat baseline,
and run one final ASan/UBSan gate after runtime changes. Run one build/export/replay
per worker under `nice -n 15` and builds with `-j 2`. Strict runs establish reached
coverage; surveys discover gaps. Use original assets and callback/input paths, with
no mocks, stubs, forced progression, substitute art or silent field dropping.

Keep original/derived game data and session evidence under ignored `.local/`; commit
only implementation and recovered facts. Preserve current/rollback APKs, saves,
backups, signing identity and pinned tools. Existing exports can share read-only
hardlinks: replace destination directory entries rather than changing shared
inodes. Remove only owned scratch work after recording results. Directory accounting
and file deduplication do not establish reclaimed physical APFS space.
