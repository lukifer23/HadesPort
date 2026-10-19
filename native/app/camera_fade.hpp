#pragma once
#include <array>
#include <string>
namespace hades {
// Actual Camera.mGoalColor/currentColor/mColorChangeSpeed. Fade is a constant
// per-channel step, independent of Body's easing transition helper.
class CameraFade {
    std::array<float,4> target_{},current_{};
    float speed_=0;
public:
    bool out(const std::array<float,4>& rgba,float duration,std::string& error);
    bool in(float duration,std::string& error);
    bool advance(float actualCameraElapsed,std::string& error);
    const std::array<float,4>& color()const{return current_;}
    std::string dump()const;
};
}
