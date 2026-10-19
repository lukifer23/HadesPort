#include "native_shake.hpp"
#include <cmath>
namespace hades {
namespace {
void pick(NativeShake&s,const NativeShakeRandom&random){
    // OffsetHelper.pickAngle1520c0: native reversal, bounded jitter, wrap.
    float angle=s.angle+3.1415927410125732421875f;
    if(!s.fixedDirection)angle=(float(random())*1.57079637050628662109375f-.785398185253143310546875f)+angle;
    angle-=std::floor(angle/6.283185482025146484375f)*6.283185482025146484375f;s.angle=angle;
    s.targetX=std::cos(angle)*s.distance+0.f;s.targetY=-(std::sin(angle)*s.distance)+0.f;
}
}
void NativeShake::initialize(uint64_t ownerLifetime,float newSpeed,float newDistance,float duration,float direction,const NativeShakeRandom&random){
    // Thing.Shake2e1dd4..2e1e84 constructs a fresh helper. It consumes one
    // RNG sample even when the supplied direction subsequently overrides it.
    *this={};lifetime=ownerLifetime;active=true;distance=newDistance;distanceSq=distance*distance;speed=newSpeed;remaining=duration;
    angle=float(random())*6.283185482025146484375f+0.f;if(direction>=0){angle=direction;fixedDirection=true;}
}
void NativeShake::advance(float elapsed,const NativeShakeRandom&random){
    if(!active)return;remaining-=elapsed;if(remaining<=0&&falloffSpeed>0)falling=true;
    if(falling){const float step=elapsed*falloffSpeed;if(step<std::fabs(10.f-speed)){if(speed<10)speed+=step;else if(speed>10)speed-=step;}else speed=10;if(speed==10){distance=0;targetX=targetY=0;}}
    // Native vector stepToward1ea990 normalizes first, then compares squared
    // displacement to squared distance (strict >). Preserve operation order.
    const float dx=targetX-currentX,dy=targetY-currentY;
    if(dx!=0||dy!=0){const float square=dx*dx+dy*dy,inverse=1.f/std::sqrt(square),step=elapsed*speed;const float sx=(inverse*dx)*step,sy=(inverse*dy)*step;
        if(sx*sx+sy*sy>square){currentX=targetX;currentY=targetY;}else{currentX+=sx;currentY+=sy;}}
    if(currentX==targetX&&currentY==targetY)pick(*this,random);
    if(speedRate!=0)speed=speedRate*elapsed+speed;if(distanceRate!=0){distance=distanceRate*elapsed+distance;distanceSq=distance*distance;}
    // ShakeSystem2d6274..2d62b8 disables display before returning false.
    if(falling?(targetX==0&&targetY==0&&currentX==0&&currentY==0):!(remaining>0))active=false;
}
}
