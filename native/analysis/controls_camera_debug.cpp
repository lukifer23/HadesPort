#include "../app/camera_rig.hpp"
#include "../app/hardware_input.hpp"
#include "../app/frame_pacing.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}}
int main(){try{
    require(hades::UiSettings{}.frameCap==60,"Fresh settings run above 60 FPS");
    require(hades::displayFrameCap(0,1,-1,true)==0&&hades::displayFrameCap(120,1,0,true)==120,"Cool device lost requested display rate");
    require(hades::displayFrameCap(0,3,0,true)==60&&hades::displayFrameCap(120,3,0,true)==60,"Battery mode did not bound frame rate");
    require(hades::displayFrameCap(120,1,2,true)==60&&hades::displayFrameCap(0,1,3,true)==30&&hades::displayFrameCap(60,1,4,true)==15,"Platform thermal status did not bound rendering");
    require(hades::displayFrameCap(30,1,2,true)==30&&hades::displayFrameCap(0,1,0,false)==30,"Frame limiter raised a lower cap or left menu uncapped");
    // Use the production touch policy at actual cover/inner panel geometry.
    // The cover's old 45 mm bottom offset put its visible rest ABOVE the
    // floating acquisition zone: bottom touches worked only until release.
    for(const auto panel: {std::array<int,2>{2520,1080},std::array<int,2>{2184,1968}})
    for(bool left: {false,true})for(bool floating: {false,true})for(float scale: {.7f,1.f,1.3f}){
        hades::UiSettings layout;layout.leftHanded=left;layout.floatingStick=floating;layout.touchScale=scale;
        hades::TouchControls stick;stick.configure(panel[0],panel[1],layout,420);
        require(stick.y()>panel[1]*.45f,"Visible default stick is outside lower movement area");
        for(int cycle=0;cycle<4;++cycle){
            const float x=stick.x(),y=stick.y(),direction=left?-1.f:1.f;
            require(stick.pointer(hades::UiPointer::Down,50,x,y)&&stick.active(),"Visible stick could not be acquired after release");
            stick.pointer(hades::UiPointer::Move,50,x+direction*stick.stickRadius()*.8f,y);
            require(direction*stick.dx()>.5f,"Reacquired visible stick did not move");
            stick.pointer(hades::UiPointer::Up,51,x,y);
            require(stick.active(),"Unrelated release cancelled reacquired stick");
            stick.pointer(hades::UiPointer::Up,50,x,y);
            require(!stick.active()&&stick.dx()==0&&stick.dy()==0,"Release retained movement");
            stick.configure(panel[0],panel[1],layout,420);
            stick.pointer(hades::UiPointer::Move,50,x,y);
            require(!stick.active(),"Released pointer moved without a new press");
        }
        // An explicitly edited rest must remain touchable above the general
        // floating zone; physical size and editor offsets do not define input.
        layout.touchLayout[15]=-45;
        stick.configure(panel[0],panel[1],layout,420);
        require(stick.pointer(hades::UiPointer::Down,52,stick.x(),stick.y())&&stick.active(),"Edited visible stick is unreachable");
        stick.cancel();stick.configure(panel[0],panel[1],layout,420);
        require(stick.pointer(hades::UiPointer::Down,53,stick.x(),stick.y())&&stick.active(),"Cancelled stick cannot reacquire");
    }
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
    // Context is supplied by actual native control waits in Engine. Switching
    // labels must never reinterpret a contact or queued tap from the old mode.
    controls.pointer(hades::UiPointer::Down,19,use.x,use.y);
    controls.setContinueTextRequested(true);
    const auto next=controls.actionButtons()[4];std::string label;
    require(std::string(next.control)=="ContinueText"&&std::string(next.label)=="NEXT"&&next.x==use.x&&next.y==use.y&&next.radius==use.radius,"Dialogue control changed the saved Use hitbox");
    require(!controls.active()&&controls.heldControls().empty()&&controls.takeControls().empty(),"Use/movement leaked into dialogue context");
    require(controls.controlLabel("Use",label)&&label=="USE"&&controls.controlLabel("ContinueText",label)&&label=="NEXT","Context discarded canonical binding labels");
    controls.pointer(hades::UiPointer::Move,19,next.x,next.y);controls.pointer(hades::UiPointer::Up,19,next.x,next.y);
    require(controls.takeControls().empty(),"Old Use contact became a dialogue press");
    controls.pointer(hades::UiPointer::Down,10,250,900);controls.pointer(hades::UiPointer::Move,10,350,900);
    controls.pointer(hades::UiPointer::Down,11,attack.x,attack.y);
    require(!controls.active()&&controls.takeControls().empty()&&!controls.takeZoom(),"Hidden dialogue controls still accepted movement/combat/pinch");
    controls.pointer(hades::UiPointer::Up,10,350,900);controls.pointer(hades::UiPointer::Up,11,attack.x,attack.y);
    controls.pointer(hades::UiPointer::Down,19,next.x,next.y);controls.pointer(hades::UiPointer::Up,19,next.x,next.y);
    require(controls.takeControls()==std::vector<std::string>{"ContinueText"}&&controls.takeControls().empty(),"NEXT quick tap lost or repeated");
    controls.pointer(hades::UiPointer::Down,19,next.x,next.y);controls.pointer(hades::UiPointer::Down,20,next.x,next.y);
    controls.setContinueTextRequested(true);controls.pointer(hades::UiPointer::Up,19,next.x,next.y);
    require(controls.heldControls()==std::vector<std::string>{"ContinueText"},"Unchanged context or one release lost the remaining NEXT owner");
    controls.setContinueTextRequested(false);
    require(controls.heldControls().empty()&&controls.takeControls().empty()&&std::string(controls.actionButtons()[4].control)=="Use"&&!controls.controlLabel("ContinueText",label),"NEXT leaked back into gameplay context");
    controls.pointer(hades::UiPointer::Up,20,use.x,use.y);require(controls.takeControls().empty(),"Old NEXT release became Use");
    controls.pointer(hades::UiPointer::Down,19,use.x,use.y);controls.pointer(hades::UiPointer::Up,19,use.x,use.y);
    controls.setContinueTextRequested(true);require(controls.takeControls().empty(),"Queued Use tap became NEXT");
    controls.pointer(hades::UiPointer::Down,19,next.x,next.y);controls.pointer(hades::UiPointer::Up,19,next.x,next.y);
    controls.setContinueTextRequested(false);require(controls.takeControls().empty(),"Queued NEXT tap became Use");
    controls.pointer(hades::UiPointer::Down,10,250,900);controls.pointer(hades::UiPointer::Move,10,250+controls.stickRadius()*.8f,900);
    require(controls.active()&&controls.dx()==movement,"Dialogue context changed ordinary movement after release");
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
    const auto editButton=controls.actionButtons()[0];
    require(controls.editPointer(hades::UiPointer::Down,40,editButton.x,editButton.y)&&controls.editingSlot()==0,"Layout drag did not acquire pointer");
    controls.cancel();
    require(controls.editingSlot()==-1,"Focus loss retained layout editor pointer");
    require(controls.editPointer(hades::UiPointer::Down,41,editButton.x,editButton.y)&&controls.editingSlot()==0,"Cancelled layout drag blocked a new pointer");
    controls.configure(1800,1200,settings);
    require(controls.editingSlot()==-1,"Surface resize retained layout editor pointer");
    const auto resizedEditButton=controls.actionButtons()[0];
    require(controls.editPointer(hades::UiPointer::Down,42,resizedEditButton.x,resizedEditButton.y)&&controls.editingSlot()==0,"Resized layout editor blocked a new pointer");
    controls.editPointer(hades::UiPointer::Up,42,resizedEditButton.x,resizedEditButton.y);
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
    auto click=[&](int id){
        for(const auto& item:ui.controls(1800))if(item.id==id){
            const float x=item.bounds.x+item.bounds.w/2,y=item.bounds.y+item.bounds.h/2;
            ui.pointer(hades::UiPointer::Down,1,x,y,1800,1080);ui.pointer(hades::UiPointer::Up,1,x,y,1800,1080);return;
        }
        throw std::runtime_error("Missing menu control in lifecycle regression");
    };
    require(ui.takeNewRun()&&!ui.takeNewRun(),"Play did not request exactly one fresh world");
    ui.roomReady(true);ui.back();click(4);
    require(ui.playing()&&!ui.takeNewRun(),"Resume requested a fresh world");
    ui.back();click(5);click(2);
    require(ui.loading()&&ui.takeNewRun(),"New Run reused the previous ready world");
    ui.roomReady(true);ui.back();click(5);ui.saveAvailable=true;click(31);
    require(ui.loading()&&ui.takeContinue()&&!ui.takeContinue()&&!ui.takeNewRun(),"Continue lost its separate load request");
    ui.roomReady(true);
    require(ui.handleBackControls({"Cancel","Pause"})&&ui.screen()==hades::UiScreen::Paused,"Shared Escape controls did not pause exactly once");
    require(ui.handleBackControls({"Cancel","Pause"})&&ui.playing(),"Shared Escape controls did not resume exactly once");
    ui.back();click(3);
    require(ui.handleBackControls({"Cancel","Pause"})&&ui.screen()==hades::UiScreen::Paused,"Shared Escape controls skipped the settings return screen");

    hades::input::HardwareInput hardware;
    const hades::input::ControlList attackControls{{"Attack2"},1},leftControls{{"Left"},1};
    const hades::input::HardwareInput::Source key1{1,0,29},key2{1,0,30},pad{2,0,99},hat{2,1,46};
    hardware.update(key1,attackControls,true);hardware.update(key2,attackControls,true);
    hardware.update(key1,attackControls,false);
    require(hardware.held().contains("Attack2"),"One physical key released another key's control");
    hardware.update(pad,attackControls,true);hardware.releaseDevice(1);
    require(hardware.held().contains("Attack2"),"Disconnect released another device's control");
    hardware.releaseDevice(2);
    require(hardware.held().empty()&&hardware.takePressed().empty(),"Disconnected controller left held or pending actions");
    hardware.update(key1,leftControls,true);hardware.update(hat,leftControls,false);
    require(hardware.held().contains("Left"),"Neutral gamepad hat released keyboard movement");
    hardware.clear();hardware.update(pad,attackControls,true);hardware.update(pad,attackControls,false);
    require(hardware.held().empty()&&hardware.takePressed().contains("Attack2")&&hardware.takePressed().empty(),"Hardware quick tap was lost or repeated");
    hardware.update(key1,attackControls,true);hardware.update(key1,attackControls,false);
    require(hardware.devices().contains(1),"Queued keyboard tap lost its disconnect owner");
    hardware.releaseDevice(1);require(hardware.takePressed().empty(),"Disconnected keyboard retained its queued tap");
    hardware.update(hat,{},true);hardware.clear();
    require(!hardware.down(hat)&&hardware.update(hat,leftControls,true).size()==1,"Pause retained axis edge state");
    std::cout<<"CONTROLS_CAMERA_DEBUG_OK floating=true deadZone=true independentPinch=true unrelatedReleaseSafe=true resizeCancels=true handedness=true frameIndependent=true loadingGate=true\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"CONTROLS_CAMERA_DEBUG_FAILURE "<<e.what()<<'\n';return 1;}}
