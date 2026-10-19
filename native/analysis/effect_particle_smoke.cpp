#include "../host/file_assets.hpp"
#include "../app/effect_particle.hpp"
#include <cmath>
#include <iostream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
static void need(bool ok,const std::string& error){if(!ok)throw std::runtime_error(error);}
int main(int argc,char** argv){try{
 need(argc==3,"effect_particle_smoke effects.lua CINDER_ASSETS");lua_State* L=luaL_newstate();need(L!=nullptr,"Lua allocation");need(luaL_loadfile(L,argv[1])==0,"Source Lua load");need(lua_pcall(L,0,1,0)==0,"Source Lua evaluation");SourceValue catalog;std::string error;need(readSourceValue(L,-1,catalog,error),error);lua_close(L);
 FileAssets assets(argv[2]);auto bank=std::make_shared<ActorAnimationBank>();bank->load(assets,"cinder-frames.bin");EffectParticleWorld particles;need(particles.load(catalog,bank,error),error);
 EffectAnimationWorld spawners;need(spawners.loadCatalog(catalog,error),error);EffectOwnerState owner;owner.lifetime=17;owner.x=100;owner.y=200;owner.scale=.7f;owner.angle=.25f;owner.isUnit=true;
 EffectOwnerProvider owners=[&](int32_t id,EffectOwnerState& out,std::string&){need(id==42,"Actual diagnostic owner identity");out=owner;return true;};EffectAnimationRandom half=[](double& value,std::string&){value=.5;return true;};uint64_t root=0;need(spawners.create("LaurelCindersSpawner",42,owners,half,root,error),error);const auto& source=*spawners.entity(root);need(particles.setRegistry(spawners.registry(),error),error);need(spawners.registry()->allocationCount()==2&&spawners.registry()->managerCount(0)==1,"Native ECS/manager ownership distinction");
 size_t doubles=0;std::vector<uint32_t> bounds;EffectAnimationRandom random=[&](double& value,std::string&){++doubles;value=.5;return true;};EffectBoundedRandom integers=[&](uint32_t bound,uint32_t& value,std::string&){bounds.push_back(bound);value=0;return true;};
 EffectSpawnRequest request;request.source=root;request.ownerId=42;request.animation="LaurelCinders";request.position={100,200,0};request.nativeAngle=-1.5707963705062866f;request.distance=true;
 need(particles.prepare(request,error),error);EffectParticleAdmission admission;uint64_t handle=0;need(!particles.create(request,source,owner,admission,random,integers,handle,error)&&doubles==0&&bounds.empty()&&particles.particles().empty(),"Missing native manager/time admission before RNG");
 // Explicit diagnostic owner/admission/RNG exercise recovered numerical state.
 // This is not proof of original manager admission or Android particle update.
 admission.nativeManagerAdmissionResolved=true;admission.completePermanentRegistry=true;admission.permanentVfxCap=EffectParticleWorld::CompiledPermanentVfxCap;admission.ownerTimeModifierFraction=1;
 need(particles.create(request,source,owner,admission,random,integers,handle,error),error);need(doubles==7&&bounds==std::vector<uint32_t>{5,10},"Selected Random/Slide RNG draw order/count");
 auto p=particles.particles().at(handle);need(p.name=="LaurelBurnA"&&!p.book&&std::abs(p.speed-62.5f)<.0001f&&p.slideTicks[0]==5,"Original leaf typed initialization");need(std::abs(EffectParticleWorld::drawScale(p)-.60025f)<.00001f,"Separate source scale modification and inherited owner scale");
 need(particles.phase(EffectParticlePhase::SlideUpdateInternal,.1f,integers,error),error);need(particles.phase(EffectParticlePhase::SlidePostUpdate,.1f,integers,error),error);need(particles.particles().at(handle).frame==2&&bounds.back()==1,"Native one positive slide advance and bounded PCG bound1 consumption");
 need(particles.phase(EffectParticlePhase::Gravity,.1f,integers,error),error);need(particles.phase(EffectParticlePhase::UpwardVelocity,.1f,integers,error),error);need(particles.phase(EffectParticlePhase::Acceleration,.1f,integers,error),error);need(particles.phase(EffectParticlePhase::Heading,.1f,integers,error),error);need(particles.phase(EffectParticlePhase::Translation,.1f,integers,error),error);need(particles.phase(EffectParticlePhase::Fraction,.1f,integers,error),error);p=particles.particles().at(handle);need(std::abs(p.baseZ-1.5f)<.00001f&&std::abs(p.speed-56.5f)<.00001f,"Exact source Gravity/UpwardVelocity/Acceleration equations");
 request.animation="LaurelCindersAmbient";request.distance=false;request.source=source.child;const auto& childSource=*spawners.entity(source.child);const auto before=doubles;need(particles.create(request,childSource,owner,admission,random,integers,handle,error),error);need(doubles-before==8&&bounds.back()==5&&particles.particles().at(handle).book&&particles.particles().at(handle).playSpeed==32,"Original Book random play-speed initialized after generic data");need(EffectParticleWorld::drawScale(particles.particles().at(handle))==0,"Original ambient StartScale0 before AnimFraction update");
 need(particles.phase(EffectParticlePhase::PlaySpeed,1.f,integers,error),error);need(particles.alive(handle),"Book lifetime evaluated in UpdateInternal, not PlaySpeed system");need(particles.phase(EffectParticlePhase::BookUpdateInternal,1.f,integers,error),error);need(!particles.alive(handle),"Native Book end+1 cutoff");particles.disposeDone();
 request.animation="LaurelCinders";request.source=root;request.distance=true;
 for(size_t n=0;n<80;++n)need(particles.create(request,source,owner,admission,random,integers,handle,error),error);
 need(particles.particles().size()==81&&particles.liveCount()==80&&spawners.registry()->managerCount(0)==82,"Native pending-canceled entries remain in actual shared manager vector");
 particles.disposeDone();need(spawners.registry()->managerCount(0)==81,"Actual disposal removes canceled manager entry only in caller phase");
 AnimationAllocation external;external.name="UncoveredOriginalPermanentClass";external.ownerId=42;external.ownerLifetime=owner.lifetime;external.type=NativeAnimationType::Book;external.ownership=AnimationOwnership::ManagerCreate;external.cancelOverCap=true;uint64_t externalId=0;std::optional<uint64_t> victim;need(spawners.registry()->allocate(external,externalId,error)&&spawners.registry()->commitInitialization(externalId,false,error)&&spawners.registry()->addToManager(externalId,80,victim,error),error);const auto priorDraws=doubles;
 need(!particles.permanentRegistryCovered()&&!particles.create(request,source,owner,admission,random,integers,handle,error)&&doubles==priorDraws,"Uncovered actual permanent manager class rejects before RNG");
 size_t images=0;for(size_t c=0;c<bank->clips().size();++c)for(size_t s=0;s<bank->clips()[c].slides.size();++s){const auto im=bank->sample(c,s,0);need(im.rgba.size()==im.width*im.height*4,"Original image CRC/extent");++images;}
 std::cout<<"{\"result\":\"passed\",\"original_leaf_definitions\":10,\"original_image_bindings\":"<<images<<",\"slide_uniform_draws\":7,\"book_uniform_draws\":8,\"selector_pcg_bound\":5,\"slide_max_exclusive\":true,\"bound1_consumed\":true,\"native_manager_admission_observed\":false,\"combined_schedule_observed\":false,\"android_particle_runtime_observed\":false}\n";
 }catch(const std::exception& ex){std::cerr<<ex.what()<<'\n';return 1;}}
