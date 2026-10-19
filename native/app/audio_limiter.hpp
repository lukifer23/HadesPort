#pragma once
#include <array>
#include <cstddef>
#include <string>
namespace hades {
struct AudioLimiterData {float releaseMs=0,ceilingDb=0,maximizerGainDb=0;bool linked=false;};
// FMOD Core shipped limiter read callback1800717c0 -> process180071d10.
// Source supplies all four parameters; this struct does not assert defaults.
class AudioLimiter {
    std::array<float,32> envelopes_{};
    AudioLimiterData data_;
    float rate_=0,ceiling_=0,maxGain_=0,release_=0;
public:
    bool configure(float sampleRate,const AudioLimiterData&,std::string& error);
    void reset(); // Native reset fills all32 envelope entries with1.0f.
    bool process(float* interleaved,size_t frames,size_t channels,std::string& error);
    const std::array<float,32>& envelopes()const{return envelopes_;}
};
}
