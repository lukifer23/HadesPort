#pragma once
#include "color_grading.hpp"
#include <array>
namespace hades {
class ColorGradingRenderer {
    unsigned program_=0,vao_=0,source_=0;
    int width_=0,height_=0;
    struct VolumeTexture {unsigned texture=0;const ColorVolume* volume=nullptr;};
    std::array<VolumeTexture,2> volumes_{};
    void initialize();
    unsigned upload(size_t,const ColorVolume*);
public:
    // Current draw framebuffer is the input and output; this bounded copy
    // avoids sampling a texture attached to the active draw target.
    void render(const ColorGrading&,int width,int height);
    void shutdown();
};
}
