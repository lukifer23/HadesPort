# Hades port review and implementation plan

Reviewed 2026-11-06; the initial review baseline was `faee143`, followed by the
verified pipeline batch `86b0344`, Athena graph, package/config and current boon-shell milestone.
The scope is HadesPort: native engine, host drivers, exporters/stagers,
build/dependencies, recovered contracts, delivery evidence and storage hygiene.
The sibling REA checkout is supporting tooling; its existing bridge edit is
preserved. The designated device now runs the verified iteration19 joystick/reward-marker repair
over wireless ADB; USB transport was not enumerated.
This is a bounded source, host-runtime and launch review,
not evidence that every original-engine branch has been executed.

## Current result

The repository implements a substantial two-room native prototype. Original Lua
scripts control progression; reconstructed C++ components supply engine behavior;
original assets are exported locally. Unsupported behavior fails explicitly in
strict host mode. Device containment records gaps and skips failed work; it does
not implement that work and cannot count as acceptance.

The paused input checkpoint passes original Athena dialogue and boon construction,
then hovers all three real choices and selects through actual pointer/Interact/
Notify. Effect changes and UseableOff execute; the original callback creates the
real BoonGet movie. The strict request6000 completes5578 at missing native
HideTopMenuScreenTooltips (UIScripts2650). Full close/teardown is still open.

Native InGameUI owns six MenuScreen tooltip slots. Parsed Attack/Deflect keys are
real producers, so an empty/no-op hide is invalid. Reconstruct actual queue/update,
source-template GUI components, original backing/glyph resources and fade/hide.
ModifyTextBox BlockTooltip and ClearUseTarget follow. The latter dispatches target
loss, restores immutable Graphic only on the native matching primary, then clears
the player target; it does not clear the separate cursor target.

Source/default/canonical assets select screen17/action27/audio32. All prior pixels,
clips, audio samples and semantic values are preserved; the new action companion
contains 26 actual BoonGet frames. Fast build/replay covers the later effect/useable
classes. Hotfix ASan/UBSan input/marker5500, exact held2950 and component checks pass,
but final sanitizer/Android verification for the entire checkpoint remains pending.
No new gameplay APK was published for that checkpoint. The user requested a stop;
workers and temporary builds were closed, with exact research/restart notes retained.

Iteration19 (`5b54c0d`) is installed with actual APK identity and four pre-launch
save files verified. The owner rejected iteration18's still-high stick placement.
The new rest shares ATTACK's 19 mm bottom anchor and resets immediately on release/
cancel. Four genuine alternating movement/repress cycles passed on the inner display.

The AthenaUpgrade marker failure now follows native no-dataset child traversal.
Actual-source host and ASan/UBSan checks pass across five live Book nodes; held2950
retains its exact five historical hits. Phone reward replay remains unverified:
the character died during follow-up and hit the existing missing DeathArea asset.
Thermal status stayed 0; about 4.1 GiB was available and PSI avg10 was 0.00% after
controls. No new process crash/ANR was found. Hades was stopped after the checks.
Cover/fold behavior, throttling response and sustained play remain unproven.

## Architecture and gaps

| Area | Current boundary and implication |
|---|---|
| Original script execution | `lua_runtime`, scheduler, notifications and Lua action units share the native world between host and Android. Control notification advancement is working on the verified route; unsupported script calls must remain explicit. |
| World/combat | Actor lifecycle, movement/pathing, effects, weapons, projectiles, collision and audio support the initial encounter. Beam/Lob/Sky and specialized delayed/live-target branches remain incomplete. Keep shared and projectile hit consumers aligned. |
| Animation ownership | `animation_registry` owns allocation/lifecycle; screen and action-effect worlds supply different owner state. Screen layers now initialize actual local/delegated children and independent created manager roots. Reached Athena FX/emitter ownership and disposal execute. External-child ChainTo/TransferChildren still reject explicitly. |
| Boon UI | Actual geometry/text areas, three-choice cursor/highlights, pointer Interact/Notify and original trait changes execute. Close stops at native tooltip production/lifecycle. Complete native six-slot tooltips, BlockTooltip and ClearUseTarget, then verify teardown; phone/controller acceptance remains open. |
| Maps/resources | Native transitions check 26 room-bound files before retiring the current map. Only RoomOpening/RoomSimple01 are staged. Original boon package admission now implements source-derived exact-case unit mapping, all-manifest preflight, real CPU reads and deferred OnPackageLoaded. GPU readiness remains separate. DeathArea is absent. Generic room staging still assumes `HeroEnd` and a unique `CameraIntroEnd`; inspect actual House geometry before exporting. |
| Rendering | Actor/scene/UI pipelines and camera/blur state exist. Visible Shroud still throws in `fullscreen_blur_renderer.cpp`; radial sampling, text layout and some material/sort behavior retain documented approximations. |
| Persistence | Original SGB1/Luabins/LZ4 and profile formats exist. `save_runtime.cpp` backs up then directly truncates/writes the primary. A direct host round trip does not establish script-reached saves, interrupted-write recovery or Android Continue. |
| Content delivery | APK publication has identity/permission checks and an immutable receipt. ZIP32 and the already large APK cannot be the full-game packaging strategy. Content expansion needs a real local-content delivery implementation. |
| Platform/performance | Iteration19 is installed; lower placement and four alternating movement cycles passed. Fresh thermal status stayed 0; no new scoped crash/ANR. Cover/fold and actual throttling response remain open. Physical controls, controller loss/remap, pause/resume, surface changes, audio routes and sustained thermal/frame-time/memory behavior require hardware. The 384 MiB actor GPU cache is a measurement target, not a measured defect. |

Source consumers and specialized recovered contracts take precedence over broad
historical contract summaries. Some older summaries still describe subsequently
implemented behavior; do not reconstruct a feature twice based on stale prose.

## Repairs completed now

**Dependency integrity:** directory existence previously counted as completed
extraction. A cancelled or modified source tree could silently enter builds. The
preparer now checks actual files against the hash-pinned archive, rejects missing,
modified, unexpected and symlinked entries, and publishes verified new extraction
with one rename. Valid existing sources remain untouched. Real cached archives
exercise fresh/idempotent preparation and damaged-input rejection without mocks.

**Room-pack admission and publication:** staging previously accepted arbitrary
file lists and unbounded names, wrote files before later validation, and merged
duplicate selections. Builders/revisions/staging now share admission and the native
26-file declaration, validate identities, and publish complete directories from
scratch. Copied catalog bytes are checked against source identities. Boot-map
resources remain at the root. Original-game output protection now resolves the
actual `.local` symlink target before comparing paths. Real RoomSimple01 staging
and actor revision exercise both valid publication and malformed manifests.

**Hygiene:** 584 tracked files total about 5.22 MB, with no exact duplicates, files
over 1 MiB or game payloads. Earlier owned scratch cleanup removed review probes,
hotfix and package builds. The dedicated storage audit then consolidated 1,327
exact binary/capture groups, preserving all 3,938 paths as read-only shared data;
all export versions remain available. It archived iteration16, 17 legacy probes
and older bulk logs/raw renders losslessly (108 files), with SHA-checked restore
and original mode/mtime. Candidate18/rollback17 stay ready; iteration16 needs restore.

The private directory's accounted usage fell from 44.2 to 35.5 GiB. Physical APFS free-space
gain is unverified; do not report directory accounting as reclaimed volume space.
Original game/installers, saves/backups, key, pinned dependencies, unique device
media and supporting REA checkout remain protected. Python bytecode was removed;
Rust analysis caches remain because offline dependency coverage was incomplete.
All 90 build-manifest checks, 23 package checks and 49 room checks pass after cleanup.
The restore tool was exercised on an actual render and executable. No Codex
transcripts were found. HANDOFF documents shared-file and restoration rules;
`.local/maintenance/storage-audit-20261106/summary.json` indexes exact evidence.

## Athena runtime milestone

The earlier Athena batch reconstructs initialization order and real cross-consumer child
ownership, per-slide original image/random-duration/Loop playback, VFX offsets,
actual screen-owner state, sound eligibility, clock links and pending disposal.
Matching DLL/PDB evidence proves that CreateScreenObstacle calls World SpawnObstacle
then applies a coordinate flag. Its primaries and nested effects remain World0;
actual GameUI manager ownership must not be inferred from the portable screen bank.
Body helpers, Move helpers and per-animation modified time have different equations.
Script SetAnimation does not invoke PreferUnmodifiedTime; Script CreateAnimation's
Setup does, along the self/Child chain only. These facts are recorded in
`native/analysis/screen-thing-fx-owner-contract.json` and cited at the consumers.

The earlier Athena resource closure used screen15/action26/audio31. Independent checks
preserve every previous screen frame/clip, action definition/movie/frame payload/
clip image binding and prior audio semantics/samples/mixing values. Audio export
now accepts only proven same-file aliases for otherwise identical compressed sample
records, retaining historical rows and incoming provenance. Cache identity includes
ctime, preventing same-size edits with restored mtime from reusing old validation.
No original game bytes or derived evidence were added to git.

That earlier milestone exposed the original boon package request. The current
offline batch completes its source-derived admission/CPU completion and reached
config storage. The new shell batch implements movie resources, helper transforms,
loop RNG and original color conversion; BoonSlot1 geometry is the next explicit gap.
The current milestone does not prove boon selection, later rooms, full visual
parity or device play. Existing material/sort/ambient and nonspatial audio adaptations
remain documented; additional unrelated audio playlist branches remain unsupported.

## Ordered implementation plan

| Order | Concrete implementation | Acceptance before moving on |
|---|---|---|
| 0 | Iteration19 delivered over wireless ADB; lower joystick placement and four alternating cycles passed. | Preserve verified APK/saves. Next check cover/fold transitions and bounded longer play, monitoring actual faults/thermal/memory. Stop on severe throttling. |
| 1 | Athena portrait graph — implemented and host-verified. | Strict default route passes the former 4,748 boundary through 4,837; slower real input reaches 5,437 after repeated blink loops. Preserve held-combat and final sanitizer/platform checks. Physical rendering/audio acceptance remains pending. |
| 2 | Original LoadPackages admission and reached config storage — implemented and host-verified. Source-derived mappings, manifest-first admission, actual worker reads and deferred callbacks replace the former Interactables.lua:826 stop. | Native Lua5.2/worker tests, preserved original atlas bytes, strict progression to BoonSelectIn, held combat and final sanitizer/platform checks. GPU residency and actual boon selection remain separate. |
| 3 | Resume the paused boon close path: native InGameUI keyword tooltip producers, source GUI components and fade/hide; BlockTooltip; recovered ClearUseTarget. Existing input reaches the original trait callback. | Complete original callback/teardown through genuine input; sanitizer and Android gates; actual phone touch/controller acceptance. No fixed bounds, forced rewards or direct callbacks. |
| 4 | Restore death/House and the script-selected next room. Reconstruct low-health FX, AllowAnyFire/Graphic side effects, camera clamp clearing and Stop/Halt/death voice resources. Implement the proven disabled analytics/email branch. Review actual DeathArea MapGroups/anchors before export. | Strict original death reaches actual House/DeathArea and normal control. Normal progression loads the map selected by scripts. No invented room, injected damage or forced encounter clear. |
| 5 | Implement save durability while retaining original wire formats. Document Android atomic publication/recovery and complete script-reached checkpoint/restart/Continue across actual maps. | Compatibility, interrupted/truncated writes and recovery verified using copies of actual saves. Device process death/relaunch resumes without resetting saves. |
| 6 | Implement full-game local content delivery before broad room expansion. Retain immutable payload identity and offline import while removing single-APK size limits. | Complete identity checks, bounded reads and explicit missing-content errors. No network permission or uploaded game files. |
| 7 | Validate Android behavior, then expand full runs. Preserve current APK/app files, install a milestone candidate non-incrementally, verify actual hash/size, then exercise controls/lifecycle/audio/thermal and frame-time behavior. Expand from original script/resource requirements. | Source/pack/APK/device identities and real manual play per milestone. Host-only evidence never closes a device gate. |

The next device gates are actual reward replay, cover/fold behavior and lifecycle.
DeathArea delivery is required before death/House acceptance; the phone reproduced
that missing asset. Keep later play bounded and pause before unattended work.
The owner authorized independent offline progress; the next host deliverable is
native tooltip production/lifecycle and actual boon close/teardown.
The held historical screen patch remains reference
material; the integrated implementation was independently reviewed and verified.
STATUS indexes resource preservation, source evidence and exact input routes.

The boon-shell batch retains source-derived coverage rather than a name whitelist:
37 added clips, seven actual GUI images and three qualifying original movies (54
frames). Screen and action helpers share proven scale/angle math. Original duration
initialization and loop rerandomization use the real engine RNG, including child-first
local graph ordering and cached helper fractions. Book completion preserves native
cancellation, ChainTo and immutable ThingData.Graphic fallback. Shared color parsing
uses native RGB-selected normalized/byte conversion, not a byte-only approximation.

Next derive transformed screen polygons from actual sprite/Thing geometry, keep
individual text-box bounds and owner lifetimes, then implement cursor ordering,
highlights and Interact.Use/NotifyOnInteract. No fixed bounds, skipped callbacks or
forced rewards. Existing Material/Ambient/SortMode hints and nonspatial audio remain
adaptations. Exact native ECS command-buffer flush and cross-consumer child-update
phases remain unresolved; the local graph tests do not establish global phase parity.

## Verification discipline

Use one build/export/replay process at a time per worker, `nice -n 15`, `-j 2`,
RelWithDebInfo without sanitizers while iterating. Use the shortest real route that
reaches the change, then preserve the held-combat baseline and one final sanitized
run. Surveys discover gaps; zero-fault strict runs establish only reached coverage.
No new game-specific switches, substitute assets, mocks or silent field dropping.

Keep private evidence under `.local/`; commit source and recovered facts only.
Delete owned scratch builds after recording results. Existing exports can share
hardlinked storage; logical duplicate paths do not establish wasted physical bytes.

Previous detailed audit/checkpoint history is preserved at git commit `faee143`.
Current STATUS and this acceptance plan replace its stale connected-device and
rollback-retention claims.
