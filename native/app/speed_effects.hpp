#pragma once
#include "effect_affect.hpp"
#include <functional>
namespace hades {
struct SpeedEffectData : EffectAffectData {
    float modifier=0,expiringModifierFalloff=0,expiringTimeThreshold=0;
    int32_t changeType=1,maxStacks=0;
    bool stacks=false;
};
struct SpeedEffectOwner : EffectAffectOwner {
    float speedModifier=1,speedAddition=0;
};
struct SpeedEffectState : EffectAffectState {
    float modifier=0,appliedModifier=0;
    int32_t numStacks=0;
};
using SpeedEffectOwnerLookup=std::function<SpeedEffectOwner*(int32_t)>;
using SpeedEffectBaseCallback=std::function<bool(SpeedEffectState&,std::string&)>;
// Native callbacks can change actual owner/effect data. Lookup is deliberately
// repeated after base operations, matching UnitManager::Get calls in original.
bool affectSpeedEffect(SpeedEffectState&,SpeedEffectOwner&,const SpeedEffectData&,
    const EffectAffectExecutor&,const SpeedEffectOwnerLookup&,std::string&);
bool clearSpeedEffect(SpeedEffectState&,const SpeedEffectData&,
    const SpeedEffectOwnerLookup&,const SpeedEffectBaseCallback& actualBaseClear,std::string&);
// Noarg virtual Reapply slot28. The data/source/duration overload slot20 must
// apply its own native ExtendDurationOnReapply logic before invoking this.
bool reapplySpeedEffect(SpeedEffectState&,const SpeedEffectData&,
    const SpeedEffectOwnerLookup&,const SpeedEffectBaseCallback& actualBaseReapply,
    const std::function<bool(int32_t&,std::string&)>& actualGetMaxStacks,std::string&);
// Speed falloff prelude; actualBaseUpdate performs native lifetime/FX/clear logic
// and reports expiry through its parent-owned transaction. It is mandatory.
bool updateSpeedEffect(SpeedEffectState&,const SpeedEffectData&,float elapsed,
    const SpeedEffectOwnerLookup&,const SpeedEffectBaseCallback& actualBaseUpdate,std::string&);
}
