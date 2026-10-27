#pragma once
#include "effect_animation.hpp"
#include "effect_particle.hpp"
#include "scene_renderer.hpp"
#include <set>
namespace hades {
struct ActionEffectProviders {
 EffectOwnerProvider owner;
 EffectMarkerProvider marker;
 EffectAnimationRandom random;
 EffectBoundedRandom bounded;
 uint64_t worldFrame=0;
 // Concrete Animation.InitData sound branch, attached3D/standalone3DWithCap.
 std::function<bool(const std::string&,int32_t,const std::array<float,3>&,uint32_t,uint32_t&,std::string&)> sound;
 // Portable immutable coverage check before allocating any animation in the
 // source dependency graph. Does not play or reserve audio, or consume RNG.
 std::function<bool(const std::string&,std::string&)> prepareSound;
 // Source ModifiedTime/TimeModifierFraction adapter. Must use actual clocks.
 std::function<bool(int32_t,float,bool,float,float&,std::string&)> elapsed;
 // AnimationManager::CreateAndInit 0x18029fd65: RequiresUnblocked (data+0x64)
 // -> GridManager::IsLocationBlocked 0x180307f70(location, units=true,
 // PathfindersPreferAvoid=false, BlockSpawns=true); blocked -> no Animation.
 std::function<bool(float,float,bool&,std::string&)> locationBlocked;
};
struct ActionEffectState {
 uint64_t handle=0,parent=0,child=0,light=0,ownerLifetime=0;
 int32_t ownerId=0;std::string name;SourceValue definition;
 NativeAnimationType type=NativeAnimationType::Constant;
 size_t imageClip=0,slide=0;float frame=1,remaining=0,duration=0,playSpeed=0,fraction=0,frameTarget=0;
 std::array<float,3> position{},spawnPosition{};
 EffectOwnerState ownerSnapshot;
 bool detached=false,preferUnmodified=false,useScreenLocation=false,movie=false;
 std::optional<std::string> runtimeGroup;
 // sgg::ShiftFraction (8 bytes): {phase,rate}. InitData29205a..2920d4 copies
 // {GetValue(ShiftSpeed,Min,Max),0} verbatim (Copy14f69f memmove 8); absent
 // when that value compares equal to zero. Only GetZShift297c30 reads it.
 std::optional<float> shiftFraction;
 std::array<float,2> scaleVector{1,1};
 float ownScale=1,heading=0,rotation=0,speed=0,acceleration=0,headingSpeed=0,timeRemaining=0;
 float distanceRemaining=0;std::array<float,3> previousVfxLocation{};
 float randomX=0,randomY=0;bool flipX=false,flipY=false,isLight=false,done=false,durationBound=false;
 float rotationSpeed=0,gravity=0,upwardVelocity=0,ownZ=0;
 // Entity components added by InitData: Speed if the initial Velocity draw !=0
 // (0x180292970), UpwardVelocity if Gravity!=0 (0x180292a8e), RotateWithGravity (0x180292df6).
 bool speedComponent=false,upwardComponent=false,rotateWithGravity=false,rotationComponent=false;
 uint32_t totalSpawned=0,sound=0;
 // Animation+0x6e4, sampled once by InitData from VisualFxCapMin/Max.
 int32_t visualFxCap=0;
 std::array<uint8_t,4> baseColor{255,255,255,255};
 std::array<float,3> hsv{};
 std::vector<uint64_t> created;
 // Animation spawned-VFX list (+0x740): TimeVFX-phase VisualFx spawns only,
 // counted by UpdateActiveVfxCount 0x1802940b0 for ActiveVisualFxCap.
 std::vector<uint64_t> vfx;
 // ThingPrimary (Thing::SetAnimation) rather than a manager-created animation.
 bool primary=false,guiLine=false;
 // Animation+0x6c4: set by AnimationManager::Cancel(PreventChain); suppresses ChainTo.
 bool preventChain=false;
 // Script SetupAnimation/SetAnimation live fields (Animation mAttachOffset
 // +0xa0, mOffsetZ +0x6dc, mScaleRadius +0x6cc). Absent attachOffset means the
 // definition OffsetX/OffsetY seeded at InitData.
 std::optional<std::array<float,2>> attachOffset;
 float addedOffsetZ=0,scaleRadius=0,parallax=1;
 // ECS PlaySpeedMultiplier component (TR and Book frame step), Book
 // CustomUpdateSystem mode, runtime PlayBackwards, Slide timer.
 float playSpeedMultiplier=1,slideRemaining=0;
 bool customFrames=false,backwards=false;
};
// ScriptAction::SetupAnimation 0x180262c50 arguments, in native order.
struct ActionAnimationSetup {
 float scale=1,scaleRadius=0,duration=0,offsetX=0,offsetY=0,offsetZ=0;
 bool mirrorOffsetX=false;
 bool useScreenLocation=false;
 std::optional<float> angleDegrees;
 std::optional<std::array<uint8_t,4>> color; // packed RGBA bytes (LuaTableExtensions::GetColor)
 std::optional<std::string> group;           // "" = MapThingGroupId::None
 // Thing mParallax x MapThing ParallaxAmount (only with a destination Thing).
 std::optional<float> parallax;
};
class ActionEffectWorld {
 SourceValue catalog_;std::map<std::string,SourceValue> definitions_;
 std::shared_ptr<ActorAnimationBank> bank_,movieBank_;std::shared_ptr<AnimationRegistry> registry_;
 std::set<std::string> movies_;
 std::string fault_;
 void retainFault(std::string& error){if(!faulted_){fault_=error.empty()?"Action FX failed after native allocation/mutation":error;faulted_=true;}error=fault_;}
 std::map<uint64_t,ActionEffectState> states_;bool faulted_=false,mutating_=false,primaryRequest_=false,guiLineRequest_=false;
 // Property coverage review: exporter rejections and, per admitted
 // definition, source fields with no reconstructed consumer (fail on use)
 // or with a known pending presentation consumer (admitted, reported).
 std::map<std::string,std::string> rejected_;
 std::map<std::string,std::vector<std::string>> unsupported_,pending_;
 std::set<uint64_t> retiredPrimaries_;
 // AudioManager::StopCue(id, fade) adapter for Animation::Dispose 0x1802996a0
 // and OnLoop 0x180296670 (StopSoundOnFinishFade).
 std::function<bool(uint32_t,float,std::string&)> stopSound_;
 uint32_t activeVfxCount(uint64_t);
 uint32_t angleNum(const ActionEffectState&,const EffectOwnerState&)const;
 void forChain(uint64_t,const std::function<void(ActionEffectState&,bool)>&);
 bool transferFrame(uint64_t,const ActionEffectState&,std::string&);
 bool loopAnimation(uint64_t,bool force,const ActionEffectProviders&,std::string&);
 bool initialize(const std::string&,int32_t,const EffectOwnerState&,std::array<float,3>,
   uint64_t,bool,const ActionEffectProviders&,uint64_t&,std::string&);
 bool slideFx(uint64_t,const ActionEffectProviders&,std::string&);
 bool preflight(const std::string&,const ActionEffectProviders&,std::vector<std::string>&,std::string&,bool guiLine=false)const;
public:
 bool load(SourceValue,std::shared_ptr<ActorAnimationBank>,std::shared_ptr<AnimationRegistry>,std::string&,std::shared_ptr<ActorAnimationBank> movieBank={});
 bool movie(const std::string& name)const{return movies_.contains(name);}
 void setSoundStop(std::function<bool(uint32_t,float,std::string&)> f){stopSound_=std::move(f);}
 bool admits(const std::string& name)const{return definitions_.contains(name);}
 // Exporter/coverage reason for a covered original name that is not admitted.
 const std::string* rejection(const std::string& name)const{auto i=rejected_.find(name);return i==rejected_.end()?nullptr:&i->second;}
 const std::vector<std::string>* pendingFields(const std::string& name)const{auto i=pending_.find(name);return i==pending_.end()||i->second.empty()?nullptr:&i->second;}
 // Thing::SetAnimation with an action-bank definition as the owner primary.
 bool createPrimary(const std::string&,int32_t,const ActionEffectProviders&,uint64_t&,std::string&);
 bool retiredPrimary(uint64_t h)const{return retiredPrimaries_.contains(h);}
 const std::string& faultReason()const{return fault_;}
 std::vector<uint64_t> completedPrimaries()const;
 bool setup(uint64_t,const ActionAnimationSetup&,std::string&);
 // Animation::SetRadius 0x1802974e0: pChildAnimation first, then each created
 // animation named by CreateAnimation/CreateAnimations, then self mScaleRadius.
 bool setRadius(uint64_t,float,std::string&);
 // ScriptAction::SetAnimation optional Scale/Duration/Offset on the new primary.
 bool setScale(uint64_t,float,std::string&);
 bool setDuration(uint64_t,float,std::string&);
 bool setAttachOffset(uint64_t,float,float,std::string&);
 // Animation::AddAngle 0x180298260 (radians, self and ChildAnimation chain).
 bool addAngle(uint64_t,float,std::string&);
 // Animation::SetZLocation 0x2990e0: own Z (+0x6e0) and ZLocation component; self only.
 bool setZLocation(uint64_t,float,std::string&);
 // Weapon::Charge hooks: Animation vfunc+0xe0 GetTimeRemaining (Constant
 // mTimeRemaining; Book (EndFrame-mFrame)/(DataPlaySpeed x PSM), 0x1802b0780),
 // the PlaySpeedMultiplier component, and PreferUnmodifiedTime 0x18029a5c0.
 bool timeRemaining(uint64_t,float&,std::string&)const;
 bool setPlaySpeedMultiplier(uint64_t,float,std::string&);
 bool setFrameTarget(int32_t,const std::string&,float,bool,std::string&);
 bool preferUnmodifiedTime(uint64_t,std::string&);
 bool prepare(const std::string&,int32_t,const ActionEffectProviders&,std::string&,bool guiLine=false)const;
 bool create(const std::string&,int32_t,std::optional<std::array<float,3>>,const ActionEffectProviders&,uint64_t&,std::string&,bool guiLine=false);
 // Native CreateCustom with nullptr Thing: a genuinely ownerless allocation.
 bool createStandalone(const std::string&,std::array<float,3>,const ActionEffectProviders&,uint64_t&,std::string&);
 bool copyState(uint64_t target,uint64_t source,const ActionEffectProviders&,std::string&);
 bool advance(float,const ActionEffectProviders&,std::string&);
 bool updateTime(float,const ActionEffectProviders&,uint32_t& spawnedThisFrame,std::string&);
 bool updateDistance(const ActionEffectProviders&,uint32_t& spawnedThisFrame,std::string&);
 bool refreshOwner(int32_t,const ActionEffectProviders&,std::string&);
 bool destroy(uint64_t,std::string&);
 // Animation+0x17c colour alpha byte write (ObstacleManager::Update 0x1802c9a2c).
 bool setBaseAlpha(uint64_t h,uint8_t a,std::string& error){auto i=states_.find(h);if(i==states_.end()){error="Actual action FX alpha handle unavailable";return false;}i->second.baseColor[3]=a;return true;}
 // AnimationManager::Cancel 0x29e520 for one owner: empty name = all; returns matches.
 bool cancelAttached(int32_t owner,const std::string& name,bool preventChain,size_t& matched,std::string&);
 // Animation.Attach299ebc CancelAllAttached: actual attachments except the
 // Thing's current primary; ignores CanCancelAsAttached/PreventChain.
 bool cancelAllAttachedForAttach(int32_t owner,uint64_t lifetime,uint64_t currentPrimary,std::string&);
 bool configureCreated(uint64_t,float rawZ,const std::string& actualFirstGroup,bool preferUnmodified,std::string&);
 bool preDisposeOwner(int32_t,const ActionEffectProviders&,std::string&);
 bool preDisposeOwner(int32_t,const EffectOwnerState&,std::string&);
 const ActionEffectState* state(uint64_t h)const{auto i=states_.find(h);return i==states_.end()?nullptr:&i->second;}
 const std::map<uint64_t,ActionEffectState>& states()const{return states_;}
 std::vector<uint64_t> handles(bool includeLights=false)const;
 // Caller supplies actual group/blend/sort and owner fraction/HSV. This method
 // supplies source original frame, pivot/flip, native transition/body fields.
 bool visual(uint64_t,const EffectOwnerState&,const std::array<uint8_t,4>& ownerFraction,ActorVisual&,std::string&);
};
}
