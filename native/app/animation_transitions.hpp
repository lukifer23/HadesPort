#pragma once
#include "effect_particle.hpp"
#include <cmath>

namespace hades {
// AnimationData::Read 0x180090212/0x18009026d: authored degrees are
// multiplied in double precision, then stored as native float radians.
inline float animationRadians(float degrees){
#ifdef __clang__
#pragma clang fp contract(off)
#endif
 return float(double(degrees)*0.017453292519943295);
}

// InitData29217d..2922fd / BodyComponentTransitionHelper::CalcCurrentValue
// 2b5420. Equal endpoints leave the scale transition inactive (modifier1).
inline float animationScaleTransition(float start,float end,float fraction,float easeIn,float easeOut,bool pingPong){
#ifdef __clang__
#pragma clang fp contract(off)
#endif
 if(start==end)return 1;
 if(pingPong)return fraction<.5f?EffectParticleWorld::interpolate(start,end,fraction*2,easeIn,easeOut):EffectParticleWorld::interpolate(end,start,(fraction-.5f)*2,easeIn,easeOut);
 return EffectParticleWorld::interpolate(start,end,fraction,easeIn,easeOut);
}

// Inputs start/end are already native radians; phase remains authored degrees.
// InitData2928ab..292941 wraps only the end angle. Adjust2b7440 and
// CalcCurrentValue2b5420 apply the phase only for a ping-pong transition.
inline float animationAngleTransition(float start,float end,float fraction,float easeIn,float easeOut,bool pingPong,float phaseDegrees){
#ifdef __clang__
#pragma clang fp contract(off)
#endif
 if(start==end)return 0;
 const float tau=6.2831854820251465f;const volatile float q=end/tau;const volatile float m=std::floor(float(q))*tau;const float wrappedEnd=end-m;
 if(!pingPong)return EffectParticleWorld::interpolate(start,wrappedEnd,fraction,easeIn,easeOut);
 float phase=phaseDegrees;if(0.f>phase)phase=360.f-(0.f-phase);else if(phase>360.f)phase=phase-360.f+0.f;phase*=0.0027777778450399637f;
 float f=phase+fraction;if(0.f>f)f=1.f-(0.f-f);else if(f>1.f)f=f-1.f+0.f;
 return .5f>f?EffectParticleWorld::interpolate(start,wrappedEnd,f+f,easeIn,easeOut):EffectParticleWorld::interpolate(wrappedEnd,start,(f-.5f)+(f-.5f),easeIn,easeOut);
}
}
