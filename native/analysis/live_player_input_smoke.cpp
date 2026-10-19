#include "../host/file_assets.hpp"
#include "../app/lua_runtime.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <filesystem>
int main(int argc,char**argv){if(argc!=3){std::cerr<<"live_player_input_smoke ASSETS NEW_REPORT\n";return 2;}try{if(std::filesystem::exists(argv[2]))throw std::runtime_error("Fresh report required");hades::FileAssets assets(argv[1]);ScriptRuntime scripts(&assets);scripts.initialize();bool boot=scripts.bootRoom(),activation=boot&&scripts.activateRoom();unsigned tick=0,inputFrames=0;double moved=0;std::string error;std::optional<ObjectSnapshot>last=scripts.localPlayerBody();bool complete=activation;for(;complete&&tick<900;++tick){std::vector<std::string>controls;float x=0,y=0;if(tick>=360&&scripts.inputAllowed()){++inputFrames;if(inputFrames<120)x=1;else if(inputFrames<240)y=-1;else if(inputFrames==260)controls={"Attack2"};}if(!scripts.setLocalInput(x,y,controls,error)){complete=false;break;}if(!scripts.advanceWorld(1.f/60,1./60)){error=scripts.error();complete=false;break;}auto now=scripts.localPlayerBody();if(now&&last)moved+=std::hypot(now->x-last->x,now->y-last->y);last=now;}
 std::ofstream out(argv[2]);out<<"{\"source_booted\":"<<(boot?"true":"false")<<",\"source_room_activated\":"<<(activation?"true":"false")<<",\"world_frames\":"<<tick<<",\"allowed_input_frames\":"<<inputFrames<<",\"actual_body_path_length\":"<<moved<<",\"completed_schedule\":"<<(complete?"true":"false")<<",\"source_error\":\"";for(char c:error){if(c=='"'||c=='\\')out<<'\\';if(c=='\n')out<<"\\n";else out<<c;}out<<"\",\"android_observed\":false}\n";std::ofstream(std::string(argv[2])+".actors.txt")<<scripts.actorsDump();std::ofstream(std::string(argv[2])+".input.txt")<<scripts.inputDump();std::cout<<"LIVE_INPUT complete="<<complete<<" tick="<<tick<<" allowedFrames="<<inputFrames<<" moved="<<moved<<" error="<<error<<'\n';return complete&&inputFrames>0&&moved>0?0:1;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
