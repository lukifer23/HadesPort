#include "audio_channels.hpp"
#include <cassert>
#include <iostream>
using namespace hades;
struct Observer:AudioEventInstance {
    std::vector<std::string> calls;float value=0;bool fail=false;
    bool start(std::string& e)override{calls.push_back("start");if(fail){e="real renderer failure";return false;}return true;}
    bool setPaused(bool p,std::string&)override{calls.push_back(p?"pause":"unpause");return true;}
    bool stop(bool immediate,std::string&)override{calls.push_back(immediate?"stop-now":"stop-release");return true;}
    AudioParameterAccess parameter(const std::string& n,float& v,std::string&)override{if(n!="Keys")return AudioParameterAccess::Absent;v=value;return AudioParameterAccess::Present;}
    AudioParameterAccess setParameter(const std::string& n,float v,std::string&)override{if(n!="Keys")return AudioParameterAccess::Absent;value=v;return AudioParameterAccess::Present;}
    bool setVolume(float,std::string&)override{return true;}
};
int main(){
    std::string error;AudioChannels channels;uint32_t id=999;assert(!channels.admit("Music",{},id,error)&&id==0);
    auto renderer=std::make_unique<Observer>();auto* observe=renderer.get();assert(channels.admit("/Music/MusicExploration4_MC",std::move(renderer),id,error)&&id==1);
    assert(!channels.snapshot()[0].started&&observe->calls.empty()); // delayed play has not happened
    assert(channels.setParameter(id,"Guitar",1,0,error)); // actual missing source name, native immediate ignores status
    assert(channels.pause(id,error));assert(channels.resume(id,error));assert((observe->calls==std::vector<std::string>{"pause","start"}));assert(channels.snapshot()[0].paused);
    assert(channels.resume(id,error));assert(observe->calls.back()=="unpause");
    assert(channels.setParameter(id,"Keys",0.8f,2,error));assert(channels.advanceParameterTargets(0.5f,error));assert(observe->value==0.25f); // speed1/duration, not delta/duration
    assert(channels.setParameter(id,"Keys",0.5f,0,error));assert(channels.snapshot()[0].parameterTargets==1);assert(channels.advanceParameterTargets(1,error));assert(observe->value==0.8f);assert(channels.snapshot()[0].parameterTargets==1);assert(channels.advanceParameterTargets(0,error));assert(channels.snapshot()[0].parameterTargets==0);
    assert(channels.pause(0,error)&&channels.resume(1000,error));uint32_t second=0;assert(channels.admit("Other",std::make_unique<Observer>(),second,error)&&second==2);
    observe->fail=true;assert(!channels.play(id,error)&&channels.faulted());assert(!channels.resume(second,error));
    std::cout<<"Source audio channel identity, delayed start, resume and parameter-target contracts passed; synthetic renderer observers only\n";
}
