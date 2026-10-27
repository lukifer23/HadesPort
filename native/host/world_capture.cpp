#define GL_SILENCE_DEPRECATION
#include "../app/bloom_renderer.hpp"
#include "world_capture.hpp"
#include "../app/lua_runtime.hpp"
#include "../app/scene_renderer.hpp"
#include "../app/color_grading_renderer.hpp"
#include "../app/fullscreen_blur_renderer.hpp"
#include "../app/ui_renderer.hpp"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <cmath>
#include <vector>

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
    hades::SceneRenderer renderer;renderer.initialize(scripts.currentRoomAssets());
    renderer.setSourceSceneProvider([&](uint32_t index,std::optional<hades::SceneFrameSelection>& frame,std::string& error){return scripts.scenePrimaryFrame(index,frame,error);},[&](uint32_t index,std::string& error){return scripts.scenePrimarySubmitted(index,error);},[&](uint32_t index,hades::SceneSprite& sprite,std::string& error){return scripts.sourceSceneSprite(index,sprite,error);});
    const auto player=scripts.localPlayerBody();if(!player)throw std::runtime_error("Actual local Player body unavailable for capture");
    auto visuals=scripts.presentationVisuals();if(worldOnly)std::erase_if(visuals,[](const auto& key){return key.kind==ScriptRuntime::VisualKey::Kind::Screen;});const float viewHeight=1680,viewWidth=viewHeight*float(width)/height;
    hades::SceneView view{player->x-viewWidth*.5f,player->y-viewHeight*.55f,viewWidth,viewHeight};
    const auto& camera=scripts.cameraState();if(camera.initialized){const float h=1080.f/camera.zoom,w=h*float(width)/height;view={camera.x-w*.5f,camera.y-h*.5f,w,h};}
    hades::ColorGradingRenderer grading;
    hades::BloomRenderer bloom;struct BloomOwner{hades::BloomRenderer& b;~BloomOwner(){b.shutdown();}} bloomOwner{bloom};uint32_t effectGroup=UINT32_MAX;bool drawColor=false,drawBloom=false;std::string effectError;if(!scripts.fullscreenPresentation(effectGroup,drawColor,drawBloom,effectError))throw std::runtime_error(effectError);
    hades::FullscreenBlurRenderer blur;struct BlurOwner{hades::FullscreenBlurRenderer& b;~BlurOwner(){b.shutdown();}} blurOwner{blur};
    renderer.setFullscreenEffects(effectGroup,[&](int w,int h){if(drawColor)grading.render(scripts.colorGrading(),w,h);bloom.render(scripts.bloom(),drawBloom,w,h);blur.renderRadialBlur(scripts.radialBlur(),w,h);});
    renderer.renderActors(view,width,height,visuals.size(),[&](size_t i,hades::ActorVisual& out,std::string& error){return scripts.samplePresentation(visuals.at(i),out,error);});
    {hades::VignetteFrameDraw frame;if(!scripts.fullscreenFramePresentation(frame,effectError))throw std::runtime_error(effectError);blur.renderVignetteFrame(frame,width,height);}
    if(!worldOnly){
        hades::UiRenderer text;text.initialize(assets);if(scripts.textFonts())text.renderText(scripts.textVisuals(),*scripts.textFonts(),width,height,view);text.renderCameraFade(scripts.cameraFadeColor(),width,height);text.shutdown();
    }
    glFinish();
    glReadBuffer(GL_COLOR_ATTACHMENT0);std::vector<uint8_t> rgba(size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Live-capture GL error");
    std::ofstream output(path,std::ios::binary);output<<"P6\n"<<width<<' '<<height<<"\n255\n";for(int y=height-1;y>=0;--y)for(int x=0;x<width;++x)output.write(reinterpret_cast<const char*>(rgba.data()+(size_t(y)*width+x)*4),3);output.close();if(!output)throw std::runtime_error("Cannot write actual world capture");
    const auto stats=renderer.stats();std::cout<<"LIVE_WORLD_CAPTURE path="<<path<<" scope="<<(worldOnly?"world-only-diagnostic":"live-world-and-screens")<<" visuals="<<visuals.size()<<" visible="<<stats.visibleSprites<<" draws="<<stats.drawCalls<<" world_error="<<scripts.error()<<'\n';grading.shutdown();renderer.shutdown();glDeleteFramebuffers(1,&target);glDeleteTextures(1,&texture);
}

struct FullscreenEffectValidator::Impl {
    static constexpr int width=64,height=40;
    CGLContextObj context=nullptr;GLuint texture=0,target=0;hades::FullscreenBlurRenderer blur;std::vector<uint8_t> input;
    Impl(){
        const CGLPixelFormatAttribute attributes[]{kCGLPFAOpenGLProfile,static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),kCGLPFAColorSize,static_cast<CGLPixelFormatAttribute>(24),kCGLPFAAlphaSize,static_cast<CGLPixelFormatAttribute>(8),static_cast<CGLPixelFormatAttribute>(0)};
        CGLPixelFormatObj format=nullptr;GLint count=0;if(CGLChoosePixelFormat(attributes,&format,&count)!=kCGLNoError||!format)throw std::runtime_error("No fullscreen-validation GL format");
        const auto created=CGLCreateContext(format,nullptr,&context);CGLDestroyPixelFormat(format);if(created!=kCGLNoError||!context)throw std::runtime_error("No fullscreen-validation GL context");
        if(CGLSetCurrentContext(context)!=kCGLNoError){CGLDestroyContext(context);throw std::runtime_error("Cannot select fullscreen-validation GL context");}
        glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&target);glBindFramebuffer(GL_FRAMEBUFFER,target);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
        const bool complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;CGLSetCurrentContext(nullptr);if(!complete){release();throw std::runtime_error("Fullscreen-validation framebuffer incomplete");}
        // Fixed diagnostic input (not a game asset): two ramps plus a hashed channel.
        input.resize(size_t(width)*height*4);for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto*p=&input[(size_t(y)*width+x)*4];p[0]=uint8_t(x*255/(width-1));p[1]=uint8_t(y*255/(height-1));p[2]=uint8_t(((x*7+y*13)*5)&255);p[3]=255;}
    }
    void release(){if(!context)return;CGLSetCurrentContext(context);blur.shutdown();if(target)glDeleteFramebuffers(1,&target);if(texture)glDeleteTextures(1,&texture);CGLSetCurrentContext(nullptr);CGLDestroyContext(context);context=nullptr;}
    ~Impl(){release();}
    float texel(int x,int y,int c)const{x=std::clamp(x,0,width-1);y=std::clamp(y,0,height-1);return input[(size_t(y)*width+x)*4+c]/255.f;}
    float bilinear(float u,float v,int c)const{const float px=u*width-.5f,py=v*height-.5f;const int x0=int(std::floor(px)),y0=int(std::floor(py));const float fx=px-float(x0),fy=py-float(y0);
        const float top=texel(x0,y0,c)+(texel(x0+1,y0,c)-texel(x0,y0,c))*fx,bottom=texel(x0,y0+1,c)+(texel(x0+1,y0+1,c)-texel(x0,y0+1,c))*fx;return top+(bottom-top)*fy;}
};
FullscreenEffectValidator::FullscreenEffectValidator()=default;
FullscreenEffectValidator::~FullscreenEffectValidator()=default;
bool FullscreenEffectValidator::validateRadialBlur(const hades::RadialBlurState& state,int& maxChannelError,std::string& error){
    maxChannelError=0;if(!state.shouldDraw()){error.clear();return true;}
    try{
        if(!impl_)impl_=std::make_unique<Impl>();auto& v=*impl_;constexpr int W=Impl::width,H=Impl::height;
        if(CGLSetCurrentContext(v.context)!=kCGLNoError)throw std::runtime_error("Cannot select fullscreen-validation GL context");
        struct Release{~Release(){CGLSetCurrentContext(nullptr);}} release;
        glBindFramebuffer(GL_FRAMEBUFFER,v.target);glBindTexture(GL_TEXTURE_2D,v.texture);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,v.input.data());glBindTexture(GL_TEXTURE_2D,0);glViewport(0,0,W,H);
        v.blur.renderRadialBlur(state,W,H);glFinish();
        std::vector<uint8_t> out(size_t(W)*H*4);glBindFramebuffer(GL_READ_FRAMEBUFFER,v.target);glReadPixels(0,0,W,H,GL_RGBA,GL_UNSIGNED_BYTE,out.data());if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Fullscreen-validation readback GL error");
        const float dist0=state.sampleDist(),strength=state.sampleStrength();constexpr float taps[]{-0.08f,-0.05f,-0.03f,-0.02f,-0.01f,0.01f,0.02f,0.03f,0.05f,0.08f};
        for(int y=0;y<H;++y)for(int x=0;x<W;++x){
            const float u=(x+.5f)/W,t=(y+.5f)/H;float dx=.5f-u,dy=.5f-t;const float dist=std::sqrt(dx*dx+dy*dy);dx=dx/dist*dist0;dy=dy/dist*dist0;const float weight=std::clamp(dist*strength,0.f,1.f);
            for(int c=0;c<4;++c){const float color=v.bilinear(u,t,c);float sum=color;for(float k:taps)sum+=v.bilinear(u+k*dx,t+k*dy,c);sum*=1.f/11.f;
                const float expected=std::clamp(color+weight*(sum-color),0.f,1.f)*255.f;const int got=out[(size_t(y)*W+x)*4+c];maxChannelError=std::max(maxChannelError,int(std::ceil(std::abs(expected-float(got))-.5f)));}
        }
        // Linear-filter weight quantization plus RGBA8 rounding.
        if(maxChannelError>3){error="Native radial blur GPU output differs from RadialBlur.frag evaluation by "+std::to_string(maxChannelError)+" (strength="+std::to_string(strength)+" dist="+std::to_string(dist0)+")";return false;}
    }catch(const std::exception& e){error=e.what();return false;}
    error.clear();return true;
}
