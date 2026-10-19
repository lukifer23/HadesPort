#include "../app/audio_timeline.hpp"
#include <cassert>
using namespace hades;
int main(){
    std::string error;AudioTimeline clock;std::vector<AudioTimelineEvent> events;
    // Exact source ambience loop region: source starts0, loops55s back3s.
    AudioTimelineDefinition ambient;ambient.end=2998194;ambient.transitions={{2640000,144000,false,{},0,0}};
    assert(clock.load(ambient,error));assert(clock.start(events,error));assert(events.empty());
    assert(clock.advance(2640000,events,error));assert(clock.tick()==144000);assert(events.size()==1&&events[0].kind==AudioTimelineEvent::Kind::Jump);
    assert(clock.advance(2496001,events,error));assert(clock.tick()==144001);
    AudioTimelineDefinition music;music.end=42545454;music.parameters={{"Section",0,10,1},{"Keys",0,1,0}};music.markers={{0,0,"Restart"},{196364,0,"Start"},{19898089,0,"Section2"}};
    music.transitions={{19898182,196364,true,"Section",0,1},{0,19898089,true,"Section",2,3}};
    events.clear();assert(clock.load(music,error));assert(clock.start(events,error));assert(events.size()==1);
    for(int i=0;i<5;++i)assert(clock.advance(0,events,error));assert(events.size()==1); // No duplicate marker on paused frames.
    assert(clock.advance(19898182,events,error));assert(clock.tick()==196364);
    assert(clock.setParameter("Keys",2,error)==AudioTimeline::ParameterResult::Changed);assert(clock.parameters().at("Keys")==1);
    assert(clock.setParameter("Guitar",1,error)==AudioTimeline::ParameterResult::Absent);
    events.clear();assert(clock.load(music,error));assert(clock.setParameter("Section",3,error)==AudioTimeline::ParameterResult::Changed);assert(clock.start(events,error));assert(clock.tick()==19898089); // Inclusive range upper bound.
    AudioTimelineDefinition ambiguous;ambiguous.end=100;ambiguous.transitions={{5,10,false,{},0,0},{5,20,false,{},0,0}};
    events.clear();assert(clock.load(ambiguous,error));assert(clock.start(events,error));assert(!clock.advance(5,events,error));assert(clock.faulted());
    AudioTimelineDefinition cycle;cycle.end=100;cycle.transitions={{0,0,false,{},0,0}};assert(clock.load(cycle,error));assert(!clock.start(events,error));assert(clock.faulted());
}
