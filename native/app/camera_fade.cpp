#include "camera_fade.hpp"
#include <cmath>
#include <sstream>
namespace hades {
bool CameraFade::out(const std::array<float,4>& color,float duration,std::string& error){
    if(!std::isfinite(duration)){error="Nonfinite camera fade duration";return false;}
    for(float c:color)if(!std::isfinite(c)||c<0||c>1){error="Camera fade requires actual normalized source color";return false;}
    target_=color;if(duration<=0){current_=target_;speed_=0;}else speed_=1.f/duration;
    error.clear();return true;
}
bool CameraFade::in(float duration,std::string& error){
    if(!std::isfinite(duration)){error="Nonfinite camera fade duration";return false;}
    target_[3]=0;if(duration<=0){current_[3]=0;speed_=0;}else speed_=1.f/duration;
    error.clear();return true;
}
bool CameraFade::advance(float elapsed,std::string& error){
    if(!std::isfinite(elapsed)||elapsed<0){error="Camera fade requires actual nonnegative Camera elapsed";return false;}
    if(speed_!=0){const float step=elapsed*speed_;for(size_t i=0;i<4;++i){if(step>=std::abs(target_[i]-current_[i]))current_[i]=target_[i];else if(target_[i]>current_[i])current_[i]+=step;else if(current_[i]>target_[i])current_[i]-=step;}}
    error.clear();return true;
}
std::string CameraFade::dump()const{std::ostringstream o;o<<"CAMERA_FADE current=";for(float c:current_)o<<c<<',';o<<" target=";for(float c:target_)o<<c<<',';o<<" speed="<<speed_<<'\n';return o.str();}
}
