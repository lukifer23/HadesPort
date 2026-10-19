#pragma once
#include "actor_runtime.hpp"
namespace hades {
// Actual native LifeSystem timer bookkeeping; health remains original Lua state.
// Parent owns phase ordering and supplies true per-Thing elapsed time.
class LifeSystemRuntime {
    ActorWorld& actors_;
    std::vector<uint64_t> active_,dirty_;
    std::map<uint64_t,int32_t> identities_;
    bool updating_=false;
public:
    explicit LifeSystemRuntime(ActorWorld& actors):actors_(actors){}
    bool registerActor(int32_t,std::string&);
    bool markDirty(int32_t,std::string&);
    bool removeActor(uint64_t lifetime,std::string&);
    bool reconcile(std::string&);
    bool update(const std::function<bool(const ActorComponents&,float&,std::string&)>& elapsed,std::string&);
    const std::vector<uint64_t>& active()const{return active_;}
    const std::vector<uint64_t>& dirty()const{return dirty_;}
    std::string dump()const;
};
bool lifeOfInterest(const ActorLife&);
// Returns native UpdateThing's keep-membership bool; no zero clamping.
bool advanceLifeTimers(ActorLife&,float elapsed,bool& keep,std::string&);
}
