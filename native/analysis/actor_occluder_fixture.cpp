#include "actor_runtime.hpp"
#include "actor_light_world.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
struct DiskFile:AssetFile {std::ifstream in;size_t length=0,at=0;explicit DiskFile(const std::filesystem::path& p):in(p,std::ios::binary){if(in)length=std::filesystem::file_size(p);}size_t size()const override{return length;}size_t remaining()const override{return length-at;}size_t read(void* out,size_t n)override{in.read(static_cast<char*>(out),n);const auto got=size_t(in.gcount());at+=got;return got;}bool seek(size_t n)override{if(n>length)return false;in.clear();in.seekg(n);if(!in)return false;at=n;return true;}};
struct DiskStore:AssetStore{std::filesystem::path root;std::unique_ptr<AssetFile> open(std::string_view name)override{auto f=std::make_unique<DiskFile>(root/std::string(name));if(f->in)return f;return {};}};

int main(int argc,char** argv){try{
 if(argc!=2&&argc!=3)throw std::runtime_error("actor_occluder_fixture ACTUAL_ACTORS_LUA");lua_State* L=luaL_newstate();if(luaL_loadfile(L,argv[1])||lua_pcall(L,0,1,0))throw std::runtime_error(lua_tostring(L,-1));SourceValue catalog;std::string error;if(!readSourceValue(L,-1,catalog,error))throw std::runtime_error(error);lua_close(L);
 std::vector<int32_t> ids;for(const auto& row:catalog.get("map")->array()){const auto& map=*row.get("map");const auto key=row.get("definition")->text();const auto& def=*catalog.get("definitions")->get(key);if(def.get("kind")->text()=="Obstacle"&&map.get("DoCreate")->boolean())ids.push_back(int32_t(row.get("id")->number()));}
 ActorWorld world;if(!world.loadCatalog(std::move(catalog),error))throw std::runtime_error(error);std::map<int32_t,ObjectSnapshot> objects;
 for(const auto id:ids){auto& o=objects[id];if(!world.initializeMapActor(id,o,nullptr,[](double& r,std::string& e){r=.5;e.clear();return true;},error)||!world.registerActor(id,o,error)||!world.initializeOccluder(id,error)||!world.updateOccluder(id,error))throw std::runtime_error(std::to_string(id)+": "+error);}
 if(argc==3){
  DiskStore assets;assets.root=argv[2];auto bank=std::make_shared<ActorAnimationBank>();bank->load(assets,"actor-light-frames.bin");
  L=luaL_newstate();const auto catalogPath=assets.root/"actor-lights.lua";if(luaL_loadfile(L,catalogPath.c_str())||lua_pcall(L,0,1,0))throw std::runtime_error(lua_tostring(L,-1));SourceValue lights;if(!readSourceValue(L,-1,lights,error))throw std::runtime_error(error);lua_close(L);
  auto registry=std::make_shared<AnimationRegistry>();ActorLightWorld lightWorld(world);if(!lightWorld.load(std::move(lights),bank,registry,error))throw std::runtime_error(error);
  unsigned draws=0;const auto random=[&](double& r,std::string& e){++draws;r=.5;e.clear();return true;};
  for(const auto id:ids)if(!lightWorld.admit(id,random,error))throw std::runtime_error(std::to_string(id)+": "+error);
  const auto sourceLights=lightWorld.snapshot();if(sourceLights.size()!=63||draws!=12||registry->managerCount(1)!=0)throw std::runtime_error("Original actor light count/RNG/customownership mismatch");
  for(const auto& light:sourceLights){ActorLightVisual visual;if(!lightWorld.visual(light.ownerId,world.actor(light.ownerId)->baseColorBytes,visual,error))throw std::runtime_error(error);if(visual.image.frame.rgba.empty())throw std::runtime_error("Missing original light pixels");}
  if(!lightWorld.advanceControllers(.6f,1,random,error)||draws!=12)throw std::runtime_error("Source strict light expiry boundary");
  if(!lightWorld.advanceControllers(.0001f,1,random,error)||draws!=18)throw std::runtime_error("Source duration rerandomization count");
  for(const auto& light:sourceLights)if(!lightWorld.remove(light.ownerId,error))throw std::runtime_error(error);
  if(registry->allocationCount()!=0)throw std::runtime_error("Leaked actor light controller");
  std::cout<<"ACTOR_LIGHT_SOURCE actors=63 images=7 initRng=12 afterOneLoopRng=18 managerAdd=0 realPixels=true retirement=true\n";
 }
 const auto before=world.occluderSnapshot();if(before.empty())throw std::runtime_error("No actual occluder geometry");size_t custom=0;
 for(const auto& o:before){const auto* value=world.currentProperty(o.ownerId,"Thing.OccluderPoints");if(value&&!value->array().empty()){++custom;if(o.polygon.size()!=value->array().size())throw std::runtime_error("Custom geometry lost");}}
 for(const auto& o:before)if(!world.removeOccluder(o.ownerId,error))throw std::runtime_error(error);
 if(!world.occluderSnapshot().empty())throw std::runtime_error("Occluder removal failed");
 std::cout<<"{\"result\":\"passed\",\"all_do_create_obstacle_fixtures\":"<<ids.size()<<",\"actual_occluders\":"<<before.size()<<",\"custom_occluders\":"<<custom<<",\"deterministic_fixture_rng\":0.5,\"original_execution_observed\":false}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
