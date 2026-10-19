#include "actor_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hades {
bool ActorWorld::clearObstacleGridRecord(int32_t id,std::string& error){
    const auto* a=actor(id);if(!a){error="Grid removal requires retained actor lifetime";return false;}
    spatialGrid_.remove(a->entityHandle);error.clear();return true;
}
bool ActorWorld::removeObstacleDrawGridRecord(int32_t id,std::string& error){
    const auto* a=actor(id);if(!a){error="Draw-grid removal requires retained actor lifetime";return false;}
    drawGridDirty_.erase(a->entityHandle);
    // The Android renderer owns scene primaries separately. Its mandatory
    // lifecycle ReleasePrimaryAnimation stage retires actual drawable storage.
    if(a->object)a->object->graphicsDirty=true;
    error.clear();return true;
}
bool ActorWorld::registerObstacleCollision(int32_t id,std::string& error){
    try{
        auto* a=actor(id);if(!a||!a->registered||!a->object||!a->definition||a->definition->kind!="Obstacle")throw std::runtime_error("Collision-grid registration requires actual registered obstacle");
        if(a->isDisposed){error.clear();return true;}
        if(a->object->geometryDirty&&!refreshGeometry(id,error))return false;
        SpatialObstacle record;record.id=id;record.lifetime=a->entityHandle;
        if(!obstacleStops(id,true,record.stopsProjectiles,error)||!obstacleStops(id,false,record.stopsUnits,error))return false;
        const auto* sourceSeed=a->sourceId?seed(a->sourceId):nullptr;
        if(sourceSeed){
            if(!record.stopsProjectiles){const auto* movement=sourceSeed->map.get("AllowMovementReaction");const auto* attachment=sourceSeed->map.get("AttachToID");if(!movement||!attachment)throw std::runtime_error("Original MapThing movement-reaction registration fields unavailable");record.movementReaction=movement->boolean()&&attachment->number()==0;}}
        {
            if(!a->geometry.empty()){
                record.bounds={a->geometry.front().x,a->geometry.front().y,a->geometry.front().x,a->geometry.front().y};
                for(const auto& p:a->geometry){record.bounds.minX=std::min(record.bounds.minX,p.x);record.bounds.maxX=std::max(record.bounds.maxX,p.x);record.bounds.minY=std::min(record.bounds.minY,p.y);record.bounds.maxY=std::max(record.bounds.maxY,p.y);}
            }else{
                if(record.stopsProjectiles||record.stopsUnits)throw std::runtime_error("Blocking obstacle lacks actual transformed geometry");
                float x=0,y=0;if(!location(id,x,y,error))return false;
                // Conservative portable point-proxy for noncolliding decor.
                // It is never admitted as a unit/projectile collision polygon.
                record.bounds={x-1,y-1,x+1,y+1};
            }
            // Forced native updates clear/reinsert even unchanged coverage;
            // Android additionally refreshes conservative bounds every update.
            if(!spatialGrid_.update(record,error))return false;
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::obstacleLocationChanged(int32_t id,bool force,std::string& error){
    try{
        auto* a=actor(id);if(!a||!a->registered||!a->object||!a->definition||a->definition->kind!="Obstacle")throw std::runtime_error("LocationChanged requires actual registered obstacle");
        if(a->isDisposed){error.clear();return true;}
        a->drawBoundsChanged=true;
        (void)force;if(!registerObstacleCollision(id,error))return false;
        // Android renderer consumes actual ObjectSnapshot changes; keep a
        // distinct lifetime queue for its draw-bounds index consumer.
        drawGridDirty_[a->entityHandle]=id;a->object->graphicsDirty=true;
        if(!locationAuxiliary_)throw std::runtime_error("LocationChanged requires actual auxiliary component/attached-animation registry provider");
        for(const auto stage:{ActorLocationStage::Interaction,ActorLocationStage::Light,ActorLocationStage::Occluder})if(!locationAuxiliary_(id,stage,error))return false;
        const auto* invisible=currentProperty(id,"Thing.Invisible");if(!invisible)throw std::runtime_error("Original Thing.Invisible unavailable");
        if(!a->object->screenSpace&&(!a->children.empty()||(a->object->visible&&
            !invisible->boolean()&&(a->animation.active()||a->primaryEffectAnimation||a->primaryAnimationLifetime||a->hasText))))sortDirty_[a->entityHandle]=id;
        a->stopsUnitsDirty=a->stopsProjectilesDirty=true;
        if(!locationAuxiliary_(id,ActorLocationStage::AttachedAnimations,error))return false;
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ActorWorld::rebuildObstacleGrid(std::string& error){
    // Source initial obstacle admission also invokes LocationChanged. This
    // explicit rebuild applies the same actual auxiliary update boundaries.
    if(!locationAuxiliary_){error="Obstacle grid rebuild requires real location auxiliary provider";return false;}
    for(const auto id:registrationOrder_){const auto* a=actor(id);if(a&&a->registered&&!a->isDisposed&&a->definition&&a->definition->kind=="Obstacle")if(!obstacleLocationChanged(id,true,error))return false;}
    error.clear();return true;
}
bool ActorWorld::spatialObstacleCandidates(const SpatialBounds& bounds,ObstacleGridCategory category,std::vector<int32_t>& out,std::string& error)const{
    std::vector<SpatialObstacle> candidates;if(!spatialGrid_.query(bounds,category,candidates,error))return false;out.clear();
    for(const auto& record:candidates){const auto* a=actor(record.id);if(a&&a->registered&&!a->isDisposed&&a->entityHandle==record.lifetime)out.push_back(record.id);}
    error.clear();return true;
}
std::vector<int32_t> ActorWorld::takeDrawGridDirty(){
    std::vector<int32_t> out;for(const auto& [handle,id]:drawGridDirty_){const auto* a=actor(id);if(a&&a->registered&&!a->isDisposed&&a->entityHandle==handle)out.push_back(id);}drawGridDirty_.clear();return out;
}
bool ActorWorld::movementReactionTargets(int32_t root,std::vector<int32_t>& out,std::string& error)const{
    out.clear();std::vector<int32_t> stack{root},visited;
    while(!stack.empty()){const int32_t id=stack.back();stack.pop_back();
        if(std::find(visited.begin(),visited.end(),id)==visited.end())visited.push_back(id);
        const auto* a=actor(id);if(!a||!a->registered||!a->definition||a->definition->kind!="Obstacle")continue; // HandleManager::Get null
        const auto* s=a->sourceId?seed(a->sourceId):nullptr;
        if(s){const auto* allow=s->map.get("AllowMovementReaction");if(!allow){error="Original MapThing AllowMovementReaction unavailable";return false;}if(allow->boolean())out.push_back(id);}
        for(size_t i=a->children.size();i-->0;){const int32_t c=a->children[i];if(std::find(visited.begin(),visited.end(),c)==visited.end())stack.push_back(c);}
    }
    error.clear();return true;
}
}
