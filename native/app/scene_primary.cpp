#include "scene_primary.hpp"
#include "scene_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef __clang__
#pragma clang fp contract(off)
#endif
namespace hades {
namespace {
float number(const SourceValue& d,const char* key,float fallback=0){const auto* p=d.get(key);if(!p)return fallback;const auto n=p->number();if(!std::isfinite(n)||std::abs(n)>100000)throw std::runtime_error(std::string("Scene primary source numeric bound: ")+key);return float(n);}
bool flag(const SourceValue& d,const char* key){const auto* p=d.get(key);return p&&p->boolean();}
std::string text(const SourceValue& d,const char* key){const auto* p=d.get(key);return p?p->text():"";}
NativeAnimationType type(const std::string& value){if(value=="Book")return NativeAnimationType::Book;if(value=="Constant")return NativeAnimationType::Constant;if(value=="ConstantNoUpdate"||value.empty())return NativeAnimationType::ConstantNoUpdate;if(value=="Dataless")return NativeAnimationType::Dataless;throw std::runtime_error("Unsupported source scene primary type: "+value);}
std::string digest(AssetStore& assets,const char* name){auto file=assets.open(name);if(!file||file->size()!=32)throw std::runtime_error(std::string("Missing primary resource identity: ")+name);std::array<uint8_t,32> bytes{};size_t at=0;while(at<bytes.size()){auto n=file->read(bytes.data()+at,bytes.size()-at);if(!n)throw std::runtime_error("Truncated primary resource identity");at+=n;}const char hex[]="0123456789abcdef";std::string s;for(auto b:bytes){s+=hex[b>>4];s+=hex[b&15];}return s;}
}
bool ScenePrimaryWorld::load(AssetStore& assets,SourceValue catalog,std::shared_ptr<AnimationRegistry> registry,std::string& error){try{
 if(!states_.empty()||!registry||text(catalog,"Schema")!="HadesScenePrimaries1"||text(catalog,"Target")!="d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254")throw std::runtime_error("Invalid/live scene primary catalog");
 if(digest(assets,"scene-sha256.bin")!=text(catalog,"SceneSha256")||digest(assets,"animations-sha256.bin")!=text(catalog,"AnimationSha256"))throw std::runtime_error("Scene primary original resource bank hash mismatch");
 // Read/bounds-check real original RGBA. Discard temporary CPU texture bytes;
 // the existing renderer owns its GPU copies. Keep actual binding metadata.
 scene_=loadGraphicsScene(assets,[](uint32_t,uint32_t w,uint32_t h,std::span<const uint8_t> rgba){if(rgba.size()!=uint64_t(w)*h*4)throw std::runtime_error("Original primary texture extent mismatch");});
 animated_.load(assets,scene_);auxiliary_=std::make_shared<ActorAnimationBank>();auxiliary_->load(assets,"primary-child-frames.bin");
 for(size_t i=0;i<auxiliary_->clips().size();++i)(void)auxiliary_->sample(i,0,0,false).pixels();
 definitions_.clear();for(const auto& [name,d]:catalog.get("Definitions")->object())definitions_.emplace(name,d);
 recipes_.clear();for(const auto& r:catalog.get("Recipes")->array()){
  ScenePrimaryRecipe p;const double ownerId=r.get("OwnerId")->number();if(!std::isfinite(ownerId)||ownerId<1||ownerId>INT32_MAX||std::floor(ownerId)!=ownerId)throw std::runtime_error("Invalid source primary owner id");p.ownerId=int32_t(ownerId);p.graphic=text(r,"Graphic");p.named=flag(r,"Named");p.type=type(text(r,"Type"));const auto* haa=r.get("HaaClip");p.externalHaa=haa&&!std::holds_alternative<std::monostate>(haa->value);
  for(const auto& s:r.get("SceneSprites")->array()){const auto index=s.number();if(index<0||index>=scene_.sprites.size()||std::floor(index)!=index)throw std::runtime_error("Original primary scene binding outside bounds");p.sceneSprites.push_back(uint32_t(index));}
  if(!recipes_.emplace(p.ownerId,std::move(p)).second)throw std::runtime_error("Duplicate original primary owner recipe");}
 catalog_=std::move(catalog);registry_=std::move(registry);spriteOwners_.assign(scene_.sprites.size(),0);faulted_=false;return true;
 }catch(const std::exception& e){error=e.what();return false;}}
const ScenePrimaryRecipe* ScenePrimaryWorld::recipe(int32_t id)const{auto p=recipes_.find(id);return p==recipes_.end()?nullptr:&p->second;}
bool ScenePrimaryWorld::externalEffect(const std::string& name){return name=="DustSpawner"||name=="BatCluster"||name=="BatClusterWait"||name=="TartarusEmberSpawner";}
bool ScenePrimaryWorld::initialize(const std::string& name,const ScenePrimaryRecipe& recipe,const EffectOwnerState& owner,uint64_t parent,const EffectAnimationRandom& random,const EffectBoundedRandom& integers,uint64_t& handle,std::string& error){
 auto found=definitions_.find(name);const bool named=found!=definitions_.end();SourceValue d=named?found->second:SourceValue{SourceValue::Object{}};ScenePrimaryState s;s.name=name;s.definition=d;s.ownerId=recipe.ownerId;s.ownerLifetime=owner.lifetime;s.parent=parent;s.type=named?type(text(d,"Type")):NativeAnimationType::Dataless;s.sceneSprites=parent?std::vector<uint32_t>{}:recipe.sceneSprites;s.loop=flag(d,"Loop");s.loopFramesOnly=flag(d,"LoopFramesOnly");s.allowRestart=flag(d,"AllowRestart");s.holdLastFrame=flag(d,"HoldLastFrame");s.onlyWhenVisible=flag(d,"OnlyWhenVisible");s.ownerImmobile=flag(d,"OwnerImmobile");s.disableWeapons=flag(d,"DisableOwnerWeapons");s.ownerHasNoCollision=flag(d,"OwnerHasNoCollision");s.ownerInvulnerable=flag(d,"OwnerInvulnerable");s.ownerNoArmorCoverage=flag(d,"OwnerNoArmorCoverage");s.ownerNoInvulnerableCoverage=flag(d,"OwnerNoInvulnerableCoverage");s.blockNewOwnerAnims=flag(d,"BlockNewOwnerAnims");s.ownScale=number(d,"Scale",1);s.offsetX=number(d,"OffsetX");s.offsetY=number(d,"OffsetY");s.offsetZ=number(d,"OffsetZ");s.hsv={number(d,"Hue"),number(d,"Saturation"),number(d,"Value")};
 if(externalEffect(name)){error="Scene primary initializer cannot duplicate live VFX primary owner";return false;}
 const auto* resources=catalog_.get("Resources")->get(name);
 if(resources){const auto kind=text(*resources,"Kind");
  if(kind=="hda"){s.resource=PrimaryResourceKind::SceneAnimated;s.resourceIndex=uint32_t(number(*resources,"Index"));if(s.resourceIndex>=animated_.clips().size()){error="Original HDA primary resource outside bounds";return false;}}
  else if(kind=="aux"){s.resource=PrimaryResourceKind::Auxiliary;s.resourceIndex=uint32_t(number(*resources,"Index"));if(s.resourceIndex>=auxiliary_->clips().size()){error="Original child primary resource outside bounds";return false;}if(const auto* scale=resources->get("AtlasScale")){if(scale->array().size()!=2){error="Original primary atlas ratio extent";return false;}for(size_t k=0;k<2;++k){const auto n=scale->array()[k].number();if(!std::isfinite(n)||n<=0||n>16){error="Original primary atlas ratio bound";return false;}s.atlasScale[k]=float(n);}}}
  else if(!s.sceneSprites.empty())s.resource=PrimaryResourceKind::SceneStatic;
 }else if(named){error="Original primary resource descriptor unavailable";return false;}else if(!s.sceneSprites.empty())s.resource=PrimaryResourceKind::SceneStatic;
 // Source-data may alias an already verified actual auxiliary frame by
 // identical original FilePath (ArchCrumbleDelay and Blank). Never reuse the
 // old scene quad as a replacement image or invent a transparent texel.
 if(resources&&text(*resources,"Kind")=="source-data"&&name!=recipe.graphic){
  s.resource=PrimaryResourceKind::Pending;
  for(const auto& [other,resource]:catalog_.get("Resources")->object())if(text(resource,"Kind")=="aux"&&text(resource,"FilePath")==text(*resources,"FilePath")&&number(d,"NumFrames",1)==1){
   s.resource=PrimaryResourceKind::Auxiliary;s.resourceIndex=uint32_t(number(resource,"Index"));
   if(s.resourceIndex>=auxiliary_->clips().size()||auxiliary_->clips()[s.resourceIndex].slides.size()!=1){error="Actual source primary frame alias extent mismatch";return false;}break;
  }
 }
 if(s.type==NativeAnimationType::Constant){
  // Animation.InitData291dc6 chooses native seconds/frames duration, then
  // RandomDouble duration before the TimeRemaining system consumes it.
  const float lo=number(d,"RandomDurationMin"),hi=number(d,"RandomDurationMax");
  s.hasDuration=number(d,"Duration")>0||number(d,"DurationFrames")>0||lo>0||hi>0;
  s.duration=std::max(number(d,"Duration"),number(d,"DurationFrames")/60.f);
  if(lo!=0||hi!=0){double r=0;if(hi<lo||!random||!random(r,error))return false;const volatile float product=float(r)*(hi-lo);s.duration=product+lo;}
  s.durationRemaining=s.duration;
 }
 s.graphicsPending=s.resource==PrimaryResourceKind::Pending;
 AnimationAllocation allocation;allocation.name=name;allocation.sourceDefinition=d;allocation.type=s.type;allocation.ownership=parent?AnimationOwnership::Child:AnimationOwnership::ThingPrimary;allocation.ownerId=s.ownerId;allocation.ownerLifetime=owner.lifetime;allocation.parent=parent;allocation.loop=s.loop;allocation.onlyWhenVisible=s.onlyWhenVisible;allocation.cancelOverCap=flag(d,"CancelOverCap");if(!registry_->allocate(allocation,s.handle,error))return false;
 if(s.type==NativeAnimationType::Book){s.startFrame=number(d,"StartFrame",1);s.endFrame=number(d,"EndFrame",number(d,"NumFrames"));if(s.startFrame<1||s.endFrame<s.startFrame||s.endFrame>10000){error="Original primary Book frame range";return false;}s.frame=s.startFrame;
  // Native Book Init chooses random initial frame BEFORE InitData and the
  // later SetDefaultPlaySpeed. Native end-start bound is max-exclusive.
  if(flag(d,"RandomStartFrame")){const uint32_t bound=uint32_t(s.endFrame-s.startFrame);uint32_t picked=0;if(bound&&(!integers||!integers(bound,picked,error)||picked>=bound))return false;s.frame+=picked;}
 }
 const auto child=text(d,"ChildAnimation");states_.emplace(s.handle,s);
 if(!child.empty()&&child!="null"){uint64_t c=0;if(!initialize(child,recipe,owner,s.handle,random,integers,c,error))return false;states_.at(s.handle).child=c;}
 if(s.type==NativeAnimationType::Book){float speed=number(d,"PlaySpeed",60),lo=number(d,"RandomPlaySpeedMin"),hi=number(d,"RandomPlaySpeedMax");if(lo!=0||hi!=0){if(hi<lo||!random){error="Original primary Book speed RNG/range unavailable";return false;}double v=0;if(!random(v,error)||!std::isfinite(v)||v<0||v>=1)return false;const volatile float product=float(v)*(hi-lo);speed=product+lo;}states_.at(s.handle).playSpeed=speed;}
 // Deferred resource is a real explicit component state, never substitute
 // pixel art. Static/Dataless do not invent a frame-update component. Root
 // reports pending hidden/NPC graphics separately from ready scene resources.
 const bool skip=!parent&&s.type!=NativeAnimationType::ConstantNoUpdate&&s.type!=NativeAnimationType::Dataless&&s.onlyWhenVisible&&s.loop;
 if(!registry_->commitInitialization(s.handle,skip,error))return false;states_.at(s.handle).initialized=true;if(!parent)for(auto index:s.sceneSprites)spriteOwners_.at(index)=s.handle;handle=s.handle;return true;
}
bool ScenePrimaryWorld::create(int32_t id,const std::string& name,const EffectOwnerState& owner,const EffectAnimationRandom& random,const EffectBoundedRandom& integers,uint64_t& handle,std::string& error){try{
 if(faulted_||!registry_||!owner.lifetime||states_.size()>100000){error="Source primary world/lifetime unavailable";return false;}const auto* r=recipe(id);ScenePrimaryRecipe dynamic;
 if(!r){bool allowed=false;if(const auto* graphics=catalog_.get("DynamicGraphics"))for(const auto& graphic:graphics->array())allowed|=graphic.text()==name;if(!allowed||id<=0){error="Original primary owner recipe unavailable";return false;}dynamic.ownerId=id;dynamic.graphic=name;dynamic.named=definitions_.contains(name);dynamic.type=dynamic.named?type(text(definitions_.at(name),"Type")):NativeAnimationType::Dataless;r=&dynamic;}
 if(r->externalHaa&&name==r->graphic){error="HAA primary has its own source playback owner";return false;}
 if(!initialize(name,*r,owner,0,random,integers,handle,error)){faulted_=true;return false;}return true;
 }catch(const std::exception& e){faulted_=true;error=e.what();return false;}}
const ScenePrimaryState* ScenePrimaryWorld::state(uint64_t h)const{auto s=states_.find(h);return s==states_.end()?nullptr:&s->second;}
bool ScenePrimaryWorld::destroy(uint64_t h,std::string& error){auto s=states_.find(h);if(s==states_.end()){error="Actual source scene primary lifetime unavailable";return false;}if(s->second.child&&!destroy(s->second.child,error))return false;if(!registry_->dispose(h,error))return false;for(auto index:s->second.sceneSprites)if(spriteOwners_.at(index)==h)spriteOwners_[index]=0;states_.erase(h);return true;}
bool ScenePrimaryWorld::selectSceneFrame(uint32_t sprite,std::optional<SceneFrameSelection>& out,std::string& error)const{
 out.reset();if(sprite>=spriteOwners_.size()){error="Actual primary source sprite bound";return false;}const auto* s=state(spriteOwners_[sprite]);if(!s)return true;const auto* life=registry_->find(s->handle);if(!life||!life->initialized){error="Actual primary source frame lifetime lost";return false;}if(life->canceled||s->resource!=PrimaryResourceKind::SceneAnimated)return true;
 const auto& clip=animated_.clips().at(s->resourceIndex);const uint32_t source=s->type==NativeAnimationType::Book?uint32_t(s->frame):clip.frames.front().originalNumber;
 for(uint32_t i=0;i<clip.frames.size();++i)if(clip.frames[i].originalNumber==source){out=SceneFrameSelection{s->resourceIndex,i,s->handle};return true;}error="Actual primary source frame unavailable in original HDA";return false;
}
bool ScenePrimaryWorld::submitted(uint32_t sprite,std::string& error){
 if(sprite>=spriteOwners_.size()){error="Actual primary submitted sprite bound";return false;}const auto* root=state(spriteOwners_[sprite]);if(!root)return true;
 // Exact Obstacle.SubmitDraw gate is on its primary, followed by the whole
 // child chain; it does not independently test child OnlyWhenVisible.
 if(!root->onlyWhenVisible||root->type==NativeAnimationType::ConstantNoUpdate||root->type==NativeAnimationType::Dataless)return true;
 for(auto* s=root;s;s=state(s->child))if(!registry_->setSkipUpdate(s->handle,false,error))return false;return true;
}
std::vector<uint64_t> ScenePrimaryWorld::auxiliaryHandles()const{
 std::vector<uint64_t> out;for(const auto& [handle,s]:states_){const auto* life=registry_->find(handle);if(!life||!life->initialized)throw std::runtime_error("Actual auxiliary primary lifetime lost");if(!life->canceled&&s.resource==PrimaryResourceKind::Auxiliary)out.push_back(handle);}return out;
}
bool ScenePrimaryWorld::sampleAuxiliary(uint64_t handle,ActorVisual& out,std::string& error){try{
 const auto* s=state(handle);const auto* life=registry_->find(handle);if(!s||!life||!life->initialized||life->canceled||s->resource!=PrimaryResourceKind::Auxiliary){error="Actual live auxiliary primary unavailable";return false;}
 const auto& clip=auxiliary_->clips().at(s->resourceIndex);const auto sourceFrame=uint32_t(s->frame);size_t slide=0;for(;slide<clip.slides.size();++slide)if(clip.slides[slide].originalFrame==sourceFrame)break;if(slide==clip.slides.size()){error="Original auxiliary source frame unavailable";return false;}
 out.id=uint32_t(s->ownerId);out.lifetime=handle;out.sourceBank=auxiliary_.get();out.frame=auxiliary_->sample(s->resourceIndex,slide,0,false);out.localRect=out.frame.worldRect(clip);
 // Native AtlasMap::GetOrigin divides original origin by source atlas ratio
 // before subtracting trim. Renderer geometry then multiplies each axis by
 // that exact (possibly nonuniform) ratio. Source own/owner scale is separate.
 for(size_t k=0;k<4;++k)out.localRect[k]*=s->atlasScale[k%2];return true;
 }catch(const std::exception&e){error=e.what();return false;}
}
bool ScenePrimaryWorld::replace(uint64_t previous,const std::string& name,const EffectOwnerState& owner,const EffectAnimationRandom& random,const EffectBoundedRandom& integers,uint64_t& current,std::string& error){
 const auto* old=state(previous);const auto* life=registry_->find(previous);if(!old||!life||life->ownerLifetime!=owner.lifetime){error="Actual current primary replacement lifetime unavailable";return false;}if(old->blockNewOwnerAnims&&!old->finished){current=previous;return true;}
 if(old->name==name&&!life->canceled&&!old->finished){if(!registry_->restart(previous,error))return false;auto& s=states_.at(previous);if(s.allowRestart)s.frame=s.startFrame;current=previous;return true;}
 const auto id=old->ownerId;if(!create(id,name,owner,random,integers,current,error))return false;return destroy(previous,error);
}
bool ScenePrimaryWorld::setFrameTarget(int32_t owner,const std::string& name,float fraction,bool instant,std::string& error){
 if(!std::isfinite(fraction)){error="Native primary Book frame target must be finite";return false;}
 for(auto& [handle,s]:states_){(void)handle;if(s.ownerId!=owner||s.name!=name)continue;
  if(s.type==NativeAnimationType::Slide){error="Native frame target on scene Slide not reconstructed: "+name;return false;}
  if(s.type!=NativeAnimationType::Book)continue;
  // Animation.GetFrameForFraction2960e0 / PlayToFraction2961f0,
  // consumed by the same DataPlaySpeed target update as screen Books.
  const float target=std::clamp(s.startFrame+(s.endFrame-s.startFrame+1)*fraction,s.startFrame,s.endFrame);
  if(instant)s.frame=target;
  if(s.playSpeed!=0)s.frameTarget=instant?s.frame:target;
 }
 error.clear();return true;
}
bool ScenePrimaryWorld::advance(float seconds,std::string& error){
 if(faulted_||!std::isfinite(seconds)||seconds<0||seconds>1){error="Invalid/faulted source primary clock";return false;}
 for(auto& [id,s]:states_){
  const auto* life=registry_->find(id);if(!life||!life->initialized){error="Actual source primary lifetime lost";return false;}
  if(life->skipUpdate||life->canceled||s.finished)continue;
  if(s.type==NativeAnimationType::Constant&&s.hasDuration){s.durationRemaining-=seconds;if(s.durationRemaining<0)s.finished=true;}
  if(s.type!=NativeAnimationType::Book)continue;
  const float step=s.playSpeed*seconds;
  if(s.frameTarget>0){if(step>=std::abs(s.frameTarget-s.frame))s.frame=s.frameTarget;else if(s.frameTarget>s.frame)s.frame+=step;else if(s.frameTarget<s.frame)s.frame-=step;}
  else s.frame+=step;
  if(s.frame>=s.endFrame+1){
   if(s.loop||s.loopFramesOnly)s.frame=s.startFrame;
   else if(s.holdLastFrame)s.frame=s.endFrame;
   else{s.frame=s.endFrame;s.finished=true;}
  }
 }
 return true;
}
std::vector<uint64_t> ScenePrimaryWorld::completedPrimaries()const{
 std::vector<uint64_t> result;for(const auto& [handle,s]:states_)if(s.finished)result.push_back(handle);return result;
}
}
