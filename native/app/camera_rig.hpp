#pragma once
#include "graphics_scene.hpp"
#include "touch_controls.hpp"

namespace hades {
// Responsive Android camera policy, separate from the unreconstructed original
// cinematic camera. Follow/zoom smoothing uses elapsed time, not frame count.
class CameraRig {
    bool initialized_=false;
    float x_=0,y_=0,height_=1680,targetHeight_=1680;
    float manualX_=0,manualY_=0;
    bool sourceMode_=false;
public:
    static constexpr float minimumHeight=900,maximumHeight=3200;
    void reset(){initialized_=sourceMode_=false;manualX_=manualY_=0;}
    void update(float dt,float heroX,float heroY,float targetHeight,bool follow=true,bool pinching=false);
    void updateSource(float dt,float centerX,float centerY,float nativeZoom,float userHeight,bool pinching=false);
    float zoom(const ZoomGesture&,int width,int height);
    SceneView view(int width,int height)const;
    float height()const{return height_;}
};
}
