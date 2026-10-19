#include "lua_runtime.hpp"
#include "input_runtime.hpp"
#include "recovered_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
std::string speechText(const hades::SourceValue& a,const char* k){const auto* p=a.get(k);return p?p->text():"";}
int32_t speechOwner(const hades::SourceValue& a){
    const auto* p=a.get("Ids");if(!p)p=a.get("Id");if(!p)return -1;
    if(const auto* v=std::get_if<hades::SourceValue::Array>(&p->value)){if(v->empty())return -1;p=&v->front();}
    if(const auto* v=std::get_if<hades::SourceValue::Object>(&p->value);v&&v->empty())return -1;
    const auto n=p->number();if(!std::isfinite(n)||n<INT32_MIN||n>INT32_MAX)throw std::runtime_error("Invalid speech owner ID");return int32_t(n);
}
}
void ScriptRuntime::initializeSpeechBridge(){
    speech_.setPlayObserver([this]{++audioFramePlays_;});
    speech_.setSubtitleColorProvider([this](int32_t id,hades::SourceValue& color,std::string& error){const auto* a=actorWorld_.actor(id);if(!a||!a->registered)return true;const auto* value=actorWorld_.currentProperty(id,"Thing.SubtitleColor");if(!value){error="Actual Thing SubtitleColor unavailable";return false;}color=*value;return true;});
    speech_.setSourceProvider([this](int32_t id,hades::AudioSource& source,std::string& error){
        const auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered)return true;
        const auto players=snapshotInputPlayers(state_);if(!players.empty()&&players.front().activeUnitId==id)return true;
        return actorAudioSource(id,source,error);
    });
}
bool ScriptRuntime::speechActionImpl(lua_State* L,uint32_t& id,std::string& error){
    id=0;if(!lua_istable(L,1)){error="PlaySpeech requires source arguments";return false;}
    hades::SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;
    const int top=lua_gettop(L);lua_getglobal(L,"GetConfigOptionValue");lua_newtable(L);lua_pushliteral(L,"AllowPlaySpeech");lua_setfield(L,-2,"Name");
    if(lua_pcall(L,1,1,0)!=LUA_OK){const auto* text=lua_tostring(L,-1);error=text?text:"AllowPlaySpeech config failed";lua_settop(L,top);return false;}
    if(!lua_isboolean(L,-1)){lua_settop(L,top);error="Actual AllowPlaySpeech boolean unavailable";return false;}
    const bool allowed=lua_toboolean(L,-1);lua_settop(L,top);if(!allowed)return true;
    hades::SpeechRequest request;request.cue=speechText(args,"Name");request.actor=speechText(args,"Actor");request.ownerId=speechOwner(args);
    if(const auto* p=args.get("UseSubtitles"))request.useSubtitles=p->boolean();
    if(const auto* p=args.get("SubtitleColor"))request.subtitleColor=*p;
    bool valid=false;if(!audio_.resolveCue(request.cue,valid,error))return false;if(!valid)return true;
    if(request.useSubtitles&&!audio_.subtitleText(request.cue,request.subtitleText,error))return false;
    if(const auto* p=args.get("Queue");p&&std::holds_alternative<std::string>(p->value)){
        auto queue=p->text();for(char& c:queue)if(c>='a'&&c<='z')c=char(c-'a'+'A');
        if(queue=="TRUE"||queue=="SOMETIMES")request.queue=hades::SpeechQueueMode::Sometimes;
        else if(queue=="FALSE"||queue=="NEVER")request.queue=hades::SpeechQueueMode::Never;
        else if(queue=="ALWAYS")request.queue=hades::SpeechQueueMode::Always;
        else if(queue=="INTERRUPT")request.queue=hades::SpeechQueueMode::Interrupt;
    }
    bool record=true;bool direct=request.queue==hades::SpeechQueueMode::Never;
    if(request.ownerId>0){const auto* a=actorWorld_.actor(request.ownerId);if(!a||!a->registered){direct=true;record=false;}else{
        const auto* capable=actorWorld_.currentProperty(request.ownerId,"Thing.SpeechCapable");if(!capable){error="Actual Thing SpeechCapable unavailable";return false;}if(!capable->boolean()){direct=true;record=false;}
    }}
    else request.ownerId=-1;
    if(direct){
        hades::AudioSource source;const auto* a=actorWorld_.actor(request.ownerId);
        if(a&&a->registered){const auto players=snapshotInputPlayers(L);source.spatial=players.empty()||players.front().activeUnitId!=request.ownerId;if(source.spatial&&!actorAudioSource(request.ownerId,source,error))return false;}
        if(!request.actor.empty()){error="Direct speech actor override requires original actor-template provider";return false;}
        bool valid=false;if(!audio_.resolveCue(request.cue,valid,error))return false;if(!valid)return true;
        if(!audio_.createCue(request.cue,source,id,error,true)||!audio_.play(id,error))return false;++audioFramePlays_;
    }else if(!speech_.request(request,id,error))return false;
    if(id&&record){lua_getglobal(L,"SpeechRecord");if(lua_istable(L,-1)){lua_pushlstring(L,request.cue.data(),request.cue.size());lua_setfield(L,-2,request.cue.c_str());}lua_pop(L,1);}
    return true;
}
int ScriptRuntime::speechAction(lua_State* L){
    uint32_t id=0;bool ok=false;char message[2048]{};
    try{std::string error;ok=runtime(L)->speechActionImpl(L,id,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);lua_pushnumber(L,id);return 1;
}
bool ScriptRuntime::advanceSpeech(float elapsed,std::string& error){
    if(!speech_.advance(elapsed,error))return false;
    for(const auto& event:audio_.drainCompletions()){
        if(event.voice){
        const auto args=[&event](lua_State* L,std::string&){lua_pushinteger(L,event.channelId);lua_setfield(L,-2,"Id");lua_pushlstring(L,event.name.data(),event.name.size());lua_setfield(L,-2,"Name");return true;};
        if(!dispatchCallbacks("OnSoundComplete",event.name.c_str(),0,args)){error=error_;return false;}
        if(!notifyScriptEvent(state_,event.name,true,false,error))return false;
        }
        if(!speech_.complete(event,error))return false;
    }
    return true;
}
std::vector<hades::SpeechPresentation> ScriptRuntime::drainSpeechPresentation(){return speech_.drainPresentation();}

bool ScriptRuntime::speechControlImpl(lua_State* L,int mode,std::string& error){
    if(!lua_istable(L,1)){error="Speech control requires source table";return false;}
    hades::SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;
    std::vector<int32_t> selected;if(!resolveObjectTargets(L,false,selected,error))return false;
    selected.erase(std::remove_if(selected.begin(),selected.end(),[this](int32_t id){const auto* a=actorWorld_.actor(id);return !a||!a->registered||!a->object||!a->object->active||!a->object->alive||a->object->queryProxy;}),selected.end());
    const bool global=selected.empty();
    const auto capable=[this](int32_t id){const auto* a=actorWorld_.actor(id);const auto* p=a&&a->registered?actorWorld_.currentProperty(id,"Thing.SpeechCapable"):nullptr;return p&&p->boolean();};
    if(global){selected.push_back(-1);if(mode<2)for(const auto id:actorWorld_.registrationOrder())if(capable(id))selected.push_back(id);}
    bool pitch=true;if(const auto* p=args.get("PitchTransition"))pitch=p->boolean();
    const auto number=[&](const char* key){const auto* p=args.get(key);const float n=p?float(p->number()):0.f;if(!std::isfinite(n))throw std::runtime_error(std::string("Nonfinite speech control ")+key);return n;};
    for(const auto owner:selected){if(owner!=-1&&!capable(owner))continue;
        if(mode==0){if(!speech_.pause(owner,pitch,error))return false;}
        else if(mode==1){if(!speech_.resume(owner,pitch,error))return false;}
        else{
            const auto minimum=number("MinDuration"),maximum=number("MaxDuration");float duration=number("Duration");
            if(minimum!=0&&maximum!=0){double draw=0;if(!nextRecoveredEngineRandom(L,draw)){error="Speech duration requires real stream0";return false;}volatile float scaled=float(draw)*(maximum-minimum);duration=minimum+scaled;}
            if(!speech_.stop(owner,duration,error))return false;
        }
    }
    return true;
}
int ScriptRuntime::speechControlAction(lua_State* L){
    char message[2048]{};bool ok=false;const auto mode=int(lua_tointeger(L,lua_upvalueindex(2)));
    try{std::string error;ok=runtime(L)->speechControlImpl(L,mode,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
