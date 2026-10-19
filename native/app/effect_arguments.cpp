#include "effect_arguments.hpp"
#include <cmath>
extern "C" {
#include "lua.h"
}
namespace hades {
const char* nativeEffectTypeName(int32_t type){
    static constexpr const char* names[]={"SPEED","GRIP","DAMAGE_OUTPUT","DAMAGE_TAKEN","DAMAGE_OVER_TIME","DAMAGE_OVER_DISTANCE","DELAYED_KNOCKBACK","CHARM","WEAPON_FIRE","PROJECTILE_DEFENSE","TAG","INVULNERABLE"};
    return type>=0&&type<12?names[type]:"UNKNOWN";
}
bool pushEffectApplyArguments(lua_State* L,const EffectApplyArguments& a,int owner,std::string& error){
    if(!L){error="Effect callback requires actual Lua state";return false;}
    if(owner&&!lua_isnoneornil(L,owner)&&!lua_istable(L,owner)){error="Effect callback owner alias is not attached Lua table";return false;}
    if(a.ownerId==-1||a.ownerName.find('\0')!=std::string::npos||a.effectName.find('\0')!=std::string::npos||!std::isfinite(a.modifier)||!std::isfinite(a.duration)||!std::isfinite(a.elapsedTimeMultiplier)||!std::isfinite(a.unitElapsedTimeMultiplier)){error="Effect callback contains unsupported native identity/text/scalar";return false;}
    if(owner&&!lua_isnoneornil(L,owner))owner=lua_absindex(L,owner);else owner=0;
    lua_createtable(L,0,12);
    auto text=[&](const char* key,const std::string& value){lua_pushlstring(L,value.data(),value.size());lua_setfield(L,-2,key);};
    auto number=[&](const char* key,float value){lua_pushnumber(L,double(value));lua_setfield(L,-2,key);};
    auto boolean=[&](const char* key,bool value){lua_pushboolean(L,value);lua_setfield(L,-2,key);};
    text("EffectType",nativeEffectTypeName(a.type));text("EffectName",a.effectName);boolean("Reapplied",a.reapplied);
    const int32_t count=int32_t(uint32_t(a.numStacks)+uint32_t(1));
    lua_pushinteger(L,count);lua_setfield(L,-2,"Stacks");
    number("Modifier",a.modifier);number("Duration",a.duration);boolean("IsVulnerabilityEffect",a.isVulnerabilityEffect);number("ElapsedTimeMultiplier",a.elapsedTimeMultiplier);number("UnitElapsedTimeMultiplier",a.unitElapsedTimeMultiplier);
    text("name",a.ownerName);lua_pushinteger(L,a.ownerId);lua_setfield(L,-2,"triggeredById");
    if(owner)lua_pushvalue(L,owner);else lua_pushnil(L);lua_setfield(L,-2,"TriggeredByTable");
    lua_pushvalue(L,-1);lua_setglobal(L,"global_triggerArgs");error.clear();return true;
}
}
