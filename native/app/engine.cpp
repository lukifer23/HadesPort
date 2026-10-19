#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/configuration.h>
#include <cstdio>
#include <dlfcn.h>
#include <sys/stat.h>
#include <jni.h>
#include <thread>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include "lua_runtime.hpp"
#include "player.hpp"
#include "scene_renderer.hpp"
#include "color_grading_renderer.hpp"
#include "bloom_renderer.hpp"
#include "android_assets.hpp"
#include "game_ui.hpp"
#include "ui_renderer.hpp"
#include "touch_controls.hpp"
#include "camera_rig.hpp"
#include "input_bindings.hpp"
#include "control_bindings.hpp"
#include <set>

#define INFO(...) __android_log_print(ANDROID_LOG_INFO, "HadesNative", __VA_ARGS__)
#define ERROR(...) __android_log_print(ANDROID_LOG_ERROR, "HadesNative", __VA_ARGS__)

namespace {
class Engine {
    android_app* app_;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    hades::SceneRenderer renderer_;
    hades::ColorGradingRenderer colorGradingRenderer_;
    hades::BloomRenderer bloomRenderer_;
    hades::UiRenderer uiRenderer_;
    hades::GameUi ui_;
    std::string settingsPath_;
    std::unique_ptr<ScriptRuntime> scripts_;
    std::shared_ptr<hades::ActorAnimationBank> heroBank_;
    hades::AndroidAssets assets_;
    std::chrono::steady_clock::time_point lastFrame_ = std::chrono::steady_clock::now();
    hades::TouchControls controls_;
    hades::CameraRig camera_;
    // Graphics settings: offscreen scene target for render-resolution scaling
    // (UI stays native resolution) and sleep-based frame-rate capping.
    unsigned sceneFbo_=0,sceneColor_=0;int sceneW_=0,sceneH_=0;std::chrono::steady_clock::time_point nextPresent_{};int hintedCap_=-1;
    // Letterbox (original ConfigOptions::Letterbox): centred 16:9 scene rect.
    std::array<int,4> sceneRect()const{
        if(!ui_.settings.letterbox||width_<=0||height_<=0)return {0,0,width_,height_};
        int w=width_,h=int(std::lround(width_*9.0/16));if(h>height_){h=height_;w=int(std::lround(height_*16.0/9));}
        return {(width_-w)/2,(height_-h)/2,w,h};
    }
    void bindSceneTarget(int w,int h,bool forceOffscreen=false){
        if(!forceOffscreen&&w==width_&&h==height_){glBindFramebuffer(GL_FRAMEBUFFER,0);return;}
        if(!sceneFbo_){glGenFramebuffers(1,&sceneFbo_);glGenTextures(1,&sceneColor_);}
        if(sceneW_!=w||sceneH_!=h){glBindTexture(GL_TEXTURE_2D,sceneColor_);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER,sceneFbo_);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,sceneColor_,0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Scaled scene framebuffer incomplete");sceneW_=w;sceneH_=h;INFO("RENDER_SCALE target=%dx%d surface=%dx%d",w,h,width_,height_);}
        glBindFramebuffer(GL_FRAMEBUFFER,sceneFbo_);
    }
    void resolveSceneTarget(int w,int h,const std::array<int,4>& rect){
        if(w==width_&&h==height_&&rect[2]==width_&&rect[3]==height_)return;
        glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width_,height_);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
        glBindFramebuffer(GL_READ_FRAMEBUFFER,sceneFbo_);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,0);
        glBlitFramebuffer(0,0,w,h,rect[0],rect[1],rect[0]+rect[2],rect[1]+rect[3],GL_COLOR_BUFFER_BIT,GL_LINEAR);glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
    // Original rumble requests drive the phone motor (Android adaptation; the
    // Windows build targets a gamepad). Disabled vibration records a no-op.
    JNIEnv* jni_=nullptr;jmethodID rumbleMethod_=nullptr;
    void rumble(float intensity,float seconds){
        if(!std::isfinite(intensity)||!std::isfinite(seconds)||intensity<=0||seconds<=0)return;
        if(usingController_&&ui_.settings.controllerRumble&&lastPadDevice_&&javaMethod(padRumbleMethod_,"controllerRumble","(IFF)Z")){
            const bool played=jni_->CallBooleanMethod(app_->activity->clazz,padRumbleMethod_,lastPadDevice_,std::clamp(intensity,0.f,1.f),seconds);
            if(jni_->ExceptionCheck())jni_->ExceptionClear();else if(played)return;
        }
        if(!ui_.settings.vibration)return;
        if(!jni_&&app_->activity->vm->AttachCurrentThread(&jni_,nullptr)!=JNI_OK){jni_=nullptr;return;}
        if(!rumbleMethod_){jclass type=jni_->GetObjectClass(app_->activity->clazz);rumbleMethod_=jni_->GetMethodID(type,"rumble","(FF)V");jni_->DeleteLocalRef(type);if(!rumbleMethod_){jni_->ExceptionClear();return;}}
        jni_->CallVoidMethod(app_->activity->clazz,rumbleMethod_,std::clamp(intensity,0.f,1.f),seconds);
        if(jni_->ExceptionCheck())jni_->ExceptionClear();
    }
    void paceFrame(){
        const int cap=effectiveFrameCap();
        if(cap!=hintedCap_&&app_->window){hintedCap_=cap;using SetRate=int32_t(*)(ANativeWindow*,float,int8_t);
            if(auto set=reinterpret_cast<SetRate>(dlsym(RTLD_DEFAULT,"ANativeWindow_setFrameRate")))set(app_->window,float(cap),0);}
        if(ui_.settings.vsync!=appliedVsync_){appliedVsync_=ui_.settings.vsync;eglSwapInterval(display_,appliedVsync_?1:0);INFO("VSYNC swap_interval=%d",appliedVsync_?1:0);}
        if(cap<=0){nextPresent_={};return;}
        const auto period=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(1./cap));
        const auto now=std::chrono::steady_clock::now();
        if(nextPresent_.time_since_epoch().count()==0||now-nextPresent_>period*2)nextPresent_=now;
        else if(now<nextPresent_)std::this_thread::sleep_until(nextPresent_);
        nextPresent_+=period;
    }
    std::array<float,4> appliedVolumes_{-1,-1,-1,-1};
    std::string savesDir_;bool continueLoad_=false;float saveProbe_=0;
    // Controllers: detection (Java InputDevice list), last-used input source,
    // original config options applied to scripts when they change.
    bool usingController_=false;int32_t lastPadDevice_=0;float controllerProbe_=0;jmethodID controllersMethod_=nullptr,padRumbleMethod_=nullptr;
    std::array<bool,7> appliedConfig_{};bool configApplied_=false;
    bool attachJni(){if(!jni_&&app_->activity->vm->AttachCurrentThread(&jni_,nullptr)!=JNI_OK){jni_=nullptr;return false;}return true;}
    jmethodID javaMethod(jmethodID& cache,const char* name,const char* signature){if(!cache&&attachJni()){jclass type=jni_->GetObjectClass(app_->activity->clazz);cache=jni_->GetMethodID(type,name,signature);jni_->DeleteLocalRef(type);if(!cache)jni_->ExceptionClear();}return cache;}
    void pollControllers(){
        if(!javaMethod(controllersMethod_,"connectedControllers","()Ljava/lang/String;"))return;
        auto names=static_cast<jstring>(jni_->CallObjectMethod(app_->activity->clazz,controllersMethod_));
        if(jni_->ExceptionCheck()){jni_->ExceptionClear();return;}
        std::string value;if(names){const char* chars=jni_->GetStringUTFChars(names,nullptr);if(chars){value=chars;jni_->ReleaseStringUTFChars(names,chars);}jni_->DeleteLocalRef(names);}
        if(value!=ui_.controllerName){INFO("CONTROLLERS connected=\"%s\"",value.c_str());ui_.controllerName=value;if(value.empty())usingController_=false;}
    }
    jmethodID gameModeMethod_=nullptr;int gameMode_=0;
    void pollGameMode(){if(!javaMethod(gameModeMethod_,"gameMode","()I"))return;const int mode=jni_->CallIntMethod(app_->activity->clazz,gameModeMethod_);if(jni_->ExceptionCheck()){jni_->ExceptionClear();return;}if(mode!=gameMode_){gameMode_=mode;INFO("GAME_MODE mode=%d",mode);}}
    // Battery game mode caps an uncapped display rate at 60 FPS; an explicit
    // user cap always wins (Android adaptation of GameManager modes).
    int effectiveFrameCap()const{const int cap=ui_.settings.frameCap;return cap==0&&gameMode_==3?60:cap;}
    bool touchVisible()const{return ui_.settings.touchMode==1||(ui_.settings.touchMode==0&&!usingController_);}
    void applyConfig(){
        if(!scripts_)return;const auto& s=ui_.settings;const std::array<bool,7> v{s.easyMode,s.damageNumbers,s.uiAnimations,s.speechBubbles,s.subtitles,s.bloom,s.cameraShake};
        if(configApplied_&&v==appliedConfig_)return;
        if(!scripts_->applyConfigOptions({{"EasyMode",v[0]},{"ShowDamageNumbers",v[1]},{"ShowUIAnimations",v[2]},{"ShowSpeechBubble",v[3]},{"Subtitles",v[4]},{"DrawBloom",v[5]},{"CameraShake",v[6]}}))throw std::runtime_error(scripts_->error());
        appliedConfig_=v;configApplied_=true;
    }
    bool saveExists()const{if(savesDir_.empty())return false;struct stat st{};for(const char* n:{"/Profile1.sav","/Profile1_Temp.sav"})if(::stat((savesDir_+n).c_str(),&st)==0&&S_ISREG(st.st_mode))return true;return false;}
    bool appliedVsync_=true;
    float dpi_=0,fpsWindow_=0,fpsWorst_=0;unsigned fpsFrames_=0,heroTraceFrames_=0;std::string fpsLine_;bool fpsWarn_=false;
    void readDensity(){if(app_&&app_->config){const int density=AConfiguration_getDensity(app_->config);dpi_=density>0&&density<2000?float(density):0;}}
    enum class LoadStage { Scene, Player, Scripts, Done, Failed };
    LoadStage loadStage_=LoadStage::Scene;
    bool sceneReady_=false;
    double loadSeconds_=0;
    std::string loadError_;
    bool focused_ = false, failed_ = false;
    bool firstFrame_ = true;
    int width_ = 0, height_ = 0;
    uint64_t frames_ = 0;
    double animationSeconds_ = 0,worldAccumulator_=0;
    std::array<float,480> activeFrameMs_{};
    size_t metricCount_=0,metricAt_=0;
    double updateMs_=0,submitMs_=0,presentMs_=0;
    uint64_t metricFrames_=0;
    std::chrono::steady_clock::time_point interval_ = std::chrono::steady_clock::now();
public:
    explicit Engine(android_app* app) : app_(app), assets_(app->activity->assetManager) {
        if(app->activity->internalDataPath){
            settingsPath_=std::string(app->activity->internalDataPath)+"/native-ui-settings.cfg";
            savesDir_=std::string(app->activity->internalDataPath)+"/Saves";::mkdir(savesDir_.c_str(),0700);
            const bool restored=ui_.loadSettings(settingsPath_);profileDir_=std::string(app->activity->internalDataPath);loadProfile();
            INFO("UI_SETTINGS_RESTORED loaded=%d brightness=%.3f camera=%.1f controlScale=%.3f opacity=%.3f leftHanded=%d",restored,ui_.settings.brightness,ui_.settings.cameraHeight,ui_.settings.touchScale,ui_.settings.touchOpacity,ui_.settings.leftHanded);
        }
    }
    ~Engine() { shutdown(); }
    bool active() const { return focused_ && display_ != EGL_NO_DISPLAY && !failed_; }
    void releaseMovement(){controls_.cancel();hwHeld_.clear();hwPressed_.clear();stickX_=stickY_=0;}
    // Hardware gamepad/keyboard with the original default bindings
    // (native/analysis/default-bindings-contract.json). Gameplay controls merge
    // with touch; one button may drive several controls, as in the original.
    hades::input::ControlBindings bindings_;bool bindingsLoaded_=false;
    // Original PC profile persistence (native/analysis/config-file-contract.json):
    // Profile1.sjson holds ConfigOptions (only values differing from the
    // recovered defaults); Profile1.ctrls holds RemappableControl records.
    // Android-only settings stay in native-ui-settings.cfg.
    hades::config::ConfigFile profile_;std::string profileDir_;bool bindingsEdited_=false;
    static bool readFile(const std::string& path,std::string& out){FILE* f=std::fopen(path.c_str(),"rb");if(!f)return false;char buf[8192];size_t n;out.clear();while((n=std::fread(buf,1,sizeof(buf),f))>0)out.append(buf,n);std::fclose(f);return true;}
    static bool writeFileAtomic(const std::string& path,const void* data,size_t size){const auto tmp=path+".tmp";FILE* f=std::fopen(tmp.c_str(),"wb");if(!f)return false;const bool ok=std::fwrite(data,1,size,f)==size;if(std::fclose(f)!=0||!ok)return false;return std::rename(tmp.c_str(),path.c_str())==0;}
    // PC Brightness is 0..1 (default .5); the native display exposure maps
    // .65..1.35 onto it so the defaults coincide (Android adaptation).
    void loadProfile(){
        std::string text;auto& s=ui_.settings;
        if(readFile(profileDir_+"/"+hades::config::profileConfigFileName(),text)){std::string error;if(!profile_.parse(text,&error)){ERROR("PROFILE_CONFIG_INVALID %s",error.c_str());profile_.resetDefaults();}}
        else{profileToConfig();return;} // first run: seed from current settings
        auto b=[&](const char* n,bool& v){if(auto x=profile_.getBool(n))v=*x;};auto f=[&](const char* n,float& v){if(auto x=profile_.getFloat(n))v=*x;};
        f("MasterVolume",s.masterVolume);f("MusicVolume",s.musicVolume);f("FxVolume",s.effectsVolume);f("DialogueVolume",s.voiceVolume);f("DeadZone",s.deadZone);
        b("Letterbox",s.letterbox);b("EasyMode",s.easyMode);b("ShowDamageNumbers",s.damageNumbers);b("Subtitles",s.subtitles);b("Rumble",s.controllerRumble);b("CameraShake",s.cameraShake);
        b("ShowUIAnimations",s.uiAnimations);b("ShowSpeechBubble",s.speechBubbles);b("DrawBloom",s.bloom);b("DrawLights",s.drawLights);
        if(auto x=profile_.getInt("SwapInterval"))s.vsync=*x!=0;
        if(auto x=profile_.getFloat("Brightness"))s.brightness=std::clamp(.65f+.7f*(*x),.65f,1.35f);
        for(auto* v:{&s.masterVolume,&s.musicVolume,&s.effectsVolume,&s.voiceVolume})*v=std::clamp(*v,0.f,1.f);s.deadZone=std::clamp(s.deadZone,.05f,.5f);
        std::string controls;
        if(hades::config::shouldLoadSavedControls(profile_)&&readFile(profileDir_+"/"+hades::config::profileControlsFileName(),controls)){
            std::vector<hades::config::ControlRecord> records;std::string error;
            if(hades::config::readControlsFile(reinterpret_cast<const uint8_t*>(controls.data()),controls.size(),records,&error))hades::input::applyControlRecords(bindings_,records);
            else ERROR("PROFILE_CONTROLS_INVALID %s",error.c_str());
        }
        bindingsLoaded_=true;INFO("PROFILE_LOADED options=%s controls=%d",hades::config::profileConfigFileName().c_str(),hades::config::shouldLoadSavedControls(profile_));
    }
    void profileToConfig(){
        const auto& s=ui_.settings;
        profile_.setFloat("MasterVolume",s.masterVolume);profile_.setFloat("MusicVolume",s.musicVolume);profile_.setFloat("FxVolume",s.effectsVolume);profile_.setFloat("DialogueVolume",s.voiceVolume);
        profile_.setFloat("DeadZone",s.deadZone);profile_.setBool("Letterbox",s.letterbox);profile_.setBool("EasyMode",s.easyMode);profile_.setBool("ShowDamageNumbers",s.damageNumbers);
        profile_.setBool("Subtitles",s.subtitles);profile_.setBool("Rumble",s.controllerRumble);profile_.setBool("CameraShake",s.cameraShake);profile_.setInt("SwapInterval",s.vsync?1:0);profile_.setFloat("Brightness",(s.brightness-.65f)/.7f);
        profile_.setBool("ShowUIAnimations",s.uiAnimations);profile_.setBool("ShowSpeechBubble",s.speechBubbles);profile_.setBool("DrawBloom",s.bloom);profile_.setBool("DrawLights",s.drawLights);
    }
    void saveProfile(){
        if(profileDir_.empty())return;profileToConfig();
        if(bindingsEdited_)hades::config::markControlsEdited(profile_);
        // PC never persists DrawBloom/DrawLights/ShowUIAnimations/ShowSpeechBubble
        // (flag "not saved"); this port writes them because its menu edits them.
        hades::config::ConfigFile::WriteOptions options;options.includeNotSavedOptions=true;
        const auto text=profile_.serialize(options);
        if(!writeFileAtomic(profileDir_+"/"+hades::config::profileConfigFileName(),text.data(),text.size()))ERROR("PROFILE_CONFIG_SAVE_FAILED");
        if(bindingsEdited_){const auto bytes=hades::config::writeControlsFile(hades::input::toControlRecords(bindings_));
            if(!writeFileAtomic(profileDir_+"/"+hades::config::profileControlsFileName(),bytes.data(),bytes.size()))ERROR("PROFILE_CONTROLS_SAVE_FAILED");}
    }
    void syncBindings(){
        if(ui_.takeRemapReset()){bindings_.resetDefaults();bindingsEdited_=true;ui_.setBindings(bindings_.serialize());}
        if(ui_.screen()!=hades::UiScreen::Remap)return;
        ui_.remapRows.clear();
        for(const auto& row:hades::input::remapRows()){const auto* b=bindings_.find(row.control);
            ui_.remapRows.push_back({row.label,b?hades::input::padButtonName(b->gamepadButton):"-",b?(b->keys[0]<0&&b->mouseButton>=0?std::string(b->mouseButton==0?"MOUSE LEFT":b->mouseButton==1?"MOUSE RIGHT":"MOUSE"):hades::input::keyName(b->keys[0])):"-",row.gamepad,row.keyboard});}
    }
    // Remap capture: the next physical button/key is assigned to the row.
    bool captureBinding(bool pad,std::optional<int32_t> code){
        if(ui_.screen()!=hades::UiScreen::Remap||ui_.remapCaptureRow<0)return false;
        const auto& rows=hades::input::remapRows();if(size_t(ui_.remapCaptureRow)>=rows.size())return false;
        if(!code||pad!=(ui_.remapCaptureDevice==0))return true; // consume, keep waiting for the right device
        const auto control=rows[ui_.remapCaptureRow].control;
        if(pad)bindings_.assignPad(control,*code);else bindings_.assignKey(control,*code);
        INFO("BINDING_ASSIGNED control=%s %s=%d",control,pad?"pad":"key",*code);
        ui_.remapCaptureRow=-1;bindingsEdited_=true;ui_.setBindings(bindings_.serialize());return true;
    }
    std::set<std::string> hwHeld_,hwPressed_;float stickX_=0,stickY_=0;std::array<bool,4> hwAxisButtons_{};
    static bool gameplayControl(std::string_view n){for(auto c:{"Attack1","Attack2","Attack3","Rush","Use","Shout","Assist"})if(n==c)return true;return false;}
    void hardwareControl(std::string_view control,bool down){
        const std::string name(control);
        if(down){if(!hwHeld_.contains(name)){hwPressed_.insert(name);hwHeld_.insert(name);
            if(name=="Pause"&&(ui_.playing()||ui_.screen()==hades::UiScreen::Paused)){ui_.back();releaseMovement();}
            else if(name=="Cancel"&&!ui_.playing()){ui_.back();releaseMovement();}}}
        else hwHeld_.erase(name);
    }
    void hardwareControls(const hades::input::ControlList& list,bool down){for(size_t i=0;i<list.count;++i)hardwareControl(list.names[i],down);}
    float hardwareDx()const{float x=stickX_;if(hwHeld_.contains("Left"))x-=1;if(hwHeld_.contains("Right"))x+=1;return x;}
    float hardwareDy()const{float y=-stickY_;if(hwHeld_.contains("Up"))y-=1;if(hwHeld_.contains("Down"))y+=1;return y;}
    std::vector<std::string> mergedControls(){
        auto names=controls_.takeControls();
        for(const auto* set:{&hwHeld_,&hwPressed_})for(const auto& n:*set)if(gameplayControl(n)&&std::find(names.begin(),names.end(),n)==names.end())names.push_back(n);
        hwPressed_.clear();return names;
    }
    std::array<float,2> mergedMove()const{
        if(controls_.active())return {controls_.dx(),controls_.dy()};
        float x=hardwareDx(),y=hardwareDy();const float m=std::hypot(x,y);if(m>1){x/=m;y/=m;}return {x,y};
    }
    int32_t input(AInputEvent* event) {
        if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY){
            const int32_t key=AKeyEvent_getKeyCode(event);
            if(key==AKEYCODE_BACK){if(AKeyEvent_getAction(event)==AKEY_EVENT_ACTION_UP){ui_.back();releaseMovement();}return 1;}
            const int32_t keyAction=AKeyEvent_getAction(event);if(keyAction!=AKEY_EVENT_ACTION_DOWN&&keyAction!=AKEY_EVENT_ACTION_UP)return 0;
            if(AKeyEvent_getRepeatCount(event)>0)return 1;
            const int32_t source=AInputEvent_getSource(event);const bool pad=(source&AINPUT_SOURCE_GAMEPAD)==AINPUT_SOURCE_GAMEPAD||(source&AINPUT_SOURCE_JOYSTICK)==AINPUT_SOURCE_JOYSTICK;
            if(keyAction==AKEY_EVENT_ACTION_DOWN){const auto padButton=hades::input::padButtonFromAndroid(key);
                if(captureBinding(pad,pad?(padButton?std::optional<int32_t>(int32_t(*padButton)):std::nullopt):hades::input::engineKeyFromAndroid(key)))return 1;}
            hades::input::ControlList list;
            if(pad){if(const auto b=hades::input::padButtonFromAndroid(key))list=bindings_.padControls(*b);}
            else if(const auto k=hades::input::engineKeyFromAndroid(key))list=bindings_.keyControls(*k);
            if(!list.count)return 0;
            if(pad){usingController_=true;lastPadDevice_=AInputEvent_getDeviceId(event);}
            if(keyAction==AKEY_EVENT_ACTION_DOWN&&scripts_&&ui_.playing())scripts_->notePlayerInput();
            hardwareControls(list,keyAction==AKEY_EVENT_ACTION_DOWN);return 1;
        }
        if(AInputEvent_getType(event)!=AINPUT_EVENT_TYPE_MOTION)return 0;
        if((AInputEvent_getSource(event)&AINPUT_SOURCE_JOYSTICK)==AINPUT_SOURCE_JOYSTICK){
            hades::input::GamepadAxes axes;axes.deadZone=ui_.settings.deadZone;const auto stick=axes.leftStick(AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_X,0),AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_Y,0));
            stickX_=stick.x;stickY_=stick.y;if(stick.x!=0||stick.y!=0){usingController_=true;lastPadDevice_=AInputEvent_getDeviceId(event);if(scripts_&&ui_.playing())scripts_->notePlayerInput();}
            const bool l2=axes.triggerPressed(AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_LTRIGGER,0)),r2=axes.triggerPressed(AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_RTRIGGER,0));
            const auto edge=[&](size_t slot,bool now,hades::input::PadButton button){if(now!=hwAxisButtons_[slot]){hwAxisButtons_[slot]=now;if(now&&captureBinding(true,int32_t(button)))return;hardwareControls(bindings_.padControls(button),now);}};
            edge(0,l2,hades::input::PadButton::L2);edge(1,r2,hades::input::PadButton::R2);
            const float hx=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_HAT_X,0),hy=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_HAT_Y,0);
            // Hat as D-pad: track horizontal/vertical presses independently.
            static constexpr float hat=hades::input::GamepadAxes::kHatPressed;
            const bool left=hx<=-hat,right=hx>=hat,up=hy<=-hat,down=hy>=hat;
            if(hwAxisButtons_[2]!=(left||right)||hwHeld_.contains("Left")!=left||hwHeld_.contains("Right")!=right){hwAxisButtons_[2]=left||right;hardwareControl("Left",left);hardwareControl("Right",right);}
            if(hwAxisButtons_[3]!=(up||down)||hwHeld_.contains("Up")!=up||hwHeld_.contains("Down")!=down){hwAxisButtons_[3]=up||down;hardwareControl("Up",up);hardwareControl("Down",down);}
            return 1;
        }
        const int packed=AMotionEvent_getAction(event),action=packed&AMOTION_EVENT_ACTION_MASK;
        const size_t count=AMotionEvent_getPointerCount(event);
        if(action==AMOTION_EVENT_ACTION_CANCEL){ui_.cancel();releaseMovement();return 1;}
        auto point=[&](size_t index,hades::UiPointer phase){
            const int id=AMotionEvent_getPointerId(event,index);
            const float x=AMotionEvent_getX(event,index),y=AMotionEvent_getY(event,index);
            if(ui_.screen()==hades::UiScreen::LayoutEdit){
                controls_.configure(width_,height_,ui_.settings,dpi_);
                if(controls_.editPointer(phase,id,x,y)){std::array<float,16> layout;if(controls_.takeLayout(layout))ui_.setTouchLayout(layout);return;}
            }
            const bool consumed=ui_.pointer(phase,id,x,y,width_,height_);
            if(consumed){if(!ui_.playing())releaseMovement();return;}
            if(!ui_.playing()||!sceneReady_)return;
            controls_.configure(width_,height_,ui_.settings,dpi_);
            if(phase==hades::UiPointer::Down){if(scripts_)scripts_->notePlayerInput();usingController_=false;}
            if(ui_.settings.touchMode==2)return; // touch controls hidden by setting
            const bool wasMoving=controls_.active();
            controls_.pointer(phase,id,x,y);
            if(wasMoving&&!controls_.active())INFO("PLAYER_TOUCH_END");
            if(const auto zoom=controls_.takeZoom()){
                ui_.cameraHeight(camera_.zoom(*zoom,width_,height_));
                INFO("CAMERA_PINCH targetHeight=%.2f stickActive=%d",ui_.settings.cameraHeight,controls_.active());
            }
        };
        if(action==AMOTION_EVENT_ACTION_MOVE){for(size_t i=0;i<count;++i)point(i,hades::UiPointer::Move);}
        else {
            const size_t index=(packed&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
            if(index<count)point(index,(action==AMOTION_EVENT_ACTION_UP||action==AMOTION_EVENT_ACTION_POINTER_UP)?hades::UiPointer::Up:hades::UiPointer::Down);
        }
        return 1;
    }
    void shutdown() {
        if (display_ == EGL_NO_DISPLAY) return;
        if(scripts_)scripts_->stopAudio();
        uiRenderer_.shutdown();
        colorGradingRenderer_.shutdown();bloomRenderer_.shutdown();
        renderer_.shutdown();
        if(sceneFbo_){glDeleteFramebuffers(1,&sceneFbo_);glDeleteTextures(1,&sceneColor_);sceneFbo_=sceneColor_=0;sceneW_=sceneH_=0;}hintedCap_=-1;
        sceneReady_=false;loadStage_=LoadStage::Scene;loadSeconds_=0;ui_.loadingProgress=0;ui_.roomReady(false);
        if(ui_.playing())ui_.show(hades::UiScreen::Loading);
        releaseMovement();camera_.reset();
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display_, context_); eglDestroySurface(display_, surface_); eglTerminate(display_);
        display_ = EGL_NO_DISPLAY; surface_ = EGL_NO_SURFACE; context_ = EGL_NO_CONTEXT;
        INFO("Surface released");
    }
    void advanceLoading() {
        if(ui_.takeContinue()){continueLoad_=true;if(scripts_)scripts_->stopAudio();loadStage_=LoadStage::Failed;} // full teardown, then load
        if(ui_.screen()==hades::UiScreen::Title&&(saveProbe_-=1.f/60)<=0){saveProbe_=1;ui_.saveAvailable=saveExists();}
        if(!ui_.loading())return;
        if(loadStage_==LoadStage::Failed){
            colorGradingRenderer_.shutdown();bloomRenderer_.shutdown();
            renderer_.shutdown();sceneReady_=false;
            scripts_.reset();loadStage_=LoadStage::Scene;loadError_.clear();
        }
        const auto started=std::chrono::steady_clock::now();
        const int stage=static_cast<int>(loadStage_);
        try {
            if(loadStage_==LoadStage::Scene){renderer_.initialize(assets_);sceneReady_=true;loadStage_=LoadStage::Player;}
            else if(loadStage_==LoadStage::Player){
                if(!heroBank_){heroBank_=std::make_shared<hades::ActorAnimationBank>();heroBank_->load(assets_);}
                loadStage_=LoadStage::Scripts;
            }
            else if(loadStage_==LoadStage::Scripts){
                // Never resume a runtime whose world already faulted (e.g. surface
                // lost after a failure): it would fail again with an empty reason.
                if(scripts_&&scripts_->worldFaulted()){scripts_->stopAudio();scripts_.reset();}
                if(!scripts_){appliedVolumes_={-1,-1,-1,-1};scripts_=std::make_unique<ScriptRuntime>(&assets_);scripts_->setAnimationBank(heroBank_);
                    scripts_->setPhysicsRumble([this](int32_t,float intensity,float seconds,std::string&){rumble(intensity,seconds);return true;});
                    scripts_->setSaveDirectory(savesDir_);scripts_->initialize();
                    // Durability: contain unreconstructed behaviour (logged once as
                    // CONTAINED_FAULT/SURVEY_BLOCKER, counted on screen) instead of
                    // ending the room. Load failures still use the failure screen.
                    scripts_->setFaultContainment(true);{std::string containError;if(!scripts_->enableBlockerSurvey(containError))ERROR("FAULT_CONTAINMENT_UNAVAILABLE %s",containError.c_str());}configApplied_=false;applyConfig();
                    if(continueLoad_){continueLoad_=false;if(!scripts_->loadSave())throw std::runtime_error("Continue: "+scripts_->lastSaveError());INFO("SAVE_LOADED path=%s",scripts_->loadedSavePath().c_str());}}
                if(!scripts_->gameLoaded()||!scripts_->bootRoom()||!scripts_->activateRoom())throw std::runtime_error(scripts_->error());
                renderer_.setSourceSceneProvider([this](uint32_t i,std::optional<hades::SceneFrameSelection>& frame,std::string& error){return scripts_->scenePrimaryFrame(i,frame,error);},[this](uint32_t i,std::string& error){return scripts_->scenePrimarySubmitted(i,error);},[this](uint32_t i,hades::SceneSprite& sprite,std::string& error){return scripts_->sourceSceneSprite(i,sprite,error);});
                if(focused_&&!scripts_->startAudio())throw std::runtime_error(scripts_->error());
                worldAccumulator_=0;
                loadStage_=LoadStage::Done;camera_.reset();ui_.roomReady(true);
            }
            ui_.loadingProgress=static_cast<int>(loadStage_)/3.f;
            const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();
            loadSeconds_+=elapsed;
            INFO("ROOM_LOAD_STAGE stage=%d elapsed_ms=%.1f ready=%d",stage,elapsed*1000,loadStage_==LoadStage::Done);
            if(loadStage_==LoadStage::Done)INFO("NATIVE_WORLD_READY load_ms=%.1f original_map_begin=true full_phase_coverage=false",loadSeconds_*1000);
        }catch(const std::exception& error){
            loadError_=error.what();loadStage_=LoadStage::Failed;ui_.failureDetail=loadError_;ui_.loadFailed();releaseMovement();
            ERROR("ROOM_LOAD_FAILED stage=%d reason=%s",stage,loadError_.c_str());
        }
        lastFrame_=std::chrono::steady_clock::now();
    }
    void initialize() {
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (!eglInitialize(display_, nullptr, nullptr)) throw std::runtime_error("EGL initialization failed");
        const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config; EGLint count = 0;
        if (!eglChooseConfig(display_, attributes, &config, 1, &count) || count != 1)
            throw std::runtime_error("No GLES3 EGL config");
        EGLint visual; eglGetConfigAttrib(display_, config, EGL_NATIVE_VISUAL_ID, &visual);
        ANativeWindow_setBuffersGeometry(app_->window, 0, 0, visual);
        const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttributes);
        surface_ = eglCreateWindowSurface(display_, config, app_->window, nullptr);
        if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE ||
            !eglMakeCurrent(display_, surface_, surface_, context_)) throw std::runtime_error("EGL context creation failed");
        eglSwapInterval(display_, 1);appliedVsync_=true;
        INFO("NATIVE_BOOT abi=arm64-v8a renderer=%s version=%s", glGetString(GL_RENDERER), glGetString(GL_VERSION));
        uiRenderer_.initialize(assets_);readDensity();INFO("DISPLAY_DENSITY dpi=%.0f",dpi_);
        INFO("TITLE_READY room_assets_loaded=false");
        frames_ = 0; firstFrame_ = true; interval_ = std::chrono::steady_clock::now();
    }
    void frame() {
        if (!active()) return;
        try {
        const auto frameTime=std::chrono::steady_clock::now();
        const float dt=std::chrono::duration<float>(frameTime-lastFrame_).count();lastFrame_=frameTime;
        eglQuerySurface(display_, surface_, EGL_WIDTH, &width_); eglQuerySurface(display_, surface_, EGL_HEIGHT, &height_);
        if(width_<=0||height_<=0)return;
        if(scripts_&&scripts_->roomActivated()){
            std::string detail;const auto rect=sceneRect();const uint32_t logicalWidth=uint32_t(std::lround(1080.*rect[2]/rect[3]));
            if(!scripts_->setCameraViewport(logicalWidth,1080,detail))throw std::runtime_error(detail);
        }
        if(ui_.playing()&&scripts_&&scripts_->roomActivated()){
            worldAccumulator_+=std::clamp(double(dt),0.,.25);
            // Bounded fixed-step Android adaptation; slow emulator rendering
            // cannot trigger an unbounded simulation catch-up loop.
            for(unsigned step=0;worldAccumulator_>=1./60&&step<8;++step){
                std::string inputError;
                const auto move=mergedMove();
                if(!scripts_->setLocalInput(move[0],move[1],mergedControls(),inputError))throw std::runtime_error(inputError);
                if(!scripts_->advanceWorld(1.f/60,1./60)){
                    loadError_=scripts_->error();if(loadError_.empty())loadError_="World update stopped without a reason ("+std::to_string(scripts_->containedFaults())+" contained faults; last: "+scripts_->lastContainedFault()+")";scripts_->stopAudio();ui_.failureDetail=loadError_;ui_.loadFailed();loadStage_=LoadStage::Failed;releaseMovement();ERROR("NATIVE_WORLD_UPDATE_FAILED reason=%s",loadError_.c_str());break;
                }
                worldAccumulator_-=1./60;
            }
            if(worldAccumulator_>=1./60){INFO("WORLD_CATCHUP_LIMIT retained_ms=%.2f",worldAccumulator_*1000);worldAccumulator_=std::min(worldAccumulator_,1./60);}
        }
        if((controllerProbe_-=dt)<=0){controllerProbe_=2;pollControllers();pollGameMode();}
        syncBindings();
        if(scripts_&&scripts_->gameLoaded())applyConfig();
        if(scripts_&&scripts_->roomActivated()){const auto& s=ui_.settings;const std::array<float,4> v{s.masterVolume,s.musicVolume,s.effectsVolume,s.voiceVolume};
            if(v!=appliedVolumes_){if(!scripts_->setAudioVolumes(v[0],v[1],v[2],v[3]))throw std::runtime_error(scripts_->error());appliedVolumes_=v;}}
        {const float target=!touchVisible()?0.f:((scripts_&&scripts_->roomActivated()&&!scripts_->inputAllowed())?.15f:1.f);
            ui_.controlsVisibility+=(target-ui_.controlsVisibility)*(1.f-std::exp(-std::clamp(dt,0.f,.25f)/.25f));}
        const auto updateDone=std::chrono::steady_clock::now();
        controls_.configure(width_,height_,ui_.settings,dpi_);
        if(ui_.takeDirty()&&!settingsPath_.empty()){
            saveProfile();
            if(!ui_.saveSettings(settingsPath_))ERROR("UI_SETTINGS_SAVE_FAILED");
            else INFO("UI_SETTINGS_SAVED brightness=%.3f camera=%.1f controlScale=%.3f opacity=%.3f leftHanded=%d",ui_.settings.brightness,ui_.settings.cameraHeight,ui_.settings.touchScale,ui_.settings.touchOpacity,ui_.settings.leftHanded);
        }
        if(ui_.playing())animationSeconds_+=std::clamp(dt,0.f,.25f);
        if(sceneReady_&&scripts_&&scripts_->roomActivated()&&!ui_.loading()&&ui_.screen()!=hades::UiScreen::Title&&ui_.screen()!=hades::UiScreen::LoadFailed){
            auto options=renderer_.options();options.displayExposure=ui_.settings.brightness;options.drawLights=ui_.settings.drawLights;renderer_.setOptions(options);
            const auto player=scripts_->localPlayerBody();if(!player)throw std::runtime_error("Actual local Player body unavailable");
            const auto& sourceCamera=scripts_->cameraState();
            if(sourceCamera.initialized)camera_.updateSource(dt,sourceCamera.x+sourceCamera.shakeOffsetX(),sourceCamera.y+sourceCamera.shakeOffsetY(),sourceCamera.zoom,ui_.settings.cameraHeight,controls_.pinching());
            else camera_.update(dt,player->x,player->y,ui_.settings.cameraHeight,ui_.playing(),controls_.pinching());
            uint32_t effectGroup=UINT32_MAX;bool drawColor=false,drawBloom=false;std::string effectError;if(!scripts_->fullscreenPresentation(effectGroup,drawColor,drawBloom,effectError))throw std::runtime_error(effectError);
            renderer_.setFullscreenEffects(effectGroup,[this,drawColor,drawBloom](int width,int height){if(drawColor)colorGradingRenderer_.render(scripts_->colorGrading(),width,height);bloomRenderer_.render(scripts_->bloom(),drawBloom,width,height);});
            const auto visuals=scripts_->presentationVisuals();
            const auto rect=sceneRect();
            const float scale=std::clamp(ui_.settings.renderScale,.5f,1.f);const int sw=std::max(1,int(std::lround(rect[2]*scale))),sh=std::max(1,int(std::lround(rect[3]*scale)));
            bindSceneTarget(sw,sh,rect[2]!=width_||rect[3]!=height_);
            renderer_.renderActors(camera_.view(rect[2],rect[3]),sw,sh,visuals.size(),[this,&visuals](size_t i,hades::ActorVisual& visual,std::string& error){return scripts_->samplePresentation(visuals.at(i),visual,error);},animationSeconds_);
            if(scripts_->textFonts())uiRenderer_.renderText(scripts_->textVisuals(),*scripts_->textFonts(),sw,sh,camera_.view(rect[2],rect[3]));
            resolveSceneTarget(sw,sh,rect);
        }else {glViewport(0,0,width_,height_);glClearColor(.012f,.018f,.02f,1);glClear(GL_COLOR_BUFFER_BIT);}
        const float scale=height_/1080.f;
        if(ui_.playing()&&scripts_&&scripts_->roomActivated())uiRenderer_.renderCameraFade(scripts_->cameraFadeColor(),width_,height_);
        uiRenderer_.render(ui_,width_,height_,animationSeconds_,controls_.dx(),controls_.dy(),controls_.active(),controls_.x()/scale,controls_.y()/scale,&controls_);
        const auto submitDone=std::chrono::steady_clock::now();
        if(ui_.playing()||ui_.screen()==hades::UiScreen::Paused){
            fpsFrames_++;fpsWindow_+=dt;fpsWorst_=std::max(fpsWorst_,dt);
            if(fpsWindow_>=.5f){char line[96];std::snprintf(line,sizeof(line),"%.0f FPS  %.1f ms  worst %.1f",fpsFrames_/fpsWindow_,1000.f*fpsWindow_/fpsFrames_,fpsWorst_*1000);fpsLine_=line;fpsWarn_=fpsFrames_/fpsWindow_<50;fpsFrames_=0;fpsWindow_=fpsWorst_=0;}
            // Low-rate motion trace for device diagnosis (every ~0.5 s of play).
            if(scripts_&&(++heroTraceFrames_%60)==0)INFO("HERO_TRACE %s",scripts_->heroMotionTrace().c_str());
            const size_t gaps=scripts_?scripts_->containedFaults():0;
            if(ui_.settings.showFps)uiRenderer_.renderFrameCounter(gaps?fpsLine_+"  "+std::to_string(gaps)+" engine gaps":fpsLine_,width_,height_,fpsWarn_||gaps);
            else if(gaps)uiRenderer_.renderFrameCounter(std::to_string(gaps)+" engine gaps",width_,height_,true);
        }
        paceFrame();
        if (!eglSwapBuffers(display_, surface_)) { ERROR("Swap failed EGL=%x", eglGetError()); failed_ = true; return; }
        const auto presented=std::chrono::steady_clock::now();
        if(ui_.playing()&&scripts_&&scripts_->roomActivated()){
            activeFrameMs_[metricAt_]=dt*1000;metricAt_=(metricAt_+1)%activeFrameMs_.size();metricCount_=std::min(metricCount_+1,activeFrameMs_.size());++metricFrames_;
            updateMs_+=std::chrono::duration<double,std::milli>(updateDone-frameTime).count();
            submitMs_+=std::chrono::duration<double,std::milli>(submitDone-updateDone).count();
            presentMs_+=std::chrono::duration<double,std::milli>(presented-submitDone).count();
        }
        advanceLoading();
        ++frames_;
        if (firstFrame_) { INFO("FIRST_FRAME size=%dx%d sprites=%zu", width_, height_, renderer_.scene().sprites.size()); firstFrame_ = false; }
        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - interval_).count();
        if(seconds>=10.){
            INFO("RENDER_INTERVAL frames=%llu seconds=%.3f",static_cast<unsigned long long>(frames_),seconds);
            if(metricFrames_&&metricCount_){
                auto sorted=activeFrameMs_;std::sort(sorted.begin(),sorted.begin()+metricCount_);const auto& stats=renderer_.stats();
                INFO("ACTIVE_FRAME_PERF frames=%llu interval_p50_ms=%.2f interval_p95_ms=%.2f update_mean_ms=%.3f submit_mean_ms=%.3f present_mean_ms=%.3f draw_calls=%u visible=%u uploads=%u upload_bytes=%llu vertex_bytes=%llu",static_cast<unsigned long long>(metricFrames_),sorted[metricCount_/2],sorted[std::min(metricCount_-1,size_t(metricCount_*.95))],updateMs_/metricFrames_,submitMs_/metricFrames_,presentMs_/metricFrames_,stats.drawCalls,stats.visibleSprites,stats.frameUploads,static_cast<unsigned long long>(stats.frameUploadBytes),static_cast<unsigned long long>(stats.vertexBytes));
                if(scripts_&&scripts_->roomActivated()){const auto fade=scripts_->cameraFadeColor();INFO("PRESENTATION_STATE fade=%.3f,%.3f,%.3f,%.3f exposure=%.3f actor_cache_bytes=%llu actor_cached=%u dpi=%.0f",fade[0],fade[1],fade[2],fade[3],ui_.settings.brightness,static_cast<unsigned long long>(stats.actorCacheBytes),stats.actorCachedFrames,dpi_);
                    const auto audio=scripts_->audioStats();INFO("AUDIO_OUTPUT running=%d rate=%d burst=%d submitted=%llu underrun_frames=%llu",audio.running,audio.deviceRate,audio.framesPerBurst,static_cast<unsigned long long>(audio.framesSubmitted),static_cast<unsigned long long>(audio.underrunFrames));}
                if(heroBank_){const auto& cache=heroBank_->stats();INFO("HERO_FRAME_CACHE frames=%zu bytes=%zu inflates=%llu inflate_ms=%.3f",cache.cachedFrames,cache.cachedBytes,static_cast<unsigned long long>(cache.misses),cache.inflateMilliseconds);}
            }
            frames_=0;interval_=now;metricCount_=metricAt_=0;metricFrames_=0;updateMs_=submitMs_=presentMs_=0;
        }
        } catch(const std::exception& failure) {
            ERROR("ENGINE_FRAME_FAILURE reason=%s",failure.what());
            // Surface room-level failures with their exact reason and Retry
            // instead of closing the app. A failure while already showing the
            // failure/title screen still ends the activity.
            if(ui_.screen()!=hades::UiScreen::LoadFailed&&ui_.screen()!=hades::UiScreen::Title){
                loadError_=failure.what();if(scripts_)scripts_->stopAudio();ui_.failureDetail=loadError_;ui_.loadFailed();loadStage_=LoadStage::Failed;releaseMovement();
                glBindFramebuffer(GL_FRAMEBUFFER,0);while(glGetError()!=GL_NO_ERROR){}
            }else{failed_=true;ANativeActivity_finish(app_->activity);}
        }
    }
    void command(int32_t cmd) {
        try {
            if (cmd == APP_CMD_INIT_WINDOW && app_->window && display_ == EGL_NO_DISPLAY) { failed_ = false; initialize(); }
            else if (cmd == APP_CMD_TERM_WINDOW) shutdown();
            else if (cmd == APP_CMD_CONFIG_CHANGED) { readDensity(); INFO("DISPLAY_DENSITY dpi=%.0f",dpi_); }
            else if (cmd == APP_CMD_GAINED_FOCUS) { focused_ = true;if(scripts_&&scripts_->roomActivated()&&!scripts_->startAudio())throw std::runtime_error(scripts_->error());lastFrame_=std::chrono::steady_clock::now();interval_=lastFrame_;frames_=0;metricCount_=metricAt_=0;metricFrames_=0;updateMs_=submitMs_=presentMs_=0; INFO("Resumed"); }
            else if (cmd == APP_CMD_LOST_FOCUS) { focused_ = false;if(scripts_)scripts_->stopAudio();ui_.cancel();releaseMovement(); INFO("Paused"); }
        } catch (const std::exception& failure) {
            ERROR("ENGINE_FAILURE stage=%d reason=%s", cmd, failure.what()); failed_ = true;
            ANativeActivity_finish(app_->activity);
        }
    }
};
} // namespace

void android_main(android_app* app) {
    // The rumble bridge attaches this thread lazily; ART aborts if an attached
    // native thread exits without detaching.
    struct DetachOnExit {JavaVM* vm;~DetachOnExit(){JNIEnv* env=nullptr;if(vm->GetEnv(reinterpret_cast<void**>(&env),JNI_VERSION_1_6)==JNI_OK)vm->DetachCurrentThread();}} detach{app->activity->vm};
    Engine engine(app); app->userData = &engine;
    app->onAppCmd = [](android_app* state, int32_t command) { static_cast<Engine*>(state->userData)->command(command); };
    app->onInputEvent = [](android_app* state,AInputEvent* event) { return static_cast<Engine*>(state->userData)->input(event); };
    while (!app->destroyRequested) {
        int events; android_poll_source* source;
        int result;
        do {
            result = ALooper_pollOnce(engine.active() ? 0 : -1, nullptr, &events, reinterpret_cast<void**>(&source));
            if (result >= 0 && source) source->process(app, source);
            if (app->destroyRequested) return;
        } while (result >= 0);
        engine.frame();
    }
}
