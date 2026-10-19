#pragma once
#include "asset_store.hpp"
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hades {
enum ActorAnimationFlag : uint32_t {
    ActorLoop=1,ActorHoldLastFrame=2,ActorAllowRestart=4,ActorCancelOnMove=8,
    ActorDisableWeapons=16,ActorDisableRotation=32,ActorDisableInput=64,
    ActorDisableManualInteract=128,ActorNoCollision=256,ActorBook=512
};
struct ActorAnimationSlide {
    uint32_t originalFrame=0,ticks=0;
    std::vector<uint32_t> angleFrames;
    std::string sourceEventsJson; // Data only: owner must route real FX/audio.
};
struct ActorAnimationClip {
    std::string name,chain,moveAnimation;
    uint32_t originalAngles=0,flags=0;
    float worldScale=1,offsetX=0,offsetY=0,secondsPerTick=1.f/60;
    std::vector<uint32_t> originalAngleIndices;
    std::vector<ActorAnimationSlide> slides;
};
class ActorAnimationBank;
struct ActorAnimationImage {
    uint32_t canvasWidth=0,canvasHeight=0,cropX=0,cropY=0,width=0,height=0;
    uint32_t packetFrameIndex=0,originalMovieFrame=0,originalAngle=0;
    float originX=0,originY=0;
    std::span<const uint8_t> rgba;
    // Bank frames decode lazily: GPU-resident frames never re-inflate. The
    // returned span is valid until the decoder's next decode/reset/load.
    ActorAnimationBank* decoder=nullptr;
    std::span<const uint8_t> pixels()const;
    size_t byteCount()const{return size_t(width)*height*4;}
    // Crop top-left relative to original actor ground anchor (before actor Z).
    std::array<float,4> worldRect(const ActorAnimationClip&)const;
};
struct ActorAnimationCacheStats {
    uint64_t hits=0,misses=0,prefetches=0,inflateBytes=0,compressedBytes=0;
    size_t cachedBytes=0,cachedFrames=0;
    double inflateMilliseconds=0;
};
class ActorAnimationBank {
    struct Frame {
        ActorAnimationImage image;
        uint32_t rawBytes=0,packedBytes=0,crc=0;
        size_t offset=0;
    };
    struct Cached {uint32_t frame=0;uint64_t use=0;std::vector<uint8_t> rgba;};
    std::unique_ptr<AssetFile> file_;
    std::vector<Frame> frames_;
    std::vector<ActorAnimationClip> clips_;
    struct Facing {bool isometric=false,mirror=false,ignoreOwner=false;float ownAngle=0,angleOffset=0;int32_t angleNumOffset=0;};
    std::vector<Facing> facing_;
    std::vector<float> inverseAngleTable_;
    struct Properties {uint32_t known=0,values=0,locationFromOwner=0,scaleVectorFromOwner=0;float airborneZ=0;std::vector<uint8_t> airborne;};
    std::vector<Properties> properties_;
    std::vector<Cached> cache_;
    std::vector<uint8_t> compressed_;
    ActorAnimationCacheStats stats_;
    uint64_t use_=0;
    const Cached& cached(uint32_t frame,bool prefetch);
public:
    // A full original 1920x1080 vignette is 8,294,400 RGBA bytes. Keep it
    // lossless in the same bounded streamed bank; hero frames remain cropped.
    static constexpr size_t CacheBytes=16*1024*1024,CacheFrames=8,MaxFrameBytes=16*1024*1024;
    // Canonical bank auto-loads its canonical HAF1/hash. Alternate image banks
    // never borrow hero companions; custom facing requires both explicit names.
    void load(AssetStore&,const char* filename="actor-animations.bin",
        const char* facingFilename=nullptr,const char* identityFilename=nullptr);
    void reset();
    // Optional HAP1 companion; absence stays unknown, never a healthy default.
    void loadProperties(AssetStore&,const char* filename="actor-properties.bin",
        const char* identityFilename="actor-animations-sha256.bin");
    std::optional<bool> exactOwnerImmobile(size_t clip)const;
    std::optional<bool> exactBlockNewOwnerAnims(size_t clip)const;
    std::optional<bool> exactDisableOwnerInteract(size_t clip)const;
    std::optional<bool> exactDisableOwnerManualInteract(size_t clip)const;
    std::optional<bool> exactOwnerInvulnerable(size_t clip)const;
    std::optional<bool> exactOwnerNoArmorCoverage(size_t clip)const;
    std::optional<bool> exactOwnerNoInvulnerableCoverage(size_t clip)const;
    std::optional<bool> exactUseAttachedFlasher(size_t clip)const;
    std::optional<bool> exactUseAttachedShake(size_t clip)const;
    std::optional<uint32_t> exactLocationFromOwner(size_t clip)const;
    std::optional<uint32_t> exactScaleVectorFromOwner(size_t clip)const;
    std::optional<bool> exactAirborne(size_t clip,size_t slide)const;
    // Native AnimationDataDef.mAirborneZ; only contributes to Unit.GetTotalZ
    // when the actual primary is Slide and its current SlideData.Airborne.
    std::optional<float> exactAnimationZ(size_t clip)const;
    const std::vector<ActorAnimationClip>& clips()const{return clips_;}
    std::optional<size_t> find(std::string_view name)const;
    // Exact native full-angle quantization then nearest exported source angle.
    // Requires hash-bound actor-facing.bin + actor-animations-sha256.bin.
    uint32_t selectedAngleForFacing(size_t clip,float ownerRadians)const;
    // Full original angle, before the visible bank's selected-angle reduction.
    uint32_t originalAngleForFacing(size_t clip,float ownerRadians)const;
    // Result span is valid until the next bank sample/reset/load. Upload it
    // synchronously; do not retain pointers in render jobs. Prefetch happens
    // before the returned current frame is pinned for this call.
    ActorAnimationImage sample(size_t clip,size_t slide,uint32_t selectedAngle,bool prefetchNext=true);
    const ActorAnimationCacheStats& stats()const{return stats_;}
    std::span<const uint8_t> decode(uint32_t packetFrame){return cached(packetFrame,false).rgba;}
};
class ActorAnimationPlayback {
    const ActorAnimationBank* bank_=nullptr;
    size_t clip_=0,slide_=0;
    float remaining_=0,playSpeedMultiplier_=1;
    bool active_=false,finished_=false;
public:
    using SlideEvent=std::function<void(const ActorAnimationClip&,const ActorAnimationSlide&)>;
    void play(const ActorAnimationBank&,std::string_view name,const SlideEvent& onSlide={});
    // Base restart clears completion/cancel state even if concrete restart is
    // disabled. Only AllowRestart rewinds the source frame/remaining time.
    void restartSource(const SlideEvent& onSlide={});
    void replaceCurrentSlideTicks(uint32_t ticks);
    void reset();
    // Caller supplies original modified-time and speed multiplier. Returns a
    // source ChainTo request at completion; it never invents or executes owner
    // state/FX/physics. Loop is handled here, external chains by the owner.
    std::optional<std::string> advance(double seconds,float playSpeedMultiplier=1,const SlideEvent& onSlide={});
    bool active()const{return active_;}
    bool finished()const{return finished_;}
    void setPlaySpeedMultiplier(float multiplier);
    float playSpeedMultiplier()const{return playSpeedMultiplier_;}
    size_t clipIndex()const{return clip_;}
    size_t slideIndex()const{return slide_;}
    uint32_t originalFrame()const;
    std::optional<bool> animationBlocksAttack()const;
    const ActorAnimationClip* clip()const;
};
}
