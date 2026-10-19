#include "effect_update.hpp"
#include <cmath>
namespace hades {
namespace {
float mul(float a,float b){volatile float n=a*b;return n;}
float sub(float a,float b){volatile float n=a-b;return n;}
}
bool updateBaseEffect(EffectAffectState& effect,EffectUpdateFxState& fx,const EffectUpdateData& data,
    float elapsed,const EffectUpdateCallbacks& cb,bool& expired,std::string& error){
    expired=false;
    if(!effect.status||!cb.queryOwner||!std::isfinite(elapsed)||!std::isfinite(effect.timeRemaining)||!std::isfinite(data.duration)||!std::isfinite(data.expiringTimeThreshold)||!std::isfinite(data.expiringRapidFlashThreshold)){
        error="Effect Update requires finite actual data/state and native owner query";return false;
    }
    EffectUpdateOwner owner;
    if(!cb.queryOwner(effect.status->unitId,owner,error))return false;
    float localElapsed=elapsed;
    if(owner.exists){
        if(data.disableAttack||data.disableMove||data.disableRotate){
            if(!std::isfinite(owner.stunBreakModifier)){error="Native stun-break modifier is nonfinite";return false;}
            localElapsed=mul(localElapsed,owner.stunBreakModifier);
        }else if(data.type==7&&owner.charmBreakModifier!=1){
            if(!std::isfinite(owner.charmBreakModifier)){error="Native charm-break modifier is nonfinite";return false;}
            localElapsed=mul(localElapsed,owner.charmBreakModifier);
        }
    }
    effect.timeRemaining=sub(effect.timeRemaining,localElapsed);
    if(!std::isfinite(effect.timeRemaining)){error="Effect remaining duration is outside finite native subset";return false;}
    const bool noHandles=!effect.frontFx&&!effect.backFx&&!effect.overheadFx;
    // Bounded exact UpdateFx empty branch: cleared effects with no handles
    // return, or fresh nonflashing no-handle state with both thresholds <=0 and
    // both flash booleans false traverses no mutation/call branch.
    const bool emptyFx=noHandles&&(effect.status->cleared||
        (!fx.expireFlashRequested&&!fx.expireRapidFlashRequested&&data.expiringTimeThreshold<=0&&data.expiringRapidFlashThreshold<=0&&!data.flashFrontFxWhenExpiring&&!data.flashBackFxWhenExpiring));
    if(!emptyFx){
        if(!cb.updateFx){error="Effect Update requires actual nonempty UpdateFx executor";return false;}
        if(!cb.updateFx(effect,fx,error))return false;
    }
    bool end=false;
    if(effect.timeRemaining<0){
        if(!cb.hasRefreshOnExpireEffect){error="Effect expiration requires actual refresh-effect lookup";return false;}
        bool refresh=false;if(!cb.hasRefreshOnExpireEffect(refresh,error))return false;
        if(refresh)effect.timeRemaining=data.duration;else end=true;
    }
    // Source liveness check is skipped when already expired, matching the native
    // early branch directly to virtual Clear.
    if(!end&&data.clearOnSourceDeath&&effect.hasSourceWeapon){
        if(!cb.sourceWeaponOwnerAlive){error="Effect expiration requires actual source weapon owner liveness";return false;}
        bool alive=false;if(!cb.sourceWeaponOwnerAlive(alive,error))return false;end=!alive;
    }
    if(end){
        if(!effect.status->cleared){
            if(!cb.clearVirtual){error="Effect expiration requires actual virtual Clear";return false;}
            if(!cb.clearVirtual(effect,error))return false;
        }
        expired=true;
    }
    error.clear();return true;
}
}
