#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace hades {
// Pulls genuine interleaved stereo float samples at 48kHz. Decoding/mixing runs
// on a producer thread; the real-time AAudio callback only drains the ring.
class AudioOutput {
public:
    using Render=std::function<bool(float*,uint32_t,std::string&)>;
    struct Stats {uint64_t framesSubmitted=0,underrunFrames=0;int deviceRate=0,framesPerBurst=0;bool running=false;};
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput&)=delete;
    AudioOutput& operator=(const AudioOutput&)=delete;
    bool start(Render,std::string& error);
    // Owner-thread maintenance only: reopen a disconnected Android stream once,
    // preserving the renderer/mixer. Other output failures remain explicit.
    bool service(std::string& error);
    void stop();
    Stats stats()const;
    std::string error()const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
