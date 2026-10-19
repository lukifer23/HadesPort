#include "lua_runtime.hpp"
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
    std::set_difference(next.begin(),next.end(),rawControlsDown_.begin(),rawControlsDown_.end(),std::back_inserter(rawControlsPressed_));
    std::set_difference(rawControlsDown_.begin(),rawControlsDown_.end(),next.begin(),next.end(),std::back_inserter(rawControlsReleased_));rawControlsDown_=std::move(next);
}
bool ScriptRuntime::controlNotifyImpl(lua_State* L,bool released,std::string& error){try{
    if(!lua_istable(L,1))throw std::runtime_error("Control notification requires source argument table");
    WorldNotify pending;pending.control=true;pending.released=released;
    lua_getfield(L,1,"Names");if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,1,"Name");}
    auto add=[&](){if(lua_type(L,-1)!=LUA_TSTRING)throw std::runtime_error("Control notification name must be a string");size_t n=0;const auto* value=lua_tolstring(L,-1,&n);if(n>4096)throw std::runtime_error("Control name bounds");pending.names.emplace_back(value,n);};
    if(lua_istable(L,-1)){const size_t count=lua_rawlen(L,-1);if(count>4096)throw std::runtime_error("Control notification name count bounds");for(size_t i=1;i<=count;++i){lua_rawgeti(L,-1,int(i));add();lua_pop(L,1);}}
    else if(!lua_isnil(L,-1))add();lua_pop(L,1);
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
    lua_getfield(state_,-1,"AllowHold");const bool hold=lua_toboolean(state_,-1);lua_settop(state_,top);
    // Native CheckControlPressed has the EditingMode guard; Released does not.
    if(!pending.released){lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushliteral(state_,"EditingMode");lua_setfield(state_,-2,"Name");if(lua_pcall(state_,1,1,0)!=LUA_OK){error=lua_tostring(state_,-1);lua_settop(state_,top);return false;}const bool editing=lua_toboolean(state_,-1);lua_settop(state_,top);if(editing)return true;}
    const auto has=[&](const auto& values,const std::string& n){return std::binary_search(values.begin(),values.end(),n);};
    for(const auto& n:pending.names){
        if(pending.released?has(rawControlsReleased_,n):(has(rawControlsPressed_,n)||(hold&&has(rawControlsDown_,n))||(n=="Move"&&has(rawControlsDown_,n)))){matched=true;name=n;break;}
    }
    return true;
}
