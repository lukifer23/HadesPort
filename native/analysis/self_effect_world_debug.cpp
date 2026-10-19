#include "self_effect_world.hpp"
#include "effect_name_hash.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
int main(int argc,char** argv){try{
 if(argc!=3)throw std::runtime_error("Provide actual actor Lua and HWA3 catalog");
 std::unique_ptr<lua_State,decltype(&lua_close)> L(luaL_newstate(),lua_close);luaL_openlibs(L.get());
 if(luaL_dofile(L.get(),argv[1]))throw std::runtime_error(lua_tostring(L.get(),-1));
 hades::SourceValue source;std::string error;if(!hades::readSourceValue(L.get(),-1,source,error))throw std::runtime_error(error);
 hades::ActorWorld actors;if(!actors.loadCatalog(std::move(source),error))throw std::runtime_error(error);
 std::ifstream file(argv[2],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),{});auto catalog=std::make_shared<hades::WeaponCatalog>();if(!hades::loadWeaponCatalog(bytes,*catalog,error))throw std::runtime_error(error);
 ObjectSnapshot object;if(!actors.initializeMapActor(40000,object,catalog,{},error)||!actors.registerActor(40000,object,error))throw std::runtime_error(error);
 auto* owner=actors.actor(40000);if(!owner||!owner->arsenal||!owner->arsenal->equip("SwordWeapon",error))throw std::runtime_error(error);
 hades::WeaponSelfEffectWorld world(actors);hades::WeaponSelfEffectProviders p;
 unsigned draws=0,applies=0,reapplies=0,clears=0;
 p.randomStream0=[&](double& draw,std::string&){draw=0.5;++draws;return true;};
 // This diagnostic records callback barriers, not original Lua execution.
 // Reject every operation beyond the actual SlowCharge's OnEffectApply path.
 p.affect=[&](hades::EffectAffectOperation op,hades::WeaponSelfEffectInstance&,hades::EffectAffectOwner&,const hades::WeaponEffectDefinition&,bool reapplied,std::string& e){
  if(op!=hades::EffectAffectOperation::OnEffectApply){e="Diagnostic does not implement this native side effect";return false;}
  if(reapplied)++reapplies;else ++applies;return true;
 };
 p.onCleared=[&](hades::WeaponSelfEffectInstance&,const hades::WeaponEffectDefinition&,std::string&){++clears;return true;};
 p.hasRefreshOnExpireEffect=[&](const hades::WeaponSelfEffectInstance& actual,const hades::WeaponEffectDefinition& data,bool& result,std::string& e){return world.hasRefreshOnExpire(actual,data,result,e);};
 auto check=[&](bool value){if(!value)throw std::runtime_error(error.empty()?"Source SlowCharge state mismatch":error);};
 hades::SelfEffectAdmissionResult result;
 check(world.applyWeaponEffects(40000,"SwordWeapon",3,p,result,error));
 check(draws==2&&result.created==1&&owner->status->snapshot().pending.size()==1&&std::abs(owner->speedModifier-0.35f)<1e-6f&&applies==1);
 check(world.applyWeaponEffects(40000,"SwordWeapon",3,p,result,error));
 check(result.reapplied==1&&reapplies==1&&std::abs(owner->speedModifier-0.35f)<1e-6f);
 check(world.cancelWeaponEffects(40000,"SwordWeapon",3,p,error));
 check(owner->status->snapshot().pending.empty()&&clears==1&&std::abs(owner->speedModifier-1)<1e-6f&&world.snapshot().front().disposed&&world.snapshot().front().ownerId==-1);
 check(world.applyWeaponEffects(40000,"SwordWeapon",3,p,result,error));
 check(world.updateUnit(40000,0.1f,p,error));
 check(owner->status->snapshot().active.size()==1&&owner->status->snapshot().pending.empty()&&std::abs(owner->speedModifier-0.35f)<1e-6f);
 check(world.clearOnAttack(40000,p,error)); // Source ClearOnAttack=false: real active record persists.
 check(owner->status->snapshot().toDelete.empty()&&clears==1);
 check(world.updateUnit(40000,0.2f,p,error));
 check(owner->status->snapshot().active.empty()&&clears==2&&std::abs(owner->speedModifier-1)<1e-6f);
 // Empty trigger list still consumes native global weapon chance draw.
 const unsigned before=draws;check(world.applyWeaponEffects(40000,"SwordWeapon",999,p,result,error));check(draws==before+1&&result.created==0);
 unsigned names=0;for(const auto& weapon:catalog->weapons)for(const auto& effect:weapon.effects){check(effect.nativeIdsKnown&&effect.nativeNameId!=0);++names;}
 uint64_t emptyHash=0;check(hades::nativeEffectNameHash("",emptyHash,error));check(emptyHash!=0);
 std::cout<<"NATIVE_EFFECT_ID_DEBUG_OK sourceNames="<<names<<" allNativeNameIdsNonzero=true emptyReadHash="<<std::hex<<emptyHash<<std::dec<<" omittedRefreshIds=0 actual_status_id_query=true\n";
 std::cout<<world.dump()<<"ORIGINAL_SLOW_CHARGE_STATE_DEBUG_OK source_modifier=.35 duration=.25 actual_owner_speed_restored=true pending_cancel_disposed=true active_expiry_disposed=true callback_barriers_recorded=true original_lua_dispatched=false gameplay_observed=false\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"SELF_EFFECT_WORLD_DEBUG_FAILURE "<<e.what()<<'\n';return 1;}}
