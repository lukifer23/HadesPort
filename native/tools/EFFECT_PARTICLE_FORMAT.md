# Selected original Laurel leaves

`export_cinder_frames.py` exports 140 original atlas images shared by ten source
leaf definitions, using unchanged HAA1 layout, CRC-checked independent zlib frames
and original crop pivots. Nominal one-tick rows preserve source base DurationFrames;
live randomized Slide duration/Book speed must come from `EffectParticleWorld`.
The packet contains no simulated motion or baked random timings.

The full `prepare` overload verifies source handle, selector, Time/Distance kind,
actual owner lifetime/scale, native uniform and bounded PCG providers, actual owner
time fraction and actual ordered-manager admission before spawner mutation.
Bind `setRegistry(effectWorld.registry())`. `permanentRegistryCovered` checks
every current manager-vector CancelOverCap entry has a genuine particle lifetime
here, while nonpermanent root entries still contribute to total manager length.
Caller must separately prove all source creation paths register in this shared
store; it cannot certify absent world implementations. A cinder-only count in a
mixed room cannot satisfy completePermanentRegistry.
Failures after mutation/RNG consumption fault the world rather than pretend rollback.

Native selector consumes boundedPCG(5). Generic leaf initialization consumes float
draws for scale, velocity, rotation speed, heading speed, rotation, X and Y offset,
in that order. Slide then consumes boundedPCG(max-min); maximum is exclusive and
bound1 still consumes PCG. Book consumes an eighth float for play speed. Angular
source floats use the native float->double*pi/180->float conversion. Attach adds
owner heading, source Data.Angle, then SetupSpawnedVfx adds spawn/source angles.
Own random scale receives source GetScale; live owner inheritance stays separate.
Draw angle is rotation minus own heading.

Source system phases are exposed separately. There is no invented total update
schedule: original independent/dependent worker batches can overlap. Book lifetime
ends in UpdateInternal at frame>=29. Slide advances at most one positive duration
per PostUpdate and becomes done on the subsequent UpdateInternal after its last
slide. Actual manager disposal remains an explicit caller phase. Selected easing
uses recovered original interpolation, not replacement smoothstep.

Native permanent cap80 is separate from Laurel manager cap200*global multiplier4.
Reverse ordered existing permanent admission includes pending-canceled records.
Add/registration precedes SetupSpawnedVfx as in the source manager wrapper.
Missing permanent classes reject before RNG rather than bypass their cap.
Original owner sort/bounds/flasher/visibility and renderer multi-actor admission
must be supplied genuinely before world integration. See effect-particle-contract
and world-system-order-contract for evidence and remaining limits.
