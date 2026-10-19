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

## Device (Galaxy Z Fold, `$HADES_DEVICE_SERIAL`)

| Date / time | APK sha256 (prefix) | Result |
|---|---|---|
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

## Fixed today (evidence in `.local/reports/`, contracts in `native/analysis/`)

- Perf: lazy animation decode, 384 MB hashed GPU frame cache, resampler fast path.
- Aspect: uniform fit of the 1920x1080 script UI reference; overlays cover.
- Settings: Graphics / Display / Audio / Controls tabs; FPS counter; render scale; frame cap; vibration.
- Audio categories: original Master/Music/Fx/Dialogue → original FMOD buses (`AudioManager::LoadCategories`).
- Bloom wired in; failure screen shows exact reason; frame errors no longer close the app.
- Misnamed fields corrected: `ImpactRotation`→`SpinFactor`, `CancelOnTouchdown`→`ReloadOnTouchdown`.
- Landing: rumble, PostFire disable timers, base `Projectile::UpdateSelf`/`Detonate`/`OnDeath` (STRAIGHT),
  RubbleFall decal coverage, `"null"` animations, `DustPuffALarge`, `SetSoundSource`, script `Rumble`.

## Active workstreams

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
| Combat pass 4: dash (BlinkWeapon fire/update, CalcValidLocationOnVector, InvulnerableEffect, SwapWeapon → SwordWeaponDash, dash-strike anim) | agent | done; replay runs 1700 frames. But dash target == start (Zagreus doesn't move) |
| Combat pass 5: dash target==start root cause = Zagreus backed into a concave wall corner; ApplyShift order recovered (blink-weapon-contract.json) | agent | done (analysis) |
| Exact Unit::ApplyFullShift/ApplyShift wall push-out (replaces axis-slide approximation) | agent | in progress |
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
- Pending until the feature itself is reconstructed: Camera Shake (ShakeScreen), Hero Occlusion silhouette, Gameplay Timer, Off-screen Indicators, Low-res (720p) assets.
  The PC game has no shadow-quality or ambient-occlusion options (2D; shadows are authored and drawn by LightAndShadow).

## Next milestones

1. Player control after landing (HUD + text) → first combo/dash on device.
2. Athena first boon (fresh save: `OpeningEmpty` is non-combat, so the reward spawns at once):
   loot spawn → USE interaction → Athena dialogue (portrait + text) → boon choice screen → doors unlock.
   Queued to start as soon as walking works; builds on the reconstructed text-box system.
3. Exit door → room transition → next room loading (multi-room asset pipeline;
   ship original BC7 blocks via `GL_EXT_texture_compression_bptc`, verified on the Fold).
4. Enemies and combat in the next room.
5. Save / restart / reload on device (Continue button wired; needs first room checkpoint).
6. ~~Gamepad/keyboard with the original default bindings~~ (done; verify on device with a controller).

## Known issues

- Device "unable to enter" screen = first unreconstructed engine behaviour; read the reason with
  `adb -s $HADES_DEVICE_SERIAL logcat -d -s HadesNative HadesLua | grep -E "FAILED|BLOCKED"`.
- Only the opening room's assets are packed; leaving the room needs the multi-room/BC7 pipeline.
- ApplyFullShift (dash into walls) uses the walking sweep, a labelled approximation.
- ReRandomizeOnLoop: engine default true, native loop never re-randomizes.
- 137 audio events unsupported (exporter reasons in the v17 receipt).
- APK ~1.2 GB (RGBA frames); BC7 pass planned.
- Old export dirs (audio-events-opening-v1..v16, earlier action-effects) can be pruned once v17/v8 are confirmed on device.
