#include "lua_runtime.hpp"
#include "text_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace {
std::string deathCueName(const hades::SourceValue& value){
    std::string name;if(std::holds_alternative<std::string>(value.value))name=value.text();
    else{const auto* field=value.get("name");if(!field)throw std::runtime_error("Original DeathSound cue name unavailable");name=field->text();}
    return name=="null"?std::string{}:name;
}
}
bool ScriptRuntime::markThingDead(int32_t id,bool noSound,std::string& error){
    auto* actor=actorWorld_.actor(id);
    if(!actor||!actor->registered||!actor->object){error.clear();return true;}
    if(actor->hasLife){
        const auto* graphic=actorWorld_.currentProperty(id,"Life.DeathGraphic");
        const auto* fx=actorWorld_.currentProperty(id,"Life.DeathFx");
        if(!graphic||!fx){error="Destroy requires original Life death data";return false;}
        if(!setActorAnimation(id,graphic->text(),error))return false;
        if(!noSound&&!actor->suppressSounds){
            const auto* sound=actorWorld_.currentProperty(id,"Life.DeathSound");
            const auto* managerCap=actorWorld_.currentProperty(id,"Life.SoundManagerCap");
            if(!sound||!managerCap){error="Destroy requires original Life DeathSound/SoundManagerCap data";return false;}
            const double cap=managerCap->number();if(!std::isfinite(cap)||cap<INT32_MIN||cap>INT32_MAX||std::floor(cap)!=cap){error="Invalid native Life SoundManagerCap";return false;}
            std::array<float,3> location{};if(!actorWorld_.location(id,location[0],location[1],error))return false;
            // Thing.Destroy passes a copied cue by value and actual XY to the
            // location overload. Descriptor mValid is not tested at this call.
            uint32_t channel=0;if(!playLocationAudioCue(deathCueName(*sound),location,int32_t(cap),channel,error))return false;
        }
        if(!fx->text().empty()&&fx->text()!="null"){
            std::array<float,3> location{};if(!actorWorld_.location(id,location[0],location[1],error))return false;
            uint64_t handle=0;if(!createActionEffect(fx->text(),id,location,handle,error))return false;
            if(handle){
                if(actor->object->groups.empty()){error="DeathFx empty-membership GroupId0/virtual-name renderer adapter unavailable";return false;}
                if(!actionEffects_.configureCreated(handle,actor->object->z,actor->object->groups.front(),true,error))return false;
            }
        }
    }
    // Thing.Destroy2e1ef0 writes LifeStatus DEAD(+238) after genuine death
    // side effects. Disposal(+63) is a separate later transition.
    // Manager removal/Dispose is deferred; Lua state and entity remain live
    // throughout this synchronous action.
    actor->lifeStatus=1;
    // Obstacle.Destroy queues an empty DeathGraphic immediately. A genuine
    // nonempty death primary remains owned until the source manager's finish
    // condition; it must not be removed on an invented fixed delay.
    if(actor->definition&&actor->definition->kind=="Obstacle"){
        const auto* graphic=actor->hasLife?actorWorld_.currentProperty(id,"Life.DeathGraphic"):nullptr;
        if(!actor->hasLife||(graphic&&graphic->text().empty()))return actorWorld_.queueObstacleDestroy(id,error);
    }
    error.clear();return true;
}

bool ScriptRuntime::advanceObstacleDisposal(std::string& error){
    return actorWorld_.advanceObstacleDestruction([this](const hades::ActorDisposalEvent& event,std::string& detail){return disposeThingStage(event,detail);},error);
}
bool ScriptRuntime::advanceUnitDisposal(bool collectReady,std::string& error){
    if(collectReady)for(const auto id:actorWorld_.registrationOrder()){
        const auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->definition||actor->definition->kind!="Unit")continue;
        const std::string primary=actor->object?actor->object->animation:"";
        if(!actorWorld_.queueReadyUnitRetirement(id,primary,error))return false;
    }
    return actorWorld_.advanceUnitDestruction([this](const hades::ActorDisposalEvent& event,std::string& detail){return disposeThingStage(event,detail);},
        [this](int32_t id,uint64_t lifetime,hades::UnitDestructorStage stage,std::string& detail){
            auto* actor=actorWorld_.actor(id);if(!actor){detail="Unit destructor lost retained actual allocation";return false;}
            switch(stage){
            case hades::UnitDestructorStage::ClearGridRecord:return actorWorld_.clearObstacleGridRecord(id,detail);
            case hades::UnitDestructorStage::StatusReset:
                return selfEffects_.resetUnit(id,selfEffectProviders(),[](std::string& e){e="Retired traitor Unit requires native allegiance-reset consumer";return false;},detail);
            case hades::UnitDestructorStage::ArsenalDispose:
                return retireActorArsenal(id,lifetime,detail);
            }
            detail="Unknown genuine Unit destructor stage";return false;
        },error);
}
bool ScriptRuntime::retireActorArsenal(int32_t id,uint64_t lifetime,std::string& error){
    auto* actor=actorWorld_.actor(id);if(!actor){error="Retired arsenal lost actual allocation";return false;}
    if(!actor->arsenal){retainedArsenalOwners_.erase(lifetime);return true;}
    auto* arsenal=actor->arsenal.get();hades::WeaponRetirementProviders providers;
    providers.retained=[this,arsenal,id](const std::string& name,bool& retained,std::string&){retained=projectileWorld_.retainsWeapon(arsenal,name)||selfEffects_.retainsSourceWeapon(id,name);return true;};
    providers.stopImpactCue=[this](uint32_t channel,std::string& e){return audio_.stop(channel,false,e);};
    providers.dropFireFx=[](uint64_t,std::string& e){e="Retired weapon FireFx requires actual strong-reference release provider";return false;};
    bool released=false;if(!arsenal->retireOwner(providers,released,error))return false;
    if(released){actor->arsenal.reset();retainedArsenalOwners_.erase(lifetime);}
    else{if(retainedArsenalOwners_.size()>=4096&&!retainedArsenalOwners_.contains(lifetime)){error="Retained real weapon-data owner bound";return false;}retainedArsenalOwners_[lifetime]=id;}
    return true;
}
bool ScriptRuntime::collectRetiredArsenals(std::string& error){
    const auto pending=retainedArsenalOwners_;for(const auto& [lifetime,id]:pending)if(!retireActorArsenal(id,lifetime,error))return false;return true;
}
bool ScriptRuntime::disposeThingStage(const hades::ActorDisposalEvent& event,std::string& detail){
        using Stage=hades::ActorDisposalStage;auto* actor=actorWorld_.actor(event.id);
        if(!actor||!actor->object){detail="Actual disposal actor/body unavailable";return false;}
        switch(event.stage){
        case Stage::AttachedAnimationsPreDispose:{
            hades::EffectOwnerState owner;
            owner.lifetime=event.lifetime;owner.scale=actor->object->scale*actor->object->scaleFraction;owner.angle=actor->object->angle;
            owner.baseColor=actor->baseColorBytes;owner.baseHsv=std::array<float,4>{actor->hsv[0],actor->hsv[1],actor->hsv[2],0};
            if(!actorWorld_.location(event.id,owner.x,owner.y,detail)||!actorWorld_.zLocation(event.id,owner.z,detail))return false;
            owner.hasPrimaryAnimation=event.primaryAnimationLifetime||event.primaryEffectAnimation;owner.isUnit=bool(actor->arsenal);
            return actionEffects_.preDisposeOwner(event.id,owner,detail);
        }
        case Stage::OwnerDeathAnimations:
            // Attached action allocations were handled above. Other manager
            // allocations retain their own source disposal policy.
            for(const auto& [h,e]:effects_.entities())if(e.ownerLifetime==event.lifetime&&h!=event.primaryEffectAnimation&&e.parent==0){detail="Retiring nonprimary Effect animation requires its actual detach policy";return false;}
            return true;
        case Stage::DrawGridRemove:return actorWorld_.removeObstacleDrawGridRecord(event.id,detail);
        case Stage::LifeDispose:{
            const auto* graphic=actorWorld_.currentProperty(event.id,"Life.InvulnerableFx");
            if(actor->invulnerable&&graphic&&!graphic->text().empty()&&graphic->text()!="null"){detail="Life.Dispose requires owned invulnerability animation provider";return false;}
            // LifeComponent.Dispose2b7980 only cancels its optional owned
            // invulnerability image. Health remains the original Lua table.
            actor->hasLife=false;return true;
        }
        case Stage::ClearGridRecord:return actorWorld_.clearObstacleGridRecord(event.id,detail);
        case Stage::RemoveAutoLockableAndUseable:{
            // UseableManager removal of the Thing's Interact record (lua_interaction.cpp).
            return removeInteraction(event.id,detail);
        }
        case Stage::StopAmbientSound:
            return stopActorAmbientSound(event.id,detail);
        case Stage::AudioOwnerDeath:
            return audioOwnerDeath(event.id,detail);
        case Stage::ReleasePrimaryAnimation:{
            if(event.primaryAnimationLifetime&&!retirePrimaryAnimation(event.primaryAnimationLifetime,detail))return false;
            if(event.primaryEffectAnimation&&event.primaryEffectAnimation!=event.primaryAnimationLifetime&&!retirePrimaryAnimation(event.primaryEffectAnimation,detail))return false;
            return true;
        }
        case Stage::ClearAnimationCollections:
            // Action pre-dispose transferred surviving roots to standalone
            // ownership. No second blanket deletion of those real lifetimes.
            return true;
        case Stage::RemoveLight:return actorLights_.remove(event.id,detail);
        case Stage::ResetComponents:
            disposeTextOwner(state_,event.id);
            return true;
        case Stage::WorldOnDisposed:
            presentationBases_.erase(event.id);localMovement_.erase(event.lifetime);
            return lifeSystem_.removeActor(event.lifetime,detail);
        case Stage::ObstacleTail:return actorWorld_.removeOccluder(event.id,detail);
        case Stage::FreeRemainingComponents:
        case Stage::RetireManagerHandle:
        case Stage::RetireEntity:return true; // Typed ActorWorld owns these stores.
        }
        detail="Unknown genuine disposal stage";return false;
}
