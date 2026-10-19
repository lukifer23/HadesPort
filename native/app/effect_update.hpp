#pragma once
#include "effect_affect.hpp"
#include <functional>
namespace hades {
struct EffectUpdateData {
    int32_t type=12;
    float duration=0,expiringTimeThreshold=0,expiringRapidFlashThreshold=0;
    bool disableMove=false,disableRotate=false,disableAttack=false;
    bool clearOnSourceDeath=false,flashFrontFxWhenExpiring=false,flashBackFxWhenExpiring=false;
};
struct EffectUpdateOwner {
    bool exists=false;
    float stunBreakModifier=1,charmBreakModifier=1;
};
struct EffectUpdateFxState {
    bool expireFlashRequested=false,expireRapidFlashRequested=false;
};
struct EffectUpdateCallbacks {
    std::function<bool(int32_t,EffectUpdateOwner&,std::string&)> queryOwner;
    std::function<bool(EffectAffectState&,EffectUpdateFxState&,std::string&)> updateFx;
    // Original HasRefreshOnExpireEffect consults actual hashed effect IDs on
    // this owner. Empty strings are not used to fabricate a lookup result.
    std::function<bool(bool&,std::string&)> hasRefreshOnExpireEffect;
    std::function<bool(bool&,std::string&)> sourceWeaponOwnerAlive;
    std::function<bool(EffectAffectState&,std::string&)> clearVirtual;
};
// Native Effect::Update0x5d220. expired is the original true return value;
// parent queues it in UnitStatus.toDelete and performs actual cleanup later.
bool updateBaseEffect(EffectAffectState&,EffectUpdateFxState&,const EffectUpdateData&,
    float elapsed,const EffectUpdateCallbacks&,bool& expired,std::string&);
}
