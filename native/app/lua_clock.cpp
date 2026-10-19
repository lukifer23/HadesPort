#include "lua_runtime.hpp"
#include "platform_log.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
// Execute(AdjustSimulationSpeed198)2540e3..25415c: original World scalar
// target/rate/lifetime, not an animation or per-Thing speed setter.
int ScriptRuntime::simulationSpeedAction(lua_State*L){char message[2048]{};bool ok=false;{try{std::string e;auto*self=runtime(L);ok=self&&self->simulationSpeedImpl(L,e);if(!ok)std::snprintf(message,sizeof(message),"%s",e.c_str());}catch(const std::exception&e){std::snprintf(message,sizeof(message),"%s",e.what());}}if(!ok)return luaL_error(L,"%s",message);return 0;}
bool ScriptRuntime::simulationSpeedImpl(lua_State*L,std::string&e){
    hades::SourceValue args;if(!lua_istable(L,1)||!hades::readSourceValue(L,1,args,e))return false;
    const auto number=[&](const char*k,float fallback){const auto*p=args.get(k);const float v=p?float(p->number()):fallback;if(!std::isfinite(v))throw std::runtime_error("Nonfinite native simulation speed parameter");return v;};
    if(number("MinFraction",0)!=0||number("MaxFraction",0)!=0)throw std::runtime_error("Native randomized simulation fraction requires exact engine RNG");
    const float fraction=number("Fraction",0),lerp=number("LerpTime",0),duration=number("Duration",4);
    if(fraction<=0)throw std::runtime_error("Native zero/negative world speed requires zero-clock component projection");
    worldSpeedRemaining_=duration;worldSpeedRate_=lerp>0?1.f/lerp:999.f;worldSpeedTarget_=fraction;
    hades::log(hades::Severity::Info,"HadesNative","WORLD_SPEED_REQUEST frame=%llu current=%.6f target=%.6f rate=%.6f lifetime=%.6f",(unsigned long long)worldFrame_,worldSpeedModifier_,worldSpeedTarget_,worldSpeedRate_,worldSpeedRemaining_);e.clear();return true;
}
bool ScriptRuntime::advanceWorldSpeed(float realElapsed,std::string&e){
    // World.Update314f57..314fdf: lifetime counts real time only while current
    // speed!=1, strict negative expiry restores target/rate1, then StepToward.
    if(worldSpeedModifier_!=1){worldSpeedRemaining_-=realElapsed;if(worldSpeedRemaining_<0){worldSpeedTarget_=1;worldSpeedRate_=1;}}
    const float step=realElapsed*worldSpeedRate_,difference=worldSpeedTarget_-worldSpeedModifier_;
    if(step<std::abs(difference)){if(difference>0)worldSpeedModifier_+=step;else if(difference<0)worldSpeedModifier_-=step;}else worldSpeedModifier_=worldSpeedTarget_;
    if(!std::isfinite(worldSpeedModifier_)||worldSpeedModifier_<=0){e="World speed outside finite positive component projection";return false;}e.clear();return true;
}
