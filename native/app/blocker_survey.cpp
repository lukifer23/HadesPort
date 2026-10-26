// Blocker survey (hades-debug --survey-blockers) and device fault containment.
// Not a reconstruction: it lets one replay list every unreconstructed engine
// behaviour instead of stopping at the first. Each script-visible C function
// is wrapped so a failure is recorded and returns nothing, and `assert` records
// instead of raising, so the original Main.lua thread loop keeps running.
// Results after the first entry may be cascades of earlier ones. The Android
// engine enables it with setFaultContainment(true) so one missing behaviour
// doesn't end the room; every contained fault is logged (CONTAINED_FAULT /
// SURVEY_BLOCKER) and counted on screen.
#include "lua_runtime.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <sstream>
#include <vector>
#include <cstdio>
#include "object_runtime.hpp"
#include "input_runtime.hpp"
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
namespace {
constexpr const char* kWrap = R"lua(
local record = ...
local wrap = function(name, f)
  -- Allocation-free: pass pcall's results straight through.
  local finish = function(ok, ...)
    if not ok then record(name .. ": " .. tostring((...))) return end
    return ...
  end
  return function(...) return finish(pcall(f, ...)) end
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
    if(self&&message){auto& e=self->survey_[message];if(e.count++==0){e.firstFrame=self->worldFrame_;self->lastContained_=message;
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
bool ScriptRuntime::containStage(const char* stage){
    if(!faultContainment_){worldFaulted_=true;reportError(stage);return false;}
    const std::string key=std::string("world ")+stage+": "+error_;auto& e=survey_[key];
    if(e.count++==0){e.firstFrame=worldFrame_;lastContained_=key;
        hades::log(hades::Severity::Error,"HadesSurvey","CONTAINED_FAULT frame=%llu stage=%s detail=%s",(unsigned long long)worldFrame_,stage,error_.c_str());}
    error_.clear();return true;
}
std::string ScriptRuntime::blockerSurveyReport()const{
    std::vector<std::pair<std::string,SurveyEntry>> rows(survey_.begin(),survey_.end());
    std::sort(rows.begin(),rows.end(),[](const auto& a,const auto& b){return a.second.firstFrame<b.second.firstFrame;});
    std::ostringstream out;out<<"SURVEY_SUMMARY distinct="<<rows.size()<<'\n';
    for(const auto& [message,e]:rows)out<<"  frame="<<e.firstFrame<<" count="<<e.count<<" "<<message<<'\n';
    return out.str();
}

std::string ScriptRuntime::heroMotionTrace(){
    const auto players=snapshotInputPlayers(state_);if(players.empty())return "hero=none";
    const int32_t id=players.front().activeUnitId;auto* a=actorWorld_.actor(id);if(!a||!a->object)return "hero=missing";
    const auto m=localMovement_.find(a->entityHandle);const auto& cam=camera_.state();char line[640];
    std::string tg;for(const auto& t:cam.targets)tg+=std::to_string(t.id)+",";
    // UpdateMovement clears speedFraction each frame; report the consumed
    // previous value plus raw input and velocity so an idle trace is meaningful.
    const float vx=a->physics?a->physics->velocity.x:0,vy=a->physics?a->physics->velocity.y:0;
    const auto facing=a->animation.active()?int(animationBank_->originalAngleForFacing(a->animation.clipIndex(),actorAnimationFacing(*a))):-1;
    std::snprintf(line,sizeof(line),"hero x=%.1f y=%.1f angle=%.3f goal=%.3f input=%.2f,%.2f consumedSpeedFraction=%.2f velocity=%.1f,%.1f moving=%d anim=%s frame=%u finished=%d playSpeed=%.2f facing=%d cam=%.1f,%.1f zoom=%.3f targets=[%s] panActive=%d",a->object->x,a->object->y,a->object->angle,
        m!=localMovement_.end()?m->second.goalAngle:-1.f,localMoveX_,localMoveY_,m!=localMovement_.end()?m->second.previousSpeedFraction:-1.f,vx,vy,int(m!=localMovement_.end()&&m->second.moving),a->object->animation.c_str(),a->animation.originalFrame(),int(a->animation.finished()),a->animation.playSpeedMultiplier(),facing,cam.x,cam.y,cam.zoom,tg.c_str(),int(cam.panX.duration>0||cam.panY.duration>0));
    // Keep the actual rotation/control gates beside direction evidence: an
    // interrupted weapon can leave a request or disable active after release.
    std::ostringstream gates;gates<<line<<" registered="<<a->registered<<" lifeStatus="<<int(a->lifeStatus)<<" allowInput="<<a->allowInput<<" speedModifier="<<a->speedModifier<<" worldSpeed="<<worldSpeedModifier_<<" worldTime="<<worldTime_<<" held=[";
    for(const auto& name:localActionDown_)gates<<name<<',';
    gates<<"] animFlags="<<(a->animation.active()?a->animation.clip()->flags:0);
    if(const auto* speed=actorWorld_.currentProperty(id,"RotationSpeed"))gates<<" rotationSpeed="<<speed->number();
    if(m!=localMovement_.end())gates<<" cosmetic="<<m->second.cosmeticAngle;
    if(a->status){const auto status=a->status->snapshot();gates<<" disableTimers="<<status.timers.move<<','<<status.timers.attack<<','<<status.timers.rotate<<" statusFault="<<status.faulted<<" effects=[";
        for(const auto* list:{&status.active,&status.pending})for(const auto handle:*list){std::string detail;const auto* effect=selfEffects_.effectData(handle,detail);if(effect)gates<<effect->name<<',';}
        gates<<']';}
    if(a->arsenal){std::string detail;gates<<" strafing="<<a->arsenal->isStrafing("",detail)<<" requests=[";
        for(const auto& name:a->arsenal->controllableWeapons()){const auto* weapon=a->arsenal->weaponState(name,detail);if(weapon&&weapon->currentFireRequest)gates<<name<<',';}
        gates<<']';}
    return gates.str();
}
