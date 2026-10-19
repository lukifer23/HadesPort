#pragma once
#include "body_transition.hpp"
#include "unit_path.hpp"
#include <cstdint>
#include <map>
#include <string>
namespace hades {
struct ActorMoveOrder {int32_t ownerId=0;uint64_t lifetime=0;float goalX=0,goalY=0;bool precise=true;UnitPathConfig pathConfig;UnitPathState path;};
struct ActorZTransition {int32_t ownerId=0;uint64_t lifetime=0;BodyTransition transition;};
// Native TransitionHelper<float> (size 0x50) as used by Obstacle X/Y move
// helpers at Obstacle+0x5a0/+0x5f0: +0 easeIn, +4 easeOut, +8 TimeModifier-
// Fraction, +0x30 active, +0x33 smooth, +0x34 start, +0x38 end, +0x3c duration,
// +0x40 remaining, +0x44 fraction, +0x48 rate divisor. Callback/pingPong/phase
// are unused by Obstacle::MoveTo (Reset clears them) and not modelled.
struct ObstacleMoveAxis {bool active=false,smooth=false;float easeIn=0,easeOut=0,tmf=1,start=0,end=0,duration=0,remaining=0,fraction=0,rate=1;};
struct ObstacleMove {int32_t ownerId=0;uint64_t lifetime=0;ObstacleMoveAxis x,y;};
// Portable ownership for actual script-created Unit targets and world update
// transitions. No renderer interpolation or fake arrival timers.
struct ActorMotionWorld {
 std::map<uint64_t,ActorMoveOrder> moves;
 std::map<uint64_t,ActorZTransition> heights;
 // ObstacleManager update set (vector_set<Handle<Obstacle>>); keyed by the
 // actor lifetime, which stands in for native handle order.
 std::map<uint64_t,ObstacleMove> obstacles;
};
}
