#pragma once
#include "graphics_scene.hpp"
#include "scene_animation.hpp"
#include "player.hpp"
#include "actor_animation.hpp"
#include <string>
#include <memory>
#include <unordered_map>

namespace hades {
// Backdrop comes from original room metadata. Exposure is a separate display
// preference; it does not replace the unreconstructed native lighting system.
struct RenderOptions {
    std::array<float,4> backdrop{0,0,0,1};
    float displayExposure=1;
    bool drawLights=true; // original ConfigOptions::DrawLights
};
// One original actor visual, uploaded synchronously from the bounded frame bank.
// localRect comes from frame.worldRect(clip); scale is relative to its source
// unit scale already included in that rectangle. x/y are ground coordinates.
struct LightShadowPolygon {uint64_t lifetime=0,revision=0;std::vector<std::array<float,2>> points;};
struct LightShadowGeometry {uint64_t revision=0;std::vector<LightShadowPolygon> polygons;};
struct ActorVisual {
    uint32_t id=0;float x=0,y=0,z=0,scale=1;
    ActorAnimationImage frame;
    std::array<float,4> localRect{};
    std::array<float,4> color{1,1,1,1};
    // New multi-actor path: actual immutable loaded-bank identity, allocation
    // lifetime, source draw angle/group/order. Reloading a bank requires clearing
    // renderer actor textures while its GL context is current.
    const void* sourceBank=nullptr;
    uint64_t lifetime=0;
    float angle=0;
    uint32_t drawGroup=10;
    int32_t sortIndex=0;
    std::optional<float> sourceSortY;
    uint32_t blend=0;
    bool addColor=false;
    std::array<float,4> flashColor{};
    std::array<float,3> hsv{};
    bool flipX=false,flipY=false;
    bool screenSpace=false; // Original1920x1080 UI coordinates; no world camera/Z.
    bool light=false,visible=true;
    float parallax=1;
    std::shared_ptr<const LightShadowGeometry> lightShadows;
    float postRotateScaleX=1,postRotateScaleY=1;
    // Additional native mesh transform, row-major, about the actual draw
    // anchor after ordinary rotation/post-scale and before viewport projection.
    std::array<float,4> meshTransform{1,0,0,1};
};
using ActorVisualProvider=std::function<bool(size_t,ActorVisual&,std::string&)>;
struct RenderStats {
    uint32_t sourceSprites=0,visibleSprites=0,drawCalls=0,groupPasses=0,frameUploads=0;
    uint64_t frameUploadBytes=0,vertexBytes=0;
    uint64_t actorCacheBytes=0;
    uint32_t actorCachedFrames=0;
};
// Context-independent ownership; initialize/shutdown require a current GL
// context. Mac capture uses desktop GL; Android uses GLES3 and the same shader.
class SceneRenderer {
    unsigned program_=0,vao_=0,buffer_=0,background_=0,hero_=0,white_=0;
    size_t skippedVisuals_=0; // visuals skipped by containment (logged once per reason)
    unsigned groupTexture_=0,groupFramebuffer_=0;
    int groupWidth_=0,groupHeight_=0;
    int backgroundWidth_=0,backgroundHeight_=0,blendLocation_=-1,addLocation_=-1;
    int postLocation_=-1,exposureLocation_=-1;
    int flashLocation_=-1;
    int regionLocation_=-1;
    int lightLocation_=-1,shadowResolutionLocation_=-1;
    uint32_t heroWidth_=0,heroHeight_=0,heroActiveWidth_=0,heroActiveHeight_=0;
    struct TextureRegion {uint32_t width=0,height=0,activeWidth=0,activeHeight=0;};
    std::vector<TextureRegion> textureRegions_;
    RenderStats stats_;
    RenderOptions options_;
    std::function<void(int,int)> fullscreenEffects_;
    uint32_t fullscreenEffectGroup_=UINT32_MAX;
    SceneFrameProvider sourceFrames_;
    SceneSubmission sourceSubmitted_;
    SceneSpriteProvider sourceSprites_;
    std::vector<unsigned> textures_;
    std::vector<unsigned> ownedTextures_;
    std::vector<std::array<unsigned,3>> streamTextures_;
    std::vector<uint32_t> streamSlots_;
    GraphicsScene scene_;
    SceneAnimation animations_;
    std::vector<SceneSprite> draw_;
    std::vector<SceneVertex> vertices_;
    std::vector<size_t> vertexOffsets_;
    const HeroClip* uploadedClip_=nullptr;
    uint32_t uploadedFrame_=UINT32_MAX,uploadedActorId_=UINT32_MAX,uploadedActorFrame_=UINT32_MAX;
    struct ActorTexture {const void* bank=nullptr;uint32_t frame=0,width=0,height=0;unsigned texture=0;uint64_t use=0;};
    std::vector<ActorTexture> actorTextures_;
    // (bank, frame) -> actorTextures_ slot; avoids a linear cache scan per visual.
    struct ActorKeyHash {size_t operator()(const std::pair<const void*,uint32_t>& k)const{return std::hash<const void*>()(k.first)^(size_t(k.second)*0x9E3779B97F4A7C15ull);}};
    std::unordered_map<std::pair<const void*,uint32_t>,size_t,ActorKeyHash> actorIndex_;
    uint64_t actorTextureBytes_=0,actorRender_=0;
    struct ShadowTexture {uint64_t lifetime=0,revision=0,use=0;std::array<float,8> corners{};std::array<float,4> uv{};unsigned texture=0;};
    std::vector<ShadowTexture> shadowTextures_;
    unsigned prepareShadow(const ActorVisual&,const SceneSprite&);
    uint32_t prepareActor(const ActorVisual&);
    void renderImpl(const SceneView&,int,int,const Player*,const ActorVisual*,double,size_t=0,const ActorVisualProvider& = {});
    void captureBackground(int width,int height);
    void ensureGroupTarget(int width,int height);
    void drawRange(size_t start,size_t count,const SceneSprite&,int width,int height,bool groupSourceOver=false);
public:
    void initialize(AssetStore&);
    void shutdown();
    void render(const SceneView&,int width,int height,const Player* player=nullptr,double seconds=0);
    void renderActor(const SceneView&,int width,int height,const ActorVisual*,double seconds=0);
    // Provider is called sequentially and each source frame uploads before the
    // next call. Never retain HAA1 sample spans across provider calls.
    void renderActors(const SceneView&,int width,int height,size_t count,const ActorVisualProvider&,double seconds=0);
    void clearActorTextures();
    void joystick(int width,int height,float x,float y,float dx,float dy);
    void setOptions(const RenderOptions&);
    void setSourceSceneProvider(SceneFrameProvider frames,SceneSubmission submitted,SceneSpriteProvider sprites={}){sourceFrames_=std::move(frames);sourceSubmitted_=std::move(submitted);sourceSprites_=std::move(sprites);}
    void setFullscreenEffects(uint32_t group,std::function<void(int,int)> draw){fullscreenEffectGroup_=group;fullscreenEffects_=std::move(draw);}
    const RenderOptions& options()const{return options_;}
    const GraphicsScene& scene()const{return scene_;}
    const SceneAnimation& animations()const{return animations_;}
    const RenderStats& stats()const{return stats_;}
};
}
