// Real Lua5.2 registered native API checks. Original-script integration is
// exercised separately by hades-debug; this driver supplies no fake imports/clocks.
#include "../app/recovered_runtime.hpp"
#include <cstdio>
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}
int main(int argc, char**) {
    if (argc != 1) return 2;
    lua_State* state = luaL_newstate();
    luaL_openlibs(state);
    registerRecoveredRuntime(state);
    if (!configureRecoveredLanguage(state, "en")) return 3;
    const int result = luaL_dostring(state,
        "assert(GetConfigOptionValue{Name='DebugRNGSeed'} == 0); "
        "assert(type(GetConfigOptionValue{Name='EasyMode'}) == 'boolean'); "
        "assert(GetLanguage{} == 'en'); "
        "SetConfigOption{Name='GamepadCursorControl',Value=true}; "
        "assert(GetConfigOptionValue{Name='gamepadcursorcontrol'} == true); "
        "SetConfigOption{Name='GamepadCursorControl',Value=false}; "
        "assert(GetConfigOptionValue{Name='GamepadCursorControl'} == false); "
        "assert(GetConfigOptionValue{Name='freeformselectwrapy'} == false); "
        "SetConfigOption{Name='FREEFORMSELECTWRAPY',Value=true}; "
        "assert(GetConfigOptionValue{Name='FreeFormSelectWrapY'} == true); "
        "SetConfigOption{Name='FreeFormSelectWrapY',Value=false}; "
        "assert(GetConfigOptionValue{Name='freeformselectwrapy'} == false); "
        "SetConfigOption{Name='ExclusiveInteractGroup',Value='Combat_Menu'}; "
        "assert(GetConfigOptionValue{Name='ExclusiveInteractGroup'} == 'Combat_Menu'); "
        "SetConfigOption{Name='ExclusiveInteractGroup',Value=nil}; "
        "assert(GetConfigOptionValue{Name='exclusiveinteractgroup'} == ''); "
        "SetConfigOption{Name='ExclusiveInteractGroup',Value='Combat_Menu'}; "
        "SetConfigOption{Name='ExclusiveInteractGroup'}; "
        "assert(GetConfigOptionValue{Name='ExclusiveInteractGroup'} == ''); "
        "assert(GetConfigOptionValue{Name='FullscreenEffectGroup'} == 'Vignette'); "
        "SetConfigOption{Name='FullscreenEffectGroup',Value=nil}; "
        "assert(GetConfigOptionValue{Name='FullscreenEffectGroup'} == ''); "
        "SetConfigOption{Name='FullscreenEffectGroup',Value='Vignette'}; "
        "for _,value in ipairs({false,17,{},function() end}) do "
        "local accepted=pcall(SetConfigOption,{Name='ExclusiveInteractGroup',Value=value}); "
        "assert(not accepted and GetConfigOptionValue{Name='ExclusiveInteractGroup'} == ''); end; "
        "for _,value in ipairs({'true',1,{},function() end}) do "
        "local accepted=pcall(SetConfigOption,{Name='FreeFormSelectWrapY',Value=value}); "
        "assert(not accepted and GetConfigOptionValue{Name='FreeFormSelectWrapY'} == false); end; "
        "assert(not pcall(SetConfigOption,{Name='FreeFormSelectWrapY',Value=nil})); "
        "local ok,why=pcall(GetConfigOptionValue,{Name='NotARealRecoveredKey'}); "
        "assert(not ok and why:match('not recovered')); "
        "assert(randomseed(123,1) == 1); "
        "assert(random(1) == 353602019/4294967296); "
        "assert(random(1) == 2452673855/4294967296); "
        "assert(randomseed(123,1) == 1); assert(randomint(1,10,1) == 10); "
        "print('RECOVERED_RUNTIME_SMOKE_OK native registered APIs, seed123 stream1, PCG and typed config; no substitute imports/clocks')");
    if (result) std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
    lua_close(state);
    return result ? 1 : 0;
}
