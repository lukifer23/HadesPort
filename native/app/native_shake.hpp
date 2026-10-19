#pragma once
#include <array>
#include <cstdint>
#include <functional>
namespace hades {
using NativeShakeRandom=std::function<double()>;
// OffsetHelper (56 bytes natively); Thing ShakeComponent owns a distinct
// instance from Camera.Shake and animation ECS ShakeOffset components.
struct NativeShake {
    uint64_t lifetime=0;
    bool active=false,fixedDirection=false,falling=false;
    float distanceSq=0,remaining=0,distance=0,speed=0,speedRate=0,distanceRate=0,angle=0,falloffSpeed=0;
    float targetX=0,targetY=0,currentX=0,currentY=0;
    void initialize(uint64_t,float speed,float distance,float duration,float angle,const NativeShakeRandom&);
    void advance(float,const NativeShakeRandom&);
    std::array<float,2> offset()const{return active?std::array<float,2>{currentX,currentY}:std::array<float,2>{};}
};
}
