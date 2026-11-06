# Hades port review and implementation plan

Reviewed 2026-11-06; the initial review baseline was `faee143`, followed by the
verified pipeline batch `86b0344` and the current Athena runtime milestone. The scope is HadesPort: native engine, host drivers, exporters/stagers,
build/dependencies, recovered contracts, delivery evidence and storage hygiene.
The sibling REA checkout is supporting tooling; its existing bridge edit is
preserved. The designated device reached RoomOpening on iteration17, then disconnected before
the iteration18 joystick/thermal repair could be installed.
This is a bounded source, host-runtime and launch review,
not evidence that every original-engine branch has been executed.

## Current result

The repository implements a substantial two-room native prototype. Original Lua
scripts control progression; reconstructed C++ components supply engine behavior;
original assets are exported locally. Unsupported behavior fails explicitly in
strict host mode. Device containment records gaps and skips failed work; it does
not implement that work and cannot count as acceptance.

Fresh strict progression now passes Athena's former frame-4,748 failure and
completes 4,837 frames through the original dialogue to its boon-menu package
request. The longer route stops explicitly on unimplemented LoadPackages at
Scripts/Interactables.lua:826. A slower original ContinueText input cadence exercises
multiple Athena blink loops/random-duration resampling and reaches that same package
boundary at frame 5,437. The held-combat 2,950-frame route retains the historical
five-hit sequence. Presentation sampling checks decoded/runtime state; it is not
full GPU or physical-device proof. Final ASan/UBSan passes 5,437 frames with no findings (leak detection disabled);
Android ARM64 compilation also passes. STATUS indexes exact receipts.

Iteration17's actual installed hash/size and app-data preservation were verified;
RoomOpening rendered on the device. Owner control acceptance failed: the floating
stick's physical rest was outside its acquisition zone on the cover display.
Iteration18 repairs responsive placement and visible-control acquisition, cancels
old-panel input before simulation and refreshes platform status on resume. Its
production controls regression check fails on the old code, passes the repair,
and passes ASan/UBSan. The signed candidate is built from clean source `4381770`,
with all 30 resource manifests unchanged. USB disconnected before install; device
release/repress acceptance remains open. STATUS records exact identity and receipts.

No current Hades crash/ANR was found. Current memory snapshots showed available
headroom and low PSI, but Android reported severe thermal throttling. The app was
stopped and backed up; the phone cooled to light status. The candidate reads actual
PowerManager thermal status and caps rendering while retaining fixed simulation,
adds a 60 FPS fresh default and 30 FPS menu cap. Existing profiles/saves remain
preserved. These changes require short device observation; no stress test or thermal
override is justified. Sustained memory/thermal stability remains unproven.

## Architecture and gaps

| Area | Current boundary and implication |
|---|---|
| Original script execution | `lua_runtime`, scheduler, notifications and Lua action units share the native world between host and Android. Control notification advancement is working on the verified route; unsupported script calls must remain explicit. |
| World/combat | Actor lifecycle, movement/pathing, effects, weapons, projectiles, collision and audio support the initial encounter. Beam/Lob/Sky and specialized delayed/live-target branches remain incomplete. Keep shared and projectile hit consumers aligned. |
| Animation ownership | `animation_registry` owns allocation/lifecycle; screen and action-effect worlds supply different owner state. Screen layers now initialize actual local/delegated children and independent created manager roots. Reached Athena FX/emitter ownership and disposal execute. External-child ChainTo/TransferChildren still reject explicitly. |
| Boon UI | Screen animation, text and input infrastructure exist. `lua_interaction.cpp` rejects missing actual screen geometry/lifecycle and individual text-box bounds. Cursor selection/highlights and original screen Interact use must be completed before choice works. |
| Maps/resources | Native transitions check 26 room-bound files before retiring the current map. Only RoomOpening/RoomSimple01 are staged. Original boon pickup now reaches unimplemented LoadPackages; exact-case unit-package mapping, manifest-first pass, asynchronous completion and OnPackageLoaded remain to implement. DeathArea is absent. Generic room staging still assumes `HeroEnd` and a unique `CameraIntroEnd`; inspect actual House geometry before exporting. |
| Rendering | Actor/scene/UI pipelines and camera/blur state exist. Visible Shroud still throws in `fullscreen_blur_renderer.cpp`; radial sampling, text layout and some material/sort behavior retain documented approximations. |
| Persistence | Original SGB1/Luabins/LZ4 and profile formats exist. `save_runtime.cpp` backs up then directly truncates/writes the primary. A direct host round trip does not establish script-reached saves, interrupted-write recovery or Android Continue. |
| Content delivery | APK publication has identity/permission checks and an immutable receipt. ZIP32 and the already large APK cannot be the full-game packaging strategy. Content expansion needs a real local-content delivery implementation. |
| Platform/performance | Joystick regression repair is packaged but uninstalled; severe thermals were observed and cooled after stopping Hades. Thermal-aware pacing is implemented, awaiting hardware observation. Physical controls, controller loss/remap, pause/resume, surface changes, audio routes and sustained thermal/frame-time/memory behavior require hardware. The 384 MiB actor GPU cache is a measurement target, not a measured defect. |

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

**Hygiene:** the tracked tree had 575 files totaling about 5.15 MB, no exact byte
duplicates, no large generated logs and no game payloads found. Removed about
373.7 MiB of unused probe binaries/objects/debug symbols after checking open
handles. Preserved original files, immutable exports, saves, receipts/captures,
current APK, key and required decoder builds. No Codex transcript files were found.
Historical reports include repeated snapshots; their run-specific evidence paths
remain intact. Reduce future snapshot production when changing the replay driver.
The joystick checkpoint rechecked all 581 tracked files (about 5.17 MB): no exact
duplicates or files over 1 MiB. Removed another 5.08 GiB of owned APK/build/probe
scratch and the clean hotfix worktree after recording identity; candidate, rollbacks,
actual app backups, canonical exports and signing key remain retained.

## Athena runtime milestone

The current batch reconstructs initialization order and real cross-consumer child
ownership, per-slide original image/random-duration/Loop playback, VFX offsets,
actual screen-owner state, sound eligibility, clock links and pending disposal.
Matching DLL/PDB evidence proves that CreateScreenObstacle calls World SpawnObstacle
then applies a coordinate flag. Its primaries and nested effects remain World0;
actual GameUI manager ownership must not be inferred from the portable screen bank.
Body helpers, Move helpers and per-animation modified time have different equations.
Script SetAnimation does not invoke PreferUnmodifiedTime; Script CreateAnimation's
Setup does, along the self/Child chain only. These facts are recorded in
`native/analysis/screen-thing-fx-owner-contract.json` and cited at the consumers.

Selected resource closure now uses screen15/action26/audio31. Independent checks
preserve every previous screen frame/clip, action definition/movie/frame payload/
clip image binding and prior audio semantics/samples/mixing values. Audio export
now accepts only proven same-file aliases for otherwise identical compressed sample
records, retaining historical rows and incoming provenance. Cache identity includes
ctime, preventing same-size edits with restored mtime from reusing old validation.
No original game bytes or derived evidence were added to git.

The original script now requests the actual boon package. LoadPackages remains an
explicit failure; completing its source contract and actual resource closure is
next. The current milestone does not prove boon selection, later rooms, full visual
parity or device play. Existing material/sort/ambient and nonspatial audio adaptations
remain documented; additional unrelated audio playlist branches remain unsupported.

## Ordered implementation plan

| Order | Concrete implementation | Acceptance before moving on |
|---|---|---|
| 0 | Deliver the iteration18 joystick/thermal repair after USB reconnect. | Verify actual installed identity, app-file preservation, repeated visible-stick release/repress, no current faults and short thermal/memory observations. Stop on severe throttling. Device acceptance remains open; host package work can proceed independently. |
| 1 | Athena portrait graph — implemented and host-verified. | Strict default route passes the former 4,748 boundary through 4,837; slower real input reaches 5,437 after repeated blink loops. Preserve held-combat and final sanitizer/platform checks. Physical rendering/audio acceptance remains pending. |
| 2 | Implement original LoadPackages and actual AthenaUpgrade resource admission. Reuse existing package parsing/residency, reconstruct exact-case unit-name mapping and manifest-first ordering, queue real work and dispatch original OnPackageLoaded only at the recovered completion boundary. Derive includes from the original package files. | Strict original pickup passes the current Interactables.lua:826 stop. No package-name insertion as fake completion, silent load success, direct callback or missing atlas/movie substitution. CPU residency, worker completion and GPU readiness remain distinct. |
| 3 | Implement boon selection. Derive actual transformed screen polygons and individual text areas with original integer/justification rules; retain owner lifetimes, cursor ordering and floating-highlight FX/audio. Route real Select/touch through Interact.Use and NotifyOnInteract. | Original menu appears and a real input selects a boon via the original callback. Exercise disposal/ambiguity rejection. No fixed bounds, forced reward state or direct Lua callback. Device touch/controller acceptance remains separate. |
| 4 | Restore death/House and the script-selected next room. Reconstruct low-health FX, AllowAnyFire/Graphic side effects, camera clamp clearing and Stop/Halt/death voice resources. Implement the proven disabled analytics/email branch. Review actual DeathArea MapGroups/anchors before export. | Strict original death reaches actual House/DeathArea and normal control. Normal progression loads the map selected by scripts. No invented room, injected damage or forced encounter clear. |
| 5 | Implement save durability while retaining original wire formats. Document Android atomic publication/recovery and complete script-reached checkpoint/restart/Continue across actual maps. | Compatibility, interrupted/truncated writes and recovery verified using copies of actual saves. Device process death/relaunch resumes without resetting saves. |
| 6 | Implement full-game local content delivery before broad room expansion. Retain immutable payload identity and offline import while removing single-APK size limits. | Complete identity checks, bounded reads and explicit missing-content errors. No network permission or uploaded game files. |
| 7 | Validate Android behavior, then expand full runs. Preserve current APK/app files, install a milestone candidate non-incrementally, verify actual hash/size, then exercise controls/lifecycle/audio/thermal and frame-time behavior. Expand from original script/resource requirements. | Source/pack/APK/device identities and real manual play per milestone. Host-only evidence never closes a device gate. |

The joystick/thermal repair is the next device delivery gate when hardware returns.
The owner authorized independent offline progress; the next host deliverable is
actual package admission/
completion at the original boon request. The held historical screen patch remains reference
material; the integrated implementation was independently reviewed and verified.
STATUS indexes resource preservation, source evidence and exact input routes.

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
