#include "audio_limiter.hpp"
#include <algorithm>
#include <cmath>
namespace hades {
namespace {float gain(float db){return db>-80.0f?std::pow(10.0f,db*0.05f):0.0f;}}
bool AudioLimiter::configure(float rate,const AudioLimiterData& data,std::string& error){
    error.clear();
    if(!std::isfinite(rate)||rate<4000||rate>192000||!std::isfinite(data.releaseMs)||data.releaseMs<0||!std::isfinite(data.ceilingDb)||!std::isfinite(data.maximizerGainDb)){error="Invalid original limiter parameters";return false;}
    const float ceiling=gain(data.ceilingDb),maxGain=gain(data.maximizerGainDb),seconds=data.releaseMs*0.001f;
    const float release=seconds/((1.0f/rate)+seconds);
    if(!std::isfinite(ceiling)||!std::isfinite(maxGain)||!std::isfinite(release)){error="Limiter parameters overflow";return false;}
    data_=data;rate_=rate;ceiling_=ceiling;maxGain_=maxGain;release_=release;reset();return true;
}
void AudioLimiter::reset(){envelopes_.fill(1.0f);}
bool AudioLimiter::process(float* samples,size_t frames,size_t channels,std::string& error){
    error.clear();
    if(!samples||channels==0||channels>envelopes_.size()||frames>1048576||rate_==0){error="Invalid bounded limiter processing request";return false;}
    for(size_t i=0;i<frames*channels;++i)if(!std::isfinite(samples[i])){error="Limiter received nonfinite decoded PCM";return false;}
    for(size_t frame=0;frame<frames;++frame){
        if(data_.linked)envelopes_[0]*=release_;
        for(size_t channel=0;channel<channels;++channel){
            const size_t index=data_.linked?0:channel;
            float& envelope=envelopes_[index];
            if(!data_.linked)envelope*=release_;
            const float input=samples[frame*channels+channel];
            if(envelope<std::fabs(input))envelope=std::fabs(input);
            const float level=ceiling_/envelope;
            // Native linked processing updates/output each channel in order;
            // it does not pre-scan a simultaneous maximum across the frame.
            samples[frame*channels+channel]=(maxGain_<=level?maxGain_:level)*input;
        }
    }
    return true;
}
}
