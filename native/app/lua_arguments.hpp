#pragma once
#include <cstring>
extern "C" {
#include <lua.h>
}

namespace hades {
// LuaTableExtensions::Bool 0x180247fe0: actual booleans first; otherwise
// lua_isstring/lua_tolstring (including numeric carriers) and exact lowercase
// "false"/"true" comparisons at 0x180248110..145. Other values use fallback.
inline bool luaArgumentBool(lua_State* state,int table,const char* key,bool fallback=false) {
    lua_getfield(state,table,key);
    bool value=fallback;
    if(lua_type(state,-1)==LUA_TBOOLEAN)value=lua_toboolean(state,-1);
    else if(lua_isstring(state,-1)) {
        const char* text=lua_tostring(state,-1);
        if(std::strcmp(text,"false")==0)value=false;
        else if(std::strcmp(text,"true")==0)value=true;
    }
    lua_pop(state,1);
    return value;
}
}
