#include "game_ui.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hades {
namespace {
std::string percent(float value){return std::to_string(static_cast<int>(std::lround(value*100)))+"%";}
constexpr int frameCaps[]={0,30,60,90,120};
std::string capLabel(int cap){return cap?std::to_string(cap)+" FPS":"DISPLAY RATE";}
}
std::vector<UiControl> GameUi::controls(float width)const {
    using Kind=UiControl::Kind;
    if(screen_==UiScreen::Playing)return {{Kind::Button,1,"", "",{width-132,42,82,82},0}};
    if(screen_==UiScreen::Remap){
        const float panel=std::min(1240.f,width-160),left=(width-panel)*.5f,cw=panel*.22f,padX=left+panel*.50f,keyX=left+panel*.75f;
        std::vector<UiControl> items{{Kind::Button,61,"BACK", "",{64,50,180,70},0},{Kind::Button,60,"RESET DEFAULTS", "",{width-64-420,50,420,70},0}};
        for(size_t i=0;i<remapRows.size();++i){const float y=250+float(i)*50;const auto& r=remapRows[i];
            const bool capPad=remapCaptureRow==int(i)&&remapCaptureDevice==0,capKey=remapCaptureRow==int(i)&&remapCaptureDevice==1;
            if(r.padCell)items.push_back({Kind::BindCell,int(100+2*i),r.label,capPad?"PRESS BUTTON":r.pad,{padX,y,cw,44},capPad?1.f:0.f});
            if(r.keyCell)items.push_back({Kind::BindCell,int(101+2*i),r.padCell?"":r.label,capKey?"PRESS KEY":r.key,{keyX,y,cw,44},capKey?1.f:0.f});}
        return items;
    }
    if(screen_==UiScreen::LayoutEdit)return {
        {Kind::Button,29,"DONE", "",{64,50,210,70},0},
        {Kind::Button,30,"RESET", "",{300,50,210,70},0}};
    if(screen_==UiScreen::Loading)return {{Kind::Button,7,"BACK", "",{64,50,180,70},0}};
    if(screen_==UiScreen::LoadFailed)return {
        {Kind::Button,8,"TRY AGAIN", "",{width*.5f-260,548,520,86},0},
        {Kind::Button,7,"MAIN MENU", "",{width*.5f-260,658,520,86},0}};
    if(screen_==UiScreen::Title){
        if(saveAvailable)return {
            {Kind::Button,31,"CONTINUE", "",{width*.095f,560,460,86},0},
            {Kind::Button,2,"NEW RUN", "",{width*.095f,670,460,86},0},
            {Kind::Button,3,"SETTINGS", "",{width*.095f,780,460,86},0}};
        return {
            {Kind::Button,2,"PLAY", "",{width*.095f,600,460,86},0},
            {Kind::Button,3,"SETTINGS", "",{width*.095f,710,460,86},0}};
    }
    if(screen_==UiScreen::Paused)return {
        {Kind::Button,4,"RESUME", "",{width*.5f-260,438,520,86},0},
        {Kind::Button,3,"SETTINGS", "",{width*.5f-260,548,520,86},0},
        {Kind::Button,5,"MAIN MENU", "",{width*.5f-260,658,520,86},0}};
    const float panel=std::min(1240.f,width-160),left=(width-panel)*.5f;
    const float sx=left+panel*.53f,sw=panel*.40f,tw=panel/6;
    std::vector<UiControl> items{
        {Kind::Button,6,"BACK", "",{64,50,180,70},0},
        {Kind::Tab,20,"GRAPHICS","",{left,236,tw,58},tab_==SettingsTab::Graphics?1.f:0.f},
        {Kind::Tab,21,"DISPLAY","",{left+tw,236,tw,58},tab_==SettingsTab::Display?1.f:0.f},
        {Kind::Tab,23,"AUDIO","",{left+2*tw,236,tw,58},tab_==SettingsTab::Audio?1.f:0.f},
        {Kind::Tab,40,"GAMEPLAY","",{left+3*tw,236,tw,58},tab_==SettingsTab::Gameplay?1.f:0.f},
        {Kind::Tab,22,"TOUCH","",{left+4*tw,236,tw,58},tab_==SettingsTab::Controls?1.f:0.f},
        {Kind::Tab,41,"CONTROLLER","",{left+5*tw,236,tw,58},tab_==SettingsTab::Controller?1.f:0.f}};
    auto onOff=[](bool v){return std::string(v?"ON":"OFF");};
    auto row=[&](size_t i){return UiRect{sx,330+float(i)*105,sw,82};};
    if(tab_==SettingsTab::Graphics){
        items.push_back({Kind::Slider,16,"RENDER RESOLUTION",percent(settings.renderScale),row(0),(settings.renderScale-.5f)/.5f});
        items.push_back({Kind::Toggle,17,"FRAME RATE LIMIT",capLabel(settings.frameCap),row(1),settings.frameCap?1.f:0.f});
        items.push_back({Kind::Toggle,18,"SHOW FPS COUNTER",settings.showFps?"ON":"OFF",row(2),settings.showFps?1.f:0.f});
        items.push_back({Kind::Toggle,42,"BLOOM",onOff(settings.bloom),row(3),settings.bloom?1.f:0.f});
        items.push_back({Kind::Toggle,52,"LIGHTING",onOff(settings.drawLights),row(4),settings.drawLights?1.f:0.f});
        items.push_back({Kind::Toggle,53,"VSYNC",onOff(settings.vsync),row(5),settings.vsync?1.f:0.f});
        items.push_back({Kind::Toggle,54,"LETTERBOX (16:9)",onOff(settings.letterbox),row(6),settings.letterbox?1.f:0.f});
    }else if(tab_==SettingsTab::Display){
        items.push_back({Kind::Slider,10,"BRIGHTNESS",percent(settings.brightness),row(0),(settings.brightness-.65f)/.7f});
        items.push_back({Kind::Slider,11,"CAMERA ZOOM",percent(1680/settings.cameraHeight),row(1),(3200-settings.cameraHeight)/2300});
    }else if(tab_==SettingsTab::Gameplay){
        items.push_back({Kind::Toggle,43,"GOD MODE",onOff(settings.easyMode),row(0),settings.easyMode?1.f:0.f});
        items.push_back({Kind::Toggle,44,"DAMAGE NUMBERS",onOff(settings.damageNumbers),row(1),settings.damageNumbers?1.f:0.f});
        items.push_back({Kind::Toggle,45,"UI ANIMATIONS",onOff(settings.uiAnimations),row(2),settings.uiAnimations?1.f:0.f});
        items.push_back({Kind::Toggle,46,"SPEECH BUBBLES",onOff(settings.speechBubbles),row(3),settings.speechBubbles?1.f:0.f});
        items.push_back({Kind::Toggle,47,"SUBTITLES",onOff(settings.subtitles),row(4),settings.subtitles?1.f:0.f});
        items.push_back({Kind::Toggle,56,"CAMERA SHAKE",onOff(settings.cameraShake),row(5),settings.cameraShake?1.f:0.f});
    }else if(tab_==SettingsTab::Controller){
        std::string name=controllerName.empty()?"NONE DETECTED":controllerName.substr(0,controllerName.find('\n'));
        if(name.size()>22)name=name.substr(0,21)+"...";
        items.push_back({Kind::Toggle,48,"CONNECTED",name,row(0),controllerName.empty()?0.f:1.f});
        static const char* touchModes[]{"AUTO","ALWAYS","HIDDEN"};
        items.push_back({Kind::Toggle,49,"TOUCH CONTROLS",touchModes[std::clamp(settings.touchMode,0,2)],row(1),1.f});
        items.push_back({Kind::Slider,50,"STICK DEAD ZONE",percent(settings.deadZone),row(2),(settings.deadZone-.05f)/.45f});
        items.push_back({Kind::Toggle,51,"CONTROLLER RUMBLE",onOff(settings.controllerRumble),row(3),settings.controllerRumble?1.f:0.f});
        items.push_back({Kind::Toggle,55,"BUTTON MAPPING","CUSTOMIZE",row(4),1.f});
    }else if(tab_==SettingsTab::Audio){
        items.push_back({Kind::Slider,24,"MASTER VOLUME",percent(settings.masterVolume),row(0),settings.masterVolume});
        items.push_back({Kind::Slider,25,"MUSIC VOLUME",percent(settings.musicVolume),row(1),settings.musicVolume});
        items.push_back({Kind::Slider,26,"EFFECTS VOLUME",percent(settings.effectsVolume),row(2),settings.effectsVolume});
        items.push_back({Kind::Slider,27,"VOICE VOLUME",percent(settings.voiceVolume),row(3),settings.voiceVolume});
    }else{
        items.push_back({Kind::Slider,12,"CONTROL SIZE",percent(settings.touchScale),row(0),(settings.touchScale-.75f)/.75f});
        items.push_back({Kind::Slider,13,"CONTROL OPACITY",percent(settings.touchOpacity),row(1),(settings.touchOpacity-.25f)/.65f});
        items.push_back({Kind::Toggle,14,"LEFT-HANDED CONTROLS",settings.leftHanded?"ON":"OFF",row(2),settings.leftHanded?1.f:0.f});
        items.push_back({Kind::Toggle,15,"FLOATING JOYSTICK",settings.floatingStick?"ON":"OFF",row(3),settings.floatingStick?1.f:0.f});
        items.push_back({Kind::Toggle,19,"VIBRATION",settings.vibration?"ON":"OFF",row(4),settings.vibration?1.f:0.f});
        items.push_back({Kind::Toggle,28,"BUTTON LAYOUT","CUSTOMIZE",row(5),1.f});
    }
    return items;
}
void GameUi::activate(int id){
    if(id==1)screen_=UiScreen::Paused;
    else if(id==2||id==4)screen_=roomReady_?UiScreen::Playing:UiScreen::Loading;
    else if(id==31){continueRequested_=true;roomReady_=false;screen_=UiScreen::Loading;}
    else if(id==3){returnFromSettings_=screen_;screen_=UiScreen::Settings;}
    else if(id==5)screen_=UiScreen::Title;
    else if(id==6)screen_=returnFromSettings_;
    else if(id==14){settings.leftHanded=!settings.leftHanded;dirty_=true;}
    else if(id==15){settings.floatingStick=!settings.floatingStick;dirty_=true;}
    else if(id==17){size_t i=0;while(i<std::size(frameCaps)&&frameCaps[i]!=settings.frameCap)++i;settings.frameCap=frameCaps[(i+1)%std::size(frameCaps)];dirty_=true;}
    else if(id==18){settings.showFps=!settings.showFps;dirty_=true;}
    else if(id==19){settings.vibration=!settings.vibration;dirty_=true;}
    else if(id==28)screen_=UiScreen::LayoutEdit;
    else if(id==29)screen_=UiScreen::Settings;
    else if(id==30){settings.touchLayout.fill(0);dirty_=true;}
    else if(id==20)tab_=SettingsTab::Graphics;
    else if(id==21)tab_=SettingsTab::Display;
    else if(id==22)tab_=SettingsTab::Controls;
    else if(id==23)tab_=SettingsTab::Audio;
    else if(id==40)tab_=SettingsTab::Gameplay;
    else if(id==41)tab_=SettingsTab::Controller;
    else if(id>=42&&id<=47){bool* flags[]{&settings.bloom,&settings.easyMode,&settings.damageNumbers,&settings.uiAnimations,&settings.speechBubbles,&settings.subtitles};*flags[id-42]=!*flags[id-42];dirty_=true;}
    else if(id==56){settings.cameraShake=!settings.cameraShake;dirty_=true;}
    else if(id==52){settings.drawLights=!settings.drawLights;dirty_=true;}
    else if(id==53){settings.vsync=!settings.vsync;dirty_=true;}
    else if(id==54){settings.letterbox=!settings.letterbox;dirty_=true;}
    else if(id==55){screen_=UiScreen::Remap;remapCaptureRow=-1;}
    else if(id==60){remapReset_=true;remapCaptureRow=-1;dirty_=true;}
    else if(id==61){screen_=UiScreen::Settings;remapCaptureRow=-1;}
    else if(id>=100&&id<100+2*int(remapRows.size())){remapCaptureRow=(id-100)/2;remapCaptureDevice=(id-100)%2;}
    else if(id==49){settings.touchMode=(settings.touchMode+1)%3;dirty_=true;}
    else if(id==51){settings.controllerRumble=!settings.controllerRumble;dirty_=true;}
    else if(id==7)screen_=UiScreen::Title;
    else if(id==8)screen_=UiScreen::Loading;
}
void GameUi::slide(int id,float f){
    f=std::clamp(f,0.f,1.f);
    if(id==10)settings.brightness=.65f+.7f*f;
    else if(id==11)settings.cameraHeight=3200-2300*f;
    else if(id==12)settings.touchScale=.75f+.75f*f;
    else if(id==13)settings.touchOpacity=.25f+.65f*f;
    else if(id==50)settings.deadZone=std::round((.05f+.45f*f)*100)/100;
    else if(id==24)settings.masterVolume=std::round(f*20)/20;
    else if(id==25)settings.musicVolume=std::round(f*20)/20;
    else if(id==26)settings.effectsVolume=std::round(f*20)/20;
    else if(id==27)settings.voiceVolume=std::round(f*20)/20;
    else if(id==16)settings.renderScale=std::round((.5f+.5f*f)*20)/20;
    dirty_=true;
}
void GameUi::cameraHeight(float value){
    if(!std::isfinite(value))return;value=std::clamp(value,900.f,3200.f);
    if(value!=settings.cameraHeight){settings.cameraHeight=value;dirty_=true;}
}
void GameUi::back(){
    if(screen_==UiScreen::Playing)screen_=UiScreen::Paused;
    else if(screen_==UiScreen::Paused)screen_=roomReady_?UiScreen::Playing:UiScreen::Loading;
    else if(screen_==UiScreen::Settings)screen_=returnFromSettings_;
    else if(screen_==UiScreen::LayoutEdit)screen_=UiScreen::Settings;
    else if(screen_==UiScreen::Remap){if(remapCaptureRow>=0)remapCaptureRow=-1;else screen_=UiScreen::Settings;}
    else if(loading()||screen_==UiScreen::LoadFailed)screen_=UiScreen::Title;
    cancel();
}
bool GameUi::pointer(UiPointer action,int id,float px,float py,int width,int height){
    if(width<=0||height<=0)return false;
    const float scale=height/1080.f,x=px/scale,y=py/scale;
    const auto items=controls(width/scale);
    if(action==UiPointer::Cancel){const bool owned=pointer_>=0;cancel();return owned||!playing();}
    if(action==UiPointer::Down){
        if(pointer_>=0)return !playing();
        for(const auto& item:items)if(item.bounds.contains(x,y)){
            pointer_=id;pressed_=item.id;
            if(item.kind==UiControl::Kind::Slider)slide(item.id,(x-item.bounds.x)/item.bounds.w);
            return true;
        }
        return !playing();
    }
    if(id!=pointer_)return !playing();
    for(const auto& item:items)if(item.id==pressed_){
        if(item.kind==UiControl::Kind::Slider&&(action==UiPointer::Move||action==UiPointer::Up))slide(item.id,(x-item.bounds.x)/item.bounds.w);
        else if(action==UiPointer::Up&&item.bounds.contains(x,y))activate(item.id);
        break;
    }
    if(action==UiPointer::Up)cancel();
    return true;
}
bool GameUi::loadSettings(const std::string& path){
    std::ifstream input(path);std::string magic;UiSettings value;int handed=0;
    if(!(input>>magic>>value.brightness>>value.cameraHeight>>value.touchScale>>value.touchOpacity>>handed)||
       (magic!="HADES_UI_1"&&magic!="HADES_UI_2"&&magic!="HADES_UI_3"))return false;
    if(magic!="HADES_UI_1"){int floating=0;if(!(input>>floating)||(floating!=0&&floating!=1))return false;value.floatingStick=floating;}
    if(magic=="HADES_UI_3"){int fps=1;if(!(input>>value.renderScale>>value.frameCap>>fps)||!std::isfinite(value.renderScale)||value.renderScale<.5f||value.renderScale>1||
        std::find(std::begin(frameCaps),std::end(frameCaps),value.frameCap)==std::end(frameCaps)||(fps!=0&&fps!=1))return false;value.showFps=fps;int vib=1;if(input>>vib){value.vibration=vib!=0;float v[4];if(input>>v[0]>>v[1]>>v[2]>>v[3]){for(float x:v)if(!std::isfinite(x)||x<0||x>1)return false;value.masterVolume=v[0];value.musicVolume=v[1];value.effectsVolume=v[2];value.voiceVolume=v[3];
            std::array<float,16> layout{};bool complete=true;for(auto& x:layout)if(!(input>>x)||!std::isfinite(x)||std::abs(x)>120){complete=false;break;}if(complete){value.touchLayout=layout;
                int b[6];int tm=0;float dz=0;int cr=1;if(input>>b[0]>>b[1]>>b[2]>>b[3]>>b[4]>>b[5]>>tm>>dz>>cr&&tm>=0&&tm<=2&&std::isfinite(dz)&&dz>=.05f&&dz<=.5f){
                    value.easyMode=b[0];value.damageNumbers=b[1];value.uiAnimations=b[2];value.speechBubbles=b[3];value.subtitles=b[4];value.bloom=b[5];value.touchMode=tm;value.deadZone=dz;value.controllerRumble=cr;
                    int dl=1,vs=1,lb=0;if(input>>dl>>vs>>lb){value.drawLights=dl;value.vsync=vs;value.letterbox=lb;}}}}}}
    if(!std::isfinite(value.brightness)||value.brightness<.65f||value.brightness>1.35f||
       !std::isfinite(value.cameraHeight)||value.cameraHeight<900||value.cameraHeight>3200||
       !std::isfinite(value.touchScale)||value.touchScale<.75f||value.touchScale>1.5f||
       !std::isfinite(value.touchOpacity)||value.touchOpacity<.25f||value.touchOpacity>.9f||(handed!=0&&handed!=1))return false;
    
    value.leftHanded=handed;settings=value;return true;
}
bool GameUi::saveSettings(const std::string& path)const{
    const auto temporary=path+".tmp";
    std::ofstream output(temporary,std::ios::trunc);
    output<<"HADES_UI_3\n"<<std::setprecision(9)<<settings.brightness<<' '<<settings.cameraHeight<<' '
          <<settings.touchScale<<' '<<settings.touchOpacity<<' '<<settings.leftHanded<<' '<<settings.floatingStick<<' '
          <<settings.renderScale<<' '<<settings.frameCap<<' '<<settings.showFps<<' '<<settings.vibration<<' '<<settings.masterVolume<<' '<<settings.musicVolume<<' '<<settings.effectsVolume<<' '<<settings.voiceVolume;
    for(float x:settings.touchLayout)output<<' '<<x;
    output<<' '<<settings.easyMode<<' '<<settings.damageNumbers<<' '<<settings.uiAnimations<<' '<<settings.speechBubbles<<' '<<settings.subtitles<<' '<<settings.bloom
          <<' '<<settings.touchMode<<' '<<settings.deadZone<<' '<<settings.controllerRumble
          <<' '<<settings.drawLights<<' '<<settings.vsync<<' '<<settings.letterbox<<'\n';
    output.close();if(!output)return false;
    return std::rename(temporary.c_str(),path.c_str())==0;
}
}
