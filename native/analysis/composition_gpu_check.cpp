// Focused source-group composition/lifecycle check on the real Mac GPU.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include "../host/file_assets.hpp"
#include "../app/scene_renderer.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <map>

struct MemoryFile : hades::AssetFile {
 std::vector<uint8_t> bytes;size_t at=0;
 explicit MemoryFile(std::vector<uint8_t> data):bytes(std::move(data)){}
 size_t size()const override{return bytes.size();}size_t remaining()const override{return bytes.size()-at;}
 size_t read(void* p,size_t n)override{n=std::min(n,remaining());std::memcpy(p,bytes.data()+at,n);at+=n;return n;}
};
struct Assets : hades::AssetStore {
 hades::FileAssets source;int mode=0;
 explicit Assets(const char* path):source(path){}
 std::unique_ptr<hades::AssetFile> open(std::string_view name)override{
  if(name=="composition.bin"&&mode==1)return nullptr;
  auto f=source.open(name);
  if(name=="composition.bin"&&mode==2&&f){std::vector<uint8_t>b(f->remaining());f->read(b.data(),b.size());b.at(4)^=1;return std::make_unique<MemoryFile>(std::move(b));}
  return f;
 }
};
void check(bool condition,const char* msg){if(!condition)throw std::runtime_error(msg);}
// Deliberately tiny overlap diagnostic, never used as a game asset.
struct PixelAssets : hades::AssetStore {
 std::map<std::string,std::vector<uint8_t>> packets;
 template<class T>void put(std::vector<uint8_t>& b,T value){auto p=reinterpret_cast<const uint8_t*>(&value);b.insert(b.end(),p,p+sizeof(value));}
 explicit PixelAssets(uint32_t mode){
  auto& scene=packets["scene.bin"];put(scene,uint32_t(0x33534448));put(scene,uint32_t(3));put(scene,uint32_t(3));
  const std::array<std::array<uint8_t,4>,3> pixels{{{{64,128,192,128}},{{255,0,0,128}},{{0,255,0,128}}}};
  for(const auto& p:pixels){put(scene,uint32_t(1));put(scene,uint32_t(1));scene.insert(scene.end(),p.begin(),p.end());}
  for(uint32_t i=0;i<3;++i){put(scene,i);for(float f:std::array<float,8>{-1,-1,1,-1,-1,1,1,1})put(scene,f);for(float f:std::array<float,4>{0,0,1,1})put(scene,f);for(int n=0;n<4;++n)put(scene,1.f);put(scene,i?mode:0);put(scene,uint32_t(0));}
  auto& material=packets["materials.bin"];put(material,uint32_t(0x314d5348));put(material,uint32_t(3));for(int n=0;n<9;++n)put(material,0.f);
  packets["scene-sha256.bin"]=std::vector<uint8_t>(32,0);
  auto& group=packets["composition.bin"];put(group,uint32_t(0x31434748));group.insert(group.end(),32,0);put(group,uint32_t(3));put(group,uint32_t(0));put(group,uint32_t(1));put(group,uint32_t(1));
  auto& order=packets["ordering.bin"];put(order,uint32_t(0x314f4448));put(order,3u);for(uint32_t i=0;i<3;++i){put(order,float(i?1:0));put(order,0.f);put(order,0.f);}
 }
 std::unique_ptr<hades::AssetFile> open(std::string_view name)override{auto i=packets.find(std::string(name));return i==packets.end()?nullptr:std::make_unique<MemoryFile>(i->second);}
};
// A coordinate ramp distinguishes real displacement direction/strength from
// drawing the distortion map as ordinary color. This is diagnostic data only.
struct DisplacementAssets : PixelAssets {
 explicit DisplacementAssets(std::array<uint8_t,4> displacement):PixelAssets(13){
  packets.clear();auto& scene=packets["scene.bin"];put(scene,uint32_t(0x33534448));put(scene,uint32_t(2));put(scene,uint32_t(2));
  put(scene,uint32_t(32));put(scene,uint32_t(16));for(int y=0;y<16;++y)for(int x=0;x<32;++x){scene.push_back(uint8_t(std::lround(x*255./31)));scene.push_back(uint8_t(y*17));scene.push_back(0);scene.push_back(255);}
  put(scene,uint32_t(1));put(scene,uint32_t(1));scene.insert(scene.end(),displacement.begin(),displacement.end());
  for(uint32_t i=0;i<2;++i){put(scene,i);for(float f:std::array<float,8>{-1,-1,1,-1,-1,1,1,1})put(scene,f);for(float f:std::array<float,4>{0,0,1,1})put(scene,f);for(int n=0;n<4;++n)put(scene,1.f);put(scene,i?13u:0u);put(scene,0u);}
  auto& material=packets["materials.bin"];put(material,uint32_t(0x314d5348));put(material,2u);for(int n=0;n<6;++n)put(material,0.f);packets["scene-sha256.bin"]=std::vector<uint8_t>(32,0);
  auto& group=packets["composition.bin"];put(group,uint32_t(0x31434748));group.insert(group.end(),32,0);put(group,2u);put(group,0u);put(group,1u);
 }
};
int main(int argc,char** argv){
 try{
  check(argc==3,"Usage: composition_gpu_check ASSETS NEW_OUTPUT_PREFIX");
  CGLPixelFormatAttribute attributes[]{kCGLPFAOpenGLProfile,static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),kCGLPFAColorSize,static_cast<CGLPixelFormatAttribute>(24),kCGLPFAAlphaSize,static_cast<CGLPixelFormatAttribute>(8),static_cast<CGLPixelFormatAttribute>(0)};
  CGLPixelFormatObj format=nullptr;GLint count=0;check(CGLChoosePixelFormat(attributes,&format,&count)==kCGLNoError&&format,"Pixel format");
  CGLContextObj context=nullptr;check(CGLCreateContext(format,nullptr,&context)==kCGLNoError,"Context");CGLDestroyPixelFormat(format);check(CGLSetCurrentContext(context)==kCGLNoError,"Current context");
  std::cout<<"GPU "<<glGetString(GL_RENDERER)<<'\n';
  GLuint tex=0,fbo=0;glGenTextures(1,&tex);glGenFramebuffers(1,&fbo);
  auto resize=[&](int w,int h){glBindTexture(GL_TEXTURE_2D,tex);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"Target");glReadBuffer(GL_COLOR_ATTACHMENT0);};
  auto capture=[&](const std::string& suffix,int w,int h){const auto path=std::string(argv[2])+suffix;check(!std::filesystem::exists(path),"Capture exists");std::vector<uint8_t>rgba(size_t(w)*h*4);glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());std::ofstream file(path,std::ios::binary);file<<"P6\n"<<w<<' '<<h<<"\n255\n";for(int y=h-1;y>=0;--y)for(int x=0;x<w;++x)file.write(reinterpret_cast<const char*>(rgba.data()+(size_t(y)*w+x)*4),3);check(bool(file),"Write capture");};
  hades::SceneRenderer renderer;
  resize(8,8);
  for(uint32_t mode:{9u,11u}){
   PixelAssets diagnostic(mode);renderer.initialize(diagnostic);hades::RenderOptions transparent;transparent.backdrop={0,0,0,0};renderer.setOptions(transparent);renderer.render(hades::SceneView{-1,-1,2,2},8,8);
   std::array<uint8_t,4> actual{};glReadPixels(4,4,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
   const std::array<int,4> expected=mode==9?std::array<int,4>{24,80,24,224}:std::array<int,4>{64,128,0,255};
   for(size_t n=0;n<4;++n)check(std::abs(int(actual[n])-expected[n])<=2,"Overlap/clear/opaque shader diagnostic mismatch");
   std::cout<<"SOURCE_GROUP_PIXEL mode="<<mode<<" rgba="<<int(actual[0])<<','<<int(actual[1])<<','<<int(actual[2])<<','<<int(actual[3])<<'\n';renderer.shutdown();
  }
  resize(32,16);
  for(const auto direction:{std::array<uint8_t,4>{128,128,0,255},std::array<uint8_t,4>{255,128,0,255},std::array<uint8_t,4>{128,255,0,255}}){
   DisplacementAssets diagnostic(direction);renderer.initialize(diagnostic);renderer.render(hades::SceneView{-1,-1,2,2},32,16);
   std::array<uint8_t,4> actual{};glReadPixels(16,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
   const float shiftX=direction[0]==255?.15f:0,shiftY=direction[1]==255?.15f:0;
   const int expectedR=int(std::lround((16+shiftX*32)*255.f/31)),expectedG=int(std::lround((7+shiftY*16)*17));
   check(std::abs(int(actual[0])-expectedR)<=2&&std::abs(int(actual[1])-expectedG)<=2&&actual[3]==255,"Source displacement neutral/direction/strength mismatch");
   check(renderer.stats().groupPasses==1&&glGetError()==GL_NO_ERROR,"Source displacement group/GL failure");
   std::cout<<"SOURCE_DISPLACEMENT_PIXEL vector="<<int(direction[0])<<','<<int(direction[1])<<" rgba="<<int(actual[0])<<','<<int(actual[1])<<','<<int(actual[2])<<','<<int(actual[3])<<'\n';renderer.shutdown();
  }
  resize(16,16);PixelAssets lightDiagnostic(0);renderer.initialize(lightDiagnostic);const hades::SceneView lightView{-1,-1,2,2};
  renderer.render(lightView,16,16);std::array<uint8_t,4> base{};glReadPixels(12,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,base.data());
  std::array<uint8_t,4> originalLightPixel{192,160,128,255};
  hades::ActorVisual light;light.id=1;light.lifetime=1;light.sourceBank=&originalLightPixel;light.frame.width=light.frame.height=1;light.frame.rgba=originalLightPixel;light.localRect={-1,-1,2,2};light.drawGroup=2;light.light=true;
  auto drawLight=[&](){renderer.renderActors(lightView,16,16,1,[&](size_t,hades::ActorVisual& out,std::string&){out=light;return true;});};
  drawLight();std::array<uint8_t,4> lit{};glReadPixels(12,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,lit.data());
  for(size_t c=0;c<3;++c)check(std::abs(int(lit[c])-int(std::lround(std::min(255.f,2.f*base[c]*originalLightPixel[c]/255))))<=2,"Source light shader/state6 diagnostic mismatch");
  auto shadows=std::make_shared<hades::LightShadowGeometry>();shadows->revision=1;shadows->polygons.push_back({1,1,{{{.25f,-1},{.35f,-1},{.35f,1},{.25f,1}}}});light.lightShadows=shadows;
  drawLight();std::array<uint8_t,4> blocked{},clear{};glReadPixels(12,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,blocked.data());glReadPixels(3,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,clear.data());
  for(size_t c=0;c<3;++c){check(std::abs(int(blocked[c])-int(base[c]))<=2,"Actual polygon shadow failed to neutralize light");check(std::abs(int(clear[c])-int(lit[c]))<=2,"Actual polygon shadow blocked wrong side");}
  drawLight();check(glGetError()==GL_NO_ERROR,"Cached light shadow GL failure");
  std::cout<<"SOURCE_LIGHT_PIXEL base="<<int(base[0])<<','<<int(base[1])<<','<<int(base[2])<<" lit="<<int(lit[0])<<','<<int(lit[1])<<','<<int(lit[2])<<" blocked="<<int(blocked[0])<<','<<int(blocked[1])<<','<<int(blocked[2])<<" polarCache=true\n";renderer.shutdown();
  // sampler.inc premultiplies texture RGB before light.frag interpolates from
  // neutral .5. An opaque-only check cannot distinguish that source operation.
  renderer.initialize(lightDiagnostic);light.lightShadows.reset();
  std::array<uint8_t,4> translucentLightPixel{192,160,128,128};
  light.sourceBank=&translucentLightPixel;light.frame.rgba=translucentLightPixel;
  light.color={.8f,.6f,.4f,.75f};drawLight();
  std::array<uint8_t,4> translucent{};glReadPixels(12,8,1,1,GL_RGBA,GL_UNSIGNED_BYTE,translucent.data());
  for(size_t c=0;c<3;++c){const float sample=translucentLightPixel[c]/255.f*translucentLightPixel[3]/255.f;const float neutral=.5f+(sample-.5f)*light.color[c]*light.color[3];check(std::abs(int(translucent[c])-int(std::lround(std::min(255.f,2.f*base[c]*neutral))))<=2,"Source premultiplied translucent light mismatch");}
  std::cout<<"SOURCE_LIGHT_TRANSLUCENT_PIXEL rgba="<<int(translucent[0])<<','<<int(translucent[1])<<','<<int(translucent[2])<<','<<int(translucent[3])<<" samplerPremul=true\n";renderer.shutdown();
  Assets assets(argv[1]);Player player;player.load(&assets);
  // A source binding failure after texture creation must clean up for retry.
  assets.mode=2;bool rejected=false;try{renderer.initialize(assets);}catch(const std::runtime_error& e){rejected=std::string(e.what()).find("composition hash binding")!=std::string::npos;}
  check(rejected,"Bad source binding accepted");check(renderer.scene().sprites.empty(),"Partial scene survives failed initialization");
  assets.mode=0;renderer.initialize(assets);check(renderer.scene().hasSourceComposition,"Companion not loaded");
  const hades::SceneView view{578.0337f,2876.104f,5093.7788f,3183.61175f};
  resize(1600,1000);renderer.render(view,1600,1000,&player,1);capture("-grouped.ppm",1600,1000);
  GLint actual=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&actual);check(actual==static_cast<GLint>(fbo),"Draw framebuffer not restored");glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&actual);check(actual==static_cast<GLint>(fbo),"Read framebuffer not restored");
  resize(877,619);auto options=renderer.options();options.displayExposure=1.25f;renderer.setOptions(options);renderer.render(view,877,619,&player,2);check(glGetError()==GL_NO_ERROR,"Resize/exposure GL failure");renderer.shutdown();renderer.shutdown();
  assets.mode=1;renderer.initialize(assets);check(!renderer.scene().hasSourceComposition,"Absent companion changed default");resize(1600,1000);renderer.render(view,1600,1000,&player,1);capture("-legacy.ppm",1600,1000);renderer.shutdown();
  // Reuse the same renderer in a newly created context, as after context loss.
  glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);CGLSetCurrentContext(nullptr);CGLDestroyContext(context);
  check(CGLChoosePixelFormat(attributes,&format,&count)==kCGLNoError,"New pixel format");check(CGLCreateContext(format,nullptr,&context)==kCGLNoError,"New context");CGLDestroyPixelFormat(format);CGLSetCurrentContext(context);
  glGenTextures(1,&tex);glGenFramebuffers(1,&fbo);assets.mode=0;renderer.initialize(assets);resize(320,200);renderer.render(view,320,200,&player,3);renderer.shutdown();check(glGetError()==GL_NO_ERROR,"Context reinitialization GL failure");glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&tex);CGLSetCurrentContext(nullptr);CGLDestroyContext(context);
  std::cout<<"PASS binding rejection + partial cleanup + source groups + hero + resize/exposure + repeated shutdown + new context reinitialize\n";
 }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
