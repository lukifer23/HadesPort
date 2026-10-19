#pragma once
#include "physics_runtime.hpp"
#include <span>
#include <vector>
namespace hades {
struct UnitMovementState {
 float speedFraction=0,previousSpeedFraction=0,unappliedSpeed=0,goalAngle=0;
 float cosmeticAngle=0,cosmeticAngleTarget=0; // Unit+3d0/+3d4, SetCompleteAngle2f8770.
 float lastMoveStartX=0,lastMoveStartY=0;bool initialized=false,moving=false;
};
struct UnitMoveData {
 bool canMove=false,onlyForward=false,playerControlled=false;
 float speed=0,speedAddition=0,speedModifier=1,playerSpeedMultiplier=1,acceleration=0,ownerAngle=0;
};
// Exact numeric branches of native Unit.MoveInput2f6220: ordinary direct
// velocity, external-force high-velocity handling, footing-lost acceleration.
bool applyUnitMoveInput(PhysicsState&,UnitMovementState&,const UnitMoveData&,float angle,
 float fraction,float elapsed,const PhysicsForceOwner&,const PhysicsForceCallbacks&,std::string&);
struct MovementPoint {float x=0,y=0;};
// Conservative native-Android narrowphase adaptation for original transformed
// polygons. Preserves source points; no baked preview-body/collider ownership.
bool movementPolygonsOverlap(std::span<const MovementPoint>,std::span<const MovementPoint>);
}
