# Original actor animations

`export_actor_animations.py` reads original hero SJSON/Bink/atlas/SGA identities,
independently decodes selected movie frames, and exports `actor-animations.bin`.
This data is private and derived from supplied game files. HAA1 does not change
or replace original movies, definitions, scene, or room animation packets.

All numbers are little endian. Header: `HAA1`, uint32 frameCount, clipCount.
Each frame has eight uint32 values (movie canvas width/height, crop x/y/width/
height, zero-based original movie frame, original angle), two float32 values
(original atlas origin x/y), then uint32 rawBytes, compressedBytes, CRC32,
followed by compressedBytes zlib bytes. The reader indexes and seeks past these
bytes; it never loads the whole bank. Crops retain one transparent border pixel
for linear sampling and preserve original alpha/color inside their bounds.

Each clip contains three length-prefixed UTF-8 strings (name, ChainTo,
MoveAnimation), uint32 originalAngles, exportedAngles, flags, slideCount, then
float32 worldScale, offsetX, offsetY, secondsPerTick. Follow with exportedAngles
uint32 original angle indices, then slideCount records: uint32 originalFrame1,
durationTicks; exportedAngles uint32 frame indices; one length-prefixed JSON
string of original event properties. Event strings are data, not commands.

Flag bits: Loop0, HoldLastFrame1, AllowRestart2, CancelOnOwnerMove3,
DisableOwnerWeapons4, DisableOwnerRotation5, DisableOwnerInput6,
DisableOwnerManualInteract7, OwnerHasNoCollision8, Book9. Boolean source
properties are retained with reviewed compiled constructor defaults; this is not
a general animation-default database.

Slide timing follows original DurationFrames/60, zero-duration skipping, and
one positive-duration transition per update at remaining<0.0001. Negative time
remainder carries forward. Book timing uses original explicit PlaySpeed or the
native DefaultDataPlaySpeed initializer value60. The caller supplies actual
modified time/speed multipliers. Original chain requests are returned to the
owner; the module does not invent an owner, dispatch FX/audio, or modify weapons,
collision, input, movement or animation-control state. It exposes the recovered
DisableOwnerWeapons property for the weapon's animationBlocksAttack input.

The selected v3 bank has 29 original clips, 6,119 selected source movie frames,
and 219,918,114 compressed frame bytes. Full cropped RGBA is 515,286,528 bytes;
largest individual frame is410,000 bytes. Original eight directions are an
explicit coverage reduction from full32/64 directions. The native frame cache
is bounded to8MiB and8frames; each decoded frame is capped at4MiB. Every inflate
checks exact input/output extent and CRC32. Next-frame/chain prefetch precedes
returning the current frame. The returned pixel span lasts until the next bank
sample/load/reset and must be uploaded synchronously.

`ActorAnimationImage::worldRect(clip)` preserves original canvas origin,
symmetric Bink atlas trim, crop and source unit/animation/atlas scale. Dynamic
actor scale is a relative multiplier because original unit scale is included.
`SceneRenderer::renderActor` accepts one ActorVisual with ground x/y, separate
z, frame, localRect and color. It retains the existing Standing10 insertion
approximation, and projects height by sqrt(3)/2. GPU storage grows to power-of-two
extents and streams active cropped pixels with frame-edge clamping. Existing
Player/HDH1 render API remains available for retained debug callers.

## Native facing companion

`export_actor_facing.py` writes `actor-facing.bin`, bound to the exact HAA1 hash.
Layout: `HAF1`,32raw bankSHA256,uint32clipCount,uint32LUTCount720,720float32
original inverse-isometric angle values; per clip uint32useIso,float32ownRadians,
float32angleOffsetRadians,int32angleNumOffset,uint32mirror,uint32ignoreOwner.
Total selected size3,620bytes. Runtime requires raw32 identity sidecar
`actor-animations-sha256.bin`. Missing or mismatched data fails explicitly.

The original helper quantizes radians to half-degree indices with native
float32 constant bits42e52ee6, signed truncation after +0.5, modulo720, and reads
the exact original table. Native GetAngleNum then rounds by half a source angle
interval, applies offsets/wrap/mirror. `selectedAngleForFacing` finally chooses
the nearest exported original angle. Thus native full-angle quantization is
recovered; eight-direction asset coverage remains an approximation. The caller
must supply the actual native owner facing, not screen-space joystick bearing.

Evidence, hashes, validation and remaining ownership/event/control limits are
in `native/analysis/actor-animation-contract.json`.
# Multiple banks and native actor visuals

Canonical `actor-animations.bin` alone auto-loads `actor-facing.bin` and its
canonical hash sidecar. Alternate image banks (`cinder-frames.bin`,
`spawner-frames.bin`) never borrow hero facing metadata from a shared asset root.
Custom facing metadata requires explicit companion and hash filenames in `load`.
One-angle banks select native angle0 without requiring a 720-angle companion.

`SceneRenderer::renderActors` calls its provider sequentially, uploading each
original image before requesting the next. HAA decoded spans must not be retained
across bank sampling. ActorVisual supplies actual immutable bank identity,
allocation lifetime, source angle, group/order and color. Reloading a bank at the
same address requires `clearActorTextures` with the GL context current. The GPU
frame cache shares identical original images across actors, bounds residency at
64MiB/512 frames and never evicts a frame used by the current draw. A rotated crop
center is projected around the original ground anchor, preserving source pivots.

Multi-actor insertion preserves the existing source group/SortIndex/Y ordering
approximation; it does not claim recovered native scene-graph SCC ordering. The
actual native SortFromOwner result can be supplied as sourceSortY. Supported
composition modes remain0/1/2/9/11; unsupported actor blend modes reject. Rendering
alpha/culling never writes native SkipUpdate or other gameplay state.

`effect_particle_visual.hpp` converts a live selected Laurel particle using its
original image, scale interpolation, Y shift, base/offset Z and rotation-heading.
Its caller must supply actual source owner/group/sort/color state. No guessed
white tint, marker or alternate animation is admitted by this helper.
