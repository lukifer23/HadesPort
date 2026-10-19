// Host save/load debugger path (--probe-save=NEW_DIRECTORY). Uses the real
// ScriptRuntime, the original Main.lua Save()/Load() and the native save files.
#include "save_probe.hpp"
#include "../app/lua_runtime.hpp"
#include "../app/save_runtime.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
int luaopen_luabins(lua_State*);
}

namespace {
void json(std::ostream& out,const std::string& text){
    out<<'"';for(unsigned char c:text){if(c=='"'||c=='\\')out<<'\\'<<c;else if(c<32){constexpr char h[]="0123456789abcdef";out<<"\\u00"<<h[c>>4]<<h[c&15];}else out<<c;}out<<'"';
}
// Independent VM: luabins.load each byte string and compare structurally,
// so table iteration order cannot hide or fake a difference.
constexpr const char* compareChunk=R"LUA(
local a,b = ...
local function eq(x,y,path,seen)
  if type(x) ~= type(y) then return false, path..": type "..type(x).." ~= "..type(y) end
  if type(x) ~= "table" then
    if x ~= x and y ~= y then return true end
    if x == y then return true end
    return false, path..": "..tostring(x).." ~= "..tostring(y)
  end
  for k,v in pairs(x) do
    local ok,why = eq(v, y[k], path.."."..tostring(k), seen)
    if not ok then return false, why end
  end
  for k,_ in pairs(y) do if x[k] == nil then return false, path.."."..tostring(k)..": missing on left" end end
  return true
end
local okA, ta = luabins.load(a)
local okB, tb = luabins.load(b)
if not okA then return false, "left decode: "..tostring(ta), 0 end
if not okB then return false, "right decode: "..tostring(tb), 0 end
local count = 0
for _ in pairs(ta) do count = count + 1 end
local hasState = type(ta.GameState) == "table" and type(ta.CurrentRun) == "table"
if not hasState then return false, "GameState/CurrentRun missing from save table", count end
local resave = luabins.save(ta)
local ok, why = eq(ta, tb, "_G", {})
return ok, why or "", count, resave == a, resave == b
)LUA";
struct Compare {bool equal=false;std::string detail;int keys=0;bool resaveIdentical=false,resaveMatchesRight=false;};
Compare compare(const std::string& a,const std::string& b){
    Compare result;lua_State* L=luaL_newstate();
    if(!L){result.detail="VM allocation failed";return result;}
    luaL_openlibs(L);luaL_requiref(L,"luabins",luaopen_luabins,1);lua_pop(L,1);
    if(luaL_loadbufferx(L,compareChunk,std::strlen(compareChunk),"=save_probe_compare","t")!=LUA_OK){result.detail=lua_tostring(L,-1);lua_close(L);return result;}
    lua_pushlstring(L,a.data(),a.size());lua_pushlstring(L,b.data(),b.size());
    if(lua_pcall(L,2,5,0)!=LUA_OK){result.detail=lua_tostring(L,-1);lua_close(L);return result;}
    result.equal=lua_toboolean(L,-5);result.detail=lua_tostring(L,-4)?lua_tostring(L,-4):"";result.keys=int(lua_tointeger(L,-3));result.resaveIdentical=lua_toboolean(L,-2);result.resaveMatchesRight=lua_toboolean(L,-1);
    lua_close(L);return result;
}
std::string fileBytes(const std::string& path){std::ifstream in(path,std::ios::binary);std::ostringstream s;s<<in.rdbuf();return s.str();}
}

bool runSaveProbe(hades::AssetStore& assets,ScriptRuntime& first,const std::string& directory,const std::string& reportPath){
    std::ostringstream report;bool ok=true;std::string error;
    auto fail=[&](const std::string& what){ok=false;if(error.empty())error=what;};
    report<<"{\"scope\":\"SaveCheckpoint invoked directly through the registered Lua action after original pre-creation; no script-reached checkpoint exists in the opening-room flow yet\"";
    if(std::filesystem::exists(directory))throw std::runtime_error("Save probe directory exists; choose a new directory");
    first.setSaveDirectory(directory);
    report<<",\"has_save_before\":"<<(first.hasSave()?"true":"false");
    // 1. luabins round trip of the original pre-creation save table.
    std::string before;
    if(!first.serializeLuaState(before,error))fail("serialize: "+error);
    const auto roundTrip=compare(before,before);
    report<<",\"luabins_roundtrip\":{\"bytes\":"<<before.size()<<",\"top_level_keys\":"<<roundTrip.keys<<",\"structural_equal\":"<<(roundTrip.equal?"true":"false")<<",\"resave_byte_identical\":"<<(roundTrip.resaveIdentical?"true":"false")<<",\"detail\":";json(report,roundTrip.detail);report<<'}';
    // Re-saving a loaded table may reorder hash iteration; structural equality is the criterion.
    if(!roundTrip.equal)fail("luabins round trip: "+roundTrip.detail);
    // 2. Original-script checkpoint calls (RoomManager.lua:4761-4762 shape).
    std::string saveError;
    const bool saved=first.invokeSaveCheckpoint("_Temp","",saveError);
    if(!saved)fail("SaveCheckpoint: "+saveError);
    std::string savedState;if(!first.serializeLuaState(savedState,error))fail("post-save serialize: "+error);
    const std::string tempPath=(std::filesystem::path(directory)/"Profile1_Temp.sav").string();
    const std::string fileData=fileBytes(tempPath);
    std::vector<uint8_t> bytes(fileData.begin(),fileData.end());hades::save::SaveFile file;std::string decodeError;
    const bool decoded=hades::save::decodeSave(bytes,true,file,decodeError);
    std::string fileState;if(decoded&&!hades::save::luabinsState(file,fileState,decodeError))fail("decompress: "+decodeError);
    if(!decoded)fail("decode: "+decodeError);
    report<<",\"save_checkpoint\":{\"ok\":"<<(saved?"true":"false")<<",\"error\":";json(report,saveError);
    report<<",\"path\":";json(report,tempPath);report<<",\"file_bytes\":"<<fileData.size()<<",\"magic\":";json(report,fileData.substr(0,4));
    report<<",\"checksum_verified\":"<<(decoded?"true":"false")<<",\"version\":"<<file.version<<",\"timestamp\":"<<file.info.timestamp<<",\"location\":";json(report,file.info.location);
    report<<",\"completed_runs\":"<<file.info.completedRuns<<",\"accumulated_meta_points\":"<<file.info.accumulatedMetaPoints<<",\"spent_shrine_points\":"<<file.info.spentShrinePoints<<",\"easy_mode\":"<<file.info.easyMode<<",\"hard_mode\":"<<file.info.hardMode<<",\"notable\":[";
    for(size_t i=0;i<file.info.notable.size();++i){if(i)report<<',';json(report,file.info.notable[i]);}
    report<<"],\"current_map\":";json(report,file.currentMap);report<<",\"next_map\":";json(report,file.nextMap);
    report<<",\"compressed_lua_bytes\":"<<file.luaBlock.size()<<",\"luabins_bytes\":"<<fileState.size()<<",\"file_state_equals_saveData\":"<<(fileState==savedState?"true":"false")<<'}';
    if(decoded&&fileState!=savedState)fail("file Lua state differs from _saveData");
    // 3. Fresh runtime: load, then original OnPreThingCreation with restored state.
    ScriptRuntime second(&assets);second.initialize();second.setSaveDirectory(directory);
    const bool hasSave=second.hasSave();
    const bool loaded=hasSave&&second.loadSave();
    const std::string loadError=second.error();
    // Compare immediately after ProgressManager::LoadLua, before original
    // OnPreThingCreation (DoPatches etc.) legitimately mutates GameState.
    std::string after;if(loaded&&!second.serializeLuaState(after,error))fail("reload serialize: "+error);
    const auto restored=compare(savedState,after);
    const bool booted=loaded&&second.bootRoom();
    report<<",\"reload\":{\"has_save\":"<<(hasSave?"true":"false")<<",\"loaded\":"<<(loaded?"true":"false")<<",\"path\":";json(report,second.loadedSavePath());
    report<<",\"error\":";json(report,loadError);report<<",\"pre_creation_after_load\":"<<(booted?"true":"false");
    report<<",\"luabins_bytes\":"<<after.size()<<",\"byte_identical\":"<<(after==savedState?"true":"false")<<",\"byte_identical_to_luabins_resave_of_saved\":"<<(restored.resaveMatchesRight?"true":"false")<<",\"structural_equal\":"<<(restored.equal?"true":"false")<<",\"detail\":";json(report,restored.detail);report<<'}';
    if(!loaded)fail("load: "+loadError);
    if(!restored.equal)fail("restored state differs: "+restored.detail);
    report<<",\"ok\":"<<(ok?"true":"false")<<",\"error\":";json(report,error);report<<"}\n";
    std::ofstream out(reportPath);out<<report.str();
    return ok;
}
