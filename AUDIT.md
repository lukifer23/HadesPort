# Hades port review and implementation plan

Reviewed 2026-11-06 against source `faee143` and its canonical original-resource
stage. The scope is HadesPort: native engine, host drivers, exporters/stagers,
build/dependencies, recovered contracts, delivery evidence and storage hygiene.
The sibling REA checkout is supporting tooling; its existing bridge edit is
preserved. No device is connected. This is a bounded source and host-runtime review,
not evidence that every original-engine branch has been executed.

## Current result

The repository implements a substantial two-room native prototype. Original Lua
scripts control progression; reconstructed C++ components supply engine behavior;
original assets are exported locally. Unsupported behavior fails explicitly in
strict host mode. Device containment records gaps and skips failed work; it does
not implement that work and cannot count as acceptance.

Fresh strict progression reproduces 4,748 frames through encounter completion,
Athena pickup and the first original ContinueText advancement. Frame 4,748 rejects
`Portrait_Athena_Default_01`. The held-combat 2,950-frame route passes with the
historical five-hit sequence. Presentation sampling checks decoded/runtime state;
it is not full GPU or physical-device proof. Final ASan/UBSan passes 4,748 frames with no findings; Android ARM64 compilation
also passes. Exact current receipts are in STATUS.

The last observed device delivery is iteration16 from 2026-10-26. Current device
state and manual gameplay are unknown beyond the user's report that no device is
connected. Earlier rollback APKs were retired before this review; preserve the
current APK before future publication.

## Architecture and gaps

| Area | Current boundary and implication |
|---|---|
| Original script execution | `lua_runtime`, scheduler, notifications and Lua action units share the native world between host and Android. Control notification advancement is working on the verified route; unsupported script calls must remain explicit. |
| World/combat | Actor lifecycle, movement/pathing, effects, weapons, projectiles, collision and audio support the initial encounter. Beam/Lob/Sky and specialized delayed/live-target branches remain incomplete. Keep shared and projectile hit consumers aligned. |
| Animation ownership | `animation_registry` owns allocation/lifecycle; screen and action-effect worlds supply different owner state. Screen layers implement a bounded Child chain. Independent created manager roots, screen-owned FX and emitter lifetimes are missing from Athena's graph. |
| Boon UI | Screen animation, text and input infrastructure exist. `lua_interaction.cpp` rejects missing actual screen geometry/lifecycle and individual text-box bounds. Cursor selection/highlights and original screen Interact use must be completed before choice works. |
| Maps/resources | Native transitions check 26 room-bound files before retiring the current map. Only RoomOpening/RoomSimple01 are staged. DeathArea is absent. Generic room staging still assumes `HeroEnd` and a unique `CameraIntroEnd`; inspect actual House geometry before exporting. |
| Rendering | Actor/scene/UI pipelines and camera/blur state exist. Visible Shroud still throws in `fullscreen_blur_renderer.cpp`; radial sampling, text layout and some material/sort behavior retain documented approximations. |
| Persistence | Original SGB1/Luabins/LZ4 and profile formats exist. `save_runtime.cpp` backs up then directly truncates/writes the primary. A direct host round trip does not establish script-reached saves, interrupted-write recovery or Android Continue. |
| Content delivery | APK publication has identity/permission checks and an immutable receipt. ZIP32 and the already large APK cannot be the full-game packaging strategy. Content expansion needs a real local-content delivery implementation. |
| Platform/performance | Physical controls, controller loss/remap, pause/resume, surface changes, audio routes and sustained thermal/frame-time/memory behavior require hardware. The 384 MiB actor GPU cache is a measurement target, not a measured defect. |

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

## Ordered implementation plan

| Order | Concrete implementation | Acceptance before moving on |
|---|---|---|
| 1 | Complete Athena initialization. Add graph preflight, real screen-owner effect state, fractions/clocks/location, independent created-manager handles and attachment/disposal. Preserve Defaults → Attach → InitData → Child → singleton Create → ordered Creates → Sound, including child initialization completion. Implement per-slide original FilePath, Loop and random-duration behavior and the ray emitter's nested creation graph. Then derive a fresh versioned resource closure. | Strict original dialogue advances beyond frame 4,748 with zero contained faults and every-frame presentation; creation order, owner death/cancellation and audio are exercised. Preserve held-combat results and run one final ASan/UBSan check. Keep all unsupported fields rejected until their consumers exist. |
| 2 | Implement boon selection. Derive actual transformed screen polygons from source geometry/frame metadata, preserve each attached text component's area and integer/justification rules, register actual screen owner lifetimes, implement cursor candidate ordering and floating highlight FX/audio. Route real Select/touch through Interact.Use with null user and existing NotifyOnInteract. | Original menu appears and a real input selects a boon through the original callback; no fixed button bounds, direct Lua callbacks or forced reward state. Exercise disposal and ambiguous target rejection. Physical touch/controller acceptance remains a separate device gate. |
| 3 | Restore death/House and the script-selected next room. Reproduce first historical death faults; reconstruct low-health screen FX, AllowAnyFire/Graphic side effects, camera clamp clearing and audio Stop/Halt/death voice resources. Rebuild the proven disabled analytics/email branch. Review MapGroups and actual anchors before deriving DeathArea and its complete resource closure. | Strict original death reaches actual DeathArea/House and returns to normal control. Normal progression loads the map selected by original scripts. No fallback scene, invented next room, injected damage or forced encounter clear. |
| 4 | Implement save durability. Retain the original wire formats; document the Android atomic-publication adaptation and recovery precedence. Complete script-reached checkpoint, restart and Continue across actual maps with real saved state. | Byte/semantic compatibility, interrupted/truncated writes and recovery tested using copies of actual saves. Device process death/relaunch resumes correctly without resetting user saves. |
| 5 | Implement full-game local content delivery before broad room expansion. Measure selected closure sizes; retain immutable payload identity and offline ownership/import workflow while removing the single-APK size bottleneck. | Original content loads from the selected local store with complete identity checks, bounded reads and explicit missing-content errors. No network permission or uploaded game files. |
| 6 | Validate Android behavior, then expand full runs. On reconnection, preserve current APK/app files, build a milestone candidate, install non-incrementally and verify actual installed hash/size. Exercise controls, lifecycle, audio recovery and sustained play. Expand enemies/weapons/biomes/bosses/House from reached original script/resource requirements. | Source/pack/APK/device identities recorded per milestone; complete logs, real manual play and measured frame-time/memory/audio/thermal results. Host-only evidence never closes a device gate. |

The immediate next runtime deliverable is Athena's complete graph, not a longer
idle replay. The held unfinished patch under local evidence is reference material,
not an approved implementation to apply wholesale. The detailed original geometry
and cursor evidence is indexed by STATUS.

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
