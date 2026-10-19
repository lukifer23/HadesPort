#include "speed_effects.hpp"
#include <cmath>
namespace hades {
namespace {
float add(float a,float b){volatile float n=a+b;return n;}
float sub(float a,float b){volatile float n=a-b;return n;}
float mul(float a,float b){volatile float n=a*b;return n;}
float div(float a,float b){volatile float n=a/b;return n;}
bool finiteOwner(const SpeedEffectOwner& owner){return std::isfinite(owner.speedModifier)&&std::isfinite(owner.speedAddition);}
bool base(const SpeedEffectBaseCallback& cb,SpeedEffectState& effect,std::string& error){
    if(!cb){error="SpeedEffect requires actual base Effect callback";return false;}
    return cb(effect,error);
}
}
bool affectSpeedEffect(SpeedEffectState& effect,SpeedEffectOwner& owner,const SpeedEffectData& data,
    const EffectAffectExecutor& executor,const SpeedEffectOwnerLookup& lookup,std::string& error){
    if(!std::isfinite(data.modifier)||!finiteOwner(owner)||!lookup){error="SpeedEffect requires finite actual data/owner and native owner lookup";return false;}
    effect.modifier=data.modifier;
    if(data.modifier>0&&data.modifier<1&&data.changeType==1&&owner.slowModifier!=1)
        effect.modifier=sub(1,mul(sub(1,data.modifier),owner.slowModifier));
    if(!std::isfinite(effect.modifier)){error="SpeedEffect modifier is outside finite native subset";return false;}
    if(!affectBaseEffect(effect,owner,data,executor,error))return false;
    SpeedEffectOwner* actual=lookup(effect.status->unitId);
    if(!actual||!finiteOwner(*actual)){error="SpeedEffect owner disappeared or became nonfinite after original AffectUnit barrier";return false;}
    if(data.changeType==1)actual->speedModifier=mul(actual->speedModifier,effect.modifier);
    else if(data.changeType==0){actual->speedAddition=add(actual->speedAddition,data.modifier);effect.appliedModifier=data.modifier;}
    if(!finiteOwner(*actual)){error="SpeedEffect applied speed is outside finite native subset";return false;}
    error.clear();return true;
}
bool clearSpeedEffect(SpeedEffectState& effect,const SpeedEffectData& data,
    const SpeedEffectOwnerLookup& lookup,const SpeedEffectBaseCallback& actualBaseClear,std::string& error){
    if(!effect.status||!lookup){error="SpeedEffect Clear requires actual status and owner lookup";return false;}
    SpeedEffectOwner* owner=lookup(effect.status->unitId);
    const int32_t count=int32_t(uint32_t(effect.numStacks)+uint32_t(1));
    if(owner&&count>0){
        if(!finiteOwner(*owner)||!std::isfinite(effect.modifier)||!std::isfinite(effect.appliedModifier)){error="SpeedEffect Clear contains nonfinite native state";return false;}
        if(data.changeType==1){
            const float reciprocal=div(1,effect.modifier);
            if(!std::isfinite(reciprocal)){error="SpeedEffect zero modifier reciprocal is outside finite native subset";return false;}
            for(int32_t i=0;i<count;++i)owner->speedModifier=mul(owner->speedModifier,reciprocal);
        }else if(data.changeType==0){for(int32_t i=0;i<count;++i)owner->speedAddition=add(owner->speedAddition,-effect.appliedModifier);}
        if(!finiteOwner(*owner)){error="SpeedEffect Clear speed is outside finite native subset";return false;}
    }
    return base(actualBaseClear,effect,error);
}
bool reapplySpeedEffect(SpeedEffectState& effect,const SpeedEffectData& data,
    const SpeedEffectOwnerLookup& lookup,const SpeedEffectBaseCallback& actualBaseReapply,
    const std::function<bool(int32_t&,std::string&)>& maxStacks,std::string& error){
    if(!effect.status||!lookup){error="SpeedEffect Reapply requires actual status and owner lookup";return false;}
    if(!base(actualBaseReapply,effect,error))return false;
    SpeedEffectOwner* owner=lookup(effect.status->unitId);
    if(!owner||!finiteOwner(*owner)){error="SpeedEffect owner disappeared or became nonfinite after Reapply barrier";return false;}
    if(!data.stacks||(data.changeType!=0&&data.changeType!=1)){error.clear();return true;}
    if(data.maxStacks>0){
        if(!maxStacks){error="SpeedEffect stack cap requires actual Effect::GetMaxStacks";return false;}
        int32_t cap=0;if(!maxStacks(cap,error))return false;if(effect.numStacks>=cap){error.clear();return true;}
    }
    if(data.changeType==1)owner->speedModifier=mul(owner->speedModifier,effect.modifier);
    else owner->speedAddition=add(owner->speedAddition,data.modifier);
    effect.numStacks=int32_t(uint32_t(effect.numStacks)+uint32_t(1));
    if(!finiteOwner(*owner)){error="SpeedEffect stacked speed is outside finite native subset";return false;}
    error.clear();return true;
}
bool updateSpeedEffect(SpeedEffectState& effect,const SpeedEffectData& data,float elapsed,
    const SpeedEffectOwnerLookup& lookup,const SpeedEffectBaseCallback& actualBaseUpdate,std::string& error){
    if(!effect.status||!lookup||!std::isfinite(elapsed)||!std::isfinite(data.expiringModifierFalloff)||!std::isfinite(data.expiringTimeThreshold)||!std::isfinite(effect.timeRemaining)){error="SpeedEffect Update requires finite native state and actual owner lookup";return false;}
    SpeedEffectOwner* owner=lookup(effect.status->unitId);
    if(data.expiringModifierFalloff>0&&data.expiringTimeThreshold>effect.timeRemaining&&owner){
        const float delta=mul(data.expiringModifierFalloff,elapsed);
        owner->speedAddition=sub(owner->speedAddition,delta);effect.appliedModifier=sub(effect.appliedModifier,delta);
        if(!finiteOwner(*owner)||!std::isfinite(effect.appliedModifier)){error="SpeedEffect falloff speed is outside finite native subset";return false;}
    }
    return base(actualBaseUpdate,effect,error);
}
}
