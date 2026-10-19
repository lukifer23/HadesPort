#pragma once
#include "weapon_runtime.hpp"
#include "self_effects.hpp"
#include "speed_effects.hpp"
#include "effect_update.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace hades {
struct WeaponControlDefinition { std::string name; int32_t nativeIndex=-1; };
struct UnitWeaponLoadout {
    std::string unitName;
    std::vector<std::pair<std::string,std::string>> explicitInitialSlots;
};
struct WeaponCatalog {
    std::vector<WeaponDefinition> weapons;
    std::vector<ProjectileDefinition> projectiles;
    std::vector<WeaponControlDefinition> controls;
    std::vector<UnitWeaponLoadout> unitLoadouts;
    std::vector<std::string> knownMissingWeapons = std::vector<std::string>{std::string{},std::string{"null"}};
    std::vector<std::string> knownMissingProjectiles = std::vector<std::string>{std::string{},std::string{"null"}};
    bool missingWeapon(const std::string&)const;
    bool missingProjectile(const std::string&)const;
    const WeaponDefinition* weapon(const std::string&) const;
    const ProjectileDefinition* projectile(const std::string&) const;
    int32_t controlIndex(const std::string&) const;
};
// HWA1/2 remain supported; HWA3 adds verified namespace absences, needed
// for original unused definitions whose named projectile is absent in source.
bool loadWeaponCatalog(std::span<const uint8_t>,WeaponCatalog&,std::string& error);
// A bounded initialization profile, not a complete native WeaponData image.
bool loadWeaponCatalog(std::span<const uint8_t>,std::vector<WeaponDefinition>&,std::string& error);
// Projection only: no allocation/admission/World mutation. Parent supplies the
// actual mutable EffectData identity; an admitted effect gets its own lifetime.
bool projectWeaponEffectAdmission(const WeaponEffectDefinition&,uint64_t dataHandle,
    WeaponSelfEffect&,std::string& error);
struct WeaponEffectRuntimeData {
    int32_t nativeType=12;
    EffectAffectData base;
    EffectUpdateData update;
    std::optional<SpeedEffectData> speed;
    UnitStatusEffect statusPrototype; // no handle/unit or admitted state
    float tagCooldown=0;
};
// Initial supported factories are real SPEED0 and native UNKNOWN12->Tag.
// Other source types remain data and explicitly fail if actually admitted.
bool projectWeaponEffectRuntimeData(const WeaponEffectDefinition&,uint64_t dataHandle,
    WeaponEffectRuntimeData&,std::string& error);
}
