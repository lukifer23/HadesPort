#include "fullscreen_blur.hpp"
#include "body_transition.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace hades {
namespace {
// x86 maxss/minss return the second (source) operand when either is NaN.
float maxss(float a,float b){return a>b?a:b;}
float minss(float a,float b){return a<b?a:b;}
// IEEE quotient without relying on a host float divide-by-zero trap policy.
float quotient(float n,float d){if(d!=0)return n/d;if(n==0||std::isnan(n))return std::numeric_limits<float>::quiet_NaN();return (n>0)==!std::signbit(d)?std::numeric_limits<float>::infinity():-std::numeric_limits<float>::infinity();}
bool finite(float v,const char*what,std::string&error){if(std::isfinite(v))return true;error=std::string("Native fullscreen transition nonfinite ")+what;return false;}
// cvtdq2ps channel, (b-a)*t+a in single precision, cvttss2si then low byte.
uint8_t lerpByte(uint32_t from,uint32_t to,unsigned shift,float t){const float a=float((from>>shift)&0xff),b=float((to>>shift)&0xff);const float v=(b-a)*t+a;if(!(v>-2147483648.f&&v<2147483648.f))throw std::runtime_error("Native vignette color lerp outside cvttss2si range");return uint8_t(int32_t(v));}
uint32_t lerpPacked(uint32_t from,uint32_t to,float t){uint32_t out=0;for(unsigned shift:{0u,8u,16u,24u})out|=uint32_t(lerpByte(from,to,shift,t))<<shift;return out;}
}
void FloatTransitionHelper::reset(float from,float to,float seconds){start=from;end=to;duration=remaining=seconds;fraction=0;rate=1;active=true;}
void FloatTransitionHelper::setNewTarget(float to,float seconds){if(end==to)return;start=current();end=to;duration=remaining=seconds;fraction=0;}
float FloatTransitionHelper::current()const{if(duration==0)return end;return bodyInterpolate(start,end,fraction,BodyTransitionOptions{});}
void FloatTransitionHelper::step(float dt){remaining-=quotient(dt,rate);fraction=minss(maxss(1.f-quotient(remaining,duration),0.f),1.f);}

bool RadialBlurState::adjust(bool strength,float fraction,float duration,std::string&error){
 if(!finite(fraction,"blur fraction",error)||!finite(duration,"blur duration",error))return false;
 auto&h=strength?strength_:distance_;if(h.active)h.setNewTarget(fraction,duration);else h.reset(0,fraction,duration);error.clear();return true;
}
bool RadialBlurState::advance(float dt,std::string&error){
 if(!finite(dt,"App elapsed",error))return false;if(dt<0){error="Native App elapsed is negative";return false;}
 // ucomiss duration,0 (jp -> update), comiss 0,remaining (ja -> skip).
 for(auto [h,out]:{std::pair{&strength_,&sampleStrength_},std::pair{&distance_,&sampleDist_}})if(!(h->duration==0)&&!(0.f>h->remaining)){h->step(dt);*out=h->current();}
 if(!finite(sampleStrength_,"blur strength",error)||!finite(sampleDist_,"blur distance",error))return false;
 error.clear();return true;
}
bool VignetteFrameState::show(float fraction,float duration,uint32_t color,std::string&error){
 if(!finite(fraction,"frame fraction",error)||!finite(duration,"frame duration",error))return false;
 const float t=helper_.current();
 // Order at 0x180303d53..e71: color lerp to +0xf4 with old target, new target
 // to +0xf8, current fraction lerp to +0x3c with the old +0x40, new +0x40.
 currentColor_=lerpPacked(currentColor_,targetColor_,t);targetColor_=color;
 current_=(target_-current_)*t+current_;target_=fraction;
 helper_.reset(0,1,duration);error.clear();return true;
}
bool VignetteFrameState::advance(float dt,std::string&error){
 if(!finite(dt,"camera elapsed",error))return false;if(dt<0){error="Native camera elapsed is negative";return false;}
 helper_.step(dt);error.clear();return true;
}
VignetteFrameDraw VignetteFrameState::draw()const{
 VignetteFrameDraw out;const float t=helper_.current();
 out.radius=1.f-((target_-current_)*t+current_);
 const uint32_t packed=lerpPacked(currentColor_,targetColor_,t);
 for(unsigned k=0;k<4;++k)out.rgba[k]=uint8_t(packed>>(8*k));
 out.visible=out.rgba[3]!=0;return out;
}
}
