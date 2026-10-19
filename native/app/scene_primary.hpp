#pragma once
#include "animation_registry.hpp"
#include "effect_animation.hpp"
#include "effect_particle.hpp"
#include "scene_animation.hpp"
#include <memory>

namespace hades {
struct ActorVisual;
struct ScenePrimaryRecipe {
    int32_t ownerId=0;std::string graphic;NativeAnimationType type=NativeAnimationType::Dataless;
    bool named=false,externalHaa=false;
    std::vector<uint32_t> sceneSprites;
};
enum class PrimaryResourceKind {SceneStatic,SceneAnimated,Auxiliary,Pending};
struct ScenePrimaryState {
    uint64_t handle=0,child=0,parent=0,ownerLifetime=0;int32_t ownerId=0;
    std::string name;SourceValue definition;NativeAnimationType type=NativeAnimationType::Dataless;
    PrimaryResourceKind resource=PrimaryResourceKind::Pending;uint32_t resourceIndex=0;
    std::vector<uint32_t> sceneSprites;
    float frame=1,playSpeed=0,startFrame=1,endFrame=1;
    float ownScale=1,offsetX=0,offsetY=0,offsetZ=0;
    std::array<float,2> atlasScale{1,1};
    float duration=0,durationRemaining=0;
    std::array<float,3> hsv{};
    bool loop=false,loopFramesOnly=false,allowRestart=false,onlyWhenVisible=false;
    bool ownerImmobile=false,disableWeapons=false,ownerHasNoCollision=false,blockNewOwnerAnims=false;
    bool ownerInvulnerable=false,ownerNoArmorCoverage=false,ownerNoInvulnerableCoverage=false;
    bool initialized=false,graphicsPending=false,finished=false,hasDuration=false,holdLastFrame=false;
};
// Actual typed primary lifetimes/resources. HAA hero and the nine live VFX
// primary classes have separate existing owners and are never allocated twice.
class ScenePrimaryWorld {
    SourceValue catalog_;
    std::map<std::string,SourceValue> definitions_;
    std::map<int32_t,ScenePrimaryRecipe> recipes_;
    std::map<uint64_t,ScenePrimaryState> states_;
    std::vector<uint64_t> spriteOwners_;
    std::shared_ptr<AnimationRegistry> registry_;
    GraphicsScene scene_;
    SceneAnimation animated_;
    std::shared_ptr<ActorAnimationBank> auxiliary_;
    bool faulted_=false;
    bool initialize(const std::string&,const ScenePrimaryRecipe&,const EffectOwnerState&,
        uint64_t parent,const EffectAnimationRandom&,const EffectBoundedRandom&,uint64_t&,std::string&);
public:
    bool load(AssetStore&,SourceValue,std::shared_ptr<AnimationRegistry>,std::string&);
    const ScenePrimaryRecipe* recipe(int32_t)const;
    bool admitsDynamic(const std::string& name)const{const auto* list=catalog_.get("DynamicGraphics");if(!list)return false;for(const auto& value:list->array())if(value.text()==name)return true;return false;}
    static bool externalEffect(const std::string&);
    bool create(int32_t,const std::string&,const EffectOwnerState&,
        const EffectAnimationRandom&,const EffectBoundedRandom&,uint64_t&,std::string&);
    bool replace(uint64_t,const std::string&,const EffectOwnerState&,
        const EffectAnimationRandom&,const EffectBoundedRandom&,uint64_t&,std::string&);
    bool advance(float,std::string&);
    std::vector<uint64_t> completedPrimaries()const;
    bool selectSceneFrame(uint32_t,std::optional<SceneFrameSelection>&,std::string&)const;
    bool submitted(uint32_t,std::string&);
    std::vector<uint64_t> auxiliaryHandles()const;
    // Fill image identity and atlas-local rectangle only. Caller composes the
    // actual native owner/location/scale/offset/group/color in ActorVisual.
    bool sampleAuxiliary(uint64_t,ActorVisual&,std::string&);
    bool destroy(uint64_t,std::string&);
    const ScenePrimaryState* state(uint64_t)const;
    const ScenePrimaryState* stateForSprite(uint32_t index)const{return index<spriteOwners_.size()?state(spriteOwners_[index]):nullptr;}
    const std::map<uint64_t,ScenePrimaryState>& states()const{return states_;}
    const GraphicsScene& graphics()const{return scene_;}
    const std::shared_ptr<ActorAnimationBank>& auxiliary()const{return auxiliary_;}
    size_t size()const{return states_.size();}
};
}
