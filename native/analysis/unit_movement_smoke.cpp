#include "../app/unit_movement.hpp"
#include "../app/unit_path.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace hades;
static void boundedPathFailureChoice(){
    // Diagnostic empty-plane node admission; this calls the actual native
    // graph/heap/path builder without supplying any gameplay actor substitutes.
    // CalcPath VA1802ea16c..181 updates nearest only after the bounds check;
    // the budget-ending node must not replace the last expanded nearest node.
    UnitPathConfig config;config.nodeCountLimit=3;config.lookAheadDistance=4096;
    if(config.onFailBehavior!=1)throw std::runtime_error("Native last-node path failure default changed");
    std::vector<std::array<float,2>> checked;
    const auto clearPlane=[&](float x,float y,bool& blocked,std::string& error){checked.push_back({x,y});blocked=false;error.clear();return true;};
    std::vector<UnitPathNode> last,nearest;std::string error;
    if(!planUnitPath(0,0,1000,200,config,clearPlane,last,error))throw std::runtime_error(error);
    if(checked.size()!=2||last.empty()||last.front().x!=128||last.front().y!=64)
        throw std::runtime_error("Bounded native path did not retain the last popped node");
    const auto lastChecks=checked;checked.clear();config.onFailBehavior=0;
    if(!planUnitPath(0,0,1000,200,config,clearPlane,nearest,error))throw std::runtime_error(error);
    if(checked!=lastChecks||nearest.empty()||nearest.front().x!=64||nearest.front().y!=32)
        throw std::runtime_error("Bounded native path did not retain the nearest previously expanded node");
    if(nearest.front().distance>=1019||nearest.front().distance<=last.front().distance)
        throw std::runtime_error("Native nearest-node diagnostic no longer distinguishes partial failure targets");
}
int main(){try{boundedPathFailureChoice();std::string error;PhysicsState p;UnitMovementState s;UnitMoveData d;d.canMove=true;d.playerControlled=true;d.speed=540;d.acceleration=2000;PhysicsForceOwner owner;PhysicsForceCallbacks cb;unsigned forceCalls=0;cb.onForce=[&](float,std::string&){++forceCalls;return true;};cb.addComponent=[](std::string&){return true;};auto need=[&](bool b){if(!b)throw std::runtime_error(error);};need(applyUnitMoveInput(p,s,d,0,1,1.f/60,owner,cb,error));if(p.velocity.x!=540||p.velocity.y!=0||!p.lastForceSelfApplied||s.speedFraction!=1)throw std::runtime_error("Native ordinary MoveInput mismatch");p={};need(applyUnitMoveInput(p,s,d,0,.2f,1.f/60,owner,cb,error));if(p.velocity.x!=108)throw std::runtime_error("Source low speed fraction mismatch");p={};d.speedModifier=.5;need(applyUnitMoveInput(p,s,d,0,1,1.f/60,owner,cb,error));if(p.velocity.x!=270)throw std::runtime_error("Actual live modifier not applied");p={};d.speedModifier=1;p.footingLost=1;p.velocity.x=100;need(applyUnitMoveInput(p,s,d,0,1,1.f/60,owner,cb,error));if(std::abs(p.velocity.x-(100+2000.f/60))>.001f||p.footingLost!=1)throw std::runtime_error("Source footing-lost acceleration branch mismatch");d.canMove=false;const auto old=p.velocity;need(applyUnitMoveInput(p,s,d,1,1,1.f/60,owner,cb,error));if(p.velocity.x!=old.x||p.velocity.y!=old.y)throw std::runtime_error("Native CanMove gate mutated Physics");const MovementPoint body[]{{-2,-2},{2,-2},{2,2},{-2,2}},hit[]{{1,-1},{3,-1},{3,1},{1,1}},miss[]{{4,-1},{6,-1},{6,1},{4,1}};if(!movementPolygonsOverlap(body,hit)||movementPolygonsOverlap(body,miss))throw std::runtime_error("Live polygon narrowphase adaptation mismatch");std::cout<<"{\"result\":\"passed\",\"source_ground_velocity\":540,\"source_low_stick_velocity\":108,\"live_modifier_velocity\":270,\"footing_lost_acceleration_observed\":true,\"can_move_gate_preserved\":true,\"actual_on_force_calls\":"<<forceCalls<<",\"collision_algorithm_adaptation\":true,\"live_room_input_execution_observed\":false}\n";}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
