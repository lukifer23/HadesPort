# Hades native port — status tracker

New here? Start with `HANDOFF.md`.

Single source of truth for workstreams, builds and device results.
One source tree (`native/`), one APK (`.local/build/native/hades-native-engine-preview.apk`),
built and installed only by the main session (`scripts/build_native.py`). Agent
scratch build dirs (`.local/native-host-build-<name>`) are temporary and deleted
when the agent finishes; `.local/native-host-build` is the main host build.

## Testing policy

Iterate on fast unsanitized host builds (RelWithDebInfo, `HADES_SANITIZE=OFF`),
batch fixes before rerunning, use the shortest replay that reaches the changed
path, and do one sanitized (ASan/UBSan) run at the end. Device installs only for
meaningful milestones.

## Dialogue continuation and iteration16 (2026-10-26)

**Iteration16 installed** — source `f6339d9`, full non-incremental user0 replacement.
SHA-256 `b1bdc3a03969d45bd03d333a8558607fc9d6aa4d1f78ad366cbedf135c93ff19`,
2,571,651,037 bytes. Actual installed hash/size match. Launch/process observed;
manual gameplay16 remains unverified. Iteration15 is preserved as
`baseline-iteration15-891116b5.apk`; fresh app-files backup retained without reset.
The immutable build receipt binds 313 code files and 30 selected manifests. Code
bytes match the final host verification; no INTERNET permission was added.

The original ContinueText notification now drives host press/release pulses and an
Android touch NEXT button. The full recovered control registry admits dialogue
controls independently of weapon controls. Context changes clear held/queued touch
contacts. Disabled controls retain physical history and expose native cached flags.
Screen coordinate flags no longer select the component-manager backend. Screen
spawn destinations use actual location/parallax; late numeric actions skip disposed
manager entries. Portrait exits perform actual cross-manager attachment cancellation
in recovered Attach order, preserving the current primary during that pass.

Text12 exports all 4,340 TextData identities, 4,328 DisplayNames and 4,340 descriptions;
all prior 4,029 names, 4,098 descriptions and 177 font/symbol payloads are preserved.
Ten numeric and two ambiguous DisplayName fields stay explicit unknowns. HasDisplayName
checks record membership. Screen14 preserves all 775 old clips and their 1,930
compressed frames, and adds 12 original portrait exits. The bounded cancellation contract rejects unsupported
child/owner-input/foreign-domain branches. Original NotifyOnInteract and scalar
SetInteractProperty consumers are reconstructed, with remaining selectors explicit.

Final strict host continuation and ASan/UBSan pass 4,748 frames, zero faults and
presentation sampling every frame; no sanitizer findings (leak detection disabled).
This advances the original first dialogue through Zagreus's exit and reward FX.
The exact held-combat 2,950 replay retains the same five-hit sequence. Touch transition
checks and a portable Interact runtime-state mutation probe pass. These are not full
GPU rendering or physical-device interaction proof.

The longer strict route stops at frame 4,748 on `Portrait_Athena_Default_01`.
Its created FX, looping/random slide children and attached ray-emitter graph remain
unsupported. The shorter frame count versus iteration15's idle 5,600 is a changed
input route: the current driver advances dialogue rather than waiting. No Athena
conversation completion or boon choice is claimed. Next: complete that animation
ownership graph, then real screen geometry/text areas/cursor/floating highlights
and original Select/Interact callbacks; death/House/save acceptance remains separate.
No partial CreateAnimation field admission or placeholder callbacks were integrated.

Canonical packs now select text12/screen14 with action24/audio30/actor9/weapons25.
Evidence and install/build receipts: `.local/reports/dialogue-continue-2026-10-26/`,
plus immutable build receipt by APK hash. Owned host/ASan/candidate scratch and the
probe executable were removed; all packs, evidence, saves and rollback APKs retained.

## Review and iteration15 (2026-10-26 evening; superseded by16)

The full review and ordered acceptance plan are in [AUDIT.md](AUDIT.md). This entry
supersedes historical host-only/no-device checkpoints below.

**Iteration15 installed** — source `f04cd02`, full non-incremental user0 install.
SHA-256 `891116b5c2119e2a707232ec9c39c1dbca40b008656601ce334c01694db9b1a4`,
2,564,954,077 bytes. Actual installed SHA/size match; launch/process/resumed activity
observed, zero launch fault lines in the captured current-process log. This is
installation/launch evidence only; manual gameplay for iteration15 remains unverified.
Iteration14 is preserved as `baseline-iteration14-11e3e368.apk`; app files are
backed up privately. Canonical assets now select text10/screen-dialogue13 with
actor9/weapons25/action24/audio30. Build receipt binds 311 code files and 30 selected
manifests to source `f04cd02`; source identities match final host verification.
Immutable build/device receipts stay ignored under `.local/reports/`.


Before replacement, designated-device inspection re-verified iteration14's installed
SHA/size and captured complete logs plus current-process filtering. Actual logs show low-health
presentation gaps, then death consumers/audio faults and original DeathArea request;
transition fails because DeathArea is not packaged. Later nil speech errors follow
contained PlaySpeech failure and must not be treated as separate native contracts.
Iteration14 and app files are backed up before any new publication. No injected
phone input or unrelated app capture was used.

The reviewed text/input/screen batch was integrated and tested with original-text-v10
and screen-dialogue-v13 in an isolated host stage. Text10 retains all prior DisplayNames,
adds independently inherited Description/AdditionalDescription (4,098 records) and
preserves known empty descriptions. It exports 21 original fonts; no font substitutes.
Coverage v20 matches v19's resource closure; the `or` lexer fix adds provenance only.

Strict hold-combat 2,950 passes with every-frame presentation and the exact existing
five-hit sequence (`takeover-mixed-combat-hold.tsv`). The separate release/repress
input (`native-firstcombat-replay-v1.tsv`) also passes 2,950 with seven hits and a
structurally equal save/reload probe; these input routes are not interchangeable.
The save probe invokes the registered action directly and is not a script-reached
checkpoint or device Continue proof. New native Bool/lifetime boundary checks pass.

Strict autoplay with new resources reaches 4,575 frames, past Athena use and prior
missing screen/font/description resources; it exposes original dialogue spacing and
emphasis-font consumers. These failures are retained, not suppressed. Top/Bottom
spacing and the complete original emphasis-font map are now recovered. A subsequent
strict route passes 5,600 frames with every-frame presentation, zero faults and
original dialogue presentation sampled while waiting for input. This proves dialogue entry, not
advancement or boon choice. Android now retains all original mapped hardware controls
(including Confirm/Select/ContinueText); physical confirmation remains pending. No
new device gameplay claim follows from this host checkpoint.

Private evidence: `.local/reports/review-2026-10-26/`, text export verification under
`.local/reports/full-review-2026-10-26/`. Final ASan/UBSan autoplay passes 5,600/5,600
with zero faults and every-frame presentation, no sanitizer findings (leak detection
disabled). Final hold-combat
passes with text10 and the unchanged five-hit sequence. Android ARM64/Java/signing/
permission checks pass; the installed result is recorded above. No INTERNET permission was added. Owned temporary host/ASan/candidate trees
were removed after retaining receipts. Pre-existing builds and all immutable packs,
evidence, actual saves and rollback APKs were preserved.

## Earlier device session (iterations13–14, superseded by15)

The Fold (SM-F966U, Android 17) is connected again over USB and wireless debugging.
The installed iteration12 hash was re-verified before replacement and is retained as
`baseline-iteration12-d39fefae.apk` alongside the iteration10a rollback.

**Iteration13 installed** — source `2c5efc4` (the 2026-10-26 audit + progression
batches; the `.so` compile finished before any later edit). Canonical assets were
freshly staged with the default packs (action-effects-v24, audio-events-v30).
SHA-256 `c5f7859909e6c43060920179dce6528ee4dca4e3291c42f9dd013de053417121`,
2,540,195,648 bytes; full non-incremental user0 install; on-device hash and size match.
A short manual session reached RoomOpening only (p50 8.0 ms, p95 14.2 ms, zero
underruns, no contained faults); it predates the reward fixes below.

**Iteration14 installed** — source `94efe22` (everything below plus the fullscreen
blur/vignette reconstruction). SHA-256
`11e3e368301ca76d72831787dab71175b510b3924c439c2eb3693c3423237680`, 2,540,220,224 bytes;
full non-incremental user0 install; on-device hash and size match; launched.
Iteration13 is kept as `baseline-iteration13-c5f78599.apk`. Expected on device: the
whole first encounter, the Athena reward spawning and pickup; the dialogue that
follows is the next host gap (below). Manual play results will be recorded here.

`94efe22` reconstructs BlurComponent radial blur (AdjustRadialBlurStrength/Distance,
App::Update stepping, original RadialBlur.frag pass after grading and bloom) and the
camera vignette frame (AdjustFrame, DrawHealthFX default). Zero-duration blur calls
change no drawn value, exactly as App::Update 0x180037145 gates them. The reached
route therefore never activates the blur, so its GPU pass is checked only by the
host shader validator. Strict mixed 2,950 and autoplay with every-frame presentation
(4,555 frames: all four waves, reward spawn and pickup) pass; autoplay now stops at
`OnActiveUseTarget`: `Original text token unreconstructed: \` (Athena use prompt).

Baseline note: strict mixed combat now records five COMBAT_HIT lines (2374, 2442,
2532, 2546, 2828); runs earlier on 2026-10-26 recorded seven. Rebuilding committed
sources from iteration13 (`2c5efc4`) through `94efe22` reproduces the five-hit
sequence deterministically; the held Attack2 stays trigger-locked by the original
`SwordDisableCancelableAndLockTrigger` until release. The seven-hit runs are not
reproducible from any committed source and are treated as superseded.

Workstreams at iteration14 (integrated by iteration15 above): text runtime (markup tokens, UseDescription, CreateTextBox Id,
ModifySubtitles) and dialogue/boon screen assets (screen animations, CaesarDressing
font, ToggleMove), integrated and committed by main only.

Host fixes since iteration13 (all included in iteration14):

- `0901272` Attached FX surviving their owner's disposal keep the owner's draw group
  (the pre-dispose snapshot omitted it). Autoplay with every-frame presentation now
  validates 4,400/4,400 frames (previously stopped at 2,529). Projectile owner markers
  for non-Book primaries use the shared base `Animation::GetMarkerLocation` dispatch.
- `79f71e0` Host autoplay steers along the engine's own A* path (read-only query, no
  RNG). The earlier "stall with three enemies" was the straight-line driver walking
  into a concave wall corner, not an engine movement gate: dash moved the hero freely.
  Autoplay now clears waves 1–3, enters wave 4 and reaches the original
  `LastKillPresentation`, which stops strictly at `AdjustRadialBlurDistance`.
- Strict mixed combat: 2,950/2,950 frames with every-frame presentation, zero faults,
  combat-hit sequence identical to the pre-change run. Sanitized pass pending at batch end.

- `253700b` ShiftFraction Z-shift phase for action FX: boon-drop icons no longer
  block, so the encounter reward spawns and can be picked up.

## Historical host progression checkpoint (superseded by evening device session)

This batch integrates native collision weapon/force/wall-hit/effect-clear consumers,
per-entity VFX clocks, non-HAA friction, persistent occluder roots, MoveAI failure
selection/live offsets, stopping filters/LOS notifications, ordinary screen Teleport,
projectile impact/tip location, live Thing reflection, and Book/Constant frame
transfer. Atlas geometry now uses the same clamped display frame as the pixels.
[AUDIT.md](AUDIT.md) records the scope and remaining native branches.

Reward coverage derives loot/store/consumable references with field provenance,
preserving combat arsenal scope. Build defaults select action-effects-v24
(1,913 definitions/13,615 frames) and audio-events-v30 (2,190 events/3,635 samples).
The isolated candidate validates original source hashes and animation/audio binding;
its reward roots, banks, packages and arsenal reproduce the immutable v24 coverage.
Canonical assets still contain v22/v29. No APK was published or device accessed;
iteration12 remains installed. Temporary validation assets/builds are removed at
checkpoint cleanup; versioned source packs and evidence are retained.

Verification:

- Host and native ARM64 library builds pass. Effect-clock, movement, occlusion
  lifecycle and actual-resource Book/Constant frame-transfer probes pass.
- Strict mixed combat passes 2,950 frames, samples presentation every frame, and
  records zero faults. Actual original OnHit dispatch reduces wall-hit health
  from 130 to 111 using 19 native damage.
- Strict autoplay completes 9,000 world frames with zero faults, but stalls with
  three enemies remaining. It is not encounter acceptance. A 6,000-frame contained
  trace records zero gaps and shows consumed movement/normal speed, no effects,
  strafe or request lock, and collision-limited displacement. A detour/navigation
  repro remains next work; no movement gate was forcibly cleared.
- Final ASan/UBSan frame-transfer and strict 2,950-frame combat/presentation/save
  checks pass without findings; structural save/reload comparison passes.
  The stronger autoplay every-frame presentation check stops at frame 2,529 with
  `Action FX owner draw group unavailable` (status 2, 2,529 frames sampled).
  It has no reported sanitizer finding, but is a failed presentation route.
  The 9,000-frame result samples presentation at the final snapshot only.

Current blockers: repair transient action-FX owner draw-group/lifetime resolution;
diagnose the longer route's collision/navigation stall; reconstruct
fullscreen AdjustFrame/radial blur, reward ShiftSpeed ECS behavior and remaining
subtitle consumers; then validate reward Use, dialogue, boon choice and next-room
flow. ShiftSpeed layout/copy/RNG evidence remains incomplete, so its barrier stays
explicit. Historical/intermediate survey counts are not comparable across different
reached routes. Evidence is private under `.local/reports/progression-2026-10-26/`.

Cleanup removed completed host/Android/CMake builds, scratch validation assets,
generated report binaries/objects/debug symbols, bytecode/Finder caches, the redundant
`.local 2` alias and four empty numbered asset folders. Estimated allocated storage
released: 5.4 GiB, accounting for hardlinks; filesystem snapshots/compression can
affect actual free space. No exact duplicate tracked files were found. Original
data, immutable exports, decoder dependencies, signing key, saves/evidence, canonical
assets and both current/rollback APKs are preserved. See HANDOFF.md for restart policy.

## First code audit checkpoint (2026-10-26, code-only)

The source audit and prioritized acceptance work are in [AUDIT.md](AUDIT.md).
This first pass used no device, APK publication, or asset staging; iteration12 stayed
installed. The preserved `wip/agents-2026-10-20` branch was reviewed separately;
subsequent selected integration is recorded above.

The repair batch addresses fresh New Run state, physical input ownership and
disconnect cleanup, touch-editor cancellation, owner-thread AAudio disconnect
recovery, strict renderer errors and visible contained visual gaps, callback-safe
actor iteration, malformed metadata/null checks, package CPU atlas sharing, truthful
host replay status, and verified atomic APK publication. Builds use two low-priority
jobs. These source repairs do not establish device behavior or full engine parity.

Verification:

- Baseline strict mixed route: 2,950 frames passed. Initial repaired strict route:
  2,950 frames passed with live presentation sampling on every frame and zero
  recorded faults. Presentation sampling is not full per-frame GPU rendering.
- Host/native ARM64 builds and Java compilation passed. Controls/menu and
  package-sharing smoke probes passed; six injected APK publication cases passed.
- Selected package atlas payload: 12 references, 10 unique textures;
  131,201,536 logical bytes versus 103,741,952 unique bytes, saving 26.2 MiB.
  This is CPU payload accounting, not a measured RSS/GPU reduction.
- Fresh 9,000-frame autoplay containment survey reached the encounter reward with
  16 distinct faults and returned status 2. It is not a passing acceptance route.
- Final strict mixed replay passes 2,950 frames, samples presentation every frame,
  records zero contained faults and passes structural save/reload comparison.
  The GPU diagnostic passes strict rejection, contained reason deduplication/bounds
  and reload behavior. Missing/invalid spawner metadata and invalid CLI arguments
  fail explicitly. Final ASan/UBSan controls, package-sharing and strict
  2,950-frame combat/presentation checks pass without findings.

Evidence remains private under `.local/reports/audit-2026-10-26/`. Priorities are
the reached collision/AI/filter/force, occlusion/reflection, screen/fullscreen,
subtitle and reward consumers, then original boon/dialogue/next-room progression.
Controller menu/mouse/action coverage, save interruption recovery, measured
performance, later game content and device validation remain open.

## Installed checkpoint (reviewed 2026-10-20)

Iteration12 is the latest installed build: SHA-256
`d39fefae60155eafbce5806dcb4502403c04b2a7d65f3358813041248aabb351`,
2,453,881,955 bytes. Its runtime source checkpoint is `2e3cba3`; subsequent commits
record deployment evidence and documentation. Installation used a full
non-incremental user0 stream, followed by on-device hash/size verification and launch.
Iteration10a remains the preserved, user-confirmed improved-controls rollback.

The goal is a complete offline native Android game using the original Lua and assets.
This remains a prototype. The current repair focuses on shared combat, controls,
animation recovery, cast, enemy AI/death and progression. Main coordinates builds
and installs; the user drives manual device play. Do not inject device input.

Installed checkpoint packs (historical): opening-actors-v13, actor-simple-v4 via room-packs-v6,
actor-actions-v9, actor-markers-v9, actor-properties-v14 (HAP6),
player-weapons-v25, action-effects-v22, screen-opening-v12, original-text-v7,
audio-events-v29. RoomOpening and RoomSimple01 are packaged; later rooms need
validated original packs and lifecycle coverage. No fallback scene is supplied.

### Verification and its limits

- Device: verified installation, title launch and live RoomOpening capture. The
  room screenshot shows 84 FPS / 11.8 ms; the captured process log has no fault
  lines. A 12-second passive video is retained locally. These captures do not
  prove device combat or encounter completion.
- Strict mixed route: 2,950 frames, empty initialization/presentation/mixer/input
  errors. Two real enemy hits lower hero health 50→40→30; sword and special fire
  afterward. All four directions recover after melee; the first post-special
  left window overlaps its legitimate disable effect. That effect then expires,
  later movement resumes, and final requests/effects are empty with strafe0.
  Original zero-fuse Instant touchdown detonation and explosion audio complete.
  These final host runs use sampled presentation; validated_presentation_frames
  is0, so they do not establish full per-frame rendering correctness.
- Strict next-wave route: 3,100 frames, no blocked stage or presentation error.
  First enemy dies at2626 after eight real20-damage hits; money pickup completes.
  Two next-wave enemies finish child-movie activation, run original AI/pathing
  and damage the hero at2974. Projectile/effect/scheduler faults remain0.
- Final ASan/UBSan: the 2,950-frame mixed route passes without findings.
- Earlier cast/pickup/recast: strict and sanitized1,810-frame route passed,
  including ammo return, second cast and cardinal recovery. This was earlier in
  the repair batch; the final candidate has not repeated this route on device.
- Full forced encounter, Athena dialogue/boon, subsequent room selection and
  transition, and device save/reload remain unverified.

Evidence is private under `.local/reports/`: `takeover-instant-mixed-strict-v4`,
`takeover-child-movie-nextwave-r1`, `takeover-iteration12-sanitized-pass`,
`takeover-iteration12-installed-verification.json` and `takeover-iteration12-device/`.
Use these final results; earlier contained surveys and failed intermediate runs
are retained history, not current blockers.

### Root repairs to preserve

- Same-name victim Effect reapplication retains its original data/factory/source
  identity across weapons. A false identity guard interrupted firing cleanup and
  repeatedly renewed disable effects. Do not clear legitimate requests to hide it.
- Native reflection reads ReflectAddition; packed weapons preserve proven-empty
  RemoveEffects. Missing hit consumers previously prevented request cleanup.
- Native world/animation clock ownership prevents hitstop or effects from applying
  global speed twice. HERO_TRACE reports held controls, requests, strafe, timers,
  velocity, facing and animation; COMBAT_HIT reports actual health/life changes.
- Real Unit activation, firing/markers, A* movement, targetability and child-movie
  ownership allow original Lua AI to progress. Attach and draw offsets remain
  separate. Money pickups use deferred native destruction.
- Zero-fuse Instant uses base initialization and synchronous native detonation.
  Audio coverage preflights before FX allocation; first faults remain visible.
- Resource closure derives dependencies across native producers, child/chain
  animations and map namespaces. Fresh validated staging removes stale assets;
  packaging verifies audio bindings to the selected actor/action closure.

### Workstream checkpoint (2026-10-20 evening, code-only session)

By the user's direction this session is code work only: no device installs or
device tests. Iteration12 stays the installed build (hash re-verified on the
device at session start). Process limits: one build/export/replay per worker,
`nice -n 15`, `-j 2`, no copied source trees, no background processes left running.
The machine also hosts other projects; do not starve them.

Host progression now runs without recorded input: `hades-debug ... --autoplay`
(native/host/autoplay.cpp) drives the original `RequiredKillEnemies` and live
Interact owners through the same `setLocalInput` path as touch. With
`--contain-faults` it lists every engine gap real play reaches. Current reach:

- RoomOpening → door → RoomSimple01; all four EnemyIntroFight01 waves (SimpleMelee,
  2× HeavyMelee, 3× LightRanged, HeavyMelee + 3× LightRanged) killed; money pickup.
- Encounter end reaches the original reward spawn. Fixed so far: cross-world VFX
  cap victim (particle world threw map::at), string ScriptArgs name lists
  (`DestinationNames = "SpawnPoints"`), reward-chain resource closure
  (action-effects-v24: all LootData/consumable reward obstacles), per-entity VFX
  owner time, live ElapsedTimeMultiplier.
- Save mid-RoomSimple01 → fresh reload is structurally identical (the native-
  rewritten `_worldTime`/`_screenTime` excluded per save-contract); `--load-save`
  Continue resumes the encounter.
- Next on the reward path: projectile ImpactFx consumer (GunWeaponImpact, reached
  by real collision weapons), AdjustRadialBlur*/AdjustFrame, then loot Use →
  dialogue → UpgradeChoice, then the door and LoadMap of the original next room.

All workstream agents were stopped at 21:05 by the user's request; no build, export or
replay process is running. Their unfinished work is preserved on the branch
`wip/agents-2026-10-20` (origin), not on `main`. It has not passed the strict regression yet: the last strict firstcombat run stopped at frame 0 on
`DrawHealthFX` (the vignette-frame work was in progress), so none of it is on `main`.
Resume by checking out that branch (or cherry-picking per area), building it once,
finishing what is half-done, then landing each area on `main` only after strict
firstcombat2950 + autoplay pass.

Reported state of the branch work when stopped:

- projectiles: Projectile::ImpactFx (6e090) consumer with CalcTipLocation (6e2d0) —
  GunWeaponImpact gap gone in autoplay; Beam/Lob/Sky subclasses not started in code.
- combat: FireCollisionWeapon / initiated collision weapons, MoveAI OnFailBehavior,
  GetClosest StopsProjectiles/StopsUnits, ApplyForce, screen Teleport, non-HAA
  friction, live ElapsedTimeMultiplier; shared non-projectile TakeHit
  (life_take_hit.cpp) was mid-wiring into OnObstacleCollision.
- presentation: per-entity VFX owner time (lua_vfx / effect_animation); fullscreen blur
  (AdjustRadialBlur*) and AdjustFrame/vignette in progress (source of the DrawHealthFX
  stop); occlusion ObstacleSet change mid-edit.
- rooms: reward-chain closure (animation_coverage reward_objects → action-effects-v24),
  external content store (content_store.cpp, host_assets.hpp), per-map export
  (export_room_pack.py, 7 maps exported into room-packs-v7), `--start-map` mid-edit.
- audio: audio-events-v30 bound to action-effects-v24 (2190 events, 3635 samples;
  autoplay with v30 had no audio gaps), export_audio_release.py one-command recipe
  (`.local/reports/audio-v30-recipe.md`); full VO table/subtitle rule/playlist mode 1
  not started.

Workstreams to recreate when work resumes (one coordinator builds/packages; agents never commit):

| Area | Owner | Scope |
|---|---|---|
| Progression driver, interaction, save/Continue, integration, docs | main | autoplay, reward → door → LoadMap, commits |
| Beam/Lob/Sky projectiles, delayed-fuse Instant, ImpactFx consumer | projectiles | projectile_*, lua_projectiles |
| Collision weapons, wall-slam TakeHit, MoveAI, LOS notifies, ApplyForce | combat | lua_combat, lua_actor_motion, unit_*, life_take_hit |
| Boon/dialogue screens, FX branches, blur/frame, PC features (occlusion, timer, indicators) | presentation | action_effect, effect_*, screens, ui |
| External content store, all-map room packs, reward closure | rooms | packaging, tools, build_native.py |
| Full-game audio: v30 rebind, all 19,597 VO lines + subtitles, all banks | audio | audio exporters and runtime |

Evidence for this session is private under `.local/reports/session-1020/`
(autoN/saveN/contN logs). The detailed local restart notes are in ignored
`.local/private/RESTART_HANDOFF.md`; never commit it.

## Device (Galaxy Z Fold, `$HADES_DEVICE_SERIAL`)

| Date / time | APK sha256 (prefix) | Result |
|---|---|---|
| 2026-10-19 00:03 | d39fefae (iteration12) | Full non-incremental user0 install; installed2453881955-byte SHA verified and app launched. Launch plus live RoomOpening screenshot and12second passive video captured; live screenshot84FPS/11.8ms, current-process captured logs have0 fault lines. Strict mixed2950 + ASan/UBSan2950 pass, including enemy hits, later sword/special, four cardinals after melee and final clean requests/effects/strafe. Strict nextwave3100 proves enemy death, money pickup, next-wave activation/AI/path/attack. Manual device combat/full encounter/reward/next-room verification remains pending. |
| 2026-10-18 23:09 | e15269e9 (iteration10a) | Full non-incremental user0 install, installed SHA verified and launched. Shared same-name cross-weapon effect reapplication, world scale axes, IdExists/Shift/stale Destroy selection, native Shake/HAP6, dynamic rubble/raw graphics, Unit activation/CanAttack/state notifications and precise arrival. Mixed2750-frame contained replay logs actual cross-source OnHitStun and all4 directions recover with requests/effects empty and strafe0. User confirms improved controls; enemy attack/death and progression still fail. Screenshot and12second passive video retained locally. Subsequent source repairs are not yet device verified. |
| 2026-10-18 22:54 | ca356a17 (iteration9) | Full non-incremental user0 install and on-device SHA verified, launched. Real RoomSimple01 screenshot captured; user repeated attacks exposes projectile frame335/weapon frame366 cross-source victim Reapply guard. Released SwordWeapon3 stays queued, strafe1 and disable effects recur, reproducing control lock. Source repair retains original effect identity per native Reapply; not yet device verified. |
| 2026-10-18 22:45 | 851a3be5 (iteration8) | Full install/SHA verified. Real hit then projectile defense Reflection lookup interrupts firing cleanup; corrected ReflectAddition in iteration9. |
| 2026-10-18 22:28 | 3c0732dc (iteration6) | Full install/hash verified. Includes actual victim HitSound, actor scale correction, action13 PostRead normalization and original room VO. Continue restores actual RoomSimple01 assets, then fails at frame1 because timed missing audio parameters incorrectly poison the audio system. Host reproduces from actual device checkpoint; source repair underway. Strict melee additionally reaches original enemy hit FX cap fields; cast reaches actual ammo primary/marker disposal. |
| 2026-10-18 22:19–22:22 | 3e4ebc1d (iteration5) | Full install and installed SHA verified. User enters RoomSimple01 without immediate crash; guarded screenshot/video confirm actual SimpleMelee visible. Released attack repeats because reached enemy HitSound consumer interrupts firing before request cleanup; source fixes the consumer. Original room VO missing; source exports it. HUD screenshot follows original16s auto-hide; cast pickup remains under investigation. |
| 2026-10-18 20:58 | b437817c (build 39) | Device turn test (HERO_TRACE in logcat every ~0.5 s): body angle turns correctly (0/π/3π/2/π/2). Host action trace: sword combo animates (Charge1→Attack1), dash moves ~230 units, but dash + special projectiles hit the unreconstructed Detonation obstacle LOS/ApplyImpact branch; after the special the hero stays in ZagreusSwordParryFire (soft-lock) → weapons agent. Device now on wireless ADB (name in .local/private/LOCAL.md) |
| 2026-10-18 20:48 | 66595fde (build 38) | E2E (fixed harness): sword combo + special fire, no room failure (render-path visuals now skipped+logged). + special DetonateGraphic/DetonateSound, CheckProjectileDefense, AddControlOnFire, timed FocusCamera, PanCamera new-target (room exit/entry), obstacle occlusion fade (UseOcclusion). Remaining soft-lock: special's detonation obstacle ApplyImpact → weapons agent. Next: room progression (Athena boon → door) agent. Harness bug fixed (taps were mis-scaled; it toggled Bloom/Lighting, restored) |
| 2026-10-18 20:27 | 1dd3ba86 (build 36) | E2E (scripted + user): HUD visible (health 50/50, ammo 1/1, boons), walking/attacks/dash work. action-effects-v10 (root cause: Books playing a later window of an inherited sequence, e.g. SwordSwipeBDark 8-20, were exported only to NumFrames), collision-end fade-in + CollisionAnimation/CollisionEndedAnimation. Soft-lock: sword special RadialNovaSwordParry DetonateGraphic gap keeps the attack active (movement disabled) → weapons agent #1. Touch stick moved above the HUD health band (next build). Hero occlusion silhouette not rebuilt (Zagreus hidden behind foreground statues) |
| 2026-10-18 20:17 | 7de04017 (build 35) | + CollisionBounce/VelocityRetention (ApplyCollision 0x2bd3d0, MultiplyVelocity 0x2bd450; the device "character won't move" gap), ClearEffect (Unit 0x1802f410a, UnitStatus::ClearEffect/Reset), render-path containment (bad visual skipped + logged, not room failure). Strict survey of first-combat replay: 0 gaps. Device-play gaps → agents: AddControlOnFire, DetonateGraphic RadialNovaSwordParry, ShowAimLine, OnOutOfAmmo, timed FocusCamera |
| 2026-10-18 20:10 | 7597f7a0 (build 32) | Device e2e (scripted + user): moved, attacked, killed ghosts; render failure "action FX frame outside resource" ended the room (now contained + detailed); then movement stopped on contained CollisionBounce gap |
| 2026-10-18 20:07 | 7597f7a0 (build 32) | + CollisionAlphaOutDuration obstacle fade (Body+0x320, OnObstacleCollision 0x2f6ce0) — the contained gap hit at frame 1658 on device |
| 2026-10-18 20:03 | a8b01272 (build 31) | Device (user): moved further with no "unable to enter"; the "crash" was the package update killing the app mid-play (now: install only when the game isn't foreground). + VisualFxUseAttachedMarkerLocation, exact ApplyShift walking+dash, OnObstacleCollision. Strict survey: only ClearEffect (kill path) → agent |
| 2026-10-18 20:00 | e23b93c8 (build 30) | Fix: a runtime whose world had faulted was reused after the app surface was lost → instant blank "please try again". Now torn down; empty reasons get a fallback message |
| 2026-10-18 19:57 | e8a9d771 (build 29) | Durability: device contains unreconstructed behaviour (Lua call wrappers + `containStage` for 20 world stages), logs `CONTAINED_FAULT`, shows "N engine gaps" on the FPS line instead of ending the room. + StopAnimation (ScriptAction 0x2632e0 / AnimationManager::Cancel 0x29e520), action-effects-v9 (StatusIconSpeaking_End), screen-opening-v9 (LocationTextBGIn/Out Slide animations), marker lookup + RequiresUnblocked, exact ApplyShift for the dash. Strict survey: 2 blockers left in the first-combat replay (VisualFxUseAttachedMarkerLocation; OnObstacleCollision CollisionSound) |
| 2026-10-18 19:45 | 672a8549 (build 28) | Device run: title → intro (95 fps, 11.5 ms p50) → room passes door cue, SetColor, RemoveFromGroup, SetAngle, ghost patrol; stops at end of intro speech: `StopAnimation` (RoomManager.lua:5516) → agent. Survey list behind it: StatusIconSpeaking_End export, RequiresUnblocked, LocationTextBGIn/Out |
| 2026-10-18 19:37 | ee045bdb (build 27) | Clean Android rebuild from the public repo layout; same content as 26. Survey (`--survey-blockers`): no script-level blockers before frame 1435; sole blocker = projectile marker lookup (HouseDoor02 `weapon_2_TRACKING`, no live primary) → agent |
| 2026-10-18 19:23 | cdd5e539 (build 26) | Installed: + audio-events-v19 (699/749 cues: AuraCharge, SwordLunge, bus automation, any-rate resample), RotateWithGravity (RotateWithGravitySystem jobs 0x180151740/8a0/ac0) |
| 2026-10-18 19:17 | b34dc266 (build 25) | Installed: + ghost patrol (obstacle Move → Obstacle::MoveTo 0x2c5bf0, NotifyOnStopped/CheckStopped 0x2797f0), hit-FX tail (SetZLocation/AddAngle). Host stop then: InvincibleHitSparkDynamic RotateWithGravity (fixed in 26) |
| 2026-10-18 19:08 | 128b61b9 (build 24) | Installed: + audio-events-v18 (670/750 cues incl. Athena VO), Thing AngleTowardTarget (0x74, Body+0x80). Host stop: ghost-patrol obstacle `Move` (Speed/EaseIn/EaseOut) → agent. Exact Action enum: `.local/reports/native-action-enum.json` |
| 2026-10-18 19:00 | d5a17888 (build 23) | Installed: + RemoveFromGroup, SetAngle/SetGoalAngle (Thing 0x72/0x73 + Unit override), combat pass 4 dash. Device confirmed build 22 stopped at RemoveFromGroup. Awaiting device run |
| 2026-10-18 18:47 | (build 22) | audio-events-v17 + action-effects-v8 + timed SetColor + camera shake installed; room loads in 3.8 s. Host replay of this build passes the door cue and timed SetColor, stops at `RemoveFromGroup` (GhostScripts.lua:274), now implemented (build 23 pending). Device screen not checked: the phone was in use |
| 2026-10-18 14:32 | 9eaad2ce | Title OK; room ~20 fps; world stops at `Thing.ImpactRotation` |
| 2026-10-18 14:53 | (build 6) | Lazy decode: room ~100 fps; stops at touchdown haptic |
| 2026-10-18 15:44 | 3ac2e5ee | Intro + full landing run; stops at HUD `BarShadow` (~23 s); 6.5–8.3 ms p50 |
| 2026-10-18 16:13 | 8583a06b | New 7-button layout + layout editor; audio 48 kHz, 0 underruns; 6.6 ms p50 |
| 2026-10-18 18:08 | (build 21) | Speech icon + door reward icons pass (animation coverage v7/v8); stops at audio cue `/Leftovers/SFX/DoorStateChangeRewardAppearance` (audio coverage agent) |
| 2026-10-18 17:50 | (build 20) | SetupAnimation landed; stops at door icon `EndOffsetZ` |
| 2026-10-18 17:23 | (build 17) | Past control handoff; stops ~0.5 s later at `StatusIconSpeaking.OffsetZ` (SetupAnimation option) — animation-coverage agent |
| 2026-10-18 16:53 | 9fbfb6d4 | HUD graphics + text ("50 / 50", "1 / 1") created; stops same tick at `NotifyWithinDistanceAny` (RoomManager.lua:5237), before control |

## Earlier implementation history (2026-10-18)

- Perf: lazy animation decode, 384 MB hashed GPU frame cache, resampler fast path.
- Aspect: uniform fit of the 1920x1080 script UI reference; overlays cover.
- Settings: Graphics / Display / Audio / Controls tabs; FPS counter; render scale; frame cap; vibration.
- Audio categories: original Master/Music/Fx/Dialogue → original FMOD buses (`AudioManager::LoadCategories`).
- Bloom wired in; failure screen shows exact reason; frame errors no longer close the app.
- Misnamed fields corrected: `ImpactRotation`→`SpinFactor`, `CancelOnTouchdown`→`ReloadOnTouchdown`.
- Landing: rumble, PostFire disable timers, base `Projectile::UpdateSelf`/`Detonate`/`OnDeath` (STRAIGHT),
  RubbleFall decal coverage, `"null"` animations, `DustPuffALarge`, `SetSoundSource`, script `Rumble`.

## Earlier completed workstreams (historical pack versions)

| Workstream | Owner | State |
|---|---|---|
| HUD graphics (screen-opening-v7, screen_animation) | agent | done |
| HUD text (CreateTextBox / ModifyTextBox / Parse, original-text-v6 fonts) | agent | done |
| NotifyOnPlayerInput (Player::HandleInput any-input flag) | main | done |
| World notifies/queries: NotifyWithinDistance(Any), GetAngle, IsAlive, IsHorizontallyFlipped, SortMode | agent | done (world now runs past control) |
| Animation coverage pipeline: derived export roots (758 action FX, 125 GUI), property-coverage review, SetupAnimation/SetAnimation, time/loop/shift model | agent | done |
| Controls remap screen (PC KeyMappingScreen semantics; live ControlBindings, swap-on-assign, capture, reset) | main | done (on device) |
| Original PC profile persistence: `Profile1.sjson` (ConfigOptions, non-default only) + `Profile1.ctrls` (RemappableControl, SGB1) with the original load gate | agent → main wired | done (verified on device across restart) |
| Combat pass 1: SwordWeapon + SwordWeapon2 fire natively (attach, StepMove, Projectile::Move, SetLocation); obstacle defaults root-cause fix (actors v5, NeedsLife etc.) | agent | done |
| Combat pass 2–3: SwordWeapon3 fires and its hit completes (OnWeaponFailedToFire, IsImmuneToForce, ApplyImpact forces, charge FX hooks) | agent | done |
| Combat pass 4: dash (BlinkWeapon fire/update, CalcValidLocationOnVector, InvulnerableEffect, SwapWeapon → SwordWeaponDash, dash-strike anim) | prior agent | done; original zero-displacement issue subsequently fixed by ApplyShift reconstruction |
| Combat pass 5: dash target==start root cause = Zagreus backed into a concave wall corner; ApplyShift order recovered (blink-weapon-contract.json) | agent | done (analysis) |
| Exact Unit::ApplyFullShift/ApplyShift wall push-out + Unit::OnObstacleCollision, shared by walking (UpdateMovement) and dash | prior agent | done: Rush slides 168 units along the wall (was ~4). Subsequent obstacle detonation gap fixed in c8b8378; strict first-combat replay passes |
| SetAngle / SetGoalAngle / AngleTowardTarget (Body+0x80 angle transition; Unit overrides → SetCompleteAngle/SetGoalAngle) | main | done (host replay passes SetAngle) |
| World obstacle Move (ScriptAction::Move 0x274230 → Obstacle::MoveTo 0x2c5bf0) + NotifyOnStopped — ghost patrol | agent | done (build 25) |
| RotateWithGravity (FX rotation from Speed/UpwardVelocity) | main | done (build 26) |
| Camera shake (ShakeScreen → Camera::Shake / OffsetHelper), Camera Shake setting | main | done (installed build 22) |
| Timed SetColor (RunScriptAction 0x6c: Body color helpers +0x200/260/2c0/320, MultiplyBase 0x2b76e0, timed SetBase) | main | done (host replay passes) |
| RemoveFromGroup (MapThing::RemoveFromGroup 0xce2a0, no draw-group promotion) | main | done (host replay passes) |
| Audio coverage pipeline (derived cue set) → audio-events-v19: 699/749 cues; bus/timeline automation, triggered instruments, any-rate resampler | agent | done. Left: 37 Skelly VO (DSP 9), 5 SFX (DSP 14/20), instrument loop_count/sustain points ignored |
| Animation multi-angle atlas FX (26 names, GetAngleNum) + FX fields (angles, HSV, alpha, radial offset, ActiveVisualFxCap…): action-effects-v8, field review passes 626/758 | agent | done. Next FX gaps: RequiresUnblocked (22, needs GridManager::IsLocationBlocked; blocks sword swing B crack), CancelOnOwnerMove (19), OwnerHasNoCollision (11), ReRandomizeOnLoop default true (not done) |
| Save/load: original SGB1 v16 format, Luabins 0.3 + LZ4 1.9.4, SaveCheckpoint, Continue rule (`native/analysis/save-contract.json`) | agent → main wired Continue button | done on Mac; device pending first real checkpoint |
| Touch layout, Call/Assist buttons, layout editor, dim during cinematics | main | done (on device) |
| Original default gamepad/keyboard bindings (`default-bindings-contract.json`), wired into engine; Shout=24/Assist=28 control indices (weapons v19) | agent → main | done (compiled; device pending gameplay) |

## App

- Launcher icon: original Hades.exe RT_GROUP_ICON (`native/tools/export_app_icon.py`, `app-icon-v1`).
- Android game category (`appCategory=game`) + GameManager config (performance/battery); Battery mode caps an uncapped frame rate at 60.

## Settings (all persisted; original ConfigOptions where they exist)

- Graphics: render resolution, frame cap, FPS counter, Bloom, Lighting (DrawLights), VSync (SwapInterval), Letterbox 16:9 (PC default on; Android default off).
- Display: brightness, camera zoom. Audio: Master/Music/Fx/Dialogue (original buses).
- Gameplay: God Mode (EasyMode), Damage Numbers, UI Animations, Speech Bubbles, Subtitles (via SetConfigOption + UpdateConfigOptionCache).
- Touch: size, opacity, handedness, floating stick, vibration, layout editor. Controller: detection, touch Auto/Always/Hidden, dead zone (0.225), controller rumble.
- Pending until the feature itself is reconstructed: Hero Occlusion silhouette, Gameplay Timer, Off-screen Indicators, Low-res (720p) assets. Camera Shake is already reconstructed (build 22).
  The PC game has no shadow-quality or ambient-occlusion options (2D; shadows are authored and drawn by LightAndShadow).

## Next milestones

1. Finish the RoomSimple01 reward path on host autoplay: ImpactFx consumer, encounter-end
   blur/frame, loot Use → Athena dialogue → UpgradeChoice boon selection (touch and
   controller input through the original screen handlers).
2. Original exit door → LoadMap of the script-chosen next map, using the external
   content store and per-map packs (no APK size ceiling, no fallback scene).
3. Every Tartarus map and encounter on host autoplay (`--start-map=`), then Megaera.
4. Full-game audio (all banks, all VO with subtitles) and the PC settings features
   withheld until their behaviour exists (occlusion silhouette, timer, indicators).
5. When the user asks for device work again: one slim APK + content bundle, install,
   verify hashes and play the same route on the Fold.

## Known issues

- Device "unable to enter" screen = first unreconstructed engine behaviour; read the reason with
  `adb -s $HADES_DEVICE_SERIAL logcat -d -s HadesNative HadesLua | grep -E "FAILED|BLOCKED"`.
- RoomOpening and RoomSimple01 are packed. Subsequent rooms require validated map packs and lifecycle coverage.
- ApplyShift uses documented portable polygon/grid ordering; actual unit-unit push and callbacks now run, with reached collision weapon/effect boundaries still explicit.
- ReRandomizeOnLoop=true remains explicitly unsupported in the reached action
  animation implementation. Other unrebuilt branches must retain loud diagnostics.
- Audio still has unsupported graphs/loop behaviour; inspect the selected receipt and actual runtime errors. The cast/pickup replay already passed; do not attribute its sound to the old unsupported Chorus boundary.
- Audio29 has836 events/1976 original samples and5 explicit unsupported cues
  (three graph failures and two Skelly subtitle ambiguities).
  Delayed-fuse/live-target specialized Instant initialization remains unsupported;
  grouped phase ordering and documented animation/audio adaptations need parity work.
- Current APK is2,453,881,955 bytes; ZIP32 packaging stays below4GiB minus1MiB.
- Preserve selected versioned packs, rollback APK and evidence across restart. Do
  not prune earlier packs indiscriminately: receipts can retain their provenance.

## Build/install reliability

Iteration5 crossed2GiB after adding the original enemy movie bank. Python's
conservative ZIP64 threshold produced an APK zipalign rejected. The packager now
uses a bounded ZIP32 append through the actual4GiB offset domain, rejects larger
payloads, and restores the Python threshold after close. Signed APK3e4ebc1d
(2,200,575,254bytes) builds/verifies successfully.

The wireless incremental installer reported Success, then the package was not
registered for device user0. Treat that attempt as failed. Use full installs
(`adb -s "$HADES_DEVICE_SERIAL" install --no-incremental -r --user 0 ...`) and verify
`pm path --user 0`, installed file size/hash and actual launch before claiming
success. Do not overwrite an APK still being served by an incremental installer.

Historical iteration5 install verification succeeded: device user0 package registration and installed
APK SHA-256 exactly match3e4ebc1dbc60036a8da2e42584d47541bf92fc49464bf7f815e6fd338d0957a2.
Actual launch and user-driven RoomSimple01/enemy rendering verified; see device row. Evidence:
`.local/reports/takeover-iteration5-installed-verification.json`.

Asset publication now uses a fresh validated directory per invocation. This
removes stale unreferenced files (including an accidental extra actor bank) and
preserves the previous published stage if validation fails. The canonical
assets-only run passes; the duplicate bank is absent from its output.
