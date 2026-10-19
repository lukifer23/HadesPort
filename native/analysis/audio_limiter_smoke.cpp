#include "../app/audio_limiter.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace hades;
int main(){
    AudioLimiter limiter;std::string error;assert(limiter.configure(48000,{10,0,0,true},error));
    assert(limiter.envelopes()[0]==1);
    float linked[]={0.5f,2.0f,0.5f,0.0f};assert(limiter.process(linked,2,2,error));
    assert(linked[0]==0.5f&&linked[1]==1.0f);assert(linked[2]>0.25f&&linked[2]<0.251f);
    assert(limiter.configure(48000,{10,0,0,false},error));float independent[]={0.5f,2.0f,0.5f,0.0f};assert(limiter.process(independent,2,2,error));assert(independent[2]==0.5f);
    assert(limiter.configure(48000,{10,-80,0,false},error));float muted[]={1,1};assert(limiter.process(muted,1,2,error));assert(muted[0]==0&&muted[1]==0);
    assert(!limiter.configure(0,{10,0,0,false},error));assert(limiter.configure(48000,{10,0,0,false},error));float invalid[]={std::numeric_limits<float>::quiet_NaN()};assert(!limiter.process(invalid,1,1,error));
    limiter.reset();assert(limiter.envelopes()[0]==1);
}
