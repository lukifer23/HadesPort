#include "hit_arguments.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
using namespace hades;
static double num(lua_State* L,int i,const char* key){lua_getfield(L,i,key);assert(lua_isnumber(L,-1));double v=lua_tonumber(L,-1);lua_pop(L,1);return v;}
int main(){lua_State* L=luaL_newstate();assert(L);luaL_openlibs(L);lua_newtable(L);lua_pushinteger(L,100);lua_setfield(L,1,"Health");lua_newtable(L);
    NativeHitArguments h{{40001,"Enemy",false,false},{false,true,false,false,true,10,1,0.5f,1,1},false,false,true,true,3.1415927410125732f,7,100,{},{},{},{}};h.attacker=HitActorIdentity{40000,"PlayerUnit",false,false};h.weapon=HitWeaponArguments{"SwordWeapon",40000,"PlayerUnit",true,false};
    std::string error;assert(pushHitArguments(L,h,1,2,error)==HitArgumentsStatus::Ready);assert(lua_gettop(L)==3);assert(num(L,3,"DamageAmount")==10&&num(L,3,"ImpactAngle")==180&&num(L,1,"Health")==100);lua_getfield(L,3,"TriggeredByTable");assert(lua_rawequal(L,1,-1));lua_pop(L,1);lua_getfield(L,3,"AttackerTable");assert(lua_rawequal(L,2,-1));lua_pop(L,1);lua_getglobal(L,"global_triggerArgs");assert(lua_rawequal(L,3,-1));lua_pop(L,1);
    lua_getfield(L,3,"SourceProjectile");assert(lua_isnil(L,-1));lua_pop(L,1);
    h.attacker.reset();assert(pushHitArguments(L,h,1,0,error)==HitArgumentsStatus::Ready);assert(num(L,4,"AttackerId")==40000);lua_getfield(L,4,"MeetsEffectDamageMultiplier");assert(lua_isnil(L,-1));lua_pop(L,1);
    h.weapon.reset();h.projectile=HitProjectileArguments{"SwordArc",false,false,0,0,3,2};h.effect=HitEffectArguments{"ArmorBreakStun",true};assert(pushHitArguments(L,h,1,0,error)==HitArgumentsStatus::Ready);assert(num(L,5,"ProjectileVolley")==3);lua_getfield(L,5,"AttackerId");assert(lua_isnil(L,-1));lua_pop(L,1);
    const int before=lua_gettop(L);assert(pushHitArguments(L,h,0,0,error)==HitArgumentsStatus::VictimHasNoAttachedLua&&lua_gettop(L)==before);h.impactLocationZ=std::nanf("");assert(pushHitArguments(L,h,1,0,error)==HitArgumentsStatus::InvalidInput&&lua_gettop(L)==before);lua_getglobal(L,"global_triggerArgs");assert(lua_rawequal(L,5,-1));lua_pop(L,1);lua_close(L);
    std::cout<<"Original-shaped hit arguments preserve owner aliases, conditional fields and health ownership; synthetic observer passed\n";
}
