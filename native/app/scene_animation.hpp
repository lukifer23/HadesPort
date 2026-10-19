#pragma once
#include "graphics_scene.hpp"
#include "actor_animation.hpp"
#include <string>

namespace hades {
struct EffectFrame { uint32_t originalNumber,width,height;size_t offset; };
struct EffectClip {
    std::string name;
    float fpsMin=0,fpsMax=0;
    uint32_t flags=0,loopStart=0;
    std::vector<EffectFrame> frames;
    uint32_t uploadedFrame=UINT32_MAX;
};
struct EffectBinding {uint32_t sprite,clip;std::vector<std::array<float,8>> quads;};
struct SceneFrameSelection {uint32_t clip=0,frame=0;uint64_t lifetime=0;};
using SceneFrameProvider=std::function<bool(uint32_t,std::optional<SceneFrameSelection>&,std::string&)>;
using SceneSubmission=std::function<bool(uint32_t,std::string&)>;
using SceneSpriteProvider=std::function<bool(uint32_t,SceneSprite&,std::string&)>;
class SceneAnimation {
    std::unique_ptr<AssetFile> file_;
    std::vector<EffectClip> clips_;
    std::vector<EffectBinding> bindings_;
    std::vector<uint8_t> pixels_;
public:
    // HDA1 retains source random flags; playback currently chooses phase0 and
    // fpsMin deterministically. Original RNG call order is still unresolved.
    void load(AssetStore&,const GraphicsScene&);
    void apply(double seconds,std::vector<SceneSprite>&,uint32_t textureBase,const TextureConsumer&);
    void reset();
    // Samples one actual frame at a time; caller uploads before another sample.
    ActorAnimationImage sample(uint32_t clip,uint32_t frame);
    const EffectBinding* binding(uint32_t sourceSprite)const;
    size_t clipCount()const{return clips_.size();}
    size_t bindingCount()const{return bindings_.size();}
    const std::vector<EffectClip>& clips()const{return clips_;}
};
}
