// Mac-only debugger: run the complete, unmodified original Main.lua scheduler.
// Synthetic callback bodies observe timing; they do not stand in for gameplay.
#include "../app/script_scheduler.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
namespace {
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void run(lua_State* state,const char* source){
    if(luaL_loadstring(state,source)||lua_pcall(state,0,0,0))throw std::runtime_error(lua_tostring(state,-1));
}
void advance(lua_State* state,float world,double screen){
    const int top=lua_gettop(state);std::string error;
    check(advanceScriptScheduler(state,world,screen,error),error);
    check(lua_gettop(state)==top,"Frame bridge changed caller stack");
}
void notify(lua_State* state,const char* event,bool existingOnly=false){
    const int top=lua_gettop(state);std::string error;
    check(notifyScriptEvent(state,event,existingOnly,false,error),error);
    check(lua_gettop(state)==top,"Event bridge changed caller stack");
}
}
int main(int argc,char** argv){
    if(argc!=2)return 64;lua_State* state=luaL_newstate();luaL_openlibs(state);
    try{
        std::string error;
        check(!initializeScriptScheduler(state,error),"Initialized before original Main.lua");
        check(lua_gettop(state)==0,"Failed initialization changed stack");
        if(luaL_loadfile(state,argv[1])||lua_pcall(state,0,0,0))throw std::runtime_error(lua_tostring(state,-1));
        // These are observation hooks required by Main.draw, not replacement
        // scheduler routines. Every deferred call and elapsed-time value counts.
        run(state,R"(
          verboseLogging=false; trace={}; elapsedSamples={}; deferred={}
          DebugMessage=function(a) debugError=a.Text end
          UpdateTimers=function(e) table.insert(elapsedSamples,e) end
          DeferredCombatPresentation=function() table.insert(deferred,'combat') end
          DeferredUIScripts=function() table.insert(deferred,'ui') end
          DeferredAudioScripts=function() table.insert(deferred,'audio') end
        )");
        check(initializeScriptScheduler(state,error),error);
        lua_pushliteral(state,"caller-stack-sentinel");
        run(state,R"(
          thread(function() table.insert(trace,'world-start');wait(nil);wait(0);wait(-1);wait(1,'world');table.insert(trace,'world-done') end)
          thread(function() table.insert(trace,'screen-start');waitScreenTime(1,'screen');table.insert(trace,'screen-done') end)
          assert(#trace==2 and #_threads==2)
        )");
        advance(state,0,2);
        run(state,"assert(#trace==3 and trace[3]=='screen-done' and #_threads==1);assert(elapsedSamples[1]==0);assert(table.concat(deferred,',')=='combat,ui,audio')");
        advance(state,1,2);
        run(state,"assert(#trace==3 and #_threads==1)");
        advance(state,std::nextafter(1.0f,2.0f),2);
        run(state,"assert(trace[4]=='world-done' and #_threads==0)");
        notify(state,"early");
        run(state,"thread(function() waitUntil('early');earlyDone=true end);assert(earlyDone and _events.early==nil)");
        notify(state,"future",true);
        run(state,"thread(function() waitUntil('future','listener');eventResumed=true;wait(0.25,'after-event');eventDone=true end);assert(not eventResumed)");
        check(snapshotScriptScheduler(state).eventWaiters==1,"Event listener was not retained");
        notify(state,"future");
        run(state,"assert(eventResumed and not eventDone and #_workingThreads==1)");
        advance(state,2,2);
        run(state,"assert(not eventDone and #_threads==1)");
        advance(state,2.1f,2);
        run(state,"assert(eventDone and #_threads==0)");
        run(state,"thread(function() wait(0.01,'kill-me');killedExecuted=true end);killTaggedThreads('kill-me')");
        advance(state,3,3);
        run(state,"assert(not killedExecuted and #_threads==0 and next(_tagsToKill)==nil)");
        // ScriptManager caches LUA_UPDATE; replacing the Lua global later must
        // not silently replace the callback retained by this native bridge.
        run(state,"savedUpdate=update;update=function()error('wrong update replacement')end");
        check(initializeScriptScheduler(state,error),error);
        advance(state,4,4);
        check(!advanceScriptScheduler(state,std::numeric_limits<float>::infinity(),4,error),"Nonfinite clock accepted");
        check(!snapshotScriptScheduler(state).faulted,"Rejected clock corrupted scheduler state");
        run(state,"thread(function() wait(0.01);error('expected coroutine failure') end)");
        check(!advanceScriptScheduler(state,5,5,error),"Coroutine error was swallowed");
        check(error.find("expected coroutine failure")!=std::string::npos,error);
        const auto failed=snapshotScriptScheduler(state);
        check(failed.faulted&&failed.updateCalls==failed.successfulUpdates+1,"Frame fault was not retained");
        check(!advanceScriptScheduler(state,6,6,error),"Faulted scheduler resumed silently");
        check(snapshotScriptScheduler(state).updateCalls==failed.updateCalls,"Faulted scheduler retried partial frame");
        check(!initializeScriptScheduler(state,error),"Reinitialization cleared retained fault");
        check(lua_gettop(state)==1&&std::string(lua_tostring(state,1))=="caller-stack-sentinel","Bridge lost caller stack");
        std::cout<<"ORIGINAL_MAIN_SCHEDULER_OK strict_wait world_screen_split event_retention tagged_kill cached_update sticky_fault stack_preservation\n"<<dumpScriptScheduler(state);
        lua_close(state);return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';lua_close(state);return 2;}
}
