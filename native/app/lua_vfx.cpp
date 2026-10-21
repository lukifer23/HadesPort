#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include "physics_runtime.hpp"
#include <cmath>
#include <stdexcept>

// Particles plus action FX (registered as the particle world's external
// owner) must own every CancelOverCap member of the permanent manager.
bool ScriptRuntime::permanentVfxCovered()const{return particles_.permanentRegistryCovered();}
bool ScriptRuntime::advanceVfx(float dt,std::string& error){
 try{
    if(!state_||!std::isfinite(dt)||dt<0||dt>1){error="Native VFX clock/provider unavailable or outside bound";return false;}
    const auto registry=effects_.registry();
    if(!actionEffects_.faultReason().empty()){error=actionEffects_.faultReason();return false;}
    if(!registry||!registry->allAllocationsInitialized()||!permanentVfxCovered()){
        error="Actual initialized shared VFX allocation/ordered-manager coverage incomplete";return false;
    }
    const hades::EffectOwnerProvider owners=[this](int32_t id,hades::EffectOwnerState& owner,std::string& detail){return effectOwner(id,owner,detail);};
    const hades::EffectMarkerProvider markers=[this](int32_t id,const std::string& name,std::array<float,3>& location,std::string& detail){return effectMarker(id,name,location,detail);};
    const hades::EffectAnimationRandom random=[this](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value))return true;detail="Actual source VFX uniform RNG unavailable";return false;};
    const hades::EffectBoundedRandom integers=[this](uint32_t bound,uint32_t& value,std::string& detail){if(nextRecoveredEngineBoundedRandom(state_,bound,value))return true;detail="Actual source VFX bounded PCG unavailable";return false;};
    // Every currently executed Create/Add path uses this actual shared registry.
    // This does not register unknown map graphics or count scene draw sprites as
    // animation allocations. Their typed primary reconstruction is separate.
    for(const auto& [id,e]:effects_.entities()){
        const auto* life=registry->find(id);if(!life||!life->initialized){error="Actual VFX source lifetime unavailable";return false;}
        if(life->skipUpdate||life->canceled||e.done)continue;
        const auto* actor=actorWorld_.actor(e.ownerId);
        if(!actor||!actor->registered||actor->entityHandle!=e.ownerLifetime){error="Actual VFX owner lifetime unavailable before world mutation";return false;}
    }
    // TimeModifierSystem 0x180188ae0 per animation entity: its own source
    // TimeModifierFraction (InitData 0x1802927a1 adds the component only for
    // authored values >=0) divides by undo+(1-undo)*fraction; Thing+0x80
    // ElapsedTimeMultiplier applies only through the AttachedToTimeMultiplier
    // link (Attach 0x180299fd3, UseAttachedTimeMultiplier). The owner's own
    // Thing TimeModifierFraction is not an animation input (same adapter as
    // action FX and HAA primaries).
    effects_.setElapsedProvider([this,registry](const hades::EffectAnimationEntity& e,float world,float& result,std::string& detail){
        const auto* life=registry->find(e.handle);if(!life||!life->initialized){detail="Actual VFX modified-time lifetime unavailable";return false;}
        const auto* own=life->sourceDefinition.get("TimeModifierFraction");const float fraction=own&&own->number()>=0?float(own->number()):1.f;
        float multiplier=1;
        if(life->attachedToTimeMultiplier){const auto* actor=actorWorld_.actor(e.ownerId);if(!actor||!actor->registered||actor->entityHandle!=e.ownerLifetime){detail="Actual VFX time-multiplier owner unavailable";return false;}multiplier=actor->timeMultiplier;}
        return hades::physicsComponentElapsed(world,worldSpeedModifier_,fraction,multiplier,result,detail);});
    if(!particles_.refreshOwners(owners,error)||!scenePrimaries_.advance(dt,error)||!actionEffects_.advance(dt,actionEffectProviders(),error))return false;
    // Thing.UpdateAnimation2df5c0: after actual PostUpdate returns done,
    // ChainTo gets SetAnimation; otherwise restore ThingData.Graphic or clear
    // the primary. Snapshot handles because SetAnimation disposes old state.
    for(const auto handle:scenePrimaries_.completedPrimaries()){
        const auto* source=scenePrimaries_.state(handle);if(!source)continue;
        const auto old=*source;
        if(old.parent){error="Original scene child completion needs OnChildFinished: "+old.name;return false;}
        auto* actor=actorWorld_.actor(old.ownerId);
        if(!actor||!actor->registered||actor->entityHandle!=old.ownerLifetime||actor->primaryAnimationLifetime!=handle){error="Source scene completion owner lifetime mismatch: "+old.name;return false;}
        const auto* chain=old.definition.get("ChainTo");std::string next=chain?chain->text():"";
        if(next.empty()||next=="null"){const auto* graphic=actorWorld_.currentProperty(old.ownerId,"Thing.Graphic");if(!graphic){error="Source scene completion fallback Graphic unavailable";return false;}next=graphic->text();}
        if(!next.empty()&&next!="null"){
            if(!setActorAnimation(old.ownerId,next,error))return false;
        }else{
            if(!scenePrimaries_.destroy(handle,error))return false;
            actor->primaryAnimationLifetime=0;actor->object->animation.clear();actor->object->graphicsDirty=true;
        }
    }
    // The action bank is another resource backend for the same native Thing
    // primary, not a different manager ownership policy. Retain old state until
    // SetAnimation can transfer it, including the actual owner/lifetime links.
    for(const auto handle:actionEffects_.completedPrimaries()){
        const auto* source=actionEffects_.state(handle);if(!source)continue;
        const auto old=*source;
        if(old.ownerId<0)continue; // projectile Thing.UpdateAnimation below
        auto* actor=actorWorld_.actor(old.ownerId);
        if(!actor||!actor->registered||actor->entityHandle!=old.ownerLifetime||actor->primaryAnimationLifetime!=handle){error="Action primary completion owner lifetime mismatch: "+old.name;return false;}
        const auto* chain=old.preventChain?nullptr:old.definition.get("ChainTo");std::string next=chain?chain->text():"";
        if(next.empty()||next=="null"){const auto* graphic=actorWorld_.currentProperty(old.ownerId,"Thing.Graphic");if(!graphic){error="Action primary completion fallback Graphic unavailable";return false;}next=graphic->text();}
        if(!next.empty()&&next!="null"){
            if(actionEffects_.admits(next)){
                if(!setActionPrimaryAnimation(old.ownerId,next,error,true))return false;
            }else if(animationBank_->find(next)){
                // The HAA backend currently has immutable per-clip animation
                // Body state. Identity transfers are exact: location is the
                // same attached Thing and all transferred fields remain their
                // native defaults. Never silently drop nondefault live state.
                const auto flag=[&](const char*k){const auto*v=old.definition.get(k);return v&&v->boolean();};
                if(old.ownScale!=1||old.scaleVector!=std::array<float,2>{1,1}||old.rotationComponent||old.ownZ!=0||old.addedOffsetZ!=0||old.scaleRadius!=0||old.playSpeedMultiplier!=1||old.flipX||old.flipY||old.randomX!=0||old.randomY!=0||(old.attachOffset&&*old.attachOffset!=std::array<float,2>{0,0})||old.parallax!=1||old.runtimeGroup||flag("TransferChildren")||flag("TransferAngle")||flag("TransferFrameFromThis")){
                    error="Action primary to HAA requires nondefault Animation.CopyState fields: "+old.name+" -> "+next;return false;
                }
                if(!setActorAnimation(old.ownerId,next,error))return false;
            }else{error="Action primary completion across resource backends requires CopyState: "+old.name+" -> "+next;return false;}
        }else{
            if(!actionEffects_.destroy(handle,error))return false;
            actor->primaryAnimationLifetime=0;actor->object->animation.clear();actor->object->graphicsDirty=true;
        }
    }
    // Defined sequential Android schedule. Windows submits overlapping worker
    // batches, so no completion-order equivalence is asserted. Each component
    // equation and explicit Time-before-Distance VFX ordering is source-backed.
    for(auto phase:{hades::EffectParticlePhase::TimeRemaining,hades::EffectParticlePhase::Rotation,
        hades::EffectParticlePhase::Acceleration,hades::EffectParticlePhase::Gravity,
        hades::EffectParticlePhase::Fraction,hades::EffectParticlePhase::Heading,
        hades::EffectParticlePhase::UpwardVelocity,hades::EffectParticlePhase::PlaySpeed,
        hades::EffectParticlePhase::Translation,hades::EffectParticlePhase::BookUpdateInternal,
        hades::EffectParticlePhase::SlideUpdateInternal,hades::EffectParticlePhase::SlidePostUpdate,
        hades::EffectParticlePhase::ConstantUpdateInternal}){
        if(!particles_.phase(phase,dt,integers,error))return false;
    }
    if(!particles_.syncSpawners(error))return false;
    particles_.disposeDone();
    std::vector<hades::EffectPrimaryReplacement> replacements;
    if(!effects_.advancePrimaryDurations(dt,owners,random,replacements,error))return false;
    for(const auto& replacement:replacements){
        auto* actor=actorWorld_.actor(replacement.ownerId);
        if(!actor||!actor->object||actor->primaryEffectAnimation!=replacement.previous){error="Actual primary duration replacement owner mismatch";return false;}
        actor->primaryEffectAnimation=replacement.current;actor->object->animation=replacement.name;
    }
    const auto admissionFor=[&](const hades::EffectSpawnRequest& request,hades::EffectParticleAdmission& admission,std::string& detail){
        const auto* actor=actorWorld_.actor(request.ownerId);
        if(!actor||!actor->registered){detail="Actual VFX emission owner unavailable";return false;}
        admission.nativeManagerAdmissionResolved=true;
        admission.completePermanentRegistry=permanentVfxCovered();
        admission.permanentVfxCap=hades::EffectParticleWorld::CompiledPermanentVfxCap;
        admission.ownerTimeModifierFraction=actor->timeModifierFraction;
        admission.componentWorld=&effects_;
        if(const auto p=particles_.particles().find(request.source);p!=particles_.particles().end()){
            admission.sourceModifiedTime=p->second.modifiedTime;
            admission.sourceTimeModifierFraction=p->second.timeModifierFraction;
        }
        admission.audio.start=[this](const std::string& cue,int32_t owner,const std::array<float,3>& position,uint32_t& id,std::string& e){
            // Concrete Book InitData uses PlayCue3DWithCap, compiled cap0;
            // Random5's earlier2D PlayCue is bypassed for EnvironmentBat.
            if(worldFrame_<2){id=0;return true;}
            return playAudioCue(cue,owner,position,id,e);
        };
        admission.audio.stop=[this](uint32_t id,float fade,std::string& e){return fade>0?audio_.setVolume(id,0,fade,true,false,e):audio_.stop(id,false,e);};
        return true;
    };
    hades::EffectEmissionProvider emit;
    emit.alive=[this](uint64_t id){return particles_.alive(id);};
    emit.prepare=[&](const hades::EffectSpawnRequest& request,std::string& detail){
        const auto* source=effects_.entity(request.source);hades::EffectOwnerState owner;hades::EffectParticleAdmission admission;
        if(!source||!owners(request.ownerId,owner,detail)||!admissionFor(request,admission,detail)){if(detail.empty())detail="Actual VFX source component unavailable";return false;}
        return particles_.prepare(request,*source,owner,admission,random,integers,detail);
    };
    emit.create=[&](const hades::EffectSpawnRequest& request,uint64_t& id,std::string& detail){
        const auto* source=effects_.entity(request.source);hades::EffectOwnerState owner;hades::EffectParticleAdmission admission;
        if(!source||!owners(request.ownerId,owner,detail)||!admissionFor(request,admission,detail))return false;
        return particles_.create(request,*source,owner,admission,random,integers,id,detail);
    };
    hades::EffectUpdateContext context;context.countsResolved=true;context.durationSystemResolved=true;
    context.managerVisualFxCount=uint32_t(registry->managerCount(0));context.managerSpawnedThisFrame=0;
    context.phase=hades::EffectUpdatePhase::Time;
    if(!effects_.update(dt,owners,markers,random,emit,context,error))return false;
    if(!actionEffects_.updateTime(dt,actionEffectProviders(),context.managerSpawnedThisFrame,error))return false;
    context.managerVisualFxCount=uint32_t(registry->managerCount(0));
    context.phase=hades::EffectUpdatePhase::Distance;
    if(!effects_.update(dt,owners,markers,random,emit,context,error))return false;
    if(!actionEffects_.updateDistance(actionEffectProviders(),context.managerSpawnedThisFrame,error))return false;
    return true;
 }catch(const std::exception& e){error=std::string("Native VFX phase failed: ")+e.what();return false;}
}
