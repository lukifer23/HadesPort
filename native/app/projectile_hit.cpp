#include "projectile_world.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace hades {
namespace {
const SourceValue& prop(const ActorWorld& w,int32_t id,const std::string& key){const auto* p=w.currentProperty(id,key);if(!p)throw std::runtime_error("Actual TakeHit source property unavailable: "+key);return *p;}
const WeaponScalar& field(const std::map<std::string,WeaponScalar>& f,const char* key){const auto i=f.find(key);if(i==f.end())throw std::runtime_error(std::string("Actual TakeHit data field unavailable: ")+key);return i->second;}
float number(const std::map<std::string,WeaponScalar>& f,const char* key){const auto& v=field(f,key);if(!std::holds_alternative<double>(v)||!std::isfinite(std::get<double>(v)))throw std::runtime_error(std::string("Actual TakeHit finite scalar unavailable: ")+key);return float(std::get<double>(v));}
bool flag(const std::map<std::string,WeaponScalar>& f,const char* key){const auto& v=field(f,key);if(!std::holds_alternative<bool>(v))throw std::runtime_error(std::string("Actual TakeHit boolean unavailable: ")+key);return std::get<bool>(v);}
std::string text(const std::map<std::string,WeaponScalar>& f,const char* key){const auto& v=field(f,key);if(!std::holds_alternative<std::string>(v))throw std::runtime_error(std::string("Actual TakeHit name unavailable: ")+key);return std::get<std::string>(v);}
}
bool ProjectileWorld::applyArcImpact(uint64_t handle,int32_t victim,const ProjectileHitProviders& providers,std::string& error){
    bool mutated=false;
    try{
        if(!providers.damageMultiplier||!providers.damageTakenMultiplier||!providers.disableDamage||!std::isfinite(*providers.damageMultiplier)||!std::isfinite(*providers.damageTakenMultiplier))throw std::runtime_error("Impact requires actual current damage configuration");
        auto* p=actor(handle);if(!p||!p->active||!p->firingWorld||!p->firingArsenal)throw std::runtime_error("Impact lost actual ARC/World/weapon");
        auto& world=*p->firingWorld;auto* target=world.actor(victim);if(!target||!target->registered||!target->object)throw std::runtime_error("Impact lost actual victim");
        ProjectileImpactGeometry geometry;mutated=true;if(!prepareArcImpact(handle,victim,geometry,error))throw std::runtime_error(error);if(!geometry.nativeImpactAdmitted){error.clear();return true;}
        {auto* q=actor(handle);if(!q||!q->firingArsenal)throw std::runtime_error("Impact lost actual projectile");std::string detail;const auto* d=q->firingArsenal->projectileData(q->request.weaponName,q->spawnedDefinition,detail);if(!d)throw std::runtime_error(detail);
        // Impact71880 inserts the actual Entity ID before ApplyImpact76590 and
        // CalcDamage6dbd0 (including DamageBonusToImpactedUnit and its RNG).
        q->hitByImpact.push_back(target->entityHandle);
        if(number(d->fields,"DamageRadius")>0||!text(d->fields,"SpawnOnDetonate").empty())throw std::runtime_error("Impact requires genuine source detonation consumer");}
        if(!applyImpact(handle,victim,geometry,providers,error))throw std::runtime_error(error);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();if(mutated){faulted_=true;fault_=error;}return false;}
}
bool ProjectileWorld::applyDetonationImpact(uint64_t handle,int32_t victim,const ProjectileHitProviders& providers,std::string& error){
    // Projectile::UpdateDetonation71c10 obstacle/unit victim: ApplyImpact76590 directly (no
    // Impact71880 prelude). Its geometry is its own vslot0x198 GetImpactAngle6e7d0 and
    // vslot0x1c8 GetImpactLocation72a90 on the current mImpactLocation, and Z = attached ?
    // GetZLocation(mAttachedTo)+mAttachOffsetZ : mZLocation(+0x74).
    bool mutated=false;
    try{ProjectileImpactGeometry geometry;if(!impactGeometry(handle,victim,geometry,error))throw std::runtime_error(error);
        mutated=true;if(!applyImpact(handle,victim,geometry,providers,error))throw std::runtime_error(error);
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();if(mutated){faulted_=true;fault_=error;}return false;}
}
bool ProjectileWorld::applyImpact(uint64_t handle,int32_t victim,const ProjectileImpactGeometry& geometry,const ProjectileHitProviders& providers,std::string& error){
    // Projectile::ApplyImpact76590 (.local/reports/native-combat3-apply-impact-pseudocode.c).
    bool mutated=false;
    try{
        if(!providers.damageMultiplier||!providers.damageTakenMultiplier||!providers.disableDamage||!std::isfinite(*providers.damageMultiplier)||!std::isfinite(*providers.damageTakenMultiplier))throw std::runtime_error("Impact requires actual current damage configuration");
        auto* p=actor(handle);if(!p||!p->initializedLifetime()||!p->firingWorld||!p->firingArsenal)throw std::runtime_error("Impact lost actual projectile/World/weapon");
        auto& world=*p->firingWorld;auto* target=world.actor(victim);if(!target||!target->registered||!target->object)throw std::runtime_error("Impact lost actual victim");const uint64_t victimLifetime=target->entityHandle;
        auto weapon=[&]()->const WeaponSnapshot&{std::string detail;const auto* value=p->firingArsenal->weaponState(p->request.weaponName,detail);if(!value)throw std::runtime_error(detail);return *value;};
        auto data=[&]()->const ProjectileDefinition&{std::string detail;const auto* value=p->firingArsenal->projectileData(p->request.weaponName,p->spawnedDefinition,detail);if(!value)throw std::runtime_error(detail);return *value;};
        const auto invincibleCue=data().cues.find("InvincibleImpactSound");if(invincibleCue==data().cues.end()||!invincibleCue->second.validityKnown||invincibleCue->second.valid)throw std::runtime_error("Impact InvincibleImpactSound requires actual source cue consumer");
        ProjectileHitOperation op;op.projectile=handle;op.victim=victim;op.geometry=geometry;
        auto invoke=[&](ProjectileHitOperationKind kind,ProjectileHitReply& reply){op.kind=kind;if(!providers.execute)throw std::runtime_error("Actual native hit consumer unavailable");mutated=true;if(!providers.execute(op,reply,error))throw std::runtime_error(error.empty()?"Actual native hit consumer failed":error);};
        auto run=[&](ProjectileHitOperationKind kind){ProjectileHitReply reply;invoke(kind,reply);if(reply.tagMultiplierAddition)throw std::runtime_error("Unexpected scalar reply from native hit operation");};
        mutated=true;
        ProjectileCalculatedDamage calculated;if(!calcDamage(handle,victim,*providers.disableDamage,providers.collision,calculated,error))throw std::runtime_error(error);
        auto* attacker=world.actor(p->request.ownerId);if(!attacker||!attacker->registered||attacker->definition->kind!="Unit")attacker=nullptr;
        op.attacker=attacker?attacker->id:-1;
        float amount=calculated.amount;
        if(attacker&&prop(world,attacker->id,"PlayerControlled").boolean())amount*=*providers.damageMultiplier;
        const bool silent=flag(weapon().definition.fields,"SilentImpact")||(flag(weapon().definition.fields,"SilentImpactAfterFirst")&&!p->thingsHit.empty());
        if(target->lifeStatus==0&&!silent)run(ProjectileHitOperationKind::TakeHitPresentation);
        target=world.actor(victim);if(!target||!target->hasLife||!target->life.initialized)throw std::runtime_error("Native TakeHit requires actual initialized victim Life");
        if(target->life.isPlayerControlled)amount*=*providers.damageTakenMultiplier;
        // Directional block can return before OnHit; retain a real barrier until
        // its OnProjectileBlock/Effect.Remove consumers exist.
        if(amount>0&&target->life.invulnerableCoverage>0&&!flag(data().fields,"IgnoreCoverageAngles"))throw std::runtime_error("TakeHit directional invulnerability requires native block consumer");
        NativeHitArguments hit{};hit.victim={victim,target->object->name,target->definition->kind=="Obstacle",target->status&&target->status->isTraitor};
        hit.damage.isCrit=calculated.critical;hit.damage.totalDamageMultipliers=target->life.damageTakenMultiplier;
        if(attacker&&target->life.isObstacle){const float weaponObstacle=number(weapon().definition.fields,"ObstacleDamageMultiplier");hit.damage.hitObstacle=true;hit.damage.obstacleMultiplier=weaponObstacle;hit.damage.totalDamageMultipliers+=weaponObstacle-1;
            const float ownerObstacle=float(prop(world,attacker->id,"ObstacleDamageMultiplier").number());hit.damage.totalDamageMultipliers+=ownerObstacle-1;hit.damage.obstacleMultiplier*=ownerObstacle;}
        bool primaryInvulnerable=false,noArmor=false,noCoverage=false;
        if(!providers.primaryFlags||!providers.primaryFlags(victim,primaryInvulnerable,noArmor,noCoverage,error))throw std::runtime_error(error.empty()?"Native hit primary properties unavailable":error);
        if(target->life.armorCoverage<6.283185482025146484375f)throw std::runtime_error("TakeHit partial armor requires source coverage and vulnerability computation");
        hit.isInvulnerable=primaryInvulnerable||target->invulnerable;hit.invulnerableFromCoverage=false;
        hit.impactAngleRadians=geometry.angle;hit.impactLocationZ=geometry.z;hit.distanceSquared=-1;
        if(attacker&&(geometry.x!=0||geometry.y!=0)){float x=0,y=0;if(!world.location(attacker->id,x,y,error))throw std::runtime_error(error);hit.distanceSquared=(geometry.x-x)*(geometry.x-x)+(geometry.y-y)*(geometry.y-y);}
        if(attacker)hit.attacker=HitActorIdentity{attacker->id,attacker->object->name,false,attacker->status&&attacker->status->isTraitor};
        // Weapon::DealsDamage84ed0 (.local/reports/native-weapons-deals-damage-pseudocode.c):
        // ProjectileData (Weapon+0x120 valid, +0x68) Damage/DamageLow/DamageHigh/
        // DamagePerConsecutiveHit (data+0x2dc/2e0/2e4/2f0) > 0, or spawned ProjectileData
        // (+0x121, +0x70) DamageLow/DamageHigh/DamagePerConsecutiveHit > 0; otherwise any
        // mEffects (data+0x720, stride 0x418) entry of EffectData type 4 or 5 (+0x68).
        // Weapon+0x121/+0x70 = validSpawnedProjectileData/spawnedProjectileData (adjacent-member mapping).
        const auto& wd=weapon();bool dealsDamage=false;
        if(wd.validProjectileData){const auto& f=wd.projectileData.fields;dealsDamage=number(f,"Damage")>0||number(f,"DamageLow")>0||number(f,"DamageHigh")>0||number(f,"DamagePerConsecutiveHit")>0;}
        if(!dealsDamage&&wd.validSpawnedProjectileData){const auto& f=wd.spawnedProjectileData.fields;dealsDamage=number(f,"DamageLow")>0||number(f,"DamageHigh")>0||number(f,"DamagePerConsecutiveHit")>0;}
        if(!dealsDamage&&wd.validProjectileData)for(const auto& e:wd.projectileData.effects)if(e.type==4||e.type==5){dealsDamage=true;break;}
        if(!dealsDamage&&wd.validSpawnedProjectileData)for(const auto& e:wd.spawnedProjectileData.effects)if(e.type==4||e.type==5){dealsDamage=true;break;}
        hit.weapon=HitWeaponArguments{weapon().definition.name,weapon().ownerId,weapon().ownerName,dealsDamage,weapon().wasLastFirePerfectCharge};
        // Bounded ARC has not admitted Reflect, so its native mDeflected remains
        // constructor false. Reflect consumer must supply that live field first.
        // LifeComponent::TakeHit hit table: ImpactVelocity is ProjectileData+0x618
        // (ProjectileDataDef mImpactVelocity) only when the victim has Physics
        // (Thing+0xd8) and PhysicsComponent::IsImmuneToForce2bd4d0 is false; else 0.
        float impactVelocity=0;
        if(target->physics){bool immune=false;if(!providers.immuneToForce||!providers.immuneToForce(victim,immune,error))throw std::runtime_error(error.empty()?"OnHit ImpactVelocity requires actual IsImmuneToForce query":error);if(!immune)impactVelocity=number(data().fields,"ImpactVelocity");}
        target=world.actor(victim);if(!target||!target->hasLife)throw std::runtime_error("Native TakeHit victim expired during IsImmuneToForce");
        hit.projectile=HitProjectileArguments{data().name,false,p->state.blinkWeaponActive,p->state.additiveDamageMultiplier,impactVelocity,p->state.volleyIndex,p->state.fireIndex};
        const auto effectMultiplierName=attacker?prop(world,attacker->id,"EffectDamageMultiplierName").text():std::string{};
        if(!effectMultiplierName.empty()){
            int32_t remaining=int32_t(prop(world,attacker->id,"EffectDamageMultiplierMinRequired").number());
            for(const auto id:world.registrationOrder()){const auto* a=world.actor(id);if(a&&a->registered&&a->status&&a->status->getEffect(effectMultiplierName)&&--remaining<1)break;}
            hit.meetsEffectDamageMultiplier=remaining<1;
        }
        op.hit=hit;
        // EffectMultipliers is real attached Lua state, queried even when the
        // caller expects it empty. No fabricated default trait table.
        if(attacker&&target->definition->kind=="Unit"){
            ProjectileHitReply reply;invoke(ProjectileHitOperationKind::LookupTagMultipliers,reply);
            if(!reply.tagMultiplierAddition||!std::isfinite(*reply.tagMultiplierAddition))throw std::runtime_error("Native effect/trait multiplier lookup returned no finite addition");
            op.hit.damage.totalDamageMultipliers+=*reply.tagMultiplierAddition;
        }
        if(amount>0||flag(data().fields,"CanHitWithoutDamage")){
            if(calculated.critical){
                float criticalMultiplier=number(data().fields,"CriticalHitMultiplier");
                const auto* directOwner=world.actor(p->state.ownerId);if(directOwner&&directOwner->registered&&directOwner->definition->kind=="Unit")criticalMultiplier+=float(prop(world,directOwner->id,"CritMultiplierAddition").number());
                if(target->status)for(const auto h:target->status->snapshot().active){const WeaponEffectDefinition* effect=nullptr;if(!providers.collision.effectData||!providers.collision.effectData(h,effect,error)||!effect)throw std::runtime_error(error.empty()?"Critical multiplier needs actual victim EffectData":error);criticalMultiplier+=number(effect->fields,"CritMultiplierVulnerabilityAddition");}
                op.hit.damage.totalDamageMultipliers+=criticalMultiplier-1;
            }
            amount*=op.hit.damage.totalDamageMultipliers;amount+=float(p->state.damageAddition)+number(data().fields,"DamageAddition");
            op.hit.damage.actualDamage=flag(data().fields,"RoundDamageDown")?std::floor(amount):std::round(amount);
            run(ProjectileHitOperationKind::MarkDirty);
            if(!silent)run(ProjectileHitOperationKind::SetDamagedAnimation);
            if(!p->firingArsenal->recordProjectileHit(p->request.weaponName,p->state.volleyIndex,p->state.fireIndex,victim,target->definition->kind=="Unit",op.hit.isInvulnerable,providers.collision.worldTime,op.hit.firstInVolley,error))throw std::runtime_error(error);
            run(ProjectileHitOperationKind::OnHit);
            if(attacker)run(ProjectileHitOperationKind::CountAttackerEffects);
            target=world.actor(victim);if(target&&target->registered&&target->definition->kind=="Unit"&&!prop(world,victim,"OnHitWeapon").text().empty())run(ProjectileHitOperationKind::OnHitWeapon);
        }else{op.hit.damage.isCrit=false;op.hit.damage.actualDamage=amount;}
        target=world.actor(victim);if(target&&target->registered&&target->lifeStatus==0)run(ProjectileHitOperationKind::ApplyVictimEffects);
        if(!text(data().fields,"SpawnOnImpact").empty())run(ProjectileHitOperationKind::SpawnOnImpact);
        if(number(data().fields,"Heal")>0||number(data().fields,"LifeSteal")>0)throw std::runtime_error("Actual projectile healing/life-steal callback unavailable");
        run(ProjectileHitOperationKind::ImpactFx);
        if(op.hit.damage.actualDamage>0){
            if(flag(data().fields,"ClearWeaponBonusesOnHit")){p->state.critBonus=0;p->state.additiveDamageMultiplier=0;}
            if(calculated.critical)run(ProjectileHitOperationKind::ClearCritEffects);
            const auto inflicted=text(data().fields,"InflictedDamageFx");if(!inflicted.empty()&&inflicted!="null")run(ProjectileHitOperationKind::InflictedDamageFx);
            const auto cue=data().cues.find("InflictedDamageSound");if(cue==data().cues.end())throw std::runtime_error("Actual projectile InflictedDamageSound default/read semantics unavailable");
            if(!cue->second.validityKnown||cue->second.valid)run(ProjectileHitOperationKind::InflictedDamageSound);
        }
        target=world.actor(victim);
        if(target&&target->registered&&target->physics&&target->lifeStatus==0&&(number(data().fields,"ImpactVelocity")!=0||number(data().fields,"ImpactUpwardVelocity")!=0))run(ProjectileHitOperationKind::ApplyImpactForces);
        if(number(data().fields,"CharmDuration")>0&&target&&target->definition->kind=="Unit")run(ProjectileHitOperationKind::Charm);
        if(target&&target->hasLife&&!op.hit.isInvulnerable){
            const float window=number(data().fields,"ConsecutiveHitWindow");if(window>0){++target->life.consecutiveHits;target->life.consecutiveHitWindow=std::max(target->life.consecutiveHitWindow,window);run(ProjectileHitOperationKind::MarkDirty);}
            if(number(data().fields,"DamagePerConsecutiveProjectileHit")>0)++p->consecutiveHits[victim];
        }
        // HomingProjectile::OnHit VA1800654c0 clears both target domains only
        // when source HomingIgnoreHits is enabled. ARC/base remain empty.
        if(p->descriptor.concreteClass==ProjectileClass::Homing&&flag(data().fields,"HomingIgnoreHits")){
            p->homing.targetId=-1;p->homing.linkedTargetId=-1;p->homing.linkedTargetLifetime=0;
        }
        p->thingsHit.push_back(victimLifetime);error.clear();return true;
    }catch(const std::exception& e){error=e.what();if(mutated){faulted_=true;fault_=error;}return false;}
}
}
