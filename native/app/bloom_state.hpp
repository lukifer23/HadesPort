#pragma once
#include "asset_store.hpp"
#include <map>
#include <string>
namespace hades {
struct BloomSettings {
 std::string name;
 float threshold=0,blur=0,bloomIntensity=0,baseIntensity=1,bloomSaturation=1,baseSaturation=1;
 bool isNull=true;
};
// Original BloomComponent transition state; renderer consumes six live scalars.
class FullscreenBloom {
 std::map<std::string,BloomSettings> definitions_;
 BloomSettings current_,previous_,target_;
 float start_=0,duration_=0;bool worldTime_=true;
public:
 void load(AssetStore&);
 bool resetOff(std::string&);
 bool adjust(const std::string&,float duration,bool useWorldTime,float worldTime,float screenTime,std::string&);
 bool advance(float worldTime,float screenTime,bool drawBloom,std::string&);
 bool shouldDraw(bool drawBloom)const;
 const BloomSettings& snapshot()const{return current_;}
 bool transitioning()const{return !target_.isNull;}
 const std::string& settingName()const{return target_.isNull?current_.name:target_.name;}
};
}
