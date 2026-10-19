#include "lua_runtime.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
// Android settings -> original ConfigOptions. Values go through the original
// Lua-facing SetConfigOption (recovered typed store, recovered_runtime.cpp)
// and then the original script UpdateConfigOptionCache() (RunManager.lua), so
// scripts observe them exactly as on PC.
bool ScriptRuntime::applyConfigOptions(const std::vector<std::pair<std::string,bool>>& options){
    if(!state_){error_="Config options require a Lua state";return false;}
    const int top=lua_gettop(state_);
    for(const auto& [name,value]:options){
        lua_getglobal(state_,"SetConfigOption");lua_newtable(state_);lua_pushstring(state_,name.c_str());lua_setfield(state_,-2,"Name");lua_pushboolean(state_,value);lua_setfield(state_,-2,"Value");
        if(lua_pcall(state_,1,0,0)!=LUA_OK){error_=std::string("SetConfigOption ")+name+": "+lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    }
    lua_getglobal(state_,"UpdateConfigOptionCache");
    if(lua_isfunction(state_,-1)){if(lua_pcall(state_,0,0,0)!=LUA_OK){error_=std::string("UpdateConfigOptionCache: ")+lua_tostring(state_,-1);lua_settop(state_,top);return false;}}
    lua_settop(state_,top);return true;
}
