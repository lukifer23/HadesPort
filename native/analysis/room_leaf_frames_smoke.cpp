#include "../host/file_assets.hpp"
#include "../app/actor_animation.hpp"
#include <iostream>
int main(int argc,char** argv){try{
 if(argc!=2)throw std::runtime_error("room_leaf_frames_smoke BANK");hades::FileAssets assets(argv[1]);hades::ActorAnimationBank bank;
 bank.load(assets,"room-leaf-frames.bin","room-leaf-facing.bin","room-leaf-sha256.bin");size_t frames=0;uint64_t raw=0;
 for(size_t c=0;c<bank.clips().size();++c)for(size_t f=0;f<bank.clips()[c].slides.size();++f)for(uint32_t a=0;a<bank.clips()[c].originalAngleIndices.size();++a){auto image=bank.sample(c,f,a,false);if(image.originalAngle!=a||image.rgba.empty())throw std::runtime_error("Original source angle binding invalid");raw+=image.rgba.size();++frames;}
 const auto bat=bank.find("EnvironmentBat").value();if(bank.clips()[bat].originalAngles!=32||bank.clips()[bat].originalAngleIndices.size()!=32)throw std::runtime_error("Missing original Bat direction");
 for(int i=0;i<720;++i)if(bank.selectedAngleForFacing(bat,float(i)*.00872664626f)!=bank.originalAngleForFacing(bat,float(i)*.00872664626f))throw std::runtime_error("Full original Bat source selection differs");
 std::cout<<"{\"result\":\"passed\",\"clips\":"<<bank.clips().size()<<",\"crc_checked_frame_bindings\":"<<frames<<",\"rgba_bytes\":"<<raw<<",\"bat_original_directions\":32,\"native_particle_execution_observed\":false}\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
