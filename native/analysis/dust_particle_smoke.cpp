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
void need(bool v,const std::string& e){if(!v)throw std::runtime_error(e);}
int main(int argc,char** argv){try{
 need(argc==4,"dust_particle_smoke SPAWNER_ASSETS CINDER_ASSETS LEAF_ASSETS");std::string error;FileAssets primaryAssets(argv[1]),cinderAssets(argv[2]),leafAssets(argv[3]);
 lua_State* L=luaL_newstate();need(luaL_loadfile(L,(std::string(argv[1])+"/effects.lua").c_str())==0,"Original source Lua load");need(lua_pcall(L,0,1,0)==0,"Original source Lua evaluation");SourceValue catalog;need(readSourceValue(L,-1,catalog,error),error);lua_close(L);
 ActorAnimationBank primaryBank;primaryBank.load(primaryAssets,"spawner-frames.bin");auto cinderBank=std::make_shared<ActorAnimationBank>(),leafBank=std::make_shared<ActorAnimationBank>();cinderBank->load(cinderAssets,"cinder-frames.bin");leafBank->load(leafAssets,"room-leaf-frames.bin","room-leaf-facing.bin","room-leaf-sha256.bin");
 EffectAnimationWorld spawners;need(spawners.loadCatalog(catalog,error),error);EffectParticleWorld particles;need(particles.load(catalog,cinderBank,error)&&particles.addImageBank(leafBank,error)&&particles.setRegistry(spawners.registry(),error),error);
 EffectOwnerState owner;owner.lifetime=17;owner.x=100;owner.y=200;owner.scale=1;owner.baseColor=std::array<uint8_t,4>{80,120,160,128};owner.baseHsv=std::array<float,4>{.1f,0,0,0};
 EffectOwnerProvider owners=[&](int32_t,EffectOwnerState& out,std::string&){out=owner;return true;};size_t draws=0;EffectAnimationRandom random=[&](double& v,std::string&){++draws;v=.5;return true;};std::vector<uint32_t> bounds;EffectBoundedRandom integers=[&](uint32_t b,uint32_t& v,std::string&){bounds.push_back(b);v=0;return true;};
 const auto frame=primaryBank.sample(*primaryBank.find("DustSpawner"),0,0);EffectTexture texture;texture.name="Fx\\DustFlyers\\DustFlyerA";texture.atlas="Fx_Textures268";texture.width=frame.width;texture.height=frame.height;texture.pixels.assign(frame.rgba.begin(),frame.rgba.end());uint64_t sourceId=0;need(spawners.createPrimary("DustSpawner",42,owners,random,std::move(texture),sourceId,error),error);const auto& source=*spawners.entity(sourceId);
 EffectSpawnRequest request;request.source=sourceId;request.ownerId=42;request.animation="DustFall";request.position={100,200,0};request.nativeAngle=-1.5707963705062866f;
 EffectParticleAdmission admission;admission.nativeManagerAdmissionResolved=true;admission.completePermanentRegistry=true;admission.permanentVfxCap=80;admission.ownerTimeModifierFraction=1;
 const auto prior=draws;auto missing=owner;missing.baseColor.reset();need(!particles.prepare(request,source,missing,admission,random,integers,error)&&draws==prior,"Missing actual owner packed color must reject before RNG");
 uint64_t id=0;need(particles.create(request,source,owner,admission,random,integers,id,error),error);auto p=particles.particles().at(id);
 need(draws-prior==8&&bounds==std::vector<uint32_t>{7},"Dust native random draw order/count");need(p.name=="DustFallA"&&p.constant&&p.duration==6&&p.durationRemaining==6&&p.speed==17.5f&&p.baseColor==std::array<uint8_t,4>{80,120,160,121}&&p.imageBank==leafBank,"Dust typed components/color/source image bank");
 need(particles.phase(EffectParticlePhase::TimeRemaining,1,integers,error)&&particles.phase(EffectParticlePhase::Fraction,1,integers,error)&&particles.phase(EffectParticlePhase::Translation,1,integers,error),error);p=particles.particles().at(id);need(p.durationRemaining==5&&std::abs(p.animationFraction-1.f/6)<.00001f&&std::abs(p.x-82.5f)<.0001f,"Original Constant time/fraction/velocity equations");
 EffectParticleDrawContext context;context.sourceResolved=true;context.group=10;context.color={1,1,1,1};context.sortY=p.y;ActorVisual visual;need(effectParticleVisual(particles,id,context,visual,error),error);need(visual.sourceBank==leafBank.get()&&visual.color[3]==121.f/255.f&&visual.hsv[0]==.1f,"Actual typed packed color/HSV reaches original-frame renderer adapter");
 for(int i=0;i<5;++i)need(particles.phase(EffectParticlePhase::TimeRemaining,1,integers,error),error);need(particles.phase(EffectParticlePhase::ConstantUpdateInternal,0,integers,error)&&particles.alive(id),"Native Constant duration threshold is strictly negative");need(particles.phase(EffectParticlePhase::TimeRemaining,.01f,integers,error)&&particles.phase(EffectParticlePhase::ConstantUpdateInternal,0,integers,error)&&!particles.alive(id),"Native Constant lifetime completion");particles.disposeDone();need(spawners.registry()->managerCount(0)==0,"Actual completed Constant manager disposal");
 std::cout<<"{\"result\":\"passed\",\"original_dust_variants\":7,\"uniform_draws\":8,\"selector_bound\":7,\"packed_alpha\":121,\"source_constant_lifetime\":true,\"android_world_execution_observed\":false}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
