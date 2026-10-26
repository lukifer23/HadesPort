#include "effect_animation.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
static void need(bool result,const std::string& message){if(!result)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
 need(argc==2,"usage: effect_animation_smoke effects.lua");
 lua_State* L=luaL_newstate();need(L!=nullptr,"Lua allocation");
 need(luaL_loadfile(L,argv[1])==0,"catalog load");need(lua_pcall(L,0,1,0)==0,"catalog evaluation");
 SourceValue catalog;std::string error;need(readSourceValue(L,-1,catalog,error),error);lua_close(L);
 EffectAnimationWorld world;need(world.loadCatalog(catalog,error),error);
 // Explicit diagnostic providers exercise the original data's boundary and
 // numerical contract; these are not game particle or marker implementations.
 EffectOwnerState owner;owner.lifetime=17;owner.x=100;owner.y=200;owner.z=5;owner.scale=.7f;owner.isUnit=true;owner.hasPrimaryAnimation=true;
 EffectOwnerProvider owners=[&](int32_t id,EffectOwnerState& out,std::string&){need(id==42,"owner identity");out=owner;return true;};
 size_t draws=0;EffectAnimationRandom random=[&](double& value,std::string&){value=draws==0?.25:draws==1?.75:.5;++draws;return true;};
 uint64_t root=0;need(world.create("LaurelCindersSpawner",42,owners,random,root,error),error);
 need(root==1&&world.size()==2&&draws==2,"real source parent/child allocation and RNG count");
 const auto* parent=world.entity(root);const uint64_t childId=parent->child;const auto* child=world.entity(childId);
 need(child&&child->parent==root&&childId==2,"bidirectional child ownership");
 need(parent->spawner==EffectSpawnerKind::Distance&&child->spawner==EffectSpawnerKind::Time,"typed native components");
 need(std::abs(parent->distanceRemaining-47.5f)<.00001f&&std::abs(child->timeRemaining-.4625f)<.00001f,"child random interval precedes parent distance");
 need(parent->texture&&parent->texture->rgba[3]==0,"actual original blank texture");
 EffectUpdateContext context;context.countsResolved=true;context.phase=EffectUpdatePhase::Time;
 const auto before=world.dump();need(!world.update(.1f,owners,{},random,{},context,error),"missing native marker rejected");need(world.dump()==before&&draws==2,"marker admission before state/RNG mutation");
 EffectMarkerProvider marker=[](int32_t id,const std::string& name,std::array<float,3>& out,std::string&){need(id==42&&name=="head_TRACKING","actual source marker request");out={100,200,30};return true;};
 need(world.update(.1f,owners,marker,random,{},context,error),error);
 const auto beforeEmission=world.dump();need(!world.update(.5f,owners,marker,random,{},context,error),"missing original particle initialization rejected");need(world.dump()==beforeEmission&&draws==2,"emission admission before state/RNG mutation");
 const float initialRemaining=world.entity(childId)->timeRemaining;size_t elapsedCalls=0;
 world.setElapsedProvider([&](const EffectAnimationEntity& entity,float,float& elapsed,std::string&){need(entity.handle==childId,"TimeSpawner clock requested another entity");++elapsedCalls;elapsed=0;return true;});
 need(world.update(.5f,owners,marker,random,{},context,error),error);
 need(elapsedCalls==1&&world.entity(childId)->timeRemaining==initialRemaining&&draws==2,"Frozen entity used the world clock or consumed RNG");
 world.setElapsedProvider([](const EffectAnimationEntity&,float,float&,std::string& detail){detail="diagnostic unavailable entity clock";return false;});
 const auto beforeClockFailure=world.dump();need(!world.update(.1f,owners,marker,random,{},context,error),"unavailable entity clock accepted");
 need(world.dump()==beforeClockFailure&&draws==2,"Entity clock rejection mutated time or RNG");
 world.setElapsedProvider([](const EffectAnimationEntity&,float seconds,float& elapsed,std::string&){elapsed=seconds*.25f;return true;});
 need(world.update(.5f,owners,marker,random,{},context,error),error);
 need(std::abs(world.entity(childId)->timeRemaining-(initialRemaining-.125f))<.000001f&&draws==2,"TimeSpawner ignored its per-entity elapsed");
 world.setElapsedProvider({});
 size_t preparations=0;std::vector<EffectSpawnRequest> requests;EffectEmissionProvider emissions;
 emissions.prepare=[&](const EffectSpawnRequest& request,std::string&){++preparations;need(request.animation=="LaurelCindersAmbient"||request.animation=="LaurelCinders","retained original selector");return true;};
 emissions.create=[&](const EffectSpawnRequest& request,uint64_t& handle,std::string&){requests.push_back(request);handle=1000+requests.size();return true;};
 emissions.alive=[](uint64_t){return true;};
 need(world.update(.5f,owners,marker,random,emissions,context,error),error);
 need(requests.size()==1&&!requests[0].distance&&requests[0].position[2]==30&&draws==3,"source time spawner callback and interval");
 context.phase=EffectUpdatePhase::Distance;context.managerSpawnedThisFrame=0;
 need(world.update(0,owners,marker,random,emissions,context,error),error);need(requests.size()==1,"zero previous position establishes distance baseline");
 owner.x=200;need(world.update(0,owners,marker,random,emissions,context,error),error);
 need(requests.size()==2&&requests[1].distance&&std::abs(requests[1].position[0]-147.5f)<.0001f&&requests[1].position[1]==200,"original linear distance interpolation");
 need(world.entity(root)->totalSpawned==1,"source VisualFxFrameCap=1");
 const auto beforeCap=world.dump();context.managerVisualFxCount=800;owner.x=500;
 need(world.update(.1f,owners,marker,random,emissions,context,error),error);need(world.dump()==beforeCap,"source manager cap200 times compiled multiplier4 gates mutation");
 need(world.destroy(root,error)&&world.size()==0,"recursive child destruction");
 const auto initialDraws=draws;need(!world.create("LaurelCinders",42,owners,random,root,error)&&world.size()==0&&draws==initialDraws,"unsupported selector initialization rejected before RNG");
 need(!world.faulted(),"admission failures do not fault world");
 std::cout<<"{\"source_definitions\":14,\"allocated_components\":2,\"rng_draws\":"<<draws<<",\"diagnostic_emissions\":"<<requests.size()<<",\"read_only_admission_failures\":4,\"per_entity_elapsed\":true,\"original_blank_rgba\":[0,0,0,0],\"game_particle_execution\":false,\"result\":\"passed\"}\n";
 return 0;
 }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
