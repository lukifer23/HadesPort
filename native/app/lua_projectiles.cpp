#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include "scene_runtime.hpp"
#include "input_runtime.hpp"
#include "physics_runtime.hpp"
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <stdexcept>
#include <limits>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

bool ScriptRuntime::applyWeaponPhysics(const hades::WeaponOutput& op,bool execute,std::string& error){
    auto* owner=actorWorld_.actor(op.ownerId);
    if(!owner||!owner->registered||!owner->arsenal){error="Weapon.ApplyPhysics lost its actual owner";return false;}
    if(!owner->physics){error.clear();return true;} // Native86e40 first gate.
    const auto* weapon=owner->arsenal->weaponState(op.weaponName,error);if(!weapon)return false;
    const auto number=[&](const char* key,float& value){const auto i=weapon->definition.fields.find(key);if(i==weapon->definition.fields.end()||!std::holds_alternative<double>(i->second)){error=std::string("Weapon physics requires original scalar ")+key;return false;}value=float(std::get<double>(i->second));if(!std::isfinite(value)){error="Nonfinite weapon force field";return false;}return true;};
    const auto property=[&](const char* key,float& value){const auto* p=actorWorld_.currentProperty(op.ownerId,key);if(!p){error=std::string("Weapon force owner field unavailable: ")+key;return false;}value=float(p->number());return std::isfinite(value);};
    float maximum=0,self=0,ground=0,up=0,upCap=0,cap=0,outOfRange=0,charge=0,chargeMultiplier=0,perfectMultiplier=0,perfectWindow=0;
    if(!number("MaxSelfVelocityZ",maximum)||!number("SelfVelocity",self)||!number("SelfVelocityCap",cap)||!number("SelfUpwardVelocity",up)||!number("SelfUpwardVelocityCap",upCap)||!number("SelfUpwardVelocityOnGround",ground)||!number("OutOfRangeSelfVelocity",outOfRange)||!number("ChargeTime",charge)||!number("ChargeVelocityMultiplier",chargeMultiplier)||!number("PerfectChargeVelocityMultiplier",perfectMultiplier)||!number("PerfectChargeWindowDuration",perfectWindow))return false;
    if(outOfRange>0){error="Weapon OutOfRangeSelfVelocity requires actual native targeting/range consumer: "+op.weaponName;return false;}
    float z=0;if(!actorWorld_.zLocation(op.ownerId,z,error))return false;
    hades::PhysicsForceOwner data;
    // ApplyDirectForce 0x1802bcffb multiplies ThingData+0x1fc = mSpinFactor (Thing.SpinFactor); no ImpactRotation field exists. Evidence: .local/reports/native-impact-rotation-default-recovery.json
    if(!property("Thing.UpwardTerminalVelocity",data.maxUpwardVelocity)||!property("Thing.SpinFactor",data.impactRotation))return false;
    if(owner->animation.active()){
        const auto immobile=animationBank_->exactOwnerImmobile(owner->animation.clipIndex());if(!immobile){error="Weapon planar force current primary OwnerImmobile unavailable";return false;}data.animationOwnerImmobile=*immobile;
    }else if(owner->primaryEffectAnimation||owner->primaryAnimationLifetime){
        if(!owner->primaryOwnerImmobile){error="Weapon force source primary OwnerImmobile unavailable";return false;}data.animationOwnerImmobile=*owner->primaryOwnerImmobile;
    }
    float fraction=charge==0?1:std::clamp(1.0f-weapon->chargeTimeRemaining/charge,0.0f,1.0f);
    float multiplier=chargeMultiplier>0?((chargeMultiplier-1)*fraction)+1:1;
    // CalcVelocityMultiplier84040 reads UnitData.PerfectChargeBonus(full2f8).
    if(perfectMultiplier>0&&perfectWindow>0&&(weapon->lockedPerfectCharge||weapon->perfectChargeWindowRemaining>0)){
        float bonus=0;if(!property("PerfectChargeBonus",bonus))return false;multiplier*=perfectMultiplier*bonus;
    }
    hades::PhysicsState staged=*owner->physics;auto& physics=execute?*owner->physics:staged;
    hades::PhysicsForceCallbacks callbacks;
    callbacks.getZLocation=[this,&op](float& value,std::string& detail){return actorWorld_.zLocation(op.ownerId,value,detail);};
    callbacks.onUpwardForce=[owner,execute](float,std::string& detail){if(execute)owner->physicsPending=true;detail.clear();return true;}; // Thing.OnUpwardForce2dfb00 ->AddComponent.
    callbacks.addComponent=[owner,execute](std::string& detail){if(execute)owner->physicsPending=true;detail.clear();return true;};
    callbacks.onForce=[this,&op,execute](float,std::string& detail){return onActorForce(op.ownerId,execute,detail);};
    // Native ordering: upward force/cap before the charge-scaled planar force.
    if(z>=maximum)physics.upVelocity=0;
    else if(z==0&&ground>0){if(!hades::applyPhysicsUpwardForce(physics,data,ground,true,ground,callbacks,error))return false;}
    else if(!hades::applyPhysicsUpwardForce(physics,data,up,true,upCap,callbacks,error))return false;
    if(!hades::applyPhysicsAngleForce(physics,data,op.angle,self*multiplier,true,cap*multiplier,0,callbacks,error))return false;
    error.clear();return true;
}

bool ScriptRuntime::projectileOwnerMarker(int32_t owner,const std::string& marker,float& x,float& y,std::string& error)const{
    // Owner Thing pAnim(+0x138) vslot0xd8 BookAnimation::GetMarkerLocation2b0d10:
    // a marker present in the clip's AnimationFrameDataSet uses the full
    // marker transform; an absent marker (including literal "null") returns
    // Animation::GetLocation296c90, i.e. owner GetLocation + Animation
    // mAttachOffset (zero for a Thing primary) unless the data selects
    // UseAttachedMarkerLocation/FiredBy/TipLocation or LocationFromOwner.
    // pAnim null -> owner Thing::GetLocation.
    try{
        const auto* a=actorWorld_.actor(owner);if(!a||!a->registered||!a->object)throw std::runtime_error("Projectile owner marker owner unavailable");
        if(!a->animation.active()){
            if(a->primaryEffectAnimation||a->primaryAnimationLifetime)throw std::runtime_error("Projectile owner marker for non-Book primary not reconstructed");
            return actorWorld_.location(owner,x,y,error);
        }
        const auto clip=a->animation.clipIndex();
        if(markers_.hasMarker(clip,marker)){std::array<float,3> r{};if(!effectMarker(owner,marker,r,error))return false;x=r[0];y=r[1];return true;}
        const auto definition=actorAnimationDefinitions_.find(a->animation.clip()->name);
        if(definition==actorAnimationDefinitions_.end())throw std::runtime_error("Projectile owner marker primary definition unavailable");
        const auto* d=definition->second.get("definition");if(!d)throw std::runtime_error("Projectile owner marker primary data unavailable");
        for(const char* k:{"UseAttachedMarkerLocation","UseAttachedFiredByMarkerLocation"}){const auto* v=d->get(k);if(v&&!v->text().empty())throw std::runtime_error(std::string("Animation::GetLocation attached marker branch not reconstructed: ")+k);}
        for(const char* k:{"UseAttachedTipLocation","LocationFromOwner"}){const auto* v=d->get(k);if(v&&v->boolean())throw std::runtime_error(std::string("Animation::GetLocation branch not reconstructed: ")+k);}
        return actorWorld_.location(owner,x,y,error);
    }catch(const std::exception& e){error=e.what();return false;}
}
int ScriptRuntime::expireProjectilesAction(lua_State* L){
    char message[1024]{};bool ok=false;
    try{auto* self=runtime(L);if(!self)throw std::runtime_error("ExpireProjectiles runtime unavailable");
        hades::SourceValue args;std::string error;if(!lua_istable(L,1)||!hades::readSourceValue(L,1,args,error))throw std::runtime_error(error.empty()?"ExpireProjectiles requires argument table":error);
        // ScriptArgs::Names2817f0: Names takes precedence over Name.
        std::vector<std::string> names;
        if(const auto* list=args.get("Names")){for(const auto& v:list->array())names.push_back(v.text());}
        else if(const auto* one=args.get("Name"))names.push_back(one->text());
        std::optional<std::string> weapon;if(const auto* w=args.get("WeaponName"))weapon=w->text();
        std::optional<std::vector<std::string>> exclude;if(const auto* x=args.get("ExcludeNames")){exclude.emplace();for(const auto& v:x->array())exclude->push_back(std::holds_alternative<std::string>(v.value)?v.text():std::string());}
        ok=self->projectileWorld_.expire(names,weapon,exclude,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());
    }catch(const std::exception& e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
void ScriptRuntime::registerProjectileActions(){
    lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,expireProjectilesAction,1);lua_setglobal(state_,"ExpireProjectiles");
    registerLifeActions(); // SetInvulnerable/SetVulnerable (lua_effects.cpp)
}
bool ScriptRuntime::createWeaponProjectile(const hades::WeaponOutput& op,uint64_t& handle,std::string& error){
    auto* owner=actorWorld_.actor(op.ownerId);
    if(!owner||!owner->registered||!owner->arsenal){error="Projectile creation requires actual firing Unit/Arsenal";return false;}
    hades::ProjectileInitializationRequest request;request.ownerId=op.ownerId;request.weaponName=op.weaponName;
    request.x=op.x;request.y=op.y;request.fireIndex=op.projectileIndex;request.volleyIndex=op.volleyIndex;
    if(!op.fireRequest){error="Captured native projectile fire request unavailable";return false;}
    request.fire=*op.fireRequest;request.shotAngle=op.angle;
    projectileWorld_.setOwnerMarkerProvider([this](int32_t o,const std::string& m,float& x,float& y,std::string& e){return projectileOwnerMarker(o,m,x,y,e);});
    hades::ProjectileShotProviders providers;providers.worldTime=worldTime_;
    providers.random=[this](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value))return true;detail="Projectile source RNG unavailable";return false;};
    providers.thing.random=providers.random;
    providers.ownerState=[](const hades::ActorComponents& actual,float& damage,int32_t& spawned,bool& traitor,std::string& detail){if(!actual.status){detail="Firing owner has no actual status";return false;}damage=actual.damageOutputModifier;spawned=actual.spawnedById;traitor=actual.status->snapshot().isTraitor;detail.clear();return true;};
    providers.validateGroup=[this](const std::string& name,std::string& detail){for(const auto& group:snapshotSceneGroups(state_))if(group.name==name){detail.clear();return true;}detail="Actual projectile scene group unavailable: "+name;return false;};
    providers.preflight=[](const hades::ProjectileInitializationOperation& operation,const hades::ProjectileActor&,std::string& detail){
        using K=hades::ProjectileInitializationOperationKind;
        if(operation.kind==K::ClearOnAttackEffects||(operation.kind==K::CreateStartAnimation&&(operation.name.empty()||operation.name=="null")))return true;
        detail="Projectile initialization requires live consumer: "+operation.name;return false;
    };
    providers.execute=[this](const hades::ProjectileInitializationOperation& operation,hades::ProjectileActor&,std::string& detail){
        using K=hades::ProjectileInitializationOperationKind;
        if(operation.kind==K::ClearOnAttackEffects)return selfEffects_.clearOnAttack(operation.ownerId,selfEffectProviders(),detail);
        if(operation.kind==K::CreateStartAnimation&&(operation.name.empty()||operation.name=="null")){detail.clear();return true;}
        detail="Unconsumed projectile initialization operation";return false;
    };
    return projectileWorld_.initializeShot(std::move(request),*owner->arsenal,actorWorld_,providers,handle,error);
}

int32_t ScriptRuntime::projectileFxOwner(uint64_t handle){
    const auto it=projectileFxOwnerIds_.find(handle);if(it!=projectileFxOwnerIds_.end())return it->second;
    const int32_t id=nextProjectileFxOwner_--;projectileFxOwnerIds_[handle]=id;projectileFxOwners_[id]=handle;return id;
}
bool ScriptRuntime::projectileEffectOwner(int32_t id,hades::EffectOwnerState& state,std::string& error)const{
    // Owner Thing = Projectile: Thing::GetLocation (attachment aware), GetZLocation,
    // body scale/angle and packed base color/HSV, as for any Thing owner.
    const auto it=projectileFxOwners_.find(id);const auto* p=it==projectileFxOwners_.end()?nullptr:projectileWorld_.actor(it->second);
    if(!p||p->disposed||!p->thingInitialized){error="Effect owner requires actual live projectile";return false;}
    state=hades::EffectOwnerState{};state.lifetime=p->request.lifetimeHandle;
    if(!projectileWorld_.projectileLocation(*p,state.x,state.y,error)||!projectileWorld_.projectileZ(*p,state.z,error))return false;
    state.scale=p->body.scale*p->body.scaleFraction;state.angle=p->body.angle;state.baseColor=p->baseColor;state.baseHsv=std::array<float,4>{p->hsv[0],p->hsv[1],p->hsv[2],0};
    state.hasPrimaryAnimation=p->primaryAnimation!=0;state.isUnit=false;error.clear();return true;
}
bool ScriptRuntime::releaseProjectileFxOwner(uint64_t handle,std::string& error){
    // Thing::Dispose of the owning projectile: attached FX detach exactly as for
    // other owners (ActionEffectWorld::preDisposeOwner), before the body is retired.
    const auto it=projectileFxOwnerIds_.find(handle);if(it==projectileFxOwnerIds_.end()){error.clear();return true;}
    const int32_t id=it->second;hades::EffectOwnerState owner;if(!projectileEffectOwner(id,owner,error)||!actionEffects_.preDisposeOwner(id,owner,error))return false;
    projectileFxOwners_.erase(id);projectileFxOwnerIds_.erase(it);error.clear();return true;
}
void ScriptRuntime::installProjectileGraphicProvider(){
    projectileWorld_.setDetonateGraphicProvider([this](uint64_t handle,const std::string& name,float x,float y,bool primary,float radius,std::string& e){
        // AnimationManager::Create29e410(name, projectile, Vector2 location) -> SetRadius2974e0 ->
        // PreferUnmodifiedTime29a5c0 (Projectile::Detonate6d670 VA18006d77a..6d80a).
        uint64_t fx=0;if(!createActionEffect(name,projectileFxOwner(handle),std::array<float,3>{x,y,0},fx,e))return false;
        if(fx&&primary&&(!actionEffects_.setRadius(fx,radius,e)||!actionEffects_.preferUnmodifiedTime(fx,e)))return false;
        e.clear();return true;});
}
bool ScriptRuntime::advanceProjectiles(float dt,std::string& error){
    installProjectileGraphicProvider();
    projectileWorld_.setOwnerMarkerProvider([this](int32_t o,const std::string& m,float& x,float& y,std::string& e){return projectileOwnerMarker(o,m,x,y,e);});
    projectileWorld_.setPrimaryCombatProvider([this](int32_t id,bool& inv,bool& noCoverage,std::string& e){bool noArmor=false;return primaryCombatFlags(id,inv,noArmor,noCoverage,e);});
    hades::ProjectileUpdateProviders providers;providers.worldTime=worldTime_;providers.worldTimeMultiplier=worldSpeedModifier_;
    providers.random=[this](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value))return true;detail="Projectile update source RNG unavailable";return false;};
    providers.preflight=[](const hades::ProjectileUpdateOperation& operation,const hades::ProjectileActor& p,std::string& detail){
        using K=hades::ProjectileUpdateOperationKind;
        if(operation.kind==K::CheckProjectileDefense||operation.kind==K::CheckForImpact)return true;
        // Projectile::Detonate6d670 / UpdateDetonation71c10 / CreateStartFx6f1a0 /
        // OnDeath6f5c0: real ProjectileWorld consumers; their unreconstructed
        // graphic/sound/spawn/victim branches fail when actually reached.
        if(operation.kind==K::Detonate||operation.kind==K::UpdateDetonation||operation.kind==K::CreateStartFx||operation.kind==K::OnDeath)return true;
        if(operation.kind==K::DisposeThing&&!p.primaryAnimation&&!p.attachedAnimation&&p.attachedAnimations.empty()&&!p.physics&&p.thing&&p.thing->get("Light")&&p.thing->get("Light")->text().empty())return true;
        detail=std::string("Projectile update requires live consumer: ")+hades::projectileUpdateOperationName(operation.kind);return false;
    };
    providers.execute=[this](const hades::ProjectileUpdateOperation& operation,hades::ProjectileActor& p,hades::ProjectileUpdateReply& reply,std::string& detail){
        using K=hades::ProjectileUpdateOperationKind;
        if(operation.kind==K::CheckProjectileDefense){
            hades::ProjectileDefenseProviders defense;
            const auto players=snapshotInputPlayers(state_);if(!players.empty())defense.teams.firstPlayerIdentity=players.front().playerIndex;
            defense.teams.unitPlayerIdentity=[this](int32_t id,uint64_t& token,std::string& e){token=0;const auto* actor=actorWorld_.actor(id);if(actor&&actor->registered)for(const auto& player:snapshotInputPlayers(state_))if(player.activeUnitId==id&&player.activeUnitLifetime==actor->entityHandle){token=player.playerIndex;break;}e.clear();return true;};
            return projectileWorld_.checkArcProjectileDefense(p.request.lifetimeHandle,defense,detail);
        }
        if(operation.kind==K::CheckForImpact){
            hades::ProjectileImpactProviders impact;impact.worldTime=worldTime_;
            const auto players=snapshotInputPlayers(state_);if(!players.empty())impact.teams.firstPlayerIdentity=players.front().playerIndex;
            impact.teams.unitPlayerIdentity=[this](int32_t id,uint64_t& token,std::string& e){token=0;const auto* actor=actorWorld_.actor(id);if(actor&&actor->registered)for(const auto& player:snapshotInputPlayers(state_))if(player.activeUnitId==id&&player.activeUnitLifetime==actor->entityHandle){token=player.playerIndex;break;}e.clear();return true;};
            impact.random=[this](double& value,std::string& e){if(nextRecoveredEngineRandom(state_,value))return true;e="Projectile collision source RNG unavailable";return false;};
            impact.effectData=[this](uint64_t handle,const hades::WeaponEffectDefinition*& data,std::string& e){data=selfEffects_.effectData(handle,e);return data!=nullptr;};
            impact.effectStacks=[this](uint64_t handle,int32_t& count,std::string& e){const auto* instance=selfEffects_.instance(handle);if(!instance){e="Actual crit effect stack instance unavailable";return false;}count=instance->state.numStacks;e.clear();return true;};
            impact.primaryInvulnerable=[this](int32_t id,bool& invulnerable,std::string& e){bool noArmor=false,noCoverage=false;return primaryCombatFlags(id,invulnerable,noArmor,noCoverage,e);};
            impact.preflight=[](const hades::ProjectileCollisionOperation& op,std::string& e){
                using C=hades::ProjectileCollisionOperationKind;
                if(op.kind==C::HasLineOfSight||op.kind==C::OnMovementReaction||op.kind==C::Impact)return true;
                e="Actual projectile collision consumer unavailable: kind="+std::to_string(int(op.kind))+" victim="+std::to_string(op.victimId);return false;
            };
            impact.execute=[this,&impact](const hades::ProjectileCollisionOperation& op,hades::ProjectileCollisionReply& result,std::string& e){
                using C=hades::ProjectileCollisionOperationKind;
                if(op.kind==C::Impact)return applyProjectileImpact(op.projectile,op.victimId,impact,e);
                if(op.kind==C::HasLineOfSight){bool clear=false;if(!projectileWorld_.hasLineOfSight(op,clear,e))return false;result.predicate=clear;return true;}
                if(op.kind==C::OnMovementReaction){
                    const auto* observer=actorWorld_.actor(op.victimId);if(!observer||!observer->registered||!observer->object){e="Movement reaction lost actual observer";return false;}
                    // ScriptManager.OnMovementReaction28b060 ignores its first
                    // Thing argument. Both the explicit ObserverTable and the
                    // Script.run receiver are the second Thing, VA28b078/0f1/
                    // 198/1e6. Preserve this observed native aliasing.
                    const int top=lua_gettop(state_);pushObjectLuaTable(state_,op.victimId);const bool attached=lua_istable(state_,-1);lua_settop(state_,top);if(!attached){e.clear();return true;}
                    const auto extra=[&](lua_State* L,std::string&){pushObjectLuaTable(L,op.victimId);lua_setfield(L,-2,"ObserverTable");return true;};
                    if(!dispatchCallbacks("OnMovementReaction",observer->object->name.c_str(),op.victimId,extra)){e=error_;return false;}e.clear();return true;
                }
                e="Unconsumed genuine collision operation";return false;
            };
            hades::ProjectileImpactResult result;if(!projectileWorld_.checkForImpact(p.request.lifetimeHandle,impact,result,detail,&reply.impactVictim))return false;reply.impactResult=result;return true;
        }
        if(operation.kind==K::Detonate||operation.kind==K::UpdateDetonation){
            hades::ProjectileDetonationProviders blast;blast.worldTime=worldTime_;
            const auto players=snapshotInputPlayers(state_);if(!players.empty())blast.teams.firstPlayerIdentity=players.front().playerIndex;
            blast.teams.unitPlayerIdentity=[this](int32_t id,uint64_t& token,std::string& e){token=0;const auto* actor=actorWorld_.actor(id);if(actor&&actor->registered)for(const auto& player:snapshotInputPlayers(state_))if(player.activeUnitId==id&&player.activeUnitLifetime==actor->entityHandle){token=player.playerIndex;break;}e.clear();return true;};
            return operation.kind==K::Detonate?projectileWorld_.detonate(p.request.lifetimeHandle,blast,detail):projectileWorld_.updateDetonation(p.request.lifetimeHandle,operation.elapsed,blast,detail);
        }
        if(operation.kind==K::CreateStartFx)return projectileWorld_.createStartFx(p.request.lifetimeHandle,detail);
        if(operation.kind==K::OnDeath){
            hades::ProjectileDeathArguments death;if(!projectileWorld_.prepareDeath(p.request.lifetimeHandle,operation.victim,death,detail))return false;
            // ScriptManager::OnProjectileDeath289ed0: one fresh TRIGGER_ARGS table
            // per registered trigger; name=ProjectileData name (vslot0x88),
            // triggeredBy = victim (null here). Field order follows VA289fd4+.
            const auto extra=[&](lua_State* L,std::string&){
                lua_pushstring(L,death.weaponName.c_str());lua_setfield(L,-2,"WeaponName");
                if(death.attackerId>=0){pushObjectLuaTable(L,death.attackerId);lua_setfield(L,-2,"AttackerTable");}
                if(death.hasLocation){lua_pushnumber(L,double(death.locationX));lua_setfield(L,-2,"LocationX");lua_pushnumber(L,double(death.locationY));lua_setfield(L,-2,"LocationY");}
                if(death.killedVictim){lua_pushboolean(L,1);lua_setfield(L,-2,"KilledVictim");}
                lua_pushnumber(L,double(death.angleDegrees));lua_setfield(L,-2,"Angle");
                lua_pushinteger(L,death.fireIndex);lua_setfield(L,-2,"FireIndex");
                lua_pushinteger(L,death.volley);lua_setfield(L,-2,"ProjectileVolley");
                if(death.lastHitId>=0&&actorWorld_.actor(death.lastHitId)&&actorWorld_.actor(death.lastHitId)->registered){pushObjectLuaTable(L,death.lastHitId);lua_setfield(L,-2,"LastHitTable");if(death.lastHitIsUnit){pushObjectLuaTable(L,death.lastHitId);lua_setfield(L,-2,"LastHitUnitTable");}}
                lua_pushinteger(L,death.lastHitIsUnit?1:0);lua_setfield(L,-2,"UnitsHit");
                if(death.firstUnitInVolley>=0&&actorWorld_.actor(death.firstUnitInVolley)&&actorWorld_.actor(death.firstUnitInVolley)->registered){pushObjectLuaTable(L,death.firstUnitInVolley);lua_setfield(L,-2,"FirstUnitInVolley");}
                lua_pushboolean(L,death.lastProjectileDeath);lua_setfield(L,-2,"LastProjectileDeath");
                lua_pushboolean(L,death.invulnerableImpact);lua_setfield(L,-2,"InvulnerableImpact");
                lua_pushboolean(L,death.isDeflected);lua_setfield(L,-2,"IsDeflected");
                return true;
            };
            if(!dispatchCallbacks("OnProjectileDeath",death.name.c_str(),death.victimId>=0?death.victimId:0,extra)){detail=error_;return false;}
            detail.clear();return true;
        }
        if(operation.kind==K::DisposeThing){if(!releaseProjectileFxOwner(p.request.lifetimeHandle,detail))return false;p.body.entityHandle=0;detail.clear();return true;}
        detail="Unconsumed live projectile operation";return false;
    };
    return projectileWorld_.update(dt,providers,error);
}

bool ScriptRuntime::advanceLife(float dt,std::string& error){
    if(!lifeSystem_.reconcile(error))return false;
    return lifeSystem_.update([this,dt](const hades::ActorComponents& actor,float& elapsed,std::string& detail){
        // Shared source ThingSystem elapsed calculation; health remains the
        // attached original Lua table, not this timer component.
        return hades::physicsComponentElapsed(dt,worldSpeedModifier_,actor.timeModifierFraction,actor.timeMultiplier,elapsed,detail);
    },error);
}

namespace {
struct HitLuaStack {lua_State* L;int top;explicit HitLuaStack(lua_State* state):L(state),top(lua_gettop(state)){}~HitLuaStack(){lua_settop(L,top);}};
const hades::SourceValue& hitProperty(const hades::ActorWorld& world,int32_t id,const char* key){const auto* value=world.currentProperty(id,key);if(!value)throw std::runtime_error(std::string("Native hit source property missing: ")+key);return *value;}
float hitNumber(const std::map<std::string,hades::WeaponScalar>& fields,const char* key){const auto i=fields.find(key);if(i==fields.end()||!std::holds_alternative<double>(i->second)||!std::isfinite(std::get<double>(i->second)))throw std::runtime_error(std::string("Native hit source scalar missing: ")+key);return float(std::get<double>(i->second));}
bool hitFlag(const std::map<std::string,hades::WeaponScalar>& fields,const char* key){const auto i=fields.find(key);if(i==fields.end()||!std::holds_alternative<bool>(i->second))throw std::runtime_error(std::string("Native hit source flag missing: ")+key);return std::get<bool>(i->second);}
std::string hitText(const std::map<std::string,hades::WeaponScalar>& fields,const char* key){const auto i=fields.find(key);if(i==fields.end()||!std::holds_alternative<std::string>(i->second))throw std::runtime_error(std::string("Native hit source name missing: ")+key);return std::get<std::string>(i->second);}
}
bool ScriptRuntime::applyProjectileImpact(uint64_t handle,int32_t victim,const hades::ProjectileImpactProviders& collision,std::string& error){
    try{
        hades::ProjectileHitProviders hit;hit.collision=collision;
        float outgoing=0,incoming=0;if(!configurationFloat("DamageMultiplier",outgoing,error)||!configurationFloat("DamageTakenMultiplier",incoming,error))return false;
        hit.damageMultiplier=outgoing;hit.damageTakenMultiplier=incoming;
        {
            HitLuaStack stack(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushliteral(state_,"DisableDamage");lua_setfield(state_,-2,"Name");
            if(lua_pcall(state_,1,1,0)!=LUA_OK){error=lua_tostring(state_,-1);return false;}if(!lua_isboolean(state_,-1)){error="Native DisableDamage configuration is not boolean";return false;}hit.disableDamage=lua_toboolean(state_,-1);
        }
        hit.primaryFlags=[this](int32_t id,bool& invulnerable,bool& noArmor,bool& noCoverage,std::string& e){return primaryCombatFlags(id,invulnerable,noArmor,noCoverage,e);};
        hit.immuneToForce=[this](int32_t id,bool& immune,std::string& e){return isImmuneToForce(id,immune,e);};
        hit.execute=[this](const hades::ProjectileHitOperation& op,hades::ProjectileHitReply& reply,std::string& detail){
            using K=hades::ProjectileHitOperationKind;
            auto* projectile=projectileWorld_.actor(op.projectile);auto* target=actorWorld_.actor(op.victim);
            if(!projectile||!projectile->firingArsenal||!target||!target->object){detail="Actual hit lifetime storage expired";return false;}
            const auto* weapon=projectile->firingArsenal->weaponState(projectile->request.weaponName,detail);if(!weapon)return false;
            const auto* data=projectile->firingArsenal->projectileData(projectile->request.weaponName,projectile->spawnedDefinition,detail);if(!data)return false;
            if(op.kind==K::MarkDirty)return lifeSystem_.markDirty(op.victim,detail);
            if(op.kind==K::TakeHitPresentation){
                if(!setActorAnimation(op.victim,hitProperty(actorWorld_,op.victim,"Life.HitGraphic").text(),detail))return false;
                const auto cap=int32_t(hitProperty(actorWorld_,op.victim,"Life.HitFxCap").number());
                if(cap<0||target->life.activeHitFxCount<cap){
                    // LifeComponent::TakeHit VA1802b7eae..1802b7f6e: Thing::IsInvulnerable651a0
                    // (pAnim OwnerInvulnerable || pLife IsInvulnerable) with nonempty
                    // Life.HitFxInvulnerable and !SilentImpactOnInvulnerable selects it,
                    // else Life.HitFx; ActiveHitFxCount++; AnimationManager::Create(name,
                    // victim, impact XY) -> SetZLocation(impact Z), AddAngle(angle+pi),
                    // PreferUnmodifiedTime. Empty/"null" names create nothing.
                    bool invulnerable=false,noArmor=false,noCoverage=false;if(!primaryCombatFlags(op.victim,invulnerable,noArmor,noCoverage,detail))return false;
                    invulnerable=invulnerable||(target->hasLife&&target->invulnerable);
                    const auto special=hitProperty(actorWorld_,op.victim,"Life.HitFxInvulnerable").text();
                    const auto name=(invulnerable&&!special.empty()&&!hitFlag(data->fields,"SilentImpactOnInvulnerable"))?special:hitProperty(actorWorld_,op.victim,"Life.HitFx").text();
                    // VA1802b7f08..f6e: Create(name, victim, impact XY) (null -> skip),
                    // Animation::SetZLocation 0x2990e0 (self +0x6e0 and ZLocation
                    // component), AddAngle 0x298260 (angle + 3.1415927f, 0x180610f94),
                    // PreferUnmodifiedTime 0x29a5c0.
                    if(!name.empty()&&name!="null"){uint64_t fx=0;if(!createActionEffect(name,op.victim,std::array<float,3>{op.geometry.x,op.geometry.y,0},fx,detail))return false;
                        if(fx&&(!actionEffects_.setZLocation(fx,op.geometry.z,detail)||!actionEffects_.addAngle(fx,op.geometry.angle+3.14159274f,detail)||!actionEffects_.preferUnmodifiedTime(fx,detail)))return false;}
                    ++target->life.activeHitFxCount;
                }
                if((!weapon->validProjectileData||hitFlag(weapon->projectileData.fields,"TriggerHitSound"))&&!weapon->impactCueId){
                    // Source omission keeps the LifeData ctor temporary default SoundCue (Life ctor VA1800c86b6 copies a default-constructed def), i.e. an invalid cue, as Thing.ImpulsingSound.
                    const auto* hitCue=actorWorld_.currentProperty(op.victim,"Life.HitSound");const auto cue=hitCue?hitCue->text():std::string{};bool valid=false;if(!audio_.resolveCue(cue,valid,detail))return false;
                    if(valid){detail="Native TakeHit HitSound requires source SoundManagerCap-aware PlayCue3D consumer: "+cue;return false;}
                }
                detail.clear();return true;
            }
            if(op.kind==K::SetDamagedAnimation){
                if(!setActorAnimation(op.victim,hitProperty(actorWorld_,op.victim,"Life.DamagedAnimation").text(),detail))return false;
                if(hitProperty(actorWorld_,op.victim,"Life.OnDamagedSpin").number()>0){detail="Native OnDamagedSpin requires actual angular-force consumer";return false;}
                detail.clear();return true;
            }
            if(op.kind==K::LookupTagMultipliers){
                // Exact TakeHit reads attacker.EffectMultipliers[weaponName],
                // then each ACTIVE victim Effect name, gated by actual attacker
                // TraitDictionary key membership. No generated trait tables.
                HitLuaStack stack(state_);reply.tagMultiplierAddition=0;
                pushObjectLuaTable(state_,op.attacker);if(!lua_istable(state_,-1))return true;const int attackerTable=lua_gettop(state_);
                lua_getfield(state_,attackerTable,"EffectMultipliers");if(!lua_istable(state_,-1))return true;
                lua_getfield(state_,-1,weapon->definition.name.c_str());if(!lua_istable(state_,-1))return true;const int entries=lua_gettop(state_);
                if(!target->status){detail="Native tag multiplier lookup requires victim status";return false;}
                float total=0;
                for(const auto effectHandle:target->status->snapshot().active){const auto* effect=selfEffects_.effectData(effectHandle,detail);if(!effect)return false;
                    const int top=lua_gettop(state_);lua_getfield(state_,entries,effect->name.c_str());
                    if(!lua_istable(state_,-1)){lua_settop(state_,top);continue;}const int record=lua_gettop(state_);
                    lua_getfield(state_,record,"TraitName");const char* nativeTrait=lua_tostring(state_,-1);const std::string trait=nativeTrait?nativeTrait:"";lua_pop(state_,1);
                    bool eligible=trait.empty();if(!eligible){lua_getfield(state_,attackerTable,"TraitDictionary");if(lua_istable(state_,-1)){lua_getfield(state_,-1,trait.c_str());eligible=!lua_isnil(state_,-1);lua_pop(state_,1);}lua_pop(state_,1);}
                    if(eligible){lua_getfield(state_,record,"Multiplier");const float value=lua_isnumber(state_,-1)?float(lua_tonumber(state_,-1)):0;if(!std::isfinite(value)){detail="Nonfinite actual Lua effect multiplier";return false;}total+=value-1;lua_pop(state_,1);}
                    lua_settop(state_,top);
                }
                reply.tagMultiplierAddition=total;detail.clear();return true;
            }
            if(op.kind==K::OnHit){
                {HitLuaStack stack(state_);pushObjectLuaTable(state_,op.victim);if(!lua_istable(state_,-1)){detail.clear();return true;}}
                const auto extra=[&](lua_State* L,std::string& e){
                    const int destination=lua_gettop(L);pushObjectLuaTable(L,op.victim);const int victimTable=lua_gettop(L);int attackerTable=0;
                    if(op.hit.attacker){pushObjectLuaTable(L,op.hit.attacker->id);attackerTable=lua_gettop(L);}
                    const auto status=hades::pushHitArguments(L,op.hit,victimTable,attackerTable,e);
                    if(status!=hades::HitArgumentsStatus::Ready){lua_settop(L,destination);if(e.empty())e="Actual OnHit attached table disappeared";return false;}
                    lua_replace(L,destination);lua_settop(L,destination);return true;
                };
                if(!dispatchCallbacks("OnHit",target->object->name.c_str(),op.victim,extra)){detail=error_;return false;}detail.clear();return true;
            }
            if(op.kind==K::CountAttackerEffects)return selfEffects_.countOwnerAttack(op.attacker,selfEffectProviders(),detail);
            if(op.kind==K::ClearCritEffects)return selfEffects_.clearOnCritted(op.victim,selfEffectProviders(),detail);
            if(op.kind==K::OnHitWeapon){
                if(!target->arsenal||!target->status){detail="Native OnHitWeapon requires actual Unit/Arsenal/status";return false;}
                const auto name=hitProperty(actorWorld_,op.victim,"OnHitWeapon").text();
                // Native Arsenal.GetWeapon returns null for an unequipped
                // source name; it does not equip an invented weapon here.
                if(!target->arsenal->hasWeapon(name)){detail.clear();return true;}
                if(op.hit.damage.hitArmor&&hitProperty(actorWorld_,op.victim,"OnHitWeaponRequiresMissedArmor").boolean()){detail.clear();return true;}
                const auto* reaction=target->arsenal->weaponState(name,detail);if(!reaction)return false;
                // Life.TakeHit91xx queries exactly the same timer/pending/
                // active CanAttack(false) gates; Android's actual mode is play.
                const bool allowed=target->status->canAttack(false,hitProperty(actorWorld_,op.victim,"ImmuneToStun").boolean(),false);
                if(!allowed&&!hitFlag(reaction->definition.fields,"CanFireWhileDisarmed")){detail.clear();return true;}
                float speed=0;if(!configurationFloat("SpeedMultiplier",speed,detail))return false;hades::WeaponOwnerFrame frame;
                if(!actorWorld_.weaponOwnerFrame(op.victim,worldTime_,speed,false,inputAllowed(),frame,detail))return false;
                // TakeHit9227 uses the same exact83160 overload as Touchdown
                // and OnStun: angle-1, zero from/target, null link, selfEffects1.
                const bool accepted=target->arsenal->requestTouchdownFire(name,frame,detail);return accepted||detail.empty();
            }
            if(op.kind==K::ApplyVictimEffects){
                if(hitProperty(actorWorld_,op.victim,"Life.RequiresDamageToAffect").boolean()&&op.hit.damage.actualDamage<=0){detail.clear();return true;}
                if(target->definition->kind!="Unit"){detail.clear();return true;}
                if(!target->status){detail="Projectile victim effect admission requires real UnitStatus";return false;}
                // ApplyEffects77630 consumes the global draw even for an empty
                // vector, then one per ACTIVE effect before later gates.
                double random=0;if(!nextRecoveredEngineRandom(state_,random)){detail="Victim effects source RNG unavailable";return false;}
                if(random<=double(hitNumber(data->fields,"AllEffectsProcChance"))){
                    bool invulnerable=false,noArmor=false,noCoverage=false;if(!primaryCombatFlags(op.victim,invulnerable,noArmor,noCoverage,detail))return false;invulnerable=invulnerable||target->invulnerable;
                    if(!invulnerable&&!hitFlag(data->fields,"IgnoreCoverageAngles")&&target->life.invulnerableCoverage>0){detail="Victim effects require exact directional coverage predicate";return false;}
                    const size_t effectCount=data->effects.size();
                    for(size_t effectIndex=0;effectIndex<effectCount;++effectIndex){
                        data=projectile->firingArsenal->projectileData(projectile->request.weaponName,projectile->spawnedDefinition,detail);weapon=projectile->firingArsenal->weaponState(projectile->request.weaponName,detail);
                        if(!data||!weapon||data->effects.size()!=effectCount){if(detail.empty())detail="Native victim EffectData vector changed across callback";return false;}
                        const auto effect=data->effects[effectIndex];
                        if(!hitFlag(effect.fields,"Active"))continue;
                        if(!nextRecoveredEngineRandom(state_,random)){detail="Victim effect source proc RNG unavailable";return false;}
                        if(random>double(hitNumber(effect.fields,"ProcChance")))continue;
                        if(hitFlag(effect.fields,"RequirePerfectCharge")&&!weapon->wasLastFirePerfectCharge)continue;
                        if(invulnerable&&!hitFlag(effect.fields,"CanAffectInvulnerable"))continue;
                        if(hitFlag(effect.fields,"RequiresAttachedLua")){HitLuaStack stack(state_);pushObjectLuaTable(state_,op.victim);if(!lua_istable(state_,-1))continue;}
                        if(effect.type==1&&!target->physics)continue;
                        const auto only=hitText(effect.fields,"OnlyAffectName"),ignore=hitText(effect.fields,"IgnoreName"),exact=hitText(effect.fields,"IgnoreExactName"),required=hitText(effect.fields,"RequireExistingEffect");
                        if((!only.empty()&&target->object->name.find(only)==std::string::npos)||(!ignore.empty()&&target->object->name.find(ignore)!=std::string::npos)||(!exact.empty()&&target->object->name==exact)||(!required.empty()&&!target->status->getEffect(required)))continue;
                        // Real UnitStatus.AddEffect2fdca0 ordered admission.
                        if(target->lifeStatus!=0||target->status->blockAll||hitProperty(actorWorld_,op.victim,"BlockAllEffects").boolean())continue;
                        if(!effect.nativeIdsKnown){detail="Victim effect native name ID unresolved";return false;}
                        if(effect.nativeNameId==target->status->blockEffectId)continue;
                        if(hitProperty(actorWorld_,op.victim,"ImmuneToCharm").boolean()&&effect.type==7)continue;
                        if(hitNumber(effect.fields,"Duration")<=0)continue;
                        if(hitProperty(actorWorld_,op.victim,"ImmuneToStun").boolean()&&((effect.type==0&&hitNumber(effect.fields,"Modifier")==0)||hitFlag(effect.fields,"DisableAttack")||hitFlag(effect.fields,"DisableMove")||hitFlag(effect.fields,"DisableRotate")||hitNumber(effect.fields,"RotationMultiplier")!=1))continue;
                        if(hitProperty(actorWorld_,op.victim,"ImmuneToSlow").boolean()&&(effect.type==0||hitNumber(effect.fields,"ElapsedTimeMultiplier")<1))continue;
                        if(effect.type==6){detail="Victim DELAYED_KNOCKBACK admission requires source valid projectile/impact velocity branch";return false;}
                        if(hitProperty(actorWorld_,op.victim,"ImmuneToDoTs").boolean()&&effect.type==4)continue;
                        if(std::find(target->status->immuneEffects.begin(),target->status->immuneEffects.end(),effect.name)!=target->status->immuneEffects.end())continue;
                        if(!selfEffects_.admitProjectileEffect(op.victim,projectile->request.ownerId,projectile->request.weaponName,projectile->spawnedDefinition,effectIndex,op.geometry.angle,selfEffectProviders(),detail))return false;
                    }
                }
                data=projectile->firingArsenal->projectileData(projectile->request.weaponName,projectile->spawnedDefinition,detail);if(!data)return false;
                const auto trigger=hitText(data->fields,"TriggerEffect");if(!trigger.empty()&&target->status->getEffect(trigger)){detail="Native TriggerEffect/ConsumeTriggerEffect consumer required: "+trigger;return false;}
                if(!data->removeEffectsKnownEmpty){detail="Projectile RemoveEffects constructor/source closure unresolved";return false;}
                detail.clear();return true;
            }
            if(op.kind==K::ImpactFx){
                bool invulnerable=false,noArmor=false,noCoverage=false;if(!primaryCombatFlags(op.victim,invulnerable,noArmor,noCoverage,detail))return false;
                if((invulnerable||target->invulnerable)&&hitFlag(data->fields,"SilentImpactOnInvulnerable")){detail.clear();return true;}
                // ImpactFx6e090 selects CriticalFx, then HitVulnerabilityFx for
                // Units, otherwise actual ImpactFx. Named FX need angle/Z/time
                // provider; empty lookup genuinely returns no animation.
                const auto name=hitText(data->fields,target->definition->kind=="Unit"&&op.hit.damage.isCrit?"CriticalFx":target->definition->kind=="Unit"&&op.hit.damage.hitVulnerability?"HitVulnerabilityFx":"ImpactFx");
                if(!name.empty()&&name!="null"){detail="Native impact animation consumer required: "+name;return false;}
                const auto cue=data->cues.find("ImpactSound");if(cue==data->cues.end()){detail="Native ImpactSound default/read semantics unavailable";return false;}
                if(!cue->second.validityKnown||cue->second.valid){detail="Native ImpactSound requires actual PlayCue3D consumer: "+cue->second.name;return false;}
                detail.clear();return true;
            }
            if(op.kind==K::InflictedDamageSound){
                const auto cue=data->cues.find("InflictedDamageSound");if(cue==data->cues.end()){detail="InflictedDamageSound actual source unavailable";return false;}
                std::array<float,3> position{};if(!actorWorld_.location(op.victim,position[0],position[1],detail)||!actorWorld_.zLocation(op.victim,position[2],detail))return false;uint32_t sound=0;return playAudioCue(cue->second.name,op.victim,position,sound,detail);
            }
            if(op.kind==K::ApplyImpactForces){
                // Projectile::ApplyImpact76590 VA180076c2a..180076d80
                // (.local/reports/native-combat3-apply-impact-pseudocode.c,
                // native-projectile-apply-impact-static.json). ProjectileData
                // offsets are ProjectileDataDef+0x50: 0x618 ImpactVelocity,
                // 0x61c ImpactUpwardVelocity, 0x620 ImpactVelocityFalloff,
                // 0x624 ImpactVelocityCap, 0x62c CounterImpactOnSelf, 0x614
                // SelfImpactVelocityMultiplier, 0x30c DamageRadius.
                float velocity=hitNumber(data->fields,"ImpactVelocity"),upward=hitNumber(data->fields,"ImpactUpwardVelocity");
                const float falloff=hitNumber(data->fields,"ImpactVelocityFalloff");
                if(falloff!=1){
                    float vx=0,vy=0,px=0,py=0;if(!actorWorld_.location(op.victim,vx,vy,detail)||!projectileWorld_.projectileLocation(*projectile,px,py,detail))return false;
                    const float distance=std::sqrt((px-vx)*(px-vx)+(py-vy)*(py-vy));float scale=1;
                    if(distance>0){scale=distance/hitNumber(data->fields,"DamageRadius");scale=std::max(scale,0.f);scale=std::min(scale,1.f);scale=scale*(falloff-1)+1;}
                    velocity*=scale;upward*=scale;
                }
                if(hitNumber(data->fields,"SelfImpactVelocityMultiplier")!=1&&op.victim==projectile->request.ownerId){
                    // VA180076ca1..cb5 multiplies by xmm0 live across GetUnit2fa820; keep
                    // the self-hit branch closed until that register value is proven.
                    detail="ApplyImpact self-hit SelfImpactVelocityMultiplier branch not reconstructed";return false;
                }
                if(!target->physics||target->lifeStatus!=0){detail.clear();return true;}
                hades::PhysicsForceOwner owner;if(!externalForceOwner(op.victim,owner,detail))return false;
                const int32_t victimId=op.victim;
                hades::PhysicsForceCallbacks callbacks;
                callbacks.addComponent=[this,victimId](std::string& e){auto* a=actorWorld_.actor(victimId);if(!a||!a->physics){e="Impact force victim Physics expired";return false;}a->physicsPending=true;e.clear();return true;};
                callbacks.onForce=[this,victimId](float,std::string& e){return onActorForce(victimId,true,e);};
                callbacks.onUpwardForce=[this,victimId](float,std::string& e){auto* a=actorWorld_.actor(victimId);if(!a||!a->physics){e="Impact upward victim Physics expired";return false;}a->physicsPending=true;e.clear();return true;}; // Thing.OnUpwardForce2dfb00 -> AddComponent.
                callbacks.getZLocation=[this,victimId](float& z,std::string& e){return actorWorld_.zLocation(victimId,z,e);};
                const float angle=op.geometry.angle;const int32_t counterId=weapon->ownerId;
                if(!hades::applyPhysicsAngleForce(*target->physics,owner,angle,velocity,false,hitNumber(data->fields,"ImpactVelocityCap"),0,callbacks,detail))return false;
                const float counter=hitNumber(data->fields,"CounterImpactOnSelf");
                if(counter>0){
                    // Counter force targets GetUnit(Weapon+0x30 owner id) when the projectile has a Weapon.
                    auto* self=actorWorld_.actor(counterId);
                    if(self&&self->registered){
                        if(!self->physics){detail="ApplyImpact CounterImpactOnSelf owner has no Physics (native dereferences it)";return false;}
                        hades::PhysicsForceOwner selfOwner;if(!externalForceOwner(counterId,selfOwner,detail))return false;
                        hades::PhysicsForceCallbacks selfCallbacks;
                        selfCallbacks.addComponent=[self](std::string& e){self->physicsPending=true;e.clear();return true;};
                        selfCallbacks.onForce=[this,counterId](float,std::string& e){return onActorForce(counterId,true,e);};
                        volatile float reverse=angle+3.1415927410125732f;const float amount=counter*velocity;
                        if(!hades::applyPhysicsAngleForce(*self->physics,selfOwner,reverse,amount,false,amount,0,selfCallbacks,detail))return false;
                    }
                }
                target=actorWorld_.actor(op.victim);if(!target||!target->physics){detail="Impact upward force victim Physics expired";return false;}
                if(!externalForceOwner(op.victim,owner,detail))return false;
                return hades::applyPhysicsUpwardForce(*target->physics,owner,upward,false,std::numeric_limits<float>::max(),callbacks,detail);
            }
            detail="Native Impact/TakeHit consumer unavailable: kind="+std::to_string(int(op.kind))+" victim="+std::to_string(op.victim);return false;
        };
        return projectileWorld_.applyArcImpact(handle,victim,hit,error);
    }catch(const std::exception& e){error=e.what();return false;}
}
