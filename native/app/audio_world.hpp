#pragma once
#include "asset_store.hpp"
#include "source_value.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
namespace hades {
struct AudioSource {bool spatial=false;float x=0,y=0,z=0;int32_t ownerId=0;uint64_t ownerLifetime=0;};
struct AudioCompletion {uint32_t channelId=0;std::string name;bool voice=false;};
struct AudioMixingTargetState {std::string guid;uint32_t index=0;float value=0;};
struct AudioMusicMarker {uint32_t channelId=0;std::string name;int32_t positionMs=0;};
struct AudioChannelState {
    uint32_t id=0;std::string name;AudioSource source;
    bool started=false,paused=false,stopped=false,finished=false;
    float volume=1;
    std::vector<std::string> selectedWaveforms; // Actual playlist choices, empty before Play.
};
// Production original-compressed-asset event mixer. Source timing, parameters,
// routing and identities are catalog inputs. Android DSP/resampler adaptations
// are enumerated in the companion contract; unsupported data is a real error.
class AudioWorld {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit AudioWorld(AssetStore*);
    ~AudioWorld();
    AudioWorld(const AudioWorld&)=delete;
    AudioWorld& operator=(const AudioWorld&)=delete;
    bool loadCatalog(const SourceValue&,std::string& error);
    // Name validity from original complete strings.bank namespace, separately
    // from whether this bounded asset catalog exports its actual event graph.
    bool resolveCue(const std::string& name,bool& valid,std::string& error,bool applySwaps=false)const;
    size_t allocatedChannelCount()const;
    bool voiceCue(const std::string& name,bool& voice,std::string& error)const;
    bool subtitleText(const std::string& name,std::string& text,std::string& error)const;
    bool initializeMixingControl(std::string& error);
    bool setMixingParameter(const std::string&,float,std::string& error);
    uint32_t mixingControlId()const;
    std::vector<AudioMixingTargetState> mixingTargets()const;
    bool swapCue(const std::string& from,const std::string& destination,std::string& error);
    bool createCue(const std::string& name,const AudioSource&,uint32_t& id,std::string& error,bool applySwaps=false);
    std::vector<AudioChannelState> ownedChannels(int32_t owner,uint64_t lifetime)const;
    std::vector<AudioChannelState> attachedChannels()const;
    bool detachOwner(int32_t owner,uint64_t lifetime,float finalX,float finalY,std::string& error);
    bool play(uint32_t id,std::string& error);
    bool pause(uint32_t id,std::string& error);
    bool resume(uint32_t id,std::string& error);
    bool stop(uint32_t id,bool immediate,std::string& error);
    bool stopAllGameSounds(std::string& error);
    bool setParameter(uint32_t id,const std::string& name,float value,float duration,std::string& error);
    bool setVolume(uint32_t id,float value,float duration,bool stopOnEnd,bool pauseOnEnd,std::string& error);
    bool advanceControls(float elapsed,std::string& error);
    bool setListener(float x,float y,float z,std::string& error);
    bool setCategoryVolumes(float master,float music,float fx,float dialogue,std::string& error);
    bool setSource(uint32_t id,const AudioSource&,std::string& error);
    // Fixed48k stereo; caller connects to actual AAudio producer. No output is
    // played by construction, catalog loading or host decoder verification.
    bool render(float* stereo,uint32_t frames,std::string& error);
    std::vector<AudioMusicMarker> drainMarkers();
    std::vector<AudioCompletion> drainCompletions();
    std::vector<uint32_t> channelIds(const std::string& exactCueName)const;
    bool channelState(uint32_t id,AudioChannelState&)const;
    size_t liveEventCount(const std::string& exactCueName)const;
    std::string dump()const;
};
}
