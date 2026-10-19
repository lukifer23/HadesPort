#pragma once
#include <array>
#include <cstddef>
#include <string>
namespace hades {
struct AudioThreeEqData {
    float lowGainDb=0,midGainDb=0,highGainDb=0;
    float lowCrossoverHz=0,highCrossoverHz=0;
    int slope=-1;
};
// Static-parameter 24dB/oct source subset: actual FMOD Core descriptor binds
// 18008c160 -> 18008e1d0, coefficient functions18008ea90/18008e7d0,
// slope1 processor18008f090. Runtime coefficient/gain ramps remain unsupported.
// Source must provide all six fields; no compiled defaults are asserted here.
class AudioThreeEq {
    struct Filter {
        std::array<float,5> c{}; // b0,b1,b2,a1,a2
        float w1=0,w2=0;
        float apply(float x);
    };
    struct Channel {std::array<Filter,2> low,high,midHigh,midLow;};
    std::array<Channel,32> channels_{};
    std::array<float,3> gains_{};
    bool configured_=false;
public:
    bool configure(float sampleRate,const AudioThreeEqData&,std::string& error);
    void reset();
    bool process(float* interleaved,size_t frames,size_t channels,std::string& error);
};
}
