#pragma once
#include "fullscreen_blur.hpp"
namespace hades {
// GLES translation of the shipped EffectsForgeD3D11 RadialBlur.frag (embedded
// source shader-sources-v2/5cb76813e0a18f16-radialblur.frag, ShaderEffectType
// 0x2c) as drawn by BlurComponent::Draw 0x180220c20.
class FullscreenBlurRenderer {
 unsigned program_=0,copy_=0,vao_=0;int width_=0,height_=0;
 void initialize();
public:
 // Current draw framebuffer is input and output. Caller keeps the original
 // World::DoFullscreenEffects 0x18031d1b0 order: color grading, bloom, blur.
 void renderRadialBlur(const RadialBlurState&,int width,int height);
 // Camera::DrawVignetteFrame 0x180303e90 (Shroud 0x2f over Dev\blank). Only an
 // invisible frame (alpha byte 0, the case for every shipped AdjustFrame call)
 // is reconstructed; a visible one throws.
 void renderVignetteFrame(const VignetteFrameDraw&,int width,int height);
 void shutdown();
};
}
