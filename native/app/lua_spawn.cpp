#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

bool ScriptRuntime::spawnUnitImpl(lua_State* L,int32_t& created,std::string& error){
    if(!lua_istable(L,1)){error="SpawnUnit requires table";return false;}
    const auto text=[&](const char* key,const char* fallback=""){lua_getfield(L,1,key);if(lua_isnil(L,-1)){lua_pop(L,1);return std::string(fallback);}if(lua_type(L,-1)!=LUA_TSTRING)throw std::runtime_error(std::string(key)+" requires string");std::string v=lua_tostring(L,-1);lua_pop(L,1);return v;};
    const auto number=[&](const char* key){lua_getfield(L,1,key);if(lua_isnil(L,-1)){lua_pop(L,1);return 0.f;}int ok=0;float v=float(lua_tonumberx(L,-1,&ok));lua_pop(L,1);if(!ok||!std::isfinite(v))throw std::runtime_error(std::string(key)+" requires finite number");return v;};
    const auto boolean=[&](const char* key,bool fallback=false){lua_getfield(L,1,key);if(lua_isnil(L,-1)){lua_pop(L,1);return fallback;}if(!lua_isboolean(L,-1))throw std::runtime_error(std::string(key)+" requires boolean");bool v=lua_toboolean(L,-1);lua_pop(L,1);return v;};
    // ScriptAction.SpawnUnit26eca0: first Names value (not random name),
    // actual destination MapThings, native symmetric offset RNG, Group and
    // InheritGroupNames, then CreateEnemyUnit and optional location repair.
    auto name=text("Name");lua_getfield(L,1,"Names");
    if(!lua_isnil(L,-1)){
        if(lua_type(L,-1)==LUA_TSTRING)name=lua_tostring(L,-1);
        else if(lua_istable(L,-1)){if(lua_rawlen(L,-1)>10000)throw std::runtime_error("SpawnUnit names outside bounded array");lua_rawgeti(L,-1,1);if(lua_isnil(L,-1))name.clear();else if(lua_type(L,-1)==LUA_TSTRING)name=lua_tostring(L,-1);else throw std::runtime_error("SpawnUnit names require strings");lua_pop(L,1);}
        else throw std::runtime_error("SpawnUnit Names requires string or array");
    }
    lua_pop(L,1);created=-1;if(name.empty())return true;
    std::vector<int32_t> destinations;
    const auto id=[&](int index){int ok=0;double v=lua_tonumberx(L,index,&ok);if(!ok||!std::isfinite(v)||v<1||v>INT32_MAX||v!=std::trunc(v))throw std::runtime_error("SpawnUnit destination requires positive signed32 ID");destinations.push_back(int32_t(v));};
    lua_getfield(L,1,"DestinationIds");
    if(!lua_isnil(L,-1)){if(!lua_istable(L,-1)||lua_rawlen(L,-1)>10000)throw std::runtime_error("SpawnUnit DestinationIds requires bounded array");for(size_t i=1,n=lua_rawlen(L,-1);i<=n;++i){lua_rawgeti(L,-1,int(i));id(-1);lua_pop(L,1);}lua_pop(L,1);}
    else{lua_pop(L,1);lua_getfield(L,1,"DestinationId");if(!lua_isnil(L,-1))id(-1);lua_pop(L,1);}
    lua_getfield(L,1,"DestinationNames");if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,1,"DestinationName");}
    const auto groupTargets=[&](int index){if(lua_type(L,index)!=LUA_TSTRING)throw std::runtime_error("SpawnUnit destination names require strings");std::vector<int32_t> group;if(!activeMapThingGroup(L,lua_tostring(L,index),group,error))throw std::runtime_error(error);for(auto v:group)if(std::find(destinations.begin(),destinations.end(),v)==destinations.end())destinations.push_back(v);};
    if(lua_type(L,-1)==LUA_TSTRING)groupTargets(-1);
    else if(!lua_isnil(L,-1)){if(!lua_istable(L,-1)||lua_rawlen(L,-1)>10000)throw std::runtime_error("SpawnUnit DestinationNames requires bounded array");for(size_t i=1,n=lua_rawlen(L,-1);i<=n;++i){lua_rawgeti(L,-1,int(i));groupTargets(-1);lua_pop(L,1);}}
    lua_pop(L,1);
    if(destinations.empty())return true;
    // Native selection uses the original MapThing pointer hash order. Admit
    // its unambiguous one-destination case; never substitute sorted ID order.
    if(destinations.size()!=1){error="SpawnUnit multiple destinations require original MapThing hash ordering";return false;}
    const auto* destination=findObjectSnapshot(L,destinations.front());
    if(!destination||!destination->active||!destination->alive){created=-1;return true;}
    if(boolean("RequireValidLocation")){error="SpawnUnit RequireValidLocation destination point-grid check not reconstructed";return false;}
    float x=0,y=0;if(!actorWorld_.location(destination->id,x,y,error))return false;
    const auto random=[&](double& v,std::string& detail){if(nextRecoveredEngineRandom(state_,v))return true;detail="SpawnUnit native RNG unavailable";return false;};
    double r=0;const float offsetX=number("OffsetX"),offsetY=number("OffsetY");
    //26f01d/26f076 consume a draw even when the source offset is zero.
    if(!random(r,error))return false;x+=float(r)*(offsetX-(-offsetX))+(-offsetX);
    if(!random(r,error))return false;y+=float(r)*(offsetY-(-offsetY))+(-offsetY);
    const auto group=text("Group");const bool inherit=boolean("InheritGroupNames"),presentation=boolean("DoActivatePresentation",true);
    if(!group.empty()){bool exists=false;for(const auto& g:snapshotSceneGroups(L))if(g.name==group)exists=true;if(!exists){lua_getglobal(L,"CreateGroup");lua_newtable(L);lua_pushlstring(L,group.data(),group.size());lua_setfield(L,-2,"Name");if(lua_pcall(L,1,0,0)!=LUA_OK){error=lua_tostring(L,-1);lua_pop(L,1);return false;}}}
    if(!allocateUnitObjectId(L,created,error))return false;
    ObjectSnapshot initial;if(!actorWorld_.initializeDynamicUnit(created,name,x,y,group,initial,weaponCatalog_,random,error))return false;
    if(inherit)for(const auto& g:destination->groups)if(std::find(initial.groups.begin(),initial.groups.end(),g)==initial.groups.end())initial.groups.push_back(g);
    if(!insertActorSnapshot(L,std::move(initial),error))return false;
    auto* object=findObjectSnapshot(L,created);if(!object||!actorWorld_.registerActor(created,*object,error)||!lifeSystem_.registerActor(created,error)||!actorWorld_.setAngle(created,object->angle,error))return false;
    if(!initializeActorLight(created,error)||!initializeInteraction(created,error)||!initializePrimarySpawner(created,error))return false;
    // Thing.Init2dbf47 gates authored ActivateGraphic + ActivateFx only;
    // its base Graphic is always initialized above.
    if(presentation){
        const auto* start=actorWorld_.currentProperty(created,"Thing.ActivateGraphic");const auto* fx=actorWorld_.currentProperty(created,"Thing.ActivateFx");
        if(!start||!fx){error="SpawnUnit activation presentation source fields unavailable";return false;}
        if(!setActorAnimation(created,start->text(),error))return false;
        if(!fx->text().empty()&&fx->text()!="null"){uint64_t handle=0;if(!createActionEffect(fx->text(),created,std::array<float,3>{x,y,0},handle,error))return false;}
    }
    // UnitManager.Add2f9510 adds the actual map thing then ALWAYS OnSpawn;
    // unlike Obstacle creation this is not gated by Thing.TriggerOnSpawn.
    if(!dispatchCallbacks("OnSpawn",name.c_str(),created)){error=error_;return false;}actorWorld_.actor(created)->spawnDispatched=true;
    if(boolean("ForceToValidLocation")){bool valid=false;if(!unitLocationValid(created,x,y,valid,error))return false;if(!valid){error="SpawnUnit ForceToValidLocation relocation search not reconstructed: "+name;return false;}}
    hades::log(hades::Severity::Info,"HadesLua","NATIVE_UNIT_SPAWN id=%d name=%s destination=%d x=%.2f y=%.2f presentation=%s",created,name.c_str(),destination->id,x,y,presentation?"true":"false");
    return true;
}
int ScriptRuntime::spawnUnit(lua_State* L){
    char message[2048]{};bool ok=false;int32_t id=-1;
    try{std::string error;ok=runtime(L)->spawnUnitImpl(L,id,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);lua_pushnumber(L,id);return 1;
}
