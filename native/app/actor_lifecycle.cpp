#include "actor_runtime.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include <set>
#include <stdexcept>

namespace hades {
const char* actorDisposalStageName(ActorDisposalStage stage){
    switch(stage){
    case ActorDisposalStage::AttachedAnimationsPreDispose:return "AttachedAnimationsPreDispose";
    case ActorDisposalStage::OwnerDeathAnimations:return "OwnerDeathAnimations";
    case ActorDisposalStage::DrawGridRemove:return "DrawGridRemove";
    case ActorDisposalStage::LifeDispose:return "LifeDispose";
    case ActorDisposalStage::ClearGridRecord:return "ClearGridRecord";
    case ActorDisposalStage::RemoveAutoLockableAndUseable:return "RemoveAutoLockableAndUseable";
    case ActorDisposalStage::StopAmbientSound:return "StopAmbientSound";
    case ActorDisposalStage::AudioOwnerDeath:return "AudioOwnerDeath";
    case ActorDisposalStage::ReleasePrimaryAnimation:return "ReleasePrimaryAnimation";
    case ActorDisposalStage::ClearAnimationCollections:return "ClearAnimationCollections";
    case ActorDisposalStage::RemoveLight:return "RemoveLight";
    case ActorDisposalStage::ResetComponents:return "ResetComponents";
    case ActorDisposalStage::WorldOnDisposed:return "WorldOnDisposed";
    case ActorDisposalStage::ObstacleTail:return "ObstacleTail";
    case ActorDisposalStage::FreeRemainingComponents:return "FreeRemainingComponents";
    case ActorDisposalStage::RetireManagerHandle:return "RetireManagerHandle";
    case ActorDisposalStage::RetireEntity:return "RetireEntity";
    }
    return "Unknown";
}

bool ActorWorld::queueObstacleDestroy(int32_t id,std::string& error){
    if(disposalFaulted_){error=disposalFault_;return false;}
    auto* a=actor(id);
    if(!a||!a->definition||a->definition->kind!="Obstacle"||!a->object){error="Native obstacle Destroy requires an actual obstacle";return false;}
    if(a->isDisposed||!a->registered){error.clear();return true;}
    if(a->hasLife&&a->lifeStatus==0){error="Obstacle with Life requires its real death animation/audio path before Destroy admission";return false;}
    if(!a->entityHandle){error="Registered obstacle lacks a lifetime";return false;}
    // Thing::Destroy no-Life branch writes mLifeStatus DEAD; Obstacle::Destroy
    // inserts the manager handle into its deferred sorted set. No unref/removal.
    a->lifeStatus=1;
    obstacleDestroyQueue_.emplace(a->entityHandle,id);
    error.clear();return true;
}

bool ActorWorld::advanceObstacleDestruction(const ActorDisposalCallback& callback,std::string& error){
    if(disposalFaulted_){error=disposalFault_;return false;}
    if(disposalInProgress_){error="Reentrant obstacle destruction pump";return false;}
    // Source captures this phase's sorted handle count. Callback admissions are
    // retained for the next call. Portable lifetimes supply its manager order;
    // native Windows numeric handles themselves are not reused as actor IDs.
    const auto ready=obstacleDestroyQueue_;
    for(const auto& [lifetime,id]:ready){
        auto* a=actor(id);
        if(!a||!a->registered||a->isDisposed||a->entityHandle!=lifetime){obstacleDestroyQueue_.erase(lifetime);continue;}
        if(!disposeObstacleTree(id,callback,error))return false;
        obstacleDestroyQueue_.erase(lifetime);
    }
    error.clear();return true;
}

bool ActorWorld::disposeObstacleTree(int32_t id,const ActorDisposalCallback& callback,std::string& error){
    if(disposalFaulted_){error=disposalFault_;return false;}
    if(disposalInProgress_){error="Reentrant obstacle disposal";return false;}
    auto* first=actor(id);
    if(!first||!first->registered||first->isDisposed){error.clear();return true;}
    if(!callback){error="Obstacle disposal requires actual provider callbacks";return false;}
    // Validate actual ownership before the first irreversible stage. Life
    // disposal is delivered to the real provider before entity retirement.
    std::deque<int32_t> check{id};std::set<int32_t> visited;
    while(!check.empty()){
        const auto next=check.front();check.pop_front();
        if(!visited.insert(next).second){error="Structural obstacle cycle or duplicate child";return false;}
        const auto* a=actor(next);
        if(!a||!a->registered||a->isDisposed)continue;
        if(!a->definition||a->definition->kind!="Obstacle"||!a->object||!a->entityHandle){error="Obstacle disposal subtree contains an unavailable actor";return false;}
        for(auto child:a->children)check.push_back(child);
    }
    disposalInProgress_=true;
    try{
        // Native removes only the entry root from its surviving parent's vector.
        // Children are queued before each parent's Dispose clears its own vector.
        if(first->parentId){
            if(auto* parent=actor(first->parentId)){
                const auto i=std::find(parent->children.begin(),parent->children.end(),id);
                if(i!=parent->children.end())parent->children.erase(i);
            }
        }
        std::deque<std::pair<int32_t,uint64_t>> queue{{id,first->entityHandle}};
        while(!queue.empty()){
            const auto [next,lifetime]=queue.front();queue.pop_front();
            auto* a=actor(next);
            if(!a||a->isDisposed||!a->registered||a->entityHandle!=lifetime)continue;
            for(auto child:a->children){const auto* c=actor(child);if(c&&c->registered&&!c->isDisposed)queue.emplace_back(child,c->entityHandle);}
            bool hasBody=true;
            auto stage=[&](ActorDisposalStage value){
                const ActorDisposalEvent event{next,lifetime,value,a->hasLife,a->hasText,bool(a->physics),hasBody,
                    a->animation.active(),a->primaryAnimationLifetime,a->primaryEffectAnimation,
                    a->parentId,a->attachedParent,uint32_t(a->children.size()),uint32_t(a->attachedChildren.size())};
                std::string detail;
                if(!callback(event,detail))throw std::runtime_error(std::string("Obstacle disposal ")+actorDisposalStageName(value)+": "+(detail.empty()?"provider rejected stage":detail));
            };
            stage(ActorDisposalStage::AttachedAnimationsPreDispose);
            stage(ActorDisposalStage::OwnerDeathAnimations);
            stage(ActorDisposalStage::DrawGridRemove);
            if(a->hasLife)stage(ActorDisposalStage::LifeDispose);
            stage(ActorDisposalStage::ClearGridRecord);
            stage(ActorDisposalStage::RemoveAutoLockableAndUseable);
            stage(ActorDisposalStage::StopAmbientSound);
            stage(ActorDisposalStage::AudioOwnerDeath);
            stage(ActorDisposalStage::ReleasePrimaryAnimation);
            a->animation.reset();a->primaryEffectAnimation=0;a->primaryAnimationLifetime=0;
            a->primaryOwnerHasNoCollision.reset();
            stage(ActorDisposalStage::ClearAnimationCollections);
            stage(ActorDisposalStage::RemoveLight);
            stage(ActorDisposalStage::ResetComponents);
            a->physics.reset();a->physicsPending=false;a->hasText=false;
            stage(ActorDisposalStage::WorldOnDisposed);
            a->registered=false;
            a->object->active=false;a->object->alive=false;
            a->entityHandle=0;a->object->entityHandle=0; // Thing.Dispose(bool) tail.
            sortDirty_.erase(lifetime);
            stage(ActorDisposalStage::ObstacleTail);
            a->children.clear();a->parentId=0;
            // Native writes mIsDisposed only after virtual Dispose(true).
            a->isDisposed=true;
            stage(ActorDisposalStage::FreeRemainingComponents);
            hasBody=false;
            stage(ActorDisposalStage::RetireManagerHandle);
            stage(ActorDisposalStage::RetireEntity);
            a->physicsActive=false;
            obstacleDestroyQueue_.erase(lifetime);
            // mAttachedLua is intentionally untouched: ordinary Dispose does
            // not call luaL_unref. Parent retains its reference owner separately.
        }
        disposalInProgress_=false;error.clear();return true;
    }catch(const std::exception& e){
        disposalInProgress_=false;disposalFaulted_=true;disposalFault_=e.what();error=disposalFault_;return false;
    }catch(...){
        disposalInProgress_=false;disposalFaulted_=true;disposalFault_="Unknown obstacle disposal provider failure";error=disposalFault_;return false;
    }
}
}
