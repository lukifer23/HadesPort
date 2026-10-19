#include "../host/file_assets.hpp"
#include "../app/effect_particle.hpp"
#include "../app/effect_particle_visual.hpp"
#include <cmath>
#include <iostream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
static void need(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
int main(int argc,char** argv){try{
 need(argc==4,"room_particle_frame_smoke SPAWNERS CINDERS LEAVES");std::string error;FileAssets primaryAssets(argv[1]),cinderAssets(argv[2]),leafAssets(argv[3]);
 lua_State* L=luaL_newstate();need(luaL_loadfile(L,(std::string(argv[1])+"/effects.lua").c_str())==0,"Original source catalog load");need(lua_pcall(L,0,1,0)==0,"Original catalog evaluation");SourceValue catalog;need(readSourceValue(L,-1,catalog,error),error);lua_close(L);
 auto cinderBank=std::make_shared<ActorAnimationBank>(),leafBank=std::make_shared<ActorAnimationBank>();cinderBank->load(cinderAssets,"cinder-frames.bin");leafBank->load(leafAssets,"room-leaf-frames.bin","room-leaf-facing.bin","room-leaf-sha256.bin");ActorAnimationBank primaryBank;primaryBank.load(primaryAssets,"spawner-frames.bin");
 EffectAnimationWorld world;need(world.loadCatalog(catalog,error),error);EffectParticleWorld particles;need(particles.load(catalog,cinderBank,error)&&particles.addImageBank(leafBank,error)&&particles.setRegistry(world.registry(),error),error);
 EffectOwnerProvider owners=[](int32_t id,EffectOwnerState& owner,std::string&){owner.lifetime=uint64_t(id)+1;owner.x=100;owner.y=200;owner.scale=.7f;owner.baseColor=std::array<uint8_t,4>{80,120,160,128};owner.baseHsv=std::array<float,4>{.1f,0,0,0};return true;};
 size_t draws=0,starts=0,stops=0,selector3=0,selector7=0,drawsAfterPrimary=0;uint32_t nextSound=0;EffectAnimationRandom random=[&](double& v,std::string&){++draws;v=.5;return true;};EffectBoundedRandom integers=[&](uint32_t bound,uint32_t& v,std::string&){selector3+=bound==3;selector7+=bound==7;v=0;return true;};
 std::map<std::string,EffectTexture> textures;
 for(const auto& d:catalog.get("PrimaryGraphics")->array()){EffectTexture t;t.name=d.get("graphic")->text();t.atlas=d.get("atlas")->text();auto f=primaryBank.sample(size_t(d.get("clip_index")->number()),0,0);t.width=f.width;t.height=f.height;t.pixels.assign(f.rgba.begin(),f.rgba.end());textures.emplace(d.get("animation")->text(),std::move(t));}
 std::vector<uint64_t> primaries;for(const auto& row:catalog.get("PrimaryRecipes")->array()){uint64_t h=0;need(world.createPrimary(row.get("graphic")->text(),int32_t(row.get("owner_map_id")->number()),owners,random,textures.at(row.get("graphic")->text()),h,error),error);primaries.push_back(h);}need(primaries.size()==9,"Actual nine selected source primary recipes");drawsAfterPrimary=draws;
 EffectParticleAdmission admission;admission.nativeManagerAdmissionResolved=true;admission.completePermanentRegistry=true;admission.permanentVfxCap=80;admission.ownerTimeModifierFraction=1;admission.componentWorld=&world;
 // Diagnostic callback records the exact requested cue/fade, not audio mixing.
 admission.audio.start=[&](const std::string& cue,int32_t,const std::array<float,3>&,uint32_t& id,std::string&){need(cue=="/SFX/BatGroupLoop","Original Bat source cue");id=++nextSound;++starts;return true;};admission.audio.stop=[&](uint32_t id,float fade,std::string&){need(id&&fade==.2f,"Original Bat source stop fade");++stops;return true;};
 EffectEmissionProvider emit;emit.alive=[&](uint64_t h){return particles.alive(h);};emit.prepare=[&](const EffectSpawnRequest& request,std::string& e){const auto* source=world.entity(request.source);EffectOwnerState owner;if(!source||!owners(request.ownerId,owner,e))return false;return particles.prepare(request,*source,owner,admission,random,integers,e);};emit.create=[&](const EffectSpawnRequest& request,uint64_t& h,std::string& e){const auto* source=world.entity(request.source);EffectOwnerState owner;if(!source||!owners(request.ownerId,owner,e))return false;const auto before=draws;if(!particles.create(request,*source,owner,admission,random,integers,h,e))return false;const auto& p=particles.particles().at(h);const size_t expected=p.name=="EnvironmentBat"?7:p.name=="TartarusEmber01B"?6:p.name.starts_with("TartarusEmber")?9:8;need(draws-before==expected,"Source per-family native init random draw count/order");return true;};
 size_t nested=0,dust=0,bats=0,embers=0,rendered=0;
 for(int step=0;step<12;++step){
  // Explicit sequential platform schedule. This harness validates numerical
  // component phases/ownership, not Windows worker completion equivalence.
  for(auto phase:{EffectParticlePhase::TimeRemaining,EffectParticlePhase::Rotation,EffectParticlePhase::Acceleration,EffectParticlePhase::Gravity,EffectParticlePhase::Fraction,EffectParticlePhase::Heading,EffectParticlePhase::UpwardVelocity,EffectParticlePhase::PlaySpeed,EffectParticlePhase::Translation,EffectParticlePhase::BookUpdateInternal,EffectParticlePhase::ConstantUpdateInternal})need(particles.phase(phase,.55f,integers,error),error);
  need(particles.refreshOwners(owners,error)&&particles.syncSpawners(error),error);particles.disposeDone();
  std::vector<EffectPrimaryReplacement> replacements;need(world.advancePrimaryDurations(.55f,owners,random,replacements,error),error);for(const auto& replacement:replacements)for(auto& h:primaries)if(h==replacement.previous)h=replacement.current;
  EffectUpdateContext context;context.phase=EffectUpdatePhase::Time;context.countsResolved=true;context.durationSystemResolved=true;context.managerVisualFxCount=uint32_t(world.registry()->managerCount(0));need(world.update(.55f,owners,{},random,emit,context,error),error);
  for(const auto& [h,p]:particles.particles())if(particles.alive(h)){dust+=p.name.starts_with("DustFall");bats+=p.name=="EnvironmentBat";embers+=p.name.starts_with("TartarusEmber");nested+=p.name=="TartarusEmber01B";if(p.name=="EnvironmentBat")need(p.ownerScale==1&&EffectParticleWorld::drawAngle(p)==0,"Native Bat Ignore scale and multidirection draw rotation");EffectParticleDrawContext draw;draw.sourceResolved=true;draw.group=10;draw.sortY=p.y;draw.color={1,1,1,1};draw.ownerBaseColor=std::array<uint8_t,4>{80,120,160,128};draw.ownerFractionColor=std::array<uint8_t,4>{255,255,255,255};draw.ownerHsv=std::array<float,3>{.1f,0,0};ActorVisual visual;need(effectParticleVisual(particles,h,draw,visual,error),error);++rendered;}
 }
 need(dust&&bats&&embers&&nested&&starts&&stops&&selector3&&selector7&&drawsAfterPrimary==11&&rendered,"All original room leaf families and nested component/audio continuation exercised");
 std::cout<<"{\"result\":\"passed\",\"selected_primary_allocations\":9,\"sequential_steps\":12,\"diagnostic_step_seconds\":0.55,\"source_image_draw_bindings\":"<<rendered<<",\"nested_ember_observations\":"<<nested<<",\"source_sound_requests\":"<<starts<<",\"source_sound_stops\":"<<stops<<",\"uniform_draws\":"<<draws<<",\"windows_worker_order_equivalence\":false,\"audio_mixing_observed\":false,\"full_room_or_android_execution_observed\":false}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
