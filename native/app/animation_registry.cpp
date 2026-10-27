#include "animation_registry.hpp"
#include <algorithm>
#include <stdexcept>

namespace hades {
const AnimationLifetime* AnimationRegistry::find(uint64_t id)const{const auto it=live_.find(id);return it==live_.end()?nullptr:&it->second;}
bool AnimationRegistry::allocate(const AnimationAllocation& a,uint64_t& id,std::string& error){
 if(a.name.empty()||a.name.size()>4096||a.name=="null"||(a.ownerLifetime==0&&(a.ownerId!=0||(a.ownership!=AnimationOwnership::ManagerCreate&&a.ownership!=AnimationOwnership::Child)))||a.manager>2||uint8_t(a.type)>uint8_t(NativeAnimationType::Text)||a.type==NativeAnimationType::Random||a.type==NativeAnimationType::Graft){error="Actual concrete animation allocation/owner/type unresolved";return false;}
 if(live_.size()>=100000||next_==UINT64_MAX){error="Native animation allocation bound";return false;}
 if(a.ownership==AnimationOwnership::Child){const auto* p=find(a.parent);if(!p||p->child||p->ownerId!=a.ownerId||p->ownerLifetime!=a.ownerLifetime||p->manager!=a.manager){error="Actual parent/owner lifetime for child allocation unavailable";return false;}}
 else if(a.parent){error="Only child admission may have parent";return false;}
 AnimationLifetime value;static_cast<AnimationAllocation&>(value)=a;value.handle=next_++;const auto* linked=a.sourceDefinition.get("UseAttachedTimeMultiplier");value.attachedToTimeMultiplier=a.ownerLifetime&&linked&&linked->boolean();id=value.handle;live_.emplace(id,std::move(value));if(a.parent)live_.at(a.parent).child=id;return true;
}
bool AnimationRegistry::commitInitialization(uint64_t id,bool skip,std::string& error){auto it=live_.find(id);if(it==live_.end()||it->second.initialized){error="Animation init requires a unique real pending lifetime";return false;}it->second.initialized=true;it->second.skipUpdate=skip;return true;}
bool AnimationRegistry::addToManager(uint64_t id,uint32_t cap,std::optional<uint64_t>& victim,std::string& error){
 victim.reset();auto it=live_.find(id);if(it==live_.end()||!it->second.initialized||it->second.managerMember||it->second.ownership!=AnimationOwnership::ManagerCreate||!cap||cap>100000){error="Native Add requires initialized ManagerCreate allocation and real cap";return false;}
 auto& entry=it->second;auto& order=managed_[entry.manager];
 if(entry.cancelOverCap){uint32_t count=1;for(auto i=order.rbegin();i!=order.rend();++i){auto& previous=live_.at(*i);if(!previous.cancelOverCap)continue;if(count>=cap){victim=*i;previous.canceled=true;break;}++count;}}
 order.push_back(id);entry.managerMember=true;return true;
}
bool AnimationRegistry::setSkipUpdate(uint64_t id,bool value,std::string& error){auto it=live_.find(id);if(it==live_.end()||!it->second.initialized){error="Actual initialized animation visibility lifetime missing";return false;}it->second.skipUpdate=value;return true;}
// Animation.UseAttachedToTimeMultiplier299d40 adds ModifiedTime and a live
// Thing pointer component; clearing an effect changes Thing+80, not this link.
bool AnimationRegistry::linkTimeMultiplier(uint64_t id,int32_t owner,uint64_t lifetime,std::string& error){auto it=live_.find(id);if(it==live_.end()||!it->second.initialized||it->second.ownerId!=owner||it->second.ownerLifetime!=lifetime){error="Actual animation clock attachment lifetime unavailable";return false;}it->second.attachedToTimeMultiplier=true;error.clear();return true;}
bool AnimationRegistry::cancel(uint64_t id,std::string& error){auto it=live_.find(id);if(it==live_.end()){error="Actual canceled animation lifetime missing";return false;}it->second.canceled=true;return true;}
bool AnimationRegistry::restart(uint64_t id,std::string& error){auto it=live_.find(id);if(it==live_.end()||!it->second.initialized){error="Actual initialized animation lifetime missing for source Restart";return false;}it->second.canceled=false;return true;}
bool AnimationRegistry::reparentChild(uint64_t child,uint64_t parent,std::string& error){
 auto found=live_.find(child);if(found==live_.end()||found->second.ownership!=AnimationOwnership::Child||found->second.managerMember){error="Native child transfer requires its actual child allocation";return false;}
 auto& c=found->second;
 if(parent){const auto* p=find(parent);if(!p||p->child||p->ownerId!=c.ownerId||p->ownerLifetime!=c.ownerLifetime||p->manager!=c.manager){error="Native child transfer parent/owner lifetime mismatch";return false;}
  for(auto ancestor=parent;ancestor;){if(ancestor==child){error="Native child transfer would form a cycle";return false;}const auto* p=find(ancestor);if(!p){error="Native child transfer ancestor lifetime missing";return false;}ancestor=p->parent;}}
 if(c.parent){auto p=live_.find(c.parent);if(p==live_.end()||p->second.child!=child){error="Native child transfer old parent link mismatch";return false;}p->second.child=0;}
 c.parent=parent;if(parent)live_.at(parent).child=child;error.clear();return true;
}
bool AnimationRegistry::dispose(uint64_t id,std::string& error){auto it=live_.find(id);if(it==live_.end()){error="Actual animation disposal lifetime missing";return false;}const auto entry=it->second;if(entry.child&&!dispose(entry.child,error))return false;if(entry.parent){auto p=live_.find(entry.parent);if(p!=live_.end())p->second.child=0;}if(entry.managerMember){auto& order=managed_.at(entry.manager);const auto i=std::find(order.begin(),order.end(),id);if(i==order.end())throw std::runtime_error("Native animation manager ownership invariant");order.erase(i);}live_.erase(id);return true;}
std::span<const uint64_t> AnimationRegistry::managed(uint32_t manager)const{auto i=managed_.find(manager);return i==managed_.end()?std::span<const uint64_t>{}:std::span<const uint64_t>{i->second};}
size_t AnimationRegistry::managerCount(uint32_t manager)const{return managed(manager).size();}
bool AnimationRegistry::allAllocationsInitialized()const{for(const auto& [id,a]:live_){(void)id;if(!a.initialized)return false;}return true;}
}

namespace hades {
std::vector<uint64_t> AnimationRegistry::ownerAllocations(int32_t id,uint64_t lifetime)const {
    std::vector<uint64_t> handles;
    for(const auto& [handle,allocation]:live_)if(allocation.ownerId==id&&allocation.ownerLifetime==lifetime)handles.push_back(handle);
    return handles;
}
}
