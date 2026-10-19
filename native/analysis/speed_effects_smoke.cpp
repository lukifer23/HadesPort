#include "../app/speed_effects.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace hades;
static auto state(){SpeedEffectState e;e.status=std::make_shared<UnitStatusEffect>();e.status->handle=1;return e;}
int main(){
    SpeedEffectOwner owner;owner.id=40000;owner.slowModifier=0.5f;
    SpeedEffectData data;data.modifier=0.35f;data.duration=0.25f;
    SpeedEffectState effect=state();std::string error;bool applied=false,cleared=false;
    auto lookup=[&](int32_t id){return id==owner.id?&owner:nullptr;};
    auto execute=[&](EffectAffectOperation op,EffectAffectState& e,EffectAffectOwner&,const EffectAffectData&,std::string&){
        assert(op==EffectAffectOperation::OnEffectApply);assert(owner.speedModifier==1);assert(e.timeRemaining==0.25f);applied=true;return true;};
    assert(affectSpeedEffect(effect,owner,data,execute,lookup,error));assert(applied&&owner.speedModifier==effect.modifier);assert(std::fabs(effect.modifier-0.675f)<0.000001f);
    auto baseClear=[&](SpeedEffectState& e,std::string&){assert(std::fabs(owner.speedModifier-1)<0.000001f);e.status->cleared=true;cleared=true;return true;};
    assert(clearSpeedEffect(effect,data,lookup,baseClear,error));assert(cleared);
    // Native base callback is before derived effect mutation, including reapply.
    effect=state();owner.speedModifier=1;owner.slowModifier=1;assert(affectSpeedEffect(effect,owner,data,execute,lookup,error));
    data.stacks=true;data.maxStacks=1;
    bool capQueried=false;auto cap=[&](int32_t& n,std::string&){capQueried=true;n=1;return true;};
    auto reapply=[&](SpeedEffectState& e,std::string&){assert(e.numStacks==0&&owner.speedModifier==0.35f);return true;};
    assert(reapplySpeedEffect(effect,data,lookup,reapply,cap,error));assert(capQueried&&effect.numStacks==1);assert(std::fabs(owner.speedModifier-0.1225f)<0.000001f);
    auto noMutation=[](SpeedEffectState&,std::string&){return true;};assert(reapplySpeedEffect(effect,data,lookup,noMutation,cap,error));assert(effect.numStacks==1);
    assert(clearSpeedEffect(effect,data,lookup,baseClear,error));
    // ADD falloff is performed before base lifetime/FX/expiry processing.
    effect=state();owner.speedAddition=0;data={};data.changeType=0;data.modifier=20;data.duration=1;
    assert(affectSpeedEffect(effect,owner,data,[](auto,auto&,auto&,const auto&,std::string&){return true;},lookup,error));assert(owner.speedAddition==20&&effect.appliedModifier==20);
    effect.timeRemaining=0.2f;data.expiringTimeThreshold=0.3f;data.expiringModifierFalloff=10;
    assert(updateSpeedEffect(effect,data,0.1f,lookup,[&](SpeedEffectState& e,std::string&){assert(owner.speedAddition==19&&e.appliedModifier==19);return true;},error));
    assert(clearSpeedEffect(effect,data,lookup,[&](SpeedEffectState&,std::string&){assert(owner.speedAddition==0);return true;},error));
    assert(!updateSpeedEffect(effect,data,0.1f,lookup,{},error));
    std::cout<<"native SpeedEffect modifier/callback ordering smoke passed\n";
}
