#include "scene_renderer.hpp"
#include "platform_log.hpp"
#include <set>
#ifdef __ANDROID__
#include <GLES3/gl3.h>
#define SHADER_HEADER "#version 300 es\nprecision highp float;\n"
#else
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#define SHADER_HEADER "#version 330 core\n"
#endif
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace hades {
namespace {
unsigned compile(GLenum type,const char* text){
    const auto shader=glCreateShader(type);glShaderSource(shader,1,&text,nullptr);glCompileShader(shader);
    GLint good=0;glGetShaderiv(shader,GL_COMPILE_STATUS,&good);
    if(!good){char detail[4096]{};glGetShaderInfoLog(shader,sizeof(detail),nullptr,detail);glDeleteShader(shader);throw std::runtime_error(std::string("Scene shader: ")+detail);}return shader;
}
void configureTexture(unsigned texture){
    glBindTexture(GL_TEXTURE_2D,texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
}
bool sameMaterial(const SceneSprite& a,const SceneSprite& b){return a.texture==b.texture&&a.blend==b.blend&&a.addColor==b.addColor&&a.lightEffect==b.lightEffect&&a.shadowTexture==b.shadowTexture;}
uint32_t textureExtent(uint32_t value){uint32_t result=1;while(result<value)result*=2;return result;}
bool visible(const SceneSprite& sprite,const SceneView& view){
    if(sprite.runtimeSuppressed)return false;
    float left=sprite.corners[0],right=left,top=sprite.corners[1],bottom=top;
    for(size_t i=1;i<4;++i){left=std::min(left,sprite.corners[2*i]);right=std::max(right,sprite.corners[2*i]);top=std::min(top,sprite.corners[2*i+1]);bottom=std::max(bottom,sprite.corners[2*i+1]);}
    return right>=view.left&&left<=view.left+view.width&&bottom>=view.top&&top<=view.top+view.height;
}
}
void SceneRenderer::setOptions(const RenderOptions& options){
    for(float v:options.backdrop)if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("Invalid render backdrop");
    if(!std::isfinite(options.displayExposure)||options.displayExposure<=0||options.displayExposure>4)throw std::runtime_error("Invalid display exposure");
    options_=options;
}
void SceneRenderer::initialize(AssetStore& assets){
    if(program_)throw std::runtime_error("Scene renderer already initialized");
    try {
    if(auto file=assets.open("render-options.bin")){
        std::array<uint8_t,24> packet{};
        if(file->remaining()!=packet.size())throw std::runtime_error("Render options packet size mismatch");
        size_t position=0;
        while(position<packet.size()){
            const auto got=file->read(packet.data()+position,packet.size()-position);
            if(!got)throw std::runtime_error("Truncated render options packet");
            position+=got;
        }
        if(std::memcmp(packet.data(),"HRO1",4))throw std::runtime_error("Render options packet version mismatch");
        RenderOptions loaded;std::memcpy(loaded.backdrop.data(),packet.data()+4,16);std::memcpy(&loaded.displayExposure,packet.data()+20,4);setOptions(loaded);
    }
    constexpr const char* vertex=SHADER_HEADER R"(
        layout(location=0) in vec2 position; layout(location=1) in vec2 uv;
        layout(location=2) in vec4 tint; layout(location=3) in vec3 hsv;
        out vec2 sampleUv; out vec4 multiplier; out vec3 hsvShift;
        void main(){gl_Position=vec4(position,0.,1.);sampleUv=uv;multiplier=tint;hsvShift=hsv;})";
    // Algebra translated from the shipped hue.inc/samplerhsv.inc and overlay2d
    // shader sources. Hash-bound source groups select the original software
    // offscreen composition route; absent companions retain the earlier path.
    constexpr const char* fragment=SHADER_HEADER R"(
        uniform sampler2D image; uniform sampler2D background;
        uniform int blendMode; uniform int addColor;
        uniform int displayPass; uniform float displayExposure;
        uniform int lightEffect;uniform sampler2D shadowMap;uniform float shadowResolution;
        uniform vec4 imageRegion;
        in vec2 sampleUv; in vec4 multiplier; in vec3 hsvShift; out vec4 color;
        vec3 rgb2hsv(vec3 c){
            vec4 K=vec4(0.,-.333333,.666666,-1.);
            vec4 p=mix(vec4(c.bg,K.wz),vec4(c.gb,K.xy),step(c.b,c.g));
            vec4 q=mix(vec4(p.xyw,c.r),vec4(c.r,p.yzx),step(p.x,c.r));
            float d=q.x-min(q.w,q.y),e=1.e-10;
            return vec3(abs(q.z+(q.w-q.y)/(6.*d+e)),d/(q.x+e),q.x);
        }
        vec3 hsv2rgb(vec3 c){vec4 K=vec4(1.,.666666,.333333,3.);vec3 p=abs(fract(c.xxx+K.xyz)*6.-K.www);return c.z*mix(K.xxx,clamp(p-K.xxx,0.,1.),c.y);}
        float luminosity(vec3 c){return dot(c,vec3(.3,.59,.11));}
        void main(){
            if(displayPass==1){
                vec4 composed=texelFetch(background,ivec2(gl_FragCoord.xy),0);
                color=vec4(composed.rgb*displayExposure,composed.a);return;
            }
            if(displayPass==2){
                vec4 base=texelFetch(background,ivec2(gl_FragCoord.xy),0);
                vec4 group=texelFetch(image,ivec2(gl_FragCoord.xy),0);
                if(blendMode==13){
                    // Original Displacement2D.frag, SHA615e19f986975622...:
                    // neutral8-bit epsilon, alpha-weighted RG, source scale.15.
                    vec2 shift=(group.rg*2.-1.)*group.a;
                    float eps=abs((128./255.)*2.-1.);
                    shift*=vec2(1.)-step(abs(shift),vec2(eps));
                    vec2 uv=gl_FragCoord.xy/vec2(textureSize(background,0));
                    // GL framebuffer UV has the opposite Y origin to D3D.
                    color=texture(background,uv+shift*.15*vec2(1.,-1.));
                }else if(blendMode==9)color=mix(2.*base*group,1.-2.*(1.-base)*(1.-group),step(vec4(.5),base));
                else color=luminosity(base.rgb)>luminosity(group.rgb)?base:group;
                return;
            }
            // A streamed frame occupies a bounded subrectangle of persistent
            // storage. Clamp to its edge texel centers to retain the original
            // frame-sized texture's GL_CLAMP_TO_EDGE behavior.
            vec2 uv=clamp(sampleUv*imageRegion.xy,imageRegion.zw,imageRegion.xy-imageRegion.zw);
            vec4 sampled=texture(image,uv);if(sampled.a<=0.)discard;
            if(lightEffect!=0){
                // Original Light.frag/LightAndShadow.frag. Neutral .5 is
                // preserved by native state6's symmetric multiply blend.
                // sampler.inc SampleFromFileDiscard has PREMUL=1 before
                // Light's neutral interpolation; alpha belongs here as well.
                vec3 lit=mix(vec3(.5),sampled.rgb*sampled.a,multiplier.rgb*multiplier.a);
                if(lightEffect==2){
                    vec2 local=sampleUv*2.-1.;float r=length(local);
                    float coord=(atan(local.y,local.x)+3.14)/(2.*3.14);
                    float blur=smoothstep(0.,1.,r)/shadowResolution;
                    float visibility=0.;
                    for(int i=-4;i<=4;++i){
                        int n=abs(i);float weight=n==4?.05:(n==3?.09:(n==2?.12:(n==1?.15:.16)));
                        visibility+=step(r,texture(shadowMap,vec2(coord+float(i)*blur,.5)).r)*weight;
                    }
                    lit=mix(vec3(.5),lit,visibility);
                }
                color=vec4(lit,sampled.a);return;
            }
            if(any(notEqual(hsvShift,vec3(0.)))){vec3 h=rgb2hsv(sampled.rgb);h.yz=clamp(h.yz+hsvShift.yz,0.,1.);h.x=fract(h.x+hsvShift.x+1.);sampled.rgb=hsv2rgb(h);}
            vec4 source=vec4(addColor!=0?sampled.rgb+multiplier.rgb:sampled.rgb*multiplier.rgb,sampled.a*multiplier.a);
            if(blendMode==9||blendMode==11){
                vec3 dest=texelFetch(background,ivec2(gl_FragCoord.xy),0).rgb;
                vec3 result;
                if(blendMode==9)result=mix(2.*dest*source.rgb,1.-2.*(1.-dest)*(1.-source.rgb),step(vec3(.5),dest));
                else result=luminosity(source.rgb)>luminosity(dest)?source.rgb:dest;
                color=vec4(mix(dest,result,source.a),1.);
            }else color=vec4(source.rgb*source.a,source.a);
        })";
    const auto vs=compile(GL_VERTEX_SHADER,vertex);unsigned fs=0;
    try{fs=compile(GL_FRAGMENT_SHADER,fragment);}catch(...){glDeleteShader(vs);throw;}
    program_=glCreateProgram();glAttachShader(program_,vs);glAttachShader(program_,fs);glLinkProgram(program_);glDeleteShader(vs);glDeleteShader(fs);
    GLint linked=0;glGetProgramiv(program_,GL_LINK_STATUS,&linked);
    if(!linked){char detail[4096]{};glGetProgramInfoLog(program_,sizeof(detail),nullptr,detail);throw std::runtime_error(std::string("Scene program: ")+detail);}
    glUseProgram(program_);glUniform1i(glGetUniformLocation(program_,"image"),0);glUniform1i(glGetUniformLocation(program_,"background"),1);
    glUniform1i(glGetUniformLocation(program_,"shadowMap"),2);lightLocation_=glGetUniformLocation(program_,"lightEffect");shadowResolutionLocation_=glGetUniformLocation(program_,"shadowResolution");
    blendLocation_=glGetUniformLocation(program_,"blendMode");addLocation_=glGetUniformLocation(program_,"addColor");
    postLocation_=glGetUniformLocation(program_,"displayPass");exposureLocation_=glGetUniformLocation(program_,"displayExposure");
    regionLocation_=glGetUniformLocation(program_,"imageRegion");glUniform4f(regionLocation_,1,1,0,0);
    glUniform1i(postLocation_,0);glUniform1f(exposureLocation_,options_.displayExposure);
    glGenVertexArrays(1,&vao_);glBindVertexArray(vao_);glGenBuffers(1,&buffer_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,sizeof(SceneVertex),reinterpret_cast<void*>(offsetof(SceneVertex,x)));
    glEnableVertexAttribArray(1);glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(SceneVertex),reinterpret_cast<void*>(offsetof(SceneVertex,u)));
    glEnableVertexAttribArray(2);glVertexAttribPointer(2,4,GL_FLOAT,GL_FALSE,sizeof(SceneVertex),reinterpret_cast<void*>(offsetof(SceneVertex,r)));
    glEnableVertexAttribArray(3);glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,sizeof(SceneVertex),reinterpret_cast<void*>(offsetof(SceneVertex,h)));
    GLint maxTexture=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maxTexture);
    scene_=loadGraphicsScene(assets,[&](uint32_t i,uint32_t w,uint32_t h,std::span<const uint8_t> pixels){
        if(i!=textures_.size())throw std::runtime_error("Unexpected scene texture order");
        unsigned texture;glGenTextures(1,&texture);textures_.push_back(texture);ownedTextures_.push_back(texture);configureTexture(texture);
        textureRegions_.push_back({w,h,w,h});
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Scene texture upload failed");
    },static_cast<uint32_t>(maxTexture));
    for(const auto& s:scene_.sprites)if(s.blend!=0&&s.blend!=1&&s.blend!=2&&s.blend!=5&&s.blend!=9&&s.blend!=11&&s.blend!=13)throw std::runtime_error("Scene uses an unreconstructed blend mode: "+std::to_string(s.blend));
    animations_.load(assets,scene_);
    for(const auto& clip:animations_.clips()){
        uint32_t w=1,h=1;for(const auto& frame:clip.frames){w=std::max(w,frame.width);h=std::max(h,frame.height);}
        w=textureExtent(w);h=textureExtent(h);
        if(w>static_cast<uint32_t>(maxTexture)||h>static_cast<uint32_t>(maxTexture))throw std::runtime_error("Animation storage exceeds GPU texture limit");
        // Rotate three preallocated images to avoid overwriting storage still
        // sampled by queued frames. Logical scene texture indices stay stable.
        std::array<unsigned,3> ring{};glGenTextures(3,ring.data());ownedTextures_.insert(ownedTextures_.end(),ring.begin(),ring.end());
        for(auto texture:ring){configureTexture(texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);}
        textures_.push_back(ring[0]);streamTextures_.push_back(ring);streamSlots_.push_back(0);
        textureRegions_.push_back({w,h,0,0});
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Animation texture allocation failed");
    }
    unsigned auxiliary[3];glGenTextures(3,auxiliary);hero_=auxiliary[0];white_=auxiliary[1];background_=auxiliary[2];
    for(auto texture:auxiliary)configureTexture(texture);
    const uint8_t white[]{255,255,255,255};glBindTexture(GL_TEXTURE_2D,white_);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);
    // Keep both samplers complete even before a destination blend is reached.
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,background_);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,white);glActiveTexture(GL_TEXTURE0);
    log(Severity::Info,"HadesGraphics","SCENE_READY version=%u textures=%u sprites=%zu rgba_bytes=%llu",scene_.version,scene_.textureCount,scene_.sprites.size(),static_cast<unsigned long long>(scene_.textureBytes));
    log(Severity::Info,"HadesGraphics","RENDER_OPTIONS backdrop=%.6f,%.6f,%.6f,%.6f exposure=%.3f",options_.backdrop[0],options_.backdrop[1],options_.backdrop[2],options_.backdrop[3],options_.displayExposure);
    log(Severity::Info,"HadesGraphics","SOURCE_COMPOSITION enabled=%d",scene_.hasSourceComposition);
    log(Severity::Info,"HadesGraphics","SOURCE_PARALLAX enabled=%d affected=%u",scene_.hasSourceParallax,scene_.parallaxSprites);
    log(Severity::Info,"HadesGraphics","SOURCE_ATLAS_HULLS enabled=%d",scene_.hasSourceHulls);
    } catch(...) { shutdown();throw; }
}
void SceneRenderer::shutdown(){
    sourceFrames_={};sourceSubmitted_={};sourceSprites_={};
    for(const auto& entry:shadowTextures_)if(entry.texture)glDeleteTextures(1,&entry.texture);shadowTextures_.clear();
    clearActorTextures();
    if(!ownedTextures_.empty())glDeleteTextures(static_cast<GLsizei>(ownedTextures_.size()),ownedTextures_.data());ownedTextures_.clear();textures_.clear();streamTextures_.clear();streamSlots_.clear();
    for(auto texture:{hero_,white_,background_,groupTexture_})if(texture)glDeleteTextures(1,&texture);
    if(groupFramebuffer_)glDeleteFramebuffers(1,&groupFramebuffer_);
    if(buffer_)glDeleteBuffers(1,&buffer_);if(vao_)glDeleteVertexArrays(1,&vao_);if(program_)glDeleteProgram(program_);
    hero_=white_=background_=program_=buffer_=vao_=0;backgroundWidth_=backgroundHeight_=0;uploadedClip_=nullptr;uploadedFrame_=UINT32_MAX;scene_={};animations_.reset();draw_.clear();vertices_.clear();
    groupTexture_=groupFramebuffer_=0;groupWidth_=groupHeight_=0;
    textureRegions_.clear();vertexOffsets_.clear();heroWidth_=heroHeight_=heroActiveWidth_=heroActiveHeight_=0;uploadedActorId_=uploadedActorFrame_=UINT32_MAX;stats_={};
}
void SceneRenderer::clearActorTextures(){for(const auto& entry:actorTextures_)if(entry.texture)glDeleteTextures(1,&entry.texture);actorTextures_.clear();actorIndex_.clear();actorTextureBytes_=actorRender_=0;}
unsigned SceneRenderer::prepareShadow(const ActorVisual& light,const SceneSprite& sprite){
    // Android adaptation: exact original occluder polygons produce a cached
    // polar distance map directly. This replaces the Windows occlusion-target
    // rasterization/radial sweep, while retaining LightAndShadow's nine taps.
    if(!light.lightShadows||!light.lifetime)throw std::runtime_error("Actual light shadow geometry/lifetime unavailable");
    ShadowTexture* cached=nullptr;
    for(auto& entry:shadowTextures_)if(entry.lifetime==light.lifetime){cached=&entry;break;}
    if(cached&&cached->revision==light.lightShadows->revision&&cached->corners==sprite.corners&&cached->uv==sprite.uv){cached->use=actorRender_;return cached->texture;}
    if(!cached){
        if(shadowTextures_.size()>=256){auto oldest=std::min_element(shadowTextures_.begin(),shadowTextures_.end(),[](const auto&a,const auto&b){return a.use<b.use;});if(oldest->use==actorRender_)throw std::runtime_error("Visible native light shadow cache bound");glDeleteTextures(1,&oldest->texture);*oldest={};cached=&*oldest;}
        else{shadowTextures_.emplace_back();cached=&shadowTextures_.back();}
    }
    const float ax=sprite.corners[2]-sprite.corners[0],ay=sprite.corners[3]-sprite.corners[1],bx=sprite.corners[4]-sprite.corners[0],by=sprite.corners[5]-sprite.corners[1],det=ax*by-ay*bx;
    if(!std::isfinite(det)||std::abs(det)<1.e-9f)throw std::runtime_error("Degenerate native light shadow transform");
    using Point=std::array<float,2>;std::vector<std::vector<Point>> polygons;
    for(const auto& source:light.lightShadows->polygons){
        if(source.points.size()<3||source.points.size()>4096)throw std::runtime_error("Actual light occluder polygon bound");
        std::vector<Point> polygon;polygon.reserve(source.points.size());float minX=INFINITY,maxX=-INFINITY,minY=INFINITY,maxY=-INFINITY;
        for(const auto& point:source.points){
            const float x=point[0]-sprite.corners[0],y=point[1]-sprite.corners[1];
            const float u=(x*by-y*bx)/det,v=(ax*y-ay*x)/det;
            Point local{2*(sprite.uv[0]+u*(sprite.uv[2]-sprite.uv[0]))-1,2*(sprite.uv[1]+v*(sprite.uv[3]-sprite.uv[1]))-1};
            if(!std::isfinite(local[0])||!std::isfinite(local[1]))throw std::runtime_error("Nonfinite actual shadow polygon");
            minX=std::min(minX,local[0]);maxX=std::max(maxX,local[0]);minY=std::min(minY,local[1]);maxY=std::max(maxY,local[1]);polygon.push_back(local);
        }
        if(minX<=1&&maxX>=-1&&minY<=1&&maxY>=-1)polygons.push_back(std::move(polygon));
    }
    std::array<float,512> distances;distances.fill(1);
    auto cross=[](Point a,Point b){return a[0]*b[1]-a[1]*b[0];};
    for(const auto& polygon:polygons){
        bool inside=false;for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){const auto a=polygon[i],b=polygon[j];if((a[1]>0)!=(b[1]>0)&&0<(b[0]-a[0])*(-a[1])/(b[1]-a[1])+a[0])inside=!inside;}
        if(inside){distances.fill(0);break;}
        for(size_t ray=0;ray<distances.size();++ray){const float theta=((float(ray)+.5f)/distances.size()*2-1)*3.14f;const Point direction{std::cos(theta),std::sin(theta)};
            for(size_t i=0;i<polygon.size();++i){const auto a=polygon[i],b=polygon[(i+1)%polygon.size()];const Point edge{b[0]-a[0],b[1]-a[1]};const float denominator=cross(direction,edge);if(std::abs(denominator)<1.e-8f)continue;const float distance=cross(a,edge)/denominator,along=cross(a,direction)/denominator;if(distance>=0&&distance<distances[ray]&&along>=0&&along<=1)distances[ray]=distance;}
        }
    }
    if(!cached->texture){glGenTextures(1,&cached->texture);configureTexture(cached->texture);}else glBindTexture(GL_TEXTURE_2D,cached->texture);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,512,1,0,GL_RED,GL_FLOAT,distances.data());
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Native light polar shadow texture upload failed");
    cached->lifetime=light.lifetime;cached->revision=light.lightShadows->revision;cached->corners=sprite.corners;cached->uv=sprite.uv;cached->use=actorRender_;return cached->texture;
}
uint32_t SceneRenderer::prepareActor(const ActorVisual& value){
 const auto& frame=value.frame;if(!value.sourceBank||!value.lifetime||!frame.width||!frame.height||frame.width>2048||frame.height>2048||frame.byteCount()>ActorAnimationBank::MaxFrameBytes||(frame.rgba.empty()&&!frame.decoder)||(!frame.rgba.empty()&&frame.rgba.size()!=frame.byteCount()))throw std::runtime_error("Actual source actor bank/frame/lifetime unavailable");
 if(const auto hit=actorIndex_.find({value.sourceBank,frame.packetFrameIndex});hit!=actorIndex_.end()){auto& e=actorTextures_[hit->second];if(e.width!=frame.width||e.height!=frame.height)throw std::runtime_error("Loaded actor bank identity changed without cache invalidation");e.use=actorRender_;return uint32_t(textures_.size()+2+hit->second);}
 auto evict=[&](){auto candidate=actorTextures_.end();for(auto i=actorTextures_.begin();i!=actorTextures_.end();++i)if(i->texture&&i->use!=actorRender_&&(candidate==actorTextures_.end()||i->use<candidate->use))candidate=i;if(candidate==actorTextures_.end())return false;actorIndex_.erase({candidate->bank,candidate->frame});glDeleteTextures(1,&candidate->texture);actorTextureBytes_-=uint64_t(candidate->width)*candidate->height*4;*candidate={};return true;};
 constexpr uint64_t budget=384ull*1024*1024;while(actorTextureBytes_+frame.byteCount()>budget)if(!evict())throw std::runtime_error("Current source actor frames exceed bounded GPU cache");
 auto slot=std::find_if(actorTextures_.begin(),actorTextures_.end(),[](const auto& a){return !a.texture;});if(slot==actorTextures_.end()){if(actorTextures_.size()>=4096){if(!evict())throw std::runtime_error("Source actor GPU frame count bound");slot=std::find_if(actorTextures_.begin(),actorTextures_.end(),[](const auto& a){return !a.texture;});}else{actorTextures_.push_back({});slot=actorTextures_.end()-1;}}
 const auto pixels=frame.pixels();if(pixels.size()!=frame.byteCount())throw std::runtime_error("Actual source actor frame pixels unavailable");
 unsigned texture=0;glGenTextures(1,&texture);glActiveTexture(GL_TEXTURE0);configureTexture(texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,frame.width,frame.height,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());if(glGetError()!=GL_NO_ERROR){glDeleteTextures(1,&texture);throw std::runtime_error("Original actor frame GPU upload failed");}
 *slot={value.sourceBank,frame.packetFrameIndex,frame.width,frame.height,texture,actorRender_};actorIndex_[{value.sourceBank,frame.packetFrameIndex}]=size_t(slot-actorTextures_.begin());actorTextureBytes_+=frame.byteCount();++stats_.frameUploads;stats_.frameUploadBytes+=frame.byteCount();return uint32_t(textures_.size()+2+size_t(slot-actorTextures_.begin()));
}
void SceneRenderer::ensureGroupTarget(int width,int height){
    if(groupWidth_==width&&groupHeight_==height)return;
    GLint previous=0;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previous);
    if(!groupTexture_)glGenTextures(1,&groupTexture_);
    glActiveTexture(GL_TEXTURE0);configureTexture(groupTexture_);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    if(!groupFramebuffer_)glGenFramebuffers(1,&groupFramebuffer_);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,groupFramebuffer_);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,groupTexture_,0);
    const auto status=glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(previous));
    if(status!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Scene group framebuffer incomplete");
    groupWidth_=width;groupHeight_=height;
}
void SceneRenderer::captureBackground(int width,int height){
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,background_);
    if(backgroundWidth_!=width||backgroundHeight_!=height){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,width,height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);backgroundWidth_=width;backgroundHeight_=height;}
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,width,height);
}
void SceneRenderer::drawRange(size_t start,size_t count,const SceneSprite& sprite,int width,int height,bool groupSourceOver){
    const auto first=vertexOffsets_.at(start),vertices=vertexOffsets_.at(start+count)-first;
    if(!vertices)return;
    const auto blend=groupSourceOver?0:sprite.blend;
    glUniform1i(blendLocation_,blend);glUniform1i(addLocation_,sprite.addColor);
    glUniform1i(lightLocation_,sprite.lightEffect);
    if(sprite.lightEffect){
        glEnable(GL_BLEND);glBlendFuncSeparate(GL_DST_COLOR,GL_SRC_COLOR,GL_DST_ALPHA,GL_SRC_ALPHA);
        if(sprite.lightEffect==2){glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,sprite.shadowTexture);glUniform1f(shadowResolutionLocation_,512.f);}
    }else if(blend==9||blend==11){
        captureBackground(width,height);glDisable(GL_BLEND);
    }else if(blend==1)glDisable(GL_BLEND);
    else{glEnable(GL_BLEND);glBlendFuncSeparate(GL_ONE,(blend==2||blend==5)?GL_ONE:GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);}
    unsigned texture=sprite.texture<textures_.size()?textures_[sprite.texture]:(sprite.texture==textures_.size()?hero_:white_);if(sprite.texture>=textures_.size()+2)texture=actorTextures_.at(sprite.texture-textures_.size()-2).texture;glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);
    if(sprite.texture>=scene_.textureCount&&sprite.texture<textureRegions_.size()){
        const auto& r=textureRegions_[sprite.texture];glUniform4f(regionLocation_,float(r.activeWidth)/r.width,float(r.activeHeight)/r.height,.5f/r.width,.5f/r.height);
    }else if(sprite.texture==textures_.size()&&heroWidth_&&heroHeight_){glUniform4f(regionLocation_,float(heroActiveWidth_)/heroWidth_,float(heroActiveHeight_)/heroHeight_,.5f/heroWidth_,.5f/heroHeight_);}
    else if(sprite.texture>=textures_.size()+2){const auto& r=actorTextures_.at(sprite.texture-textures_.size()-2);glUniform4f(regionLocation_,1,1,.5f/r.width,.5f/r.height);}
    else glUniform4f(regionLocation_,1,1,0,0);
    glDrawArrays(GL_TRIANGLES,static_cast<GLint>(first),static_cast<GLsizei>(vertices));++stats_.drawCalls;
}
void SceneRenderer::render(const SceneView& view,int width,int height,const Player* player,double seconds){renderImpl(view,width,height,player,nullptr,seconds);}
void SceneRenderer::renderActor(const SceneView& view,int width,int height,const ActorVisual* actor,double seconds){renderImpl(view,width,height,nullptr,actor,seconds);}
void SceneRenderer::renderActors(const SceneView& view,int width,int height,size_t count,const ActorVisualProvider& provider,double seconds){if(count>4096||(!provider&&count))throw std::runtime_error("Invalid actual actor visual provider/count");renderImpl(view,width,height,nullptr,nullptr,seconds,count,provider);}
void SceneRenderer::renderImpl(const SceneView& view,int width,int height,const Player* player,const ActorVisual* actor,double seconds,size_t actorCount,const ActorVisualProvider& actorProvider){
    if(!program_||width<=0||height<=0)return;
    if(!std::isfinite(view.width)||!std::isfinite(view.height)||view.width<=0||view.height<=0)throw std::runtime_error("Invalid scene view");
    stats_={};++actorRender_;
    GLint sceneFramebuffer=0,previousRead=0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&sceneFramebuffer);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previousRead);
    struct RestoreRead { GLint value; ~RestoreRead(){glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(value));} } restoreRead{previousRead};
    glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(sceneFramebuffer));
    glViewport(0,0,width,height);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_SCISSOR_TEST);glBlendEquation(GL_FUNC_ADD);glClearColor(options_.backdrop[0],options_.backdrop[1],options_.backdrop[2],options_.backdrop[3]);glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);glActiveTexture(GL_TEXTURE0);
    glUniform1i(postLocation_,0);
    draw_=scene_.sprites;
    auto drawOrder=scene_.order;
    if(!sourceFrames_)animations_.apply(seconds,draw_,scene_.textureCount,[&](uint32_t texture,uint32_t w,uint32_t h,std::span<const uint8_t> pixels){
        auto& region=textureRegions_.at(texture);if(w>region.width||h>region.height)throw std::runtime_error("Animation frame exceeds allocated storage");
        const auto clip=texture-scene_.textureCount;auto& slot=streamSlots_.at(clip);slot=(slot+1)%3;textures_[texture]=streamTextures_.at(clip)[slot];
        glBindTexture(GL_TEXTURE_2D,textures_.at(texture));glTexSubImage2D(GL_TEXTURE_2D,0,0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());region.activeWidth=w;region.activeHeight=h;
        ++stats_.frameUploads;stats_.frameUploadBytes+=pixels.size();
    });
    for(auto& sprite:draw_){
        std::optional<SceneFrameSelection> selected;std::string error;
        if(sourceFrames_&&!sourceFrames_(sprite.sourceIndex,selected,error))throw std::runtime_error("Actual primary source frame: "+error);
        if(selected){
            const auto* binding=animations_.binding(sprite.sourceIndex);
            if(!selected->lifetime||!binding||binding->clip!=selected->clip||selected->frame>=binding->quads.size())throw std::runtime_error("Actual primary frame/binding/lifetime mismatch");
            sprite.corners=binding->quads[selected->frame];sprite.animationClip=selected->clip;sprite.animationFrame=selected->frame;
        }
        if(sourceSprites_&&!sourceSprites_(sprite.sourceIndex,sprite,error))throw std::runtime_error("Actual primary source sprite: "+error);
        applySceneParallax(sprite,view);
        if(selected&&visible(sprite,view)){
            const uint32_t frame=(selected->clip<<16)|selected->frame;bool cached=false;
            if(const auto hit=actorIndex_.find({&animations_,frame});hit!=actorIndex_.end()){actorTextures_[hit->second].use=actorRender_;sprite.texture=uint32_t(textures_.size()+2+hit->second);cached=true;}
            if(!cached){ActorVisual source;source.sourceBank=&animations_;source.lifetime=selected->lifetime;source.frame=animations_.sample(selected->clip,selected->frame);sprite.texture=prepareActor(source);}
        }
    }
    if(sourceSprites_){
        if(drawOrder.size()!=draw_.size())throw std::runtime_error("Live scene ordering requires original source keys");
        struct SourcePlacement {std::array<float,3> key;SceneSprite sprite;};std::vector<SourcePlacement> sorted;sorted.reserve(draw_.size());
        for(size_t i=0;i<draw_.size();++i){auto key=draw_[i].runtimeSortKey.value_or(drawOrder[i]);key[0]=float(draw_[i].drawGroup);sorted.push_back({key,draw_[i]});}
        std::stable_sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.key<b.key;});
        for(size_t i=0;i<sorted.size();++i){draw_[i]=std::move(sorted[i].sprite);drawOrder[i]=sorted[i].key;}
    }
    if(actor){
        const auto& frame=actor->frame;const auto& r=actor->localRect;
        if(!frame.width||!frame.height||frame.pixels().size()!=frame.byteCount()||frame.width>2048||frame.height>2048||!std::isfinite(actor->scale)||actor->scale<=0||actor->scale>16||!std::isfinite(actor->x)||!std::isfinite(actor->y)||!std::isfinite(actor->z))throw std::runtime_error("Invalid source actor visual");
        for(float v:r)if(!std::isfinite(v))throw std::runtime_error("Non-finite actor rectangle");
        for(float v:actor->color)if(!std::isfinite(v)||v<0||v>1)throw std::runtime_error("Invalid actor color");
        if(r[2]<=0||r[3]<=0)throw std::runtime_error("Invalid actor rectangle");
        if(uploadedActorId_!=actor->id||uploadedActorFrame_!=frame.packetFrameIndex){
            glBindTexture(GL_TEXTURE_2D,hero_);
            if(frame.width>heroWidth_||frame.height>heroHeight_){heroWidth_=textureExtent(std::max(frame.width,heroWidth_));heroHeight_=textureExtent(std::max(frame.height,heroHeight_));glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,heroWidth_,heroHeight_,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);}heroActiveWidth_=frame.width;heroActiveHeight_=frame.height;
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,frame.width,frame.height,GL_RGBA,GL_UNSIGNED_BYTE,frame.pixels().data());
            if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Source actor texture upload failed");
            uploadedActorId_=actor->id;uploadedActorFrame_=frame.packetFrameIndex;uploadedClip_=nullptr;uploadedFrame_=UINT32_MAX;++stats_.frameUploads;stats_.frameUploadBytes+=frame.byteCount();
        }
        const std::array<float,3> key{10,0,actor->y};size_t at=draw_.size();
        if(scene_.order.size()==draw_.size())at=std::lower_bound(scene_.order.begin(),scene_.order.end(),key)-scene_.order.begin();
        auto visual=rectangleSprite(static_cast<uint32_t>(textures_.size()),actor->x+(r[0]+r[2]*.5f)*actor->scale,actor->y-actor->z*.8660254037844386f+(r[1]+r[3]*.5f)*actor->scale,r[2]*actor->scale,r[3]*actor->scale);
        visual.drawGroup=10;visual.tint=actor->color;draw_.insert(draw_.begin()+at,visual);
    }
    if(actorCount){
        if(drawOrder.size()!=draw_.size()||!std::is_sorted(drawOrder.begin(),drawOrder.end()))throw std::runtime_error("Source actor merge requires current scene order");
        struct Placement {std::array<float,3> key;SceneSprite sprite;};std::vector<Placement> placements;placements.reserve(actorCount);
        for(size_t i=0;i<actorCount;++i){ActorVisual value;std::string error;if(!actorProvider(i,value,error)){
            // Durability: an unreconstructable visual is skipped (logged once per
            // distinct reason) instead of failing the whole room's frame.
            static std::set<std::string> reported;if(reported.size()<256&&reported.insert(error).second)hades::log(hades::Severity::Error,"HadesSurvey","CONTAINED_FAULT stage=actor-visual detail=%s",error.c_str());
            ++skippedVisuals_;continue;}
            if(!value.visible)continue;
            const auto& r=value.localRect;
            // Durability: a visual outside the renderer's validated ranges is
            // skipped and logged once per reason (with its values) instead of
            // failing the room.
            const auto reject=[&](const char* why){char line[320];std::snprintf(line,sizeof(line),"%s id=%u scale=%.3f rect=%.1f,%.1f blend=%d psx=%.3f psy=%.3f",why,unsigned(value.id),value.scale,r[2],r[3],int(value.blend),value.postRotateScaleX,value.postRotateScaleY);
                static std::set<std::string> seen;if(seen.size()<256&&seen.insert(why+std::to_string(int(value.blend))).second)hades::log(hades::Severity::Error,"HadesSurvey","CONTAINED_FAULT stage=actor-visual detail=%s",line);++skippedVisuals_;};
            bool finite=true;for(float f:{value.x,value.y,value.z,value.scale,value.angle,r[0],r[1],r[2],r[3]})if(!std::isfinite(f))finite=false;
            if(!finite){reject("Non-finite native actor visual");continue;}
            bool colorOk=true;for(float f:value.color)if(!std::isfinite(f)||f<0||f>1)colorOk=false;for(float f:value.hsv)if(!std::isfinite(f))colorOk=false;
            if(!colorOk){reject("Invalid native actor color/HSV");continue;}
            if(value.scale<0||value.scale>16||r[2]<=0||r[3]<=0||(value.blend!=0&&value.blend!=1&&value.blend!=2&&value.blend!=5&&value.blend!=9&&value.blend!=11&&value.blend!=13)){reject("Invalid source actor transform/material");continue;}
            if(value.scale==0)continue;
            const float x=(r[0]+r[2]*.5f)*value.scale,y=(r[1]+r[3]*.5f)*value.scale,c=std::cos(value.angle),sn=std::sin(value.angle);
            auto visual=rectangleSprite(0,value.x+c*x-sn*y,value.y-value.z*.8660254037844386f+sn*x+c*y,r[2]*value.scale,r[3]*value.scale,value.angle);
            if(!std::isfinite(value.postRotateScaleX)||!std::isfinite(value.postRotateScaleY)||value.postRotateScaleX<=0||value.postRotateScaleY<=0||value.postRotateScaleX>16||value.postRotateScaleY>16){reject("Actual source post-rotation scale bound");continue;}
            for(size_t corner=0;corner<4;++corner){visual.corners[corner*2]=value.x+(visual.corners[corner*2]-value.x)*value.postRotateScaleX;const float anchor=value.y-value.z*.8660254037844386f;visual.corners[corner*2+1]=anchor+(visual.corners[corner*2+1]-anchor)*value.postRotateScaleY;}
            for(float f:value.meshTransform)if(!std::isfinite(f)||std::abs(f)>32)throw std::runtime_error("Actual native mesh transform bound");
            for(size_t corner=0;corner<4;++corner){const float anchor=value.y-value.z*.8660254037844386f,x=visual.corners[corner*2]-value.x,y=visual.corners[corner*2+1]-anchor;visual.corners[corner*2]=value.x+value.meshTransform[0]*x+value.meshTransform[1]*y;visual.corners[corner*2+1]=anchor+value.meshTransform[2]*x+value.meshTransform[3]*y;}
            if(value.screenSpace){
                // Android adaptation: the original scripts lay out a fixed
                // 1920x1080 reference. Map it with one uniform scale about the
                // screen center (no stretch on the Fold's ~1.1:1 panel). UI
                // fits inside the view; full-reference overlays cover it.
                const bool cover=r[2]*value.scale>=1900&&r[3]*value.scale>=1060;
                const float s=cover?std::max(view.width/1920.f,view.height/1080.f):std::min(view.width/1920.f,view.height/1080.f);
                const float cx=view.left+view.width*.5f,cy=view.top+view.height*.5f;
                for(size_t corner=0;corner<4;++corner){visual.corners[corner*2]=cx+(visual.corners[corner*2]-960.f)*s;visual.corners[corner*2+1]=cy+(visual.corners[corner*2+1]-540.f)*s;}
            }
            if(value.flipX)std::swap(visual.uv[0],visual.uv[2]);if(value.flipY)std::swap(visual.uv[1],visual.uv[3]);
            visual.drawGroup=value.drawGroup;visual.blend=value.blend;visual.addColor=value.addColor;visual.tint=value.color;visual.hsv=value.hsv;
            visual.parallax=value.parallax;visual.parallaxOrigin={value.x,value.y};if(value.parallax!=1)applySceneParallax(visual,view);
            // Do not equate alpha/culling with native SkipUpdate state.
            if(!visible(visual,view))continue;
            if(value.light&&!options_.drawLights)continue;
            if(value.light){visual.lightEffect=value.lightShadows?2:1;if(value.lightShadows)visual.shadowTexture=prepareShadow(value,visual);}
            visual.texture=prepareActor(value);const float sortY=value.sourceSortY.value_or(value.y);if(!std::isfinite(sortY))throw std::runtime_error("Actual source actor sort Y unavailable");
            placements.push_back({{float(value.drawGroup),float(value.sortIndex),sortY},visual});
        }
        std::stable_sort(placements.begin(),placements.end(),[](const auto& a,const auto& b){return a.key<b.key;});
        std::vector<SceneSprite> merged;merged.reserve(draw_.size()+placements.size());size_t next=0;
        for(size_t i=0;i<draw_.size();++i){while(next<placements.size()&&!(drawOrder[i]<placements[next].key))merged.push_back(placements[next++].sprite);merged.push_back(draw_[i]);}
        while(next<placements.size())merged.push_back(placements[next++].sprite);draw_=std::move(merged);
    }
    if(player&&player->ready){
        uploadedActorId_=uploadedActorFrame_=UINT32_MAX;
        const auto& clip=player->clip();const auto frame=player->frame();
        if(uploadedClip_!=&clip||uploadedFrame_!=frame){
            glBindTexture(GL_TEXTURE_2D,hero_);
            if(heroWidth_!=clip.width||heroHeight_!=clip.height){glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,clip.width,clip.height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);heroWidth_=clip.width;heroHeight_=clip.height;}
            glTexSubImage2D(GL_TEXTURE_2D,0,0,0,clip.width,clip.height,GL_RGBA,GL_UNSIGNED_BYTE,player->pixels());heroActiveWidth_=clip.width;heroActiveHeight_=clip.height;uploadedClip_=&clip;uploadedFrame_=frame;
            ++stats_.frameUploads;stats_.frameUploadBytes+=uint64_t(clip.width)*clip.height*4;
        }
        const std::array<float,3> key{10,0,player->position.y};size_t at=draw_.size();
        if(scene_.order.size()==draw_.size())at=std::lower_bound(scene_.order.begin(),scene_.order.end(),key)-scene_.order.begin();
        auto hero=rectangleSprite(static_cast<uint32_t>(textures_.size()),player->position.x,player->position.y+clip.offsetY,clip.width*clip.scale,clip.height*clip.scale);
        hero.drawGroup=10;
        draw_.insert(draw_.begin()+at,hero);
    }
    // Retain every original group record, including a wholly offscreen group:
    // its exact neutral clear/final composition must still occur. Only geometry
    // outside the view is omitted; surviving primitives keep source order.
    stats_.sourceSprites=static_cast<uint32_t>(draw_.size());
    vertices_.clear();vertices_.reserve((draw_.size()+1)*6);vertexOffsets_.resize(draw_.size()+1);
    for(size_t i=0;i<draw_.size();++i){
        vertexOffsets_[i]=vertices_.size();const auto& sprite=draw_[i];
        if(visible(sprite,view)){
            std::span<const std::array<float,2>> hull;
            if(scene_.hasSourceHulls){
                if(sprite.animationClip!=UINT32_MAX)hull=scene_.animationHulls.at(sprite.animationClip).at(sprite.animationFrame);
                else if(sprite.texture<scene_.textureCount)hull=scene_.textureHulls.at(sprite.texture);
                else if(sprite.texture<textures_.size()){
                    const auto clip=sprite.texture-scene_.textureCount;hull=scene_.animationHulls.at(clip).at(animations_.clips().at(clip).uploadedFrame);
                }
            }
            appendSceneVertices(sprite,view,hull,vertices_);++stats_.visibleSprites;
            if(sourceSubmitted_&&sprite.sourceIndex!=UINT32_MAX){std::string error;if(!sourceSubmitted_(sprite.sourceIndex,error))throw std::runtime_error("Actual primary draw admission: "+error);}
        }
    }
    vertexOffsets_[draw_.size()]=vertices_.size();const auto fullscreenOffset=vertices_.size();
    // Append once so group fullscreen draws cannot overwrite pending sprites.
    const auto fullscreen=sceneVertices(rectangleSprite(0,0,0,2,2),SceneView{-1,-1,2,2});
    vertices_.insert(vertices_.end(),fullscreen.begin(),fullscreen.end());
    stats_.vertexBytes=vertices_.size()*sizeof(SceneVertex);
    glBufferData(GL_ARRAY_BUFFER,vertices_.size()*sizeof(SceneVertex),vertices_.data(),GL_STREAM_DRAW);
    size_t start=0;bool fullscreenApplied=false;
    while(start<draw_.size()){
        if(fullscreenEffects_&&!fullscreenApplied&&draw_[start].drawGroup>fullscreenEffectGroup_){fullscreenEffects_(width,height);fullscreenApplied=true;}
        size_t end=start+1;
        if(scene_.hasSourceComposition&&(draw_[start].blend==9||draw_[start].blend==11||draw_[start].blend==13)){
            while(end<draw_.size()&&draw_[end].drawGroup==draw_[start].drawGroup)++end;
            ensureGroupTarget(width,height);captureBackground(width,height);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,groupFramebuffer_);
            // World::Draw's source clear includes alpha, not just RGB.
            if(draw_[start].blend==9)glClearColor(128.f/255,128.f/255,128.f/255,128.f/255);
            else if(draw_[start].blend==13)glClearColor(128.f/255,128.f/255,0,128.f/255);
            else glClearColor(0,0,0,1);
            glClear(GL_COLOR_BUFFER_BIT);
            for(size_t batch=start;batch<end;){
                size_t next=batch+1;while(next<end&&sameMaterial(draw_[batch],draw_[next]))++next;
                drawRange(batch,next-batch,draw_[batch],width,height,true);batch=next;
            }
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER,static_cast<GLuint>(sceneFramebuffer));
            glUniform1i(postLocation_,2);glUniform1i(lightLocation_,0);glUniform1i(blendLocation_,draw_[start].blend);glDisable(GL_BLEND);
            glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,groupTexture_);
            glDrawArrays(GL_TRIANGLES,static_cast<GLint>(fullscreenOffset),6);++stats_.drawCalls;++stats_.groupPasses;
            glUniform1i(postLocation_,0);start=end;continue;
        }
        // Destination-dependent blending must see each preceding object.
        if(draw_[start].blend!=9&&draw_[start].blend!=11)while(end<draw_.size()&&(!fullscreenEffects_||draw_[end].drawGroup==draw_[start].drawGroup)&&sameMaterial(draw_[start],draw_[end]))++end;
        drawRange(start,end-start,draw_[start],width,height);start=end;
    }
    if(fullscreenEffects_&&!fullscreenApplied)fullscreenEffects_(width,height);
    if(options_.displayExposure!=1){
        // Apply a user display preference once after all destination-dependent
        // scene composition; UI rendered later keeps its own brightness.
        captureBackground(width,height);
        const auto quad=sceneVertices(rectangleSprite(0,0,0,2,2),SceneView{-1,-1,2,2});
        glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad.data(),GL_STREAM_DRAW);
        glUniform1i(postLocation_,1);glUniform1i(lightLocation_,0);glUniform1f(exposureLocation_,options_.displayExposure);
        glDisable(GL_BLEND);glDrawArrays(GL_TRIANGLES,0,6);glUniform1i(postLocation_,0);
        ++stats_.drawCalls;
    }
    stats_.actorCacheBytes=actorTextureBytes_;for(const auto& t:actorTextures_)stats_.actorCachedFrames+=t.texture!=0;
    if(const auto error=glGetError();error!=GL_NO_ERROR)throw std::runtime_error("Scene GL draw failed: "+std::to_string(error));
}
void SceneRenderer::joystick(int width,int height,float x,float y,float dx,float dy){
    std::vector<SceneVertex> points;const float radius=height*.075f;
    auto circle=[&](float cx,float cy,float r,float alpha){
        auto v=[&](float px,float py){return SceneVertex{2*px/width-1,1-2*py/height,.5f,.5f,1,1,1,alpha,0,0,0};};
        for(int i=0;i<48;++i){const float a=6.283185307f*i/48,b=6.283185307f*(i+1)/48;points.push_back(v(cx,cy));points.push_back(v(cx+std::cos(a)*r,cy+std::sin(a)*r));points.push_back(v(cx+std::cos(b)*r,cy+std::sin(b)*r));}
    };
    circle(x,y,radius,.12f);circle(x+dx*radius,y+dy*radius,radius*.36f,.42f);
    glUseProgram(program_);glBindVertexArray(vao_);glBindBuffer(GL_ARRAY_BUFFER,buffer_);glUniform1i(blendLocation_,0);glUniform1i(addLocation_,0);glUniform1i(postLocation_,0);glUniform1i(lightLocation_,0);
    glUniform4f(regionLocation_,1,1,0,0);
    glEnable(GL_BLEND);glBlendFuncSeparate(GL_ONE,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,white_);
    glBufferData(GL_ARRAY_BUFFER,points.size()*sizeof(SceneVertex),points.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,static_cast<GLsizei>(points.size()));
}
}
