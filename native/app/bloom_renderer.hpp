#pragma once
#include "bloom_state.hpp"
#include <array>
namespace hades {
struct BloomKernel {std::array<float,15> weights{},offsets{};};
BloomKernel makeBloomKernel(float blur);
class BloomRenderer {
 std::array<unsigned,3> programs_{};std::array<unsigned,3> textures_{};std::array<unsigned,2> targets_{};unsigned vao_=0;int width_=0,height_=0;
 void initialize();void resize(int,int);
public:
 // Current draw framebuffer is input/output. Caller applies original group
 // ordering: color grading, bloom, then the later scene groups.
 void render(const FullscreenBloom&,bool drawBloom,int width,int height);
 void shutdown();
};
}
