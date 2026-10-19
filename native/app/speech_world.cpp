#include "speech_world.hpp"
#include <algorithm>
#include <cmath>
namespace hades {
bool SpeechWorld::fail(const std::string& e,std::string& error){faulted_=true;fault_=e;error=e;return false;}
bool SpeechWorld::playNow(Component& c,const SpeechRequest& request,uint32_t& id,std::string& error){
    id=0;if(c.paused)return true;
    bool valid=false;if(!audio_.resolveCue(request.cue,valid,error))return false;if(!valid)return true;
    AudioSource source;if(request.ownerId!=-1){if(!source_){error="Speech requires live Thing source provider";return false;}if(!source_(request.ownerId,source,error))return false;}
    if(!request.actor.empty()){error="Speech actor override requires original actor-template selection";return false;}
    SpeechRequest actual=request;if(request.ownerId!=-1){if(!subtitleColor_){error="Speech requires current Thing subtitle color provider";return false;}if(!subtitleColor_(request.ownerId,actual.subtitleColor,error))return false;}
    if(c.active){if(!audio_.stop(c.active,false,error))return false;c.active=0;}
    if(!audio_.createCue(request.cue,source,id,error,true)||!audio_.play(id,error))return false;
    if(id){if(played_)played_();c.active=id;c.tracked.push_back({id,actual});presentation_.push_back({true,id,actual});}
    return true;
}
bool SpeechWorld::playNext(Component& c,std::string& error){
    if(c.paused)return true;
    uint32_t id=0;
    if(c.sometimes){auto request=std::move(*c.sometimes);c.sometimes.reset();request.useSubtitles=true;return playNow(c,request,id,error);}
    while(!c.always.empty()){auto request=std::move(c.always.front());c.always.pop_front();request.useSubtitles=true;if(!playNow(c,request,id,error))return false;if(id)break;}
    return true;
}
bool SpeechWorld::request(const SpeechRequest& r,uint32_t& id,std::string& error){
    id=0;if(faulted_){error=fault_;return false;}
    if(components_.size()>=4096&&!components_.contains(r.ownerId))return fail("Speech component bound exceeded",error);
    auto& c=components_[r.ownerId];
    switch(r.queue){
    case SpeechQueueMode::NeverQueue:
        if(!c.always.empty()||c.sometimes||!c.tracked.empty()||c.paused||c.buffer>0)return true;
        break;
    case SpeechQueueMode::Sometimes:
        if(!c.always.empty())return true;
        if(!c.tracked.empty()||c.paused){c.sometimes=r;return true;}
        break;
    case SpeechQueueMode::Always:
        if(!c.tracked.empty()||c.paused){if(c.always.size()>=4096)return fail("Speech queue bound exceeded",error);c.sometimes.reset();c.always.push_back(r);return true;}
        break;
    case SpeechQueueMode::Interrupt:
        if(c.paused)return true;c.sometimes.reset();c.always.clear();break;
    case SpeechQueueMode::Never:
        return fail("Direct nonqueued PlaySpeech must bypass SpeechComponent through native audio bridge",error);
    }
    if(!playNow(c,r,id,error))return fail(error,error);return true;
}
bool SpeechWorld::complete(const AudioCompletion& event,std::string& error){
    if(faulted_){error=fault_;return false;}
    for(auto& [owner,c]:components_){
        const auto channel=std::find_if(c.tracked.begin(),c.tracked.end(),[&](const Tracked& t){return t.id==event.channelId;});
        if(channel==c.tracked.end())continue;
        const auto ended=*channel;c.active=0;
        // Original callback removes the first matching valid SoundCue name.
        const auto same=std::find_if(c.tracked.begin(),c.tracked.end(),[&](const Tracked& t){return t.request.cue==event.name;});
        if(same!=c.tracked.end())c.tracked.erase(same);
        presentation_.push_back({false,event.channelId,ended.request});
        if(!playNext(c,error))return fail(error,error);
        if(owner==-1)c.buffer=.25f;
        return true;
    }
    return true;
}
bool SpeechWorld::advance(float elapsed,std::string& error){
    if(faulted_){error=fault_;return false;}if(!std::isfinite(elapsed)||elapsed<0)return fail("Invalid speech clock",error);
    for(auto& [owner,c]:components_){(void)owner;c.buffer-=elapsed;}return true;
}
bool SpeechWorld::pause(int32_t owner,bool pitchTransition,std::string& error){
    constexpr float sourceScale=0,sourceEntryDuration=0;
    if(pitchTransition&&sourceEntryDuration>0&&sourceScale!=0)return fail("Nonzero speech pitch transition requires native pitch envelope",error);
    if(faulted_){error=fault_;return false;}
    auto& c=components_[owner];if(c.paused)return true;c.paused=true;
    if(c.active&&!audio_.pause(c.active,error))return fail(error,error);return true;
}
bool SpeechWorld::resume(int32_t owner,bool pitchTransition,std::string& error){
    constexpr float sourceScale=0,sourceExitDuration=0;
    if(pitchTransition&&sourceExitDuration>0&&sourceScale!=0)return fail("Nonzero speech pitch transition requires native pitch envelope",error);
    if(faulted_){error=fault_;return false;}
    auto& c=components_[owner];if(!c.paused)return true;c.paused=false;
    if(c.active){if(!audio_.resume(c.active,error))return fail(error,error);}
    else if(!playNext(c,error))return fail(error,error);return true;
}
bool SpeechWorld::stop(int32_t owner,float duration,std::string& error){
    if(faulted_){error=fault_;return false;}if(!std::isfinite(duration))return fail("Nonfinite speech stop duration",error);
    const auto i=components_.find(owner);if(i==components_.end()||!i->second.active)return true;
    const auto id=i->second.active;AudioChannelState state;if(!audio_.channelState(id,state))return true;
    const bool ok=duration>0&&state.volume>0?audio_.setVolume(id,0,duration,true,false,error):audio_.stop(id,false,error);
    return ok?true:fail(error,error);
}
std::vector<SpeechPresentation> SpeechWorld::drainPresentation(){std::vector<SpeechPresentation> out;out.swap(presentation_);return out;}
}
