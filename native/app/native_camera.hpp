#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace hades {
struct NativeCameraTarget {int32_t id=0;uint64_t lifetime=0;float x=0,y=0;};
using NativeCameraResolve=std::function<bool(int32_t,NativeCameraTarget&,std::string&)>;
struct NativeCameraState {
    bool initialized=false,lazyTether=false,dynamicZoom=false;
    float x=0,y=0,zoom=1,goalZoom=1,zoomSpeed=1;
    float offsetX=0,offsetY=0;
    std::vector<NativeCameraTarget> targets;
    // Two-Thing source rectangular clamp. Wider polygon/rail paths remain
    // explicit unsupported branches.
    std::vector<NativeCameraTarget> clampTargets;
    float softClamp=0;
    uint32_t viewportWidth=1920,viewportHeight=1080;
    std::map<int32_t,std::pair<NativeCameraTarget,float>> zoomWeights;
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
public:
    const NativeCameraState& state()const{return state_;}
    bool setViewport(uint32_t width,uint32_t height,std::string&);
    bool lock(const std::vector<int32_t>&,float duration,float speed,float acceleration,float offsetX,float offsetY,bool lazy,bool dynamic,const NativeCameraResolve&,std::string&);
    bool pan(const std::vector<int32_t>&,bool retarget,const NativeCameraResolve&,std::string&);
    bool unlock(std::string&);
    bool adjustZoom(float fraction,float lerpTime,std::string&,const NativeCameraResolve& resolve={});
    bool focus(float fraction,float duration,bool relative,const std::string& zoomType,std::string&,const NativeCameraResolve& resolve={});
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
