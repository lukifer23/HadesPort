#include "../host/file_assets.hpp"
#include "../app/actor_animation.hpp"
#include "../app/source_value.hpp"
#include <iostream>
#include <set>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
int main(int argc,char**argv){try{
 if(argc!=2)throw std::runtime_error("action_effect_resource_smoke ORIGINAL_ACTION_BANK");hades::FileAssets files(argv[1]);hades::ActorAnimationBank bank;bank.load(files,"action-effect-frames.bin");auto*L=luaL_newstate();if(luaL_loadfile(L,(std::string(argv[1])+"/action-effects.lua").c_str())||lua_pcall(L,0,1,0))throw std::runtime_error("Source action catalog");hades::SourceValue catalog;std::string error;if(!hades::readSourceValue(L,-1,catalog,error))throw std::runtime_error(error);lua_close(L);const auto& definitions=*catalog.get("Definitions");const auto* swipe=definitions.get("SwordSwipeAFlipped");for(const auto*name:{"SwordSwipeAFlippedDark","SwordSwipeAFlippedDisplace"}){const auto*d=definitions.get(name);if(!d->get("CreateAnimations")->array().empty())throw std::runtime_error("Native source ClearCreateAnimations must remove inherited creates");}if(swipe->get("CreateAnimations")->array().size()!=2||swipe->get("Slides")->array().size()!=18)throw std::runtime_error("Source first-sword replacement slides/create graph");std::set<uint32_t>visited;size_t bindings=0;
 for(size_t c=0;c<bank.clips().size();++c){const auto& clip=bank.clips()[c];for(size_t s=0;s<clip.slides.size();++s){auto image=bank.sample(c,s,0,false);if(image.rgba.empty())throw std::runtime_error("Missing source action pixel");visited.insert(image.packetFrameIndex);++bindings;}}
 if(bank.clips().size()!=11||visited.size()!=60)throw std::runtime_error("Original first-action resource closure");std::cout<<"{\"result\":\"passed\",\"source_definitions\":11,\"unique_original_images_crc_checked\":"<<visited.size()<<",\"positive_source_image_bindings\":"<<bindings<<",\"native_clear_create_normalization_verified\":true,\"effect_execution_observed\":false,\"android_observed\":false}\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
