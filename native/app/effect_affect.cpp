#include "effect_affect.hpp"
#include <cmath>
namespace hades {
namespace {
float nativeAdd(float a,float b){volatile float n=a+b;return n;}
float nativeSub(float a,float b){volatile float n=a-b;return n;}
float nativeMul(float a,float b){volatile float n=a*b;return n;}
}
bool affectBaseEffect(EffectAffectState& effect,EffectAffectOwner& owner,
    const EffectAffectData& data,const EffectAffectExecutor& execute,std::string& error){
    if(!effect.status||owner.id<0||!std::isfinite(data.duration)||!std::isfinite(data.elapsedTimeMultiplier)||!std::isfinite(owner.elapsedTimeMultiplier)||!std::isfinite(owner.slowModifier)||!std::isfinite(data.flashDuration)||!std::isfinite(data.flashSpeed)){
        error="Effect::AffectUnit requires actual typed effect, owner and finite native fields";return false;
    }
    auto run=[&](EffectAffectOperation operation){
        if(!execute){error="Effect::AffectUnit requires original side-effect/callback executor";return false;}
        return execute(operation,effect,owner,data,error);
    };
    effect.status->unitId=owner.id;
    effect.timeRemaining=data.duration;
    if(data.haltOnStart&&owner.hasPhysics&&!run(EffectAffectOperation::HaltPhysics))return false;
    if(data.destroyProjectileOnStart&&!run(EffectAffectOperation::DestroySourceProjectiles))return false;
    if(!data.enableWeaponForDuration.empty()&&!run(EffectAffectOperation::EnableWeapon))return false;
    if(data.elapsedTimeMultiplier!=1){
        float applied=data.elapsedTimeMultiplier;
        effect.appliedElapsedTimeMultiplier=applied;
        if(owner.slowModifier!=1&&data.elapsedTimeMultiplier<1){
            applied=nativeSub(1,nativeMul(nativeSub(1,applied),owner.slowModifier));
            effect.appliedElapsedTimeMultiplier=applied;
        }
        owner.elapsedTimeMultiplier=nativeMul(applied,owner.elapsedTimeMultiplier);
        if(!std::isfinite(applied)||!std::isfinite(owner.elapsedTimeMultiplier)){error="Effect elapsed multiplier result is outside finite native subset";return false;}
        if(owner.hasAttachedAnimation&&!run(EffectAffectOperation::RefreshAttachedAnimationTime))return false;
    }
    if(!run(EffectAffectOperation::OnEffectApply))return false;
    // Original callback can mutate actual data and/or clear this effect. Only
    // the three persistent FX admissions consult mCleared at this point.
    if(data.flashColor!=0xffffffff&&!run(EffectAffectOperation::FlashColor))return false;
    if(data.flashDuration>0&&!run(EffectAffectOperation::FlashDuration))return false;
    if(!data.startAnimation.empty()&&!run(EffectAffectOperation::StartAnimation))return false;
    if(!data.startFx.empty()&&data.startFx!="null"&&!run(EffectAffectOperation::StartFx))return false;
    if(!effect.status->cleared){
        if(!effect.overheadFx&&!data.overheadFx.empty()&&!run(EffectAffectOperation::CreateOverheadFx))return false;
        if(!effect.backFx&&!data.backFx.empty()&&!run(EffectAffectOperation::CreateBackFx))return false;
        if(!effect.frontFx&&!data.frontFx.empty()&&!run(EffectAffectOperation::CreateFrontFx))return false;
    }
    if(data.scaleFxToProjectileRadius&&effect.hasSourceWeapon&&(effect.frontFx||effect.backFx)&&!run(EffectAffectOperation::ScaleFrontBackFx))return false;
    if(!data.unitPropertyName.empty()&&!run(EffectAffectOperation::CaptureAndSetUnitProperty))return false;
    if(data.disableAttack&&!owner.immuneToStun&&data.duration>0&&!run(EffectAffectOperation::OnStun))return false;
    // Both AudioManager::Play3D and Play2D return zero for an empty cue name,
    // before event lookup/creation. Nonempty invalid cues still need actual
    // admission; their behavior is not inferred from mValid here.
    if(data.soundName.empty())effect.soundId=0;
    else if(!run(EffectAffectOperation::PlaySound))return false;
    error.clear();return true;
}
bool affectTagEffect(EffectAffectState& effect,EffectAffectOwner& owner,
    const EffectAffectData& data,const EffectAffectExecutor& execute,std::string& error){
    if(!std::isfinite(effect.tagCooldown)){error="TagEffect captured Cooldown is nonfinite";return false;}
    if(!affectBaseEffect(effect,owner,data,execute,error))return false;
    if(effect.tagCooldown==0)effect.tagActive=true;
    else effect.timeRemaining=nativeAdd(effect.timeRemaining,effect.tagCooldown);
    if(!std::isfinite(effect.timeRemaining)){error="TagEffect remaining duration is outside finite native subset";return false;}
    error.clear();return true;
}
}
