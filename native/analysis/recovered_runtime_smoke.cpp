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
        "assert(GetConfigOptionValue{Name='TooltipShowDelay'} == 0.5); "
        "SetConfigOption{Name='TooltipShowDelay',Value=0.125}; "
        "assert(GetConfigOptionValue{Name='tooltipshowdelay'} == 0.125); "
        "for _,value in ipairs({'0.5',false,{},function() end,math.huge,-math.huge,0/0,1e39}) do "
        "assert(not pcall(SetConfigOption,{Name='TooltipShowDelay',Value=value})); "
        "assert(GetConfigOptionValue{Name='TooltipShowDelay'} == 0.125); end; "
        "SetConfigOption{Name='TooltipShowDelay',Value=0.5}; "
        "assert(GetConfigOptionValue{Name='ProfileZoneUsecThreshold'} == 500); "
        "for _,value in ipairs({-2147483648,2147483647}) do "
        "SetConfigOption{Name='ProfileZoneUsecThreshold',Value=value}; "
        "assert(GetConfigOptionValue{Name='ProfileZoneUsecThreshold'} == value); end; "
        "for _,value in ipairs({-2147483649,2147483648,4294967295,0.5,'500',math.huge,0/0}) do "
        "assert(not pcall(SetConfigOption,{Name='ProfileZoneUsecThreshold',Value=value})); "
        "assert(GetConfigOptionValue{Name='ProfileZoneUsecThreshold'} == 2147483647); end; "
        "SetConfigOption{Name='ProfileZoneUsecThreshold',Value=500}; "
        "assert(not pcall(GetConfigOptionValue,{Name='EnabledDebugDisplays'})); "
        "assert(not pcall(GetConfigOptionValue,{Name='EasyMode'..string.char(0)..'junk'})); "
        "assert(GetConfigOptionValue{Name='DamageTakenMultiplier'} == 1); "
        "SetConfigOption{Name='DamageTakenMultiplier',Value=0.25}; "
        "assert(GetConfigOptionValue{Name='DamageTakenMultiplier'} == 0.25); "
        "SetConfigOption{Name='DamageTakenMultiplier',Value=1}; "
        "assert(GetConfigOptionValue{Name='DisableDamage'} == false); "
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
        "assert(type(randomgaussian)=='function' and randomgaussian(-1)==0 and randomgaussian(1)==0 and randomgaussian(64)==0); "
        "local function near(a,b) assert(math.abs(a-b)<1e-14,tostring(a)..' != '..tostring(b)) end; "
        "assert(randomseed(123,1)==1); "
        "near(randomgaussian(1),-0.24469125560083177); "
        "assert(random(1)==353602019/4294967296); "
        "assert(randomseed(999,1)==1); local normal=random(1); "
        "assert(randomseed(999,1)==1); near(randomgaussian(1),0.9840311346289089); assert(random(1)==normal); "
        "assert(randomseed(321,0)==0); local engine=random(0); assert(randomseed(321,0)==0); "
        "near(randomgaussian(0),-0.24469125560083177); assert(random(0)==engine); "
        "near(randomgaussian(1),0.8585312036874195); near(randomgaussian(1),1.072893315358657); "
        "assert(randomseed(456,-1)==2); near(randomgaussian(2),-0.24469125560083177); "
        "near(randomgaussian(1),-1.1036079050260648); near(randomgaussian(1),0.4032430416841491); "
        "assert(randomgaussian(3)==0 and randomgaussian(-1)==0); "
        "assert(randomseed(321,0)==0); local expected=random(0); assert(randomseed(321,0)==0); assert(math.random()==expected); "
        "assert(randomseed(321,0)==0); local expected=randomint(1,10,0); assert(randomseed(321,0)==0); assert(math.random(10)==expected); "
        "assert(randomseed(321,0)==0); local expected=randomint(-4,12,0); assert(randomseed(321,0)==0); assert(math.random(-4,12,63)==expected); "
        "assert(randomseed(321,0)==0); local expected=random(0); assert(randomseed(321,0)==0); assert(math.random(false,10)==expected); "
        "assert(randomseed(321,0)==0); local expected=randomint(1,10,0); assert(randomseed(321,0)==0); assert(math.random(10,false)==expected); "
        "assert(randomseed(123,1) == 1); "
        "assert(random(1) == 353602019/4294967296); "
        "assert(random(1) == 2452673855/4294967296); "
        "assert(randomseed(123,1) == 1); assert(randomint(1,10,1) == 10); "
        "print('RECOVERED_RUNTIME_SMOKE_OK native registered APIs, PCG, separate Gaussian state/spare/seed isolation, native math.random dispatch and typed config; no substitute imports/clocks')");
    if (result) std::fprintf(stderr, "%s\n", lua_tostring(state, -1));
    lua_close(state);
    return result ? 1 : 0;
}
