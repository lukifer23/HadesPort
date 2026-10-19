#include "player.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
class Asset {
    std::unique_ptr<hades::AssetFile> file_;
public:
    Asset(hades::AssetStore* assets,const char* name):file_(assets->open(name)) {
        if(!file_) throw std::runtime_error(std::string("Missing player asset: ")+name);
    }
    void read(void* out,size_t count) {
        if(count>file_->remaining()) throw std::runtime_error("Truncated player asset");
        auto* p=static_cast<uint8_t*>(out);
        while(count) { size_t got=file_->read(p,std::min(count,size_t{1<<20}));
            if(got<=0) throw std::runtime_error("Player asset read failed"); p+=got;count-=got; }
    }
    uint32_t integer(){uint32_t n;read(&n,4);return n;}
    float real(){float f;read(&f,4);if(!std::isfinite(f))throw std::runtime_error("Invalid player data");return f;}
    bool end()const{return file_->remaining()==0;}
};
HeroClip loadClip(hades::AssetStore* assets,const char* name) {
    Asset file(assets,name);HeroClip clip;
    if(file.integer()!=0x31484448)throw std::runtime_error("Unsupported hero format");
    clip.frames=file.integer();clip.width=file.integer();clip.height=file.integer();clip.angles=file.integer();
    clip.fps=file.real();clip.scale=file.real();clip.offsetY=file.real();
    const uint64_t bytes=uint64_t(clip.frames)*clip.width*clip.height*clip.angles*4;
    if(!clip.frames||clip.frames>1000||!clip.width||clip.width>2048||!clip.height||clip.height>2048||
       !clip.angles||clip.angles>64||clip.fps<=0||clip.scale<=0||bytes>256ull*1024*1024)
        throw std::runtime_error("Hero clip exceeds bounds");
    clip.rgba.resize(bytes);file.read(clip.rgba.data(),clip.rgba.size());
    if(!file.end())throw std::runtime_error("Unexpected hero clip suffix");return clip;
}
float cross(Point a,Point b,Point c){return(b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
bool inside(Point p,const std::vector<Point>& polygon) {
    bool result=false;
    for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++) {
        auto a=polygon[i],b=polygon[j];
        if((a.y>p.y)!=(b.y>p.y)&&p.x<(b.x-a.x)*(p.y-a.y)/(b.y-a.y)+a.x)result=!result;
    }
    return result;
}
bool intersects(Point a,Point b,Point c,Point d) {
    return ((cross(a,b,c)>0)!=(cross(a,b,d)>0))&&((cross(c,d,a)>0)!=(cross(c,d,b)>0));
}
}
void Player::load(hades::AssetStore* assets,bool loadVisualClips) {
    // Assets carry source-recovered values. Dynamics and polygon handling are an
    // initial reconstruction and have not been matched to original engine traces.
    if(loadVisualClips){idle_=loadClip(assets,"ZagreusIdle.bin");run_=loadClip(assets,"ZagreusRun.bin");}
    Asset world(assets,"player-world.bin");
    const auto format=world.integer();
    if(format!=0x31504448&&format!=0x32504448)throw std::runtime_error("Unsupported player world format");
    position={world.real(),world.real()};speed_=world.real();acceleration_=world.real();grip_=world.real();
    if(speed_<=0||speed_>10000||acceleration_<=0||acceleration_>100000||grip_<=0||grip_>100000)
        throw std::runtime_error("Invalid movement parameters");
    const auto bodyCount=world.integer();if(bodyCount<3||bodyCount>128)throw std::runtime_error("Invalid player hull");
    for(uint32_t i=0;i<bodyCount;++i)body_.push_back({world.real(),world.real()});
    const auto count=world.integer();if(count>10000)throw std::runtime_error("Too many collision hulls");
    for(uint32_t i=0;i<count;++i){
        const auto id=format==0x32504448?world.integer():0;
        if(format==0x32504448&&(id==0||id>INT32_MAX))throw std::runtime_error("Invalid collision source ID");
        obstacleIds_.push_back(static_cast<int32_t>(id));
        const auto n=world.integer();if(n<3||n>1024)throw std::runtime_error("Invalid obstacle hull");
        std::vector<Point> polygon;for(uint32_t j=0;j<n;++j)polygon.push_back({world.real(),world.real()});obstacles_.push_back(std::move(polygon));}
    if(!world.end())throw std::runtime_error("Unexpected collision suffix");ready=true;
    hades::log(hades::Severity::Info,"HadesNative","PLAYER_READY spawn=%.2f,%.2f speed=%.0f acceleration=%.0f grip=%.0f polygons=%zu angles=%u/%u",
        position.x,position.y,speed_,acceleration_,grip_,obstacles_.size(),idle_.angles,run_.angles);
}
bool Player::collides(float x,float y) const {
    std::vector<Point> body;body.reserve(body_.size());
    float minX=x,maxX=x,minY=y,maxY=y;
    for(auto p:body_){p.x+=x;p.y+=y;body.push_back(p);minX=std::min(minX,p.x);maxX=std::max(maxX,p.x);minY=std::min(minY,p.y);maxY=std::max(maxY,p.y);}
    for(size_t at=0;at<obstacles_.size();++at) {
        const auto& polygon=obstacles_[at];
        float a=polygon[0].x,b=a,c=polygon[0].y,d=c;
        for(auto p:polygon){a=std::min(a,p.x);b=std::max(b,p.x);c=std::min(c,p.y);d=std::max(d,p.y);}
        if(maxX<a||minX>b||maxY<c||minY>d)continue;
        for(auto p:body)if(inside(p,polygon)){blockingObstacle_=obstacleIds_[at];return true;}
        for(auto p:polygon)if(inside(p,body)){blockingObstacle_=obstacleIds_[at];return true;}
        for(size_t i=0;i<body.size();++i)for(size_t j=0;j<polygon.size();++j)
            if(intersects(body[i],body[(i+1)%body.size()],polygon[j],polygon[(j+1)%polygon.size()])){blockingObstacle_=obstacleIds_[at];return true;}
    }
    return false;
}
void Player::update(float dt) {
    if(!ready)return;
    if(!std::isfinite(dt)||dt<0)throw std::runtime_error("Invalid player frame duration");
    // Collision already subdivides displacement into <=4-world-unit steps.
    // Preserve active elapsed time through 250ms; bound suspension/stall catchup.
    blockingObstacle_=0;dt=std::min(dt,.25f);
    const float magnitude=std::min(1.f,std::hypot(input_.x,input_.y));
    // Original Unit::MoveInput directly sets ordinary grounded velocity. Its
    // acceleration branch applies only while footing is lost. Low-stick input
    // selects LowSpeedFraction=.2 when length squared is below .5.
    // TouchControls owns the Android radial dead zone before this input.
    if(magnitude<=.0001f)velocity_={};
    else {const float fraction=magnitude*magnitude<.5f?.2f:1.f;
        velocity_={input_.x/magnitude*speed_*fraction,input_.y/magnitude*speed_*fraction};}
    const float moveX=velocity_.x*dt,moveY=velocity_.y*dt;
    const int steps=std::max(1,static_cast<int>(std::ceil(std::hypot(moveX,moveY)/4.f)));
    for(int i=0;i<steps;++i){
        const float nx=position.x+moveX/steps,ny=position.y+moveY/steps;
        if(!collides(nx,ny))position={nx,ny};
        else if(!collides(nx,position.y))position.x=nx;
        else if(!collides(position.x,ny))position.y=ny;
    }
    const bool moving=std::hypot(velocity_.x,velocity_.y)>1;
    if(moving!=moving_){animationTime_=0;moving_=moving;}
    if(magnitude>.05f){constexpr float pi=3.14159265358979323846f;
        facing_=std::atan2(-input_.y,input_.x);if(facing_<0)facing_+=2*pi;
        direction_=(static_cast<int>(std::lround(facing_*4/pi))+8)%8;}
    animationTime_+=dt;
}
uint32_t Player::frame()const {
    const auto& c=clip();const auto angle=static_cast<uint32_t>(direction_)*c.angles/8;
    return angle*c.frames+static_cast<uint32_t>(animationTime_*c.fps)%c.frames;
}
const uint8_t* Player::pixels()const {
    const auto& c=clip();return c.rgba.data()+size_t(frame())*c.width*c.height*4;
}
