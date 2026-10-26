#include <limits>
#include "lua_runtime.hpp"
#include "input_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
using hades::SourceValue;
float number(const SourceValue& v,const char* key,float fallback=0){
    const auto* p=v.get(key);if(!p)return fallback;
    float result=fallback;
    if(std::holds_alternative<double>(p->value))result=float(p->number());
    else if(std::holds_alternative<std::string>(p->value))result=std::strtof(p->text().c_str(),nullptr);
    if(!std::isfinite(result))throw std::runtime_error(std::string("Nonfinite sound property: ")+key);
    return result;
}
float alias(const SourceValue& v,const char* key,const char* old){return v.get(key)?number(v,key):number(v,old);}
bool flag(const SourceValue& v,const char* key){const auto* p=v.get(key);return p&&p->boolean();}
std::string text(const SourceValue& v,const char* key){const auto* p=v.get(key);return p?p->text():"";}
std::vector<uint32_t> ids(const SourceValue& v){
    std::vector<uint32_t> out;
    const auto* values=v.get("Ids");if(!values)values=v.get("Id");if(!values)return out;
    if(const auto* empty=std::get_if<SourceValue::Object>(&values->value);empty&&empty->empty())return out;
    auto append=[&](const SourceValue& x){if(!std::holds_alternative<double>(x.value))throw std::runtime_error("Sound Id requires number");const auto n=x.number();if(n>0&&n<=UINT32_MAX&&std::floor(n)==n)out.push_back(uint32_t(n));};
    if(std::holds_alternative<SourceValue::Array>(values->value))for(const auto& x:values->array())append(x);else append(*values);
    return out;
}
std::vector<std::string> names(const SourceValue& v){
    std::vector<std::string> out;const auto* values=v.get("Names");if(!values)values=v.get("Name");if(!values)return out;
    if(const auto* empty=std::get_if<SourceValue::Object>(&values->value);empty&&empty->empty())return out;
    if(std::holds_alternative<SourceValue::Array>(values->value))for(const auto& x:values->array())out.push_back(x.text());else out.push_back(values->text());return out;
}
}
bool ScriptRuntime::loadAudioDefinitions(){
    auto file=assets_->open("audio-events.lua");if(!file||file->size()>32*1024*1024){error_="Missing or oversized original audio event catalog";return false;}
    std::vector<char> bytes(file->size());size_t at=0;while(at<bytes.size()){auto n=file->read(bytes.data()+at,bytes.size()-at);if(!n){error_="Truncated audio event catalog";return false;}at+=n;}
    const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@audio-events.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);
    if(status!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    SourceValue catalog;bool ok=hades::readSourceValue(state_,-1,catalog,error_);lua_settop(state_,top);if(!ok||!audio_.loadCatalog(catalog,error_)||!audio_.initializeMixingControl(error_))return false;if(audio_.mixingControlId())++audioFramePlays_;return true;
}
bool ScriptRuntime::audioActionImpl(lua_State* L,int mode,uint32_t& result,std::string& error){
    if(!lua_istable(L,1)){error="Sound action requires table";return false;}
    SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;
    if(mode==8){result=audio_.mixingControlId();return true;}
    if(mode==7){for(const auto& name:names(args))if(!audio_.setMixingParameter(name,number(args,"Value"),error))return false;return true;}
    if(mode==6){auto from=names(args);const auto* ds=args.get("DestinationNames");if(!ds)ds=args.get("DestinationName");std::string destination;if(ds){if(const auto* a=std::get_if<SourceValue::Array>(&ds->value)){if(!a->empty())destination=a->front().text();}else if(std::holds_alternative<std::string>(ds->value))destination=ds->text();}if(from.empty()){error="SwapSound requires at least one native Name";return false;}return audio_.swapCue(from.front(),destination,error);}
    if(mode==0){
        const auto cue=text(args,"Name");if(cue.empty()){result=0;return true;}
        float delay=alias(args,"Delay","DelaySeconds"),minimum=alias(args,"DelayMin","DelaySecondsMin"),maximum=alias(args,"DelayMax","DelaySecondsMax");
        if(delay>0&&maximum>minimum&&!delayedRandom(this,minimum,maximum,delay,error))return false;
        bool valid=false;if(!audio_.resolveCue(cue,valid,error,!(delay>0)))return false;if(!valid){result=0;return true;}
        const auto sourceId=int32_t(number(args,"Id",-1));hades::AudioSource source;
        const auto players=snapshotInputPlayers(L);const bool listener=!players.empty()&&players.front().activeUnitId==sourceId;
        const auto* actor=actorWorld_.actor(sourceId);
        if(actor&&actor->registered&&!listener){if(!actorAudioSource(sourceId,source,error))return false;}
        // The shipped 2D path ignores ManagerCap. The 3D path uses global
        // manager/frame counters; never substitute a per-cue count.
        if(source.spatial&&!(delay>0)){
            bool allowed=false;if(!admitOwnerAudio(cue,sourceId,allowed,error,int32_t(number(args,"ManagerCap"))))return false;if(!allowed){result=0;return true;}
        }
        if(!audio_.createCue(cue,source,result,error,!(delay>0)))return false;
        if(result&&flag(args,"AddCallbacks"))audioCallbacks_.insert(result);
        if(delay>0){const float deadline=worldTime_+delay;if(!std::isfinite(deadline)){error="Sound delay overflow";return false;}delayedSounds_.push_back({result,deadline});return true;}
        if(!audio_.play(result,error))return false;if(result)++audioFramePlays_;return true;
    }
    if(mode==9){
        // ScriptAction::SetSoundSource 0x180275f70 (native-set-sound-source-pseudocode.c):
        // average destination locations (accumulators start at FLT_MIN), first
        // destination becomes the channel source Thing; every nonzero live,
        // active channel Id takes both. No destinations -> null Thing, ~origin.
        for(const char* k:{"DestinationName","DestinationNames"})if(args.get(k)){error="SetSoundSource name destinations require native name-query provider";return false;}
        std::vector<int32_t> targets;auto add=[&](const SourceValue& v){if(!std::holds_alternative<double>(v.value))throw std::runtime_error("SetSoundSource destination requires number");const double n=v.number();if(n<1||n>INT32_MAX||std::floor(n)!=n)return;const auto* a=actorWorld_.actor(int32_t(n));if(a&&a->registered&&a->object)targets.push_back(int32_t(n));};
        for(const char* k:{"DestinationId","DestinationIds"})if(const auto* p=args.get(k)){if(std::holds_alternative<SourceValue::Array>(p->value)){for(const auto& x:p->array())add(x);}else if(!std::get_if<SourceValue::Object>(&p->value))add(*p);}
        float x=std::numeric_limits<float>::min(),y=std::numeric_limits<float>::min();int32_t first=0;uint64_t firstLifetime=0;
        for(auto id:targets){float px=0,py=0;if(!actorWorld_.location(id,px,py,error))return false;x+=px;y+=py;if(!first){first=id;firstLifetime=actorWorld_.actor(id)->entityHandle;}}
        if(!targets.empty()){x/=float(targets.size());y/=float(targets.size());}
        for(auto id:ids(args)){hades::AudioChannelState channel;if(!audio_.channelState(id,channel)||channel.stopped||channel.finished)continue;
            hades::AudioSource source=channel.source;source.x=x;source.y=y;source.ownerId=first;source.ownerLifetime=firstLifetime;
            if(!audio_.setSource(id,source,error))return false;}
        return true;
    }
    const auto selected=ids(args);const float duration=number(args,"Duration");
    if(mode==5){for(const auto& name:names(args))for(auto id:selected){hades::AudioChannelState channel;if(audio_.channelState(id,channel)&&!audio_.setParameter(id,name,number(args,"Value"),duration,error))return false;}return true;}
    // Shipped GetSoundCue(name) returns a descriptor with null owner. Its
    // Name/Names control branches therefore do not select running channels.
    for(auto id:selected){
        hades::AudioChannelState channel;if(!audio_.channelState(id,channel))continue;
        if(mode==1){if(duration>0&&channel.volume!=0){if(!audio_.setVolume(id,0,duration,false,true,error))return false;}else if(!audio_.pause(id,error))return false;}
        else if(mode==2){if(!audio_.resume(id,error))return false;if(duration>0&&channel.volume<1&&!audio_.setVolume(id,1,duration,false,false,error))return false;}
        else if(mode==3){if(duration>0&&channel.volume>0){if(!audio_.setVolume(id,0,duration,true,false,error))return false;}else{if(duration>0&&channel.paused&&!audio_.resume(id,error))return false;if(!audio_.stop(id,false,error))return false;}}
        else if(mode==4){const auto value=number(args,"Value");if(!audio_.setVolume(id,value,duration>0?duration:1.f/60,false,false,error))return false;if(duration<=0&&!audio_.setVolume(id,value,0,false,false,error))return false;}
    }
    return true;
}
int ScriptRuntime::audioAction(lua_State* L){
    char message[2048]{};bool ok=false;uint32_t result=0;const int mode=int(lua_tointeger(L,lua_upvalueindex(2)));
    try{std::string error;ok=runtime(L)->audioActionImpl(L,mode,result,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);if(mode==0||mode==8){lua_pushnumber(L,result);return 1;}return 0;
}
bool ScriptRuntime::advanceAudio(float elapsed,std::string& error){
    for(auto it=delayedSounds_.begin();it!=delayedSounds_.end();){if(worldTime_>it->deadline){hades::AudioChannelState channel;if(audio_.channelState(it->id,channel)){if(!audio_.play(it->id,error))return false;++audioFramePlays_;}it=delayedSounds_.erase(it);}else ++it;}
    const auto players=snapshotInputPlayers(state_);if(!players.empty()&&players.front().activeUnitLifetime){float x,y,z;if(!actorWorld_.location(players.front().activeUnitId,x,y,error)||!actorWorld_.zLocation(players.front().activeUnitId,z,error)||!audio_.setListener(x,y,z,error))return false;}
    for(const auto& channel:audio_.attachedChannels()){
        hades::AudioSource source;if(!actorAudioSource(channel.source.ownerId,source,error))return false;
        if(source.ownerLifetime!=channel.source.ownerLifetime){error="Attached audio owner vanished before native OnOwnerDeath";return false;}
        if(!audio_.setSource(channel.id,source,error))return false;
    }
    if(!audio_.advanceControls(elapsed,error))return false;
    for(const auto& marker:audio_.drainMarkers())if(audioCallbacks_.contains(marker.channelId)){
        const auto extra=[&marker](lua_State* L,std::string&){lua_pushinteger(L,marker.positionMs);lua_setfield(L,-2,"Position");return true;};
        if(!dispatchCallbacks("OnMusicMarker",marker.name.c_str(),0,extra)){error=error_;return false;}
    }
    audioFramePlays_=0;return true;
}
bool ScriptRuntime::startAudio(){return audioOutput_.start([this](float* buffer,uint32_t count,std::string& error){return audio_.render(buffer,count,error);},error_);}
bool ScriptRuntime::serviceAudio(){return audioOutput_.service(error_);}
void ScriptRuntime::stopAudio(){audioOutput_.stop();}

// Source Play3D admission reads the actual owner's HighPrioritySfx and the
// global per-frame counter (default16); VO cues bypass the frame cap.
bool ScriptRuntime::playAudioCue(const std::string& cue,int32_t owner,const std::array<float,3>& p,uint32_t& id,std::string& error,int32_t managerCap){
    bool valid=false;if(!audio_.resolveCue(cue,valid,error,true))return false;if(!valid){id=0;return true;}
    const auto players=snapshotInputPlayers(state_);const bool spatial=owner<0||(owner>0&&(players.empty()||players.front().activeUnitId!=owner));
    if(spatial){bool allowed=false;if(!admitOwnerAudio(cue,owner,allowed,error,managerCap))return false;if(!allowed){id=0;return true;}}
    hades::AudioSource source{spatial,p[0],p[1],0};if(spatial&&!actorAudioSource(owner,source,error))return false;
    if(!audio_.createCue(cue,source,id,error,true)||!audio_.play(id,error))return false;
    if(id)++audioFramePlays_;return true;
}

// Native location overload4aa90 (Life.DeathSound); it has no Thing priority flag.
bool ScriptRuntime::playLocationAudioCue(const std::string& cue,const std::array<float,3>& p,int32_t managerCap,uint32_t& id,std::string& error){
    id=0;bool valid=false;if(!audio_.resolveCue(cue,valid,error,true))return false;if(!valid)return true;
    bool spatial=p[0]!=0||p[1]!=0;
    if(spatial){const auto players=snapshotInputPlayers(state_);if(players.empty()||!players.front().activeUnitLifetime){error="Nonzero location sound requires actual AudioManager listener XY";return false;}float x=0,y=0;if(!actorWorld_.location(players.front().activeUnitId,x,y,error))return false;spatial=p[0]!=x||p[1]!=y;}
    if(spatial){
        const bool frameAllowed=audioFramePlays_<16;
        const auto unsignedCap=uint64_t(int64_t(managerCap));
        if(!(frameAllowed&&uint64_t(audio_.allocatedChannelCount())<unsignedCap)){bool voice=false;if(!audio_.voiceCue(cue,voice,error))return false;if(!voice&&!frameAllowed)return true;}
    }
    // Original location API accepts XY only; Create3D's Z is0 for null Thing.
    if(!audio_.createCue(cue,{spatial,p[0],p[1],0},id,error,true)||!audio_.play(id,error))return false;
    if(id)++audioFramePlays_;return true;
}

// AudioChannel3D.GetSourceLocation uses actual Thing location (including
// attachment), then transforms screen-location Things through Camera. Native
// Set3DAttributes always places this XY in a plane; raw Thing Z is not used.
bool ScriptRuntime::actorAudioSource(int32_t id,hades::AudioSource& out,std::string& error){
 if(id<0){const auto it=projectileFxOwners_.find(id);const auto* p=it==projectileFxOwners_.end()?nullptr:projectileWorld_.actor(it->second);if(!p||!p->bodyPending){error="Projectile audio source body unavailable";return false;}if(p->body.screenSpace){error="Spatial screen projectile audio requires camera conversion";return false;}out={true,0,0,0,id,p->request.lifetimeHandle};return projectileWorld_.projectileLocation(*p,out.x,out.y,error);}
 const auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object){error="Audio source requires registered Thing";return false;}
 if(a->object->screenSpace){error="Spatial screen Thing audio requires native Camera screen-to-world provider";return false;}
 out={true,0,0,0,id,a->entityHandle};return actorWorld_.location(id,out.x,out.y,error);
}
bool ScriptRuntime::audioOwnerDeath(int32_t id,std::string& error){
 if(id<0){hades::AudioSource final;if(!actorAudioSource(id,final,error))return false;return audio_.detachOwner(id,final.ownerLifetime,final.x,final.y,error);}
 const auto* a=actorWorld_.actor(id);if(!a||!a->registered){error="AudioOwnerDeath requires still-registered actor";return false;}
 if(audio_.ownedChannels(id,a->entityHandle).empty())return true;
 hades::AudioSource final;if(!actorAudioSource(id,final,error))return false;
 return audio_.detachOwner(id,a->entityHandle,final.x,final.y,error);
}
bool ScriptRuntime::stopActorAmbientSound(int32_t id,std::string& error){
 const auto* a=actorWorld_.actor(id);if(!a||!a->registered){error="StopAmbientSound requires registered actor";return false;}
 // Native does not clear mAmbientSoundId. A stale ID simply finds no channel.
 return !a->ambientSoundId||audio_.stop(a->ambientSoundId,false,error);
}

bool ScriptRuntime::admitOwnerAudio(const std::string& cue,int32_t owner,bool& allowed,std::string& error,int32_t managerCap){
 allowed=false;
 if(owner<0){const auto it=projectileFxOwners_.find(owner);const auto* p=it==projectileFxOwners_.end()?nullptr:projectileWorld_.actor(it->second);if(!p||!p->bodyPending||!p->thing){error="Projectile owner3D sound requires actual initialized Thing";return false;}bool voice=false;if(!audio_.voiceCue(cue,voice,error))return false;if(p->suppressSounds&&!voice)return true;const auto* priority=p->thing->get("HighPrioritySfx");if(!priority){error="Projectile sound source HighPrioritySfx unavailable";return false;}allowed=priority->boolean()||audioFramePlays_<16||voice;return true;}
 const auto* a=actorWorld_.actor(owner);if(!a||!a->registered){error="Owner3D sound requires registered Thing";return false;}
 bool voice=false;if(!audio_.voiceCue(cue,voice,error))return false;
 // Native49d93 reads LIVE Thing+62 before Data.HighPrioritySfx and frame cap.
 // Active-listener owner2D and delayed CreateCue3D bypass this entire path.
 if(a->suppressSounds&&!voice)return true;
 const auto* priority=actorWorld_.currentProperty(owner,"Thing.HighPrioritySfx");if(!priority){error="Owner3D sound requires actual HighPrioritySfx";return false;}
 // Play3D49db8..49dff: manager cap counts ALL allocated channels, not
 // instances of this cue. Exceeding it alone does not reject an ordinary
 // sound: native falls through to the VO/frame admission test.
 const bool frameAllowed=audioFramePlays_<16;
 allowed=priority->boolean()||(frameAllowed&&uint64_t(audio_.allocatedChannelCount())<uint64_t(int64_t(managerCap)))||voice||frameAllowed;return true;
}
