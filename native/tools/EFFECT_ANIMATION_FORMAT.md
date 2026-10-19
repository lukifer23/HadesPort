# Original Laurel animation initialization

`export_laurel_dependencies.py` traverses the exact original `Fx.sjson` source
from `LaurelCindersSpawner`, retaining declarations, source identity, raw records,
inheritance-merged properties and all 14 reachable definitions. Merged source is
not a replacement for native post-read processing: `ClearSlides`, Random choice,
and other leaf properties remain explicit source data.

`export_effect_initialization.py` writes a fresh output directory. `effects.lua`
returns a data table, read by the existing `readSourceValue` API. Its schema is
`HadesEffectInitialization1`. Top-level keys are `Definitions`, `NativeDefaults`,
`CompiledConfig` and `Texture`. `manifest.json` binds the source file, packages,
dependency export, JSON and Lua. Never replace a supplied package or existing
graphics/action packet.

The selected Constant texture is **the original** `Dev\blank_invisible` texel,
decoded from `Dev.pkg`: 1x1 RGBA `00 00 00 00`, atlas rect `[259,830,1,1]`, trim
`[4,4]`, original dimensions `[4,4]`. This is an invisible source-spawner image,
not a substitute for visible cinder particles.

`EffectAnimationWorld::loadCatalog` admits the data. `create(name, ownerId,
owners, random, handle, error)` admits only the two reviewed Constant spawners,
allocates real owned parent/child records, retains the actual owner lifetime,
initializes native Time/Distance components, and registers real allocation
lifetimes in the shared `AnimationRegistry`. Exact PDB enum is Book0, Slide1,
Constant2, ConstantNoUpdate3, Dataless4, Random5, Graft6, Text7. The public root
Create wrapper adds to the ordered manager vector after initialization; its
AddChild/CreateCustom child does not. `size()` is allocation count;
`managerMemberCount()` is a distinct count. Manager caps must use the complete
shared manager vector, including other actually admitted classes.
The owner provider must return actual world coordinates, Z, scale, angle and
primary-animation status. The RNG callback must use the actual native stream,
returning a double in `[0,1)`. Creation recursively initializes the child before
consuming the parent's distance interval random value. Provider failure after
allocation/RNG consumption faults the world; it must not be retried as if no
state was consumed. Destruction recursively removes owned child spawners;
emitted particles retain their separate lifetimes.

`update(modifiedSeconds, owners, markers, random, emissions, context, error)`
requires an explicit Time or Distance ECS phase and actual manager live-VFX and
frame counts. Original system registration orders Time before Distance; exact
general component iteration is not claimed. Context defaults 60 and 4 reproduce
the matching build's `VfxFrameCap` and `VfxManagerCapMultiplier`, rather than a
new aesthetic or performance cap. Source manager cap 200 therefore gates at 800.
The selected distance-spawner frame cap is 1. Source zero-previous-XY baseline,
1920 distance clamp, negative-time clamp, interval draws and linear interpolation
are retained. This module does not select the game's global RNG seed or clock.

`UseAttachedMarkerLocation="head_TRACKING"` belongs to the animation's location
binding. An actual marker provider is required when the owner's primary animation
exists; no guessed head offset is permitted. This differs from the separate
`VisualFxUseAttachedMarkerLocation` field in the native spawn routine.

`EffectEmissionProvider::prepare` must admit the actual selected Random/Slide/Book
initializer and particle motion **without mutation**, before this world consumes
timing or RNG. `create` must perform that initialization, native spawned-VFX setup
and live-manager admission, and return the real lifetime handle. `alive` provides
real emitted-particle liveness when required. A diagnostic callback is not game
particle execution. The current module intentionally rejects missing callbacks,
unsupported initializers and unsupported Constant source properties. The native
null-create branch (which still increments counters) is outside this strict
admission interface; it is not silently replaced with a fake particle.

`actor_marker.*` and `effect_particle.*` now cover selected original marker and
Laurel leaf data/math with separate explicit update phases; see their format
documents. The shared registry adapter checks actual current permanent-class
coverage and keeps canceled entries until original disposal. Full world phase,
visibility, sorting and renderer integration remain separate requirements.

`export_room_animation_registry.py` exports source-bound primary ownership
recipes; these do not allocate native objects. Use registry-v2; provisional v1
misnamed compiled default type3. Active map primaries are 755 dataless, 206 Book,
62 ConstantNoUpdate and 10 Constant, and contribute zero manager-vector members.
Nine Constant2 primaries carry TimeVFX components. `export_room_spawner_assets.py`
exports their three original images in a fresh HAA1 packet and an extended
catalog (`PrimaryRecipes`, `PrimaryGraphics`). `createPrimary` requires actual
owner/RNG/original decoded pixels and runs at the actual actor creation point.
Bat duration randomization precedes its VFX interval draw. No bulk post-startup
metadata seeding may stand in for source-order creation.

The selected nine Constant2 primaries have OnlyWhenVisible=true and Loop=false;
native OnAnimationSet initially leaves SkipUpdate absent. Later SubmitDraw and
DoneUpdating modify it through actual source guards. `setSkipUpdate` accepts that
actual result; it does not infer visibility from alpha or a replacement cull.
EntityManager.Query always excludes SkipUpdate before VFX component filtering.
Bat requires the real Duration/TimeRemaining phase via `setDurationRemaining` and
`durationSystemResolved`; expiry requires the original ChainTo primary bridge.
Dust/Bat/Ember leaf execution is not a Laurel alias and remains explicit until
its own source motion/color/nested-VFX/audio paths are implemented.
