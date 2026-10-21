#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
// ScriptAction::Execute (0x180251910) cases, jump table bytes RVA 0x254cc8:
//   38 AdjustRadialBlurStrength / 39 AdjustRadialBlurDistance -> 0x180252396:
//      getDuration 0x180281bc0, getFraction 0x180281e50, fractionForDuration 0x18025cac0.
//   160 AdjustFrame -> 0x180254015: GetColor 0x180281f00, getDuration, getFraction,
//      Camera::ShowVignetteFrame 0x180303d30.
namespace {
// LuaTableExtensions::Float semantics for a number field (absent -> fallback).
float argNumber(lua_State*L,const char*key,float fallback){lua_getfield(L,1,key);int ok=0;const double v=lua_tonumberx(L,-1,&ok);lua_pop(L,1);if(!ok)return fallback;if(!std::isfinite(v))throw std::runtime_error(std::string("Native fullscreen argument nonfinite: ")+key);return float(v);}
bool argPresent(lua_State*L,const char*key){lua_getfield(L,1,key);const bool v=!lua_isnil(L,-1);lua_pop(L,1);return v;}
// LuaTableExtensions::Byte 0x180248160: number -> truncation, string -> strtof
// truncation, anything else (including absent) -> 0.
uint8_t argByte(lua_State*L,const char*key){lua_getfield(L,1,key);int ok=0;const double v=lua_tonumberx(L,-1,&ok);lua_pop(L,1);if(!ok)return 0;if(!std::isfinite(v))throw std::runtime_error(std::string("Native color byte nonfinite: ")+key);return uint8_t(int(v));}
// ScriptArgs::getDuration 0x180281bc0: MinDuration and MaxDuration both
// nonzero -> RandomDouble x (max-min) + min, else Duration (default 0).
// ScriptArgs::getFraction 0x180281e50: MinFraction or MaxFraction nonzero ->
// the same draw, else Fraction (default 0). Both draw from the engine's
// thread-local Random (TLS +8, Random::RandomDouble 0x180054f80), the
// generator nextRecoveredEngineRandom reconstructs.
float randomBetween(lua_State*L,float lo,float hi){double r=0;if(!nextRecoveredEngineRandom(L,r))throw std::runtime_error("Native ScriptArgs RandomDouble unavailable");return float(r)*(hi-lo)+lo;}
float duration(lua_State*L){const float lo=argNumber(L,"MinDuration",0),hi=argNumber(L,"MaxDuration",0);if(lo!=0&&hi!=0)return randomBetween(L,lo,hi);return argNumber(L,"Duration",0);}
float fraction(lua_State*L){const float lo=argNumber(L,"MinFraction",0),hi=argNumber(L,"MaxFraction",0);if(lo==0&&hi==0)return argNumber(L,"Fraction",0);return randomBetween(L,lo,hi);}
// ScriptArgs::GetColor 0x180281f00 -> packed RGBA. Color key: LuaTableExtensions
// ::GetColor 0x1802496f0 ({r,g,b[,a]}, unit-range rescale); otherwise R/G/B
// Byte plus A = clamp(Float(A,0),0,1) x 255 truncated.
uint32_t color(lua_State*L){
 if(argPresent(L,"Color")){
  std::array<uint8_t,4> out{255,255,255,255};lua_getfield(L,1,"Color");
  if(lua_istable(L,-1)){double c[4]{0,0,0,1};for(int i=0;i<4;++i){lua_rawgeti(L,-1,i+1);int ok=0;const double v=lua_tonumberx(L,-1,&ok);if(ok){if(!std::isfinite(v))throw std::runtime_error("Native Color channel nonfinite");c[i]=v;}lua_pop(L,1);}
   const bool unit=c[0]>=0&&c[0]<=1&&c[1]>=0&&c[1]<=1&&c[2]>=0&&c[2]<=1;
   for(int i=0;i<4;++i)out[i]=uint8_t(unit?int(std::clamp(c[i],0.,1.)*255.):int(std::clamp(c[i],0.,255.)));}
  lua_pop(L,1);return hades::packRgba(out);
 }
 const float a=std::min(std::max(argNumber(L,"A",0),0.f),1.f);
 const uint8_t b=argByte(L,"B"),g=argByte(L,"G"),r=argByte(L,"R");
 return hades::packRgba({r,g,b,uint8_t(int(a*255.f))});
}
}
void ScriptRuntime::registerFullscreenBlurActions(){
 const char* names[]{"AdjustRadialBlurStrength","AdjustRadialBlurDistance","AdjustFrame"};
 for(int mode=0;mode<3;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,fullscreenBlurAction,2);lua_setglobal(state_,names[mode]);}
}
int ScriptRuntime::fullscreenBlurAction(lua_State*L){char message[2048]{};bool ok=false;{auto*self=runtime(L);std::string error;try{ok=self&&self->fullscreenBlurActionImpl(L,int(lua_tointeger(L,lua_upvalueindex(2))),error);}catch(const std::exception&e){error=e.what();ok=false;}if(!ok)std::snprintf(message,sizeof(message),"%s",error.empty()?"Native fullscreen effect runtime unavailable":error.c_str());}if(!ok){lua_pushstring(L,message);return lua_error(L);}return 0;}
bool ScriptRuntime::fullscreenBlurActionImpl(lua_State*L,int mode,std::string&error){
 // Execute passes a null-table ScriptArgs for a non-table argument; every
 // reader then yields its default (GetColor 0xffffffff).
 if(!lua_istable(L,1)){
  if(mode<2)return radialBlur_.adjust(mode==0,0,0,error);
  return vignetteFrame_.show(0,0,0xffffffffu,error);
 }
 if(mode<2){const float d=duration(L);const float f=fraction(L);return radialBlur_.adjust(mode==0,f,d,error);}
 const uint32_t c=color(L);const float d=duration(L);const float f=fraction(L);return vignetteFrame_.show(f,d,c,error);
}
bool ScriptRuntime::advanceRadialBlur(float appElapsed,std::string&error){return radialBlur_.advance(appElapsed,error);}
bool ScriptRuntime::advanceVignetteFrame(float cameraElapsed,std::string&error){
 // ConfigOptions::DrawHealthFX (compiled default true, config_file.cpp) gates
 // the GameplayScreen draw only; the camera helper always updates.
 const int top=lua_gettop(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushstring(state_,"DrawHealthFX");lua_setfield(state_,-2,"Name");
 if(lua_pcall(state_,1,1,0)!=LUA_OK){const char*e=lua_tostring(state_,-1);error=e?e:"Native DrawHealthFX configuration failed";lua_settop(state_,top);return false;}
 if(!lua_isboolean(state_,-1)){error="Native DrawHealthFX must be boolean";lua_settop(state_,top);return false;}drawHealthFx_=lua_toboolean(state_,-1);lua_settop(state_,top);
 return vignetteFrame_.advance(cameraElapsed,error);
}
