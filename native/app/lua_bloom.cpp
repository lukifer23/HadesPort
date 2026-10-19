#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
void ScriptRuntime::loadBloomDefinitions(hades::AssetStore&assets){bloom_.load(assets);}
int ScriptRuntime::bloomAction(lua_State*L){char detail[2048]{};bool ok=false;{auto*self=runtime(L);std::string error;ok=self&&self->bloomActionImpl(L,error);if(!ok)std::snprintf(detail,sizeof(detail),"%s",error.empty()?"Native bloom runtime unavailable":error.c_str());}if(!ok){lua_pushstring(L,detail);return lua_error(L);}return 0;}
bool ScriptRuntime::bloomActionImpl(lua_State*L,std::string&error){try{
 hades::SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;std::string name;
 if(const auto*p=args.get("Name"))name=p->text();else if(const auto*p=args.get("Names")){if(p->array().empty())throw std::runtime_error("Native bloom requires a first name");name=p->array().front().text();}else throw std::runtime_error("Native bloom requires a name");
 const auto number=[&](const char*k){const auto*p=args.get(k);const double v=p?p->number():0;if(!std::isfinite(v)||std::abs(v)>1e7)throw std::runtime_error("Native bloom duration bounds");return float(v);};
 float duration=number("Duration"),lo=number("MinDuration"),hi=number("MaxDuration");if(lo!=0&&hi!=0){double random=0;if(!nextRecoveredEngineRandom(L,random))throw std::runtime_error("Original bloom RNG unavailable");duration=lo+float(random)*(hi-lo);}
 const auto*use=args.get("UseWorldTime");return bloom_.adjust(name,duration,use?use->boolean():true,worldTime_,float(screenTime_),error);
 }catch(const std::exception&e){error=e.what();return false;}}
bool ScriptRuntime::advanceBloom(std::string&error){
 const int top=lua_gettop(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushstring(state_,"DrawBloom");lua_setfield(state_,-2,"Name");if(lua_pcall(state_,1,1,0)!=LUA_OK){const char*e=lua_tostring(state_,-1);error=e?e:"Native DrawBloom configuration failed";lua_settop(state_,top);return false;}if(!lua_isboolean(state_,-1)){error="Native DrawBloom must be boolean";lua_settop(state_,top);return false;}const bool enabled=lua_toboolean(state_,-1);lua_settop(state_,top);return bloom_.advance(worldTime_,float(screenTime_),enabled,error);
}
