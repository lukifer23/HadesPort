#include "fullscreen_blur.hpp"
#include "body_transition.hpp"
#include <cmath>
namespace hades {
namespace {
// x86 maxss/minss: the second operand is returned when either is NaN.
float maxss(float a,float b){return a>b?a:b;}
float minss(float a,float b){return a<b?a:b;}
bool finite(float v,const char* what,std::string& error){if(std::isfinite(v))return true;error=std::string("Native fullscreen transition nonfinite ")+what;return false;}
uint8_t lerpByte(uint32_t from,uint32_t to,unsigned shift,float t){const float a=float((from>>shift)&0xff),b=float((to>>shift)&0xff);return uint8_t(int((b-a)*t+a));}
}
void FloatTransitionHelper::reset(float from,float to,float seconds){start=from;end=to;duration=remaining=seconds;fraction=0;rate=1;active=true;}
void FloatTransitionHelper::setNewTarget(float to,float seconds){if(end==to)return;start=current();end=to;duration=remaining=seconds;fraction=0;}
float FloatTransitionHelper::current()const{if(duration==0)return end;return bodyInterpolate(start,end,fraction,BodyTransitionOptions{});}
void FloatTransitionHelper::step(float dt){remaining-=dt/rate;fraction=minss(maxss(1.f-remaining/duration,0.f),1.f);}

bool RadialBlurState::adjust(bool strength,float fraction,float duration,std::string& error){
 if(!finite(fraction,"blur fraction",error)||!finite(duration,"blur duration",error))return false;
 auto& h=strength?strength_:distance_;if(h.active)h.setNewTarget(fraction,duration);else h.reset(0,fraction,duration);error.clear();return true;
}
bool RadialBlurState::advance(float dt,std::string& error){
 if(!finite(dt,"App elapsed",error)||dt<0){if(error.empty())error="Native App elapsed outside bound";return false;}
 // Strength (+0x10 -> +0x8) is updated before distance (+0x60 -> +0x4).
 if(strength_.duration!=0&&!(0.f>strength_.remaining)){strength_.step(dt);sampleStrength_=strength_.current();}
 if(distance_.duration!=0&&!(0.f>distance_.remaining)){distance_.step(dt);sampleDist_=distance_.current();}
 if(!finite(sampleStrength_,"blur strength",error)||!finite(sampleDist_,"blur distance",error))return false;
 error.clear();return true;
}
bool VignetteFrameState::show(float fraction,float duration,uint32_t color,std::string& error){
 if(!finite(fraction,"frame fraction",error)||!finite(duration,"frame duration",error))return false;
 const float t=helper_.current();
 uint32_t packed=0;for(unsigned shift:{0u,8u,16u,24u})packed|=uint32_t(lerpByte(currentColor_,targetColor_,shift,t))<<shift;
 currentColor_=packed;targetColor_=color;
 current_=(target_-current_)*t+current_;target_=fraction;
 helper_.reset(0,1,duration);error.clear();return true;
}
bool VignetteFrameState::advance(float dt,std::string& error){
 if(!finite(dt,"camera elapsed",error)||dt<0){if(error.empty())error="Native camera elapsed outside bound";return false;}
 helper_.step(dt);error.clear();return true;
}
VignetteFrameState::Draw VignetteFrameState::draw()const{
 Draw out;const float t=helper_.current();
 out.radius=1.f-((target_-current_)*t+current_);
 for(unsigned k=0;k<4;++k)out.rgba[k]=lerpByte(currentColor_,targetColor_,8*k,t);
 out.visible=out.rgba[3]!=0;return out;
}
}
