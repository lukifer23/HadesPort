# Staged unit weapon initialization

`export_native_weapons.py` consumes the retained first-weapon export and the
independently recovered effect-normalization contract. It checks original DLL
and source identities, the exact first-definition initialization branch and
the normalized effect's source binding. Output must use a new directory.

```sh
python3 hades/native/tools/export_native_weapons.py \
  --source hades/.local/native-assets/first-weapon-v1/weapon.json \
  --normalization hades/native/analysis/weapon-effect-normalization-contract.json \
  --output hades/.local/native-assets/first-weapon-runtime-v1
```

`weapons.bin` uses little endian:

- `HWA1`, 32-byte first-weapon JSON SHA, 32-byte normalization contract SHA,
  32-byte original DLL SHA, uint32 definition count.
- Each definition starts with three strings: weapon name, literal projectile
  name, effect name. Strings are uint32 UTF-8 byte length followed by bytes.
- Six int32 values: weapon type, starting ammo, maximum ammo, clip size,
  effect type, effect trigger.
- Three float32 values: charge time, clip regeneration interval, initial cooldown.
- Four uint8 flags: reload on creation, reload on equip, default control,
  effect container resolved.
- Four control-name strings and seven bundled-weapon strings, in the native
  order recorded by the equip contract.
- uint32 unresolved-field count, then that many strings.

The current packet is440bytes and contains only ArmorBreakAttack. The reader
checks target identity, finite scalar values, flags, strings/counts, unique
names and complete consumption. Parsing is transactional. The build verifies
the packet and both provenance inputs against the adjacent manifest.

This is a bounded initialization profile, not a complete WeaponData image.
Known source effect parameters remain in the linked normalization contract;
only effect name/type/trigger are modeled in this component. Unknown sound cues,
projectile offset and remaining effect defaults/execution stay explicit.

The shared core stages empty arsenal ownership for22 source unit records after
original precreation. It does not create live Things or complete Unit::Init.
Actual initialization must still process each unit's definition weapons and
all actor components before registration, OnSpawn and World::Start.

`hades-debug ... --probe-staged-weapons` initializes ArmorBreakAttack on a
detached copy of the staged player arsenal. Its dump is labeled diagnostic;
no equipment or Lua mutation is carried into pending production components.
The normal Lua EquipWeapon action remains blocked until real active units and
the native selection/arsenal path are implemented. The original weapon is
equipped during OnAnyLoad, later than this component-only debug check.

The first supported branch retains ammo=-1, zero clips/timers, no controls,
no valid projectile and one partial ArmorBreakStun metadata entry with
Type=UNKNOWN12 and Trigger=Fire1. Duplicate equips are idempotent; independent
owners and definition copies do not share mutable state. Unsupported creation
branches fail explicitly. Firing, hit detection, damage, effects and combat
remain unimplemented.
