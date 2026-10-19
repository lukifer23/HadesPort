#include "hit_arguments.hpp"
#include <cmath>
#include <initializer_list>
extern "C" {
#include "lua.h"
}
namespace hades {
namespace {
bool validText(const std::string& s){return s.find('\0')==std::string::npos;}
void integer(lua_State* L,const char* k,int32_t n){lua_pushinteger(L,n);lua_setfield(L,-2,k);}
void number(lua_State* L,const char* k,float n){lua_pushnumber(L,double(n));lua_setfield(L,-2,k);}
void boolean(lua_State* L,const char* k,bool n){lua_pushboolean(L,n);lua_setfield(L,-2,k);}
void text(lua_State* L,const char* k,const std::string& s){lua_pushlstring(L,s.data(),s.size());lua_setfield(L,-2,k);}
void alias(lua_State* L,const char* k,int index){if(index)lua_pushvalue(L,index);else lua_pushnil(L);lua_setfield(L,-2,k);}
}
HitArgumentsStatus pushHitArguments(lua_State* L,const NativeHitArguments& h,int victimIndex,int attackerIndex,std::string& error){
    if(!L){error="Native hit arguments require an actual Lua VM";return HitArgumentsStatus::InvalidInput;}
    const int top=lua_gettop(L);
    if(!victimIndex||lua_isnoneornil(L,victimIndex)){error.clear();return HitArgumentsStatus::VictimHasNoAttachedLua;}
    if(!lua_istable(L,victimIndex)|| (attackerIndex&&!lua_isnoneornil(L,attackerIndex)&&!lua_istable(L,attackerIndex))){error="Native hit owner reference is not an attached Lua table";return HitArgumentsStatus::InvalidInput;}
    victimIndex=lua_absindex(L,victimIndex);if(attackerIndex&&!lua_isnoneornil(L,attackerIndex))attackerIndex=lua_absindex(L,attackerIndex);else attackerIndex=0;
    if(!h.attacker&&attackerIndex){error="An attacker Lua table requires an actual attacker Thing";return HitArgumentsStatus::InvalidInput;}
    bool valid=h.victim.id!=-1&&validText(h.victim.name)&&std::isfinite(h.impactAngleRadians)&&std::isfinite(h.impactLocationZ)&&std::isfinite(h.distanceSquared);
    for(float n:{h.damage.actualDamage,h.damage.totalDamageMultipliers,h.damage.armorMultiplier,h.damage.vulnerabilityMultiplier,h.damage.obstacleMultiplier})valid=valid&&std::isfinite(n);
    if(h.attacker)valid=valid&&h.attacker->id!=-1&&validText(h.attacker->name);
    if(h.weapon)valid=valid&&validText(h.weapon->name)&&validText(h.weapon->cachedOwnerName);
    if(h.projectile)valid=valid&&validText(h.projectile->name)&&std::isfinite(h.projectile->additiveDamageMultiplier)&&std::isfinite(h.projectile->impactVelocity);
    if(h.effect)valid=valid&&validText(h.effect->name);
    const float degrees=float(double(h.impactAngleRadians)*0x1.ca5dc1a63c1f8p+5);
    if(!valid||!std::isfinite(degrees)){error="Native hit arguments contain unsupported identity, text or nonfinite scalar";lua_settop(L,top);return HitArgumentsStatus::InvalidInput;}
    lua_createtable(L,0,36);
    if(h.attacker){integer(L,"AttackerId",h.attacker->id);text(L,"AttackerName",h.attacker->name);alias(L,"AttackerTable",attackerIndex);boolean(L,"MeetsEffectDamageMultiplier",h.meetsEffectDamageMultiplier);boolean(L,"AttackerIsObstacle",h.attacker->isObstacle);}
    else if(h.weapon&&h.weapon->cachedOwnerId!=-1){integer(L,"AttackerId",h.weapon->cachedOwnerId);text(L,"AttackerName",h.weapon->cachedOwnerName);}
    if(h.weapon){text(L,"SourceWeapon",h.weapon->name);boolean(L,"DealsDamageOrControlEffect",h.weapon->dealsDamageOrControlEffect);boolean(L,"IsPerfectCharge",h.weapon->isPerfectCharge);}
    if(h.projectile){const auto& p=*h.projectile;text(L,"SourceProjectile",p.name);boolean(L,"ProjectileDeflected",p.deflected);boolean(L,"BlinkWeaponActive",p.blinkWeaponActive);number(L,"ProjectileAdditiveDamageMultiplier",p.additiveDamageMultiplier);integer(L,"ProjectileVolley",p.volley);integer(L,"FireIndex",p.fireIndex);number(L,"ImpactVelocity",p.impactVelocity);}
    if(h.effect){text(L,"EffectName",h.effect->name);boolean(L,"EffectIsTriggered",h.effect->triggered);}
    boolean(L,"IsInvulnerable",h.isInvulnerable);number(L,"DamageAmount",h.damage.actualDamage);boolean(L,"IsCrit",h.damage.isCrit);boolean(L,"HitArmor",h.damage.hitArmor);number(L,"ArmorMultiplier",h.damage.armorMultiplier);boolean(L,"HitVulnerability",h.damage.hitVulnerability);number(L,"VulnerabilityMultiplier",h.damage.vulnerabilityMultiplier);boolean(L,"HitObstacle",h.damage.hitObstacle);number(L,"ObstacleMultiplier",h.damage.obstacleMultiplier);number(L,"ImpactAngle",degrees);boolean(L,"InvulnerableFromCoverage",h.invulnerableFromCoverage);number(L,"ImpactLocationZ",h.impactLocationZ);boolean(L,"UsedTag",h.damage.usedTagMultiplier);boolean(L,"FirstInVolley",h.firstInVolley);number(L,"DistanceSquared",h.distanceSquared);boolean(L,"VictimIsTraitor",h.victim.isTraitor);
    // Script::run adds these common fields immediately before invoking the
    // original registration callback, retaining this same args table.
    text(L,"name",h.victim.name);integer(L,"triggeredById",h.victim.id);alias(L,"TriggeredByTable",victimIndex);
    lua_pushvalue(L,-1);lua_setglobal(L,"global_triggerArgs");error.clear();return HitArgumentsStatus::Ready;
}
}
