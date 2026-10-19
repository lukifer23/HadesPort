#pragma once
#include "asset_store.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace hades {
using SceneHull=std::vector<std::array<float,2>>;
// Private decoded interchange formats, not the shipped map representation.
struct SceneSprite {
    // Runtime identity only; neither changes the immutable HDS3 packet layout.
    uint32_t sourceIndex=UINT32_MAX,animationClip=UINT32_MAX,animationFrame=UINT32_MAX;
    bool runtimeSuppressed=false; // Actual owner draw admission, independent of alpha.
    std::optional<std::array<float,3>> runtimeSortKey;
    uint32_t lightEffect=0,shadowTexture=0; // Runtime source Light shader class and polar texture; not HDS fields.
    uint32_t texture = 0, blend = 0;
    uint32_t drawGroup = UINT32_MAX;
    bool addColor = false;
    std::array<float,8> corners{}; // TL, TR, BL, BR, in world coordinates
    std::array<float,4> uv{0,0,1,1}, tint{1,1,1,1};
    std::array<float,3> hsv{};
    // Original Lighting3DFast vertex-color ambient contribution, independent
    // of user exposure. Unit multiplier when no verified companion exists.
    std::array<float,3> ambientMultiplier{1,1,1};
    std::array<float,2> parallaxOrigin{}; // original ground XY, excluding height
    float parallax=1;
};
struct GraphicsScene {
    uint32_t version = 0, textureCount = 0;
    uint64_t textureBytes = 0;
    std::vector<SceneSprite> sprites;
    std::vector<std::array<float,3>> order;
    std::array<float,4> bounds{};
    bool hasSourceAmbient=false;
    bool hasSourceComposition=false;
    bool hasSourceParallax=false;
    uint32_t parallaxSprites=0;
    bool hasSourceHulls=false;
    std::vector<SceneHull> textureHulls;
    std::vector<std::vector<SceneHull>> animationHulls;
    uint32_t ambientSprites=0;
};
using TextureConsumer = std::function<void(uint32_t,uint32_t,uint32_t,std::span<const uint8_t>)>;
// Streams at most one texture into CPU memory. Both Mac debugging and Android
// consume the same bounded decoder and transformed vertices.
GraphicsScene loadGraphicsScene(AssetStore&, const TextureConsumer&, uint32_t maxTexture = 16384);
SceneSprite rectangleSprite(uint32_t texture,float x,float y,float width,float height,float rotation=0);
struct SceneVertex { float x,y,u,v,r,g,b,a,h,s,value; };
struct SceneView { float left,top,width,height; };
std::array<SceneVertex,6> sceneVertices(const SceneSprite&,const SceneView&);
void applySceneParallax(SceneSprite&,const SceneView&);
void appendSceneVertices(const SceneSprite&,const SceneView&,std::span<const std::array<float,2>> hull,std::vector<SceneVertex>&);
}
