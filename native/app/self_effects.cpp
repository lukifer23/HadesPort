#include "self_effects.hpp"
#include <cmath>
namespace hades {
bool admitWeaponSelfEffects(float chance,int32_t trigger,
    std::span<const std::shared_ptr<WeaponSelfEffect>> effects,UnitStatus& status,
    const SelfEffectCallbacks& cb,SelfEffectAdmissionResult& result,std::string& error){
    result={};
    if(!std::isfinite(chance)||!cb.randomStream0){error="Self-effect admission requires finite native chance and stream0 RNG";return false;}
    auto draw=[&](double& value){
        if(!cb.randomStream0(value,error))return false;
        ++result.randomDraws;
        if(!std::isfinite(value)||value<0||value>1){error="Native self-effect RNG returned a value outside [0,1]";return false;}
        return true;
    };
    double value=0;
    if(!draw(value))return false;
    if(value>double(chance)){error.clear();return true;}
    result.weaponChancePassed=true;
    for(const auto& data:effects){
        if(!data){error="Self-effect vector contains no actual EffectData record";return false;}
        if(!data->active||data->trigger!=trigger)continue;
        if(!std::isfinite(data->procChance)){error="Self-effect ProcChance is nonfinite";return false;}
        if(!draw(value))return false;
        if(value>double(data->procChance))continue;
        if(data->requirePerfectCharge){
            bool perfect=false;
            if(!cb.isPerfectCharge){error="Self-effect requires actual perfect-charge query";return false;}
            if(!cb.isPerfectCharge(perfect,error))return false;
            if(!perfect)continue;
        }
        if(data->name.find('\0')!=std::string::npos||!cb.findEffect){error="Self-effect requires exact native name lookup";return false;}
        std::shared_ptr<UnitStatusEffect> effect;
        if(!cb.findEffect(data->name,effect,error))return false;
        if(effect){
            if(!cb.reapply){error="Existing self-effect requires real virtual Reapply";return false;}
            if(!cb.reapply(*data,*effect,-1.0f,error))return false;
            ++result.reapplied;
        }else{
            if(!cb.createEffect||!cb.affectUnit){error="New self-effect requires actual EffectFactory and AffectUnit";return false;}
            if(!cb.createEffect(*data,0.0f,effect,error))return false;
            if(!effect){error="EffectFactory returned no actual effect";return false;}
            effect->selfApplied=true;
            if(!cb.affectUnit(*data,*effect,error))return false;
            if(!status.trackAdmittedEffect(effect,error))return false;
            ++result.created;
        }
    }
    error.clear();return true;
}
}
