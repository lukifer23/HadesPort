#include "script_scheduler.hpp"
#include <cmath>
#include <sstream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
constexpr const char* registryKey = "hades.scriptScheduler";
struct StackRestore {
    lua_State* state;
    int top;
    explicit StackRestore(lua_State* value):state(value),top(lua_gettop(value)){}
    ~StackRestore(){lua_settop(state,top);}
};
bool scheduler(lua_State* state,std::string& error) {
    lua_getfield(state,LUA_REGISTRYINDEX,registryKey);
    if(!lua_istable(state,-1)){error="Original Main.lua scheduler has not been initialized";return false;}
    lua_getfield(state,-1,"fault");
    if(!lua_isnil(state,-1)){
        error=lua_isstring(state,-1)?lua_tostring(state,-1):"Original script scheduler is faulted";
        return false;
    }
    lua_pop(state,1);return true;
}
bool failure(lua_State* state,int record,const char* operation,std::string& error) {
    const char* message=lua_tostring(state,-1);
    error=std::string(operation)+": "+(message?message:"non-string Lua error");
    lua_pushlstring(state,error.data(),error.size());lua_setfield(state,record,"fault");
    return false;
}
size_t countTable(lua_State* state,int table) {
    if(!lua_istable(state,table))return 0;
    table=lua_absindex(state,table);size_t count=0;lua_pushnil(state);
    while(lua_next(state,table)){++count;lua_pop(state,1);}return count;
}
size_t globalCount(lua_State* state,const char* name) {
    lua_getglobal(state,name);const auto count=countTable(state,-1);lua_pop(state,1);return count;
}
}

bool initializeScriptScheduler(lua_State* state,std::string& error) {
    error.clear();if(!state){error="Missing Lua VM";return false;}
    StackRestore stack(state);
    lua_getfield(state,LUA_REGISTRYINDEX,registryKey);
    if(lua_istable(state,-1)){lua_pop(state,1);return scheduler(state,error);}
    lua_pop(state,1);lua_newtable(state);const int record=lua_gettop(state);
    for(const char* name:{"update","notify","notifyExistingWaiters"}){
        lua_getglobal(state,name);
        if(!lua_isfunction(state,-1)){error=std::string("Original Main.lua function missing: ")+name;return false;}
        lua_setfield(state,record,name);
    }
    for(const char* name:{"_threads","_workingThreads","_eventListeners","_tagsToKill"}){
        lua_getglobal(state,name);const bool valid=lua_istable(state,-1);lua_pop(state,1);
        if(!valid){error=std::string("Original Main.lua state missing: ")+name;return false;}
    }
    lua_pushvalue(state,record);lua_setfield(state,LUA_REGISTRYINDEX,registryKey);
    return true;
}

bool advanceScriptScheduler(lua_State* state,float worldTime,double screenTime,std::string& error) {
    error.clear();if(!state){error="Missing Lua VM";return false;}
    if(!std::isfinite(worldTime)||!std::isfinite(screenTime)){error="Scheduler clocks must be finite";return false;}
    StackRestore stack(state);if(!scheduler(state,error))return false;
    const int record=lua_gettop(state);
    lua_getfield(state,record,"updateCalls");const auto calls=lua_tointeger(state,-1);lua_pop(state,1);
    lua_pushinteger(state,calls+1);lua_setfield(state,record,"updateCalls");
    lua_pushnumber(state,worldTime);lua_setfield(state,record,"worldTime");
    lua_pushnumber(state,screenTime);lua_setfield(state,record,"screenTime");
    lua_getfield(state,record,"update");lua_pushnumber(state,worldTime);lua_pushnumber(state,screenTime);
    // The native call requests one result and discards it. Main.update itself
    // returns none. A failed frame may have partially mutated original state;
    // preserve that state and latch the failure rather than retry silently.
    if(lua_pcall(state,2,1,0)!=LUA_OK)return failure(state,record,"Original update",error);
    lua_pop(state,1);
    lua_getfield(state,record,"successfulUpdates");const auto done=lua_tointeger(state,-1);lua_pop(state,1);
    lua_pushinteger(state,done+1);lua_setfield(state,record,"successfulUpdates");return true;
}

bool notifyScriptEvent(lua_State* state,const std::string& event,bool existingOnly,bool wasTimeout,std::string& error,bool omitTimeoutArgument) {
    error.clear();if(!state){error="Missing Lua VM";return false;}
    StackRestore stack(state);if(!scheduler(state,error))return false;
    const int record=lua_gettop(state);
    lua_getfield(state,record,existingOnly?"notifyExistingWaiters":"notify");
    lua_pushlstring(state,event.data(),event.size());if(!omitTimeoutArgument)lua_pushboolean(state,wasTimeout);
    if(lua_pcall(state,omitTimeoutArgument?1:2,0,0)!=LUA_OK)return failure(state,record,"Original notify",error);
    return true;
}

ScriptSchedulerSnapshot snapshotScriptScheduler(lua_State* state) {
    ScriptSchedulerSnapshot result;if(!state)return result;StackRestore stack(state);
    lua_getfield(state,LUA_REGISTRYINDEX,registryKey);if(!lua_istable(state,-1))return result;
    result.initialized=true;const int record=lua_gettop(state);
    lua_getfield(state,record,"fault");result.faulted=!lua_isnil(state,-1);
    if(lua_isstring(state,-1))result.error=lua_tostring(state,-1);lua_pop(state,1);
    lua_getfield(state,record,"updateCalls");result.updateCalls=lua_tointeger(state,-1);lua_pop(state,1);
    lua_getfield(state,record,"successfulUpdates");result.successfulUpdates=lua_tointeger(state,-1);lua_pop(state,1);
    lua_getfield(state,record,"worldTime");result.worldTime=static_cast<float>(lua_tonumber(state,-1));lua_pop(state,1);
    lua_getfield(state,record,"screenTime");result.screenTime=lua_tonumber(state,-1);lua_pop(state,1);
    result.timedThreads=globalCount(state,"_threads");result.workingThreads=globalCount(state,"_workingThreads");
    result.pendingKillTags=globalCount(state,"_tagsToKill");
    lua_getglobal(state,"_eventListeners");
    if(lua_istable(state,-1)){
        const int events=lua_gettop(state);lua_pushnil(state);
        while(lua_next(state,events)){++result.eventNames;result.eventWaiters+=countTable(state,-1);lua_pop(state,1);}
    }
    return result;
}

std::string dumpScriptScheduler(lua_State* state) {
    const auto value=snapshotScriptScheduler(state);std::ostringstream out;
    out<<"SCRIPT_SCHEDULER initialized="<<value.initialized<<" faulted="<<value.faulted
       <<" updates="<<value.successfulUpdates<<'/'<<value.updateCalls<<" worldTime="<<value.worldTime
       <<" screenTime="<<value.screenTime<<" timed="<<value.timedThreads<<" working="<<value.workingThreads
       <<" eventNames="<<value.eventNames<<" eventWaiters="<<value.eventWaiters<<" killTags="<<value.pendingKillTags<<'\n';
    if(value.faulted)out<<"SCRIPT_SCHEDULER_ERROR "<<value.error<<'\n';
    if(!state)return out.str();StackRestore stack(state);
    const auto scalar=[&](int table,const char* name){lua_getfield(state,table,name);std::string text=lua_isstring(state,-1)?lua_tostring(state,-1):lua_isboolean(state,-1)?(lua_toboolean(state,-1)?"true":"false"):lua_isnil(state,-1)?"nil":lua_typename(state,lua_type(state,-1));lua_pop(state,1);return text;};
    const auto threadStack=[&](int record,const char* key){lua_getfield(state,record,key);auto* co=lua_tothread(state,-1);if(co){out<<" stack=[";lua_Debug frame;for(int i=0;i<8&&lua_getstack(co,i,&frame);++i)if(lua_getinfo(co,"Sl",&frame))out<<(frame.source?frame.source:"?")<<':'<<frame.currentline<<',';out<<']';}lua_pop(state,1);};
    lua_getglobal(state,"ActiveEnemies");if(lua_istable(state,-1)){const int enemies=lua_gettop(state);lua_pushnil(state);size_t count=0;while(lua_next(state,enemies)){if(lua_istable(state,-1)&&count++<64){const int enemy=lua_gettop(state);out<<"SCRIPT_ENEMY_AI";for(const char* key:{"ObjectId","Name","Health","MaxHealth","IsDead","AIDisabled","DisableAIWhenReady","IsAggroed","AIThreadName","AINotifyName","WeaponName","ActivationFinished"})out<<' '<<key<<'='<<scalar(enemy,key);out<<'\n';}lua_pop(state,1);}}lua_pop(state,1);
    for(const char* name:{"_threads","_workingThreads"}){lua_getglobal(state,name);if(lua_istable(state,-1)){const int threads=lua_gettop(state);lua_pushnil(state);size_t count=0;while(lua_next(state,threads)){if(lua_istable(state,-1)&&count++<64){const int entry=lua_gettop(state);out<<"SCRIPT_TIMED_THREAD table="<<name<<" tag="<<scalar(entry,"tag")<<" resumeTime="<<scalar(entry,"resumeTime");threadStack(entry,"thread");out<<'\n';}lua_pop(state,1);}}lua_pop(state,1);}
    lua_getglobal(state,"_eventListeners");if(lua_istable(state,-1)){const int events=lua_gettop(state);lua_pushnil(state);size_t count=0;while(lua_next(state,events)){if(lua_istable(state,-1)){const int listeners=lua_gettop(state);lua_pushnil(state);while(lua_next(state,listeners)){if(lua_istable(state,-1)&&count++<64){const int entry=lua_gettop(state);out<<"SCRIPT_EVENT_THREAD event="<<scalar(entry,"Event")<<" tag="<<scalar(entry,"Tag");threadStack(entry,"Thread");out<<'\n';}lua_pop(state,1);}}lua_pop(state,1);}}lua_pop(state,1);return out.str();
}
