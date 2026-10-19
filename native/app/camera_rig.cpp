#include "camera_rig.hpp"
#include <algorithm>
#include <cmath>

namespace hades {
void CameraRig::update(float dt,float heroX,float heroY,float targetHeight,bool follow,bool pinching){
    sourceMode_=false;
    if(!std::isfinite(heroX)||!std::isfinite(heroY)||!std::isfinite(targetHeight))return;
    targetHeight_=std::clamp(targetHeight,minimumHeight,maximumHeight);
    if(!initialized_){height_=targetHeight_;x_=heroX;y_=heroY-height_*.08f;initialized_=true;return;}
    dt=std::isfinite(dt)?std::clamp(dt,0.f,.25f):0;
    const float zoomFactor=-std::expm1(-dt/ .09f),followFactor=-std::expm1(-dt/ .12f);
    height_+=(targetHeight_-height_)*zoomFactor;
    if(follow){
        if(!pinching){manualX_*=std::exp(-dt/ .6f);manualY_*=std::exp(-dt/ .6f);}
        x_+=(heroX+manualX_-x_)*followFactor;
        y_+=(heroY-height_*.08f+manualY_-y_)*followFactor;
    }
}
void CameraRig::updateSource(float dt,float x,float y,float nativeZoom,float userHeight,bool pinching){
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(nativeZoom)||nativeZoom<=0||!std::isfinite(userHeight))return;
    dt=std::isfinite(dt)?std::clamp(dt,0.f,.25f):0;
    targetHeight_=std::clamp(userHeight,minimumHeight,maximumHeight);
    // Native camera already owns its source interpolation/clamp. Android adds
    // only the explicit user zoom ratio and temporary pinch anchor offset.
    height_=1080.f/nativeZoom*(targetHeight_/1680.f);
    if(!pinching){manualX_*=std::exp(-dt/.6f);manualY_*=std::exp(-dt/.6f);}
    x_=x+manualX_;y_=y+manualY_;initialized_=sourceMode_=true;
}
float CameraRig::zoom(const ZoomGesture& gesture,int width,int screenHeight){
    if(!initialized_||width<=0||screenHeight<=0||!std::isfinite(gesture.ratio)||gesture.ratio<=0)return targetHeight_;
    const float old=targetHeight_,next=std::clamp(old*gesture.ratio,minimumHeight,maximumHeight);
    // Anchor the requested camera target beneath the pinch midpoint. Follow
    // gently recenters after release; pinch never owns or cancels the stick.
    const float oldVisible=sourceMode_?height_:old,nextVisible=oldVisible*next/old;
    const float sx=(gesture.fromX-width*.5f)*oldVisible/screenHeight-(gesture.toX-width*.5f)*nextVisible/screenHeight;
    const float sy=(gesture.fromY-screenHeight*.5f)*oldVisible/screenHeight-(gesture.toY-screenHeight*.5f)*nextVisible/screenHeight;
    manualX_+=sx;manualY_+=sy;x_+=sx;y_+=sy;
    targetHeight_=next;if(sourceMode_)height_=nextVisible;return next;
}
SceneView CameraRig::view(int width,int screenHeight)const{
    const float w=height_*std::max(1,width)/std::max(1,screenHeight);
    return {x_-w*.5f,y_-height_*.5f,w,height_};
}
}
