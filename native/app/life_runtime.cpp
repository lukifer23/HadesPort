#include "life_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
namespace hades {
bool lifeOfInterest(const ActorLife& l){
    // OfInterest2cf5f0: Life+54 >0 || +34 >0 || +58 >0.
    return l.consecutiveHitWindow>0||l.activeHitFxCount>0||l.activeHitFxCooldown>0;
}
bool advanceLifeTimers(ActorLife& l,float elapsed,bool& keep,std::string& error){
    if(!std::isfinite(elapsed)||elapsed<0||!std::isfinite(l.consecutiveHitWindow)||!std::isfinite(l.activeHitFxCooldown)){error="Invalid native LifeSystem elapsed/state";return false;}
    // UpdateThing2cf620: test current positive timer, subtract, reset counter
    // after crossing0. Active FX count contributes keep BEFORE cooldown reset.
    keep=false;
    if(l.consecutiveHitWindow>0){l.consecutiveHitWindow-=elapsed;if(l.consecutiveHitWindow<=0)l.consecutiveHits=0;else keep=true;}
    if(l.activeHitFxCount>0)keep=true;
    if(l.activeHitFxCooldown>0){l.activeHitFxCooldown-=elapsed;if(l.activeHitFxCooldown<=0){l.activeHitFxCount=0;l.activeHitFxCooldown=1;}}
    error.clear();return true;
}
bool LifeSystemRuntime::registerActor(int32_t id,std::string& error){
    auto* a=actors_.actor(id);if(!a||!a->registered||!a->hasLife||!a->life.initialized||!a->entityHandle){error="LifeSystem requires actual registered initialized Life component";return false;}
    if(updating_){error="LifeSystem registration during component iteration requires parent dirty phase";return false;}
    const auto [i,inserted]=identities_.emplace(a->entityHandle,id);
    if(!inserted&&i->second!=id){error="LifeSystem lifetime identity collision";return false;}
    if(lifeOfInterest(a->life)&&std::find(active_.begin(),active_.end(),a->entityHandle)==active_.end())active_.push_back(a->entityHandle);
    error.clear();return true;
}
bool LifeSystemRuntime::markDirty(int32_t id,std::string& error){
    auto* a=actors_.actor(id);if(!a||!a->registered||!a->hasLife||!a->life.initialized||!a->entityHandle){error="World.MarkDirty requires retained actual Life owner";return false;}
    identities_[a->entityHandle]=id;if(std::find(dirty_.begin(),dirty_.end(),a->entityHandle)==dirty_.end())dirty_.push_back(a->entityHandle);
    error.clear();return true;
}
bool LifeSystemRuntime::removeActor(uint64_t lifetime,std::string& error){
    if(updating_){error="Cannot dispose Life during component iteration";return false;}
    std::erase(active_,lifetime);std::erase(dirty_,lifetime);identities_.erase(lifetime);error.clear();return true;
}
bool LifeSystemRuntime::reconcile(std::string& error){
    if(updating_){error="Cannot reconcile Life memberships during native iteration";return false;}
    for(const auto h:dirty_){const auto i=identities_.find(h);if(i==identities_.end()){error="Life dirty queue lost identity";return false;}const auto* a=actors_.actor(i->second);
        const bool interested=a&&a->registered&&a->entityHandle==h&&a->hasLife&&a->life.initialized&&lifeOfInterest(a->life);const auto at=std::find(active_.begin(),active_.end(),h);
        if(interested&&at==active_.end())active_.push_back(h);else if(!interested&&at!=active_.end())active_.erase(at);
    }
    dirty_.clear();error.clear();return true;
}
bool LifeSystemRuntime::update(const std::function<bool(const ActorComponents&,float&,std::string&)>& elapsed,std::string& error){
    if(updating_||!elapsed){error="Invalid/reentrant native Life phase";return false;}updating_=true;
    std::vector<uint64_t> survivors;survivors.reserve(active_.size());
    for(const auto h:active_){const auto i=identities_.find(h);if(i==identities_.end()){updating_=false;error="Active Life identity missing";return false;}auto* a=actors_.actor(i->second);
        if(!a||!a->registered||a->entityHandle!=h||!a->hasLife||!a->life.initialized)continue;
        float dt=0;bool keep=false;if(!elapsed(*a,dt,error)||!advanceLifeTimers(a->life,dt,keep,error)){updating_=false;return false;}if(keep)survivors.push_back(h);
    }
    active_=std::move(survivors);updating_=false;error.clear();return true;
}
std::string LifeSystemRuntime::dump()const{std::ostringstream o;o<<"LIFE_SYSTEM active="<<active_.size()<<" dirty="<<dirty_.size()<<"\n";for(auto h:active_){const auto i=identities_.find(h);const auto* a=i==identities_.end()?nullptr:actors_.actor(i->second);if(a)o<<"LIFE owner="<<a->id<<" lifetime="<<h<<" hits="<<a->life.consecutiveHits<<" window="<<a->life.consecutiveHitWindow<<" activeFx="<<a->life.activeHitFxCount<<" cooldown="<<a->life.activeHitFxCooldown<<"\n";}return o.str();}
}
