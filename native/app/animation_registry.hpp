#pragma once
#include "source_value.hpp"
#include <map>
#include <optional>
#include <span>
#include <vector>

namespace hades {
// Exact matching PDB enum, not inferred from class names or source omission.
enum class NativeAnimationType:uint8_t {Book=0,Slide=1,Constant=2,ConstantNoUpdate=3,Dataless=4,Random=5,Graft=6,Text=7};
enum class AnimationOwnership {ThingPrimary,Child,ManagerCreate,LightController,GuiComponent};
struct AnimationAllocation {
    std::string name;
    SourceValue sourceDefinition;
    NativeAnimationType type=NativeAnimationType::ConstantNoUpdate;
    AnimationOwnership ownership=AnimationOwnership::ThingPrimary;
    uint64_t ownerLifetime=0,parent=0;
    int32_t ownerId=0;
    uint32_t manager=0;
    bool loop=false,onlyWhenVisible=false,cancelOverCap=false;
};
struct AnimationLifetime:AnimationAllocation {
    uint64_t handle=0,child=0;
    bool initialized=false,managerMember=false,canceled=false,skipUpdate=false;
    bool attachedToTimeMultiplier=false;
};
// Allocation ownership and Add's ordered vector are deliberately distinct.
// This store does not pretend that a scene sprite or metadata recipe is an
// initialized Animation. Commit only after real component/resource init.
class AnimationRegistry {
    uint64_t next_=1;
    std::map<uint64_t,AnimationLifetime> live_;
    std::map<uint32_t,std::vector<uint64_t>> managed_;
public:
    bool allocate(const AnimationAllocation&,uint64_t&,std::string&);
    bool commitInitialization(uint64_t,bool actualSkipUpdate,std::string&);
    // Returns the native reverse-scan CancelOverCap victim. It stays registered
    // and counted until disposal. Caller must mirror canceled in actual state.
    bool addToManager(uint64_t,uint32_t permanentCap,std::optional<uint64_t>& victim,std::string&);
    bool setSkipUpdate(uint64_t,bool,std::string&);
    bool linkTimeMultiplier(uint64_t,int32_t ownerId,uint64_t ownerLifetime,std::string&);
    bool cancel(uint64_t,std::string&);
    bool restart(uint64_t,std::string&); // Base Restart clears actual canceled.
    bool dispose(uint64_t,std::string&);
    const AnimationLifetime* find(uint64_t)const;
    std::span<const uint64_t> managed(uint32_t)const;
    size_t managerCount(uint32_t)const;
    size_t allocationCount()const{return live_.size();}
    bool allAllocationsInitialized()const;
};
}
