#pragma once
#include "game_ui.hpp"
#include <algorithm>
#include <array>
#include <cmath>

namespace hades {
// Android adaptation: inverse of SceneRenderer's centered, uniform fit of the
// original 1920x1080 screen reference. The viewport is the actual physical
// scene rectangle, before optional offscreen render-resolution scaling.
class ScreenPointer {
    std::array<int,4> viewport_{};int surfaceHeight_=0,owner_=-1;
public:
    struct Sample {float x=0,y=0;bool down=false;};
    enum class Result {Ignored,Updated,Cancelled};
    bool configure(std::array<int,4> viewport,int surfaceHeight){
        if(viewport_==viewport&&surfaceHeight_==surfaceHeight)return false;
        viewport_=viewport;surfaceHeight_=surfaceHeight;cancel();return true;
    }
    void cancel(){owner_=-1;}
    int owner()const{return owner_;}
    Result pointer(UiPointer phase,int id,float x,float y,Sample&sample){
        if(phase==UiPointer::Cancel){const bool owned=owner_>=0;cancel();return owned?Result::Cancelled:Result::Ignored;}
        if(phase==UiPointer::Down){
            if(owner_>=0||id<0||viewport_[2]<=0||viewport_[3]<=0||surfaceHeight_<=0||!std::isfinite(x)||!std::isfinite(y))return Result::Ignored;
            // GL viewport Y starts at the bottom, Android pointer Y at the top.
            const float top=float(surfaceHeight_)-float(viewport_[1])-float(viewport_[3]);
            if(x<float(viewport_[0])||x>=float(viewport_[0])+viewport_[2]||y<top||y>=top+viewport_[3])return Result::Ignored;
            owner_=id;
        }else if(id!=owner_||owner_<0)return Result::Ignored;
        const float scale=std::min(float(viewport_[2])/1920.f,float(viewport_[3])/1080.f);
        const float cx=float(viewport_[0])+float(viewport_[2])*.5f;
        const float cy=float(surfaceHeight_)-float(viewport_[1])-float(viewport_[3])*.5f;
        sample={960.f+(x-cx)/scale,540.f+(y-cy)/scale,phase!=UiPointer::Up};
        if(!std::isfinite(sample.x)||!std::isfinite(sample.y)){cancel();return Result::Cancelled;}
        if(phase==UiPointer::Up)cancel();
        return Result::Updated;
    }
};
}
