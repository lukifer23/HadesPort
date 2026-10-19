# Source-backed static obstacle collision

`export_native_collision.py` applies the selected original native polygon
transform to original RoomOpening obstacle points. It does not create actors or
supply a complete physics engine. The previous provisional exporter is preserved.

Run from `hades/` with the existing interpreter:

```sh
python3 native/analysis/recover_collision_defaults.py
python3.13 native/tools/export_native_collision.py \
  .local/native-assets/opening-fx-v5/scene-manifest.json \
  --creation .local/native-assets/opening-creation-v2/creation.json \
  --output .local/native-assets/opening-collision-v1
```

The output directory must be new. Original map and definition hashes are checked
before export. The exporter requires the exact original DLL SHA bound by the
recovered defaults report. Source files remain read-only.

`collision.bin`:

- Four bytes `HCG1`, uint32LE polygonCount.
- For each polygon: int32LE originalMapId, uint32LE pointCount.
- Then pointCount pairs of float32LE x,y in original world coordinates.

The actual packet is10,112bytes containing207 convex obstacle polygons. The JSON
contains every candidate, full original coordinates and source range, inherited
property provenance, resolved scalar defaults, transform inputs and explicit
unresolved reasons. Use `world_points_exact` only when `world_transform_exact`
is true. The old zero-angle predicate is retained solely for comparison; it is
not the new acceptance condition. The packet contains only accepted polygons.

The current source inventory has256 explicit obstacle collision polygons:
207 deterministic and49 with original random MinScale/MaxScale. Of the old209
accepted by the zero-angle filter,22 actually require RNG; of its47 exclusions,
20 are now resolved. Those20 comprise17 rotated invisible barriers and3 chains
whose default RotateGeometry=false leaves collision unrotated. Seven previously
accepted instances also gain their original inherited Thing.Scale multiplier.

The actual native order is canonical local polygon winding, world translation,
directional squash/stretch, fresh-body uniform scale, optional extent adjustments,
horizontal and vertical reflection, then negative Body.Angle rotation only when
Thing.RotateGeometry=true. Reflections reverse point order. Compiled defaults
include RotateGeometry=false, Scale=1, Min/MaxScale=0, StartAngle=0,
ExtentScale/RelativeExtentScale=0 and StopsUnits=true. The native SetupGeometry
reflection branches directly consult Body flip bits; they do not gate those
branches on the separate MirrorGeometry field.

The detailed address-bound contract is
`native/analysis/collision-transform-contract.json`; retained bounded native
reports and exact default store/assignment evidence are referenced there.
`native-collision-export-verification.json` records complete binary extent,
JSON/packet float equality, source eligibility, convexity and native winding.
Native-style float32 world arithmetic gives a maximum radial cancellation error
of0.0003663worldunits in the current export. Windows CRT/host sinf/cosf bit
identity and original runtime polygon snapshots have not been observed.

All candidates use actual map DoCreate/Active/Collision and resolved StopsUnits,
and only original obstacle DataType1. Inactive records, non-obstacles, nonblocking
Thing definitions and absent explicit shapes are excluded. Range-deferred,
random-scale, unsupported attachment, optional extent adjustment or degenerate
scale cases remain explicit failures. No unseen polygon or replacement collider
is invented. IDs are original map metadata IDs, not live obstacle handles.

The Android build packages these coordinates in `player-world.bin` as `HDP2`:
magic, five float32LE values (spawnX/Y, speed, acceleration, grip), uint32LE
hero-point count, hero XY float32LE pairs, uint32LE obstacle count, then each
obstacle's positive int32LE map ID, uint32LE point count and XY pairs. The reader
also accepts legacy `HDP1` without per-obstacle IDs. The hero polygon uses
original nonempty Thing.Points times Thing.Scale; RotateGeometry=false means
the map's facing angle does not rotate its collision shape. Touch release logs
include the last blocking original map ID. This packet still uses the
reconstruction's provisional polygon/axis-slide solver.
