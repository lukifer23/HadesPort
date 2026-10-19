#include "audio_channels.hpp"
#include <cmath>
#include <limits>
namespace hades {
bool AudioChannels::fault(const std::string& detail,std::string& error){faulted_=true;fault_=detail.empty()?"Original audio operation failed in the reconstructed event renderer":detail;error=fault_;return false;}
bool AudioChannels::admit(const std::string& name,std::unique_ptr<AudioEventInstance> renderer,uint32_t& id,std::string& error){
    id=0;if(faulted_){error=fault_;return false;}
    if(name.empty()||!renderer){error="Audio admission requires a real prepared event instance and source event name";return false;}
    if(nextId_==std::numeric_limits<uint32_t>::max()){error="Audio channel ID allocation exceeds portable supported range";return false;}
    const uint32_t allocated=nextId_;
    Channel c;c.name=name;c.renderer=std::move(renderer);channels_.emplace(allocated,std::move(c));
    ++nextId_;id=allocated;error.clear();return true;
}
bool AudioChannels::play(uint32_t id,std::string& error){
    if(faulted_){error=fault_;return false;}auto i=channels_.find(id);if(i==channels_.end()){error.clear();return true;}
    if(!i->second.renderer->start(error))return fault(error,error);
    i->second.started=true;error.clear();return true;
}
bool AudioChannels::pause(uint32_t id,std::string& error){
    if(faulted_){error=fault_;return false;}auto i=channels_.find(id);if(i==channels_.end()){error.clear();return true;}
    if(!i->second.renderer->setPaused(true,error))return fault(error,error);
    i->second.paused=true;error.clear();return true;
}
bool AudioChannels::resume(uint32_t id,std::string& error){
    if(faulted_){error=fault_;return false;}auto i=channels_.find(id);if(i==channels_.end()){error.clear();return true;}
    // Actual ResumeCue starts an unstarted channel; it does not call setPaused
    // false on that branch, even if PauseCue was called before first play.
    if(!i->second.started)return play(id,error);
    if(!i->second.renderer->setPaused(false,error))return fault(error,error);
    i->second.paused=false;error.clear();return true;
}
bool AudioChannels::stop(uint32_t id,bool immediate,std::string& error){
    if(faulted_){error=fault_;return false;}auto i=channels_.find(id);if(i==channels_.end()){error.clear();return true;}
    if(!i->second.renderer->stop(immediate,error))return fault(error,error);
    // Native cleanup/removal is a separate update phase. Do not recycle IDs or
    // infer stopped=>destroyed until actual event lifetime tells the parent.
    error.clear();return true;
}
void AudioChannels::retireCompleted(uint32_t id){channels_.erase(id);}
bool AudioChannels::setParameter(uint32_t id,const std::string& name,float value,float duration,std::string& error){
    if(faulted_){error=fault_;return false;}if(!std::isfinite(value)||!std::isfinite(duration)){error="Audio parameter controls require finite values and duration";return false;}
    auto i=channels_.find(id);if(i==channels_.end()){error.clear();return true;}
    if(duration>0){const float speed=1.0f/duration;if(!std::isfinite(speed)){error="Audio parameter target speed exceeds finite range";return false;}i->second.targets[name]={value,speed};error.clear();return true;}
    if(i->second.renderer->setParameter(name,value,error)==AudioParameterAccess::Failed)return fault(error,error);
    // The native immediate setter does not remove a previously queued target.
    error.clear();return true;
}
bool AudioChannels::setVolumeImmediate(uint32_t id,float value,std::string& error){
    if(faulted_){error=fault_;return false;}if(!std::isfinite(value)){error="Audio volume must be finite";return false;}
    auto i=channels_.find(id);if(i==channels_.end()){error.clear();return true;}
    if(!i->second.renderer->setVolume(value,error))return fault(error,error);
    error.clear();return true;
}
bool AudioChannels::advanceParameterTargets(float elapsed,std::string& error){
    if(faulted_){error=fault_;return false;}if(!std::isfinite(elapsed)||elapsed<0){error="Audio parameter targets require nonnegative finite elapsed time";return false;}
    // Native channel iteration order is not inferred from portable numeric IDs.
    // Each independent renderer has no script callback in this target phase.
    for(auto& [id,c]:channels_){(void)id;for(auto it=c.targets.begin();it!=c.targets.end();){
        float current=0;const auto read=c.renderer->parameter(it->first,current,error);
        if(read==AudioParameterAccess::Failed)return fault(error,error);
        if(read==AudioParameterAccess::Absent){error="Timed parameter target addresses a missing source parameter; native failure-output behavior is not recovered";return fault(error,error);}
        if(!std::isfinite(current))return fault("Audio renderer returned a nonfinite parameter",error);
        if(current==it->second.value){it=c.targets.erase(it);continue;}
        const float step=elapsed*it->second.speed;const float distance=std::fabs(it->second.value-current);
        if(!std::isfinite(step)||!std::isfinite(distance))return fault("Audio parameter approach exceeds finite range",error);
        const float next=step>=distance?it->second.value:current+(it->second.value>current?step:-step);
        if(c.renderer->setParameter(it->first,next,error)!=AudioParameterAccess::Present)return fault(error,error);
        ++it;
    }}
    error.clear();return true;
}
std::vector<AudioChannelSnapshot> AudioChannels::snapshot()const{std::vector<AudioChannelSnapshot> out;for(const auto& [id,c]:channels_)out.push_back({id,c.name,c.started,c.paused,c.targets.size()});return out;}
}
