#include "audio_three_eq.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using namespace hades;
static double level(float frequency,float midDb){
    AudioThreeEq eq;std::string error;assert(eq.configure(48000,{0,midDb,0,400,4000,1},error));
    std::vector<float> pcm(48000);for(size_t i=0;i<pcm.size();++i)pcm[i]=std::sin(6.283185307179586*frequency*i/48000);
    assert(eq.process(pcm.data(),pcm.size(),1,error));double sum=0;for(size_t i=24000;i<pcm.size();++i)sum+=pcm[i]*pcm[i];return std::sqrt(sum/24000);
}
int main(){
    // Selected MapZoomIn source: low/high0dB, mid-25.5dB,400/4000Hz,slope1.
    assert(level(1000,-25.5f)<level(1000,0)*0.1);
    assert(level(40,-25.5f)>level(40,0)*0.99);
    assert(level(16000,-25.5f)>level(16000,0)*0.99);
    std::string error;AudioThreeEq whole,split;assert(whole.configure(44100,{0,-25.5f,0,400,4000,1},error));assert(split.configure(44100,{0,-25.5f,0,400,4000,1},error));
    std::vector<float> a(4000);a[0]=1;auto b=a;assert(whole.process(a.data(),2000,2,error));assert(split.process(b.data(),127,2,error));assert(split.process(b.data()+254,1873,2,error));assert(a==b);for(size_t i=1;i<a.size();i+=2)assert(a[i]==0);
    whole.reset();auto reset=std::vector<float>(4000);reset[0]=1;assert(whole.process(reset.data(),2000,2,error));assert(reset==a);
    assert(!whole.configure(44100,{0,0,0,400,4000,2},error));
    std::cout<<"Source 24dB three-EQ subset: band rejection, channel independence, reset and chunk invariance passed\n";
}
