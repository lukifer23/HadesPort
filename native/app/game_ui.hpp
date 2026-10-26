#pragma once
#include <array>
#include <string>
#include <vector>

namespace hades {
enum class UiScreen { Title, Loading, LoadFailed, Playing, Paused, Settings, LayoutEdit, Remap };
enum class UiPointer { Down, Move, Up, Cancel };
struct UiRect {
    float x=0,y=0,w=0,h=0;
    bool contains(float px,float py)const{return px>=x&&px<=x+w&&py>=y&&py<=y+h;}
};
struct UiSettings {
    float brightness=1,cameraHeight=1680,touchScale=1,touchOpacity=.65f;
    bool leftHanded=false,floatingStick=true;
    // Graphics (Android adaptation): internal scene resolution fraction,
    // frame-rate cap (0 = display rate) and the performance overlay.
    float renderScale=1;int frameCap=0;bool showFps=true,vibration=true;
    // Original ConfigOptions Master/Music/Fx/DialogueVolume (default 1.0).
    float masterVolume=1,musicVolume=1,effectsVolume=1,voiceVolume=1;
    // Touch layout editor offsets (mm): 7 action buttons + movement stick rest.
    std::array<float,16> touchLayout{};
    // Original ConfigOptions (recovered defaults; applied via SetConfigOption).
    bool easyMode=false,damageNumbers=true,uiAnimations=true,speechBubbles=true,subtitles=true,bloom=true,cameraShake=true;
    // Controller: touch visibility (0 auto, 1 always, 2 hidden), original
    // DeadZone 0.225 (0x3e666666), rumble on the controller's own motors.
    int touchMode=0;float deadZone=.225f;bool controllerRumble=true;
    // Original ConfigOptions DrawLights (true) and SwapInterval (1). Letterbox
    // defaults true on PC; this Android adaptation defaults to fill the panel.
    bool drawLights=true,vsync=true,letterbox=false;
    std::string bindings; // in-memory change marker only; persisted in Profile1.ctrls
};
enum class SettingsTab { Graphics, Display, Audio, Gameplay, Controls, Controller };
struct UiControl {
    enum class Kind { Button, Slider, Toggle, Tab, BindCell } kind=Kind::Button;
    int id=0;
    std::string label,value;
    UiRect bounds;
    float fraction=0;
};
// Android adaptation of menu navigation and movement-control layout. Original
// menu assets/fonts are retained; this is not the original Lua GUI interpreter.
class GameUi {
    UiScreen screen_=UiScreen::Title,returnFromSettings_=UiScreen::Title;
    SettingsTab tab_=SettingsTab::Graphics;
    int pointer_=-1,pressed_=-1;
    bool dirty_=false;
    bool roomReady_=false,continueRequested_=false,newRunRequested_=false,remapReset_=false;
    void activate(int id);
    void slide(int id,float fraction);
public:
    UiSettings settings;
    float loadingProgress=0;
    std::string failureDetail;
    bool saveAvailable=false;
    std::string controllerName; // connected physical controllers ("" = none)
    // Controls (remap) screen: engine fills one row per remappable control
    // {label, gamepad binding, keyboard binding, has gamepad cell, has key cell}.
    struct RemapDisplay {std::string label,pad,key;bool padCell=true,keyCell=true;};
    std::vector<RemapDisplay> remapRows;
    int remapCaptureRow=-1,remapCaptureDevice=0; // device 0 gamepad, 1 keyboard
    bool takeRemapReset(){const bool v=remapReset_;remapReset_=false;return v;} // original Continue rule: Profile1.sav or Profile1_Temp.sav exists
    bool takeContinue(){const bool v=continueRequested_;continueRequested_=false;return v;}
    bool takeNewRun(){const bool v=newRunRequested_;newRunRequested_=false;return v;}
    float controlsVisibility=1; // eased toward 0 while original scripts block input // exact engine stop reason shown on LoadFailed
    UiScreen screen()const{return screen_;}
    SettingsTab settingsTab()const{return tab_;}
    void show(UiScreen value){screen_=value;cancel();}
    bool playing()const{return screen_==UiScreen::Playing;}
    bool loading()const{return screen_==UiScreen::Loading;}
    void roomReady(bool ready){roomReady_=ready;if(ready&&loading())show(UiScreen::Playing);}
    void loadFailed(){roomReady_=false;show(UiScreen::LoadFailed);}
    void cameraHeight(float value);
    void setBindings(std::string text){if(text!=settings.bindings){settings.bindings=std::move(text);dirty_=true;}}
    void setTouchLayout(const std::array<float,16>& layout){if(layout!=settings.touchLayout){settings.touchLayout=layout;dirty_=true;}}
    int pressed()const{return pressed_;}
    bool takeDirty(){const bool value=dirty_;dirty_=false;return value;}
    void cancel(){pointer_=-1;pressed_=-1;}
    void back();
    bool handleBackControls(const std::vector<std::string>& pressed);
    std::vector<UiControl> controls(float virtualWidth)const;
    // Coordinates in real surface pixels; all UI layout uses a 1080-high canvas.
    bool pointer(UiPointer,int id,float x,float y,int width,int height);
    bool loadSettings(const std::string& path);
    bool saveSettings(const std::string& path)const;
};
}
