#pragma once
#include <array>
#include <cstdint>
#include <limits>
namespace hades {
// GUIFlasher.Init167ed0, Update151e20, GetFlashedOpacityColor16b960.
struct NativeFlash {
    uint64_t lifetime=0;
    bool enabled=false,updating=true,expireAfterCycle=false,previousIncreasing=true;
    float speed=1,minimum=.25f,maximum=.65f,previous=0;
    float remaining=std::numeric_limits<float>::max(),fixed=-1,target=-1,phase=-.7853981852531433f;
    std::array<uint8_t,4> color{255,255,255,255};
    void advance(float elapsed);
    float value()const;
    std::array<float,4> opacityColor()const;
};
}
