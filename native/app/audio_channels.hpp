#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace hades {
// Implemented by the actual event renderer. A channel cannot be admitted with
// an absent renderer. Absent parameters reproduce FMOD's returned missing-name
// status, which ScriptAction.SetSoundCueValue ignores.
enum class AudioParameterAccess { Present,Absent,Failed };
class AudioEventInstance {
public:
    virtual ~AudioEventInstance()=default;
    virtual bool start(std::string&)=0;
    virtual bool setPaused(bool,std::string&)=0;
    virtual bool stop(bool immediate,std::string&)=0;
    virtual AudioParameterAccess parameter(const std::string&,float&,std::string&)=0;
    virtual AudioParameterAccess setParameter(const std::string&,float,std::string&)=0;
    virtual bool setVolume(float,std::string&)=0;
};
struct AudioChannelSnapshot {
    uint32_t id=0;
    std::string event;
    bool started=false,paused=false;
    size_t parameterTargets=0;
};
// Native AudioManager ID/start/pause/parameter-control subset. Parent queues
// actual Play(id) on the recovered World delayed-event clock. Admission here
// creates the renderer and ID immediately; it never pretends the sound started.
// Call under the parent's AudioWorld lock; this class adds no thread scheduling.
class AudioChannels {
    struct Target {float value=0,speed=0;};
    struct Channel {
        std::string name;
        std::unique_ptr<AudioEventInstance> renderer;
        bool started=false,paused=false;
        std::map<std::string,Target> targets;
    };
    std::map<uint32_t,Channel> channels_;
    uint32_t nextId_=1;
    bool faulted_=false;
    std::string fault_;
    bool fault(const std::string&,std::string&);
public:
    bool admit(const std::string& event,std::unique_ptr<AudioEventInstance>,uint32_t& id,std::string& error);
    bool play(uint32_t id,std::string& error);
    bool pause(uint32_t id,std::string& error);
    bool resume(uint32_t id,std::string& error);
    bool stop(uint32_t id,bool immediate,std::string& error);
    // Actual event renderer has reached stopped/finished playback state; caller
    // invokes during AudioManager.Update cleanup, never while rendering it.
    void retireCompleted(uint32_t id);
    bool setParameter(uint32_t id,const std::string&,float value,float duration,std::string& error);
    bool setVolumeImmediate(uint32_t id,float value,std::string& error);
    // Parameter targets progress by elapsed/duration, not the remaining value
    // difference/duration. Parent supplies AudioManager::Update's actual clock.
    bool advanceParameterTargets(float elapsed,std::string& error);
    std::vector<AudioChannelSnapshot> snapshot()const;
    bool faulted()const{return faulted_;}
    const std::string& error()const{return fault_;}
};
}
