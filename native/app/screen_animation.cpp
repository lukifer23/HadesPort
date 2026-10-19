#include "screen_animation.hpp"
#include <algorithm>
#include <cmath>
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
const std::set<std::string> guiKeys{"Name","InheritFrom","FilePath","Type","StartFrame","EndFrame","NumFrames","PlaySpeed","OffsetX","OffsetY","OriginX","OriginY","Scale","ScaleX","ScaleY","Alpha","AddColor","Material","SortMode","Loop","HoldLastFrame","ChildAnimation","AngleFromOwner","ScaleFromOwner","ColorFromOwner","DieWithOwner","Red","Green","Blue","Slides","Sound"};
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
bool ScreenAnimationWorld::load(SourceValue catalog,std::shared_ptr<ActorAnimationBank>bank,std::shared_ptr<AnimationRegistry>registry,std::string&e){try{
 if(!things_.empty()||!bank||!registry||text(catalog,"schema")!="hades-screen-animations-v1"||text(catalog,"target_sha256")!="d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254")throw std::runtime_error("Original screen animation resource identity");
 for(const auto&[name,row]:catalog.get("clips")->object()){
  const auto*definition=row.get("definition");if(!definition)throw std::runtime_error("Screen clip without original definition "+name);
  if(!legacyClip(name)){
   // Reviewed original GUI closure: only reconstructed properties/types.
   for(const auto&[key,value]:definition->object()){(void)value;if(!guiKeys.contains(key))throw std::runtime_error("Unreviewed native screen animation property "+name+"."+key);}
   const auto t=text(*definition,"Type");if(t!="Constant"&&t!="ConstantNoUpdate"&&t!="Book"&&t!="Dataless"&&t!="Slide")throw std::runtime_error("Unreviewed native screen animation type "+name);
   // SlideAnimation (SetupSlideIndex 0x1802b5010 / NextSlide 0x1802b46d0 /
   // SlideStart 0x1802b4080): timing is the bank's per-slide DurationFrames/60
   // with zero-duration slides skipped (ActorAnimationPlayback). Loop/PingPong
   // wrap and draw offsets are not reviewed for GUI slides.
   if(t=="Slide"){const auto bankIndex=bank->find(name);if(!bankIndex||(bank->clips().at(*bankIndex).flags&(ActorBook|ActorLoop))||flag(*definition,"Loop")||num(*definition,"OffsetX")!=0||num(*definition,"OffsetY")!=0)throw std::runtime_error("Unreviewed native screen Slide record "+name);}
   if(t=="Book"&&(startFrame(*definition)<1||endFrame(*definition)<startFrame(*definition)))throw std::runtime_error("Unreviewed native Book frame range "+name);
  }
  if(!bank->find(name))throw std::runtime_error("Screen original pixels unavailable: "+name);definitions_[name]=*definition;}
 for(const char*required:{"RemBGIntro","RemBG","Backgrounds\\rectangle_01","Blank","VignetteOverlay"})if(!definitions_.contains(required))throw std::runtime_error("Incomplete screen original resource graph");
 for(const auto&[name,d]:definitions_){const auto child=text(d,"ChildAnimation");if(!child.empty()&&child!="null"&&!definitions_.contains(child))throw std::runtime_error("Original screen ChildAnimation closure missing: "+child);}
 bank_=std::move(bank);registry_=std::move(registry);return true;
 }catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::create(ObjectSnapshot&body,std::array<float,4>color,const ScreenAnimationSound&sound,std::string&e){
 if(!body.entityHandle||!body.screenSpace||things_.contains(body.id)){e="Actual selected screen Thing allocation unavailable";return false;}ScreenThing s;s.body=&body;s.baseColor=color;things_.emplace(body.id,std::move(s));return body.animation.empty()?true:setAnimation(body.id,body.animation,sound,e);
}
bool ScreenAnimationWorld::releasePrimary(ScreenThing&s,const ScreenAnimationSound&sound,std::string&e){
 if(!s.primary)return true;const auto&d=definitions_.at(s.body->animation);const float fade=num(d,"StopSoundOnFinishFade");if(s.sound&&fade>0&&(!sound.stop||!sound.stop(s.sound,fade,e)))return false;
 // Registry disposal is recursive over the ChildAnimation chain.
 if(!registry_->cancel(s.primary,e)||!registry_->dispose(s.primary,e))return false;s.primary=0;s.sound=0;s.playback.reset();s.layers.clear();return true;
}
bool ScreenAnimationWorld::allocateLayers(int32_t id,ScreenThing&s,const std::string&name,std::vector<ScreenLayer>&out,std::string&e){
 // Animation.InitData creates ChildAnimation before the parent is committed
 // (AddChild 0x293ae0): one separate animation per chained data record.
 std::string next=name;uint64_t parent=0;
 while(!next.empty()&&next!="null"){
  if(out.size()>8){e="Original screen ChildAnimation chain bound";return false;}
  const auto&d=definitions_.at(next);ScreenLayer l;l.name=next;l.clip=*bank_->find(next);
  AnimationAllocation a;a.name=next;a.sourceDefinition=d;a.ownerId=id;a.ownerLifetime=s.body->entityHandle;a.ownership=parent?AnimationOwnership::Child:AnimationOwnership::ThingPrimary;a.parent=parent;a.type=typeOf(d);a.onlyWhenVisible=flag(d,"OnlyWhenVisible");a.loop=(bank_->clips().at(l.clip).flags&ActorLoop)!=0;
  if(!registry_->allocate(a,l.handle,e))return false;
  if(a.type==NativeAnimationType::Book){
   // BookAnimation::SetupFrameUpdateMode 0x2b1040: speed=PlaySpeed, else 60.
   l.speed=num(d,"PlaySpeed",0);if(l.speed==0)l.speed=60;l.target=0;l.multiplier=1;l.frac=float(startFrame(d));l.frame=startFrame(d);
  }else if(a.type==NativeAnimationType::Slide){
   // SetupSlideIndex: index=StartFrame-1 then NextSlide to the first
   // positive-duration slide (exported bank omits zero-duration slides).
   l.playback.play(*bank_,next);l.frame=int32_t(l.playback.originalFrame());l.frac=float(l.frame);
  }else{l.frac=1;l.frame=1;}
  parent=l.handle;out.push_back(std::move(l));next=text(d,"ChildAnimation");
 }
 for(const auto&l:out)if(!registry_->commitInitialization(l.handle,false,e))return false;
 return true;
}
bool ScreenAnimationWorld::setAnimation(int32_t id,const std::string&name,const ScreenAnimationSound&sound,std::string&e){return setAnimation(id,name,ScreenAnimationRequest{},sound,e);}
bool ScreenAnimationWorld::setAnimation(int32_t id,const std::string&name,const ScreenAnimationRequest&request,const ScreenAnimationSound&sound,std::string&e){try{
 auto*s=thing(id);auto found=definitions_.find(name);if(!s||s->dead||!s->body->alive||found==definitions_.end()){e="Original live screen animation unsupported: "+name;return false;}
 const bool defaults=!request.frameTarget&&!request.startFrameFraction&&!request.instant&&request.playSpeed==1&&!request.color;
 if(legacyClip(name)){
  if(!defaults){e="Native SetAnimation arguments on legacy screen clip not reconstructed: "+name;return false;}
  if(name==s->body->animation&&s->primary){s->playback.restartSource();return registry_->restart(s->primary,e);}const auto&d=found->second;const auto cue=text(d,"Sound");const bool playSound=!cue.empty()&&sound.worldFrame>=uint64_t(num(d,"SoundAllowedAfterFrame",2));if(playSound&&!sound.play){e="Actual original screen audio provider unavailable";return false;}
  AnimationAllocation a;a.name=name;a.sourceDefinition=d;a.ownerId=id;a.ownerLifetime=s->body->entityHandle;a.ownership=AnimationOwnership::ThingPrimary;a.type=typeOf(d);a.onlyWhenVisible=flag(d,"OnlyWhenVisible");a.loop=(bank_->clips().at(*bank_->find(name)).flags&ActorLoop)!=0;uint64_t h=0;if(!registry_->allocate(a,h,e))return false;uint32_t newSound=0;if(playSound&&!sound.play(cue,newSound,e))return false;
  if(!releasePrimary(*s,sound,e))return false;s->primary=h;s->sound=newSound;s->playback.play(*bank_,name);ScreenLayer l;l.name=name;l.handle=h;l.legacy=true;l.clip=*bank_->find(name);s->layers={l};s->body->animation=name;s->body->animationPath=text(d,"FilePath");s->body->frame=1;s->body->numFrames=uint32_t(num(d,"NumFrames",1));s->body->graphicsDirty=true;return registry_->commitInitialization(h,false,e);
 }
 const auto&d=found->second;
 // Thing::SetAnimation 0x2df130: same name and not canceled => Restart.
 // AllowRestart (not in the reviewed records, default false) would rewind;
 // otherwise the current frame and DataPlaySpeed target are kept.
 if(name==s->body->animation&&s->primary&&!s->layers.empty()&&!s->layers.front().legacy){if(!registry_->restart(s->primary,e))return false;}
 else{
  std::vector<ScreenLayer> layers;if(!allocateLayers(id,*s,name,layers,e))return false;
  // Animation.InitData 0x180291c70: the animation-level Sound cue plays when
  // the world frame passes SoundAllowedAfterFrame (default 2; not in the
  // reviewed records). Owner audio is non-spatial for screen Things.
  std::string cue;for(const auto&l:layers){const auto c=text(definitions_.at(l.name),"Sound");if(c.empty())continue;if(!cue.empty()){e="Native screen ChildAnimation chain with several Sound cues not reconstructed: "+name;return false;}cue=c;}
  uint32_t newSound=0;if(!cue.empty()&&sound.worldFrame>=2){if(!sound.play){e="Actual original screen audio provider unavailable";return false;}if(!sound.play(cue,newSound,e))return false;}
  if(!releasePrimary(*s,sound,e))return false;s->layers=std::move(layers);s->primary=s->layers.front().handle;s->sound=newSound;
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
bool ScreenAnimationWorld::advance(float dt,const ScreenAnimationSound&sound,std::string&e){try{
 if(!std::isfinite(dt)||dt<0||dt>1){e="Actual screen clock outside bound";return false;}for(auto i=things_.begin();i!=things_.end();){auto&s=i->second;if(s.dead){if(!releasePrimary(s,sound,e))return false;s.body->alive=s.body->active=false;s.body->entityHandle=0;++i;continue;}
 for(size_t k=0;k<4;++k)if(s.fractions[k].active()){if(!s.fractions[k].advance(dt,1,e))return false;if(k==0)s.body->scaleFraction=s.fractions[k].current();else if(k==1)s.body->alpha=s.fractions[k].current();s.body->graphicsDirty=true;}
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
  if(!onlyVisible||s.body->visible){auto chain=s.playback.advance(dt);if(chain){if(!setAnimation(i->first,*chain,sound,e))return false;}else if(s.playback.finished()&&!(s.playback.clip()->flags&ActorHoldLastFrame)){
   // Native Book.UpdateInternal marks canceled on finite completion unless
   // HoldLastFrame; Thing.UpdateAnimation clears a no-Graphic/no-ChainTo
   // primary. BlankObstacle remains a genuine screen Thing anchor.
   if(!releasePrimary(s,sound,e))return false;s.body->animation.clear();s.body->frame=0;s.body->numFrames=0;
  }else{s.body->frame=s.playback.originalFrame();}s.body->graphicsDirty=true;}
 }else if(s.primary){
  bool finished=false;
  for(size_t k=0;k<s.layers.size();++k){auto&l=s.layers[k];const auto&d=definitions_.at(l.name);
   if(typeOf(d)==NativeAnimationType::Slide){
    if(l.playback.finished())continue;
    // Slide PostUpdate 0x1802b4a26: one positive-duration slide per update.
    (void)l.playback.advance(dt);l.frame=int32_t(l.playback.originalFrame());l.frac=float(l.frame);
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
   if(typeOf(d)!=NativeAnimationType::Book)continue;
   // PlaySpeedUpdateSystem::Execute 0x153b80: step toward a positive target
   // (snap when within one step), otherwise free-running playback.
   const float step=l.speed*l.multiplier*dt;
   if(l.target>0){if(step>=std::abs(l.target-l.frac))l.frac=l.target;else if(l.target>l.frac)l.frac+=step;else if(l.frac>l.target)l.frac-=step;}
   else l.frac+=step;
   // BookAnimation::UpdateInternal 0x2af530.
   const int32_t start=startFrame(d),end=endFrame(d);l.frame=std::clamp(int32_t(std::trunc(l.frac)),start,end);
   if(!flag(d,"HoldLastFrame")&&(l.frac>=float(end)+1||l.frac<float(start))){
    if(flag(d,"Loop"))throw std::runtime_error("Native GUI Book Loop() wrap not reconstructed: "+l.name);
    if(k!=0)throw std::runtime_error("Native finished screen child Book not reconstructed: "+l.name);
    finished=true;
   }
  }
  if(finished){if(!releasePrimary(s,sound,e))return false;s.body->animation.clear();s.body->frame=0;s.body->numFrames=0;}
  else s.body->frame=uint32_t(s.layers.front().frame);s.body->graphicsDirty=true;
 }++i;}return true;
 }catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::layerVisual(const ScreenThing&s,const ScreenLayer&l,ActorVisual&out,std::string&e)const{
 out={};out.id=uint32_t(s.body->id);out.lifetime=l.handle;out.screenSpace=true;out.sourceBank=bank_.get();out.flipX=s.body->flipHorizontal;
 if(l.legacy){out.frame=bank_->sample(s.playback.clipIndex(),s.playback.slideIndex(),0);out.localRect=out.frame.worldRect(*s.playback.clip());out.x=s.body->x;out.y=s.body->y;out.scale=s.body->scale*s.body->scaleFraction;for(size_t k=0;k<3;++k)out.color[k]=s.baseColor[k]*s.fractionColor[k];out.color[3]=s.baseColor[3]*s.body->alpha;return true;}
 const auto&d=definitions_.at(l.name);const auto&clip=bank_->clips().at(l.clip);const bool book=typeOf(d)==NativeAnimationType::Book;
 const size_t slide=book?size_t(l.frame-startFrame(d)):(typeOf(d)==NativeAnimationType::Slide?l.playback.slideIndex():0);if(slide>=clip.slides.size()){e="Actual screen frame outside original resource: "+l.name;return false;}
 out.frame=bank_->sample(l.clip,slide,0);out.localRect=out.frame.worldRect(clip);
 // GetScale 0x298bb0: Data.Scale x own transition x (Maintain) Thing body
 // mScale x Thing ScaleTransition. GetDrawScaleVector 0x298500: Data.ScaleX/Y
 // x (Maintain) Thing ScaleX/ScaleY transitions. Drawable scale = (v.x s, v.y s).
 const float scale=num(d,"Scale",1)*(maintain(d,"ScaleFromOwner")?s.body->scale*s.body->scaleFraction:1.f);
 const float vx=num(d,"ScaleX",1)*s.modifier(2),vy=num(d,"ScaleY",1)*s.modifier(3);
 out.localRect[0]*=vx;out.localRect[2]*=vx;out.localRect[1]*=vy;out.localRect[3]*=vy;out.scale=scale;
 // Constant screen path adds the offset scaled by the owner body (2b1f73..2b1ff7);
 // Book adds it unscaled (2af8e8..2af93c). The original HUD geometry fixes the
 // factor as the owner scale only, not Data.Scale: HealthBar-Left (300px,
 // Scale .5, OffsetX 92) spans 17..167, HealthBar-Stretch sits at OffsetX 167
 // and HealthBar-Right (393px, OffsetX 265) spans 166.75..363.25, i.e. the
 // three original pieces abut only with unscaled data offsets.
 const float ownerScale=maintain(d,"ScaleFromOwner")?s.body->scale*s.body->scaleFraction:1.f;
 const float offsetScale=book?1.f:ownerScale;out.x=s.body->x+num(d,"OffsetX")*offsetScale;out.y=s.body->y+num(d,"OffsetY")*offsetScale;
 // InitData/GetDrawColor 0x298810: own {mColor.rgb,trunc(mColor.a/255*Alpha*255)};
 // top-level Red/Green/Blue set mColor only when all three are present (>=0).
 std::array<uint32_t,4> c{255,255,255,255};
 if(d.get("Red")&&d.get("Green")&&d.get("Blue")&&num(d,"Red")>=0&&num(d,"Green")>=0&&num(d,"Blue")>=0){size_t k=0;for(const char*key:{"Red","Green","Blue"})c[k++]=uint32_t(std::trunc(std::clamp(num(d,key),0.f,1.f)*255.f));}
 c[3]=uint32_t(std::trunc(255.f/255.f*num(d,"Alpha",1)*255.f));
 if(maintain(d,"ColorFromOwner")){const std::array<uint8_t,4> base{byteOf(s.baseColor[0]),byteOf(s.baseColor[1]),byteOf(s.baseColor[2]),byteOf(s.baseColor[3])};const std::array<uint8_t,4> fraction{byteOf(s.fractionColor[0]),byteOf(s.fractionColor[1]),byteOf(s.fractionColor[2]),byteOf(s.body->alpha)};for(size_t k=0;k<4;++k){c[k]=c[k]*base[k]/255;c[k]=c[k]*fraction[k]/255;}}
 for(size_t k=0;k<4;++k)out.color[k]=float(std::min<uint32_t>(c[k],255))/255.f;
 // ShouldAddColor 0x296760 -> Drawable shader 13 (AddColor.frag).
 out.addColor=flag(d,"AddColor");return true;
}
bool ScreenAnimationWorld::visual(int32_t id,ActorVisual&out,std::string&e){try{auto*s=thing(id);if(!s||s->dead||!s->body->alive||!s->primary||s->layers.empty()||!registry_->find(s->primary)){e="Actual screen visual lifetime unavailable";return false;}return layerVisual(*s,s->layers.front(),out,e);}catch(const std::exception&x){e=x.what();return false;}}
bool ScreenAnimationWorld::visual(int32_t id,uint64_t handle,ActorVisual&out,std::string&e){try{auto*s=thing(id);if(!s||s->dead||!s->body->alive||!s->primary){e="Actual screen visual lifetime unavailable";return false;}for(const auto&l:s->layers)if(l.handle==handle){if(!registry_->find(handle)){e="Actual screen layer lifetime unavailable";return false;}return layerVisual(*s,l,out,e);}e="Actual screen layer lifetime unavailable";return false;}catch(const std::exception&x){e=x.what();return false;}}
}
