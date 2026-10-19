#include "weapon_catalog.hpp"
#include "weapon_execution.hpp"
#include "projectile_runtime.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <limits>
#include <cmath>
#include <algorithm>
#include <stdexcept>
int main(int argc,char**argv){try{
 if(argc!=2)throw std::runtime_error("Provide source HWA2 catalog");
 std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)),{});
 auto c=std::make_shared<hades::WeaponCatalog>();std::string error;
 if(!hades::loadWeaponCatalog(b,*c,error))throw std::runtime_error(error);
 if(c->projectile("SwordWeapon")->fields.contains("Type")){
  if(std::get<double>(c->projectile("SwordWeapon")->fields.at("Type"))!=5||std::get<double>(c->projectile("RangedWeapon")->fields.at("Type"))!=1||std::get<double>(c->projectile("SwordWeaponDash")->fields.at("Type"))!=0||std::get<double>(c->projectile("RangedWeapon")->fields.at("ObstacleCollisionCheck"))!=4)throw std::runtime_error("Source/compiled native projectile enum normalization failed");
  hades::ProjectileCreationDescriptor dispatch;
  if(!hades::describeProjectileCreation(*c->projectile("SwordWeapon"),dispatch,error)||dispatch.concreteClass!=hades::ProjectileClass::Arc||!dispatch.usesBaseProjectileInit||dispatch.publicThingId!=-1)throw std::runtime_error("Native ARC dispatch failed");
  if(!hades::describeProjectileCreation(*c->projectile("RangedWeapon"),dispatch,error)||dispatch.concreteClass!=hades::ProjectileClass::Homing||!dispatch.requiresHomingDefaults||dispatch.obstacleCollisionCheck!=hades::ProjectileCollisionCheck::PolygonContainsPoint)throw std::runtime_error("Native HOMING dispatch failed");
  if(c->projectile("SwordWeapon")->effectsJson.empty())throw std::runtime_error("Projectile inherited effects missing from current packet");
  hades::ProjectileInitializationRequest creation;creation.lifetimeHandle=0x100000055;creation.ownerId=73;creation.weaponName="SwordWeapon";creation.fire.angle=0.4f;creation.fireIndex=2;creation.volleyIndex=3;
  hades::ProjectileInitialization init(creation);hades::ProjectileInitializationFrame pf;pf.worldTime=7;pf.ownerExists=true;pf.ownerSpawnedById=40000;pf.weaponHasAmmo=true;
  hades::ProjectileInitializationStep ps;const auto& pd=*c->projectile("SwordWeapon");
  if(init.advance(pd,pf,{}, {},ps,error)||init.state().lifetimeHandle!=0)throw std::runtime_error("Unsupported projectile Thing init committed state");
  std::vector<hades::ProjectileInitializationOperationKind> initOps;std::optional<double> reply;bool initFinished=false;
  for(int i=0;i<30;++i){
   if(!init.advance(pd,pf,reply,[](const auto&,std::string&){return true;},ps,error))throw std::runtime_error("Projectile native init prefix: "+error);
   reply.reset();
   if(ps.progress==hades::ProjectileInitializationProgress::BeforeWeaponModifiers){initFinished=true;break;}
   initOps.push_back(ps.operation.kind);
   if(ps.operation.kind==hades::ProjectileInitializationOperationKind::ThingInitialize){pf.thingInitialized=true;pf.hasBody=true;pf.hasPhysics=true;}
   if(ps.operation.kind==hades::ProjectileInitializationOperationKind::RequestRandomDouble){
    if(init.advance(pd,pf,{},[](const auto&,std::string&){return true;},ps,error))throw std::runtime_error("Projectile random barrier silently skipped");
    reply=0.25;
   }
  }
  const auto& pi=init.state();
  if(!initFinished||pi.publicThingId!=-1||pi.ownerId!=73||pi.fireIndex!=2||pi.volleyIndex!=3||pi.waveIndex!=0||pi.speed!=2700||pi.range!=260||pi.rangeSquared!=67600||pi.numPenetrations!=99999||pi.lastJumpTime!=7||!pi.hasStarted||pi.fuseRemaining!=std::numeric_limits<float>::max())throw std::runtime_error("Original sword projectile scalar initialization mismatch");
  const std::vector<hades::ProjectileInitializationOperationKind> expectedInit{
   hades::ProjectileInitializationOperationKind::ThingInitialize,hades::ProjectileInitializationOperationKind::SetPhysicsIgnoreGravity,
   hades::ProjectileInitializationOperationKind::AdjustBodyAlpha,hades::ProjectileInitializationOperationKind::RetainFiringWeapon,
   hades::ProjectileInitializationOperationKind::SetWeaponLastFirePerfectCharge,hades::ProjectileInitializationOperationKind::SetAttachedAnimationRadius,
   hades::ProjectileInitializationOperationKind::RequestRandomDouble,hades::ProjectileInitializationOperationKind::SetupGeometry,
   hades::ProjectileInitializationOperationKind::RefreshLocation,hades::ProjectileInitializationOperationKind::SetAngle,
   hades::ProjectileInitializationOperationKind::ClearOnAttackEffects};
  if(initOps!=expectedInit)throw std::runtime_error("Native original sword projectile callback/RNG ordering mismatch");
  hades::WeaponArsenal firingOwner(73,"_PlayerUnit",c);if(!firingOwner.equip("SwordWeapon",error))throw std::runtime_error(error);
  hades::ProjectileInitializationTail tail(creation,pi);hades::ProjectileInitializationTailFrame tf;
  tf.gunOwner.exists=true;tf.gunOwner.damageOutputMultiplier=1;tf.gunOwner.damageOutputModifier=1;tf.gunOwner.projectileSpeedMultiplier=1;tf.gunOwner.perfectChargeBonus=1;
  tf.spawnX=100;tf.spawnY=200;tf.worldX=100;tf.worldY=200;tf.ownerElapsedTimeMultiplier=1;
  const auto fw=firingOwner.snapshot().front();hades::ProjectileInitializationTailStep ts;std::vector<hades::ProjectileInitializationOperationKind> tailOps;bool tailFinished=false;
  for(int i=0;i<30;++i){if(!tail.advance(pd,fw,tf,[](const auto&,std::string&){return true;},ts,error))throw std::runtime_error("Native projectile tail: "+error);
   if(ts.progress==hades::ProjectileInitializationTailProgress::PendingQueueSetupFinished){tailFinished=true;break;}tailOps.push_back(ts.operation.kind);
  }
  const std::vector<hades::ProjectileInitializationOperationKind> expectedTail{hades::ProjectileInitializationOperationKind::SetBodyScale,hades::ProjectileInitializationOperationKind::SetupGeometry,
   hades::ProjectileInitializationOperationKind::QueuePendingProjectile,hades::ProjectileInitializationOperationKind::InheritElapsedTimeMultiplier,hades::ProjectileInitializationOperationKind::CopyOwnerGroups};
  if(!tailFinished||tailOps!=expectedTail||tail.state().damageMultiplier!=1||tail.state().speed!=2700||tail.state().range!=260||std::abs(tail.state().targetX-(100+std::cos(0.4f)*260))>0.001)throw std::runtime_error("Native projectile modifiers/queue sequence mismatch");
  const auto initialDefinition=firingOwner.projectileData("SwordWeapon",false,error);if(!initialDefinition)throw std::runtime_error(error);
  if(!firingOwner.setScriptProjectileProperty("SwordWeapon","Speed",2.0,hades::WeaponValueChange::MultiplyBase,error))throw std::runtime_error(error);
  const auto* changed=firingOwner.projectileData("SwordWeapon",false,error);
  if(!changed||std::get<double>(changed->fields.at("Speed"))!=5400||std::get<double>(c->projectile("SwordWeapon")->fields.at("Speed"))!=2700)throw std::runtime_error("Per-owner projectile MultiplyBase changed source or wrong arithmetic");
  if(firingOwner.setScriptProjectileProperty("SwordWeapon","UnknownNativeField",3.0,hades::WeaponValueChange::Absolute,error)||std::get<double>(firingOwner.projectileData("SwordWeapon",false,error)->fields.at("Speed"))!=5400)throw std::runtime_error("Rejected projectile mutation changed current data");
 }
 hades::ArcProjectileMoveFrame amf;amf.worldTime=1;amf.creationTime=1;amf.dt=0.02f;amf.spawnX=100;amf.spawnY=200;amf.range=260;amf.spawnAngle=0.4f;
 float arcAngle=0;if(!hades::initialArcProjectileAngle(*c->projectile("SwordWeapon"),arcAngle,error))throw std::runtime_error(error);
 hades::ArcProjectileMovePlan amp;
 if(!hades::planArcProjectileMove(*c->projectile("SwordWeapon"),arcAngle,amf,amp,error)||amp.nextAngleOffset!=arcAngle||!amp.hasTransform||amp.nativeShouldRemove)throw std::runtime_error("Original ARC must retain ArcStart at exact creation time");
 amf.worldTime=1.02f;
 if(!hades::planArcProjectileMove(*c->projectile("SwordWeapon"),arcAngle,amf,amp,error)||std::abs(amp.nextAngleOffset-(arcAngle+static_cast<float>(2700.0*0.017453292519943295)*0.02f))>0.00001f||std::abs(amp.x-(100+std::cos(amp.nextAngleOffset+0.4f)*260))>0.0001f)throw std::runtime_error("Native ARC source degree-speed sweep math mismatch");
 amf.dt=1;if(!hades::planArcProjectileMove(*c->projectile("SwordWeapon"),arcAngle,amf,amp,error)||amp.nextAngleOffset!=static_cast<float>(std::get<double>(c->projectile("SwordWeapon")->fields.at("ArcEnd"))))throw std::runtime_error("Native ARC end clamp mismatch");
 if(!c->weapon("SwordWeapon")->effects.front().fields.empty()){
  const auto& fx=c->weapon("SwordWeapon")->effects;const auto slow=std::find_if(fx.begin(),fx.end(),[](const auto& e){return e.name=="SwordSelfSlowCharge";});if(slow==fx.end())throw std::runtime_error("Original SlowCharge absent");
  hades::WeaponSelfEffect admission;hades::WeaponEffectRuntimeData runtime;
  if(!hades::projectWeaponEffectAdmission(*slow,123,admission,error)||!hades::projectWeaponEffectRuntimeData(*slow,123,runtime,error)||!runtime.speed||admission.name!="SwordSelfSlowCharge"||admission.trigger!=3||admission.procChance!=1||!admission.active||admission.requirePerfectCharge||runtime.base.duration!=0.25f||runtime.speed->modifier!=0.35f||runtime.speed->changeType!=1||runtime.speed->stacks||runtime.statusPrototype.handle||runtime.statusPrototype.unitId!=-1||!runtime.base.soundName.empty()||runtime.update.duration!=0.25f)throw std::runtime_error("Source SlowCharge compiled/default/postread projection failed: "+error);
  std::cout<<"NATIVE_EFFECT_DATA_DEBUG_OK SlowCharge_duration=0.25 modifier=0.35 changeType=Multiply no_actor_admission_fabricated=true\n";
 }
 hades::WeaponArsenal a(73,"_PlayerUnit",c),other(74,"_PlayerUnit",c);
 if(c->unitLoadouts.size()!=1)throw std::runtime_error("Missing original startup loadout");
 for(const auto& [slot,name]:c->unitLoadouts[0].explicitInitialSlots){std::cout<<"STARTUP slot="<<slot<<" name="<<name<<'\n';if(!name.empty()&&!a.equip(name,error))throw std::runtime_error(error);}
 for(const auto* n:{"ArmorBreakAttack","RushWeapon","RangedWeapon","SwordWeapon","SwordParry"})if(!a.equip(n,error))throw std::runtime_error(error);
 const auto before=a.dump();if(a.snapshot().size()!=12)throw std::runtime_error("Incorrect native dependency closure");
 if(!a.equip("SwordWeapon",error)||a.dump()!=before)throw std::runtime_error("Duplicate mutated state");
 auto unsupported=*c->weapon("SwordWeapon");unsupported.name="FailureAfterParent";unsupported.bundled[0]="UnknownOriginalDefinition";
 if(a.equip(unsupported,error)||a.dump()!=before)throw std::runtime_error("Nontransactional missing dependency");
 if(!other.equip("RangedWeapon",error)||other.snapshot().size()!=1||other.snapshot()[0].ammo!=1)throw std::runtime_error("Owners not independent");
 if(a.spendAmmo("RangedWeapon",error))throw std::runtime_error("Unimplemented OnOutOfAmmo incorrectly succeeded");
 if(c->weapon("SwordWeaponDash")){if(!a.equip("SwordWeaponDash",error)||!a.equip("SwordWeaponWave",error)||a.snapshot().size()!=14)throw std::runtime_error("Original linked sword setup failed: "+error);}
 std::vector<hades::WeaponOutput> depleted;
 if(!other.spendAmmo("RangedWeapon",depleted,[](const auto& e,std::string&){return e.kind==hades::WeaponOutputKind::ScriptCallback&&e.name=="OnOutOfAmmo"&&e.weaponName=="RangedWeapon"&&e.ownerId==74;},error)||depleted.size()!=1||other.hasAmmo("RangedWeapon",error))throw std::runtime_error("Finite ammo callback/output state failed");
 hades::WeaponOwnerFrame owner;owner.ownerId=73;owner.exists=true;owner.alive=true;owner.enabled=true;owner.canAttack=true;owner.canAttackAllowCancellation=true;owner.inputAllowed=false;std::vector<hades::WeaponOutput> events;
 hades::WeaponPoint origin;owner.x=100;owner.y=200;
 if(!a.fireOrigin("RangedWeapon",owner,0,origin,error)||origin.x!=132||origin.y!=200)throw std::runtime_error("Original ranged barrel origin failed");
 if(!a.fireOrigin("SwordWeapon",owner,0,origin,error)||origin.x!=100||origin.y!=200)throw std::runtime_error("Original sword no-barrel origin failed");
 const double tape[]{0.5,0.5};hades::WeaponVolleyPlan volley;
 if(!a.planGunProjectiles("RangedWeapon",owner,0,tape,volley,error)||volley.randomValuesUsed!=2||volley.outputs.size()!=1||volley.outputs[0].kind!=hades::WeaponOutputKind::ProjectileSpawn||volley.outputs[0].x!=132||!volley.outputs[0].projectile||volley.outputs[0].projectile->name!="RangedWeapon")throw std::runtime_error("Original cast projectile plan failed: "+error);
 if(!a.planGunProjectiles("SwordWeapon",owner,0,tape,volley,error)||volley.outputs.size()!=2||volley.outputs[0].kind!=hades::WeaponOutputKind::CheckInvulnerableBarrelImpact||volley.outputs[0].x!=164||volley.outputs[0].value!=300)throw std::runtime_error("Original sword impact query/spawn layout failed: "+error);
 const double edgeTape[]{0.5,0.0};if(!a.planGunProjectiles("RangedWeapon",owner,0,edgeTape,volley,error)||volley.outputs.size()!=2)throw std::runtime_error("Native <= bonus zero boundary lost");
 if(a.planGunProjectiles("RangedWeapon",owner,0,{},volley,error)||volley.outputs.size()!=2)throw std::runtime_error("RNG failure changed projectile plan");
 if(a.canFire("RangedWeapon",owner,error)||!error.empty()||!a.canFire("SwordWeapon",owner,error)||!a.canFire("RushWeapon",owner,error))throw std::runtime_error("Native pre-tick canFire gates mismatch");
 if(!a.step(0.25f,owner,{},events,{},error)||!events.empty())throw std::runtime_error("Blocked-input timers failed: "+error);
 for(const auto& w:a.snapshot())if(w.definition.name=="RangedWeapon"&&!(w.cooldownTimeRemaining<0))throw std::runtime_error("Native cooldown negative overshoot lost");
 if(!a.canFire("RangedWeapon",owner,error))throw std::runtime_error("Native post-cooldown canFire mismatch");
 const auto timed=a.dump();owner.inputAllowed=true;hades::WeaponTrigger down;down.nativeControlIndex=21;down.down=true;
 if(a.step(0.1f,owner,{&down,1},events,{},error)||a.dump()!=timed)throw std::runtime_error("Unsupported active frame partly committed");
 hades::WeaponScalar queried;
 if(!a.setProperty("SwordWeapon","Enabled",false,error)||a.canFire("SwordWeapon",owner,error)||!a.dataProperty("SwordWeapon","Enabled",queried,error)||std::get<bool>(queried))throw std::runtime_error("Per-instance native Enabled setter failed");
 if(!std::get<bool>(c->weapon("SwordWeapon")->fields.at("Enabled")))throw std::runtime_error("Property mutation changed source catalog");
 if(!a.setProperty("SwordWeapon","MaxAmmo",3.0,error)||!a.stateProperty("SwordWeapon","Ammo",queried,error)||std::get<double>(queried)!=3)throw std::runtime_error("Native MaxAmmo reset failed");
 if(!a.setProperty("SwordWeapon","ClipSize",2.0,error)||!a.stateProperty("SwordWeapon","ClipShotsRemaining",queried,error)||std::get<double>(queried)!=2)throw std::runtime_error("Native ClipSize reset failed");
 if(a.setProperty("SwordWeapon","Enabled",1.0,error))throw std::runtime_error("Wrong reflected field type accepted");
 if(!a.setScriptProperty("SwordWeapon","Enabled",std::string{"TRUE"},hades::WeaponValueChange::Absolute,true,error)||!a.dataProperty("SwordWeapon","Enabled",queried,error)||!std::get<bool>(queried))throw std::runtime_error("Native bool string conversion failed");
 if(!a.setScriptProperty("SwordWeapon","FireGraphic",std::string{"null"},hades::WeaponValueChange::Absolute,true,error)||!a.dataProperty("SwordWeapon","FireGraphic",queried,error)||!std::get<std::string>(queried).empty())throw std::runtime_error("Data literal-null clearing failed");
 if(!a.setScriptProperty("SwordWeapon","MaxAmmo",2.75,hades::WeaponValueChange::Absolute,true,error)||!a.stateProperty("SwordWeapon","Ammo",queried,error)||std::get<double>(queried)!=2)throw std::runtime_error("Native numeric absolute truncation failed");
 if(!a.setScriptProperty("SwordWeapon","MaxAmmo",1.75,hades::WeaponValueChange::Multiply,true,error)||!a.stateProperty("SwordWeapon","Ammo",queried,error)||std::get<double>(queried)!=4)throw std::runtime_error("Native integer multiply roundf failed");
 if(!a.setScriptProperty("SwordWeapon","MaxAmmo",-1.75,hades::WeaponValueChange::Add,true,error)||!a.stateProperty("SwordWeapon","Ammo",queried,error)||std::get<double>(queried)!=3)throw std::runtime_error("Native integer Add truncation failed");
 if(!a.setScriptProperty("SwordWeapon","MaxAmmo",2.0,hades::WeaponValueChange::MultiplyBase,true,error)||!a.stateProperty("SwordWeapon","Ammo",queried,error)||std::get<double>(queried)!=3)throw std::runtime_error("Generic GameData missing numeric base behavior failed");
 if(!a.setScriptProperty("SwordWeapon","Ammo",7.25,hades::WeaponValueChange::Absolute,false,error)||!a.stateProperty("SwordWeapon","Ammo",queried,error)||std::get<double>(queried)!=7)throw std::runtime_error("Native live property setter failed");
 if(a.setScriptProperty("SwordWeapon","Ammo",std::string{"null"},hades::WeaponValueChange::Absolute,false,error)||!a.stateProperty("SwordWeapon","Ammo",queried,error)||std::get<double>(queried)!=7)throw std::runtime_error("Live literal-null incorrectly normalized or mutated");
 hades::WeaponArsenal chargingOwner(73,"_PlayerUnit",c);if(!chargingOwner.equip("SwordWeapon",error))throw std::runtime_error(error);
 hades::WeaponExecution charge(chargingOwner,{"SwordWeapon",1.0f/60,0,false});hades::WeaponExecutionStep chargeStep;
 if(charge.advance(owner,{},chargeStep,error)||chargingOwner.snapshot()[0].chargeTimeRemaining!=c->weapon("SwordWeapon")->chargeTime)throw std::runtime_error("Unsupported charge prefix committed timer");
 std::vector<std::string> chargeOps;
 for(int i=0;i<20;++i){
  if(!charge.advance(owner,[](const auto&,std::string&){return true;},chargeStep,error))throw std::runtime_error("Native charge cursor failed: "+error);
  if(chargeStep.progress==hades::WeaponExecutionProgress::ChargeFinished){if(chargeStep.chargeReady)throw std::runtime_error("Partial source sword charge fired early");break;}
  if(chargeStep.progress!=hades::WeaponExecutionProgress::Yielded)throw std::runtime_error("Unexpected charge progress");
  chargeOps.push_back(chargeStep.operation.name);
  if(chargeStep.operation.kind==hades::WeaponOutputKind::ScriptCallback){if(!chargingOwner.setProperty("SwordWeapon","Enabled",false,error))throw std::runtime_error(error);}
 }
 const std::vector<std::string> expectedChargeOps{"ZagreusSwordCharge1","null","OnWeaponCharging","OnWeaponChargingSoundChance","charge","Charging"};
 if(chargeOps!=expectedChargeOps||!chargingOwner.snapshot()[0].charging||std::get<bool>(chargingOwner.snapshot()[0].definition.fields.at("Enabled")))throw std::runtime_error("Native synchronous charge callback order/mutation lost");
 hades::WeaponArsenal combo(73,"_PlayerUnit",c);if(!combo.equip("SwordWeapon",error))throw std::runtime_error(error);
 hades::WeaponFireRequest comboRequest;comboRequest.angle=0.75f;comboRequest.targetX=5;comboRequest.targetY=7;comboRequest.fromX=12;
 if(!combo.requestFire("SwordWeapon",comboRequest,owner,error)||!combo.swapWeaponControl("SwordWeapon","SwordWeapon2",owner,error))throw std::runtime_error("Native control transfer failed: "+error);
 auto controls=combo.controllableWeapons();if(controls.size()!=1||controls[0]!="SwordWeapon2")throw std::runtime_error("Native swap remove/append membership failed");
 bool checkedTransfer=false;for(const auto& w:combo.snapshot())if(w.definition.name=="SwordWeapon2"){
  checkedTransfer=w.currentFireRequest&&w.currentFireRequest->angle==0.75f&&w.currentFireRequest->fromX==0&&w.currentFireRequest->targetX==5&&w.currentFireRequest->applySelfEffects&&w.requireControlRelease&&w.gainedControlFrom=="SwordWeapon";
 }
 if(!checkedTransfer)throw std::runtime_error("Native combo request transfer or release state lost");
 if(combo.requestFire("SwordWeapon2",comboRequest,owner,error)||!error.empty())throw std::runtime_error("Native require-control-release gate lost");
 hades::WeaponArsenal cast(73,"_PlayerUnit",c);if(!cast.equip("RangedWeapon",error))throw std::runtime_error(error);
 hades::WeaponFirePrefix prefix(cast,"RangedWeapon",{});hades::WeaponFirePrefixStep fireStep;std::vector<std::string> prefixOps;owner.worldTime=77;
 for(int i=0;i<20;++i){
  if(!prefix.advance(owner,[](const auto&,std::string&){return true;},fireStep,error))throw std::runtime_error("Native common Fire cursor failed: "+error);
  if(fireStep.progress==hades::WeaponFirePrefixProgress::CommonFireFinished)break;
  prefixOps.push_back(fireStep.operation.name);
 }
 const std::vector<std::string> expectedPrefix{"ZagreusRangedWeapon_Fire","Fire","Weapon::ApplyPhysics","OnOutOfAmmo","fire","fire"};
 if(prefixOps!=expectedPrefix||cast.snapshot()[0].ammo!=0||cast.snapshot()[0].lastFireTime!=77)throw std::runtime_error("Native Fire prefix ordering/ammo/time failed");
 hades::WeaponArsenal gunOwner(73,"_PlayerUnit",c);if(!gunOwner.equip("SwordWeapon",error))throw std::runtime_error(error);
 hades::WeaponGunExecution gun(gunOwner,"SwordWeapon",{});hades::WeaponGunStep gs;hades::WeaponGunReply gr;
 if(gun.advance(owner,{}, {},gs,error)||gunOwner.weaponState("SwordWeapon",error)->volley!=0)throw std::runtime_error("Unsupported Gun RNG consumer committed volley");
 std::vector<std::string> gunOps;int gunDraws=0,created=0;bool gunFinished=false;
 for(int i=0;i<30;++i){
  if(!gun.advance(owner,gr,[](const auto&,std::string&){return true;},gs,error))throw std::runtime_error("Native GUN continuation "+error);gr={};
  if(gs.progress==hades::WeaponGunProgress::GunBodyFinished){if(!gs.nativeVirtualFireResult)throw std::runtime_error("Source Sword native virtual Fire false");gunFinished=true;break;}
  gunOps.push_back(gs.operation.name);
  if(gs.operation.kind==hades::WeaponOutputKind::RequestRandomDouble){++gunDraws;gr.randomDouble=0.5;}
  else if(gs.operation.kind==hades::WeaponOutputKind::ProjectileSpawn){++created;gr.createdProjectile=0x100000044;}
  else if(gs.operation.kind==hades::WeaponOutputKind::FinalizeGunProjectile){if(gs.operation.projectileLifetimeHandle!=0x100000044||gs.operation.waveIndex!=0||gs.operation.blastRadiusMultiplier!=1)throw std::runtime_error("Gun post-create adjustment handle/state lost");}
 }
 const auto* gunState=gunOwner.weaponState("SwordWeapon",error);
 if(!gunFinished||gunDraws!=2||created!=1||gunState->volley!=1||gunState->volleyHitTimes[1].size()!=1||gunState->volleyFirstHitId[1]!=-1||gunOps!=std::vector<std::string>{"Gun wave spread","native barrel obstacle query/force","Gun bonus projectile chance","SwordWeapon","native post-create wave/crit/damage/blast"})throw std::runtime_error("Native resumable Gun RNG/spawn/volley order failed");
 hades::WeaponFireEffects fireEffects(gunOwner,"SwordWeapon",{});hades::WeaponFireEffectsStep fs;std::optional<uint64_t> animationReply;
 std::vector<hades::WeaponOutputKind> fxOps;bool fxFinished=false;
 for(int i=0;i<15;++i){
  if(!fireEffects.advance(owner,animationReply,[](const auto&,std::string&){return true;},fs,error))throw std::runtime_error("Native trigger FX continuation: "+error);animationReply.reset();
  if(fs.progress==hades::WeaponFireEffectsProgress::BeforePostFire){fxFinished=true;break;}
  fxOps.push_back(fs.operation.kind);
  if(fs.operation.kind==hades::WeaponOutputKind::CreateWeaponWorldAnimation){
   animationReply=fs.operation.name.empty()?0:0x100000066;
   if(!fs.operation.name.empty()&&!gunOwner.setScriptProjectileProperty("SwordWeapon","DamageRadius",50.0,hades::WeaponValueChange::Absolute,error))throw std::runtime_error(error);
  }else if(fs.operation.kind==hades::WeaponOutputKind::SetWeaponWorldAnimationRadius){
   const auto* current=gunOwner.projectileData("SwordWeapon",false,error);const float scale=static_cast<float>(std::get<double>(current->fields.at("DamageRadiusScaleX")));
   if(fs.operation.animationLifetimeHandle!=0x100000066||fs.operation.value!=50*scale)throw std::runtime_error("FireFx failed to reacquire current per-instance radius after creation callback");
  }
 }
 if(!fxFinished||fxOps!=std::vector<hades::WeaponOutputKind>{hades::WeaponOutputKind::CreateWeaponWorldAnimation,hades::WeaponOutputKind::SetWeaponWorldAnimationRadius,hades::WeaponOutputKind::CreateWeaponWorldAnimation})throw std::runtime_error("Native trigger FireFx/FireFx2/radius order mismatch");
 hades::WeaponArsenal orderedOwner(73,"_PlayerUnit",c);if(!orderedOwner.equip("RangedWeapon",error))throw std::runtime_error(error);
 hades::WeaponArsenalExecution arsenalFrame(orderedOwner,0.02f);hades::WeaponArsenalFrame af;af.owner=owner;af.continuousWeapon="RangedWeapon";af.priorityWeapon="RangedWeapon";
 hades::WeaponArsenalStep as;
 if(!arsenalFrame.advance(af,false,as,error)||as.progress!=hades::WeaponArsenalProgress::WeaponUpdateRequired||as.weaponName!="RangedWeapon"||!as.priority||!orderedOwner.weaponState("RangedWeapon",error)->currentFireRequest)throw std::runtime_error("Native Arsenal continuous/priority prepass failed: "+error);
 if(arsenalFrame.advance(af,false,as,error))throw std::runtime_error("Arsenal advanced without full child-update acknowledgement");
 if(!orderedOwner.equip("SwordWeapon",error))throw std::runtime_error(error);
 std::vector<std::string> expectedOrder;for(const auto& w:orderedOwner.snapshot())if(w.definition.name!="RangedWeapon")expectedOrder.push_back(w.definition.name);
 std::vector<std::string> ordinaryOrder;bool outerFinished=false;
 for(int i=0;i<30;++i){
  if(!arsenalFrame.advance(af,true,as,error))throw std::runtime_error("Native Arsenal order cursor: "+error);
  if(as.progress==hades::WeaponArsenalProgress::FrameFinished){outerFinished=true;break;}
  ordinaryOrder.push_back(as.weaponName);
  if(ordinaryOrder.size()==1&&!orderedOwner.equip("SwordWeaponDash",error))throw std::runtime_error(error);
 }
 if(!outerFinished||ordinaryOrder!=expectedOrder||std::find(ordinaryOrder.begin(),ordinaryOrder.end(),"SwordWeaponDash")!=ordinaryOrder.end())throw std::runtime_error("Native Arsenal captured-count iteration includes newly appended ordinary callback weapon");
 if(!chargingOwner.setProperty("SwordWeapon","Enabled",true,error)||!chargingOwner.requestFire("SwordWeapon",{},owner,error))throw std::runtime_error("Prepare genuine charge cancellation state: "+error);
 hades::WeaponCancelCharge cancel(chargingOwner,"SwordWeapon");hades::WeaponCancelChargeStep cs;
 const auto chargeBeforeCancel=chargingOwner.weaponState("SwordWeapon",error)->chargeTimeRemaining;
 if(cancel.advance(owner,{},cs,error)||chargingOwner.weaponState("SwordWeapon",error)->chargeTimeRemaining!=chargeBeforeCancel)throw std::runtime_error("Unsupported charge-cancel effects consumer committed reset");
 std::vector<std::string> cancelOps;bool cancelFinished=false;
 for(int i=0;i<12;++i){
  if(!cancel.advance(owner,[](const auto&,std::string&){return true;},cs,error))throw std::runtime_error("Native charge cancellation: "+error);
  if(cs.progress==hades::WeaponCancelChargeProgress::Finished){cancelFinished=true;break;}
  cancelOps.push_back(cs.operation.name);const auto* state=chargingOwner.weaponState("SwordWeapon",error);
  if(state->currentFireRequest||state->chargeTimeRemaining!=state->definition.chargeTime)throw std::runtime_error("Native charge reset must precede synchronous effect clear");
  if(cs.operation.kind==hades::WeaponOutputKind::CancelWeaponSelfEffects&&!state->charging)throw std::runtime_error("Native charging cleared before self-effect cancellation callback");
  if(cs.operation.kind==hades::WeaponOutputKind::ScriptCallback&&state->charging)throw std::runtime_error("Native OnWeaponChargeCanceled must observe charging=false");
 }
 if(!cancelFinished||cancelOps!=std::vector<std::string>{"Charging","OnWeaponChargeCanceled"})throw std::runtime_error("Native charge-cancel effect/callback ordering mismatch");
 hades::WeaponArsenal idleOwner(73,"_PlayerUnit",c);if(!idleOwner.equip("RangedWeapon",error))throw std::runtime_error(error);
 hades::WeaponUpdateExecution idleUpdate(idleOwner,"RangedWeapon",0.5f);hades::WeaponUpdateStep us;
 const float idleBefore=idleOwner.weaponState("RangedWeapon",error)->cooldownTimeRemaining;
 if(!idleUpdate.advance(owner,{}, {},us,error)||us.progress!=hades::WeaponUpdateProgress::FrameFinished||idleOwner.weaponState("RangedWeapon",error)->cooldownTimeRemaining!=idleBefore-0.5f||idleOwner.weaponState("RangedWeapon",error)->previousFireRequest)throw std::runtime_error("Native idle per-weapon frame did not advance exact cooldown overshoot");
 hades::WeaponArsenal requestingOwner(73,"_PlayerUnit",c);if(!requestingOwner.equip("SwordWeapon",error)||!requestingOwner.requestFire("SwordWeapon",{},owner,error))throw std::runtime_error(error);
 hades::WeaponUpdateExecution requesting(requestingOwner,"SwordWeapon",0.02f);
 if(!requesting.advance(owner,{},[](const auto&,std::string&){return true;},us,error)||us.progress!=hades::WeaponUpdateProgress::ChildRequired||us.child!=hades::WeaponUpdateChild::HandleTriggerDown)throw std::runtime_error("Native weapon update bypassed actual trigger child");
 if(requesting.advance(owner,{},[](const auto&,std::string&){return true;},us,error))throw std::runtime_error("Native per-weapon frame accepted absent trigger result");
 if(!requestingOwner.clearFireRequest("SwordWeapon",error)||requestingOwner.weaponState("SwordWeapon",error)->currentFireRequest)throw std::runtime_error("Explicit input producer request-clear failed");
 hades::WeaponArsenal postOwner(73,"_PlayerUnit",c);if(!postOwner.equip("SwordWeapon",error)||!postOwner.setScriptProperty("SwordWeapon","Charging",true,hades::WeaponValueChange::Absolute,false,error)||!postOwner.setScriptProperty("SwordWeapon","ChargeTimeRemaining",0.0,hades::WeaponValueChange::Absolute,false,error))throw std::runtime_error(error);
 hades::WeaponFireRequest pr;pr.targetX=12;pr.targetY=34;
 if(!postOwner.requestFire("SwordWeapon",pr,owner,error))throw std::runtime_error(error);
 hades::WeaponPostFire post(postOwner,"SwordWeapon");hades::WeaponPostFireStep pfs;std::vector<std::string> postOps;bool postFinished=false;
 for(int i=0;i<40;++i){
  if(!post.advance(owner,{},[](const auto&,std::string&){return true;},pfs,error))throw std::runtime_error("Native first sword PostFire: "+error);
  if(pfs.progress==hades::WeaponPostFireProgress::Finished){postFinished=true;break;}
  postOps.push_back(pfs.operation.name);const auto* state=postOwner.weaponState("SwordWeapon",error);
  if(pfs.operation.name=="OnWeaponFired"&&(!state->charging||!state->currentFireRequest||!pfs.operation.hasTargetLocation||pfs.operation.x!=12||pfs.operation.y!=34))throw std::runtime_error("OnWeaponFired lost original pre-cancel state/target local");
 }
 const auto* postState=postOwner.weaponState("SwordWeapon",error);const auto postControls=postOwner.controllableWeapons();
 if(!postFinished||postState->charging||postState->currentFireRequest||postState->clipShotsRemaining!=1||postState->chargeTimeRemaining!=postState->definition.chargeTime||std::find(postControls.begin(),postControls.end(),"SwordWeapon")!=postControls.end()||std::find(postControls.begin(),postControls.end(),"SwordWeapon2")==postControls.end()||postOps!=std::vector<std::string>{"PostFire","","","","OnWeaponFired","Charging","OnWeaponChargeCanceled"})throw std::runtime_error("Native PostFire reload/callback/cancel/combo-swap sequence mismatch");
 hades::WeaponArsenal triggerOwner(73,"_PlayerUnit",c);if(!triggerOwner.equip("SwordWeapon",error)||!triggerOwner.requestFire("SwordWeapon",{},owner,error))throw std::runtime_error(error);
 hades::WeaponTriggerExecution partialTrigger(triggerOwner,"SwordWeapon",0.02f,{});hades::WeaponTriggerStep tstep;bool partialFinished=false;
 for(int i=0;i<30;++i){if(!partialTrigger.advance(owner,{},[](const auto&,std::string&){return true;},tstep,error))throw std::runtime_error("Composed native partial trigger: "+error);if(tstep.progress==hades::WeaponTriggerProgress::Finished){if(tstep.nativeResult)throw std::runtime_error("Partial sword charge claimed fired");partialFinished=true;break;}}
 if(!partialFinished||!triggerOwner.weaponState("SwordWeapon",error)->currentFireRequest)throw std::runtime_error("Partial native trigger lost persistent quick-tap request");
 hades::WeaponTriggerExecution fullTrigger(triggerOwner,"SwordWeapon",0.1f,{});hades::WeaponTriggerReply tr;bool triggerFinished=false;int triggerProjectiles=0,triggerRandoms=0;
 for(int i=0;i<80;++i){
  if(!fullTrigger.advance(owner,tr,[](const auto&,std::string&){return true;},tstep,error))throw std::runtime_error("Composed native first Sword trigger: "+error);tr={};
  if(tstep.progress==hades::WeaponTriggerProgress::Finished){if(!tstep.nativeResult)throw std::runtime_error("Complete Sword trigger native result false");triggerFinished=true;break;}
  if(tstep.expectsRandomReply){tr.randomDouble=0.5;++triggerRandoms;}
  else if(tstep.expectsProjectileReply){tr.createdProjectile=0x100000088;++triggerProjectiles;}
  else if(tstep.expectsAnimationReply)tr.createdAnimation=tstep.operation.name.empty()?0:0x100000099;
 }
 if(!triggerFinished||triggerRandoms!=2||triggerProjectiles!=1||triggerOwner.weaponState("SwordWeapon",error)->currentFireRequest||triggerOwner.weaponState("SwordWeapon",error)->charging)throw std::runtime_error("Composed native trigger did not complete real children/first combo state");
 hades::WeaponTriggerRelease release(triggerOwner,"SwordWeapon",0.02f,{});std::vector<std::string> releaseOps;bool releaseFinished=false;
 for(int i=0;i<10;++i){if(!release.advance(owner,[](const auto&,std::string&){return true;},tstep,error))throw std::runtime_error(error);if(tstep.progress==hades::WeaponTriggerProgress::Finished){releaseFinished=true;break;}releaseOps.push_back(tstep.operation.name);}
 if(!releaseFinished||releaseOps!=std::vector<std::string>{"OnWeaponTriggerRelease"})throw std::runtime_error("Original Sword release callback path mismatch");
 std::cout<<"NATIVE_TRIGGER_COMPOSITION_DEBUG_OK partial_request_retained=true projectile_consumer_barrier=true combo_swapped=true release_callback=true gameplay_observed=false\n";
 hades::WeaponArsenal frameOwner(73,"_PlayerUnit",c);if(!frameOwner.equip("SwordWeapon",error)||!frameOwner.requestFire("SwordWeapon",{},owner,error))throw std::runtime_error(error);
 hades::WeaponArsenalFrame nativeFrame;nativeFrame.owner=owner;nativeFrame.priorityWeapon="SwordWeapon";
 int frameProjectiles=0;bool frameOneFinished=false;
 for(float delta:{0.02f,0.1f}){
  hades::WeaponArsenalFrameExecution composedFrame(frameOwner,delta);hades::WeaponArsenalFrameStep step;hades::WeaponTriggerReply response;bool done=false;
  for(int i=0;i<200;++i){if(!composedFrame.advance(nativeFrame,response,[](const auto&,std::string&){return true;},step,error))throw std::runtime_error("Composed actual Arsenal frame: "+error);response={};
   if(step.progress==hades::WeaponArsenalFrameProgress::FrameFinished){done=true;break;}
   if(step.expectsRandomReply)response.randomDouble=0.5;
   else if(step.expectsProjectileReply){response.createdProjectile=0x1000000aa;++frameProjectiles;}
   else if(step.expectsAnimationReply)response.createdAnimation=step.operation.name.empty()?0:0x1000000bb;
  }
  if(!done)throw std::runtime_error("Actual Arsenal frame composition did not finish");
  if(!frameOneFinished){if(!frameOwner.weaponState("SwordWeapon",error)->currentFireRequest||!frameOwner.weaponState("SwordWeapon",error)->previousFireRequest)throw std::runtime_error("Partial native Arsenal frame failed request finalization");frameOneFinished=true;}
 }
 if(frameProjectiles!=1||frameOwner.weaponState("SwordWeapon",error)->currentFireRequest)throw std::runtime_error("Composed native Arsenal frame did not complete exactly one real construction barrier");
 std::cout<<"NATIVE_ARSENAL_FRAME_COMPOSITION_DEBUG_OK partial_and_fire_frames=true independent_child_acknowledgments=false gameplay_observed=false\n";
 hades::WeaponArsenal reloadOwner(73,"_PlayerUnit",c);if(!reloadOwner.equip("SwordWeapon",error)||!reloadOwner.setScriptProperty("SwordWeapon","ClipShotsRemaining",0.0,hades::WeaponValueChange::Absolute,false,error))throw std::runtime_error(error);
 hades::WeaponReloadExecution immediateReload(reloadOwner,"SwordWeapon");hades::WeaponReloadStep rs;
 if(!immediateReload.advance(owner,{},[](const auto&,std::string&){return true;},rs,error)||rs.progress!=hades::WeaponReloadProgress::Yielded||rs.operation.kind!=hades::WeaponOutputKind::SetOwnerAnimation||!rs.operation.name.empty()||reloadOwner.weaponState("SwordWeapon",error)->clipShotsRemaining!=1)throw std::runtime_error("Native immediate reload must refill and attempt even empty finish animation: "+error);
 if(!immediateReload.advance(owner,{}, {},rs,error)||rs.progress!=hades::WeaponReloadProgress::Finished)throw std::runtime_error("Native immediate reload did not finish");
 if(!reloadOwner.setProperty("SwordWeapon","ReloadTimeMin",0.1,error)||!reloadOwner.setProperty("SwordWeapon","ReloadTimeMax",0.2,error))throw std::runtime_error(error);
 hades::WeaponReloadExecution randomReload(reloadOwner,"SwordWeapon");
 if(!randomReload.advance(owner,{},[](const auto&,std::string&){return true;},rs,error)||!rs.expectsRandomReply||rs.operation.kind!=hades::WeaponOutputKind::RequestRandomDouble)throw std::runtime_error("Native positive ReloadTimeMin must request RNG");
 if(!randomReload.advance(owner,0.5,{},rs,error)||rs.progress!=hades::WeaponReloadProgress::Finished||std::abs(reloadOwner.weaponState("SwordWeapon",error)->reloadTimeRemaining-0.15f)>0.000001f)throw std::runtime_error("Native reload RNG float arithmetic/positive delay failed: "+error);
 for(const auto& d:c->weapons){hades::WeaponArsenal probe(200,d.name,c);const bool ok=probe.equip(d.name,error);std::cout<<"INITIALIZER_CAPABILITY name="<<d.name<<" supported="<<ok<<" detail="<<error<<'\n';}
 std::cout<<a.dump()<<"SOURCE_CATALOG_DEBUG_OK weapons="<<c->weapons.size()<<" projectiles="<<c->projectiles.size()<<" duplicate_idempotent=true transactional_rejection=true independent_owners=true native_property_mutation=true source_volley_plans=true native_fire_origin=true native_can_fire_gates=true finite_ammo_output=true blocked_input_timers=true transactional_frame_rejection=true fire_supported=false\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<"PLAYER_WEAPON_DEBUG_FAILURE "<<e.what()<<'\n';return 1;}}
