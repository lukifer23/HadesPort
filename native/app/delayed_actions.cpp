#include "delayed_actions.hpp"
#include "../analysis/lua_bridge_contract.hpp"
#include <cmath>
#include <cstdlib>
#include <locale.h>
#include <sstream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
constexpr const char* key="hades.delayedActions";
struct StackRestore {
    lua_State* state;int top;
    explicit StackRestore(lua_State* s):state(s),top(lua_gettop(s)){}
    ~StackRestore(){lua_settop(state,top);}
};
bool record(lua_State* s,std::string& error,bool permitFault=false){
    lua_getfield(s,LUA_REGISTRYINDEX,key);
    if(!lua_istable(s,-1)){error="Delayed native actions are not initialized";return false;}
    lua_getfield(s,-1,"fault");
    if(!permitFault&&!lua_isnil(s,-1)){
        error=lua_isstring(s,-1)?lua_tostring(s,-1):"Delayed native action queue is faulted";return false;
    }
    lua_pop(s,1);return true;
}
bool flag(lua_State* s,int table,const char* name){lua_getfield(s,table,name);const bool v=lua_toboolean(s,-1);lua_pop(s,1);return v;}
void setFlag(lua_State* s,int table,const char* name,bool value){lua_pushboolean(s,value);lua_setfield(s,table,name);}
lua_Integer integer(lua_State* s,int table,const char* name){lua_getfield(s,table,name);const auto v=lua_tointeger(s,-1);lua_pop(s,1);return v;}
void add(lua_State* s,int table,const char* name,lua_Integer amount){const auto value=integer(s,table,name);lua_pushinteger(s,value+amount);lua_setfield(s,table,name);}
size_t count(lua_State* s,int table){
    if(!lua_istable(s,table))return 0;table=lua_absindex(s,table);size_t n=0;lua_pushnil(s);
    while(lua_next(s,table)){++n;lua_pop(s,1);}return n;
}
void empty(lua_State* s,int table,const char* name){lua_newtable(s);lua_setfield(s,table,name);}
bool nativeFloat(lua_State* s,int args,const char* shortName,const char* legacy,float& value,std::string& error){
    // Original LuaTableExtensions::Float accepts numbers/numeric strings,
    // otherwise C-locale string prefixes; non-string/non-number uses default0.
    lua_getfield(s,args,shortName);
    if(lua_isnil(s,-1)){lua_pop(s,1);lua_getfield(s,args,legacy);}
    value=0;
    if(lua_isnumber(s,-1))value=static_cast<float>(lua_tonumber(s,-1));
    else if(lua_isstring(s,-1)){
        locale_t locale=newlocale(LC_NUMERIC_MASK,"C",nullptr);
        if(!locale){lua_pop(s,1);error="Cannot create C numeric locale";return false;}
        value=strtof_l(lua_tostring(s,-1),nullptr,locale);freelocale(locale);
    }
    lua_pop(s,1);
    if(!std::isfinite(value)){error=std::string("Nonfinite native delay property: ")+shortName;return false;}
    return true;
}
void append(lua_State* s,int owner,int task){
    owner=lua_absindex(s,owner);task=lua_absindex(s,task);
    lua_getfield(s,owner,"pending");lua_pushvalue(s,task);lua_rawseti(s,-2,lua_rawlen(s,-2)+1);lua_pop(s,1);
}
bool fault(lua_State* s,int owner,const std::string& message,std::string& error){
    error=message;lua_pushlstring(s,error.data(),error.size());lua_setfield(s,owner,"fault");setFlag(s,owner,"pumping",false);return false;
}
void snapshots(lua_State* s,int owner,const char* name,std::vector<DelayedActionSnapshot>& out){
    lua_getfield(s,owner,name);if(lua_istable(s,-1)){
        const int tasks=lua_gettop(s);lua_pushnil(s);
        while(lua_next(s,tasks)){
            const int task=lua_gettop(s);
            if(lua_istable(s,task)){
                DelayedActionSnapshot value;value.sequence=integer(s,task,"sequence");value.actionId=static_cast<int>(integer(s,task,"actionId"));
                lua_getfield(s,task,"deadline");value.deadline=static_cast<float>(lua_tonumber(s,-1));lua_pop(s,1);out.push_back(value);
            }lua_pop(s,1);
        }
    }lua_pop(s,1);
}
}

bool initializeDelayedActions(lua_State* s,const std::vector<int>& supported,std::string& error){
    error.clear();if(!s){error="Missing Lua VM";return false;}StackRestore stack(s);
    lua_getfield(s,LUA_REGISTRYINDEX,key);
    if(lua_istable(s,-1)){error="Delayed action whitelist already initialized";return false;}
    lua_pop(s,1);lua_newtable(s);const int owner=lua_gettop(s);lua_newtable(s);
    for(int id:supported){
        bool valid=false;
        for(const auto& action:hades::contract::actions)if(action.id==id&&action.result==hades::contract::ReturnKind::Void){valid=true;break;}
        if(!valid){error="Whitelist requires an original native Void action ID";return false;}
        lua_pushboolean(s,1);lua_rawseti(s,-2,id);
    }
    lua_setfield(s,owner,"supported");empty(s,owner,"pending");empty(s,owner,"active");
    lua_pushvalue(s,owner);lua_setfield(s,LUA_REGISTRYINDEX,key);return true;
}

bool admitDelayedAction(lua_State* s,int id,int args,float world,DelayedActionAdmission& outcome,
                        std::string& error,DelayedActionRandom random,void* randomContext){
    error.clear();if(!s){error="Missing Lua VM";return false;}StackRestore stack(s);args=lua_absindex(s,args);
    if(!std::isfinite(world)){error="Native action world clock must be finite";return false;}
    if(!lua_istable(s,args)){error="Native delayed action requires its original argument table";return false;}
    if(!record(s,error))return false;const int owner=lua_gettop(s);
    lua_getfield(s,owner,"supported");lua_rawgeti(s,-1,id);const bool supported=lua_toboolean(s,-1);lua_pop(s,2);
    if(!supported){error="Native Void action is not a verified supported handler: "+std::to_string(id);return false;}
    float delay=0,minimum=0,maximum=0;
    if(!nativeFloat(s,args,"Delay","DelaySeconds",delay,error)||!nativeFloat(s,args,"DelayMin","DelaySecondsMin",minimum,error)||!nativeFloat(s,args,"DelayMax","DelaySecondsMax",maximum,error))return false;
    if(!(delay>0||minimum>0||maximum>0)){outcome=DelayedActionAdmission::ExecuteNow;return true;}
    // Exact shipped closure condition: Min/Max alone cause queue admission but
    // do not sample RNG unless base Delay>0 as well. Preserve that behavior.
    if(delay>0&&maximum>minimum){
        if(!random){error="Randomized native delay requires the recovered deterministic RNG stream";return false;}
        if(!random(randomContext,minimum,maximum,delay,error))return false;
        if(!std::isfinite(delay)){error="Recovered native delay RNG returned a nonfinite value";return false;}
    }
    const float deadline=world+delay;if(!std::isfinite(deadline)){error="Native delayed action deadline overflowed";return false;}
    add(s,owner,"sequence",1);lua_newtable(s);const int task=lua_gettop(s);
    lua_pushinteger(s,integer(s,owner,"sequence"));lua_setfield(s,task,"sequence");
    lua_pushinteger(s,id);lua_setfield(s,task,"actionId");lua_pushnumber(s,deadline);lua_setfield(s,task,"deadline");
    lua_pushvalue(s,args);lua_setfield(s,task,"args");append(s,owner,task);
    outcome=DelayedActionAdmission::Queued;return true;
}

bool advanceDelayedActions(lua_State* s,float world,DelayedActionExecutor execute,void* context,std::string& error){
    error.clear();if(!s){error="Missing Lua VM";return false;}StackRestore stack(s);
    if(!std::isfinite(world)){error="Native action world clock must be finite";return false;}
    if(!execute){error="A genuine immediate native action executor is required";return false;}
    if(!record(s,error))return false;const int owner=lua_gettop(s);
    if(flag(s,owner,"pumping")){error="Reentrant native delayed action pump is unsupported";return false;}
    lua_getfield(s,owner,"pending");const int active=lua_gettop(s);const auto total=lua_rawlen(s,active);
    lua_pushvalue(s,active);lua_setfield(s,owner,"active");empty(s,owner,"pending");setFlag(s,owner,"pumping",true);
    for(size_t i=1;i<=total;++i){
        lua_rawgeti(s,active,static_cast<int>(i));const int task=lua_gettop(s);
        lua_getfield(s,task,"deadline");const float deadline=static_cast<float>(lua_tonumber(s,-1));lua_pop(s,1);
        if(!(world>deadline))append(s,owner,task);
        else{
            const int id=static_cast<int>(integer(s,task,"actionId"));lua_getfield(s,task,"args");const int args=lua_gettop(s);
            const int before=lua_gettop(s);std::string executionError;
            const bool success=execute(s,id,args,context,executionError);
            if(lua_gettop(s)!=before){lua_settop(s,before);return fault(s,owner,"Native delayed executor changed the borrowed Lua stack",error);}
            lua_pop(s,1);
            if(!success)return fault(s,owner,"Delayed native action "+std::to_string(id)+": "+executionError,error);
            add(s,owner,"executed",1);
        }
        lua_pushnil(s);lua_rawseti(s,active,static_cast<int>(i));lua_pop(s,1);
    }
    empty(s,owner,"active");setFlag(s,owner,"pumping",false);return true;
}

bool clearDelayedActions(lua_State* s,std::string& error){
    error.clear();if(!s){error="Missing Lua VM";return false;}StackRestore stack(s);
    if(!record(s,error,true))return false;const int owner=lua_gettop(s);
    if(flag(s,owner,"pumping")){error="Native delayed cancellation during execution requires additional lifecycle recovery";return false;}
    lua_getfield(s,owner,"pending");const auto pending=count(s,-1);lua_pop(s,1);
    lua_getfield(s,owner,"active");const auto active=count(s,-1);lua_pop(s,1);
    add(s,owner,"cancelled",static_cast<lua_Integer>(pending+active));empty(s,owner,"pending");empty(s,owner,"active");
    lua_pushnil(s);lua_setfield(s,owner,"fault");return true;
}

DelayedActionsSnapshot snapshotDelayedActions(lua_State* s){
    DelayedActionsSnapshot result;if(!s)return result;StackRestore stack(s);lua_getfield(s,LUA_REGISTRYINDEX,key);
    if(!lua_istable(s,-1))return result;const int owner=lua_gettop(s);result.initialized=true;
    result.pumping=flag(s,owner,"pumping");result.executed=integer(s,owner,"executed");result.cancelled=integer(s,owner,"cancelled");
    lua_getfield(s,owner,"fault");result.faulted=!lua_isnil(s,-1);if(lua_isstring(s,-1))result.error=lua_tostring(s,-1);lua_pop(s,1);
    snapshots(s,owner,"pending",result.pending);snapshots(s,owner,"active",result.interrupted);return result;
}

std::string dumpDelayedActions(lua_State* s){
    const auto v=snapshotDelayedActions(s);std::ostringstream out;
    out<<"DELAYED_ACTIONS initialized="<<v.initialized<<" pumping="<<v.pumping<<" faulted="<<v.faulted<<" pending="<<v.pending.size()<<" interrupted="<<v.interrupted.size()<<" executed="<<v.executed<<" cancelled="<<v.cancelled<<'\n';
    for(const auto& task:v.pending)out<<"DELAYED_ACTION sequence="<<task.sequence<<" actionId="<<task.actionId<<" deadline="<<task.deadline<<'\n';
    if(v.faulted)out<<"DELAYED_ACTION_ERROR "<<v.error<<'\n';return out.str();
}
