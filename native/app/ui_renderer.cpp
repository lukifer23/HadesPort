#include "ui_renderer.hpp"
#include "touch_controls.hpp"
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#define UI_SHADER_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#define UI_SHADER_HEADER "#version 330 core\n"
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace hades {
namespace {
constexpr std::array<float,4> gold{.88f,.72f,.39f,1},ivory{.94f,.91f,.82f,1},muted{.59f,.58f,.52f,1};
std::vector<uint8_t> read(AssetStore& assets,const std::string& name,size_t maximum){
    auto source=assets.open(name);if(!source||source->size()>maximum)throw std::runtime_error("Missing/oversized native UI asset: "+name);
    std::vector<uint8_t> bytes(source->size());size_t offset=0;
    while(offset<bytes.size()){const auto n=source->read(bytes.data()+offset,bytes.size()-offset);if(!n)throw std::runtime_error("Truncated native UI asset");offset+=n;}return bytes;
}
struct Reader {
    const std::vector<uint8_t>& bytes;size_t offset=0;
    template<class T>T take(){if(offset+sizeof(T)>bytes.size())throw std::runtime_error("Invalid native UI packet");T value;std::memcpy(&value,bytes.data()+offset,sizeof(T));offset+=sizeof(T);return value;}
};
unsigned shader(unsigned type,const char* source){
    unsigned value=glCreateShader(type);glShaderSource(value,1,&source,nullptr);glCompileShader(value);int good=0;glGetShaderiv(value,GL_COMPILE_STATUS,&good);
    if(!good){char message[2048]{};glGetShaderInfoLog(value,sizeof(message),nullptr,message);glDeleteShader(value);throw std::runtime_error(message);}return value;
}
struct Vertex{float x,y,u,v,r,g,b,a;};
}
void UiRenderer::initialize(AssetStore& assets){
    constexpr const char* vs=UI_SHADER_HEADER R"(
      layout(location=0) in vec2 position;layout(location=1) in vec2 uv;layout(location=2) in vec4 tint;
      out vec2 sampleUv;out vec4 multiplier;
      void main(){gl_Position=vec4(position,0.,1.);sampleUv=uv;multiplier=tint;})";
    constexpr const char* fs=UI_SHADER_HEADER R"(
      uniform sampler2D image;uniform int shape;uniform vec2 blurStep;in vec2 sampleUv;in vec4 multiplier;out vec4 color;
      void main(){vec4 c=texture(image,sampleUv)*multiplier;
        if(shape==1){float d=length(sampleUv*2.-1.);c.a*=1.-smoothstep(.97,1.,d);}
        if(shape==2){float d=length(sampleUv*2.-1.);c.a*=(1.-smoothstep(.98,1.,d))*smoothstep(.85,.88,d);}
        if(shape==3)c.a*=1.-sampleUv.x;
        if(shape==4){
          // Portable bitmap-font shadow filter. Source blur value controls
          // atlas-texel spread; original Alpha8 coverage and color are retained.
          float coverage=0.;for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            float weight=float((x==0?2:1)*(y==0?2:1))/16.;
            coverage+=texture(image,sampleUv+vec2(float(x),float(y))*blurStep).a*weight;}
          c=vec4(multiplier.rgb,coverage*multiplier.a);
        }
        color=vec4(c.rgb*c.a,c.a);})";
    const auto vert=shader(GL_VERTEX_SHADER,vs),frag=shader(GL_FRAGMENT_SHADER,fs);
    program_=glCreateProgram();glAttachShader(program_,vert);glAttachShader(program_,frag);glLinkProgram(program_);glDeleteShader(vert);glDeleteShader(frag);
    int good=0;glGetProgramiv(program_,GL_LINK_STATUS,&good);if(!good)throw std::runtime_error("UI shader link failed");
    shapeLocation_=glGetUniformLocation(program_,"shape");blurStepLocation_=glGetUniformLocation(program_,"blurStep");glUseProgram(program_);glUniform1i(glGetUniformLocation(program_,"image"),0);
    glGenVertexArrays(1,&vao_);glBindVertexArray(vao_);glGenBuffers(1,&buffer_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);
    for(unsigned i=0;i<3;++i)glEnableVertexAttribArray(i);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(2*sizeof(float)));
    glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(4*sizeof(float)));
    auto upload=[&](int width,int height,const void* pixels){unsigned texture;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels);return texture;};
    const uint8_t white[4]{255,255,255,255};white_=upload(1,1,white);
    for(const auto* name:{"font","background","logo","zagreus","selector","selector_backing"}){
        const auto bytes=read(assets,std::string("ui/")+name+".rgba",64*1024*1024);
        if(bytes.size()<12||std::memcmp(bytes.data(),"UIR1",4))throw std::runtime_error("Bad UI texture header");Reader reader{bytes,4};
        const auto w=reader.take<uint32_t>(),h=reader.take<uint32_t>();
        if(!w||!h||w>8192||h>8192||bytes.size()!=12+uint64_t(w)*h*4)throw std::runtime_error("Bad UI texture extent");
        textures_[name]={upload(w,h,bytes.data()+12),static_cast<int>(w),static_cast<int>(h)};
    }
    const auto font=read(assets,"ui/font.bin",4*1024*1024);
    if(font.size()<20||std::memcmp(font.data(),"UFN1",4))throw std::runtime_error("Bad original font header");Reader reader{font,4};
    const auto count=reader.take<uint32_t>();lineSpacing_=reader.take<int32_t>();spacing_=reader.take<float>();fallback_=reader.take<uint32_t>();
    if(count>65536||lineSpacing_<=0||!std::isfinite(spacing_)||font.size()!=20+uint64_t(count)*48)throw std::runtime_error("Bad original font metrics");
    const auto& atlas=textures_.at("font");
    for(uint32_t i=0;i<count;++i){const auto codepoint=reader.take<uint32_t>();Glyph glyph;
        for(auto& v:glyph.rect)v=reader.take<int32_t>();for(auto& v:glyph.crop)v=reader.take<int32_t>();for(auto& v:glyph.kerning)v=reader.take<float>();
        if(glyph.rect[0]<0||glyph.rect[1]<0||glyph.rect[2]<0||glyph.rect[3]<0||glyph.rect[0]+int64_t(glyph.rect[2])>atlas.width||glyph.rect[1]+int64_t(glyph.rect[3])>atlas.height)throw std::runtime_error("Original glyph outside atlas");
        for(float v:glyph.kerning)if(!std::isfinite(v))throw std::runtime_error("Nonfinite original font metric");
        if(!glyphs_.emplace(codepoint,glyph).second)throw std::runtime_error("Duplicate original glyph");
    }
}
void UiRenderer::shutdown(){
    for(auto& [name,t]:textures_)if(t.id)glDeleteTextures(1,&t.id);textures_.clear();glyphs_.clear();
    if(white_)glDeleteTextures(1,&white_);if(buffer_)glDeleteBuffers(1,&buffer_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);
    white_=buffer_=vao_=program_=0;
}
void UiRenderer::quad(UiRect rect,const std::array<float,4>& c,unsigned texture,std::array<float,4> uv,int shape){
    const float x0=rect.x/width_*2-1,x1=(rect.x+rect.w)/width_*2-1,y0=1-rect.y/height_*2,y1=1-(rect.y+rect.h)/height_*2;
    const Vertex a{x0,y0,uv[0],uv[1],c[0],c[1],c[2],c[3]},b{x1,y0,uv[2],uv[1],c[0],c[1],c[2],c[3]},
        d{x0,y1,uv[0],uv[3],c[0],c[1],c[2],c[3]},e{x1,y1,uv[2],uv[3],c[0],c[1],c[2],c[3]};
    const Vertex vertices[]{a,b,d,b,e,d};glUniform1i(shapeLocation_,shape);glBindTexture(GL_TEXTURE_2D,texture?texture:white_);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,6);
}
void UiRenderer::image(const char* name,UiRect box,float opacity){
    const auto& texture=textures_.at(name);quad(box,{1,1,1,opacity},texture.id);
}
void UiRenderer::text(const std::string& value,float x,float y,float size,std::array<float,4> color,bool centered){
    const float scale=size/lineSpacing_;float extent=0;bool first=true;
    auto glyph=[&](uint32_t cp)->const Glyph&{auto at=glyphs_.find(cp);if(at==glyphs_.end())at=glyphs_.find(fallback_);if(at==glyphs_.end())throw std::runtime_error("Original font fallback missing");return at->second;};
    for(unsigned char cp:value){const auto& g=glyph(cp);extent+=first?std::max(0.f,g.kerning[0]):spacing_+g.kerning[0];extent+=g.kerning[1]+g.kerning[2];first=false;}
    if(centered)x-=extent*scale*.5f;float offset=0;first=true;const auto& atlas=textures_.at("font");
    for(unsigned char cp:value){const auto& g=glyph(cp);offset+=first?std::max(0.f,g.kerning[0]):spacing_+g.kerning[0];first=false;
        quad({x+(offset+g.crop[0])*scale,y+g.crop[1]*scale,g.rect[2]*scale,g.rect[3]*scale},color,atlas.id,
             {float(g.rect[0])/atlas.width,float(g.rect[1])/atlas.height,float(g.rect[0]+g.rect[2])/atlas.width,float(g.rect[1]+g.rect[3])/atlas.height});
        offset+=g.kerning[1]+g.kerning[2];}
}
void UiRenderer::renderCameraFade(const std::array<float,4>& rgba,int width,int height){
    if(rgba[3]<=0||width<=0||height<=0)return;
    width_=float(width)*1080/height;height_=1080;
    glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);glActiveTexture(GL_TEXTURE0);
    quad({0,0,width_,height_},rgba);
}
void UiRenderer::renderText(const std::vector<TextVisual>& visuals,const TextFontBank& fonts,int width,int height,const SceneView& view){
    if(width<=0||height<=0||view.width<=0||view.height<=0)return;
    width_=float(width)*1080/height;height_=1080;
    glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);glActiveTexture(GL_TEXTURE0);glUniform1i(shapeLocation_,0);
    for(const auto& visual:visuals){
        if(visual.glyphs.empty()&&visual.symbols.empty())continue;if(visual.glyphs.size()>16384||visual.symbols.size()>1024)throw std::runtime_error("Actual text glyph draw bound");
        const auto font=fonts.fonts().find(visual.font);if(font==fonts.fonts().end())throw std::runtime_error("Actual source text atlas unavailable: "+visual.font);
        const auto key="text-font:"+visual.font;auto cached=textures_.find(key);
        if(cached==textures_.end()){
            const auto& source=font->second;if(!source.width||!source.height||source.rgba.size()!=size_t(source.width)*source.height*4)throw std::runtime_error("Original source font atlas extent");
            unsigned texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,source.width,source.height,0,GL_RGBA,GL_UNSIGNED_BYTE,source.rgba.data());
            cached=textures_.emplace(key,Texture{texture,int(source.width),int(source.height)}).first;
        }
        // Screen-space text uses the same uniform fit of the original 1920x1080
        // reference as screen visuals (Android adaptation, no stretch).
        const float fit=std::min(width_/1920.f,1.f),sx=visual.screenSpace?fit:width_/view.width,sy=visual.screenSpace?fit:1080.f/view.height;
        const float ox=visual.screenSpace?visual.ownerX+(width_/fit-1920.f)*.5f:(visual.ownerX-view.left),oy=visual.screenSpace?visual.ownerY+(1080.f/fit-1080.f)*.5f:(visual.ownerY-visual.ownerZ*.8660254037844386f-view.top);
        auto pass=[&](const std::array<float,4>& color,float offsetX,float offsetY,bool blur=false){
            if(color[3]<=0)return;std::vector<Vertex> vertices;vertices.reserve(visual.glyphs.size()*6);
            for(const auto& glyph:visual.glyphs){
                const float x=(ox+glyph.x+offsetX)*sx,y=(oy+glyph.y+offsetY)*sy,w=glyph.w*sx,h=glyph.h*sy;
                if(x+w<0||y+h<0||x>width_||y>1080)continue;
                const std::array<std::array<float,4>,4> corners{{{x,y,glyph.u0,glyph.v0},{x+w,y,glyph.u1,glyph.v0},{x,y+h,glyph.u0,glyph.v1},{x+w,y+h,glyph.u1,glyph.v1}}};
                for(int index:{0,1,2,2,1,3}){const auto& p=corners[index];vertices.push_back({2*p[0]/width_-1,1-2*p[1]/1080,p[2],p[3],color[0],color[1],color[2],color[3]});}
            }
            if(vertices.empty())return;glUniform1i(shapeLocation_,blur?4:0);if(blur)glUniform2f(blurStepLocation_,visual.shadowBlur/cached->second.width,visual.shadowBlur/cached->second.height);glBindTexture(GL_TEXTURE_2D,cached->second.id);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,GLsizei(vertices.size()));
        };
        if(!std::isfinite(visual.shadowBlur)||visual.shadowBlur<0||visual.shadowBlur>50||!std::isfinite(visual.outlineThickness)||visual.outlineThickness<0||visual.outlineThickness>32)throw std::runtime_error("Source text filter draw bounds");
        pass(visual.shadowColor,visual.shadowX,visual.shadowY,visual.shadowBlur>0);
        if(visual.outlineThickness>0){const float r=visual.outlineThickness;for(const auto& d:std::array<std::array<float,2>,8>{{{-r,0},{r,0},{0,-r},{0,r},{-r,-r},{r,-r},{-r,r},{r,r}}})pass(visual.outlineColor,d[0],d[1]);}
        pass(visual.color,0,0);
        // Inline original symbols (GUIComponentTextBox::DrawLine "@path"):
        // white vertex colour carrying only the text alpha.
        for(const auto& symbol:visual.symbols){
            const auto source=fonts.symbols().find(symbol.texture);if(source==fonts.symbols().end())throw std::runtime_error("Actual source text symbol unavailable: "+symbol.texture);
            const auto skey="text-symbol:"+symbol.texture;auto st=textures_.find(skey);
            if(st==textures_.end()){unsigned texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,source->second.width,source->second.height,0,GL_RGBA,GL_UNSIGNED_BYTE,source->second.rgba.data());st=textures_.emplace(skey,Texture{texture,int(source->second.width),int(source->second.height)}).first;}
            const float x=(ox+symbol.x)*sx,y=(oy+symbol.y)*sy,w=symbol.w*sx,h=symbol.h*sy;
            const Vertex a{2*x/width_-1,1-2*y/1080,0,0,1,1,1,visual.color[3]},b{2*(x+w)/width_-1,1-2*y/1080,1,0,1,1,1,visual.color[3]},c{2*x/width_-1,1-2*(y+h)/1080,0,1,1,1,1,visual.color[3]},d{2*(x+w)/width_-1,1-2*(y+h)/1080,1,1,1,1,1,visual.color[3]};
            const Vertex vertices[]{a,b,c,c,b,d};glUniform1i(shapeLocation_,0);glBindTexture(GL_TEXTURE_2D,st->second.id);glBufferData(GL_ARRAY_BUFFER,sizeof vertices,vertices,GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,6);
        }
    }
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Original text glyph draw GL failure");
}
void UiRenderer::renderFrameCounter(const std::string& line,int width,int height,bool warn){
    if(width<=0||height<=0||line.empty())return;
    width_=float(width)*1080/height;height_=1080;
    glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);glActiveTexture(GL_TEXTURE0);
    quad({18,16,float(line.size())*11.5f+28,40},{.0f,.0f,.0f,.55f});
    text(line,32,20,24,warn?std::array<float,4>{1.f,.55f,.35f,1}:std::array<float,4>{.75f,.95f,.70f,1});
}
void UiRenderer::render(const GameUi& ui,int width,int height,double seconds,float dx,float dy,bool stickActive,float stickX,float stickY,const TouchControls* touch){
    width_=float(width)*1080/height;height_=1080;
    glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);glActiveTexture(GL_TEXTURE0);
    if(ui.screen()==UiScreen::Title){
        image("background",{0,0,width_,1080});
        const auto& zag=textures_.at("zagreus");const float h=1080,w=h*zag.width/zag.height;
        image("zagreus",{width_-w,0,w,h});
        quad({0,0,width_*.72f,1080},{.012f,.017f,.018f,.85f},0,{0,0,1,1},3);
        const auto& logo=textures_.at("logo");const float lw=std::min(760.f,width_*.47f),lh=lw*logo.height/logo.width;
        image("logo",{width_*.095f-12,210,lw,lh});
        quad({width_*.095f,540,420,2},{.75f,.56f,.28f,.45f});
    }else if(ui.loading()||ui.screen()==UiScreen::LoadFailed){
        image("background",{0,0,width_,1080});quad({0,0,width_,1080},{.012f,.018f,.02f,.55f});
        const bool failed=ui.screen()==UiScreen::LoadFailed;
        text(failed?"UNABLE TO ENTER":"ENTERING TARTARUS",width_*.5f,failed?330:390,62,gold,true);
        if(failed){
            // Exact engine reason, word-wrapped to two lines (diagnostic, not a hint).
            std::string detail=ui.failureDetail.empty()?"Please try again.":ui.failureDetail,line;std::vector<std::string> lines;
            // Drop the generic Lua dispatch prefixes so the specific cause fits.
            for(const std::string prefix:{"Original update: ","Scripts/Main.lua:419: "})while(detail.starts_with(prefix))detail.erase(0,prefix.size());
            for(size_t at=0;at<=detail.size()&&lines.size()<2;){size_t next=detail.find(' ',at);if(next==std::string::npos)next=detail.size();const auto word=detail.substr(at,next-at);
                if(!line.empty()&&line.size()+1+word.size()>72){lines.push_back(line);line.clear();}line+=(line.empty()?"":" ")+word;at=next+1;}
            if(!line.empty()&&lines.size()<2)lines.push_back(line);
            for(size_t i=0;i<lines.size();++i)text(lines[i],width_*.5f,420+float(i)*40,26,ivory,true);
        }
        else{
            const float progress=std::clamp(ui.loadingProgress,0.f,1.f),w=std::min(580.f,width_-200);
            quad({(width_-w)*.5f,515,w,4},{.15f,.18f,.16f,1});
            quad({(width_-w)*.5f,515,w*progress,4},gold);
            text("Drag near the joystick to move. Pinch above to zoom.",width_*.5f,615,26,muted,true);
        }
    }else if(ui.screen()==UiScreen::Paused){
        quad({0,0,width_,1080},{.014f,.019f,.021f,.83f});
        text("PAUSED",width_*.5f,260,86,gold,true);
        quad({width_*.5f-210,376,420,2},{.75f,.56f,.28f,.5f});
    }else if(ui.screen()==UiScreen::Settings){
        quad({0,0,width_,1080},{.015f,.022f,.025f,.96f});
        const float panel=std::min(1240.f,width_-160),left=(width_-panel)*.5f;
        text("SETTINGS",left,162,80,gold);
        quad({left,294,panel,1},{.67f,.55f,.34f,.35f});
    }
    if(ui.screen()==UiScreen::Remap){
        quad({0,0,width_,1080},{.015f,.022f,.025f,.96f});
        const float panel=std::min(1240.f,width_-160),left=(width_-panel)*.5f;
        text("CONTROLS",left,140,62,gold);
        text("GAMEPAD",left+panel*.61f,205,26,muted,true);text("KEYBOARD",left+panel*.86f,205,26,muted,true);
        if(ui.remapCaptureRow>=0)text(ui.remapCaptureDevice==0?"Press a controller button. Back cancels.":"Press a key. Back cancels.",width_*.5f,1040,24,ivory,true);
    }
    const bool editing=ui.screen()==UiScreen::LayoutEdit;
    if(editing){
        quad({0,0,width_,1080},{.015f,.022f,.025f,.92f});
        text("BUTTON LAYOUT",width_*.5f,150,62,gold,true);
        text("Drag a button or the joystick to move it. Changes save automatically.",width_*.5f,235,28,muted,true);
    }
    const auto controls=ui.controls(width_);
    for(const auto& item:controls){const auto& r=item.bounds;const bool pressed=ui.pressed()==item.id;
        if(item.id==1){
            quad(r,{.03f,.035f,.035f,.65f},0,{0,0,1,1},1);quad({r.x+27,r.y+25,8,32},gold);quad({r.x+47,r.y+25,8,32},gold);continue;
        }
        if(item.kind==UiControl::Kind::Button){
            quad(r,pressed?std::array<float,4>{.30f,.22f,.11f,.85f}:std::array<float,4>{.025f,.032f,.033f,.82f});
            if(pressed)quad(r,{.75f,.56f,.26f,.3f},textures_.at("selector_backing").id);
            quad({r.x,r.y,3,r.h},pressed?ivory:gold);
            quad({r.x+3,r.y+r.h-1,r.w-3,1},{.64f,.49f,.26f,.45f});
            text(item.label,r.x+30,r.y+(r.h-50)*.5f,50,pressed?ivory:gold);continue;
        }
        if(item.kind==UiControl::Kind::BindCell){
            const float panel=std::min(1240.f,width_-160),left=(width_-panel)*.5f;
            if(!item.label.empty())text(item.label,left+3,r.y+6,30,ivory);
            const bool capturing=item.fraction>0;
            quad(r,capturing?std::array<float,4>{.30f,.22f,.11f,.85f}:(pressed?std::array<float,4>{.14f,.16f,.14f,1}:std::array<float,4>{.07f,.09f,.085f,1}));
            quad({r.x,r.y+r.h-2,r.w,2},capturing?gold:std::array<float,4>{.30f,.27f,.20f,.6f});
            text(item.value,r.x+r.w*.5f,r.y+6,28,capturing?ivory:gold,true);continue;
        }
        if(item.kind==UiControl::Kind::Tab){
            const bool on=item.fraction>0;
            if(on)quad({r.x+8,r.y,r.w-16,r.h},{.30f,.22f,.11f,.55f});
            quad({r.x+8,r.y+r.h-3,r.w-16,3},on?gold:std::array<float,4>{.30f,.27f,.20f,.6f});
            text(item.label,r.x+r.w*.5f,r.y+10,34,on||pressed?ivory:muted,true);continue;
        }
        const float panel=std::min(1240.f,width_-160),left=(width_-panel)*.5f;
        text(item.label,left+3,r.y+15,38,ivory);
        if(item.kind==UiControl::Kind::Slider){
            quad({r.x,r.y+38,r.w,5},{.16f,.18f,.17f,1});quad({r.x,r.y+38,r.w*item.fraction,5},gold);
            quad({r.x+r.w*item.fraction-12,r.y+28,24,24},ivory,0,{0,0,1,1},1);
            text(item.value,r.x+r.w,r.y-8,26,muted,true);
        }else{
            const float box=std::max(130.f,float(item.value.size())*19.f+36);
            quad({r.x+r.w-box,r.y+18,box,48},{.09f,.12f,.11f,1});
            text(item.value,r.x+r.w-box*.5f,r.y+23,35,item.fraction>0?gold:muted,true);
        }
        quad({left,r.y+105,panel,1},{.18f,.22f,.20f,.55f});
    }
    if(ui.playing()||editing){
        auto s=ui.settings;if(!editing)s.touchOpacity*=std::clamp(ui.controlsVisibility,0.f,1.f);const float px=1080.f/height,radius=touch?touch->stickRadius()*px:86*s.touchScale;
        const float x=touch?touch->x()*px:(stickActive&&stickX>=0?stickX:(s.leftHanded?width_-190:190)),y=touch?touch->y()*px:(stickActive&&stickY>=0?stickY:864);
        const float a=s.touchOpacity*(stickActive?1.f:.7f);
        quad({x-radius,y-radius,radius*2,radius*2},{.055f,.07f,.064f,a*.65f},0,{0,0,1,1},1);
        quad({x-radius,y-radius,radius*2,radius*2},{.76f,.67f,.43f,a},0,{0,0,1,1},2);
        const float knob=radius*.43f;quad({x+dx*radius*.57f-knob,y+dy*radius*.57f-knob,knob*2,knob*2},{.85f,.78f,.56f,a},0,{0,0,1,1},1);
        if(touch)for(const auto& button:touch->actionButtons()){
            const float scale=1080.f/height,cx=button.x*scale,cy=button.y*scale,r=button.radius*scale;
            const float opacity=s.touchOpacity*(button.held?1.f:.75f);
            const UiRect box{cx-r,cy-r,2*r,2*r};
            quad(box,button.held?std::array<float,4>{.45f,.31f,.10f,opacity}:std::array<float,4>{.035f,.045f,.04f,opacity*.8f},0,{0,0,1,1},1);
            quad(box,{.83f,.70f,.44f,opacity},0,{0,0,1,1},2);
            const float size=std::clamp(r*(button.held?.40f:.37f),13.f,26.f);
            text(button.label,cx,cy-size*.6f,size,{.94f,.89f,.74f,opacity},true);
        }
    }
    (void)seconds;
}
}
