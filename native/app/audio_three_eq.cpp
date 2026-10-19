#include "audio_three_eq.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace hades {
float AudioThreeEq::Filter::apply(float x){
    const float w=(x-c[3]*w1)-c[4]*w2;
    const float y=(w*c[0]+w1*c[1])+w2*c[2];
    w2=w1;w1=w;return y;
}
bool AudioThreeEq::configure(float rate,const AudioThreeEqData& d,std::string& error){
    if(!std::isfinite(rate)||rate<=0||d.slope!=1||
        !std::isfinite(d.lowGainDb)||!std::isfinite(d.midGainDb)||!std::isfinite(d.highGainDb)||
        !std::isfinite(d.lowCrossoverHz)||!std::isfinite(d.highCrossoverHz)){
        error="Three-EQ requires finite static parameters and recovered slope1 (24dB/oct)";return false;
    }
    auto coefficients=[&](float hz,bool high){
        // Source clamps each crossover independently; no invented low<high gate.
        hz=std::min(std::min(std::max(hz,10.0f),22000.0f),rate*0.49f);
        const float omega=(hz*6.2831855f)/rate;
        const float cosine=std::cos(omega);
        const float alpha=std::sin(omega)*(0.5f/0.70710677f);
        const float inverse=1.0f/(alpha+1.0f);
        const float b1=high?-((cosine+1.0f)*inverse):(1.0f-cosine)*inverse;
        const float b0=b1*(high?-0.5f:0.5f);
        return std::array<float,5>{b0,b1,b0,cosine*-2.0f*inverse,(1.0f-alpha)*inverse};
    };
    const auto low=coefficients(d.lowCrossoverHz,false),midHigh=coefficients(d.lowCrossoverHz,true);
    const auto midLow=coefficients(d.highCrossoverHz,false),high=coefficients(d.highCrossoverHz,true);
    auto gain=[](float db){return db>-80.0f?std::pow(10.0f,db*0.05f):0.0f;};
    const std::array<float,3> gains{gain(d.lowGainDb),gain(d.midGainDb),gain(d.highGainDb)};
    for(float g:gains)if(!std::isfinite(g)){error="Three-EQ gain exceeds finite portable range";return false;}
    gains_=gains;
    for(auto& c:channels_)for(size_t i=0;i<2;++i){c.low[i].c=low;c.midHigh[i].c=midHigh;c.midLow[i].c=midLow;c.high[i].c=high;}
    configured_=true;reset();error.clear();return true;
}
void AudioThreeEq::reset(){for(auto& c:channels_)for(auto* bank:{&c.low,&c.high,&c.midHigh,&c.midLow})for(auto& f:*bank)f.w1=f.w2=0;}
bool AudioThreeEq::process(float* pcm,size_t frames,size_t count,std::string& error){
    if(!configured_||!count||count>channels_.size()||(!pcm&&frames)||frames>std::numeric_limits<size_t>::max()/count){error="Three-EQ requires configured state and bounded channel buffer";return false;}
    for(size_t i=0;i<frames*count;++i)if(!std::isfinite(pcm[i])){error="Three-EQ input is nonfinite";return false;}
    for(size_t i=0;i<frames;++i)for(size_t j=0;j<count;++j){auto& c=channels_[j];const float x=pcm[i*count+j];
        float low=x,mid=x,high=x;
        for(auto& f:c.low)low=f.apply(low);
        for(auto& f:c.midHigh)mid=f.apply(mid);
        for(auto& f:c.midLow)mid=f.apply(mid);
        for(auto& f:c.high)high=f.apply(high);
        const float y=(mid*gains_[1]+low*gains_[0])+high*gains_[2];
        if(!std::isfinite(y)){error="Three-EQ output/state exceeds finite portable range";configured_=false;return false;}
        pcm[i*count+j]=y;
    }
    error.clear();return true;
}
}
