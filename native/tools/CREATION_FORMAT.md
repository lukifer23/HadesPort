# Original room creation data

`export_room_creation.py` writes a new `creation.json` in schema
`hades-room-creation-v1`. It does not change v5 scene/animation files or create
runtime entities. Reproduce with:

```sh
python3 hades/native/tools/export_room_creation.py \
  --content hades/.local/game/base/Content \
  --output hades/.local/native-assets/opening-creation-v2
```

The output must be a new directory. `records` preserves original SGB1 v12
serialized ordinal, ID, raw fields, complete record byte range, and checksum.
It also exports verified leading field ranges. `definition_key` joins each
record to `definitions`; those definitions contain inheritance-resolved source
values, child-first inheritance chain, exact UTF-8 definition ranges, and
leaf-property provenance. All 119 exported definition ranges round-trip through
the parser; duplicate names, missing ancestors, cycles, and invalid attachment
links fail the export. Source arrays replace inherited arrays; nested dictionaries
merge. This matches the source data's intended override structure, but the full
native definition loader has not been reconstructed.

`raw_map.Active` and `raw_map.DoCreate` are separate. Matching native v12 loader
evidence shows DoCreate controls whether the following serialized fields exist;
a false byte still appends a constructor-default MapThing. Every RoomOpening
record has a true byte. The existing parser/export has been validated on this
all-present map, not on false-presence records elsewhere.

The classification is a static creation-plan input, not a claim that an object
is alive. It prioritizes attached-child status, then inactive, then range state,
then direct top-level. Runtime editing mode and existing player/frame/distance
state remain external inputs. Original GroupManager enumeration order is not
equated with serialized ordinal.

`trigger_on_spawn.value` resolves the original Thing field, or uses the exact
matching native constructor default true. A caller may still suppress callbacks.
BlankObstacle inherits false from InvisibleTarget; both definitions and their
provenance are always included. The v2 export additionally resolves the exact
matching compiled UnitDataDef.PlayerControlled default false. Its source value
remains null when absent; `player_controlled_provenance` distinguishes an inherited
source value from this native default. `_PlayerUnit` inherits true from source;
21 Breakable units use compiled false. The exporter verifies the exact DLL hash
before applying compiled defaults. Other absent compiled defaults remain absent
or null. Scale and geometry inputs stay separate from final component transforms.

Native CreateChildren uses reverse enqueue/pop-back for list-order depth-first
creation. It directly creates obstacles without child Active/range/DataType
checks in this function, removes inactive state, adds the child to its parent,
calls OnParentAdded, and processes grandchildren. All actual attached records in
RoomOpening are active obstacles with range activation disabled. Source attachment
lists, ancestry, and reverse IDs are all retained; no graph reordering is needed.

See `.local/reports/native-room-creation-recovery.json` for the source/evidence
ledger, native callback anchors, validation, initial-player boundary, and limits.
See `native-player-controlled-default-recovery.json` in that reports directory
for the v2 default's constructor-to-assignment instruction flow and exact file
byte ranges. v1 remains preserved with its original unresolved values.
