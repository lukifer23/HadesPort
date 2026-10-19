/*
 * Force-included only when compiling the pinned upstream Luabins 0.3 sources
 * against Lua 5.2.2. Upstream 0.3 targets the Lua 5.1 API; the original engine
 * shipped it against Lua 5.2. Recovered from EngineWin64s.dll:
 *   luaopen_luabins (0x1803933bc) = lua_createtable(L, 0, 2);
 *   luaL_setfuncs(L, {save, load}, 0); then _VERSION/_COPYRIGHT/_DESCRIPTION.
 * That is exactly luaL_newlib(L, R), so luaL_register maps to it. The global
 * "luabins" is installed by the engine's luaL_openlibs table
 * (0x1804a9150: ... debug, luabins, utf8) through luaL_requiref(..., 1).
 * lua_objlen in 5.2 is lua_rawlen (the engine imports lua_rawlen).
 */
#ifndef HADES_LUABINS_LUA52_COMPAT_H
#define HADES_LUABINS_LUA52_COMPAT_H
#include <lua.h>
#include <lauxlib.h>
#define luaL_reg luaL_Reg
#define luaL_register(L, name, R) luaL_newlib((L), (R))
#ifndef lua_objlen
#define lua_objlen(L, i) lua_rawlen((L), (i))
#endif
#endif
