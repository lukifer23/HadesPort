#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include <cmath>
#include <stdexcept>

// Particles plus action FX (registered as the particle world's external
// owner) must own every CancelOverCap member of the permanent manager.
bool ScriptRuntime::permanentVfxCovered()const{return particles_.permanentRegistryCovered();}
bool ScriptRuntime::advanceVfx(float dt,std::string& error){
 try{
    if(!state_||!std::isfinite(dt)||dt<0||dt>1){error="Native VFX clock/provider unavailable or outside bound";return false;}
    const auto registry=effects_.registry();
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
        if(actor->timeModifierFraction!=1||actor->timeMultiplier!=1){error="Non-default source owner VFX time components require native per-entity adapter";return false;}
    }
    if(!particles_.refreshOwners(owners,error)||!scenePrimaries_.advance(dt,error)||!actionEffects_.advance(dt,actionEffectProviders(),error))return false;
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
    return true;
 }catch(const std::exception& e){error=std::string("Native VFX phase failed: ")+e.what();return false;}
}
