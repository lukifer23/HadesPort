#pragma once
#include "unit_status.hpp"
#include <functional>
#include <memory>
#include <string>
namespace hades {
// Typed live fields of the actual EffectData record, after compiled defaults,
// deserialization and postread. Native/source storage stays with parent.
struct EffectAffectData {
    uint64_t dataHandle=0;
    float duration=0,elapsedTimeMultiplier=1,flashDuration=0,flashSpeed=0;
    uint32_t flashColor=0xffffffff;
    bool haltOnStart=false,destroyProjectileOnStart=false;
    bool scaleFxToProjectileRadius=false,disableAttack=false;
    std::string enableWeaponForDuration,startAnimation,startFx,frontFx,overheadFx,backFx,unitPropertyName,soundName;
};
struct EffectAffectOwner {
    int32_t id=-1;
    float elapsedTimeMultiplier=1,slowModifier=1;
    bool hasPhysics=false,hasAttachedAnimation=false,immuneToStun=false;
};
struct EffectAffectState {
    std::shared_ptr<UnitStatusEffect> status;
    float timeRemaining=0,appliedElapsedTimeMultiplier=1;
    uint64_t frontFx=0,overheadFx=0,backFx=0;
    int32_t soundId=0;
    bool hasSourceWeapon=false;
    // TagEffect factory copies Cooldown, independently of later data mutation.
    float tagCooldown=0;
    bool tagActive=false;
};
enum class EffectAffectOperation {
    HaltPhysics,DestroySourceProjectiles,EnableWeapon,
    RefreshAttachedAnimationTime,OnEffectApply,
    FlashColor,FlashDuration,StartAnimation,StartFx,
    CreateFrontFx,CreateOverheadFx,CreateBackFx,ScaleFrontBackFx,
    CaptureAndSetUnitProperty,OnStun,PlaySound
};
// Parent implements the named actual native operation. FX callbacks update
// handles only when a real animation is created/attached; PlaySound updates the
// actual returned soundId. OnEffectApply uses fresh effect_arguments DTO and
// original registrations. Missing callbacks fail at the actual native barrier.
using EffectAffectExecutor=std::function<bool(EffectAffectOperation,
    EffectAffectState&,EffectAffectOwner&,const EffectAffectData&,std::string&)>;
bool affectBaseEffect(EffectAffectState&,EffectAffectOwner&,
    const EffectAffectData&,const EffectAffectExecutor&,std::string&);
// TagEffect::AffectUnit first runs the complete base operation, then its captured
// Cooldown test/addition. This function does not implement later Tag Update.
bool affectTagEffect(EffectAffectState&,EffectAffectOwner&,
    const EffectAffectData&,const EffectAffectExecutor&,std::string&);
}
