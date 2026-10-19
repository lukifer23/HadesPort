#pragma once
#include "unit_status.hpp"
#include <functional>
#include <memory>
#include <span>
#include <string>
namespace hades {
// Admission fields projected from actual mutable WeaponData::mEffects records.
// Parent owns the complete native/source record keyed by dataHandle.
struct WeaponSelfEffect {
    uint64_t dataHandle=0;
    std::string name;
    int32_t trigger=0;
    float procChance=1;
    bool active=true,requirePerfectCharge=false;
};
struct SelfEffectCallbacks {
    // Native TLS Random::RandomDouble stream0. Required even for an empty list.
    std::function<bool(double&,std::string&)> randomStream0;
    std::function<bool(bool&,std::string&)> isPerfectCharge;
    // Native UnitStatus::GetEffect lookup, including its real pending/active order.
    std::function<bool(const std::string&,std::shared_ptr<UnitStatusEffect>&,std::string&)> findEffect;
    // Native EffectFactory(data, sourceWeapon, 0.0f), before SelfApplied=true.
    std::function<bool(const WeaponSelfEffect&,float,std::shared_ptr<UnitStatusEffect>&,std::string&)> createEffect;
    // Real virtual AffectUnit barrier; must implement original side effects and
    // original OnEffectApply before returning. No pending registration yet.
    std::function<bool(const WeaponSelfEffect&,UnitStatusEffect&,std::string&)> affectUnit;
    // Real virtual Reapply(data,sourceWeapon,&duration=-1). Existing SelfApplied
    // is preserved. This is not just resetting a generic expiration timer.
    std::function<bool(const WeaponSelfEffect&,UnitStatusEffect&,float,std::string&)> reapply;
};
struct SelfEffectAdmissionResult {
    uint32_t randomDraws=0,created=0,reapplied=0;
    bool weaponChancePassed=false;
};
// Matches Weapon::ApplySelfEffects0x86930 only. General AddEffect immunities,
// duration gates and block flags do not apply to this native self-effect path.
// Keep the span's backing storage stable through callbacks; mutable pointed-to
// records remain live, so an earlier original callback can change a later item.
// Callback failure is an explicit incomplete transaction: earlier native effects
// stay applied. Caller must stop progression rather than retry/draw RNG again.
bool admitWeaponSelfEffects(float weaponEffectsProcChance,int32_t requestedTrigger,
    std::span<const std::shared_ptr<WeaponSelfEffect>> effects,UnitStatus&,
    const SelfEffectCallbacks&,SelfEffectAdmissionResult&,std::string&);
}
