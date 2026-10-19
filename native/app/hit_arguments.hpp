#pragma once
#include <cstdint>
#include <optional>
#include <string>
struct lua_State;
namespace hades {
struct NativeDamageResult {
    bool hitObstacle,hitArmor,hitVulnerability,usedTagMultiplier,isCrit;
    float actualDamage,totalDamageMultipliers,armorMultiplier,vulnerabilityMultiplier,obstacleMultiplier;
};
struct HitActorIdentity {int32_t id;std::string name;bool isObstacle,isTraitor;};
struct HitWeaponArguments {
    std::string name;
    int32_t cachedOwnerId; // -1 means absent, exact native weapon fallback.
    std::string cachedOwnerName;
    bool dealsDamageOrControlEffect,isPerfectCharge;
};
struct HitProjectileArguments {
    std::string name;
    bool deflected,blinkWeaponActive;
    float additiveDamageMultiplier,impactVelocity;
    int32_t volley,fireIndex;
};
struct HitEffectArguments {std::string name;bool triggered;};
// This is the OUTPUT of real native collision/TakeHit reconstruction, never a
// request to calculate damage or subtract health. Supply every required scalar.
struct NativeHitArguments {
    HitActorIdentity victim;
    NativeDamageResult damage;
    bool isInvulnerable,invulnerableFromCoverage,meetsEffectDamageMultiplier,firstInVolley;
    float impactAngleRadians,impactLocationZ,distanceSquared;
    std::optional<HitActorIdentity> attacker;
    std::optional<HitWeaponArguments> weapon;
    std::optional<HitProjectileArguments> projectile;
    std::optional<HitEffectArguments> effect;
};
enum class HitArgumentsStatus { Ready,VictimHasNoAttachedLua,InvalidInput };
// Table indices must be ACTUAL attached owner tables in this VM, not copies or
// stand-ins. Missing attacker table becomes nil; missing victim skips dispatch.
// Ready leaves one fresh table on top AND publishes that exact table as original
// global_triggerArgs. Parent invokes each matching original OnHit callback with
// this table, preserving its reference across yields. Failure restores stack.
HitArgumentsStatus pushHitArguments(lua_State*,const NativeHitArguments&,
    int victimTableIndex,int attackerTableIndex,std::string& error);
}
