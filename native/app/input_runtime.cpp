#include "input_runtime.hpp"
#include "../analysis/control_names.hpp"
#include <map>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
// EngineWin64s.dll SHA256 d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254,
// matching PDB GUID15f83bcc-e740-4d0b-bf67-6ac938c8bdfa age1.
// Execute60/61 switch entries RVAs0x25199f/0x2519ed read PlayerIndex default1,
// call ScriptArgs::Names, then insert/erase FIRST name in Player.mInputBlocks.
// This is a unique set of strings, not counters or per-control bit masks.
// ExecuteBool229 RVA0x25521a tests set size +0x50 then mAllowInput +0x1.
constexpr const char* registryKey="hades.inputRuntime";
constexpr const char* metatableName="hades.inputRuntime.owner";
struct Player {bool allowInput=true;std::set<std::string> blocks;int32_t activeUnitId=0;uint64_t activeUnitLifetime=0;};
struct InputState {std::vector<Player> players;std::map<std::string,bool> controls;};
InputState* lookup(lua_State* state) {
    lua_getfield(state,LUA_REGISTRYINDEX,registryKey);
    auto* result=static_cast<InputState*>(lua_touserdata(state,-1));lua_pop(state,1);return result;
}
InputState& runtime(lua_State* state) {return *static_cast<InputState*>(lua_touserdata(state,lua_upvalueindex(1)));}
void arguments(lua_State* state) {
    if(!lua_istable(state,1))throw std::runtime_error("Input action requires argument table");
    lua_pushnil(state);
    while(lua_next(state,1)) {
        if(lua_type(state,-2)!=LUA_TSTRING)throw std::runtime_error("Input action requires named arguments");
        const std::string key=lua_tostring(state,-2);
        if(key!="PlayerIndex" && key!="Name" && key!="Names" && key!="Delay" && key!="DelaySeconds" && key!="DelayMin" && key!="DelayMax" && key!="DelaySecondsMin" && key!="DelaySecondsMax")throw std::runtime_error("Unsupported input action argument: "+key);
        lua_pop(state,1);
    }
}
int64_t playerIndex(lua_State* state) {
    lua_getfield(state,1,"PlayerIndex");
    if(lua_isnil(state,-1)){lua_pop(state,1);return 1;}
    if(lua_type(state,-1)!=LUA_TNUMBER)throw std::runtime_error("PlayerIndex requires number");
    const double value=lua_tonumber(state,-1);lua_pop(state,1);
    if(!std::isfinite(value) || value<INT32_MIN || value>INT32_MAX)throw std::runtime_error("PlayerIndex outside finite signed32-bit range");
    return static_cast<int32_t>(value); // Native LuaTableExtensions::Int truncates.
}
std::string nameAt(lua_State* state,int at) {
    if(lua_type(state,at)!=LUA_TSTRING)throw std::runtime_error("Input block name requires string");
    size_t size=0;const char* bytes=lua_tolstring(state,at,&size);
    if(size>65536 || std::memchr(bytes,0,size))throw std::runtime_error("Input block name outside supported bounds");
    return std::string(bytes,size);
}
std::string firstName(lua_State* state) {
    lua_getfield(state,1,"Names");
    if(lua_isnil(state,-1)) {lua_pop(state,1);lua_getfield(state,1,"Name");}
    // Native LuaTableExtensions::List accepts a string or a table for either
    // key. Names overrides Name, and the action consumes only vector[0].
    if(lua_type(state,-1)==LUA_TSTRING) {const auto name=nameAt(state,-1);lua_pop(state,1);return name;}
    if(!lua_istable(state,-1))throw std::runtime_error("Input block requires string or nonempty array Name/Names");
    const auto size=lua_rawlen(state,-1);
    if(size==0 || size>10000)throw std::runtime_error("Input block names array outside supported bounds");
    std::string first;
    for(size_t i=1;i<=size;++i) {lua_rawgeti(state,-1,static_cast<int>(i));const auto name=nameAt(state,-1);if(i==1)first=name;lua_pop(state,1);}
    lua_pushnil(state);
    while(lua_next(state,-2)) {
        if(lua_type(state,-2)!=LUA_TNUMBER || lua_tonumber(state,-2)<1 ||
           lua_tonumber(state,-2)>static_cast<double>(size) || std::trunc(lua_tonumber(state,-2))!=lua_tonumber(state,-2))
            throw std::runtime_error("Input block named/map list entries are not reconstructed");
        lua_pop(state,1);
    }
    lua_pop(state,1);return first;
}
Player* player(InputState& state,int64_t index) {
    return index>=1 && static_cast<uint64_t>(index)<=state.players.size()?&state.players[static_cast<size_t>(index-1)]:nullptr;
}
template<bool Add> int change(lua_State* state) {
    arguments(state);auto& input=runtime(state);const auto index=playerIndex(state);
    auto* selected=player(input,index);
    // Native Add/Remove directly indexes the player vector without bounds
    // checks. Reject out-of-range mutation instead of reproducing unsafe memory.
    if(!selected)throw std::runtime_error("Input block PlayerIndex has no configured local player");
    const auto name=firstName(state);
    if constexpr(Add)selected->blocks.insert(name);else selected->blocks.erase(name);
    return 0;
}
int allowed(lua_State* state) {
    arguments(state);auto& input=runtime(state);
    const auto* selected=player(input,playerIndex(state));
    lua_pushboolean(state,selected && selected->allowInput && selected->blocks.empty());return 1;
}
int toggleControl(lua_State* state){
    if(!lua_istable(state,1))throw std::runtime_error("ToggleControl requires table");
    lua_getfield(state,1,"Enabled");const bool enabled=lua_toboolean(state,-1);lua_pop(state,1);
    lua_getfield(state,1,"Names");if(lua_isnil(state,-1)){lua_pop(state,1);lua_getfield(state,1,"Name");}
    std::vector<std::string> names;
    if(lua_isstring(state,-1))names.emplace_back(lua_tostring(state,-1));
    else if(lua_istable(state,-1)){
        const auto count=lua_rawlen(state,-1);if(count>100000)throw std::runtime_error("Control names outside bounds");
        for(size_t i=1;i<=count;++i){lua_rawgeti(state,-1,int(i));names.push_back(nameAt(state,-1));lua_pop(state,1);}
    }
    lua_pop(state,1);auto& input=runtime(state);
    // Execute203 RVA254161 looks up each global RemappableControl; unknown
    // names are ignored, not new invented controls and not player input blocks.
    for(const auto& name:names)if(auto at=input.controls.find(name);at!=input.controls.end())at->second=enabled;
    return 0;
}
template<int(*Action)(lua_State*)>int guarded(lua_State* state) {
    char message[1024]={};try{return Action(state);}catch(const std::exception& error){std::snprintf(message,sizeof(message),"%s",error.what());}
    return luaL_error(state,"%s",message);
}
int finalize(lua_State* state) {static_cast<InputState*>(lua_touserdata(state,1))->~InputState();return 0;}
}
void registerInputRuntime(lua_State* state) {
    const int top=lua_gettop(state);
    auto* input=new(lua_newuserdata(state,sizeof(InputState)))InputState();
    for(const auto name:hades::contract::controlNames)input->controls.emplace(std::string(name),true);
    const int owner=lua_gettop(state);
    if(luaL_newmetatable(state,metatableName)){lua_pushcfunction(state,finalize);lua_setfield(state,-2,"__gc");}
    lua_setmetatable(state,owner);lua_pushvalue(state,owner);lua_setfield(state,LUA_REGISTRYINDEX,registryKey);
    const auto bind=[&](const char* name,lua_CFunction action){lua_pushvalue(state,owner);lua_pushcclosure(state,action,1);lua_setglobal(state,name);};
    bind("AddInputBlock",guarded<change<true>>);bind("RemoveInputBlock",guarded<change<false>>);bind("IsInputAllowed",guarded<allowed>);
    bind("ToggleControl",guarded<toggleControl>);
    lua_settop(state,top);
}
bool createRecoveredInputPlayer(lua_State* state,uint32_t index,std::string& error) {
    auto* input=lookup(state);
    if(!input){error="Input runtime not registered";return false;}
    if(index!=input->players.size()+1 || index>64){error="Explicit local input slots must be created sequentially within 1..64";return false;}
    // Native Player::Player RVA0x20ef50 writes 0x00010100 at offset0:
    // mWasAnyInputGiven=false, mAllowInput=true, mIsConnected=true,
    // mPauseRequested=false; mInputBlocks.mNodeCount at0x50 starts0.
    input->players.emplace_back();error.clear();return true;
}
std::vector<InputPlayerSnapshot> snapshotInputPlayers(lua_State* state) {
    std::vector<InputPlayerSnapshot> result;const auto* input=lookup(state);if(!input)return result;
    for(size_t i=0;i<input->players.size();++i){const auto& player=input->players[i];InputPlayerSnapshot snapshot;
        snapshot.playerIndex=static_cast<uint32_t>(i+1);snapshot.allowInput=player.allowInput;
        snapshot.activeUnitId=player.activeUnitId;snapshot.activeUnitLifetime=player.activeUnitLifetime;
        snapshot.namedBlocks.assign(player.blocks.begin(),player.blocks.end());snapshot.inputAllowed=player.allowInput && player.blocks.empty();result.push_back(std::move(snapshot));}
    return result;
}
std::string dumpInputRuntime(lua_State* state) {
    const auto players=snapshotInputPlayers(state);std::ostringstream out;out<<"INPUT_PLAYERS count="<<players.size()<<'\n';
    for(const auto& player:players){out<<"player="<<player.playerIndex<<" allowInput="<<player.allowInput<<" inputAllowed="<<player.inputAllowed<<" activeUnit="<<player.activeUnitId<<" activeLifetime="<<player.activeUnitLifetime<<" namedBlocks="<<player.namedBlocks.size();for(const auto& name:player.namedBlocks)out<<" ["<<name<<']';out<<'\n';}
    if(const auto* input=lookup(state))for(const auto& [name,enabled]:input->controls)out<<"CONTROL name="<<name<<" enabled="<<enabled<<'\n';
    return out.str();
}
bool isRecoveredInputAllowed(lua_State* state,uint32_t index) {
    auto* input=lookup(state);const auto* selected=input?player(*input,index):nullptr;
    return selected && selected->allowInput && selected->blocks.empty();
}
bool hasRecoveredInputBlock(lua_State* state,const std::string& name,uint32_t index) {
    auto* input=lookup(state);const auto* selected=input?player(*input,index):nullptr;return selected && selected->blocks.contains(name);
}
bool setRecoveredPlayerInputPermission(lua_State* state,uint32_t index,bool allowInput,std::string& error) {
    auto* input=lookup(state);auto* selected=input?player(*input,index):nullptr;
    if(!selected){error="Input PlayerIndex has no configured local player";return false;}
    selected->allowInput=allowInput;error.clear();return true;
}
bool bindRecoveredPlayerUnit(lua_State* state,uint32_t index,int32_t id,uint64_t lifetime,std::string& error){
    auto* input=lookup(state);auto* selected=input?player(*input,index):nullptr;
    if(!selected){error="Active unit binding requires configured player slot";return false;}
    selected->activeUnitId=id;selected->activeUnitLifetime=lifetime;error.clear();return true;
}
bool isRecoveredControlEnabled(lua_State* state,const std::string& name){
    const auto* input=lookup(state);if(!input)return false;const auto at=input->controls.find(name);return at!=input->controls.end()&&at->second;
}
