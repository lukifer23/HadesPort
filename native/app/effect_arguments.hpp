#pragma once
#include <cstdint>
#include <string>
struct lua_State;
namespace hades {
struct EffectApplyArguments {
    int32_t ownerId=-1,type=12,numStacks=0;
    std::string ownerName,effectName;
    bool reapplied=false,isVulnerabilityEffect=false;
    float modifier=0,duration=0,elapsedTimeMultiplier=1,unitElapsedTimeMultiplier=1;
};
const char* nativeEffectTypeName(int32_t);
// Call per matching original OnEffectApply registration. The attached owner
// alias can be absent; ScriptManager::OnEffectApply does not gate on its presence.
// Fresh args become global_triggerArgs and stay on stack. Parent runs the
// original registration using the same retained table; no callbacks run here.
bool pushEffectApplyArguments(lua_State*,const EffectApplyArguments&,
    int ownerAttachedTableStackIndex,std::string&);
}
