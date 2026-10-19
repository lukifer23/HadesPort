# Original room graphics export

`build_graphics_scene.py` creates a **new** output directory from the preserved
`opening-v2` scene and the original, read-only game content. It resolves original
FX Book animations by SJSON inheritance and exact frame names. Each original
frame has a decoded PNG/RGBA file, atlas crop/origin/scale metadata and SHA-256 in
`animation-clips.json`. The exporter never substitutes generated artwork.

```
/opt/homebrew/bin/python3.13 hades/native/tools/build_graphics_scene.py \
  --content hades/.local/game/base/Content \
  --base hades/.local/native-assets/opening-v2 \
  --output NEW_DIRECTORY
```

`scene-affine.bin` is **HDS3**, a first-frame scene for host reconstruction.
Every integer and float is little endian; floats are IEEE binary32.

| Field | Layout |
|---|---|
| Header | ASCII `HDS3`, uint32 textureCount, uint32 spriteCount |
| Each texture | uint32 width, uint32 height, width × height × 4 RGBA8 bytes |
| Each sprite | 76 bytes, Python struct `<I8f4f4fII>` |
| Sprite texture | uint32 texture index |
| Sprite corners | 8 floats: TL.x/y, TR.x/y, BL.x/y, BR.x/y in original world space |
| Sprite UV | 4 floats: u0, v0, u1, v1; flips represented by reversed limits |
| Sprite tint | 4 floats: native RGBA color bytes / 255 (stored BGRA is converted by native loader logic) |
| Sprite material | uint32 original layer BlendMode, uint32 explicit Animation.AddColor |

Matching PDB identifies BlendMode Alpha=0, Opaque=1, Additive=2,
Multiplicative2x=3, Screen=4, AdditiveMerge=5, Multiplicative2xMerge=6,
ScreenMerge=7, ColorBurn=8, Overlay=9, DarkerColor=10, LighterColor=11,
Displacement=12, DisplacementMerge=13, Undefined=255.

The scene manifest retains source map byte ranges, raw HSV, skew, OffsetZ,
parallax, inherited Thing values, original group names and raw source atlas
pivots. Original map BGRA bytes are retained separately as ColorStoredBGRA;
REA LoadVersion12 Evidence proves the R/B conversion. Atlas GetOrigin
instructions prove inverse-scale and integer-truncated flip correction.
Default origin/shader selection remain provisional. V5 applies all 57 native
squash/stretch cases, including 14 non-axis cases. Native height projects by
-sqrt(3)/2*OffsetZ. Thing.Offset is added in world axes using Body scale,
independently of Animation.Scale and sprite rotation. PostRotateScale follows
native stretch and rotation. Group order uses recovered GroupManager
initialization; for this map it equals the original map_text array order.
The HDS3 packet does not currently include animation timing or HSV; the JSON
manifest and clip records do. Different frame crops can have different pivots,
so an animation reader must transform each frame using its own atlas metadata.

`coverage.json` partitions every actual room object into created/inactive/
invisible/static/book/unsupported/missing categories. It records missing frame
names and any unsupported texture format with source package location and XNB
hash. This is actual-content diagnostic output, not a synthetic coverage test.

`preview_graphics_scene.py` implements a host-only affine compositor. It uses
recovered HLSL HSV, AddColor, Overlay and LighterColor equations at Pillow's
8-bit precision. Per-object application instead of native offscreen group
compositing, shader selection and default origin interpretation
still need validation; the preview is not an authenticated original render.

`export_shader_sources.py` extracts original HLSL from DXBC SPDB / MSF7 named
streams without executing shaders. Its manifest maps every byte back to the
shipped shader's exact block ranges, stream index and SHA-256. The source is a
private derived artifact under `.local`, not bundled into the source repository.

Validate actual export bounds/hashes/coverage:

```
python3 hades/native/tools/validate_graphics_scene.py OUTPUT_DIRECTORY
```

`preview_host_trace.py` can overlay an actual recorded native host trace and
one original HDH1 hero pose on a saved scene preview. The overlay is diagnostic
and deliberately draws above the foreground; it does not simulate new gameplay.

## HDA1 animation companion

HDS3 is unchanged. Export a new companion directory:

```
/opt/homebrew/bin/python3.13 hades/native/tools/export_scene_animation.py \
  --scene hades/.local/native-assets/opening-fx-v5 --output NEW_DIRECTORY
```

All fields are little endian, with no alignment padding. Strings are UTF-8
bytes prefixed by uint32 byte length. `animations.bin` contains:

| Record | Layout |
|---|---|
| Header (44 bytes) | ASCII `HDA1`, uint32 clipCount, uint32 bindingCount, 32 **raw** bytes of bound HDS3 SHA-256 |
| Each clip | string name, uint32 frameCount, float fpsMin, float fpsMax, uint32 flags, uint32 loopStartFrameIndex |
| Each frame | uint32 originalFrameNumber, uint32 width, uint32 height, width × height × 4 straight RGBA8 bytes |
| Each binding after all clips | uint32 sceneSpriteIndex, uint32 clipIndex, uint32 frameCount, frameCount × 8 floats of TL/TR/BL/BR world quads |

Flags: Loop=1, LoopFramesOnly=2, RandomStartFrame=4, ReRandomizeOnLoop=8.
`loopStartFrameIndex` is zero-based into the packet clip. All selected original
clips start at source frame 1 and loop to packet index 0. DoorLocked is a
one-frame constant with fpsMin=fpsMax=0. Rivers/ripples/iso-fog/smoke use 30 fps,
GroundFog uses 24 fps, flames/glow retain random 30..40 fps, and hands retain
random 24..36 fps. No NumLoopFrames field exists in selected definitions or the
matching AnimationDataDef type.

Native BookAnimation::UpdateInternal checks the OR of mLoop and mLoopFramesOnly
before invoking Loop at the end frame. Thus wrapping on `flags & 3` is supported
for these positive-playback clips. Reverse playback, end-state changes, RNG
scheduling and the full LoopFramesOnly distinction in resetting other animation
properties are not implemented by this frame-only packet.

The debug profile is phase=0 and speed=fpsMin. Shared clip texture upload is
valid for this shared phase. Original random phase/speed would need per-instance
state and a suitable texture cache. Two hands variants also have source
angle/bob interpolation fields, retained as pending in the packet manifest.

Parse headers, retain each RGBA offset and skip pixels using bounded seek.
At draw time upload the selected clip frame and replace binding quads. Retain
HDS3 UV flips, tint, HSV companion, blend/AddColor and sprite order. Apply
bindings **before** inserting runtime hero/dynamic sprites. First-frame quads
are byte-identical to actual bound HDS3 records; validation checks the binary.

V5 has 245,190,248 pixel bytes; the largest individual frame is 1,010,592 bytes.
Streaming avoids a second full CPU copy. Keep frame allocation and aggregate
quad budgets explicit. Export bounds are 4096 frames per clip, 16 million pixels
per frame and finite geometry. The manifest retains definitions, hashes and
seek offsets for every frame.

## Recovered non-axis affine reduction

Use local image `(x,y)` relative to the drawable anchor before sprite rotation.
Let `k=SkewScale-1`, `c=cos(SkewAngle)`, `s=sin(SkewAngle)`, `h=sqrt(3)/2`.
Native mYOffset is `-Animation.GetScale()*Animation.mOffset.y`, zero for these
14 direct-texture cases. Native Flat vertices have `(X,Y,Z)=(x,2y,0)`;
Vertical vertices have `(x,0,y/h)`. Native scalar stretch reduces to:

```
zTerm = (mesh == Vertical) ? y/h : y
d = k * (c*x - s*zTerm + s*mYOffset)
x += c*d
y -= h*s*d
```

Then apply sprite rotation, PostRotateScale and drawable anchor, which already
includes world-axis Thing.Offset and height projection.
`validate_native_affines.py` independently reconstructs original 3D vertices,
stretch, camera/rotation/inverse-camera operations and projection for all 14
actual placements. V5 differs by at most 9.1e-13 world units. This validates the
affine reduction; it does not establish official renderer pixel equality,
lighting, procedural offsets or default origin selection.

## Original group composition companion (HGC1)

`export_scene_composition.py` binds the original RoomOpening group hierarchy to
unchanged v5 sprite records. It validates ChildGroups reversed traversal, parent
identity, each existing source layer/material and monotonically contiguous group
order. This does not regenerate HDS3 or HDA1.

`composition.bin` is little endian: ASCII `HGC1`, 32 raw scene SHA-256 bytes,
uint32 spriteCount, then uint32 nativeGroupIndex per source sprite. Total size is
`40 + 4*spriteCount`. Runtime checks the staged `scene-sha256.bin`, count, exact
length, bounded group IDs, group continuity/material and optional HDO1 layer keys.
Absence retains the previous rendering path exactly.

For the verified static source groups, modes9/11 now use the original software
route recovered from matching `World::Draw` and original shader sources:

| Mode | Group clear RGBA8 | Sprite accumulation | Fullscreen final |
|---|---|---|---|
| 9 Overlay | 128,128,128,128 | Premultiplied source-over | Original Overlay2D on all RGBA channels, opaque blend |
| 11 LighterColor | 0,0,0,255 | Premultiplied source-over | Original luminosity comparison returns full base/group RGBA, opaque blend |

Mode9 is **not** a per-sprite alpha interpolation. LighterColor chooses group on
equal luminosity. The group clear alpha participates in the final result. Group
rendering uses an RGBA8 framebuffer and preserves source sprite order, HSV,
AddColor and recovered ambient. It restores the caller's draw/read framebuffer.
Framebuffer storage resizes with the surface and is released on shutdown; a
failed initialize releases partially created scene resources before retry.

RoomOpening has 796 source sprites: group7 LighterColor spans source412..414,
and group14 Overlay spans733..736. Hero insertion remains at source Standing
layer10, so it splits neither composed group. Run boundaries are determined
again after insertion, rather than assuming unchanged absolute indices.

The original also has a `UseOverlayHardware` branch. Its compiled default is
not established here; this implementation deliberately selects the recovered
software path. This fixes these static groups, not the remaining world lighting,
shadow/mask/procedural rendering, all native group modes, or future dynamic group
mutations. The visible red/black artifacts are not claimed resolved.

## Original parallax companion (HPP1)

`export_scene_parallax.py` binds each unchanged source sprite to its original
positive parallax and ground origin. Packet layout is little endian: ASCII
`HPP1`, 32 raw scene SHA-256 bytes, uint32 spriteCount, then three float32 values
per source sprite: groundOriginX, groundOriginY, parallax. The selected packet is
9,592 bytes; 53 of 796 sprites have factors other than one. Runtime requires the
staged `scene-sha256.bin` identity and rejects invalid count, suffix, nonfinite
values and factors outside `(0,16]`.

Original `vertexHelpers.inc::calcPosition`, joined to matching native
`RenderMesh::Set`/`SetFlatOrVertical` origin writes, reduces under this fixed
orthographic projection to:

```
cornerXY += (groundOriginXY - viewCenterXY) * (1/parallax - 1)
```

The origin has native Z=0. Export removes height projection from the stored
drawable anchor and retains Thing.Offset. Apply this after animated frame quads,
before culling. Future actor movement must update both corners and origin.
Screen-space and other native camera matrices remain unsupported. Exact joins,
native defaults and limitations are in `native/analysis/parallax-contract.json`.

## Original atlas hull companion (HHL1)

`export_scene_hulls.py` exports original Tartarus/Fx atlas hulls without changing
the source scene, animation, or texture bytes. Little endian layout:

```
HHL1
32 raw scene SHA-256 bytes
32 raw animation SHA-256 bytes
uint32 textureCount, clipCount
textureCount polygons
for each clip: uint32 frameCount, then frameCount polygons
polygon: uint32 pointCount, then pointCount * (float32 u, float32 v)
```

The packet header is 76 bytes. Empty polygons use the original rectangle path.
Runtime checks both `scene-sha256.bin` and `animations-sha256.bin`, exact static
texture and HDA clip/frame counts, normalized finite points, complete extent and
explicit point/frame/vertex budgets. Current packet is 78,560 bytes, with 73/74
static textures and 1,058/1,069 animation frames retaining hulls.

The exact native `NormalizeHull` uses integer floor half-dimensions and float32
normalization/clamping. It retains signed area strictly between -1 and -0.05;
full rectangles, tiny hulls and invalid winding use rectangle fallback. Original
native `BuildMeshWithHull` emits a triangle fan. The portable renderer maps these
normalized UVs through the same affine HDS/HDA quad, accounting for original UV
flips before applying the world transform. The hero Bink clip retains its quad.
Matching PDB evidence confirms EnableHulls defaults true and original eligible
Flat/Vertical mesh construction selects this path. Source details and bounds are
in `native/analysis/atlas-hull-contract.json`.

Atlas hulls mainly exclude transparent atlas regions. They reduce overdraw and
subtle filtered edge pixels; they do not establish that broad black regions were
missing geometry. Native scene-graph ordering and all world effects remain
incomplete.

## Shared-renderer performance diagnostics

`SceneRenderer::stats()` exposes the last frame's source/visible sprite counts,
draw/group passes, upload bytes and vertex bytes. Conservative affine-quad AABB
culling retains source order and all group boundaries, including composition of
an entirely offscreen group. Clip frames stream into three preallocated images
per clip, avoiding writes into recently sampled storage. A shader image region
scales UVs and clamps to active-frame texel centers; static and hero textures
retain their existing coordinate paths. The selected ring uses 26,542,080 bytes.

`native/analysis/renderer_perf.cpp` measures the actual shared renderer with CGL
GPU timer queries and captures the final RGBA framebuffer. Serialized 360-frame
M3 Pro runs at 1600x1000 show atlas hull GPU mean 1.767 to 1.267 ms in the player
view, and 2.897 to 1.680 ms across the room. These are host measurements, not
Android performance guarantees. All 1,069 original clip frames were visited by
the final coverage run. ASAN/UBSan lifecycle checks include partial-initialize
cleanup, original group pixels, hero, resize/exposure, repeated shutdown and a
new GL context. Reports and the independent 59-atlas BC7 audit are indexed by
`native/analysis/renderer-performance-contract.json`.

Source primary frame adapter (2026-10-17)

`opening-primaries-v2/primaries.lua` binds actual owner recipes and definitions to
unchanged HDS3/HDA1 resources. `primary-child-frames.bin` is a separate HAA1 bank
of original door reward, urn/Torch/Dev marker and vignette atlas images. Its ticks
index images; source live components own playback. Per-resource `AtlasScale`
preserves both source axes, including the urn's nonuniform ratio.

Install `SceneRenderer::setSourceSceneProvider` after renderer initialization:

- `SceneFrameProvider(sourceSprite, optional<SceneFrameSelection>&, error)` selects
  actual HDA clip/frame (zero based) and allocation lifetime. Exact per-frame
  affine quads and hulls are retained. Provider mode bypasses the old shared
  phase-zero/minimum-FPS clock.
- `SceneSpriteProvider(sourceSprite, SceneSprite&, error)` runs after source frame
  geometry and before parallax. The owner adapter supplies actual live changes.
  `runtimeSuppressed` removes draw admission separately from alpha.
- `SceneSubmission(sourceSprite, error)` reports retained visible geometry added
  to the submitted GL vertex stream. Source `Obstacle::SubmitDraw` tests primary
  OnlyWhenVisible/type, then clears SkipUpdate on its entire child chain. Initial
  owner OnAnimationSet acts on the primary, not independently on each child.

ScriptRuntime forwards frame/submission and exposes an auxiliary lifetime
snapshot, source state, state-for-sprite and synchronous image sampling. Auxiliary
sampling fills only source frame/bank identity and atlas-local rectangle; final
location/scale/offset/color/group remains actual owner state. In particular, native
GetDrawLocation applies attached-body scale to animation offsets even where
ScaleFromOwner Ignore excludes that body scale from image geometry. Do not merge
these into one guessed scale.

The GPU frame cache remains bounded at64MiB/512textures. The lossless full original
1920x1080 vignette requires8,294,400RGBA bytes, so streamed HAA CPU frames/cache
are bounded at16MiB/8frames. Original packet bytes remain unchanged. See
`native/analysis/scene-primary-runtime-contract-v2.json` for evidence, diagnostic
Mac timings, ARM64 checks and unresolved source transitions/49ghost resources.

`first-action-effects-v1` independently retains11original first-sword/footstep
source definitions and60unique CRC-checked original images. The bounded matching
native deserializer proves ClearCreateAnimations clears inherited create vectors;
its read is additive, whereas the first Slides field replaces inherited slides.
These assets do not acknowledge effect execution: source marker, motion, light,
audio, child allocation and displacement composition require real providers.

The first sword/footstep closure is now an independent `HAA1` bank plus source
`action-effects.lua`. `first-action-effect-execution-contract.json` records the
actual HAA frame17/18 → HMR marker → TimeSpawner → original leaf/child/audio
validation. LightController allocations belong to manager1 without ManagerAdd;
this does not claim the native lighting/displacement shader pass is complete.

`screen-remembrance-v2` supplies a separate original720p `HAA1` stream bank,
`screen-animations.lua`, and the original `Backgrounds\rectangle_01` atlas image.
131 original frames are CRC checked. Source `RemBGIntro` plays10 of the movie's11
frames at24fps, chains to looping120frame `RemBG`. ScreenThing allocations use
actual public object IDs and shared lifetime allocation; the portable store
excludes world collision/physics. `ActorVisual.screenSpace` maps the original
1920x1080 script coordinates to the platform viewport, independent of camera.
The new bank is97,585,634bytes; bounded CPU frame cache remains16MiB.

The staged primary-properties default is now `hero-properties-v3` (HAP2 layout
unchanged). AirborneZ's absent source default is **100**, explicitly written by
native ctor180094273 after its zero-template copy. Historicalv2's0 inference is
superseded; the previous banks remain intact. It contributes only on an actual
Slide frame with `Airborne=true`, never generically to the Thing's Z.
