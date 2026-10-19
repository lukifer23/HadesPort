#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace hades {
struct AudioChorusData {float mixPercent=0,rateHz=0,depthPercent=0;};
// Current shipped fmod.dll Chorus descriptor1a4160, read55170->55470,
// reset55160->55c40. The adjacent game PDB does not match this DLL.
class AudioChorus {
    AudioChorusData data_;
    std::vector<int16_t> delay_;
    uint32_t frames_=0,channels_=0,write_=0;
    float sampleRate_=0,phase_=0,phaseStep_=0,depth_=0,targetDepth_=0,dry_=0;
    std::array<float,3> wet_{};
public:
    bool configure(float sampleRate,const AudioChorusData&,std::string& error);
    void reset();
    bool process(float* interleaved,size_t frames,size_t channels,std::string& error);
};
}
