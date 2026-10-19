#include "body_transition.hpp"
#include <algorithm>
#include <cmath>
namespace hades {
namespace {
float interpolate(float start,float end,float t,const BodyTransitionOptions& o){
    if(o.easeIn>0||o.easeOut>0){
        const float a=o.easeIn*3.f,b=o.easeOut*3.f,one=1.f-t;
        const float left=(a*t)*(one*one),right=((b*(t*t))*one),cube=(t*t)*t;
        const float f=float(double(left)+double((one*one*one)*0.f)+double(right)+double(cube));
        return f*(end-start)+start;
    }
    if(!o.smoothStep)return (end-start)*t+start;
    const double x=std::clamp(t,0.f,1.f),s=start,e=end;
    if(x==0)return start;if(x==1)return end;
    const double square=x*x,cube=square*x;
    // Preserve the original double-precision Hermite branch's operation order.
    return float(((s+s)-(e+e))*cube+(e*3.-s*3.)*square+0.*x+s);
}
}
float bodyInterpolate(float start,float end,float t,const BodyTransitionOptions& o){return interpolate(start,end,t,o);}
float BodyTransition::current()const{
    if(options_.duration==0)return end_;
    if(!options_.pingPong)return interpolate(start_,end_,fraction_,options_);
    float t=fraction_+phase_;if(t<0)t=1.f-(0.f-t);else if(t>1)t=(t-1.f)+0.f;
    return t<.5f?interpolate(start_,end_,t+t,options_):interpolate(end_,start_,(t-.5f)+(t-.5f),options_);
}
bool BodyTransition::adjust(float target,float initial,const BodyTransitionOptions& o,bool& changed,std::string& error){
    for(float n:{target,initial,o.duration,o.easeIn,o.easeOut})if(!std::isfinite(n)){error="Nonfinite native Body transition";return false;}
    if(o.duration<0){error="Negative Body transition duration outside reconstructed subset";return false;}
    changed=false;if(initialized_&&end_==target){error.clear();return true;}
    start_=initialized_?current():initial;end_=target;remaining_=o.duration;fraction_=0;phase_=0;
    options_=o;initialized_=true;active_=o.duration!=0;changed=true;error.clear();return true;
}
bool BodyTransition::advance(float elapsed,float undo,std::string& error){
    if(!std::isfinite(elapsed)||elapsed<0||!std::isfinite(undo)||undo<=0){error="Body phase requires finite elapsed and positive actual UndoModifier";return false;}
    if(!active_){error.clear();return true;}
    remaining_-=elapsed/undo;fraction_=std::clamp(1.f-remaining_/options_.duration,0.f,1.f);
    // Native BodySystem invokes its update callback before strict removal.
    // Exactly zero remaining stays in the active vector for the next frame.
    active_=options_.duration!=0&&remaining_>=0;error.clear();return true;
}
}
