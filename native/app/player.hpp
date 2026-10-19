#pragma once
#include "asset_store.hpp"
#include <cstdint>
#include <vector>

struct Point { float x,y; };
struct HeroClip {
    uint32_t frames=0,width=0,height=0,angles=0;
    float fps=0,scale=1,offsetY=0;
    std::vector<uint8_t> rgba;
};
class Player {
    HeroClip idle_,run_;
    std::vector<Point> body_;
    std::vector<std::vector<Point>> obstacles_;
    std::vector<int32_t> obstacleIds_;
    mutable int32_t blockingObstacle_=0;
    Point input_{},velocity_{};
    float speed_=0,acceleration_=0,grip_=0;
    double animationTime_=0;
    bool moving_=false;
    int direction_=0;
    float facing_=0;
    bool collides(float x,float y) const;
public:
    Point position{};
    bool ready=false;
    void load(hades::AssetStore*,bool loadVisualClips=true);
    bool moving()const{return moving_;}
    uint32_t selectedAngle()const{return uint32_t(direction_);}
    float facingRadians()const{return facing_;}
    void setInput(float x,float y) { input_={x,y}; }
    void pause() { input_={}; velocity_={}; }
    void update(float dt);
    const HeroClip& clip() const { return moving_?run_:idle_; }
    uint32_t frame() const;
    const uint8_t* pixels() const;
    int32_t blockingObstacle()const{return blockingObstacle_;}
};
