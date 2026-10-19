#include "actor_spatial.hpp"
#include <cassert>
#include <iostream>
int main(){using namespace hades;ActorSpatialGrid grid;std::string error;std::vector<SpatialObstacle> out;
SpatialObstacle a{1,11,{-600,-2,600,2},true,true,false};assert(grid.update(a,error));
assert(grid.query({-601,-3,-590,3},ObstacleGridCategory::StopsProjectiles,out,error)&&out.size()==1);
assert(grid.query({-50,-3,50,3},ObstacleGridCategory::All,out,error)&&out.size()==1);
a.bounds={2048,2048,2050,2050};a.stopsProjectiles=false;a.movementReaction=true;assert(grid.update(a,error));
assert(grid.query({-50,-3,50,3},ObstacleGridCategory::All,out,error)&&out.empty());
assert(grid.query(a.bounds,ObstacleGridCategory::StopsProjectiles,out,error)&&out.empty());
assert(grid.query(a.bounds,ObstacleGridCategory::MovementReaction,out,error)&&out.size()==1);
assert(grid.update({1,12,a.bounds,true,false,false},error));grid.remove(11);assert(grid.size()==1);
assert(grid.query(a.bounds,ObstacleGridCategory::All,out,error)&&out.size()==1&&out[0].lifetime==12);
grid.remove(12);grid.remove(12);assert(grid.size()==0);
assert(!grid.update({2,20,{0,0,1e30f,2},true,true,false},error));assert(grid.size()==0);
std::cout<<"actual grid: negative/multicell dedup, changed coverage/flags, independent lifetimes, disposal, bounded rejection passed\n";}
