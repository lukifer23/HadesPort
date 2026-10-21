#include "effect_animation.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>
#include <stdexcept>

#ifdef __clang__
#pragma clang fp contract(off)
#endif

namespace hades {
namespace {
struct MutationScope {bool& active;explicit MutationScope(bool& flag):active(flag){active=true;}~MutationScope(){active=false;}};
const SourceValue& required(const SourceValue& v,const std::string& key){auto* p=v.get(key);if(!p)throw std::runtime_error("Effect catalog missing "+key);return *p;}
float number(const SourceValue& v){const double d=v.number();if(!std::isfinite(d)||std::abs(d)>1.e9)throw std::runtime_error("Effect number outside bounds");return static_cast<float>(d);}
std::array<float,3> emissionPosition(const std::array<float,3>& from,const std::array<float,3>& to,float remaining,float delta){
 // Exact selected original linear branch: RadialInterpolationLength=0 and
 // VisualFxUseLocationSincePrevSpawn=false. Native division by zero clamps t0.
 const float t=delta>0?std::clamp(1-std::abs(remaining/delta),0.f,1.f):0.f;
 return {(to[0]-from[0])*t+from[0],(to[1]-from[1])*t+from[1],(to[2]-from[2])*t+from[2]};
}
bool ownerValid(const EffectOwnerState& o){return o.lifetime&&std::isfinite(o.x)&&std::isfinite(o.y)&&std::isfinite(o.z)&&std::isfinite(o.angle)&&std::isfinite(o.scale)&&o.scale>=0;}
bool randomRange(const EffectAnimationRandom& random,float lo,float hi,float& out,std::string& error){double value=0;if(!random||!random(value,error))return false;if(!std::isfinite(value)||value<0||value>=1){error="Native effect RandomDouble must return [0,1)";return false;}const volatile float scaled=static_cast<float>(value)*(hi-lo);out=scaled+lo;return true;}
}
bool EffectAnimationWorld::loadCatalog(SourceValue catalog,std::string& error){
 try{
  if(!entities_.empty()||inMutation_)throw std::runtime_error("Cannot replace live effect catalog");
  if(required(catalog,"Schema").text()!="HadesEffectInitialization1")throw std::runtime_error("Effect catalog schema mismatch");
  SourceValue defaults=required(catalog,"NativeDefaults");const auto& texture=required(catalog,"Texture");EffectTexture tex;tex.name=required(texture,"Name").text();tex.atlas=required(texture,"Atlas").text();tex.width=static_cast<uint32_t>(number(required(texture,"Width")));tex.height=static_cast<uint32_t>(number(required(texture,"Height")));
  const auto& rgba=required(texture,"RGBA").array();if(tex.name!="Dev\\blank_invisible"||tex.width!=1||tex.height!=1||rgba.size()!=4)throw std::runtime_error("Unsupported original effect texture");
  for(size_t i=0;i<4;++i){const double x=rgba[i].number();if(x<0||x>255||std::floor(x)!=x)throw std::runtime_error("Invalid original effect texel");tex.rgba[i]=static_cast<uint8_t>(x);}if(tex.rgba[3])throw std::runtime_error("Expected original invisible texel");
  auto integers=[&](const char* key,auto& out){const auto& a=required(texture,key).array();if(a.size()!=out.size())throw std::runtime_error("Effect atlas shape");for(size_t i=0;i<a.size();++i)out[i]=static_cast<int32_t>(number(a[i]));};integers("AtlasRect",tex.atlasRect);integers("Trim",tex.trim);integers("OriginalSize",tex.originalSize);
  std::map<std::string,EffectDefinition> definitions;
  for(const auto& [name,source]:required(catalog,"Definitions").object()){
   if(definitions.size()>=256)throw std::runtime_error("Effect catalog count bound");
   EffectDefinition d;d.name=name;d.source=source;
   auto get=[&](const std::string& k)->const SourceValue&{if(auto* v=source.get(k))return *v;return required(defaults,k);};
   d.type=source.get("Type")?source.get("Type")->text():"ConstantNoUpdate";d.graphic=source.get("FilePath")?source.get("FilePath")->text():"";d.group=get("GroupName").text();d.child=get("ChildAnimation").text();d.vfx=get("VisualFx").text();d.marker=get("UseAttachedMarkerLocation").text();d.scale=number(get("Scale"));d.offsetX=number(get("OffsetX"));d.offsetY=number(get("OffsetY"));d.offsetZ=number(get("OffsetZ"));d.intervalMin=number(get("VisualFxIntervalMin"));d.intervalMax=number(get("VisualFxIntervalMax"));d.distanceMin=number(get("VisualFxDistanceMin"));d.distanceMax=number(get("VisualFxDistanceMax"));d.distanceMinChange=number(get("VisualFxDistanceMinChange"));d.managerCap=static_cast<int32_t>(number(get("VisualFxManagerCap")));d.activeVfxCap=static_cast<int32_t>(number(get("ActiveVisualFxCap")));d.totalVfxCap=static_cast<int32_t>(number(get("VisualFxCap")));d.frameCap=static_cast<int32_t>(number(get("VisualFxFrameCap")));d.loop=get("Loop").boolean();d.onlyWhenVisible=source.get("OnlyWhenVisible")&&source.get("OnlyWhenVisible")->boolean();d.alpha=source.get("Alpha")?number(*source.get("Alpha")):1;d.duration=source.get("Duration")?number(*source.get("Duration")):0;d.durationMin=source.get("RandomDurationMin")?number(*source.get("RandomDurationMin")):0;d.durationMax=source.get("RandomDurationMax")?number(*source.get("RandomDurationMax")):0;d.chain=source.get("ChainTo")?source.get("ChainTo")->text():"";
   if(d.intervalMin<0||d.intervalMax<d.intervalMin||d.distanceMin<0||d.distanceMax<d.distanceMin||d.scale<=0||d.scale>16||d.managerCap<0||d.activeVfxCap<0||d.totalVfxCap<0||d.frameCap<0)throw std::runtime_error("Effect definition bounds");definitions.emplace(name,std::move(d));
  }
  for(const auto& [name,d]:definitions){if(!d.child.empty()&&d.child!="null"&&!definitions.contains(d.child))throw std::runtime_error("Missing source child "+d.child);if(!d.vfx.empty()&&d.vfx!="null"&&!definitions.contains(d.vfx))throw std::runtime_error("Missing source VFX "+d.vfx);}
  definitions_=std::move(definitions);defaults_=std::move(defaults);texture_=std::move(tex);nextHandle_=1;faulted_=false;fault_.clear();return true;
 }catch(const std::exception& e){error=e.what();return false;}
}
bool EffectAnimationWorld::setRegistry(std::shared_ptr<AnimationRegistry> registry,std::string& error){if(!registry||!entities_.empty()||inMutation_){error="Cannot replace live native animation registry";return false;}registry_=std::move(registry);return true;}
bool EffectAnimationWorld::preflightCreate(const std::string& name,std::vector<std::string>& path,std::string& error)const{
 auto it=definitions_.find(name);if(it==definitions_.end()){error="Unadmitted original effect definition: "+name;return false;}const auto& d=it->second;
 if(std::find(path.begin(),path.end(),name)!=path.end()||path.size()>=32){error="Original effect child cycle/depth bound";return false;}
 if(d.type!="Constant"||d.graphic!=texture_.name||!d.loop){error="Unsupported native effect initializer: "+name+" type="+d.type;return false;}
 static const std::set<std::string> fields{"Name","Type","ChildAnimation","FilePath","GroupName","VisualFx","EndFrame","StartFrame","NumFrames","Loop","VisualFxDistanceMax","VisualFxDistanceMin","VisualFxFrameCap","VisualFxManagerCap","UseAttachedMarkerLocation","VisualFxIntervalMin","VisualFxIntervalMax","InheritFrom"};
 for(const auto& [key,value]:d.source.object()){(void)value;if(!fields.contains(key)){error="Unsupported native Constant initializer property: "+name+"."+key;return false;}}
 for(const auto* key:{"NumFrames","StartFrame","EndFrame"})if(required(d.source,key).number()!=1){error="Constant frame scope differs";return false;}
 path.push_back(name);if(!d.child.empty()&&!preflightCreate(d.child,path,error))return false;path.pop_back();return true;
}
bool EffectAnimationWorld::initialize(const std::string& name,int32_t ownerId,const EffectOwnerState& owner,uint64_t parent,const EffectAnimationRandom& random,std::map<uint64_t,EffectAnimationEntity>& stage,uint64_t& handle,std::string& error,AnimationOwnership rootOwnership,const EffectTexture* primaryTexture){
 const auto& d=definitions_.at(name);AnimationAllocation allocation;allocation.name=name;allocation.sourceDefinition=d.source;allocation.type=NativeAnimationType::Constant;allocation.ownership=parent?AnimationOwnership::Child:rootOwnership;allocation.ownerId=ownerId;allocation.ownerLifetime=owner.lifetime;allocation.parent=parent;allocation.loop=d.loop;allocation.onlyWhenVisible=d.source.get("OnlyWhenVisible")&&d.source.get("OnlyWhenVisible")->boolean();allocation.cancelOverCap=d.source.get("CancelOverCap")&&d.source.get("CancelOverCap")->boolean();if(!registry_->allocate(allocation,handle,error))return false;EffectAnimationEntity e;e.handle=handle;e.parent=parent;e.ownerId=ownerId;e.ownerLifetime=owner.lifetime;e.definition=&d;e.texture=primaryTexture?primaryTexture:&texture_;e.location={owner.x,owner.y,owner.z};e.ownScale=d.scale;e.ownerScale=owner.scale;e.angle=0;e.registered=true;e.managerVectorMember=parent==0&&rootOwnership==AnimationOwnership::ManagerCreate;
 if(!d.vfx.empty()){if(d.intervalMin||d.intervalMax)e.spawner=EffectSpawnerKind::Time;else if(d.distanceMin||d.distanceMax)e.spawner=EffectSpawnerKind::Distance;}
 if(d.duration>0||d.durationMin>0||d.durationMax>0){e.hasDuration=true;e.duration=d.duration;if(d.durationMin||d.durationMax)if(!randomRange(random,d.durationMin,d.durationMax,e.duration,error))return false;e.durationRemaining=e.duration;}stage.emplace(handle,e);
 // Exact native InitData: create/initialize ChildAnimation before drawing this
 // animation's initial random time/distance interval.
 if(!d.child.empty()){uint64_t child=0;if(!initialize(d.child,ownerId,owner,handle,random,stage,child,error))return false;stage.at(handle).child=child;}
 auto& current=stage.at(handle);
 if(d.intervalMin||d.intervalMax)if(!randomRange(random,d.intervalMin,d.intervalMax,current.timeRemaining,error))return false;
 if(d.distanceMin||d.distanceMax)if(!randomRange(random,d.distanceMin,d.distanceMax,current.distanceRemaining,error))return false;
 return registry_->commitInitialization(handle,false,error);
}
bool EffectAnimationWorld::create(const std::string& name,int32_t ownerId,const EffectOwnerProvider& owners,const EffectAnimationRandom& random,uint64_t& output,std::string& error){
 bool started=false;
 try{
  if(inMutation_||faulted_){error=faulted_?fault_:"Reentrant effect creation";return false;}
  MutationScope scope(inMutation_);
  std::vector<std::string> path;if(!preflightCreate(name,path,error))return false;if(!owners||!random){error="Actual effect owner and native RNG providers required";return false;}
  EffectOwnerState owner;if(!owners(ownerId,owner,error)||!ownerValid(owner)){if(error.empty())error="Unresolved live effect owner";return false;}
  if(entities_.size()+2>100000){error="Effect entity budget exceeded";return false;}
  std::map<uint64_t,EffectAnimationEntity> stage;uint64_t handle=0;started=true;
  if(!initialize(name,ownerId,owner,0,random,stage,handle,error)){inMutation_=false;faulted_=true;fault_="Effect native initialization failed after RNG/handle allocation: "+error;error=fault_;return false;}
  std::optional<uint64_t> victim;if(!registry_->addToManager(handle,80,victim,error)||victim){faulted_=true;fault_="Native managed Constant admission requires unsupported cancellation adapter: "+error;error=fault_;return false;}entities_.merge(stage);inMutation_=false;output=handle;return true;
 }catch(const std::exception& e){inMutation_=false;if(started){faulted_=true;fault_="Effect initialization threw after handle allocation: "+std::string(e.what());error=fault_;}else error=e.what();return false;}
}
bool EffectAnimationWorld::createPrimary(const std::string& name,int32_t ownerId,const EffectOwnerProvider& owners,const EffectAnimationRandom& random,EffectTexture original,uint64_t& output,std::string& error){
 bool started=false;try{
  if(inMutation_||faulted_){error=faulted_?fault_:"Reentrant map primary initialization";return false;}MutationScope scope(inMutation_);
  const auto it=definitions_.find(name);if(it==definitions_.end()){error="Original map primary definition unavailable";return false;}const auto& d=it->second;
  if((name!="DustSpawner"&&name!="BatCluster"&&name!="TartarusEmberSpawner"&&name!="BatClusterWait")||d.type!="Constant"||d.loop||!d.child.empty()||!d.onlyWhenVisible||d.distanceMin||d.distanceMax||d.marker.size()){error="Unsupported original map Constant primary initializer";return false;}
  static const std::set<std::string> fields{"Name","Type","FilePath","VisualFx","VisualFxManagerCap","EndFrame","NumFrames","StartFrame","Ambient","VisualFxIntervalMax","VisualFxIntervalMin","OnlyWhenVisible","ChainTo","RandomDurationMin","RandomDurationMax","UseAttachedSpriteEffects","VisualFxCap","Alpha","Duration"};
  for(const auto& [key,value]:d.source.object()){(void)value;if(!fields.contains(key)){error="Unsupported original map primary property: "+key;return false;}}
  if(!owners||!random||original.name!=d.graphic||original.atlas.empty()||!original.width||!original.height||original.width>4096||original.height>4096||original.pixels.size()!=uint64_t(original.width)*original.height*4||original.pixels.size()>4*1024*1024){error="Actual source primary image/owner/native RNG unavailable";return false;}
  EffectOwnerState owner;if(!owners(ownerId,owner,error)||!ownerValid(owner)){if(error.empty())error="Actual source primary owner lifetime unavailable";return false;}
  if(d.duration<0||d.durationMin<0||d.durationMax<d.durationMin){error="Original Constant duration bounds";return false;}
  auto [image,inserted]=primaryTextures_.emplace(original.name,std::move(original));(void)inserted;
  std::map<uint64_t,EffectAnimationEntity> stage;uint64_t handle=0;started=true;
  if(!initialize(name,ownerId,owner,0,random,stage,handle,error,AnimationOwnership::ThingPrimary,&image->second)){faulted_=true;fault_="Map primary init failed after RNG/allocation: "+error;error=fault_;return false;}
  // Native OnAnimationSet: Type2, screen=false, Loop=false => no initial SkipUpdate.
  entities_.merge(stage);output=handle;return true;
 }catch(const std::exception& ex){if(started){faulted_=true;fault_="Map primary init threw after native allocation: "+std::string(ex.what());error=fault_;}else error=ex.what();return false;}}
bool EffectAnimationWorld::setSkipUpdate(uint64_t handle,bool value,std::string& error){if(inMutation_||!entities_.contains(handle)){error="Actual source animation visibility lifetime unavailable/reentrant";return false;}return registry_->setSkipUpdate(handle,value,error);}
bool EffectAnimationWorld::setDurationRemaining(uint64_t handle,float remaining,std::string& error){auto i=entities_.find(handle);if(inMutation_||i==entities_.end()||!i->second.hasDuration||!std::isfinite(remaining)){error="Actual native TimeRemaining component unavailable/reentrant";return false;}i->second.durationRemaining=remaining;return true;}
bool EffectAnimationWorld::attachParticleSpawner(uint64_t handle,const std::string& name,const std::array<float,3>& location,float ownScale,float ownerScale,float heading,float duration,const EffectAnimationRandom& random,std::string& error){
 if((inMutation_&&!admittingParticleComponent_)||faulted_||entities_.contains(handle)){error="Reentrant/duplicate native particle TimeSpawner admission";return false;}const auto* a=registry_->find(handle);auto d=definitions_.find(name);
 if(!a||a->name!=name||a->type!=NativeAnimationType::Constant||d==definitions_.end()||!name.starts_with("TartarusEmber")||d->second.vfx!="TartarusEmber01B"||d->second.intervalMin<=0||d->second.intervalMax<d->second.intervalMin||!d->second.marker.empty()||d->second.distanceMin||d->second.distanceMax){error="Actual selected particle TimeSpawner source/lifetime unavailable";return false;}
 EffectAnimationEntity e;e.handle=handle;e.ownerId=a->ownerId;e.ownerLifetime=a->ownerLifetime;e.definition=&d->second;e.registered=true;e.particleComponent=true;e.spawner=EffectSpawnerKind::Time;e.location=location;e.ownScale=ownScale;e.ownerScale=ownerScale;e.angle=heading;e.duration=duration;e.durationRemaining=duration;e.hasDuration=true;
 if(!randomRange(random,d->second.intervalMin,d->second.intervalMax,e.timeRemaining,error))return false;entities_.emplace(handle,std::move(e));return true;
}
bool EffectAnimationWorld::syncParticleSpawner(uint64_t handle,const std::array<float,3>& location,float ownScale,float ownerScale,float heading,float remaining,bool done,std::string& error){auto i=entities_.find(handle);const auto* a=registry_->find(handle);if((inMutation_&&!admittingParticleComponent_)||i==entities_.end()||!i->second.particleComponent||!a||!a->initialized){error="Actual native particle component/lifetime unavailable";return false;}for(float f:{location[0],location[1],location[2],ownScale,ownerScale,heading,remaining})if(!std::isfinite(f)){error="Nonfinite particle TimeSpawner state";return false;}auto& e=i->second;e.location=location;e.ownScale=ownScale;e.ownerScale=ownerScale;e.angle=heading;e.durationRemaining=remaining;e.done=done;e.managerVectorMember=a->managerMember;return true;}
bool EffectAnimationWorld::detachParticleSpawner(uint64_t handle,std::string& error){auto i=entities_.find(handle);if(inMutation_||i==entities_.end()||!i->second.particleComponent){error="Actual native particle component unavailable/reentrant";return false;}entities_.erase(i);return true;}
bool EffectAnimationWorld::advancePrimaryDurations(float seconds,const EffectOwnerProvider& owners,const EffectAnimationRandom& random,std::vector<EffectPrimaryReplacement>& replacements,std::string& error){
 if(inMutation_||faulted_||!std::isfinite(seconds)||seconds<0||seconds>1){error="Invalid/reentrant primary duration phase";return false;}struct Step{uint64_t id;float remaining;};std::vector<Step> steps;
 for(const auto& [id,e]:entities_){const auto* a=registry_->find(id);if(!a||!a->initialized){error="Primary duration actual lifetime unavailable";return false;}if(!e.hasDuration||a->ownership!=AnimationOwnership::ThingPrimary||a->skipUpdate||a->canceled)continue;
  if(e.definition->name!="BatCluster"&&e.definition->name!="BatClusterWait"){error="Unsupported original primary Duration/ChainTo class";return false;}EffectOwnerState owner;if(!owners||!owners(e.ownerId,owner,error)||owner.lifetime!=e.ownerLifetime)return false;
  float elapsed=seconds;if(elapsed_&&!elapsed_(e,seconds,elapsed,error))return false;
  const float remaining=e.durationRemaining-elapsed;if(remaining<0&&(e.definition->chain!="BatCluster"&&e.definition->chain!="BatClusterWait")){error="Original primary duration ChainTo unavailable";return false;}steps.push_back({id,remaining});}
 for(const auto& step:steps){auto& e=entities_.at(step.id);if(step.remaining>=0){e.durationRemaining=step.remaining;continue;}const auto name=e.definition->chain;const auto owner=e.ownerId;const EffectTexture image=*e.texture;uint64_t next=0;
  // Native Thing.SetAnimation creates/initializes replacement before Release
  // of the old primary. Both source classes use the same original arrow atlas.
  if(!createPrimary(name,owner,owners,random,image,next,error))return false;if(!destroy(step.id,error))return false;replacements.push_back({owner,step.id,next,name});}
 return true;
}
const EffectAnimationEntity* EffectAnimationWorld::entity(uint64_t handle)const{auto i=entities_.find(handle);return i==entities_.end()?nullptr:&i->second;}
bool EffectAnimationWorld::destroy(uint64_t handle,std::string& error){
 if(inMutation_){error="Reentrant effect destruction";return false;}auto i=entities_.find(handle);if(i==entities_.end()){error="Unknown effect lifetime handle";return false;}
 const auto child=i->second.child,parent=i->second.parent;
 if(child&&!destroy(child,error))return false;
 if(parent&&entities_.contains(parent))entities_.at(parent).child=0;
 // Emitted VFX have their own native ownership/lifetime; they are not blindly
 // destroyed with the invisible source spawner.
 if(!registry_->dispose(handle,error))return false;entities_.erase(handle);return true;
}
size_t EffectAnimationWorld::managerMemberCount()const{size_t n=0;for(const auto& [id,e]:entities_){(void)id;n+=e.managerVectorMember;}return n;}
bool EffectAnimationWorld::update(float seconds,const EffectOwnerProvider& owners,const EffectMarkerProvider& markers,const EffectAnimationRandom& random,const EffectEmissionProvider& emissions,EffectUpdateContext& context,std::string& error){
 bool started=false;
 try{
  if(inMutation_||faulted_){error=faulted_?fault_:"Reentrant effect update";return false;}
  MutationScope scope(inMutation_);
  if(!std::isfinite(seconds)||seconds<0||seconds>1||!std::isfinite(context.managerCapMultiplier)||context.managerCapMultiplier<0||context.managerCapMultiplier>100||context.frameCap<0){error="Effect update bounds";return false;}
  if(context.phase==EffectUpdatePhase::Unresolved){error="Original Time/Distance ECS phase must be selected explicitly";return false;}
  if(!context.countsResolved){error="Actual manager VFX/frame counts are unresolved";return false;}
  struct Plan {uint64_t id;std::array<float,3> location;float time,distance,delta;bool emit;};std::vector<Plan> plans;
  const auto kind=context.phase==EffectUpdatePhase::Time?EffectSpawnerKind::Time:EffectSpawnerKind::Distance;
  for(const auto& [id,e]:entities_){
   if(e.spawner!=kind||e.blockVfx||e.done||e.suspendVfx)continue;const auto* lifetime=registry_->find(id);if(!lifetime||!lifetime->initialized){error="Actual initialized spawner lifetime missing";return false;}if(lifetime->skipUpdate||lifetime->canceled)continue;if(e.hasDuration&&!context.durationSystemResolved){error="Original Constant Duration/TimeRemaining system phase unresolved";return false;}if(e.hasDuration&&e.durationRemaining<0){error="Original Constant ChainTo/primary replacement required before VFX update";return false;}const auto& d=*e.definition;
   if(d.totalVfxCap>0&&e.totalSpawned>=uint32_t(d.totalVfxCap))continue;
   if(d.managerCap>0&&context.managerVisualFxCount>=uint32_t(float(d.managerCap)*context.managerCapMultiplier))continue;
   if(context.managerSpawnedThisFrame>=uint32_t(context.frameCap))continue;
   if(d.activeVfxCap){if(!emissions.alive){error="Actual active VFX lifetime lookup required";return false;}size_t active=0;for(auto spawned:e.spawned)active+=emissions.alive(spawned);if(active>=size_t(d.activeVfxCap))continue;}
   EffectOwnerState owner;if(!owners||!owners(e.ownerId,owner,error)||!ownerValid(owner)||owner.lifetime!=e.ownerLifetime){if(error.empty())error="Effect owner lifetime unavailable";return false;}
   std::array<float,3> location=e.particleComponent?e.location:std::array<float,3>{owner.x,owner.y,owner.z};
   if(!d.marker.empty()&&owner.hasPrimaryAnimation){if(!markers){error="Original effect marker provider required: "+d.marker;return false;}if(!markers(e.ownerId,d.marker,location,error))return false;}
   for(float v:location)if(!std::isfinite(v)){error="Non-finite original effect marker/location";return false;}
   float time=e.timeRemaining,distance=e.distanceRemaining,delta=0;
   if(kind==EffectSpawnerKind::Time){float elapsed=seconds;if(elapsed_&&!elapsed_(e,seconds,elapsed,error))return false;time=std::max(time-elapsed,-d.intervalMax);}
   else{
    const bool hasPrevious=e.previousVfxLocation[0]!=0||e.previousVfxLocation[1]!=0;
    if(hasPrevious){const float dx=location[0]-e.previousVfxLocation[0],dy=location[1]-e.previousVfxLocation[1];delta=std::min(std::sqrt(dx*dx+dy*dy),1920.f);if(delta>=d.distanceMinChange)distance-=delta;}
   }
   const bool emit=time<0||distance<0;
   if(emit){
    EffectSpawnRequest request;request.source=id;request.ownerId=e.ownerId;request.animation=d.vfx;request.position=kind==EffectSpawnerKind::Distance?emissionPosition(e.previousVfxLocation,location,distance,delta):location;request.segmentFrom=e.previousVfxLocation;request.segmentTo=location;request.nativeAngle=-1.5707963705062866f;request.distanceRemaining=distance;request.distanceDelta=delta;request.distance=kind==EffectSpawnerKind::Distance;
    if(!random||!emissions.prepare||!emissions.create){error="Actual original effect emission/RNG providers required: "+d.vfx;return false;}
    if(!emissions.prepare(request,error))return false;
   }
   plans.push_back({id,location,time,distance,delta,emit});
  }
  // Every missing/unsupported dependency has been checked before mutable work.
  // A provider failure after emission begins faults the world; it is not retried
  // as if RNG, native handles or external effects had never been consumed.
  inMutation_=true;started=true;
  for(const auto& p:plans){
   auto& e=entities_.at(p.id);const auto& d=*e.definition;if((d.managerCap>0&&context.managerVisualFxCount>=uint32_t(float(d.managerCap)*context.managerCapMultiplier))||context.managerSpawnedThisFrame>=uint32_t(context.frameCap))continue;e.location=p.location;e.timeRemaining=p.time;e.distanceRemaining=p.distance;
   uint32_t emitted=0;
   while(e.timeRemaining<0||e.distanceRemaining<0){
    if((d.totalVfxCap>0&&e.totalSpawned>=uint32_t(d.totalVfxCap))||(d.frameCap>0&&emitted>=uint32_t(d.frameCap))||emitted>uint32_t(context.frameCap))break;
    EffectSpawnRequest request;request.source=e.handle;request.ownerId=e.ownerId;request.animation=d.vfx;request.distance=kind==EffectSpawnerKind::Distance;request.distanceRemaining=e.distanceRemaining;request.distanceDelta=p.delta;request.position=request.distance?emissionPosition(e.previousVfxLocation,p.location,e.distanceRemaining,p.delta):p.location;request.segmentFrom=e.previousVfxLocation;request.segmentTo=p.location;request.nativeAngle=-1.5707963705062866f;
    uint64_t handle=0;
    bool created=false;{MutationScope componentAdmission(admittingParticleComponent_);created=emissions.create(request,handle,error);}
    if(!created||!handle){faulted_=true;fault_="Native effect emission failed after mutation: "+error;error=fault_;inMutation_=false;return false;}
    e.spawned.push_back(handle);++e.totalSpawned;++emitted;++context.managerVisualFxCount;++context.managerSpawnedThisFrame;
    float interval=0;if(!randomRange(random,kind==EffectSpawnerKind::Time?d.intervalMin:d.distanceMin,kind==EffectSpawnerKind::Time?d.intervalMax:d.distanceMax,interval,error)){faulted_=true;fault_="Native effect interval RNG failed after emission: "+error;error=fault_;inMutation_=false;return false;}if(kind==EffectSpawnerKind::Time)e.timeRemaining+=interval;else e.distanceRemaining+=interval;
   }
   e.previousVfxLocation=p.location;
  }
  inMutation_=false;return true;
 }catch(const std::exception& ex){inMutation_=false;if(started){faulted_=true;fault_="Effect update threw after mutation: "+std::string(ex.what());error=fault_;}else error=ex.what();return false;}
}
std::string EffectAnimationWorld::dump()const{std::ostringstream s;s<<"{\"entities\":"<<entities_.size()<<",\"faulted\":"<<(faulted_?"true":"false")<<",\"records\":[";bool first=true;for(const auto& [id,e]:entities_){if(!first)s<<',';first=false;s<<"{\"handle\":"<<id<<",\"parent\":"<<e.parent<<",\"child\":"<<e.child<<",\"owner\":"<<e.ownerId<<",\"definition\":\""<<e.definition->name<<"\",\"time_remaining\":"<<e.timeRemaining<<",\"distance_remaining\":"<<e.distanceRemaining<<",\"spawned\":"<<e.totalSpawned<<'}';}return s.str()+"]}";}
}
