#pragma once
#include "../app/lua_runtime.hpp"
#include <map>
#include <string>
#include <vector>

namespace hades {
struct AutoplayInput {float x=0,y=0;std::vector<std::string> controls;std::string summary;};
class Autoplay {
    std::string map_;std::map<int32_t,int> tried_;int32_t targetId_=-1;int targetSince_=0;float lastDistance_=1e30f;int stall_=0;
public:
    AutoplayInput step(int frame,const ScriptRuntime::AutoplayView&);
};
}
