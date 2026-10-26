#pragma once
#include "body_transition.hpp"
#include "unit_path.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string>
namespace hades {
struct ActorMoveOrder {int32_t ownerId=0;uint64_t lifetime=0;float goalX=0,goalY=0;bool precise=true;UnitPathConfig pathConfig;UnitPathState path;
 // MoveAI+0x08 Target stores location at+0, Thing ID at+8. SetTarget with a
 // Thing changes only its ID; fallback XY retains the AI's previous value.
 int32_t targetId=0;float targetFallbackX=0,targetFallbackY=0;
 // MoveAI+0x18/+0x1c/+0x20, rewritten by each ScriptAction::Move.
 int32_t liveOffsetFromId=0;float liveOffsetDistance=0,liveOffsetAngle=0,targetHeadingShift=0;
};
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
 // MoveAIPathfind+0x11c survives SetTarget2e94a0 and Stop. Unlike a move
 // request, this collision timer belongs to the Unit's persistent AI.
 std::map<uint64_t,float> pathCollisionTimes;
 std::map<uint64_t,float> pathRepathDelays; // MoveAI+0x118 also survives SetTarget/Stop.
 // MoveAI ctor VA1802e8454 zeros Target XY; MoveAIPathfind::SetTarget
 // VA1802e94a0 changes XY only without a destination Thing. Keep this field
 // outside the transient order so a later Thing target retains its fallback.
 std::map<uint64_t,std::array<float,2>> targetLocations;
 // ScriptAction::Move VA180274a8a..a98 persists only positive overrides into
 // the Unit's AI-owned MoveAIData+0xe0; later Move calls retain that value.
 std::map<uint64_t,float> successDistanceOverrides;
 std::map<uint64_t,ActorZTransition> heights;
 // ObstacleManager update set (vector_set<Handle<Obstacle>>); keyed by the
 // actor lifetime, which stands in for native handle order.
 std::map<uint64_t,ObstacleMove> obstacles;
};
}
