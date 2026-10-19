#pragma once
#include "asset_store.hpp"
#include "game_ui.hpp"
#include "text_layout.hpp"
#include "graphics_scene.hpp"
#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace hades {
class TouchControls;
class UiRenderer {
    struct Texture {unsigned id=0;int width=0,height=0;};
    struct Glyph {std::array<int32_t,4> rect{},crop{};std::array<float,3> kerning{};};
    unsigned program_=0,vao_=0,buffer_=0,white_=0;
    int shapeLocation_=-1,blurStepLocation_=-1;
    std::map<std::string,Texture> textures_;
    std::map<uint32_t,Glyph> glyphs_;
    int lineSpacing_=1;
    float spacing_=0,width_=0,height_=1080;
    unsigned fallback_='?';
    void quad(UiRect,const std::array<float,4>&,unsigned texture=0,std::array<float,4> uv={0,0,1,1},int shape=0);
    void text(const std::string&,float x,float y,float size,std::array<float,4> color,bool centered=false);
    void image(const char*,UiRect,float opacity=1);
public:
    void initialize(AssetStore&);
    void shutdown();
    // Android performance overlay: smoothed FPS and frame-time percentiles.
    void renderFrameCounter(const std::string& line,int width,int height,bool warn);
    void renderCameraFade(const std::array<float,4>&,int width,int height);
    void renderText(const std::vector<TextVisual>&,const TextFontBank&,int width,int height,const SceneView&);
    void render(const GameUi&,int width,int height,double seconds,float stickDx=0,float stickDy=0,bool stickActive=false,float stickX=-1,float stickY=-1,const TouchControls* touch=nullptr);
};
}
