#include "file_assets.hpp"
#include "../app/lua_runtime.hpp"
#include "../app/player.hpp"
#include "../app/scene_renderer.hpp"
#include "world_capture.hpp"
#include "save_probe.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <limits>
#include <chrono>
#include <algorithm>

static void jsonString(std::ostream& out,const std::string& text) {
    out<<'"';constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:text){
        if(c=='"'||c=='\\')out<<'\\'<<c;
        else if(c<32)out<<"\\u00"<<hex[c>>4]<<hex[c&15];
        else out<<c;
    }
    out<<'"';
}

int main(int argc,char** argv) {
    bool capture=false,activate=false,probe=false,weaponProbe=false,survey=false,contain=false;int worldFrames=120;std::string inputReplay,saveProbeDirectory;
    for(int i=3;i<argc;++i){const std::string arg=argv[i];
        if(arg=="--capture-active-room")capture=activate=true;
        else if(arg=="--activate-room")activate=true;
        else if(arg=="--survey-blockers")survey=true; // diagnostic: list all blockers in one run
        else if(arg=="--contain-faults")contain=survey=true; // device durability mode: skip a failing stage, keep the frame running
        else if(arg=="--probe-load-callbacks")probe=true;
        else if(arg=="--probe-staged-weapons")weaponProbe=true;
        else if(arg.starts_with("--probe-save="))saveProbeDirectory=arg.substr(13);
        else if(arg.starts_with("--world-frames=")){try{worldFrames=std::stoi(arg.substr(15));}catch(...){worldFrames=0;}}
        else if(arg.starts_with("--input-replay="))inputReplay=arg.substr(15);
        else{std::cerr<<"Unknown debug argument: "<<arg<<'\n';return 64;}}
    if(argc<3||worldFrames<1||worldFrames>36000){std::cerr<<"Usage: hades-debug ASSET_ROOT NEW_TRACE.json [--capture-active-room|--activate-room] [--world-frames=1..36000] [--input-replay=TSV]\n";return 64;}
    try {
        if(std::filesystem::exists(argv[2]))throw std::runtime_error("Trace already exists; choose a new output");
        struct ReplayInput {int frame;float x,y;std::vector<std::string> controls;};std::vector<ReplayInput> replay;
        if(!inputReplay.empty()){
            if(!activate)throw std::runtime_error("Input replay requires actual room activation");
            std::ifstream source(inputReplay);if(!source)throw std::runtime_error("Cannot open actual input recording");std::string line;
            while(std::getline(source,line)){if(line.empty()||line.front()=='#')continue;std::istringstream row(line);ReplayInput input{};std::string controls;
                if(!(row>>input.frame>>input.x>>input.y>>controls)||input.frame<0||input.frame>=worldFrames||(!replay.empty()&&input.frame<=replay.back().frame))throw std::runtime_error("Invalid ordered debug input recording");
                if(controls!="-"){std::istringstream names(controls);std::string name;while(std::getline(names,name,','))input.controls.push_back(name);}replay.push_back(std::move(input));if(replay.size()>36000)throw std::runtime_error("Input recording bound");}
        }
        hades::FileAssets assets(argv[1]);
        ScriptRuntime scripts(&assets);scripts.initialize();
        if(survey){std::string e;if(!scripts.enableBlockerSurvey(e))throw std::runtime_error(e);}
        if(contain)scripts.setFaultContainment(true);
        std::string cameraError;if(!scripts.setCameraViewport(1728,1080,cameraError))throw std::runtime_error(cameraError);
        const bool roomBooted=scripts.bootRoom();
        const bool activationComplete=activate&&roomBooted?scripts.activateRoom():false;
        const bool probeComplete=probe&&roomBooted?scripts.probeLoadCallbacks():false;
        const bool weaponProbeComplete=weaponProbe&&roomBooted?scripts.probeStagedWeaponInitialization():false;
        std::cout<<scripts.sceneGroupsDump();
        std::cout<<scripts.stateSummary()<<'\n';
        // Keep the complete entity snapshot beside the focused movement trace.
        std::ofstream objectDump(std::string(argv[2])+".objects.txt");objectDump<<scripts.objectsDump();
        std::ofstream textDump(std::string(argv[2])+".text.txt");textDump<<scripts.textFormatsDump();
        std::ofstream inputDump(std::string(argv[2])+".input.txt");inputDump<<scripts.inputDump();
        std::ofstream creationDump(std::string(argv[2])+".creation.txt");creationDump<<scripts.mapCreationDump();
        std::ofstream weaponDump(std::string(argv[2])+".weapons.txt");weaponDump<<scripts.stagedWeaponsDump();
        for(size_t i=0;scripts.assetsPending()&&i<128;++i)if(!scripts.serviceAssets())break;
        bool updateComplete=activationComplete;
        std::ostringstream worldTrace;size_t replayAt=0;int worldFramesCompleted=0;uint64_t pcmSamples=0;double pcmSquared=0;float pcmPeak=0;bool firstWorldSample=true;std::string mixerError;
        using Clock=std::chrono::steady_clock;std::vector<double> frameMilliseconds;double updateMilliseconds=0,mixMilliseconds=0;
        if(activationComplete)for(int i=0;i<worldFrames;++i){
            const auto frameStart=Clock::now();
            if(replayAt<replay.size()&&replay[replayAt].frame==i){const auto& input=replay[replayAt++];if(!scripts.setLocalInput(input.x,input.y,input.controls,mixerError)){updateComplete=false;break;}}
            if(!scripts.advanceWorld(1.f/60,1./60)){if(survey){scripts.surveyRecordWorldFault();continue;}updateComplete=false;break;}
            const auto mixStart=Clock::now();updateMilliseconds+=std::chrono::duration<double,std::milli>(mixStart-frameStart).count();
            std::array<float,1600> pcm{};if(!scripts.renderOfflineAudio(pcm.data(),800,mixerError)){updateComplete=false;break;}
            const auto mixEnd=Clock::now();mixMilliseconds+=std::chrono::duration<double,std::milli>(mixEnd-mixStart).count();frameMilliseconds.push_back(std::chrono::duration<double,std::milli>(mixEnd-frameStart).count());
            for(const float value:pcm){if(!std::isfinite(value))throw std::runtime_error("Actual source mixer produced nonfinite PCM");pcmPeak=std::max(pcmPeak,std::abs(value));pcmSquared+=double(value)*value;}pcmSamples+=pcm.size();++worldFramesCompleted;
            if(i%12==0)if(const auto player=scripts.localPlayerBody()){
                if(!firstWorldSample)worldTrace<<',';firstWorldSample=false;worldTrace<<"{\"tick\":"<<i<<",\"x\":"<<player->x<<",\"y\":"<<player->y<<",\"z\":"<<player->z<<",\"frame\":"<<player->frame<<",\"input_allowed\":"<<(scripts.inputAllowed()?"true":"false")<<",\"animation\":";jsonString(worldTrace,player->animation);worldTrace<<'}';}
            if(i%600==0)std::cout<<"LIVE_WORLD_REPLAY frame="<<i<<" source_pcm_samples="<<pcmSamples<<" input_allowed="<<scripts.inputAllowed()<<'\n'<<std::flush;
        }
        // Save/load diagnostic: direct SaveCheckpoint, fresh-runtime reload, state comparison.
        const bool saveProbeComplete=!saveProbeDirectory.empty()&&roomBooted?runSaveProbe(assets,scripts,saveProbeDirectory,std::string(argv[2])+".save.json"):false;
        size_t presentationSamples=0;std::string presentationError;
        if(activationComplete){
            auto scene=hades::loadGraphicsScene(assets,[](uint32_t,uint32_t,uint32_t,std::span<const uint8_t>){});
            for(auto sprite:scene.sprites){std::optional<hades::SceneFrameSelection> frame;if(!scripts.scenePrimaryFrame(sprite.sourceIndex,frame,presentationError)||!scripts.sourceSceneSprite(sprite.sourceIndex,sprite,presentationError))break;++presentationSamples;}
            if(presentationError.empty())for(const auto& key:scripts.presentationVisuals()){hades::ActorVisual visual;if(!scripts.samplePresentation(key,visual,presentationError))break;const auto px=visual.frame.pixels();if(px.empty()){presentationError="Actual presentation frame pixels unavailable";break;}volatile uint8_t first=px.front(),last=px.back();(void)first;(void)last;++presentationSamples;}
            std::cout<<"LIVE_PRESENTATION samples="<<presentationSamples<<" error="<<presentationError<<'\n';
            if(capture&&presentationError.empty()){
                captureLiveWorld(scripts,assets,std::string(argv[2])+".ppm");
                captureLiveWorld(scripts,assets,std::string(argv[2])+".world-only.ppm",true);
            }
        }
        std::ofstream actorDump(std::string(argv[2])+".actors.txt");actorDump<<scripts.actorsDump();
        std::ofstream worldObjectDump(std::string(argv[2])+".world-objects.txt");worldObjectDump<<scripts.objectsDump();
        Player player;if(!activate)player.load(&assets);
        // An explicit debugging input recording, not an imitation of game logic.
        // Drive the actual shared native movement/collision/animation component.
        constexpr std::array<Point,9> input{{{0,0},{1,0},{0,-1},{1,-1},{0,1},{-1,0},{-1,1},{0,0},{1,0}}};
        std::ofstream trace(argv[2]);if(!trace)throw std::runtime_error("Cannot create trace");
        trace<<"{\"lua_main_loaded\":"<<(scripts.mainLoaded()?"true":"false")
             <<",\"game_scripts_loaded\":"<<(scripts.gameLoaded()?"true":"false")
             <<",\"room_pre_creation_complete\":"<<(roomBooted?"true":"false")
             <<",\"diagnostic_load_callback_probe\":"<<(probe?"true":"false")
             <<",\"diagnostic_load_callbacks_complete\":"<<(probeComplete?"true":"false")
             <<",\"diagnostic_staged_weapon_probe\":"<<(weaponProbe?"true":"false")
             <<",\"diagnostic_staged_weapon_complete\":"<<(weaponProbeComplete?"true":"false")<<",\"native_map_begin_complete\":"<<(activationComplete?"true":"false")
             <<",\"native_world_phase_coverage_complete\":false,\"native_world_updates_complete\":false,\"native_supported_world_replay_complete\":"<<(updateComplete?"true":"false")<<",\"initialization_error\":";
        jsonString(trace,scripts.error());trace<<",\"world_frames_completed\":"<<worldFramesCompleted<<",\"source_pcm_samples\":"<<pcmSamples<<",\"source_pcm_rms\":"<<(pcmSamples?std::sqrt(pcmSquared/pcmSamples):0)<<",\"source_pcm_peak\":"<<pcmPeak<<",\"mixer_or_input_error\":";jsonString(trace,mixerError);
        const auto& nativeCamera=scripts.cameraState();trace<<",\"source_camera\":{\"initialized\":"<<(nativeCamera.initialized?"true":"false")<<",\"x\":"<<nativeCamera.x<<",\"y\":"<<nativeCamera.y<<",\"zoom\":"<<nativeCamera.zoom<<",\"goal_zoom\":"<<nativeCamera.goalZoom<<",\"viewport_width\":"<<nativeCamera.viewportWidth<<",\"viewport_height\":"<<nativeCamera.viewportHeight<<",\"target_count\":"<<nativeCamera.targets.size()<<",\"clamp_count\":"<<nativeCamera.clampTargets.size()<<'}';
        std::sort(frameMilliseconds.begin(),frameMilliseconds.end());const auto percentile=[&](double fraction){return frameMilliseconds.empty()?0:frameMilliseconds[size_t(std::ceil(fraction*frameMilliseconds.size()))-1];};
        trace<<",\"host_cpu_timing\":{\"scope\":\"Actual successful world updates and source PCM mixer; excludes rendering, asset setup and failed frame\",\"update_mean_ms\":"<<(worldFramesCompleted?updateMilliseconds/worldFramesCompleted:0)<<",\"mixer_mean_ms\":"<<(worldFramesCompleted?mixMilliseconds/worldFramesCompleted:0)<<",\"combined_p50_ms\":"<<percentile(.5)<<",\"combined_p95_ms\":"<<percentile(.95)<<",\"combined_p99_ms\":"<<percentile(.99)<<",\"combined_max_ms\":"<<percentile(1)<<'}';
        trace<<",\"world_samples\":["<<worldTrace.str()<<"],\"presentation_samples\":"<<presentationSamples<<",\"presentation_error\":";jsonString(trace,presentationError);trace<<",\"lua_state\":";jsonString(trace,scripts.stateSummary());trace<<",\"samples\":[";
        bool first=true;
        for(size_t segment=0;!activate&&segment<input.size();++segment){
            Point direction=input[segment];const float length=std::hypot(direction.x,direction.y);
            if(length>1){direction.x/=length;direction.y/=length;}
            for(int frame=0;frame<120;++frame){
                const bool allowed=scripts.inputAllowed();player.setInput(allowed?direction.x:0,allowed?direction.y:0);
                player.update(1.f/120);
                // Touch the actual selected RGBA frame to expose bounds errors under ASAN.
                const auto& clip=player.clip();const auto* pixels=player.pixels();
                volatile uint8_t firstPixel=pixels[0],lastPixel=pixels[size_t(clip.width)*clip.height*4-1];
                (void)firstPixel;(void)lastPixel;
                if(frame%12==0){if(!first)trace<<',';first=false;
                    trace<<"{\"tick\":"<<(segment*120+frame)<<",\"x\":"<<player.position.x<<",\"y\":"<<player.position.y
                         <<",\"frame\":"<<player.frame()<<",\"input_x\":"<<direction.x<<",\"input_y\":"<<direction.y<<'}';}
            }
            std::cout<<"MOVEMENT_SEGMENT "<<segment<<" position="<<player.position.x<<','<<player.position.y
                     <<" frame="<<player.frame()<<'\n';
        }
        trace<<"]}\n";
        if(survey)std::cout<<scripts.blockerSurveyReport();
        std::cout<<"HOST_DEBUG_COMPLETE main="<<scripts.mainLoaded()<<" game_scripts="<<scripts.gameLoaded()<<'\n';
        return roomBooted&&(!activate||(activationComplete&&updateComplete))&&(!probe||probeComplete)&&(!weaponProbe||weaponProbeComplete)&&(saveProbeDirectory.empty()||saveProbeComplete)?0:2;
    }catch(const std::exception& error){std::cerr<<"HOST_DEBUG_FAILURE "<<error.what()<<'\n';return 1;}
}
