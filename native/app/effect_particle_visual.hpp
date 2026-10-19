#pragma once
#include "effect_particle.hpp"
#include "scene_renderer.hpp"
#include <cmath>
#include <exception>

namespace hades {
struct EffectParticleDrawContext {
    bool sourceResolved=false;
    uint32_t group=0,blend=0;
    int32_t sortIndex=0;
    float sortY=0;
    std::array<float,4> color{};
    bool addColor=false;
    // Required only for native ColorFromOwner Multiply(default0). Packed
    // components preserve the original integer /255 rounding at each step.
    std::optional<std::array<uint8_t,4>> ownerBaseColor,ownerFractionColor;
    std::optional<std::array<float,3>> ownerHsv; // Base + live HSV transitions.
};
// Call only inside renderActors' sequential provider: the next imageBank sample
// may reuse its decoded span. Actual source owner/group/sort/color is mandatory.
inline bool effectParticleVisual(EffectParticleWorld& world,uint64_t handle,
    const EffectParticleDrawContext& context,ActorVisual& out,std::string& error){
 try{
  auto i=world.particles().find(handle);if(i==world.particles().end()||!world.alive(handle)||!context.sourceResolved){error="Actual live particle/source draw state unresolved";return false;}
  const auto& p=i->second;const auto imageBank=p.imageBank?p.imageBank:world.imageBank();if(!imageBank){error="Original particle image bank missing";return false;}auto& bank=*imageBank;const auto& clip=bank.clips().at(p.imageClip);
  if(!std::isfinite(p.frame)||p.frame<1||size_t(p.frame)>clip.slides.size()){error="Original particle draw frame outside source bank";return false;}
  out={};out.id=uint32_t(handle);out.lifetime=handle;out.sourceBank=&bank;out.x=p.x;out.y=p.y+EffectParticleWorld::drawOffsetY(p);out.z=p.baseZ+p.offsetZ;out.angle=EffectParticleWorld::drawAngle(p);out.scale=EffectParticleWorld::drawScale(p);
  out.frame=bank.sample(p.imageClip,size_t(p.frame)-1,bank.selectedAngleForFacing(p.imageClip,p.heading),false);out.localRect=out.frame.worldRect(clip);out.color=context.color;
  // For source-resolved base color, context contains actual fraction/material
  // modulation only. Reapplying attached owner baseRGBA would multiply twice.
  if(p.sourceColorResolved){
   const auto* policy=p.sourceDefinition.get("ColorFromOwner");const bool inherit=!policy||policy->text()=="Multiply";
   if(inherit&&(!context.ownerBaseColor||!context.ownerFractionColor||!context.ownerHsv)){error="Actual owner packed base/fraction color and current HSV required for native Multiply inheritance";return false;}
   for(size_t c=0;c<4;++c){uint32_t channel=p.baseColor[c];if(inherit){channel=channel*(*context.ownerBaseColor)[c]/255;channel=channel*(*context.ownerFractionColor)[c]/255;}out.color[c]*=float(channel)/255.f;}
   for(size_t c=0;c<3;++c)out.hsv[c]=p.baseHsv[c]+(inherit?(*context.ownerHsv)[c]:0);
  }
  out.drawGroup=context.group;out.blend=context.blend;out.addColor=context.addColor;out.sortIndex=context.sortIndex;out.sourceSortY=context.sortY;return true;
 }catch(const std::exception& ex){error=ex.what();return false;}
}
}
