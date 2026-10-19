#include "../host/file_assets.hpp"
#include "../app/screen_animation.hpp"
#include <cmath>
#include <iostream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
int main(int argc,char**argv){try{
 if(argc!=2)throw std::runtime_error("screen_use_prompt_smoke ORIGINAL_SCREEN_EXPORT");
 FileAssets files(argv[1]);auto bank=std::make_shared<ActorAnimationBank>();bank->load(files,"actor-animations.bin");
 auto*L=luaL_newstate();if(luaL_loadfile(L,(std::string(argv[1])+"/screen-animations.lua").c_str())||lua_pcall(L,0,1,0))throw std::runtime_error("Original screen catalog");
 SourceValue catalog;std::string error;if(!readSourceValue(L,-1,catalog,error))throw std::runtime_error(error);lua_close(L);
 auto registry=std::make_shared<AnimationRegistry>();ScreenAnimationWorld world;
 auto need=[&](bool ok){if(!ok)throw std::runtime_error(error);};
 need(world.load(catalog,bank,registry,error));
 std::array<float,2> dimensions{-1,-1};world.setTextDimensionsProvider([&](int32_t id,std::array<float,2>&out,std::string&){if(id!=700001)throw std::runtime_error("Wrong attached text owner");out=dimensions;return true;});
 ObjectSnapshot body;body.id=700001;body.entityHandle=700001;body.screenSpace=true;body.name="BlankObstacle";body.animation="Blank";body.alive=body.active=true;body.x=960;body.y=540;
 ScreenAnimationSound sound;sound.worldFrame=100;need(world.create(body,{1,1,1,1},sound,error));
 need(world.setAnimation(body.id,"InteractBacking",sound,error));ActorVisual full,half,empty;need(world.visual(body.id,full,error));
 if(full.color[0]!=0||full.color[1]!=0||full.color[2]!=0||full.color[3]!=1||full.y!=460)throw std::runtime_error("Prompt initial color/location");
 dimensions={560,52.5f};need(world.visual(body.id,half,error));
 for(size_t k=0;k<4;++k)if(std::abs(half.localRect[k]-.5f*full.localRect[k])>.001f)throw std::runtime_error("Attached text width/height scalar");
 dimensions={0,0};need(world.visual(body.id,empty,error));for(float v:empty.localRect)if(v!=0)throw std::runtime_error("Empty text component must collapse backing");
 dimensions={560,52.5f};for(int frame=0;frame<30;++frame)need(world.advance(1.f/60,sound,error));need(world.visual(body.id,half,error));
 if(body.animation!="InteractBacking"||std::abs(half.color[3]-.8f)>.004f||registry->allocationCount()!=1)throw std::runtime_error("Prompt HoldLastFrame/color duration");
 need(world.setAnimation(body.id,"InteractBackingFade",sound,error));need(world.visual(body.id,half,error));if(std::abs(half.color[3]-.8f)>.004f)throw std::runtime_error("Prompt fade initial alpha");
 for(int frame=0;frame<30;++frame)need(world.advance(1.f/60,sound,error));need(world.visual(body.id,half,error));if(half.color[3]!=0||registry->allocationCount()!=1)throw std::runtime_error("Prompt fade end/lifetime");
 need(world.markDead(body.id,error));need(world.advance(0,sound,error));if(body.alive||registry->allocationCount())throw std::runtime_error("Prompt disposal");
 std::cout<<"{\"result\":\"passed\",\"original_prompt_pixels\":true,\"constant_alpha_transition\":true,\"hold_last_frame\":true,\"attached_text_scalars\":true,\"deferred_disposal\":true,\"device_observed\":false}\n";
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
