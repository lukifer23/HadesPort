// Actual shared GL renderer/source allocation diagnostic; no game binary runs.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include "../host/file_assets.hpp"
#include "../app/scene_primary.hpp"
#include "../app/scene_renderer.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
class Assets:public hades::AssetStore{hades::FileAssets base_,extra_;public:Assets(const char* a,const char* b):base_(a),extra_(b){}std::unique_ptr<hades::AssetFile>open(std::string_view n)override{return n=="primary-child-frames.bin"?extra_.open(n):base_.open(n);}};
int main(int argc,char**argv){try{
 if(argc!=4||std::filesystem::exists(argv[3]))throw std::runtime_error("scene_primary_gpu STAGED_ASSETS PRIMARY_ASSETS NEW_REPORT");Assets assets(argv[1],argv[2]);auto*L=luaL_newstate();if(luaL_loadfile(L,(std::string(argv[2])+"/primaries.lua").c_str())||lua_pcall(L,0,1,0))throw std::runtime_error("Catalog load");hades::SourceValue catalog;std::string error;if(!hades::readSourceValue(L,-1,catalog,error))throw std::runtime_error(error);lua_close(L);
 auto registry=std::make_shared<hades::AnimationRegistry>();hades::ScenePrimaryWorld world;if(!world.load(assets,catalog,registry,error))throw std::runtime_error(error);uint32_t seed=123;hades::EffectAnimationRandom random=[](double&v,std::string&){v=.5;return true;};hades::EffectBoundedRandom bounded=[&](uint32_t b,uint32_t&v,std::string&){seed=seed*1664525+1013904223;v=seed%b;return true;};
 for(const auto&r:catalog.get("Recipes")->array()){if(!r.get("Active")->boolean()||!r.get("DoCreate")->boolean())continue;const int32_t id=int32_t(r.get("OwnerId")->number());const auto*p=world.recipe(id);if(p->externalHaa||hades::ScenePrimaryWorld::externalEffect(p->graphic))continue;hades::EffectOwnerState o;o.lifetime=uint64_t(id)+1;uint64_t h;if(!world.create(id,p->graphic,o,random,bounded,h,error))throw std::runtime_error(error);}
 CGLPixelFormatAttribute attr[]{kCGLPFAOpenGLProfile,static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),kCGLPFAColorSize,static_cast<CGLPixelFormatAttribute>(24),kCGLPFAAlphaSize,static_cast<CGLPixelFormatAttribute>(8),static_cast<CGLPixelFormatAttribute>(0)};CGLPixelFormatObj format=nullptr;GLint count=0;if(CGLChoosePixelFormat(attr,&format,&count)!=kCGLNoError)throw std::runtime_error("GL pixel format");CGLContextObj context=nullptr;if(CGLCreateContext(format,nullptr,&context)!=kCGLNoError||CGLSetCurrentContext(context)!=kCGLNoError)throw std::runtime_error("GL context");CGLDestroyPixelFormat(format);
 constexpr int w=1600,h=1000;GLuint tex,fbo;glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("GL framebuffer");hades::SceneRenderer renderer;renderer.initialize(assets);uint64_t selections=0,submitted=0;
 renderer.setSourceSceneProvider([&](uint32_t i,std::optional<hades::SceneFrameSelection>&out,std::string&e){const bool ok=world.selectSceneFrame(i,out,e);selections+=out.has_value();return ok;},[&](uint32_t i,std::string&e){++submitted;return world.submitted(i,e);});
 constexpr size_t frames=120;std::vector<GLuint>queries(frames);glGenQueries(frames,queries.data());std::vector<double>cpu,gpu;const hades::SceneView view{747.37f,4438.61f,2400,1500};uint64_t uploads=0,bytes=0;
 for(size_t f=0;f<frames;++f){if(!world.advance(1.f/60,error))throw std::runtime_error(error);const auto start=std::chrono::steady_clock::now();glBeginQuery(GL_TIME_ELAPSED,queries[f]);renderer.render(view,w,h,nullptr,f/60.);glEndQuery(GL_TIME_ELAPSED);cpu.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());uploads+=renderer.stats().frameUploads;bytes+=renderer.stats().frameUploadBytes;}
 glFinish();for(auto q:queries){GLuint64 ns;glGetQueryObjectui64v(q,GL_QUERY_RESULT,&ns);gpu.push_back(ns/1e6);}if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("GL error");auto median=[](std::vector<double>v){std::sort(v.begin(),v.end());return v[v.size()/2];};const auto stats=renderer.stats();std::ofstream out(argv[3]);out<<"{\"result\":\"passed\",\"gpu\":\""<<glGetString(GL_RENDERER)<<"\",\"frames\":"<<frames<<",\"source_frame_selections\":"<<selections<<",\"actual_geometry_submissions\":"<<submitted<<",\"gpu_median_ms\":"<<median(gpu)<<",\"cpu_median_ms\":"<<median(cpu)<<",\"frame_uploads\":"<<uploads<<",\"frame_upload_bytes\":"<<bytes<<",\"gpu_cache_bytes\":"<<stats.actorCacheBytes<<",\"gl_errors\":0,\"rng\":\"diagnostic varied-frame LCG, not original PCG proof\",\"android_gameplay_observed\":false}\n";out.close();renderer.shutdown();glDeleteQueries(frames,queries.data());glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);CGLSetCurrentContext(nullptr);CGLDestroyContext(context);
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
