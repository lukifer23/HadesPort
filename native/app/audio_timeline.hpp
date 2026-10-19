#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace hades {
constexpr uint32_t AudioTimelineRate=48000;
struct AudioParameter { std::string name; float minimum=0,maximum=1,initial=0; };
struct AudioMarker { uint32_t tick=0,length=0;std::string name; };
struct AudioTransition {
    uint32_t tick=0,destination=0;
    bool conditional=false;
    std::string parameter;
    float minimum=0,maximum=0;
};
struct AudioInstrumentRegion { std::string instrument;uint32_t start=0,end=0; };
struct AudioTimelineDefinition {
    uint32_t end=0;
    std::vector<AudioParameter> parameters;
    std::vector<AudioMarker> markers;
    std::vector<AudioTransition> transitions;
    std::vector<AudioInstrumentRegion> regions;
};
struct AudioTimelineEvent {
    enum class Kind { Marker,Jump,Finished } kind=Kind::Marker;
    std::string name;
    uint32_t tick=0,destination=0;
};
// Exact selected timeline metadata, advanced in fixed original 48k time units.
// No guessed ordering when competing eligible transitions disagree: fault.
class AudioTimeline {
    AudioTimelineDefinition definition_;
    std::map<std::string,float> parameters_;
    uint32_t tick_=0;
    bool started_=false,finished_=false,faulted_=false,includeCurrent_=true;
    mutable bool boundaryValid_=false;
    mutable uint32_t boundary_=0;
    std::string fault_;
    bool fault(const std::string&,std::string&);
public:
    bool load(const AudioTimelineDefinition&,std::string& error);
    bool start(std::vector<AudioTimelineEvent>&,std::string& error);
    bool advance(uint32_t ticks,std::vector<AudioTimelineEvent>&,std::string& error);
    bool seek(uint32_t tick,std::string& error);
    uint32_t nextBoundaryTick()const;
    // Missing parameter is a distinct result: native setParameterByName returns
    // an error that ScriptAction.SetSoundCueValue ignores. No parameter invented.
    enum class ParameterResult { Changed,Absent,Faulted };
    ParameterResult setParameter(const std::string&,float value,std::string& error);
    const std::map<std::string,float>& parameters()const{return parameters_;}
    const AudioTimelineDefinition& definition()const{return definition_;}
    uint32_t tick()const{return tick_;}
    bool started()const{return started_;}
    bool finished()const{return finished_;}
    bool faulted()const{return faulted_;}
    const std::string& error()const{return fault_;}
};
}
