#include "actor_runtime.hpp"
#include <algorithm>
#include <stdexcept>
namespace hades {
bool ActorWorld::queueReadyUnitRetirement(int32_t id,const std::string& primary,std::string& error){
    if(disposalFaulted_){error=disposalFault_;return false;}
    const auto* a=actor(id);if(!a||!a->registered||a->isDisposed){error.clear();return true;}
    if(!a->definition||a->definition->kind!="Unit"||!a->object){error="Unit retirement requires actual Unit/body/data";return false;}
    if(a->lifeStatus==0){error.clear();return true;}
    const auto* ragdoll=currentProperty(id,"RagDollOnDeath");const auto* finish=currentProperty(id,"FinishAnimOnDeath");
    const auto* death=currentProperty(id,"Life.DeathGraphic");
    if(!ragdoll||!finish||(a->hasLife&&!death)){error="Dead Unit retirement source data unavailable";return false;}
    // Unit.UpdateSelf2f1632 + Physics.IsAtRest2bd490; native mZero is zero.
    if(ragdoll->boolean()&&a->physics){const auto& p=*a->physics;
        if(!(p.velocity.x==0&&p.velocity.y==0&&p.upVelocity==0&&p.spin==0)){error.clear();return true;}
    }
    if(death&&!death->text().empty()&&finish->boolean()&&primary==death->text()){error.clear();return true;}
    const auto lifetime=a->entityHandle;if(!lifetime){error="Unit retirement lacks a live entity lifetime";return false;}
    const auto same=[&](const auto& entry){return entry.first==lifetime;};
    if(std::find_if(unitDestroyQueue_.begin(),unitDestroyQueue_.end(),same)==unitDestroyQueue_.end())unitDestroyQueue_.emplace_back(lifetime,id);
    error.clear();return true;
}
bool ActorWorld::advanceUnitDestruction(const ActorDisposalCallback& callback,const UnitDestructorCallback& destructor,std::string& error){
    if(disposalFaulted_){error=disposalFault_;return false;}
    if(disposalInProgress_){error="Reentrant UnitManager removal pump";return false;}
    if(unitDestroyQueue_.empty()){error.clear();return true;}
    if(!callback||!destructor){error="Unit removal requires real Thing/Unit destructor providers";return false;}
    const auto ready=unitDestroyQueue_;disposalInProgress_=true;
    try{
        for(const auto& [lifetime,id]:ready){auto* a=actor(id);
            if(!a||!a->registered||a->entityHandle!=lifetime||a->isDisposed)continue;
            if(!a->definition||a->definition->kind!="Unit"||!a->object)throw std::runtime_error("Unit removal queue identity mismatch");
            // Portable Unit manager collection owns registrationOrder. Native
            // Remove erases ALL_UNITS/player/enemy/control registries first;
            // no traversal or retirement of structural children occurs here.
            std::erase(registrationOrder_,id);
            auto stage=[&](ActorDisposalStage value){ActorDisposalEvent event{id,lifetime,value,a->hasLife,a->hasText,bool(a->physics),true,a->animation.active(),a->primaryAnimationLifetime,a->primaryEffectAnimation,a->parentId,a->attachedParent,uint32_t(a->children.size()),uint32_t(a->attachedChildren.size())};
                std::string detail;if(!callback(event,detail))throw std::runtime_error("Unit Thing disposal "+std::string(actorDisposalStageName(value))+": "+detail);};
            auto unit=[&](UnitDestructorStage value){std::string detail;if(!destructor(id,lifetime,value,detail))throw std::runtime_error("Unit destructor provider: "+detail);};
            // UnitManager.Remove2f9cbc ->DrawGrid.Remove before Thing.Dispose.
            stage(ActorDisposalStage::DrawGridRemove);
            stage(ActorDisposalStage::AttachedAnimationsPreDispose);
            stage(ActorDisposalStage::OwnerDeathAnimations);
            if(a->hasLife)stage(ActorDisposalStage::LifeDispose);
            stage(ActorDisposalStage::ClearGridRecord);
            stage(ActorDisposalStage::RemoveAutoLockableAndUseable);
            stage(ActorDisposalStage::StopAmbientSound);
            stage(ActorDisposalStage::AudioOwnerDeath);
            stage(ActorDisposalStage::ReleasePrimaryAnimation);
            a->animation.reset();a->primaryEffectAnimation=0;a->primaryAnimationLifetime=0;a->primaryOwnerHasNoCollision.reset();
            stage(ActorDisposalStage::ClearAnimationCollections);
            stage(ActorDisposalStage::RemoveLight);
            stage(ActorDisposalStage::ResetComponents);
            a->physics.reset();a->physicsPending=false;a->hasText=false;
            stage(ActorDisposalStage::WorldOnDisposed);
            a->registered=false;a->object->active=false;a->object->alive=false;
            a->entityHandle=0;a->object->entityHandle=0;sortDirty_.erase(lifetime);
            // Thing.Dispose(noargs) writes THIS+63 only after virtual
            // Dispose(true) returned (2dc358 ->2dc365), before Unit destructor.
            a->isDisposed=true;
            // UnitManager erases public ID lookup after Thing.Dispose; native
            // Unit::~Unit then performs these additional genuine operations.
            unit(UnitDestructorStage::ClearGridRecord);
            unit(UnitDestructorStage::StatusReset);
            unit(UnitDestructorStage::ArsenalDispose);
            // Retain the source data owner while real projectiles/effects can
            // reference its copied Weapon/ProjectileData. It is absent from
            // manager iteration and cannot fire. Parent owns final retention
            // release, rather than invalidating an active victim effect.
            a->status.reset();
            stage(ActorDisposalStage::FreeRemainingComponents);
            stage(ActorDisposalStage::RetireManagerHandle);
            stage(ActorDisposalStage::RetireEntity);a->physicsActive=false;
        }
        // Preserve removals admitted by a real synchronous provider callback.
        for(const auto& entry:ready)std::erase(unitDestroyQueue_,entry);
        disposalInProgress_=false;error.clear();return true;
    }catch(const std::exception& e){disposalInProgress_=false;disposalFaulted_=true;disposalFault_=e.what();error=disposalFault_;return false;}
}
}
