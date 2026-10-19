#include "../app/self_effects.hpp"
#include <cassert>
#include <iostream>
using namespace hades;
int main(){
    UnitStatus status;std::string error;assert(status.initializeFreshUnit(error));
    auto first=std::make_shared<WeaponSelfEffect>();first->name="ArmorBreakStun";first->trigger=1;
    auto second=std::make_shared<WeaponSelfEffect>();second->name="Later";second->trigger=1;
    auto inactive=std::make_shared<WeaponSelfEffect>();inactive->active=false;
    std::vector<std::shared_ptr<WeaponSelfEffect>> data={first,second,inactive};
    std::shared_ptr<UnitStatusEffect> known;std::vector<std::string> log;
    SelfEffectCallbacks cb;
    cb.randomStream0=[&](double& n,std::string&){log.push_back("rng");n=1;return true;};
    cb.findEffect=[&](const std::string& name,std::shared_ptr<UnitStatusEffect>& out,std::string&){log.push_back("find:"+name);out=known;return true;};
    cb.createEffect=[&](const WeaponSelfEffect& d,float duration,std::shared_ptr<UnitStatusEffect>& out,std::string&){assert(duration==0);log.push_back("factory");out=std::make_shared<UnitStatusEffect>();out->handle=1;out->name=d.name;out->disableAttack=true;return true;};
    cb.affectUnit=[&](const WeaponSelfEffect&,UnitStatusEffect& e,std::string&){assert(e.selfApplied);assert(status.snapshot().pending.empty());e.unitId=40000;log.push_back("apply");second->active=false;return true;};
    SelfEffectAdmissionResult r;
    assert(admitWeaponSelfEffects(1,1,data,status,cb,r,error));
    assert(r.randomDraws==2&&r.created==1&&r.reapplied==0);
    assert((log==std::vector<std::string>{"rng","rng","find:ArmorBreakStun","factory","apply"}));
    assert(status.snapshot().pending.size()==1&&!status.canAttack(false,false,false));
    // Native consumes the weapon gate draw even with no effects.
    log.clear();assert(admitWeaponSelfEffects(1,1,{},status,cb,r,error));assert(r.randomDraws==1);
    // Failed perfect-charge requirement still consumes the qualifying item draw.
    first->requirePerfectCharge=true;cb.isPerfectCharge=[](bool& p,std::string&){p=false;return true;};
    assert(admitWeaponSelfEffects(1,1,data,status,cb,r,error));assert(r.randomDraws==2&&r.created==0);
    first->requirePerfectCharge=false;known=std::make_shared<UnitStatusEffect>();known->selfApplied=false;
    cb.reapply=[&](const WeaponSelfEffect&,UnitStatusEffect& e,float duration,std::string&){assert(!e.selfApplied&&duration==-1);log.push_back("reapply");return true;};
    assert(admitWeaponSelfEffects(1,1,data,status,cb,r,error));assert(r.reapplied==1&&!known->selfApplied);
    // Failure at the original Lua/side-effect barrier must never publish pending.
    UnitStatus failed;assert(failed.initializeFreshUnit(error));known.reset();
    cb.affectUnit=[](const WeaponSelfEffect&,UnitStatusEffect&,std::string& e){e="unimplemented actual effect side effect";return false;};
    assert(!admitWeaponSelfEffects(1,1,data,failed,cb,r,error));assert(failed.snapshot().pending.empty());
    std::cout<<"self-effects admission smoke passed\n";
}
