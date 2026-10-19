#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include "file_assets.hpp"
#include "../app/scene_renderer.hpp"
#include "../app/game_ui.hpp"
#include "../app/ui_renderer.hpp"
#include "../app/camera_rig.hpp"
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iostream>

namespace {
class Context {
    CGLContextObj context_=nullptr;
public:
    Context(){
        const CGLPixelFormatAttribute attributes[]{kCGLPFAOpenGLProfile,static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core),kCGLPFAColorSize,static_cast<CGLPixelFormatAttribute>(24),kCGLPFAAlphaSize,static_cast<CGLPixelFormatAttribute>(8),static_cast<CGLPixelFormatAttribute>(0)};
        CGLPixelFormatObj format=nullptr;GLint count=0;
        auto result=CGLChoosePixelFormat(attributes,&format,&count);
        if(result!=kCGLNoError||!format)throw std::runtime_error("No Mac OpenGL core pixel format");
        result=CGLCreateContext(format,nullptr,&context_);CGLDestroyPixelFormat(format);
        if(result!=kCGLNoError||!context_)throw std::runtime_error("Mac OpenGL context creation failed");
        if(CGLSetCurrentContext(context_)!=kCGLNoError)throw std::runtime_error("Cannot make Mac OpenGL context current");
    }
    ~Context(){CGLSetCurrentContext(nullptr);if(context_)CGLDestroyContext(context_);}
};
}
int main(int argc,char** argv){
    if(argc<3||argc>6){std::cerr<<"Usage: hades-render ASSET_ROOT NEW_CAPTURE.ppm [SECONDS [FRAME_COUNT [title|loading|load-failed|settings|paused|playing]]]\n";return 64;}
    try{
        if(std::filesystem::exists(argv[2]))throw std::runtime_error("Capture already exists; choose a new output");
        hades::FileAssets assets(argv[1]);Context context;
        const double seconds=argc>=4?std::stod(argv[3]):0;
        const int frames=argc>=5?std::stoi(argv[4]):1;
        if(frames<1||frames>3600)throw std::runtime_error("Capture frame count outside bounds");
        std::cout<<"HOST_GPU renderer="<<glGetString(GL_RENDERER)<<" version="<<glGetString(GL_VERSION)<<'\n';
        constexpr int width=1600,height=1000;
        GLuint target,framebuffer;glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glGenFramebuffers(1,&framebuffer);glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Capture framebuffer incomplete");
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        hades::SceneRenderer renderer;renderer.initialize(assets);Player player;player.load(&assets);
        hades::GameUi ui;hades::UiRenderer uiRenderer;
        if(argc==6){
            const std::string mode=argv[5];
            if(mode=="settings")ui.show(hades::UiScreen::Settings);
            else if(mode=="paused")ui.show(hades::UiScreen::Paused);
            else if(mode=="playing")ui.show(hades::UiScreen::Playing);
            else if(mode=="loading"){ui.show(hades::UiScreen::Loading);ui.loadingProgress=1.f/3;}
            else if(mode=="load-failed")ui.loadFailed();
            else if(mode!="title")throw std::runtime_error("Unknown UI capture mode");
            uiRenderer.initialize(assets);
        }
        // Whole original room diagnostic viewport, independent of phone size.
        hades::CameraRig camera;camera.update(0,player.position.x,player.position.y,ui.settings.cameraHeight);
        const hades::SceneView view=argc==6?camera.view(width,height):
            hades::SceneView{578.0337f,2876.104f,5093.7788f,3183.61175f};
        std::vector<std::vector<bool>> visited;
        for(const auto& clip:renderer.animations().clips())visited.emplace_back(clip.frames.size(),false);
        const auto begin=std::chrono::steady_clock::now();
        for(int frame=0;frame<frames;++frame){
            renderer.render(view,width,height,&player,frames==1?seconds:seconds*frame/(frames-1));
            if(argc==6)uiRenderer.render(ui,width,height,seconds);
            for(size_t i=0;i<visited.size();++i)visited[i].at(renderer.animations().clips()[i].uploadedFrame)=true;
        }
        glFinish();
        std::cout<<"GPU_SEQUENCE frames="<<frames<<" animation_seconds="<<seconds<<" wall_seconds="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count()<<'\n';
        for(size_t i=0;i<visited.size();++i){size_t seen=0;for(bool v:visited[i])seen+=v;
            std::cout<<"EFFECT_PLAYBACK clip="<<renderer.animations().clips()[i].name<<" visited="<<seen<<" original_frames="<<visited[i].size()<<'\n';}
        std::vector<uint8_t> rgba(size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,rgba.data());
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Capture readback failed");
        std::ofstream output(argv[2],std::ios::binary);if(!output)throw std::runtime_error("Cannot create capture");output<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(int y=height-1;y>=0;--y)for(int x=0;x<width;++x)output.write(reinterpret_cast<const char*>(rgba.data()+(size_t(y)*width+x)*4),3);
        output.close();if(!output)throw std::runtime_error("Capture write failed");
        const auto count=renderer.scene().sprites.size();if(argc==6)uiRenderer.shutdown();renderer.shutdown();glDeleteFramebuffers(1,&framebuffer);glDeleteTextures(1,&target);
        std::cout<<"HOST_GPU_CAPTURE_COMPLETE file="<<argv[2]<<" sprites="<<count<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<"HOST_GPU_CAPTURE_FAILURE "<<error.what()<<'\n';return 1;}
}
