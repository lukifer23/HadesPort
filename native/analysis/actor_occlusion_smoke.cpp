#include "actor_lighting.hpp"
#include <cassert>
#include <iostream>
int main(){using namespace hades;ActorOccluder source{1,5,1,{{-10,-10},{10,-10},{10,10},{-10,10}}};
assert(actorOccluderIntersects(source,{-20,0},{20,0}));assert(actorOccluderIntersects(source,{0,20},{0,-20}));
assert(!actorOccluderIntersects(source,{-20,11},{20,11}));assert(actorOccluderIntersects(source,{-20,10},{20,10}));
assert(actorOccluderIntersects(source,{0,0},{20,0}));assert(actorOccluderIntersects(source,{-2,0},{2,0}));
assert(actorOccluderIntersects(source,{-20,-20},{-10,-10}));assert(!actorOccluderIntersects({}, {-20,0},{20,0}));
std::cout<<"{\"result\":\"passed\",\"cpu_ray_adapter\":true,\"cases\":8,\"original_execution_observed\":false}\n";}
