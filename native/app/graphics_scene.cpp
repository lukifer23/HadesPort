#include "graphics_scene.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace hades {
namespace {
class Reader {
    std::unique_ptr<AssetFile> file_;
public:
    Reader(AssetStore& store,const char* name):file_(store.open(name)) {
        if(!file_)throw std::runtime_error(std::string("Missing graphics asset: ")+name);
    }
    void bytes(void* target,size_t size) {
        if(size>file_->remaining())throw std::runtime_error("Truncated graphics asset");
        auto* p=static_cast<uint8_t*>(target);
        while(size){const auto got=file_->read(p,size);if(!got)throw std::runtime_error("Graphics read failed");p+=got;size-=got;}
    }
    uint32_t integer(){uint32_t v;bytes(&v,4);return v;}
    float real(){float v;bytes(&v,4);if(!std::isfinite(v))throw std::runtime_error("Non-finite graphics value");return v;}
    void end(){if(file_->remaining())throw std::runtime_error("Unexpected graphics suffix");}
};
}
SceneSprite rectangleSprite(uint32_t texture,float x,float y,float w,float h,float angle) {
    SceneSprite sprite;sprite.texture=texture;
    const float c=std::cos(angle),s=std::sin(angle);
    constexpr float dx[]{-.5f,.5f,-.5f,.5f},dy[]{-.5f,-.5f,.5f,.5f};
    for(size_t i=0;i<4;++i){sprite.corners[2*i]=x+c*dx[i]*w-s*dy[i]*h;sprite.corners[2*i+1]=y+s*dx[i]*w+c*dy[i]*h;}
    return sprite;
}
GraphicsScene loadGraphicsScene(AssetStore& store,const TextureConsumer& consume,uint32_t maxTexture) {
    Reader reader(store,"scene.bin");const auto magic=reader.integer();
    if(magic!=0x31534448&&magic!=0x32534448&&magic!=0x33534448)throw std::runtime_error("Unsupported HDS scene version");
    GraphicsScene result;result.version=(magic>>24)-'0';result.textureCount=reader.integer();const auto count=reader.integer();
    if(!result.textureCount||result.textureCount>128||!count||count>100000)throw std::runtime_error("Scene counts outside bounds");
    for(uint32_t i=0;i<result.textureCount;++i){
        const auto w=reader.integer(),h=reader.integer();
        if(!w||!h||w>maxTexture||h>maxTexture)throw std::runtime_error("Scene texture dimensions outside bounds");
        const uint64_t size=uint64_t(w)*h*4;result.textureBytes+=size;
        if(result.textureBytes>512ull*1024*1024)throw std::runtime_error("Scene exceeds texture budget");
        std::vector<uint8_t> pixels(static_cast<size_t>(size));reader.bytes(pixels.data(),pixels.size());consume(i,w,h,pixels);
    }
    result.bounds={std::numeric_limits<float>::max(),std::numeric_limits<float>::max(),std::numeric_limits<float>::lowest(),std::numeric_limits<float>::lowest()};
    result.sprites.reserve(count);
    for(uint32_t i=0;i<count;++i){
        SceneSprite sprite;const auto texture=reader.integer();
        if(result.version==3){
            sprite.texture=texture;
            for(auto& v:sprite.corners)v=reader.real();
            for(auto& v:sprite.uv)v=reader.real();
            for(auto& v:sprite.tint)v=reader.real();
            sprite.blend=reader.integer();const auto add=reader.integer();
            if(add>1||(sprite.blend>13&&sprite.blend!=255))throw std::runtime_error("Invalid scene material");
            sprite.addColor=add;
        }else{
            const float x=reader.real(),y=reader.real(),w=reader.real(),h=reader.real();
            if(w<=0||h<=0)throw std::runtime_error("Invalid scene rectangle");
            std::array<float,4> uv;for(auto& v:uv)v=reader.real();const float rotation=reader.real(),alpha=reader.real();
            sprite=rectangleSprite(texture,x,y,w,h,rotation);sprite.uv=uv;sprite.tint[3]=alpha;
            if(result.version==2)for(size_t n=0;n<3;++n)sprite.tint[n]=reader.real();
        }
        if(sprite.texture>=result.textureCount)throw std::runtime_error("Invalid scene texture index");
        for(float v:sprite.tint)if(v<0||v>1)throw std::runtime_error("Invalid scene tint");
        for(float v:sprite.uv)if(v<0||v>1)throw std::runtime_error("Invalid scene UV");
        for(size_t p=0;p<4;++p){result.bounds[0]=std::min(result.bounds[0],sprite.corners[p*2]);result.bounds[1]=std::min(result.bounds[1],sprite.corners[p*2+1]);result.bounds[2]=std::max(result.bounds[2],sprite.corners[p*2]);result.bounds[3]=std::max(result.bounds[3],sprite.corners[p*2+1]);}
        sprite.sourceIndex=i;result.sprites.push_back(sprite);
    }
    reader.end();
    if(result.version==3){
        Reader materials(store,"materials.bin");
        if(materials.integer()!=0x314d5348||materials.integer()!=count)throw std::runtime_error("Scene material count/version mismatch");
        for(auto& sprite:result.sprites)for(auto& v:sprite.hsv)v=materials.real();
        materials.end();
    }
    if(auto file=store.open("ordering.bin")){
        Reader order(store,"ordering.bin");
        if(order.integer()!=0x314f4448||order.integer()!=count)throw std::runtime_error("Scene ordering count/version mismatch");
        for(uint32_t i=0;i<count;++i){std::array<float,3> key;for(auto& v:key)v=order.real();result.order.push_back(key);}order.end();
        if(!std::is_sorted(result.order.begin(),result.order.end()))throw std::runtime_error("Scene ordering is not sorted");
    }
    if(auto file=store.open("ambient.bin")){
        Reader ambient(store,"ambient.bin");
        if(ambient.integer()!=0x31414c48)throw std::runtime_error("Scene ambient version mismatch");
        std::array<uint8_t,32> binding{},expected{};ambient.bytes(binding.data(),binding.size());
        Reader sceneHash(store,"scene-sha256.bin");sceneHash.bytes(expected.data(),expected.size());sceneHash.end();
        if(binding!=expected)throw std::runtime_error("Scene ambient hash binding mismatch");
        if(ambient.integer()!=count)throw std::runtime_error("Scene ambient sprite count mismatch");
        std::array<float,3> room;for(auto& v:room){v=ambient.real();if(v<0||v>1)throw std::runtime_error("Invalid source room ambient");}
        for(auto& sprite:result.sprites){
            const auto amount=ambient.real();const auto shader=ambient.integer();
            if(amount<0||amount>1||shader>1)throw std::runtime_error("Invalid source sprite ambient");
            if(shader==1){
                for(size_t i=0;i<3;++i)sprite.ambientMultiplier[i]=1+(room[i]-1)*amount;
                ++result.ambientSprites;
            }
        }
        ambient.end();result.hasSourceAmbient=true;
    }
    if(auto file=store.open("composition.bin")){
        Reader composition(store,"composition.bin");
        if(composition.integer()!=0x31434748)throw std::runtime_error("Scene composition version mismatch");
        std::array<uint8_t,32> binding{},expected{};composition.bytes(binding.data(),binding.size());
        Reader sceneHash(store,"scene-sha256.bin");sceneHash.bytes(expected.data(),expected.size());sceneHash.end();
        if(binding!=expected)throw std::runtime_error("Scene composition hash binding mismatch");
        if(composition.integer()!=count)throw std::runtime_error("Scene composition sprite count mismatch");
        uint32_t previous=0,previousBlend=0;
        for(uint32_t i=0;i<count;++i){
            auto& sprite=result.sprites[i];const auto group=composition.integer();
            if(group>4095||(i&&group<previous)||(i&&group==previous&&sprite.blend!=previousBlend))throw std::runtime_error("Invalid scene composition group order/material");
            if(result.order.size()==count&&result.order[i][0]!=static_cast<float>(group))throw std::runtime_error("Scene composition disagrees with native ordering");
            sprite.drawGroup=group;previous=group;previousBlend=sprite.blend;
        }
        composition.end();result.hasSourceComposition=true;
    }
    if(auto file=store.open("parallax.bin")){
        Reader parallax(store,"parallax.bin");
        if(parallax.integer()!=0x31505048)throw std::runtime_error("Scene parallax version mismatch");
        std::array<uint8_t,32> binding{},expected{};parallax.bytes(binding.data(),binding.size());
        Reader sceneHash(store,"scene-sha256.bin");sceneHash.bytes(expected.data(),expected.size());sceneHash.end();
        if(binding!=expected)throw std::runtime_error("Scene parallax hash binding mismatch");
        if(parallax.integer()!=count)throw std::runtime_error("Scene parallax sprite count mismatch");
        for(auto& sprite:result.sprites){
            for(auto& v:sprite.parallaxOrigin)v=parallax.real();sprite.parallax=parallax.real();
            if(sprite.parallax<=0||sprite.parallax>16)throw std::runtime_error("Unsupported source parallax factor");
            result.parallaxSprites+=sprite.parallax!=1;
        }
        parallax.end();result.hasSourceParallax=true;
    }
    if(auto file=store.open("hulls.bin")){
        Reader hulls(store,"hulls.bin");
        if(result.version!=3||hulls.integer()!=0x314c4848)throw std::runtime_error("Scene hull version mismatch");
        std::array<uint8_t,32> binding{},expected{};hulls.bytes(binding.data(),binding.size());
        Reader sceneHash(store,"scene-sha256.bin");sceneHash.bytes(expected.data(),expected.size());sceneHash.end();
        if(binding!=expected)throw std::runtime_error("Scene hull scene binding mismatch");
        hulls.bytes(binding.data(),binding.size());Reader animationHash(store,"animations-sha256.bin");animationHash.bytes(expected.data(),expected.size());animationHash.end();
        if(binding!=expected)throw std::runtime_error("Scene hull animation binding mismatch");
        const auto textures=hulls.integer(),clips=hulls.integer();
        if(textures!=result.textureCount||clips>128)throw std::runtime_error("Scene hull count mismatch");
        uint64_t pointBudget=0,frameBudget=0;
        auto polygon=[&](){
            const auto points=hulls.integer();pointBudget+=points;
            if((points&&points<3)||points>128||pointBudget>262144)throw std::runtime_error("Scene hull points outside budget");
            SceneHull polygon;polygon.reserve(points);
            for(uint32_t i=0;i<points;++i){std::array<float,2> point{hulls.real(),hulls.real()};for(float p:point)if(p<0||p>1)throw std::runtime_error("Scene hull UV outside bounds");polygon.push_back(point);}
            return polygon;
        };
        for(uint32_t i=0;i<textures;++i)result.textureHulls.push_back(polygon());
        for(uint32_t i=0;i<clips;++i){
            const auto frames=hulls.integer();frameBudget+=frames;
            if(!frames||frames>10000||frameBudget>100000)throw std::runtime_error("Scene hull frame count outside budget");
            std::vector<SceneHull> clip;clip.reserve(frames);for(uint32_t n=0;n<frames;++n)clip.push_back(polygon());result.animationHulls.push_back(std::move(clip));
        }
        hulls.end();result.hasSourceHulls=true;
    }
    return result;
}
void applySceneParallax(SceneSprite& sprite,const SceneView& view){
    if(sprite.parallax==1)return;
    // Original vertexHelpers.inc shifts projected origin by origin/p-origin.
    // Orthographic view reduces this to one world-space translation. The
    // origin has native Z=0, so sprite height must not enter this calculation.
    const float amount=1/sprite.parallax-1;
    const float dx=(sprite.parallaxOrigin[0]-(view.left+view.width*.5f))*amount;
    const float dy=(sprite.parallaxOrigin[1]-(view.top+view.height*.5f))*amount;
    for(size_t i=0;i<4;++i){sprite.corners[2*i]+=dx;sprite.corners[2*i+1]+=dy;}
}
std::array<SceneVertex,6> sceneVertices(const SceneSprite& sprite,const SceneView& view) {
    if(!std::isfinite(view.width)||!std::isfinite(view.height)||view.width<=0||view.height<=0)throw std::runtime_error("Invalid scene view");
    auto vertex=[&](size_t p){return SceneVertex{2*(sprite.corners[p*2]-view.left)/view.width-1,1-2*(sprite.corners[p*2+1]-view.top)/view.height,
        sprite.uv[p%2?2:0],sprite.uv[p>=2?3:1],sprite.tint[0]*sprite.ambientMultiplier[0],sprite.tint[1]*sprite.ambientMultiplier[1],sprite.tint[2]*sprite.ambientMultiplier[2],sprite.tint[3],sprite.hsv[0],sprite.hsv[1],sprite.hsv[2]};};
    const auto a=vertex(0),b=vertex(1),c=vertex(3),d=vertex(2);return {a,b,c,a,c,d};
}
void appendSceneVertices(const SceneSprite& sprite,const SceneView& view,std::span<const std::array<float,2>> hull,std::vector<SceneVertex>& output){
    if(!std::isfinite(view.width)||!std::isfinite(view.height)||view.width<=0||view.height<=0)throw std::runtime_error("Invalid scene view");
    if(hull.empty()){auto quad=sceneVertices(sprite,view);output.insert(output.end(),quad.begin(),quad.end());return;}
    if(hull.size()<3)throw std::runtime_error("Invalid source hull");
    const auto added=(hull.size()-2)*3;
    if(output.size()+added>2000000)throw std::runtime_error("Scene vertex budget exceeded");
    const float du=sprite.uv[2]-sprite.uv[0],dv=sprite.uv[3]-sprite.uv[1];
    if(du==0||dv==0)throw std::runtime_error("Source hull has degenerate sprite UV");
    auto vertex=[&](const std::array<float,2>& uv){
        const float u=(uv[0]-sprite.uv[0])/du,v=(uv[1]-sprite.uv[1])/dv;
        const float x=sprite.corners[0]+(sprite.corners[2]-sprite.corners[0])*u+(sprite.corners[4]-sprite.corners[0])*v;
        const float y=sprite.corners[1]+(sprite.corners[3]-sprite.corners[1])*u+(sprite.corners[5]-sprite.corners[1])*v;
        return SceneVertex{2*(x-view.left)/view.width-1,1-2*(y-view.top)/view.height,uv[0],uv[1],sprite.tint[0]*sprite.ambientMultiplier[0],sprite.tint[1]*sprite.ambientMultiplier[1],sprite.tint[2]*sprite.ambientMultiplier[2],sprite.tint[3],sprite.hsv[0],sprite.hsv[1],sprite.hsv[2]};
    };
    const auto first=vertex(hull[0]);for(size_t i=1;i+1<hull.size();++i){output.push_back(first);output.push_back(vertex(hull[i]));output.push_back(vertex(hull[i+1]));}
}
}
