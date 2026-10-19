#include "lua_runtime.hpp"
#include "physics_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
extern "C" {
#include <lua.h>
}

namespace {
const hades::SourceValue& physicsProperty(const hades::ActorWorld& world,int32_t id,const char* key){
    const auto* value=world.currentProperty(id,key);
    if(!value)throw std::runtime_error(std::string("Original Physics property unavailable: ")+key);
    return *value;
}
float physicsNumber(const hades::ActorWorld& world,int32_t id,const char* key){
    const auto value=float(physicsProperty(world,id,key).number());
    if(!std::isfinite(value))throw std::runtime_error(std::string("Nonfinite original Physics property: ")+key);
    return value;
}
}

bool ScriptRuntime::advancePhysics(float dt,std::string& error){
    if(physicsFaulted_){error=physicsFault_;return false;}
    try{
        if(!state_||!std::isfinite(dt)||dt<0||dt>1)throw std::runtime_error("Actual native Physics clock unavailable/outside bounded phase");
        // Portable sequential component schedule. The original Windows worker
        // completion order is not claimed as observable deterministic evidence.
        const std::vector<int32_t> order(actorWorld_.registrationOrder().begin(),actorWorld_.registrationOrder().end());
        for(const auto id:order){
            auto* actor=actorWorld_.actor(id);
            if(!actor||!actor->registered||!actor->object||!actor->physics)continue;
            if(actor->physicsPending){
                const auto& p=*actor->physics;
                // PhysicsSystem.OfInterest2d5030 filters component admission.
                // A freshly allocated stationary component never enters Update
                // and must not fabricate an initial OnComeToRest notification.
                const bool interested=hades::physicsOfInterest(p,actor->object->z,physicsNumber(actorWorld_,id,"Thing.OffsetZ"));
                if(interested)actor->physicsActive=true;
                actor->physicsPending=false;
            }
            if(!actor->physicsActive)continue;
            auto& body=*actor->object;auto& physics=*actor->physics;
            if(physics.spin!=0)throw std::runtime_error("Nonzero Physics spin requires its native angle/rotation provider");
            if(physics.tetherCount)throw std::runtime_error("Native Physics tethers require their real constraints/provider");
            float elapsed=0;
            if(!hades::physicsComponentElapsed(dt,worldSpeedModifier_,actor->timeModifierFraction,actor->timeMultiplier,elapsed,error))throw std::runtime_error(error);
            const auto offsetZ=physicsNumber(actorWorld_,id,"Thing.OffsetZ");
            hades::PhysicsFrictionOwner friction;
            friction.rawZ=body.z;friction.offsetZ=offsetZ;
            friction.grip=physicsNumber(actorWorld_,id,"Thing.Grip");
            friction.soundFadeDuration=physicsNumber(actorWorld_,id,"Thing.ImpulsingSoundFadeOutDuration");
            if(actor->status){
                const auto status=actor->status->snapshot();
                if(!status.active.empty()){
                    if(!(physicsGrip_?physicsGrip_(id,friction.grip,error):physicsGripValue(id,friction.grip,error)))throw std::runtime_error(error);
                }
            }
            if(actor->definition->kind=="Unit"&&!actorWorld_.unitAirborne(id,friction.unitAirborne,error))throw std::runtime_error(error);
            const bool moving=physics.velocity.x!=0||physics.velocity.y!=0;
            if(moving&&!(body.z<=offsetZ&&!friction.unitAirborne)&&!physics.ignoreGravity){
                // This branch needs the actual current primary data; do not
                // coerce an unavailable source POD default into false.
                if(actor->animation.active()){
                    const auto i=actorAnimationDefinitions_.find(actor->animation.clip()->name);
                    const auto* definition=i==actorAnimationDefinitions_.end()?nullptr:i->second.get("definition");
                    const auto* value=definition?definition->get("OwnerApplyGroundFriction"):nullptr;
                    if(!value)value=nativeAnimationDefaults_.get("OwnerApplyGroundFriction");
                    // Matching AnimationData ctor zero template at1800940b5
                    // covers full+0x9a (Def+0x42); later writes do not alter it.
                    friction.animationAppliesFriction=value?value->boolean():false;
                }else if(actor->primaryAnimationLifetime||actor->primaryEffectAnimation)throw std::runtime_error("Physics friction needs actual non-HAA primary data");
            }
            const auto stopCue=[this](int32_t cue,float duration,std::string& detail){return duration>0?audio_.setVolume(uint32_t(cue),0,duration,true,false,detail):audio_.stop(uint32_t(cue),false,detail);};
            if(!hades::applyPhysicsFriction(physics,elapsed,friction,stopCue,error))throw std::runtime_error(error);
            hades::PhysicsVerticalOwner owner;
            owner.downwardTerminalVelocity=physicsNumber(actorWorld_,id,"Thing.TerminalVelocity");
            hades::PhysicsVerticalCallbacks callbacks;
            callbacks.getZ=[this,id](float& z,std::string& detail){return actorWorld_.zLocation(id,z,detail);};
            callbacks.restingZ=[this,id,offsetZ](float& z,std::string& detail){
                const auto* a=actorWorld_.actor(id);z=offsetZ;
                const auto* parent=a?actorWorld_.actor(a->attachedParent):nullptr;
                if(parent&&parent->registered&&parent->entityHandle==a->attachedParentLifetime){float parentZ=0;if(!actorWorld_.zLocation(parent->id,parentZ,detail))return false;z+=(parentZ+a->attachOffsetZ);}
                detail.clear();return true;
            };
            callbacks.shiftZ=[this,id](float delta,bool propagate,std::string& detail){
                // Physics::UpdateSelf passes true for rising and falling
                // shifts; preserve the shared native virtual setter boundary.
                if(!propagate){detail="Physics nonpropagating ShiftZ branch unavailable";return false;}
                return shiftActorZ(id,delta,detail);
            };
            auto script=[this,id](const char* event,std::string& detail){const auto* a=actorWorld_.actor(id);if(!a||!a->object){detail="Physics script owner unavailable";return false;}if(dispatchCallbacks(event,a->object->name.c_str(),id)){detail.clear();return true;}detail=error_;return false;};
            callbacks.onTakeoff=[&](std::string& detail){return script("OnTakeoff",detail);};
            callbacks.onRising=[&](std::string& detail){return script("OnRising",detail);};
            callbacks.onFalling=[&](std::string& detail){return script("OnFalling",detail);};
            callbacks.bounceForce.getZLocation=callbacks.getZ;
            callbacks.bounceForce.onUpwardForce=[this,id](float,std::string& detail){auto* a=actorWorld_.actor(id);if(!a||!a->physics){detail="Bounce Physics owner expired";return false;}a->physicsPending=true;detail.clear();return true;};
            callbacks.bounceInputs=[this,id](hades::PhysicsVerticalOwner& live,std::string& detail){
                const auto* a=actorWorld_.actor(id);if(!a||!a->registered){detail="Bounce actual owner expired";return false;}
                live.bounceUpwardForce=physicsNumber(actorWorld_,id,"Thing.BounceUpwardForce");
                live.bounceForce.grip=physicsNumber(actorWorld_,id,"Thing.Grip");
                live.bounceForce.maxUpwardVelocity=physicsNumber(actorWorld_,id,"Thing.UpwardTerminalVelocity");
                live.bounceForce.thingImmuneToForce=physicsProperty(actorWorld_,id,"Thing.ImmuneToForce").boolean();
                if(a->status){const auto status=a->status->snapshot();if(!status.active.empty()||!status.pending.empty()){detail="Physics bounce needs actual effect force immunity";return false;}}
                if(a->animation.active()){
                    const auto immobile=animationBank_->exactOwnerImmobile(a->animation.clipIndex());
                    if(!immobile){detail="Physics bounce current animation OwnerImmobile unavailable";return false;}
                    live.bounceForce.animationOwnerImmobile=*immobile;
                }
                detail.clear();return true;
            };
            callbacks.onTouchdown=[this,id,&script](float,float rebound,std::string& detail){
                auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object){detail="Touchdown actual actor expired";return false;}
                a->object->z=physicsNumber(actorWorld_,id,"Thing.OffsetZ");
                const auto* parent=actorWorld_.actor(a->attachedParent);
                const bool attached=parent&&parent->registered&&parent->entityHandle==a->attachedParentLifetime;
                if(!attached&&rebound<=0){
                    const auto graphic=physicsProperty(actorWorld_,id,"Thing.TouchdownGraphic").text();
                    if(!setActorAnimation(id,graphic,detail))return false;
                    const auto fx=physicsProperty(actorWorld_,id,"Thing.OnTouchdownFxAnim").text();
                    if(!fx.empty()&&fx!="null"){
                        // Thing.OnTouchdown2dfbaa..2dfc11 calls manager Create
                        // with actual Thing XY and no owner; source animation
                        // initializes its own default Z (the ground puff).
                        std::array<float,3> location{};if(!actorWorld_.location(id,location[0],location[1],detail))return false;
                        uint64_t handle=0;if(!actionEffects_.createStandalone(fx,location,actionEffectProviders(),handle,detail))return false;
                    }
                    const auto cue=physicsProperty(actorWorld_,id,"Thing.TouchdownSound").text();
                    bool valid=false;if(!audio_.resolveCue(cue,valid,detail))return false;
                    if(valid){std::array<float,3> location{};if(!actorWorld_.location(id,location[0],location[1],detail)||!actorWorld_.zLocation(id,location[2],detail))return false;uint32_t sound=0;if(!playAudioCue(cue,id,location,sound,detail))return false;}
                    const auto intensity=physicsNumber(actorWorld_,id,"Thing.TouchdownRumbleIntensity"),duration=physicsNumber(actorWorld_,id,"Thing.TouchdownRumbleDuration");
                    if(intensity!=0||duration!=0){
                        if(physicsRumble_){if(!physicsRumble_(id,intensity,duration,detail))return false;}
#if defined(__ANDROID__)
                        else{detail="Android Touchdown haptic output provider unavailable";return false;}
#else
                        // The Mac host harness has no game controller/haptic
                        // output device. This records absence, not played rumble.
#endif
                    }
                }
                if(physicsProperty(actorWorld_,id,"Thing.DieOnTouchdown").boolean()){detail="Native DieOnTouchdown Kill provider unavailable";return false;}
                if(!script("OnTouchdown",detail))return false;
                // Obstacle.OnTouchdown2c30e0 calls the base callback, then its
                // PropertiesDirty override (2c314c..3171): sort/cache refresh
                // and LocationChanged(true), including the final resting Z.
                if(a->definition->kind=="Obstacle"&&!actorWorld_.propertiesDirty(id,detail))return false;
                if(a->definition->kind=="Unit"){
                    if(!(physicsTouchdownWeapon_?physicsTouchdownWeapon_(id,detail):physicsUnitTouchdown(id,detail)))return false;
                }
                return true;
            };
            if(!hades::updatePhysicsVertical(physics,elapsed,owner,callbacks,error))throw std::runtime_error(error);
            // UpdateSpin and UpdateTethers are real source phases; the proven
            // empty/zero states above establish their selected early returns.
            if(physics.spin!=0||physics.tetherCount)throw std::runtime_error("Physics callback introduced spin/tether before unsupported native phase");
            if(!hades::physicsOfInterest(physics,body.z,physicsNumber(actorWorld_,id,"Thing.OffsetZ"))){
                if(!script("OnComeToRest",error))throw std::runtime_error(error);
                actor->physicsActive=false; // Remove system membership, retain component allocation.
            }
        }
        error.clear();return true;
    }catch(const std::exception& e){physicsFaulted_=true;physicsFault_=std::string("Native Physics phase: ")+e.what();error=physicsFault_;return false;}
}

// PhysicsComponent.GetGrip2bd530 scans active effects only. Read current data
// after reflection mutations; factory subclass identity does not replace Type.
bool ScriptRuntime::physicsGripValue(int32_t id,float& grip,std::string& error){
    try{
        const auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered)throw std::runtime_error("Physics grip owner unavailable");
        grip=physicsNumber(actorWorld_,id,"Thing.Grip");
        if(actor->status)for(const auto handle:actor->status->snapshot().active){
            const auto* data=selfEffects_.effectData(handle,error);if(!data)return false;
            const auto number=[&](const char* key){const auto i=data->fields.find(key);if(i==data->fields.end()||!std::holds_alternative<double>(i->second))throw std::runtime_error(std::string("Physics current EffectData unavailable: ")+key);return float(std::get<double>(i->second));};
            if(number("Type")!=1)continue;
            // GetGrip2bd530: EffectData+0x18c = EffectDataDef+0x124 mAmount (additive when >0),
            // else +0x180 = +0x118 mModifier (multiplier unless exactly 1).
            const auto addition=number("Amount");
            if(addition>0)grip+=addition;
            else{const auto multiplier=number("Modifier");if(multiplier!=1)grip*=multiplier;}
        }
        if(!std::isfinite(grip))throw std::runtime_error("Nonfinite current Physics grip");
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

bool ScriptRuntime::physicsUnitTouchdown(int32_t id,std::string& error){
    try{
        auto* actor=actorWorld_.actor(id);
        if(!actor||!actor->registered||!actor->object||!actor->arsenal||!actor->status)throw std::runtime_error("Actual Unit touchdown components unavailable");
        const int top=lua_gettop(state_);lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushliteral(state_,"EditingMode");lua_setfield(state_,-2,"Name");
        if(lua_pcall(state_,1,1,0)!=LUA_OK){const char* detail=lua_tostring(state_,-1);error=detail?detail:"EditingMode lookup failed";lua_settop(state_,top);return false;}
        const bool typed=lua_isboolean(state_,-1),editing=lua_toboolean(state_,-1);lua_settop(state_,top);
        if(!typed)throw std::runtime_error("Actual EditingMode boolean unavailable");
        if(!selfEffects_.refreshUnit(id,error))return false;
        const bool immune=physicsProperty(actorWorld_,id,"ImmuneToStun").boolean();
        if(actor->lifeStatus==0&&!editing&&actor->status->canAttack(false,immune,false)){
            const auto weapon=physicsProperty(actorWorld_,id,"OnTouchdownWeapon").text();
            if(actor->arsenal->hasWeapon(weapon)){
                float speed=0;if(!configurationFloat("SpeedMultiplier",speed,error))return false;
                hades::WeaponOwnerFrame frame;if(!actorWorld_.weaponOwnerFrame(id,worldTime_,speed,editing,inputAllowed(),frame,error))return false;
                if(!actor->arsenal->requestTouchdownFire(weapon,frame,error)&&!error.empty())return false;
            }
        }
        // Unit::OnTouchdown RVA 0x2f6a90: creation-order arsenal walk tests
        // WeaponData+0x3b6 = WeaponDataDef.mReloadOnTouchdown (no CancelOnTouchdown
        // field exists) and calls sgg::Weapon::reload (0x180085830) when set.
        // Evidence: .local/reports/native-cancel-on-touchdown-default-recovery.json
        for(const auto& weapon:actor->arsenal->snapshot()){
            const auto i=weapon.definition.fields.find("ReloadOnTouchdown");
            if(i==weapon.definition.fields.end()||!std::holds_alternative<bool>(i->second))throw std::runtime_error("Actual WeaponData.ReloadOnTouchdown unavailable");
            if(std::get<bool>(i->second))throw std::runtime_error("Native Weapon::reload on touchdown not reconstructed: "+weapon.definition.name);
        }
        if(actor->physics){
            const auto retention=physicsNumber(actorWorld_,id,"TouchdownVelocityRetention");
            auto& physics=*actor->physics;physics.velocity.x*=retention;physics.velocity.y*=retention;
            if(physics.velocity.x*physics.velocity.x+physics.velocity.y*physics.velocity.y<.0001f)physics.velocity={};
            physics.moveShift={}; // Actual PDB mMoveShift+0x68; acceleration is+0x78.
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
