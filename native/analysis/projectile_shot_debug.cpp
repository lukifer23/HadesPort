#include "projectile_world.hpp"
#include "weapon_catalog.hpp"
#include "life_runtime.hpp"
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
 hades::ActorLife timer;timer.consecutiveHits=3;timer.consecutiveHitWindow=.1f;timer.activeHitFxCount=1;timer.activeHitFxCooldown=.05f;bool keep=false;std::string timerError;
 if(!hades::advanceLifeTimers(timer,.1f,keep,timerError)||!keep||timer.consecutiveHits||timer.activeHitFxCount||timer.consecutiveHitWindow!=0||timer.activeHitFxCooldown!=1)throw std::runtime_error("Native Life timer crossing/keep-before-reset mismatch");
 if(!hades::advanceLifeTimers(timer,.1f,keep,timerError)||keep||!hades::lifeOfInterest(timer))throw std::runtime_error("Native Life interest-vs-update keep asymmetry lost");
 if(argc!=4)throw std::runtime_error("Provide original actor Lua, projectile Thing Lua and HWA3");
 std::unique_ptr<lua_State,decltype(&lua_close)> L(luaL_newstate(),lua_close);luaL_openlibs(L.get());
 auto load=[&](const char* path){if(luaL_dofile(L.get(),path))throw std::runtime_error(lua_tostring(L.get(),-1));hades::SourceValue value;std::string e;if(!hades::readSourceValue(L.get(),-1,value,e))throw std::runtime_error(e);lua_pop(L.get(),1);return value;};
 hades::ActorWorld actors;std::string error;if(!actors.loadCatalog(load(argv[1]),error))throw std::runtime_error(error);
 std::ifstream f(argv[3],std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});auto catalog=std::make_shared<hades::WeaponCatalog>();if(!hades::loadWeaponCatalog(bytes,*catalog,error))throw std::runtime_error(error);
 ObjectSnapshot originalPlayer;
 // Real source-backed map Unit components/manager storage. This bounded debug
 // does not run original OnSpawn, World::Start or claim player gameplay.
 if(!actors.initializeMapActor(40000,originalPlayer,catalog,{},error)||!actors.registerActor(40000,originalPlayer,error))throw std::runtime_error(error);
 auto* owner=actors.actor(40000);if(!owner||!owner->arsenal||!owner->arsenal->equip("SwordWeapon",error))throw std::runtime_error(error);
 hades::ProjectileWorld projectiles;if(!projectiles.loadCatalog(load(argv[2]),error))throw std::runtime_error(error);
 hades::ProjectileShotProviders p;p.worldTime=10; // nearby admission comes from actual AmbientSound.valid
 unsigned draws=0,emptyFxLookups=0,clearEmptyStatuses=0;
 p.random=[&](double& value,std::string&){value=0.5;++draws;return true;};
 p.ownerState=[](const hades::ActorComponents&,float& damage,int32_t& spawned,bool& traitor,std::string&){
  // Bounded native Unit Defaults13a4/13b6 and live ctor0d54 establish these
  // initial values; this debug has admitted no modifier/charm/spawn mutation.
  damage=1;spawned=0;traitor=false;return true;
 };
 p.preflight=[&](const hades::ProjectileInitializationOperation& op,const hades::ProjectileActor&,std::string& e){
  using K=hades::ProjectileInitializationOperationKind;
  if(op.kind==K::ClearOnAttackEffects){const auto s=owner->status->snapshot();if(s.active.empty()&&s.pending.empty())return true;}
  // Original first Sword has literal-null StartFx/StartFx2. Native null names cannot name
  // any definition in the source animation namespace; no art is fabricated.
  if(op.kind==K::CreateStartAnimation&&(op.name.empty()||op.name=="null"))return true;
  e="Actual source-shot debug lacks external consumer "+op.name;return false;
 };
 p.execute=[&](const hades::ProjectileInitializationOperation& op,hades::ProjectileActor&,std::string& e){
  using K=hades::ProjectileInitializationOperationKind;
  if(op.kind==K::ClearOnAttackEffects){const auto s=owner->status->snapshot();if(s.active.empty()&&s.pending.empty()){++clearEmptyStatuses;return true;}}
  if(op.kind==K::CreateStartAnimation&&(op.name.empty()||op.name=="null")){++emptyFxLookups;return true;}
  e="Unconsumed actual original shot operation";return false;
 };
 hades::ProjectileInitializationRequest request;request.ownerId=40000;request.weaponName="SwordWeapon";request.x=originalPlayer.x;request.y=originalPlayer.y;request.fire.angle=0.4f;
 uint64_t handle=0;if(!projectiles.initializeShot(request,*owner->arsenal,actors,p,handle,error))throw std::runtime_error(error);
 const auto* shot=projectiles.actor(handle);
 if(!shot||!shot->shotInitialized||!shot->thingInitialized||!shot->pending||shot->active||shot->body.id!=-1||shot->geometry.size()!=3||shot->physics||draws!=1||clearEmptyStatuses!=1||emptyFxLookups!=2||shot->firingArsenal!=owner->arsenal.get()||shot->body.z!=originalPlayer.z+70||shot->state.ownerSpawnedById!=0)throw std::runtime_error("Actual original firstSword construction state mismatch");
 if(!projectiles.flushPending(error)||projectiles.active().size()!=1||!projectiles.actor(handle)->active)throw std::runtime_error(error);
 hades::ProjectileUpdateProviders tick;tick.worldTime=10;tick.worldTimeMultiplier=1;
 unsigned defenses=0,impacts=0,disposals=0;
 // Actual source predicate/narrowphase against the genuinely allocated lone
 // Hero; no obstacles were activated in this isolated construction replay.
 // Original Impact/TakeHit/Lua are explicitly unconsumed, never fixed success.
 tick.preflight=[](const hades::ProjectileUpdateOperation& op,const hades::ProjectileActor&,std::string& e){
  using K=hades::ProjectileUpdateOperationKind;
  if(op.kind==K::CheckProjectileDefense||op.kind==K::CheckForImpact||op.kind==K::DisposeThing)return true;
  e="Diagnostic cannot consume additional actual projectile branch";return false;
 };
 tick.execute=[&](const hades::ProjectileUpdateOperation& op,hades::ProjectileActor&,hades::ProjectileUpdateReply& reply,std::string& e){
  using K=hades::ProjectileUpdateOperationKind;
  if(op.kind==K::CheckProjectileDefense){++defenses;hades::ProjectileDefenseProviders actual;actual.teams.unitPlayerIdentity=[](int32_t,uint64_t& token,std::string&){token=0;return true;};return projectiles.checkArcProjectileDefense(handle,actual,e);}
  if(op.kind==K::CheckForImpact){++impacts;hades::ProjectileImpactProviders actual;actual.worldTime=tick.worldTime;
   actual.teams.unitPlayerIdentity=[](int32_t,uint64_t& token,std::string&){token=0;return true;};
   actual.preflight=[](const hades::ProjectileCollisionOperation&,std::string& detail){detail="No fake collision callbacks admitted by isolated replay";return false;};
   hades::ProjectileImpactResult hit;if(!projectiles.checkForImpact(handle,actual,hit,e))return false;reply.impactResult=hit;return true;}
  if(op.kind==K::DisposeThing){++disposals;return true;}
  e="Unconsumed diagnostic projectile operation";return false;
 };
 const float initial=projectiles.actor(handle)->arcAngleOffset;
 if(!projectiles.update(1.0f/60,tick,error)||projectiles.actor(handle)->arcAngleOffset!=initial)throw std::runtime_error(error.empty()?"ARC failed native spawn-clock equality hold":error);
 for(unsigned frame=1;frame<=4;++frame){tick.worldTime=10+float(frame)/60;if(!projectiles.update(1.0f/60,tick,error))throw std::runtime_error(error);}
 if(projectiles.actor(handle)||!projectiles.active().empty()||defenses!=5||impacts!=5||disposals!=1||projectiles.retired().size()!=1||projectiles.retired().back().polygonVertices!=3)throw std::runtime_error("ARC real storage timing/removal/release replay mismatch");
 std::cout<<"ORIGINAL_ARC_STATE_DEBUG_OK sourceSpeed=2700 spawnClockEqualityHolds=true managerFrames=5 defenseBarriers="<<defenses<<" impactBarriers="<<impacts<<" actualGeometryMoved=true nativeAngleEndRemoval=true actual_defense_predicate=true actual_polygon_query=true original_impact_dispatched=false\n";
 std::cout<<projectiles.dump()<<"ORIGINAL_CATALOG_SHOT_DEBUG_OK actual_map_unit_storage=true actual_projectile_body=true original_polygon=3 publicId=-1 rngDraws="<<draws<<" active_after_real_manager_flush=true callbacks_dispatched=false gameplay_observed=false\n";
 return 0;
}catch(const std::exception& e){std::cerr<<"PROJECTILE_SHOT_DEBUG_FAILURE "<<e.what()<<'\n';return 1;}}
