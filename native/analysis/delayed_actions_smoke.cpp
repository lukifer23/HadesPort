// Portable delivery/ownership debugger; synthetic logger callbacks are not
// original native action bodies or evidence of running gameplay.
#include "../app/delayed_actions.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
namespace {
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
void run(lua_State* s,const char* code){if(luaL_loadstring(s,code)||lua_pcall(s,0,0,0))throw std::runtime_error(lua_tostring(s,-1));}
struct Context{float world=0;std::vector<std::string> calls;};
bool execute(lua_State* s,int id,int args,void* raw,std::string& error){
    auto& context=*static_cast<Context*>(raw);
    if(id!=215){error="Unexpected action";return false;}
    lua_getfield(s,args,"Text");const char* text=lua_tostring(s,-1);const std::string label=text?text:"";lua_pop(s,1);
    if(label=="fail"){error="expected native delivery failure";return false;}
    context.calls.push_back(label);
    if(label=="spawn"){
        lua_newtable(s);lua_pushliteral(s,"followup");lua_setfield(s,-2,"Text");
        lua_pushnumber(s,-0.5);lua_setfield(s,-2,"Delay");lua_pushnumber(s,1);lua_setfield(s,-2,"DelayMin");
        DelayedActionAdmission outcome;
        const bool ok=admitDelayedAction(s,215,-1,context.world,outcome,error);lua_pop(s,1);
        if(!ok||outcome!=DelayedActionAdmission::Queued){error="Followup admission failed: "+error;return false;}
    }
    return true;
}
void pump(lua_State* s,Context& context,float world){
    context.world=world;std::string error;const int top=lua_gettop(s);
    check(advanceDelayedActions(s,world,execute,&context,error),error);check(lua_gettop(s)==top,"Pump changed stack");
}
DelayedActionAdmission admit(lua_State* s,const char* code,float world){
    run(s,code);lua_getglobal(s,"args");const int top=lua_gettop(s);std::string error;DelayedActionAdmission result;
    check(admitDelayedAction(s,215,-1,world,result,error),error);check(lua_gettop(s)==top,"Admission changed stack");lua_pop(s,1);return result;
}
bool random(void* raw,float minimum,float maximum,float& result,std::string&){
    check(minimum==2&&maximum==4,"Incorrect RNG range");++*static_cast<int*>(raw);result=3;return true;
}
}
int main(){
    lua_State* s=luaL_newstate();luaL_openlibs(s);
    try{
        std::string error;check(!initializeDelayedActions(s,{9},error),"Float action admitted to Void whitelist");
        check(initializeDelayedActions(s,{215},error),error);lua_pushliteral(s,"sentinel");Context context;
        check(admit(s,"args={Text='immediate',Delay=0,DelaySeconds=10}",0)==DelayedActionAdmission::ExecuteNow,"Short alias zero did not override legacy delay");
        check(admit(s,"args={Text='before',Delay=1}",0)==DelayedActionAdmission::Queued,"Positive delay did not queue");
        run(s,"args.Text='after';args.Delay=100;args=nil;collectgarbage()");
        admit(s,"args={Text='second',DelaySeconds=1}",0);pump(s,context,1);
        check(context.calls.empty(),"Equal deadline executed early");pump(s,context,std::nextafter(1.0f,2.0f));
        check(context.calls==std::vector<std::string>{"after","second"},"Argument alias, captured deadline or FIFO failed");
        admit(s,"args={Text='spawn',Delay=0.01}",1.1f);admit(s,"args={Text='tail',Delay=0.01}",1.1f);
        pump(s,context,2);check(context.calls.back()=="tail","New admission executed within its creating pump");
        check(snapshotDelayedActions(s).pending.size()==1,"Reentrant admission was lost");pump(s,context,2);
        check(context.calls.back()=="followup","Expired followup was not ready on the next pump");
        admit(s,"args={Text='min-only',DelayMin=1}",3);pump(s,context,3);
        check(context.calls.back()=="followup","Range-only zero base ignored strict deadline");pump(s,context,std::nextafter(3.0f,4.0f));
        check(context.calls.back()=="min-only","Native range-only admission behavior changed");
        run(s,"args={Text='random',Delay=1,DelayMin=2,DelayMax=4}");lua_getglobal(s,"args");DelayedActionAdmission outcome;
        check(!admitDelayedAction(s,215,-1,4,outcome,error),"Random delay fabricated missing RNG");
        int draws=0;check(admitDelayedAction(s,215,-1,4,outcome,error,random,&draws),error);lua_pop(s,1);
        check(draws==1&&snapshotDelayedActions(s).pending[0].deadline==7,"RNG result not retained as float deadline");
        pump(s,context,7);check(context.calls.back()=="min-only","Random deadline equality executed early");pump(s,context,8);check(context.calls.back()=="random","Randomized action did not execute");
        admit(s,"args={Text='prefix',Delay='0.5suffix'}",8);pump(s,context,9);check(context.calls.back()=="prefix","Native C-locale numeric prefix parsing failed");
        run(s,"args={Delay=1}");lua_getglobal(s,"args");check(!admitDelayedAction(s,999,-1,9,outcome,error),"Unsupported action silently queued");lua_pop(s,1);
        admit(s,"args={Text='fail',Delay=0.1};weak=setmetatable({args},{__mode='v'})",9);run(s,"args=nil;collectgarbage();assert(weak[1]~=nil)");
        admit(s,"args={Text='not-executed',Delay=0.1}",9);context.world=10;
        check(!advanceDelayedActions(s,10,execute,&context,error),"Delivery error was swallowed");
        const auto failure=snapshotDelayedActions(s);check(failure.faulted&&failure.interrupted.size()==2,"Failed/remaining actions were not retained");
        check(!advanceDelayedActions(s,11,execute,&context,error),"Faulted queue retried silently");
        check(clearDelayedActions(s,error),error);run(s,"collectgarbage();collectgarbage();assert(weak[1]==nil)");
        const auto cleared=snapshotDelayedActions(s);check(!cleared.faulted&&cleared.cancelled==2&&cleared.pending.empty()&&cleared.interrupted.empty(),"Clear did not cancel/release/reset queue");
        check(!advanceDelayedActions(s,12,nullptr,nullptr,error),"Queue accepted a missing executor");
        check(lua_gettop(s)==1&&std::string(lua_tostring(s,1))=="sentinel","Caller stack was lost");
        std::cout<<"NATIVE_DELAY_QUEUE_OK float_strict_fifo original_args_alias next_pump_admission range_quirk recovered_rng_required failure_retention full_clear_lifetime\n"<<dumpDelayedActions(s);
        lua_close(s);return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';lua_close(s);return 2;}
}
