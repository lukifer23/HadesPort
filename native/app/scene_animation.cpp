#include "scene_animation.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace hades {
namespace {
void read(AssetFile& file,void* target,size_t bytes){
    if(bytes>file.remaining())throw std::runtime_error("Truncated HDA1 animation");
    auto* p=static_cast<uint8_t*>(target);
    while(bytes){const auto got=file.read(p,bytes);if(!got)throw std::runtime_error("HDA1 read failed");p+=got;bytes-=got;}
}
uint32_t integer(AssetFile& file){uint32_t v;read(file,&v,4);return v;}
float real(AssetFile& file){float v;read(file,&v,4);if(!std::isfinite(v))throw std::runtime_error("Non-finite HDA1 metadata");return v;}
size_t frameIndex(const EffectClip& clip,double seconds){
    const auto frames=clip.frames.size();
    const double tick=std::floor(seconds*clip.fpsMin);
    if(tick<frames)return static_cast<size_t>(tick);
    if(clip.flags&3)return clip.loopStart+static_cast<size_t>(std::fmod(tick-clip.loopStart,frames-clip.loopStart));
    return frames-1;
}
}
void SceneAnimation::reset(){file_.reset();clips_.clear();bindings_.clear();pixels_.clear();}
const EffectBinding* SceneAnimation::binding(uint32_t sourceSprite)const{for(const auto& b:bindings_)if(b.sprite==sourceSprite)return &b;return nullptr;}
ActorAnimationImage SceneAnimation::sample(uint32_t clip,uint32_t index){
    const auto& frame=clips_.at(clip).frames.at(index);const size_t bytes=size_t(frame.width)*frame.height*4;
    if(bytes>ActorAnimationBank::MaxFrameBytes||frame.width>2048||frame.height>2048)throw std::runtime_error("Original scene frame exceeds bounded per-instance cache");
    pixels_.resize(bytes);if(!file_->seek(frame.offset))throw std::runtime_error("Original scene frame seek failed");read(*file_,pixels_.data(),bytes);
    ActorAnimationImage image;image.canvasWidth=image.width=frame.width;image.canvasHeight=image.height=frame.height;image.packetFrameIndex=(clip<<16)|index;image.originalMovieFrame=frame.originalNumber;image.rgba=pixels_;return image;
}
void SceneAnimation::load(AssetStore& store,const GraphicsScene& scene){
    reset();file_=store.open("animations.bin");if(!file_){if(scene.hasSourceHulls&&!scene.animationHulls.empty())throw std::runtime_error("Missing animation data for source hulls");return;}
    if(scene.version!=3)throw std::runtime_error("HDA1 requires HDS3 scene");
    if(integer(*file_)!=0x31414448)throw std::runtime_error("Invalid HDA1 version");
    const auto count=integer(*file_),bindingCount=integer(*file_);
    if(!count||count>128||bindingCount>scene.sprites.size())throw std::runtime_error("HDA1 counts outside bounds");
    if(scene.hasSourceHulls&&scene.animationHulls.size()!=count)throw std::runtime_error("HDA1 source hull clip count mismatch");
    std::array<uint8_t,32> bound{},expected{};read(*file_,bound.data(),bound.size());
    auto identity=store.open("scene-sha256.bin");
    if(!identity||identity->size()!=expected.size())throw std::runtime_error("Missing scene identity for HDA1");
    read(*identity,expected.data(),expected.size());
    if(bound!=expected)throw std::runtime_error("HDA1 is bound to a different HDS3 scene");
    uint64_t frameBytes=0;
    for(uint32_t i=0;i<count;++i){
        EffectClip clip;const auto length=integer(*file_);
        if(!length||length>4096)throw std::runtime_error("HDA1 name outside bounds");
        clip.name.resize(length);read(*file_,clip.name.data(),length);
        const auto frames=integer(*file_);clip.fpsMin=real(*file_);clip.fpsMax=real(*file_);clip.flags=integer(*file_);clip.loopStart=integer(*file_);
        if(!frames||frames>10000||clip.fpsMin<0||clip.fpsMax<clip.fpsMin||clip.fpsMax>1000||clip.flags>15||clip.loopStart>=frames)throw std::runtime_error("Invalid HDA1 clip metadata");
        if(scene.hasSourceHulls&&scene.animationHulls[i].size()!=frames)throw std::runtime_error("HDA1 source hull frame count mismatch");
        for(uint32_t n=0;n<frames;++n){
            EffectFrame frame{integer(*file_),integer(*file_),integer(*file_),0};
            if(!frame.width||!frame.height||frame.width>16384||frame.height>16384)throw std::runtime_error("HDA1 dimensions outside bounds");
            const uint64_t bytes=uint64_t(frame.width)*frame.height*4;frameBytes+=bytes;
            if(bytes>64ull*1024*1024||bytes>file_->remaining()||frameBytes>1024ull*1024*1024)throw std::runtime_error("HDA1 frame data outside budget");
            frame.offset=file_->size()-file_->remaining();
            if(!file_->seek(frame.offset+static_cast<size_t>(bytes)))throw std::runtime_error("HDA1 requires seekable bounded assets");
            clip.frames.push_back(frame);
        }
        clips_.push_back(std::move(clip));
    }
    std::vector<bool> used(scene.sprites.size());
    uint64_t totalQuads=0;
    for(uint32_t i=0;i<bindingCount;++i){
        EffectBinding binding{integer(*file_),integer(*file_),{}};const auto frames=integer(*file_);
        if(binding.sprite>=scene.sprites.size()||binding.clip>=clips_.size()||frames!=clips_[binding.clip].frames.size()||used[binding.sprite])throw std::runtime_error("Invalid HDA1 sprite binding");
        totalQuads+=frames;if(totalQuads>1000000)throw std::runtime_error("HDA1 binding geometry outside budget");
        used[binding.sprite]=true;
        for(uint32_t n=0;n<frames;++n){std::array<float,8> quad;for(auto& v:quad)v=real(*file_);binding.quads.push_back(quad);}
        bindings_.push_back(std::move(binding));
    }
    if(file_->remaining())throw std::runtime_error("Unexpected HDA1 suffix");
    log(Severity::Info,"HadesGraphics","ANIMATIONS_READY clips=%zu bindings=%zu original_rgba_bytes=%llu phase=0 fps=min random_flags=pending",clips_.size(),bindings_.size(),static_cast<unsigned long long>(frameBytes));
}
void SceneAnimation::apply(double seconds,std::vector<SceneSprite>& sprites,uint32_t textureBase,const TextureConsumer& upload){
    if(!std::isfinite(seconds)||seconds<0||seconds>1.e12)throw std::runtime_error("Invalid effect animation time");
    for(uint32_t i=0;i<clips_.size();++i){
        auto& clip=clips_[i];const auto index=frameIndex(clip,seconds);const auto& frame=clip.frames[index];
        if(clip.uploadedFrame==index)continue;
        const size_t bytes=size_t(frame.width)*frame.height*4;pixels_.resize(bytes);
        if(!file_->seek(frame.offset))throw std::runtime_error("HDA1 frame seek failed");
        read(*file_,pixels_.data(),bytes);upload(textureBase+i,frame.width,frame.height,pixels_);clip.uploadedFrame=static_cast<uint32_t>(index);
    }
    for(const auto& binding:bindings_){
        if(binding.sprite>=sprites.size())throw std::runtime_error("Animation scene was replaced");
        auto& sprite=sprites[binding.sprite];sprite.texture=textureBase+binding.clip;sprite.corners=binding.quads[clips_[binding.clip].uploadedFrame];sprite.animationClip=binding.clip;sprite.animationFrame=clips_[binding.clip].uploadedFrame;
    }
}
}
