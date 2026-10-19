#include "lua_runtime.hpp"
#include "weapon_execution.hpp"
#include "recovered_runtime.hpp"
#include "input_runtime.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
extern "C" {
#include <lua.h>
}
bool ScriptRuntime::configurationFloat(const char* name,float& result,std::string& error){
    const int top=lua_gettop(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushstring(state_,name);lua_setfield(state_,-2,"Name");
    if(lua_pcall(state_,1,1,0)!=LUA_OK){error=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    const bool numeric=lua_isnumber(state_,-1);result=float(lua_tonumber(state_,-1));lua_settop(state_,top);
    if(!numeric||!std::isfinite(result)){error=std::string("Native numeric configuration unavailable: ")+name;return false;}return true;
}
bool ScriptRuntime::onActorForce(int32_t id,bool execute,std::string& error){
    auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object||!actor->physics){error="Thing.OnForce requires actual owner/data/Physics";return false;}
    const auto value=[&](const char* key)->const hades::SourceValue&{const auto* p=actorWorld_.currentProperty(id,key);if(!p)throw std::runtime_error(std::string("Thing.OnForce source unavailable: ")+key);return *p;};
    // DirectForce2bd04b passes false,false independently of LastForceSelfApplied.
    // Native optimized Thing.IsAlive checks the actual LifeStatus, not Entity.
    if(actor->lifeStatus==0&&worldTime_-actor->lastImpulsedAnimTime>float(value("Thing.ImpulsedFxCooldown").number())){
        const auto animation=value("Thing.ImpulsedAnimation").text();const auto fx=value("Thing.ImpulsedFx").text();
        // OnForce2df9ac..bd uses exactly the same SetAnimation(false,true,false)
        // flags as actual Weapon.Charge87ee4..ef2, consumed by setActorAnimation.
        if(!fx.empty()&&fx!="null"&&!prepareActionEffect(fx,id,error))return false;
        if(execute){
            if(!setActorAnimation(id,animation,error))return false;
            if(!fx.empty()&&fx!="null"){uint64_t handle=0;float x=0,y=0,z=0;if(!actorWorld_.location(id,x,y,error)||!actorWorld_.zLocation(id,z,error)||!createActionEffect(fx,id,std::array<float,3>{x,y,z},handle,error))return false;}
            actor->lastImpulsedAnimTime=worldTime_;
        }
    }
    if(actor->physics->impulsingSoundId==0){
        // All inherited source overrides survive Actor export. Source omission
        // uses exact invalid compiled cue (ThingData ctor eb44b..eb4c6).
        const auto* cue=actorWorld_.currentProperty(id,"Thing.ImpulsingSound");const std::string name=cue?cue->text():std::string{};
        if(!name.empty()&&name!="null"&&execute){uint32_t channel=0;float x=0,y=0,z=0;
            if(!actorWorld_.location(id,x,y,error)||!actorWorld_.zLocation(id,z,error)||!playAudioCue(name,id,{x,y,z},channel,error))return false;
            actor->physics->impulsingSoundId=int32_t(channel);
        }
    }
    if(execute)actor->physicsPending=true;error.clear();return true;
}
// PhysicsComponent::IsImmuneToForce2bd4d0 (.local/reports/native-active-physics-force-immune-static.json):
// owner ThingData+0x205 (Thing.ImmuneToForce) -> true; else for a Unit (typeflags
// mask0x221) any entry of the Unit's active effect vector (+0x4b0..0x4b8) whose
// EffectData+0x1b3 (ImmuneToForce) is set -> true; else false.
bool ScriptRuntime::isImmuneToForce(int32_t id,bool& immune,std::string& error){
    immune=false;const auto* owner=actorWorld_.actor(id);if(!owner||!owner->registered){error="IsImmuneToForce owner unavailable";return false;}
    const auto* thing=actorWorld_.currentProperty(id,"Thing.ImmuneToForce");if(!thing){error="Actual Thing.ImmuneToForce unavailable";return false;}
    if(thing->boolean()){immune=true;error.clear();return true;}
    if(owner->definition&&owner->definition->kind=="Unit"&&owner->status)for(const auto h:owner->status->snapshot().active){
        const auto* effect=selfEffects_.effectData(h,error);if(!effect)return false;
        const auto f=effect->fields.find("ImmuneToForce");if(f==effect->fields.end()||!std::holds_alternative<bool>(f->second)){error="Actual effect ImmuneToForce unavailable";return false;}
        if(std::get<bool>(f->second)){immune=true;break;}
    }
    error.clear();return true;
}
// Owner inputs read by PhysicsComponent::ApplyForce2bd070/ApplyUpwardForce2bd2c0
// for an externally applied (selfApplied=false) force: ExternalForceMultiplier,
// SpinFactor, IsImmuneToForce, current primary OwnerImmobile, GetGrip2bd530 and
// UpwardTerminalVelocity. IsImmuneToForce is reported through thingImmuneToForce.
bool ScriptRuntime::externalForceOwner(int32_t id,hades::PhysicsForceOwner& data,std::string& error){
    const auto* owner=actorWorld_.actor(id);if(!owner||!owner->registered||!owner->physics){error="External force owner Physics unavailable";return false;}
    const auto property=[&](const char* key,float& value){const auto* p=actorWorld_.currentProperty(id,key);if(!p){error=std::string("External force owner field unavailable: ")+key;return false;}value=float(p->number());if(!std::isfinite(value)){error=std::string("Nonfinite external force owner field: ")+key;return false;}return true;};
    data=hades::PhysicsForceOwner{};
    if(!property("Thing.ExternalForceMultiplier",data.forceMultiplier)||!property("Thing.SpinFactor",data.impactRotation)||!property("Thing.UpwardTerminalVelocity",data.maxUpwardVelocity))return false;
    if(!isImmuneToForce(id,data.thingImmuneToForce,error))return false;
    if(owner->animation.active()){const auto immobile=animationBank_->exactOwnerImmobile(owner->animation.clipIndex());if(!immobile){error="External force primary OwnerImmobile unavailable";return false;}data.animationOwnerImmobile=*immobile;}
    else if(owner->primaryEffectAnimation||owner->primaryAnimationLifetime){if(!owner->primaryOwnerImmobile){error="External force source primary OwnerImmobile unavailable";return false;}data.animationOwnerImmobile=*owner->primaryOwnerImmobile;}
    return physicsGrip_?physicsGrip_(id,data.grip,error):physicsGripValue(id,data.grip,error);
}
bool ScriptRuntime::weaponCallback(const hades::WeaponOutput& op,std::string& error){
    auto* actor=actorWorld_.actor(op.ownerId);if(!actor||!actor->registered||!actor->arsenal){error="Weapon callback lost actual owner";return false;}
    const auto extra=[&](lua_State* L,std::string& detail){
        auto* actor=actorWorld_.actor(op.ownerId);if(!actor||!actor->arsenal){detail="Weapon callback owner data unavailable";return false;}
        const auto* weapon=actor->arsenal->weaponState(op.weaponName,detail);if(!weapon)return false;
        pushObjectLuaTable(L,op.ownerId);lua_setfield(L,-2,"OwnerTable");
        if(op.name=="OnWeaponChargeCanceled"){lua_pushboolean(L,weapon->cooldownTimeRemaining>0);lua_setfield(L,-2,"OnCooldown");}
        if(op.name=="OnWeaponTriggerRelease"||op.name=="OnWeaponFired"){
            int32_t ammo=0;if(!actor->arsenal->getAmmo(op.weaponName,ammo,detail))return false;
            lua_pushinteger(L,ammo);lua_setfield(L,-2,"Ammo");lua_pushinteger(L,weapon->definition.maxAmmo);lua_setfield(L,-2,"MaxAmmo");
        }
        if(op.name=="OnWeaponFailedToFire"){
            // ScriptManager::OnWeaponFailedToFire28d0d0(Weapon*,bool Controllable,bool FreshInput):
            // Ammo = WeaponData+0x6f0 ShareAmmoWeapon nonempty && owner arsenal(+0x428)
            // GetWeapon(share) ? that Weapon::GetAmmo83af0 : this mAmmo(+0x34) raw;
            // MaxAmmo = WeaponData+0x6e8; then the two caller booleans. Both
            // callers (Weapon::Update183bd1, AttemptFireWeapon) pass Controllable=1.
            int32_t ammo=weapon->ammo;const auto& share=weapon->definition.shareAmmoWeapon;
            if(!share.empty()&&actor->arsenal->hasWeapon(share)&&!actor->arsenal->getAmmo(share,ammo,detail))return false;
            lua_pushinteger(L,ammo);lua_setfield(L,-2,"Ammo");lua_pushinteger(L,weapon->definition.maxAmmo);lua_setfield(L,-2,"MaxAmmo");
            lua_pushboolean(L,op.controllable);lua_setfield(L,-2,"Controllable");lua_pushboolean(L,op.freshInput);lua_setfield(L,-2,"FreshInput");
        }
        if(op.name=="OnWeaponFired"){
            std::string lookup;const bool perfect=actor->arsenal->isPerfectCharge(op.weaponName,lookup);if(!lookup.empty()){detail=lookup;return false;}
            if(perfect){lua_pushboolean(L,true);lua_setfield(L,-2,"IsPerfectCharge");}
            if(weapon->validProjectileData){lua_pushstring(L,weapon->projectileData.name.c_str());lua_setfield(L,-2,"ProjectileName");}
            lua_pushinteger(L,weapon->volley);lua_setfield(L,-2,"Volley");lua_pushinteger(L,actor->nearbyDangerId);lua_setfield(L,-2,"NearbyDangerId");
            if(op.hasTargetLocation){lua_pushnumber(L,op.x);lua_setfield(L,-2,"TargetX");lua_pushnumber(L,op.y);lua_setfield(L,-2,"TargetY");}
        }
        return true;
    };
    if(!dispatchCallbacks(op.name.c_str(),op.weaponName.c_str(),op.ownerId,extra,op.name!="OnWeaponTriggerRelease"&&op.name!="OnWeaponFailedToFire")){error=error_;return false;}return true;
}
bool ScriptRuntime::weaponAnimationRadius(const hades::WeaponOutput& op,bool execute,std::string& error){
    // Animation::SetRadius2974e0: recurse pChildAnimation(+0xc8) first, then
    // each live mCreatedAnimations(+0x728) entry whose data name equals own
    // CreateAnimation(+0x4d8) or occurs in CreateAnimations(+0x5f8); finally
    // store mScaleRadius(+0x6cc). Portable action effects keep no radius
    // field: the store is admitted only while every reached definition has
    // zero ScaleRadius/RadialOffsetScaleRadius, i.e. no draw consumer reads it.
    if(!std::isfinite(op.value)){error="Nonfinite native animation radius";return false;}
    std::vector<uint64_t> stack{op.animationLifetimeHandle},seen;
    while(!stack.empty()){const auto h=stack.back();stack.pop_back();if(std::find(seen.begin(),seen.end(),h)!=seen.end())continue;seen.push_back(h);
        const auto* state=actionEffects_.state(h);if(!state){error="SetRadius target is not an actual live action Animation";return false;}
        for(const char* key:{"ScaleRadius","RadialOffsetScaleRadius"}){const auto* v=state->definition.get(key);if(v&&v->number()!=0){error=std::string("Animation mScaleRadius draw consumer not reconstructed: ")+state->name+" "+key;return false;}}
        if(state->child)stack.push_back(state->child);
        const auto* single=state->definition.get("CreateAnimation");const auto* list=state->definition.get("CreateAnimations");
        for(const auto created:state->created){const auto* c=actionEffects_.state(created);if(!c)continue;bool match=single&&single->text()==c->name;
            if(!match&&list)for(const auto& entry:list->array()){const auto* n=entry.get("Name");if(n&&n->text()==c->name){match=true;break;}}
            if(match)stack.push_back(created);}
    }
    (void)execute;error.clear();return true;
}
bool ScriptRuntime::weaponOperationSupported(const hades::WeaponOutput& op,std::string& error){
    using K=hades::WeaponOutputKind;
    switch(op.kind){
    case K::SetOwnerCompleteAngle:case K::CancelOwnerMovement:case K::CancelOwnerDisables:case K::CancelOwnerRotateDisables:case K::RemoveWeaponControl:case K::ExtendOwnerDisables:case K::RootOwnerMovement:{
        const auto* actor=actorWorld_.actor(op.ownerId);
        if(!actor||!actor->registered||!actor->object||!actor->arsenal||!actor->status){error="Weapon owner operation requires actual registered Unit/body/status/arsenal";return false;}
        if(op.kind==K::SetOwnerCompleteAngle){
            if(!std::isfinite(op.angle)){error="Native complete angle requires finite angle";return false;}
        }
        return true;
    }
    case K::ApplyOwnerPhysics:return applyWeaponPhysics(op,false,error);
    case K::ProjectileSpawn:case K::FinalizeGunProjectile:return true;
    case K::SetWeaponWorldAnimationRadius:return weaponAnimationRadius(op,false,error);
    case K::CheckInvulnerableBarrelImpact:{
        const auto* actor=actorWorld_.actor(op.ownerId);if(!actor||!actor->registered||!actor->object){error="Barrel impact requires actual registered owner";return false;}
        if(!std::isfinite(op.x)||!std::isfinite(op.y)||!std::isfinite(op.angle)||!std::isfinite(op.value)){error="Nonfinite native barrel impact input";return false;}
        return true;
    }
    case K::ApplySelfEffects:case K::CancelWeaponSelfEffects:case K::SetOwnerAnimation:case K::RequestRandomDouble:case K::ConsumeRandomChance:return true;
    case K::QueryNearbyDanger:
    case K::CancelAttachedProjectiles:{const auto* actor=actorWorld_.actor(op.ownerId);if(!actor||!actor->registered){error="PostFire attached scan requires actual owner";return false;}return true;}
    case K::BlinkTeleport:case K::BlinkUpdate:{
        const auto* actor=actorWorld_.actor(op.ownerId);
        if(!actor||!actor->registered||!actor->object||!actor->arsenal){error="BlinkWeapon requires actual registered owner Unit";return false;}
        if(op.kind==K::BlinkTeleport&&(!op.fireRequest||!std::isfinite(op.angle))){error="BlinkWeapon::Fire teleport requires actual request";return false;}
        return true;
    }
    case K::ScriptCallback:
        if(op.name=="OnWeaponCharging"||op.name=="OnWeaponChargeCanceled"||op.name=="OnWeaponTriggerRelease"||op.name=="OnWeaponFired"||op.name=="OnWeaponFailedToFire")return true;
        break;
    case K::CreateAttachedAnimation:case K::CreateWeaponWorldAnimation:
        // Original empty/null lookup returns no animation, not a placeholder.
        if(op.name.empty()||op.name=="null")return true;
        return prepareActionEffect(op.name,op.ownerId,error);
    default:break;
    }
    error="Native weapon operation requires live consumer: kind="+std::to_string(int(op.kind))+" weapon="+op.weaponName+" operation="+op.name;return false;
}
bool ScriptRuntime::consumeWeaponOperation(const hades::WeaponOutput& op,hades::WeaponTriggerReply& reply,std::string& error){
    using K=hades::WeaponOutputKind;
    switch(op.kind){
    case K::CancelOwnerMovement:{
        if(!weaponOperationSupported(op,error))return false;
        // Unit.StopMoving2ebd00, virtual+188: no velocity/shift cancellation.
        auto* actor=actorWorld_.actor(op.ownerId);localMovement_[actor->entityHandle].speedFraction=0;return true;
    }
    case K::SetOwnerCompleteAngle:{
        if(!weaponOperationSupported(op,error))return false;
        auto* actor=actorWorld_.actor(op.ownerId);auto& body=*actor->object;auto& movement=localMovement_[actor->entityHandle];
        // Unit.SetCompleteAngle2f8770: Thing.SetAngle then SetGoalAngle then
        // both cosmetic stores. Body angle wraps; the other three retain raw
        // requested angle. Thing.SetAngle's tail is MarkSortScoreChanged.
        if(!actorWorld_.setAngle(op.ownerId,op.angle,error))return false;
        // SetGoalAngle stores first. Its optional RotationSound compares body
        // angle with the request; after SetAngle the wrapped difference is 0.
        if(!movement.initialized){movement.initialized=true;movement.lastMoveStartX=body.x;movement.lastMoveStartY=body.y;}
        movement.goalAngle=movement.cosmeticAngle=movement.cosmeticAngleTarget=op.angle;return true;
    }
    case K::ExtendOwnerDisables:{
        // Weapon.PostFire VA 180089xxx (native-player-fire-post-fire-pseudocode.c
        // l.454-482): WeaponData+0x6c8 DisableAttackForDuration raises the unit
        // attack timer (+0x468, cancelable +0x462); +0x6b8 DisableOwnerForDuration
        // raises move (+0x464/+0x460), rotate (+0x46c/+0x461) and attack, each max().
        if(!weaponOperationSupported(op,error))return false;
        if(!std::isfinite(op.disableAttackFor)||!std::isfinite(op.disableOwnerFor)){error="Native PostFire disable duration nonfinite";return false;}
        auto& t=actorWorld_.actor(op.ownerId)->status->timers;
        if(op.disableAttackFor>0){t.attack=std::max(t.attack,op.disableAttackFor);t.attackCancelable=true;}
        if(op.disableOwnerFor>0){const float d=op.disableOwnerFor;t.move=std::max(t.move,d);t.moveCancelable=true;t.rotate=std::max(t.rotate,d);t.rotateCancelable=true;t.attack=std::max(t.attack,d);t.attackCancelable=true;}
        return true;
    }
    case K::RootOwnerMovement:{
        // Weapon::HandleTriggerDown VA180086425..450: WeaponData RootOwnerWhileFiring
        // (+0x6c0) && request+0x24 && duration(+0x6c4)>0 -> Unit+0x464 move timer =
        // max(timer,duration) and Unit+0x460 move-cancelable = false (unconditional store).
        if(!weaponOperationSupported(op,error))return false;
        if(!std::isfinite(op.value)||op.value<=0){error="Native root-owner duration invalid";return false;}
        auto& t=actorWorld_.actor(op.ownerId)->status->timers;t.move=std::max(t.move,op.value);t.moveCancelable=false;return true;
    }
    case K::CancelOwnerDisables:case K::CancelOwnerRotateDisables:{
        if(!weaponOperationSupported(op,error))return false;
        auto* actor=actorWorld_.actor(op.ownerId);const auto* weapon=actor->arsenal->weaponState(op.weaponName,error);if(!weapon)return false;
        const auto policy=weapon->definition.fields.find("CanCancelOwnDisables");
        if(policy==weapon->definition.fields.end()||!std::holds_alternative<bool>(policy->second)){error="Native CanCancelOwnDisables unavailable";return false;}
        std::vector<std::string> own;for(const auto& effect:weapon->definition.effects)own.push_back(effect.name);
        const auto clear=[this](hades::UnitStatusEffect& effect,std::string& detail){return selfEffects_.clearEffect(effect.handle,selfEffectProviders(),detail);};
        const bool canOwn=std::get<bool>(policy->second);
        if(op.kind==K::CancelOwnerDisables){if(!actor->status->cancelDisables(own,canOwn,clear,error))return false;}
        else if(!actor->status->cancelRotateDisables(own,canOwn,clear,error))return false;
        const auto canceled=actor->status->snapshot().canceledNames;
        // Weapon.Charge/PostFire call OnEffectCanceled only for nonempty
        // returned names. ScriptManager28e680 filters by OWNER, not weapon,
        // then Script.run supplies weapon name and owner table arguments.
        if(canceled.empty())return true;
        const std::string ownerName=actor->object->name;
        const auto extra=[&](lua_State* L,std::string&){lua_pushstring(L,op.weaponName.c_str());lua_setfield(L,-2,"name");lua_createtable(L,int(canceled.size()),0);
            for(size_t i=0;i<canceled.size();++i){lua_pushstring(L,canceled[i].c_str());lua_rawseti(L,-2,int(i+1));}lua_setfield(L,-2,"EffectNames");return true;};
        if(!dispatchCallbacks("OnEffectCanceled",ownerName.c_str(),op.ownerId,extra)){error=error_;return false;}return true;
    }
    case K::RemoveWeaponControl:{
        if(!weaponOperationSupported(op,error))return false;auto* actor=actorWorld_.actor(op.ownerId);bool cancel=false;
        if(!actor->arsenal->removeWeaponControl(op.name,cancel,error))return false;if(!cancel)return true;
        // Native membership/request mutation precedes cancelChargeOnUnit.
        hades::WeaponCancelCharge execution(*actor->arsenal,op.name);
        for(unsigned i=0;i<256;++i){float speed=0;if(!configurationFloat("SpeedMultiplier",speed,error)||!selfEffects_.refreshUnit(op.ownerId,error))return false;
            hades::WeaponOwnerFrame owner;if(!actorWorld_.weaponOwnerFrame(op.ownerId,worldTime_,speed,false,inputAllowed(),owner,error))return false;
            hades::WeaponCancelChargeStep step;if(!execution.advance(owner,[this](const hades::WeaponOutput& next,std::string& detail){return weaponOperationSupported(next,detail);},step,error))return false;
            if(step.progress==hades::WeaponCancelChargeProgress::Finished)return true;
            hades::WeaponTriggerReply nested;if(!consumeWeaponOperation(step.operation,nested,error))return false;
        }
        error="Native control-removal charge cancellation exceeded operation bound";return false;
    }
    case K::ApplyOwnerPhysics:return applyWeaponPhysics(op,true,error);
    case K::SetWeaponWorldAnimationRadius:return weaponAnimationRadius(op,true,error);
    case K::CheckInvulnerableBarrelImpact:{
        // GunWeapon::Fire VA18007fedf..18008003f: first obstacle containing the
        // barrel point, then (pAnim data+0x8a OwnerInvulnerable || pLife+0x31
        // IsInvulnerable) && ObstacleData AllowInvulnerableImpacts(+0x553) ->
        // owner pPhysics ApplyForce2bd070(angle+pi, v, false, v, 0) where v is
        // ProjectileData InvulnerableImpactOwnerVelocity(+0x634).
        if(!weaponOperationSupported(op,error))return false;
        int32_t hit=0;if(!hades::barrelObstacleAt(actorWorld_,op.x,op.y,hit,error))return false;
        if(!hit){error.clear();return true;}
        bool invulnerable=false,noArmor=false,noCoverage=false;if(!primaryCombatFlags(hit,invulnerable,noArmor,noCoverage,error))return false;
        const auto* obstacle=actorWorld_.actor(hit);if(!obstacle){error="Barrel obstacle lost";return false;}
        invulnerable=invulnerable||(obstacle->hasLife&&obstacle->invulnerable);
        const auto* allow=actorWorld_.currentProperty(hit,"AllowInvulnerableImpacts");if(!allow){error="Actual ObstacleData AllowInvulnerableImpacts unavailable";return false;}
        if(!invulnerable||!allow->boolean()){error.clear();return true;}
        auto* owner=actorWorld_.actor(op.ownerId);if(!owner->physics){error.clear();return true;}
        hades::PhysicsForceOwner data;
        const auto property=[&](const char* key,float& value){const auto* p=actorWorld_.currentProperty(op.ownerId,key);if(!p){error=std::string("Barrel force owner field unavailable: ")+key;return false;}value=float(p->number());return std::isfinite(value);};
        if(!property("Thing.ExternalForceMultiplier",data.forceMultiplier)||!property("Thing.SpinFactor",data.impactRotation))return false;
        {const auto* immune=actorWorld_.currentProperty(op.ownerId,"Thing.ImmuneToForce");if(!immune){error="Barrel force owner ImmuneToForce unavailable";return false;}data.thingImmuneToForce=immune->boolean();}
        if(owner->animation.active()){const auto immobile=animationBank_->exactOwnerImmobile(owner->animation.clipIndex());if(!immobile){error="Barrel force primary OwnerImmobile unavailable";return false;}data.animationOwnerImmobile=*immobile;}
        else if(owner->primaryEffectAnimation||owner->primaryAnimationLifetime){if(!owner->primaryOwnerImmobile){error="Barrel force source primary OwnerImmobile unavailable";return false;}data.animationOwnerImmobile=*owner->primaryOwnerImmobile;}
        if(owner->definition&&owner->definition->kind=="Unit"&&owner->status)for(const auto h:owner->status->snapshot().active){const auto* effect=selfEffects_.effectData(h,error);if(!effect)return false;const auto f=effect->fields.find("ImmuneToForce");if(f==effect->fields.end()||!std::holds_alternative<bool>(f->second)){error="Actual effect ImmuneToForce unavailable";return false;}if(std::get<bool>(f->second))data.effectImmuneToForce=true;}
        hades::PhysicsForceCallbacks callbacks;
        callbacks.addComponent=[owner](std::string& detail){owner->physicsPending=true;detail.clear();return true;};
        callbacks.onForce=[this,&op](float,std::string& detail){return onActorForce(op.ownerId,true,detail);};
        volatile float reverse=op.angle+3.1415927410125732f;
        return hades::applyPhysicsAngleForce(*owner->physics,data,reverse,op.value,false,op.value,0,callbacks,error);
    }
    case K::ProjectileSpawn:{uint64_t handle=0;if(!createWeaponProjectile(op,handle,error))return false;
        // BlinkWeapon::FireProjectile VA18007e879: Projectile+0x3b2 mUseStartMarker=false after Create.
        if(op.clearUseStartMarker){auto* p=projectileWorld_.actor(handle);if(!p){error="Blink projectile lost after Create";return false;}p->state.useStartMarker=false;}
        reply.createdProjectile=handle;return true;}
    case K::BlinkTeleport:{bool next=false;if(!blinkTeleport(op,next,error))return false;reply.blinkFireContinues=next;return true;}
    case K::BlinkUpdate:return blinkUpdate(op,error);
    case K::QueryNearbyDanger:{
        // Weapon::GetNearbyDanger877a0 (.local/reports/native-combat4-weapon-get-nearby-danger-static.json):
        // ProjectileManager active list: AffectsUnit6b200(owner) && ProjectileData DangerDistance(+0x650)>0
        // -> blast/range geometry; then UnitManager::ENEMY_UNITS with primary AnimationData
        // DangerDistance(+0x2dc)>0 -> distance test; else 0. Only the zero-candidate result is reconstructed.
        for(const auto h:projectileWorld_.active()){const auto* p=projectileWorld_.actor(h);if(!p||p->disposed)continue;
            if(!p->firingArsenal){error="GetNearbyDanger projectile source data unavailable";return false;}
            const auto* data=p->firingArsenal->projectileData(p->request.weaponName,p->spawnedDefinition,error);if(!data)return false;
            const auto v=data->fields.find("DangerDistance");
            if(v==data->fields.end()||!std::holds_alternative<double>(v->second)){error="GetNearbyDanger projectile DangerDistance unavailable";return false;}
            if(std::get<double>(v->second)>0){error="Weapon::GetNearbyDanger projectile danger geometry not reconstructed";return false;}}
        for(auto id:actorWorld_.registrationOrder()){if(id==op.ownerId)continue;const auto* u=actorWorld_.actor(id);if(!u||!u->registered||!u->definition||u->definition->kind!="Unit")continue;
            if(u->animation.active()){const auto def=actorAnimationDefinitions_.find(u->animation.clip()->name);if(def==actorAnimationDefinitions_.end()){error="GetNearbyDanger unit primary definition unavailable";return false;}
                const auto* d=def->second.get("definition");const auto* dd=d?d->get("DangerDistance"):nullptr;if(dd&&dd->number()>0){error="Weapon::GetNearbyDanger enemy-unit danger test not reconstructed";return false;}}
            else if(const auto* a=actionEffects_.state(u->primaryAnimationLifetime);a&&a->primary){const auto* dd=a->definition.get("DangerDistance");if(dd&&dd->number()>0){error="Weapon::GetNearbyDanger enemy-unit danger test not reconstructed";return false;}}
            else if(const auto* e=effects_.entity(u->primaryEffectAnimation)){const auto* dd=e->definition->source.get("DangerDistance");if(dd&&dd->number()>0){error="Weapon::GetNearbyDanger enemy-unit danger test not reconstructed";return false;}}
            else if(const auto* s=scenePrimaries_.state(u->primaryAnimationLifetime)){const auto* dd=s->definition.get("DangerDistance");if(dd&&dd->number()>0){error="Weapon::GetNearbyDanger enemy-unit danger test not reconstructed";return false;}}
            else if(u->primaryEffectAnimation||u->primaryAnimationLifetime){error="GetNearbyDanger primary AnimationData unavailable: unit "+std::to_string(id)+" "+u->object->name+" animation="+u->object->animation+" effect="+std::to_string(u->primaryEffectAnimation)+" lifetime="+std::to_string(u->primaryAnimationLifetime);return false;}}
        error.clear();return true;}
    case K::CancelAttachedProjectiles:{
        const auto* owner=actorWorld_.actor(op.ownerId);if(!owner){error="PostFire attached scan owner lost";return false;}
        return projectileWorld_.cancelAttached(op.ownerId,owner->entityHandle,[](const hades::ProjectileActor& p,bool& live,std::string& e){
            const auto* w=p.firingArsenal->weaponState(p.request.weaponName,e);if(!w)return false;live=w->currentFireRequest.has_value();return true;},error);}
    case K::FinalizeGunProjectile:{
        auto* p=projectileWorld_.actor(op.projectileLifetimeHandle);if(!p||!p->shotInitialized||p->disposed){error="GUN continuation lost actual constructed projectile";return false;}
        p->state.waveIndex=op.waveIndex;
        if(op.addCritBonus)p->state.critBonus+=op.critBonus;
        if(op.setAdditiveDamageMultiplier)p->state.additiveDamageMultiplier=op.additiveDamageMultiplier;
        p->state.blastRadiusModifier=op.blastRadiusMultiplier;
        if(op.delayedProjectile){bool player=false;auto* owner=actorWorld_.actor(op.ownerId);for(const auto& entry:snapshotInputPlayers(state_))if(owner&&entry.activeUnitId==op.ownerId&&entry.activeUnitLifetime==owner->entityHandle)player=true;
            if(player&&!owner->arsenal->setDelayedProjectile(op.weaponName,op.projectileLifetimeHandle,error))return false;}
        return true;
    }
    case K::ApplySelfEffects:{hades::SelfEffectAdmissionResult result;return selfEffects_.applyWeaponEffects(op.ownerId,op.weaponName,int32_t(op.value),selfEffectProviders(),result,error);}
    case K::CancelWeaponSelfEffects:return selfEffects_.cancelWeaponEffects(op.ownerId,op.weaponName,int32_t(op.value),selfEffectProviders(),error);
    case K::SetOwnerAnimation:return setActorAnimation(op.ownerId,op.name,error);
    case K::ScriptCallback:return weaponCallback(op,error);
    case K::RequestRandomDouble:{double value=0;if(!nextRecoveredEngineRandom(state_,value)){error="Native weapon random source unavailable";return false;}reply.randomDouble=value;return true;}
    case K::ConsumeRandomChance:{double value=0;if(!nextRecoveredEngineRandom(state_,value)){error="Native charge sound random source unavailable";return false;}
        // Charge cursor currently proves all selected charge sound cues invalid.
        // The original chance draw still occurs even when chance is zero.
        return true;}
    case K::CreateAttachedAnimation:case K::CreateWeaponWorldAnimation:
        if(op.name.empty()||op.name=="null"){reply.createdAnimation=uint64_t(0);return true;}
        {uint64_t handle=0;std::optional<std::array<float,3>> position;
        if(op.kind==K::CreateWeaponWorldAnimation)position=std::array<float,3>{op.x,op.y,op.z};
        if(!createActionEffect(op.name,op.ownerId,position,handle,error))return false;
        // Weapon::Charge VA180088xxx: null Create skips both follow-ups. A live
        // ChargeStartFx needs PlaySpeedMultiplier component = Animation vfunc+0xe0
        // / ChargeTime(+0x4f4); a live ChargeFx needs Animation::SetDuration298310
        // (mChargeTimeRemaining); both then PreferUnmodifiedTime29a5c0 when the
        // owner UnitData byte +0x69 is set: full-data offset 0x69 is
        // UnitData PlayerControlled (native-actor-compiled-defaults-v4.json).
        // .local/reports/native-player-fire-charge-pseudocode.c lines 128-164.
        if(handle&&op.chargeFxRole){
            if(op.chargeFxRole==1){
                float remaining=0;if(!actionEffects_.timeRemaining(handle,remaining,error))return false;
                if(!(op.value!=0)){error="Weapon::Charge ChargeStartFx divides by actual ChargeTime 0";return false;}
                if(!actionEffects_.setPlaySpeedMultiplier(handle,remaining/op.value,error))return false;
            }else if(!actionEffects_.setDuration(handle,op.x,error))return false;
            const auto* player=actorWorld_.currentProperty(op.ownerId,"PlayerControlled");if(!player){error="Weapon::Charge owner UnitData PlayerControlled unavailable";return false;}
            if(player->boolean()&&!actionEffects_.preferUnmodifiedTime(handle,error))return false;
        }
        reply.createdAnimation=handle;return true;}
    default:break;
    }
    error="Native weapon yielded an unsupported consumer";return false;
}
bool ScriptRuntime::advanceWeapons(float dt,std::string& error){
    try{
        float speed=0;if(!configurationFloat("SpeedMultiplier",speed,error))return false;
        // The Android sequential manager uses actual actor registration order.
        // Source per-arsenal creation/priority order is owned by its cursor.
        for(auto id:actorWorld_.registrationOrder()){
            auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->arsenal||actor->arsenal->empty())continue;
            if(actor->timeModifierFraction!=1||actor->timeMultiplier!=1){error="Unit weapon elapsed requires non-default source time adapter";return false;}
            if(!actor->arsenal->clearDelayedProjectiles([this](uint64_t h,bool& clear,std::string& e){const auto* p=projectileWorld_.actor(h);clear=!p||p->state.hasStarted||p->state.detonated||p->lifeStatus==1;e.clear();return true;},error))return false;
            hades::WeaponArsenalFrameExecution execution(*actor->arsenal,dt);hades::WeaponTriggerReply reply;
            const auto support=[this](const hades::WeaponOutput& op,std::string& e){return weaponOperationSupported(op,e);};
            bool done=false;
            for(unsigned operation=0;operation<16384;++operation){
                if(!selfEffects_.refreshUnit(id,error))return false;
                hades::WeaponArsenalFrame frame;
                if(!actorWorld_.weaponOwnerFrame(id,worldTime_,speed,false,inputAllowed(),frame.owner,error))return false;
                const auto* continuous=actorWorld_.currentProperty(id,"ContinuousWeapon");const auto* priority=actorWorld_.currentProperty(id,"PriorityWeapon");
                if(!continuous||!priority){error="Actual Unit arsenal ordering data unavailable";return false;}
                frame.continuousWeapon=continuous->text();frame.priorityWeapon=priority->text();
                hades::WeaponArsenalFrameStep step;
                if(!execution.advance(frame,reply,support,step,error))return false;reply={};
                if(step.progress!=hades::WeaponArsenalFrameProgress::Yielded){done=true;break;}
                if(!consumeWeaponOperation(step.operation,reply,error))return false;
                if(!step.expectsRandomReply)reply.randomDouble.reset();
                if(!step.expectsProjectileReply)reply.createdProjectile.reset();
                if(!step.expectsAnimationReply)reply.createdAnimation.reset();
                if(!step.expectsBlinkReply)reply.blinkFireContinues.reset();
            }
            if(!done){error="Native arsenal frame exceeded operation bound";return false;}
        }
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
// Unit::SetLocation2f2a40 (Unit vtable +0x80, ??_7Unit@sgg@@6B@ RVA0x5f9918): the same
// body/geometry/attached-child update the local movement path performs.
bool ScriptRuntime::unitSetLocation(int32_t id,float x,float y,std::string& error){
    auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object){error="Unit::SetLocation requires actual registered Unit";return false;}
    if(!std::isfinite(x)||!std::isfinite(y)){error="Unit::SetLocation nonfinite target";return false;}
    if(a->attachedParent){error="Unit::SetLocation attached-parent relative store not reconstructed";return false;}
    auto& body=*a->object;body.x=x;body.y=y;body.graphicsDirty=body.geometryDirty=true;
    if(!actorWorld_.refreshGeometry(id,error)||!actorWorld_.propertiesDirty(id,error))return false;
    return projectileWorld_.ownerMoved(id,error);
}
bool ScriptRuntime::blinkTeleport(const hades::WeaponOutput& op,bool& fireContinues,std::string& error){
    // BlinkWeapon::Fire VA18007d8e2..7dec1 (.local/reports/native-combat4-blink-fire-pseudocode.c),
    // GetTeleportTarget7d540 (native-combat4-blink-teleport-target-pseudocode.c).
    try{
    fireContinues=false;
    auto* a=actorWorld_.actor(op.ownerId);if(!a||!a->registered||!a->object||!a->arsenal){error="BlinkWeapon::Fire lost owner";return false;}
    const auto* w=a->arsenal->weaponState(op.weaponName,error);if(!w)return false;
    const auto& d=w->definition;const auto& request=*op.fireRequest;
    const auto num=[&](const auto& fields,const char* k){auto f=fields.find(k);if(f==fields.end()||!std::holds_alternative<double>(f->second))throw std::runtime_error(std::string("BlinkWeapon field unavailable: ")+k);const float v=float(std::get<double>(f->second));if(!std::isfinite(v))throw std::runtime_error("Nonfinite BlinkWeapon field");return v;};
    const auto flag=[&](const auto& fields,const char* k){auto f=fields.find(k);if(f==fields.end()||!std::holds_alternative<bool>(f->second))throw std::runtime_error(std::string("BlinkWeapon flag unavailable: ")+k);return std::get<bool>(f->second);};
    const auto text=[&](const char* k){auto f=d.fields.find(k);if(f==d.fields.end()||!std::holds_alternative<std::string>(f->second))throw std::runtime_error(std::string("BlinkWeapon text unavailable: ")+k);return std::get<std::string>(f->second);};
    float sx=0,sy=0;if(a->attachedParent)throw std::runtime_error("BlinkWeapon attached-owner GetLocation not reconstructed");
    if(!actorWorld_.location(op.ownerId,sx,sy,error))return false;
    // GetTeleportTarget: WeaponRange(+0x83c); ValidProjectileData && 0 -> ProjectileData Range(+0x3a4);
    // times each active effect's BlinkRangeMultiplier (EffectData+0x1bc = EffectDataDef 0x154).
    float range=num(d.fields,"WeaponRange");
    if(w->validProjectileData&&range==0)range=num(w->projectileData.fields,"Range");
    if(a->status)for(const auto h:a->status->snapshot().active){const auto* e=selfEffects_.effectData(h,error);if(!e)return false;range*=num(e->fields,"BlinkRangeMultiplier");}
    float tx=0,ty=0;
    if(w->validProjectileData&&flag(w->projectileData.fields,"AutoAdjustForTarget")){
        // ProjectileData+0x664 AutoAdjustForTarget: target entity location, else request target.
        if(request.targetId!=-1)throw std::runtime_error("BlinkWeapon AutoAdjustForTarget entity target not reconstructed");
        tx=request.targetX;ty=request.targetY;
    }else{tx=std::cos(request.angle)*range+sx;ty=-std::sin(request.angle)*range+sy;}
    // target==(0,0) skips every blink store (VA18007d9d3..db).
    if(tx==0&&ty==0){fireContinues=true;error.clear();return true;}
    auto* blink=a->arsenal->blinkState(op.weaponName,error);if(!blink)return false;
    if(blink->remaining>0&&!blink->endpointDetonated&&flag(d.fields,"BlinkDetonateAtEndpoint"))throw std::runtime_error("BlinkWeapon::Fire in-progress endpoint FireProjectile not reconstructed");
    // Unit::IsLocationValid2f20f0 (vtable +0xa8) with (target, 1, 4, false).
    bool valid=false;if(!unitLocationValid(op.ownerId,tx,ty,valid,error))return false;
    float distance=0;
    if(!valid){
        // limit = BlinkMaxRange>0 ? start + Thing.mDirection(+0xb0)*BlinkMaxRange : target.
        // Thing::SetAngle2e0ce0 stores mDirection=(cos body angle, -sin body angle) (VA1802e0e29..47).
        float lx=tx,ly=ty;const float maxRange=num(d.fields,"BlinkMaxRange");
        if(maxRange>0){const float angle=a->object->angle;lx=std::cos(angle)*maxRange+sx;ly=-std::sin(angle)*maxRange+sy;}
        distance=std::sqrt((sx-tx)*(sx-tx)+(sy-ty)*(sy-ty));
        float ox=0,oy=0;if(!thingCalcValidLocationOnVector(op.ownerId,tx,ty,sx,sy,lx,ly,0.1745329201221466f,ox,oy,error))return false;
        if(ox==0&&oy==0){tx=sx;ty=sy;
            if(blink->remaining>0){fireContinues=false;error.clear();return true;} // FireResult 0 VA18007dafd
        }else{distance-=std::sqrt((sx-ox)*(sx-ox)+(sy-oy)*(sy-oy));tx=ox;ty=oy;}
    }
    if(num(d.fields,"BlinkCameraLerpTime")>0)throw std::runtime_error("BlinkWeapon::Fire BlinkCameraLerpTime camera follow lerp not reconstructed");
    if(distance>0){
        // VA18007dc63..dddf: SetLocation(target), shift=(cos a,-sin a)*remaining distance,
        // ApplyFullShift(GetLocation+shift, shift, false, false), target=GetLocation, SetLocation(start).
        if(!unitSetLocation(op.ownerId,tx,ty,error))return false;
        const float shx=std::cos(request.angle)*distance,shy=-std::sin(request.angle)*distance;
        {float lx0=0,ly0=0;if(!actorWorld_.location(op.ownerId,lx0,ly0,error))return false;
        if(!unitApplyFullShift(op.ownerId,lx0+shx,ly0+shy,shx,shy,false,false,error))return false;}
        if(!actorWorld_.location(op.ownerId,tx,ty,error))return false;
        if(!unitSetLocation(op.ownerId,sx,sy,error))return false;
    }else{
        const auto fx=text("UnblockedBlinkFx");
        if(!fx.empty()){
            bool blocked=false;if(!gridLineBlocked(sx,sy,tx,ty,blocked,error))return false;
            if(!blocked&&fx!="null"){
                // FireRequest::GetFromLocation83020: request from (+0x10) when nonzero,
                // else owner Thing::GetLocation + Weapon::GetFireOffset86010(request angle).
                float fx0=request.fromX,fy0=request.fromY;
                if(fx0==0&&fy0==0){hades::WeaponOwnerFrame frame;float speed=0;if(!configurationFloat("SpeedMultiplier",speed,error)||!actorWorld_.weaponOwnerFrame(op.ownerId,worldTime_,speed,false,inputAllowed(),frame,error))return false;
                    hades::WeaponPoint origin;if(!a->arsenal->fireOrigin(op.weaponName,frame,request.angle,origin,error))return false;fx0=origin.x;fy0=origin.y;}
                if(!prepareActionEffect(fx,op.ownerId,error))return false;
                uint64_t handle=0;if(!createActionEffect(fx,op.ownerId,std::array<float,3>{fx0,fy0,0},handle,error))return false;
            }
        }
    }
    a=actorWorld_.actor(op.ownerId);if(!a||!a->arsenal){error="BlinkWeapon owner lost during UnblockedBlinkFx";return false;}
    blink=a->arsenal->blinkState(op.weaponName,error);if(!blink)return false;
    w=a->arsenal->weaponState(op.weaponName,error);if(!w)return false;
    hades::log(hades::Severity::Info,"HadesLua","NATIVE_BLINK_FIRE weapon=%s start=%.2f,%.2f target=%.2f,%.2f valid=%d angle=%.4f",op.weaponName.c_str(),sx,sy,tx,ty,int(valid),request.angle);
    blink->startX=sx;blink->startY=sy;blink->targetX=tx;blink->targetY=ty;blink->angle=request.angle;
    blink->remaining=num(w->definition.fields,"BlinkDuration");blink->interval=num(w->definition.fields,"BlinkDetonateOnInterval");blink->endpointDetonated=false;
    fireContinues=true;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ScriptRuntime::blinkUpdate(const hades::WeaponOutput& op,std::string& error){
    // BlinkWeapon::Update7d1c0 body after Weapon::Update83b60
    // (.local/reports/native-combat4-blink-update-pseudocode.c).
    try{
    auto* a=actorWorld_.actor(op.ownerId);if(!a||!a->registered||!a->object||!a->arsenal){error="BlinkWeapon::Update lost owner";return false;}
    auto* blink=a->arsenal->blinkState(op.weaponName,error);if(!blink)return false;
    if(!(blink->remaining>0)){error.clear();return true;}
    const auto* w=a->arsenal->weaponState(op.weaponName,error);if(!w)return false;const auto& d=w->definition;
    const auto num=[&](const char* k){auto f=d.fields.find(k);if(f==d.fields.end()||!std::holds_alternative<double>(f->second))throw std::runtime_error(std::string("BlinkWeapon field unavailable: ")+k);return float(std::get<double>(f->second));};
    const auto flag=[&](const char* k){auto f=d.fields.find(k);if(f==d.fields.end()||!std::holds_alternative<bool>(f->second))throw std::runtime_error(std::string("BlinkWeapon flag unavailable: ")+k);return std::get<bool>(f->second);};
    const float dt=op.value,old=blink->remaining;blink->remaining=old-dt;
    const float t=std::clamp(1.0f-(old-dt)/num("BlinkDuration"),0.0f,1.0f);
    float lx=0,ly=0;if(a->attachedParent)throw std::runtime_error("BlinkWeapon attached-owner GetLocation not reconstructed");
    if(!actorWorld_.location(op.ownerId,lx,ly,error))return false;
    // sgg::Interpolate1eaba0(start,end,t,smooth=false,BlinkEaseIn+0x914,BlinkEaseOut+0x918).
    hades::BodyTransitionOptions ease;ease.easeIn=num("BlinkEaseIn");ease.easeOut=num("BlinkEaseOut");
    const float x=hades::bodyInterpolate(blink->startX,blink->targetX,t,ease),y=hades::bodyInterpolate(blink->startY,blink->targetY,t,ease);
    if(!unitSetLocation(op.ownerId,x,y,error))return false;
    a=actorWorld_.actor(op.ownerId);blink=a->arsenal->blinkState(op.weaponName,error);if(!blink)return false;
    if(num("BlinkDetonateOnInterval")>0)throw std::runtime_error("BlinkWeapon::Update BlinkDetonateOnInterval FireProjectile not reconstructed");
    if(flag("BlinkDetonateAtEndpoint")){const float delay=num("BlinkDetonateAtEndpointDelay");if(delay<=0&&-delay>blink->remaining&&!blink->endpointDetonated)throw std::runtime_error("BlinkWeapon::Update early endpoint FireProjectile not reconstructed");}
    if(blink->remaining<0){
        // OnBlinkFinished7dff0(start, angle) (.local/reports/native-combat4-blink-finished-pseudocode.c).
        const float sx=blink->startX,sy=blink->startY;
        float cx=0,cy=0;if(!actorWorld_.location(op.ownerId,cx,cy,error))return false;
        if(sx!=cx||sy!=cy){
            constexpr float tau=6.2831854820251465f;
            blink->lastRadius=std::sqrt((sx-cx)*(sx-cx)+(sy-cy)*(sy-cy));
            const float angle=std::atan2((cy-sy)*-1.0f,cx-sx);blink->lastAngle=angle-std::floor(angle/tau)*tau;
            const float reaction=num("CheckMovementReactionDistance");
            if(reaction>0){if(!gridCheckMovementReactions(sx,sy,cx,cy,reaction+reaction,error))return false;}
        }
        a=actorWorld_.actor(op.ownerId);blink=a->arsenal->blinkState(op.weaponName,error);if(!blink)return false;
        if(w->validProjectileData&&flag("BlinkDetonateAtEndpoint")&&num("BlinkDetonateAtEndpointDelay")>=0)throw std::runtime_error("BlinkWeapon::OnBlinkFinished endpoint FireProjectile not reconstructed");
        // BlinkNumGhosts x AnimationManager::Create(BlinkGhostAnimation, owner, lerp(start,loc,i/n)).
        const float ghosts=num("BlinkNumGhosts");
        {auto g=d.fields.find("BlinkGhostAnimation");if(g==d.fields.end()||!std::holds_alternative<std::string>(g->second))throw std::runtime_error("BlinkGhostAnimation unavailable");
            const std::string ghost=std::get<std::string>(g->second);
            float f=0;int i=0;if(ghosts>0)do{f=f/ghosts;const float gx=(cx-sx)*f+sx,gy=(cy-sy)*f+sy;
                if(!ghost.empty()&&ghost!="null"){uint64_t h=0;if(!prepareActionEffect(ghost,op.ownerId,error)||!createActionEffect(ghost,op.ownerId,std::array<float,3>{gx,gy,0},h,error))return false;}
                f=float(++i);}while(f<ghosts);}
        a=actorWorld_.actor(op.ownerId);if(!a){error="Blink owner lost";return false;}
        auto p=d.fields.find("PostBlinkAnim");if(p==d.fields.end()||!std::holds_alternative<std::string>(p->second))throw std::runtime_error("PostBlinkAnim unavailable");
        const std::string post=std::get<std::string>(p->second);
        if(post.empty()){
            // VA18007e3xx: owner pAnim data name == FireGraphic(+0x268) -> Animation+0x63 mCanceled=1
            // (+ pAttachedToUnit AllowInput restore when the data flag at +0x83 is set).
            auto fg=d.fields.find("FireGraphic");const std::string fire=fg!=d.fields.end()&&std::holds_alternative<std::string>(fg->second)?std::get<std::string>(fg->second):std::string{};
            if(a->animation.active()&&a->animation.clip()->name==fire)throw std::runtime_error("BlinkWeapon::OnBlinkFinished FireGraphic primary cancel not reconstructed: "+fire);
        }else if(!setActorAnimation(op.ownerId,post,error))return false; // Thing::SetAnimation2decb0(owner,PostBlinkAnim,0,1)
        a=actorWorld_.actor(op.ownerId);blink=a->arsenal->blinkState(op.weaponName,error);if(!blink)return false;
        blink->targetX=blink->targetY=0;
    }
    error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
namespace {
float blinkCross(hades::MovementPoint a,hades::MovementPoint b,hades::MovementPoint c){return(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool blinkPointInside(hades::MovementPoint p,const std::vector<hades::MovementPoint>& v){bool yes=false;for(size_t i=0,j=v.size()-1;i<v.size();j=i++)if((v[i].y>p.y)!=(v[j].y>p.y)&&p.x<(v[j].x-v[i].x)*(p.y-v[i].y)/(v[j].y-v[i].y)+v[i].x)yes=!yes;return yes;}
}
// Unit::IsLocationValid2f20f0 (vtable +0xa8) = IsDestinationValid2f23f0(...)==9: the Unit's
// polygon translated to the destination, GridManager::DetectCollisions, Obstacle::StopsUnits
// (.local/reports/native-combat4-unit-is-location-valid-static.json,
// native-combat4-unit-is-destination-valid-static.json). Uses the same transformed source
// polygons and overlap test as the local Unit movement sweep (unit_movement.cpp).
bool ScriptRuntime::unitLocationValid(int32_t id,float x,float y,bool& valid,std::string& error){
    try{
    valid=false;auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object){error="IsLocationValid requires actual Unit";return false;}
    if(!actorWorld_.refreshGeometry(id,error))return false;if(a->geometry.size()<3)throw std::runtime_error("IsLocationValid actual Unit polygon unavailable");
    float sx=0,sy=0;if(!actorWorld_.location(id,sx,sy,error))return false;
    std::vector<hades::MovementPoint> hull;hades::SpatialBounds bounds{x,y,x,y};
    for(const auto& q:a->geometry){hades::MovementPoint v{q.x+x-sx,q.y+y-sy};hull.push_back(v);bounds.minX=std::min(bounds.minX,v.x);bounds.maxX=std::max(bounds.maxX,v.x);bounds.minY=std::min(bounds.minY,v.y);bounds.maxY=std::max(bounds.maxY,v.y);}
    std::vector<int32_t> candidates;if(!actorWorld_.spatialObstacleCandidates(bounds,hades::ObstacleGridCategory::StopsUnits,candidates,error))return false;
    for(auto o:candidates){auto* obstacle=actorWorld_.actor(o);if(!obstacle||!obstacle->registered||!obstacle->object||!obstacle->object->alive||o==id)continue;
        bool stops=false;if(!actorWorld_.obstacleStops(o,false,stops,error))return false;if(!stops)continue;
        if(obstacle->geometry.size()<3)throw std::runtime_error("IsLocationValid blocking obstacle polygon unavailable");
        std::vector<hades::MovementPoint> shape;for(const auto& v:obstacle->geometry)shape.push_back({v.x,v.y});
        if(hades::movementPolygonsOverlap(hull,shape)){lastLocationBlocker_=o;valid=false;error.clear();return true;}}
    valid=true;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
// GridManager::IsLineBlocked308230 (.local/reports/native-combat4-grid-is-line-blocked-static.json):
// (pseudocode native-combat4-grid-is-line-blocked-pseudocode.c): obstacles in the grid cells
// of the segment bounds, Obstacle::StopsUnits2c55b0, PolygonHelpers::PolygonCollision2ce630
// (segment vs polygon) or PointInPolygon2ceaf0(polygon, start).
bool ScriptRuntime::gridLineBlocked(float x0,float y0,float x1,float y1,bool& blocked,std::string& error){
    try{
    blocked=false;hades::SpatialBounds bounds{std::min(x0,x1),std::min(y0,y1),std::max(x0,x1),std::max(y0,y1)};
    std::vector<int32_t> candidates;if(!actorWorld_.spatialObstacleCandidates(bounds,hades::ObstacleGridCategory::StopsUnits,candidates,error))return false;
    const hades::MovementPoint p{x0,y0},q{x1,y1};
    for(auto o:candidates){auto* obstacle=actorWorld_.actor(o);if(!obstacle||!obstacle->registered||!obstacle->object||!obstacle->object->alive)continue;
        bool stops=false;if(!actorWorld_.obstacleStops(o,false,stops,error))return false;if(!stops)continue;
        if(obstacle->geometry.size()<3)throw std::runtime_error("IsLineBlocked blocking obstacle polygon unavailable");
        std::vector<hades::MovementPoint> shape;for(const auto& v:obstacle->geometry)shape.push_back({v.x,v.y});
        if(blinkPointInside(p,shape)){blocked=true;break;} // PointInPolygon2ceaf0(polygon, start) only
        for(size_t i=0;i<shape.size()&&!blocked;++i){const auto u=shape[i],v=shape[(i+1)%shape.size()];
            if((blinkCross(p,q,u)>0)!=(blinkCross(p,q,v)>0)&&(blinkCross(u,v,p)>0)!=(blinkCross(u,v,q)>0))blocked=true;}
        if(blocked)break;}
    error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
// GridManager::CheckMovementReactions309850 (.local/reports/native-combat4-grid-check-movement-reactions-pseudocode.c):
// Polygon::FromThickLine62e10(from,to,width): unit direction d, n=(-d.y,d.x)*0.5*width;
// points from+n, to+n, to-n, from-n. AABB floor/ceil -> GridManager::GetObstacles3071b0(All);
// width==0 -> PolygonCollision(segment), else PolygonCollisionOpt2cefd0 (both polygons nonempty);
// a hit runs Obstacle::CheckMovementReactionsWithChildren2c37e0 (ActorWorld::movementReactionTargets);
// a target with AllowMovementReaction needs OnMovementReaction (not reconstructed: fails loudly).
bool ScriptRuntime::gridCheckMovementReactions(float x0,float y0,float x1,float y1,float width,std::string& error){
    try{
    float dx=x1-x0,dy=y1-y0;const float len=std::sqrt(dy*dy+dx*dx);dx/=len;dy/=len;
    const float nx=dy*(width*-0.5f),ny=dx*(width*0.5f);
    std::vector<hades::MovementPoint> rect{{x0+nx,y0+ny},{x1+nx,y1+ny},{x1-nx,y1-ny},{x0-nx,y0-ny}};
    float minX=rect[0].x,minY=rect[0].y,maxX=minX,maxY=minY;for(const auto& q:rect){minX=std::min(minX,q.x);minY=std::min(minY,q.y);maxX=std::max(maxX,q.x);maxY=std::max(maxY,q.y);}
    const float left=std::floor(minX),top=std::floor(minY);const float w=float(int(std::ceil(maxX+0.f))-int(left)),h=float(int(std::ceil(maxY+0.f))-int(top));
    std::vector<int32_t> candidates;if(!actorWorld_.spatialObstacleCandidates(hades::SpatialBounds{left,top,left+w,top+h},hades::ObstacleGridCategory::All,candidates,error))return false;
    for(auto o:candidates){const auto* obstacle=actorWorld_.actor(o);if(!obstacle||!obstacle->registered||obstacle->geometry.empty())continue;
        std::vector<hades::MovementPoint> shape;for(const auto& v:obstacle->geometry)shape.push_back({v.x,v.y});
        bool hit=false;
        if(width==0){const hades::MovementPoint p{x0,y0},q{x1,y1};for(size_t i=0;i<shape.size()&&!hit;++i){const auto u=shape[i],v=shape[(i+1)%shape.size()];if((blinkCross(p,q,u)>0)!=(blinkCross(p,q,v)>0)&&(blinkCross(u,v,p)>0)!=(blinkCross(u,v,q)>0))hit=true;}}
        else hit=shape.size()>=3?hades::movementPolygonsOverlap(shape,rect):false;
        if(hit){std::vector<int32_t> targets;if(!actorWorld_.movementReactionTargets(o,targets,error))return false;
            if(!targets.empty())throw std::runtime_error("ScriptManager::OnMovementReaction28b060 not reconstructed: obstacle "+std::to_string(targets.front()));}
    }
    error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

// Thing::CalcValidLocationOnVector2e1770 (.local/reports/native-combat4-thing-calc-valid-location-static.json),
// every probe through Unit::IsLocationValid(p,1,4,false) (vtable +0xa8):
// A k=1..9: target+(limit-target)*k/10; B k=1..8: target+(start-target)*k/10; if angleStep>0:
// d=limit-start, base=wrap(atan2(-d.y,d.x)), P1/P2=start+|d|*(cos,-sin)(base+-step);
// C k=1..9: P1+(P1-P1)*k/10 then P2+(P2-P2)*k/10 (the binary's zero-length lerp, VA1802e1a08/16);
// D k=1..8: P1+(start-P1)*k/10 then P2+(start-P2)*k/10. None valid -> (0,0).
bool ScriptRuntime::thingCalcValidLocationOnVector(int32_t id,float tx,float ty,float sx,float sy,float lx,float ly,float step,float& ox,float& oy,std::string& error){
    ox=oy=0;bool valid=false;
    const auto probe=[&](float x,float y)->int{if(!unitLocationValid(id,x,y,valid,error))return -1;if(valid){ox=x;oy=y;return 1;}return 0;};
    for(int k=1;k<10;++k){const float f=float(k)/10.0f;if(int r=probe((lx-tx)*f+tx,(ly-ty)*f+ty))return r>0;}
    for(int k=1;k<9;++k){const float f=float(k)/10.0f;if(int r=probe((sx-tx)*f+tx,(sy-ty)*f+ty))return r>0;}
    if(!(step>0)){error.clear();return true;}
    constexpr float tau=6.2831854820251465f;
    const float dx=lx-sx,dy=ly-sy;float base=std::atan2(dy*-1.0f,dx);base=base-std::floor(base/tau)*tau;
    const float dist=std::sqrt(dx*dx+dy*dy);const float a1=base+step,a2=base-step;
    const float p1x=std::cos(a1)*dist+sx,p1y=-(std::sin(a1)*dist)+sy,p2x=std::cos(a2)*dist+sx,p2y=-(std::sin(a2)*dist)+sy;
    const float z1x=p1x-p1x,z1y=p1y-p1y,z2x=p2x-p2x,z2y=p2y-p2y;
    for(int k=1;k<10;++k){const float f=float(k)/10.0f;if(int r=probe(f*z1x+p1x,f*z1y+p1y))return r>0;if(int r=probe(f*z2x+p2x,f*z2y+p2y))return r>0;}
    for(int k=1;k<9;++k){const float f=float(k)/10.0f;if(int r=probe((sx-p1x)*f+p1x,(sy-p1y)*f+p1y))return r>0;if(int r=probe((sx-p2x)*f+p2x,(sy-p2y)*f+p2y))return r>0;}
    ox=oy=0;error.clear();return true;
}
namespace {
// PolygonHelpers::PolygonCollisionOpt2cefd0 (.local/reports/native-shift-polygon-collision-opt-pseudocode.c),
// ProjectPolygon2ce880 (min/max of dot), Polygon::GetHashCode2ce120, Polygon::Rebuild62cd0 centre
// (vertex average). Only edges facing the other centre are tested; the gap includes the velocity
// projection; MTV = min |gap| axis oriented B->A. DAT_181a095d0[(hA*41+hB)%1999] keeps the last
// separating axis index as the start of the next test (affects tie order only).
struct ShiftPolygonResult {bool willIntersect=false,intersect=false;float x=0,y=0;};
uint64_t shiftPolygonHash(const std::vector<hades::MovementPoint>& p){
    uint64_t h=0;
    for(const auto& v:p){
        const auto toU=[](float f)->uint64_t{if(f>=9.223372e18f){return uint64_t(int64_t(f-9.223372e18f))+0x8000000000000000ull;}return uint64_t(int64_t(f));};
        h=((toU(v.x)^(h*0x18d))*0x18d)^toU(v.y);
    }
    return h;
}
void shiftProject(float nx,float ny,const std::vector<hades::MovementPoint>& p,float& mn,float& mx){
    mn=mx=ny*p.back().y+nx*p.back().x;
    for(size_t i=p.size()-1;i-->0;){const float d=ny*p[i].y+nx*p[i].x;if(d<mn)mn=d;else if(mx<d)mx=d;}
}
void shiftCentre(const std::vector<hades::MovementPoint>& p,float& cx,float& cy){
    cx=0;cy=0;for(const auto& v:p){cx+=v.x;cy+=v.y;}cx/=float(p.size());cy/=float(p.size());
}
ShiftPolygonResult shiftPolygonCollision(std::array<uint64_t,1999>& cache,const std::vector<hades::MovementPoint>& A,const std::vector<hades::MovementPoint>& B,float vx,float vy){
    ShiftPolygonResult r;r.willIntersect=r.intersect=true;
    const size_t na=A.size(),nb=B.size();if(!na||!nb){r.willIntersect=r.intersect=false;return r;} // PolygonCollision2ce1d0 empty guard
    float acx,acy,bcx,bcy;shiftCentre(A,acx,acy);shiftCentre(B,bcx,bcy);
    const float dx=acx-bcx,dy=acy-bcy;
    const uint64_t key=(shiftPolygonHash(A)*0x29+shiftPolygonHash(B))%1999;
    const uint64_t first=cache[key];
    float best=std::numeric_limits<float>::infinity(),ax=0,ay=0;bool tested=false;
    const size_t total=na+nb;
    for(size_t k=0;k<total;++k){
        const size_t e=(first+k)%total;
        const bool own=e<na;const auto& P=own?A:B;const size_t i=own?e:e-na,n=own?na:nb;
        const float ex=P[(i+1)%n].x-P[i].x,ey=P[(i+1)%n].y-P[i].y;
        float nx=-ey,ny=ex;const float side=nx*dx+dy*ny;
        float gap=0;
        if(own){
            if(!(side<0))continue;
            const float l=std::sqrt(nx*nx+ny*ny);nx=nx*(1.f/l);ny=(1.f/l)*ny;
            float mnB,mxB;shiftProject(nx,ny,B,mnB,mxB);
            const float e0=nx*P[i].x+ny*P[i].y,e1=nx*P[(i+1)%n].x+ny*P[(i+1)%n].y;
            gap=mnB-std::max(e0,e1);if(gap>0)r.intersect=false;
            const float vel=ny*vy+nx*vx;if(vel>0)gap-=vel;
        }else{
            if(!(side>0))continue;
            const float l=std::sqrt(nx*nx+ny*ny);nx=nx*(1.f/l);ny=(1.f/l)*ny;
            float mnA,mxA;shiftProject(nx,ny,A,mnA,mxA);
            const float e0=nx*P[i].x+ny*P[i].y,e1=nx*P[(i+1)%n].x+ny*P[(i+1)%n].y;
            gap=mnA-std::max(e0,e1);if(gap>0)r.intersect=false;
            const float vel=ny*vy+nx*vx;if(vel<0)gap+=vel;
        }
        if(gap>0)r.willIntersect=false;
        if(!r.intersect&&!r.willIntersect){cache[key]=e;r.x=r.y=0;return r;}
        gap=std::abs(gap);tested=true;
        if(gap<best){best=gap;ax=nx;ay=ny;if(dx*nx+dy*ny<0){ax=-nx;ay=-ny;}}
    }
    if(!r.willIntersect){r.x=r.y=0;return r;}
    // No facing axis (coincident centres) leaves the native axis uninitialised.
    if(!tested)throw std::runtime_error("PolygonCollisionOpt2cefd0 coincident-centre uninitialised axis branch");
    r.x=ax*best;r.y=ay*best;return r;
}
bool shiftHas(const std::vector<std::pair<int32_t,uint64_t>>& set,uint64_t lifetime){
    for(const auto& e:set)if(e.second==lifetime)return true;return false;
}
}
// Unit::ApplyFullShift2f76d0 (.local/reports/native-combat5-unit-apply-full-shift-pseudocode.c):
// |shift|^2<64 -> one ApplyShift(loc); else n=1-int(-|shift|*0.125) steps of shift/n from the live
// location, stopping early when stopOnCollision and ApplyShift reports a stopping collision. Then
// the collision-set swap: obstacles in mPrevColliding(+0x4f8) not in mCurrentlyColliding(+0x528)
// get CollisionEndedAlphaInDuration / CollisionEndedAnimation / ScriptManager::OnCollisionEnd28b290;
// previous=current, current cleared. Callers: BlinkWeapon::Fire18007dd74 (false,false) and
// Unit::UpdateMovement2f69be (UnitData+0x15e StopShiftOnCollision, true).
bool ScriptRuntime::unitApplyFullShift(int32_t id,float locX,float locY,float dx,float dy,bool stopOnCollision,bool ignoreNoCollisionAnim,std::string& error){
    try{
    if(dx==0&&dy==0){error.clear();return true;}
    auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object){error="ApplyFullShift requires actual Unit";return false;}
    const uint64_t lifetime=a->entityHandle;
    const float len2=dx*dx+dy*dy;bool collided=false;
    if(64.f<=len2){
        const float len=std::sqrt(len2);const int n=1-int(len*-0.125f);const float f=(len/float(n))/len;
        const float stx=dx*f,sty=dy*f;
        for(int i=0;i<n;++i){
            float x=0,y=0;if(!actorWorld_.location(id,x,y,error))return false;
            if(!unitApplyShift(id,stx+x,sty+y,stx,sty,ignoreNoCollisionAnim,collided,error))return false;
            if(stopOnCollision&&collided)break;
        }
    }else if(!unitApplyShift(id,locX,locY,dx,dy,ignoreNoCollisionAnim,collided,error))return false;
    auto& sets=unitCollisionSets_[lifetime];
    const auto ended=sets.previous;
    for(const auto& [o,olife]:ended){
        if(shiftHas(sets.current,olife))continue;
        const auto* obstacle=actorWorld_.actor(o);if(!obstacle||!obstacle->registered||obstacle->entityHandle!=olife)continue; // ObstacleManager lookup null
        const auto* alphaIn=actorWorld_.currentProperty(o,"CollisionEndedAlphaInDuration");const auto* endAnim=actorWorld_.currentProperty(o,"CollisionEndedAnimation");
        if(!alphaIn||!endAnim)throw std::runtime_error("Original ObstacleData CollisionEnded fields unavailable");
        if(alphaIn->number()>0)throw std::runtime_error("ApplyFullShift CollisionEndedAlphaInDuration BodyComponent::Adjust2b7440 not reconstructed");
        if(!endAnim->text().empty())throw std::runtime_error("ApplyFullShift CollisionEndedAnimation Thing::SetAnimation2decb0 not reconstructed");
        // ScriptManager::OnCollisionEnd28b290 (GhostScripts.lua OnCollisionEnd reads CollideeTable).
        // APPROXIMATION (labelled): argument shape assumed from OnObstacleCollision28ab40; 28b290 not decompiled.
        lua_State* L=state_;pushObjectLuaTable(L,o);const bool hasTable=!lua_isnil(L,-1);lua_pop(L,1);
        if(hasTable&&!dispatchCallbacks("OnCollisionEnd",a->object->name.c_str(),id,[o](lua_State* S,std::string&){pushObjectLuaTable(S,o);lua_setfield(S,-2,"CollideeTable");return true;})){error=error_;return false;}
        a=actorWorld_.actor(id);if(!a){error="OnCollisionEnd lost actual Unit";return false;}
    }
    auto& after=unitCollisionSets_[lifetime];after.previous=after.current;after.current.clear();
    error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
// Unit::ApplyShift2f7c00 (.local/reports/native-combat5-unit-apply-shift-pseudocode.c) with
// CollisionParameters306ab0 defaults + ApplyShift overrides (+0e=+0f=1 PolygonCollisionOpt, +03 StopsUnits
// list, +0b willIntersect) and GridSquare::detectCollisions30ae10 obstacle branch: z overlap,
// MaxUpwardVelocityToCollide, rectangle overlap, PolygonCollision2ce1d0, per-axis accumulation
// (same sign keeps the larger magnitude, mixed signs add) when UseCollisionShift && StopsUnits.
// APPROXIMATIONS (labelled): Thing::mRectangle is taken as the integer AABB of the transformed
// polygon; obstacle order is the Android broadphase candidate order, not GridSquare list order
// (affects the order-dependent per-axis accumulation only); live Thing.Tallness read from data.
bool ScriptRuntime::unitApplyShift(int32_t id,float nx,float ny,float sx,float sy,bool ignoreNoCollisionAnim,bool& collided,std::string& error){
    try{
    collided=false;
    auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object){error="ApplyShift requires actual Unit";return false;}
    if(a->attachedParent)throw std::runtime_error("ApplyShift attached Unit parent-relative location not reconstructed");
    if(!actorWorld_.refreshGeometry(id,error))return false;
    if(a->geometry.size()<3)throw std::runtime_error("ApplyShift actual Unit polygon unavailable");
    const auto prop=[&](int32_t o,const char* k)->const hades::SourceValue&{const auto* v=actorWorld_.currentProperty(o,k);if(!v)throw std::runtime_error(std::string("ApplyShift original field unavailable: ")+k);return *v;};
    float cx=0,cy=0;if(!actorWorld_.location(id,cx,cy,error))return false;
    std::vector<hades::MovementPoint> A;for(const auto& q:a->geometry)A.push_back({q.x,q.y});
    float minX=A[0].x,maxX=A[0].x,minY=A[0].y,maxY=A[0].y;for(const auto& v:A){minX=std::min(minX,v.x);maxX=std::max(maxX,v.x);minY=std::min(minY,v.y);maxY=std::max(maxY,v.y);}
    const int rw=int(maxX-minX),rh=int(maxY-minY);
    const int rx=int(nx-float(rw)*0.5f),ry=int(ny-float(rh)*0.5f);
    float z=0;if(!actorWorld_.zLocation(id,z,error))return false;
    const bool noCollisionAnim=hades::actorOwnerHasNoCollision(*a);
    const bool obstaclesFlag=prop(id,"CollideWithObstacles").boolean()&&!(ignoreNoCollisionAnim&&noCollisionAnim);
    bool unitsFlag=prop(id,"CollideWithUnits").boolean()&&!noCollisionAnim;
    if(unitsFlag&&a->status){const auto st=a->status->snapshot();for(auto h:st.active){const auto* d=selfEffects_.effectData(h,error);if(!d)return false;const auto f=d->fields.find("NoUnitCollision");if(f==d->fields.end()||!std::holds_alternative<bool>(f->second))throw std::runtime_error("Original EffectData NoUnitCollision unavailable");if(std::get<bool>(f->second)){unitsFlag=false;break;}}}
    if(unitsFlag)unitsFlag=z<=float(prop(id,"MaxCollisionZ").number());
    const float moveShift=a->physics?std::sqrt(a->physics->moveShift.x*a->physics->moveShift.x+a->physics->moveShift.y*a->physics->moveShift.y):0.f;
    if(unitsFlag){
        // GridSquare::detectCollisions30ae10 unit branch -> Unit::OnUnitCollision2f7580: fail loudly on any candidate.
        for(const auto o:actorWorld_.registrationOrder()){if(o==id)continue;const auto* u=actorWorld_.actor(o);
            if(!u||!u->registered||u->isDisposed||!u->object||!u->definition||u->definition->kind!="Unit"||!u->object->alive||u->geometry.size()<3)continue;
            std::vector<hades::MovementPoint> U;for(const auto& q:u->geometry)U.push_back({q.x,q.y});
            const auto r=shiftPolygonCollision(polygonAxisCache_,A,U,sx,sy);
            if(r.intersect||r.willIntersect)throw std::runtime_error("ApplyShift unit-unit push (GridSquare::detectCollisions30ae10 unit branch / Unit::OnUnitCollision2f7580) not reconstructed");}
    }
    float px=0,py=0;std::vector<int32_t> hits;
    if(obstaclesFlag){
        hades::SpatialBounds bounds{float(rx),float(ry),float(rx+rw),float(ry+rh)};
        std::vector<int32_t> candidates;if(!actorWorld_.spatialObstacleCandidates(bounds,hades::ObstacleGridCategory::StopsUnits,candidates,error))return false;
        for(const auto o:candidates){auto* ob=actorWorld_.actor(o);if(!ob||!ob->registered||!ob->object||o==id)continue;
            if(std::find(hits.begin(),hits.end(),o)!=hits.end())continue;
            float oz=0;if(!actorWorld_.zLocation(o,oz,error))return false;
            if(!(z<=oz+float(prop(o,"Thing.Tallness").number())))continue;
            const float up=float(prop(o,"MaxUpwardVelocityToCollide").number());
            if(!(up<=0||!ob->physics||ob->physics->upVelocity<=up))continue;
            if(ob->geometry.empty())continue; // PolygonCollision2ce1d0 empty geometry -> no MTV
            float ominX=ob->geometry[0].x,omaxX=ominX,ominY=ob->geometry[0].y,omaxY=ominY;
            for(const auto& v:ob->geometry){ominX=std::min(ominX,v.x);omaxX=std::max(omaxX,v.x);ominY=std::min(ominY,v.y);omaxY=std::max(omaxY,v.y);}
            const int ox=int(ominX),oy=int(ominY),ow=int(omaxX-ominX),oh=int(omaxY-ominY);
            if(!(rx<=ow+ox&&ox<=rw+rx&&ry<=oh+oy&&oy<=rh+ry))continue;
            std::vector<hades::MovementPoint> B;for(const auto& v:ob->geometry)B.push_back({v.x,v.y});
            const auto r=shiftPolygonCollision(polygonAxisCache_,A,B,sx,sy);
            if(r.x==0&&r.y==0)continue;
            bool stops=false;if(!actorWorld_.obstacleStops(o,false,stops,error))return false;
            if(prop(o,"UseCollisionShift").boolean()&&stops){
                const auto acc=[](float& c,float v){if(c<0&&v<0){if(v<=c)c=v;}else if(c>0&&v>0){if(c<=v)c=v;}else c=v+c;};
                acc(px,r.x);acc(py,r.y);
            }
            hits.push_back(o);
        }
    }
    float qx=px,qy=py;
    if(px!=0||py!=0){const float l=std::sqrt(py*py+px*px),s=py*0.25f,l2=std::sqrt(s*s+px*px);qx=(1.f/l2)*px*l;qy=s*(1.f/l2)*l;}
    auto& sets=unitCollisionSets_[a->entityHandle];sets.lastTranslationX+=qx;sets.lastTranslationY+=qy;
    if(!std::isfinite(qx)||!std::isfinite(qy))throw std::runtime_error("ApplyShift nonfinite push -> Random::RandomDouble54f80 jitter not reconstructed");
    if((qx!=0||qy!=0)&&!a->object->playerControlled)throw std::runtime_error("ApplyShift Unit+0x418 mAI collision notify (vtable+0x28) not reconstructed");
    float damage=0;
    for(const auto o:hits){
        if(!unitOnObstacleCollision(id,o,sx,sy,qx,qy,damage,error))return false;
        a=actorWorld_.actor(id);if(!a||!a->object){error="OnObstacleCollision lost actual Unit";return false;}
        bool stops=false;if(!actorWorld_.obstacleStops(o,false,stops,error))return false;
        if(stops&&prop(o,"CollisionBounce").number()>0)collided=true;
        const auto weapon=prop(id,"InitiatedCollisionWeapon").text();
        if(!weapon.empty()&&a->physics){const float req=float(prop(id,"CollisionWeaponRequiredVelocity").number());
            if(req*req<=moveShift*moveShift&&a->physics->lastForceSelfApplied)throw std::runtime_error("ApplyShift InitiatedCollisionWeapon Weapon::RequestFire83160 not reconstructed");}
    }
    float tx=nx,ty=ny;
    if(qx!=0||qy!=0){
        const float c2=qx*qx+qy*qy,s2=sx*sx+sy*sy;
        if(s2<c2){const float cl=std::sqrt(c2),sl=std::sqrt(s2);if((1.f/cl)*qy*sy*(1.f/sl)+(1.f/sl)*sx*(1.f/cl)*qx<-0.8f){error.clear();return true;}}
        const float ux=qx+sx,uy=sy+qy,u2=ux*ux+uy*uy;
        if(u2<=s2){tx=qx+nx;ty=qy+ny;}
        else{const float sl=std::sqrt(s2),ul=std::sqrt(u2);ty=uy*(sl/ul)+cy;tx=(sl/ul)*ux+cx;}
        const float mx=tx-cx,my=ty-cy,m2=mx*mx+my*my;
        if(m2<s2){const auto found=localMovement_.find(a->entityHandle);if(found==localMovement_.end())throw std::runtime_error("ApplyShift Unit+0x3cc mUnappliedSpeed requires actual Unit movement state");
            auto& unapplied=found->second.unappliedSpeed;const float blocked=std::sqrt(s2)-std::sqrt(m2);if(unapplied<=blocked)unapplied=blocked;}
    }
    // PhysicsComponent::CheckTether2bd890 (TetherElasticity<=0): with no tethers it returns the proposal.
    if(a->physics&&a->physics->tetherCount)throw std::runtime_error("ApplyShift PhysicsComponent::CheckTether2bd890 tether clamp not reconstructed");
    return unitSetLocation(id,tx,ty,error);
    }catch(const std::exception& e){error=e.what();return false;}
}
// Unit::OnObstacleCollision2f6ce0 (.local/reports/native-shift-on-obstacle-collision-pseudocode.c);
// ObstacleDataDef offsets from the PDB (native-shift-obstacle-data-def-type.txt). Reconstructed
// branches: collision-set insert, first-contact cues (fail loudly when authored), StopsUnits gate,
// reaction gate, ScriptManager::OnCollisionReaction28a8b0 / OnObstacleCollision28ab40 Lua dispatch
// (only when the obstacle has an attached Lua table), UnitStatus::ClearOnCollisionEffects2fd730.
// APPROXIMATION (labelled): OnCollisionReaction28a8b0 arguments assumed to match OnObstacleCollision28ab40
// (CollideeTable, Velocity); ClearOnCollisionEffects fails loudly on any ClearOnCollision effect (not decompiled).
bool ScriptRuntime::unitOnObstacleCollision(int32_t id,int32_t o,float sx,float sy,float px,float py,float& damage,std::string& error){
    try{
    auto* a=actorWorld_.actor(id);auto* ob=actorWorld_.actor(o);if(!a||!a->object||!ob||!ob->registered){error="OnObstacleCollision requires actual Unit and Obstacle";return false;}
    const auto prop=[&](int32_t n,const char* k)->const hades::SourceValue&{const auto* v=actorWorld_.currentProperty(n,k);if(!v)throw std::runtime_error(std::string("OnObstacleCollision original field unavailable: ")+k);return *v;};
    const uint64_t olife=ob->entityHandle;auto& sets=unitCollisionSets_[a->entityHandle];
    if(!shiftHas(sets.current,olife))sets.current.push_back({o,olife});
    const bool wasColliding=shiftHas(sets.previous,olife);
    if(!wasColliding){
        // ObstacleDataDef ctor 0x1800d43f0 zeroes CollisionSound (def+0xb0: null cue + empty name);
        // the actor catalog lists it as an unresolved compiled field, so absent = that empty default.
        const auto* sound=actorWorld_.currentProperty(o,"CollisionSound");
        if(sound&&!sound->text().empty())throw std::runtime_error("OnObstacleCollision CollisionSound AudioManager::PlayCue3D4a690 not reconstructed");
        if(!prop(o,"CollisionAnimation").text().empty())throw std::runtime_error("OnObstacleCollision CollisionAnimation Thing::SetAnimation2decb0 not reconstructed");
        // Unit::OnObstacleCollision 0x2f6ce0: CollisionAlphaOutDuration>0 ->
        // FUN_1802b7440(body, body+0x320 alpha, {target 0, initial 1, duration, no ease}).
        if(const float fade=float(prop(o,"CollisionAlphaOutDuration").number());fade>0){
            hades::BodyTransitionOptions alphaOut;alphaOut.duration=fade;
            if(!actorWorld_.adjustColorChannel(o,3,0.f,1.f,alphaOut,error))return false;}
        if(!prop(o,"CollisionFx").text().empty())throw std::runtime_error("OnObstacleCollision CollisionFx AnimationManager::Create29e410 not reconstructed");
    }
    const float speed=a->physics?std::sqrt(a->physics->velocity.x*a->physics->velocity.x+a->physics->velocity.y*a->physics->velocity.y):0.f;
    bool stops=false;if(!actorWorld_.obstacleStops(o,false,stops,error))return false;
    if(!stops){error.clear();return true;}
    const auto clearOnCollision=[&]()->bool{
        if(a->status){const auto st=a->status->snapshot();for(const auto* list:{&st.active,&st.pending})for(auto h:*list){const auto* d=selfEffects_.effectData(h,error);if(!d)return false;const auto f=d->fields.find("ClearOnCollision");if(f==d->fields.end()||!std::holds_alternative<bool>(f->second))throw std::runtime_error("Original EffectData ClearOnCollision unavailable");if(std::get<bool>(f->second))throw std::runtime_error("UnitStatus::ClearOnCollisionEffects2fd730 effect clear not reconstructed");}}
        return true;};
    if(px==0&&py==0){if(!clearOnCollision())return false;error.clear();return true;}
    const float pl=1.f/std::sqrt(px*px+py*py),sl=1.f/std::sqrt(sx*sx+sy*sy);
    const float facing=sx*sl*px*pl+sy*sl*py*pl;
    if(prop(o,"CollisionBounce").number()>0&&float(prop(o,"CollisionBounceRequiredVelocity").number())<speed&&float(prop(id,"Speed").number())+1.f<speed&&facing<-0.3f)
        throw std::runtime_error("OnObstacleCollision CollisionBounce PhysicsComponent::ApplyCollision2bd3d0 not reconstructed");
    if(prop(o,"CollisionVelocityRetention").number()!=1)throw std::runtime_error("OnObstacleCollision CollisionVelocityRetention PhysicsComponent::MultiplyVelocity2bd450 not reconstructed");
    if(prop(o,"VelocityAbsorbtionFraction").number()>0&&ob->physics&&prop(id,"CanVelocityBeAbsorbed").boolean())throw std::runtime_error("OnObstacleCollision VelocityAbsorbtionFraction ApplyDirectForce2bcea0 not reconstructed");
    if(!ob->hasLife)throw std::runtime_error("OnObstacleCollision reads obstacle pLife(+0xd0) LifeDataDef.DieOnCollision but obstacle has no LifeComponent");
    const bool die=prop(o,"Life.DieOnCollision").boolean();
    bool reaction=die;
    if(!die){const bool selfOk=!a->physics||!a->physics->lastForceSelfApplied||prop(id,"AllowSelfCollisions").boolean();
        const float req=float(prop(o,"CollisionReactionRequiredVelocity").number());
        reaction=selfOk&&!wasColliding&&(req<=0||req<=speed);}
    lua_State* L=state_;pushObjectLuaTable(L,o);const bool hasTable=!lua_isnil(L,-1);lua_pop(L,1);
    if(reaction){
        if(facing<-0.3f){
            if(prop(o,"CollisionDamage").number()>0)throw std::runtime_error("OnObstacleCollision CollisionDamage LifeComponent::TakeHit2b7cd0 not reconstructed");
            const float perVelocity=float(prop(o,"CollisionDamagePerVelocity").number());
            if(perVelocity>0&&damage<=0)throw std::runtime_error("OnObstacleCollision CollisionDamagePerVelocity LifeComponent::TakeHit2b7cd0 not reconstructed");
            if(!prop(o,"CollisionReactionFx").text().empty())throw std::runtime_error("OnObstacleCollision CollisionReactionFx CalcIntersectionPoint2ced20/AnimationManager::Create not reconstructed");
        }
        if(a->physics&&prop(o,"CollisionKnockback").number()>0)throw std::runtime_error("OnObstacleCollision CollisionKnockback PhysicsComponent::ApplyForce2bd160 not reconstructed");
        if(die&&float(prop(id,"Life.DieOnCollisionRequiredVelocityMultiplier").number())*float(prop(o,"Life.DieOnCollisionRequiredVelocity").number())<=speed)throw std::runtime_error("OnObstacleCollision DieOnCollision obstacle vtable+0xc0 not reconstructed");
        if(hasTable&&!dispatchCallbacks("OnCollisionReaction",a->object->name.c_str(),id,[o,speed](lua_State* S,std::string&){pushObjectLuaTable(S,o);lua_setfield(S,-2,"CollideeTable");lua_pushnumber(S,speed);lua_setfield(S,-2,"Velocity");return true;})){error=error_;return false;}
    }
    if(hasTable&&!dispatchCallbacks("OnObstacleCollision",a->object->name.c_str(),id,[o,speed](lua_State* S,std::string&){pushObjectLuaTable(S,o);lua_setfield(S,-2,"CollideeTable");lua_pushnumber(S,speed);lua_setfield(S,-2,"Velocity");return true;})){error=error_;return false;}
    a=actorWorld_.actor(id);if(!a){error="OnObstacleCollision Lua lost actual Unit";return false;}
    if(!clearOnCollision())return false;
    error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
