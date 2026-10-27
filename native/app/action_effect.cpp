#include "action_effect.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#ifdef __clang__
#pragma clang fp contract(off)
#endif
namespace hades {namespace {
float n(const SourceValue&d,const char*k,float fallback=0){auto*p=d.get(k);const double v=p?p->number():fallback;if(!std::isfinite(v)||std::abs(v)>100000)throw std::runtime_error(std::string("Action FX source scalar bound: ")+k);return float(v);}
std::string t(const SourceValue&d,const char*k){auto*p=d.get(k);return p&&!std::holds_alternative<std::monostate>(p->value)?p->text():"";}
bool b(const SourceValue&d,const char*k){auto*p=d.get(k);return p&&p->boolean();}
// Frame consumers store a signed native integer alongside the float. Reject
// malformed live values rather than inventing CVTTSS2SI's indefinite result.
bool transferableFrame(float frame){return std::isfinite(frame)&&std::trunc(frame)>=-2147483648.f&&std::trunc(frame)<2147483648.f;}
// Animation.GetLocation296c90 is distinct from GetDrawLocation297d60:
// the former adds raw attachment offset, never source draw OffsetX/Y.
// A null marker provider selects the already resolved marker anchor for draw;
// gameplay location consumers pass their real provider and resolve it fresh.
bool sourceLocation(const ActionEffectState&s,const EffectOwnerState&owner,const EffectMarkerProvider* markerProvider,std::array<float,3>&out,std::string&error){
 out=s.position;if(s.detached)return true;
 const float x=s.attachOffset?(*s.attachOffset)[0]:s.randomX,y=s.attachOffset?(*s.attachOffset)[1]:s.randomY;
 const auto marker=t(s.definition,"UseAttachedMarkerLocation");
 if(!marker.empty()){
  if(markerProvider){std::array<float,3> anchor{};if(owner.hasPrimaryAnimation){if(!*markerProvider){error="Native Animation.GetLocation marker provider unavailable";return false;}if(!(*markerProvider)(s.ownerId,marker,anchor,error))return false;}else{anchor[0]=owner.x;anchor[1]=owner.y;}out[0]=anchor[0]+x;out[1]=anchor[1]+y;}
  else{out[0]+=x-s.randomX;out[1]+=y-s.randomY;}
 }else if(t(s.definition,"LocationFromOwner").empty()||t(s.definition,"LocationFromOwner")=="Maintain"){out[0]=owner.x+x;out[1]=owner.y+y;}
 return true;
}
float rad(float v){return v*.01745329251994329577f;}
bool rng(const ActionEffectProviders&p,float lo,float hi,float&v,std::string&e){double r;if(!p.random||!p.random(r,e)||!std::isfinite(r)||r<0||r>=1){if(e.empty())e="Actual action FX uniform RNG unavailable";return false;}const volatile float product=float(r)*(hi-lo);v=product+lo;return true;}
float transition(const SourceValue&d,const char*start,const char*end,float fraction,float fallback,float sourceDefault=0){const float a=n(d,start,sourceDefault),z=n(d,end,sourceDefault);return a==z?fallback:EffectParticleWorld::interpolate(a,z,fraction,n(d,"EaseIn"),n(d,"EaseOut"));}
NativeAnimationType nativeType(const SourceValue&d){const auto name=t(d,"Type");if(name=="Slide")return NativeAnimationType::Slide;if(name=="Book")return NativeAnimationType::Book;if(name=="Constant")return NativeAnimationType::Constant;if(name=="ConstantNoUpdate")return NativeAnimationType::ConstantNoUpdate;if(name=="Random")return NativeAnimationType::Random;throw std::runtime_error("Unsupported original action FX class "+name);}
// GetXShift/GetYShift/GetZShift 0x180297b40/0x180297a50/0x180297c30 with
// Animation::GetValue 0x180298750 (PingPongShiftOverDuration) and Interpolate.
float shift(const SourceValue&d,const char*start,const char*end,float fraction){const float a=n(d,start),z=n(d,end);if(a==z)return 0;float f=n(d,"PhaseOffsetShiftOverDuration")/360.f+fraction;if(f>1)f-=1;else if(f<0)f+=1;const float in=n(d,"EaseIn"),out=n(d,"EaseOut");if(b(d,"PingPongShiftOverDuration"))return f<.5f?EffectParticleWorld::interpolate(a,z,f*2,in,out):EffectParticleWorld::interpolate(z,a,(f-.5f)*2,in,out);return EffectParticleWorld::interpolate(a,z,f,in,out);}
// InitData 0x18029217d..0x1802922fd: BodyComponent::Adjust transition when
// Start!=End (AnimationData ctor default 1), PingPongScale, EaseIn/EaseOut;
// BodyComponentTransitionHelper::CalcCurrentValue 0x1802b5420 at mFraction.
float scaleHelper(const SourceValue&d,const char*start,const char*end,float f){const float a=n(d,start,1),z=n(d,end,1);if(a==z)return 1;const float in=n(d,"EaseIn"),out=n(d,"EaseOut");if(b(d,"PingPongScale"))return f<.5f?EffectParticleWorld::interpolate(a,z,f*2,in,out):EffectParticleWorld::interpolate(z,a,(f-.5f)*2,in,out);return EffectParticleWorld::interpolate(a,z,f,in,out);}
// AnimationData::Read 0x180090212/0x18009026d: StartAngle/EndAngle are
// float(double(degrees) x 0.017453292519943295) (constant 0x180610e80).
float radD(float v){return float(double(v)*0.017453292519943295);}
// BodyComponent mAngleTransition (body+0x80): InitData 0x1802928ab..0x180292941
// adjusts it only when StartAngle!=EndAngle, start unwrapped, end wrapped
// End-floor(End/tau)*tau; Adjust 0x1802b7440 wraps PhaseOffsetAngle into
// [0,360] and scales by 1/360 (0x1806110a0/0x180610d48); CalcCurrentValue
// 0x1802b5420 applies the phase only with PingPongAngle. The fraction is the
// animation fraction pushed to active transitions by Book/Constant
// UpdateInternal (0x1802af591). GetAngleModifier is additive to mAngle
// (GetDrawAngle 0x1802980a0 subtracts mAngle + CalcCurrentValue).
float angleModifier(const SourceValue&d,float fraction){
 const float a=radD(n(d,"StartAngle")),z0=radD(n(d,"EndAngle"));if(a==z0)return 0;
 const float tau=6.2831854820251465f;const volatile float q=z0/tau;const volatile float m=std::floor(float(q))*tau;const float z=z0-m;
 const float in=n(d,"EaseIn"),out=n(d,"EaseOut");
 if(!b(d,"PingPongAngle"))return EffectParticleWorld::interpolate(a,z,fraction,in,out);
 float phase=n(d,"PhaseOffsetAngle");if(0.f>phase)phase=360.f-(0.f-phase);else if(phase>360.f)phase=phase-360.f+0.f;phase*=0.0027777778450399637f;
 float f=phase+fraction;if(0.f>f)f=1.f-(0.f-f);else if(f>1.f)f=f-1.f+0.f;
 return .5f>f?EffectParticleWorld::interpolate(a,z,f+f,in,out):EffectParticleWorld::interpolate(z,a,(f-.5f)+(f-.5f),in,out);}
bool maintainsOwnerAngle(const SourceValue&d){return t(d,"AngleFromOwner").empty()||t(d,"AngleFromOwner")=="Maintain";}
float wrapAngle(float a){const volatile float q=a/6.2831854820251465f;const volatile float m=std::floor(float(q))*6.2831854820251465f;return a-m;}
// Property coverage review (replaces the former reviewed-name allowlist).
// Every source key of an exported definition is classified once at load:
// consumed by this initializer/updater/draw or by the caller's presentation
// (GroupName/UseAttachedGroup), structural export data, a pending presentation
// hint (admitted, reported by name), or unreconstructed (instantiating the
// definition fails naming the field).
const std::set<std::string>& handledFields(){static const std::set<std::string> v{
 "Name","InheritFrom","FilePath","Type","NumFrames","Slides","ClearSlides","ClearCreateAnimations",
 "Acceleration","AddColor","Angle","AngleChangeSpeedMax","AngleChangeSpeedMin","AngleFromOwner","AngleMax","AngleMin",
 "CancelOverCap","ChainTo","ChildAnimation","Color","ColorFromOwner","CreateAnimation","CreateAnimationOnEnd","CreateAnimations","CreateChance",
 "DieWithOwner","Duration","EaseIn","EaseOut","EndAlpha","EndBlue","EndFrame","EndGreen","EndRed","EndScale",
 "OwnerHasNoCollision","OwnerImmobile","DisableOwnerWeapons",
 // Actual primaryCombatFlags consumes these AnimationData fields for Unit
 // defense/crit/coverage queries. Manager-created FX are not Thing primaries.
 "OwnerInvulnerable","OwnerNoArmorCoverage","OwnerNoInvulnerableCoverage","OwnerUntargetable",
 "FlipHorizontal","FlipVertical","Gravity","IsometricSkew","Light","LocationFromOwner","LocationZFromOwner",
 "OffsetX","OffsetY","OffsetZ","OnlyWhenVisible","OriginX","OriginY","PingPongColor","PlaySpeed",
 "PostRotateScaleX","PostRotateScaleY","Random","RandomAccelerationMax","RandomAccelerationMin","RandomDurationMax",
 "RandomDurationMin","RandomFlipHorizontal","RandomOffsetX","RandomOffsetY","RandomPlaySpeedMax","RandomPlaySpeedMin",
 "RandomRotation","RotateWithGravity","RandomRotationSpeedMax","RandomRotationSpeedMin","Scale","ScaleFromOwner","ScaleMax","ScaleMin",
 "ScaleX","ScaleY","Sound","SoundAllowedAfterFrame","SoundManagerCap","StartAlpha","StartBlue","StartFrame","StartGreen",
 "StartRed","StartScale","TimeModifierFraction","TransferCopyBody","TransferDrawLayer","TransferFrameFromThis","TransferFrameToThis","UseAttachedMarkerLocation",
 "UseOwnAngle","UseVelocityAngleFromOwner","VelocityMax","VelocityMin","VisualFx","VisualFxCap","VisualFxCapMin","VisualFxCapMax","VisualFxIntervalMax","VisualFxIntervalMin",
 "VisualFxManagerCap","ShiftSpeed","ShiftSpeedMin","ShiftSpeedMax","GroupName","UseAttachedGroup","MaintainAlphaOnly","ScaleRadius","GammaCorrection","NonHighColorChannelMultiplier","Red","Green","Blue","Alpha","DurationFrames","LoopFramesOnly","PlayBackwards","StartOffsetX","EndOffsetX","StartOffsetY","EndOffsetY","StartOffsetZ","EndOffsetZ","PingPongShiftOverDuration","PhaseOffsetShiftOverDuration","RandomFlipVertical","StartScaleX","EndScaleX","StartScaleY","EndScaleY","PingPongScale",
 // Multi-angle atlas selection (BookAnimation::GetAngleNum 0x1802b0630),
 // angle transition, Hue/Saturation/Value (InitData 0x180292459), upward
 // velocity (0x180292a58), StopCue on Dispose/OnLoop, attached clock link
 // (UseAttachedToTimeMultiplier 0x180299d40; per-entity elapsed adapters
 // validate actual clock links), ActiveVisualFxCap (UpdateVfxTimeInterval 0x1802952cd).
 "NumAngles","AngleNumOffset","StartAngle","EndAngle","PingPongAngle","PhaseOffsetAngle","Hue","Saturation","Value",
 "RandomStartFrame","VisualFxAngleOffset","VisualFxInheritAngle","VisualFxDistanceMin","VisualFxDistanceMax","VisualFxDistanceMinChange","VisualFxFrameCap","UpwardVelocity","UpwardVelocityMin","UpwardVelocityMax","RadialOffsetMin","RadialOffsetMax","RadialOffsetScaleY","AlphaMin","AlphaMax","StopSoundOnFinishFade","UseAttachedTimeMultiplier","ActiveVisualFxCap"};return v;}
// Presentation hints with no native consumer yet in the shared renderer/draw
// order (material/ambient/sort/overlay, attached flasher, chain transfers).
// Admitted as explicit approximations and reported per definition.
const std::set<std::string>& pendingFieldsSet(){static const std::set<std::string> v{
 "Material","Ambient","SortMode","SortFromOwner","UseBoundsForSortArea","DrawBehindOwner","OverlayVfx",
 "KeepVisualFxAttached","TransferAngle","TransferChildren"};return v;}
}
bool ActionEffectWorld::load(SourceValue catalog,std::shared_ptr<ActorAnimationBank> bank,std::shared_ptr<AnimationRegistry> registry,std::string&error,std::shared_ptr<ActorAnimationBank> movieBank){try{
 if(!states_.empty()||!bank||!registry||t(catalog,"Schema")!="HadesActionEffectResources1"||t(catalog,"Target")!="d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254")throw std::runtime_error("Source action FX catalog/bank/registry mismatch");
 if(const auto*rejected=catalog.get("Rejected"))for(const auto&[name,reason]:rejected->object())rejected_.emplace(name,reason.text());
 auto definitions=catalog.get("Definitions")->object();
 if(const auto* movies=catalog.get("MovieDefinitions"))for(const auto&[name,d]:movies->object()){
  const auto clip=movieBank?movieBank->find(name):std::optional<size_t>{};
  if(t(d,"Type")!="Book"||t(d,"VideoTexture").empty()||!clip||!(movieBank->clips()[*clip].flags&ActorBook)||!definitions.emplace(name,d).second)throw std::runtime_error("Actual movie Book definition/image binding unavailable: "+name);
  movies_.insert(name);rejected_.erase(name);
 }
 for(const auto&[name,d]:definitions){
  if(t(d,"Type")!="Random"&&!(movies_.contains(name)?movieBank:bank)->find(name))throw std::runtime_error("Exported original action FX has no bank clip: "+name);
  const auto type=nativeType(d);auto&unsupported=unsupported_[name];auto&pending=pending_[name];
  for(const auto&[key,value]:d.object()){
   // VideoTexture selects the hash-bound original movie image backend;
   // inherited Slides are not dispatched by native Book.Init/PostUpdate.
   if(movies_.contains(name)&&(key=="VideoTexture"||key=="FrameDataFile"))continue;
   // Unit.UpdateAnimation owns this primary-only policy; child/manager
   // animations do not independently cancel when their Thing moves.
   if(key=="CancelOnOwnerMove")continue;
   if(key=="Loop")continue;
   // OnLoop 0x180296670 replays Sound; with StopSoundOnFinishFade it first
   // stops the cue, which needs a stop provider not reconstructed here.
   if(key=="ReplaySoundOnLoop")continue;
   // BookAnimation::UpdateInternal 0x1802af62f: HoldLastFrame skips the frame
   // end loop/finish block; IsFinished 0x1802959f0 ignores TimeRemaining.
   if(key=="HoldLastFrame"){if(value.boolean()&&type!=NativeAnimationType::Constant&&type!=NativeAnimationType::ConstantNoUpdate&&type!=NativeAnimationType::Book)unsupported.push_back("HoldLastFrame on "+t(d,"Type"));continue;}
   // AnimationData ctor 0x18009425b default ReRandomizeOnLoop=true; Animation::Loop
   // 0x1802963ea skips re-randomization when false (exact); true is not rebuilt.
   if(key=="ReRandomizeOnLoop"){if(value.boolean())unsupported.push_back("ReRandomizeOnLoop=true");continue;}
   // GetAngleNum mirror (data+0x6e) and isometric (data+0x9d) branches.
   if(key=="Mirror"){if(value.boolean()&&movies_.contains(name))unsupported.push_back("Movie Mirror UV policy");else if(value.boolean()&&n(d,"NumAngles",1)<=1)unsupported.push_back("Mirror on single-angle");continue;}
   if(key=="UseIsometricAngle"){if(value.boolean()&&!movies_.contains(name))unsupported.push_back("UseIsometricAngle");continue;}
   // GetDrawFlasher owner inheritance is consumed by the presentation bridge.
   if(key=="UseAttachedFlasher")continue;
   if(key=="UseAttachedShake")continue;
   if(key=="UseAttachedAddColor"){if(value.boolean())pending.push_back(key);continue;}
   if(key=="PlayBackwards"&&value.boolean()&&type!=NativeAnimationType::Book){unsupported.push_back("PlayBackwards on "+t(d,"Type"));continue;}
   // AnimationData::Read 0x18008f7bc -> data+0x64; consumed only by
   // AnimationManager::CreateAndInit 0x18029fd65 (gated in initialize).
   if(key=="RequiresUnblocked")continue;
   // VisualFxUseAttachedMarkerLocation (data+0x4c0, AnimationData::Read
   // 0x180092b86) is read only by SpawnVfxTimeInterval 0x1802943f9 and
   // SpawnVfxDistanceInterval 0x180294887; inert when VisualFx is empty/"null".
   if(key=="VisualFxUseAttachedMarkerLocation"){const auto vfx=t(d,"VisualFx");if(!vfx.empty()&&vfx!="null"&&!value.text().empty())unsupported.push_back(key);continue;}
   if(handledFields().contains(key))continue;
   (pendingFieldsSet().contains(key)?pending:unsupported).push_back(key);
  }
  definitions_.emplace(name,d);
 }
 if(definitions_.empty())throw std::runtime_error("Empty original action FX catalog");catalog_=std::move(catalog);bank_=std::move(bank);movieBank_=std::move(movieBank);registry_=std::move(registry);return true;
 }catch(const std::exception&e){error=e.what();return false;}}
bool ActionEffectWorld::preflight(const std::string&name,const ActionEffectProviders&p,std::vector<std::string>&path,std::string&error,bool guiLine)const{
 auto i=definitions_.find(name);if(i==definitions_.end()){const auto*why=rejection(name);error="Original action FX not exported: "+name+(why?" ("+*why+")":" (outside animation coverage)");return false;}
 if(path.size()>=16||std::find(path.begin(),path.end(),name)!=path.end()){error="Original action FX dependency cycle/depth: "+name;return false;}
 if(const auto u=unsupported_.find(name);u!=unsupported_.end())for(const auto& field:u->second){
  // GUIComponentLine::Draw170b68..b9f consumes these animation-data fields,
  // unlike ordinary Animation::SubmitDraw. Only its explicit caller admits them.
  if(guiLine&&(field=="ScaleLength"||field=="ScaleWidth"))continue;
  error="Original action FX "+name+" uses unreconstructed source field "+field;return false;}
 const auto&d=i->second;if(guiLine){if(nativeType(d)!=NativeAnimationType::Constant||n(d,"ScaleLength")<=0||n(d,"ScaleWidth")<=0){error="GUI line requires source Constant/ScaleLength/ScaleWidth";return false;}for(const auto*key:{"ChildAnimation","CreateAnimation","CreateAnimationOnEnd","VisualFx","Light","ChainTo"})if(!t(d,key).empty()&&t(d,key)!="null"){error="GUI line nested animation lifecycle not reconstructed";return false;}}if(!t(d,"Sound").empty()&&!p.sound){error="Actual action FX source sound provider unavailable";return false;}if(!t(d,"Sound").empty()&&n(d,"StopSoundOnFinishFade")>0&&!stopSound_){error="Actual action FX StopCue fade provider unavailable";return false;}if((!t(d,"UseAttachedMarkerLocation").empty())&&!p.marker){error="Actual action FX original marker provider unavailable";return false;}if(!p.owner||!p.random||!p.bounded){error="Actual action FX owner/native RNG providers unavailable";return false;}
 const auto cue=t(d,"Sound");if(!cue.empty()&&p.prepareSound&&!p.prepareSound(cue,error))return false;
 path.push_back(name);for(const auto*key:{"ChildAnimation","CreateAnimation","CreateAnimationOnEnd","VisualFx","Light","ChainTo"}){const auto dep=t(d,key);if(!dep.empty()&&dep!="null"&&!preflight(dep,p,path,error))return false;}
 if(auto*creates=d.get("CreateAnimations"))for(const auto&r:creates->array())if(!preflight(t(r,"Name"),p,path,error))return false;
 if(nativeType(d)==NativeAnimationType::Slide)if(auto*slides=d.get("Slides"))for(const auto&s:slides->array()){auto dep=t(s,"VisualFx");if(!dep.empty()&&!preflight(dep,p,path,error))return false;}
 if(auto*choices=d.get("Random"))for(const auto&r:choices->array())if(!preflight(t(r,"Name"),p,path,error))return false;
 path.pop_back();return true;
}
bool ActionEffectWorld::prepare(const std::string&name,int32_t ownerId,const ActionEffectProviders&p,std::string&error,bool guiLine)const{try{if(faulted_){error=fault_;return false;}std::vector<std::string>path;if(!registry_||!preflight(name,p,path,error,guiLine))return false;EffectOwnerState o;if(!p.owner(ownerId,o,error)||!o.lifetime||!o.baseColor||!o.baseHsv){if(error.empty())error="Actual action FX source owner/body unavailable";return false;}return true;}catch(const std::exception&e){error=e.what();return false;}}
bool ActionEffectWorld::initialize(const std::string&name,int32_t ownerId,const EffectOwnerState&owner,std::array<float,3>position,uint64_t parent,bool light,const ActionEffectProviders&p,uint64_t&handle,std::string&error){
 const auto&d=definitions_.at(name);
 if(nativeType(d)==NativeAnimationType::Random){const float chance=n(d,"CreateChance",1);if(chance<1){float roll;if(!rng(p,0,1,roll,error))return false;if(roll>chance){handle=0;return true;}}const auto&choices=d.get("Random")->array();if(choices.empty()||choices.size()>64){error="Original Random action choice bound";return false;}uint32_t index=0;if(!p.bounded(uint32_t(choices.size()),index,error)||index>=choices.size())return false;primaryRequest_=primaryRequest_&&!parent&&!light;return initialize(t(choices[index],"Name"),ownerId,owner,position,parent,light,p,handle,error);}
 // CreateAndInit 0x18029fb70: after the Random (type 5) resolution loop,
 // RequiresUnblocked tests the creation location (Vector2 param) with
 // GridManager::IsLocationBlocked 0x180307f70; blocked returns null. Every
 // factory reaches CreateAndInit: AnimationManager::Create 0x18029e3df/e4e5
 // (InitData CreateAnimations 0x2933b0/435, slide/interval VisualFx, OnDeath,
 // TakeHit, Thing::SetAnimation 0x2df358) and CreateCustom 0x29f946 (AddChild).
 // Approximation: the location tested is the position this model passes to
 // initialize for that factory.
 if(const auto*u=d.get("RequiresUnblocked");u&&u->boolean()){
  if(!p.locationBlocked){error="Actual GridManager::IsLocationBlocked provider unavailable";return false;}
  bool blocked=false;if(!p.locationBlocked(position[0],position[1],blocked,error))return false;
  if(blocked){handle=0;return true;}
 }
 const bool primary=primaryRequest_&&!parent&&!light;primaryRequest_=false;
 ActionEffectState s;s.name=name;s.definition=d;s.primary=primary;s.detached=owner.lifetime==0;s.guiLine=guiLineRequest_;guiLineRequest_=false;s.ownerId=ownerId;s.ownerLifetime=owner.lifetime;s.parent=parent;s.isLight=light;s.type=nativeType(d);s.movie=movies_.contains(name);s.imageClip=*(s.movie?movieBank_:bank_)->find(name);s.position=s.spawnPosition=position;s.ownerSnapshot=owner;if(t(d,"LocationZFromOwner")=="Ignore")s.position[2]=0;s.flipX=b(d,"FlipHorizontal");s.flipY=b(d,"FlipVertical");
 AnimationAllocation a;a.name=name;a.sourceDefinition=d;a.type=s.type;a.ownerId=light?0:ownerId;a.ownerLifetime=light?parent:owner.lifetime;a.manager=light?1:0;a.ownership=s.guiLine?AnimationOwnership::GuiComponent:(light?AnimationOwnership::LightController:(primary?AnimationOwnership::ThingPrimary:(parent?AnimationOwnership::Child:AnimationOwnership::ManagerCreate)));a.parent=light?0:parent;a.cancelOverCap=b(d,"CancelOverCap");a.loop=b(d,"Loop");a.onlyWhenVisible=b(d,"OnlyWhenVisible");if(!registry_->allocate(a,s.handle,error))return false;
 // InitData 0x180291dc6: HasDuration (Duration/DurationFrames/RandomDuration >0);
 // D = GetValue(max(DurationFrames/60,Duration),RandMin,RandMax).
 s.durationBound=n(d,"Duration")>0||n(d,"DurationFrames")>0||n(d,"RandomDurationMin")>0||n(d,"RandomDurationMax")>0;
 s.duration=std::max(n(d,"DurationFrames")/60.f,n(d,"Duration"));if(n(d,"RandomDurationMin")||n(d,"RandomDurationMax"))if(!rng(p,n(d,"RandomDurationMin"),n(d,"RandomDurationMax"),s.duration,error))return false;s.remaining=s.duration;
 // InitData 0x18029205a: GetValue291290(ShiftSpeed,ShiftSpeedMin,ShiftSpeedMax)
 // draws once unless both bounds are zero, after duration and before scale RNG.
 {float value=n(d,"ShiftSpeed");if(n(d,"ShiftSpeedMin")||n(d,"ShiftSpeedMax"))if(!rng(p,n(d,"ShiftSpeedMin"),n(d,"ShiftSpeedMax"),value,error))return false;if(value!=0||std::isnan(value))s.shiftFraction=value;}
 s.scaleVector={n(d,"ScaleX",1),n(d,"ScaleY",1)};s.ownScale=n(d,"Scale",1);if(n(d,"ScaleMin")||n(d,"ScaleMax")){float scale;if(!rng(p,n(d,"ScaleMin"),n(d,"ScaleMax"),scale,error))return false;s.ownScale*=scale;}
 if(t(d,"ScaleFromOwner")=="Take")s.ownScale*=owner.scale;
 // InitData 0x18029243c: alpha = GetValue(Alpha [ctor 1.0, 0x180094236], AlphaMin,
 // AlphaMax), drawn after the ScaleMin/Max and before the Angle RandomDouble.
 float alpha=n(d,"Alpha",1);if(n(d,"AlphaMin")||n(d,"AlphaMax"))if(!rng(p,n(d,"AlphaMin"),n(d,"AlphaMax"),alpha,error))return false;
 s.heading=rad(n(d,"Angle"));if(n(d,"AngleMin")||n(d,"AngleMax")){float angle;if(!rng(p,rad(n(d,"AngleMin")),rad(n(d,"AngleMax")),angle,error))return false;s.heading+=angle;}
 // InitData reads mAngle for RadialOffset before Init's SetValuesFromOwner
 // (0x291870 -> vfunc+0xe8 InitData, then 0x299ea0) adds a Take owner angle.
 const float initDataAngle=s.heading;
 if(t(d,"AngleFromOwner")=="Take"){
  // SetValuesFromOwner29a0fa..1af: only Take(1) enters; data+6b and
  // actual Thing PhysicsComponent+d8 select wrap(atan2(-vy,vx)), even
  // for zero velocity. No physics follows the native body-angle branch.
  float angle=owner.angle;if(b(d,"UseVelocityAngleFromOwner")&&owner.lifetime&&owner.velocity){const auto v=*owner.velocity;if(!std::isfinite(v[0])||!std::isfinite(v[1])){error="Actual action FX owner physics velocity invalid";return false;}angle=wrapAngle(std::atan2(-v[1],v[0]));}s.heading=wrapAngle(s.heading+angle);
 }
 if(n(d,"VelocityMin")||n(d,"VelocityMax"))if(!rng(p,n(d,"VelocityMin"),n(d,"VelocityMax"),s.speed,error))return false;s.acceleration=n(d,"Acceleration");if(n(d,"RandomAccelerationMin")||n(d,"RandomAccelerationMax"))if(!rng(p,n(d,"RandomAccelerationMin"),n(d,"RandomAccelerationMax"),s.acceleration,error))return false;s.gravity=n(d,"Gravity");
 // InitData 0x180292a58: GetValue(UpwardVelocity,Min,Max) always (RNG order),
 // stored as the UpwardVelocity component only when Gravity!=0.
 {float up=n(d,"UpwardVelocity");if(n(d,"UpwardVelocityMin")||n(d,"UpwardVelocityMax"))if(!rng(p,n(d,"UpwardVelocityMin"),n(d,"UpwardVelocityMax"),up,error))return false;if(s.gravity!=0)s.upwardVelocity=up;}
 s.speedComponent=s.speed!=0;s.upwardComponent=s.gravity!=0;s.rotateWithGravity=b(d,"RotateWithGravity");
 if(n(d,"AngleChangeSpeedMin")||n(d,"AngleChangeSpeedMax"))if(!rng(p,rad(n(d,"AngleChangeSpeedMin")),rad(n(d,"AngleChangeSpeedMax")),s.headingSpeed,error))return false;
 if(n(d,"RandomRotationSpeedMin")||n(d,"RandomRotationSpeedMax"))if(!rng(p,rad(n(d,"RandomRotationSpeedMin")),rad(n(d,"RandomRotationSpeedMax")),s.rotationSpeed,error))return false;
 if(b(d,"RandomRotation"))if(!rng(p,0,6.2831854820251465f,s.rotation,error))return false;
 s.rotationComponent=b(d,"RandomRotation")||s.rotationSpeed!=0||s.rotateWithGravity;
 // InitData 0x180292e8b..0x180292f3f: FlipHorizontal, else RandomFlipHorizontal
 // pcg32_bounded(2)==1; then FlipVertical, else RandomFlipVertical likewise.
 if(!s.flipX&&b(d,"RandomFlipHorizontal")){uint32_t coin;if(!p.bounded(2,coin,error)||coin>1)return false;s.flipX=coin==1;}
 if(!s.flipY&&b(d,"RandomFlipVertical")){uint32_t coin;if(!p.bounded(2,coin,error)||coin>1)return false;s.flipY=coin==1;}
 if(n(d,"RandomOffsetX"))if(!rng(p,-n(d,"RandomOffsetX"),n(d,"RandomOffsetX"),s.randomX,error))return false;if(n(d,"RandomOffsetY"))if(!rng(p,-n(d,"RandomOffsetY"),n(d,"RandomOffsetY"),s.randomY,error))return false;
 // InitData 0x180292fe8..0x1802930f8: RadialOffsetMin/Max (either nonzero):
 // r = RandomDouble x (max-min) + min along (cosf(mAngle), -(sinf(mAngle) x
 // RadialOffsetScaleY, ctor default 1)), added with the random offsets to the
 // Animation offset/attach offset. RadialOffsetScaleRadius is not admitted.
 if(n(d,"RadialOffsetMin")!=0||n(d,"RadialOffsetMax")!=0){const float sn=std::sin(initDataAngle),cs=std::cos(initDataAngle);const volatile float sy=sn*n(d,"RadialOffsetScaleY",1);float r;if(!rng(p,n(d,"RadialOffsetMin"),n(d,"RadialOffsetMax"),r,error))return false;const volatile float rx=r*cs,ry=r*-float(sy);s.randomX=rx+s.randomX;s.randomY=ry+s.randomY;}
 s.position[0]+=s.randomX;s.position[1]+=s.randomY;
 if(owner.lifetime&&!t(d,"UseAttachedMarkerLocation").empty()){std::array<float,3> marker;if(!p.marker(ownerId,t(d,"UseAttachedMarkerLocation"),marker,error))return false;s.position[0]=marker[0]+s.randomX;s.position[1]=marker[1]+s.randomY;}
 if(const auto*color=d.get("Color")){size_t channel=0;for(const auto*key:{"Red","Green","Blue","Alpha"}){s.baseColor[channel++]=uint8_t(std::clamp(int(n(*color,key,1)*255),0,255));}}
 // InitData/GetDrawColor 0x298810 (as the screen consumer): top-level
 // Red/Green/Blue set mColor only when all three are present and >=0;
 // Alpha scales mColor.a with truncation.
 if(d.get("Red")&&d.get("Green")&&d.get("Blue")&&n(d,"Red")>=0&&n(d,"Green")>=0&&n(d,"Blue")>=0){size_t k=0;for(const char*key:{"Red","Green","Blue"})s.baseColor[k++]=uint8_t(std::trunc(std::clamp(n(d,key),0.f,1.f)*255.f));}
 // 0x180292492..0x1802924e2 (unconditional): Color.a byte/255 x alpha x 255,
 // truncated and saturated, then BodyComponent::MultiplyColor.
 {const volatile float scaled=float(s.baseColor[3])/255.f*alpha;const float bytes=float(scaled)*255.f;const int v=int(bytes);s.baseColor[3]=uint8_t(std::clamp(v,0,255));}
 // InitData 0x180292459: body mHsv += (Hue,Saturation,Value,0); SetValuesFromOwner
 // 0x18029a259 adds the owner HSV the same way (addps).
 s.hsv[0]+=n(d,"Hue");s.hsv[1]+=n(d,"Saturation");s.hsv[2]+=n(d,"Value");
 if(t(d,"ColorFromOwner")=="Take"){for(size_t c=0;c<4;++c)s.baseColor[c]=uint8_t(uint32_t(s.baseColor[c])*(*owner.baseColor)[c]/255);for(size_t c=0;c<3;++c)s.hsv[c]+=(*owner.baseHsv)[c];}
 states_.emplace(s.handle,s);auto&actual=states_.at(s.handle);
 const auto child=t(d,"ChildAnimation");if(!child.empty()&&child!="null"){uint64_t h;if(!initialize(child,ownerId,owner,position,s.handle,false,p,h,error))return false;actual.child=h;}
 const auto add=[&](const std::string&dep)->bool{if(dep.empty()||dep=="null")return true;uint64_t h;if(!initialize(dep,ownerId,owner,position,0,false,p,h,error))return false;if(h)actual.created.push_back(h);return true;};
 if(!add(t(d,"CreateAnimation")))return false;if(auto*creates=d.get("CreateAnimations"))for(const auto&r:creates->array())if(!add(t(r,"Name")))return false;
 if(n(d,"VisualFxIntervalMin")||n(d,"VisualFxIntervalMax"))if(!rng(p,n(d,"VisualFxIntervalMin"),n(d,"VisualFxIntervalMax"),actual.timeRemaining,error))return false;
 if(n(d,"VisualFxDistanceMin")||n(d,"VisualFxDistanceMax"))if(!rng(p,n(d,"VisualFxDistanceMin"),n(d,"VisualFxDistanceMax"),actual.distanceRemaining,error))return false;
 // InitData29356d..35c2: data+2f8 is the fixed cap, +2fc/+300
 // are signed integer random bounds. Either nonzero selects min +
 // pcg32_bounded(max-min), excluding max; equal bounds consume no RNG.
 // UpdateVfxTimeInterval29520d and the distance path use this live cap.
 actual.visualFxCap=int32_t(n(d,"VisualFxCap"));
 {const int32_t lo=int32_t(n(d,"VisualFxCapMin")),hi=int32_t(n(d,"VisualFxCapMax"));if(lo||hi){if(hi<lo){error="Native VisualFxCap random bounds invalid";return false;}uint32_t offset=0;if(hi!=lo&&(!p.bounded(uint32_t(hi-lo),offset,error)||offset>=uint32_t(hi-lo)))return false;actual.visualFxCap=lo+int32_t(offset);}}
 const auto cue=t(d,"Sound");if(!cue.empty()&&p.worldFrame>=uint64_t(n(d,"SoundAllowedAfterFrame",2)))if(!p.sound(cue,ownerId,actual.position,uint32_t(n(d,"SoundManagerCap")),actual.sound,error))return false;
 const auto lightName=t(d,"Light");if(!lightName.empty()){uint64_t h;if(!initialize(lightName,ownerId,owner,position,s.handle,true,p,h,error))return false;actual.light=h;}
 actual.frame=n(d,"StartFrame",1);actual.playSpeed=n(d,"PlaySpeed",60);if(actual.type==NativeAnimationType::Book&&(n(d,"RandomPlaySpeedMin")||n(d,"RandomPlaySpeedMax")))if(!rng(p,n(d,"RandomPlaySpeedMin"),n(d,"RandomPlaySpeedMax"),actual.playSpeed,error))return false;
 if(actual.type==NativeAnimationType::Book){
  // BookAnimation::SetupFrameUpdateMode 0x1802b1040: no PlaySpeed driver and
  // a Duration -> CustomUpdateSystem frame from the time fraction; no
  // driver and no Duration -> DataPlaySpeed 60. Init 0x1802af310: backwards
  // books start at EndFrame.
  const bool noSpeed=n(d,"PlaySpeed")==0&&n(d,"RandomPlaySpeedMin")==0&&n(d,"RandomPlaySpeedMax")==0;
  actual.customFrames=noSpeed&&actual.durationBound;if(noSpeed&&!actual.durationBound)actual.playSpeed=60;
  actual.backwards=b(d,"PlayBackwards");if(actual.backwards)actual.frame=n(d,"EndFrame",n(d,"NumFrames"));
  // BookAnimation.Init VA1802af37d..3d5 uses bounded(end-start), excludes end.
  if(b(d,"RandomStartFrame")){const int32_t start=int32_t(n(d,"StartFrame",1)),end=int32_t(n(d,"EndFrame",n(d,"NumFrames")));if(end<start){error="Native random book frame bounds invalid";return false;}uint32_t offset=0;if(end!=start&&(!p.bounded||!p.bounded(uint32_t(end-start),offset,error)))return false;actual.frame=float(start+int32_t(offset));}
 }
 if(actual.type==NativeAnimationType::Slide){const auto&clip=bank_->clips()[actual.imageClip];actual.slide=0;actual.frame=float(clip.slides.front().originalFrame);actual.slideRemaining=float(clip.slides.front().ticks)/60;if(!slideFx(s.handle,p,error))return false;}
 // AddChild293ae0 passes the actual parent's skip-update byte (+62) to
 // CreateCustom. A child remains in that source ownership/update policy.
 const auto* parentLife=parent?registry_->find(parent):nullptr;
 if(parent&&!parentLife){error="Actual child parent update lifetime unavailable";return false;}
 if(!registry_->commitInitialization(s.handle,parentLife&&parentLife->skipUpdate,error))return false;
 if(!parent&&!primary&&!actual.guiLine){std::optional<uint64_t>victim;if(!registry_->addToManager(s.handle,80,victim,error))return false;}
 handle=s.handle;return true;
}
bool ActionEffectWorld::create(const std::string&name,int32_t owner,std::optional<std::array<float,3>>position,const ActionEffectProviders&p,uint64_t&handle,std::string&error,bool guiLine){try{if(mutating_||!prepare(name,owner,p,error,guiLine))return false;EffectOwnerState body;if(!p.owner(owner,body,error))return false;guiLineRequest_=guiLine;if(guiLine){body.scale=1;body.angle=0;body.z=0;}mutating_=true;const bool ok=initialize(name,owner,body,position.value_or(std::array<float,3>{body.x,body.y,body.z}),0,false,p,handle,error);mutating_=false;if(!ok)retainFault(error);return ok;}catch(const std::exception&e){mutating_=false;error=e.what();retainFault(error);return false;}}
bool ActionEffectWorld::createStandalone(const std::string&name,std::array<float,3>position,const ActionEffectProviders&p,uint64_t&handle,std::string&error){try{
 if(faulted_){error=fault_;return false;}if(mutating_||!registry_)throw std::runtime_error("Invalid standalone Animation factory state");std::vector<std::string>path;if(!preflight(name,p,path,error))return false;
 // AnimationManager.CreateCustom29f870 passes nullptr Thing into CreateAndInit.
 // The neutral body values below are native no-owner semantics; lifetime 0
 // records that no Thing exists, including for generated child animations.
 EffectOwnerState noOwner;noOwner.baseColor=std::array<uint8_t,4>{255,255,255,255};noOwner.baseHsv=std::array<float,4>{0,0,0,0};noOwner.group=std::string{};
 mutating_=true;const bool ok=initialize(name,0,noOwner,position,0,false,p,handle,error);mutating_=false;if(!ok)retainFault(error);return ok;
 }catch(const std::exception&e){const bool mutated=mutating_;mutating_=false;error=e.what();if(mutated)retainFault(error);return false;}}
// Animation.CopyState295bb0 normal source with no mCopyBody(+0xa8): location,
// Z/offset/parallax, Rotation, radius, PSM, attach offset, draw scale/vector/group,
// flip; Book frame transfer is the virtual consumer, child ownership remains gated.
bool ActionEffectWorld::copyState(uint64_t target,uint64_t source,const ActionEffectProviders&p,std::string&error){try{
 if(!source)return true;auto* dst=state(target);const auto* src=state(source);if(!dst||!src)throw std::runtime_error("Animation.CopyState source/target lifetime unavailable");
 const auto copied=*src;const auto& from=copied.definition;const auto& to=dst->definition;
 if(b(to,"TransferChildren")||b(from,"TransferChildren"))throw std::runtime_error("Animation.CopyState child ownership transfer not reconstructed");
 const bool transferFrame=b(to,"TransferFrameToThis")||b(from,"TransferFrameFromThis");
 if(transferFrame&&!transferableFrame(copied.frame))throw std::runtime_error("Animation.CopyState frame outside finite int32 range: "+copied.name+" -> "+dst->name);
 if(transferFrame&&dst->type!=NativeAnimationType::Book&&dst->type!=NativeAnimationType::Constant)throw std::runtime_error("Animation.CopyState frame transfer not reconstructed: "+copied.name+" ("+t(from,"Type")+") -> "+dst->name+" ("+t(to,"Type")+")");
 EffectOwnerState owner=copied.ownerSnapshot;if(!copied.detached&&(!p.owner||!p.owner(copied.ownerId,owner,error)))return false;
 std::array<float,3> position;if(!sourceLocation(copied,owner,&p.marker,position,error))return false;
 forChain(target,[&](ActionEffectState&s,bool first){s.position[0]=position[0];s.position[1]=position[1];s.parallax=copied.parallax;s.attachOffset=std::array<float,2>{copied.attachOffset?(*copied.attachOffset)[0]:copied.randomX,copied.attachOffset?(*copied.attachOffset)[1]:copied.randomY};if(first){s.ownZ=copied.ownZ;s.position[2]=copied.ownZ;s.addedOffsetZ=copied.addedOffsetZ;if(copied.rotationComponent){s.rotation=copied.rotation;s.rotationComponent=true;}s.playSpeedMultiplier=copied.playSpeedMultiplier;s.flipX=copied.flipX;s.flipY=copied.flipY;}});
 if(!setRadius(target,copied.scaleRadius,error))return false;
 // TransferDrawLayer ctor defaults true (+0x7b); source OR destination.
 const auto transfer=[](const SourceValue& d,const char* key,bool fallback){const auto* v=d.get(key);return v?v->boolean():fallback;};
 auto& actual=states_.at(target);// PDB BodyComponent+0x48=Scale, +0x60=ScaleVector; full Animation
 // +0x158/+0x170 (CopyState295dbc..5deb). TransferScale is data+0x7d.
 if(transfer(to,"TransferScale",true)||transfer(from,"TransferScale",true)){actual.ownScale=copied.ownScale;actual.scaleVector=copied.scaleVector;}
 if(transfer(to,"TransferDrawLayer",true)||transfer(from,"TransferDrawLayer",true))actual.runtimeGroup=copied.runtimeGroup;
 if(transferFrame&&!this->transferFrame(target,copied,error))return false;
 if(b(to,"TransferAngle")||b(from,"TransferAngle")){actual.heading=copied.heading;if(actual.child&&!addAngle(actual.child,copied.heading,error))return false;}
 return true;
 }catch(const std::exception&e){error=e.what();return false;}}
bool ActionEffectWorld::transferFrame(uint64_t target,const ActionEffectState&source,std::string&error){
 auto&actual=states_.at(target);
 // CopyState295e66..e83: destination ToThis OR source FromThis invokes
 // destination vfunc+50, self only. Book.TransferFrame2b014f..21b writes
 // source mFractionalFrame(+78) verbatim and AnimFrame{destination Start,
 // End, frame};2b021c..24c clamps only the displayed integer mFrame(+80).
 // visual already selects clamp(trunc(frame),Start,End). Preserve raw frame,
 // destination duration/fraction/frame driver, attachment and RNG stream.
 if(!b(actual.definition,"TransferFrameToThis")&&!b(source.definition,"TransferFrameFromThis"))return true;
 if(!transferableFrame(source.frame)){error="Animation.CopyState frame outside finite int32 range: "+source.name+" -> "+actual.name;return false;}
 if(actual.type==NativeAnimationType::Book){actual.frame=source.frame;return true;}
 if(actual.type==NativeAnimationType::Constant){
  // Constant vtable1805f8220 slot+50 -> Animation.TransferFrame2913b0:
  // source integer mFrame(+80), converted to float, then SetFractionalFrame.
  // Book mFrame is the clamped display integer; Constant stores the integer
  // written by SetFractionalFrame even when its single wrap stays out of range.
  float frame=std::trunc(source.frame);
  if(source.type==NativeAnimationType::Book)frame=std::clamp(frame,n(source.definition,"StartFrame",1),n(source.definition,"EndFrame",n(source.definition,"NumFrames",1)));
  // SetFractionalFrame295ff0..604d wraps once using End-Start (no +1),
  // writes raw fractional +78 and truncation to integer +80. Constant's
  // single image does not depend on either frame value. No duration reset.
  const float start=n(actual.definition,"StartFrame",1),end=n(actual.definition,"EndFrame",n(actual.definition,"NumFrames",1));
  if(frame<start)frame=end-(start-frame);else if(frame>end)frame=(frame-end)+start;
  actual.frame=frame;return true;
 }
 error="Animation.CopyState frame transfer not reconstructed: "+source.name+" ("+t(source.definition,"Type")+") -> "+actual.name+" ("+t(actual.definition,"Type")+")";return false;
}
bool ActionEffectWorld::slideFx(uint64_t handle,const ActionEffectProviders&p,std::string&error){auto&source=states_.at(handle);const auto&clip=bank_->clips().at(source.imageClip);const auto&step=source.definition.get("Slides")->array().at(clip.slides.at(source.slide).originalFrame-1);const auto fx=t(step,"VisualFx");if(fx.empty())return true;EffectOwnerState owner;if(!p.owner(source.ownerId,owner,error))return false;uint64_t h;if(!initialize(fx,source.ownerId,owner,source.position,0,false,p,h,error))return false;if(!h)return true;auto&spawned=states_.at(h);spawned.heading+=-1.5707963705062866f+source.heading;if(t(spawned.definition,"ScaleFromOwner")!="Ignore")spawned.ownScale*=source.ownScale*owner.scale;source.created.push_back(h);return true;}
bool ActionEffectWorld::advance(float dt,const ActionEffectProviders&p,std::string&error){try{
 if(faulted_){error=fault_;return false;}if(mutating_||!std::isfinite(dt)||dt<0||dt>1){error="Invalid/faulted original action FX world";return false;}const auto snapshot=handles(true);std::vector<uint64_t>dead;
 for(auto h:snapshot){auto& s=states_.at(h);auto*life=registry_->find(h);if(!life||!life->initialized){error="Actual action FX lifetime lost";return false;}
  // ShiftFractionSystem::Execute153390: phase+=rate*dt (rate stays 0), one wrap.
  if(s.shiftFraction){float f=*s.shiftFraction+0.f*dt;if(0.f>f)f=1.f-(0.f-f);else if(f>1.f)f=f-1.f+0.f;s.shiftFraction=f;}
  if(life->skipUpdate||s.done)continue;if(life->canceled){dead.push_back(h);continue;}const auto&d=s.definition;EffectOwnerState owner=s.ownerSnapshot;if(!s.detached){if(!p.owner(s.ownerId,owner,error)||owner.lifetime!=s.ownerLifetime)return false;s.ownerSnapshot=owner;}float elapsed=dt;if(p.elapsed&&!p.elapsed(s.ownerId,s.preferUnmodified?0:n(d,"TimeModifierFraction",-1),!s.detached&&life->attachedToTimeMultiplier,dt,elapsed,error))return false;if(!std::isfinite(elapsed)||elapsed<0||elapsed>1){error="Invalid actual action FX modified time";return false;}
 if(!s.detached&&(t(d,"LocationFromOwner").empty()||t(d,"LocationFromOwner")=="Maintain")){s.position[0]=owner.x+s.randomX;s.position[1]=owner.y+s.randomY;}if(!s.detached&&(t(d,"LocationZFromOwner").empty()||t(d,"LocationZFromOwner")=="Maintain"))s.position[2]=owner.z+s.ownZ;
 if(!s.detached&&!t(d,"UseAttachedMarkerLocation").empty()){std::array<float,3> marker;if(!p.marker(s.ownerId,t(d,"UseAttachedMarkerLocation"),marker,error))return false;s.position[0]=marker[0]+s.randomX;s.position[1]=marker[1]+s.randomY;}
 if(s.isLight&&state(s.parent))s.position=state(s.parent)->position;
 // TimeRemainingSystem 0x180189140: TR -= dt x PlaySpeedMultiplier.
 if(s.type==NativeAnimationType::ConstantNoUpdate)continue;if(s.durationBound)s.remaining-=elapsed*s.playSpeedMultiplier;s.rotation+=s.rotationSpeed*elapsed;s.upwardVelocity-=s.gravity*elapsed;s.ownZ+=s.upwardVelocity*elapsed;s.position[2]=s.ownZ+(!s.detached&&(t(d,"LocationZFromOwner").empty()||t(d,"LocationZFromOwner")=="Maintain")?owner.z:0);s.speed=std::max(0.f,s.speed+s.acceleration*elapsed);s.heading+=s.headingSpeed*elapsed;s.position[0]+=std::cos(s.heading)*s.speed*elapsed;s.position[1]-=std::sin(s.heading)*s.speed*elapsed;
 // RotateWithGravitySystem::RunJobs 0x180150ee0: three EntityManager queries
 // (AnimHandle, RotateWithGravity, Rotation + Speed/UpwardVelocity), jobs
 // 0x180151ac0 (both), 0x1801518a0 (Speed only), 0x180151740 (UpwardVelocity
 // only). a = Animation+0x164 (mAngle, per AddAngle 0x2982b0) = heading.
 // Result wrapped v - floor(v/2pi)*2pi. Approximation: runs after this
 // frame's motion integration; its slot in the native system order is not recovered.
 if(s.rotateWithGravity&&(s.speedComponent||s.upwardComponent)){constexpr float tau=6.2831854820251465f;float v;
  if(s.speedComponent&&s.upwardComponent)v=std::atan2(std::sin(s.heading)*s.speed+s.upwardVelocity,std::cos(s.heading)*s.speed);
  else if(s.speedComponent)v=std::atan2(std::sin(s.heading)*s.speed,std::cos(s.heading)*s.speed);
  else v=std::atan2(s.upwardVelocity,0.f);
  s.rotation=v-std::floor(v/tau)*tau;}
 // Animation::IsFinished: canceled || (!HoldLastFrame && mTimeRemaining<0).
 // AnimationFractionFromDurationSystem 0x180152ce0 for any animation with a Duration.
 if(s.durationBound)s.fraction=std::clamp(1-s.remaining/s.duration,0.f,1.f);
 const bool hold=b(d,"HoldLastFrame"),loop=b(d,"Loop"),framesOnly=b(d,"LoopFramesOnly");
 if(s.type==NativeAnimationType::Constant){
  // ConstantAnimation::UpdateInternal 0x1802b1bb0 / IsFinished.
  if(!hold&&s.durationBound&&s.remaining<0){if(loop||framesOnly){if(!loopAnimation(h,false,p,error))return false;}else dead.push_back(h);}
 }
 else if(s.type==NativeAnimationType::Book){
  // BookAnimation::UpdateInternal 0x1802af530.
  const float start=n(d,"StartFrame",1),end=n(d,"EndFrame",n(d,"NumFrames"));
  if(s.customFrames){const float f=s.backwards?1-s.fraction:s.fraction;s.frame=start+(end-start)*f;}
  else{
   // PlaySpeedUpdateSystem.Execute153b80 approaches a positive target;
   // Book.UpdateInternal2af530 then consumes the resulting fractional frame.
   const float step=s.playSpeed*s.playSpeedMultiplier*elapsed;
   if(s.frameTarget>0){if(step>=std::abs(s.frameTarget-s.frame))s.frame=s.frameTarget;else if(s.frameTarget>s.frame)s.frame+=step;else if(s.frameTarget<s.frame)s.frame-=step;}
   else s.frame+=(s.backwards?-1.f:1.f)*step;
  }
  if(!s.durationBound)s.fraction=std::clamp((s.frame-start+1)/(end-start+1),0.f,1.f);
  bool finished=false;
  if(s.durationBound&&s.remaining<0){if(loop){if(!loopAnimation(h,true,p,error))return false;}else if(!hold)finished=true;}
  if(!finished&&!hold&&((!s.backwards&&s.frame>=end+1)||(s.backwards&&s.frame<start))){if(loop||framesOnly){if(!loopAnimation(h,false,p,error))return false;}else finished=true;}
  if(hold)s.frame=std::clamp(s.frame,start,end);
  if(finished)dead.push_back(h);
 }
 else {if(!hold&&s.durationBound&&s.remaining<0){dead.push_back(h);continue;}s.slideRemaining-=elapsed;if(s.slideRemaining<.0001f){const auto&clip=bank_->clips()[s.imageClip];if(++s.slide>=clip.slides.size())dead.push_back(h);else{s.slideRemaining+=float(clip.slides[s.slide].ticks)/60;s.frame=float(clip.slides[s.slide].originalFrame);if(!slideFx(h,p,error))return false;}}s.fraction=std::clamp((s.frame-n(d,"StartFrame",1)+1)/(n(d,"EndFrame",n(d,"NumFrames"))-n(d,"StartFrame",1)+1),0.f,1.f);}
 }
 // Native Book/Constant.PostUpdate2b0870/2b1cb0 finishes descendants before
 // their parent's manager death, even when both finish on this update.
 const std::set<uint64_t> completed(dead.begin(),dead.end());std::set<uint64_t> visited;std::vector<uint64_t> postorder;
 std::function<void(uint64_t)> finishOrder=[&](uint64_t h){if(!visited.insert(h).second)return;const auto* s=state(h);if(s&&s->child)finishOrder(s->child);if(completed.contains(h))postorder.push_back(h);};
 for(auto h:dead)finishOrder(h);dead=std::move(postorder);
 for(auto h:dead)if(state(h)){const auto old=*state(h);
  if(old.parent&&!old.isLight){
   // Animation.OnChildFinished295780 releases its finished child, then
   // AddChild293ae0 creates ChainTo UNDER THE SAME PARENT. There is no
   // manager Add, CopyState, or OnDeath/CreateAnimationOnEnd in this path.
   const auto* actualParent=state(old.parent);if(!actualParent||actualParent->child!=h){error="Native finished child parent lifetime/link unavailable";return false;}const auto parent=*actualParent;
   const auto chain=t(old.definition,"ChainTo");
   if(!chain.empty()&&chain!="null"&&parent.detached){error="Native finished child of detached parent needs ownerless lineage factory";return false;}
   if(n(parent.definition,"ZWobbleSpeed")!=0||n(parent.definition,"ZWobbleDistance")!=0){error="Native OnChildFinished parent WobbleOffsetZ component copy unavailable";return false;}
   uint64_t transferred=0;if(b(old.definition,"TransferChildren")&&old.child){transferred=old.child;if(!registry_->reparentChild(transferred,0,error))return false;states_.at(transferred).parent=0;states_.at(h).child=0;}
   states_.at(parent.handle).child=0;if(!destroy(h,error))return false;
   uint64_t replacement=0;
   if(!chain.empty()&&chain!="null"){
    EffectOwnerState owner=parent.ownerSnapshot;if(!p.owner(parent.ownerId,owner,error)||owner.lifetime!=parent.ownerLifetime)return false;
    if(!initialize(chain,parent.ownerId,owner,parent.position,parent.handle,false,p,replacement,error))return false;
    if(!replacement){error="Native OnChildFinished AddChild returned no replacement allocation";return false;}
    states_.at(parent.handle).child=replacement;
    const float x=parent.attachOffset?(*parent.attachOffset)[0]:parent.randomX,y=parent.attachOffset?(*parent.attachOffset)[1]:parent.randomY;
    if(!setAttachOffset(replacement,x,y,error))return false;
    forChain(replacement,[&](ActionEffectState& s,bool){s.parallax=parent.parallax;});
    // EntityManager.CopyComponent(WobbleOffsetZ) copies absence here: nonzero
    // wobble data is rejected above rather than silently losing a component.
   }
   if(transferred){uint64_t tail=replacement?replacement:parent.handle;while(state(tail)&&state(tail)->child)tail=state(tail)->child;if(!state(tail)){error="Native transferred child tail lifetime missing";return false;}if(!registry_->reparentChild(transferred,tail,error))return false;states_.at(tail).child=transferred;states_.at(transferred).parent=tail;}
   continue;
  }
  // Thing.UpdateAnimation2df5c0 owns ThingPrimary completion. Keep the
  // completed allocation alive until its actual Thing selects ChainTo or
  // ThingData.Graphic through SetAnimation (CopyState then Release). It must
  // never become a manager-created animation or leave the Thing dangling.
  if(old.primary){states_.at(h).done=true;continue;}
  // AnimationManager.Update29d479 invokes OnDeath295a20 before ChainTo and
  // Release, including canceled animations. OnDeath creates the authored
  // CreateAnimationOnEnd through this manager at GetLocation (XY only),
  // attached to the same not-disposed Thing; no CopyState/chain transfers.
  // Manager reset uses direct disposal and intentionally does not run this.
  const auto onEnd=t(old.definition,"CreateAnimationOnEnd");
  if(!onEnd.empty()&&onEnd!="null"){
   EffectOwnerState owner;std::array<float,3> location{old.position[0],old.position[1],0};
   if(!old.detached){if(!p.owner(old.ownerId,owner,error)||owner.lifetime!=old.ownerLifetime)return false;
    if(!sourceLocation(old,owner,&p.marker,location,error))return false;
   }else{owner.baseColor=std::array<uint8_t,4>{255,255,255,255};owner.baseHsv=std::array<float,4>{0,0,0,0};owner.group=std::string{};}
   location[2]=0;uint64_t spawned;if(!initialize(onEnd,old.detached?0:old.ownerId,owner,location,0,false,p,spawned,error))return false;
  }
  const auto chain=t(old.definition,"ChainTo");if(!chain.empty()&&chain!="null"&&!old.preventChain){EffectOwnerState owner=old.ownerSnapshot;if(!old.detached&&!p.owner(old.ownerId,owner,error))return false;uint64_t next;if(!initialize(chain,old.ownerId,owner,old.position,0,false,p,next,error))return false;if(next){/* AnimationManager.Update29d614 invokes CopyState after Init. */if(!transferFrame(next,old,error))return false;auto&replacement=states_.at(next);replacement.detached=old.detached;replacement.ownerSnapshot=owner;replacement.preferUnmodified=old.preferUnmodified;replacement.position=old.position;replacement.ownZ=old.ownZ;replacement.rotation=old.rotation;if(b(old.definition,"TransferCopyBody")||b(replacement.definition,"TransferCopyBody")||(!old.definition.get("TransferCopyBody")&&!replacement.definition.get("TransferCopyBody"))){replacement.heading=old.heading;replacement.ownScale=old.ownScale;replacement.baseColor=old.baseColor;replacement.hsv=old.hsv;replacement.flipX=old.flipX;replacement.flipY=old.flipY;}const auto transferLayer=[](const SourceValue&v){const auto*p=v.get("TransferDrawLayer");return !p||p->boolean();};if(transferLayer(old.definition)||transferLayer(replacement.definition))replacement.runtimeGroup=old.runtimeGroup;}}if(!destroy(h,error))return false;}
 return true;
 }catch(const std::exception&e){error=e.what();retainFault(error);return false;}}
bool ActionEffectWorld::updateTime(float dt,const ActionEffectProviders&p,uint32_t& spawnedThisFrame,std::string&error){try{
 // Explicit main-thread TimeVFX phase after component motion/frame updates.
 if(faulted_){error=fault_;return false;}if(mutating_||!registry_||!std::isfinite(dt)||dt<0||dt>1){error="Invalid/faulted action FX Time phase";return false;}
 const auto spawners=handles();for(auto h:spawners){auto&s=states_.at(h);const auto* life=registry_->find(h);if(!life||!life->initialized){error="Actual action Time lifetime unavailable";return false;}if(life->skipUpdate||life->canceled)continue;const auto&d=s.definition;if(t(d,"VisualFx").empty()||t(d,"VisualFx")=="null"||spawnedThisFrame>=60)continue;float elapsed=dt;if(p.elapsed&&!p.elapsed(s.ownerId,s.preferUnmodified?0:n(d,"TimeModifierFraction",-1),!s.detached&&life->attachedToTimeMultiplier,dt,elapsed,error))return false;if(!std::isfinite(elapsed)||elapsed<0||elapsed>1){error="Invalid action Time elapsed";return false;}s.timeRemaining=std::max(s.timeRemaining-elapsed,-n(d,"VisualFxIntervalMax"));if(s.timeRemaining>=0||(s.visualFxCap>0&&s.totalSpawned>=uint32_t(s.visualFxCap)))continue;if(n(d,"VisualFxManagerCap")>0&&registry_->managerCount(0)>=size_t(n(d,"VisualFxManagerCap")*4))continue;if(n(d,"ActiveVisualFxCap")>0&&activeVfxCount(h)>=uint32_t(n(d,"ActiveVisualFxCap")))continue;EffectOwnerState owner=s.ownerSnapshot;if(!s.detached&&!p.owner(s.ownerId,owner,error))return false;std::array<float,3>location;if(!sourceLocation(s,owner,&p.marker,location,error))return false;EffectOwnerState initialOwner=owner;if(s.detached){initialOwner.velocity.reset();initialOwner.scale=1;initialOwner.angle=0;initialOwner.baseColor=std::array<uint8_t,4>{255,255,255,255};initialOwner.baseHsv=std::array<float,4>{0,0,0,0};}uint64_t spawned;if(!initialize(t(d,"VisualFx"),s.ownerId,initialOwner,location,0,false,p,spawned,error))return false;if(!spawned)continue;auto&leaf=states_.at(spawned);leaf.detached=s.detached;leaf.preferUnmodified=s.preferUnmodified;leaf.ownerSnapshot=owner;// UpdateVfxTimeInterval29538f..542c + SetupSpawned294e4f..ec5:
 // default VisualFxAngleOffset=0; inherit own angle only when requested,
 // then source mAngle unless spawned AngleFromOwner=Ignore.
 leaf.heading+=radD(n(d,"VisualFxAngleOffset"))+(b(d,"VisualFxInheritAngle")?s.heading:0.f);if(t(leaf.definition,"AngleFromOwner")!="Ignore")leaf.heading+=s.heading;if(t(leaf.definition,"ScaleFromOwner")!="Ignore")leaf.ownScale*=s.ownScale*((s.detached||t(s.definition,"ScaleFromOwner")=="Ignore")?1.f:owner.scale);s.created.push_back(spawned);s.vfx.push_back(spawned);++s.totalSpawned;++spawnedThisFrame;float interval;if(!rng(p,n(d,"VisualFxIntervalMin"),n(d,"VisualFxIntervalMax"),interval,error))return false;s.timeRemaining+=interval;}
 return true;
 }catch(const std::exception&e){error=e.what();retainFault(error);return false;}}

bool ActionEffectWorld::refreshOwner(int32_t id,const ActionEffectProviders&p,std::string&error){try{
 EffectOwnerState owner;if(!p.owner||!p.owner(id,owner,error))return false;
 for(auto&[h,s]:states_)if(s.ownerId==id&&!s.detached&&!s.guiLine){if(s.ownerLifetime!=owner.lifetime)throw std::runtime_error("Action owner transform lifetime mismatch");s.ownerSnapshot=owner;const auto&d=s.definition;
  if(t(d,"LocationFromOwner").empty()||t(d,"LocationFromOwner")=="Maintain"){s.position[0]=owner.x+s.randomX;s.position[1]=owner.y+s.randomY;}
  if(t(d,"LocationZFromOwner").empty()||t(d,"LocationZFromOwner")=="Maintain")s.position[2]=owner.z+s.ownZ;
  if(!t(d,"UseAttachedMarkerLocation").empty()){std::array<float,3> marker;if(!p.marker||!p.marker(id,t(d,"UseAttachedMarkerLocation"),marker,error))return false;s.position[0]=marker[0]+s.randomX;s.position[1]=marker[1]+s.randomY;}}
 error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}
// Animation.UpdateVfxDistanceInterval2954a0 / SpawnVfxDistanceInterval294680.
// Same native linear branch as EffectAnimationWorld's Distance ECS consumer;
// original distance accumulator, source interval RNG and actual emitted frames.
bool ActionEffectWorld::updateDistance(const ActionEffectProviders&p,uint32_t& spawnedThisFrame,std::string&error){try{
 if(faulted_){error=fault_;return false;}if(mutating_||!registry_){error="Invalid/faulted action Distance phase";return false;}
 for(auto h:handles()){auto&s=states_.at(h);const auto&d=s.definition;const auto*life=registry_->find(h);
  if(!life||!life->initialized)throw std::runtime_error("Action Distance animation lifetime unavailable");
  if(s.guiLine||life->skipUpdate||life->canceled||t(d,"VisualFx").empty()||t(d,"VisualFx")=="null"||(n(d,"VisualFxDistanceMin")==0&&n(d,"VisualFxDistanceMax")==0))continue;
  if((s.visualFxCap>0&&s.totalSpawned>=uint32_t(s.visualFxCap))||spawnedThisFrame>=60)continue;
  if(n(d,"VisualFxManagerCap")>0&&registry_->managerCount(0)>=size_t(n(d,"VisualFxManagerCap")*4))continue;
  if(n(d,"ActiveVisualFxCap")>0&&activeVfxCount(h)>=uint32_t(n(d,"ActiveVisualFxCap")))continue;
  EffectOwnerState owner=s.ownerSnapshot;if(!s.detached&&(!p.owner||!p.owner(s.ownerId,owner,error)))return false;
  std::array<float,3> location;if(!sourceLocation(s,owner,&p.marker,location,error))return false;
  location[2]+=n(d,"OffsetZ")+s.addedOffsetZ+shift(d,"StartOffsetZ","EndOffsetZ",s.shiftFraction.value_or(s.fraction));
  const auto marker=t(d,"VisualFxUseAttachedMarkerLocation");if(!marker.empty()&&owner.hasPrimaryAnimation&&(!p.marker||!p.marker(s.ownerId,marker,location,error)))return false;
  float delta=0;if(s.previousVfxLocation[0]!=0||s.previousVfxLocation[1]!=0){const float dx=location[0]-s.previousVfxLocation[0],dy=location[1]-s.previousVfxLocation[1];delta=std::min(std::sqrt(dx*dx+dy*dy),1920.f);if(delta>=n(d,"VisualFxDistanceMinChange"))s.distanceRemaining-=delta;}
  uint32_t emitted=0;
  while(s.distanceRemaining<0){if((s.visualFxCap>0&&s.totalSpawned>=uint32_t(s.visualFxCap))||(n(d,"VisualFxFrameCap")>0&&emitted>=uint32_t(n(d,"VisualFxFrameCap")))||spawnedThisFrame>=60||(n(d,"ActiveVisualFxCap")>0&&activeVfxCount(h)>=uint32_t(n(d,"ActiveVisualFxCap"))))break;
   const float fraction=delta>0?std::clamp(1-std::abs(s.distanceRemaining/delta),0.f,1.f):0;
   std::array<float,3> position;for(size_t k=0;k<3;++k)position[k]=(location[k]-s.previousVfxLocation[k])*fraction+s.previousVfxLocation[k];
   EffectOwnerState initialOwner=owner;if(s.detached){initialOwner.velocity.reset();initialOwner.scale=1;initialOwner.angle=0;initialOwner.baseColor=std::array<uint8_t,4>{255,255,255,255};initialOwner.baseHsv=std::array<float,4>{0,0,0,0};}
   uint64_t spawned=0;if(!initialize(t(d,"VisualFx"),s.ownerId,initialOwner,position,0,false,p,spawned,error))return false;
   if(spawned){auto&leaf=states_.at(spawned);leaf.detached=s.detached;leaf.preferUnmodified=s.preferUnmodified;leaf.ownerSnapshot=owner;leaf.ownZ=position[2];leaf.position[2]=position[2];
    const float angle=radD(n(d,"VisualFxAngleOffset"))+(b(d,"VisualFxInheritAngle")?s.heading:0.f);leaf.heading+=angle;
    if(t(leaf.definition,"AngleFromOwner")!="Ignore")leaf.heading+=s.heading;
    if(t(leaf.definition,"ScaleFromOwner")!="Ignore")leaf.ownScale*=s.ownScale*((s.detached||t(d,"ScaleFromOwner")=="Ignore")?1.f:owner.scale);
    s.created.push_back(spawned);s.vfx.push_back(spawned);}
   ++s.totalSpawned;++emitted;++spawnedThisFrame;float interval;if(!rng(p,n(d,"VisualFxDistanceMin"),n(d,"VisualFxDistanceMax"),interval,error))return false;s.distanceRemaining+=interval;
  }
  s.previousVfxLocation=location;
 }
 return true;
 }catch(const std::exception&e){error=e.what();retainFault(error);return false;}}
bool ActionEffectWorld::destroy(uint64_t h,std::string&error){auto i=states_.find(h);if(i==states_.end()){error="Actual action FX disposal handle unavailable";return false;}
 // Animation::Dispose 0x1802996c5: StopSoundOnFinishFade>0 -> AudioManager::StopCue(mSound, fade).
 if(const float fade=n(i->second.definition,"StopSoundOnFinishFade");fade>0&&i->second.sound){if(!stopSound_){error="Actual action FX StopCue fade provider unavailable";return false;}if(!stopSound_(i->second.sound,fade,error))return false;i->second.sound=0;}const auto child=i->second.child,light=i->second.light;if(child&&state(child)&&!destroy(child,error))return false;if(light&&state(light)&&!destroy(light,error))return false;if(!registry_->dispose(h,error))return false;if(i->second.primary)retiredPrimaries_.insert(h);states_.erase(h);return true;}
bool ActionEffectWorld::configureCreated(uint64_t h,float rawZ,const std::string&group,bool prefer,std::string&error){auto i=states_.find(h);if(i==states_.end()||!std::isfinite(rawZ)||group.size()>4096){error="Actual action factory post-init handle/fields unavailable";return false;}i->second.ownZ=rawZ;i->second.position[2]=rawZ;i->second.runtimeGroup=group;for(auto*current=&i->second;current;current=current->child&&state(current->child)?&states_.at(current->child):nullptr)if(prefer&&n(current->definition,"TimeModifierFraction",-1)<0)current->preferUnmodified=true;return true;}
bool ActionEffectWorld::preDisposeOwner(int32_t id,const ActionEffectProviders&p,std::string&error){EffectOwnerState owner;if(!p.owner||!p.owner(id,owner,error))return false;return preDisposeOwner(id,owner,error);}
bool ActionEffectWorld::preDisposeOwner(int32_t id,const EffectOwnerState&owner,std::string&error){try{if(!owner.lifetime||!owner.baseColor||!owner.baseHsv){error="Actual action pre-dispose body/color lifetime unavailable";return false;}std::vector<uint64_t>dead;for(auto&[h,s]:states_){if(s.ownerId!=id||s.detached||s.guiLine)continue;if(s.ownerLifetime!=owner.lifetime){error="Actual action attachment owner lifetime mismatch";return false;}if(b(s.definition,"DieWithOwner")){dead.push_back(h);continue;}/* OnAttachedToPreDispose29a2f6..323 snapshots Thing.GetLocation for Maintain, without a marker branch;29a4ef clears attached Thing/body. GetLocation296c90 then uses this own location even when a marker name remains in data. */if(t(s.definition,"LocationFromOwner").empty()||t(s.definition,"LocationFromOwner")=="Maintain"){s.position[0]=owner.x;s.position[1]=owner.y;}if(t(s.definition,"ScaleFromOwner").empty()||t(s.definition,"ScaleFromOwner")=="Maintain")s.ownScale*=owner.scale;if(t(s.definition,"AngleFromOwner").empty()||t(s.definition,"AngleFromOwner")=="Maintain")s.heading+=owner.angle;s.ownerSnapshot=owner;s.detached=true;}for(auto h:dead)if(state(h)&&!destroy(h,error))return false;return true;}catch(const std::exception&e){error=e.what();return false;}}
std::vector<uint64_t> ActionEffectWorld::handles(bool lights)const{std::vector<uint64_t>out;for(const auto&[h,s]:states_)if((lights||!s.isLight)&&!s.done)out.push_back(h);return out;}
bool ActionEffectWorld::visual(uint64_t h,const EffectOwnerState&owner,const std::array<uint8_t,4>&ownerFraction,ActorVisual&out,std::string&error){try{
 const auto*s=state(h);if(!s||s->done||!registry_->find(h)||owner.lifetime!=s->ownerLifetime){error="Actual action FX draw lifetime unavailable";return false;}const auto&d=s->definition;
 const auto imageBank=s->movie?movieBank_:bank_;if(!imageBank){error="Actual action Movie/image bank unavailable: "+s->name;return false;}const auto&clip=imageBank->clips().at(s->imageClip);
 size_t slide=0;const float originalFrame=std::clamp(std::trunc(s->frame),n(d,"StartFrame",1),n(d,"EndFrame",n(d,"NumFrames",1)));
 if(s->movie){
  const auto found=std::lower_bound(clip.slides.begin(),clip.slides.end(),uint32_t(originalFrame),[](const ActorAnimationSlide& value,uint32_t frame){return value.originalFrame<frame;});
  if(found==clip.slides.end()||found->originalFrame!=uint32_t(originalFrame)){error="Actual Movie Book source frame was not exported: "+s->name+" frame="+std::to_string(originalFrame);return false;}slide=size_t(found-clip.slides.begin());
 }else slide=s->type==NativeAnimationType::Slide?s->slide:size_t(originalFrame)-1;
 if(slide>=clip.slides.size()){error="Actual action FX frame outside resource: "+s->name+" type="+std::to_string(int(s->type))+" frame="+std::to_string(s->frame)+" index="+std::to_string(slide)+" exported="+std::to_string(clip.slides.size());return false;}
 // Book.PrepDraw2af9a6 uses full native GetAngleNum. Movie coverage retains
 // selected original angles; use the bank's documented nearest-angle mapping.
 const uint32_t nativeAngle=clip.originalAngles>1?angleNum(*s,owner):0;
 const uint32_t angleIndex=s->movie?imageBank->selectedAngleFromOriginal(s->imageClip,nativeAngle):nativeAngle;
 out.id=uint32_t(s->ownerId);out.lifetime=h;out.sourceBank=imageBank.get();out.frame=imageBank->sample(s->imageClip,slide,angleIndex,false);out.localRect=out.frame.worldRect(clip);
 if(s->movie){
  // HAA clip geometry already contains Animation.Scale/BinkAtlas.scale and
  // Animation.OffsetX/Y. Remove source Scale and offsets, preserving inverse
  // atlas scale. The common native Animation draw pipeline below applies each
  // source/live scale and attached-body draw offset exactly once.
  const float sourceScale=n(d,"Scale",1);if(sourceScale<=0){error="Actual Movie geometry source scale unavailable";return false;}
  out.localRect[0]=(out.localRect[0]-clip.offsetX)/sourceScale;out.localRect[1]=(out.localRect[1]-clip.offsetY)/sourceScale;out.localRect[2]/=sourceScale;out.localRect[3]/=sourceScale;
  if(n(d,"OriginX")!=0||n(d,"OriginY")!=0){error="Movie Book explicit origin requires original atlas extent/trim metadata";return false;}
  // The packet keeps the original midpoint pivot (including odd-canvas half
  // pixels). Mirror cropped geometry about that pivot as well as its UVs.
  if(s->flipX)out.localRect[0]=-out.localRect[0]-out.localRect[2];if(s->flipY)out.localRect[1]=-out.localRect[1]-out.localRect[3];
 }
 std::array<float,3> location;if(!sourceLocation(*s,owner,nullptr,location,error))return false;out.x=location[0];out.y=location[1];
 // GetZLocation297f50 adds OffsetZ + ZShift, unscaled.
 out.z=s->position[2]+n(d,"OffsetZ")+s->addedOffsetZ+shift(d,"StartOffsetZ","EndOffsetZ",s->shiftFraction.value_or(s->fraction));out.scale=s->ownScale*scaleHelper(d,"StartScale","EndScale",s->fraction);if(!s->detached&&(t(d,"ScaleFromOwner").empty()||t(d,"ScaleFromOwner")=="Maintain"))out.scale*=owner.scale;
 // Animation::GetScale 0x180298bb0: x mScaleRadius/def.ScaleRadius when both >0.
 if(s->scaleRadius>0&&n(d,"ScaleRadius")>0)out.scale*=s->scaleRadius/n(d,"ScaleRadius");out.angle=s->rotation-((d.get("UseOwnAngle")&&!b(d,"UseOwnAngle"))?0.f:s->heading+angleModifier(d,s->fraction));if(!s->detached&&maintainsOwnerAngle(d))out.angle-=owner.angle;
 // GetDrawAngle 0x1802980b0: NumAngles>1 draws the selected angle image unrotated.
 if(clip.originalAngles>1)out.angle=0;
 // BuildMesh239f75..9a passes angle (+pi/2 for Y), atan(.5), and
 // RotationZ to IsometricSkewAndScale322b80. PostRotateScaleVector(+6c)
 // is passed only to the ordinary RotateAndScale branch239fa1..ab;
 // it is ignored on this native isometric path, including authored .5 Y.
 const auto skew=t(d,"IsometricSkew");const bool isometric=!skew.empty()&&skew!="None";if(isometric){if((skew!="X"&&skew!="Y")||b(d,"RandomRotationZ"))throw std::runtime_error("Unsupported native action isometric transform branch: "+s->name);const float a=out.angle+(skew=="Y"?1.5707963705062866f:0.f);out.meshTransform={std::cos(a),0,std::tan(std::sin(a)*.46364760398864746f),1};out.angle=0;}
 // Animation.GetDrawLocation297d60 adds mOffset before the world
 // anchor. The attached Body scale (including its live fraction) scales
 // this offset independently of the animation own Scale/ScaleFromOwner.
 // Selected source has default ParallaxAmount1 and no attached animation
 // offset accumulation. Detached animations have no attached Body here.
 const float offsetScale=s->detached?1.f:owner.scale;
 // Draw offsets (+90) are independent of raw attachment offsets (+a0).
 // GetDrawLocation297e31 reads those source draw fields even after a live
 // SetAttachOffset or a child-chain replacement copied the parent attachment.
 const float ox=n(d,"OffsetX"),oy=n(d,"OffsetY");
 // GetDrawLocation 0x180297d60: (Shift + offset) x attached body scale.
 out.x+=(shift(d,"StartOffsetX","EndOffsetX",s->fraction)+ox)*offsetScale;out.y+=(shift(d,"StartOffsetY","EndOffsetY",s->fraction)+oy)*offsetScale;
 out.screenSpace=s->useScreenLocation;out.parallax=s->parallax;out.flipX=s->flipX;out.flipY=s->flipY;out.postRotateScaleX=isometric?1.f:n(d,"PostRotateScaleX",1);out.postRotateScaleY=isometric?1.f:n(d,"PostRotateScaleY",1);
 if(!s->movie){
 // Book.PrepDraw2af9a6 selects atlas/texture using displayed integer mFrame,
 // not raw fractional +78. TransferFrame may retain raw values beyond this
 // destination's range, so geometry must use the same clamped frame as pixels.
 const auto* resource=catalog_.get("Clips");const auto&ratios=resource->array().at(s->imageClip).get("atlas_ratio_by_frame")->array().at(size_t(angleIndex)*size_t(n(d,"NumFrames",1))+size_t(originalFrame)-1).array();const float rx=float(ratios[0].number()),ry=float(ratios[1].number());for(size_t k=0;k<4;++k)out.localRect[k]*=k%2?ry:rx;
 // Explicit source origin replaces native input origin before trim/flip math.
 const auto* metadata=catalog_.get("FrameMetadata");if(!metadata){error="Original action atlas pivot metadata companion unavailable";return false;}const auto& atlas=*metadata->array().at(out.frame.packetFrameIndex).get("sprite");
 const auto&original=atlas.get("original")->array();const auto&trim=atlas.get("trim")->array();const float ratiosXY[]{rx,ry};const bool flips[]{s->flipX,s->flipY};const float sizes[]{float(out.frame.width),float(out.frame.height)};
 for(size_t axis=0;axis<2;++axis){const auto* key=axis?"OriginY":"OriginX";float origin=n(d,key,float(original[axis].number())*.5f)/ratiosXY[axis]-float(trim[axis].number());if(flips[axis])origin-=std::trunc(float(original[axis].number())/ratiosXY[axis]-sizes[axis]-2*float(trim[axis].number()));out.localRect[axis]=-origin*ratiosXY[axis];}
 }
 // GetDrawScaleVector 0x180298500: body scale vector x StartScaleX/Y..EndScaleX/Y
 // transition helpers. Maintain ScaleVectorFromOwner(0) multiplies the attached Body.
 const float sx=s->scaleVector[0]*scaleHelper(d,"StartScaleX","EndScaleX",s->fraction),sy=s->scaleVector[1]*scaleHelper(d,"StartScaleY","EndScaleY",s->fraction);
 const bool inheritVector=!s->detached&&(t(d,"ScaleVectorFromOwner").empty()||t(d,"ScaleVectorFromOwner")=="Maintain");
 const float vx=sx*(inheritVector?owner.scaleVector[0]:1.f),vy=sy*(inheritVector?owner.scaleVector[1]:1.f);
 out.localRect[0]*=vx;out.localRect[2]*=vx;out.localRect[1]*=vy;out.localRect[3]*=vy;
 const char*starts[]{"StartRed","StartGreen","StartBlue","StartAlpha"};const char*ends[]{"EndRed","EndGreen","EndBlue","EndAlpha"};const bool inherit=!s->detached&&(t(d,"ColorFromOwner").empty()||t(d,"ColorFromOwner")=="Maintain"||t(d,"ColorFromOwner")=="Multiply");if(inherit&&!owner.baseColor){error="Actual action FX inherited packed owner color unavailable";return false;}
 for(size_t c=0;c<4;++c){uint32_t channel=s->baseColor[c];channel=channel*uint32_t(std::clamp(int((b(d,"PingPongColor")?(s->fraction<.5f?transition(d,starts[c],ends[c],s->fraction*2,1):transition(d,ends[c],starts[c],(s->fraction-.5f)*2,1)):transition(d,starts[c],ends[c],s->fraction,1))*255),0,255))/255;// GetDrawColor 0x180298810: MaintainAlphaOnly multiplies only alpha by the owner.
 if(inherit&&(c==3||!b(d,"MaintainAlphaOnly"))){channel=channel*(*owner.baseColor)[c]/255;channel=channel*ownerFraction[c]/255;}out.color[c]=float(channel)/255;}out.addColor=b(d,"AddColor");out.hsv=s->hsv;return true;
 }catch(const std::exception&e){error=e.what();return false;}}
bool ActionEffectWorld::createPrimary(const std::string&name,int32_t owner,const ActionEffectProviders&p,uint64_t&handle,std::string&error){
 if(auto i=definitions_.find(name);i!=definitions_.end()&&b(i->second,"CancelOnOwnerMove")){EffectOwnerState body;if(!p.owner||!p.owner(owner,body,error))return false;if(body.isUnit){error="Action primary CancelOnOwnerMove requires native Unit movement-animation policy: "+name;return false;}}
 primaryRequest_=true;const bool ok=create(name,owner,std::nullopt,p,handle,error);primaryRequest_=false;return ok;}
std::vector<uint64_t> ActionEffectWorld::completedPrimaries()const{
 std::vector<uint64_t> result;for(const auto&[h,s]:states_)if(s.primary&&s.done)result.push_back(h);return result;
}
void ActionEffectWorld::forChain(uint64_t h,const std::function<void(ActionEffectState&,bool)>&f){bool first=true;for(auto i=states_.find(h);i!=states_.end();i=i->second.child?states_.find(i->second.child):states_.end()){f(i->second,first);first=false;}}
bool ActionEffectWorld::setScale(uint64_t h,float scale,std::string&error){if(!state(h)||!std::isfinite(scale)){error="SetAnimation Scale target is not a live action Animation";return false;}forChain(h,[&](ActionEffectState&s,bool){s.ownScale=scale;});return true;}
bool ActionEffectWorld::setDuration(uint64_t h,float d,std::string&error){
 // Animation::SetDuration: ECS Duration/TimeRemaining and mTimeRemaining on self and child chain.
 if(!state(h)||!std::isfinite(d)){error="SetDuration target is not a live action Animation";return false;}
 forChain(h,[&](ActionEffectState&s,bool){s.duration=s.remaining=d;s.durationBound=true;});return true;}
bool ActionEffectWorld::setAttachOffset(uint64_t h,float x,float y,std::string&error){if(!state(h)||!std::isfinite(x)||!std::isfinite(y)){error="Script attach offset target unavailable";return false;}forChain(h,[&](ActionEffectState&s,bool){s.attachOffset=std::array<float,2>{x,y};});return true;}
// AnimationManager::Cancel 0x29e520: each manager animation whose data has
// CanCancelAsAttached (+0x9e; AnimationData ctor default 1, VA 0x18009420d),
// whose data name (+0x8) or instance name (+0x710) equals the name (empty =
// any) and whose owner (+0xb8) is the Thing: canceled (+0x63) = 1, and with
// PreventChain +0x6c4 = 1. With a child (+0xc0) and DisableOwnerInput (+0x83)
// the child's +0x3b8 is set; that owner-input path fails loudly here.
bool ActionEffectWorld::cancelAttached(int32_t owner,const std::string& name,bool preventChain,size_t& matched,std::string& error){
 matched=0;
 for(auto& [h,s]:states_){
  if(s.ownerId!=owner||s.guiLine)continue;const auto& d=s.definition;
  if(d.get("CanCancelAsAttached")&&!b(d,"CanCancelAsAttached"))continue;
  if(!name.empty()&&t(d,"Name")!=name&&s.name!=name)continue;
  auto* life=registry_->find(h);if(!life||!life->initialized)continue;
  if(s.primary){error="StopAnimation cancel of a Thing primary animation not reconstructed: "+s.name;return false;}
  if(b(d,"DisableOwnerInput")){error="StopAnimation DisableOwnerInput owner-input release (+0xc0 -> +0x3b8) not reconstructed: "+s.name;return false;}
  if(!life->canceled&&!registry_->cancel(h,error))return false;if(preventChain)s.preventChain=true;++matched;
 }
 error.clear();return true;
}
bool ActionEffectWorld::addAngle(uint64_t h,float radians,std::string&error){if(!state(h)||!std::isfinite(radians)){error="Animation AddAngle target unavailable";return false;}if(radians==0)return true;forChain(h,[&](ActionEffectState&s,bool){s.heading=wrapAngle(s.heading+radians);});return true;}
bool ActionEffectWorld::setRadius(uint64_t h,float value,std::string&error){
 if(!state(h)||!std::isfinite(value)){error="Animation SetRadius target unavailable";return false;}
 std::function<void(uint64_t)> radius=[&](uint64_t id){auto it=states_.find(id);if(it==states_.end())return;if(it->second.child)radius(it->second.child);const auto&d=it->second.definition;std::set<std::string> names;if(auto c=t(d,"CreateAnimation");!c.empty())names.insert(c);if(auto*list=d.get("CreateAnimations"))for(const auto&r:list->array())names.insert(t(r,"Name"));for(auto c:it->second.created)if(auto*cs=state(c);cs&&names.contains(cs->name))radius(c);it->second.scaleRadius=value;};
 radius(h);error.clear();return true;}
bool ActionEffectWorld::setup(uint64_t h,const ActionAnimationSetup&a,std::string&error){try{
 auto*root=state(h);if(!root){error="SetupAnimation target is not a live action Animation";return false;}
 // 1 Scale: ModifyScale multiplies body.mScale down the child chain.
 if(a.scale!=1)forChain(h,[&](ActionEffectState&s,bool){s.ownScale*=a.scale;});
 // 2 ScaleRadius: SetRadius 0x1802974e0 (child, matching created, then self).
 if(a.scaleRadius>0&&!setRadius(h,a.scaleRadius,error))return false;
 // 3 Duration (>0) through SetDuration.
 if(a.duration>0&&!setDuration(h,a.duration,error))return false;
 // 4 PreferUnmodifiedTime: only where definition TimeModifierFraction<0 (default -1).
 if(!preferUnmodifiedTime(h,error))return false;
 // 5 OffsetX/OffsetY replace mAttachOffset when either is nonzero.
 const float x=a.mirrorOffsetX?-a.offsetX:a.offsetX;if((x!=0||a.offsetY!=0)&&!setAttachOffset(h,x,a.offsetY,error))return false;
 // 6 AddOffsetZ (zero-safe): self, child chain and live created animations.
 if(a.offsetZ!=0){std::function<void(uint64_t)> addZ=[&](uint64_t id){auto it=states_.find(id);if(it==states_.end())return;it->second.addedOffsetZ+=a.offsetZ;if(it->second.child)addZ(it->second.child);for(auto c:it->second.created)addZ(c);};addZ(h);}
 // 7 SetUseScreenLocation297830: self and ChildAnimation chain. The
 // renderer retains original logical screen coordinates; its screen route
 // performs the inverse of the native camera-space component conversion.
 if(a.useScreenLocation)forChain(h,[&](ActionEffectState&s,bool){s.useScreenLocation=true;});
 // 8 Parallax: mParallaxAmount copied down the child chain (SetParallax).
 if(a.parallax)forChain(h,[&](ActionEffectState&s,bool){s.parallax=*a.parallax;});
 // 9 Angle in degrees: SetAngle(rad,true) wraps self; child gets unwrapped; created get wrapped.
 if(a.angleDegrees){const float rad=float(double(*a.angleDegrees)*0.017453292519943295);forChain(h,[&](ActionEffectState&s,bool first){s.heading=first?wrapAngle(rad):rad;});for(auto c:std::vector<uint64_t>(states_.at(h).created))forChain(c,[&](ActionEffectState&s,bool first){s.heading=first?wrapAngle(rad):rad;});}
 // 10 Color: SetColor(color,"",false,toChild) 0x180296ff0, child first.
 if(a.color){std::vector<ActionEffectState*>chain;forChain(h,[&](ActionEffectState&s,bool){chain.push_back(&s);});for(auto it=chain.rbegin();it!=chain.rend();++it){auto color=*a.color;const auto&d=(*it)->definition;const float gamma=n(d,"GammaCorrection");if(gamma>0)for(size_t c=0;c<3;++c)color[c]=uint8_t(255.f*std::pow(float(color[c])/255.f,1.f/gamma));const float high=n(d,"NonHighColorChannelMultiplier");if(high>0){const auto top=std::max({color[0],color[1],color[2]});for(auto&c:color)if(c<top)c=uint8_t(float(c)*high);}(*it)->baseColor=color;}}
 // 11 Group overrides the definition GroupName; "" is MapThingGroupId::None.
 if(a.group)states_.at(h).runtimeGroup=*a.group;
 return true;
 }catch(const std::exception&e){error=e.what();return false;}}
// Animation::Loop 0x180296310 / BookAnimation::Loop 0x1802b1480 / OnLoop 0x180296670.
bool ActionEffectWorld::loopAnimation(uint64_t h,bool force,const ActionEffectProviders&p,std::string&error){
 auto&s=states_.at(h);const auto&d=s.definition;
 if(s.type==NativeAnimationType::Book)s.frame=s.backwards?n(d,"EndFrame",n(d,"NumFrames")):n(d,"StartFrame",1);
 // ResetTimeRemaining: TimeRemaining = current Duration component.
 if((!b(d,"LoopFramesOnly")||force)&&s.durationBound)s.remaining=s.duration;
 // OnLoop 0x1802966a6: StopSoundOnFinishFade>0 first StopCue(mSound, fade).
 if(b(d,"ReplaySoundOnLoop")&&!t(d,"Sound").empty()&&n(d,"StopSoundOnFinishFade")>0&&s.sound){if(!stopSound_){error="Actual action FX StopCue fade provider unavailable";return false;}if(!stopSound_(s.sound,n(d,"StopSoundOnFinishFade"),error))return false;s.sound=0;}
 if(b(d,"ReplaySoundOnLoop")&&!t(d,"Sound").empty()){if(!p.sound){error="Actual action FX loop sound provider unavailable";return false;}if(!p.sound(t(d,"Sound"),s.ownerId,s.position,uint32_t(n(d,"SoundManagerCap")),s.sound,error))return false;}
 return true;}
bool ActionEffectWorld::timeRemaining(uint64_t h,float&out,std::string&error)const{const auto*s=state(h);if(!s){error="GetTimeRemaining target is not a live action Animation";return false;}
 if(s->type==NativeAnimationType::Book){const auto&d=s->definition;const float speed=s->playSpeed*s->playSpeedMultiplier;if(s->customFrames||speed==0){error="Book GetTimeRemaining without DataPlaySpeed not reconstructed: "+s->name;return false;}out=float(int32_t(n(d,"EndFrame",n(d,"NumFrames")))-int32_t(s->frame))/speed;return true;}
 if(s->type==NativeAnimationType::Slide){error="Slide GetTimeRemaining not reconstructed: "+s->name;return false;}
 out=s->remaining;return true;}
bool ActionEffectWorld::setFrameTarget(int32_t owner,const std::string& name,float fraction,bool instant,std::string& error){
 if(!std::isfinite(fraction)){error="Native Book frame target must be finite";return false;}
 for(auto& [handle,s]:states_){(void)handle;if(s.ownerId!=owner||s.detached||s.name!=name)continue;
  if(s.type==NativeAnimationType::Slide){error="Native frame target on world Slide not reconstructed: "+name;return false;}
  if(s.type!=NativeAnimationType::Book)continue;
  // GetFrameForFraction2960e0: source Start+(End-Start+1)*fraction,
  // clamped without rounding. SetFractionalFrame295ff0 writes that float.
  const float start=n(s.definition,"StartFrame",1),end=n(s.definition,"EndFrame",n(s.definition,"NumFrames",1));
  const float target=std::clamp(start+(end-start+1)*fraction,start,end);
  if(instant)s.frame=target;
  if(!s.customFrames&&s.playSpeed!=0)s.frameTarget=instant?s.frame:target;
 }
 error.clear();return true;
}
bool ActionEffectWorld::setPlaySpeedMultiplier(uint64_t h,float m,std::string&error){auto i=states_.find(h);if(i==states_.end()||!std::isfinite(m)){error="PlaySpeedMultiplier target is not a live action Animation";return false;}i->second.playSpeedMultiplier=m;return true;}
bool ActionEffectWorld::setZLocation(uint64_t h,float z,std::string&error){auto i=states_.find(h);if(i==states_.end()||!std::isfinite(z)){error="Animation SetZLocation target unavailable";return false;}i->second.ownZ=z;i->second.position[2]=z;return true;}
bool ActionEffectWorld::preferUnmodifiedTime(uint64_t h,std::string&error){if(!state(h)){error="PreferUnmodifiedTime target is not a live action Animation";return false;}forChain(h,[&](ActionEffectState&s,bool){if(n(s.definition,"TimeModifierFraction",-1)<0)s.preferUnmodified=true;});return true;}
// BookAnimation::GetAngleNum 0x1802b0630 (Slide/Book/Constant share it):
// mAngle (+ attached owner angle and its modifier when AngleFromOwner is
// Maintain), NumAngles<=1 -> 0, interval tau/N, - AngleNumAngleOffset (not
// admitted), + interval/2, truncate, + AngleNumOffset, one wrap, Mirror fold.
uint32_t ActionEffectWorld::angleNum(const ActionEffectState&s,const EffectOwnerState&owner)const{
 const auto&d=s.definition;const int angles=int((s.movie?movieBank_:bank_)->clips().at(s.imageClip).originalAngles);if(angles<=1)return 0;
 float angle=s.heading;if(!s.detached&&maintainsOwnerAngle(d)){if(owner.isUnit&&!owner.bookOwnerAngle)throw std::runtime_error("Actual Unit Book cosmetic angle unavailable: "+s.name);angle+=owner.isUnit?*owner.bookOwnerAngle:owner.angle;}
 // AnimationData ctor094170 sets UseIsometricAngle(full+0x9d)=true. Movie
 // Book children use the same exact native LUT as Thing HAA primaries.
 if(s.movie&&(!d.get("UseIsometricAngle")||b(d,"UseIsometricAngle")))angle=movieBank_->inverseIsometricAngle(angle);
 const float interval=6.2831854820251465f/float(angles),half=interval*.5f;
 const volatile float scaled=(angle+half)/interval;int index=int(float(scaled))+int(n(d,"AngleNumOffset"));
 if(angles-1==0)index=0;else if(index<0)index+=angles;else if(index>angles-1)index=index-(angles-1)-1;
 if(b(d,"Mirror")&&index>angles/4&&index<angles*3/4){const int mid=angles/2;index=index<=mid?mid-index:angles-index+mid;}
 if(index<0||index>=angles)throw std::runtime_error("Original action FX angle index outside one native wrap: "+s.name);
 return uint32_t(index);}
// Animation::UpdateActiveVfxCount 0x1802940b0: live spawned VisualFx, plus for
// each the live members of its own VFX list, created list and child that share
// this animation's owner Thing (+0xb8 id at +0x6c, both null counts). Dead
// entries are erased from the list.
uint32_t ActionEffectWorld::activeVfxCount(uint64_t h){
 auto&self=states_.at(h);const int32_t ownerKey=self.detached?0:self.ownerId;uint32_t count=0;
 const auto same=[&](uint64_t id){const auto*o=state(id);return o&&!o->done&&(o->detached?0:o->ownerId)==ownerKey;};
 std::vector<uint64_t> live;for(auto id:self.vfx){const auto*v=state(id);if(!v||v->done)continue;live.push_back(id);++count;
  for(auto c:v->vfx)count+=same(c);for(auto c:v->created)if(std::find(v->vfx.begin(),v->vfx.end(),c)==v->vfx.end())count+=same(c);if(v->child)count+=same(v->child);}
 self.vfx=std::move(live);return count;}
}

