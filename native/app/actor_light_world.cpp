#include "actor_light_world.hpp"
#include "effect_particle.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#ifdef __clang__
#pragma clang fp contract(off)
#endif
namespace hades {namespace {
const SourceValue& need(const SourceValue& v,const char* key){const auto* p=v.get(key);if(!p)throw std::runtime_error(std::string("Original light field unavailable: ")+key);return *p;}
float scalar(const SourceValue& v,const char* key,float compiled){const auto* p=v.get(key);const float n=p?float(p->number()):compiled;if(!std::isfinite(n))throw std::runtime_error("Nonfinite source light scalar");return n;}
bool randomRange(const ActorRandom& random,float lo,float hi,float& out,std::string& error){double r=0;if(!random||!random(r,error)||!std::isfinite(r)||r<0||r>=1){if(error.empty())error="Actual native light stream0 unavailable";return false;}const volatile float product=float(r)*(hi-lo);out=product+lo;return true;}
const SourceValue& property(const ActorWorld& world,int32_t id,const char* key){const auto* p=world.currentProperty(id,key);if(!p)throw std::runtime_error(std::string("Actual Thing light property unavailable: ")+key);return *p;}
}
bool ActorLightWorld::load(SourceValue catalog,std::shared_ptr<ActorAnimationBank> bank,std::shared_ptr<AnimationRegistry> registry,std::string& error){try{
 if(loaded_||!bank||!registry||need(catalog,"Schema").text()!="HadesActorLightResources1"||need(catalog,"Target").text()!="d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254")throw std::runtime_error("Invalid/live actual actor light resources");
 for(const auto& [name,entry]:need(catalog,"Definitions").object()){
  const auto& d=need(entry,"Definition");const auto type=need(d,"Type").text();if((type!="Dataless"&&type!="Constant")||!bank->find(name))throw std::runtime_error("Unsupported/missing actual actor light controller");
  if(type=="Constant"&&name!="LightTorchFlicker")throw std::runtime_error("Unreviewed named actor light controller");definitions_.emplace(name,entry);
 }
 for(size_t i=0;i<bank->clips().size();++i)(void)bank->sample(i,0,0,false).pixels();
 catalog_=std::move(catalog);bank_=std::move(bank);registry_=std::move(registry);loaded_=true;error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}}
bool ActorLightWorld::shadow(const ActorComponents& actor,bool& result,std::string& error)const{try{
 const auto* seed=actor.sourceId?actors_.seed(actor.sourceId):nullptr;
 if(seed){const auto* value=seed->map.get("CreatesShadows");if(!value)throw std::runtime_error("Original MapThing.CreatesShadows unavailable");const auto mode=value->number();if(mode!=0){if(mode!=1&&mode!=2)throw std::runtime_error("Unknown original shadow tri-state");result=mode==2;error.clear();return true;}}
 result=property(actors_,actor.id,"Thing.CreatesShadows").boolean();error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}}
const ActorLightState* ActorLightWorld::state(int32_t id)const{const auto* actor=actors_.actor(id);if(!actor)return nullptr;const auto i=lights_.find(actor->entityHandle);return i==lights_.end()?nullptr:&i->second;}
bool ActorLightWorld::admit(int32_t id,const ActorRandom& random,std::string& error){try{
 if(faulted_||!loaded_){error=faulted_?fault_:"Original actor light catalog unavailable";return false;}
 auto* actor=actors_.actor(id);if(!actor||!actor->registered||!actor->object)throw std::runtime_error("Actor light requires actual registered owner/body");
 const auto name=property(actors_,id,"Thing.Light").text();if(name.empty()){error.clear();return true;}
 if(const auto* existing=state(id)){if(existing->name!=name)throw std::runtime_error("Actor light replacement requires actual old LightSystem removal");return locationChanged(id,error);}
 const auto definition=definitions_.find(name);
 if(definition==definitions_.end()){
  // Per-owner loud failure without faulting the whole light world: other
  // rooms list unreviewed controllers (e.g. Book lights) in Unsupported.
  const auto* unsupported=catalog_.get("Unsupported");const auto* reason=unsupported?unsupported->get(name):nullptr;
  error="Original actor light controller not reconstructed: "+name+(reason?" ("+reason->text()+")":" (not exported)");return false;}
 const auto& entry=definition->second;const auto& d=need(entry,"Definition");ActorLightState s;s.ownerId=id;s.ownerLifetime=actor->entityHandle;s.name=name;s.imageClip=*bank_->find(name);s.named=need(entry,"Named").boolean();s.constant=need(d,"Type").text()=="Constant";
 if(!shadow(*actor,s.createsShadow,error))return false;
 AnimationAllocation allocation;allocation.name=name;allocation.sourceDefinition=d;allocation.type=s.constant?NativeAnimationType::Constant:NativeAnimationType::Dataless;allocation.ownership=AnimationOwnership::LightController;allocation.manager=1;allocation.ownerId=id;allocation.ownerLifetime=actor->entityHandle;
 if(!registry_->allocate(allocation,s.controller,error))return false;
 if(s.constant){
  // Native InitData duration precedes alpha random draw. Selected controller
  // has no motion, sound, child, vfx, play-speed/scale ranges or random frame.
  if(!randomRange(random,scalar(d,"RandomDurationMin",0),scalar(d,"RandomDurationMax",0),s.duration,error)){faulted_=true;fault_=error;return false;}s.remaining=s.duration;
  if(!randomRange(random,scalar(d,"AlphaMin",0),scalar(d,"AlphaMax",0),s.baseAlpha,error)){faulted_=true;fault_=error;return false;}
  if(s.duration<=0)throw std::runtime_error("Source flicker duration must remain positive");
 }
 if(!registry_->commitInitialization(s.controller,false,error)){faulted_=true;fault_=error;return false;}
 lights_.emplace(s.ownerLifetime,s);return locationChanged(id,error);
 }catch(const std::exception& e){faulted_=true;fault_=e.what();error=fault_;return false;}}
bool ActorLightWorld::locationChanged(int32_t id,std::string& error){try{
 const auto* actor=actors_.actor(id);if(!actor||!actor->registered)throw std::runtime_error("Actual light owner location unavailable");auto i=lights_.find(actor->entityHandle);
 if(i==lights_.end()){if(property(actors_,id,"Thing.Light").text().empty()){error.clear();return true;}throw std::runtime_error("Thing.Light location update requires actual prior light admission");}
 if(!shadow(*actor,i->second.createsShadow,error))return false;
 ++i->second.locationRevision;error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}}
bool ActorLightWorld::advanceControllers(float dt,const ActorRandom& random,std::string& error){try{
 if(faulted_){error=fault_;return false;}if(!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Actual light clock outside bounded phase");
 for(auto& [lifetime,s]:lights_){const auto* actor=actors_.actor(s.ownerId);if(!actor||!actor->registered||actor->entityHandle!=lifetime)throw std::runtime_error("Light owner retired without actual LightSystem removal");if(!s.constant)continue;
  const auto* registry=registry_->find(s.controller);if(!registry||!registry->initialized)throw std::runtime_error("Actual light controller lifetime lost");if(registry->skipUpdate)continue;
  if(actor->timeMultiplier!=1||actor->timeModifierFraction!=1)throw std::runtime_error("Nondefault actor light clock needs native modified-time projection");
  s.remaining-=dt;s.fraction=std::clamp(1-s.remaining/s.duration,0.f,1.f);
  // Constant.UpdateInternal has strict negative expiry, Loop(false) resets
  // TimeRemaining to newly randomized Duration; overshoot is not carried.
  if(s.remaining<0){const auto& d=need(definitions_.at(s.name),"Definition");if(!randomRange(random,scalar(d,"RandomDurationMin",0),scalar(d,"RandomDurationMax",0),s.duration,error)){faulted_=true;fault_=error;return false;}s.remaining=s.duration;}
 }
 error.clear();return true;
 }catch(const std::exception& e){faulted_=true;fault_=e.what();error=fault_;return false;}}
bool ActorLightWorld::visual(int32_t id,const std::array<uint8_t,4>& ownerColor,ActorLightVisual& out,std::string& error){try{
 const auto* s=state(id);const auto* actor=actors_.actor(id);if(!s||!actor||!actor->registered||!actor->object||actor->entityHandle!=s->ownerLifetime)throw std::runtime_error("Actual actor light draw lifetime unavailable");const auto& d=need(definitions_.at(s->name),"Definition");const auto& clip=bank_->clips().at(s->imageClip);
 ActorLightVisual result;auto& image=result.image;image.id=uint32_t(id);image.lifetime=s->controller;image.sourceBank=bank_.get();image.frame=bank_->sample(s->imageClip,0,0,false);image.localRect=image.frame.worldRect(clip);
 const auto& ratio=need(need(catalog_,"Clips").array().at(s->imageClip),"AtlasRatio").array();for(size_t k=0;k<4;++k)image.localRect[k]*=float(ratio[k%2].number());
 if(!actors_.location(id,image.x,image.y,error)||!actors_.zLocation(id,image.z,error))return false;
 image.scale=actor->object->scale*actor->object->scaleFraction*scalar(d,"Scale",1);image.angle=-actor->object->angle;image.flipX=actor->object->flipHorizontal;
 const auto* seed=actor->sourceId?actors_.seed(actor->sourceId):nullptr;if(seed){const auto* flip=seed->map.get("FlipVertical");if(!flip)throw std::runtime_error("Actual light owner vertical flip unavailable");image.flipY=flip->boolean();}
 float alpha=1;if(s->constant){const float easeIn=scalar(d,"EaseIn",0),easeOut=scalar(d,"EaseOut",0);const float start=scalar(d,"StartAlpha",0),end=scalar(d,"EndAlpha",0);alpha=s->fraction<.5f?EffectParticleWorld::interpolate(start,end,s->fraction*2,easeIn,easeOut):EffectParticleWorld::interpolate(end,start,(s->fraction-.5f)*2,easeIn,easeOut);}
 const uint32_t baseAlpha=uint32_t(std::clamp(int(s->baseAlpha*255),0,255));const uint32_t fractionAlpha=uint32_t(std::clamp(int(alpha*255),0,255));
 for(size_t c=0;c<4;++c){uint32_t value=ownerColor[c];if(c==3){value=value*baseAlpha/255;value=value*fractionAlpha/255;}image.color[c]=float(value)/255;}
 image.hsv=actor->hsv;result.createsShadow=s->createsShadow;result.parallax=actor->ownParallax*actor->mapParallax;out=std::move(result);error.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}}
bool ActorLightWorld::remove(int32_t id,std::string& error){const auto* actor=actors_.actor(id);if(!actor){error="Light removal requires retained actor lifetime";return false;}const auto i=lights_.find(actor->entityHandle);if(i==lights_.end()){error.clear();return true;}if(!registry_->dispose(i->second.controller,error))return false;lights_.erase(i);error.clear();return true;}
std::vector<ActorLightState> ActorLightWorld::snapshot()const{std::vector<ActorLightState> out;for(const auto& [handle,s]:lights_){(void)handle;out.push_back(s);}return out;}
}
