# Projectile initialization integration

`projectile_runtime.cpp` is portable C++20 and requires existing weapon runtime types.
The expanded weapon runtime scalar projectile setter additionally calls the existing
`primitive_properties.cpp`, already included by both parent CMake targets.

1. After common Weapon::Fire, Gun::Fire must recompute its FromLocation using the
   fresh owner frame. Native common Fire callbacks may have moved the owner.
2. Parent allocates a real projectile component/lifetime handle. The handle is
   distinct from public Thing ID; native base/ARC/HOMING covered path keeps ID -1.
3. `ProjectileInitialization` starts with launch coordinates, actual owner ID,
   weapon name, original copied FireRequest, fire index and volley index. Source
   data is the owner's actual copied primary/spawned ProjectileData. Reacquire
   it with `WeaponArsenal::projectileData` after every callback.
4. Consume every yielded operation synchronously. `ThingInitialize` must run the
   actual Thing initializer, including subclass Defaults before Thing/Physics/
   graphics. Generic registered actor creation is not an equivalent substitute.
   Body alpha uses actual AdjustArgs `{target,start=1,duration,smooth=false,
   easeIn=0,easeOut=0,pingPong=false,phaseOffset=0}` with the alpha helper.
5. Each RequestRandomDouble consumes exactly one existing VM/global native RNG
   draw and passes it on the next advance. Speed variance consumes a draw even
   when zero. Fuse range has a separate draw when both bounds are non--1.
6. `BeforeWeaponModifiers` is a boundary, not completion. Instantiate
   `ProjectileInitializationTail` with the prefix state and same request.
   Supply fresh actual firing WeaponSnapshot and owner fields:
   UnitData.DamageOutputMultiplier (+29c), ProjectileSpeedMultiplier (+2a0),
   PerfectChargeBonus (+2f8), live Unit.mDamageOutputModifier (+3ec), actual
   IsPerfectCharge result. Boolean exists must be explicit.
7. Tail operations need real Body/geometry, FX, attachment, and pending-manager
   consumers. `CreateStartAnimation` calls original animation lookup even for
   empty/literal-null names; if it returns a real animation, perform
   PreferUnmodifiedTime. This is not permission to fabricate absent animations.
   `ApplyStartMarker` must resolve original owner animation marker (or native
   fallback location), update mSpawnLocation, add projectile Z*.8660254 to Y,
   then SetLocation. Tail target-zero fallback reads that mSpawnLocation.
8. `QueuePendingProjectile` is the native PROJECTILES_TO_ADD append; manager
   elapsed-time inheritance and owner membership copy occur afterward. No
   sorted-ID traversal, immediate ALL_PROJECTILES activation or OnSpawn is
   inferred. `PendingQueueSetupFinished` does not implement subsequent
   projectile movement/collision/damage/detonation/lifetime updates.

HOMING Defaults calls GetAdjustRateBonus over actual PROJECTILE_EFFECTORS.
An explicit empty effector collection may establish zero contribution; do not
assume a zero bonus once effectors exist. Child target-link copy is distinct
from initialization of a real homing target. Real target arbitration is not yet
supported by the cursor. ARC uses base initialization; its later behavior still
needs actual projectile subclass update implementation.

COW planning DTOs remain stable for issued plans, but native live projectiles
retain a pointer to the weapon's copied ProjectileData. Active updates must
resolve that current data identity; do not permanently freeze a planning DTO.

`setScriptProjectileProperty` supports the unfiltered scalar native helper.
Parent must handle actual live Unit/weapon selection, delayed invocation, and
skip invalid copied data just as the source action does. ProjectileName and
ExcludeProjectileName introduce a separate filtered route: primary matching is
case-sensitive and checks only ProjectileName, while spawned matching uses
comparei and both include/exclude. Do not send filtered calls through the
unfiltered helper. Type, DetonateChildGraphics, spawn-name dependency recopy,
and ThingData fallback fields currently fail explicitly.

Evidence and coverage: `projectile-initialization-contract.json`. Focused native
ASan+UBSan replay passed using player-weapons-v9 HWA3. This ran reconstructed C++
only and did not execute the original game or establish Android combat.
