#pragma once
#include "audio_world.hpp"
#include <deque>
#include <functional>
#include <map>
#include <optional>
namespace hades {
enum class SpeechQueueMode { Sometimes=0, Never=1, Always=2, NeverQueue=3, Interrupt=4 };
struct SpeechRequest {
    std::string cue,actor,subtitleText;
    int32_t ownerId=-1; // -1 is native global PLAYER_SPEECH.
    SpeechQueueMode queue=SpeechQueueMode::NeverQueue;
    bool useSubtitles=true;
    SourceValue subtitleColor;
};
struct SpeechPresentation {
    bool begin=false;
    uint32_t channelId=0;
    SpeechRequest request;
};
// Source SpeechComponent queue ownership, backed by real AudioWorld channels.
// Call only from the game thread; source lookup runs anew when a queued cue starts.
class SpeechWorld {
    struct Tracked {uint32_t id=0;SpeechRequest request;};
    struct Component {
        bool paused=false;
        float buffer=0;
        uint32_t active=0;
        std::optional<SpeechRequest> sometimes;
        std::deque<SpeechRequest> always;
        std::vector<Tracked> tracked;
    };
    AudioWorld& audio_;
    std::map<int32_t,Component> components_;
    std::function<bool(int32_t,AudioSource&,std::string&)> source_;
    std::vector<SpeechPresentation> presentation_;
    std::function<void()> played_;
    std::function<bool(int32_t,SourceValue&,std::string&)> subtitleColor_;
    bool faulted_=false;std::string fault_;
    bool fail(const std::string&,std::string&);
    bool playNow(Component&,const SpeechRequest&,uint32_t&,std::string&);
    bool playNext(Component&,std::string&);
public:
    explicit SpeechWorld(AudioWorld& audio):audio_(audio){}
    void setSourceProvider(std::function<bool(int32_t,AudioSource&,std::string&)> p){source_=std::move(p);}
    void setSubtitleColorProvider(std::function<bool(int32_t,SourceValue&,std::string&)> p){subtitleColor_=std::move(p);}
    void setPlayObserver(std::function<void()> p){played_=std::move(p);}
    bool request(const SpeechRequest&,uint32_t& channelId,std::string& error);
    bool complete(const AudioCompletion&,std::string& error);
    bool advance(float elapsed,std::string& error);
    // Current original AudioData FocusSpeechPitchShift scale/entry/exit are0;
    // absent source fields preserve those static compiled values, so even the
    // action's default PitchTransition=true takes immediate pause/resume.
    bool pause(int32_t ownerId,bool pitchTransition,std::string& error);
    bool resume(int32_t ownerId,bool pitchTransition,std::string& error);
    bool stop(int32_t ownerId,float duration,std::string& error);
    std::vector<SpeechPresentation> drainPresentation();
};
}
