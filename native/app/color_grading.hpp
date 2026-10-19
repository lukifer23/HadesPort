#pragma once
#include "asset_store.hpp"
#include <cstdint>
#include <map>
#include <string>
#include <vector>
namespace hades {
struct ColorVolume {uint32_t size=0;std::vector<uint8_t> rgba;};
// Original ColorGradingComponent retains previous/current volume identities;
// changing a transition again takes the old target, not an invented blended LUT.
class ColorGrading {
    std::map<std::string,ColorVolume> volumes_;
    const ColorVolume* previous_=nullptr;const ColorVolume* current_=nullptr;
    std::string previousName_,currentName_;
    float start_=0,duration_=0,lerp_=1;bool worldTime_=true;
public:
    void load(AssetStore&);
    bool set(const std::string&,float duration,bool useWorldTime,float worldTime,float screenTime,std::string&);
    bool advance(float worldTime,float screenTime,std::string&);
    bool active()const{return current_||(previous_&&lerp_<1);}
    const ColorVolume* previous()const{return previous_;}
    const ColorVolume* current()const{return current_;}
    float fraction()const{return lerp_;}
    const std::string& previousName()const{return previousName_;}
    const std::string& currentName()const{return currentName_;}
};
}
