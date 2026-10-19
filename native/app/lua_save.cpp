// Native reconstruction of the original save pipeline. Evidence and engine
// addresses: native/analysis/save-contract.json.
#include "lua_runtime.hpp"
#include "save_runtime.hpp"
#include "platform_log.hpp"
#include <ctime>
#include <filesystem>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
int luaopen_luabins(lua_State*); // pinned upstream Luabins 0.3 (luabins.c)
}

namespace {
constexpr const char* tag="HadesSave";
// sgg::LuaTableExtensions::String/Bool on the action argument table.
std::string argString(lua_State* L,int table,const char* key){
    std::string result;
    if(lua_istable(L,table)){lua_getfield(L,table,key);if(lua_type(L,-1)==LUA_TSTRING)result=lua_tostring(L,-1);lua_pop(L,1);}
    return result;
}
bool argBool(lua_State* L,int table,const char* key,bool fallback){
    bool result=fallback;
    if(lua_istable(L,table)){lua_getfield(L,table,key);if(lua_type(L,-1)==LUA_TBOOLEAN)result=lua_toboolean(L,-1);lua_pop(L,1);}
    return result;
}
// LuaTable::Get<double> after lua_isnumber, truncated by (int)(double).
int32_t fieldInt(lua_State* L,int table,const char* key){
    lua_getfield(L,table,key);int32_t value=0;
    if(lua_isnumber(L,-1))value=int32_t(lua_tonumber(L,-1));
    lua_pop(L,1);return value;
}
// LuaKey::toString for the string/number keys the original iterates.
std::string keyString(lua_State* L,int index){
    if(lua_type(L,index)==LUA_TSTRING)return lua_tostring(L,index);
    lua_pushvalue(L,index);const char* text=lua_tostring(L,-1);std::string result=text?text:"";lua_pop(L,1);return result;
}
// GetByRef<LuaTable>(key) followed by Get<const char*>("Name").
void pushTableNames(lua_State* L,int table,std::vector<std::string>& out){
    lua_pushnil(L);
    while(lua_next(L,table)){
        const int kt=lua_type(L,-2);
        if((kt==LUA_TNUMBER||kt==LUA_TSTRING)&&lua_istable(L,-1)){
            lua_getfield(L,-1,"Name");
            if(lua_type(L,-1)==LUA_TSTRING)out.emplace_back(lua_tostring(L,-1));
            lua_pop(L,1);
        }
        lua_pop(L,1);
    }
}
}

void ScriptRuntime::registerSaveActions(){
    // Engine luaL_openlibs table: _G coroutine table string bit32 math debug
    // luabins utf8, each through luaL_requiref(L, name, open, 1).
    luaL_requiref(state_,"luabins",luaopen_luabins,1);lua_pop(state_,1);
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,saveCheckpointAction,1);lua_setglobal(state_,"SaveCheckpoint");
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,validateCheckpointAction,1);lua_setglobal(state_,"ValidateCheckpoint");
}

std::string ScriptRuntime::savePath(const std::string& name,const char* extension)const{
    return (std::filesystem::path(saveDirectory_)/(name+extension)).string();
}

int ScriptRuntime::saveCheckpointAction(lua_State* L){
    auto* self=static_cast<ScriptRuntime*>(lua_touserdata(L,lua_upvalueindex(1)));
    std::string error;
    // ProgressManager::Save reports failures through its log and SaveResult;
    // the script action itself never raises into Lua.
    if(!self->saveCheckpointImpl(L,error)){self->lastSaveError_=error;hades::log(hades::Severity::Error,tag,"SAVE_CHECKPOINT_FAILED detail=%s",error.c_str());}
    return 0;
}

bool ScriptRuntime::saveCheckpointImpl(lua_State* L,std::string& error){
    // sgg::ScriptAction::SaveCheckpoint (0x180264e00). RepeatSaveOnLoad is a
    // compiled false developer option, so the early return is not taken.
    const int args=1;
    nextDevSaveName_=argString(L,args,"DevSaveName"); // dev saves: UseDevSaves=false in ConfigOptions::Defaults
    nextSaveName_=argString(L,args,"SaveName");
    const std::string startNextMap=argString(L,args,"StartNextMap");
    if(!startNextMap.empty()){
        // Deferred: World::Begin performs the save when the next map starts.
        saveOnLoad_=true;lastSaveError_.clear();
        hades::log(hades::Severity::Info,tag,"SAVE_ON_LOAD_DEFERRED name=%s%s start_next_map=%s",activeProfile_.c_str(),nextSaveName_.c_str(),startNextMap.c_str());
        return true;
    }
    (void)argBool(L,args,"SuspendNonPersisted",false); // read and unused by the original
    (void)argBool(L,args,"ShowSaveSpinner",true);      // App::ShowSaveSpinner presentation only
    const std::string loadOnly=argString(L,args,"LoadOnlyStartNextMap");
    return saveProfile(activeProfile_+nextSaveName_,loadOnly.empty()?startNextMap:loadOnly,error);
}

bool ScriptRuntime::performSaveOnLoad(const std::string& nextMap){
    // World::Begin (0x1803186e0): if s_SaveOnLoad, Save(ACTIVE_PROFILE +
    // NEXT_SAVE_NAME, ..., the map being begun), then clear s_SaveOnLoad.
    if(!saveOnLoad_)return true;
    std::string error;
    const bool ok=saveProfile(activeProfile_+nextSaveName_,nextMap,error);
    saveOnLoad_=false;
    if(!ok){lastSaveError_=error;hades::log(hades::Severity::Error,tag,"SAVE_ON_LOAD_FAILED detail=%s",error.c_str());}
    return ok;
}

int ScriptRuntime::validateCheckpointAction(lua_State* L){
    auto* self=static_cast<ScriptRuntime*>(lua_touserdata(L,lua_upvalueindex(1)));
    // ScriptAction::Execute ValidateCheckpoint: ValidCheckpoint = Bool("Value", true);
    // SaveValidCheckpoint(GetActiveProfile(), valid) when AllowSaveLoad.
    const bool valid=argBool(L,1,"Value",true);
    self->validCheckpoint_=valid;
    if(self->saveDirectory_.empty())return 0;
    std::string error;
    if(!hades::save::writeSaveFile(self->savePath(self->activeProfile_,hades::save::kValidTempExtension),hades::save::encodeValidCheckpoint(valid),true,error)){
        self->lastSaveError_=error;hades::log(hades::Severity::Error,tag,"VALID_CHECKPOINT_FAILED detail=%s",error.c_str());
    }
    return 0;
}

bool ScriptRuntime::gatherSaveInfo(hades::save::SaveInfo& info,std::string& error){
    // ProgressManager::Save, SaveInfo population before GatherSaveData.
    lua_State* L=state_;const int top=lua_gettop(L);
    info=hades::save::SaveInfo{};
    info.timestamp=uint64_t(uint32_t(std::time(nullptr))); // _time64 truncated to u32 (movl %eax,%ecx)
    lua_getglobal(L,"GameState");
    if(lua_istable(L,-1)){
        const int gs=lua_gettop(L);
        lua_getfield(L,gs,"LocationName");if(lua_isstring(L,-1))info.location=lua_tostring(L,-1);lua_pop(L,1);
        info.completedRuns=fieldInt(L,gs,"CompletedRunsCache");
        info.accumulatedMetaPoints=fieldInt(L,gs,"AccumulatedMetaPointsCache");
        info.spentShrinePoints=fieldInt(L,gs,"SpentShrinePointsCache");
        lua_getfield(L,gs,"Flags");
        if(lua_istable(L,-1)){lua_getfield(L,-1,"HardMode");info.hardMode=lua_toboolean(L,-1);lua_pop(L,1);}
        lua_pop(L,1);
        // ConfigOptions::EasyMode, through the recovered config option runtime.
        lua_getglobal(L,"GetConfigOptionValue");
        lua_createtable(L,0,1);lua_pushliteral(L,"EasyMode");lua_setfield(L,-2,"Name");
        if(lua_pcall(L,1,1,0)!=LUA_OK){error=std::string("EasyMode config: ")+lua_tostring(L,-1);lua_settop(L,top);return false;}
        info.easyMode=lua_toboolean(L,-1);lua_pop(L,1);
        // ConfigOptions::SaveNotableLuaInfo compiled default is true.
        lua_getfield(L,gs,"MetaUpgrades");
        if(lua_istable(L,-1)){
            const int t=lua_gettop(L);lua_pushnil(L);
            while(lua_next(L,t)){if(lua_isnumber(L,-1)&&int(lua_tonumber(L,-1))>0)info.notable.push_back(keyString(L,-2));lua_pop(L,1);}
        }
        lua_pop(L,1);
        lua_getglobal(L,"CurrentRun");
        if(lua_istable(L,-1)){
            const int run=lua_gettop(L);
            lua_getfield(L,run,"Hero");
            if(lua_istable(L,-1)){
                const int hero=lua_gettop(L);
                lua_getfield(L,hero,"Weapons");
                if(lua_istable(L,-1)){const int t=lua_gettop(L);lua_pushnil(L);while(lua_next(L,t)){info.notable.push_back(keyString(L,-2));lua_pop(L,1);}}
                lua_pop(L,1);
                lua_getfield(L,hero,"Traits");
                if(lua_istable(L,-1)){
                    // Traits are fetched by integer key only (GetByRef<LuaTable>(int)).
                    const int t=lua_gettop(L);lua_pushnil(L);
                    while(lua_next(L,t)){
                        if(lua_type(L,-2)==LUA_TNUMBER&&lua_istable(L,-1)){lua_getfield(L,-1,"Name");if(lua_type(L,-1)==LUA_TSTRING)info.notable.emplace_back(lua_tostring(L,-1));lua_pop(L,1);}
                        lua_pop(L,1);
                    }
                }
                lua_pop(L,1);
            }
            lua_pop(L,1);
            lua_getfield(L,run,"CurrentRoom");
            if(lua_istable(L,-1)){
                const struct {const char* key;const char* label;} rooms[]{{"WellShop","WellShop"},{"ChallengeSwitch","ChallengeSwitch"},{"SellTraitShop","SellTraitShop"},{"FishingPointId","FishingPoint"}};
                for(const auto& entry:rooms){lua_getfield(L,-1,entry.key);if(!lua_isnil(L,-1))info.notable.emplace_back(entry.label);lua_pop(L,1);}
            }
            lua_pop(L,1);
        }
        lua_pop(L,1);
        lua_getglobal(L,"OfferedExitDoors");
        if(lua_istable(L,-1))pushTableNames(L,lua_gettop(L),info.notable);
        lua_pop(L,1);
    }
    lua_settop(L,top);return true;
}

bool ScriptRuntime::gatherLuaState(std::string& bytes,std::string& error){
    // ProgressManager::GatherLuaState (0x18021c710).
    lua_State* L=state_;const int top=lua_gettop(L);
    lua_rawgeti(L,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);
    lua_pushinteger(L,hades::save::kRevision);lua_setfield(L,-2,"Revision");
    lua_getfield(L,-1,"Save");
    if(lua_type(L,-1)!=LUA_TFUNCTION){lua_settop(L,top);error="Original Lua Save() is unavailable";return false;}
    // Lua::CallVoid. The original ignores a script error here and reads any
    // previous _saveData; this reconstruction refuses to write a stale state.
    if(lua_pcall(L,0,0,0)!=LUA_OK){error=std::string("Lua Save() failed: ")+(lua_tostring(L,-1)?lua_tostring(L,-1):"?");lua_settop(L,top);return false;}
    lua_getfield(L,-1,"_saveData");
    if(lua_type(L,-1)!=LUA_TSTRING){lua_settop(L,top);error="Lua Save() produced no _saveData string";return false;}
    size_t length=0;const char* data=lua_tolstring(L,-1,&length);
    bytes.assign(data,length);lua_settop(L,top);
    if(length>=hades::save::kLuaStateLimit){error="Uncompressed lua state exceeded max allowable";return false;}
    return true;
}

bool ScriptRuntime::saveProfile(const std::string& name,const std::string& nextMap,std::string& error){
    // ProgressManager::Save (0x1802182d0) -> GatherSaveData -> PrepareForWrite
    // -> SaveToDeviceAsync -> PCSaveDevice::Save (backup, direct write).
    if(saveDirectory_.empty()){error="No save device found";return false;}
    if(!state_||!mainLoaded_){error="Lua state unavailable for save";return false;}
    hades::save::SaveInfo info;std::string lua;std::vector<uint8_t> bytes;
    if(!gatherSaveInfo(info,error)||!gatherLuaState(lua,error))return false;
    if(!hades::save::encodeSave(info,mapName_,nextMap,lua,bytes,error))return false;
    const auto path=savePath(name,hades::save::kSaveExtension);
    if(!hades::save::writeSaveFile(path,bytes,true,error))return false;
    lastSaveError_.clear();
    hades::log(hades::Severity::Info,tag,"SAVE_WRITTEN path=%s bytes=%zu lua_state=%zu map=%s next_map=%s",path.c_str(),bytes.size(),lua.size(),mapName_.c_str(),nextMap.c_str());
    return true;
}

bool ScriptRuntime::invokeSaveCheckpoint(const std::string& saveName,const std::string& startNextMap,std::string& error){
    if(!state_){error="Lua state unavailable";return false;}
    const int top=lua_gettop(state_);
    lua_getglobal(state_,"SaveCheckpoint");
    lua_createtable(state_,0,2);
    if(!saveName.empty()){lua_pushstring(state_,saveName.c_str());lua_setfield(state_,-2,"SaveName");}
    if(!startNextMap.empty()){lua_pushstring(state_,startNextMap.c_str());lua_setfield(state_,-2,"StartNextMap");}
    lastSaveError_.clear();
    if(lua_pcall(state_,1,0,0)!=LUA_OK){error=lua_tostring(state_,-1)?lua_tostring(state_,-1):"SaveCheckpoint failed";lua_settop(state_,top);return false;}
    lua_settop(state_,top);
    if(!lastSaveError_.empty()){error=lastSaveError_;return false;}
    return true;
}

bool ScriptRuntime::hasSave()const{
    if(saveDirectory_.empty())return false;
    return hades::save::fileExists(savePath(activeProfile_,hades::save::kSaveExtension))||
           hades::save::fileExists(savePath(activeProfile_+"_Temp",hades::save::kSaveExtension));
}

bool ScriptRuntime::loadSave(){
    if(!state_||!mainLoaded_){error_="Lua state unavailable for load";return false;}
    if(roomBooted_){error_="Saves restore before OnPreThingCreation; this world already booted";return false;}
    if(saveDirectory_.empty()){error_="No save device found";return false;}
    const auto mainPath=savePath(activeProfile_,hades::save::kSaveExtension);
    const auto tempPath=savePath(activeProfile_+"_Temp",hades::save::kSaveExtension);
    // RemoteProfileScreen::GetPreferredSave: prefer the newer of Profile.sav
    // and Profile_Temp.sav; the temp checkpoint also needs a valid .v.sav.
    struct Candidate {std::string path;hades::save::SaveFile file;bool ok=false;};
    Candidate main{mainPath,{},false},temp{tempPath,{},false};
    for(auto* c:{&main,&temp}){
        std::vector<uint8_t> bytes;std::string detail;
        if(!hades::save::fileExists(c->path))continue;
        if(!hades::save::readSaveFile(c->path,bytes,detail)||!hades::save::decodeSave(bytes,true,c->file,detail)){
            hades::log(hades::Severity::Error,tag,"SAVE_UNREADABLE path=%s detail=%s",c->path.c_str(),detail.c_str());continue;
        }
        c->ok=true;
    }
    // GetPreferredSave (0x1801ce750): main wins when the temp save is strictly
    // older. Otherwise the temp save wins when Profile.v.sav is missing (Load
    // result 1), says valid (result 3), or no main save exists.
    const Candidate* chosen=nullptr;
    if(main.ok&&(!temp.ok||temp.file.info.timestamp<main.file.info.timestamp))chosen=&main;
    else if(temp.ok){
        const auto validPath=savePath(activeProfile_,hades::save::kValidTempExtension);
        bool missing=!hades::save::fileExists(validPath),valid=false;
        if(!missing){std::vector<uint8_t> bytes;std::string detail;
            if(hades::save::readSaveFile(validPath,bytes,detail)&&!hades::save::decodeValidCheckpoint(bytes,valid,detail))valid=false;}
        if(missing||valid||!main.ok)chosen=&temp;
        else chosen=&main;
    }
    if(!chosen){error_="Missing save "+mainPath;return false;}
    const auto& file=chosen->file;
    // ProgressManager::Restore: the world's next map is nextMap, else currentMap.
    const std::string& target=file.restoreMap();
    std::string state;
    if(!hades::save::luabinsState(file,state,error_))return false;
    if(target!=mapName_){MapRequest request;request.name=target;pendingMap_=std::move(request);if(!beginRequestedMap(true))return false;}
    // ProgressManager::LoadLua: Lua Load(buffer) then _worldTime/_screenTime.
    const int top=lua_gettop(state_);
    lua_rawgeti(state_,LUA_REGISTRYINDEX,LUA_RIDX_GLOBALS);
    lua_getfield(state_,-1,"Load");
    if(lua_type(state_,-1)!=LUA_TFUNCTION){lua_settop(state_,top);error_="Original Lua Load() is unavailable";return false;}
    lua_pushlstring(state_,state.data(),state.size());
    if(lua_pcall(state_,1,0,0)!=LUA_OK){error_=std::string("Lua Load() failed: ")+(lua_tostring(state_,-1)?lua_tostring(state_,-1):"?");lua_settop(state_,top);return false;}
    lua_pushnumber(state_,worldTime_);lua_setfield(state_,-2,"_worldTime");
    lua_pushnumber(state_,screenTime_);lua_setfield(state_,-2,"_screenTime");
    lua_settop(state_,top);
    loadedSavePath_=chosen->path;
    hades::log(hades::Severity::Info,tag,"SAVE_LOADED path=%s version=%d map=%s next_map=%s lua_state=%zu",chosen->path.c_str(),file.version,file.currentMap.c_str(),file.nextMap.c_str(),state.size());
    return true;
}
