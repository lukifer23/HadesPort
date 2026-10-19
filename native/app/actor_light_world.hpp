#pragma once
#include "actor_runtime.hpp"
#include "animation_registry.hpp"
#include "scene_renderer.hpp"
namespace hades {
struct ActorLightState {
    int32_t ownerId=0;uint64_t ownerLifetime=0,controller=0;
    std::string name;size_t imageClip=0;
    bool named=false,createsShadow=false,constant=false;
    float duration=0,remaining=0,fraction=0,baseAlpha=1;
    uint64_t locationRevision=0;
};
struct ActorLightVisual {ActorVisual image;bool createsShadow=false;float parallax=1;};
// Thing.Light ownership, distinct from animation-owned ActionFX light children.
// Original manager1 custom controller allocations do not enter manager Add.
class ActorLightWorld {
    ActorWorld& actors_;
    SourceValue catalog_;
    std::shared_ptr<ActorAnimationBank> bank_;
    std::shared_ptr<AnimationRegistry> registry_;
    std::map<uint64_t,ActorLightState> lights_;
    std::map<std::string,SourceValue> definitions_;
    bool loaded_=false,faulted_=false;
    std::string fault_;
    bool shadow(const ActorComponents&,bool&,std::string&)const;
public:
    explicit ActorLightWorld(ActorWorld& world):actors_(world){}
    bool load(SourceValue,std::shared_ptr<ActorAnimationBank>,std::shared_ptr<AnimationRegistry>,std::string&);
    bool admit(int32_t,const ActorRandom& stream0,std::string&);
    bool locationChanged(int32_t,std::string&);
    bool advanceControllers(float,const ActorRandom& stream0,std::string&);
    bool visual(int32_t,const std::array<uint8_t,4>& actualOwnerDrawColor,ActorLightVisual&,std::string&);
    bool remove(int32_t,std::string&);
    const ActorLightState* state(int32_t)const;
    std::vector<ActorLightState> snapshot()const;
    const std::shared_ptr<ActorAnimationBank>& bank()const{return bank_;}
};
}
