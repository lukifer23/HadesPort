#pragma once
#include <functional>
#include <cstdint>
#include <string>

namespace hades {
struct PhysicsVector {float x=0,y=0;};
// Native PhysicsComponent fields, initialized by Thing::EnsurePhysics. The
// caller owns ECS membership, actual Thing location, callbacks and tethers.
struct PhysicsState {
    bool beingVacuumed=false,ignoreGravity=false,lastForceSelfApplied=false;
    float gravity=1024,footingLost=0,upVelocity=0,spin=0,bounce=0;
    int32_t impulsingSoundId=0;
    uint32_t tetherCount=0; // Actual native constructor has an empty tether vector.
    PhysicsVector moveShift,velocity,acceleration;
};
struct PhysicsForceOwner {
    bool animationOwnerImmobile=false,thingImmuneToForce=false,effectImmuneToForce=false;
    // impactRotation holds native ThingData.mSpinFactor (full data +0x1fc).
    float forceMultiplier=1,impactRotation=0,grip=0,maxUpwardVelocity=0;
};
// Callbacks run at the original mutation boundaries. An absent callback is an
// unsupported native operation, never an implicit successful visual/audio step.
struct PhysicsForceCallbacks {
    std::function<bool(std::string&)> addComponent;
    std::function<bool(float spinRadians,std::string&)> onForce;
    std::function<bool(float appliedForce,std::string&)> onUpwardForce;
    std::function<bool(float& actualZ,std::string&)> getZLocation;
};
bool applyPhysicsDirectForce(PhysicsState&,const PhysicsForceOwner&,PhysicsVector delta,
    float maxVelocity,const PhysicsForceCallbacks&,std::string&);
bool applyPhysicsForce(PhysicsState&,const PhysicsForceOwner&,PhysicsVector direction,
    float force,bool selfApplied,float maxVelocity,const PhysicsForceCallbacks&,std::string&);
bool applyPhysicsAngleForce(PhysicsState&,const PhysicsForceOwner&,float angle,
    float force,bool selfApplied,float maxVelocity,float footingLost,
    const PhysicsForceCallbacks&,std::string&);
bool applyPhysicsUpwardForce(PhysicsState&,const PhysicsForceOwner&,float force,
    bool selfApplied,float maxVelocity,const PhysicsForceCallbacks&,std::string&);
struct PhysicsFrictionOwner {
    float rawZ=0,offsetZ=0,grip=0,soundFadeDuration=0;
    bool unitAirborne=false,animationAppliesFriction=false;
};
using PhysicsStopCue=std::function<bool(int32_t,float,std::string&)>;
// ApplyFriction writes moveShift as velocity, without multiplying by elapsed;
// native movement consumes it in its separate phase.
bool applyPhysicsFriction(PhysicsState&,float actualElapsed,const PhysicsFrictionOwner&,
    const PhysicsStopCue&,std::string&);
bool physicsComponentElapsed(float worldElapsed,float worldUndoModifier,
    float thingFraction,float thingMultiplier,float& result,std::string&);
bool physicsOfInterest(const PhysicsState&,float ownRawZ,float dataOffsetZ);
struct PhysicsVerticalOwner {
    float downwardTerminalVelocity=0,bounceUpwardForce=0;
    PhysicsForceOwner bounceForce; // Actual owner immunity/grip/upward cap.
};
struct PhysicsVerticalCallbacks {
    std::function<bool(float&,std::string&)> getZ,restingZ;
    std::function<bool(float delta,bool propagate,std::string&)> shiftZ;
    std::function<bool(std::string&)> onTakeoff,onRising,onFalling;
    std::function<bool(float impactVelocity,float reboundVelocity,std::string&)> onTouchdown;
    // Native reads owner bounce force/grip/immunity AFTER OnFalling; Lua can
    // mutate them synchronously. Do not retain a pre-callback property snapshot.
    std::function<bool(PhysicsVerticalOwner&,std::string&)> bounceInputs;
    PhysicsForceCallbacks bounceForce;
};
// PhysicsSystem.UpdateThing's vertical segment only: caller runs real friction
// first, then spin/tethers/resting admission in their source phases. Callback
// mutation is visible to subsequent reads; failure retains preceding writes.
bool updatePhysicsVertical(PhysicsState&,float componentElapsed,
    const PhysicsVerticalOwner&,const PhysicsVerticalCallbacks&,std::string&);
}
