#include "screen_animation.hpp"
#include "animation_transitions.hpp"
#include "effect_particle.hpp"
#include "physics_runtime.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <set>
#include <tuple>
#include <stdexcept>
// Native semantics evidence (static disassembly of the matching engine DLL):
//  .local/reports/hud-ft-semantics.json      SetAnimation/SetAnimationFrameTarget/
//                                             PlayToFraction/PlaySpeedUpdateSystem/Book.UpdateInternal
//  .local/reports/hud-draw-semantics.json    GetScale/GetDrawScaleVector/GetDrawColor/
//                                             ShouldAddColor/GetLocation/InitData origin/AddChild
//  .local/reports/hud-motion-semantics.json  SetAlpha/SetScale/SetScaleX/SetScaleY,
//                                             FadeObstacleIn/HideObstacle/Move/Obstacle.MoveTo
namespace hades {namespace {
std::string text(const SourceValue&d,const char*k){auto*p=d.get(k);return p?p->text():"";}
float num(const SourceValue&d,const char*k,float fallback=0){auto*p=d.get(k);return p?float(p->number()):fallback;}
bool flag(const SourceValue&d,const char*k){auto*p=d.get(k);return p&&p->boolean();}
// Source keys reconstructed for reviewed GUI records (mirrors the exporter's
// screen_gui_review.KEYS). Anything else is rejected at load.
const std::set<std::string> guiKeys{"Name","InheritFrom","FilePath","Type","StartFrame","EndFrame","NumFrames","PlaySpeed","OffsetX","OffsetY","OriginX","OriginY","Scale","ScaleX","ScaleY","Alpha","AddColor","Material","SortMode","Loop","HoldLastFrame","ChildAnimation","AngleFromOwner","ScaleFromOwner","ColorFromOwner","DieWithOwner","Red","Green","Blue","Slides","Sound","Color","StartAlpha","EndAlpha","Duration","ChainTo","AttachedTextScalarX","AttachedTextScalarY","Ambient",
 // Constant transition helpers (InitData 0x180292566..0x1802926e3 per channel,
 // GetXShift/GetYShift 0x180297b40/0x180297a50, Animation::Loop 0x180296310).
 "StartRed","StartGreen","StartBlue","EndRed","EndGreen","EndBlue","EaseIn","EaseOut","PingPongColor",
 "StartOffsetX","EndOffsetX","StartOffsetY","EndOffsetY","PingPongShiftOverDuration","PhaseOffsetShiftOverDuration",
 "StartScale","EndScale","PingPongScale","CancelAllAttached","CreateAnimation","CreateAnimations","TimeModifierFraction","StartScaleX","EndScaleX","StartScaleY","EndScaleY","StartAngle","EndAngle","PingPongAngle","PhaseOffsetAngle","RandomDurationMin","RandomDurationMax","ReRandomizeOnLoop","VideoTexture"};
// Keys whose transition fraction is the Constant duration fraction
// (AnimationFractionFromDurationSystem 0x180152ce0). Book/Slide frame
// fractions for these helpers are not reviewed for screen records.
const char* const constantTransitionKeys[]{"Duration","StartAlpha","EndAlpha","StartRed","StartGreen","StartBlue","EndRed","EndGreen","EndBlue","EaseIn","EaseOut","PingPongColor","StartOffsetX","EndOffsetX","StartOffsetY","EndOffsetY","PingPongShiftOverDuration","PhaseOffsetShiftOverDuration","StartScale","EndScale","PingPongScale","StartScaleX","EndScaleX","StartScaleY","EndScaleY","StartAngle","EndAngle","PingPongAngle","PhaseOffsetAngle","RandomDurationMin","RandomDurationMax","ReRandomizeOnLoop"};
// AnimationData ctor 0x180093c70 sets Alpha(+0x214)=1 but leaves the
// Start/End Red..Alpha pairs (+0x22c..+0x248, SGD_Deserialize 0x18008f020)
// at 0. InitData 0x180292566..0x1802926e3 creates a channel helper only when
// Start!=End; an absent helper contributes 1 (GetDrawColor 0x180298810).
float colorTransition(const SourceValue&d,const char*a,const char*z,float f){const float s=num(d,a),e=num(d,z);return s==e?1.f:EffectParticleWorld::interpolate(s,e,f,num(d,"EaseIn"),num(d,"EaseOut"));}
// Animation::GetValue 0x180298750 for GetXShift/GetYShift (PhaseOffset wraps once).
float shiftValue(const SourceValue&d,const char*a,const char*z,float f){const float s=num(d,a),e=num(d,z);if(s==e)return 0;float x=num(d,"PhaseOffsetShiftOverDuration")/360.f+f;if(x>1)x-=1;else if(x<0)x+=1;const float in=num(d,"EaseIn"),out=num(d,"EaseOut");if(flag(d,"PingPongShiftOverDuration"))return x<.5f?EffectParticleWorld::interpolate(s,e,x*2,in,out):EffectParticleWorld::interpolate(e,s,(x-.5f)*2,in,out);return EffectParticleWorld::interpolate(s,e,x,in,out);}
// InitData 0x18029217d..0x1802922fd scale helper (default Start=End=1),
// BodyComponentTransitionHelper::CalcCurrentValue 0x1802b5420.
float scaleTransition(const SourceValue&d,float f,const char*start="StartScale",const char*end="EndScale"){return animationScaleTransition(num(d,start,1),num(d,end,1),f,num(d,"EaseIn"),num(d,"EaseOut"),flag(d,"PingPongScale"));}
// InitData291dc6 and GetValue291290. Duration is selected before child,
// created-manager animations and audio; Loop2963ea reselects only for min>0.

// APPROXIMATION: Material/Ambient/SortMode remain renderer presentation hints,
// as in ActionEffectWorld::pendingFieldsSet; exporter receipts enumerate every
// admitted definition that has one. This does not rebuild native lighting.
NativeAnimationType typeOf(const SourceValue&d){const auto t=text(d,"Type");return t=="Slide"?NativeAnimationType::Slide:t=="Dataless"?NativeAnimationType::Dataless:(t=="ConstantNoUpdate"?NativeAnimationType::ConstantNoUpdate:(t=="Constant"?NativeAnimationType::Constant:NativeAnimationType::Book));}
// FromOwnerBehavior: 0 Maintain (constructor default), 1 Take, 2 Ignore.
bool maintain(const SourceValue&d,const char*k){const auto v=text(d,k);if(v.empty()||v=="Maintain")return true;if(v=="Ignore")return false;throw std::runtime_error(std::string("Native screen FromOwner Take branch not reconstructed: ")+k);}
int32_t startFrame(const SourceValue&d){return int32_t(num(d,"StartFrame",1));}
int32_t endFrame(const SourceValue&d){return int32_t(num(d,"EndFrame",num(d,"NumFrames",1)));}
// Animation::GetFrameForFraction 0x2960e0: float, no rounding.
float frameForFraction(const SourceValue&d,float f){const float s=float(startFrame(d)),e=float(endFrame(d));const float span=e>0?e-s+1:num(d,"NumFrames",1);return std::min(std::max(s+span*f,s),e);}
// Animation::SetFractionalFrame: wrap outside [Start,End], mFrame=trunc.
void setFractionalFrame(const SourceValue&d,ScreenLayer&l,float x){const float s=float(startFrame(d)),e=float(endFrame(d));if(x<s)x=e-(s-x);else if(x>e)x=s+(x-e);l.frac=x;l.frame=int32_t(std::trunc(x));}
uint8_t byteOf(float v){return uint8_t(std::clamp(int(std::lround(v*255.f)),0,255));}
}
bool ScreenAnimationWorld::legacyClip(const std::string&name){static const std::set<std::string> v{"RemBGIntro","RemBG","Backgrounds\\rectangle_01","Blank","VignetteOverlay","RoomTransitionIn","RoomTransitionOut"};return v.contains(name);}
bool ScreenAnimationWorld::validateSlide(const SourceValue&d,const ActorAnimationClip&clip,std::string&e)const{try{
 // SetupSlideIndex2b5010 copies DurationFrames into instance SlideInfos;
 // NextSlide2b46d0 skips zero-duration entries and loops to StartFrame.
 const auto*source=d.get("Slides");if(!source)throw std::runtime_error("Original screen Slide data unavailable");
 const auto&slides=source->array();if(slides.empty()||slides.size()>1000||startFrame(d)!=1||num(d,"EndFrame",float(slides.size()))!=float(slides.size())||num(d,"NumFrames",float(slides.size()))!=float(slides.size()))throw std::runtime_error("Original screen Slide frame range unsupported");
 if((clip.flags&ActorBook)||bool(clip.flags&ActorLoop)!=flag(d,"Loop"))throw std::runtime_error("Original screen Slide bank flags mismatch");
 size_t positive=0;uint32_t totalTicks=0;
 for(size_t at=0;at<slides.size();++at){const auto&slide=slides[at];
  for(const auto&[key,value]:slide.object()){(void)value;if(key!="DurationFrames"&&key!="FilePath"&&key!="RandomDurationFramesMin"&&key!="RandomDurationFramesMax")throw std::runtime_error("Native screen Slide field not reconstructed: "+key);}
  const double ticks=slide.get("DurationFrames")?slide.get("DurationFrames")->number():-1;
  const double lo=slide.get("RandomDurationFramesMin")?slide.get("RandomDurationFramesMin")->number():0,hi=slide.get("RandomDurationFramesMax")?slide.get("RandomDurationFramesMax")->number():0;
  for(const double value:{ticks,lo,hi})if(!std::isfinite(value)||value<0||value>100000||value!=std::trunc(value))throw std::runtime_error("Original screen Slide integer duration bounds");
  if(lo>0&&hi<lo)throw std::runtime_error("Original screen random Slide range invalid");
  if(const auto*path=slide.get("FilePath"))(void)path->text();
  totalTicks+=uint32_t(ticks);if(totalTicks>1000000)throw std::runtime_error("Original screen Slide schedule outside bank bounds");
  if(ticks>0){if(positive>=clip.slides.size()||clip.slides[positive].originalFrame!=at+1||clip.slides[positive].ticks!=uint32_t(ticks))throw std::runtime_error("Original screen Slide bank/source schedule mismatch");++positive;}
 }
 if(!positive||positive!=clip.slides.size())throw std::runtime_error("Original screen Slide bank/source frame closure mismatch");
 return true;
 }catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::startSlide(ScreenLayer&l,const SourceValue&d,std::string&e){
 // SlideStart2b40e7..2b4136 replaces the current instance ticks before
 // effects, adds ticks/60 to the existing remainder, and draws RandomInt
 // only for min>0 and max!=min. RandomInt291250 is upper-exclusive.
 const auto&slide=d.get("Slides")->array().at(l.playback.originalFrame()-1);
 const uint32_t lo=uint32_t(num(slide,"RandomDurationFramesMin")),hi=uint32_t(num(slide,"RandomDurationFramesMax"));
 uint32_t ticks=uint32_t(num(slide,"DurationFrames"));bool drew=false;
 if(lo){if(hi<lo){e="Original random screen Slide duration range";return false;}uint32_t draw=0;
  if(hi!=lo){if(!boundedRandom_){e="Actual screen Slide deterministic RNG unavailable";return false;}if(!boundedRandom_(hi-lo,draw,e))return false;if(draw>=hi-lo){e="Actual screen Slide deterministic RNG outside requested bound";return false;}}
  ticks=lo+draw;drew=hi!=lo;l.playback.replaceCurrentSlideTicks(ticks);
 }
 if(std::getenv("HADES_TRACE_SLIDES"))log(Severity::Info,"HadesNative","SCREEN_SLIDE_ENTER name=%s handle=%llu frame=%u ticks=%u random_draw=%d",l.name.c_str(),static_cast<unsigned long long>(l.handle),l.playback.originalFrame(),ticks,int(drew));
 return true;
}
bool ScreenAnimationWorld::initializeSlide(ScreenLayer&l,const SourceValue&d,std::string&e){try{
 l.playback.play(*bank_,l.name,[&](const auto&,const auto&){if(!startSlide(l,d,e))throw std::runtime_error(e);});
 l.frame=int32_t(l.playback.originalFrame());l.frac=float(l.frame);return true;
 }catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::initializeDuration(ScreenLayer&l,const SourceValue&d,std::string&e){
 l.durationBound=num(d,"Duration")>0||num(d,"RandomDurationMin")>0||num(d,"RandomDurationMax")>0;
 l.duration=num(d,"Duration");
 const float lo=num(d,"RandomDurationMin"),hi=num(d,"RandomDurationMax");
 if(lo!=0||hi!=0){double r=0;if(!random_||!random_(r,e)||!std::isfinite(r)||r<0||r>=1){if(e.empty())e="Actual screen animation duration RNG unavailable";return false;}const volatile float product=float(r)*(hi-lo);l.duration=product+lo;}
 l.remaining=l.duration;return true;
}
bool ScreenAnimationWorld::load(SourceValue catalog,std::shared_ptr<ActorAnimationBank>bank,std::shared_ptr<AnimationRegistry>registry,std::string&e){try{
 if(!things_.empty()||!bank||!registry||text(catalog,"schema")!="hades-screen-animations-v1"||text(catalog,"target_sha256")!="d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254")throw std::runtime_error("Original screen animation resource identity");
 if(const auto*children=catalog.get("action_children"))for(const auto&child:children->array())if(!actionChildren_.insert(child.text()).second)throw std::runtime_error("Duplicate delegated screen ChildAnimation");
 for(const auto&[name,row]:catalog.get("clips")->object()){
  const auto*definition=row.get("definition");if(!definition)throw std::runtime_error("Screen clip without original definition "+name);
  if(!legacyClip(name)){
   // Reviewed original GUI closure: only reconstructed properties/types.
   for(const auto&[key,value]:definition->object()){(void)value;if(!guiKeys.contains(key))throw std::runtime_error("Unreviewed native screen animation property "+name+"."+key);}
   if(flag(*definition,"CancelAllAttached")&&!text(*definition,"ChildAnimation").empty()&&text(*definition,"ChildAnimation")!="null")throw std::runtime_error("Native screen CancelAllAttached with new ChildAnimation not reconstructed: "+name);
   const auto t=text(*definition,"Type");if(t!="Constant"&&t!="ConstantNoUpdate"&&t!="Book"&&t!="Dataless"&&t!="Slide")throw std::runtime_error("Unreviewed native screen animation type "+name);
   if(t=="Slide"){const auto clip=bank->find(name);if(!clip||!validateSlide(*definition,bank->clips().at(*clip),e))throw std::runtime_error(e.empty()?"Original screen Slide bank missing: "+name:e);}
   if(t!="Constant")for(const char*k:constantTransitionKeys)if(definition->get(k))throw std::runtime_error("Unreviewed non-Constant screen duration/transition "+name+"."+k);
   if(t=="Book"&&(startFrame(*definition)<1||endFrame(*definition)<startFrame(*definition)))throw std::runtime_error("Unreviewed native Book frame range "+name);
   const float duration=num(*definition,"Duration"),lo=num(*definition,"RandomDurationMin"),hi=num(*definition,"RandomDurationMax");
   if(!std::isfinite(duration)||duration<0||!std::isfinite(lo)||!std::isfinite(hi)||lo<0||hi<lo)throw std::runtime_error("Native screen duration range invalid: "+name);
   if(!text(*definition,"VideoTexture").empty()){
    const auto*binding=row.get("movie_binding");const auto index=bank->find(name);
    if(t!="Book"||!binding||!index||text(*binding,"video_texture")!=text(*definition,"VideoTexture"))throw std::runtime_error("Original screen movie binding unavailable: "+name);
    const auto digest=[](const std::string&v){return v.size()==64&&std::all_of(v.begin(),v.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});};
    if(!digest(text(*binding,"source_sha256"))||!digest(text(*binding,"atlas_sha256")))throw std::runtime_error("Original screen movie identity invalid: "+name);
    if(!binding->get("canvas")||!binding->get("original_size")||!binding->get("exported_source_frames"))throw std::runtime_error("Original screen movie metadata incomplete: "+name);
    const auto&canvas=binding->get("canvas")->array();const auto&original=binding->get("original_size")->array();const auto&frames=binding->get("exported_source_frames")->array();const auto&clip=bank->clips().at(*index);
    const float atlasScale=num(*binding,"atlas_scale"),sourceScale=num(*definition,"Scale",1);const int32_t first=startFrame(*definition),last=endFrame(*definition);
    if(canvas.size()!=2||original.size()!=2||!(atlasScale>0)||!std::isfinite(atlasScale)||!(sourceScale>0)||!std::isfinite(sourceScale)||clip.originalAngles!=1||clip.originalAngleIndices!=std::vector<uint32_t>{0}||!(clip.flags&ActorBook)||clip.slides.size()!=size_t(last-first+1)||frames.size()!=clip.slides.size()||num(*binding,"source_frame_count")!=last||first!=1||num(*definition,"OriginX")!=0||num(*definition,"OriginY")!=0||std::abs(clip.worldScale-sourceScale/atlasScale)>1e-5f||clip.offsetX!=num(*definition,"OffsetX")||clip.offsetY!=num(*definition,"OffsetY"))throw std::runtime_error("Original screen movie geometry/range mismatch: "+name);
    for(size_t axis=0;axis<2;++axis)for(const auto*pair:{&canvas,&original}){const auto value=pair->at(axis).number();if(!std::isfinite(value)||value<=0||value>8192||value!=std::trunc(value))throw std::runtime_error("Original screen movie dimensions invalid: "+name);}
    for(size_t at=0;at<frames.size();++at){const auto image=bank->sample(*index,at,0,false);if(frames[at].number()!=first+int32_t(at)||clip.slides[at].originalFrame!=first+at||image.originalMovieFrame!=first+at-1||image.canvasWidth!=canvas[0].number()||image.canvasHeight!=canvas[1].number())throw std::runtime_error("Original screen movie frame binding mismatch: "+name);}
    movies_.insert(name);
   }

  }
  // A source definition may have both a screen-primary resource and an
  // explicitly delegated action-child resource. Child edges select their
  // bound consumer; primary admission always selects this screen bank.

  if(!bank->find(name))throw std::runtime_error("Screen original pixels unavailable: "+name);definitions_[name]=*definition;}
 for(const char*required:{"RemBGIntro","RemBG","Backgrounds\\rectangle_01","Blank","VignetteOverlay"})if(!definitions_.contains(required))throw std::runtime_error("Incomplete screen original resource graph");
 for(const auto&[name,d]:definitions_){for(const auto*field:{"ChildAnimation","ChainTo"}){const auto child=text(d,field);if(!child.empty()&&child!="null"&&!definitions_.contains(child)&&!(std::string(field)=="ChildAnimation"&&actionChildren_.contains(child)))throw std::runtime_error(std::string("Original screen ")+field+" closure missing: "+child);}}
 bank_=std::move(bank);registry_=std::move(registry);return true;
 }catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::create(ObjectSnapshot&body,std::array<float,4>color,const ScreenAnimationSound&sound,std::string&e,float ownParallax,float mapParallax){
 if(!body.entityHandle||!body.screenSpace||things_.contains(body.id)||!std::isfinite(ownParallax)||!std::isfinite(mapParallax)){e="Actual selected screen Thing allocation unavailable";return false;}ScreenThing s;s.body=&body;s.sourceGraphic=body.animation;s.baseColor=color;s.ownParallax=ownParallax;s.mapParallax=mapParallax;body.backend=ObjectBackend::Screen;things_.emplace(body.id,std::move(s));return body.animation.empty()?true:setAnimation(body.id,body.animation,sound,e);
}
bool ScreenAnimationWorld::releasePrimary(ScreenThing&s,const ScreenAnimationSound&sound,std::string&e){
 if(!s.primary)return true;
 for(auto&l:s.layers){const auto&d=definitions_.at(l.name);const float fade=num(d,"StopSoundOnFinishFade");const uint32_t cue=l.legacy?s.sound:l.sound;
  if(cue&&fade>0&&(!sound.stop||!sound.stop(cue,fade,e)))return false;
  if(l.externalChild){if(!graph_.releaseChild||!graph_.releaseChild(l.externalChild,e))return false;l.externalChild=0;}
 }
 // Release action-owned descendants before the shared registry recurses;
 // their owner also owns sound/components, not merely the registry record.
 if(!registry_->cancel(s.primary,e)||!registry_->dispose(s.primary,e))return false;
 s.primary=0;s.sound=0;s.playback.reset();s.layers.clear();return true;
}
bool ScreenAnimationWorld::childFinished(uint64_t parent,uint64_t child,std::string&e){
 for(auto&[id,s]:things_)for(auto&l:s.layers)if(l.handle==parent){(void)id;const auto*life=registry_->find(parent);if(!life||life->child!=child||l.externalChild!=child){e="Actual screen child completion ownership mismatch";return false;}l.externalChild=0;return true;}
 e="Actual screen child completion parent unavailable";return false;
}
bool ScreenAnimationWorld::allocateLayers(int32_t id,ScreenThing&s,const std::string&name,const ScreenAnimationSound&sound,std::vector<ScreenLayer>&out,std::string&e){
 // Validate the whole graph before allocation, audio, or native RNG mutation.
 std::set<std::string>seen;
 for(std::string next=name;!next.empty()&&next!="null";){
  if(!seen.insert(next).second||seen.size()>9){e="Original screen ChildAnimation chain bound/cycle";return false;}
  const auto&d=definitions_.at(next);
  if(seen.size()>1&&flag(d,"CancelAllAttached")){e="Native screen child CancelAllAttached parent cancellation not reconstructed: "+next;return false;}
  if(flag(d,"CancelAllAttached")&&!cancelAllAttached_){e="Actual CancelAllAttached cross-manager provider unavailable: "+next;return false;}
  const auto prepare=[&](const std::string&dep){if(dep.empty()||dep=="null")return true;if(!graph_.prepare){e="Actual screen animation graph provider unavailable: "+dep;return false;}return graph_.prepare(id,dep,e);};
  if(!prepare(text(d,"CreateAnimation")))return false;
  if(const auto*list=d.get("CreateAnimations"))for(const auto&row:list->array()){if(row.object().size()!=1||!row.get("Name")){e="Native screen CreateAnimations record not reconstructed";return false;}if(!prepare(text(row,"Name")))return false;}
  const auto cue=text(d,"Sound");if(!cue.empty()&&!s.suppressSounds&&sound.worldFrame>=uint64_t(num(d,"SoundAllowedAfterFrame",2))){if(!sound.prepare||!sound.play){e="Actual screen sound preparation/playback provider unavailable: "+cue;return false;}if(!sound.prepare(cue,e))return false;}
  if(typeOf(d)==NativeAnimationType::Slide&&!boundedRandom_)for(const auto&slide:d.get("Slides")->array())if(num(slide,"RandomDurationFramesMin")>0&&num(slide,"RandomDurationFramesMax")>num(slide,"RandomDurationFramesMin")){e="Actual screen Slide deterministic RNG unavailable";return false;}
  if((num(d,"RandomDurationMin")!=0||num(d,"RandomDurationMax")!=0)&&!random_){e="Actual screen animation duration RNG unavailable: "+next;return false;}
  next=text(d,"ChildAnimation");
  if(actionChildren_.contains(next)){if(!graph_.prepareChild||!graph_.child||!graph_.releaseChild){e="Actual screen ChildAnimation graph provider unavailable: "+next;return false;}if(!graph_.prepareChild(id,next,e))return false;break;}
 }
 std::function<bool(const std::string&,uint64_t)> initialize=[&](const std::string&next,uint64_t parent){
  const auto&d=definitions_.at(next);const size_t index=out.size();ScreenLayer layer;layer.name=next;layer.clip=*bank_->find(next);layer.movie=movies_.contains(next);
  AnimationAllocation a;a.name=next;a.sourceDefinition=d;a.ownerId=id;a.ownerLifetime=s.body->entityHandle;a.manager=0;a.ownership=parent?AnimationOwnership::Child:AnimationOwnership::ThingPrimary;a.parent=parent;a.type=typeOf(d);a.onlyWhenVisible=flag(d,"OnlyWhenVisible");a.loop=flag(d,"Loop");
  if(!registry_->allocate(a,layer.handle,e))return false;out.push_back(std::move(layer));const auto handle=out[index].handle;
  // Constant.Init2b1b27 attaches before base Init291bf7/InitData. InitData
  // 2932db..293465 initializes Child, singleton Create, ordered Creates,
  // then Sound. Child initialization completes before its parent returns.
  if(flag(d,"CancelAllAttached")){
   if(!cancelAllAttached_(id,s.body->entityHandle,s.primary,e))return false;
   for(const auto&old:s.layers)if(old.handle!=s.primary&&!registry_->cancel(old.handle,e))return false;
  }
  if(a.type==NativeAnimationType::Constant&&!initializeDuration(out[index],d,e))return false;
  const auto child=text(d,"ChildAnimation");if(!child.empty()&&child!="null"){
   if(actionChildren_.contains(child)){uint64_t external=0;if(!graph_.child(id,child,handle,{s.body->x,s.body->y,0},external,e))return false;out[index].externalChild=external;}
   else if(!initialize(child,handle))return false;
  }
  const auto create=[&](const std::string&dep){if(dep.empty()||dep=="null")return true;if(!graph_.create){e="Actual screen manager creation provider unavailable";return false;}
   // InitData293368/2933f0: attached parent GetLocation296c90 and the same
   // Thing. Source OffsetX/Y are draw offsets (+90/+94), not the separate
   // attach offset (+a0). No nondefault attach offset is admitted here.
   uint64_t created=0;if(!graph_.create(id,dep,{s.body->x,s.body->y,0},created,e))return false;if(created)out[index].created.push_back(created);return true;};
  if(!create(text(d,"CreateAnimation")))return false;
  if(const auto*list=d.get("CreateAnimations"))for(const auto&row:list->array())if(!create(text(row,"Name")))return false;
  const auto cue=text(d,"Sound");if(!cue.empty()&&!s.suppressSounds&&sound.worldFrame>=uint64_t(num(d,"SoundAllowedAfterFrame",2)))if(!sound.play(cue,out[index].sound,e))return false;
  auto&l=out[index];if(a.type==NativeAnimationType::Book){l.speed=num(d,"PlaySpeed");if(l.speed==0)l.speed=60;l.frac=float(startFrame(d));l.frame=startFrame(d);}
  else if(a.type==NativeAnimationType::Slide){if(!initializeSlide(l,d,e))return false;}

  if(!registry_->commitInitialization(handle,false,e))return false;
  if(!parent&&s.timeMultiplier!=1&&!registry_->linkTimeMultiplier(handle,id,s.body->entityHandle,e))return false;
  return true;
 };
 return initialize(name,0);
}
bool ScreenAnimationWorld::setAnimation(int32_t id,const std::string&name,const ScreenAnimationSound&sound,std::string&e){return setAnimation(id,name,ScreenAnimationRequest{},sound,e);}
bool ScreenAnimationWorld::setAnimation(int32_t id,const std::string&name,const ScreenAnimationRequest&request,const ScreenAnimationSound&sound,std::string&e){try{
 auto*s=thing(id);auto found=definitions_.find(name);if(!s||s->dead||!s->body->alive||found==definitions_.end()){e="Original live screen animation unsupported: "+name;return false;}
 const bool defaults=!request.frameTarget&&!request.startFrameFraction&&!request.instant&&request.playSpeed==1&&!request.color;
 if(legacyClip(name)){
  if(!defaults){e="Native SetAnimation arguments on legacy screen clip not reconstructed: "+name;return false;}
  if(name==s->body->animation&&s->primary&&registry_->find(s->primary)&&!registry_->find(s->primary)->canceled){s->playback.restartSource();return registry_->restart(s->primary,e);}const auto&d=found->second;const auto cue=text(d,"Sound");const bool playSound=!s->suppressSounds&&!cue.empty()&&sound.worldFrame>=uint64_t(num(d,"SoundAllowedAfterFrame",2));if(playSound&&!sound.play){e="Actual original screen audio provider unavailable";return false;}
  AnimationAllocation a;a.name=name;a.sourceDefinition=d;a.ownerId=id;a.ownerLifetime=s->body->entityHandle;a.ownership=AnimationOwnership::ThingPrimary;a.manager=0;a.type=typeOf(d);a.onlyWhenVisible=flag(d,"OnlyWhenVisible");a.loop=(bank_->clips().at(*bank_->find(name)).flags&ActorLoop)!=0;uint64_t h=0;if(!registry_->allocate(a,h,e))return false;uint32_t newSound=0;if(playSound&&!sound.play(cue,newSound,e))return false;
  if(!releasePrimary(*s,sound,e))return false;s->primary=h;s->sound=newSound;s->playback.play(*bank_,name);ScreenLayer l;l.name=name;l.handle=h;l.legacy=true;l.clip=*bank_->find(name);s->layers={l};s->body->animation=name;s->body->animationPath=text(d,"FilePath");s->body->frame=1;s->body->numFrames=uint32_t(num(d,"NumFrames",1));s->body->graphicsDirty=true;if(!registry_->commitInitialization(h,false,e))return false;return s->timeMultiplier==1||registry_->linkTimeMultiplier(h,id,s->body->entityHandle,e);
 }
 const auto&d=found->second;
 // Thing::SetAnimation 0x2df130: same name and not canceled => Restart.
 // AllowRestart (not in the reviewed records, default false) would rewind;
 // otherwise the current frame and DataPlaySpeed target are kept.
 if(name==s->body->animation&&s->primary&&registry_->find(s->primary)&&!registry_->find(s->primary)->canceled&&!s->layers.empty()&&!s->layers.front().legacy){if(!registry_->restart(s->primary,e))return false;}
 else{
  std::vector<ScreenLayer> layers;if(!allocateLayers(id,*s,name,sound,layers,e))return false;
  if(!releasePrimary(*s,sound,e))return false;s->layers=std::move(layers);s->primary=s->layers.front().handle;s->sound=s->layers.front().sound;
  s->body->animation=name;s->body->animationPath=text(d,"FilePath");s->body->numFrames=uint32_t(num(d,"NumFrames",1));
 }
 auto&primary=s->layers.front();const bool book=typeOf(d)==NativeAnimationType::Book;
 if(typeOf(d)==NativeAnimationType::Slide&&(request.frameTarget||request.startFrameFraction||request.instant)){e="Native SetAnimation frame arguments on screen Slide not reconstructed: "+name;return false;}
 // ScriptAction::SetAnimation 0x261a10 argument order: PlaySpeed multiplier,
 // StartFrameFraction, FrameTarget(+Instant), then Color via Thing SetColor.
 if(request.playSpeed!=1){if(!book){e="Native PlaySpeed multiplier on non-Book screen animation not reconstructed";return false;}primary.multiplier=request.playSpeed;}
 if(request.startFrameFraction&&*request.startFrameFraction!=0&&book)setFractionalFrame(d,primary,frameForFraction(d,*request.startFrameFraction));
 if(request.frameTarget&&*request.frameTarget>=0&&book){
  if(request.instant){setFractionalFrame(d,primary,frameForFraction(d,*request.frameTarget));if(primary.speed!=0)primary.target=float(primary.frame);}
  else if(primary.speed!=0)primary.target=frameForFraction(d,*request.frameTarget);
 }
 if(request.color&&!setColor(id,*request.color,false,e))return false;
 s->body->frame=uint32_t(primary.frame);s->body->graphicsDirty=true;return true;
 }catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::setFrameTarget(int32_t id,const std::string&name,float f,bool instant,std::string&e){try{
 auto*s=thing(id);if(!s||s->dead)return true; // Native returns when the Thing is not active.
 for(auto&l:s->layers){if(l.name!=name)continue;const auto&d=definitions_.at(l.name);if(l.legacy){e="SetAnimationFrameTarget on legacy screen clip not reconstructed";return false;}
  if(typeOf(d)==NativeAnimationType::Slide){e="SetAnimationFrameTarget on screen Slide not reconstructed: "+name;return false;}
  if(typeOf(d)!=NativeAnimationType::Book)continue; // No DataPlaySpeed: PlayToFraction is a no-op.
  if(instant){setFractionalFrame(d,l,frameForFraction(d,f));if(l.speed!=0)l.target=float(l.frame);}else if(l.speed!=0)l.target=frameForFraction(d,f);
 }
 if(!s->layers.empty())s->body->frame=uint32_t(s->layers.front().frame);s->body->graphicsDirty=true;return true;
 }catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::adjustFraction(int32_t id,bool scale,float target,const BodyTransitionOptions&o,std::string&e){auto*s=thing(id);if(!s||s->dead){e="Actual screen Body fraction lifetime unavailable";return false;}bool changed=false;const size_t i=scale?0:1;if(!s->fractions[i].adjust(target,scale?s->body->scaleFraction:s->body->alpha,o,changed,e))return false;s->adjusted[i]=true;if(scale)s->body->scaleFraction=s->fractions[i].current();else s->body->alpha=s->fractions[i].current();s->body->graphicsDirty=true;return true;}
bool ScreenAnimationWorld::adjustChannel(int32_t id,size_t k,float target,const BodyTransitionOptions&o,std::string&e){
 if(k<2)return adjustFraction(id,k==0,target,o,e);auto*s=thing(id);if(!s||s->dead||k>3){e="Actual screen Body scale-vector lifetime unavailable";return false;}
 // BodyComponent::Adjust: initial 1.0 on a channel's first adjust.
 bool changed=false;if(!s->fractions[k].adjust(target,1.f,o,changed,e))return false;s->adjusted[k]=true;s->body->graphicsDirty=true;return true;}
bool ScreenAnimationWorld::moveTo(int32_t id,float x,float y,float duration,const BodyTransitionOptions&o,std::string&e){
 auto*s=thing(id);if(!s||s->dead){e="Actual screen Obstacle move lifetime unavailable";return false;}
 for(float v:{x,y,duration,o.easeIn,o.easeOut})if(!std::isfinite(v)){e="Nonfinite native screen move";return false;}if(duration<0){e="Negative native screen move duration";return false;}
 const auto current=[&](const ScreenMoveAxis&a,float fallback){return a.active?bodyInterpolate(a.start,a.end,a.fraction,s->moveOptions):fallback;};
 if(s->moveX.active&&s->moveY.active){
  // TransitionHelper::SetNewTarget: per axis no-op if end unchanged.
  for(auto[axis,target,loc]:{std::tuple{&s->moveX,x,s->body->x},std::tuple{&s->moveY,y,s->body->y}}){if(axis->end==target)continue;axis->start=current(*axis,loc);axis->end=target;axis->duration=axis->remaining=duration;axis->fraction=0;}
 }else{
  // TransitionHelper::Reset clears ease/smooth.
  s->moveX={true,s->body->x,x,duration,duration,0};s->moveY={true,s->body->y,y,duration,duration,0};s->moveOptions={};
 }
 if(o.easeIn==0&&o.easeOut==0){if(o.smoothStep)s->moveOptions.smoothStep=true;}else{s->moveOptions.easeIn=o.easeIn;s->moveOptions.easeOut=o.easeOut;}
 s->moveOptions.duration=duration;return true;
}
bool ScreenAnimationWorld::setColor(int32_t id,const std::array<float,4>&color,bool base,std::string&e){auto*s=thing(id);if(!s||s->dead){e="Actual screen Color lifetime unavailable";return false;}for(float c:color)if(!std::isfinite(c)||c<0||c>1){e="Actual screen Color outside normalized original bytes";return false;}if(base)s->baseColor=color;else{s->fractionColor=color;s->body->alpha=color[3];}s->body->graphicsDirty=true;return true;}
bool ScreenAnimationWorld::markDead(int32_t id,std::string&e){auto*s=thing(id);if(!s){e="Actual screen destroy lifetime unavailable";return false;}s->dead=true;return true;}
bool ScreenAnimationWorld::disposePending(const ScreenAnimationSound&sound,std::string&e){
 // World.ObstacleManager.Update disposes marked Things before the animation
 // manager's update. Snapshot attachments before releasing the Thing primary;
 // waiting until the later presentation adapter could emit an extra FX/RNG draw.
 for(auto&[id,s]:things_)if(s.dead&&s.body->entityHandle){
  if(preDisposeOwner_&&!preDisposeOwner_(id,e))return false;
  if(!releasePrimary(s,sound,e))return false;
  s.body->alive=s.body->active=false;s.body->entityHandle=0;
 }
 return true;
}
bool ScreenAnimationWorld::advance(float dt,const ScreenAnimationSound&sound,std::string&e,float worldSpeed){try{
 if(!std::isfinite(dt)||dt<0||dt>1||!std::isfinite(worldSpeed)||worldSpeed<=0){e="Actual screen clock outside bound";return false;}if(!disposePending(sound,e))return false;for(auto i=things_.begin();i!=things_.end();){auto&s=i->second;if(s.dead){++i;continue;}
 for(size_t k=0;k<4;++k)if(s.fractions[k].active()){if(!s.fractions[k].advance(dt,worldSpeed,e))return false;if(k==0)s.body->scaleFraction=s.fractions[k].current();else if(k==1)s.body->alpha=s.fractions[k].current();s.body->graphicsDirty=true;}
 // ObstacleManager::Update 0x2c80c0 / UpdateTransitionHelper 0x2c6760.
 if(s.moveX.active&&s.moveY.active){
  for(auto*a:{&s.moveX,&s.moveY}){a->remaining-=dt;a->fraction=a->duration>0?std::clamp(1.f-a->remaining/a->duration,0.f,1.f):1.f;}
  s.body->x=bodyInterpolate(s.moveX.start,s.moveX.end,s.moveX.fraction,s.moveOptions);s.body->y=bodyInterpolate(s.moveY.start,s.moveY.end,s.moveY.fraction,s.moveOptions);s.body->graphicsDirty=true;
  // Clear both only when each axis is done (1802c8867..ae): (dur==0||rem<0) per axis.
  if((s.moveX.duration==0||s.moveX.remaining<0)&&(s.moveY.duration==0||s.moveY.remaining<0)){s.moveX.active=s.moveY.active=false;}
 }
 if(s.primary&&!s.layers.empty()&&s.layers.front().legacy&&text(definitions_.at(s.body->animation),"Type")=="Book"){
  const auto&definition=definitions_.at(s.body->animation);
  // Original selected screen owner is visibly rendered. Honor the actual
  // Thing visibility bit here; wider renderer-cull/group scheduling remains
  // a separate provider rather than a fabricated camera visibility test.
  const bool onlyVisible=definition.get("OnlyWhenVisible")&&definition.get("OnlyWhenVisible")->boolean();
  if(!onlyVisible||s.body->visible){float elapsed=0;const auto*life=registry_->find(s.primary);if(!life||!physicsComponentElapsed(dt,worldSpeed,num(definition,"TimeModifierFraction",-1)>=0?num(definition,"TimeModifierFraction"):1,life->attachedToTimeMultiplier?s.timeMultiplier:1,elapsed,e))return false;auto chain=s.playback.advance(elapsed);if(s.playback.finished()&&!(s.playback.clip()->flags&ActorHoldLastFrame)&&!registry_->cancel(s.primary,e))return false;if(chain){if(!setAnimation(i->first,*chain,sound,e))return false;}else if(s.playback.finished()&&!(s.playback.clip()->flags&ActorHoldLastFrame)){
   // Native Book.UpdateInternal marks canceled on finite completion unless
   // HoldLastFrame; Thing.UpdateAnimation clears a no-Graphic/no-ChainTo
   // primary. BlankObstacle remains a genuine screen Thing anchor.
   if(!s.sourceGraphic.empty()){if(!setAnimation(i->first,s.sourceGraphic,sound,e))return false;}else{if(!releasePrimary(s,sound,e))return false;s.body->animation.clear();s.body->frame=0;s.body->numFrames=0;}
  }else{s.body->frame=s.playback.originalFrame();}s.body->graphicsDirty=true;}
 }else if(s.primary){
  bool finished=false;
  // UpdateChild295900: actual child layers update before their parent.
  for(size_t k=s.layers.size();k-->0;){auto&l=s.layers[k];const auto&d=definitions_.at(l.name);float elapsed=0;const auto*life=registry_->find(l.handle);if(!life||!physicsComponentElapsed(dt,worldSpeed,num(d,"TimeModifierFraction",-1)>=0?num(d,"TimeModifierFraction"):1,life->attachedToTimeMultiplier?s.timeMultiplier:1,elapsed,e))return false;
   if(typeOf(d)==NativeAnimationType::Slide){
    if(l.playback.finished())continue;
    // Slide PostUpdate 0x1802b4a26: one positive-duration slide per update.
    (void)l.playback.advance(elapsed,1,[&](const auto&,const auto&){if(!startSlide(l,d,e))throw std::runtime_error(e);});l.frame=int32_t(l.playback.originalFrame());l.frac=float(l.frame);
    if(l.playback.finished()){
     if(flag(d,"HoldLastFrame")){
      // NextSlide 0x1802b48a2..b0: past the last slide with HoldLastFrame,
      // index=EndFrame (the slide-record count here). That image is only
      // exported when it is a positive-duration slide.
      const auto&slides=d.get("Slides")->array();if(l.playback.originalFrame()!=slides.size())throw std::runtime_error("Native screen Slide HoldLastFrame zero-duration EndFrame image not exported: "+l.name);
     }else{if(k!=0)throw std::runtime_error("Native finished screen child Slide not reconstructed: "+l.name);finished=true;}
    }
    continue;
   }
   if(typeOf(d)==NativeAnimationType::Constant){if(l.durationBound){l.remaining-=elapsed*l.multiplier;l.progress=l.duration>0?std::clamp(1.f-l.remaining/l.duration,0.f,1.f):1.f;if(l.remaining<0&&!flag(d,"HoldLastFrame")){
    // ConstantAnimation::UpdateInternal 0x1802b1bb0: a looping duration
    // record loops (Animation::Loop 0x180296310 ResetTimeRemaining sets
    // TimeRemaining=Duration) instead of finishing.
    if(flag(d,"Loop")){
     l.remaining=l.duration;
     // Portable adaptation: apply queued native component writes immediately;
     // their exact command-buffer flush phase remains unresolved. Keep this
     // frame's cached fraction; only the next ECS pass updates it.
     if((!d.get("ReRandomizeOnLoop")||flag(d,"ReRandomizeOnLoop"))&&num(d,"RandomDurationMin")>0){const float cached=l.progress;if(!initializeDuration(l,d,e))return false;l.progress=cached;}
    }
    else{if(k!=0)throw std::runtime_error("Native finished screen child Constant not reconstructed: "+l.name);finished=true;}}}continue;}
   if(typeOf(d)!=NativeAnimationType::Book)continue;
   // PlaySpeedUpdateSystem::Execute 0x153b80: step toward a positive target
   // (snap when within one step), otherwise free-running playback.
   const float step=l.speed*l.multiplier*elapsed;
   if(l.target>0){if(step>=std::abs(l.target-l.frac))l.frac=l.target;else if(l.target>l.frac)l.frac+=step;else if(l.frac>l.target)l.frac-=step;}
   else l.frac+=step;
   // BookAnimation::UpdateInternal 0x2af530.
   const int32_t start=startFrame(d),end=endFrame(d);l.frame=std::clamp(int32_t(std::trunc(l.frac)),start,end);
   if(!flag(d,"HoldLastFrame")&&(l.frac>=float(end)+1||l.frac<float(start))){
    if(flag(d,"Loop")){l.frac=float(start);l.frame=start;if(num(d,"PlaySpeed")!=0){l.speed=num(d,"PlaySpeed");l.target=0;}continue;}
    if(k!=0)throw std::runtime_error("Native finished screen child Book not reconstructed: "+l.name);
    // Book.UpdateInternal2af725/2af752 marks canceled before Thing completion;
    // unlike Constant expiration this affects same-name Graphic fallback.
    if(!registry_->cancel(l.handle,e))return false;finished=true;
   }
  }
  if(finished){
   // Thing::UpdateAnimation 0x2df602..652: finite primary completion
   // selects ChainTo, then immutable ThingData.Graphic, through the normal setter.
   const auto chain=text(definitions_.at(s.body->animation),"ChainTo");
   if(!chain.empty()&&chain!="null"){if(!setAnimation(i->first,chain,sound,e))return false;}
   else if(!s.sourceGraphic.empty()){if(!setAnimation(i->first,s.sourceGraphic,sound,e))return false;}
   else{if(!releasePrimary(s,sound,e))return false;s.body->animation.clear();s.body->frame=0;s.body->numFrames=0;}
  }
  else s.body->frame=uint32_t(s.layers.front().frame);s.body->graphicsDirty=true;
 }++i;}return true;
 }catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::layerVisual(const ScreenThing&s,const ScreenLayer&l,ActorVisual&out,std::string&e)const{
 out={};out.id=uint32_t(s.body->id);out.lifetime=l.handle;out.screenSpace=s.body->screenSpace;out.sourceBank=bank_.get();out.flipX=s.body->flipHorizontal;
 if(l.legacy){out.frame=bank_->sample(s.playback.clipIndex(),s.playback.slideIndex(),0);out.localRect=out.frame.worldRect(*s.playback.clip());out.x=s.body->x;out.y=s.body->y;out.scale=s.body->scale*s.body->scaleFraction;for(size_t k=0;k<3;++k)out.color[k]=s.baseColor[k]*s.fractionColor[k];out.color[3]=s.baseColor[3]*s.body->alpha;return true;}
 const auto&d=definitions_.at(l.name);const auto&clip=bank_->clips().at(l.clip);const bool book=typeOf(d)==NativeAnimationType::Book,sourceSlide=typeOf(d)==NativeAnimationType::Slide;
 const size_t slide=book?size_t(l.frame-startFrame(d)):(sourceSlide?l.playback.slideIndex():0);if(slide>=clip.slides.size()){e="Actual screen frame outside original resource: "+l.name;return false;}
 out.frame=bank_->sample(l.clip,slide,0);out.localRect=out.frame.worldRect(clip);
 if(l.movie){const float sourceScale=num(d,"Scale",1);out.localRect[0]=(out.localRect[0]-clip.offsetX)/sourceScale;out.localRect[1]=(out.localRect[1]-clip.offsetY)/sourceScale;out.localRect[2]/=sourceScale;out.localRect[3]/=sourceScale;if(out.flipX)out.localRect[0]=-out.localRect[0]-out.localRect[2];}
 out.angle=-animationAngleTransition(animationRadians(num(d,"StartAngle")),animationRadians(num(d,"EndAngle")),l.progress,num(d,"EaseIn"),num(d,"EaseOut"),flag(d,"PingPongAngle"),num(d,"PhaseOffsetAngle"))-(maintain(d,"AngleFromOwner")?s.body->angle:0.f);
 // GetScale 0x298bb0: Data.Scale x own transition x (Maintain) Thing body
 // mScale x Thing ScaleTransition. GetDrawScaleVector 0x298500: Data.ScaleX/Y
 // x (Maintain) Thing ScaleX/ScaleY transitions. Drawable scale = (v.x s, v.y s).
 const float scale=num(d,"Scale",1)*scaleTransition(d,l.progress)*(maintain(d,"ScaleFromOwner")?s.body->scale*s.body->scaleFraction:1.f);
 float vx=num(d,"ScaleX",1)*scaleTransition(d,l.progress,"StartScaleX","EndScaleX")*s.modifier(2),vy=num(d,"ScaleY",1)*scaleTransition(d,l.progress,"StartScaleY","EndScaleY")*s.modifier(3);
 // GetDrawScaleVector 0x2985ef..6e8: largest attached textbox width/height
 // divided by the corresponding scalar, independently of text opacity.
 if(num(d,"AttachedTextScalarX")>0||num(d,"AttachedTextScalarY")>0){
  if(!textDimensions_){e="Actual screen attached-text dimensions provider unavailable";return false;}
  std::array<float,2> dimensions{-1,-1};if(!textDimensions_(s.body->id,dimensions,e))return false;
  if(dimensions[0]>=0&&num(d,"AttachedTextScalarX")>0)vx*=dimensions[0]/num(d,"AttachedTextScalarX");
  if(dimensions[1]>=0&&num(d,"AttachedTextScalarY")>0)vy*=dimensions[1]/num(d,"AttachedTextScalarY");
 }
 out.localRect[0]*=vx;out.localRect[2]*=vx;out.localRect[1]*=vy;out.localRect[3]*=vy;out.scale=scale;
 // Constant screen path adds the offset scaled by the owner body (2b1f73..2b1ff7);
 // Book adds it unscaled (2af8e8..2af93c); Slide vtable5f80d0+10/+18
 // selects the same Book PrepDraw/SubmitDraw, so Slide shares this path.
 // The original HUD geometry fixes the
 // factor as the owner scale only, not Data.Scale: HealthBar-Left (300px,
 // Scale .5, OffsetX 92) spans 17..167, HealthBar-Stretch sits at OffsetX 167
 // and HealthBar-Right (393px, OffsetX 265) spans 166.75..363.25, i.e. the
 // three original pieces abut only with unscaled data offsets.
 const float ownerScale=maintain(d,"ScaleFromOwner")?s.body->scale*s.body->scaleFraction:1.f;
 const float offsetScale=book||sourceSlide?1.f:ownerScale;out.x=s.body->x+(num(d,"OffsetX")+shiftValue(d,"StartOffsetX","EndOffsetX",l.progress))*offsetScale;out.y=s.body->y+(num(d,"OffsetY")+shiftValue(d,"StartOffsetY","EndOffsetY",l.progress))*offsetScale;
 // InitData/GetDrawColor 0x298810: own {mColor.rgb,trunc(mColor.a/255*Alpha*255)};
 // top-level Red/Green/Blue set mColor only when all three are present (>=0).
 std::array<uint32_t,4> c{255,255,255,255};
 // AnimationData Color deserialization 0x92172 and InitData 0x292492:
 // nested Color is the initial packed color, overridden by top-level RGB.
 if(const auto*color=d.get("Color")){size_t k=0;for(const auto*key:{"Red","Green","Blue","Alpha"})c[k++]=uint32_t(std::trunc(std::clamp(num(*color,key,1),0.f,1.f)*255.f));}
 if(d.get("Red")&&d.get("Green")&&d.get("Blue")&&num(d,"Red")>=0&&num(d,"Green")>=0&&num(d,"Blue")>=0){size_t k=0;for(const char*key:{"Red","Green","Blue"})c[k++]=uint32_t(std::trunc(std::clamp(num(d,key),0.f,1.f)*255.f));c[3]=255;}
 c[3]=uint32_t(std::trunc(float(c[3])/255.f*num(d,"Alpha",1)*255.f));
 // InitData 0x180292566..0x1802926e3 channel helpers (Start/End default 0,
 // inactive when equal); their fraction comes from the duration component.
 // PingPongColor (+0xaa) runs the helper forward then back over the duration.
 {const char*starts[]{"StartRed","StartGreen","StartBlue","StartAlpha"};const char*ends[]{"EndRed","EndGreen","EndBlue","EndAlpha"};const float f=l.progress;
  for(size_t k=0;k<4;++k){const float v=flag(d,"PingPongColor")?(f<.5f?colorTransition(d,starts[k],ends[k],f*2):colorTransition(d,ends[k],starts[k],(f-.5f)*2)):colorTransition(d,starts[k],ends[k],f);c[k]=c[k]*uint32_t(std::clamp(int(v*255),0,255))/255;}}
 if(maintain(d,"ColorFromOwner")){const std::array<uint8_t,4> base{byteOf(s.baseColor[0]),byteOf(s.baseColor[1]),byteOf(s.baseColor[2]),byteOf(s.baseColor[3])};const std::array<uint8_t,4> fraction{uint8_t(std::clamp(s.fractionColor[0],0.f,1.f)*255.f),uint8_t(std::clamp(s.fractionColor[1],0.f,1.f)*255.f),uint8_t(std::clamp(s.fractionColor[2],0.f,1.f)*255.f),uint8_t(std::clamp(s.body->alpha,0.f,1.f)*255.f)};for(size_t k=0;k<4;++k){c[k]=c[k]*base[k]/255;c[k]=c[k]*fraction[k]/255;}}
 for(size_t k=0;k<4;++k)out.color[k]=float(std::min<uint32_t>(c[k],255))/255.f;
 // ShouldAddColor 0x296760 -> Drawable shader 13 (AddColor.frag).
 out.addColor=flag(d,"AddColor");return true;
}
bool ScreenAnimationWorld::visual(int32_t id,ActorVisual&out,std::string&e){try{auto*s=thing(id);if(!s||s->dead||!s->body->alive||!s->primary||s->layers.empty()||!registry_->find(s->primary)){e="Actual screen visual lifetime unavailable";return false;}return layerVisual(*s,s->layers.front(),out,e);}catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::visual(int32_t id,uint64_t handle,ActorVisual&out,std::string&e){try{auto*s=thing(id);if(!s||s->dead||!s->body->alive||!s->primary){e="Actual screen visual lifetime unavailable";return false;}for(const auto&l:s->layers)if(l.handle==handle){if(!registry_->find(handle)){e="Actual screen layer lifetime unavailable";return false;}return layerVisual(*s,l,out,e);}e="Actual screen layer lifetime unavailable";return false;}catch(const std::exception&x){e=x.what();return false;}}
}
