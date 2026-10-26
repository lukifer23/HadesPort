#include "touch_controls.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace hades {
void TouchControls::cancel(){owner_=editOwner_=-1;dx_=dy_=0;free_.clear();zoom_.reset();actionContacts_.clear();held_.fill(false);pressed_.fill(false);}
std::array<TouchControls::ActionButton,TouchControls::ButtonCount> TouchControls::actionButtons()const{
    // Android touch layout: ATTACK anchored in the lower corner, the chained
    // actions (DASH/SPECIAL/CAST) on a tight inner ring and the occasional ones
    // (USE/ASSIST/CALL) on an outer ring; millimetres from display density.
    // Control names feed the original control map; the Unit/Arsenal owns what
    // each source control does with held input. User offsets apply on top.
    const float k=scale_,ring=mm(20)*k,outer=mm(37)*k;
    const float ax=leftHanded_?mm(19)*k:width_-mm(19)*k,ay=height_-mm(19)*k;
    struct Slot{const char* control;const char* label;float degrees,distance,radius;};
    const Slot slots[ButtonCount]{{"Attack2","ATTACK",0,0,8.5f},{"Rush","DASH",185,ring,7.5f},{"Attack3","SPECIAL",130,ring,6.5f},
        {"Attack1","CAST",80,ring,6.5f},{"Use","USE",152,outer,6.f},{"Assist","ASSIST",121,outer,5.5f},{"Shout","CALL",90,outer,5.5f}};
    std::array<ActionButton,ButtonCount> out{};
    for(size_t i=0;i<ButtonCount;++i){const auto& d=slots[i];const float r=d.degrees*0.017453292f,radius=mm(d.radius)*k;
        float x=ax+(leftHanded_?-1.f:1.f)*std::cos(r)*d.distance,y=ay-std::sin(r)*d.distance;
        x+=(leftHanded_?-1.f:1.f)*mm(layout_[2*i]);y+=mm(layout_[2*i+1]);
        x=std::clamp(x,radius,std::max(radius,width_-radius));y=std::clamp(y,radius,std::max(radius,height_-radius));
        out[i]={d.control,d.label,x,y,radius,held_[i]};}
    return out;
}
std::array<float,2> TouchControls::defaultStick()const{
    // Rest above the original HUD's bottom health/ammo band (drawn ~20% from
    // the bottom on the Fold's aspect) so the stick never covers it.
    const float rest=mm(25)*scale_,restY=mm(45)*scale_,offset=(leftHanded_?-1.f:1.f)*mm(layout_[2*ButtonCount]);
    const float r=stickRadius();
    return {std::clamp((leftHanded_?width_-rest:rest)+offset,r,std::max(r,width_-r)),std::clamp(height_-restY+mm(layout_[2*ButtonCount+1]),r,std::max(r,height_-r))};
}
bool TouchControls::editPointer(UiPointer phase,int id,float x,float y){
    if(phase==UiPointer::Cancel){editOwner_=-1;return true;}
    if(width_<=0||height_<=0||!std::isfinite(x)||!std::isfinite(y))return false;
    if(phase==UiPointer::Down){
        if(editOwner_>=0)return true;
        const auto buttons=actionButtons();size_t best=LayoutSlots;float bestDistance=0;
        for(size_t i=0;i<ButtonCount;++i){const float d=std::hypot(x-buttons[i].x,y-buttons[i].y)/(buttons[i].radius+mm(3));if(d<=1&&(best==LayoutSlots||d<bestDistance)){best=i;bestDistance=d;}}
        const auto stick=defaultStick();if(best==LayoutSlots&&std::hypot(x-stick[0],y-stick[1])<=stickRadius()+mm(3))best=ButtonCount;
        if(best==LayoutSlots)return false;
        editOwner_=id;editSlot_=best;editX_=x;editY_=y;return true;
    }
    if(id!=editOwner_)return false;
    const float px=25.4f/(dpi_>0?dpi_:400.f);
    layout_[2*editSlot_]+=(leftHanded_?-1.f:1.f)*(x-editX_)*px;layout_[2*editSlot_+1]+=(y-editY_)*px;
    for(auto& v:layout_)v=std::clamp(v,-120.f,120.f);
    editX_=x;editY_=y;layoutDirty_=true;
    if(phase==UiPointer::Up)editOwner_=-1;
    if(!active()){const auto stick=defaultStick();visualX_=stick[0];visualY_=stick[1];}
    return true;
}
std::vector<std::string> TouchControls::heldControls()const{
    std::vector<std::string> result;const auto buttons=actionButtons();
    for(const auto& button:buttons)if(button.held)result.emplace_back(button.control);return result;
}
std::vector<std::string> TouchControls::takeControls(){
    std::vector<std::string> result;const auto buttons=actionButtons();
    for(size_t i=0;i<buttons.size();++i)if(held_[i]||pressed_[i])result.emplace_back(buttons[i].control);
    pressed_.fill(false);return result;
}
void TouchControls::configure(int width,int height,const UiSettings& settings,float dpi){
    if(width!=width_||height!=height_||leftHanded_!=settings.leftHanded||floating_!=settings.floatingStick||scale_!=settings.touchScale||dpi!=dpi_)cancel();
    width_=width;height_=height;leftHanded_=settings.leftHanded;floating_=settings.floatingStick;scale_=settings.touchScale;dpi_=dpi;
    if(editOwner_<0)layout_=settings.touchLayout;
    if(!active()){const auto stick=defaultStick();visualX_=stick[0];visualY_=stick[1];}
}
void TouchControls::updatePinch(const std::map<int,Contact>& before){
    if(before.size()<2||free_.size()<2)return;
    auto a=before.cbegin(),b=std::next(a),c=free_.cbegin(),d=std::next(c);
    if(a->first!=c->first||b->first!=d->first)return;
    const float oldDistance=std::hypot(a->second.x-b->second.x,a->second.y-b->second.y);
    const float newDistance=std::hypot(c->second.x-d->second.x,c->second.y-d->second.y);
    if(oldDistance<24||newDistance<24)return;
    ZoomGesture change{oldDistance/newDistance,(a->second.x+b->second.x)*.5f,(a->second.y+b->second.y)*.5f,
        (c->second.x+d->second.x)*.5f,(c->second.y+d->second.y)*.5f};
    if(zoom_){zoom_->ratio*=change.ratio;zoom_->toX=change.toX;zoom_->toY=change.toY;}
    else zoom_=change;
}
bool TouchControls::pointer(UiPointer phase,int id,float x,float y){
    if(phase==UiPointer::Cancel){cancel();return true;}
    if(width_<=0||height_<=0||id<0||!std::isfinite(x)||!std::isfinite(y))return false;
    const float s=mm(2),radius=stickRadius();
    if(phase==UiPointer::Down){
        // Nearest button within a forgiving touch slop, so the compact arc
        // never resolves an edge press to the wrong neighbour.
        const auto buttons=actionButtons();size_t best=buttons.size();float bestDistance=0;
        for(size_t i=0;i<buttons.size();++i){const auto& b=buttons[i];const float d=std::hypot(x-b.x,y-b.y)/(b.radius+mm(2.5f));
            if(d<=1&&(best==buttons.size()||d<bestDistance)){best=i;bestDistance=d;}}
        if(best<buttons.size()){actionContacts_[id]=best;pressed_[best]=held_[best]=true;return true;}
        const bool movementSide=leftHanded_?x>width_*.55f:x<width_*.45f;
        const bool eligible=floating_?(movementSide&&y>height_*.45f):std::hypot(x-visualX_,y-visualY_)<radius*1.65f;
        if(owner_<0&&eligible){
            owner_=id;anchorX_=floating_?x:visualX_;anchorY_=floating_?y:visualY_;
            visualX_=std::clamp(anchorX_,radius+s,std::max(radius+s,width_-radius-s));
            visualY_=std::clamp(anchorY_,radius+s,std::max(radius+s,height_-radius-s));
            dx_=dy_=0;return true;
        }
        if(free_.size()<10)free_[id]={x,y};return false;
    }
    if(const auto action=actionContacts_.find(id);action!=actionContacts_.end()){
        const auto index=action->second;
        if(phase==UiPointer::Up)actionContacts_.erase(action);
        held_[index]=false;
        for(const auto& contact:actionContacts_)if(contact.second==index){held_[index]=true;break;}
        return true;
    }
    if(id==owner_){
        if(phase==UiPointer::Up){owner_=-1;dx_=dy_=0;}
        else{
            const float dx=x-anchorX_,dy=y-anchorY_,distance=std::hypot(dx,dy),raw=std::min(1.f,distance/radius);
            const float magnitude=std::max(0.f,(raw-.12f)/.88f);
            dx_=distance>0?dx/distance*magnitude:0;dy_=distance>0?dy/distance*magnitude:0;
        }
        return true;
    }
    auto it=free_.find(id);if(it==free_.end())return false;
    if(phase==UiPointer::Up)free_.erase(it);
    else {const auto before=free_;it->second={x,y};updatePinch(before);}
    return false;
}
}
