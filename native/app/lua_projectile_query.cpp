#include "lua_runtime.hpp"
#include "object_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string_view>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
// Exact case-sensitive native ProjectileData reflection vocabulary/type IDs,
// recovered at RVA e14b0. Grouping only saves table space; no key aliases.
int projectileReflectionType(std::string_view key) {
    { constexpr std::string_view keys[]={
        "ExpireIfFirstCollisionIsBounce","UnpauseResetLocation","RequireHitCenter","ClearOnAttackEffects",
        "CheckObstacleImpact","IgnoreOwnerAllegiance","DestroySpawnsIfBlocked","BeamStartAnimSkipsImpact",
        "PenetrateJumpTargets","SpawnsInheritGroups","DeflectProjectiles","ExpireWhenOutOfJumps",
        "CanBeProjectileDefenseDestroyed","AffectsFriends","IgnoreCoverageAngles","SkipSpawnsIfBlocked",
        "InvulnerableToProjectileDefenseDuringPause","UnlimitedObstaclePenetration","RoundDamageDown","DestroyOnReflect",
        "DangerDistance","TriggerHitSound","ClearWeaponBonusesOnHit","HideDetonateGraphic",
        "UseDetonationForProjectileDefense","MultipleObstacleCollisions","AutoAdjustForTarget","ImpactLineOfSight",
        "SpawnOnInvulnerableImpact","SpawnsKeepAngle","ImpactLineOfSightFromOwner","ReviveDormantSpawns",
        "UseStartLocation","AffectsEnemies","SpawnAtVictimLocation","OffsetFromOwnerZ",
        "CanHitWithoutDamage","AllowTargetInvulnerable","DetonateAtVictimLocation","ForceSpawnsToValidLocation",
        "InvulnerableToProjectileDefenseBeforeStart","FinalJumpToOwner","SpawnCapOnCamera","HasActiveBlinkTriggers",
        "DetonateOnTouchdown","AffectsSelf","ReflectOnObstacleImpact","ImpactTargetUnitOnly",
        "CanCrit","ResetCollisionOutsideDetonation","MultipleUnitCollisions","MultipleCollisionPerFrame",
        "ScaleGeometryAlongRange","DetonateAtTarget","CheckUnitImpact","ReflectOnUnitImpact",
        "UseArmor","ContinouslyRetarget","UnlimitedUnitPenetration","DetonateLineOfSight",
        "RequireUniqueTarget","DetonateLineOfSightFromOwner","ConsumeTriggerEffect","IgnoreNoCollisionAnimData",
        "FizzleOldSpawns","DrawAsBeam","AllowRepeatedJumpHit","ExcludeLastHitThing",
        "BounceWhenOutOfJumpTargets","MultiDetonate","RequireCharmable","StartFuseOnTouchdown",
        "AttachToOwner","DetonateOnImpact","IgnoreDodge","SetAnimRadialOffset",
        "SkipFireCancelIfFiring","ExpireWhenOutOfObstacleJumps","HomingIgnoreHits","ManuallySetTarget",
        "UseRadialImpact","SilentImpactOnInvulnerable","SpawnsEvenlySpaced","BeamDieOnTargetLost",
        "FollowOwner","JumpRequiresLos","TrackIn3D","ExpandBlastDuringDetonation",
        "MatchOwnerBody","UseVulnerability","CanBeReflected","FizzleOldSpawnsOnDetonate",
        "DieWithOwner","InheritOwnerElapsedTimeMultiplier","SkipAscent","GaussianSpawnDistribution",
    }; for(auto name:keys)if(name==key)return 1; }
    { constexpr std::string_view keys[]={
        "MaxDetonations","NumJumps","NumBounces","SpawnCap",
        "SpawnCount","NumPenetrations","SpawnOnKillDamageThreshold",
    }; for(auto name:keys)if(name==key)return 2; }
    { constexpr std::string_view keys[]={
        "ProjectileNumOpacityMultiplier","JumpDamageMultiplier","Gravity","SkyTime",
        "JumpScaleMultiplier","RandomSpawnOffset","BounceSpeedMultiplier","Speed",
        "ArcEnd","ReturnToOwnerSpeed","StartDelay","PenetrateFinalJumpUnitRangeModifier",
        "ImpactFxInterval","TotalFuse","ArmorDamageMultiplier","OnDeathImpactForce",
        "DamageRadius","SpawnSnapToNearestAngle","Damage","LifeSteal",
        "DamageHigh","MaxSpeed","MaxVictimZ","ReflectionSpread",
        "ProjectileDefenseRadius","ImpactFilterOpacity","InvulnerableImpactOwnerVelocity","DamageMultiplier",
        "BlastSpeed","SpeedMultiplierOfEnemyProjectilesInside","BlastDuration","DetonateArcStart",
        "ImpactVelocity","MaxAdjustRate","SpawnVelocity","IncreaseAdjustRateOfFriendlyProjectilesInside",
        "CharmDuration","DamageFalloff","ImpactUpwardVelocity","TimingMultiplierBonus",
        "CriticalHitMultiplier","DamageLow","DamageRadiusFalloff","DescentStartZ",
        "SpawnVelocityVariance","ActiveBlinkSpeedMultiplier","NoJumpTargetRandomSpread","VacuumStrength",
        "HollowBlastRadiusBand","Acceleration","Fuse","BeamDrawTime",
        "CounterImpactOnSelf","ReturnToOwnerAfterInactiveSeconds","VulnerabilityDamageMultiplier","VacuumDistance",
        "SpawnRadius","CriticalHitChance","DetonateScreenShakeSpeed","DetonateArcEnd",
        "JumpDamageMultiplierAddition","FlashBeforeExpireMax","ImmunityDuration","MinVictimZ",
        "AlphaOverride","Deceleration","AdjustRateAcceleration","Range",
        "LandingZ","DamageRadiusScaleX","SelfImpactVelocityMultiplier","SpawnsSpeedModifierScaleY",
        "DamageRadiusScaleY","SpinRate","ImpactVelocityCap","Heal",
        "ConsecutiveHitWindow","ImpactVelocityFalloff","ImpactFilterDuration","ArcSize",
        "FuseMin","SpawnOffsetY","FlashBeforeExpireDuration","SpawnUpwardVelocity",
        "SpeedVariance","MinRange","DamagePerConsecutiveProjectileHit","MoveInterval",
        "JumpRange","AscentEndZ","FuseMax","DamageBonusToImpactedUnit",
        "DamagePerConsecutiveHit","PlayerImpactScreenFxInterval","AllEffectsProcChance","SpawnChance",
        "DamageAddition","JumpSpeedMultiplier","LaunchAngle","MaxRange",
        "MaxJumpDamageMultiplier","ProjectileDefenseAngle","ArcStart","ActiveBlinkRangeMultiplier",
    }; for(auto name:keys)if(name==key)return 4; }
    { constexpr std::string_view keys[]={
        "SpawnGroup","PlayerImpactScreenFx","UnpauseAnimation","AddControlOnDeath",
        "AngleFromMarker","SpawnOnDetonate","TriggerEffect","StartAtMarker",
        "DescentGraphic","ReticleGraphic","DissipateGraphic","GroupName",
        "BeamEndFx","AddControlOnDeath2","UnpauseFx","StartFx2",
        "PerfectChargeGraphic","ImpactFx","AttachedPowershotAnimation","TrippedGraphic",
        "StartFx","FinalShotGraphic","ImmunityKey","AttachToMarker",
        "HitVulnerabilityFx","TipFx","SpawnOnDeath","ReflectFx",
        "CriticalFx","BeamStartupFx","DetonateGraphic","SpawnOnImpact",
        "InflictedDamageFx","OnDeathFiredByAnimation","CanBeProjectileDefenseDestroyedByName",
    }; for(auto name:keys)if(name==key)return 5; }
    { constexpr std::string_view keys[]={
        "DetonateSound","InvincibleImpactSound","ImpactSound","InflictedDamageSound",
    }; for(auto name:keys)if(name==key)return 8; }
    { constexpr std::string_view keys[]={
        "HomingAllegiance","UnitCollisionCheck","ObstacleCollisionCheck","SpawnType",
        "Type",
    }; for(auto name:keys)if(name==key)return 9; }
    if(key=="Name"||key=="Organization"||key=="InheritFrom")return 5;
    return 0;
}
std::string projectileQueryString(lua_State* L,const char* key) {
    lua_getfield(L,1,key);
    if(lua_isnil(L,-1)){lua_pop(L,1);return {};}
    if(lua_type(L,-1)!=LUA_TSTRING)throw std::runtime_error(std::string("GetProjectileProperty ")+key+" requires a string");
    size_t n=0;const char* s=lua_tolstring(L,-1,&n);
    if(n>65536||std::memchr(s,0,n))throw std::runtime_error("GetProjectileProperty string outside bounded native text subset");
    std::string result(s,n);lua_pop(L,1);return result;
}
// ScriptArgs plural-first IntList/StringList supports scalar or dense array.
// Native also has converting/named-table forms; do not silently drop them.
void projectileQueryIds(lua_State* L,std::vector<int32_t>& ids,std::vector<std::string>& names) {
    const auto list=[&](const char* plural,const char* singular,bool text){
        lua_getfield(L,1,plural);if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,1,singular);}
        const auto append=[&](int at){
            if(text){
                if(lua_type(L,at)!=LUA_TSTRING)throw std::runtime_error("GetProjectileProperty named selector needs a native string");
                size_t n=0;const char* value=lua_tolstring(L,at,&n);if(n>65536||std::memchr(value,0,n))throw std::runtime_error("GetProjectileProperty group name bounds");
                names.emplace_back(value,n);
            }else{
                if(lua_type(L,at)!=LUA_TNUMBER)throw std::runtime_error("GetProjectileProperty converting IntList form not reconstructed");
                const double value=std::trunc(lua_tonumber(L,at));if(!std::isfinite(value)||value<INT32_MIN||value>INT32_MAX)throw std::runtime_error("GetProjectileProperty signed32 ID bounds");
                ids.push_back(int32_t(value));
            }
        };
        if(lua_istable(L,-1)){
            const int table=lua_absindex(L,-1);size_t count=0;
            for(int i=1;;++i){if(i>100000)throw std::runtime_error("GetProjectileProperty selector count bound");lua_rawgeti(L,table,i);if(lua_isnil(L,-1)){lua_pop(L,1);break;}append(-1);++count;lua_pop(L,1);}
            lua_pushnil(L);while(lua_next(L,table)){
                const double key=lua_type(L,-2)==LUA_TNUMBER?lua_tonumber(L,-2):0;
                if(key<1||key>double(count)||key!=std::trunc(key))throw std::runtime_error("GetProjectileProperty sparse/named selector form not reconstructed");lua_pop(L,1);
            }
        }else if(!lua_isnil(L,-1))append(-1);
        lua_pop(L,1);
    };
    list("Ids","Id",false);list("Names","Name",true);
}
}

bool ScriptRuntime::getProjectilePropertyImpl(lua_State* L,std::string& error) {
    const int top=lua_gettop(L);
    try {
        if(!lua_istable(L,1))throw std::runtime_error("GetProjectileProperty requires the original argument table");
        std::vector<int32_t> selected;std::vector<std::string> groups;
        projectileQueryIds(L,selected,groups);
        const auto property=projectileQueryString(L,"Property"),weaponName=projectileQueryString(L,"WeaponName");
        for(const auto& group:groups){
            // The shared group projection covers ordinary active members.
            // A retained pending-disposal member needs native group-set
            // membership evidence; do not silently turn that case into nil.
            for(const auto& object:snapshotObjects(L)){
                const bool member=object.groups.empty()?object.group==group:std::find(object.groups.begin(),object.groups.end(),group)!=object.groups.end();
                if(!member||object.queryProxy||(object.active&&object.alive))continue;
                const auto* actor=actorWorld_.actor(object.id);
                if(actor&&actor->registered&&actor->entityHandle==object.entityHandle)
                    throw std::runtime_error("GetProjectileProperty named selector has pending-disposal native group membership outside recovered subset");
            }
            std::vector<int32_t> members;
            if(!activeMapThingGroup(L,group,members,error)){lua_settop(L,top);return false;}
            selected.insert(selected.end(),members.begin(),members.end());
        }
        std::sort(selected.begin(),selected.end());selected.erase(std::unique(selected.begin(),selected.end()),selected.end());
        const hades::ProjectileDefinition* projectile=nullptr;
        for(const auto id:selected){
            const auto* object=findObjectSnapshot(L,id);const auto* actor=actorWorld_.actor(id);
            // Native's 0x221 cast is a Unit cast, not a Life/visibility test.
            if(!object||object->queryProxy||!object->entityHandle||object->backend!=ObjectBackend::Actor||!actor||!actor->registered||actor->object!=object||actor->entityHandle!=object->entityHandle)continue;
            if(!actor->definition)throw std::runtime_error("GetProjectileProperty registered actor has no actual class definition");
            if(actor->definition->kind!="Unit")continue;
            if(!actor->arsenal)throw std::runtime_error("GetProjectileProperty actual Unit arsenal unavailable");
            std::string missing;const auto* weapon=actor->arsenal->weaponState(weaponName,missing);
            if(!weapon)continue;
            // Weapon ctor allocates ProjectileData at +0x68 even when its
            // validity byte +0x120 stays false. The native getter checks the
            // pointer, not that byte. This DTO has not serialized the default
            // storage, so treating !valid as native nullptr would invent nil.
            if(!weapon->validProjectileData)
                throw std::runtime_error("GetProjectileProperty WeaponName="+weaponName+" Property="+property+": native allocated default projectile storage not represented");
            if(projectile)throw std::runtime_error("GetProjectileProperty multiple qualifying owners need native hash iteration order");
            projectile=&weapon->projectileData;
        }
        if(!projectile){lua_pushnil(L);error.clear();return true;}
        const int reflected=projectileReflectionType(property);
        if(!reflected||reflected==6){lua_pushnil(L);error.clear();return true;}
        if(property=="Name"){lua_pushlstring(L,projectile->name.data(),projectile->name.size());error.clear();return true;}
        const auto value=projectile->fields.find(property);const auto type=projectile->fieldTypes.find(property);
        if(value==projectile->fields.end()||type==projectile->fieldTypes.end())throw std::runtime_error("GetProjectileProperty actual reflected member unavailable: "+property);
        const auto& v=value->second;const auto& t=type->second;
        if(reflected==1&&t=="bool"&&std::holds_alternative<bool>(v))lua_pushboolean(L,std::get<bool>(v));
        else if(reflected==5&&t=="string"&&std::holds_alternative<std::string>(v)){const auto& s=std::get<std::string>(v);lua_pushlstring(L,s.data(),s.size());}
        else if(reflected==4&&t=="float32"&&std::holds_alternative<double>(v)){
            const float n=float(std::get<double>(v));if(!std::isfinite(n))throw std::runtime_error("GetProjectileProperty nonfinite native float outside portable subset");lua_pushnumber(L,double(n));
        }else if((reflected==2||reflected==9)&&t=="int32"&&std::holds_alternative<double>(v)){
            const double n=std::get<double>(v);if(!std::isfinite(n)||n<INT32_MIN||n>INT32_MAX||n!=std::trunc(n))throw std::runtime_error("GetProjectileProperty invalid native int32 member");lua_pushnumber(L,n);
        }else throw std::runtime_error("GetProjectileProperty reflected type outside recovered scalar subset: "+property+" ("+t+")");
        error.clear();return true;
    }catch(const std::exception& detail){lua_settop(L,top);error=detail.what();return false;}
}
int ScriptRuntime::getProjectilePropertyAction(lua_State* L) {
    char message[2048]{};bool ok=false;
    try{std::string error;auto* self=runtime(L);ok=self&&self->getProjectilePropertyImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.empty()?"GetProjectileProperty runtime unavailable":error.c_str());}
    catch(const std::exception& detail){std::snprintf(message,sizeof(message),"%s",detail.what());}
    if(!ok)return luaL_error(L,"%s",message);return 1;
}
