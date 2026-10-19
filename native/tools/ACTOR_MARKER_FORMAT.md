# Original actor markers

`export_actor_markers.py` reads original SGB1 `.sga` files and emits HMR1 in a
fresh directory. The manifest pins every original source and the unchanged HAA1.
All integers/floats are little endian. Strings are u32 UTF-8 byte length + bytes.

HMR1: magic, raw32 HAA1 SHA256, u32 datasetCount, clipCount. Each dataset has u32
trackCount, then each track has name, five u32 (firstFrame, lastFrame, width,
height, pointCount), followed by normalized float XY pairs. Each clip has name,
three u32 (datasetIndex, originalNumFrames, originalNumAngles), then three float
(sourceOwnScale, sourceOffsetX, sourceOffsetY).

Use `originalAngleForFacing`, not the selected eight-direction image index.
Track index is fullAngle*originalNumFrames+originalFrame1. Source track range
misses yield normalized zero with the original dimensions, as native Book does.
Missing track names reject; no invented head offset.

`applySourceTransform` derives native animation scale from original own scale,
actual own multiplier and actual body scale. Body scale already includes .7 and
the live body transition; apply it once. HAA1 worldScale includes atlas scaling
and is unsuitable for marker math. Zero actual scale is valid. Explicit resolved
state distinguishes unknown transforms. Original multi-angle Book drawAngle is
zero; single-angle branches require their actual native draw angle.

`location` returns native Vector2, not ground XYZ. It rotates normalized XY,
multiplies by original dimensions/2 and native animation scale, applies body-scaled
source offsets, subtracts actual Thing totalZ*.8660253882408142 in Y, then adds
actual shake. Source/native anchors and diagnostic limits are in
`native/analysis/actor-marker-contract.json`.
