// One host debugging smoke run: imports the actual supplied Random.lua unchanged.
#include "../app/recovered_runtime.hpp"
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    lua_State* state = luaL_newstate();
    luaL_openlibs(state);
    registerRecoveredRuntime(state);
    if (!configureRecoveredLanguage(state, "en")) return 3;
    // Only host harness dependencies: Random.lua's Import is intentionally
    // isolated here; production uses the real recursive importer and world clock.
    int result = luaL_dostring(state, "function Import() end; function GetTime() return 1 end");
    if (!result) result = luaL_dofile(state, argv[1]);
    if (!result) result = luaL_dostring(state,
        "assert(GetConfigOptionValue{Name='DebugRNGSeed'} == 0); "
        "assert(type(GetConfigOptionValue{Name='EasyMode'}) == 'boolean'); "
        "assert(GetLanguage{} == 'en'); "
        "SetConfigOption{Name='GamepadCursorControl',Value=true}; "
        "assert(GetConfigOptionValue{Name='gamepadcursorcontrol'} == true); "
        "SetConfigOption{Name='GamepadCursorControl',Value=false}; "
        "assert(GetConfigOptionValue{Name='GamepadCursorControl'} == false); "
        "local ok,why=pcall(GetConfigOptionValue,{Name='NotARealRecoveredKey'}); "
        "assert(not ok and why:match('not recovered')); "
        "RandomSeed(123); assert(GetGlobalRng().id == 1); "
        "assert(GetGlobalRng():Random() == 353602019/4294967296); "
        "assert(GetGlobalRng():Random() == 2452673855/4294967296); "
        "RandomSeed(123); assert(GetGlobalRng():Random(1,10) == 10); "
        "print('RECOVERED_RUNTIME_SMOKE_OK original Random.lua, seed123 stream1, PCG and typed config')");
    if (result) std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
    lua_close(state);
    return result ? 1 : 0;
}
