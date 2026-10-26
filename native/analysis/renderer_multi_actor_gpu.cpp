// Measures the real shared renderer with CGL timer queries, never game code.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include "../host/file_assets.hpp"
#include "../app/scene_renderer.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
class SharedBankAssets:public hades::AssetStore{hades::FileAssets& hero_;hades::FileAssets& cinders_;public:SharedBankAssets(hades::FileAssets& h,hades::FileAssets& c):hero_(h),cinders_(c){}std::unique_ptr<hades::AssetFile> open(std::string_view name)override{return name=="cinder-frames.bin"?cinders_.open(name):hero_.open(name);}};
int main(int argc,char** argv){
 try{
  if(argc!=5)throw std::runtime_error("renderer_multi_actor_gpu ASSETS HERO_BANK CINDER_BANK NEW_JSON");
  if(std::filesystem::exists(argv[4]))throw std::runtime_error("Output exists");
  const int frames=360;
  CGLPixelFormatAttribute attr[]{kCGLPFAOpenGLProfile,static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),kCGLPFAColorSize,static_cast<CGLPixelFormatAttribute>(24),kCGLPFAAlphaSize,static_cast<CGLPixelFormatAttribute>(8),static_cast<CGLPixelFormatAttribute>(0)};
  CGLPixelFormatObj format=nullptr;GLint count=0;if(CGLChoosePixelFormat(attr,&format,&count)!=kCGLNoError||!format)throw std::runtime_error("CGL format");CGLContextObj ctx=nullptr;auto status=CGLCreateContext(format,nullptr,&ctx);CGLDestroyPixelFormat(format);if(status!=kCGLNoError||CGLSetCurrentContext(ctx)!=kCGLNoError)throw std::runtime_error("CGL context");
  constexpr int w=1600,h=1000;GLuint tex=0,fbo=0;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("CGL FBO");
  hades::FileAssets assets(argv[1]),actorAssets(argv[2]);hades::SceneRenderer renderer;renderer.initialize(assets);hades::ActorAnimationBank bank;bank.load(actorAssets);hades::FileAssets cinderAssets(argv[3]);SharedBankAssets sharedBanks(actorAssets,cinderAssets);hades::ActorAnimationBank cinders;cinders.load(sharedBanks,"cinder-frames.bin");if(cinders.originalAngleForFacing(0,0)!=0)throw std::runtime_error("Single original angle bank requires no hero companion");hades::ActorAnimationPlayback playback;
  hades::SceneView view{747.37f,4438.61f,2400,1500};const bool hero=true;
  // Host presentation validation must fail on missing or invalid actor visuals;
  // device containment is explicit and counts each distinct reason once.
  hades::ActorVisualProvider missing=[](size_t,hades::ActorVisual&,std::string& error){error="diagnostic actor provider failure";return false;};
  hades::ActorVisualProvider invalid=[](size_t,hades::ActorVisual& out,std::string&){out={};out.scale=-1;return true;};
  auto rejects=[&](const hades::ActorVisualProvider& source,const char* expected){
    bool rejected=false;try{renderer.renderActors(view,w,h,1,source);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find(expected)!=std::string::npos;}
    if(!rejected)throw std::runtime_error("Strict renderer did not reject the expected visual failure");
  };
  rejects(missing,"diagnostic actor provider failure");rejects(invalid,"Invalid source actor transform/material");
  if(renderer.containedFaults()!=0)throw std::runtime_error("Strict visual failures were counted as contained");
  renderer.setFaultContainment(true);
  for(int i=0;i<2;++i){renderer.renderActors(view,w,h,1,missing);renderer.renderActors(view,w,h,1,invalid);}
  if(renderer.containedFaults()!=2)throw std::runtime_error("Contained visual failures were lost or counted repeatedly");
  hades::ActorVisualProvider overflow=[](size_t i,hades::ActorVisual&,std::string& error){error="diagnostic visual failure "+std::to_string(i);return false;};
  for(int i=0;i<2;++i)renderer.renderActors(view,w,h,300,overflow);
  if(renderer.containedFaults()!=257)throw std::runtime_error("Visual diagnostic bound did not retain one overflow indication");
  renderer.shutdown();
  if(renderer.containedFaults()!=0)throw std::runtime_error("Renderer shutdown retained per-map diagnostics");
  renderer.initialize(assets);renderer.renderActors(view,w,h,1,missing);
  if(renderer.containedFaults()!=1)throw std::runtime_error("Renderer reload lost explicit containment or retained suppression");
  renderer.shutdown();renderer.setFaultContainment(false);renderer.initialize(assets);
  std::array<const char*,6> names{"ZagreusSwordCharge1","ZagreusSwordAttack1","ZagreusDashNoCollide","ZagreusRangedWeapon_Fire","ZagreusHeroLanding_Air","ZagreusRun"};
  hades::ActorVisual visual;visual.id=1;visual.lifetime=1;visual.sourceBank=&bank;visual.x=1947.37f;visual.y=5188.61f;
  auto actor=[&](){const auto* clip=playback.clip();const auto angle=bank.selectedAngleForFacing(playback.clipIndex(),4.782202f);visual.frame=bank.sample(playback.clipIndex(),playback.slideIndex(),angle);visual.localRect=visual.frame.worldRect(*clip);};
  hades::ActorVisualProvider provider=[&](size_t index,hades::ActorVisual& out,std::string&){if(index==0){actor();out=visual;return true;}const auto clip=(index-1)%5;const auto slide=(index-1)%28;out={};out.id=uint32_t(index+1);out.lifetime=index+1;out.sourceBank=&cinders;out.x=visual.x;out.y=visual.y;out.scale=.60025f;out.angle=.25f;out.frame=cinders.sample(clip,slide,0,false);out.localRect=out.frame.worldRect(cinders.clips()[clip]);out.sourceSortY=visual.y;return true;};
  playback.play(bank,names[0]);for(int i=0;i<10;++i)renderer.renderActors(view,w,h,81,provider,0);glFinish();
  std::vector<GLuint> queries(frames);glGenQueries(frames,queries.data());std::vector<double> cpu;cpu.reserve(frames);std::vector<std::vector<bool>> visited;for(const auto& clip:renderer.animations().clips())visited.emplace_back(clip.frames.size(),false);
  const auto begin=std::chrono::steady_clock::now();
  for(int i=0;i<frames;++i){const auto start=std::chrono::steady_clock::now();glBeginQuery(GL_TIME_ELAPSED,queries[i]);if(i%60==0)playback.play(bank,names[i/60]);else playback.advance(1.f/60);renderer.renderActors(view,w,h,81,provider,i/60.0);for(size_t c=0;c<visited.size();++c){const auto frame=renderer.animations().clips()[c].uploadedFrame;if(frame<visited[c].size())visited[c][frame]=true;}glEndQuery(GL_TIME_ELAPSED);cpu.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
  glFinish();const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();std::vector<double> gpu;for(auto q:queries){GLuint64 ns=0;glGetQueryObjectui64v(q,GL_QUERY_RESULT,&ns);gpu.push_back(ns/1e6);}glDeleteQueries(frames,queries.data());
  if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("GL timing/render error");
  std::vector<uint8_t>pixels(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
  std::ofstream raw(std::string(argv[4])+".rgba",std::ios::binary);raw.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());raw.close();
  auto samples=[](std::ofstream& out,const std::vector<double>& s){out<<'[';for(size_t i=0;i<s.size();++i){if(i)out<<',';out<<s[i];}out<<']';};
  std::ofstream out(argv[4]);out<<"{\"schema\":\"hades-renderer-gpu-timing-v1\",\"gpu\":\""<<glGetString(GL_RENDERER)<<"\",\"gl\":\""<<glGetString(GL_VERSION)<<"\",\"width\":"<<w<<",\"height\":"<<h<<",\"view\":\""<<(hero?"hero":"room")<<"\",\"frames\":"<<frames<<",\"wall_seconds\":"<<wall<<",\"cpu_ms\":";samples(out,cpu);out<<",\"gpu_ms\":";samples(out,gpu);out<<",\"actor_clips\":"<<bank.clips().size()<<",\"actor_inflate_ms\":"<<bank.stats().inflateMilliseconds<<",\"actor_inflates\":"<<bank.stats().misses<<",\"actor_cache_bytes\":"<<bank.stats().cachedBytes;uint32_t totalFrames=0,visitedFrames=0;for(const auto& clip:visited)for(bool v:clip){++totalFrames;visitedFrames+=v;}out<<",\"animation_frames\":"<<totalFrames<<",\"visited_frames\":"<<visitedFrames;const auto& stats=renderer.stats();out<<",\"diagnostic_image_bindings\":81,\"native_gameplay_observed\":false,\"actor_gpu_cache_bytes\":"<<stats.actorCacheBytes<<",\"actor_gpu_cached_frames\":"<<stats.actorCachedFrames;out<<",\"stats\":{\"source_sprites\":"<<stats.sourceSprites<<",\"visible_sprites\":"<<stats.visibleSprites<<",\"draw_calls\":"<<stats.drawCalls<<",\"group_passes\":"<<stats.groupPasses<<",\"frame_uploads\":"<<stats.frameUploads<<",\"frame_upload_bytes\":"<<stats.frameUploadBytes<<",\"vertex_bytes\":"<<stats.vertexBytes<<"}}\n";out.close();
  renderer.shutdown();glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);CGLSetCurrentContext(nullptr);CGLDestroyContext(ctx);std::cout<<"TIMING_COMPLETE "<<argv[4]<<'\n';
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
