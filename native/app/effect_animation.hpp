#pragma once
#include "source_value.hpp"
#include "animation_registry.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hades {
struct EffectOwnerState {
    uint64_t lifetime=0;
    float x=0,y=0,z=0,scale=1,angle=0;
    bool hasPrimaryAnimation=false,isUnit=false;
    // Actual BodyComponent packed base color/HSV, before fraction controls.
    // SetValuesFromOwner reads these fields rather than final shader color.
    std::optional<std::array<uint8_t,4>> baseColor;
    std::optional<std::array<float,4>> baseHsv;
    // Portable snapshot of the owning Thing's draw group. Attached FX keep
    // this with position/color at preDisposeOwner, before the Thing retires.
    std::optional<std::string> group;
};
using EffectOwnerProvider=std::function<bool(int32_t,EffectOwnerState&,std::string&)>;
using EffectAnimationRandom=std::function<bool(double&,std::string&)>;
using EffectMarkerProvider=std::function<bool(int32_t,const std::string&,std::array<float,3>&,std::string&)>;
struct EffectTexture {
    std::string name,atlas;
    uint32_t width=0,height=0;
    std::array<uint8_t,4> rgba{};
    std::array<int32_t,4> atlasRect{};
    std::array<int32_t,2> trim{},originalSize{};
    // For non-blank primary Constant graphics, caller supplies CRC-validated
    // original frame bytes from the source-bound bank; never placeholder art.
    std::vector<uint8_t> pixels;
};
struct EffectDefinition {
    std::string name,type,graphic,group,child,vfx,marker;
    SourceValue source;
    float scale=1,offsetX=0,offsetY=0,offsetZ=0,alpha=1;
    float duration=0,durationMin=0,durationMax=0;
    std::string chain;
    float intervalMin=0,intervalMax=0,distanceMin=0,distanceMax=0,distanceMinChange=0;
    int32_t managerCap=0,activeVfxCap=0,totalVfxCap=0,frameCap=0;
    bool loop=false,onlyWhenVisible=false;
};
enum class EffectSpawnerKind { None,Time,Distance };
struct EffectAnimationEntity {
    uint64_t handle=0,parent=0,child=0,ownerLifetime=0;
    int32_t ownerId=0;
    const EffectDefinition* definition=nullptr;
    const EffectTexture* texture=nullptr;
    std::array<float,3> location{},previousVfxLocation{};
    float ownScale=1,ownerScale=1,angle=0;
    EffectSpawnerKind spawner=EffectSpawnerKind::None;
    float timeRemaining=0,distanceRemaining=0;
    float duration=0,durationRemaining=0;
    bool hasDuration=false;
    uint32_t totalSpawned=0;
    // registered means allocated ECS lifetime. Native AddChild/CreateCustom
    // does not append to AnimationManager.mAnimations; only the public root
    // Create wrapper does. Do not use entity count as manager vector length.
    bool registered=false,managerVectorMember=false,done=false,blockVfx=false,suspendVfx=false;
    bool particleComponent=false;
    std::vector<uint64_t> spawned;
};
struct EffectSpawnRequest {
    uint64_t source=0;int32_t ownerId=0;
    std::string animation;
    std::array<float,3> position{},segmentFrom{},segmentTo{};
    float nativeAngle=0,distanceRemaining=0,distanceDelta=0;
    bool distance=false;
};
enum class EffectUpdatePhase { Unresolved,Time,Distance };
struct EffectUpdateContext {
    EffectUpdatePhase phase=EffectUpdatePhase::Unresolved;
    // Counts are actual native-manager live VFX/frame counts, not sprite count.
    bool countsResolved=false;
    // Bat Constant source has a real Duration/TimeRemaining component. Its
    // original time-system phase must precede VFX; no hidden local timer.
    bool durationSystemResolved=false;
    uint32_t managerVisualFxCount=0,managerSpawnedThisFrame=0;
    int32_t frameCap=60;float managerCapMultiplier=4; // Recovered compiled defaults.
};
struct EffectEmissionProvider {
    // Must validate the real selected Random/Slide/motion admission BEFORE any
    // time, distance, or RNG mutation. Absent/unsupported provider fails clearly.
    std::function<bool(const EffectSpawnRequest&,std::string&)> prepare;
    std::function<bool(const EffectSpawnRequest&,uint64_t&,std::string&)> create;
    std::function<bool(uint64_t)> alive;
};
struct EffectPrimaryReplacement {int32_t ownerId=0;uint64_t previous=0,current=0;std::string name;};
class EffectAnimationWorld {
    std::map<std::string,EffectDefinition> definitions_;
public:
    bool admits(const std::string& name)const{return definitions_.contains(name);}
private:
    SourceValue defaults_;
    std::shared_ptr<AnimationRegistry> registry_=std::make_shared<AnimationRegistry>();
    EffectTexture texture_;
    std::map<std::string,EffectTexture> primaryTextures_;
    std::map<uint64_t,EffectAnimationEntity> entities_;
    uint64_t nextHandle_=1;
    bool inMutation_=false,faulted_=false;
    bool admittingParticleComponent_=false;
    std::string fault_;
    bool preflightCreate(const std::string&,std::vector<std::string>&,std::string&)const;
    bool initialize(const std::string&,int32_t,const EffectOwnerState&,uint64_t,
        const EffectAnimationRandom&,std::map<uint64_t,EffectAnimationEntity>&,uint64_t&,std::string&,
        AnimationOwnership rootOwnership=AnimationOwnership::ManagerCreate,const EffectTexture* primaryTexture=nullptr);
public:
    bool setRegistry(std::shared_ptr<AnimationRegistry>,std::string&);
    const std::shared_ptr<AnimationRegistry>& registry()const{return registry_;}
    bool loadCatalog(SourceValue,std::string&);
    bool create(const std::string&,int32_t,const EffectOwnerProvider&,const EffectAnimationRandom&,uint64_t&,std::string&);
    // Invoke at the real map Thing.SetAnimation initialization point, preserving
    // global RNG order. No manager Add, fake bulk metadata activation or alpha
    // based visibility. Selected Dust/Bat/Ember Constant initializers only.
    bool createPrimary(const std::string&,int32_t,const EffectOwnerProvider&,
        const EffectAnimationRandom&,EffectTexture originalGraphic,uint64_t&,std::string&);
    bool setSkipUpdate(uint64_t,bool actualSkipUpdate,std::string&);
    bool setDurationRemaining(uint64_t,float actualTimeRemaining,std::string&);
    // Particle owner allocates/commits/Adds the single real shared lifetime.
    // These methods own its TimeSpawner component only, not a second entity.
    bool attachParticleSpawner(uint64_t,const std::string&,const std::array<float,3>&,
        float ownScale,float ownerScale,float heading,float duration,
        const EffectAnimationRandom&,std::string&);
    bool syncParticleSpawner(uint64_t,const std::array<float,3>&,float ownScale,
        float ownerScale,float heading,float remaining,bool done,std::string&);
    bool detachParticleSpawner(uint64_t,std::string&);
    bool advancePrimaryDurations(float,const EffectOwnerProvider&,const EffectAnimationRandom&,
        std::vector<EffectPrimaryReplacement>&,std::string&);
    bool destroy(uint64_t,std::string&);
    bool update(float,const EffectOwnerProvider&,const EffectMarkerProvider&,
        const EffectAnimationRandom&,const EffectEmissionProvider&,EffectUpdateContext&,std::string&);
    const EffectAnimationEntity* entity(uint64_t)const;
    const std::map<uint64_t,EffectAnimationEntity>& entities()const{return entities_;}
    size_t size()const{return entities_.size();}
    size_t managerMemberCount()const;
    bool faulted()const{return faulted_;}
    std::string dump()const;
};
}
