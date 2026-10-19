# Native weapon frame integration

Use `WeaponArsenalFrameExecution` for one actual unit's Arsenal update. Construct
it once per native frame with that unit's scaled delta. Rebuild
`WeaponArsenalFrame` after every yielded operation using the same actual live
unit and current source ContinuousWeapon/PriorityWeapon strings. The cursor
preserves native priority and creation-vector order; do not sort weapon IDs.

Call `advance(frame, reply, pureSupport, step, error)`. `pureSupport` validates
that an output's actual consumer exists and does not mutate the world. Consume
`step.operation` synchronously before resuming, including original Lua callbacks
and nested projectile initialization. Reacquire all live owner/data state.
Only send a random/projectile/animation result when the corresponding
`expectsRandomReply`, `expectsProjectileReply`, or `expectsAnimationReply` flag
is true. An animation-creation kind alone does not imply an ID reply: reload FX
in PostFire does not use the returned animation. A real absent animation lookup
returns an explicit zero only at an actual ID-reply boundary.

`FrameFinished` means this Arsenal frame's actual supported native routines
completed, not that the World frame completed. Projectile pending activation,
collision, effects, VFX, physics, animation and scripts retain their own phases.
A failed operation must not be acknowledged as complete. Keep the cursor/state
for debugging; do not invent child successes to cross an error.

The original source first Sword path is supported through charge, common Fire,
GUN creation barriers, inline FX, PostFire, combo control swap and release.
`WeaponReloadExecution` also provides native reload and onReloadFinished,
including the mandatory RNG draw when ReloadTimeMin > 0. Unit-status duration,
original aim-line UI, non-GUN/FireOnRelease and other explicit guards remain
real missing consumer branches, not successful no-ops.

Player control requests are a separate phase before Unit update. Native partial
requests persist: do not clear them on ordinary touch-up. The native weapon
Update cursor owns normal clearing/finalization. Standard Lua RequestFire and
manual input must target the actual owner's Arsenal; metadata proxies never
qualify.

Use `.local/native-assets/player-weapons-v12/weapons.bin` (HWA3) and its
`projectiles.lua` companion for effective original nested Thing data. The latter
returns `projectiles[name].thing`, `.thing_property_types`,
`.unresolved_thing_fields`, and `.source_thing`. All 113 omitted Thing definition
semantic defaults are recovered. SoundCue padding remains unmodeled. Source
Color.Opacity spelling is intentionally preserved for actual packed-color
conversion by the renderer.

After the full real base projectile initialization, call
`initialArcProjectileAngle` for ARC and retain that per-instance angle offset.
`planArcProjectileMove` supplies source-correct transform targets; it does not
perform collision, defense, UpdateAnimation, detonation or lifetime removal.
Native manager UpdateSelf returning true means remove; newly Unit-created
pending shots promote/update in the same World frame's later Projectile phase.

Evidence: `weapon-trigger-composition-contract.json`,
`projectile-thing-data-contract.json`, `arc-projectile-motion-contract.json`.
Focused ASan/UBSan runs use synthetic explicit consumers and do not establish
Android gameplay.
