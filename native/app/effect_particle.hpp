#pragma once
#include <functional>
#include "effect_animation.hpp"
#include "actor_animation.hpp"
#include <optional>

namespace hades {
// Must call the real bounded PCG stream, including bound1 consumption. Uniform
// float rounding is not a substitute for pcg32_boundedrand_r rejection sampling.
using EffectBoundedRandom=std::function<bool(uint32_t,uint32_t&,std::string&)>;
struct EffectParticleAudio {
    std::function<bool(const std::string&,int32_t,const std::array<float,3>&,uint32_t&,std::string&)> start;
    std::function<bool(uint32_t,float,std::string&)> stop;
};
struct EffectParticleAdmission {
    bool nativeManagerAdmissionResolved=false;
    // Selected bounded manager path: every CancelOverCap animation registered
    // in this actual manager is owned here. A mixed registry needs its real
    // ordered-manager adapter instead of guessing the number of external FX.
    bool completePermanentRegistry=false;
    std::optional<uint32_t> permanentVfxCap;
    std::optional<float> sourceModifiedTime,sourceTimeModifierFraction;
    // Actual attached Thing+0x7c, not a global clock assumption.
    std::optional<float> ownerTimeModifierFraction;
    EffectParticleAudio audio;
    EffectAnimationWorld* componentWorld=nullptr; // Must outlive live particles.
};
struct EffectParticleState {
    uint64_t handle=0,source=0,ownerLifetime=0;
    int32_t ownerId=0;
    std::string name;
    SourceValue sourceDefinition;
    std::shared_ptr<ActorAnimationBank> imageBank;
    size_t imageClip=0;
    float x=0,y=0,baseZ=0,offsetZ=0,heading=0,ownScale=1,ownerScale=1;
    float speed=0,acceleration=0,upwardVelocity=0,gravity=0,rotation=0,rotationSpeed=0,headingSpeed=0,headingAcceleration=0;
    float frame=1,playSpeed=0,slideRemaining=0,animationFraction=0;
    float startScale=1,endScale=1,offsetY=0,startOffsetY=0,endOffsetY=0,easeIn=0,easeOut=0;
    bool book=false,done=false,slidesFinished=false;
    bool constant=false,sourceColorResolved=false;
    float duration=0,durationRemaining=0;
    std::array<uint8_t,4> baseColor{255,255,255,255};
    std::array<float,4> baseHsv{};
    bool durationBound=false,loopFramesOnly=false;
    bool inheritOwnerScale=true;
    bool pingPongScale=false;
    EffectAnimationWorld* componentWorld=nullptr;
    uint32_t sound=0;
    std::function<bool(uint32_t,float,std::string&)> stopSound;
    std::optional<float> modifiedTime,timeModifierFraction;
    std::vector<uint32_t> slideTicks;
};
enum class EffectParticlePhase {Rotation,Acceleration,Gravity,Fraction,Heading,UpwardVelocity,PlaySpeed,Translation,BookUpdateInternal,SlideUpdateInternal,SlidePostUpdate,TimeRemaining,ConstantUpdateInternal};
class EffectParticleWorld {
    std::function<bool(uint64_t)> externalOwner_;
    std::map<std::string,SourceValue> definitions_;
    std::shared_ptr<ActorAnimationBank> bank_;
    std::vector<std::shared_ptr<ActorAnimationBank>> extraBanks_;
    std::shared_ptr<AnimationRegistry> registry_;
    std::map<uint64_t,EffectParticleState> particles_;
    uint64_t nextHandle_=1;
    bool mutating_=false,faulted_=false;
    std::string fault_;
    bool preflight(const std::string&,std::vector<std::string>&,std::string&)const;
    std::shared_ptr<ActorAnimationBank> imageBankFor(const std::string&)const;
    bool createDust(const std::string&,const EffectSpawnRequest&,const EffectAnimationEntity&,
        const EffectOwnerState&,const EffectParticleAdmission&,const EffectAnimationRandom&,uint64_t&,std::string&);
    bool createBat(const EffectSpawnRequest&,const EffectAnimationEntity&,const EffectOwnerState&,
        const EffectParticleAdmission&,const EffectAnimationRandom&,uint64_t&,std::string&);
    bool createEmber(const std::string&,const EffectSpawnRequest&,const EffectAnimationEntity&,
        const EffectOwnerState&,const EffectParticleAdmission&,const EffectAnimationRandom&,uint64_t&,std::string&);
public:
    static constexpr uint32_t CompiledPermanentVfxCap=80;
    // Uses the retained source Definitions from HadesEffectInitialization1 and
    // a separate original cinder HAA1 image bank. No bank bytes are modified.
    bool load(const SourceValue&,std::shared_ptr<ActorAnimationBank>,std::string&);
    bool addImageBank(std::shared_ptr<ActorAnimationBank>,std::string&);
    bool setRegistry(std::shared_ptr<AnimationRegistry>,std::string&);
    // Actual current ManagerAdd vector, including pending canceled entries.
    // Does not certify unimplemented caller paths outside this shared registry.
    bool permanentRegistryCovered(uint32_t manager=0)const;
    // Other worlds sharing this registry that mirror a CancelOverCap cancel
    // themselves (action FX dispose canceled lifetimes, e.g. dust/decals).
    void setExternalOwner(std::function<bool(uint64_t)> owns){externalOwner_=std::move(owns);}
    bool prepare(const EffectSpawnRequest&,std::string&)const;
    // Use this full overload in EffectEmissionProvider::prepare so missing
    // manager/time/owner/RNG admission is rejected before spawner mutation.
    bool prepare(const EffectSpawnRequest&,const EffectAnimationEntity&,
        const EffectOwnerState&,const EffectParticleAdmission&,const EffectAnimationRandom&,
        const EffectBoundedRandom&,std::string&)const;
    // Actual manager/visibility/owner-time admission must be resolved BEFORE
    // invoking create. This initializer owns no invented viewport or RNG seed.
    bool create(const EffectSpawnRequest&,const EffectAnimationEntity& actualSource,
        const EffectOwnerState&,const EffectParticleAdmission&,const EffectAnimationRandom&,const EffectBoundedRandom&,
        uint64_t&,std::string&);
    bool phase(EffectParticlePhase,float modifiedSeconds,const EffectBoundedRandom&,std::string&);
    bool refreshOwners(const EffectOwnerProvider&,std::string&);
    bool syncSpawners(std::string&); // After actual motion/fraction/time phases.
    bool alive(uint64_t handle)const;
    size_t liveCount()const;
    void disposeDone(); // Caller routes the actual manager-disposal phase.
    const std::map<uint64_t,EffectParticleState>& particles()const{return particles_;}
    const std::shared_ptr<ActorAnimationBank>& imageBank()const{return bank_;}
    // Original non-ping-pong, non-smoothstep interpolation branch used by these
    // selected definitions. Call after actual AnimFraction system phase.
    static float interpolate(float start,float end,float fraction,float easeIn,float easeOut);
    static float drawScale(const EffectParticleState&);
    static float drawOwnScale(const EffectParticleState&);
    static float drawOffsetY(const EffectParticleState&);
    static float drawAngle(const EffectParticleState& p){return p.book&&p.imageBank&&p.imageBank->clips().at(p.imageClip).originalAngles>1?0:p.rotation-p.heading;}
};
}
