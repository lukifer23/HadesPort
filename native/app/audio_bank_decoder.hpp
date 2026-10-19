#pragma once
#include "asset_store.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace hades {
struct AudioBankSample {
    std::string asset;
    int subsong = 1;
    int channels = 0;
    int sampleRate = 0;
    int64_t frames = 0;
    uint64_t encodedFileBytes = 0;
};
// Original FSB5/FEV Vorbis -> interleaved float32 at ORIGINAL rate/channels.
// A sample decoder only: event timing, loops, gains, spatial audio and callbacks
// are the event engine's responsibility. No output device or filesystem needed.
class AudioBankDecoder {
public:
    explicit AudioBankDecoder(AssetStore* assets);
    ~AudioBankDecoder();
    AudioBankDecoder(const AudioBankDecoder&) = delete;
    AudioBankDecoder& operator=(const AudioBankDecoder&) = delete;
    bool open(const AudioBankSample&, std::string& error);
    bool readFrames(float* output, size_t capacityFrames, size_t& decodedFrames, std::string& error);
    bool seek(int64_t frame, std::string& error);
    int64_t position() const;
    const AudioBankSample* sample() const;
    uint64_t sourceBytesRead() const;
    uint64_t deniedCompanionRequests() const;
    void close();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
