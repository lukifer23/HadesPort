#pragma once
#include "fullscreen_blur.hpp"
namespace hades {
// GLES port of the shipped RadialBlur.frag (EffectsForgeD3D11, DXBC-embedded
// source shader-sources-v2/5cb76813e0a18f16) and Shroud.vert/Shroud.frag
// (a6fec70332f4c2da), used by BlurComponent::Draw 0x180220c20 and
// Camera::DrawVignetteFrame 0x180303e90 (GameplayScreen effect byte 0x2f Shroud).
class FullscreenBlurRenderer {
 unsigned blurProgram_=0,shroudProgram_=0,blankTexture_=0,copy_=0,vao_=0;int width_=0,height_=0;
 void initialize();
public:
 // Current draw framebuffer is input and output (after bloom, DoFullscreenEffects order).
 void renderRadialBlur(const RadialBlurState&,int width,int height);
 // Full-viewport rectangle drawn over the composed world (GameplayScreen::Draw).
 void renderVignetteFrame(const VignetteFrameState::Draw&,int width,int height);
 void shutdown();
};
}
