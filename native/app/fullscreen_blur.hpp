#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace hades {
// sgg::TransitionHelper<float> fields driven by the BlurComponent and Camera
// vignette helpers: +0x30 active, +0x34 start, +0x38 end, +0x3c duration,
// +0x40 remaining, +0x44 fraction, +0x48 rate (+0x20/+0x28 callback, cleared
// by Reset and therefore never invoked on these paths).
struct FloatTransitionHelper {
 bool active=false;float start=0,end=0,duration=0,remaining=0,fraction=0,rate=0;
 // TransitionHelper<float>::Reset 0x1800551f0 (phase 0): start/end,
 // duration=remaining, fraction 0, ease/smooth/pingPong cleared, rate 1, active.
 void reset(float from,float to,float seconds);
 // TransitionHelper<float>::SetNewTarget 0x180220ba0: no-op when end is
 // unchanged; otherwise start=CalcCurrentValue, end, duration=remaining, fraction 0.
 void setNewTarget(float to,float seconds);
 // TransitionHelper<float>::CalcCurrentValue 0x1800550f0: duration 0 -> end,
 // else sgg::Interpolate with the cleared (linear) options.
 float current()const;
 // Inlined step (App::Update 0x180037156..181, Camera::Update 0x1803025d1..626):
 // remaining -= dt/rate; fraction = minss(maxss(1-remaining/duration,0),1).
 void step(float dt);
};
// sgg::BlurComponent (App+0xc8): +0x4 sampleDist, +0x8 sampleStrength, strength
// helper +0x10 (active +0x40), distance helper +0x60 (active +0x90). Zeroed at
// construction; App-owned, so map transitions do not reset it.
class RadialBlurState {
 FloatTransitionHelper strength_,distance_;float sampleDist_=0,sampleStrength_=0;
public:
 // ScriptAction::fractionForDuration 0x18025cac0 (Execute 0x180252396 passes
 // getFraction, getDuration): action 38 Strength -> helper +0x10, 39 Distance ->
 // helper +0x60; active helper -> SetNewTarget(fraction,duration), else
 // Reset(0,fraction,duration). The drawn scalar is NOT written here.
 bool adjust(bool strength,float fraction,float duration,std::string&);
 // App::Update 0x18003712e..0x180037212 on App's own elapsed (its float
 // argument, also handed to ScreenManager::Update): strength first, then
 // distance; each steps only while duration!=0 and !(0>remaining), then stores
 // CalcCurrentValue into +0x8 / +0x4.
 bool advance(float appElapsed,std::string&);
 float sampleDist()const{return sampleDist_;}
 float sampleStrength()const{return sampleStrength_;}
 // World::DoFullscreenEffects 0x18031d24c..26c and BlurComponent::Draw
 // 0x180220c4b..c74: drawn only when strength and distance are both nonzero
 // (an unordered NaN counts as nonzero).
 bool shouldDraw()const{return !(sampleStrength_==0)&&!(sampleDist_==0);}
};
// GameplayScreen::Draw 0x1801b0de2..0x1801b0f04 inputs to Camera::DrawVignetteFrame.
struct VignetteFrameDraw {bool visible=false;float radius=0;std::array<uint8_t,4> rgba{};};
// Camera vignette frame: Camera+0x3c current fraction, +0x40 target fraction,
// +0xf4 current packed RGBA, +0xf8 target packed RGBA (R lowest byte), helper +0x140.
// Camera::Reset 0x180301519..0x180301564: helper Reset(1,1,0), both colors
// 0x00ffffff, fractions 0.
class VignetteFrameState {
 FloatTransitionHelper helper_;float current_=0,target_=0;uint32_t currentColor_=0x00ffffffu,targetColor_=0x00ffffffu;
public:
 VignetteFrameState(){helper_.reset(1,1,0);}
 // Camera::ShowVignetteFrame 0x180303d30 (AdjustFrame, Execute case 160 at
 // 0x180254015: GetColor, getDuration, getFraction).
 bool show(float fraction,float duration,uint32_t packedColor,std::string&);
 // Camera::Update 0x1803025d1: unconditional helper step on the camera elapsed.
 bool advance(float cameraElapsed,std::string&);
 // GameplayScreen::Draw: Radius = 1 - lerp(current,target,t); color = per-byte
 // truncated lerp; Camera::DrawVignetteFrame 0x180303edd skips alpha byte 0.
 VignetteFrameDraw draw()const;
};
}
