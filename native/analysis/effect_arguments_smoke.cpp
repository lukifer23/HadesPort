#include "../app/effect_arguments.hpp"
#include <cassert>
#include <iostream>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
}
int main(){
    lua_State* L=luaL_newstate();assert(L);lua_newtable(L);
    hades::EffectApplyArguments a;a.ownerId=40000;a.ownerName="Hero";a.effectName="ArmorBreakStun";a.duration=1;
    std::string error;assert(hades::pushEffectApplyArguments(L,a,1,error));
    lua_getfield(L,-1,"TriggeredByTable");assert(lua_rawequal(L,-1,1));lua_pop(L,1);
    lua_getfield(L,-1,"EffectType");assert(std::string(lua_tostring(L,-1))=="UNKNOWN");lua_pop(L,1);
    lua_getfield(L,-1,"Stacks");assert(lua_tointeger(L,-1)==1);lua_pop(L,1);
    lua_getglobal(L,"global_triggerArgs");assert(lua_rawequal(L,-1,-2));lua_pop(L,1);
    lua_getfield(L,-1,"SourceWeapon");assert(lua_isnil(L,-1));lua_pop(L,1);
    assert(hades::pushEffectApplyArguments(L,a,0,error));lua_getfield(L,-1,"TriggeredByTable");assert(lua_isnil(L,-1));lua_pop(L,1);
    a.type=0;assert(std::string(hades::nativeEffectTypeName(a.type))=="SPEED");
    const int top=lua_gettop(L);a.duration=__builtin_inff();assert(!hades::pushEffectApplyArguments(L,a,1,error));assert(lua_gettop(L)==top);
    lua_close(L);std::cout<<"effect arguments smoke passed\n";
}
