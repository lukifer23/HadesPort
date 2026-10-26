# Code audit — 2026-10-26

The port remains a prototype. This audit maps the implementation, repairs verified
source defects, and orders the remaining work. It does not establish complete engine
parity or device readiness. No device testing or APK publication was performed;
iteration12 remains the installed build. The canonical asset stage is untouched;
the second progression pass uses an isolated host candidate described below.

Initial inventory: approximately 30,000 lines across 205 C++ source/header files in `native/app/`,
75 Python exporters/build helpers, the Java platform bridge, host tools, existing
probes, recovered contracts, and the preserved workstream branch. Findings combine
source inspection with bounded host checks; unsupported-branch searches are an
inventory aid, not a completeness metric. Original assets and detailed evidence stay local.

## Architecture and coverage

| Area | Implementation and current boundary |
|---|---|
| Android lifecycle | [engine.cpp](native/app/engine.cpp), [HadesActivity.java](native/app/HadesActivity.java): NativeActivity, EGL, focus/surface recovery, platform vibration/controllers/game mode; lifecycle behavior still needs device regression. |
| Script/world | [lua_runtime.cpp](native/app/lua_runtime.cpp), [script_scheduler.cpp](native/app/script_scheduler.cpp): original Lua, callbacks, clocks, staged native systems; partial phase coverage and explicit missing consumers. |
| Combat/AI | [weapon_execution.cpp](native/app/weapon_execution.cpp), [projectile_world.cpp](native/app/projectile_world.cpp), [lua_combat.cpp](native/app/lua_combat.cpp), [unit_path.cpp](native/app/unit_path.cpp): opening combat works on bounded routes; selected collision/AI/force consumers are integrated in the second pass, with specialized projectiles and other branches still incomplete. |
| Rooms/progression | [lua_map.cpp](native/app/lua_map.cpp), [room_assets.hpp](native/app/room_assets.hpp): retained Lua across map changes; packaged opening/two-room route only, reward/boon/next-room path incomplete. |
| Graphics/animation | [scene_renderer.cpp](native/app/scene_renderer.cpp), [actor_animation.cpp](native/app/actor_animation.cpp), [action_effect.cpp](native/app/action_effect.cpp): GLES scene/actor composition, caches, lighting and effects; full frame and later-content coverage unproven. |
| UI/input | [game_ui.cpp](native/app/game_ui.cpp), [touch_controls.cpp](native/app/touch_controls.cpp), [control_bindings.cpp](native/app/control_bindings.cpp): native menus/settings/touch editor and original bindings; controller menus, mouse and some mapped actions incomplete. |
| Audio/text | [audio_world.cpp](native/app/audio_world.cpp), [audio_output.cpp](native/app/audio_output.cpp), [speech_world.cpp](native/app/speech_world.cpp), [text_layout.cpp](native/app/text_layout.cpp): compressed assets/mixing/AAudio/text; event graph, subtitle and full voice coverage remain partial. |
| Persistence | [save_runtime.cpp](native/app/save_runtime.cpp), [lua_save.cpp](native/app/lua_save.cpp), [config_file.cpp](native/app/config_file.cpp): original save/profile formats and host round trips; interruption recovery and device Continue still need work. |
| Content/build | [asset_packages.cpp](native/app/asset_packages.cpp), [build_native.py](scripts/build_native.py), [stage_room_packs.py](native/tools/stage_room_packs.py): validated local packs and bounded ZIP32 APK; full-game external content remains separate WIP. |
| Diagnostics | [main.cpp](native/host/main.cpp), [blocker_survey.cpp](native/app/blocker_survey.cpp): strict replays, containment surveys, presentation sampling and performance counters; a survey reaching its frame limit is not acceptance. |

## Repairs in this audit batch

- New Run now requests a fresh runtime after returning from a retained game to the
  title; Resume keeps its separate meaning ([game_ui.cpp](native/app/game_ui.cpp),
  [engine.cpp](native/app/engine.cpp)).
  Continue retains its load intent through Retry. Shared Pause/Cancel bindings
  make exactly one menu transition, including when returning from Settings.
- Hardware input tracks physical sources/device IDs, so releasing one binding does
  not clear another held source. Disconnect polling releases stale buttons/sticks;
  trigger/hat state resets with input cancellation ([hardware_input.hpp](native/app/hardware_input.hpp)).
- Touch-layout cancellation releases the editor pointer, allowing a new drag after
  cancellation or resize ([touch_controls.cpp](native/app/touch_controls.cpp)).
- AAudio disconnection is serviced on the owning thread: the old worker/stream closes
  before reopening with the retained mixer. Other failures retain their diagnostics;
  route recovery latency and playback remain device-unverified ([audio_output.cpp](native/app/audio_output.cpp)).
- Actor rendering now defaults to strict errors. Android explicitly enables bounded
  containment, whose visual gaps contribute to the displayed count; suppression is
  renderer-owned and resets on shutdown ([scene_renderer.cpp](native/app/scene_renderer.cpp)).
- Callback-capable activation, weapons, effects and movement phases snapshot actor
  IDs before callbacks can grow manager storage. This removes invalidated span iteration
  while preserving phase membership ([lua_actor_animation.cpp](native/app/lua_actor_animation.cpp),
  [lua_combat.cpp](native/app/lua_combat.cpp), [lua_effects.cpp](native/app/lua_effects.cpp),
  [lua_player_input.cpp](native/app/lua_player_input.cpp)).
- Primary spawner metadata, movement definitions and aim-line polygon fields now fail
  with explicit missing/bounds errors; aim-line maximum Y handles negative coordinates.
  Rumble releases C++ error owners before Lua raises via longjmp ([lua_runtime.cpp](native/app/lua_runtime.cpp)).
- Package atlas CPU payloads share immutable storage by validated content-addressed
  path; package metadata/readiness remain independent. The selected catalog has 12
  references to 10 unique textures: 131,201,536 logical bytes become 103,741,952 unique
  payload bytes, saving 27,459,584 bytes (26.2 MiB). This is payload accounting, not an
  RSS or GPU-memory measurement ([asset_packages.cpp](native/app/asset_packages.cpp)).
- Host success now requires completed requested frames, no contained faults, and no
  mixer/input/presentation failure. Argument and trace-write errors fail explicitly
  ([main.cpp](native/host/main.cpp)).
- APK signing/verification/permission checks finish in a private candidate directory
  before atomic publication, preserving the previous APK on failure. Build defaults
  now use `nice -n 15` and two jobs ([build_native.py](scripts/build_native.py)).

## First-pass verification

These results apply to the audit repair batch before the progression integration below.

The baseline strict 2,950-frame route passed. The initial repaired route also passed
2,950 frames with `--validate-live-presentation`, no recorded faults, and all 2,950
frames sampled for presentation data. Sampling does not draw every frame or establish
complete rendering parity. Host and native ARM64 builds and Java compilation passed;
controls/menu and package-sharing probes passed. The GPU diagnostic passed strict
rejection, containment deduplication/bounds and reload checks. Malformed spawner
fields/indices return explicit errors without crashing; invalid CLI arguments are
rejected. The final strict 2,950-frame route, every-frame presentation sampling and
structural save/reload comparison passed. Six injected APK publication cases passed;
no production APK was published by those probes.

A fresh 9,000-frame autoplay containment survey reached the first encounter reward
with 16 distinct faults and returned failure status 2. It is diagnostic evidence of
remaining gaps. Final ASan/UBSan controls, package-sharing and 2,950-frame strict
combat/presentation checks pass without sanitizer findings.
Detailed evidence is retained locally under `.local/reports/audit-2026-10-26/`.

## Second progression pass

Selected consumers from the preserved workstream branch have now been reviewed and
integrated into current source, without a bulk branch merge:

- Collision weapons, ApplyForce, collision-effect clearing and the shared wall-hit
  damage path ([lua_combat.cpp](native/app/lua_combat.cpp), [life_take_hit.cpp](native/app/life_take_hit.cpp)).
  The shared damage method still rejects weapon/projectile/effect-bearing requests;
  this does not establish every collision or damage branch.
- Per-entity VFX clocks, non-HAA primary friction and persistent occluder-root updates
  ([lua_vfx.cpp](native/app/lua_vfx.cpp), [lua_physics.cpp](native/app/lua_physics.cpp),
  [lua_occlusion.cpp](native/app/lua_occlusion.cpp)).
- Native MoveAI failure selection/live target offsets, GetClosest obstacle stop filters and ordinary
  screen Teleport ([unit_path.cpp](native/app/unit_path.cpp), [lua_actor_motion.cpp](native/app/lua_actor_motion.cpp),
  [lua_spatial_queries.cpp](native/app/lua_spatial_queries.cpp)). Retained native reader
  evidence corrected WIP's case-sensitive enum parsing to case-insensitive matching.
- Distance notifications now use actual obstacle stopping predicates and swept LOS
  geometry, retaining the documented portable grid/narrowphase adaptations
  ([lua_collision_notify.cpp](native/app/lua_collision_notify.cpp)).
- Projectile impact FX/tip location and live elapsed-time/AllProjectiles reflection
  ([lua_projectiles.cpp](native/app/lua_projectiles.cpp), [projectile_world.cpp](native/app/projectile_world.cpp),
  [lua_runtime.cpp](native/app/lua_runtime.cpp)). Unsupported reflection consumers remain explicit.
- Book and Constant frame transfer now follows the destination's native virtual
  consumer in both primary replacement and manager chains. Book keeps the raw
  fractional frame; atlas geometry and pixels both use the clamped displayed frame.
  A real seven-frame destination probe caught and fixed an out-of-bounds atlas-ratio
  lookup ([action_effect.cpp](native/app/action_effect.cpp),
  [animation-frame-transfer-contract.json](native/analysis/animation-frame-transfer-contract.json)).
- Reward animation/resource closure derives spawnable LootData and transitive
  RewardStoreData/ConsumableData references with source-field provenance, preserving
  combat arsenal scope ([animation_coverage.py](native/tools/animation_coverage.py)).

The isolated host candidate selects validated action-effects-v24 and audio-events-v30:
1,913 action definitions/13,615 frames and 2,190 audio events/3,635 samples. A focused
source comparison finds its reward roots, banks, packages and arsenal scope identical
to the immutable coverage used to generate v24; no new binary export was needed.
Build defaults now select v24/v30. The canonical stage still contains v22/v29 and
the APK is untouched; the candidate is temporary host validation data.

Host compilation, effect-clock, movement, occlusion lifecycle and actual-resource
Book/Constant transfer probes pass. Strict mixed combat passes 2,950 frames with
every-frame presentation sampling and zero faults. A strict 9,000-frame autoplay
run also records zero faults, but stalls with three enemies remaining. It proves
bounded world updates, not encounter completion. Earlier intermediate surveys
reached reward barriers, but their counts cannot be compared directly after the
route changes. The wall-hit path dispatches original OnHit and records actual
health reduction (130 to 111 from 19 native damage).

The final native ARM64 build passes. ASan/UBSan frame-transfer and strict 2,950-frame
combat/every-frame-presentation/save checks pass without findings, including a
structural save/reload comparison. Stronger autoplay every-frame presentation
sampling fails at frame 2,529 with `Action FX owner draw group unavailable`, status
2, and no reported sanitizer finding. The fault-free 9,000-frame replay samples
presentation at its final snapshot only; it does not cover transient visuals.
The 6,000-frame trace shows normal movement consumption with empty effects/requests
and no strafe lock, but collision-limited displacement; detour diagnosis remains open.

Remaining explicit presentation gaps include transient owner draw-group/lifetime
resolution, fullscreen AdjustFrame/radial blur and reward ShiftSpeed components;
the latter has an evidence ledger, but insufficient component-layout proof for a
faithful implementation. No reward, boon or full-game acceptance claim follows.
Evidence stays local under `.local/reports/progression-2026-10-26/`.

## Priority work

| Priority | Work and source boundary | Acceptance |
|---|---|---|
| 1 | Repair transient action-FX owner draw-group/lifetime resolution and diagnose the longer autoplay combat stall using actual movement/request traces; distinguish host-driver navigation from engine gates. Finish remaining collision/damage and AI branches: [lua_combat.cpp](native/app/lua_combat.cpp), [lua_actor_motion.cpp](native/app/lua_actor_motion.cpp), [autoplay.cpp](native/host/autoplay.cpp). | Every-frame presentation on the autoplay route, then strict original encounter completion with movement, death and next-wave milestones; a fault-free frame budget is insufficient. |
| 1 | Reconstruct fullscreen blur/frame, reward ShiftSpeed and remaining subtitle consumers: [action_effect.cpp](native/app/action_effect.cpp), [lua_speech.cpp](native/app/lua_speech.cpp), [lua_presentation.cpp](native/app/lua_presentation.cpp). | Prove ECS layout/update/copy and RNG order before admission; sample transient visuals and validate actual GPU output. |
| 2 | Original reward Use → dialogue → boon selection → script-chosen door/map, followed by later encounters/biomes/bosses and House features. | Strict progression and save/Continue across transitions; touch/controller screen paths; device confirmation later. |
| 2 | Controller title/settings/retry navigation, mouse buttons/scroll, and mapped controls discarded by the gameplay whitelist: [engine.cpp](native/app/engine.cpp), [game_ui.hpp](native/app/game_ui.hpp). | Physical-input source tests plus later device menus/remapping/hotplug; retain original control semantics. |
| 2 | Save interruption and recovery: [save_runtime.cpp](native/app/save_runtime.cpp) writes with `fopen(...,"wb")` after a backup; [lua_save.cpp](native/app/lua_save.cpp) does not provide general backup recovery. | Proven original behavior/adaptation policy, injected write/truncation failures, recoverable checkpoint selection; preserve real saves. |
| 3 | Full audio graphs/loops/voice/subtitles, timer/indicators/hero silhouette and later content; existing settings do not prove their engine consumers. | Resource-derived coverage and feature-specific routes; device audio/rendering/accessibility review. |

Performance candidates remain unmeasured: full scene copies/sorts per frame
([scene_renderer.cpp](native/app/scene_renderer.cpp)), settings/profile writes during
slider/layout changes ([engine.cpp](native/app/engine.cpp)), fixed 384 MiB actor GPU
cache plus decoded CPU caches, and scans retaining retired actors
([actor_runtime.hpp](native/app/actor_runtime.hpp)). Capture frame/update/upload/cache
and memory data before changing ordering, eviction or lifecycle semantics. Sustained
frame rate, thermal behavior, load latency and memory pressure require device testing.

Connectivity means wired/Bluetooth controller and audio lifecycle plus local content
delivery. The app intentionally has no network permission; online services are outside
the architecture ([AndroidManifest.xml](native/app/AndroidManifest.xml)).

## Preserved branch integration

`wip/agents-2026-10-20` remains separate: 52 changed files, roughly +2,036/-352 lines,
without new test files in that preserved snapshot. Selected areas are now integrated
as described above; remaining areas need separate review and verification on `main`.
Its combat, presentation, exporter and packaging changes are not one acceptance unit.
The recorded frame-zero `DrawHealthFX` failure needs a fresh check: stored source
already adds that default. The shared null-weapon damage method is wired to wall
collision, while weapon/projectile/effect branches remain rejected. Beam/Lob/Sky state
records do not implement those projectile classes. Fullscreen effects lack host draw
wiring and vignette blending remains an explicit approximation.

Before adopting its external content store, repair retry-after-install, bounds checked
after allocation, deduplicated-byte progress, and synchronous cold verification during
the loading frame. Validate hash vectors, malformed/truncated/missing content, cache
invalidation, cold/warm loads and recovery. Verify the current reward/ImpactFx and
timing/friction/combat batches before that larger delivery change. Exported maps are
not proof of gameplay coverage.
