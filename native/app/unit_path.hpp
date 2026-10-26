#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
namespace hades {
// MoveAIData::MoveAIData VA1800d3310, MoveAIDataDef PDB size80 at +0x50.
// Authored source can override these values; no platform movement defaults.
struct UnitPathConfig {
 float nodeDistance=32,nodeSuccessDistance=4,lookAheadDistance=256,lookAheadMultiplier=1;
 float successDistance=32,repathDelayMin=.1f,repathDelayMax=.3f,repathTargetMoveDistance=200;
 int32_t nodeCountLimit=INT32_MAX,skippedNodeLimit=4;
 bool stopToRotate=false,slowToRotate=true,moveWithoutPath=false;
 float avoidLocationDistance=0;
 // MoveAIDataDef+0x4c (MoveAIData+0x9c): ctor VA1800d3388 stores 1.
 // Reader VA1800d3a58..3a8e uses case-insensitive enum text comparisons;
 // GOTO_NEAREST_TO_GOAL and unrecognized text -> 0, GOTO_LAST_NODE_FILLED -> 1.
 uint8_t onFailBehavior=1;
};
struct UnitPathNode {int32_t x=0,y=0,cost=0,distance=0;bool alternate=false;};
struct UnitPathState {
 std::vector<UnitPathNode> nodes; // Native end-to-start BuildMoveList order.
 float lastGoalX=0,lastGoalY=0,repathRemaining=0;
 bool initialized=false,rotationPending=false;
};
// Native Pathfinder::CalcPath VA1802e9930, AddNode2ea520,
// BuildMoveList2ea9b0. The callback runs the actual collision query and
// distinguishes ordinary blocked nodes from an unsupported query; no camera/LOS gate,
// no alternate nodes/FinishAtGoal; other branches remain explicit blockers.
bool planUnitPath(float x,float y,float goalX,float goalY,
 const UnitPathConfig&,const std::function<bool(float,float,bool& blocked,std::string&)>& validateNode,
 std::vector<UnitPathNode>& backwards,std::string& error);
}
