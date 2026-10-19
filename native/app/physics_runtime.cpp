#include "physics_runtime.hpp"
#include <algorithm>
#include <cmath>

namespace hades {
namespace {
float magnitude(PhysicsVector v){return std::sqrt(v.x*v.x+v.y*v.y);}
bool finite(float v,std::string& error){if(std::isfinite(v))return true;error="Nonfinite native Physics force input";return false;}
bool finite(PhysicsVector v,std::string& error){return finite(v.x,error)&&finite(v.y,error);}
}
bool applyPhysicsDirectForce(PhysicsState& state,const PhysicsForceOwner& owner,
    PhysicsVector delta,float maxVelocity,const PhysicsForceCallbacks& callbacks,std::string& error){
    if(!finite(delta,error)||!finite(maxVelocity,error)||!finite(owner.impactRotation,error))return false;
    if(delta.x==0&&delta.y==0){error.clear();return true;}
    const float impulse=magnitude(delta),oldMagnitude=magnitude(state.velocity);
    const float maxMagnitude=std::max(std::max(impulse,maxVelocity),oldMagnitude);
    PhysicsVector next{state.velocity.x+delta.x,state.velocity.y+delta.y};
    const float nextSquared=next.x*next.x+next.y*next.y;
    if(nextSquared>maxMagnitude*maxMagnitude){const float inverse=1.f/magnitude(next);next.x=(next.x*inverse)*maxMagnitude;next.y=(next.y*inverse)*maxMagnitude;}
    const float spinRadians=float(double(impulse*owner.impactRotation)*0.017453292519943295);
    if(!finite(next,error)||!finite(spinRadians,error))return false;
    if(!callbacks.onForce||(spinRadians>.0001f&&!callbacks.addComponent)){
        error="Native direct force requires Thing.OnForce/component providers";return false;
    }
    state.velocity=next;
    if(spinRadians>.0001f){state.spin=std::max(state.spin,spinRadians);if(!callbacks.addComponent(error))return false;}
    return callbacks.onForce(spinRadians,error);
}
bool applyPhysicsForce(PhysicsState& state,const PhysicsForceOwner& owner,
    PhysicsVector direction,float force,bool selfApplied,float maxVelocity,
    const PhysicsForceCallbacks& callbacks,std::string& error){
    if(!finite(direction,error)||!finite(force,error)||!finite(owner.forceMultiplier,error))return false;
    // Animation immunity applies even to a self-applied planar force.
    if(owner.animationOwnerImmobile){error.clear();return true;}
    if(!selfApplied)force*=owner.forceMultiplier;
    if(force==0){error.clear();return true;}
    state.lastForceSelfApplied=selfApplied;
    if(!selfApplied&&(owner.thingImmuneToForce||owner.effectImmuneToForce)){error.clear();return true;}
    const float length=magnitude(direction);
    // Original divides by zero for a zero direction. Reject that input instead
    // of propagating NaNs into the native world's geometry and render buffers.
    if(!(length>0)||!std::isfinite(length)){error="Native force direction has zero/invalid length";return false;}
    const float inverse=1.f/length;
    return applyPhysicsDirectForce(state,owner,{(direction.x*inverse)*force,(direction.y*inverse)*force},maxVelocity,callbacks,error);
}
bool applyPhysicsAngleForce(PhysicsState& state,const PhysicsForceOwner& owner,float angle,
    float force,bool selfApplied,float maxVelocity,float footingLost,
    const PhysicsForceCallbacks& callbacks,std::string& error){
    if(!finite(angle,error)||!finite(force,error)||!finite(footingLost,error))return false;
    if(force==0){error.clear();return true;}
    if(force<0){angle+=3.1415927410125732f;force=std::abs(force);}
    if(!applyPhysicsForce(state,owner,{std::cos(angle),-std::sin(angle)},force,selfApplied,maxVelocity,callbacks,error))return false;
    // Source writes this after the vector call even when immunity rejected it.
    if(footingLost>0)state.footingLost=footingLost;
    return true;
}
bool applyPhysicsUpwardForce(PhysicsState& state,const PhysicsForceOwner& owner,float force,
    bool selfApplied,float maxVelocity,const PhysicsForceCallbacks& callbacks,std::string& error){
    if(!finite(force,error)||!finite(maxVelocity,error)||!finite(owner.maxUpwardVelocity,error)||!finite(owner.grip,error))return false;
    if(force==0){error.clear();return true;}
    if(!selfApplied&&(owner.animationOwnerImmobile||owner.thingImmuneToForce||owner.effectImmuneToForce||owner.grip>=99999.f)){error.clear();return true;}
    if(std::abs(force)<1){state.upVelocity=std::max(state.upVelocity,0.f);error.clear();return true;}
    const float cap=std::min(maxVelocity,owner.maxUpwardVelocity);
    if(cap<3.4028234663852886e38f){const float excess=(force+state.upVelocity)-cap;if(excess>0){force-=excess;if(force<=0){error.clear();return true;}}}
    if(!callbacks.onUpwardForce||!callbacks.getZLocation){error="Native upward force requires owner takeoff/Z providers";return false;}
    const float next=state.upVelocity+force;if(!finite(next,error))return false;
    state.upVelocity=next;
    if(!callbacks.onUpwardForce(force,error))return false;
    float z=0;return callbacks.getZLocation(z,error);
}
bool physicsComponentElapsed(float elapsed,float undo,float fraction,float multiplier,
    float& result,std::string& error){
    if(!finite(elapsed,error)||!finite(undo,error)||!finite(fraction,error)||!finite(multiplier,error)||elapsed<0)return false;
    const volatile float complement=1.f-undo;
    const volatile float weighted=complement*fraction;
    const volatile float denominator=weighted+undo;
    if(denominator==0){error="Native Physics component time denominator is zero";return false;}
    const volatile float unscaled=elapsed/denominator;
    const float next=unscaled*multiplier;if(!finite(next,error))return false;
    result=next;error.clear();return true;
}
bool physicsOfInterest(const PhysicsState& state,float ownRawZ,float dataOffsetZ){
    return state.tetherCount||state.velocity.x!=0||state.velocity.y!=0||state.upVelocity!=0||state.spin!=0||ownRawZ>dataOffsetZ;
}
bool applyPhysicsFriction(PhysicsState& state,float elapsed,const PhysicsFrictionOwner& owner,
    const PhysicsStopCue& stopCue,std::string& error){
    if(!finite(elapsed,error)||elapsed<0||!finite(owner.rawZ,error)||!finite(owner.offsetZ,error)||!finite(owner.grip,error)||!finite(owner.soundFadeDuration,error)||!finite(state.velocity,error))return false;
    PhysicsVector next=state.velocity;
    const bool moving=next.x!=0||next.y!=0;
    bool frictionOvershot=false;
    if(moving&&((owner.rawZ<=owner.offsetZ&&!owner.unitAirborne)||owner.animationAppliesFriction||state.ignoreGravity)){
        const float squared=next.x*next.x+next.y*next.y;
        const float inverse=1.f/std::sqrt(squared);
        const float xReduction=((inverse*next.x)*owner.grip)*elapsed;
        const float yReduction=((inverse*next.y)*owner.grip)*elapsed;
        if(xReduction*xReduction+yReduction*yReduction>squared){next={};frictionOvershot=true;}
        else{next.x-=xReduction;next.y-=yReduction;}
    }
    if(!finite(next,error))return false;
    const bool stop=moving&&next.x==0&&next.y==0&&state.impulsingSoundId;
    if(stop&&!stopCue){error="Native friction requires actual impulsing audio channel stop";return false;}
    state.footingLost-=elapsed; // No clamp and no positive-only gate.
    // Equality with the exact reduction does not clear lastForceSelfApplied.
    if(frictionOvershot)state.lastForceSelfApplied=false;
    state.velocity=next;
    if(stop){if(!stopCue(state.impulsingSoundId,owner.soundFadeDuration,error))return false;state.impulsingSoundId=0;}
    state.moveShift=state.velocity;error.clear();return true;
}
bool updatePhysicsVertical(PhysicsState& state,float elapsed,const PhysicsVerticalOwner& owner,
    const PhysicsVerticalCallbacks& cb,std::string& error){
    if(!finite(elapsed,error)||elapsed<0||!finite(state.upVelocity,error)||!finite(state.gravity,error)||
       !finite(state.bounce,error)||!finite(owner.downwardTerminalVelocity,error)||!finite(owner.bounceUpwardForce,error))return false;
    if(!cb.getZ||!cb.restingZ){error="Native vertical Physics requires actual Z/resting-Z providers";return false;}
    if(!state.ignoreGravity){
        bool gravity=state.upVelocity>0;
        if(!gravity){float z=0,rest=0;if(!cb.getZ(z,error)||!cb.restingZ(rest,error))return false;if(!finite(z,error)||!finite(rest,error))return false;gravity=z>rest;}
        if(gravity){
            const volatile float acceleration=elapsed*state.gravity;
            state.upVelocity-=acceleration;
            const float terminal=-owner.downwardTerminalVelocity;
            if(state.upVelocity<terminal)state.upVelocity=terminal;
        }
    }
    const float delta=elapsed*state.upVelocity;
    if(!finite(delta,error))return false;
    if(delta>0){
        float z=0;if(!cb.getZ(z,error)||!finite(z,error))return false;
        if(std::abs(z)<delta){if(!cb.onTakeoff){error="Native Physics Takeoff callback unavailable";return false;}if(!cb.onTakeoff(error))return false;}
        if(!cb.shiftZ||!cb.onRising){error="Native Physics upward ShiftZ/Rising providers unavailable";return false;}
        if(!cb.shiftZ(delta,true,error))return false;
        if(!cb.onRising(error))return false;
    }else if(delta<0){
        float rest=0,before=0;if(!cb.restingZ(rest,error)||!cb.getZ(before,error))return false;
        if(!finite(rest,error)||!finite(before,error))return false;
        if(!cb.shiftZ||!cb.onFalling){error="Native Physics downward ShiftZ/Falling providers unavailable";return false;}
        if(!cb.shiftZ(delta,true,error)||!cb.onFalling(error))return false;
        float after=0;if(!cb.getZ(after,error)||!finite(after,error))return false;
        if(after<=rest){
            const float impact=state.upVelocity; // After Falling callback mutations.
            if(state.bounce>.0001f&&std::abs(impact)>.0001f&&before>rest){
                PhysicsVerticalOwner live=owner;
                if(!cb.bounceInputs){error="Native Physics bounce requires post-Falling owner inputs";return false;}
                if(!cb.bounceInputs(live,error)||!finite(live.bounceUpwardForce,error))return false;
                const float rebound=std::abs(state.bounce*impact)+live.bounceUpwardForce;
                state.upVelocity=0;
                if(!applyPhysicsUpwardForce(state,live.bounceForce,rebound,false,
                    3.4028234663852886e38f,cb.bounceForce,error))return false;
            }else state.upVelocity=0;
            if(!cb.onTouchdown){error="Native Physics Touchdown provider unavailable";return false;}
            if(!cb.onTouchdown(impact,state.upVelocity,error))return false;
        }
    }
    error.clear();return true;
}
}
