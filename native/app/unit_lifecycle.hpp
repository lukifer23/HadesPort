#pragma once
#include <cstdint>
#include <functional>
#include <string>
namespace hades {
// UnitManager.Remove invokes Thing.Dispose, then Unit's destructor. These
// barriers are distinct from the Obstacle structural-subtree policy.
enum class UnitDestructorStage { ClearGridRecord,StatusReset,ArsenalDispose };
using UnitDestructorCallback=std::function<bool(int32_t,uint64_t,UnitDestructorStage,std::string&)>;
}
