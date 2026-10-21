#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include "scene_runtime.hpp"
#include "object_runtime.hpp"
#include "text_runtime.hpp"
#include "input_runtime.hpp"
#include "weapon_catalog.hpp"
#include "delayed_actions.hpp"
#include <cstdio>
#include "../analysis/lua_bridge_contract.hpp"
#include "platform_log.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <vector>
#include <sstream>
extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

namespace {
constexpr const char* tag = "HadesLua";
void log(const char* message) { hades::log(hades::Severity::Info, tag, "%s", message); }
}

ScriptRuntime::ScriptRuntime(hades::AssetStore* assets) : assets_(assets), selfEffects_(actorWorld_), packages_(assets), audio_(assets), movies_(assets) {roomAssets_=std::make_unique<hades::RoomAssets>(*assets,mapName_);assets_=roomAssets_.get();}
ScriptRuntime::~ScriptRuntime() { audioOutput_.stop(); if (state_) lua_close(state_); }
ScriptRuntime* ScriptRuntime::runtime(lua_State* state) {
    return static_cast<ScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
}
int ScriptRuntime::print(lua_State* state) {
    // Use tostring semantics without allocating a string per log line.
    for (int i = 1; i <= lua_gettop(state); ++i) {
        const char* value = lua_tostring(state, i);
        hades::log(hades::Severity::Info, tag, "LUA_PRINT %s", value ? value : luaL_typename(state, i));
    }
    return 0;
}
int ScriptRuntime::debugMessage(lua_State* state) {
    if (lua_istable(state, 1)) {
        lua_getfield(state, 1, "Text");
        const char* text = lua_tostring(state, -1);
        hades::log(hades::Severity::Info, tag, "LUA_DEBUG %s", text ? text : ""); lua_pop(state, 1);
    }
    return 0;
}
int ScriptRuntime::getTime(lua_State* state) {
    lua_pushnumber(state,runtime(state)->worldTime_);return 1;
}
int ScriptRuntime::getMapName(lua_State* state) {
    lua_pushstring(state,runtime(state)->mapName_.c_str());return 1;
}
int ScriptRuntime::getMapThings(lua_State* state) {
    luaL_checktype(state,1,LUA_TTABLE);lua_settop(state,1);
    lua_getfield(state,1,"Names");
    if(lua_isnil(state,-1)){lua_pop(state,1);lua_getfield(state,1,"Name");}
    const int selected=lua_gettop(state);
    if(!lua_isnil(state,selected)&&lua_type(state,selected)!=LUA_TSTRING&&!lua_istable(state,selected))return luaL_error(state,"GetMapThings Name/Names requires string or array");
    const auto count=lua_istable(state,selected)?lua_rawlen(state,selected):0;
    if(count>10000)return luaL_error(state,"GetMapThings group count outside supported bounds");
    for(size_t i=1;i<=count;++i){lua_rawgeti(state,selected,static_cast<int>(i));const bool valid=lua_type(state,-1)==LUA_TSTRING;lua_pop(state,1);if(!valid)return luaL_error(state,"GetMapThings groups require strings");}
    lua_newtable(state);const int result=lua_gettop(state);
    // ExecuteTable274 -> GroupManager::GetAllThings + HasGroupName. The
    // result includes source metadata regardless of active/live entity state.
    for(const auto& object:runtime(state)->mapMetadata_){
        const auto member=[&](const char* name){for(const auto& group:object.groups)if(group==name)return true;return false;};
        bool matches=lua_type(state,selected)==LUA_TSTRING&&member(lua_tostring(state,selected));
        for(size_t i=1;!matches&&i<=count;++i){lua_rawgeti(state,selected,static_cast<int>(i));matches=member(lua_tostring(state,-1));lua_pop(state,1);}
        if(matches){lua_pushstring(state,object.name.c_str());lua_rawseti(state,result,object.id);}
    }
    lua_pushvalue(state,result);lua_setglobal(state,"MapThingsTable");return 1;
}
int ScriptRuntime::debugAssert(lua_State* state) {
    luaL_checktype(state,1,LUA_TTABLE);lua_getfield(state,1,"Condition");
    const bool valid=lua_toboolean(state,-1);lua_pop(state,1);if(valid)return 0;
    lua_getfield(state,1,"Text");if(!lua_isstring(state,-1)){lua_pop(state,1);lua_rawgeti(state,1,1);}
    const char* text=lua_tostring(state,-1);
    return luaL_error(state,"Game assertion: %s",text?text:"condition was false");
}
int ScriptRuntime::setRichPresence(lua_State* state) {
    luaL_checktype(state,1,LUA_TTABLE);
    lua_settop(state,1);
    lua_getfield(state,1,"Key");lua_getfield(state,1,"Value");
    for(int index=2;index<=3;++index){
        if(lua_isnil(state,index)){lua_pushliteral(state,"");lua_replace(state,index);}
        luaL_checktype(state,index,LUA_TSTRING);
        luaL_argcheck(state,lua_rawlen(state,index)<=65536,1,"presence string exceeds bounds");
    }
    // Android offline platform adapter: retain the script's presence state
    // locally. There is no Steam/social backend or external publication.
    // Native Execute192 reads Key/Value strings with empty defaults.
    lua_getfield(state,LUA_REGISTRYINDEX,"hades.localPresence");
    if(lua_isnil(state,-1)){lua_pop(state,1);lua_newtable(state);lua_pushvalue(state,-1);lua_setfield(state,LUA_REGISTRYINDEX,"hades.localPresence");}
    lua_pushvalue(state,2);lua_pushvalue(state,3);lua_settable(state,-3);
    return 0;
}
int ScriptRuntime::unsupported(lua_State* state) {
    const char* action = lua_tostring(state, lua_upvalueindex(1));
    hades::log(hades::Severity::Error, tag, "UNIMPLEMENTED_ENGINE_ACTION name=%s", action);
    if(action&&std::strcmp(action,"PlaySpeech")==0&&lua_istable(state,1)){
        for(const auto* key:{"Name","Id","Queue","Actor","UseSubtitles","SubtitleColor"}){
            lua_getfield(state,1,key);
            const auto* value=lua_tostring(state,-1);
            hades::log(hades::Severity::Info,tag,"PLAY_SPEECH_REQUEST key=%s type=%s value=%s bool=%d",key,luaL_typename(state,-1),value?value:"<non-scalar>",lua_toboolean(state,-1));lua_pop(state,1);
        }
    }
    if(action&&std::strcmp(action,"PlaySound")==0&&lua_istable(state,1)){
        lua_getfield(state,1,"Name");const char* cue=lua_tostring(state,-1);hades::log(hades::Severity::Info,tag,"PLAY_SOUND_REQUEST cue=%s",cue?cue:"<nil>");lua_pop(state,1);
    }
    if(action&&std::strcmp(action,"PreLoadBinks")==0&&lua_istable(state,1)){
        lua_getfield(state,1,"Cache");const char* cache=lua_tostring(state,-1);const std::string selected=cache?cache:"";lua_pop(state,1);
        lua_getfield(state,1,"Names");if(lua_istable(state,-1))for(size_t i=1,n=std::min(size_t(lua_rawlen(state,-1)),size_t(1000));i<=n;++i){lua_rawgeti(state,-1,int(i));const char* name=lua_tostring(state,-1);if(name)hades::log(hades::Severity::Info,tag,"PRELOAD_MOVIE_REQUEST cache=%s index=%zu name=%s",selected.c_str(),i,name);lua_pop(state,1);}lua_pop(state,1);
    }
    return luaL_error(state, "Native engine action not reconstructed yet: %s", action);
}
int ScriptRuntime::trigger(lua_State* state) {
    // Matching Script ctor retains the function values from argument1; the
    // trigger returns the Lua registry reference, or -1 without a callable.
    const char* name = lua_tostring(state, lua_upvalueindex(1));
    if(!lua_istable(state,1)){lua_pushinteger(state,-1);return 1;}
    int callback=LUA_NOREF;
    lua_pushnil(state);
    while(lua_next(state,1)){
        if(lua_isfunction(state,-1)){
            if(callback!=LUA_NOREF)luaL_unref(state,LUA_REGISTRYINDEX,callback);
            lua_pushvalue(state,-1);callback=luaL_ref(state,LUA_REGISTRYINDEX);
        }
        lua_pop(state,1);
    }
    if(callback==LUA_NOREF){lua_pushinteger(state,-1);return 1;}
    lua_getfield(state, LUA_REGISTRYINDEX, "hades.triggerRegistrations");
    lua_getfield(state, -1, name);
    if (lua_isnil(state, -1)) { lua_pop(state, 1); lua_newtable(state); }
    const int next = static_cast<int>(lua_rawlen(state, -1)) + 1;
    lua_newtable(state);
    lua_pushvalue(state,1);lua_setfield(state,-2,"definition");
    lua_pushinteger(state,callback);lua_setfield(state,-2,"callback");
    lua_Debug caller{};
    if(lua_getstack(state,1,&caller)&&lua_getinfo(state,"Sl",&caller)){
        lua_pushstring(state,caller.source);lua_setfield(state,-2,"source");
        lua_pushinteger(state,caller.currentline);lua_setfield(state,-2,"line");
    }
    lua_rawseti(state, -2, next);
    lua_setfield(state, -2, name); lua_pop(state, 1);
    lua_pushinteger(state,callback);return 1;
}
int ScriptRuntime::import(lua_State* state) {
    const char* name = luaL_checkstring(state, 1);
    if (!runtime(state)->load(name,state)) return luaL_error(state, "%s", runtime(state)->error_.c_str());
    return 0;
}
bool ScriptRuntime::load(const char* name,lua_State* caller) {
    lua_State* vm=caller?caller:state_;
    if (!name || !*name || std::strchr(name, '/') || std::strchr(name, '\\') || std::strstr(name, "..")) {
        error_ = "Invalid original script name"; return false;
    }
    const std::string scriptName = std::strchr(name, '.') ? std::string(name) : std::string(name) + ".lua";
    if (loaded_.contains(scriptName)) return true;
    const std::string path = std::string("Scripts/") + scriptName;
    auto asset = assets_->open(path);
    if (!asset) { error_ = "Original script missing: " + path; return false; }
    const auto size = asset->size();
    if (size <= 0 || size > 32 * 1024 * 1024) { asset.reset(); error_ = "Script size outside bounds"; return false; }
    std::vector<char> bytes(static_cast<size_t>(size));
    size_t position = 0;
    while (position < bytes.size()) {
        size_t count = asset->read(bytes.data() + position, bytes.size() - position);
        if (count <= 0) { asset.reset(); error_ = "Truncated script: " + path; return false; }
        position += static_cast<size_t>(count);
    }
    asset.reset();
    const std::string chunkName = "@" + path;
    // UTF-8 BOM is an encoding marker, not Lua code. Preserve the original asset
    // bytes and skip only the marker in the compiler view (luaL_loadfile also does this).
    const size_t bom = bytes.size() >= 3 && static_cast<uint8_t>(bytes[0]) == 0xef &&
        static_cast<uint8_t>(bytes[1]) == 0xbb && static_cast<uint8_t>(bytes[2]) == 0xbf ? 3 : 0;
    int result = luaL_loadbufferx(vm, bytes.data() + bom, bytes.size() - bom, chunkName.c_str(), "t");
    if (result == LUA_OK) {
        loaded_.insert(scriptName); // Imports can form cycles; preserve an in-progress marker.
        result = lua_pcall(vm, 0, 0, 0);
    }
    if (result != LUA_OK) {
        const char* detail = lua_tostring(vm, -1);
        error_ = detail ? detail : "Unknown Lua error"; lua_pop(vm, 1); loaded_.erase(scriptName); return false;
    }
    hades::log(hades::Severity::Info, tag, "ORIGINAL_SCRIPT_LOADED name=%s", name);
    return true;
}
void ScriptRuntime::reportError(const char* stage) {
    hades::log(hades::Severity::Error, tag, "SCRIPT_STAGE_BLOCKED stage=%s detail=%s", stage, error_.c_str());
}
void ScriptRuntime::initialize() {
    if (state_) return;
    state_ = luaL_newstate();
    if (!state_) { error_ = "Lua allocation failed"; reportError("create"); return; }
    luaL_openlibs(state_);
    lua_newtable(state_); lua_setfield(state_, LUA_REGISTRYINDEX, "hades.triggerRegistrations");
    for (const auto& action : hades::contract::actions) {
        lua_pushstring(state_, action.name); lua_pushcclosure(state_, unsupported, 1); lua_setglobal(state_, action.name);
    }
    for (const auto& event : hades::contract::triggers) {
        lua_pushstring(state_, event.name); lua_pushcclosure(state_, trigger, 1); lua_setglobal(state_, event.name);
    }
    lua_pushlightuserdata(state_, this); lua_pushcclosure(state_, import, 1); lua_setglobal(state_, "Import");
    lua_pushcfunction(state_, print); lua_setglobal(state_, "print");
    lua_pushcfunction(state_, debugMessage); lua_setglobal(state_, "DebugMessage");
    lua_pushcfunction(state_, debugMessage); lua_setglobal(state_, "DebugPrint");
    // Reconstruction debugging policy: preserve game assertion checks and report
    // a failed condition. This does not emulate a release build's crash UI.
    lua_pushcfunction(state_,debugAssert);lua_setglobal(state_,"DebugAssert");
    lua_pushcfunction(state_,setRichPresence);lua_setglobal(state_,"SetRichPresence");
    lua_pushboolean(state_, 0); lua_setglobal(state_, "verboseLogging");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,getTime,1); lua_setglobal(state_, "GetTime");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,getMapName,1);lua_setglobal(state_,"GetMapName");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,getMapThings,1);lua_setglobal(state_,"GetMapThings");
    registerRecoveredRuntime(state_);
    // Reconstruction currently selects English explicitly. The original action
    // returns Localization::UiLang, not the compiled empty Language override.
    if (!configureRecoveredLanguage(state_, "en")) { error_="English locale setup failed"; reportError("locale"); return; }
    registerSceneRuntime(state_);
    registerObjectRuntime(state_);
    setObjectDestroy(state_,[this](int32_t id,bool noSound,std::string& error){return markThingDead(id,noSound,error);});
    setObjectAnimationSet(state_,[this](int32_t id,const std::string& name,std::string& error){return setActorAnimation(id,name,error);});
    setObjectGeometryChanged(state_,[this](int32_t id,std::string& error){return actorWorld_.refreshGeometry(id,error);});
    setObjectLocation(state_,[this](int32_t id,float& x,float& y,std::string& error){return actorWorld_.location(id,x,y,error);});
    actorWorld_.setObstacleLocationChanged([this](int32_t id,bool force,std::string& error){return actorWorld_.obstacleLocationChanged(id,force,error);});
    actorWorld_.setLocationAuxiliary([this](int32_t id,hades::ActorLocationStage stage,std::string& error){return locationAuxiliary(id,stage,error);});
    actorWorld_.setPrimaryUntargetableProvider([this](int32_t id,bool& value,std::string& error){return primaryUntargetable(id,value,error);});
    setObjectFractionChanged(state_,[this](int32_t id,bool scale,float target,const hades::BodyTransitionOptions& options,std::string& error){return actorWorld_.adjustFraction(id,scale,target,options,error);});
    registerTextRuntime(state_);
    configureTextControlLabels(state_,controlLabelProvider_);
    registerInputRuntime(state_);
    // Explicit local-player policy for the Android reconstruction. The module
    // itself does not invent player slots; constructor defaults are recovered.
    if(!createRecoveredInputPlayer(state_,1,error_)){reportError("local-player");return;}
    {
        auto groups=assets_->open("groups.bin");
        if(!groups||groups->size()>16*1024*1024){error_="Missing or oversized original scene groups";reportError("scene-groups");return;}
        std::vector<uint8_t> bytes(groups->size());size_t at=0;
        while(at<bytes.size()){
            const auto got=groups->read(bytes.data()+at,bytes.size()-at);
            if(!got){error_="Truncated scene groups";reportError("scene-groups");return;}at+=got;
        }
        if(!loadSceneGroupsSGP1(state_,bytes,error_)){reportError("scene-groups");return;}
        for(const auto& group:snapshotSceneGroups(state_))originalSceneGroupNames_.push_back(group.name);
    }
    if(!loadMapObjects()){reportError("map-objects");return;}
    if(!loadWeaponDefinitions()){reportError("weapon-definitions");return;}
    if(!loadProjectileDefinitions()){reportError("projectile-definitions");return;}
    try{
        if(!animationBank_){animationBank_=std::make_shared<hades::ActorAnimationBank>();animationBank_->load(*assets_);}
        markers_.load(*assets_,*animationBank_);
    }catch(const std::exception& e){error_=e.what();reportError("actor-animation-bank");return;}
    if(!actorWorld_.setAnimationBank(animationBank_,error_)){reportError("actor-animation-bank");return;}
    try{colorGrading_.load(*assets_);loadBloomDefinitions(*assets_);}catch(const std::exception&e){error_=e.what();reportError("color-grading-resources");return;}
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,bloomAction,1);lua_setglobal(state_,"AdjustFullscreenBloom");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,colorGradingAction,1);lua_setglobal(state_,"AdjustColorGrading");
    if(!loadActorDefinitions()){reportError("actor-definitions");return;}
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,spawnObstacle,1);lua_setglobal(state_,"SpawnObstacle");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,spawnUnit,1);lua_setglobal(state_,"SpawnUnit");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,fireWeaponFromUnitAction,1);lua_setglobal(state_,"FireWeaponFromUnit");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,equipWeapon,1);lua_setglobal(state_,"EquipWeapon");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,runWeaponMethod,1);lua_setglobal(state_,"RunWeaponMethod");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,setUnitProperty,1);lua_setglobal(state_,"SetUnitProperty");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,setLifeProperty,1);lua_setglobal(state_,"SetLifeProperty");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,setThingProperty,1);lua_setglobal(state_,"SetThingProperty");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,setObstacleProperty,1);lua_setglobal(state_,"SetObstacleProperty");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,getUnitDataValue,1);lua_setglobal(state_,"GetUnitDataValue");
    {const char* names[]{"SetWeaponProperty","GetWeaponDataValue","GetWeaponProperty","SetProjectileProperty"};
     for(int mode=0;mode<4;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,weaponProperty,2);lua_setglobal(state_,names[mode]);}}
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,createAnimation,1);lua_setglobal(state_,"CreateAnimation");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,rumbleAction,1);lua_setglobal(state_,"Rumble");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,playerInputNotifyAction,1);lua_setglobal(state_,"NotifyOnPlayerInput");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,stoppedNotifyAction,1);lua_setglobal(state_,"NotifyOnStopped");
    {const char* names[]{"CanAttack","IsCharmed"};for(int mode=0;mode<2;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,unitAIQueryAction,2);lua_setglobal(state_,names[mode]);}}
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,distanceQueryAction,1);lua_setglobal(state_,"GetDistance");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,intersectingIdsAction,1);lua_setglobal(state_,"GetIntersectingIds");
    {const char* names[]{"NotifyOnCanAttack","NotifyOnRotationComplete"};for(int mode=1;mode<=2;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,unitStateNotifyAction,2);lua_setglobal(state_,names[mode-1]);}}
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,setColor,1);lua_setglobal(state_,"SetColor");
    {const char* names[]{"PlaySound","PauseSound","ResumeSound","StopSound","SetVolume","SetSoundCueValue","SwapSound","SetAudioEffectState","GetMixingId","SetSoundSource"};
     for(int mode=0;mode<10;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,audioAction,2);lua_setglobal(state_,names[mode]);}}
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,applyUpwardForce,1);lua_setglobal(state_,"ApplyUpwardForce");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,applyForceAction,1);lua_setglobal(state_,"ApplyForce");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,attach,1);lua_setglobal(state_,"Attach");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,switchActiveUnit,1);lua_setglobal(state_,"SwitchActiveUnit");
    for(int mode=0;mode<2;++mode){lua_pushlightuserdata(state_,this);lua_pushboolean(state_,mode);lua_pushcclosure(state_,inactiveIds,2);lua_setglobal(state_,mode?"GetInactiveIdsByType":"GetInactiveIds");}
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,preloadBinks,1);lua_setglobal(state_,"PreLoadBinks");
    for(int mode=0;mode<2;++mode){lua_pushlightuserdata(state_,this);lua_pushboolean(state_,mode);lua_pushcclosure(state_,fade,2);lua_setglobal(state_,mode?"FadeIn":"FadeOut");}
    for(int mode=0;mode<2;++mode){lua_pushlightuserdata(state_,this);lua_pushboolean(state_,mode);lua_pushcclosure(state_,collisionNotifyAction,2);lua_setglobal(state_,mode?"NotifyNotColliding":"NotifyOnCollide");}
    for(int mode=0;mode<2;++mode){lua_pushlightuserdata(state_,this);lua_pushboolean(state_,mode);lua_pushcclosure(state_,controlNotifyAction,2);lua_setglobal(state_,mode?"NotifyOnControlReleased":"NotifyOnControlPressed");}
    {const char* names[]{"GetClosest","GetAngleBetween","GetClosestUnitOfType"};for(int mode=0;mode<3;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,spatialQueryAction,2);lua_setglobal(state_,names[mode]);}}
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,allDeadNotifyAction,1);lua_setglobal(state_,"NotifyOnAllDead");
    // ScriptAction::Execute VA180253381/180253443 -> World::AddScriptUpdateGameEvent(CheckWithinDistanceAverage/Any).
    {const char* names[]{"NotifyWithinDistance","NotifyWithinDistanceAny","NotifyOutsideDistance"};for(int mode=1;mode<=3;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,distanceNotifyAction,2);lua_setglobal(state_,names[mode-1]);}}
    // ExecuteBool cases 0xdd IsAlive / 0xe2 IsHorizontallyFlipped (ActionToString indices); ScriptAction::GetAngle VA18026cfd0.
    {const char* names[]{"IsAlive","IsHorizontallyFlipped","GetAngle"};for(int mode=0;mode<3;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,thingQueryAction,2);lua_setglobal(state_,names[mode]);}}
    if(!loadPackageDefinitions()){reportError("package-definitions");return;}
    if(!loadEffectDefinitions()){reportError("effect-definitions");return;}
    if(!loadActorLightDefinitions()){reportError("actor-light-definitions");return;}
    if(!loadScenePrimaryDefinitions()){reportError("scene-primary-definitions");return;}
    if(!loadActorAnimationDefinitions()){reportError("actor-animation-definitions");return;}
    if(!loadAudioDefinitions()){reportError("audio-definitions");return;}
    initializeSpeechBridge();
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,speechAction,1);lua_setglobal(state_,"PlaySpeech");
    {const char* names[]{"PauseSpeech","ResumeSpeech","StopSpeech"};for(int mode=0;mode<3;++mode){lua_pushlightuserdata(state_,this);lua_pushinteger(state_,mode);lua_pushcclosure(state_,speechControlAction,2);lua_setglobal(state_,names[mode]);}}
    if(!loadMovieDefinitions()){reportError("movie-definitions");return;}
    registerCameraActions();
    registerActorMotionActions();
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,stopAnimation,1);lua_setglobal(state_,"StopAnimation");
    registerProjectileActions();
    registerSaveActions(); // luabins module + SaveCheckpoint/ValidateCheckpoint (lua_save.cpp)
    registerMapActions();
    registerFlashActions();
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,simulationSpeedAction,1);lua_setglobal(state_,"AdjustSimulationSpeed");
    registerShakeAction();
    registerFullscreenBlurActions();
    // Wrap after every actual native action is registered, including text and
    // speech controls; actions added after this point would bypass source Delay.
    if(!installDelayedActions()){reportError("delayed-actions");return;}
    mainLoaded_ = load("Main.lua");
    if (!mainLoaded_) { reportError("Main.lua"); return; }
    if(!initializeScriptScheduler(state_,error_)){reportError("scheduler");return;}
    log("LUA_VM_READY version=5.2.2 originalMain=true registryActions=319 registryTriggers=71");
    gameLoaded_ = load("RoomManager.lua");
    if (!gameLoaded_) reportError("RoomManager.lua");
    else log("ORIGINAL_GAME_SCRIPTS_READY");
}
std::string ScriptRuntime::sceneGroupsDump() const { return state_?dumpSceneGroups(state_):"SCENE_GROUPS unavailable\n"; }
std::string ScriptRuntime::objectsDump() const { return state_?dumpObjectRuntime(state_):"OBJECTS unavailable\n"; }
std::string ScriptRuntime::textFormatsDump() const { return state_?dumpTextRuntime(state_):"TEXT_FORMATS unavailable\n"; }
std::string ScriptRuntime::inputDump() const { return state_?dumpInputRuntime(state_):"INPUT unavailable\n"; }
std::string ScriptRuntime::mapCreationDump() const {return dumpMapCreationPlan(creationPlan_);}
std::string ScriptRuntime::stagedWeaponsDump() const {
    std::ostringstream out;out<<"STAGED_UNIT_ARSENALS units="<<stagedUnitArsenals_.size()<<" map_activation=false\n";
    for(const auto& [id,arsenal]:stagedUnitArsenals_){(void)id;out<<arsenal.dump();}
    out<<diagnosticWeaponDump_;return out.str();
}
bool ScriptRuntime::loadWeaponDefinitions(){
    auto asset=assets_->open("weapons.bin");
    if(!asset||asset->size()<104||asset->size()>16*1024*1024){error_="Missing or oversized source weapon initialization catalog";return false;}
    std::vector<uint8_t> bytes(asset->size());size_t at=0;
    while(at<bytes.size()){const auto got=asset->read(bytes.data()+at,bytes.size()-at);if(!got){error_="Truncated weapon initialization asset";return false;}at+=got;}
    auto catalog=std::make_shared<hades::WeaponCatalog>();
    if(!hades::loadWeaponCatalog(bytes,*catalog,error_))return false;
    weaponDefinitions_=catalog->weapons;weaponCatalog_=std::move(catalog);
    hades::log(hades::Severity::Info,tag,"WEAPON_CATALOG_READY count=%zu firing_supported=false",weaponDefinitions_.size());return true;
}
bool ScriptRuntime::loadProjectileDefinitions(){
    auto asset=assets_->open("projectiles.lua");if(!asset||asset->size()>16*1024*1024){error_="Missing or oversized original projectile Thing catalog";return false;}
    std::vector<char> bytes(asset->size());size_t at=0;while(at<bytes.size()){const auto got=asset->read(bytes.data()+at,bytes.size()-at);if(!got){error_="Truncated projectile Thing catalog";return false;}at+=got;}
    const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@projectiles.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);
    if(status!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    hades::SourceValue catalog;const bool ok=hades::readSourceValue(state_,-1,catalog,error_);lua_settop(state_,top);lua_gc(state_,LUA_GCCOLLECT,0);
    return ok&&projectileWorld_.loadCatalog(std::move(catalog),error_);
}
bool ScriptRuntime::stageUnitArsenals(){
    // Native Unit::Init assigns Arsenal.pOwner and creates definition weapons
    // before UnitManager::Add. Stage only this owned component; no active
    // Thing handle, native body, definition weapon list or OnSpawn is implied.
    std::map<int32_t,hades::WeaponArsenal> pending;
    for(const auto& entry:creationPlan_.creation){
        if(entry.kind==MapCreationKind::Obstacle)continue;
        const auto& metadata=mapMetadata_.at(entry.sourceOrdinal);
        if(metadata.id!=entry.id){error_="Staged unit/metadata identity mismatch";return false;}
        pending.try_emplace(entry.id,entry.id,metadata.name);
    }
    stagedUnitArsenals_=std::move(pending);
    hades::log(hades::Severity::Info,tag,"UNIT_ARSENALS_STAGED units=%zu live_units=0 world_registered=false",stagedUnitArsenals_.size());return true;
}
bool ScriptRuntime::probeStagedWeaponInitialization(){
    if(!roomBooted_){error_="Staged weapon probe requires original precreation";return false;}
    const auto weapon=std::find_if(weaponDefinitions_.begin(),weaponDefinitions_.end(),[](const auto& d){return d.name=="ArmorBreakAttack";});
    if(weapon==weaponDefinitions_.end()){error_="Original first weapon is absent from catalog";return false;}
    for(const auto& entry:creationPlan_.creation)if(entry.kind==MapCreationKind::PlayerUnit){
        // Original Unit::Init still must initialize its definition weapons;
        // ArmorBreakAttack is equipped later by real OnAnyLoad. Debug a copy
        // so diagnostic equipment can never leak into a future live boot.
        auto arsenal=stagedUnitArsenals_.at(entry.id);
        if(!arsenal.equip(*weapon,error_))return false;
        diagnosticWeaponDump_="DIAGNOSTIC_WEAPON_COMPONENT_COPY original_lua_equip_not_dispatched=true\n"+arsenal.dump();
        hades::log(hades::Severity::Info,tag,"DIAGNOSTIC_STAGED_WEAPON_READY owner_source_id=%d weapon=%s live_unit=false combat=false",entry.id,weapon->name.c_str());return true;
    }
    error_="No source player-unit component was staged";return false;
}
bool ScriptRuntime::inputAllowed(int index)const {return state_&&isRecoveredInputAllowed(state_,index);}
bool ScriptRuntime::loadMapObjects() {
    const int top=lua_gettop(state_);
    auto asset=assets_->open("DecodedMapObjects.lua");
    if(!asset||asset->size()>16*1024*1024){error_="Missing or oversized decoded map metadata";return false;}
    std::vector<char> bytes(asset->size());size_t at=0;
    while(at<bytes.size()){const auto got=asset->read(bytes.data()+at,bytes.size()-at);if(!got){error_="Truncated map metadata";return false;}at+=got;}
    int result=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@DecodedMapObjects.lua","t");
    if(result==LUA_OK)result=lua_pcall(state_,0,1,0);
    if(result!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    if(!lua_istable(state_,-1)||lua_rawlen(state_,-1)>100000){error_="Invalid map object table";lua_settop(state_,top);return false;}
    std::vector<ObjectSnapshot> objects;std::vector<MapCreationRecord> creationRecords;
    for(size_t i=1,count=lua_rawlen(state_,-1);i<=count;++i){
        lua_rawgeti(state_,-1,static_cast<int>(i));
        if(!lua_istable(state_,-1)){error_="Invalid map object record";lua_settop(state_,top);return false;}
        auto number=[&](const char* name){lua_getfield(state_,-1,name);const double value=lua_tonumber(state_,-1);lua_pop(state_,1);return value;};
        auto boolean=[&](const char* name){lua_getfield(state_,-1,name);const bool value=lua_toboolean(state_,-1);lua_pop(state_,1);return value;};
        auto string=[&](const char* name){lua_getfield(state_,-1,name);const char* value=lua_tostring(state_,-1);std::string text=value?value:"";lua_pop(state_,1);return text;};
        ObjectSnapshot object;object.id=static_cast<int32_t>(number("id"));object.name=string("name");object.group=string("group");
        lua_getfield(state_,-1,"groups");
        if(lua_istable(state_,-1)){
            const auto count=lua_rawlen(state_,-1);
            if(count>256){error_="Map object group count outside bounds";lua_settop(state_,top);return false;}
            for(size_t group=1;group<=count;++group){
                lua_rawgeti(state_,-1,static_cast<int>(group));
                if(lua_type(state_,-1)!=LUA_TSTRING){error_="Invalid map group membership";lua_settop(state_,top);return false;}
                object.groups.emplace_back(lua_tostring(state_,-1));lua_pop(state_,1);
            }
        }
        lua_pop(state_,1);
        object.x=number("x");object.y=number("y");object.z=number("z");object.scale=number("scale");object.alpha=number("alpha");object.angle=number("angle");
        object.active=boolean("active");object.doCreate=boolean("doCreate");object.visible=boolean("visible");object.alive=false;object.fromMap=true;
        object.flipHorizontal=boolean("flipHorizontal");
        MapCreationRecord record;record.id=object.id;record.name=object.name;record.sourceActive=object.active;record.doCreatePresence=object.doCreate;
        record.activateAtRange=boolean("activateAtRange");record.activationRange=number("activationRange");record.x=object.x;record.y=object.y;
        record.parentId=static_cast<int32_t>(number("parentId"));record.dataType=static_cast<int32_t>(number("dataType"));record.triggerOnSpawn=boolean("triggerOnSpawn");
        lua_getfield(state_,-1,"playerControlled");
        if(!lua_isnil(state_,-1)){
            if(!lua_isboolean(state_,-1)){error_="Invalid resolved PlayerControlled property";lua_settop(state_,top);return false;}
            record.playerControlled=static_cast<bool>(lua_toboolean(state_,-1));
        }
        lua_pop(state_,1);lua_getfield(state_,-1,"childIds");
        if(!lua_istable(state_,-1)||lua_rawlen(state_,-1)>100000){error_="Missing or invalid map attachment list";lua_settop(state_,top);return false;}
        for(size_t child=1,count=lua_rawlen(state_,-1);child<=count;++child){
            lua_rawgeti(state_,-1,static_cast<int>(child));const auto id=lua_tonumber(state_,-1);
            if(lua_type(state_,-1)!=LUA_TNUMBER||id<=0||id>INT32_MAX||id!=std::trunc(id)){error_="Invalid map child identity";lua_settop(state_,top);return false;}
            record.childIds.push_back(static_cast<int32_t>(id));lua_pop(state_,1);
        }
        lua_pop(state_,1);creationRecords.push_back(std::move(record));
        objects.push_back(std::move(object));lua_pop(state_,1);
    }
    lua_settop(state_,top);
    // Decoding/planning is separate from native entity/component creation. No
    // original map ID is exposed as a live Thing during precreation.
    const MapCreationContext initialContext{};
    if(!planMapCreation(creationRecords,initialContext,creationPlan_,error_))return false;
    mapMetadata_=std::move(objects);
    markObjectMapMetadataLoaded(state_);
    hades::log(hades::Severity::Info,tag,"MAP_METADATA_READY count=%zu planned=%zu deferred=%zu live_map_entities=0",mapMetadata_.size(),creationPlan_.creation.size(),creationPlan_.deferred.size());return true;
}
std::string ScriptRuntime::stateSummary() const {
    if(!state_)return "LUA_STATE unavailable";
    std::ostringstream out;out<<"LUA_STATE";
    for(const char* name:{"GameState","CurrentRun","ScreenAnchors","ConfigOptionCache"}){
        lua_getglobal(state_,name);out<<' '<<name<<'='<<luaL_typename(state_,-1);
        if(lua_istable(state_,-1)){
            size_t keys=0;lua_pushnil(state_);while(lua_next(state_,-2)){++keys;lua_pop(state_,1);}out<<"(keys="<<keys<<')';
        }
        lua_pop(state_,1);
    }
    return out.str();
}
bool ScriptRuntime::dispatchCallbacks(const char* event,const char* sourceName,int32_t actorId,const TriggerArguments& extra,bool filterPlayerIndex) {
    const bool thingMatch=thingSourceMatch_;thingSourceMatch_=false; // consumed once: nested dispatches keep their own matching
    const int top=lua_gettop(state_);
    lua_getfield(state_,LUA_REGISTRYINDEX,"hades.triggerRegistrations");
    lua_getfield(state_,-1,event);
    if(!lua_istable(state_,-1)){lua_settop(state_,top);return true;}
    const int list=lua_gettop(state_);const auto count=lua_rawlen(state_,list);
    for(size_t i=1;i<=count;++i){
        lua_rawgeti(state_,list,static_cast<int>(i));const int record=lua_gettop(state_);
        lua_getfield(state_,record,"definition");lua_rawgeti(state_,-1,1);
        bool matches=true;
        if(lua_type(state_,-1)==LUA_TSTRING&&lua_rawlen(state_,-1)>0){
            if(!sourceName){error_="Filtered pre-creation trigger requires source reconstruction";lua_settop(state_,top);reportError(event);return false;}
            const std::string filter=lua_tostring(state_,-1);
            // This map-load dispatcher supports source-name lists only. Native
            // parseSource splits ASCII spaces and stops at a literal ; token.
            // Numeric/comparison filters belong to other trigger contracts.
            matches=false;size_t at=0;
            while(at<filter.size()){
                at=filter.find_first_not_of(' ',at);if(at==std::string::npos)break;
                auto end=filter.find(' ',at);if(end==std::string::npos)end=filter.size();
                const auto token=filter.substr(at,end-at);at=end;
                if(token==";")break;
                if(thingMatch){bool hit=false;if(!actorId){error_="Thing-sourced trigger without source Thing";lua_settop(state_,top);reportError(event);return false;}
                    if(token.find_first_of("<>!=")==0){error_="Unsupported Script::isSource comparison token: "+token;lua_settop(state_,top);reportError(event);return false;}
                    std::string detail;hit=sourceTokenMatches(token,sourceName,actorId,detail);if(!detail.empty()){error_=detail;lua_settop(state_,top);reportError(event);return false;}
                    if(hit)matches=true;continue;}
                if(token.find_first_of("<>!=0123456789")==0){error_="Unsupported map trigger source filter: "+token;lua_settop(state_,top);reportError(event);return false;}
                if(token==sourceName)matches=true;
            }
        }
        lua_pop(state_,2);
        if(matches&&filterPlayerIndex){
            // Script ctor24c9a0 recognizes case-insensitive numeric PlayerIndex.
            // Weapon charging/canceled/fired filter only when owner has pPlayer.
            int32_t selectedPlayer=0;lua_getfield(state_,record,"definition");const int definition=lua_gettop(state_);
            lua_pushnil(state_);while(lua_next(state_,definition)){
                if(lua_type(state_,-2)==LUA_TSTRING&&lua_type(state_,-1)==LUA_TNUMBER){
                    std::string key=lua_tostring(state_,-2);for(char& c:key)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
                    if(key=="playerindex"){const auto index=lua_tonumber(state_,-1);if(!std::isfinite(index)||index<INT32_MIN||index>INT32_MAX){error_="Trigger PlayerIndex outside native signed range";lua_settop(state_,top);return false;}selectedPlayer=int32_t(index);}
                }lua_pop(state_,1);
            }lua_pop(state_,1);
            if(selectedPlayer>0)for(const auto& player:snapshotInputPlayers(state_))if(player.activeUnitId==actorId){const auto* actor=actorWorld_.actor(actorId);if(actor&&actor->entityHandle==player.activeUnitLifetime&&selectedPlayer!=int32_t(player.playerIndex))matches=false;}
        }
        if(!matches){lua_pop(state_,1);continue;}
        lua_getfield(state_,record,"callback");const int ref=static_cast<int>(lua_tointeger(state_,-1));lua_pop(state_,1);
        lua_getfield(state_,record,"source");const std::string source=lua_isstring(state_,-1)?lua_tostring(state_,-1):"unknown";lua_pop(state_,1);
        lua_getfield(state_,record,"line");const int line=static_cast<int>(lua_tointeger(state_,-1));lua_pop(state_,1);
        lua_getglobal(state_,"dispatch");lua_rawgeti(state_,LUA_REGISTRYINDEX,ref);
        lua_newtable(state_);
        if(sourceName){lua_pushstring(state_,sourceName);lua_setfield(state_,-2,"name");}
        if(actorId){
            lua_pushinteger(state_,actorId);lua_setfield(state_,-2,"triggeredById");
            pushObjectLuaTable(state_,actorId);lua_setfield(state_,-2,"TriggeredByTable");
            if(std::strcmp(event,"OnSpawn")==0){lua_pushboolean(state_,false);lua_setfield(state_,-2,"NeedsLuaInitialization");}
        }
        if(extra&&!extra(state_,error_)){lua_settop(state_,top);reportError(event);return false;}
        lua_pushvalue(state_,-1);lua_setglobal(state_,"global_triggerArgs");
        hades::log(hades::Severity::Info,tag,"ORIGINAL_TRIGGER_DISPATCH event=%s index=%zu map=%s source=%s:%d",event,i,mapName_.c_str(),source.c_str(),line);
        if(lua_pcall(state_,2,0,0)!=LUA_OK){
            const char* detail=lua_tostring(state_,-1);error_=detail?detail:"Unknown callback error";
            lua_settop(state_,top);reportError(event);return false;
        }
        lua_pop(state_,1);
    }
    lua_settop(state_,top);return true;
}
bool ScriptRuntime::bootRoom() {
    if(!gameLoaded_||roomBooted_)return roomBooted_;
    if(!dispatchCallbacks("OnPreThingCreation",nullptr))return false;
    if(!stageUnitArsenals())return false;
    roomBooted_=true;log("ORIGINAL_ROOM_PRE_CREATION_READY");return true;
}
bool ScriptRuntime::probeLoadCallbacks() {
    if(!roomBooted_){error_="Load callback probe requires successful pre-creation";return false;}
    if(!mapProbeInstalled_){
        auto proxies=mapMetadata_;
        for(auto& proxy:proxies){proxy.active=false;proxy.alive=false;proxy.queryProxy=true;}
        for(const auto& entry:creationPlan_.creation)for(auto& proxy:proxies)if(proxy.id==entry.id){proxy.active=true;proxy.alive=true;break;}
        if(!seedObjectRuntime(state_,proxies,error_)){reportError("diagnostic-map-query-proxies");return false;}
        mapProbeInstalled_=true;
    }
    log("DIAGNOSTIC_CALLBACK_REPLAY event=OnAnyLoad map_activation=not_performed OnSpawn=not_dispatched");
    // Map::Begin uses OnAnyLoad before OnLoad and aborts on failure. This probe
    // deliberately stops before OnLoad: actual World::Start has not occurred.
    const bool result=dispatchCallbacks("OnAnyLoad",mapName_.c_str());
    hades::log(hades::Severity::Info,tag,"DIAGNOSTIC_CALLBACK_REPLAY_COMPLETE callbacks=%s room_gameplay_ready=false",result?"completed":"blocked");
    return result;
}

bool ScriptRuntime::loadActorDefinitions(){
    auto asset=assets_->open("actors.lua");
    if(!asset||asset->size()>32*1024*1024){error_="Missing or oversized native actor source catalog";return false;}
    std::vector<char> bytes(asset->size());size_t at=0;
    while(at<bytes.size()){const auto got=asset->read(bytes.data()+at,bytes.size()-at);if(!got){error_="Truncated native actor catalog";return false;}at+=got;}
    const int top=lua_gettop(state_);int result=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@actors.lua","t");
    if(result==LUA_OK)result=lua_pcall(state_,0,1,0);
    if(result!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    hades::SourceValue catalog;bool ok=hades::readSourceValue(state_,-1,catalog,error_);
    lua_settop(state_,top);lua_gc(state_,LUA_GCCOLLECT,0);
    if(ok)ok=actorWorld_.loadCatalog(std::move(catalog),error_);return ok;
}
std::string ScriptRuntime::actorsDump()const{std::string result="ANIMATION_EFFECTS "+effects_.dump()+"\n"+packages_.dump()+movies_.dump()+cameraFade_.dump()+actorWorld_.dump()+projectileWorld_.dump()+selfEffects_.dump()+audio_.dump()+(state_?dumpScriptScheduler(state_)+dumpDelayedActions(state_):"");
    for(const auto id:actorWorld_.registrationOrder())if(const auto* a=actorWorld_.actor(id);a&&a->registered&&a->arsenal){result+="UNIT_ARSENAL owner="+std::to_string(id)+" timeMultiplier="+std::to_string(a->timeMultiplier)+"\n"+a->arsenal->dump();}return result;}
bool ScriptRuntime::activateRoom(){
    if(roomActivated_)return true;
    if(!roomBooted_||mapProbeInstalled_||activationStarted_){error_="Native activation requires a fresh booted world; failed activation cannot be replayed";return false;}
    activationStarted_=true;
    const auto random=[&](double& r,std::string& error){if(nextRecoveredEngineRandom(state_,r))return true;error="Native actor RNG unavailable";return false;};
    for(const auto& entry:creationPlan_.creation){
        ObjectSnapshot object;
        if(!actorWorld_.initializeMapActor(entry.id,object,weaponCatalog_,random,error_)){error_="Actor "+std::to_string(entry.id)+": "+error_;reportError("actor-initialization");return false;}
        if(!insertActorSnapshot(state_,std::move(object),error_))return false;
        auto* registered=findObjectSnapshot(state_,entry.id);
        if(!registered||!actorWorld_.registerActor(entry.id,*registered,error_))return false;
        if(actorWorld_.actor(entry.id)->hasLife&&!lifeSystem_.registerActor(entry.id,error_)){reportError("life-component-registration");return false;}
        // Contained on device (--contain-faults): one unreconstructed light controller must not stop map creation.
        if(!initializeActorLight(entry.id,error_)&&!containStage("actor-light-initialization"))return false;
        if(!initializeInteraction(entry.id,error_)){reportError("interaction-registration");return false;}
        if(!initializePrimarySpawner(entry.id,error_)){reportError("primary-spawner-initialization");return false;}
        if(entry.definitionTriggerOnSpawn){
            if(!dispatchCallbacks("OnSpawn",registered->name.c_str(),registered->id))return false;
            actorWorld_.actor(entry.id)->spawnDispatched=true;
        }
        if(entry.attachedChild&&!actorWorld_.linkStructuralChild(entry.parentId,entry.id,error_))return false;
    }
    for(const auto& entry:creationPlan_.deferred)if(entry.nativeInactiveRegistration){
        const auto found=std::find_if(mapMetadata_.begin(),mapMetadata_.end(),[&](const ObjectSnapshot& o){return o.id==entry.id;});
        if(found==mapMetadata_.end()){error_="Inactive native map record missing";return false;}inactiveMap_.emplace(entry.id,&*found);
    }
    // Populate the actual collision broadphase for every completed obstacle,
    // after structural ownership is linked and before room-load Lua queries.
    // Light/draw auxiliary phases retain their independent component ownership.
    for(auto id:actorWorld_.registrationOrder()){
        const auto* actor=actorWorld_.actor(id);
        if(!actor||!actor->registered||actor->definition->kind!="Obstacle")continue;
        if(!actorWorld_.registerObstacleCollision(id,error_)){reportError("obstacle-collision-registration");return false;}
        if(!actorWorld_.initializeOccluder(id,error_)||!actorWorld_.updateOccluder(id,error_)){reportError("obstacle-occluder-registration");return false;}
    }
    if(!dispatchCallbacks("OnAnyLoad",mapName_.c_str())||!dispatchCallbacks("OnLoad",mapName_.c_str()))return false;
    roomActivated_=true;log("NATIVE_MAP_BEGIN_COMPLETE original_callbacks=true");return true;
}
bool ScriptRuntime::advanceWorld(float dt,double screenDt){
    // RequestBegin only queues from a Lua coroutine. World::Begin consumes
    // that request at the following world boundary, never inside Main.update.
    if(pendingMap_&&!beginRequestedMap()){reportError("map-transition");worldFaulted_=true;return false;}
    if(!roomActivated_){error_="Cannot update an unstarted native world";return false;}
    if(!std::isfinite(dt)||dt<0||!std::isfinite(screenDt)||screenDt<0){error_="Invalid native world clock delta";return false;}
    if(worldFaulted_)return false;
    const float realElapsed=dt;
    if(!advanceWorldSpeed(realElapsed,error_)){reportError("world-speed-update");worldFaulted_=true;return false;}
    // World.Update314fdf scales ONCE before every world manager/ECS phase.
    // Screen time and ShakeSystem retain their independent original clocks.
    dt*=worldSpeedModifier_;
    ++worldFrame_;worldTime_+=dt;screenTime_+=screenDt;
    snapshotControlFrame();
    if(!advanceUnitDisposal(false,error_)){if(!containStage("unit-remove-before-update"))return false;}
    if(!advanceFlashes(dt,error_)){if(!containStage("flash-update"))return false;}
    if(!advanceShakes(realElapsed,error_)){if(!containStage("shake-update"))return false;}
    if(!advanceUnitActivations(error_)){if(!containStage("unit-activation-update"))return false;}
    if(!advanceLocalPlayerInput(dt,error_)){if(!containStage("player-input-update"))return false;}
    // UseableManager::Update + PlayerUnitInput use target / manual Use (lua_interaction.cpp, approximation A4).
    if(!advancePlayerUse(error_)){if(!containStage("player-use-update"))return false;}
    if(!advanceActorMotion(dt,error_)){if(!containStage("actor-motion-update"))return false;}
    // Each step is contained on its own: a weapon gap must not stop unit
    // effects (disable timers) from ticking, which would lock movement.
    if(!advanceWeapons(dt,error_)){if(!containStage("weapon-update"))return false;}
    if(!advanceUnitEffects(dt,error_)){if(!containStage("unit-effects-update"))return false;}
    if(!advanceProjectiles(dt,error_)){if(!containStage("projectile-update"))return false;}
    if(!collectRetiredArsenals(error_)){if(!containStage("retained-weapon-disposal-update"))return false;}
    // Actor/weapon/projectile managers are being integrated separately. Run
    // the genuine supported component schedule; the diagnostic records phase
    // coverage separately from successful replay of this partial schedule.
    if(!advancePhysics(dt,error_)){if(!containStage("physics-update"))return false;}
    if(!advanceUnitMovement(dt,error_)){if(!containStage("unit-movement-update"))return false;}
    if(!advanceUnitDisposal(true,error_)){if(!containStage("unit-disposal-update"))return false;}
    if(!advanceObstacleDisposal(error_)){if(!containStage("obstacle-disposal-update"))return false;}
    // ObstacleManager::Update 0x1802c80c0 occlusion block, before Body update.
    if(!advanceObstacleOcclusion(error_)){if(!containStage("obstacle-occlusion-update"))return false;}
    for(const auto id:actorWorld_.registrationOrder()){
        auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered)continue;
        if(!actorWorld_.advanceBody(id,dt,worldSpeedModifier_,error_)){if(!containStage("body-update"))return false;}
    }
    for(const auto handle:projectileWorld_.bodyComponents())if(!projectileWorld_.advanceBody(handle,dt,worldSpeedModifier_,error_)){if(!containStage("projectile-body-update"))return false;}
    if(!advanceActorAnimations(dt,error_)||!advanceVfx(dt,error_)||!advanceActorLights(dt,error_)||!cameraFade_.advance(dt,error_)){reportError("native-component-update");worldFaulted_=true;return false;}
    if(!advanceLife(dt,error_)){reportError("life-update");worldFaulted_=true;return false;}
    if(!advanceAudio(dt,error_)){if(!containStage("audio-update"))return false;}
    if(!advanceSpeech(dt,error_)){if(!containStage("speech-update"))return false;}
    if(!consumeSpeechText(drainSpeechPresentation(),error_)){if(!containStage("speech-text-update"))return false;}
    if(!advanceScreenAnimations(float(screenDt),error_)){if(!containStage("screen-animation-update"))return false;}
    if(!advanceText(dt,float(screenDt),error_)){if(!containStage("text-update"))return false;}
    if(!advanceNativeNotifies(dt,error_)){if(!containStage("native-notify-update"))return false;}
    if(!advanceCamera(dt,error_)){if(!containStage("camera-update"))return false;}
    if(!advanceVignetteFrame(dt,error_)){if(!containStage("vignette-frame-update"))return false;}
    // A package or delayed-action gap must never skip the script scheduler
    // (every original Lua thread), or game logic freezes while audio runs.
    if(!packages_.pump(1,error_)){if(!containStage("package-update"))return false;}
    if(!advanceDelayedActions(state_,worldTime_,executeDelayed,this,error_)){if(!containStage("delayed-action-update"))return false;}
    if(!advanceScriptScheduler(state_,worldTime_,screenTime_,error_)){if(!containStage("world-update"))return false;}
    if(!colorGrading_.advance(worldTime_,float(screenTime_),error_)){if(!containStage("color-grading-update"))return false;}
    if(!advanceBloom(error_)){if(!containStage("bloom-update"))return false;}
    // App::Update 0x18003712e runs the BlurComponent helpers on App elapsed after World update.
    if(!advanceRadialBlur(realElapsed,error_)){if(!containStage("radial-blur-update"))return false;}
    return true;
}

bool ScriptRuntime::delayedRandom(void* context,float low,float high,float& result,std::string& error){
    auto* self=static_cast<ScriptRuntime*>(context);double r=0;
    if(!nextRecoveredEngineRandom(self->state_,r)){error="Native delay RNG unavailable";return false;}
    result=float(r)* (high-low)+low;return true;
}
bool ScriptRuntime::executeDelayed(lua_State* L,int action,int args,void*,std::string& error){
    args=lua_absindex(L,args);const int top=lua_gettop(L);
    lua_getfield(L,LUA_REGISTRYINDEX,"hades.immediateVoidActions");lua_rawgeti(L,-1,action);lua_remove(L,-2);
    if(!lua_isfunction(L,-1)){lua_settop(L,top);error="Native delayed executor missing";return false;}
    lua_pushvalue(L,args);const int result=lua_pcall(L,1,0,0);
    if(result!=LUA_OK){const char* e=lua_tostring(L,-1);error=e?e:"Delayed native action failed";}
    lua_settop(L,top);return result==LUA_OK;
}
int ScriptRuntime::delayedAction(lua_State* L){
    auto* self=runtime(L);const int id=int(lua_tointeger(L,lua_upvalueindex(2)));
    char message[2048]{};bool queued=false,ok=false;
    {
        std::string error;DelayedActionAdmission outcome=DelayedActionAdmission::ExecuteNow;
        ok=admitDelayedAction(L,id,1,self->worldTime_,outcome,error,delayedRandom,self);
        queued=outcome==DelayedActionAdmission::Queued;
        if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());
    }
    if(!ok)return luaL_error(L,"%s",message);
    if(queued)return 0;
    lua_getfield(L,LUA_REGISTRYINDEX,"hades.immediateVoidActions");lua_rawgeti(L,-1,id);lua_remove(L,-2);lua_pushvalue(L,1);
    if(lua_pcall(L,1,0,0)!=LUA_OK)return lua_error(L);return 0;
}
bool ScriptRuntime::installDelayedActions(){
    lua_newtable(state_);const int handlers=lua_gettop(state_);std::vector<int> ids;
    for(const auto& action:hades::contract::actions){
        if(action.result!=hades::contract::ReturnKind::Void)continue;
        lua_getglobal(state_,action.name);
        if(lua_iscfunction(state_,-1)&&lua_tocfunction(state_,-1)!=unsupported){
            ids.push_back(action.id);lua_rawseti(state_,handlers,action.id);
            lua_pushlightuserdata(state_,this);lua_pushinteger(state_,action.id);lua_pushcclosure(state_,delayedAction,2);lua_setglobal(state_,action.name);
        }else lua_pop(state_,1);
    }
    lua_setfield(state_,LUA_REGISTRYINDEX,"hades.immediateVoidActions");
    return initializeDelayedActions(state_,ids,error_);
}

bool ScriptRuntime::equipWeaponImpl(lua_State* L,std::string& error){
    std::vector<int32_t> selected;
    if(!resolveObjectTargets(L,true,selected,error))return false;
    lua_getfield(L,1,"Names");if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,1,"Name");}
    hades::SourceValue requested;const bool parsed=hades::readSourceValue(L,-1,requested,error);lua_pop(L,1);if(!parsed)return false;
    std::vector<std::string> names;
    if(std::holds_alternative<std::string>(requested.value))names.push_back(requested.text());
    else if(!std::holds_alternative<std::monostate>(requested.value))for(const auto& n:requested.array())names.push_back(n.text());
    lua_getfield(L,1,"LoadPackages");const bool packages=lua_toboolean(L,-1);lua_pop(L,1);
    for(const auto& name:names){
        if(packages&&!packages_.request(name,error))return false;
        for(const auto id:selected){
            auto* object=findObjectSnapshot(L,id);
            auto* unit=actorWorld_.actor(id);
            // Native action ignores non-unit selections. Diagnostic query
            // proxies never satisfy actual actor/component ownership.
            if(!object||!object->active||!object->alive||object->queryProxy||!unit||!unit->registered||!unit->arsenal)continue;
            if(!unit->arsenal->equip(name,error))return false;
            hades::log(hades::Severity::Info,tag,"NATIVE_WEAPON_EQUIPPED owner=%d name=%s",id,name.c_str());
        }
    }
    return true;
}
int ScriptRuntime::equipWeapon(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->equipWeaponImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

bool ScriptRuntime::loadPackageDefinitions(){
    auto asset=assets_->open("packages.lua");if(!asset||asset->size()>32*1024*1024){error_="Missing or oversized package catalog";return false;}
    std::vector<char> bytes(asset->size());size_t at=0;while(at<bytes.size()){const auto n=asset->read(bytes.data()+at,bytes.size()-at);if(!n){error_="Truncated package catalog";return false;}at+=n;}
    const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@packages.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);
    if(status!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    hades::SourceValue value;bool ok=hades::readSourceValue(state_,-1,value,error_);lua_settop(state_,top);lua_gc(state_,LUA_GCCOLLECT,0);if(ok)ok=packages_.loadCatalog(std::move(value),error_);return ok;
}

bool ScriptRuntime::serviceAssets(size_t budget){std::string detail;const bool ok=packages_.pump(budget,detail);if(!ok){if(error_.empty())error_=detail;hades::log(hades::Severity::Error,tag,"ASSET_PACKAGE_FAILED detail=%s",detail.c_str());}return ok;}

bool ScriptRuntime::setUnitPropertyImpl(lua_State* L,std::string& error){return setActorPropertyImpl(L,false,error);}
bool ScriptRuntime::setActorPropertyImpl(lua_State* L,int kind,std::string& error){
    if(!lua_istable(L,1)){error="SetUnitProperty requires table";return false;}
    std::vector<int32_t> ids;
    auto readId=[&](int at){int numeric=0;const double value=lua_tonumberx(L,at,&numeric);if(numeric&&std::isfinite(value)&&value>0&&value<=INT32_MAX)ids.push_back(int32_t(value));return numeric;};
    lua_getfield(L,1,"DestinationId");const bool singular=readId(-1);lua_pop(L,1);
    if(!singular){lua_getfield(L,1,"DestinationIds");if(lua_istable(L,-1))for(int i=1;i<=100000;++i){lua_rawgeti(L,-1,i);if(lua_isnil(L,-1)){lua_pop(L,1);break;}readId(-1);lua_pop(L,1);if(i==100000){error="Unit property selection exceeds bounds";lua_pop(L,1);return false;}}lua_pop(L,1);}
    if(kind==2||kind==3){ids.clear();if(!resolveObjectTargets(L,true,ids,error))return false;}
    if(kind==2){
        lua_getfield(L,1,"AllProjectiles");const bool all=lua_toboolean(L,-1);lua_pop(L,1);if(all){error="SetThingProperty AllProjectiles requires current projectile reflection";return false;}}
    lua_getfield(L,1,"Property");const char* p=lua_tostring(L,-1);const std::string property=p?p:"";lua_pop(L,1);
    lua_getfield(L,1,"ValueChangeType");const char* c=lua_tostring(L,-1);const auto change=hades::parsePropertyChange(c?c:"Absolute");lua_pop(L,1);
    lua_getfield(L,1,"Value");hades::SourceValue source;bool ok=hades::readSourceValue(L,-1,source,error);lua_pop(L,1);if(!ok)return false;
    hades::PropertyInput input;if(!hades::propertyInputFromSource(source,input,error))return false;
    lua_getfield(L,1,"DataValue");const bool data=lua_isnil(L,-1)||lua_toboolean(L,-1);lua_pop(L,1);
    if(kind==2){hades::log(hades::Severity::Info,tag,"SET_THING_PROPERTY property=%s data=%d targets=%zu",property.c_str(),int(data),ids.size());
        if(!data){
            // ReflectionHelper::GetPropertyInfo<Thing>2e6eb0: ElapsedTimeMultiplier is
            // plain float Thing+0x80 (VA1802e6fe2, type4), no setter side effect; it is
            // the same live multiplier SpeedEffect elapsed-time changes divide/multiply.
            if(property!="SuppressSounds"&&property!="ElapsedTimeMultiplier"){error="SetThingProperty live reflection not reconstructed: "+property;return false;}
            for(const auto id:ids){auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object||!actor->object->active||!actor->object->alive)continue;
                hades::PropertyPrimitive next;
                if(property=="ElapsedTimeMultiplier"){if(hades::mutatePrimitiveProperty(actor->timeMultiplier,input,change,next,error)!=hades::PropertyMutation::Applied)return false;actor->timeMultiplier=std::get<float>(next);continue;}
                if(hades::mutatePrimitiveProperty(actor->suppressSounds,input,change,next,error)!=hades::PropertyMutation::Applied)return false;actor->suppressSounds=std::get<bool>(next);}
            return true;
        }}
    for(const auto id:ids){const auto* o=findObjectSnapshot(L,id);if(!o||!o->active||!o->alive||o->queryProxy)continue;if(!(kind==1?actorWorld_.setLifeProperty(id,property,input,change,data,error):(kind==2?actorWorld_.setThingProperty(id,property,input,change,error):(kind==3?actorWorld_.setObstacleProperty(id,property,input,change,error):actorWorld_.setUnitProperty(id,property,input,change,error)))))return false;}
    return true;
}
int ScriptRuntime::setThingProperty(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->setActorPropertyImpl(L,2,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
int ScriptRuntime::setObstacleProperty(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->setActorPropertyImpl(L,3,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
bool ScriptRuntime::getUnitDataValueImpl(lua_State* L,hades::SourceValue& value,std::string& error){
    if(!lua_istable(L,1)){error="GetUnitDataValue requires table";return false;}
    std::vector<int32_t> selected;if(!resolveObjectTargets(L,false,selected,error))return false;
    lua_getfield(L,1,"Property");const char* p=lua_tostring(L,-1);const std::string property=p?p:"";lua_pop(L,1);
    int32_t unit=0;
    for(const auto id:selected){const auto* a=actorWorld_.actor(id);if(a&&a->registered&&a->object&&a->object->active&&a->object->alive&&!a->object->queryProxy&&a->definition&&a->definition->kind=="Unit"){
        // Original returns the first Unit in its native pointer hash set. The
        // Android target registry cannot reproduce Windows pointer ordering.
        if(unit){error="GetUnitDataValue multi-Unit native hash-set order not reconstructed";return false;}unit=id;}}
    return actorWorld_.unitDataValue(unit,property,value,error);
}
int ScriptRuntime::getUnitDataValue(lua_State* L){
    char message[2048]{};bool ok=false;
    {hades::SourceValue value;try{std::string error;ok=runtime(L)->getUnitDataValueImpl(L,value,error);if(ok)hades::pushSourceValue(L,value);else std::snprintf(message,sizeof(message),"%s",error.c_str());}catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}}
    if(!ok)return luaL_error(L,"%s",message);return 1;
}
int ScriptRuntime::setUnitProperty(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->setUnitPropertyImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

bool ScriptRuntime::spawnObstacleImpl(lua_State* L,int32_t& created,std::string& error){
    if(!lua_istable(L,1)){error="SpawnObstacle requires table";return false;}
    auto string=[&](const char* key,const char* fallback=""){lua_getfield(L,1,key);if(lua_isnil(L,-1)){lua_pop(L,1);return std::string(fallback);}if(lua_type(L,-1)!=LUA_TSTRING)throw std::runtime_error(std::string(key)+" requires string");std::string value=lua_tostring(L,-1);lua_pop(L,1);return value;};
    auto number=[&](const char* key,float fallback=0.f){lua_getfield(L,1,key);if(lua_isnil(L,-1)){lua_pop(L,1);return fallback;}int numeric=0;const float v=float(lua_tonumberx(L,-1,&numeric));lua_pop(L,1);if(!numeric||!std::isfinite(v))throw std::runtime_error(std::string(key)+" requires finite scalar");return v;};
    auto boolean=[&](const char* key,bool fallback=false){lua_getfield(L,1,key);if(lua_isnil(L,-1)){lua_pop(L,1);return fallback;}if(!lua_isboolean(L,-1))throw std::runtime_error(std::string(key)+" requires boolean");const bool v=lua_toboolean(L,-1);lua_pop(L,1);return v;};
    // Native SpawnObstacle 0x26f4a0: ThingData UseScreenLocation (e.g. GUI.sjson TraitTray_*) also selects the screen body.
    if(boolean("UseScreenLocation"))return spawnScreenObstacleImpl(L,created,error);
    {lua_getfield(L,1,"Name");const char* n=lua_type(L,-1)==LUA_TSTRING?lua_tostring(L,-1):nullptr;const auto* d=n?actorWorld_.definition(std::string("Obstacles/")+n):nullptr;lua_pop(L,1);const auto* use=d?d->properties.path("Thing.UseScreenLocation"):nullptr;if(use&&use->boolean())return spawnScreenObstacleImpl(L,created,error);}
    auto name=string("Name");lua_getfield(L,1,"Names");
    if(!lua_isnil(L,-1)){
        if(lua_type(L,-1)==LUA_TSTRING)name=lua_tostring(L,-1);
        else if(lua_istable(L,-1)&&lua_rawlen(L,-1)<=1){if(lua_rawlen(L,-1)){lua_rawgeti(L,-1,1);if(lua_type(L,-1)!=LUA_TSTRING)throw std::runtime_error("SpawnObstacle name requires string");name=lua_tostring(L,-1);lua_pop(L,1);}else name.clear();}
        else throw std::runtime_error("SpawnObstacle multi-name selection outside recovered first-name subset");
    }
    lua_pop(L,1);if(name.empty()){created=0;return true;}
    std::vector<int32_t> destinationIds;
    auto readId=[&](int at){int ok=0;const auto v=lua_tonumberx(L,at,&ok);if(!ok||!std::isfinite(v)||v<INT32_MIN||v>INT32_MAX)throw std::runtime_error("SpawnObstacle destination requires signed32 ID");destinationIds.push_back(int32_t(v));};
    lua_getfield(L,1,"DestinationIds");
    if(!lua_isnil(L,-1)){
        if(!lua_istable(L,-1)||lua_rawlen(L,-1)>100000)throw std::runtime_error("SpawnObstacle destinations require bounded list");
        for(size_t i=1,n=lua_rawlen(L,-1);i<=n;++i){lua_rawgeti(L,-1,int(i));readId(-1);lua_pop(L,1);}lua_pop(L,1);
    }else{lua_pop(L,1);lua_getfield(L,1,"DestinationId");if(!lua_isnil(L,-1))readId(-1);lua_pop(L,1);}
    float x=number("LocationX"),y=number("LocationY"),parallax=1;int32_t attachParent=0;
    if(!destinationIds.empty()){
        x=y=parallax=0;for(const auto id:destinationIds){const auto* o=findObjectSnapshot(L,id);if(o&&o->active&&o->alive){float ox=0,oy=0;if(!actorWorld_.location(id,ox,oy,error))throw std::runtime_error(error);x+=ox;y+=oy;const auto* a=actorWorld_.actor(id);if(!a)throw std::runtime_error("Destination parallax requires actual actor components");parallax+=a->ownParallax*a->mapParallax;if(!attachParent)attachParent=id;}}
        x/=destinationIds.size();y/=destinationIds.size();parallax/=destinationIds.size();
    }
    const float offsetX=number("OffsetX"),offsetY=number("OffsetY");x+=offsetX;y+=offsetY;const float z=number("OffsetZ");
    // ScriptAction::SpawnObstacle26f4a0: relocation precedes allocation. Zero
    // sentinel plus SkipIfBlocked returns no object; otherwise retain origin.
    if(boolean("ForceToValidLocation")){float px=0,py=0;if(!closestNonBlockedLocation(x,y,px,py,error))return false;if(px==0&&py==0){if(boolean("SkipIfBlocked")){created=0;return true;}}else{x=px;y=py;}}
    const auto group=string("Group","Default");
    bool hasGroup=false;for(const auto& g:snapshotSceneGroups(L))if(g.name==group)hasGroup=true;
    if(!group.empty()&&!hasGroup){
        lua_getglobal(L,"CreateGroup");lua_newtable(L);lua_pushlstring(L,group.data(),group.size());lua_setfield(L,-2,"Name");
        if(lua_pcall(L,1,0,0)!=LUA_OK){error=lua_tostring(L,-1);lua_pop(L,1);return false;}
    }
    if(!allocateObstacleObjectId(L,created,error))return false;
    const auto random=[&](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value))return true;detail="Dynamic obstacle RNG unavailable";return false;};
    ObjectSnapshot initial;
    if(!actorWorld_.initializeDynamicObstacle(created,name,x,y,group,initial,random,error)||!insertActorSnapshot(L,std::move(initial),error))return false;
    auto* object=findObjectSnapshot(L,created);if(!object||!actorWorld_.registerActor(created,*object,error))return false;
    if(actorWorld_.actor(created)->hasLife&&!lifeSystem_.registerActor(created,error))return false;
    if(!initializeActorLight(created,error)||!initializePrimarySpawner(created,error)||!initializeInteraction(created,error))return false;
    auto* actor=actorWorld_.actor(created);const auto* trigger=actor->definition->properties.path("Thing.TriggerOnSpawn");
    if(boolean("TriggerOnSpawn",true)&&trigger&&trigger->boolean()){
        if(!dispatchCallbacks("OnSpawn",name.c_str(),created))return false;actor->spawnDispatched=true;
    }
    lua_getfield(L,1,"AttachedTable");
    if(!lua_isnil(L,-1)&&!lua_istable(L,-1))throw std::runtime_error("SpawnObstacle AttachedTable requires table or nil");
    setObjectLuaTable(L,created,-1);lua_pop(L,1);
    actor->ownParallax*=parallax;
    object->z=z;object->screenSpace=boolean("UseScreenLocation",object->screenSpace);object->graphicsDirty=true;
    if(!actorWorld_.registerObstacleCollision(created,error)||!actorWorld_.initializeOccluder(created,error)||!actorWorld_.updateOccluder(created,error))return false;
    if(boolean("DestroyIfBlocked")){bool blocked=false;if(!spawnPointBlocked(x,y,blocked,error))return false;if(blocked&&!markThingDead(created,false,error))return false;}
    // SpawnObstacle26f974 attaches to the first live selected destination,
    // after allocation/callbacks/blocking and before SortById. AttachTo receives
    // raw OffsetXY, not the averaged/relocated world point or scaled offsets.
    // It leaves the new Thing's constructor Z attachment offset at zero.
    if(boolean("Attach")&&!actorWorld_.attach(created,attachParent,offsetX,offsetY,0,false,error))return false;
    object->sortById=boolean("SortById");
    return true;
}
int ScriptRuntime::spawnObstacle(lua_State* L){
    char message[2048]{};bool ok=false;int32_t id=0;
    try{std::string error;ok=runtime(L)->spawnObstacleImpl(L,id,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);lua_pushnumber(L,id);return 1;
}

bool ScriptRuntime::runWeaponMethodImpl(lua_State* L,std::string& error){try{
    hades::SourceValue args;if(!lua_istable(L,1)||!hades::readSourceValue(L,1,args,error))throw std::runtime_error("RunWeaponMethod requires an actual argument table");
    const auto* method=args.get("Method");const auto* weapon=args.get("Weapon");
    const std::string methodName=method?method->text():"",weaponName=weapon?weapon->text():"";
    // ScriptAction.RunWeaponMethod268b80 selects live Things, then the owned
    // weapon; this bounded branch implements actual consumable AddAmmo only.
    if(methodName!="AddAmmo")throw std::runtime_error("Native RunWeaponMethod not reconstructed: "+methodName);
    if(weaponName=="All"||weaponName=="NonMultiFireRequestWeapons")throw std::runtime_error("RunWeaponMethod arsenal selector not reconstructed: "+weaponName);
    const auto* parameters=args.get("Parameters");
    if(!parameters||!std::holds_alternative<hades::SourceValue::Array>(parameters->value)||parameters->array().size()!=1)throw std::runtime_error("Native AddAmmo requires its actual first parameter");
    const float value=float(parameters->array().front().number());
    // Parameters are native float anys; INT_FromAny250d80 truncates to int32.
    if(!std::isfinite(value)||value<-2147483648.f||value>=2147483648.f)throw std::runtime_error("AddAmmo parameter outside native finite int32 subset");
    std::vector<int32_t> selected;if(!resolveObjectTargets(L,false,selected,error))return false;
    for(const auto id:selected){const auto* object=findObjectSnapshot(L,id);auto* actor=actorWorld_.actor(id);
        if(!object||!object->active||!object->alive||!actor||!actor->registered||!actor->arsenal||!actor->arsenal->hasWeapon(weaponName))continue;
        if(!actor->arsenal->addAmmo(weaponName,int32_t(value),error))return false;
    }
    error.clear();return true;
}catch(const std::exception& exception){error=exception.what();return false;}}
int ScriptRuntime::runWeaponMethod(lua_State* L){
    char message[2048]{};bool ok=false;
    {std::string error;try{ok=runtime(L)->runWeaponMethodImpl(L,error);}catch(const std::exception& exception){error=exception.what();}if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
bool ScriptRuntime::weaponPropertyImpl(lua_State* L,int mode,std::string& error){
    std::vector<int32_t> selected;if(!resolveObjectTargets(L,mode==0||mode==3,selected,error))return false;
    auto text=[&](const char* key,const char* fallback=""){lua_getfield(L,1,key);const char* p=lua_tostring(L,-1);std::string value=p?p:fallback;lua_pop(L,1);return value;};
    const auto name=text("WeaponName"),property=text("Property");
    const auto change=static_cast<hades::WeaponValueChange>(uint8_t(hades::parsePropertyChange(text("ValueChangeType","Absolute"))));
    bool data=true;lua_getfield(L,1,"DataValue");if(!lua_isnil(L,-1))data=lua_toboolean(L,-1);lua_pop(L,1);
    std::optional<hades::WeaponScalar> value;
    if(mode==0||mode==3){
        lua_getfield(L,1,"Value");hades::SourceValue source;const bool parsed=hades::readSourceValue(L,-1,source,error);lua_pop(L,1);if(!parsed)return false;
        if(const auto* v=std::get_if<bool>(&source.value))value=*v;
        else if(const auto* v=std::get_if<double>(&source.value))value=*v;
        else if(const auto* v=std::get_if<std::string>(&source.value))value=*v;
        else if(!std::holds_alternative<std::monostate>(source.value)){error="Complex native weapon property requires typed handler: "+property;return false;}
    }
    for(const auto id:selected){
        const auto* o=findObjectSnapshot(L,id);auto* a=actorWorld_.actor(id);
        if(!o||!o->active||!o->alive||o->queryProxy||!a||!a->registered||!a->arsenal||!a->arsenal->hasWeapon(name))continue;
        if(mode==3){
            // Native ScriptAction182 selects the same actual Unit arsenal as
            // SetWeaponProperty. Filtered primary/spawned dispatch is a
            // separate native path; do not apply it to both copies blindly.
            if(!text("ProjectileName").empty()||!text("ExcludeProjectileName").empty()){
                error="Filtered SetProjectileProperty requires native primary/spawned selection";return false;
            }
            if(!a->arsenal->setScriptProjectileProperty(name,property,value,change,error))return false;
        }
        else if(mode==0){if(!a->arsenal->setScriptProperty(name,property,value,change,data,error))return false;}
        else{
            hades::WeaponScalar result;
            if(!(mode==1?a->arsenal->dataProperty(name,property,result,error):a->arsenal->stateProperty(name,property,result,error)))return false;
            if(const auto* v=std::get_if<bool>(&result))lua_pushboolean(L,*v);
            else if(const auto* v=std::get_if<double>(&result))lua_pushnumber(L,*v);
            else{const auto& textValue=std::get<std::string>(result);lua_pushlstring(L,textValue.data(),textValue.size());}
            return true;
        }
    }
    if(mode==1||mode==2)lua_pushnil(L);return true;
}
int ScriptRuntime::weaponProperty(lua_State* L){
    const int mode=int(lua_tointeger(L,lua_upvalueindex(2)));char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->weaponPropertyImpl(L,mode,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return mode==1||mode==2?1:0;
}

bool ScriptRuntime::loadEffectDefinitions(){
    auto asset=assets_->open("effects.lua");if(!asset||asset->size()>8*1024*1024){error_="Missing or oversized source effect catalog";return false;}
    std::vector<char> bytes(asset->size());size_t at=0;while(at<bytes.size()){const auto n=asset->read(bytes.data()+at,bytes.size()-at);if(!n){error_="Truncated effect catalog";return false;}at+=n;}
    const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@effects.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);
    if(status!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    hades::SourceValue value;bool ok=hades::readSourceValue(state_,-1,value,error_);lua_settop(state_,top);lua_gc(state_,LUA_GCCOLLECT,0);if(!ok)return false;
    try{
        if(const auto* defaults=value.get("NativeDefaults"))nativeAnimationDefaults_=*defaults;
        if(!effects_.loadCatalog(value,error_))return false;
        particleBank_=std::make_shared<hades::ActorAnimationBank>();particleBank_->load(*assets_,"cinder-frames.bin");
        if(!particles_.setRegistry(effects_.registry(),error_)||!particles_.load(value,particleBank_,error_))return false;
        particles_.setExternalOwner([this](uint64_t id){return actionEffects_.state(id)!=nullptr;});
        // Room leaf bank exists only for maps whose primary spawners reach leaf images (export_room_leaf_frames.py).
        if(assets_->open("room-leaf-frames.bin")){leafBank_=std::make_shared<hades::ActorAnimationBank>();leafBank_->load(*assets_,"room-leaf-frames.bin","room-leaf-facing.bin","room-leaf-sha256.bin");
        if(!particles_.addImageBank(leafBank_,error_))return false;}else leafBank_.reset();
        if(const auto* recipes=value.get("PrimaryRecipes")){
            spawnerBank_=std::make_shared<hades::ActorAnimationBank>();spawnerBank_->load(*assets_,"spawner-frames.bin");
            for(const auto& recipe:recipes->array()){
                const auto id=int32_t(recipe.get("owner_map_id")->number());
                if(!primarySpawnerRecipes_.emplace(id,recipe.get("graphic")->text()).second)throw std::runtime_error("Duplicate original primary spawner owner");
            }
            const auto* graphics=value.get("PrimaryGraphics");if(!graphics)throw std::runtime_error("Original primary spawner graphics metadata missing");
            for(const auto& graphic:graphics->array()){
                hades::EffectTexture texture;texture.name=graphic.get("graphic")->text();texture.atlas=graphic.get("atlas")->text();
                const auto frame=spawnerBank_->sample(size_t(graphic.get("clip_index")->number()),0,0,false);
                texture.width=frame.width;texture.height=frame.height;{const auto px=frame.pixels();texture.pixels.assign(px.begin(),px.end());}
                auto integers=[&](const char* key,auto& out){const auto& a=graphic.get(key)->array();if(a.size()!=out.size())throw std::runtime_error("Original primary graphic metadata shape");for(size_t i=0;i<a.size();++i)out[i]=int32_t(a[i].number());};
                integers("rect",texture.atlasRect);integers("trim",texture.trim);integers("original",texture.originalSize);
                if(!primarySpawnerGraphics_.emplace(graphic.get("animation")->text(),std::move(texture)).second)throw std::runtime_error("Duplicate primary spawner graphic");
            }
        }
        return true;
    }catch(const std::exception& e){error_=e.what();return false;}
}
bool ScriptRuntime::effectOwner(int32_t id,hades::EffectOwnerState& state,std::string& error)const{
    if(id<0)return projectileEffectOwner(id,state,error);
    const auto* actor=actorWorld_.actor(id);const auto* object=findObjectSnapshot(state_,id);
    if(!actor||!actor->registered||!object||object->queryProxy||!object->active||!object->alive||!actor->entityHandle){error="Effect owner requires actual live actor components";return false;}
    state.lifetime=actor->entityHandle;
    if(!actorWorld_.location(id,state.x,state.y,error)||!actorWorld_.zLocation(id,state.z,error))return false;
    state.scale=object->scale*object->scaleFraction;state.angle=object->angle;
    state.bookOwnerAngle=actorAnimationFacing(*actor);
    state.scaleVector={actor->fractions[2].current(),actor->fractions[3].current()};
    state.velocity.reset();if(actor->physics)state.velocity=std::array<float,2>{actor->physics->velocity.x,actor->physics->velocity.y};
    state.group=object->group;
    state.baseColor=actor->baseColorBytes;state.baseHsv=std::array<float,4>{actor->hsv[0],actor->hsv[1],actor->hsv[2],0};
    state.hasPrimaryAnimation=actor->animation.active()||actor->primaryEffectAnimation||actor->primaryAnimationLifetime;state.isUnit=bool(actor->arsenal);error.clear();return true;
}
bool ScriptRuntime::initializePrimarySpawner(int32_t id,std::string& error){
    if(const auto* actor=actorWorld_.actor(id);actor&&actor->object)presentationBases_.try_emplace(id,PresentationBase{*actor->object,actor->color,actor->hsv});
    const auto recipe=primarySpawnerRecipes_.find(id);if(recipe==primarySpawnerRecipes_.end()){
        auto* actor=actorWorld_.actor(id);
        if(actor&&actor->animation.active()&&!actor->primaryAnimationLifetime)return setActorAnimation(id,actor->object->animation,error);
        if(scenePrimaries_.recipe(id))return initializeScenePrimary(id,error);
        if(!actor||!actor->object){error="Actual primary actor unavailable";return false;}
        return actor->object->animation.empty()||setActorAnimation(id,actor->object->animation,error);
    }
    auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object||actor->object->animation!=recipe->second||actor->primaryEffectAnimation){error="Original primary spawner actor/graphic lifetime mismatch";return false;}
    const auto image=primarySpawnerGraphics_.find(recipe->second);if(image==primarySpawnerGraphics_.end()){error="Original primary spawner image unavailable";return false;}
    const auto owners=[this](int32_t owner,hades::EffectOwnerState& state,std::string& detail){return effectOwner(owner,state,detail);};
    const auto random=[this](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value))return true;detail="Native primary animation RNG unavailable";return false;};
    uint64_t handle=0;if(!effects_.createPrimary(recipe->second,id,owners,random,image->second,handle,error))return false;
    actor->primaryEffectAnimation=handle;actor->primaryOwnerHasNoCollision=false;actor->primaryOwnerImmobile=false;actor->primaryDisableWeapons=false;actor->primaryAirborne=false;
    hades::log(hades::Severity::Info,tag,"NATIVE_PRIMARY_SPAWNER owner=%d animation=%s handle=%llu manager_member=false",id,recipe->second.c_str(),static_cast<unsigned long long>(handle));return true;
}
// createAnimationImpl (ScriptAction::CreateAnimation + SetupAnimation): lua_action_fx.cpp.
// Rumble{Fraction|LeftFraction,RightFraction,Duration}: Windows queues a
// Player::RumbleRequest (left/right gamepad motors). Android adaptation: the
// same request drives the phone motor at the stronger motor fraction; with
// no provider (Mac host) the request is recorded as having no output device.
int ScriptRuntime::rumbleAction(lua_State* L){
    auto* self=runtime(L);if(!self||!lua_istable(L,1))return luaL_error(L,"Rumble requires table");
    auto field=[&](const char* k){lua_getfield(L,1,k);int ok=0;const double v=lua_tonumberx(L,-1,&ok);lua_pop(L,1);return ok&&std::isfinite(v)?float(v):0.f;};
    const float fraction=std::max({field("Fraction"),field("LeftFraction"),field("RightFraction")}),duration=field("Duration");
    if(fraction>0&&duration>0&&self->physicsRumble_){std::string error;if(!self->physicsRumble_(0,std::min(fraction,1.f),duration,error))return luaL_error(L,"%s",error.c_str());}
    return 0;
}
int ScriptRuntime::createAnimation(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->createAnimationImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

// Thing::RunScriptAction case 0x6c (VA1802e2260): Color via ColorByName
// default FFFFFFFF. Plain: per-channel Body transitions (+0x200/260/2c0/320)
// with {target, initial 1, GetDuration, SmoothStep, EaseIn, EaseOut}.
// MultiplyBase: BodyComponent::MultiplyColor 0x2b76e0 (truncating byte
// multiply). SetBase: Duration>0 transitions from the old base bytes and
// resets base to FFFFFFFF; otherwise stores the base. Both refresh 0x2b7340.
bool ScriptRuntime::setColorImpl(lua_State* L,std::string& error){
    std::vector<int32_t> selected;if(!resolveObjectTargets(L,false,selected,error))return false;
    auto number=[&](const char* key){lua_getfield(L,1,key);const float n=float(lua_tonumber(L,-1));lua_pop(L,1);return n;};
    auto flag=[&](const char* key){lua_getfield(L,1,key);const bool b=lua_toboolean(L,-1);lua_pop(L,1);return b;};
    const bool base=flag("SetBase"),multiply=!base&&flag("MultiplyBase");
    hades::BodyTransitionOptions options;
    options.easeOut=number("EaseOut");options.easeIn=number("EaseIn");options.smoothStep=flag("SmoothStep");
    // GetDuration 0x281bc0: Duration, else DurationSeconds.
    lua_getfield(L,1,"Duration");options.duration=lua_isnil(L,-1)?number("DurationSeconds"):float(lua_tonumber(L,-1));lua_pop(L,1);
    std::array<uint8_t,4> bytes{255,255,255,255};std::array<float,4> rgba{1,1,1,1};lua_getfield(L,1,"Color");
    if(!lua_isnil(L,-1)){
        hades::SourceValue value;if(!hades::readSourceValue(L,-1,value,error)){lua_pop(L,1);return false;}
        const auto& array=value.array();if(array.size()!=4){lua_pop(L,1);error="Native color requires source RGBA byte array";return false;}
        for(size_t i=0;i<4;++i){const auto n=array[i].number();if(n<0||n>255||n!=std::trunc(n))throw std::runtime_error("Native color outside byte subset");bytes[i]=uint8_t(n);rgba[i]=float(n)/255.f;}
    }
    lua_pop(L,1);
    for(const auto id:selected){auto* o=findObjectSnapshot(L,id);auto* a=actorWorld_.actor(id);
        if(o&&o->screenSpace&&o->active&&o->alive){
            const bool timed=options.duration!=0||options.easeIn!=0||options.easeOut!=0;
            if(multiply||(base&&timed)){error="Native screen MultiplyBase/timed SetBase color not reconstructed";return false;}
            if(timed){for(size_t i=0;i<4;++i)if(!screenAnimations_.adjustChannel(id,i,rgba[i],options,error))return false;continue;}
            if(!screenColor(id,rgba,base,error))return false;continue;}
        if(!o||!o->active||!o->alive||o->queryProxy||!a||!a->registered)continue;
        auto storeBase=[&](const std::array<uint8_t,4>& b){a->baseColorBytes=b;for(size_t i=0;i<4;++i)a->color[i]=float(b[i])/255.f;};
        if(multiply){std::array<uint8_t,4> b{};for(size_t i=0;i<4;++i)b[i]=uint8_t(uint32_t(a->baseColorBytes[i])*bytes[i]/255u);storeBase(b);}
        else if(base&&options.duration>0){
            for(size_t i=0;i<4;++i)if(!actorWorld_.adjustColorChannel(id,i,rgba[i],float(a->baseColorBytes[i])/255.f,options,error))return false;
            storeBase({255,255,255,255});
        }
        else if(base)storeBase(bytes);
        else for(size_t i=0;i<4;++i)if(!actorWorld_.adjustColorChannel(id,i,rgba[i],1,options,error))return false;
        o->graphicsDirty=true;
    }
    return true;
}
int ScriptRuntime::setColor(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->setColorImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

bool ScriptRuntime::attachImpl(lua_State* L,std::string& error){
    if(!lua_istable(L,1)){error="Attach requires table";return false;}
    const auto number=[&](const char* key){lua_getfield(L,1,key);int valid=0;const double n=lua_tonumberx(L,-1,&valid);const bool nil=lua_isnil(L,-1);lua_pop(L,1);if(nil)return 0.f;if(!valid||!std::isfinite(n)||std::abs(n)>std::numeric_limits<float>::max())throw std::runtime_error(std::string("Invalid Attach ")+key);return float(n);};
    const auto getId=[&](const char* key){lua_getfield(L,1,key);int valid=0;const double n=lua_tonumberx(L,-1,&valid);lua_pop(L,1);return valid&&n>0&&n<=INT32_MAX?int32_t(n):0;};
    const auto child=getId("Id"),parent=getId("DestinationId");
    const auto* c=actorWorld_.actor(child);const auto* p=actorWorld_.actor(parent);
    if(!c||!p||!c->registered||!p->registered)return true;
    if(number("TetherDistance")>0){error="Attach tether requires native Physics tether updater";return false;}
    const float x=number("OffsetX"),y=number("OffsetY"),z=number("OffsetZ");
    lua_getfield(L,1,"DynamicScaleOffset");const bool scale=lua_toboolean(L,-1);lua_pop(L,1);
    return actorWorld_.attach(child,parent,x,y,z,scale,error);
}
int ScriptRuntime::attach(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->attachImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

bool ScriptRuntime::switchActiveUnitImpl(lua_State* L,std::string& error){
    if(!lua_istable(L,1)){error="SwitchActiveUnit requires table";return false;}
    lua_getfield(L,1,"PlayerIndex");const int index=lua_isnil(L,-1)?1:int(lua_tointeger(L,-1));lua_pop(L,1);
    const auto players=snapshotInputPlayers(L);if(index<1||size_t(index)>players.size())return true;
    std::vector<int32_t> ids;if(!resolveObjectTargets(L,false,ids,error))return false;
    for(const auto id:ids){
        const auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->arsenal||!a->object->playerControlled)continue;
        const auto& player=players[size_t(index-1)];
        if(player.activeUnitId==id&&player.activeUnitLifetime==a->entityHandle)return true;
        if(player.activeUnitLifetime){error="SwitchActiveUnit old-player-unit release requires live control cleanup";return false;}
        if(!bindRecoveredPlayerUnit(L,uint32_t(index),id,a->entityHandle,error))return false;
        const auto args=[index](lua_State* state,std::string&){lua_pushinteger(state,index);lua_setfield(state,-2,"PlayerIndex");return true;};
        if(!dispatchCallbacks("OnActiveUnitChanged",a->definition->name.c_str(),id,args)){error=error_;return false;}
        return true;
    }
    return true;
}
int ScriptRuntime::switchActiveUnit(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->switchActiveUnitImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

int ScriptRuntime::setLifeProperty(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->setActorPropertyImpl(L,true,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

bool ScriptRuntime::inactiveIdsImpl(lua_State* L,bool byType,std::string& error){
    if(!lua_istable(L,1)){error="Inactive IDs action requires table";return false;}
    if(!activationStarted_){error="Inactive IDs require native Map creation";return false;}
    lua_getfield(L,1,"Names");if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,1,"Name");}
    std::vector<std::string> names;
    if(lua_isstring(L,-1))names.emplace_back(lua_tostring(L,-1));
    else if(lua_istable(L,-1)){
        const size_t count=lua_rawlen(L,-1);if(count>100000)throw std::runtime_error("Inactive names outside bounds");
        for(size_t i=1;i<=count;++i){lua_rawgeti(L,-1,int(i));if(!lua_isstring(L,-1))throw std::runtime_error("Inactive query name requires string");names.emplace_back(lua_tostring(L,-1));lua_pop(L,1);}
    }
    lua_pop(L,1);lua_newtable(L);int index=1;
    for(const auto& [id,o]:inactiveMap_){
        const bool matches=std::any_of(names.begin(),names.end(),[&](const std::string& name){return byType?o->name==name:std::find(o->groups.begin(),o->groups.end(),name)!=o->groups.end();});
        if(matches){lua_pushinteger(L,id);lua_rawseti(L,-2,index++);}
    }
    lua_pushvalue(L,-1);lua_setglobal(L,byType?"InactiveIdsByTypeTable":"InactiveIdsTable");return true;
}
int ScriptRuntime::inactiveIds(lua_State* L){
    const bool byType=lua_toboolean(L,lua_upvalueindex(2));char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->inactiveIdsImpl(L,byType,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 1;
}

bool ScriptRuntime::loadMovieDefinitions(){
    auto asset=assets_->open("movie-preloads.lua");if(!asset||asset->size()>4*1024*1024){error_="Missing or oversized movie preload catalog";return false;}
    std::vector<char> bytes(asset->size());size_t at=0;while(at<bytes.size()){const auto n=asset->read(bytes.data()+at,bytes.size()-at);if(!n){error_="Truncated movie preload catalog";return false;}at+=n;}
    const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@movie-preloads.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);
    if(status!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    hades::SourceValue catalog;const bool ok=hades::readSourceValue(state_,-1,catalog,error_);lua_settop(state_,top);return ok&&movies_.loadCatalog(catalog,error_);
}
bool ScriptRuntime::preloadBinksImpl(lua_State* L,std::string& error){
    if(!lua_istable(L,1)){error="PreLoadBinks requires table";return false;}
    lua_getfield(L,1,"Cache");const char* cache=lua_tostring(L,-1);const std::string selected=cache?cache:"";lua_pop(L,1);
    lua_getfield(L,1,"Reset");const bool reset=lua_toboolean(L,-1);lua_pop(L,1);
    lua_getfield(L,1,"Names");if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,1,"Name");}
    std::vector<std::string> names;
    if(lua_isstring(L,-1))names.emplace_back(lua_tostring(L,-1));
    else if(lua_istable(L,-1)){
        const auto count=lua_rawlen(L,-1);if(count>100000)throw std::runtime_error("PreLoadBinks names outside bounds");
        for(size_t i=1;i<=count;++i){lua_rawgeti(L,-1,int(i));if(!lua_isstring(L,-1))throw std::runtime_error("PreLoadBinks name requires string");names.emplace_back(lua_tostring(L,-1));lua_pop(L,1);}
    }
    lua_pop(L,1);hades::MoviePreloadResult result;
    if(!movies_.preload(names,selected,reset,result,error))return false;
    for(const auto& a:result.attempts)hades::log(hades::Severity::Info,tag,"NATIVE_MOVIE_PRELOAD cache=%s name=%s result=%s bytes=%llu",selected.c_str(),a.movieName.c_str(),hades::moviePreloadOutcomeName(a.outcome),static_cast<unsigned long long>(a.bytes));
    return true;
}
int ScriptRuntime::preloadBinks(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->preloadBinksImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

bool ScriptRuntime::fadeImpl(lua_State* L,bool inward,std::string& error){
    if(!lua_istable(L,1)){error="Fade requires table";return false;}
    lua_getfield(L,1,"Duration");const float duration=float(lua_tonumber(L,-1));lua_pop(L,1);
    if(inward)return cameraFade_.in(duration,error);
    std::array<float,4> color{0,0,0,1};lua_getfield(L,1,"Color");
    if(lua_istable(L,-1)){
        hades::SourceValue value;if(!hades::readSourceValue(L,-1,value,error)){lua_pop(L,1);return false;}
        const auto& channels=value.array();if(channels.size()!=4)throw std::runtime_error("Fade Color requires source RGBA bytes");
        for(size_t i=0;i<4;++i){const auto v=channels[i].number();if(v<0||v>255||v!=std::trunc(v))throw std::runtime_error("Fade color outside byte range");color[i]=float(v)/255.f;}
    }else if(lua_isnil(L,-1)){
        const char* names[]{"R","G","B","A"};for(size_t i=0;i<4;++i){lua_getfield(L,1,names[i]);if(!lua_isnil(L,-1)){const double v=lua_tonumber(L,-1);if(!std::isfinite(v))throw std::runtime_error("Nonfinite fade color");if(i<3){if(v<0||v>255)throw std::runtime_error("Fade RGB outside byte subset");color[i]=float(uint8_t(v))/255.f;}else color[i]=float(uint8_t(std::clamp(v,0.,1.)*255))/255.f;}lua_pop(L,1);}
    }else throw std::runtime_error("Fade named color requires native color lookup");
    lua_pop(L,1);return cameraFade_.out(color,duration,error);
}
int ScriptRuntime::fade(lua_State* L){
    const bool inward=lua_toboolean(L,lua_upvalueindex(2));char message[2048]{};bool ok=false;
    try{std::string error;ok=runtime(L)->fadeImpl(L,inward,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

int ScriptRuntime::applyUpwardForce(lua_State* L){
    char message[2048]{};bool ok=false;
    try{
        if(!lua_istable(L,1))throw std::runtime_error("ApplyUpwardForce requires table");
        // Exact native Id-only fast path; Name/Names/Ids are diagnostic probes
        // and never expand the target selection for this action.
        lua_getfield(L,1,"Id");int numeric=0;const auto id=lua_tointegerx(L,-1,&numeric);lua_pop(L,1);
        if(!numeric||id<INT32_MIN||id>INT32_MAX)ok=true;
        else{
            const auto* object=findObjectSnapshot(L,int32_t(id));
            if(!object||!object->active||!object->alive)ok=true;
            else{
                const auto number=[&](const char* key){lua_getfield(L,1,key);const float value=float(lua_tonumber(L,-1));lua_pop(L,1);return value;};
                const float speed=number("Speed"),hang=number("HangTime");std::string error;
                ok=runtime(L)->actorWorld_.applyScriptUpwardForce(int32_t(id),speed,hang,error);
                if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());
            }
        }
    }catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}

bool ScriptRuntime::effectMarker(int32_t id,const std::string& name,std::array<float,3>& result,std::string& error)const{
    try{
        // Animation::GetLocation296c90, UseAttachedMarkerLocation (data+0x3e8)
        // branch: owner Thing(anim+0xb8) pAnim(+0x138) vslot0xd8 with the
        // marker name. vtables (PDB ??_7): Book/Slide -> BookAnimation::
        // GetMarkerLocation2b0d10; Animation/Constant/Dataless -> base
        // Animation::GetMarkerLocation299b70. pAnim null -> owner attach-to
        // location + Thing+0x200, else Thing+0x88 (= Thing::GetLocation).
        if(id<0){
            // Projectile owner Thing: pAnim(+0x138) null -> Thing::GetLocation (mAttachedTo
            // location + mAttachOffset, else +0x88) with its Z; a projectile primary
            // Animation's marker lookup is not reconstructed.
            const auto it=projectileFxOwners_.find(id);const auto* p=it==projectileFxOwners_.end()?nullptr:projectileWorld_.actor(it->second);
            if(!p||p->disposed)throw std::runtime_error("Native marker projectile owner unavailable");
            float x=0,y=0,z=0;if(!projectileWorld_.projectileLocation(*p,x,y,error)||!projectileWorld_.projectileZ(*p,z,error))return false;
            if(p->primaryAnimation){
                // Animation::GetMarkerLocation299b70 delegates to its child;
                // Constant inherits this base method and ignores the name.
                const auto* primary=actionEffects_.state(p->primaryAnimation);std::unordered_set<uint64_t> seen;
                while(primary){if(!seen.insert(primary->handle).second)throw std::runtime_error("Projectile primary marker child cycle");
                    if(primary->type==hades::NativeAnimationType::Book||primary->type==hades::NativeAnimationType::Slide)throw std::runtime_error("Projectile Book primary source marker atlas not reconstructed: "+name);
                    if(primary->child){primary=actionEffects_.state(primary->child);if(!primary)throw std::runtime_error("Projectile marker child lifetime unavailable");continue;}break;}
                if(!primary)throw std::runtime_error("Projectile marker primary lifetime unavailable");const auto& d=primary->definition;
                for(const char* key:{"UseAttachedMarkerLocation","UseAttachedFiredByMarkerLocation"})if(const auto* v=d.get(key);v&&!v->text().empty())throw std::runtime_error(std::string("Projectile primary marker recursion requires ")+key);
                if(const auto* v=d.get("UseAttachedTipLocation");v&&v->boolean())throw std::runtime_error("Projectile primary tip marker branch not reconstructed");
                const auto* policy=d.get("LocationFromOwner");if(policy&&policy->text()!="Maintain"){x=primary->position[0];y=primary->position[1];}
                else {x+=primary->attachOffset?(*primary->attachOffset)[0]:(d.get("OffsetX")?float(d.get("OffsetX")->number()):0.f);y+=primary->attachOffset?(*primary->attachOffset)[1]:(d.get("OffsetY")?float(d.get("OffsetY")->number()):0.f);}
            }
            result={x,y,z};error.clear();return true;
        }
        const auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object)throw std::runtime_error("Native marker owner unavailable: owner="+std::to_string(id));
        if(!a->animation.active()){
            const auto primaryName=[&](){return " owner="+std::to_string(id)+" name="+a->object->name+" marker="+name;};
            if(a->primaryEffectAnimation)throw std::runtime_error("GetMarkerLocation on VFX-owned primary not reconstructed:"+primaryName());
            std::array<float,2> xy{};
            if(a->primaryAnimationLifetime){
                hades::NativeAnimationType type;uint64_t child=0;const hades::SourceValue* data=nullptr;
                if(const auto* scene=scenePrimaries_.state(a->primaryAnimationLifetime)){type=scene->type;child=scene->child;data=&scene->definition;}
                else if(const auto* action=actionEffects_.state(a->primaryAnimationLifetime);action&&action->primary){type=action->type;child=action->child;data=&action->definition;}
                else throw std::runtime_error("GetMarkerLocation primary class unavailable:"+primaryName());
                if(type==hades::NativeAnimationType::Book||type==hades::NativeAnimationType::Slide)throw std::runtime_error("BookAnimation::GetMarkerLocation on non-HAA primary not reconstructed:"+primaryName());
                // Base Animation::GetMarkerLocation299b70 ignores the name:
                // VA299b76 child(+0xc8, AddChild293ae0) -> child vslot0xd8 with
                // the same name; VA299b9b owner(+0xb8) -> GetLocation296c90;
                // else own location +0x98.
                if(child)throw std::runtime_error("Animation::GetMarkerLocation child delegation (+0xc8) not reconstructed:"+primaryName());
                // GetLocation296c90 on the primary itself: its own marker
                // strings (+0x3e8/+0x400) and UseAttachedTipLocation (+0x9c)
                // precede LocationFromOwner (+0xc2, OwnerProperty enum
                // parsed by 0x18008ee30: Maintain=0, Take=1, Ignore=2).
                for(const char* k:{"UseAttachedMarkerLocation","UseAttachedFiredByMarkerLocation"}){const auto* v=data->get(k);if(v&&!v->text().empty())throw std::runtime_error(std::string("Primary Animation::GetLocation attached marker branch not reconstructed: ")+k+primaryName());}
                if(const auto* v=data->get("UseAttachedTipLocation");v&&v->boolean())throw std::runtime_error("Primary Animation::GetLocation UseAttachedTipLocation branch not reconstructed:"+primaryName());
                const auto* from=data->get("LocationFromOwner");const auto mode=from?from->text():std::string();
                if(!mode.empty()&&mode!="Maintain")throw std::runtime_error("Primary Animation::GetLocation own-location (+0x98) branch not reconstructed: LocationFromOwner="+mode+primaryName());
                // Maintain (VA296eb3..): owner location + anim offset
                // (+0xa0/+0xa4), mAttachOffset, zero for a Thing primary.
            }
            if(!actorWorld_.location(id,xy[0],xy[1],error))return false;
            float z=0;if(!actorWorld_.zLocation(id,z,error))return false;
            result={xy[0],xy[1],z};error.clear();return true;
        }
        // Book.GetMarkerLocation2b0d75..0dc4: absent frame dataset falls
        // through child delegation; absent marker in an existing dataset
        // returns the cached Animation.GetLocation directly. Neither is a
        // zero marker sample: the full marker Z/shake transform is skipped.
        if(!markers_.hasMarker(a->animation.clipIndex(),name)){
            const auto* life=effects_.registry()->find(a->primaryAnimationLifetime);
            if(!life)throw std::runtime_error("Book marker fallback actual primary lifetime unavailable");
            if(!markers_.hasDataSet(a->animation.clipIndex())&&life->child)throw std::runtime_error("Book marker fallback child delegation requires actual child marker consumer");
            const auto& data=life->sourceDefinition;
            for(const char* key:{"UseAttachedMarkerLocation","UseAttachedFiredByMarkerLocation","ChildAnimation"})if(const auto* value=data.get(key);value&&!value->text().empty()&&value->text()!="null")throw std::runtime_error(std::string("Book marker fallback base-location branch requires ")+key);
            if(const auto* value=data.get("UseAttachedTipLocation");value&&value->boolean())throw std::runtime_error("Book marker fallback attached-tip location not reconstructed");
            if(const auto* value=data.get("LocationFromOwner");value&&!value->text().empty()&&value->text()!="Maintain")throw std::runtime_error("Book marker fallback own-location policy not reconstructed");
            // Data.OffsetX/Y become DRAW offsets at Animation+90/+94
            // (InitData2923db), and Book only adds them on its real marker
            // branch. They are not GetLocation's mAttachOffset(+a0/+a4).
            // Defaults291a50 clears that separate attach offset; InitData
            // 2930dd adds only actual random/radial initialization offsets.
            // Named HAA SetAnimation offset overrides remain explicitly
            // unsupported, so these source-zero components stay zero here.
            for(const char* key:{"RandomOffsetX","RandomOffsetY","RadialOffsetMin","RadialOffsetMax"})if(const auto* value=data.get(key);value&&value->number()!=0)throw std::runtime_error(std::string("Book marker fallback actual initialized offset requires ")+key);
            float x=0,y=0,z=0;if(!actorWorld_.location(id,x,y,error)||!actorWorld_.zLocation(id,z,error))return false;
            result={x,y,z};error.clear();return true;
        }
        hades::ActorMarkerTransform transform;transform.resolved=true;
        if(!actorWorld_.location(id,transform.locationX,transform.locationY,error)||!actorWorld_.zLocation(id,transform.attachedThingZ,error))return false;
        transform.drawAngle=0; // Reviewed covered oriented Binks/Ignore-angle entrance.
        transform.shakeX=a->animationShakeX;transform.shakeY=a->animationShakeY;
        markers_.applySourceTransform(a->animation.clipIndex(),a->object->scale*a->object->scaleFraction,a->animationOwnScale,transform);
        const auto angle=animationBank_->originalAngleForFacing(a->animation.clipIndex(),actorAnimationFacing(*a));
        const auto point=markers_.sample(a->animation.clipIndex(),a->animation.originalFrame(),angle,name);
        const auto xy=hades::ActorMarkerBank::location(point,transform);
        // Marker itself returns native Vector2. Selected Laurel source animation
        // separately inherits owner GetZLocation, with zero own Z components.
        result={xy[0],xy[1],transform.attachedThingZ};error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
