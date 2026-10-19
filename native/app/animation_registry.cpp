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
 AnimationLifetime value;static_cast<AnimationAllocation&>(value)=a;value.handle=next_++;id=value.handle;live_.emplace(id,std::move(value));if(a.parent)live_.at(a.parent).child=id;return true;
}
bool AnimationRegistry::commitInitialization(uint64_t id,bool skip,std::string& error){auto it=live_.find(id);if(it==live_.end()||it->second.initialized){error="Animation init requires a unique real pending lifetime";return false;}it->second.initialized=true;it->second.skipUpdate=skip;return true;}
bool AnimationRegistry::addToManager(uint64_t id,uint32_t cap,std::optional<uint64_t>& victim,std::string& error){
 victim.reset();auto it=live_.find(id);if(it==live_.end()||!it->second.initialized||it->second.managerMember||it->second.ownership!=AnimationOwnership::ManagerCreate||!cap||cap>100000){error="Native Add requires initialized ManagerCreate allocation and real cap";return false;}
 auto& entry=it->second;auto& order=managed_[entry.manager];
 if(entry.cancelOverCap){uint32_t count=1;for(auto i=order.rbegin();i!=order.rend();++i){auto& previous=live_.at(*i);if(!previous.cancelOverCap)continue;if(count>=cap){victim=*i;previous.canceled=true;break;}++count;}}
 order.push_back(id);entry.managerMember=true;return true;
}
bool AnimationRegistry::setSkipUpdate(uint64_t id,bool value,std::string& error){auto it=live_.find(id);if(it==live_.end()||!it->second.initialized){error="Actual initialized animation visibility lifetime missing";return false;}it->second.skipUpdate=value;return true;}
bool AnimationRegistry::cancel(uint64_t id,std::string& error){auto it=live_.find(id);if(it==live_.end()){error="Actual canceled animation lifetime missing";return false;}it->second.canceled=true;return true;}
bool AnimationRegistry::restart(uint64_t id,std::string& error){auto it=live_.find(id);if(it==live_.end()||!it->second.initialized){error="Actual initialized animation lifetime missing for source Restart";return false;}it->second.canceled=false;return true;}
bool AnimationRegistry::dispose(uint64_t id,std::string& error){auto it=live_.find(id);if(it==live_.end()){error="Actual animation disposal lifetime missing";return false;}const auto entry=it->second;if(entry.child&&!dispose(entry.child,error))return false;if(entry.parent){auto p=live_.find(entry.parent);if(p!=live_.end())p->second.child=0;}if(entry.managerMember){auto& order=managed_.at(entry.manager);const auto i=std::find(order.begin(),order.end(),id);if(i==order.end())throw std::runtime_error("Native animation manager ownership invariant");order.erase(i);}live_.erase(id);return true;}
std::span<const uint64_t> AnimationRegistry::managed(uint32_t manager)const{auto i=managed_.find(manager);return i==managed_.end()?std::span<const uint64_t>{}:std::span<const uint64_t>{i->second};}
size_t AnimationRegistry::managerCount(uint32_t manager)const{return managed(manager).size();}
bool AnimationRegistry::allAllocationsInitialized()const{for(const auto& [id,a]:live_){(void)id;if(!a.initialized)return false;}return true;}
}
