#include "../host/file_assets.hpp"
#include "../app/action_effect.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
int main(int argc,char**argv){std::string stage="load source banks";try{
 if(argc!=2)throw std::runtime_error("animation_frame_transfer_smoke ACTUAL_STAGED_ASSETS");
 const std::filesystem::path assetsPath=argv[1];FileAssets assets(assetsPath.string());
 auto bank=std::make_shared<ActorAnimationBank>();bank->load(assets,"action-effect-frames.bin");
 auto movieBank=std::make_shared<ActorAnimationBank>();movieBank->load(assets);
 auto*L=luaL_newstate();if(luaL_loadfile(L,(assetsPath/"action-effects.lua").c_str())||lua_pcall(L,0,1,0))throw std::runtime_error(lua_tostring(L,-1));
 SourceValue catalog;std::string error;if(!readSourceValue(L,-1,catalog,error))throw std::runtime_error(error);lua_close(L);
 auto registry=std::make_shared<AnimationRegistry>();ActionEffectWorld world;
 const auto need=[&](bool ok){if(!ok)throw std::runtime_error(error);};stage="load action world";need(world.load(std::move(catalog),bank,registry,error,movieBank));
 EffectOwnerState owner;owner.lifetime=7001;owner.x=20;owner.y=30;owner.scale=1;owner.baseColor=std::array<uint8_t,4>{255,255,255,255};owner.baseHsv=std::array<float,4>{0,0,0,0};
 uint64_t randomDraws=0;ActionEffectProviders p;p.worldFrame=100;
 p.owner=[&](int32_t id,EffectOwnerState&out,std::string&e){if(id!=-1){e="Unexpected frame-transfer owner";return false;}out=owner;return true;};
 p.random=[&](double&v,std::string&){++randomDraws;v=.5;return true;};
 p.bounded=[&](uint32_t bound,uint32_t&v,std::string&){++randomDraws;v=0;return bound>0;};
 p.elapsed=[](int32_t,float,bool,float dt,float&out,std::string&){out=dt;return true;};
 // Diagnostic audio transport: the regression observes animation state and
 // source image bindings; it does not claim decoded cue playback coverage.
 p.sound=[](const std::string&,int32_t,const std::array<float,3>&,uint32_t,uint32_t&id,std::string&){id=0;return true;};
 world.setSoundStop([](uint32_t,float,std::string&){return true;});
 const auto create=[&](const std::string&name){stage="create primary "+name;uint64_t h=0;need(world.createPrimary(name,-1,p,h,error));if(!h)throw std::runtime_error("Missing real Book allocation: "+name);return h;};
 const auto copy=[&](uint64_t target,uint64_t source,float expectedFrame){
  const auto before=*world.state(target);const uint64_t draws=randomDraws;
  stage="copy "+world.state(source)->name+" -> "+before.name;
  need(world.copyState(target,source,p,error));const auto&after=*world.state(target);
  if(after.frame!=expectedFrame||after.duration!=before.duration||after.remaining!=before.remaining||after.fraction!=before.fraction||after.ownerId!=before.ownerId||after.ownerLifetime!=before.ownerLifetime||after.detached!=before.detached||after.preferUnmodified!=before.preferUnmodified||after.customFrames!=before.customFrames||after.playSpeed!=before.playSpeed||after.frameTarget!=before.frameTarget||randomDraws!=draws)throw std::runtime_error("Book frame transfer changed clock/ownership/RNG or lost raw fractional frame");
  stage="visual "+after.name+" raw="+std::to_string(after.frame);ActorVisual visual;need(world.visual(target,owner,{255,255,255,255},visual,error));
  const auto number=[&](const char*key,float fallback){const auto*v=after.definition.get(key);return v?float(v->number()):fallback;};
  const auto display=uint32_t(std::clamp(std::trunc(expectedFrame),number("StartFrame",1),number("EndFrame",number("NumFrames",1))));
  stage="sample expected "+after.name+" display="+std::to_string(display);const auto expected=bank->sample(after.imageClip,display-1,0,false);
  // Bank images now decode lazily; the metadata packet intentionally has an
  // empty rgba span until pixels() resolves its decoder and checks the CRC.
  stage="decode "+after.name+" packet="+std::to_string(visual.frame.packetFrameIndex);const auto pixels=visual.frame.pixels();
  if(pixels.empty()||visual.frame.packetFrameIndex!=expected.packetFrameIndex)throw std::runtime_error("Transferred Book display binding mismatch: target="+after.name+" raw="+std::to_string(after.frame)+" expectedRaw="+std::to_string(expectedFrame)+" display="+std::to_string(display)+" actualPacket="+std::to_string(visual.frame.packetFrameIndex)+" expectedPacket="+std::to_string(expected.packetFrameIndex)+" decodedBytes="+std::to_string(pixels.size()));
 };
 const uint64_t source=create("EnemyProjectileIn");
 const auto*flag=world.state(source)->definition.get("TransferFrameFromThis");if(!flag||!flag->boolean()||world.state(source)->type!=NativeAnimationType::Book)throw std::runtime_error("Actual source frame-transfer policy changed");
 stage="advance initial source";need(world.advance(.025f,p,error));const float fractional=world.state(source)->frame;
 if(fractional==std::trunc(fractional))throw std::runtime_error("Diagnostic source did not retain fractional Book frame");
 const uint64_t target=create("EnemyProjectile");copy(target,source,fractional);
 // No transfer flags: CopyState must retain the destination's frame.
 const uint64_t unchanged=create("EnemyProjectile");copy(unchanged,target,world.state(unchanged)->frame);
 // Preserve a fractional source beyond EndFrame; clamp the destination display
 // rather than clamping the stored fractional component.
 need(world.setDuration(source,2,error));
 const auto beforeBoundary=*world.state(source);
 const float boundaryStep=(60.375f-beforeBoundary.frame)/(beforeBoundary.playSpeed*beforeBoundary.playSpeedMultiplier);
 if(!std::isfinite(boundaryStep)||boundaryStep<=0||boundaryStep>1)throw std::runtime_error("Actual source boundary fixture step outside supported clock range");
 stage="advance source to boundary dt="+std::to_string(boundaryStep);need(world.advance(boundaryStep,p,error));
 const float beyond=world.state(source)->frame;if(beyond<=60||beyond>=61||world.state(source)->remaining<=0||world.state(source)->done)throw std::runtime_error("Actual source boundary fixture not live between EndFrame and EndFrame+1: raw="+std::to_string(beyond));copy(target,source,beyond);
 // Destination ToThis is independently sufficient, using real 7-frame Book
 // bindings and an unflagged EnemyProjectile source; no source-name policy.
 const uint64_t toThis=create("AxeCrackDissipate");const auto*toFlag=world.state(toThis)->definition.get("TransferFrameToThis");if(!toFlag||!toFlag->boolean())throw std::runtime_error("Actual destination frame-transfer policy changed");copy(toThis,target,beyond);
 // This real Fade also requests TransferChildren. The public CopyState branch
 // must retain that separate barrier; its actual manager frame consumer is
 // exercised below without claiming child-ownership transfer coverage.
 const uint64_t constant=create("RubbleFallImpactDecalA-Fade");stage="verify separate CopyState child barrier";
 const auto constantBefore=*world.state(constant);const auto drawsBeforeBarrier=randomDraws;
 if(world.copyState(constant,target,p,error)||error.find("child ownership transfer")==std::string::npos||world.state(constant)->frame!=constantBefore.frame||world.state(constant)->remaining!=constantBefore.remaining||randomDraws!=drawsBeforeBarrier)throw std::runtime_error("Actual CopyState child-ownership barrier was lost or mutated frame/clock/RNG");error.clear();
 // AnimationManager.Update29d614 also invokes CopyState after the new Book's
 // Init. Exercise the actual manager ChainTo, separately from Thing primaries.
 stage="create manager Book source";uint64_t managerSource=0;need(world.create("EnemyProjectileIn",-1,std::nullopt,p,managerSource,error));
 const auto managerBefore=*world.state(managerSource);std::set<uint64_t> existing;for(const auto&[h,s]:world.states()){(void)s;existing.insert(h);}
 stage="advance manager Book chain";need(world.advance(.11f,p,error));if(world.state(managerSource))throw std::runtime_error("Actual manager source was not retired at Book duration boundary");
 uint64_t replacement=0;for(const auto&[h,s]:world.states())if(!existing.contains(h)&&s.name=="EnemyProjectile"&&!s.primary)replacement=h;
 const float managerFrame=managerBefore.frame+managerBefore.playSpeed*managerBefore.playSpeedMultiplier*.11f;
 if(!replacement||world.state(replacement)->frame!=managerFrame||world.state(replacement)->fraction!=0)throw std::runtime_error("Manager Book ChainTo did not transfer actual source frame independently of destination fraction");
 // Actual Constant->Constant manager chain seen in the strict replay.
 stage="create manager Constant source";uint64_t decal=0;need(world.create("RubbleFallImpactDecalA",-1,std::nullopt,p,decal,error));need(world.setDuration(decal,.01f,error));
 existing.clear();for(const auto&[h,s]:world.states()){(void)s;existing.insert(h);}
 stage="advance manager Constant chain";need(world.advance(.02f,p,error));if(world.state(decal))throw std::runtime_error("Actual Constant manager source was not retired");
 uint64_t fade=0;for(const auto&[h,s]:world.states())if(!existing.contains(h)&&s.name=="RubbleFallImpactDecalA-Fade"&&!s.primary)fade=h;
 if(!fade||world.state(fade)->type!=NativeAnimationType::Constant||world.state(fade)->frame!=1||world.state(fade)->remaining!=2||world.state(fade)->fraction!=0)throw std::runtime_error("Constant manager ChainTo failed base virtual frame transfer or reset destination clock");
 while(!world.states().empty())need(world.destroy(world.states().begin()->first,error));
 if(registry->allocationCount())throw std::runtime_error("Frame-transfer regression leaked animation lifetimes");
 std::cout<<"{\"result\":\"passed\",\"actual_EnemyProjectileIn_Book_transfer\":true,\"destination_ToThis_independent\":true,\"raw_fractional_frame_preserved\":true,\"actual_display_binding_clamped\":true,\"unflagged_copy_retains_frame\":true,\"copy_consumes_no_rng\":true,\"copy_preserves_destination_clock_owner\":true,\"manager_Book_chain_transfers_frame\":true,\"separate_TransferChildren_barrier_preserved\":true,\"actual_Constant_manager_chain\":true,\"audio_transport\":\"diagnostic only\",\"android_observed\":false}\n";
 }catch(const std::exception&e){std::cerr<<stage<<": "<<e.what()<<'\n';return 1;}}
