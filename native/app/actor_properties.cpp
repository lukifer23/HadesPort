#include "actor_runtime.hpp"
#include <cmath>
#include <stdexcept>

namespace hades {
namespace {
const SourceValue& required(const ActorWorld& world,int32_t id,const char* path){
    const auto* value=world.currentProperty(id,path);
    if(!value)throw std::runtime_error(std::string("Native property-dirty input unavailable: ")+path);
    return *value;
}
bool ownerHasNoCollision(const ActorComponents& actor){
    if(actor.animation.active()){
        const auto* clip=actor.animation.clip();
        if(!clip)throw std::runtime_error("Active primary animation has no original definition");
        return (clip->flags&ActorNoCollision)!=0;
    }
    if(actor.primaryEffectAnimation||actor.primaryAnimationLifetime){
        if(!actor.primaryOwnerHasNoCollision)throw std::runtime_error("Actual FX/book primary OwnerHasNoCollision must be supplied before obstacle cache refresh");
        return *actor.primaryOwnerHasNoCollision;
    }
    return false; // Actual native pAnim==null branch.
}
}
bool actorOwnerHasNoCollision(const ActorComponents& actor){return ownerHasNoCollision(actor);}

bool ActorWorld::obstacleStops(int32_t id,bool projectiles,bool& result,std::string& error){
    try{
        auto* a=actor(id);
        if(!a||!a->registered||!a->object||!a->definition||a->definition->kind!="Obstacle")throw std::runtime_error("Native obstacle stop query requires a registered actual obstacle");
        bool& dirty=projectiles?a->stopsProjectilesDirty:a->stopsUnitsDirty;
        bool& cached=projectiles?a->stopsProjectiles:a->stopsUnits;
        if(!dirty){result=cached;error.clear();return true;}
        // Native compares structural parent handle against mNull before any
        // primary/MapThing/data read. Portable parentId0 is its mapped absence.
        bool stops=false;
        if(!a->parentId&&!ownerHasNoCollision(*a)){
            const auto* sourceSeed=a->sourceId?seed(a->sourceId):nullptr;
            bool collision=true;
            if(sourceSeed){const auto* value=sourceSeed->map.get("Collision");if(!value)throw std::runtime_error("Original MapThing.Collision unavailable");collision=value->boolean();}
            if(collision){
                const float ownZ=a->object->z;
                const float maximum=float(required(*this,id,"MaxCollisionZ").number());
                if(!std::isfinite(ownZ)||!std::isfinite(maximum))throw std::runtime_error("Nonfinite original obstacle collision-Z input");
                if(ownZ<=maximum)stops=required(*this,id,projectiles?"Thing.StopsProjectiles":"Thing.StopsUnits").boolean();
            }
        }
        cached=stops;dirty=false;result=stops;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

bool ActorWorld::propertiesDirty(int32_t id,std::string& error){
    try{
        auto* a=actor(id);
        if(!a||!a->registered||!a->object||!a->definition)throw std::runtime_error("Property dirty dispatch requires registered actor/body");
        const bool obstacle=a->definition->kind=="Obstacle";
        bool queue=false;
        if(!a->object->screenSpace){
            // Obstacle's override queues grouped parents directly. An empty
            // structural children vector falls through to Thing's real gate.
            if(obstacle&&!a->children.empty())queue=true;
            else if(a->object->visible&&!required(*this,id,"Thing.Invisible").boolean()&&
                    (a->animation.active()||a->primaryEffectAnimation||a->primaryAnimationLifetime||a->hasText))queue=true;
        }
        if(queue)sortDirty_[a->entityHandle]=id;
        if(!obstacle){error.clear();return true;}
        a->stopsUnitsDirty=true;bool ignored=false;
        if(!obstacleStops(id,false,ignored,error))return false;
        a->stopsProjectilesDirty=true;
        if(!obstacleStops(id,true,ignored,error))return false;
        // Native LocationChanged(true) returns immediately for disposed actor.
        if(a->isDisposed){error.clear();return true;}
        // LocationChanged additionally refreshes spatial-grid/draw/use/light/
        // occluder/attached-animation state. It is a real provider boundary;
        // refreshing one polygon alone would silently omit these operations.
        if(!obstacleLocationChanged_){error="Obstacle properties require actual LocationChanged(true) provider";return false;}
        a->drawBoundsChanged=true; // LocationChanged writes this before grid work.
        if(!obstacleLocationChanged_(id,true,error))return false;
        // Native LocationChanged marks both caches dirty again after queuing
        // draw/light updates; subsequent actual consumers recalculate lazily.
        if(!a->object->screenSpace){
            if(!a->children.empty()||
               (a->object->visible&&!required(*this,id,"Thing.Invisible").boolean()&&
                (a->animation.active()||a->primaryEffectAnimation||a->primaryAnimationLifetime||a->hasText)))sortDirty_[a->entityHandle]=id;
        }
        a->stopsUnitsDirty=a->stopsProjectilesDirty=true;
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

std::vector<int32_t> ActorWorld::takeSortDirty(){
    std::vector<int32_t> out;
    for(const auto& [handle,id]:sortDirty_){const auto* a=actor(id);if(a&&a->registered&&a->entityHandle==handle)out.push_back(id);}
    sortDirty_.clear();return out;
}
}
