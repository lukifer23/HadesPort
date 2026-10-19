#include "../host/file_assets.hpp"
#include "../app/effect_animation.hpp"
#include "../app/actor_animation.hpp"
#include <cmath>
#include <iostream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
static void need(bool v,const std::string& e){if(!v)throw std::runtime_error(e);}
int main(int argc,char** argv){try{
 need(argc==2,"room_spawner_smoke SOURCE_ASSETS");std::string error;FileAssets assets(argv[1]);ActorAnimationBank bank;bank.load(assets,"spawner-frames.bin");lua_State* L=luaL_newstate();auto path=std::string(argv[1])+"/effects.lua";need(luaL_loadfile(L,path.c_str())==0,"Original source catalog load");need(lua_pcall(L,0,1,0)==0,"Source catalog evaluation");SourceValue catalog;need(readSourceValue(L,-1,catalog,error),error);lua_close(L);EffectAnimationWorld world;need(world.loadCatalog(catalog,error),error);
 size_t randomDraws=0;EffectAnimationRandom rng=[&](double& x,std::string&){x=.5;++randomDraws;return true;};
 EffectOwnerProvider owners=[](int32_t id,EffectOwnerState& out,std::string&){out.lifetime=uint64_t(id)+1;out.x=100;out.y=200;out.scale=1;return true;};
 std::map<std::string,EffectTexture> textures;
 for(const auto& d:catalog.get("PrimaryGraphics")->array()){EffectTexture t;t.name=d.get("graphic")->text();t.atlas=d.get("atlas")->text();const auto f=bank.sample(size_t(d.get("clip_index")->number()),0,0);t.width=f.width;t.height=f.height;t.pixels.assign(f.rgba.begin(),f.rgba.end());textures.emplace(d.get("animation")->text(),std::move(t));}
 std::vector<uint64_t> handles;size_t bat=0;
 for(const auto& r:catalog.get("PrimaryRecipes")->array()){const auto name=r.get("graphic")->text();uint64_t handle=0;need(world.createPrimary(name,int32_t(r.get("owner_map_id")->number()),owners,rng,textures.at(name),handle,error),error);handles.push_back(handle);const auto* e=world.entity(handle);const auto* a=world.registry()->find(handle);need(e&&a&&a->initialized&&a->type==NativeAnimationType::Constant&&!a->managerMember&&!a->skipUpdate&&e->spawner==EffectSpawnerKind::Time,"Native initial primary ownership/query admission");if(name=="BatCluster"){++bat;need(e->hasDuration&&e->duration==5.5f&&std::abs(e->timeRemaining-.105f)<.00001f,"Original Bat duration precedes interval random draw");}}
 need(handles.size()==9&&bat==2&&randomDraws==11&&world.registry()->allocationCount()==9&&world.registry()->managerCount(0)==0,"Nine real allocations, zero manager additions");
 for(auto h:handles)need(world.setSkipUpdate(h,true,error),error);EffectUpdateContext context;context.phase=EffectUpdatePhase::Time;context.countsResolved=true;const auto before=world.dump();need(world.update(.5f,owners,{},rng,{},context,error),error);need(world.dump()==before&&randomDraws==11,"Actual SkipUpdate excludes all spawners before mutation");
 need(world.setSkipUpdate(handles.front(),false,error),error);const auto prior=world.dump();need(!world.update(.5f,owners,{},rng,{},context,error),"Unsupported actual Dust leaf must fail explicitly");need(world.dump()==prior&&randomDraws==11,"Missing leaf adapter fails before spawner time/RNG changes");
 std::cout<<"{\"result\":\"passed\",\"original_primary_allocations\":9,\"manager_vector_members\":0,\"original_graphics_decoded\":3,\"diagnostic_rng_draws\":11,\"skip_update_guard\":true,\"unsupported_leaf_preflight_read_only\":true,\"original_world_execution_observed\":false}\n";
 }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
