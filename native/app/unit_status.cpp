#include "unit_status.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace hades {
namespace {
template<class T>bool contains(const std::vector<T>& v,const T& x){return std::find(v.begin(),v.end(),x)!=v.end();}
struct MutationGuard{bool& busy;explicit MutationGuard(bool& b):busy(b){busy=true;}~MutationGuard(){busy=false;}};
}
bool UnitStatus::fault(const std::string& message,std::string& error){faulted_=true;fault_=message;error=message;return false;}
bool UnitStatus::initializeFreshUnit(std::string& error){if(inMutation_||faulted_||!pending_.empty()||!active_.empty()||!toDelete_.empty()){error="Fresh status initialization requires a fresh effect-free unit";return false;}timers.move=timers.attack=timers.rotate=-1;canceledNames_.clear();blockAll=false;blockEffectId=0;immuneEffects.clear();error.clear();return true;}
bool UnitStatus::trackAdmittedEffect(std::shared_ptr<UnitStatusEffect> effect,std::string& error){
    if(faulted_){error=fault_;return false;}if(!effect||!effect->handle||effect->unitId<0){error="Status tracking requires an actual admitted effect identity and unit";return false;}
    for(const auto& list:{&pending_,&active_,&toDelete_})for(const auto& e:*list)if(e->handle==effect->handle&&e!=effect){error="Different effect objects share a lifetime handle";return false;}
    // Native AddEffect applies its own duplicate/admission policies before this
    // seam. Retain repeated references exactly if the caller actually admitted it.
    pending_.push_back(std::move(effect));error.clear();return true;
}
bool UnitStatus::clearEffects(const UnitStatusEffectCallback& clear,std::string& error){
    if(faulted_){error=fault_;return false;}if(inMutation_){error="Reentrant ClearEffects requires original iterator lifetime handling";return false;}
    MutationGuard guard(inMutation_);
    // ClearEffects2fd520 captures active then pending, unconditionally appends
    // each retained reference to deletion even if Clear had already run.
    const auto active=active_;
    auto phase=[&](const auto& captured){for(const auto& effect:captured){std::string detail;
        if(!effect->cleared&&(!clear||!clear(*effect,detail)))return fault(detail.empty()?"ClearEffects requires actual virtual Clear":detail,error);
        toDelete_.push_back(effect);
    }return true;};
    if(!phase(active))return false;const auto pending=pending_;if(!phase(pending))return false;error.clear();return true;
}
bool UnitStatus::reset(const std::function<bool(std::string&)>& setAsAlly,
    const UnitStatusEffectCallback& clear,const UnitStatusEffectCallback& dispose,std::string& error){
    if(faulted_){error=fault_;return false;}if(inMutation_||updatingEffects_){error="Status Reset requires original non-updating teardown phase";return false;}
    timers.move=timers.rotate=timers.attack=-1;
    if(isTraitor){if(!setAsAlly){error="Traitor Reset requires real original-allegiance restore and pending manager switch";return false;}if(!setAsAlly(error))return false;isTraitor=false;}
    allegianceTime=0;
    if(!clearEffects(clear,error)||!cleanupEffects(dispose,error))return false;
    blockAll=false;blockEffectId=0;immuneEffects.clear();blockEffectDuration=0;error.clear();return true;
}
bool UnitStatus::enqueueEffectRemoval(uint64_t handle,std::string& error){
    if(faulted_){error=fault_;return false;}
    for(const auto* list:{&active_,&pending_,&toDelete_})for(const auto& effect:*list)if(effect->handle==handle){auto retained=effect;toDelete_.push_back(std::move(retained));error.clear();return true;}
    error="Native deletion queue requires an actual retained status effect";return false;
}
bool UnitStatus::canAttack(bool editingMode,bool immuneToStun,bool allow)const{
    if(faulted_||editingMode||(timers.attack>0&&!allow))return false;
    for(const auto* list:{&pending_,&active_})for(const auto& e:*list)
        if(e->disableAttack&&!(allow&&e->cancelable)&&(!immuneToStun||e->selfApplied))return false;
    return true;
}
bool UnitStatus::canMove()const{
    if(faulted_||timers.move>0)return false;
    for(const auto* list:{&pending_,&active_})for(const auto& e:*list)
        if(e->disableMove)return false;
    return true;
}
std::shared_ptr<UnitStatusEffect> UnitStatus::getEffect(const std::string& name)const{
    for(const auto& effect:toDelete_)if(effect->name==name)return {};
    for(const auto* list:{&active_,&pending_})for(const auto& effect:*list)if(effect->name==name)return effect;
    return {};
}
bool UnitStatus::advanceDisableTimers(float elapsed,float modifier,std::string& error){
    if(faulted_){error=fault_;return false;}if(!std::isfinite(elapsed)||elapsed<0||!std::isfinite(modifier)){error="Native status timer requires finite unit elapsed time and modifier";return false;}
    const float delta=elapsed*modifier;UnitDisableTimers next=timers;
    if(next.move>0)next.move-=delta;if(next.rotate>0)next.rotate-=delta;if(next.attack>0)next.attack-=delta;
    if(!std::isfinite(delta)||!std::isfinite(next.move)||!std::isfinite(next.attack)||!std::isfinite(next.rotate)){error="Native status timer result is outside the finite portable subset";return false;}
    timers=next;error.clear();return true;
}
bool UnitStatus::promotePendingEffects(std::string& error){
    if(faulted_){error=fault_;return false;}if(inMutation_||updatingEffects_){error="Status promotion during an effect callback needs the original update phase";return false;}
    // Native performs a duplicate search but still unconditionally appends in
    // this shipped build. Admission owns uniqueness; preserve actual repeats.
    active_.insert(active_.end(),pending_.begin(),pending_.end());pending_.clear();error.clear();return true;
}
bool UnitStatus::advanceEffects(float elapsed,const UnitStatusEffectTickInputs& inputs,
    const UnitStatusEffectUpdate& update,const UnitStatusEffectCallback& dispose,std::string& error){
    if(faulted_){error=fault_;return false;}
    if(inMutation_||updatingEffects_||!std::isfinite(elapsed)){error="Effect update phase requires finite elapsed time outside nested status mutation";return false;}
    if(!promotePendingEffects(error))return false;
    {
        MutationGuard guard(updatingEffects_);
        const auto captured=active_;
        for(const auto& effect:captured){
            if(!inputs||!update)return fault("Status effect phase requires actual effect data/owner queries and virtual Update",error);
            float fraction=0,ownerTime=0;std::string detail;
            if(!inputs(*effect,fraction,ownerTime,detail))return fault(detail.empty()?"Effect tick input query failed":detail,error);
            if(!std::isfinite(fraction)||!std::isfinite(ownerTime))return fault("Effect tick data or owner time multiplier is nonfinite",error);
            float effectElapsed=elapsed;
            if(fraction<1){
                volatile float remainder=1.0f-ownerTime;
                volatile float weighted=remainder*fraction;
                volatile float denominator=weighted+ownerTime;
                volatile float adjusted=elapsed/denominator;
                effectElapsed=adjusted;
            }
            if(!std::isfinite(effectElapsed))return fault("Native effect elapsed scaling is outside the finite portable subset",error);
            bool expired=false;
            if(!update(*effect,effectElapsed,expired,detail))return fault(detail.empty()?"Actual virtual Effect::Update failed":detail,error);
            if(expired)toDelete_.push_back(effect);
        }
    }
    return cleanupEffects(dispose,error);
}
bool UnitStatus::cancelDisables(const std::vector<std::string>& own,bool canOwn,const UnitStatusEffectCallback& clear,std::string& error){
    if(faulted_){error=fault_;return false;}if(inMutation_){error="Reentrant status cancellation is outside the recovered portable subset";return false;}MutationGuard guard(inMutation_);
    if(timers.moveCancelable)timers.move=-1;if(timers.rotateCancelable)timers.rotate=-1;if(timers.attackCancelable)timers.attack=-1;canceledNames_.clear();
    auto cancel=[&](const auto& captured,bool pending){for(const auto& e:captured){if(!e->cancelable||(contains(own,e->name)&&(pending||!canOwn)))continue;
        if(!e->cleared){if(!clear)return fault("Effect cancellation requires the real Effect::Clear executor",error);std::string detail;if(!clear(*e,detail))return fault(detail.empty()?"Effect::Clear executor failed":detail,error);}
        toDelete_.push_back(e);canceledNames_.push_back(e->name);
    }return true;};
    // Native each phase captures its initial count. New pending effects admitted
    // synchronously by Clear are retained for the following update phase.
    const auto pending=pending_;if(!cancel(pending,true))return false;
    const auto active=active_;if(!cancel(active,false))return false;error.clear();return true;
}
bool UnitStatus::cancelRotateDisables(const std::vector<std::string>& own,bool canOwn,const UnitStatusEffectCallback& clear,std::string& error){
    // Matching PDB/static source: native-weapon-cancel-rotate-static-v1.json.
    if(faulted_){error=fault_;return false;}if(inMutation_){error="Reentrant rotation cancellation requires native callback iterator handling";return false;}MutationGuard guard(inMutation_);
    if(timers.rotateCancelable)timers.rotate=-1;canceledNames_.clear();
    const auto active=active_;
    for(const auto& effect:active){
        if(!effect->cancelable||effect->disableAttack||!effect->disableRotate||(!canOwn&&contains(own,effect->name)))continue;
        if(!effect->cleared){std::string detail;if(!clear||!clear(*effect,detail))return fault(detail.empty()?"Rotation cancellation requires actual Effect::Clear":detail,error);}
        toDelete_.push_back(effect);canceledNames_.push_back(effect->name);
    }
    error.clear();return true;
}
bool UnitStatus::cancelSelfEffects(const std::string& name,const UnitStatusEffectCallback& clear,
    const UnitStatusEffectCallback& dispose,std::string& error){
    if(faulted_){error=fault_;return false;}
    if(inMutation_){error="Reentrant self-effect vector cancellation requires native iterator invalidation handling";return false;}
    MutationGuard guard(inMutation_);
    const auto pending=pending_;
    for(auto it=pending.rbegin();it!=pending.rend();++it){const auto& e=*it;
        if(e->name!=name)continue;
        std::string detail;
        if(!e->cleared&&(!clear||!clear(*e,detail)))return fault(detail.empty()?"Self-effect cancellation requires actual Clear":detail,error);
        if(!e->disposed){e->unitId=-1;
            if(!dispose||!dispose(*e,detail))return fault(detail.empty()?"Self-effect cancellation requires actual Dispose(true)":detail,error);
            e->disposed=true;
        }
        auto current=std::find(pending_.rbegin(),pending_.rend(),e);
        if(current==pending_.rend())return fault("Self-effect callback removed captured pending identity",error);
        pending_.erase(std::next(current).base());
    }
    const auto active=active_;
    for(const auto& e:active){if(e->name!=name)continue;std::string detail;
        if(!e->cleared&&(!clear||!clear(*e,detail)))return fault(detail.empty()?"Self-effect cancellation requires actual Clear":detail,error);
        toDelete_.push_back(e);
    }
    error.clear();return true;
}
bool UnitStatus::clearOnAttackEffects(const UnitStatusEffectPredicate& selected,
    const UnitStatusEffectCallback& clear,std::string& error){
    if(faulted_){error=fault_;return false;}
    if(inMutation_){error="Reentrant ClearOnAttack effects mutation is outside retained iterator contract";return false;}
    MutationGuard guard(inMutation_);const auto active=active_;
    for(const auto& e:active){bool match=false;std::string detail;
        if(!selected||!selected(*e,match,detail))return fault(detail.empty()?"ClearOnAttack needs actual EffectData predicate":detail,error);
        if(!match)continue;
        if(!e->cleared&&(!clear||!clear(*e,detail)))return fault(detail.empty()?"ClearOnAttack requires actual virtual Clear":detail,error);
        toDelete_.push_back(e);
    }
    error.clear();return true;
}
bool UnitStatus::cleanupEffects(const UnitStatusEffectCallback& dispose,std::string& error){
    if(faulted_){error=fault_;return false;}if(updatingEffects_){error.clear();return true;}if(inMutation_){error="Status cleanup during an effect callback requires the native updating guard";return false;}MutationGuard guard(inMutation_);
    const auto deleting=toDelete_;size_t consumed=0;
    for(const auto& e:deleting){if(!e->disposed){e->unitId=-1;if(!dispose)return fault("Effect cleanup requires the real virtual Dispose(true) executor",error);std::string detail;if(!dispose(*e,detail))return fault(detail.empty()?"Effect disposal executor failed":detail,error);e->disposed=true;}
        std::erase(active_,e);std::erase(pending_,e);++consumed;
    }
    toDelete_.erase(toDelete_.begin(),toDelete_.begin()+consumed);error.clear();return true;
}
UnitStatusSnapshot UnitStatus::snapshot()const{UnitStatusSnapshot s;s.timers=timers;s.canceledNames=canceledNames_;s.faulted=faulted_;s.fault=fault_;for(const auto& e:pending_)s.pending.push_back(e->handle);for(const auto& e:active_)s.active.push_back(e->handle);for(const auto& e:toDelete_)s.toDelete.push_back(e->handle);return s;}
std::string UnitStatus::dump()const{const auto s=snapshot();std::ostringstream o;o<<"UNIT_STATUS timers="<<timers.move<<','<<timers.attack<<','<<timers.rotate<<" pending="<<s.pending.size()<<" active="<<s.active.size()<<" deleting="<<s.toDelete.size()<<" fault="<<faulted_;if(faulted_)o<<" reason="<<fault_;return o.str();}
}
