#include "audio_timeline.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
namespace hades {
bool AudioTimeline::fault(const std::string& value,std::string& error){faulted_=true;fault_=value;error=value;return false;}
bool AudioTimeline::load(const AudioTimelineDefinition& source,std::string& error){
    error.clear();
    if(source.end==0||source.parameters.size()>256||source.markers.size()>65536||source.transitions.size()>65536||source.regions.size()>4096)return fault("Invalid bounded audio timeline",error);
    std::map<std::string,float> parameters;
    for(const auto& p:source.parameters){
        if(p.name.empty()||!std::isfinite(p.minimum)||!std::isfinite(p.maximum)||!std::isfinite(p.initial)||p.minimum>p.maximum||p.initial<p.minimum||p.initial>p.maximum||!parameters.emplace(p.name,p.initial).second)return fault("Invalid original audio parameter",error);
    }
    for(const auto& marker:source.markers)if(marker.tick>source.end||uint64_t(marker.tick)+marker.length>source.end)return fault("Original audio marker exceeds timeline",error);
    for(const auto& region:source.regions)if(region.instrument.empty()||region.start>=region.end||region.end>source.end)return fault("Invalid original audio instrument region",error);
    for(const auto& jump:source.transitions){
        if(jump.tick>source.end||jump.destination>source.end)return fault("Original audio jump exceeds timeline",error);
        if(jump.conditional&&(!parameters.contains(jump.parameter)||!std::isfinite(jump.minimum)||!std::isfinite(jump.maximum)||jump.minimum>jump.maximum))return fault("Invalid original audio jump condition",error);
    }
    definition_=source;parameters_=std::move(parameters);tick_=0;started_=false;finished_=false;faulted_=false;fault_.clear();includeCurrent_=true;boundaryValid_=false;return true;
}
bool AudioTimeline::start(std::vector<AudioTimelineEvent>& events,std::string& error){
    error.clear();if(faulted_){error=fault_;return false;}if(started_)return true;
    if(definition_.end==0)return fault("Audio timeline was not loaded",error);
    started_=true;return advance(0,events,error);
}
bool AudioTimeline::advance(uint32_t elapsed,std::vector<AudioTimelineEvent>& events,std::string& error){
    error.clear();if(faulted_){error=fault_;return false;}if(!started_)return fault("Audio timeline advanced before start",error);if(finished_)return true;
    uint64_t remaining=elapsed;size_t operations=0;
    for(;;){
        if(++operations>131072)return fault("Audio timeline contains a zero-time transition cycle",error);
        const uint32_t next=nextBoundaryTick();
        uint64_t distance=uint64_t(next)-tick_;
        if(distance>remaining){if(remaining){tick_+=static_cast<uint32_t>(remaining);includeCurrent_=true;}return true;}
        tick_=next;remaining-=distance;includeCurrent_=false;boundaryValid_=false;
        // Named-marker notification is retained even when a jump occurs here.
        for(const auto& marker:definition_.markers)if(marker.tick==tick_&&!marker.name.empty())events.push_back({AudioTimelineEvent::Kind::Marker,marker.name,tick_,0});
        bool jumps=false;uint32_t destination=0;
        for(const auto& jump:definition_.transitions){
            if(jump.tick!=tick_)continue;
            if(jump.conditional){const float v=parameters_.at(jump.parameter);if(v<jump.minimum||v>jump.maximum)continue;}
            if(jumps&&destination!=jump.destination)return fault("Competing original audio transitions require native priority recovery",error);
            jumps=true;destination=jump.destination;
        }
        if(jumps){
            events.push_back({AudioTimelineEvent::Kind::Jump,{},tick_,destination});tick_=destination;includeCurrent_=true;
            // Evaluate destination markers/conditions even at a zero remainder.
            continue;
        }
        if(tick_==definition_.end){finished_=true;events.push_back({AudioTimelineEvent::Kind::Finished,{},tick_,0});return true;}
        if(remaining==0)return true;
    }
}
bool AudioTimeline::seek(uint32_t target,std::string& error){
    error.clear();if(faulted_){error=fault_;return false;}if(target>definition_.end)return fault("Audio seek exceeds original timeline",error);
    tick_=target;includeCurrent_=true;finished_=false;boundaryValid_=false;return true;
}
uint32_t AudioTimeline::nextBoundaryTick()const{
    if(boundaryValid_)return boundary_;
    uint32_t next=definition_.end;
    for(const auto& marker:definition_.markers)
        if((marker.tick>tick_||(includeCurrent_&&marker.tick==tick_))&&marker.tick<next)next=marker.tick;
    for(const auto& jump:definition_.transitions){
        if(jump.tick<tick_||(!includeCurrent_&&jump.tick==tick_))continue;
        if(jump.conditional){const float v=parameters_.at(jump.parameter);if(v<jump.minimum||v>jump.maximum)continue;}
        if(jump.tick<next)next=jump.tick;
    }
    boundary_=next;boundaryValid_=true;return next;
}
AudioTimeline::ParameterResult AudioTimeline::setParameter(const std::string& name,float value,std::string& error){
    error.clear();if(faulted_){error=fault_;return ParameterResult::Faulted;}if(!std::isfinite(value)){fault("Nonfinite audio parameter",error);return ParameterResult::Faulted;}
    auto it=parameters_.find(name);if(it==parameters_.end())return ParameterResult::Absent;
    const auto p=std::find_if(definition_.parameters.begin(),definition_.parameters.end(),[&](const AudioParameter& q){return q.name==name;});
    it->second=std::clamp(value,p->minimum,p->maximum);boundaryValid_=false;return ParameterResult::Changed;
}
}
