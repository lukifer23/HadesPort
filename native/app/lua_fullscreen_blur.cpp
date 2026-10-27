#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
// ScriptAction::Execute 0x180251910 jump table (index bytes RVA 0x254cc8,
// targets RVA 0x254a1c): ids 36/38/39 -> 0x180252396 (getDuration 0x180281bc0,
// getFraction 0x180281e50, fractionForDuration(id,fraction,duration)
// 0x18025cac0); id 160 AdjustFrame -> 0x180254015 (GetColor 0x180281f00,
// getDuration, getFraction, Camera::ShowVignetteFrame 0x180303d30).
// Delay/MinDelay/MaxDelay are consumed by the shared delayed-action wrapper.
std::array<uint8_t,4> luaTableColor(lua_State*,const char*);
namespace {
// LuaTableExtensions::Float 0x180247cc0 for a number field (absent -> fallback).
float argNumber(lua_State*L,const char*key,float fallback){lua_getfield(L,1,key);if(lua_isnil(L,-1)){lua_pop(L,1);return fallback;}int ok=0;const double v=lua_tonumberx(L,-1,&ok);lua_pop(L,1);
 if(!ok)throw std::runtime_error(std::string("Native fullscreen non-number argument path not reconstructed: ")+key);if(!std::isfinite(v)||std::abs(v)>1e7)throw std::runtime_error(std::string("Native fullscreen argument bounds: ")+key);return float(v);}
// LuaTableExtensions::Byte 0x180248160 number path: cvttsd2si, caller keeps the low byte.
uint8_t argByte(lua_State*L,const char*key){lua_getfield(L,1,key);if(lua_isnil(L,-1)){lua_pop(L,1);return 0;}int ok=0;const double v=lua_tonumberx(L,-1,&ok);lua_pop(L,1);
 if(!ok)throw std::runtime_error(std::string("Native color byte non-number path not reconstructed: ")+key);if(!(v>-2147483649.0&&v<2147483648.0))throw std::runtime_error(std::string("Native color byte outside cvttsd2si range: ")+key);return uint8_t(int32_t(v));}
// getDuration: MinDuration and MaxDuration both nonzero; getFraction:
// MinFraction or MaxFraction nonzero. Either draws thread-local
// Random::RandomDouble 0x180054f80: float(r)*(max-min)+min.
float ranged(lua_State*L,const char*field,const char*lo,const char*hi,bool both){const float a=argNumber(L,lo,0),b=argNumber(L,hi,0);if(both?(a!=0&&b!=0):(a!=0||b!=0)){double r=0;if(!nextRecoveredEngineRandom(L,r))throw std::runtime_error("Native ScriptArgs RandomDouble unavailable");return float(r)*(b-a)+a;}return argNumber(L,field,0);}
// ScriptArgs::GetColor 0x180281f00: Color key -> LuaTableExtensions::GetColor
// 0x1802496f0; else A=minss(maxss(Float(A,0),0),1)*255 truncated, then Byte B,
// G, R; packed (A<<24)|(B<<16)|(G<<8)|R.
uint32_t scriptColor(lua_State*L){
 lua_getfield(L,1,"Color");const bool has=!lua_isnil(L,-1);lua_pop(L,1);
 if(has){const auto c=luaTableColor(L,"Color");return uint32_t(c[0])|uint32_t(c[1])<<8|uint32_t(c[2])<<16|uint32_t(c[3])<<24;}
 float a=argNumber(L,"A",0);a=a>0.f?a:0.f;a=a<1.f?a:1.f;const uint8_t b=argByte(L,"B"),g=argByte(L,"G"),r=argByte(L,"R");
 return uint32_t(r)|uint32_t(g)<<8|uint32_t(b)<<16|uint32_t(uint8_t(int32_t(a*255.f)))<<24;
}
}
void ScriptRuntime::registerFullscreenBlurActions(){
 const char*names[]{"AdjustRadialBlurStrength","AdjustRadialBlurDistance","AdjustFrame"};
 for(int mode=0;mode<3;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,fullscreenBlurAction,2);lua_setglobal(state_,names[mode]);}
}
int ScriptRuntime::fullscreenBlurAction(lua_State*L){char message[2048]{};bool ok=false;{auto*self=runtime(L);std::string error;ok=self&&self->fullscreenBlurActionImpl(L,int(lua_tointeger(L,lua_upvalueindex(2))),error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.empty()?"Native fullscreen effect runtime unavailable":error.c_str());}if(!ok){lua_pushstring(L,message);return lua_error(L);}return 0;}
bool ScriptRuntime::fullscreenBlurActionImpl(lua_State*L,int mode,std::string&error){try{
 if(!lua_istable(L,1))throw std::runtime_error("Native fullscreen effect non-table ScriptArgs path not reconstructed");
 if(mode<2){const float duration=ranged(L,"Duration","MinDuration","MaxDuration",true);const float fraction=ranged(L,"Fraction","MinFraction","MaxFraction",false);return radialBlur_.adjust(mode==0,fraction,duration,error);}
 const uint32_t color=scriptColor(L);const float duration=ranged(L,"Duration","MinDuration","MaxDuration",true);const float fraction=ranged(L,"Fraction","MinFraction","MaxFraction",false);
 return vignetteFrame_.show(fraction,duration,color,error);
 }catch(const std::exception&e){error=e.what();return false;}}
bool ScriptRuntime::advanceRadialBlur(float appElapsed,std::string&error){return radialBlur_.advance(appElapsed,error);}
bool ScriptRuntime::fullscreenFramePresentation(hades::VignetteFrameDraw&out,std::string&error)const{
 // GameplayScreen::Draw 0x1801b0da2: ConfigOptions::DrawHealthFX gates only
 // the draw (the Camera helper always steps); World+0xc>0 holds on the live
 // room route that calls this.
 const int top=lua_gettop(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushstring(state_,"DrawHealthFX");lua_setfield(state_,-2,"Name");
 if(lua_pcall(state_,1,1,0)!=LUA_OK){const char*e=lua_tostring(state_,-1);error=e?e:"Native DrawHealthFX configuration failed";lua_settop(state_,top);return false;}
 if(!lua_isboolean(state_,-1)){error="Native DrawHealthFX must be boolean";lua_settop(state_,top);return false;}const bool enabled=lua_toboolean(state_,-1);lua_settop(state_,top);
 try{out=vignetteFrame_.draw();}catch(const std::exception&e){error=e.what();return false;}
 if(!std::isfinite(out.radius)){error="Native vignette radius nonfinite";return false;}
 out.visible=out.visible&&enabled;error.clear();return true;
}
