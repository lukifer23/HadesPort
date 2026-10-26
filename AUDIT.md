# Code audit — 2026-10-26

The port remains a prototype. This audit maps the implementation, repairs verified
source defects, and orders the remaining work. It does not establish complete engine
parity or device readiness. No device testing, APK publication, or asset staging was
performed in this session; iteration12 remains the installed build.

Scope: approximately 30,000 lines across 205 C++ source/header files in `native/app/`,
75 Python exporters/build helpers, the Java platform bridge, host tools, existing
probes, recovered contracts, and the preserved workstream branch. Findings combine
source inspection with bounded host checks; unsupported-branch searches are an
inventory aid, not a completeness metric. Original assets and detailed evidence stay local.

## Architecture and coverage

| Area | Implementation and current boundary |
|---|---|
| Android lifecycle | [engine.cpp](native/app/engine.cpp), [HadesActivity.java](native/app/HadesActivity.java): NativeActivity, EGL, focus/surface recovery, platform vibration/controllers/game mode; lifecycle behavior still needs device regression. |
| Script/world | [lua_runtime.cpp](native/app/lua_runtime.cpp), [script_scheduler.cpp](native/app/script_scheduler.cpp): original Lua, callbacks, clocks, staged native systems; partial phase coverage and explicit missing consumers. |
| Combat/AI | [weapon_execution.cpp](native/app/weapon_execution.cpp), [projectile_world.cpp](native/app/projectile_world.cpp), [lua_combat.cpp](native/app/lua_combat.cpp), [unit_path.cpp](native/app/unit_path.cpp): opening combat works on bounded routes; specialized projectiles, collision damage, AI options and force consumers remain incomplete. |
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

## Verification boundary

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

## Priority work

| Priority | Work and source boundary | Acceptance |
|---|---|---|
| 1 | Reached collision weapons/damage, MoveAI failure behavior, spatial filters and ApplyForce: [lua_combat.cpp](native/app/lua_combat.cpp), [lua_actor_motion.cpp](native/app/lua_actor_motion.cpp), [lua_spatial_queries.cpp](native/app/lua_spatial_queries.cpp). | Reconstruct from matching binary/contracts; shortest real repro, strict mixed route, then encounter route without skipped native behavior. |
| 1 | Reached occlusion mutation, elapsed-time/projectile reflection, screen motion, fullscreen blur/frame, subtitles and reward resource consumers: [actor_runtime.cpp](native/app/actor_runtime.cpp), [lua_actor_motion.cpp](native/app/lua_actor_motion.cpp), [lua_speech.cpp](native/app/lua_speech.cpp), [lua_projectiles.cpp](native/app/lua_projectiles.cpp). | Resolve root consumers and derived closure; preserve explicit errors, sample transient visuals and validate GPU output where changed. |
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
without new test files. Extract coherent areas and verify each on `main`; do not merge
its combat, presentation, exporter and packaging changes as one acceptance unit.
The recorded frame-zero `DrawHealthFX` failure needs a fresh check: stored source
already adds that default. The shared null-weapon damage method is wired to wall
collision, while weapon/projectile/effect branches remain rejected. Beam/Lob/Sky state
records do not implement those projectile classes. Fullscreen effects lack host draw
wiring and vignette blending remains an explicit approximation.

Before adopting its external content store, repair retry-after-install, bounds checked
after allocation, deduplicated-byte progress, and synchronous cold verification during
the loading frame. Validate hash vectors, malformed/truncated/missing content, cache
invalidation, cold/warm loads and recovery. Integrate reward closure and ImpactFx,
then timing/friction and presentation/combat in bounded batches before that larger
delivery change. Exported maps are not proof of gameplay coverage.
