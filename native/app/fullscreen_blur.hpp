#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace hades {
// sgg::TransitionHelper<float> (size 0x50) fields used by the BlurComponent
// and Camera vignette helpers: +0x30 active, +0x34 start, +0x38 end,
// +0x3c duration, +0x40 remaining, +0x44 fraction, +0x48 rate. Reset/Set use
// no ease, smoothstep, ping-pong or callback (Reset 0x1800551f0 clears them).
struct FloatTransitionHelper {
 bool active=false;float start=0,end=0,duration=0,remaining=0,fraction=0,rate=0;
 // TransitionHelper<float>::Reset 0x1800551f0 (phase 0): rate 1, active.
 void reset(float from,float to,float seconds);
 // TransitionHelper<float>::SetNewTarget 0x180220ba0: no-op when end is
 // unchanged; otherwise start=CalcCurrentValue, duration=remaining, fraction 0.
 void setNewTarget(float to,float seconds);
 // TransitionHelper<float>::CalcCurrentValue 0x1800550f0: duration 0 -> end,
 // else linear sgg::Interpolate (Reset cleared ease/smooth).
 float current()const;
 // Inlined update (App::Update 0x18003714c, Camera::Update 0x1803025d1):
 // remaining -= dt/rate; fraction = minss(maxss(1-remaining/duration,0),1).
 void step(float dt);
};
// sgg::BlurComponent (App+0xc8, allocated by App::OnStart 0x180034e00, effect
// type byte 0x2c RadialBlur). +0x4 sampleDist, +0x8 sampleStrength; helpers at
// +0x60 (distance) and +0x10 (strength). All zero at construction.
class RadialBlurState {
 FloatTransitionHelper strength_,distance_;float sampleDist_=0,sampleStrength_=0;
public:
 // ScriptAction::fractionForDuration 0x18025cac0, action 38 Strength / 39
 // Distance: active helper -> SetNewTarget(fraction,duration), else
 // Reset(0,fraction,duration).
 bool adjust(bool strength,float fraction,float duration,std::string&);
 // App::Update 0x18003712e..0x18003720d: each helper updates only while
 // duration != 0 and remaining >= 0; the drawn scalar is then CalcCurrentValue.
 bool advance(float appElapsed,std::string&);
 float sampleDist()const{return sampleDist_;}
 float sampleStrength()const{return sampleStrength_;}
 // World::DoFullscreenEffects 0x18031d24c: drawn only when both are nonzero.
 bool shouldDraw()const{return sampleDist_!=0&&sampleStrength_!=0;}
};
// Camera vignette frame (Camera+0x3c current fraction, +0x40 target fraction,
// +0xf4 current packed RGBA, +0xf8 target packed RGBA, +0x140 helper).
// Camera::Reset 0x180301519: helper Reset(1,1,0); colors 0x00ffffff; 0/0.
class VignetteFrameState {
 FloatTransitionHelper helper_;float current_=0,target_=0;uint32_t currentColor_=0x00ffffffu,targetColor_=0x00ffffffu;
public:
 VignetteFrameState(){helper_.reset(1,1,0);}
 // Camera::ShowVignetteFrame 0x180303d30 (AdjustFrame, Execute case 160).
 bool show(float fraction,float duration,uint32_t packedColor,std::string&);
 // Camera::Update 0x1803025d1: unconditional helper step on the camera delta.
 bool advance(float cameraElapsed,std::string&);
 // GameplayScreen::Draw 0x1801b0de2..0x1801b0f04: Radius = 1 - lerp(current,
 // target,t); color = per-channel truncated lerp; DrawVignetteFrame
 // 0x180303e90 skips a zero alpha byte.
 struct Draw {bool visible=false;float radius=0;std::array<uint8_t,4> rgba{};};
 Draw draw()const;
};
// ScriptArgs::GetColor 0x180281f00 packed RGBA (R lowest byte).
inline uint32_t packRgba(const std::array<uint8_t,4>& c){return uint32_t(c[0])|uint32_t(c[1])<<8|uint32_t(c[2])<<16|uint32_t(c[3])<<24;}
}
