# Original weapon property bridge

The exact static contract and SHA-bound disassembly index are in `weapon-property-bridge-contract.json`. No target binary was executed. There are no owned REA sessions.

The portable method is:

```cpp
arsenal.setScriptProperty(weaponName, property, originalLuaScalarOrNil,
                          changeMode, dataValue, error);
```

`originalLuaScalarOrNil` is `std::optional<WeaponScalar>`: bool, double or text; absent means Lua nil/missing. Pass the original text `"null"`. The method normalizes that text only for `dataValue=true`, matching the original action's reread of Value in the false branch. `setProperty` remains the strict low-level typed setter.

Root bridge adaptation, with its existing Lua/table and active-World helpers:

```cpp
// SetWeaponProperty:
// WeaponName="", Property="", ValueChangeType="Absolute", DataValue=true.
// Select plural key if PRESENT, otherwise singular key, independently per kind.
auto ids = readIdList(hasKey("DestinationIds") ? "DestinationIds" : "DestinationId");
auto names = readStringList(hasKey("DestinationNames") ? "DestinationNames" : "DestinationName");
for (auto* actor : selectActualActiveMapThings(ids, names)) {
    if (!actor->hasNativeUnitComponents()) continue;
    auto* arsenal = actor->ownedArsenal();
    if (!arsenal || !arsenal->hasWeapon(weaponName)) continue;
    if (!arsenal->setScriptProperty(weaponName, property, originalLuaScalarOrNil,
                                   mode, dataValue, error)) failUnsupported(error);
}
// no return values

// GetWeaponDataValue/GetWeaponProperty:
auto ids = readIdList(hasKey("Ids") ? "Ids" : "Id");
auto names = readStringList(hasKey("Names") ? "Names" : "Name");
for (auto* actor : selectActualActiveMapThings(ids, names)) {
    if (!actor->hasNativeUnitComponents()) continue;
    auto* arsenal = actor->ownedArsenal();
    if (!arsenal || !arsenal->hasWeapon(weaponName)) continue;
    WeaponScalar value;
    bool ok = dataGetter ? arsenal->dataProperty(weaponName, property, value, error)
                         : arsenal->stateProperty(weaponName, property, value, error);
    if (!ok) failUnsupported(error); // explicit reconstruction coverage boundary
    return pushVariant(value);      // first qualifying owned weapon only
}
return pushNil();
```

The helper names above denote integration points, not additional implemented APIs. Native pointer/hash selection order remains unknown; do not call metadata-only query proxies live Units. Do not sort IDs and claim the native getter used that order.

Modes: Absolute=0, Multiply=1, MultiplyBase=2, Add=3, ReplaceIfNull=4. Original case-insensitive enum parser returns Absolute for unknown spelling. Numeric fields reject ReplaceIfNull. Float arithmetic rounds at float32 operations. Int Multiply uses roundf, while Add truncates its incoming number. The shared MultiplyBase helper reflects non-projectile source objects as generic GameData, which exposes only Name/Organization/InheritFrom. Numeric WeaponData MultiplyBase consequently adds zero, rather than multiplying the source catalog field.

`EffectName` is not read by any of those three actions. `SetEffectProperty` is a distinct action with Destination selectors. `GetEffectDataValue` is also distinct and reads the GLOBAL source weapon's matching effect, falling back to that weapon's source projectile effects. It has no live owner selector. These must not be redirected to per-instance WeaponData setters/getters.

The module supports known scalar data fields and a bounded set of real live state fields. Complex types, subtype replacement, projectile-copy mutation and effect execution remain explicit failures. Original native unknown-property failure can be silent; this reconstruction exposes missing coverage instead of returning invented values.
