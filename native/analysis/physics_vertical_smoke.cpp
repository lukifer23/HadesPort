#include "../app/physics_runtime.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
using namespace hades;
struct Fixture {
    PhysicsState state;PhysicsVerticalOwner owner;PhysicsVerticalCallbacks callbacks;
    float z=0,rest=0,impact=0,rebound=0;std::vector<std::string> order;
    Fixture(){
        owner.downwardTerminalVelocity=100;owner.bounceForce.maxUpwardVelocity=100;
        callbacks.getZ=[&](float& value,std::string&){value=z;return true;};
        callbacks.restingZ=[&](float& value,std::string&){value=rest;return true;};
        callbacks.shiftZ=[&](float delta,bool propagate,std::string&){assert(propagate);order.push_back("ShiftZ");z+=delta;return true;};
        callbacks.onTakeoff=[&](std::string&){order.push_back("Takeoff");return true;};
        callbacks.onRising=[&](std::string&){order.push_back("Rising");return true;};
        callbacks.onFalling=[&](std::string&){order.push_back("Falling");return true;};
        callbacks.onTouchdown=[&](float i,float r,std::string&){order.push_back("Touchdown");impact=i;rebound=r;z=rest;return true;};
        callbacks.bounceInputs=[&](PhysicsVerticalOwner& live,std::string&){order.push_back("BounceInputs");live=owner;return true;};
        callbacks.bounceForce.getZLocation=callbacks.getZ;
        callbacks.bounceForce.onUpwardForce=[&](float,std::string&){order.push_back("BounceForce");return true;};
    }
    void advance(float dt){std::string error;if(!updatePhysicsVertical(state,dt,owner,callbacks,error)){std::cerr<<error<<'\n';assert(false);}}
};
int main(){
    {
        PhysicsState state;assert(!physicsOfInterest(state,0,0));
        state.acceleration={10,20};state.footingLost=1;assert(!physicsOfInterest(state,0,0));
        assert(physicsOfInterest(state,1,0));assert(!physicsOfInterest(state,1,1));
        state.upVelocity=-1;assert(physicsOfInterest(state,0,0));state.upVelocity=0;
        state.tetherCount=1;assert(physicsOfInterest(state,0,0));
    }
    {
        Fixture f;f.state.upVelocity=10;f.state.gravity=10;f.advance(.1f);
        assert(f.state.upVelocity==9&&std::abs(f.z-.9f)<1e-7f);
        assert((f.order==std::vector<std::string>{"Takeoff","ShiftZ","Rising"}));
    }
    {
        Fixture f;f.state.gravity=0;f.state.upVelocity=10;f.z=1;f.advance(.1f);
        assert((f.order==std::vector<std::string>{"ShiftZ","Rising"})); // Strict takeoff comparison.
    }
    {
        Fixture f;f.z=1;f.state.upVelocity=-10;f.state.gravity=10;f.state.bounce=.5f;
        f.callbacks.onFalling=[&](std::string&){f.order.push_back("Falling");f.state.upVelocity=-20;f.owner.bounceUpwardForce=2;return true;};
        f.advance(.1f);assert(f.impact==-20&&f.rebound==12&&f.state.upVelocity==12&&f.z==0);
        assert((f.order==std::vector<std::string>{"ShiftZ","Falling","BounceInputs","BounceForce","Touchdown"}));
    }
    {
        Fixture f;f.state.upVelocity=-10;f.state.bounce=.5f;f.advance(.1f);
        assert(f.impact==-10&&f.rebound==0&&f.state.upVelocity==0);
        assert((f.order==std::vector<std::string>{"ShiftZ","Falling","Touchdown"})); // BeforeZ was not above rest.
    }
    {
        Fixture f;f.z=100;f.state.gravity=5000;f.owner.downwardTerminalVelocity=5;f.advance(1);
        assert(f.state.upVelocity==-5&&f.z==95);
        assert((f.order==std::vector<std::string>{"ShiftZ","Falling"}));
    }
    {
        Fixture f;f.state.gravity=10;f.state.upVelocity=10;f.state.ignoreGravity=true;f.advance(.1f);
        assert(f.state.upVelocity==10&&f.z==1);
        f.order.clear();f.advance(0);assert(f.order.empty()&&f.state.upVelocity==10);
    }
    {
        Fixture f;f.state.upVelocity=10;f.state.gravity=0;
        f.callbacks.onTakeoff=[&](std::string&){f.order.push_back("Takeoff");f.z=100;return true;};
        f.advance(.1f);assert(f.z==101); // Shift uses callback-mutated owner state.
    }
    {
        Fixture f;f.state.upVelocity=10;f.state.gravity=0;f.callbacks.shiftZ={};std::string error;
        assert(!updatePhysicsVertical(f.state,.1f,f.owner,f.callbacks,error));
        assert((f.order==std::vector<std::string>{"Takeoff"})); // Partial execution is retained.
    }
    std::cout<<"{\"result\":\"passed\",\"cases\":9,\"admission_rest_boundary\":true,\"live_callback_mutation\":true,\"strict_boundaries\":true,\"original_execution_observed\":false}\n";
}
