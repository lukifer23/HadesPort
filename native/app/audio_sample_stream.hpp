#pragma once
#include "audio_bank_decoder.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace hades {
// Streaming reconstruction resampler: 32tap Hann-windowed sinc. This is an
// explicit output-quality choice, not a recovered FMOD interpolation kernel.
// Original source clocks (1000..48000 Hz) remain intact via exact rational
// phases; 48k uses direct source frames. No full PCM packaging.
class AudioSampleStream {
    AudioBankDecoder decoder_;
    AudioBankSample sample_;
    std::array<float,2048> cache_{};
    int64_t cacheStart_=-1;
    size_t cacheFrames_=0;
    const std::vector<std::array<double,32>>* kernels_=nullptr;
    uint64_t phaseDivisor_=0;
    bool value(int64_t frame,size_t channel,float&,std::string&);
public:
    explicit AudioSampleStream(AssetStore* assets):decoder_(assets){}
    bool open(const AudioBankSample&,std::string& error);
    // Original48k timeline integer -> rational source frame. Handles exact
    // backwards jumps through decoder.seek; phase is independent of chunk size.
    bool stereoAtTick(uint64_t sourceTick,float& left,float& right,std::string& error);
    uint64_t bytesRead()const{return decoder_.sourceBytesRead();}
};
}
