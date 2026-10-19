// Host-only blocker survey (hades-debug --survey-blockers). DIAGNOSTIC, not a
// reconstruction: it lets one replay list every unreconstructed engine
// behaviour instead of stopping at the first. Each script-visible C function
// is wrapped so a failure is recorded and returns nothing, and `assert` records
// instead of raising, so the original Main.lua thread loop keeps running.
// Results after the first entry may be cascades of earlier ones. The Android
// engine never calls enableBlockerSurvey(); shipped builds still fail loudly.
#include "lua_runtime.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <sstream>
#include <vector>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
namespace {
constexpr const char* kWrap = R"lua(
local record = ...
local wrap = function(name, f)
  return function(...)
    local r = table.pack(pcall(f, ...))
    if not r[1] then record(name .. ": " .. tostring(r[2])) return end
    return table.unpack(r, 2, r.n)
  end
end
local skip = { pcall = true, xpcall = true, error = true, coroutine = true, select = true,
  type = true, tostring = true, tonumber = true, rawget = true, rawset = true, rawequal = true,
  rawlen = true, next = true, pairs = true, ipairs = true, setmetatable = true,
  getmetatable = true, unpack = true, require = true, load = true, loadstring = true,
  dofile = true, print = true, collectgarbage = true, assert = true }
local names = {}
for k, v in pairs(_G) do
  if type(k) == "string" and type(v) == "function" and not skip[k] and debug.getinfo(v, "S").what == "C" then
    names[#names + 1] = k
  end
end
for _, k in ipairs(names) do _G[k] = wrap(k, _G[k]) end
assert = function(v, msg, ...)
  if not v then record("assert: " .. tostring(msg)) end
  return v, msg, ...
end
return #names
)lua";
}
int ScriptRuntime::surveyRecord(lua_State* L){
    auto* self=runtime(L);const char* message=lua_tostring(L,1);
    if(self&&message){auto& e=self->survey_[message];if(e.count++==0){e.firstFrame=self->worldFrame_;
        hades::log(hades::Severity::Error,"HadesSurvey","SURVEY_BLOCKER frame=%llu detail=%s",(unsigned long long)self->worldFrame_,message);}}
    return 0;
}
bool ScriptRuntime::enableBlockerSurvey(std::string& error){
    if(!state_){error="Survey needs an initialized script runtime";return false;}
    if(luaL_loadstring(state_,kWrap)!=LUA_OK){error=lua_tostring(state_,-1);lua_pop(state_,1);return false;}
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,surveyRecord,1);
    if(lua_pcall(state_,1,1,0)!=LUA_OK){error=lua_tostring(state_,-1);lua_pop(state_,1);return false;}
    hades::log(hades::Severity::Info,"HadesSurvey","SURVEY_ENABLED wrapped=%d",int(lua_tointeger(state_,-1)));lua_pop(state_,1);
    error.clear();return true;
}
void ScriptRuntime::surveyRecordWorldFault(){
    // A native world stage failed: the rest of that frame's stages were skipped.
    // Record it and let the next frame run (later results may be cascades).
    auto& e=survey_["world: "+error_];if(e.count++==0){e.firstFrame=worldFrame_;
        hades::log(hades::Severity::Error,"HadesSurvey","SURVEY_BLOCKER frame=%llu detail=world: %s",(unsigned long long)worldFrame_,error_.c_str());}
    worldFaulted_=false;error_.clear();
}
std::string ScriptRuntime::blockerSurveyReport()const{
    std::vector<std::pair<std::string,SurveyEntry>> rows(survey_.begin(),survey_.end());
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.second.firstFrame<b.second.firstFrame;});
    std::ostringstream out;out<<"SURVEY_SUMMARY distinct="<<rows.size()<<'\n';
    for(const auto& [message,e]:rows)out<<"  frame="<<e.firstFrame<<" count="<<e.count<<" "<<message<<'\n';
    return out.str();
}
