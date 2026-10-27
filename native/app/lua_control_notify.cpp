#include "lua_runtime.hpp"
#include "input_runtime.hpp"
#include "lua_arguments.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <limits>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
std::vector<std::string> controlNamesArgument(lua_State* L) {
    std::vector<std::string> names;
    lua_getfield(L,1,"Names");if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,1,"Name");}
    const auto append=[&](){if(lua_type(L,-1)!=LUA_TSTRING)throw std::runtime_error("Control name must be a string");size_t n=0;const char* b=lua_tolstring(L,-1,&n);if(n>4096)throw std::runtime_error("Control name bound");names.emplace_back(b,n);};
    if(lua_istable(L,-1)){const size_t n=lua_rawlen(L,-1);if(n>4096)throw std::runtime_error("Control name count bound");for(size_t i=1;i<=n;++i){lua_rawgeti(L,-1,int(i));append();lua_pop(L,1);}}
    else if(!lua_isnil(L,-1))append();lua_pop(L,1);return names;
}
}
void ScriptRuntime::snapshotControlFrame(){
    auto next=localActionDown_;
    // Android joystick directions map into original directional control
    // states; the original Move notification also observes continuous input.
    if(localMoveX_<0)next.push_back("Left");if(localMoveX_>0)next.push_back("Right");
    if(localMoveY_<0)next.push_back("Up");if(localMoveY_>0)next.push_back("Down");
    if(localMoveX_!=0||localMoveY_!=0)next.push_back("Move");
    // Player::HandleInput 0x180211b11..bfa: any key/mouse/gamepad button down or
    // either stick nonzero sets player-1's any-input flag. Android touches are
    // the mouse-press equivalent (rawInputPulse_).
    playerInputThisFrame_=!next.empty()||rawInputPulse_;rawInputPulse_=false;
    std::sort(next.begin(),next.end());next.erase(std::unique(next.begin(),next.end()),next.end());
    rawControlsPressed_.clear();rawControlsReleased_.clear();
    // InputHandler::HandleInput 0x180194dc0 -> GetState 0x180195c50:
    // disabled RemappableControl+0x10 produces zero flags, including release.
    // Keep physical history separately: disabling a held binding is not a
    // physical release, and re-enabling it is not a new press.
    std::set_difference(next.begin(),next.end(),previousPhysicalControlsDown_.begin(),previousPhysicalControlsDown_.end(),std::back_inserter(rawControlsPressed_));
    std::set_difference(previousPhysicalControlsDown_.begin(),previousPhysicalControlsDown_.end(),next.begin(),next.end(),std::back_inserter(rawControlsReleased_));
    previousPhysicalControlsDown_=next;rawControlsDown_=std::move(next);
    const auto disabled=[&](const std::string&name){return isRecoveredControlKnown(state_,name)&&!isRecoveredControlEnabled(state_,name);};
    std::erase_if(rawControlsDown_,disabled);std::erase_if(rawControlsPressed_,disabled);std::erase_if(rawControlsReleased_,disabled);
}
bool ScriptRuntime::controlNotifyImpl(lua_State* L,bool released,std::string& error){try{
    if(!lua_istable(L,1))throw std::runtime_error("Control notification requires source argument table");
    WorldNotify pending;pending.control=true;pending.released=released;
    pending.names=controlNamesArgument(L);
    pending.timeout=std::numeric_limits<float>::max();lua_getfield(L,1,"Timeout");if(!lua_isnil(L,-1)){int numeric=0;const double n=lua_tonumberx(L,-1,&numeric);if(!numeric||!std::isfinite(n)||std::abs(n)>std::numeric_limits<float>::max())throw std::runtime_error("Control notification timeout bounds");pending.timeout=float(n);}lua_pop(L,1);
    if(worldNotifies_.size()>=10000)throw std::runtime_error("Native notification queue bounds");
    lua_pushvalue(L,1);pending.arguments=luaL_ref(L,LUA_REGISTRYINDEX);worldNotifies_.push_back(std::move(pending));return true;
}catch(const std::exception& e){error=e.what();return false;}}
// NotifyOnPlayerInput{Notify,Timeout}: satisfied on the first frame with the
// any-input flag set (RoomPresentation ReattachCameraOnInput, DeathLoop).
int ScriptRuntime::playerInputNotifyAction(lua_State* L){
    auto* self=runtime(L);if(!self||!lua_istable(L,1))return luaL_error(L,"NotifyOnPlayerInput requires source argument table");
    WorldNotify pending;pending.playerInput=true;pending.timeout=std::numeric_limits<float>::max();
    lua_getfield(L,1,"Timeout");if(!lua_isnil(L,-1)){int numeric=0;const double n=lua_tonumberx(L,-1,&numeric);if(!numeric||!std::isfinite(n))return luaL_error(L,"NotifyOnPlayerInput Timeout requires number");pending.timeout=float(n);}lua_pop(L,1);
    if(self->worldNotifies_.size()>=10000)return luaL_error(L,"Native notification queue bounds");
    lua_pushvalue(L,1);pending.arguments=luaL_ref(L,LUA_REGISTRYINDEX);self->worldNotifies_.push_back(std::move(pending));return 0;
}
int ScriptRuntime::controlNotifyAction(lua_State* L){
    char message[2048]{};bool ok=false;{std::string error;ok=runtime(L)->controlNotifyImpl(L,lua_toboolean(L,lua_upvalueindex(2)),error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
bool ScriptRuntime::checkControlNotify(const WorldNotify& pending,bool& matched,std::string& name,std::string& error){
    matched=false;const int top=lua_gettop(state_);lua_rawgeti(state_,LUA_REGISTRYINDEX,pending.arguments);
    lua_getfield(state_,-1,"PlayerIndex");int numeric=0;const int player=lua_isnil(state_,-1)?1:int(lua_tointegerx(state_,-1,&numeric));lua_pop(state_,1);
    if(player!=1){lua_settop(state_,top);error="Native control notification requires actual Android local player slot";return false;}
    const bool hold=hades::luaArgumentBool(state_,-1,"AllowHold");lua_settop(state_,top);
    // Native CheckControlPressed has the EditingMode guard; Released does not.
    if(!pending.released){lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushliteral(state_,"EditingMode");lua_setfield(state_,-2,"Name");if(lua_pcall(state_,1,1,0)!=LUA_OK){error=lua_tostring(state_,-1);lua_settop(state_,top);return false;}const bool editing=lua_toboolean(state_,-1);lua_settop(state_,top);if(editing)return true;}
    const auto has=[&](const auto& values,const std::string& n){return std::binary_search(values.begin(),values.end(),n);};
    for(const auto& n:pending.names){
        if(pending.released?has(rawControlsReleased_,n):(has(rawControlsPressed_,n)||(hold&&has(rawControlsDown_,n))||(n=="Move"&&has(rawControlsDown_,n)))){matched=true;name=n;break;}
    }
    return true;
}

std::vector<std::string> ScriptRuntime::pendingControlPresses() const {
    std::vector<std::string> names;if(!state_)return names;
    const int top=lua_gettop(state_);
    for(const auto& pending:worldNotifies_) {
        if(!pending.control||pending.released||!pending.arguments)continue;
        lua_rawgeti(state_,LUA_REGISTRYINDEX,pending.arguments);
        lua_getfield(state_,-1,"PlayerIndex");
        const bool local=lua_isnil(state_,-1)||lua_tointeger(state_,-1)==1;
        lua_settop(state_,top);if(!local)continue;
        for(const auto& name:pending.names)if(isRecoveredControlEnabled(state_,name))names.push_back(name);
    }
    std::sort(names.begin(),names.end());names.erase(std::unique(names.begin(),names.end()),names.end());
    return names;
}

int ScriptRuntime::controlDownAction(lua_State* L) {
    char message[1024]{};
    try {
        auto* self=runtime(L);if(!self||!lua_istable(L,1))throw std::runtime_error("IsControlDown requires source argument table");
        // ExecuteBool230 0x18025527b -> 0x180277c70. Native reads held
        // bit0x2, independent of gameplay input blocks and EditingMode.
        lua_getfield(L,1,"PlayerIndex");const double player=lua_isnumber(L,-1)?lua_tonumber(L,-1):1;lua_pop(L,1);
        if(!std::isfinite(player)||player<INT32_MIN||player>INT32_MAX)throw std::runtime_error("IsControlDown PlayerIndex outside native domain");
        if(int32_t(player)!=1||snapshotInputPlayers(self->state_).empty()){lua_pushboolean(L,false);return 1;}
        const auto names=controlNamesArgument(L);bool down=false;
        for(const auto& name:names)down|=std::binary_search(self->rawControlsDown_.begin(),self->rawControlsDown_.end(),name);
        lua_pushboolean(L,down);return 1;
    } catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    return luaL_error(L,"%s",message);
}
