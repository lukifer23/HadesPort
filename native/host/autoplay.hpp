#pragma once
#include "../app/lua_runtime.hpp"
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace hades {
struct AutoplayInput {float x=0,y=0;std::vector<std::string> controls;std::string summary;};
class Autoplay {
    int continuePeriod_=60;
    std::string map_;std::map<int32_t,int> tried_;int32_t targetId_=-1;int targetSince_=0;float lastDistance_=1e30f;int stall_=0;
    int waypointFrame_=-1000;float waypointX_=0,waypointY_=0;bool waypointValid_=false;std::string waypointError_;
public:
    explicit Autoplay(int continuePeriod=60):continuePeriod_(continuePeriod){if(continuePeriod<2||continuePeriod>36000)throw std::invalid_argument("Autoplay ContinueText period must be 2..36000 frames");}
    using Waypoint=std::function<bool(float,float,float&,float&,std::string&)>;
    AutoplayInput step(int frame,const ScriptRuntime::AutoplayView&,const Waypoint&);
};
}
