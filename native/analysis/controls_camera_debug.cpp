#include "../app/camera_rig.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}}
int main(){try{
    hades::UiSettings settings;hades::TouchControls controls;controls.configure(1800,1200,settings);
    controls.pointer(hades::UiPointer::Down,10,250,900);
    controls.pointer(hades::UiPointer::Move,10,250+controls.stickRadius()*.8f,900);
    require(controls.active()&&controls.dx()>.5f,"Floating stick did not acquire/move");
    const float movement=controls.dx();
    const auto attack=controls.actionButtons()[0];
    controls.pointer(hades::UiPointer::Down,11,attack.x,attack.y);
    controls.pointer(hades::UiPointer::Down,12,attack.x,attack.y);
    require(controls.heldControls()==std::vector<std::string>{"Attack2"},"Multi-touch attack did not produce the original control");
    controls.pointer(hades::UiPointer::Up,11,attack.x,attack.y);
    require(controls.heldControls().size()==1&&controls.active()&&controls.dx()==movement,"One action release lost another action contact or movement");
    controls.pointer(hades::UiPointer::Up,12,attack.x,attack.y);
    require(controls.heldControls().empty()&&controls.active(),"Action release remained held or stole movement");
    require(controls.takeControls()==std::vector<std::string>{"Attack2"},"Tap between simulation ticks was dropped");
    require(controls.takeControls().empty(),"Released tap repeated across simulation ticks");
    const auto use=controls.actionButtons()[4];require(std::string(use.control)=="Use","Use button lost source control identity");
    controls.pointer(hades::UiPointer::Down,19,use.x,use.y);controls.pointer(hades::UiPointer::Up,19,use.x,use.y);
    require(controls.takeControls()==std::vector<std::string>{"Use"}&&controls.takeControls().empty()&&controls.active(),"Use quick tap lost or repeated/stole movement");
    controls.pointer(hades::UiPointer::Down,2,600,150);
    controls.pointer(hades::UiPointer::Down,7,1000,150);
    controls.pointer(hades::UiPointer::Move,7,1200,150);
    auto gesture=controls.takeZoom();require(gesture.has_value(),"No pinch from unclaimed contacts");
    require(std::abs(gesture->ratio-2.f/3)<1e-5f,"Pinch ratio incorrect");
    require(controls.active()&&controls.dx()==movement,"Pinch stole movement pointer");
    hades::CameraRig camera;camera.update(0,1947,5188,1680);
    const float target=camera.zoom(*gesture,1800,1200);require(std::abs(target-1120)<.01f,"Camera pinch target incorrect");
    for(int i=0;i<120;++i)camera.update(1.f/120,1947,5188,target,true,true);
    require(std::abs(camera.height()-target)<.1f,"Camera zoom did not settle");
    controls.pointer(hades::UiPointer::Up,2,600,150);require(controls.active(),"Unrelated finger release cancelled stick");
    controls.pointer(hades::UiPointer::Up,10,320,900);require(!controls.active()&&controls.dx()==0,"Stick release remained held");
    controls.pointer(hades::UiPointer::Move,10,400,900);require(!controls.active(),"Stale move reacquired pointer");
    controls.pointer(hades::UiPointer::Down,10,250,900);controls.pointer(hades::UiPointer::Move,10,252,900);
    require(controls.dx()==0&&controls.dy()==0,"Dead zone allowed idle drift");
    controls.configure(2184,1968,settings);require(!controls.active(),"Surface resize retained pointer");
    settings.leftHanded=true;controls.configure(2184,1968,settings);
    controls.pointer(hades::UiPointer::Down,3,1800,1500);require(controls.active(),"Handed movement zone missing");
    controls.pointer(hades::UiPointer::Cancel,3,1800,1500);require(!controls.active(),"Cancel retained pointer");
    hades::CameraRig a,b;a.update(0,0,0,1680);b.update(0,0,0,1680);
    for(int i=0;i<60;++i)a.update(1.f/60,300,200,1200);
    for(int i=0;i<120;++i)b.update(1.f/120,300,200,1200);
    require(std::abs(a.height()-b.height())<.01f,"Zoom depends on frame count");
    auto va=a.view(1800,1200),vb=b.view(1800,1200);
    require(std::abs(va.left-vb.left)<.02f&&std::abs(va.top-vb.top)<.1f,"Follow depends on frame count");
    hades::CameraRig source;source.updateSource(0,1947,5188,.5f,1680,true);
    require(std::abs(source.height()-2160)<.01f,"Source cinematic zoom was replaced by user camera height");
    const float sourceUserHeight=source.zoom(*gesture,1800,1200);
    require(std::abs(sourceUserHeight-1120)<.01f,"Source pinch changed the native cinematic zoom");
    const auto anchored=source.view(1800,1200);source.updateSource(0,1947,5188,.5f,sourceUserHeight,true);
    const auto settled=source.view(1800,1200);
    require(std::abs(source.height()-1440)<.01f&&std::abs(anchored.left-settled.left)<.01f&&std::abs(anchored.top-settled.top)<.01f,"Source pinch doubled zoom or lost its anchor on next frame");
    hades::GameUi ui;require(ui.screen()==hades::UiScreen::Title,"Unexpected initial UI");
    ui.pointer(hades::UiPointer::Down,1,350,710,1800,1200);ui.pointer(hades::UiPointer::Up,1,350,710,1800,1200);
    require(ui.loading(),"Play skipped loading state");ui.roomReady(true);require(ui.playing(),"Room readiness did not enter movement slice");
    ui.back();require(ui.screen()==hades::UiScreen::Paused,"Back failed to pause");
    ui.roomReady(false);ui.back();require(ui.loading(),"Back resumed before lost surface assets reloaded");
    std::cout<<"CONTROLS_CAMERA_DEBUG_OK floating=true deadZone=true independentPinch=true unrelatedReleaseSafe=true resizeCancels=true handedness=true frameIndependent=true loadingGate=true\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"CONTROLS_CAMERA_DEBUG_FAILURE "<<e.what()<<'\n';return 1;}}
