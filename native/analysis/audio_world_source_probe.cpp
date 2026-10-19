#include "audio_world.hpp"
#include "speech_world.hpp"
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <set>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
using namespace hades;
struct File:AssetFile {
 std::ifstream stream;size_t length=0,pos=0;explicit File(const std::filesystem::path& path):stream(path,std::ios::binary){if(stream)length=std::filesystem::file_size(path);}
 size_t size()const override{return length;}size_t remaining()const override{return length-pos;}size_t read(void* out,size_t n)override{stream.read(static_cast<char*>(out),std::streamsize(n));const size_t got=size_t(stream.gcount());pos+=got;return got;}bool seek(size_t n)override{if(n>length)return false;stream.clear();stream.seekg(std::streamoff(n));if(!stream)return false;pos=n;return true;}
};
struct Store:AssetStore {std::vector<std::filesystem::path> roots;std::unique_ptr<AssetFile> open(std::string_view name)override{for(const auto& root:roots){auto f=std::make_unique<File>(root/std::string(name));if(f->stream)return f;}return {};}};
int main(int argc,char** argv){
 if(argc<4){std::cerr<<"catalog.lua compressedAssetRoot batAssetRoot [cue]\n";return 2;}
 lua_State* L=luaL_newstate();assert(L);if(luaL_loadfile(L,argv[1])||lua_pcall(L,0,1,0)){std::cerr<<lua_tostring(L,-1)<<'\n';return 1;}SourceValue catalog;std::string error;assert(readSourceValue(L,-1,catalog,error));lua_close(L);
 Store store;store.roots={argv[2],argv[3]};for(int i=5;i<argc;++i)store.roots.push_back(argv[i]);
 if(argc>4&&std::string(argv[4])=="@mixing"){
  AudioWorld a(&store),b(&store);assert(a.loadCatalog(catalog,error)&&b.loadCatalog(catalog,error));
  if(!a.initializeMixingControl(error)||!b.initializeMixingControl(error)){std::cerr<<error<<'\n';return 1;}
  assert(a.mixingControlId()==1&&b.mixingControlId()==1);AudioChannelState control;
  assert(a.channelState(1,control)&&control.started&&control.name=="/Control/MixingControl");
  auto before=a.mixingTargets();assert(before.size()==5);assert(a.setMixingParameter("Reverb",2,error));auto after=a.mixingTargets();size_t changed=0;
  for(size_t i=0;i<before.size();++i){if(before[i].value!=after[i].value)++changed;assert(std::isfinite(after[i].value));}
  assert(changed==5);assert(a.setMixingParameter("NoSuchOriginalMixingParameter",1,error));
  uint32_t ai=0,bi=0;const std::string cue="/SFX/Player Sounds/ZagreusWhooshDropIn";
  assert(a.createCue(cue,{},ai,error)&&b.createCue(cue,{},bi,error)&&ai==2&&bi==2&&a.play(ai,error)&&b.play(bi,error));
  std::vector<float> x(512),y(512);double difference=0,energyA=0,energyB=0;
  for(size_t at=0;at<288000;at+=256){assert(a.render(x.data(),256,error)&&b.render(y.data(),256,error));for(size_t i=0;i<x.size();++i){difference+=std::fabs(double(x[i])-y[i]);energyA+=double(x[i])*x[i];energyB+=double(y[i])*y[i];}}
  assert(energyA>0&&energyB>0&&difference>0);assert(a.advanceControls(0,error)&&!a.channelState(ai,control));
  for(size_t at=0;at<48000*24;at+=256)assert(a.render(x.data(),256,error));
  assert(a.advanceControls(0,error)&&a.channelState(1,control)&&!control.finished&&a.allocatedChannelCount()==1);
  std::cout<<"SOURCE_MIXING_CONTROL actualChannelId=1 firstScriptCueId=2 sourceLoop956332=true sourceTargetBindings=5 changedTargets="<<changed<<" sourceReverbPCMChanged="<<difference<<" energyReverb2="<<energyA<<" energyReverb0="<<energyB<<" actualControlSurvives30seconds=true originalCompressedSource=true noHardwarePlayback=true\n";return 0;
 }
 if(argc>4&&std::string(argv[4])=="@speech"){
  AudioWorld world(&store);assert(world.loadCatalog(catalog,error));SpeechWorld speech(world);uint32_t plays=0;speech.setPlayObserver([&]{++plays;});
  SpeechRequest r;r.cue="/VO/Scratch_0002_A";uint32_t id=0,rejected=99;assert(speech.request(r,id,error)&&id==1);assert(speech.request(r,rejected,error)&&rejected==0);
  r.queue=SpeechQueueMode::Always;assert(speech.request(r,rejected,error)&&rejected==0);std::vector<float> block(512);double energy=0;
  assert(speech.pause(-1,true,error));AudioChannelState paused;assert(world.channelState(id,paused)&&paused.paused);assert(world.render(block.data(),256,error));for(float v:block)assert(v==0);assert(speech.resume(-1,true,error));assert(world.channelState(id,paused)&&!paused.paused);
  for(int pass=0;pass<2;++pass){
   for(size_t at=0;at<438021;){const auto n=uint32_t(std::min<size_t>(256,438021-at));assert(world.render(block.data(),n,error));for(size_t k=0;k<n*2;++k)energy+=double(block[k])*block[k];at+=n;}
   AudioChannelState live;assert(world.channelState(uint32_t(pass+1),live)&&!live.finished);assert(world.render(block.data(),256,error));assert(world.advanceControls(0,error));auto events=world.drainCompletions();assert(events.size()==1&&events.front().channelId==uint32_t(pass+1)&&events.front().voice);SpeechRequest immediate;r.queue=SpeechQueueMode::NeverQueue;immediate.cue=r.cue;assert(speech.request(immediate,rejected,error)&&rejected==0);assert(speech.complete(events.front(),error));
  }
  assert(plays==2&&energy>0);auto presentation=speech.drainPresentation();assert(presentation.size()==4&&presentation[0].begin&&!presentation[1].begin&&presentation[2].begin&&!presentation[3].begin);
  r.queue=SpeechQueueMode::NeverQueue;assert(speech.request(r,rejected,error)&&rejected==0);assert(speech.advance(.249f,error));assert(speech.request(r,rejected,error)&&rejected==0);assert(speech.advance(.0011f,error));assert(speech.request(r,rejected,error)&&rejected==3);assert(speech.stop(-1,.1f,error));assert(world.advanceControls(.05f,error));AudioChannelState fading;assert(world.channelState(3,fading)&&std::fabs(fading.volume-.5f)<.00001f);assert(world.advanceControls(.05f,error));auto stopped=world.drainCompletions();assert(stopped.size()==1&&stopped[0].voice&&stopped[0].channelId==3);assert(speech.complete(stopped[0],error));
  std::cout<<"SPEECH_SOURCE cue="<<r.cue<<" originalFrames=402432 sourceRate=44100 actualQueuedPlays=2 exactEndBoundary=true busyRejected=true globalBuffer=.25 presentationEvents=4 pauseResume=true stopFade=true nativeNotifyBeforeSpeechCallback=true noHardwarePlayback=true\n";return 0;
 }
 if(argc>4&&(std::string(argv[4])=="/SFX/CeramicPotSmash"||std::string(argv[4])=="/SFX/Player Sounds/BurningFootsteps"||std::string(argv[4])=="/SFX/Player Sounds/FootstepsHardSurface"||std::string(argv[4])=="/SFX/Player Sounds/FootstepsHardSurfaceRun")){
  AudioWorld world(&store);assert(world.loadCatalog(catalog,error));std::vector<uint32_t> ids;
  const auto choices=catalog.get("events")->get(argv[4])->get("playlists")->array().front().get("choices")->array().size();
  for(size_t n=0;n<choices;++n){uint32_t id=0;assert(world.createCue(argv[4],{},id,error));AudioChannelState before;assert(world.channelState(id,before)&&before.selectedWaveforms.empty());ids.push_back(id);}
  std::set<std::string> waves;for(const auto id:ids){if(!world.play(id,error)){std::cerr<<error<<"\n";return 1;}AudioChannelState after;assert(world.channelState(id,after)&&after.selectedWaveforms.size()==1);waves.insert(after.selectedWaveforms.front());}
  assert(waves.size()==choices);std::cout<<"SOURCE_PLAYLIST actualUnique="<<choices<<" selectionAtPlay=true independentGameRng=true\n";
 }
 if(argc>4&&std::string(argv[4])=="@mixed"){
  AudioWorld world(&store);assert(world.loadCatalog(catalog,error));if(!world.initializeMixingControl(error)){std::cerr<<error<<'\n';return 1;}std::vector<uint32_t> ids;
  for(const std::string cue:{"/Music/MusicExploration4_MC","/Leftovers/Object Ambiences/EvilLairAmbienceMatchSiteE","/Leftovers/World Sounds/MapZoomInShortHigh","/SFX/Player Sounds/ZagreusWhooshDropIn","/SFX/BatGroupLoop"}){uint32_t id=0;assert(world.createCue(cue,{},id,error));assert(world.setParameter(id,"Keys",1,0,error));assert(world.setParameter(id,"Drums",1,0,error));assert(world.setParameter(id,"Section",1,0,error));assert(world.play(id,error));ids.push_back(id);}
  const auto began=std::chrono::steady_clock::now();std::vector<float> block(512);double energy=0,maximum=0;for(size_t at=0;at<48000*60;at+=256){if(!world.render(block.data(),256,error)){std::cerr<<error<<'\n';return 1;}for(float x:block){assert(std::isfinite(x));energy+=double(x)*x;maximum=std::max(maximum,std::fabs(double(x)));}if((at%48000)<256)assert(world.advanceControls(1,error));}
  assert(energy>0);assert(world.liveEventCount("/SFX/BatGroupLoop")==1);assert(world.liveEventCount("/Leftovers/Object Ambiences/EvilLairAmbienceMatchSiteE")==1);assert(world.liveEventCount("/SFX/Player Sounds/ZagreusWhooshDropIn")==0);assert(world.liveEventCount("/Leftovers/World Sounds/MapZoomInShortHigh")==0);
  std::cout<<"AUDIO_WORLD_MIXED frames=2880000 seconds=60 rms="<<std::sqrt(energy/(48000*60*2))<<" peak="<<maximum<<" hostSeconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<<' '<<world.dump()<<'\n';return 0;
 }
 {
  // Rebuilt event mixer must be independent of producer buffer partitioning.
  AudioWorld a(&store),b(&store);assert(a.loadCatalog(catalog,error)&&b.loadCatalog(catalog,error));uint32_t ai=0,bi=0;assert(a.createCue("/Music/MusicExploration4_MC",{},ai,error)&&b.createCue("/Music/MusicExploration4_MC",{},bi,error));assert(a.play(ai,error)&&b.play(bi,error));std::vector<float> x(4096),y(4096);assert(a.render(x.data(),2048,error));size_t at=0;for(uint32_t count:{1u,127u,513u,1407u}){assert(b.render(y.data()+at*2,count,error));at+=count;}assert(at==2048&&x==y);assert(a.channelIds("/Music/MusicExploration4_MC")==std::vector<uint32_t>{ai});AudioChannelState state;assert(a.channelState(ai,state)&&state.volume==1&&!state.finished);assert(a.stop(ai,true,error)&&a.advanceControls(0,error));assert(!a.channelState(ai,state));auto ordinary=a.drainCompletions();assert(ordinary.size()==1&&!ordinary[0].voice);uint32_t next=0;assert(a.createCue("/Music/MusicExploration4_MC",{},next,error)&&next==ai+1);
  if(catalog.get("event_namespace")){
   bool valid=false;assert(a.resolveCue("Music/MusicExploration4_MC",valid,error)&&valid);
   assert(a.resolveCue("/EmptyCue",valid,error)&&!valid);
   assert(a.resolveCue("/SFX/Player Sounds/FootstepsHardSurfaceRun",valid,error)&&valid);
   if(!catalog.get("events")->get("/SFX/Player Sounds/FootstepsHardSurfaceRun"))assert(!a.createCue("/SFX/Player Sounds/FootstepsHardSurfaceRun",{},next,error)&&next==0);
   // Valid but unexported source events cannot be substituted with silence.
   assert(a.resolveCue("/NoSuchSourceCue",valid,error)&&!valid);
  }
 }
 {
  AudioWorld world(&store);assert(world.loadCatalog(catalog,error));
  const std::string a="/SFX/Player Sounds/FootstepsHardSurface",b="/SFX/Player Sounds/FootstepsHardSurfaceRun",c="/SFX/CeramicPotSmash";
  if(catalog.get("events")->get(a)&&catalog.get("events")->get(b)&&catalog.get("events")->get(c)){
   assert(world.swapCue(a,b,error)&&world.swapCue(b,c,error));uint32_t aliased=0,direct=0;
   assert(world.createCue(a,{true,10,20,0,42,77},aliased,error,true));AudioChannelState state;assert(world.channelState(aliased,state)&&state.name==b);
   assert(world.createCue(a,{},direct,error)&&world.channelState(direct,state)&&state.name==a);
   assert(world.swapCue(a,c,error)&&world.channelState(aliased,state)&&state.name==b);
   assert(world.ownedChannels(42,77).size()==1&&world.ownedChannels(42,78).empty());
   assert(world.detachOwner(42,78,99,99,error)&&world.ownedChannels(42,77).size()==1);
   assert(world.detachOwner(42,77,30,40,error)&&world.ownedChannels(42,77).empty());
   assert(world.channelState(aliased,state)&&state.source.spatial&&state.source.ownerId==0&&state.source.x==30&&state.source.y==40&&!state.started);
   assert(world.play(aliased,error));std::vector<float> pcm(4096);assert(world.render(pcm.data(),2048,error));double energy=0;for(float x:pcm)energy+=double(x)*x;assert(energy>0);
   assert(world.swapCue(a,"/NoSuchSourceCue",error));uint32_t invalid=123;assert(world.createCue(a,{},invalid,error,true)&&invalid==0);assert(world.channelState(aliased,state)&&state.started&&!state.stopped);
   assert(world.stop(aliased,false,error)&&world.advanceControls(0,error)&&!world.channelState(aliased,state));
   std::cout<<"AUDIO_SOURCE_LIFETIME swapSnapshot=true oneLookup=true delayedCreateUnchanged=true existingChannelUnchanged=true invalidDestinationNoChannel=true ownerLifetimeChecked=true detachFreezesXY=true detachedSoundNonzero=true ambientStopSeparate=true\n";
  }
 }
 std::vector<std::string> cues={"/Music/MusicExploration4_MC","/Leftovers/Object Ambiences/EvilLairAmbienceMatchSiteE","/Leftovers/World Sounds/MapZoomInShortHigh","/SFX/Player Sounds/ZagreusWhooshDropIn","/SFX/BatGroupLoop"};if(argc>4)cues={argv[4]};
 for(const auto& cue:cues){AudioWorld world(&store);if(!world.loadCatalog(catalog,error)){std::cerr<<"LOAD "<<error<<'\n';return 1;}uint32_t id=0;assert(!world.createCue("/NoSuchSourceCue",{},id,error)&&id==0);if(!world.createCue(cue,{},id,error)){std::cerr<<"CREATE "<<cue<<' '<<error<<'\n';return 1;}assert(id==1);
  std::vector<float> pcm(256*2);assert(world.render(pcm.data(),256,error));for(float x:pcm)assert(x==0); // admitted but delayed actual Play not yet called
  if(!world.setParameter(id,"Keys",1,0,error)||!world.setParameter(id,"Drums",1,0,error)||!world.setParameter(id,"Section",1,0,error)||!world.play(id,error)){std::cerr<<"START "<<error<<'\n';return 1;}
  const auto began=std::chrono::steady_clock::now();double energy=0,max=0;size_t frames=cue=="/Ambience/RemembranceScreenAmbience2"?1152000:cue.starts_with("/Music/")?240000:144000;for(size_t at=0;at<frames;){const uint32_t count=uint32_t(std::min<size_t>(256,frames-at));if(!world.render(pcm.data(),count,error)){std::cerr<<"RENDER "<<cue<<' '<<error<<'\n';return 1;}for(size_t i=0;i<count*2;++i){assert(std::isfinite(pcm[i]));energy+=double(pcm[i])*pcm[i];max=std::max(max,std::fabs(double(pcm[i])));}at+=count;}
  assert(energy>0);if(cue=="/Ambience/RemembranceScreenAmbience2")assert(world.liveEventCount(cue)==1);assert(world.pause(id,error));assert(world.render(pcm.data(),256,error));assert(world.resume(id,error));assert(world.setVolume(id,0,0.2,true,false,error));assert(world.advanceControls(0.2,error));
  auto markers=world.drainMarkers();if(cue.starts_with("/Music/")){bool start=false;for(const auto& marker:markers)if(marker.name=="Start"&&marker.positionMs==4090)start=true;assert(start);}
  std::cout<<"AUDIO_WORLD_SOURCE cue="<<cue<<" frames="<<frames<<" rms="<<std::sqrt(energy/(frames*2))<<" peak="<<max<<" hostSeconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<<" markers="<<markers.size()<<' '<<world.dump()<<'\n';
 }
}
