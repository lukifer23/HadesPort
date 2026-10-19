#include "../app/effect_update.hpp"
#include <cassert>
#include <iostream>
using namespace hades;
int main(){
    EffectAffectState e;e.status=std::make_shared<UnitStatusEffect>();e.status->unitId=40000;e.timeRemaining=0.25f;
    EffectUpdateFxState fx;EffectUpdateData data;data.duration=0.25f;
    EffectUpdateCallbacks cb;std::string error;bool expired=true;std::vector<std::string> calls;
    cb.queryOwner=[&](int32_t id,EffectUpdateOwner& owner,std::string&){assert(id==40000);owner.exists=true;owner.stunBreakModifier=2;owner.charmBreakModifier=3;return true;};
    cb.hasRefreshOnExpireEffect=[&](bool& refresh,std::string&){calls.push_back("refresh");refresh=false;return true;};
    cb.clearVirtual=[&](EffectAffectState& actual,std::string&){calls.push_back("clear");actual.status->cleared=true;return true;};
    assert(updateBaseEffect(e,fx,data,0.25f,cb,expired,error));assert(!expired&&e.timeRemaining==0&&calls.empty());
    assert(updateBaseEffect(e,fx,data,0.01f,cb,expired,error));assert(expired&&e.status->cleared);assert((calls==std::vector<std::string>{"refresh","clear"}));
    // Genuine stun scaling has priority over charm scaling.
    e.status->cleared=false;e.timeRemaining=1;data.type=7;data.disableMove=true;calls.clear();
    assert(updateBaseEffect(e,fx,data,0.25f,cb,expired,error));assert(e.timeRemaining==0.5f);
    data.disableMove=false;assert(updateBaseEffect(e,fx,data,0.125f,cb,expired,error));assert(e.timeRemaining==0.125f);
    // UpdateFx runs before expiration lookup, which can refresh to current data
    // duration. A source-death test still follows a successful refresh.
    e.timeRemaining=0;e.frontFx=1;e.hasSourceWeapon=true;data.clearOnSourceDeath=true;calls.clear();
    cb.updateFx=[&](auto&,auto&,std::string&){calls.push_back("fx");data.duration=3;return true;};
    cb.hasRefreshOnExpireEffect=[&](bool& refresh,std::string&){calls.push_back("refresh");refresh=true;return true;};
    cb.sourceWeaponOwnerAlive=[&](bool& alive,std::string&){calls.push_back("source");alive=false;return true;};
    assert(updateBaseEffect(e,fx,data,0.1f,cb,expired,error));assert(expired&&e.timeRemaining==3);assert((calls==std::vector<std::string>{"fx","refresh","source","clear"}));
    // Existing cleared state prevents a duplicate Clear call at expiry.
    e.frontFx=0;e.timeRemaining=-1;calls.clear();cb.hasRefreshOnExpireEffect=[&](bool& b,std::string&){calls.push_back("refresh");b=false;return true;};
    assert(updateBaseEffect(e,fx,data,0,cb,expired,error));assert(expired&&(calls==std::vector<std::string>{"refresh"}));
    e.status->cleared=false;e.frontFx=1;e.timeRemaining=1;cb.updateFx={};assert(!updateBaseEffect(e,fx,data,0,cb,expired,error));
    std::cout<<"native Effect lifetime strict expiry/callback ordering smoke passed\n";
}
