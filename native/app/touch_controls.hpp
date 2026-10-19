#pragma once
#include "game_ui.hpp"
#include <map>
#include <optional>
#include <array>
#include <string>
#include <vector>

namespace hades {
struct ZoomGesture {float ratio=1,fromX=0,fromY=0,toX=0,toY=0;};
// Android input policy. Pointer ownership and dead zone are independent of the
// original unit movement semantics; unclaimed fingers alone form a pinch.
class TouchControls {
    struct Contact {float x=0,y=0;};
    int width_=0,height_=0,owner_=-1;
    bool leftHanded_=false,floating_=true;
    float scale_=1,dpi_=0,anchorX_=0,anchorY_=0,visualX_=0,visualY_=0,dx_=0,dy_=0;
    std::map<int,Contact> free_;
    std::optional<ZoomGesture> zoom_;
    std::map<int,size_t> actionContacts_;
    static constexpr size_t ButtonCount=7,LayoutSlots=ButtonCount+1; // +1 = movement stick rest
    std::array<bool,ButtonCount> held_{};
    std::array<bool,ButtonCount> pressed_{};
    // Per-element user offsets in millimetres (x mirrored for left-handed).
    std::array<float,LayoutSlots*2> layout_{};
    int editOwner_=-1;size_t editSlot_=0;float editX_=0,editY_=0;bool layoutDirty_=false;
    std::array<float,2> defaultStick()const;
    void updatePinch(const std::map<int,Contact>& before);
    // Physical millimetres to pixels from the display density (Android
    // adaptation; sizes stay thumb-sized across the Fold's two panels).
    float mm(float value)const{return value*(dpi_>0?dpi_:400.f)/25.4f;}
public:
    struct ActionButton {const char* control;const char* label;float x,y,radius;bool held;};
    std::array<ActionButton,ButtonCount> actionButtons()const;
    // Layout editor (Android adaptation): drag a button or the stick rest.
    bool editPointer(UiPointer,int id,float x,float y);
    bool takeLayout(std::array<float,LayoutSlots*2>& out){if(!layoutDirty_)return false;out=layout_;layoutDirty_=false;return true;}
    int editingSlot()const{return editOwner_>=0?int(editSlot_):-1;}
    std::vector<std::string> heldControls()const;
    std::vector<std::string> takeControls();
    void configure(int width,int height,const UiSettings&,float dpi=0);
    float stickRadius()const{return mm(11.5f)*scale_;}
    void cancel();
    bool pointer(UiPointer,int id,float x,float y);
    bool active()const{return owner_>=0;}
    bool pinching()const{return free_.size()>=2;}
    float x()const{return visualX_;} float y()const{return visualY_;}
    float dx()const{return dx_;} float dy()const{return dy_;}
    std::optional<ZoomGesture> takeZoom(){auto result=zoom_;zoom_.reset();return result;}
};
}
