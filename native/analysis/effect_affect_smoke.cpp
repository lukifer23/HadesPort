#include "../app/effect_affect.hpp"
#include <cassert>
#include <iostream>
using namespace hades;
static EffectAffectState effect(){EffectAffectState e;e.status=std::make_shared<UnitStatusEffect>();e.status->handle=1;return e;}
int main(){
    EffectAffectData d;d.duration=1;d.disableAttack=true;
    EffectAffectOwner owner;owner.id=40000;EffectAffectState e=effect();e.status->selfApplied=true;
    std::vector<EffectAffectOperation> calls;std::string error;
    auto actual=[&](EffectAffectOperation op,EffectAffectState& state,EffectAffectOwner&,const EffectAffectData&,std::string&){
        calls.push_back(op);assert(state.status->unitId==40000);assert(state.timeRemaining==1);return true;};
    assert(affectTagEffect(e,owner,d,actual,error));assert(e.tagActive&&e.timeRemaining==1&&e.soundId==0);
    assert((calls==std::vector<EffectAffectOperation>{EffectAffectOperation::OnEffectApply,EffectAffectOperation::OnStun}));
    e=effect();d.elapsedTimeMultiplier=0.5f;owner.slowModifier=0.5f;owner.elapsedTimeMultiplier=2;owner.hasAttachedAnimation=true;
    calls.clear();assert(affectBaseEffect(e,owner,d,actual,error));assert(e.appliedElapsedTimeMultiplier==0.75f&&owner.elapsedTimeMultiplier==1.5f);
    assert(calls.front()==EffectAffectOperation::RefreshAttachedAnimationTime);
    // Synchronous original callbacks can mutate later native reads. Clearing
    // skips persistent FX only, preserving property/OnStun/audio ordering.
    e=effect();e.tagCooldown=2;d={};d.duration=1;d.overheadFx="Over";d.backFx="Back";d.frontFx="Front";d.unitPropertyName="Speed";d.soundName="/Sound";
    calls.clear();
    auto clearDuringApply=[&](EffectAffectOperation op,EffectAffectState& state,EffectAffectOwner&,const EffectAffectData&,std::string&){
        calls.push_back(op);if(op==EffectAffectOperation::OnEffectApply){state.status->cleared=true;d.duration=8;d.disableAttack=true;}
        if(op==EffectAffectOperation::PlaySound)state.soundId=123;return true;};
    assert(affectTagEffect(e,owner,d,clearDuringApply,error));assert(e.timeRemaining==3&&e.soundId==123);
    assert((calls==std::vector<EffectAffectOperation>{EffectAffectOperation::OnEffectApply,EffectAffectOperation::CaptureAndSetUnitProperty,EffectAffectOperation::OnStun,EffectAffectOperation::PlaySound}));
    e=effect();e.hasSourceWeapon=true;d={};d.overheadFx="Over";d.backFx="Back";d.frontFx="Front";d.scaleFxToProjectileRadius=true;calls.clear();
    auto graphics=[&](EffectAffectOperation op,EffectAffectState& state,EffectAffectOwner&,const EffectAffectData&,std::string&){
        calls.push_back(op);if(op==EffectAffectOperation::CreateOverheadFx)state.overheadFx=1;
        if(op==EffectAffectOperation::CreateBackFx)state.backFx=2;if(op==EffectAffectOperation::CreateFrontFx)state.frontFx=3;return true;};
    assert(affectBaseEffect(e,owner,d,graphics,error));
    assert((calls==std::vector<EffectAffectOperation>{EffectAffectOperation::OnEffectApply,EffectAffectOperation::CreateOverheadFx,EffectAffectOperation::CreateBackFx,EffectAffectOperation::CreateFrontFx,EffectAffectOperation::ScaleFrontBackFx}));
    e=effect();assert(!affectBaseEffect(e,owner,d,{},error));
    std::cout<<"effect base/Tag affect ordering smoke passed\n";
}
