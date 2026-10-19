#define GL_SILENCE_DEPRECATION
#include "../app/bloom_renderer.hpp"
#include "world_capture.hpp"
#include "../app/lua_runtime.hpp"
#include "../app/scene_renderer.hpp"
#include "../app/color_grading_renderer.hpp"
#include "../app/ui_renderer.hpp"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

void captureLiveWorld(ScriptRuntime& scripts,hades::AssetStore& assets,const std::string& path,bool worldOnly){
    if(std::filesystem::exists(path))throw std::runtime_error("Live capture already exists");
    const CGLPixelFormatAttribute attributes[]{kCGLPFAOpenGLProfile,static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),kCGLPFAColorSize,static_cast<CGLPixelFormatAttribute>(24),kCGLPFAAlphaSize,static_cast<CGLPixelFormatAttribute>(8),static_cast<CGLPixelFormatAttribute>(0)};
    CGLPixelFormatObj format=nullptr;GLint count=0;CGLContextObj context=nullptr;
    if(CGLChoosePixelFormat(attributes,&format,&count)!=kCGLNoError||!format)throw std::runtime_error("No live-capture GL format");
    const auto created=CGLCreateContext(format,nullptr,&context);CGLDestroyPixelFormat(format);
    if(created!=kCGLNoError||!context)throw std::runtime_error("No live-capture GL context");
    struct ContextOwner {CGLContextObj context;~ContextOwner(){CGLSetCurrentContext(nullptr);CGLDestroyContext(context);}} owner{context};
    if(CGLSetCurrentContext(context)!=kCGLNoError)throw std::runtime_error("Cannot select live-capture GL context");
    constexpr int width=1600,height=1000;GLuint texture=0,target=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glGenFramebuffers(1,&target);glBindFramebuffer(GL_FRAMEBUFFER,target);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Live-capture framebuffer incomplete");
    hades::SceneRenderer renderer;renderer.initialize(assets);
    renderer.setSourceSceneProvider([&](uint32_t index,std::optional<hades::SceneFrameSelection>& frame,std::string& error){return scripts.scenePrimaryFrame(index,frame,error);},[&](uint32_t index,std::string& error){return scripts.scenePrimarySubmitted(index,error);},[&](uint32_t index,hades::SceneSprite& sprite,std::string& error){return scripts.sourceSceneSprite(index,sprite,error);});
    const auto player=scripts.localPlayerBody();if(!player)throw std::runtime_error("Actual local Player body unavailable for capture");
    auto visuals=scripts.presentationVisuals();if(worldOnly)std::erase_if(visuals,[](const auto& key){return key.kind==ScriptRuntime::VisualKey::Kind::Screen;});const float viewHeight=1680,viewWidth=viewHeight*float(width)/height;
    hades::SceneView view{player->x-viewWidth*.5f,player->y-viewHeight*.55f,viewWidth,viewHeight};
    const auto& camera=scripts.cameraState();if(camera.initialized){const float h=1080.f/camera.zoom,w=h*float(width)/height;view={camera.x-w*.5f,camera.y-h*.5f,w,h};}
    hades::ColorGradingRenderer grading;
    hades::BloomRenderer bloom;struct BloomOwner{hades::BloomRenderer& b;~BloomOwner(){b.shutdown();}} bloomOwner{bloom};uint32_t effectGroup=UINT32_MAX;bool drawColor=false,drawBloom=false;std::string effectError;if(!scripts.fullscreenPresentation(effectGroup,drawColor,drawBloom,effectError))throw std::runtime_error(effectError);
    renderer.setFullscreenEffects(effectGroup,[&](int w,int h){if(drawColor)grading.render(scripts.colorGrading(),w,h);bloom.render(scripts.bloom(),drawBloom,w,h);});
    renderer.renderActors(view,width,height,visuals.size(),[&](size_t i,hades::ActorVisual& out,std::string& error){return scripts.samplePresentation(visuals.at(i),out,error);});
    if(!worldOnly){
        hades::UiRenderer text;text.initialize(assets);if(scripts.textFonts())text.renderText(scripts.textVisuals(),*scripts.textFonts(),width,height,view);text.renderCameraFade(scripts.cameraFadeColor(),width,height);text.shutdown();
    }
    glFinish();
    glReadBuffer(GL_COLOR_ATTACHMENT0);std::vector<uint8_t> rgba(size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Live-capture GL error");
    std::ofstream output(path,std::ios::binary);output<<"P6\n"<<width<<' '<<height<<"\n255\n";for(int y=height-1;y>=0;--y)for(int x=0;x<width;++x)output.write(reinterpret_cast<const char*>(rgba.data()+(size_t(y)*width+x)*4),3);output.close();if(!output)throw std::runtime_error("Cannot write actual world capture");
    const auto stats=renderer.stats();std::cout<<"LIVE_WORLD_CAPTURE path="<<path<<" scope="<<(worldOnly?"world-only-diagnostic":"live-world-and-screens")<<" visuals="<<visuals.size()<<" visible="<<stats.visibleSprites<<" draws="<<stats.drawCalls<<" world_error="<<scripts.error()<<'\n';grading.shutdown();renderer.shutdown();glDeleteFramebuffers(1,&target);glDeleteTextures(1,&texture);
}
