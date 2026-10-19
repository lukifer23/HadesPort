#include "projectile_world.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace hades {
const char* projectileUpdateOperationName(ProjectileUpdateOperationKind kind){
    switch(kind){
    case ProjectileUpdateOperationKind::CreateStartFx:return "Projectile.CreateStartFx";
    case ProjectileUpdateOperationKind::UnpauseAnimation:return "Projectile.UnpauseAnimation";
    case ProjectileUpdateOperationKind::CheckProjectileDefense:return "ArcProjectile.CheckProjectileDefense";
    case ProjectileUpdateOperationKind::UpdateAnimation:return "Thing.UpdateAnimation";
    case ProjectileUpdateOperationKind::UpdateDetonation:return "Projectile.UpdateDetonation";
    case ProjectileUpdateOperationKind::Detonate:return "Projectile.Detonate";
    case ProjectileUpdateOperationKind::OnDeath:return "Projectile.OnDeath";
    case ProjectileUpdateOperationKind::CheckForImpact:return "Projectile.CheckForImpact";
    case ProjectileUpdateOperationKind::TransformChanged:return "Thing.SetLocation/SetAngle propagation";
    case ProjectileUpdateOperationKind::DisposeThing:return "Thing.Dispose";
    }
    return "unknown projectile operation";
}
namespace {
const SourceValue& required(const SourceValue& v,const std::string& name){const auto* field=v.path(name);if(!field)throw std::runtime_error("Original projectile Thing field missing: "+name);return *field;}
float number(const SourceValue& v,const std::string& name){const float result=float(required(v,name).number());if(!std::isfinite(result))throw std::runtime_error("Nonfinite projectile Thing field: "+name);return result;}
bool flag(const SourceValue& v,const std::string& name){return required(v,name).boolean();}
std::string text(const SourceValue& v,const std::string& name){return required(v,name).text();}
}
ProjectileActor* ProjectileWorld::actor(uint64_t handle){const auto it=actors_.find(handle);return it==actors_.end()?nullptr:it->second.get();}
const ProjectileActor* ProjectileWorld::actor(uint64_t handle)const{const auto it=actors_.find(handle);return it==actors_.end()?nullptr:it->second.get();}
bool ProjectileWorld::loadCatalog(SourceValue catalog,std::string& error){
    try{
        if(!actors_.empty())throw std::runtime_error("Cannot replace live projectile Thing catalog");
        if(text(catalog,"schema")!="hades-projectile-thing-catalog-v1"||text(catalog,"target_sha256")!="d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254")throw std::runtime_error("Projectile Thing catalog identity mismatch");
        std::map<std::string,SourceValue> data;
        for(const auto& [name,record]:required(catalog,"projectiles").object()){
            if(!required(record,"unresolved_thing_fields").array().empty())throw std::runtime_error("Unresolved original projectile Thing defaults");
            SourceValue thing=required(record,"thing");
            auto& properties=std::get<SourceValue::Object>(thing.value);
            for(const auto& [field,type]:required(record,"thing_property_types").object()){
                if(type.text()=="null_pointer"&&!thing.get(field))properties.emplace(field,SourceValue{});
            }
            data.emplace(name,std::move(thing));
        }
        thingData_=std::move(data);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ProjectileWorld::allocate(ProjectileInitializationRequest request,const ProjectileDefinition& definition,
    ActorWorld& world,uint64_t& output,std::string& error){
    if(faulted_){error=fault_;return false;}
    ProjectileCreationDescriptor descriptor;if(!describeProjectileCreation(definition,descriptor,error))return false;
    if(descriptor.requiresHomingDefaults){error="Actual Homing Defaults requires current projectile effector query";return false;}
    const auto source=thingData_.find(definition.name);if(source==thingData_.end()){error="Original copied projectile Thing data unavailable: "+definition.name;return false;}
    if(request.lifetimeHandle||request.ownerId<0||request.weaponName.empty()||!std::isfinite(request.x)||!std::isfinite(request.y)){error="Invalid new projectile lifetime request";return false;}
    const uint64_t handle=world.allocateEntityLifetime(error);if(!handle)return false;
    auto entity=std::make_unique<ProjectileActor>();request.lifetimeHandle=handle;entity->request=std::move(request);entity->descriptor=descriptor;entity->thing=&source->second;
    entity->state.lifetimeHandle=handle;entity->body.id=-1;entity->body.dataType=3;entity->body.entityHandle=handle;
    entity->body.fromMap=false;entity->body.active=false;entity->body.alive=true;entity->body.name=definition.name;
    actors_.emplace(handle,std::move(entity));output=handle;error.clear();return true;
}
bool ProjectileWorld::initializeThing(uint64_t handle,const ProjectileThingProviders& providers,std::string& error){
    bool begun=false;
    try{
        auto* entity=actor(handle);if(faulted_||!entity||entity->thingInitialized||entity->disposed)throw std::runtime_error(faulted_?fault_:"Invalid projectile Thing initialization lifetime");
        const auto& data=*entity->thing;
        // InitThing2dd80e tests full ThingData+71, i.e. AmbientSound.valid
        // (SoundCue begins+50, valid+21). It is not a script trigger flag.
        const bool nearby=flag(data,"AmbientSound.valid");
        if(providers.playerNearbyRequired&&*providers.playerNearbyRequired!=nearby)throw std::runtime_error("Projectile nearby provider disagrees with actual AmbientSound validity");
        auto requireProvider=[&](bool required,bool present,const char* name){if(required&&!present)throw std::runtime_error(std::string("Projectile Thing requires actual ")+name);};
        const bool interact=!std::holds_alternative<std::monostate>(required(data,"Interact").value);
        const bool metering=!std::holds_alternative<std::monostate>(required(data,"Metering").value),speech=flag(data,"SpeechCapable");
        const auto light=text(data,"Light"),attached=text(data,"AttachedAnim"),graphic=text(data,"Graphic"),activate=text(data,"ActivateGraphic"),activateFx=text(data,"ActivateFx");
        requireProvider(interact,bool(providers.createInteract),"Interact initializer");requireProvider(!light.empty(),bool(providers.createLight),"light registration");
        requireProvider(metering,bool(providers.createMetering),"metering component");requireProvider(speech,bool(providers.createSpeech),"speech component");
        requireProvider(nearby,bool(providers.createPlayerNearby),"ambient-sound nearby component");
        requireProvider(number(data,"OutlineThickness")>0,bool(providers.requestOutline),"outline request");
        requireProvider(!attached.empty(),bool(providers.createAttachedAnimation),"attached source animation");
        requireProvider(!graphic.empty()||!activate.empty(),bool(providers.setAnimation),"primary source animation");
        requireProvider(!activateFx.empty()&&activateFx!="null",bool(providers.createActivateFx),"activation FX");
        const float low=number(data,"MinScale"),high=number(data,"MaxScale");requireProvider(low!=0||high!=0,bool(providers.random),"scale RNG");
        auto& body=entity->body;begun=true;body.x=entity->request.x;body.y=entity->request.y;body.z+=number(data,"OffsetZ");
        body.screenSpace=flag(data,"UseScreenLocation");body.visible=!flag(data,"Invisible");body.angle=number(data,"StartAngle");
        const char* channels[]{"Red","Green","Blue","Alpha"};for(size_t i=0;i<4;++i)entity->baseColor[i]=uint8_t(std::clamp(number(data,std::string("Color.")+channels[i]),0.f,1.f)*255.f);
        entity->hsv={number(data,"Hue"),number(data,"Saturation"),number(data,"Value"),0};
        float scale=number(data,"Scale");if(low!=0||high!=0){double random=0;if(!providers.random(random,error))throw std::runtime_error(error);scale=float(random)*(high-low)+low;}
        body.scale*=scale;body.graphicsDirty=true;entity->bodyPending=true;
        if(interact&&!providers.createInteract(*entity,error))throw std::runtime_error(error);
        if(!light.empty()&&!providers.createLight(*entity,light,error))throw std::runtime_error(error);
        if(nearby&&!providers.createPlayerNearby(*entity,error))throw std::runtime_error(error);
        entity->ambient=number(data,"Ambient");
        if(number(data,"OutlineThickness")>0&&!providers.requestOutline(*entity,error))throw std::runtime_error(error);
        if(!attached.empty()&&!providers.createAttachedAnimation(*entity,attached,entity->attachedAnimation,error))throw std::runtime_error(error);
        entity->timeModifierFraction=number(data,"TimeModifierFraction");
        // InitMapThing(null) leaves source-map ID-1. InitPhysics runs after it.
        if(metering&&!providers.createMetering(*entity,error))throw std::runtime_error(error);
        if(speech&&!providers.createSpeech(*entity,error))throw std::runtime_error(error);
        if(number(data,"Grip")<99999.f){entity->physics=std::make_unique<PhysicsState>();entity->physics->gravity=number(data,"Gravity");entity->physics->bounce=number(data,"BounceMultiplier");entity->physicsPending=true;}
        // A mapless projectile has no light occluder while elevated, or when
        // source StopsLight=false. A genuine ground occluder needs its system.
        if(std::abs(body.z)<=.0001f&&flag(data,"StopsLight"))throw std::runtime_error("Ground projectile light occluder requires original system registration");
        if(!graphic.empty()&&!providers.setAnimation(*entity,graphic,true,error))throw std::runtime_error(error);
        if(!activate.empty()&&!providers.setAnimation(*entity,activate,true,error))throw std::runtime_error(error);
        if(!activateFx.empty()&&activateFx!="null"&&!providers.createActivateFx(*entity,activateFx,error))throw std::runtime_error(error);
        entity->thingInitialized=true;error.clear();return true;
    }catch(const std::exception& e){error=e.what();if(begun){faulted_=true;fault_=error;}return false;}
}
bool ProjectileWorld::setupGeometry(uint64_t handle,std::string& error){
    try{
        auto* entity=actor(handle);if(!entity||!entity->thingInitialized||entity->disposed)throw std::runtime_error("Projectile geometry needs initialized body");
        const auto& data=*entity->thing;const auto& body=entity->body;
        if(number(data,"ExtentScale")!=0||number(data,"RelativeExtentScale")!=0)throw std::runtime_error("Projectile extent geometry branch unavailable");
        std::vector<ActorPoint> points;for(const auto& p:required(data,"Points").array())points.push_back({number(p,"X"),number(p,"Y")});
        const float scale=body.scale*body.scaleFraction,co=std::cos(-body.angle),si=std::sin(-body.angle);
        for(auto& p:points){p.x*=scale;p.y*=scale;if(body.flipHorizontal)p.x=-p.x;if(flag(data,"RotateGeometry")&&body.angle!=0){const float x=p.x,y=p.y;p.x=x*co-y*si;p.y=x*si+y*co;}p.x+=body.x;p.y+=body.y;}
        double area=0;for(size_t i=0;i<points.size();++i){const auto& a=points[i];const auto& b=points[(i+1)%points.size()];area+=double(a.x)*b.y-double(b.x)*a.y;}if(area>0)std::reverse(points.begin(),points.end());
        entity->geometry=std::move(points);entity->body.geometryDirty=false;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ProjectileWorld::queuePending(uint64_t handle,std::string& error){auto* entity=actor(handle);if(!entity||!entity->thingInitialized||entity->pending||entity->active||entity->disposed){error="Invalid native projectile pending queue admission";return false;}pending_.push_back(handle);entity->pending=true;error.clear();return true;}
bool ProjectileWorld::setProjectileNumOpacityFlag(uint64_t handle,std::string& error){
    const auto* entity=actor(handle);if(!entity||!entity->firingArsenal){error="Projectile opacity flag requires actual source weapon binding";return false;}
    const auto* data=entity->firingArsenal->projectileData(entity->request.weaponName,entity->spawnedDefinition,error);if(!data)return false;
    const auto it=data->fields.find("ProjectileNumOpacityMultiplier");if(it==data->fields.end()||!std::holds_alternative<double>(it->second)||!std::isfinite(std::get<double>(it->second))){error="Actual ProjectileNumOpacityMultiplier unavailable";return false;}
    // Manager.Create7acbf and removal7913d OR this single manager-global byte.
    // No owner counter exists here. Nonpositive data leaves prior dirty state.
    if(std::get<double>(it->second)>0)numOpacityDirty_=true;error.clear();return true;
}
bool ProjectileWorld::updateNumOpacity(std::string& error){
    if(!numOpacityDirty_){error.clear();return true;}
    auto multiplier=[&](uint64_t handle,float& value){const auto* entity=actor(handle);if(!entity||!entity->firingArsenal){error="Opacity manager lost actual projectile/source";return false;}const auto* data=entity->firingArsenal->projectileData(entity->request.weaponName,entity->spawnedDefinition,error);if(!data)return false;const auto it=data->fields.find("ProjectileNumOpacityMultiplier");if(it==data->fields.end()||!std::holds_alternative<double>(it->second)){error="Opacity manager missing current copied data";return false;}value=float(std::get<double>(it->second));if(!std::isfinite(value)){error="Nonfinite opacity multiplier";return false;}return true;};
    int32_t count=0;for(const auto* list:{&active_,&pending_})for(uint64_t handle:*list){float value=0;if(!multiplier(handle,value))return false;if(value>0)++count;}
    if(count>0)for(const auto* list:{&active_,&pending_})for(uint64_t handle:*list){float value=0;if(!multiplier(handle,value))return false;if(value<=0)continue;
        volatile float weighted=float(count-1)*value;volatile float denominator=1.0f+weighted;volatile float target=1.0f/denominator;
        auto* entity=actor(handle);BodyTransitionOptions options;options.duration=0.3f;options.smoothStep=true;bool changed=false;
        if(!entity->fractions[1].adjust(target,1,options,changed,error))return false;
        if(changed){entity->body.alpha=entity->fractions[1].current();entity->body.graphicsDirty=true;}
    }
    numOpacityDirty_=false;error.clear();return true;
}
bool ProjectileWorld::advanceBody(uint64_t handle,float elapsed,float undo,std::string& error){
    auto* entity=actor(handle);if(!entity||entity->disposed||!entity->thingInitialized){error="Actual projectile BodySystem lifetime unavailable";return false;}
    for(auto& fraction:entity->fractions)if(!fraction.advance(elapsed,undo,error))return false;
    entity->body.scaleFraction=entity->fractions[0].current();entity->body.alpha=entity->fractions[1].current();
    entity->body.geometryDirty=entity->body.graphicsDirty=true;return setupGeometry(handle,error);
}
bool ProjectileWorld::flushPending(std::string& error){if(updating_){error="Cannot flush new pending projectiles inside native active-update iteration";return false;}for(const auto handle:pending_){auto* entity=actor(handle);if(!entity||!entity->pending||entity->active||entity->disposed){error="Invalid actual pending projectile lifetime";return false;}}
    // Native CAN_BE_REFLECTED admission is captured at pending promotion.
    std::vector<uint64_t> reflectable;
    for(const auto handle:pending_){const auto* entity=actor(handle);if(!entity->firingArsenal){error="Pending projectile lacks its actual source Arsenal";return false;}
        const auto* data=entity->firingArsenal->projectileData(entity->request.weaponName,entity->spawnedDefinition,error);if(!data)return false;
        const auto it=data->fields.find("CanBeReflected");if(it==data->fields.end()||!std::holds_alternative<bool>(it->second)){error="Actual CanBeReflected data unavailable at manager promotion";return false;}
        if(std::get<bool>(it->second))reflectable.push_back(handle);
    }
    canBeReflected_.insert(canBeReflected_.end(),reflectable.begin(),reflectable.end());
    for(const auto handle:pending_){const auto* entity=actor(handle);const auto* data=entity->firingArsenal->projectileData(entity->request.weaponName,entity->spawnedDefinition,error);if(!data)return false;
        const auto it=data->fields.find("IncreaseAdjustRateOfFriendlyProjectilesInside");if(it==data->fields.end()||!std::holds_alternative<double>(it->second)){error="Actual IncreaseAdjustRateOfFriendlyProjectilesInside unavailable at manager promotion";return false;}
        if(std::get<double>(it->second)!=0)effectors_.push_back(handle);}
    for(const auto handle:pending_){auto* entity=actor(handle);entity->pending=false;entity->active=true;entity->body.active=true;active_.push_back(handle);}pending_.clear();error.clear();return true;}
std::vector<uint64_t> ProjectileWorld::bodyComponents()const{std::vector<uint64_t> result;for(const auto& [handle,p]:actors_)if(p->thingInitialized&&p->bodyPending&&!p->disposed)result.push_back(handle);return result;}
std::string ProjectileWorld::dump()const{std::ostringstream out;out<<"PROJECTILE_WORLD allocated="<<actors_.size()<<" pending="<<pending_.size()<<" active="<<active_.size()<<" retired_history="<<retired_.size()<<" fault="<<faulted_<<'\n';for(const auto& [id,e]:actors_)out<<"PROJECTILE handle="<<id<<" publicId="<<e->body.id<<" owner="<<e->request.ownerId<<" weapon="<<e->request.weaponName<<" initialized="<<e->thingInitialized<<" active="<<e->active<<" x="<<e->body.x<<" y="<<e->body.y<<" z="<<e->body.z<<" polygon="<<e->geometry.size()<<'\n';for(const auto& e:retired_)out<<"RETIRED_PROJECTILE handle="<<e.handle<<" owner="<<e.owner<<" weapon="<<e.weapon<<" x="<<e.x<<" y="<<e.y<<" z="<<e.z<<" arc="<<e.arcAngle<<" polygon="<<e.polygonVertices<<'\n';return out.str();}
}

namespace hades {
bool ProjectileWorld::updateArc(uint64_t handle,float incoming,const ProjectileUpdateProviders& providers,bool& remove,std::string& error){
    // Matching native ArcProjectile::UpdateSelf621b0, Move62440 and manager
    // Update78db0. Source callbacks are synchronous and may alter copied data;
    // no pointer to a vector-backed WeaponSnapshot is retained across them.
    remove=false;auto* entity=actor(handle);
    if(!entity||!entity->shotInitialized||!entity->active||entity->disposed||!entity->firingArsenal||!entity->firingWorld){error="ARC update requires actual initialized active projectile/source binding";return false;}
    if(entity->descriptor.concreteClass!=ProjectileClass::Arc){error="ArcProjectile::UpdateSelf reached by a non-ARC projectile";return false;}
    auto current=[&](){return entity->firingArsenal->projectileData(entity->request.weaponName,entity->spawnedDefinition,error);};
    auto scalar=[&](const char* key,float& out){const auto* data=current();if(!data)return false;auto it=data->fields.find(key);
        if(it==data->fields.end()||!std::holds_alternative<double>(it->second)||!std::isfinite(std::get<double>(it->second))){error=std::string("Actual projectile numeric data unavailable: ")+key;return false;}
        out=float(std::get<double>(it->second));return true;};
    auto boolean=[&](const char* key,bool& out){const auto* data=current();if(!data)return false;auto it=data->fields.find(key);
        if(it==data->fields.end()||!std::holds_alternative<bool>(it->second)){error=std::string("Actual projectile boolean data unavailable: ")+key;return false;}out=std::get<bool>(it->second);return true;};
    auto call=[&](ProjectileUpdateOperationKind kind,float elapsed,ProjectileUpdateReply& reply){
        ProjectileUpdateOperation operation{kind,elapsed};reply={};
        if(!providers.preflight||!providers.execute){error=std::string("ARC update reached missing native operation consumer ")+projectileUpdateOperationName(kind);return false;}
        if(!providers.preflight(operation,*entity,error)||!providers.execute(operation,*entity,reply,error))return false;
        if(kind==ProjectileUpdateOperationKind::CheckForImpact){
            if(!reply.impactResult||(*reply.impactResult!=ProjectileImpactResult::Stop&&*reply.impactResult!=ProjectileImpactResult::Continue)){error="Native CheckForImpact consumer did not return real STOP/CONTINUE result";return false;}
        }else if(reply.impactResult){error="Impact result supplied for a different projectile operation";return false;}
        return true;
    };
    auto run=[&](ProjectileUpdateOperationKind kind,float elapsed){ProjectileUpdateReply reply;return call(kind,elapsed,reply);};
    auto sub=[](float a,float b){volatile float value=a-b;return value;};
    auto add=[](float a,float b){volatile float value=a+b;return value;};
    auto mul=[](float a,float b){volatile float value=a*b;return value;};
    float dt=incoming;
    if(entity->timeModifierFraction<1){
        const float denominator=add(mul(sub(1,providers.worldTimeMultiplier),entity->timeModifierFraction),providers.worldTimeMultiplier);
        volatile float adjusted=dt/denominator;dt=adjusted;
    }
    if(!std::isfinite(dt)){error="Native ARC elapsed scaling is outside finite supported domain";return false;}
    if(!entity->state.hasStarted){
        entity->state.startDelay=sub(entity->state.startDelay,dt);
        if(entity->state.startDelay>0){error.clear();return true;}
        entity->state.hasStarted=true;
        if(!run(ProjectileUpdateOperationKind::CreateStartFx,dt))return false;
    }
    if(entity->state.pauseTimeRemaining>0){
        entity->state.pauseTimeRemaining=sub(entity->state.pauseTimeRemaining,dt);
        if(entity->state.pauseTimeRemaining>0){error.clear();return true;}
        if(!run(ProjectileUpdateOperationKind::UnpauseAnimation,dt))return false;
    }
    float defenseRadius=0;if(!scalar("ProjectileDefenseRadius",defenseRadius))return false;
    // Exact native virtual entry returns without touching candidates when <=0.
    // A positive radius still scans CAN_BE_REFLECTED even DeflectProjectiles=false.
    if(defenseRadius>0&&!run(ProjectileUpdateOperationKind::CheckProjectileDefense,dt))return false;
    // Thing.UpdateAnimation2df5c0's real null-primary branch returns directly;
    // global attached animation manager is a separate later World phase.
    if(entity->primaryAnimation&&!run(ProjectileUpdateOperationKind::UpdateAnimation,dt))return false;
    auto blastDone=[&](bool& done){float speed=0,radius=0;if(!scalar("BlastSpeed",speed)||!scalar("DamageRadius",radius))return false;
        done=entity->state.blastTimeRemaining<=0&&(speed<=0||entity->state.blastRadius>=mul(radius,entity->state.blastRadiusModifier));return true;};
    bool multi=false;if(!boolean("MultiDetonate",multi))return false;
    float maximum=0;if(!scalar("MaxDetonations",maximum)||std::floor(maximum)!=maximum){if(error.empty())error="Noninteger native MaxDetonations";return false;}
    const bool finished=(entity->state.detonated&&!multi)||(multi&&maximum>0&&float(entity->state.detonations)>=maximum);
    if(finished){
        float blastSpeed=0;if(!scalar("BlastSpeed",blastSpeed))return false;
        if(blastSpeed>0&&!run(ProjectileUpdateOperationKind::UpdateDetonation,dt))return false;
        bool done=false;if(!blastDone(done))return false;
        if(done){if(!run(ProjectileUpdateOperationKind::OnDeath,0))return false;remove=true;}
        error.clear();return true;
    }
    entity->state.fuseRemaining=sub(entity->state.fuseRemaining,dt);
    if(entity->state.fuseRemaining<=0){
        if(!run(ProjectileUpdateOperationKind::Detonate,dt)||!boolean("MultiDetonate",multi))return false;
        if(!multi){bool done=false;if(!blastDone(done))return false;if(done){if(!run(ProjectileUpdateOperationKind::OnDeath,0))return false;remove=true;}error.clear();return true;}
        float fuse=0,lo=0,hi=0;if(!scalar("Fuse",fuse)||!scalar("FuseMin",lo)||!scalar("FuseMax",hi))return false;
        if(lo!=-1&&hi!=-1){if(!providers.random){error="Multidetonation actual fuse-range RNG missing";return false;}double draw=0;if(!providers.random(draw,error))return false;if(!std::isfinite(draw)||draw<0||draw>=1){error="Native fuse RNG outside [0,1)";return false;}fuse=add(mul(float(draw),sub(hi,lo)),lo);}
        entity->state.fuseRemaining=add(entity->state.fuseRemaining,fuse);
    }
    ArcProjectileMoveFrame frame;frame.dt=dt;frame.worldTime=providers.worldTime;frame.creationTime=entity->spawnTime;
    frame.spawnX=entity->spawnX;frame.spawnY=entity->spawnY;frame.range=entity->state.range;frame.spawnAngle=entity->state.spawnAngle;
    const auto* owner=entity->firingWorld->actor(entity->state.ownerId);frame.ownerExists=owner&&owner->registered;
    frame.ownerHasBody=frame.ownerExists&&owner->object;
    if(frame.ownerExists){if(!entity->firingWorld->location(owner->id,frame.ownerX,frame.ownerY,error))return false;if(frame.ownerHasBody)frame.ownerBodyAngle=owner->object->angle;}
    const auto* definition=current();if(!definition)return false;ArcProjectileMovePlan plan;
    if(!planArcProjectileMove(*definition,entity->arcAngleOffset,frame,plan,error))return false;
    if(plan.nativeShouldRemove){remove=true;error.clear();return true;}
    if(plan.hasTransform){
        // This bounded first-Sword path has no attached Thing/animation graph.
        // Consumers with graphics must handle actual movement propagation.
        const bool external=entity->primaryAnimation||entity->attachedAnimation||!entity->attachedAnimations.empty()||!text(*entity->thing,"Light").empty();
        const ProjectileUpdateOperation transform{ProjectileUpdateOperationKind::TransformChanged,dt};
        if(external&&(!providers.preflight||!providers.execute||!providers.preflight(transform,*entity,error))){if(error.empty())error="ARC movement requires actual attached/graphic transform consumer";return false;}
        entity->arcAngleOffset=plan.nextAngleOffset;entity->body.x=plan.x;entity->body.y=plan.y;
        entity->body.angle=plan.requestedAngle;entity->body.geometryDirty=entity->body.graphicsDirty=true;
        if(!setupGeometry(handle,error))return false;
        if(external){ProjectileUpdateReply reply;if(!providers.execute(transform,*entity,reply,error))return false;if(reply.impactResult){error="Unexpected impact result from movement propagation";return false;}}
    }
    ProjectileUpdateReply impact;if(!call(ProjectileUpdateOperationKind::CheckForImpact,dt,impact))return false;
    if(*impact.impactResult==ProjectileImpactResult::Stop){remove=true;error.clear();return true;}
    float end=0;if(!scalar("ArcEnd",end))return false;
    remove=entity->arcAngleOffset==end;error.clear();return true;
}
bool ProjectileWorld::update(float elapsed,const ProjectileUpdateProviders& providers,std::string& error){
    if(faulted_){error=fault_;return false;}
    if(updating_||!std::isfinite(elapsed)||elapsed<0||!std::isfinite(providers.worldTime)||!std::isfinite(providers.worldTimeMultiplier)){error="Projectile manager requires finite actual World clock outside nested update";return false;}
    // Reject unsupported concrete classes before promoting/mutating the frame.
    for(const auto* list:{&active_,&pending_})for(uint64_t handle:*list){const auto* entity=actor(handle);
        if(!entity||!entity->shotInitialized||entity->disposed||(entity->descriptor.concreteClass!=ProjectileClass::Arc&&entity->descriptor.concreteClass!=ProjectileClass::Straight)){error="Actual projectile manager currently supports fully initialized ARC/STRAIGHT only";return false;}}
    if(!flushPending(error))return false;
    updating_=true;
    auto stop=[&](const std::string& reason){updating_=false;faulted_=true;fault_=reason;error=reason;return false;};
    size_t read=0,write=0;
    while(read<active_.size()){
        const uint64_t handle=active_[read++];auto* entity=actor(handle);bool remove=false;
        const float dt=elapsed*entity->timeMultiplier;
        const bool base=entity->descriptor.concreteClass==ProjectileClass::Straight; // no subclass vtable: base UpdateSelf6a720
        if(!std::isfinite(dt)||!(base?updateBase(handle,dt,providers,remove,error):updateArc(handle,dt,providers,remove,error)))return stop(error.empty()?"Nonfinite actual projectile elapsed time":error);
        if(remove){
            const ProjectileUpdateOperation operation{ProjectileUpdateOperationKind::DisposeThing,dt};
            if(!providers.preflight||!providers.execute)return stop("Native projectile removal requires actual Thing.Dispose consumer");
            if(!providers.preflight(operation,*entity,error))return stop(error);
            if(!setProjectileNumOpacityFlag(handle,error))return stop(error);
            entity->lifeStatus=1;entity->disposed=true;entity->active=false;entity->body.active=false;entity->body.alive=false;
            ProjectileUpdateReply reply;if(!providers.execute(operation,*entity,reply,error))return stop(error);
            if(reply.impactResult)return stop("Unexpected impact result from Thing.Dispose");
            std::erase(canBeReflected_,handle); // native ordered vector erase after Dispose.
            std::erase(effectors_,handle); // PROJECTILE_EFFECTORS erase VA18007920b.
        }else active_[write++]=handle;
    }
    active_.resize(write);
    if(!updateNumOpacity(error))return stop(error);
    updating_=false;if(!collectDisposed(providers,error))return stop(error);error.clear();return true;
}
bool ProjectileWorld::collectDisposed(const ProjectileUpdateProviders& providers,std::string& error){
    // Native Release after Thing.Dispose must not destroy a projectile still
    // retained by a real delayed-weapon or another projectile ownership edge.
    // Iterate to a fixed point so releasing a child releases its source edge.
    bool released=false;
    do{released=false;for(auto it=actors_.begin();it!=actors_.end();){auto& entity=*it->second;
        if(!entity.disposed){++it;continue;}
        bool retained=false;
        if(entity.firingArsenal){const auto* weapon=entity.firingArsenal->weaponState(entity.request.weaponName,error);if(!weapon)return false;retained=weapon->delayedProjectileHandle==it->first;}
        if(!retained)for(const auto& [otherHandle,other]:actors_)if(otherHandle!=it->first&&other->sourceProjectileLifetime==it->first){retained=true;break;}
        if(!retained&&providers.retained&&!providers.retained(it->first,retained,error))return false;
        if(retained){++it;continue;}
        retired_.push_back({it->first,entity.state.ownerId,entity.body.name,entity.request.weaponName,entity.body.x,entity.body.y,entity.body.z,entity.arcAngleOffset,entity.geometry.size()});
        if(retired_.size()>64)retired_.pop_front();it=actors_.erase(it);released=true;
    }}while(released);
    error.clear();return true;
}

bool ProjectileWorld::initializeShot(ProjectileInitializationRequest request,WeaponArsenal& arsenal,
        ActorWorld& world,const ProjectileShotProviders& providers,uint64_t& output,
        std::string& error,bool spawnedDefinition){
    // Actual allocation -> native base Init cursors -> derived ARC completion.
    // Original DLL/PDB contracts are retained in projectile-initialization-
    // contract.json, projectile-thing-data-contract.json and arc-projectile-
    // motion-contract.json. No active membership is claimed before manager flush.
    uint64_t handle=0;bool allocated=false;
    try{
        if(faulted_)throw std::runtime_error(fault_);
        if(!providers.random||!providers.ownerState||!std::isfinite(providers.worldTime))throw std::runtime_error("Projectile shot requires actual RNG, owner-state query and world clock");
        auto* owner=world.actor(request.ownerId);
        if(!owner||!owner->registered||!owner->object||owner->arsenal.get()!=&arsenal)throw std::runtime_error("Projectile shot requires actual registered Unit/owned Arsenal");
        const auto* definition=arsenal.projectileData(request.weaponName,spawnedDefinition,error);if(!definition)throw std::runtime_error(error);
        if(!allocate(request,*definition,world,handle,error))throw std::runtime_error(error);allocated=true;
        auto* entity=actor(handle);entity->spawnedDefinition=spawnedDefinition;request=entity->request;
        entity->spawnTime=providers.worldTime;
        ProjectileInitialization prefix(request);
        std::unique_ptr<ProjectileInitializationTail> tail;
        std::optional<double> random;
        auto fresh=[&](ProjectileInitializationFrame& frame,ProjectileInitializationTailFrame& end)->bool{
            auto* actual=world.actor(request.ownerId);
            frame.worldTime=providers.worldTime;frame.thingInitialized=entity->thingInitialized;
            frame.hasBody=entity->bodyPending;frame.hasPhysics=bool(entity->physics);
            frame.ownerExists=actual&&actual->registered&&actual->object&&actual->arsenal.get()==&arsenal;
            if(!frame.ownerExists){error="Projectile initialization owner disappeared across native callback";return false;}
            if(!world.zLocation(request.ownerId,frame.ownerTotalZ,error))return false;
            if(!providers.ownerState(*actual,end.gunOwner.damageOutputModifier,frame.ownerSpawnedById,frame.ownerTraitor,error))return false;
            std::string lookup;frame.weaponPerfectCharge=arsenal.isPerfectCharge(request.weaponName,lookup);if(!lookup.empty()){error=lookup;return false;}
            frame.weaponHasAmmo=arsenal.hasAmmo(request.weaponName,lookup);if(!lookup.empty()){error=lookup;return false;}
            end.gunOwner.exists=true;end.gunOwner.perfectCharge=frame.weaponPerfectCharge;
            auto scalar=[&](const char* field,float& out){const auto* v=world.currentProperty(request.ownerId,field);if(!v){error=std::string("Projectile owner native data missing ")+field;return false;}out=float(v->number());if(!std::isfinite(out)){error="Nonfinite projectile owner property";return false;}return true;};
            if(!scalar("DamageOutputMultiplier",end.gunOwner.damageOutputMultiplier)||!scalar("ProjectileSpeedMultiplier",end.gunOwner.projectileSpeedMultiplier)||!scalar("PerfectChargeBonus",end.gunOwner.perfectChargeBonus))return false;
            end.worldX=entity->body.x;end.worldY=entity->body.y;
            end.spawnX=entity->spawnX;end.spawnY=entity->spawnY;
            end.ownerElapsedTimeMultiplier=actual->timeMultiplier;
            return true;
        };
        auto internal=[&](ProjectileInitializationOperationKind kind){
            using K=ProjectileInitializationOperationKind;
            switch(kind){case K::ThingInitialize:case K::SetPhysicsIgnoreGravity:case K::AdjustBodyAlpha:
            case K::AddOwnerZ:case K::RetainFiringWeapon:case K::AddToGroup:case K::SetWeaponLastFirePerfectCharge:
            case K::RequestRandomDouble:case K::SetupGeometry:case K::RefreshLocation:case K::SetAngle:
            case K::SetBodyScale:case K::QueuePendingProjectile:case K::CopyOwnerGroups:return true;
            case K::SetProjectileNumOpacityFlag:return true;
            case K::AttachToOwner:return true;
            case K::SetAnimation:return bool(providers.thing.setAnimation);
            case K::CreateAttachedAnimation:return bool(providers.thing.createAttachedAnimation);
            case K::SetAttachedAnimationRadius:return entity->attachedAnimations.empty()&&!entity->attachedAnimation;
            case K::InheritElapsedTimeMultiplier:return !entity->primaryAnimation&&entity->attachedAnimations.empty()&&!entity->attachedAnimation;
            default:return false;}
        };
        auto preflight=[&](const ProjectileInitializationOperation& op,std::string& e){
            if(internal(op.kind))return true;
            if(!providers.preflight||!providers.execute){e="Projectile Init operation lacks actual external consumer: "+op.name;return false;}
            return providers.preflight(op,*entity,e);
        };
        auto consume=[&](const ProjectileInitializationOperation& op)->bool{
            using K=ProjectileInitializationOperationKind;
            switch(op.kind){
            case K::ThingInitialize:
                if(!initializeThing(handle,providers.thing,error))return false;
                entity->spawnX=entity->body.x;entity->spawnY=entity->body.y;
                if(entity->attachedAnimation)entity->attachedAnimations.push_back(entity->attachedAnimation);
                return true;
            case K::SetPhysicsIgnoreGravity:
                if(!entity->physics){error="Projectile Physics disappeared before ignore-gravity mutation";return false;}
                entity->physics->ignoreGravity=op.flag;return true;
            case K::AdjustBodyAlpha:{BodyTransitionOptions options;options.duration=op.duration;bool changed=false;
                if(!entity->fractions[1].adjust(op.value,op.start,options,changed,error))return false;
                entity->body.alpha=entity->fractions[1].current();entity->body.graphicsDirty=true;return true;}
            case K::AddOwnerZ:entity->body.z+=op.value;entity->body.graphicsDirty=true;return true;
            case K::RetainFiringWeapon:entity->firingWorld=&world;entity->firingArsenal=&arsenal;return true;
            case K::AddToGroup:
                if(!providers.validateGroup){error="Projectile source group requires actual scene-group validation";return false;}
                if(!providers.validateGroup(op.name,error))return false;
                entity->body.groups.push_back(op.name);if(entity->body.group.empty())entity->body.group=op.name;return true;
            case K::SetWeaponLastFirePerfectCharge:return arsenal.recordProjectilePerfectCharge(request.weaponName,op.flag,error);
            case K::SetAnimation:
                if(providers.thing.setAnimation)return providers.thing.setAnimation(*entity,op.name,true,error);
                break;
            case K::CreateAttachedAnimation:
                if(providers.thing.createAttachedAnimation){uint64_t animation=0;if(!providers.thing.createAttachedAnimation(*entity,op.name,animation,error))return false;if(animation)entity->attachedAnimations.push_back(animation);return true;}
                break;
            case K::RequestRandomDouble:{double result=0;if(!providers.random(result,error))return false;if(!std::isfinite(result)||result<0||result>=1){error="Projectile Init RNG outside original [0,1) domain";return false;}random=result;return true;}
            case K::SetupGeometry:return setupGeometry(handle,error);
            case K::RefreshLocation:entity->body.geometryDirty=true;entity->body.graphicsDirty=true;return setupGeometry(handle,error);
            case K::SetAngle:entity->body.angle=op.value;entity->body.geometryDirty=true;entity->body.graphicsDirty=true;return setupGeometry(handle,error);
            case K::SetBodyScale:entity->body.scale=op.value;entity->body.geometryDirty=true;entity->body.graphicsDirty=true;return true;
            case K::QueuePendingProjectile:return queuePending(handle,error);
            case K::AttachToOwner:{
                // Init VA180069f2b..: GetFireOffset86010(weapon, Body?Body.mAngle:0),
                // vslot0x108 Thing::AttachTo2e59f0(UnitManager::Get(weapon owner), offset)
                // -> SetAttachOffset2e0e80 (SetLocation(parent+offset)) ->
                // OnAttachedToMoved2e5960 (SetLocation(parent+offset)); then
                // mAttachOffsetZ(+0x70) = ThingData OffsetZ(+0x1ac).
                WeaponPoint offset;if(!arsenal.fireOffset(request.weaponName,entity->bodyPending?entity->body.angle:0.f,offset,error))return false;
                const auto* parent=world.actor(request.ownerId);
                if(!parent||!parent->registered||!parent->definition||parent->definition->kind!="Unit"){error="Projectile AttachToOwner requires actual live owner Unit";return false;}
                entity->attachedTo=parent->id;entity->attachedToLifetime=parent->entityHandle;
                entity->attachOffsetX=offset.x;entity->attachOffsetY=offset.y;
                for(unsigned pass=0;pass<2;++pass){float x=0,y=0;if(!thingLocation(*entity,x,y,error)||!setLocation(handle,x,y,error))return false;entity=actor(handle);}
                entity->attachOffsetZ=number(*entity->thing,"OffsetZ");return true;
            }
            case K::SetProjectileNumOpacityFlag:return setProjectileNumOpacityFlag(handle,error);
            case K::CopyOwnerGroups:{const auto* actual=world.actor(request.ownerId);if(!actual||!actual->object){error="Projectile owner group storage disappeared";return false;}
                for(const auto& group:actual->object->groups){entity->body.groups.push_back(group);if(entity->body.group.empty()&&!group.empty())entity->body.group=group;}return true;}
            case K::SetAttachedAnimationRadius:
                if(entity->attachedAnimations.empty()&&!entity->attachedAnimation)return true;
                break;
            case K::InheritElapsedTimeMultiplier:
                entity->timeMultiplier=op.value;
                if(!entity->primaryAnimation&&entity->attachedAnimations.empty()&&!entity->attachedAnimation)return true;
                break;
            default:break;
            }
            if(!providers.execute){error="Projectile Init operation has no actual external consumer: "+op.name;return false;}
            return providers.execute(op,*entity,error);
        };
        for(unsigned steps=0;steps<256;++steps){
            definition=arsenal.projectileData(request.weaponName,spawnedDefinition,error);if(!definition)throw std::runtime_error(error);
            ProjectileInitializationFrame frame;ProjectileInitializationTailFrame end;
            if(!fresh(frame,end))throw std::runtime_error(error);
            ProjectileInitializationOperation operation;
            if(!tail){
                ProjectileInitializationStep step;
                if(!prefix.advance(*definition,frame,random,preflight,step,error))throw std::runtime_error(error);random.reset();entity->state=prefix.state();
                if(step.progress==ProjectileInitializationProgress::BeforeWeaponModifiers){tail=std::make_unique<ProjectileInitializationTail>(request,entity->state);continue;}
                operation=std::move(step.operation);
            }else{
                const auto* weapon=arsenal.weaponState(request.weaponName,error);if(!weapon)throw std::runtime_error(error);
                ProjectileInitializationTailStep step;
                if(!tail->advance(*definition,*weapon,end,preflight,step,error))throw std::runtime_error(error);entity->state=tail->state();
                if(step.progress==ProjectileInitializationTailProgress::PendingQueueSetupFinished){
                    if(entity->descriptor.concreteClass==ProjectileClass::Arc&&!initialArcProjectileAngle(*definition,entity->arcAngleOffset,error))throw std::runtime_error(error);
                    entity->shotInitialized=true;output=handle;error.clear();return true;
                }
                operation=std::move(step.operation);
            }
            if(!consume(operation))throw std::runtime_error(error);
        }
        throw std::runtime_error("Projectile Init exceeded bounded operation count");
    }catch(const std::exception& e){error=e.what();if(allocated){faulted_=true;fault_=error;}return false;}
}
}

namespace hades {
bool ProjectileWorld::retainsWeapon(const WeaponArsenal* arsenal,const std::string& name)const{
    for(const auto& [handle,projectile]:actors_){(void)handle;if(projectile->firingArsenal==arsenal&&projectile->request.weaponName==name)return true;}
    return false;
}
}

namespace hades {
bool ProjectileWorld::updateBase(uint64_t handle,float incoming,const ProjectileUpdateProviders& providers,bool& remove,std::string& error){
    // Base sgg::Projectile::UpdateSelf6a720 (2616 bytes; REA
    // ev_39ca087fe952d42a4a9edf17b06ecf1e3bd8b289301a832729d9fd85f1dbf13a and
    // LLVM .local/reports/native-projectile-base-update-self-static.json).
    // Return1 = manager removal; every return1 path first calls vslot0x1a0
    // OnDeath, except Move/StepMove results. Contract:
    // native/analysis/base-projectile-update-contract.json.
    remove=false;auto* entity=actor(handle);
    if(!entity||!entity->shotInitialized||!entity->active||entity->disposed||!entity->firingArsenal||!entity->firingWorld){error="Base projectile update requires actual initialized active projectile/source binding";return false;}
    if(entity->descriptor.concreteClass!=ProjectileClass::Straight){error="Base Projectile::UpdateSelf reached by an overriding subclass";return false;}
    auto current=[&](){return entity->firingArsenal->projectileData(entity->request.weaponName,entity->spawnedDefinition,error);};
    auto scalar=[&](const char* key,float& out){const auto* data=current();if(!data)return false;auto it=data->fields.find(key);
        if(it==data->fields.end()||!std::holds_alternative<double>(it->second)||!std::isfinite(std::get<double>(it->second))){error=std::string("Actual projectile numeric data unavailable: ")+key;return false;}
        out=float(std::get<double>(it->second));return true;};
    auto boolean=[&](const char* key,bool& out){const auto* data=current();if(!data)return false;auto it=data->fields.find(key);
        if(it==data->fields.end()||!std::holds_alternative<bool>(it->second)){error=std::string("Actual projectile boolean data unavailable: ")+key;return false;}out=std::get<bool>(it->second);return true;};
    auto name=[&](const char* key,std::string& out){const auto* data=current();if(!data)return false;auto it=data->fields.find(key);
        if(it==data->fields.end()||!std::holds_alternative<std::string>(it->second)){error=std::string("Actual projectile name data unavailable: ")+key;return false;}out=std::get<std::string>(it->second);return true;};
    auto run=[&](ProjectileUpdateOperationKind kind,float elapsed,int32_t victim,ProjectileUpdateReply& reply){
        ProjectileUpdateOperation operation{kind,elapsed};operation.victim=victim;reply={};
        if(!providers.preflight||!providers.execute){error=std::string("Base update reached missing native operation consumer ")+projectileUpdateOperationName(kind);return false;}
        if(!providers.preflight(operation,*entity,error)||!providers.execute(operation,*entity,reply,error))return false;
        entity=actor(handle);if(!entity){error="Base projectile storage lost across native callback";return false;}
        if(kind==ProjectileUpdateOperationKind::CheckForImpact){if(!reply.impactResult){error="Native CheckForImpact consumer did not return real STOP/CONTINUE result";return false;}}
        else if(reply.impactResult){error="Impact result supplied for a different projectile operation";return false;}
        return true;};
    auto op=[&](ProjectileUpdateOperationKind kind,float elapsed){ProjectileUpdateReply reply;return run(kind,elapsed,-1,reply);};
    auto die=[&](int32_t victim){ProjectileUpdateReply reply;if(!run(ProjectileUpdateOperationKind::OnDeath,0,victim,reply))return false;remove=true;error.clear();return true;}; // vslot0x1a0
    auto sub=[](float a,float b){volatile float value=a-b;return value;};
    auto add=[](float a,float b){volatile float value=a+b;return value;};
    auto mul=[](float a,float b){volatile float value=a*b;return value;};
    float dt=incoming;
    // VA6a77a: flash timer is mLifeRemaining when < FLT_MAX, else mFuseRemaining.
    {float flash=0;if(!scalar("FlashBeforeExpireDuration",flash))return false;
        const float timer=entity->state.lifeRemaining<std::numeric_limits<float>::max()?entity->state.lifeRemaining:entity->state.fuseRemaining;
        if(flash>0&&flash>=timer){error="Base projectile FlashBeforeExpire pAnim HasComponent/Animation::Flash298e70 not reconstructed";return false;}}
    // VA6a811: DieWithOwner sets mExpired when UnitManager::Get(mOwnerId) is
    // absent or its IsAlive vslot0xd0 (Thing::IsAlive2c720) is false.
    {bool dieWithOwner=false;if(!boolean("DieWithOwner",dieWithOwner))return false;
        if(dieWithOwner){const auto* owner=entity->firingWorld->actor(entity->state.ownerId);const bool alive=owner&&owner->registered&&owner->definition&&owner->definition->kind=="Unit"&&owner->lifeStatus==0;if(!alive)entity->state.expired=true;}}
    if(entity->state.expired)return die(-1); // VA6a866 -> 6aef9
    if(entity->timeModifierFraction<1){
        const float denominator=add(mul(sub(1,providers.worldTimeMultiplier),entity->timeModifierFraction),providers.worldTimeMultiplier);
        volatile float adjusted=dt/denominator;dt=adjusted;
    }
    if(!std::isfinite(dt)){error="Native base projectile elapsed scaling is outside finite supported domain";return false;}
    if(!entity->state.hasStarted){
        entity->state.startDelay=sub(entity->state.startDelay,dt);
        if(entity->state.startDelay>0){error.clear();return true;}
        entity->state.hasStarted=true;
        if(!op(ProjectileUpdateOperationKind::CreateStartFx,dt))return false; // CreateStartFx6f1a0
        // BodyComponent::Adjust2b7440(body,+0x320 alpha fraction,{AlphaOverride or 1, start 1, 0.2s}).
        float alpha=0;if(!scalar("AlphaOverride",alpha))return false;if(alpha==0)alpha=1;
        BodyTransitionOptions options;options.duration=0.2f;bool changed=false;
        if(!entity->fractions[1].adjust(alpha,1,options,changed,error))return false;
        entity->body.alpha=entity->fractions[1].current();entity->body.graphicsDirty=true;
    }
    if(entity->state.pauseTimeRemaining>0){
        entity->state.pauseTimeRemaining=sub(entity->state.pauseTimeRemaining,dt);
        if(entity->state.pauseTimeRemaining>0){error.clear();return true;}
        if(!op(ProjectileUpdateOperationKind::UnpauseAnimation,dt))return false; // OnUnpause75550
    }
    {float distance=0,strength=0;if(!scalar("VacuumDistance",distance)||!scalar("VacuumStrength",strength))return false;
        if(distance>0&&strength>0){error="Base projectile Vacuum GetUnits/PhysicsComponent::ApplyForce pass not reconstructed";return false;}}
    if(!checkBaseProjectileDefense(handle,error))return false; // vslot0x1b8 -> 72560
    if(entity->primaryAnimation&&!op(ProjectileUpdateOperationKind::UpdateAnimation,dt))return false; // vslot0x48 Thing::UpdateAnimation2df5c0
    float blastSpeed=0,blastDuration=0;bool expand=false;
    if(!boolean("ExpandBlastDuringDetonation",expand)||!scalar("BlastSpeed",blastSpeed)||!scalar("BlastDuration",blastDuration))return false;
    if(expand&&(blastSpeed>0||blastDuration>0)){ // VA6ac46
        if(!op(ProjectileUpdateOperationKind::UpdateDetonation,dt))return false;
        entity->state.blastTimeRemaining=sub(entity->state.blastTimeRemaining,dt);
    }
    bool multi=false;float maximum=0;if(!boolean("MultiDetonate",multi)||!scalar("MaxDetonations",maximum))return false;
    if(std::floor(maximum)!=maximum){error="Noninteger native MaxDetonations";return false;}
    if((entity->state.detonated&&!multi)||(multi&&maximum>0&&float(entity->state.detonations)>=maximum)){ // VA6ac94
        float remaining=entity->state.blastTimeRemaining;
        if(!expand){
            if(!scalar("BlastSpeed",blastSpeed)||!scalar("BlastDuration",blastDuration))return false;
            if((blastSpeed>0||blastDuration>0)&&!op(ProjectileUpdateOperationKind::UpdateDetonation,dt))return false;
            remaining=entity->state.blastTimeRemaining=sub(entity->state.blastTimeRemaining,dt);
        }
        if(!(remaining>0)){ // VA6ad25-6ad52 on fresh pData
            float radius=0;if(!scalar("BlastSpeed",blastSpeed)||!scalar("DamageRadius",radius))return false;
            if(!(blastSpeed>0)||entity->state.blastRadius>=mul(entity->state.blastRadiusModifier,radius))return die(-1);
        }
        // VA6ad58: IsAlive(mAttachedTo) && Type!=BEAM -> vslot0x80
        // SetLocation(GetLocation(mAttachedTo)) — parent location, no offset.
        if(attachedAlive(*entity)&&int32_t(entity->descriptor.concreteClass)!=2){
            float x=0,y=0;if(!entity->firingWorld->location(entity->attachedTo,x,y,error)||!setLocation(handle,x,y,error))return false;}
        error.clear();return true;
    }
    entity->state.fuseRemaining=sub(entity->state.fuseRemaining,dt); // VA6add2
    if(!(0<entity->state.fuseRemaining)){
        if(!op(ProjectileUpdateOperationKind::Detonate,dt))return false; // vslot0x180 Detonate6d670
        if(!boolean("MultiDetonate",multi))return false;
        if(!multi){bool done=false;if(!blastDone(handle,done,error))return false;if(done)return die(-1);error.clear();return true;} // VA6aee9
        // ProjectileData::GetFuse dcac0 refill, same as ARC.
        float fuse=0,lo=0,hi=0;if(!scalar("Fuse",fuse)||!scalar("FuseMin",lo)||!scalar("FuseMax",hi))return false;
        if(lo!=-1&&hi!=-1){if(!providers.random){error="Multidetonation actual fuse-range RNG missing";return false;}double draw=0;if(!providers.random(draw,error))return false;if(!std::isfinite(draw)||draw<0||draw>=1){error="Native fuse RNG outside [0,1)";return false;}fuse=add(mul(float(draw),sub(hi,lo)),lo);}
        entity->state.fuseRemaining=add(entity->state.fuseRemaining,fuse);
    }
    entity->state.lifeRemaining=sub(entity->state.lifeRemaining,dt); // VA6ae22
    if(!(0<entity->state.lifeRemaining))return die(-1);
    entity->state.moveIntervalRemaining=sub(entity->state.moveIntervalRemaining,dt); // VA6ae40
    if(0<entity->state.moveIntervalRemaining){error.clear();return true;}
    {float interval=0;if(!scalar("MoveInterval",interval))return false;entity->state.moveIntervalRemaining=add(entity->state.moveIntervalRemaining,interval);}
    bool follow=false;if(!boolean("FollowOwner",follow))return false;
    const auto* owner=entity->firingWorld->actor(entity->request.ownerId); // UnitManager::Get(pFiredBy+0x30)
    const bool ownerExists=owner&&owner->registered&&owner->definition&&owner->definition->kind=="Unit";
    const bool beam=int32_t(entity->descriptor.concreteClass)==2;
    bool located=false;
    if(follow&&ownerExists){ // VA6ae6f -> SetLocation(owner GetLocation)
        float x=0,y=0;if(!entity->firingWorld->location(entity->request.ownerId,x,y,error)||!setLocation(handle,x,y,error))return false;located=true;
    }else if(attachedAlive(*entity)&&!beam){ // VA6af0e
        float x=0,y=0;if(!entity->firingWorld->location(entity->attachedTo,x,y,error)||!setLocation(handle,x,y,error))return false;located=true;
    }
    entity=actor(handle);
    if(!located){
        // VA6af33: a nonempty AttachToMarker (the literal "null" included) with a
        // live owner routes through StepMove75650 toward the owner marker.
        std::string marker;if(!name("AttachToMarker",marker))return false;
        if(!marker.empty()&&ownerExists){
            if(!ownerMarker_){error="AttachToMarker requires actual owner marker provider";return false;}
            float tx=0,ty=0;if(!ownerMarker_(entity->request.ownerId,marker,tx,ty,error))return false;
            ty=add(ty,mul(entity->body.z,0.866025388240814208984375f));
            float fx=0,fy=0;if(!thingLocation(*entity,fx,fy,error))return false;
            bool died=false;if(!stepMove(handle,fx,fy,tx,ty,providers,died,error))return false;
            remove=died;error.clear();return true;
        }
        if(entity->state.speed!=0||entity->state.acceleration!=0){ // VA6af5e vslot0x1a8 Move
            bool died=false;if(!moveBase(handle,dt,providers,died,error))return false;entity=actor(handle);
            bool scale=false;if(!boolean("ScaleGeometryAlongRange",scale))return false;
            if(scale){error="Base projectile ScaleGeometryAlongRange Body scale/SetupGeometry not reconstructed";return false;}
            remove=died;error.clear();return true;
        }
        if(beam){error.clear();return true;}
    }
    // Type BEAM returns 0 here; STRAIGHT continues to vslot0x1b0 CheckForImpact.
    ProjectileUpdateReply impact;if(!run(ProjectileUpdateOperationKind::CheckForImpact,dt,-1,impact))return false;
    if(*impact.impactResult!=ProjectileImpactResult::Stop){error.clear();return true;}
    return die(impact.impactVictim); // VA6aed2-6aedc
}
}

namespace hades {
// Thing attachment/location composition and base movement, reconstructed from
// EngineWin64s.dll d98546ea...: Thing::GetLocation2e0b70, GetZLocation2e0bf0,
// AttachTo2e59f0, SetAttachOffset2e0e80, OnAttachedToMoved2e5960,
// SetAngle2e0ce0, Polygon::Offset20ee10/Rotate1204b0, Projectile::SetLocation
// 6ee90, StepMove75650, Move702d0, IsReturningToOwner77a90 and Unit::
// SetLocation2f2a40. Contract: native/analysis/player-attack-dash-contract.json.
namespace {
float sub(float a,float b){volatile float v=a-b;return v;}
float add(float a,float b){volatile float v=a+b;return v;}
float mul(float a,float b){volatile float v=a*b;return v;}
const WeaponScalar* dataField(const ProjectileActor& p,const char* key,std::string& error){
    if(!p.firingArsenal){error="Projectile source Arsenal unavailable";return nullptr;}
    const auto* d=p.firingArsenal->projectileData(p.request.weaponName,p.spawnedDefinition,error);if(!d)return nullptr;
    const auto it=d->fields.find(key);if(it==d->fields.end()){error=std::string("Actual projectile data unavailable: ")+key;return nullptr;}return &it->second;}
bool dataNumber(const ProjectileActor& p,const char* key,float& out,std::string& error){const auto* v=dataField(p,key,error);if(!v)return false;
    if(!std::holds_alternative<double>(*v)||!std::isfinite(std::get<double>(*v))){error=std::string("Actual projectile data not finite scalar: ")+key;return false;}out=float(std::get<double>(*v));return true;}
bool dataBool(const ProjectileActor& p,const char* key,bool& out,std::string& error){const auto* v=dataField(p,key,error);if(!v)return false;
    if(!std::holds_alternative<bool>(*v)){error=std::string("Actual projectile data not boolean: ")+key;return false;}out=std::get<bool>(*v);return true;}
bool dataText(const ProjectileActor& p,const char* key,std::string& out,std::string& error){const auto* v=dataField(p,key,error);if(!v)return false;
    if(!std::holds_alternative<std::string>(*v)){error=std::string("Actual projectile data not name: ")+key;return false;}out=std::get<std::string>(*v);return true;}
const ActorComponents* liveUnit(const ActorWorld& world,int32_t id){const auto* a=world.actor(id);return a&&a->registered&&a->definition&&a->definition->kind=="Unit"?a:nullptr;}
}
bool ProjectileWorld::attachedAlive(const ProjectileActor& p)const{
    // EntityManager::IsAlive(mAttachedTo+0x148) && mAttachedTo(+0x140)!=null.
    if(!p.attachedTo||!p.firingWorld)return false;const auto* a=p.firingWorld->actor(p.attachedTo);
    return a&&a->registered&&!a->isDisposed&&a->entityHandle==p.attachedToLifetime;
}
bool ProjectileWorld::thingLocation(const ProjectileActor& p,float& x,float& y,std::string& error)const{
    if(!attachedAlive(p)){x=p.body.x;y=p.body.y;error.clear();return true;}
    float px=0,py=0;if(!p.firingWorld->location(p.attachedTo,px,py,error))return false;
    x=add(px,p.attachOffsetX);y=add(p.attachOffsetY,py);error.clear();return true;
}
bool ProjectileWorld::thingZ(const ProjectileActor& p,float& z,std::string& error)const{
    if(!attachedAlive(p)){z=p.body.z;error.clear();return true;}
    float pz=0;if(!p.firingWorld->zLocation(p.attachedTo,pz,error))return false;z=add(pz,p.attachOffsetZ);error.clear();return true;
}
bool ProjectileWorld::setThingAngle(uint64_t handle,float angle,std::string& error){
    try{auto* p=actor(handle);if(!p||!p->thingInitialized||p->disposed)throw std::runtime_error("Thing::SetAngle requires initialized projectile");
        if(!std::isfinite(angle))throw std::runtime_error("Nonfinite Thing::SetAngle");
        const auto& data=*p->thing;
        // +0x1f0 is the live polygon flag; portable geometry vector holds it.
        if(!p->geometry.empty()){
            if(flag(data,"RotateGeometry")){
                const float delta=sub(p->body.angle,angle);
                float cx=0,cy=0;if(!thingLocation(*p,cx,cy,error))return false;
                if(delta!=0){const float c=std::cos(delta),s=std::sin(delta);
                    for(auto& v:p->geometry){const float dx=sub(v.x,cx),dy=sub(v.y,cy);v.x=add(cx,sub(mul(dx,c),mul(dy,s)));v.y=add(cy,add(mul(dy,c),mul(dx,s)));}}
            }else if(flag(data,"MirrorGeometry")){
                const auto left=[](float a){return a>1.5707963705062866f&&4.71238899230957f>a;};
                if(left(p->body.angle)!=left(angle))throw std::runtime_error("Thing::SetAngle MirrorGeometry Polygon::FlipHorizontal not reconstructed");
            }
        }
        constexpr float turn=6.283185482025146484375f;
        float a=angle;a=sub(a,mul(std::floor(a/turn),turn));a=sub(a,mul(std::floor(a/turn),turn));
        p->body.angle=a;p->body.graphicsDirty=true; // BodyComponent::MarkDirty; mDirection.z unchanged.
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();faulted_=true;fault_=error;return false;}
}
bool ProjectileWorld::setLocation(uint64_t handle,float x,float y,std::string& error){
    try{auto* p=actor(handle);if(!p||!p->thingInitialized||p->disposed||!p->firingWorld)throw std::runtime_error("Projectile::SetLocation requires initialized projectile");
        if(!std::isfinite(x)||!std::isfinite(y))throw std::runtime_error("Nonfinite Projectile::SetLocation");
        if(!thingLocation(*p,p->previousX,p->previousY,error))return false; // +0x508
        std::string marker;if(!dataText(*p,"AngleFromMarker",marker,error))return false;
        if(!marker.empty()&&liveUnit(*p->firingWorld,p->request.ownerId)){
            if(!ownerMarker_)throw std::runtime_error("Projectile AngleFromMarker requires actual owner marker provider");
            float mx=0,my=0;if(!ownerMarker_(p->request.ownerId,marker,mx,my,error))return false;
            my=add(my,mul(p->body.z,0.866025388240814208984375f)); // raw Thing+0x74, not attached Z
            const float a=std::atan2(mul(sub(y,my),-1.f),sub(x,mx));constexpr float turn=6.283185482025146484375f;
            if(!setThingAngle(handle,sub(a,mul(std::floor(a/turn),turn)),error))return false;p=actor(handle);
        }
        const float dx=sub(x,p->body.x),dy=sub(sub(y,0.f),p->body.y);
        p->body.x=x;p->body.y=y;
        if(dx!=0||dy!=0)for(auto& v:p->geometry){v.x=add(dx,v.x);v.y=add(dy,v.y);} // Polygon::Offset
        // Draw rectangle (+0x98) has no portable consumer for projectiles.
        p->body.graphicsDirty=true;
        const bool external=p->primaryAnimation||p->attachedAnimation||!p->attachedAnimations.empty()||!text(*p->thing,"Light").empty();
        if(external)throw std::runtime_error("Projectile::SetLocation MarkSortScoreChanged/attached graphic propagation consumer not reconstructed");
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();faulted_=true;fault_=error;return false;}
}
bool ProjectileWorld::ownerMoved(int32_t unit,std::string& error){
    for(const auto& [handle,p]:actors_){
        if(p->disposed||!p->thingInitialized||p->attachedTo!=unit||!attachedAlive(*p))continue;
        float x=0,y=0;if(!thingLocation(*p,x,y,error)||!setLocation(handle,x,y,error))return false; // OnAttachedToMoved2e5960
    }
    error.clear();return true;
}
bool ProjectileWorld::stepMove(uint64_t handle,float fromX,float fromY,float toX,float toY,const ProjectileUpdateProviders& providers,bool& died,std::string& error){
    died=false;auto* p=actor(handle);if(!p){error="StepMove lost projectile";return false;}
    // Step: half the minimum distance between distinct polygon vertices with
    // indices >=1 (native loops start at1), at least2.
    float step=0;const size_t n=p->geometry.size();
    if(n){float best=std::numeric_limits<float>::max();for(size_t i=1;i<n;++i)for(size_t j=1;j<n;++j){if(i==j)continue;const float ax=sub(p->geometry[i].x,p->geometry[j].x),ay=sub(p->geometry[i].y,p->geometry[j].y);const float d=add(mul(ax,ax),mul(ay,ay));if(d<best)best=d;}step=std::sqrt(best);}
    step=mul(step,0.5f);if(step<=2.f)step=2.f;
    const float dx=sub(toX,fromX),dy=sub(toY,fromY);const float distance=std::sqrt(add(mul(dx,dx),mul(dy,dy)));
    float travelled=0;
    if(0<distance)do{
        float segment=step;if(sub(distance,travelled)<=step)segment=sub(distance,travelled);
        travelled=add(travelled,segment);
        float x=0,y=0;if(!thingLocation(*p,x,y,error))return false;
        const float inverse=1.f/distance;x=add(mul(mul(inverse,dx),segment),x);y=add(mul(mul(inverse,dy),segment),y);
        if(!setLocation(handle,x,y,error))return false;p=actor(handle);
        // vslot0x78 ShiftZLocation(mDirection.z*segment): Thing ctor zeroes
        // mDirection and SetAngle writes only x/y, so the shift is zero here.
        ProjectileUpdateOperation operation{ProjectileUpdateOperationKind::CheckForImpact,0};ProjectileUpdateReply reply;
        if(!providers.preflight||!providers.execute||!providers.preflight(operation,*p,error)||!providers.execute(operation,*p,reply,error))return false;
        p=actor(handle);if(!p){error="StepMove lost projectile across impact";return false;}
        if(!reply.impactResult){error="Native CheckForImpact consumer did not return real STOP/CONTINUE result";return false;}
        if(*reply.impactResult==ProjectileImpactResult::Stop){
            if(0<p->state.blastTimeRemaining){error.clear();return true;}
            float speed=0,radius=0;if(!dataNumber(*p,"BlastSpeed",speed,error)||!dataNumber(*p,"DamageRadius",radius,error))return false;
            if(0<speed&&p->state.blastRadius<mul(radius,p->state.blastRadiusModifier)){error.clear();return true;}
            ProjectileUpdateOperation death{ProjectileUpdateOperationKind::OnDeath,0};death.victim=reply.impactVictim;ProjectileUpdateReply ignored;
            if(!providers.preflight(death,*p,error)||!providers.execute(death,*p,ignored,error))return false;
            died=true;error.clear();return true;
        }
    }while(travelled<distance);
    error.clear();return true;
}
bool ProjectileWorld::moveBase(uint64_t handle,float dt,const ProjectileUpdateProviders& providers,bool& died,std::string& error){
    died=false;auto* p=actor(handle);if(!p||!p->firingWorld){error="Projectile::Move lost projectile";return false;}
    float deceleration=0,jump=0,returnSpeed=0,inactive=0;bool finalJump=false;
    if(!dataNumber(*p,"Deceleration",deceleration,error)||!dataNumber(*p,"JumpSpeedMultiplier",jump,error)||!dataNumber(*p,"ReturnToOwnerSpeed",returnSpeed,error)||
       !dataNumber(*p,"ReturnToOwnerAfterInactiveSeconds",inactive,error)||!dataBool(*p,"FinalJumpToOwner",finalJump,error))return false;
    auto& s=p->state;
    auto clamp=[&](float v){if(v<=0)v=0;if(s.maxSpeed<=v)v=s.maxSpeed;return v;};
    if(!s.hitMaxRange||deceleration==0){if(s.acceleration!=0||jump!=1.f)s.speed=clamp(add(mul(s.acceleration,dt),s.speed));}
    else s.speed=clamp(sub(s.speed,mul(deceleration,dt)));
    // IsReturningToOwner77a90 is false unless FinalJumpToOwner or
    // ReturnToOwnerAfterInactiveSeconds; those owner-return branches are not reconstructed.
    if(finalJump||inactive!=0){error="Projectile::IsReturningToOwner owner-return branch not reconstructed";return false;}
    float speed=s.speed;
    // PROJECTILE_EFFECTORS slow zones (copied vector, insertion order).
    const auto effectors=effectors_;
    for(const auto e:effectors){const auto* q=actor(e);if(!q){error="Projectile effector storage lost";return false;}
        float multiplier=0;if(!dataNumber(*q,"SpeedMultiplierOfEnemyProjectilesInside",multiplier,error))return false;if(multiplier==0)continue;
        if(liveUnit(*p->firingWorld,q->state.ownerId)==liveUnit(*p->firingWorld,s.ownerId)||s.firedByTraitor)continue;
        error="Projectile slow-zone IsInBlast760e0 against a projectile target not reconstructed";return false;
    }
    if(speed==0){error.clear();return true;}
    const float dirX=std::cos(p->body.angle),dirY=-std::sin(p->body.angle); // Thing+0xb0/0xb4 from SetAngle
    float x=0,y=0;if(!thingLocation(*p,x,y,error))return false;
    const float tx=add(mul(mul(dirX,speed),dt),x),ty=add(mul(mul(dirY,speed),dt),y);
    float fx=0,fy=0;if(!thingLocation(*p,fx,fy,error))return false;
    return stepMove(handle,fx,fy,tx,ty,providers,died,error);
}
bool ProjectileWorld::cancelAttached(int32_t unit,uint64_t unitLifetime,const std::function<bool(const ProjectileActor&,bool&,std::string&)>& firing,std::string& error){
    // .local/reports/native-player-fire-post-fire-pseudocode.c l.125-147: ProjectileData +0x5d8
    // AttachToOwner, +0x5d9 SkipFireCancelIfFiring, pFiredBy(+0x410) Weapon+0xd0 (current
    // FireRequest engaged byte before mPrevFireRequest +0xd8).
    for(const auto handle:active_){auto* p=actor(handle);if(!p||p->disposed)continue;
        bool attach=false,skip=false;if(!dataBool(*p,"AttachToOwner",attach,error)||!dataBool(*p,"SkipFireCancelIfFiring",skip,error))return false;
        if(!attach)continue;
        if(skip&&p->firingArsenal){bool live=false;if(!firing(*p,live,error))return false;if(live)continue;}
        if(p->attachedTo==unit&&p->attachedToLifetime==unitLifetime)p->state.expired=true;
    }
    error.clear();return true;
}
bool ProjectileWorld::expire(const std::vector<std::string>& names,const std::optional<std::string>& weapon,
        const std::optional<std::vector<std::string>>& exclude,std::string& error){
    for(const auto handle:active_){auto* p=actor(handle);if(!p||p->disposed)continue;
        bool candidate=std::find(names.begin(),names.end(),p->body.name)!=names.end();
        if(!candidate){
            if(!names.empty()||weapon)candidate=weapon&&p->firingArsenal&&*weapon==p->request.weaponName;
            else candidate=true;
        }
        if(!candidate)continue;
        if(exclude&&std::find(exclude->begin(),exclude->end(),p->body.name)!=exclude->end())continue;
        p->state.expired=true; // +0x4c2
    }
    error.clear();return true;
}
}
