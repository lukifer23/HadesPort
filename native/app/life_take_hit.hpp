#pragma once
#include <cstdint>
namespace hades {
// Arguments of sgg::LifeComponent::TakeHit VA1802b7cd0 (PDB 5588 bytes).
// Register/stack order: param_3 damage, param_4 source Thing (passed to
// ScriptManager::OnHit24d200 as the attacker Thing), param_5/param_6 extra
// multiplier flag/value, param_9 armor-miss vulnerability flag, param_11
// UsedTag, param_12 Weapon* (owner Unit via Weapon+0x30), param_13
// Projectile*, param_14 Effect*, param_15 EffectIsTriggered, param_16 impact
// XY, param_17 impact Z, param_18 impact angle (radians). The core in
// life_take_hit.cpp reconstructs the branches reached when Weapon, Projectile
// and Effect are all null (e.g.
// Unit::OnObstacleCollision2f6ce0 wall damage); the weapon/attacker/projectile
// branches stay explicit failures until the projectile/effect hit paths are
// routed here.
struct LifeTakeHitRequest {
    int32_t victim=0;
    float damage=0;
    int32_t source=0; // 0 = null source Thing
    bool applyExtraMultiplier=false; float extraMultiplier=1;
    bool armorMissVulnerability=false,usedTag=false,effectTriggered=false;
    float impactX=0,impactY=0,impactZ=0,impactAngle=0;
    bool hasWeapon=false,hasProjectile=false,hasEffect=false;
};
}
