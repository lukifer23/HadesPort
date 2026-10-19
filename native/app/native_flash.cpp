#include "native_flash.hpp"
#include <cmath>
namespace hades {
float NativeFlash::value()const{return fixed>=0?fixed:minimum+(std::sin(phase+phase)*.5f+.5f)*(maximum-minimum);}
void NativeFlash::advance(float elapsed){
    remaining-=elapsed;const float step=elapsed*speed;phase+=step;
    if(fixed==target){if(fixed>0&&expireAfterCycle)remaining=-1;}
    else if(step>=std::fabs(target-fixed))fixed=target;
    else if(target>fixed)fixed+=step;else if(fixed>target)fixed-=step;
    const float current=value();const bool increasing=current>previous;
    if(expireAfterCycle&&!previousIncreasing&&increasing)remaining=-1;
    previousIncreasing=increasing;previous=current;
}
std::array<float,4> NativeFlash::opacityColor()const{
    // PrepDraw (Book2afc14, Constant counterpart) ignores expired flashers.
    if(!enabled||remaining<0)return {};
    // Source16b9c5 truncates opacity*255 then stores its low byte.
    const float scaled=value()*255.f;const int32_t packed=std::isfinite(scaled)&&scaled>=-2147483648.f&&scaled<2147483648.f?int32_t(std::trunc(scaled)):INT32_MIN;
    const auto alpha=uint8_t(packed&255);
    return {float(color[0])/255.f,float(color[1])/255.f,float(color[2])/255.f,float(alpha)/255.f};
}
}
