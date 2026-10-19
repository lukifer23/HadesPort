// ScriptAction::StopAnimation RVA 0x2632e0 (Action 71). Reads PreventChain,
// DestinationNames, DestinationIDs and Names (Group is not read). For each
// active destination Thing: every name -> AnimationManager::Cancel(name, thing,
// PreventChain); no names -> Cancel("", thing, false) then a primary-animation
// re-SetAnimation branch (0x1802634d0..0x180263724), which fails loudly here.
#include "lua_runtime.hpp"
#include "object_runtime.hpp"
#include <cstdio>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
bool ScriptRuntime::stopAnimationImpl(lua_State* L,std::string& error){
    if(!lua_istable(L,1)){error="StopAnimation requires argument table";return false;}
    for(const char* k:{"DestinationName","DestinationNames"}){lua_getfield(L,1,k);const bool has=!lua_isnil(L,-1);lua_pop(L,1);if(has){error="StopAnimation DestinationNames selection not reconstructed";return false;}}
    lua_getfield(L,1,"PreventChain");const bool preventChain=lua_toboolean(L,-1);lua_pop(L,1);
    // ScriptArgs::Names: Names has precedence over Name.
    std::vector<std::string> names;
    lua_getfield(L,1,"Names");
    if(lua_istable(L,-1)){for(int i=1,n=int(lua_rawlen(L,-1));i<=n;++i){lua_rawgeti(L,-1,i);if(lua_type(L,-1)==LUA_TSTRING)names.emplace_back(lua_tostring(L,-1));lua_pop(L,1);}}
    lua_pop(L,1);
    if(names.empty()){lua_getfield(L,1,"Name");if(lua_type(L,-1)==LUA_TSTRING)names.emplace_back(lua_tostring(L,-1));lua_pop(L,1);}
    std::vector<int32_t> targets;if(!resolveObjectTargets(L,true,targets,error))return false;
    for(const auto id:targets){
        const auto* o=findObjectSnapshot(L,id);if(!o||!o->active||!o->alive)continue;
        if(o->screenSpace){error="StopAnimation on a screen Thing not reconstructed";return false;}
        size_t matched=0;
        if(names.empty()){
            if(!actionEffects_.cancelAttached(id,"",false,matched,error))return false;
            error="StopAnimation without Names primary re-SetAnimation branch not reconstructed";return false;
        }
        for(const auto& name:names)if(!actionEffects_.cancelAttached(id,name,preventChain,matched,error))return false;
    }
    error.clear();return true;
}
int ScriptRuntime::stopAnimation(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->stopAnimationImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
