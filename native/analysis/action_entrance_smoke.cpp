#include "../host/file_assets.hpp"
#include "../app/action_effect.hpp"
#include <cmath>
#include <iostream>
#include <set>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
int main(int argc,char**argv){try{
 if(argc!=3)throw std::runtime_error("action_entrance_smoke ACTION_RESOURCES HERO_SOURCE_EVENTS_LUA");
 FileAssets assets(argv[1]);auto bank=std::make_shared<ActorAnimationBank>();bank->load(assets,"action-effect-frames.bin");auto*L=luaL_newstate();if(luaL_loadfile(L,(std::string(argv[1])+"/action-effects.lua").c_str())||lua_pcall(L,0,1,0))throw std::runtime_error(lua_tostring(L,-1));SourceValue catalog;std::string error;if(!readSourceValue(L,-1,catalog,error))throw std::runtime_error(error);lua_close(L);
 auto registry=std::make_shared<AnimationRegistry>();ActionEffectWorld world;auto need=[&](bool ok){if(!ok)throw std::runtime_error(error);};need(world.load(std::move(catalog),bank,registry,error));
 EffectOwnerState owner;owner.lifetime=100;owner.x=1947.373f;owner.y=5188.61f;owner.z=1500;owner.scale=.7f;owner.baseColor=std::array<uint8_t,4>{255,255,255,255};owner.baseHsv=std::array<float,4>{0,0,0,0};unsigned randomFlips=0;
 ActionEffectProviders p;p.worldFrame=100;p.owner=[&](int32_t id,EffectOwnerState&o,std::string&e){if(id!=400001){e="Unexpected actual entrance owner";return false;}o=owner;return true;};p.random=[](double&,std::string&e){e="Streak has no source uniform RNG";return false;};p.bounded=[&](uint32_t bound,uint32_t&v,std::string&e){if(bound!=2){e="Unexpected entrance bounded RNG";return false;}++randomFlips;v=1;return true;};p.elapsed=[](int32_t,float,float dt,float&r,std::string&){r=dt;return true;};
 uint64_t h=0;need(world.create("ZagreusHeroLanding_Air_Streak",400001,std::nullopt,p,h,error));if(randomFlips!=1||registry->allocationCount()!=1)throw std::runtime_error("Source streak init allocation/random flip mismatch");
 std::set<uint32_t>frames;unsigned samples=0;for(unsigned tick=0;tick<130;++tick){if(world.state(h)){ActorVisual v;need(world.visual(h,owner,{255,255,255,255},v,error));frames.insert(v.frame.packetFrameIndex);if(v.frame.rgba.empty()||std::abs(v.y-(owner.y-180*.7f))>.01f||std::abs(v.z-owner.z)>.01f||std::abs(v.scale-.7f)>1e-6f||!v.flipX||!v.addColor)throw std::runtime_error("Source streak original pixel/offset/scale/flip/height/color mismatch");++samples;}owner.z-=3;need(world.advance(1.f/60,p,error));}
 if(frames.size()!=8||registry->allocationCount()!=0)throw std::runtime_error("Source streak eight original frames/real finite Slide lifetime mismatch");
 // Consume the exact preserved HAA source init list, including repeated
 // debris names. This fixture does not replace the parent-owned full Lua room.
 auto* E=luaL_newstate();if(luaL_loadfile(E,argv[2])||lua_pcall(E,0,1,0))throw std::runtime_error(lua_tostring(E,-1));SourceValue hero;if(!readSourceValue(E,-1,hero,error))throw std::runtime_error(error);lua_close(E);
 const auto* ret=hero.get("clips")->get("ZagreusHeroLanding_ReturnToIdle");if(!ret)throw std::runtime_error("Original landing return source missing");const auto& entries=ret->get("definition")->get("CreateAnimations")->array();if(entries.size()!=8)throw std::runtime_error("Actual original landing create list differs");
 std::set<std::string> debris;unsigned choices=0,uniforms=0;owner.z=0;p.random=[&](double&v,std::string&){++uniforms;v=.5;return true;};p.bounded=[&](uint32_t bound,uint32_t&v,std::string&){if(bound==3)v=choices++%3;else if(bound==2)v=0;else return false;return true;};
 for(const auto& entry:entries){uint64_t created=0;need(world.create(entry.get("Name")->text(),400001,std::nullopt,p,created,error));}
 unsigned landingImages=0;for(unsigned tick=0;tick<240;++tick){for(auto actual:world.handles()){const auto*state=world.state(actual);if(state->name.starts_with("RubbleFallImpactDebris"))debris.insert(state->name);ActorVisual v;need(world.visual(actual,owner,{255,255,255,255},v,error));if(v.frame.rgba.empty())throw std::runtime_error("Original landing closure image missing");++landingImages;}need(world.advance(1.f/60,p,error));}
 if(debris.size()!=3||world.states().size()!=1||world.states().begin()->second.name!="RubbleFallImpactDecalA")throw std::runtime_error("Original landing random branches/persistent decal mismatch");need(world.destroy(world.states().begin()->first,error));if(registry->allocationCount())throw std::runtime_error("Original landing closure shutdown leaked allocation");
 std::cout<<"{\"landing_source_create_entries\":8,\"landing_original_image_samples\":"<<landingImages<<",\"landing_debris_variants\":3,\"persistent_original_decal\":true,\"result\":\"passed\",\"original_entrance_streak_images\":8,\"actual_slide_samples\":"<<samples<<",\"source_random_flip_draws\":"<<randomFlips<<",\"source_owner_height_followed\":true,\"native_body_scaled_offset\":true,\"animation_scale_not_double_applied_to_offset\":true,\"final_live_allocations\":0,\"rng_fixture\":\"bounded diagnostic flip\",\"android_observed\":false}\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
