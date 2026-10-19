#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
int ScriptRuntime::colorGradingAction(lua_State*L){char detail[2048]{};bool ok=false;{auto*self=runtime(L);std::string error;ok=self&&self->colorGradingActionImpl(L,error);if(!ok)std::snprintf(detail,sizeof(detail),"%s",error.empty()?"Native color grading runtime unavailable":error.c_str());}if(!ok){lua_pushstring(L,detail);return lua_error(L);}return 0;}
bool ScriptRuntime::colorGradingActionImpl(lua_State*L,std::string&error){try{
 hades::SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;std::string name;
 if(const auto*p=args.get("Name"))name=p->text();else if(const auto*p=args.get("Names")){if(p->array().empty())throw std::runtime_error("Native color grading requires a first name");name=p->array().front().text();}else throw std::runtime_error("Native color grading requires a name");
 const auto number=[&](const char*k){const auto*p=args.get(k);const double v=p?p->number():0;if(!std::isfinite(v)||std::abs(v)>1e7)throw std::runtime_error("Native color grading duration bounds");return float(v);};
 float duration=number("Duration"),lo=number("MinDuration"),hi=number("MaxDuration");if(lo!=0&&hi!=0){double random=0;if(!nextRecoveredEngineRandom(L,random))throw std::runtime_error("Original color grading RNG unavailable");duration=lo+float(random)*(hi-lo);}
 const auto*use=args.get("UseWorldTime");return colorGrading_.set(name,duration,use?use->boolean():true,worldTime_,float(screenTime_),error);
 }catch(const std::exception&e){error=e.what();return false;}}

bool ScriptRuntime::fullscreenPresentation(uint32_t&group,bool&drawColor,bool&drawBloom,std::string&error)const{
 const int top=lua_gettop(state_);const auto get=[&](const char*name){lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushstring(state_,name);lua_setfield(state_,-2,"Name");if(lua_pcall(state_,1,1,0)!=LUA_OK){const char*e=lua_tostring(state_,-1);error=e?e:"Native fullscreen configuration failed";lua_settop(state_,top);return false;}return true;};
 if(!get("DrawColorGrading"))return false;if(!lua_isboolean(state_,-1)){lua_settop(state_,top);error="Native DrawColorGrading must be boolean";return false;}drawColor=lua_toboolean(state_,-1);lua_settop(state_,top);
 if(!get("DrawBloom"))return false;if(!lua_isboolean(state_,-1)){lua_settop(state_,top);error="Native DrawBloom must be boolean";return false;}drawBloom=lua_toboolean(state_,-1);lua_settop(state_,top);
 if(!get("FullscreenEffectGroup"))return false;if(lua_type(state_,-1)!=LUA_TSTRING){lua_settop(state_,top);error="Native FullscreenEffectGroup must be a string";return false;}std::string name=lua_tostring(state_,-1);lua_settop(state_,top);
 const auto lower=[](std::string s){for(char&c:s)if(c>='A'&&c<='Z')c+=char('a'-'A');return s;};name=lower(name);group=UINT32_MAX;const auto groups=snapshotSceneGroups(state_);for(uint32_t i=0;i<groups.size();++i)if(groups[i].visible&&lower(groups[i].name)==name){group=i;break;}error.clear();return true;
}
