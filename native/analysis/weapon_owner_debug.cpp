// Focused original-data replay plus cancellation boundary checks. No gameplay claim.
#include "actor_runtime.hpp"
#include "weapon_catalog.hpp"
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
 if(argc!=3)throw std::runtime_error("Provide original actors.lua and HWA3 weapons.bin");
 std::unique_ptr<lua_State,decltype(&lua_close)> L(luaL_newstate(),lua_close);luaL_openlibs(L.get());
 if(luaL_dofile(L.get(),argv[1]))throw std::runtime_error(lua_tostring(L.get(),-1));
 hades::SourceValue source;std::string error;if(!hades::readSourceValue(L.get(),-1,source,error))throw std::runtime_error(error);
 // Exercise the actual native MirrorGeometry branch on the unchanged original
 // hero polygon with an explicit diagnostic property override before creation.
 auto& defs=std::get<hades::SourceValue::Object>(std::get<hades::SourceValue::Object>(source.value).at("definitions").value);
 auto& properties=std::get<hades::SourceValue::Object>(std::get<hades::SourceValue::Object>(defs.at("Units/_PlayerUnit").value).at("properties").value);
 auto& thing=std::get<hades::SourceValue::Object>(properties.at("Thing").value);thing["MirrorGeometry"].value=true;thing["RotateGeometry"].value=false;
 hades::ActorWorld actors;if(!actors.loadCatalog(std::move(source),error))throw std::runtime_error(error);
 std::ifstream input(argv[2],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
 auto catalog=std::make_shared<hades::WeaponCatalog>();if(!hades::loadWeaponCatalog(bytes,*catalog,error))throw std::runtime_error(error);
 ObjectSnapshot object;if(!actors.initializeMapActor(40000,object,catalog,{},error)||!actors.registerActor(40000,object,error))throw std::runtime_error(error);
 auto* actor=actors.actor(40000);const auto original=actor->geometry;
 if(original.empty())throw std::runtime_error("Original hero polygon unavailable");
 if(!actors.setAngle(40000,3.1415927410125732f,error)||!actor->geometryFacingFlipped)throw std::runtime_error(error.empty()?"Facing flip missing":error);
 if(!actors.refreshGeometry(40000,error)||!actor->geometryFacingFlipped)throw std::runtime_error("Geometry rebuild lost native facing parity");
 if(!actors.setAngle(40000,0,error)||actor->geometryFacingFlipped)throw std::runtime_error("Facing parity failed to restore");
 for(size_t i=0;i<original.size();++i)if(original[i].x!=actor->geometry[i].x||original[i].y!=actor->geometry[i].y)throw std::runtime_error("Facing flip failed to restore original polygon");
 if(!actor->arsenal->equip("SwordWeapon2",error))throw std::runtime_error(error);
 bool cancel=false;if(!actor->arsenal->removeWeaponControl("SwordWeapon2",cancel,error)||!cancel||actor->arsenal->weaponState("SwordWeapon2",error)->controllable)throw std::runtime_error("Original sword control-removal policy mismatch");
 if(!actor->arsenal->removeWeaponControl("not-owned",cancel,error)||cancel)throw std::runtime_error("Native null control-removal branch mismatch");
 hades::UnitStatus status;if(!status.initializeFreshUnit(error))throw std::runtime_error(error);
 auto effect=[&](uint64_t id,const char* name,bool attack){auto value=std::make_shared<hades::UnitStatusEffect>();value->handle=id;value->unitId=40000;value->name=name;value->cancelable=true;value->disableRotate=true;value->disableAttack=attack;if(!status.trackAdmittedEffect(value,error))throw std::runtime_error(error);return value;};
 auto own=effect(1,"Own",false),rotate=effect(2,"Other",false),attack=effect(3,"Attack",true);if(!status.promotePendingEffects(error))throw std::runtime_error(error);auto pending=effect(4,"Pending",false);
 status.timers.rotate=1;status.timers.rotateCancelable=true;status.timers.attack=2;unsigned clears=0;
 auto clear=[&](hades::UnitStatusEffect& value,std::string&){value.cleared=true;++clears;return true;};
 if(!status.cancelRotateDisables({"Own"},false,clear,error)||clears!=1||!rotate->cleared||own->cleared||attack->cleared||pending->cleared||status.timers.rotate!=-1||status.timers.attack!=2)throw std::runtime_error("Native active-only rotation cancellation asymmetry lost");
 unsigned disposed=0;status.blockAll=true;status.blockEffectId=77;status.blockEffectDuration=3;
 auto dispose=[&](hades::UnitStatusEffect& value,std::string&){if(value.unitId!=-1)throw std::runtime_error("Reset Dispose missing native invalid owner assignment");value.disposed=true;++disposed;return true;};
 if(!status.reset({},clear,dispose,error)||disposed!=4||status.blockAll||status.blockEffectId||status.blockEffectDuration||!status.snapshot().pending.empty()||!status.snapshot().active.empty()||!status.snapshot().toDelete.empty())throw std::runtime_error("Native Reset/ClearEffects failed actual retained membership cleanup");
 bool retained=true,released=false;hades::WeaponRetirementProviders retirement;retirement.retained=[&](const std::string&,bool& value,std::string&){value=retained;return true;};
 if(!actor->arsenal->retireOwner(retirement,released,error)||released||!actor->arsenal->hasWeapon("SwordWeapon2"))throw std::runtime_error("Actual retained source data freed prematurely");
 retained=false;if(!actor->arsenal->retireOwner(retirement,released,error)||!released||!actor->arsenal->empty()||actor->arsenal->retiredWeapons().empty())throw std::runtime_error("Last reference retirement failed bounded full-data release");
 std::cout<<"WEAPON_OWNER_REPLAY_OK originalHeroPolygon="<<original.size()<<" mirrorRoundTrip=true rotationActiveOnly=true originalSwordRemoval=true\n";
 return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
