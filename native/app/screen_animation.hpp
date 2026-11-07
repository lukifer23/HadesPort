#pragma once
#include "actor_animation.hpp"
#include "animation_registry.hpp"
#include "body_transition.hpp"
#include "effect_particle.hpp"
#include "object_runtime.hpp"
#include "scene_renderer.hpp"
#include <optional>
#include <set>
namespace hades {
struct ScreenAnimationSound {
 std::function<bool(const std::string&,std::string&)> prepare;
 std::function<bool(const std::string&,uint32_t&,std::string&)> play;
 std::function<bool(uint32_t,float,std::string&)> stop;
 uint64_t worldFrame=0;
};
// Created animations belong to the actual World AnimationManager; a delegated
// Child belongs to its actual registry parent. These are different lifetimes.
struct ScreenAnimationGraph {
 std::function<bool(int32_t,const std::string&,std::string&)> prepare;
 std::function<bool(int32_t,const std::string&,std::string&)> prepareChild;
 std::function<bool(int32_t,const std::string&,uint64_t,std::array<float,3>,uint64_t&,std::string&)> child;
 std::function<bool(int32_t,const std::string&,std::array<float,3>,uint64_t&,std::string&)> create;
 std::function<bool(uint64_t,std::string&)> releaseChild;
};
// One drawn animation of a screen Thing: the primary (index 0) followed by its
// original ChildAnimation chain (Animation.AddChild 0x293ae0; each child is a
// separate animation on the same Thing, drawn after its parent).
struct ScreenLayer {
 std::string name;uint64_t handle=0;size_t clip=0;
 bool legacy=false,movie=false; // Reviewed v6 closure keeps its earlier playback.
 uint64_t externalChild=0;uint32_t sound=0;
 std::vector<uint64_t> created; // Non-owning InitData+0x728 manager references.
 ActorAnimationPlayback playback;
 // Native Book frame state: DataPlaySpeed{speed,target}, PlaySpeedMultiplier,
 // Animation.mFractionalFrame/mFrame (.local/reports/hud-ft-semantics.json).
 float frac=1,speed=0,target=0,multiplier=1;int32_t frame=1;
 // Constant AnimationFractionFromDurationSystem / TimeRemainingSystem.
 float duration=0,remaining=0,progress=0;bool durationBound=false;
};
// Native ObstacleManager TransitionHelper<float> pair (Obstacle+0x5a0/0x5f0).
struct ScreenMoveAxis {bool active=false;float start=0,end=0,duration=0,remaining=0,fraction=0;};
struct ScreenThing {
 ObjectSnapshot* body=nullptr;
 std::string sourceGraphic; // Immutable ThingData.Graphic; body.animation is mutable.
 // Thing ctor 2daac6: own +22c=1, optional MapThing +240=null. These
 // factors describe destination parallax independently of render ownership.
 float ownParallax=1,mapParallax=1;
 // Thing.Defaults 0x2dbe6a: time multiplier(+0x80)=1; suppress sound(+0x62)=false.
 float timeMultiplier=1;bool suppressSounds=false;
 std::array<float,4> baseColor{1,1,1,1},fractionColor{1,1,1,1};
 // Body transitions: 0 Scale, 1 Alpha, 2 ScaleX, 3 ScaleY (BodyComponent
 // +0xe0/+0x320/+0x140/+0x1a0). Inactive/uninitialized modifiers are 1.
 std::array<BodyTransition,4> fractions;std::array<bool,4> adjusted{};
 ScreenMoveAxis moveX,moveY;BodyTransitionOptions moveOptions;
 std::vector<ScreenLayer> layers;
 ActorAnimationPlayback playback; // Legacy primary playback mirror.
 uint64_t primary=0;uint32_t sound=0;
 bool dead=false;
 float modifier(size_t k)const{return adjusted[k]?fractions[k].current():1.f;}
};
// Optional SetAnimation arguments (ScriptAction::SetAnimation 0x261a10).
struct ScreenAnimationRequest {
 std::optional<float> frameTarget,startFrameFraction;bool instant=false;
 float playSpeed=1;std::optional<std::array<float,4>> color;
};
// Script CreateScreenObstacle calls World SpawnObstacle, then changes draw
// coordinates. These genuine Thing/animation lifetimes stay in World manager0;
// the portable screen component store does not imply native GameUI ownership.
class ScreenAnimationWorld {
 std::shared_ptr<ActorAnimationBank> bank_;std::shared_ptr<AnimationRegistry> registry_;
 std::map<std::string,SourceValue> definitions_;
 std::set<std::string> actionChildren_;
 std::set<std::string> movies_;
 ScreenAnimationGraph graph_;
 std::function<bool(int32_t,std::string&)> preDisposeOwner_;
 std::map<int32_t,ScreenThing> things_;
 std::function<bool(int32_t,std::array<float,2>&,std::string&)> textDimensions_;
 std::function<bool(int32_t,uint64_t,uint64_t,std::string&)> cancelAllAttached_;
 EffectBoundedRandom boundedRandom_;
 EffectAnimationRandom random_;
 bool initializeDuration(ScreenLayer&,const SourceValue&,std::string&);
 bool validateSlide(const SourceValue&,const ActorAnimationClip&,std::string&)const;
 bool startSlide(ScreenLayer&,const SourceValue&,std::string&);
 bool initializeSlide(ScreenLayer&,const SourceValue&,std::string&);
 bool releasePrimary(ScreenThing&,const ScreenAnimationSound&,std::string&);
 bool allocateLayers(int32_t,ScreenThing&,const std::string&,const ScreenAnimationSound&,std::vector<ScreenLayer>&,std::string&);
 bool layerVisual(const ScreenThing&,const ScreenLayer&,ActorVisual&,std::string&)const;
public:
 static bool legacyClip(const std::string&);
 void setGraphProvider(ScreenAnimationGraph provider){graph_=std::move(provider);}
 void setPreDisposeOwnerProvider(std::function<bool(int32_t,std::string&)> provider){preDisposeOwner_=std::move(provider);}
 bool childFinished(uint64_t parent,uint64_t child,std::string&);
 void setBoundedRandomProvider(EffectBoundedRandom provider){boundedRandom_=std::move(provider);}
 void setRandomProvider(EffectAnimationRandom provider){random_=std::move(provider);}
 void setTextDimensionsProvider(std::function<bool(int32_t,std::array<float,2>&,std::string&)> provider){textDimensions_=std::move(provider);}
 // Animation.Attach299ea0 cancels other actual attachments before inserting
 // this animation; the old Thing primary is expressly excluded.
 void setCancelAllAttachedProvider(std::function<bool(int32_t,uint64_t,uint64_t,std::string&)> provider){cancelAllAttached_=std::move(provider);}
 bool load(SourceValue,std::shared_ptr<ActorAnimationBank>,std::shared_ptr<AnimationRegistry>,std::string&);
 bool admits(const std::string& name)const{return definitions_.contains(name);}
 const SourceValue* definition(const std::string& name)const{const auto it=definitions_.find(name);return it==definitions_.end()?nullptr:&it->second;}
 bool create(ObjectSnapshot&,std::array<float,4>,const ScreenAnimationSound&,std::string&,float ownParallax=1,float mapParallax=1);
 bool setAnimation(int32_t,const std::string&,const ScreenAnimationSound&,std::string&);
 bool setAnimation(int32_t,const std::string&,const ScreenAnimationRequest&,const ScreenAnimationSound&,std::string&);
 // ScriptAction::SetAnimationFrameTarget 0x263770: matching attached animations.
 bool setFrameTarget(int32_t,const std::string&,float fraction,bool instant,std::string&);
 bool adjustFraction(int32_t,bool,float,const BodyTransitionOptions&,std::string&);
 bool adjustChannel(int32_t,size_t channel,float,const BodyTransitionOptions&,std::string&);
 // Obstacle::MoveTo 0x2c5bf0 (screen location, no isometric transform).
 bool moveTo(int32_t,float x,float y,float duration,const BodyTransitionOptions&,std::string&);
 bool setColor(int32_t,const std::array<float,4>&,bool,std::string&);
 bool markDead(int32_t,std::string&);
 bool disposePending(const ScreenAnimationSound&,std::string&);
 bool advance(float,const ScreenAnimationSound&,std::string&,float worldSpeed=1);
 bool visual(int32_t,ActorVisual&,std::string&);
 bool visual(int32_t,uint64_t layerHandle,ActorVisual&,std::string&);
 ScreenThing* thing(int32_t id){auto i=things_.find(id);return i==things_.end()?nullptr:&i->second;}
 const ScreenThing* thing(int32_t id)const{auto i=things_.find(id);return i==things_.end()?nullptr:&i->second;}
 const std::map<int32_t,ScreenThing>& things()const{return things_;}
};
}
