#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hades {
struct NativeCameraTarget {int32_t id=0;uint64_t lifetime=0;float x=0,y=0;};
using NativeCameraResolve=std::function<bool(int32_t,NativeCameraTarget&,std::string&)>;
// sgg::AxisPath point, 0x1c bytes: +0 value, +4 normalized time, +8 type byte
// (1 = Catmull-Rom, 2 = cubic Bezier, other = linear), +0xc/+0x10 in-handle,
// +0x14/+0x18 out-handle (value,time). Only the value lanes enter Interpolate.
struct NativeSplinePoint {float value=0,time=0;uint8_t type=0;float inValue=0,inTime=0,outValue=0,outTime=0;};
// sgg::SplineHelper at Camera+0x238 (zoom): points vector +0x00, segment
// index +0x18, start/end value +0x20/+0x24, start world time +0x28, duration +0x2c.
struct NativeCameraSpline {std::vector<NativeSplinePoint> points;size_t index=0;float start=0,end=0,startTime=0,duration=0;};
struct NativeCameraState {
    bool initialized=false,lazyTether=false,dynamicZoom=false;
    float x=0,y=0,zoom=1,goalZoom=1,zoomSpeed=1;
    float offsetX=0,offsetY=0;
    std::vector<NativeCameraTarget> targets;
    // Two markers form the native rectangular clamp; more markers form an
    // ordered polygon, rebuilt from actual live marker locations each call.
    std::vector<NativeCameraTarget> clampTargets;
    float softClamp=0;
    uint32_t viewportWidth=1920,viewportHeight=1080;
    std::map<int32_t,std::pair<NativeCameraTarget,float>> zoomWeights;
    NativeCameraSpline zoomSpline; // Camera+0x238..0x267 (Camera::Focus)
    // sgg::CameraSplineHelper at Camera+0x1d0 (CameraSplineHelper::SetPan
    // 0x1801e6ab0): byte +0x1d0 moving-hold flag, X SplineHelper +0x1d8
    // (start/end/startTime/duration +0x1f8/+0x1fc/+0x200/+0x204), Y SplineHelper
    // +0x208 (+0x228/+0x22c/+0x230/+0x234). The moving-hold flag is set only by
    // ScriptAction MovingHoldCamera (Execute VA180252e88..f98), which no original
    // script calls and which is not registered natively, so it stays false here.
    NativeCameraSpline panX,panY;
    // Camera shake (sgg::OffsetHelper at Camera+0x268; Camera::Shake 0x180303970).
    struct Shake {
        bool fixedDirection=false,active=false,falling=false;
        float distanceSq=0,durationRemaining=0,distance=0,speed=0,speedRate=0,distanceRate=0,angle=0,falloffSpeed=0;
        float targetX=0,targetY=0,currentX=0,currentY=0;
    } shake;
    bool shaking=false; // Camera+0x3
    // Camera::GetLocation 0x180304fe0 adds currentX/Y while shaking applies.
    float shakeOffsetX()const{return shakeVisible()?shake.currentX:0.f;}
    float shakeOffsetY()const{return shakeVisible()?shake.currentY:0.f;}
    bool shakeVisible()const{return shake.falling?(shake.targetX!=0||shake.targetY!=0||shake.currentX!=0||shake.currentY!=0):shake.durationRemaining>0;}
};
using NativeCameraRandom=std::function<double()>; // engine uniform [0,1) (FUN_180054f80)
class NativeCamera {
    NativeCameraState state_;
    float time_=0,startTime_=0,duration_=0,startX_=0,startY_=0,speed_=0,acceleration_=0;
    bool resolveTargets(const std::vector<int32_t>&,const NativeCameraResolve&,std::vector<NativeCameraTarget>&,std::string&)const;
    bool applyClamp(const NativeCameraResolve&,std::string&);
    bool weightedZoom(const NativeCameraResolve&,float&,std::string&)const;
    // Camera::GetTetherLocation 0x1803021b0 (mean of live tether targets + Camera+0x50 offset).
    bool tetherLocation(const NativeCameraResolve&,float&x,float&y,size_t&count,std::string&)const;
public:
    const NativeCameraState& state()const{return state_;}
    bool setViewport(uint32_t width,uint32_t height,std::string&);
    bool lock(const std::vector<int32_t>&,float duration,float speed,float acceleration,float offsetX,float offsetY,bool lazy,bool dynamic,const NativeCameraResolve&,std::string&);
    // Camera::Pan 0x1803018b0 (ScriptAction PanCamera 0x180260bf0).
    bool pan(const std::vector<int32_t>&,float duration,float easeIn,float easeOut,float offsetX,float offsetY,bool retarget,bool resetWeights,bool fromCurrentLocation,const NativeCameraRandom&,const NativeCameraResolve&,std::string&);
    bool unlock(std::string&);
    bool adjustZoom(float fraction,float lerpTime,std::string&,const NativeCameraResolve& resolve={});
    // Camera::Focus 0x180303240 (ScriptAction FocusCamera, Execute VA180252d66..e1c).
    bool focus(float fraction,float duration,bool relative,const std::string& zoomType,const NativeCameraRandom&,std::string&);
    bool setZoomWeight(const std::vector<int32_t>&,float weight,float zoomSpeed,const NativeCameraResolve&,std::string&);
    bool setClamps(const std::vector<int32_t>&,float soft,const NativeCameraResolve&,std::string&);
    bool advance(float elapsed,const NativeCameraResolve&,std::string&);
    // ScriptAction ShakeScreen -> Camera::Shake(speed,distance,duration,angleRadians|<0 random,falloff).
    void shake(float speed,float distance,float duration,float angleRadians,float falloffSpeed,bool cameraShakeOption,const NativeCameraRandom&);
    // Camera::Update 0x1803030d8..107: OffsetHelper::Update then isDone; the
    // CameraShake option off also clears the shaking flag.
    void advanceShake(float elapsed,bool cameraShakeOption,const NativeCameraRandom&);
};
}
