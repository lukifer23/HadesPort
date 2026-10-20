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

## Current checkpoint (reviewed 2026-10-20)

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

Current selected packs: opening-actors-v13, actor-simple-v4 via room-packs-v6,
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

### Workstream checkpoint

All previous repair assignments are complete; no build/install/replay worker is
running at this checkpoint. Agent sessions and PIDs are not restart dependencies.
The roles below are useful for assigning new work when evidence warrants it.

| Area | Previous owner | Completed boundary |
|---|---|---|
| Android deployment, lifecycle/clocks, diagnostics, documentation | main | Iteration12 installed and verified; final sanitizer pass; rollback preserved |
| AI/firing/markers, movie resources, timed audio | movement_controls | Mixed2950 and next-wave3100 strict integration passed |
| Asset closure, native Instant and color defaults | progression_coverage | Weapon25/action22/audio29 selected and validated |
| Pathing/interaction, child lifetime/rendering, text/targetability | door_runtime | Real next-wave child activation and AI/attack passed host integration |

At the documentation review, ADB lists no connected device. Reconnect and rediscover
its current transport after restart. The detailed restart steps, local identifiers,
artifact inventory and prioritized next actions are in ignored
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

1. Reconnect the device, verify the installed iteration12 package and collect fresh
   logs during user-driven combat. Confirm mixed attacks/cast/dash, four directions,
   enemy attacks/death and recovery after repeated actions.
2. Complete every original forced encounter wave, money pickup and Athena reward:
   loot → Use → dialogue → boon selection. Reconstruct the first reached native gap.
3. Let original scripts select the next map. Export its complete validated resource
   closure and verify the real transition; RoomSimple01 has no fixed third-room link.
4. Verify actual device save, restart and Continue without overwriting its profile.
5. Verify original controller/keyboard bindings with a physical controller, then
   expand later rooms/biomes and the full run/House loop.

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
