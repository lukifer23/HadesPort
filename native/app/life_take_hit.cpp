#include "lua_runtime.hpp"
#include "life_take_hit.hpp"
#include "hit_arguments.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include <lua.h>
}
// Shared sgg::LifeComponent::TakeHit VA1802b7cd0 core
// (.local/reports/native-projectile-life-take-hit-pseudo.txt, OnHit argument
// table ScriptManager::OnHit VA18024d200 .local/reports/native-rea-life-script-on-hit.json).
// This file owns the branches reached with a null Weapon (param_12), Projectile
// (param_13) and Effect (param_14). The projectile hit path in projectile_hit.cpp
// is the other consumer of the same native function and should converge here;
// until it does, a request carrying a weapon/projectile/effect fails loudly.
namespace {
constexpr float kTau=6.2831855f;
float wrapAngle(float a){return a-std::floor(a/kTau)*kTau;}
// sgg::arcDistance VA1801ea5f0: wrap both, d=max-min, min(2pi-d, d).
float arcDistance(float a,float b){a=wrapAngle(a);b=wrapAngle(b);const float lo=std::min(a,b),hi=std::max(a,b),d=hi-lo;return std::min(kTau-d,d);}
struct LuaTop {lua_State* L;int top;explicit LuaTop(lua_State* s):L(s),top(lua_gettop(s)){}~LuaTop(){lua_settop(L,top);}};
}
bool ScriptRuntime::lifeTakeHit(const hades::LifeTakeHitRequest& request,hades::NativeDamageResult& result,std::string& error){
    try{
    if(request.hasWeapon||request.hasProjectile||request.hasEffect)throw std::runtime_error("LifeComponent::TakeHit2b7cd0 weapon/projectile/effect branches are not routed through the shared core yet");
    for(float input:{request.damage,request.extraMultiplier,request.impactX,request.impactY,request.impactZ,request.impactAngle})if(!std::isfinite(input))throw std::runtime_error("Nonfinite LifeComponent::TakeHit input");
    auto* target=actorWorld_.actor(request.victim);
    if(!target||!target->registered||!target->object||!target->hasLife||!target->life.initialized)throw std::runtime_error("LifeComponent::TakeHit requires actual initialized victim Life");
    const auto prop=[&](int32_t id,const char* k)->const hades::SourceValue&{const auto* v=actorWorld_.currentProperty(id,k);if(!v)throw std::runtime_error(std::string("TakeHit original field unavailable: ")+k);return *v;};
    result=hades::NativeDamageResult{};
    // pseudo l.147-185: the HitGraphic/HitFx/HitSound presentation block runs
    // only with a Projectile (param_13) and no Effect; not reached here.
    float damage=request.damage;
    // pseudo l.186-188: PlayerControlled Life (+0x32) multiplies by the global
    // DamageTakenMultiplier configuration (DAT_1806a7f30).
    if(target->life.isPlayerControlled){float incoming=0;if(!configurationFloat("DamageTakenMultiplier",incoming,error))return false;damage*=incoming;}
    // pseudo l.189-: InvulnerableCoverage block requires a Projectile; skipped.
    // pseudo l.250-262: total = Life.mDamageTakenMultiplier(+0x50); the weapon-owner
    // obstacle multiplier block needs a Weapon.
    float total=target->life.damageTakenMultiplier;
    result.isCrit=false; // uStack_23c low byte = param_5, cleared when the main test fails
    result.actualDamage=damage;result.totalDamageMultipliers=total;result.usedTagMultiplier=request.usedTag;
    if(!(total!=0&&damage>0)){error.clear();return true;}
    result.isCrit=request.applyExtraMultiplier; // byte4 retains param_5
    bool primaryInvulnerable=false,noArmor=false,noCoverage=false;if(!primaryCombatFlags(request.victim,primaryInvulnerable,noArmor,noCoverage,error))return false;
    // pseudo l.266-300: partial ArmorCoverage(+0x48<2pi) and no primary NoArmor:
    // HitArmor set, cleared when impact==location or arcDistance(body angle,
    // impact angle+pi) > coverage/2, which also copies param_9 to HitVulnerability.
    // Without a Projectile HitArmor is then always cleared (pseudo l.301-303).
    if(target->life.armorCoverage<kTau&&!noArmor){
        float x=0,y=0;if(!actorWorld_.location(request.victim,x,y,error))return false;
        if((request.impactX==x&&request.impactY==y)||arcDistance(target->object->angle,request.impactAngle+3.1415927f)>target->life.armorCoverage*.5f)result.hitVulnerability=request.armorMissVulnerability;
    }
    result.hitArmor=false;
    const float distanceSquared=-1; // pseudo l.304-314: only a Projectile owner supplies it
    // pseudo l.~490-505: param_5 adds (param_6-1); damage *= total; roundf
    // (RoundDamageDown is read only through a Projectile).
    if(request.applyExtraMultiplier){total+=request.extraMultiplier-1;result.totalDamageMultipliers=total;}
    damage*=total;result.actualDamage=std::round(damage);
    if(!std::isfinite(result.actualDamage)||!std::isfinite(total))throw std::runtime_error("LifeComponent::TakeHit damage overflow");
    // pseudo l.~506 (FUN_180185a60 on World+0x3c8): World+0x3c8 Life-system dirty set insertion.
    if(!lifeSystem_.markDirty(request.victim,error))return false;
    // pseudo l.~507-525: no Projectile/Effect -> Thing::SetAnimation(DamagedAnimation,
    // false, true); OnDamagedSpin(+0x178)>0 raises Physics spin (no component here).
    if(!setActorAnimation(request.victim,prop(request.victim,"Life.DamagedAnimation").text(),error))return false;
    if(prop(request.victim,"Life.OnDamagedSpin").number()>0)throw std::runtime_error("TakeHit OnDamagedSpin Physics spin consumer not reconstructed");
    target=actorWorld_.actor(request.victim);if(!target||!target->registered||!target->object){error="TakeHit victim expired after DamagedAnimation";return false;}
    // pseudo l.~526-560: IsInvulnerable = primary OwnerInvulnerable || Life
    // IsInvulnerable; InvulnerableFromCoverage = coverage>0 && !primary
    // NoInvulnerableCoverage && arcDistance(body+offset, impact+pi) <= coverage/2.
    // These native reads follow SetAnimation2decb0. A damaged primary can
    // change OwnerInvulnerable/NoInvulnerableCoverage from the armor query's
    // old primary; retain the actual post-animation flags for OnHit.
    if(!primaryCombatFlags(request.victim,primaryInvulnerable,noArmor,noCoverage,error))return false;
    const bool invulnerable=primaryInvulnerable||target->invulnerable;
    const float coverage=target->life.invulnerableCoverage;
    const bool fromCoverage=coverage>0&&!noCoverage&&arcDistance(target->object->angle+target->life.invulnerableCoverageOffset,request.impactAngle+3.1415927f)<=coverage*.5f;
    // pseudo l.~700-725 (loop calling FUN_18024d200): ScriptManager::OnHit for an attached victim Lua table.
    {
        bool attached=false;{LuaTop guard(state_);pushObjectLuaTable(state_,request.victim);attached=lua_istable(state_,-1);}
        if(attached){
            hades::NativeHitArguments hit{};
            hit.victim={request.victim,target->object->name,target->definition&&target->definition->kind=="Obstacle",target->status&&target->status->isTraitor};
            hit.damage=result;hit.isInvulnerable=invulnerable;hit.invulnerableFromCoverage=fromCoverage;hit.meetsEffectDamageMultiplier=false;
            hit.firstInVolley=true; // uVar32 stays 1 without a Weapon
            hit.impactAngleRadians=request.impactAngle;hit.impactLocationZ=request.impactZ;hit.distanceSquared=distanceSquared;
            if(request.source){const auto* s=actorWorld_.actor(request.source);
                if(!s||!s->registered||!s->object)throw std::runtime_error("TakeHit source Thing expired");
                // OnHit OnHit pseudo l.95-160: AttackerId(+0x6c), name, table, AttackerIsObstacle (mask 0x121).
                hit.attacker=hades::HitActorIdentity{request.source,s->object->name,s->definition&&s->definition->kind=="Obstacle",s->status&&s->status->isTraitor};}
            const auto extra=[&](lua_State* L,std::string& e){
                const int destination=lua_gettop(L);pushObjectLuaTable(L,request.victim);const int victimTable=lua_gettop(L);int attackerTable=0;
                if(hit.attacker){pushObjectLuaTable(L,hit.attacker->id);attackerTable=lua_gettop(L);}
                const auto status=hades::pushHitArguments(L,hit,victimTable,attackerTable,e);
                if(status!=hades::HitArgumentsStatus::Ready){lua_settop(L,destination);if(e.empty())e="Actual OnHit attached table disappeared";return false;}
                lua_replace(L,destination);lua_settop(L,destination);return true;};
            const auto health=[&](){LuaTop guard(state_);pushObjectLuaTable(state_,request.victim);if(!lua_istable(state_,-1))return -1.;lua_getfield(state_,-1,"Health");return lua_isnumber(state_,-1)?double(lua_tonumber(state_,-1)):-1.;};
            const double before=health();
            if(!dispatchCallbacks("OnHit",target->object->name.c_str(),request.victim,extra)){error=error_;return false;}
            hades::log(hades::Severity::Info,"HadesNative","COMBAT_HIT frame=%llu victim=%d weapon=<none> source=%d nativeDamage=%.3f invulnerable=%d healthBefore=%.3f healthAfter=%.3f",(unsigned long long)worldFrame_,request.victim,request.source,result.actualDamage,int(invulnerable),before,health());
        }
    }
    // pseudo l.~780-840 (LAB_1802b91e9/LAB_1802b922c): victim Unit OnHitWeapon(UnitData+0x170) via GetWeapon,
    // unless HitArmor && OnHitWeaponRequiresMissedArmor; attack-disable gates
    // (timer/effects) unless CanFireWhileDisarmed; RequestFire83160(-1, zero,
    // null target, true, zero), the Unit.OnTouchdown overload.
    target=actorWorld_.actor(request.victim);
    if(target&&target->registered&&target->definition&&target->definition->kind=="Unit"&&target->arsenal&&target->status){
        const auto name=prop(request.victim,"OnHitWeapon").text();
        if(!name.empty()&&target->arsenal->hasWeapon(name)&&!(result.hitArmor&&prop(request.victim,"OnHitWeaponRequiresMissedArmor").boolean())){
            const auto* reaction=target->arsenal->weaponState(name,error);if(!reaction)return false;
            bool editing=false;if(!configOptionBool("EditingMode",editing,error))return false;
            const bool allowed=target->status->canAttack(editing,prop(request.victim,"ImmuneToStun").boolean(),false);
            const auto disarmed=reaction->definition.fields.find("CanFireWhileDisarmed");
            if(disarmed==reaction->definition.fields.end()||!std::holds_alternative<bool>(disarmed->second))throw std::runtime_error("OnHitWeapon CanFireWhileDisarmed unavailable");
            if(allowed||std::get<bool>(disarmed->second)){
                hades::WeaponFireRequest fire;fire.angle=-1;
                if(!collisionWeaponRequestFire(request.victim,name,fire,error))return false;
            }
        }
    }
    error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
