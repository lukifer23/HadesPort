#include "self_effect_world.hpp"
#include "platform_log.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace hades {
namespace {
bool number(const WeaponEffectDefinition& d,const char* key,float& out,std::string& e){
    auto it=d.fields.find(key);if(it==d.fields.end()||!std::holds_alternative<double>(it->second)||!std::isfinite(std::get<double>(it->second))){e=std::string("Missing actual effect numeric field ")+key;return false;}
    out=float(std::get<double>(it->second));return true;
}
bool flag(const WeaponEffectDefinition& d,const char* key,bool& out,std::string& e){
    auto it=d.fields.find(key);if(it==d.fields.end()||!std::holds_alternative<bool>(it->second)){e=std::string("Missing actual effect bool field ")+key;return false;}out=std::get<bool>(it->second);return true;
}
bool text(const WeaponEffectDefinition& d,const char* key,std::string& out,std::string& e){
    auto it=d.fields.find(key);if(it==d.fields.end()||!std::holds_alternative<std::string>(it->second)){e=std::string("Missing actual effect string field ")+key;return false;}out=std::get<std::string>(it->second);return true;
}
bool actorNumber(const ActorWorld& world,int32_t id,const char* key,float& out,std::string& e){
    const auto* p=world.currentProperty(id,key);if(!p||!std::holds_alternative<double>(p->value)||!std::isfinite(p->number())){e=std::string("Actual live Unit property unavailable: ")+key;return false;}out=float(p->number());return true;
}
bool actorFlag(const ActorWorld& world,int32_t id,const char* key,bool& out,std::string& e){
    const auto* p=world.currentProperty(id,key);if(!p||!std::holds_alternative<bool>(p->value)){e=std::string("Actual live Unit bool unavailable: ")+key;return false;}out=p->boolean();return true;
}
float div(float a,float b){volatile float value=a/b;return value;}
float add(float a,float b){volatile float value=a+b;return value;}
float sub(float a,float b){volatile float value=a-b;return value;}
WeaponSelfEffectSnapshot snapshotOf(const WeaponSelfEffectInstance& p){const auto& s=p.state;const auto& t=*s.status;
    return {p.handle,p.dataHandle,t.unitId,p.sourceOwner,p.factoryType,s.numStacks,t.name,p.sourceWeapon,s.timeRemaining,s.modifier,s.appliedModifier,s.tagCooldown,t.selfApplied,t.cleared,t.disposed,s.tagActive};}
}
bool WeaponSelfEffectWorld::fail(const std::string& message,std::string& error){faulted_=true;fault_=message;error=message;return false;}
bool WeaponSelfEffectWorld::countOwnerAttack(int32_t owner,const WeaponSelfEffectProviders& providers,std::string& error){
    auto* actor=actors_.actor(owner);if(!actor||!actor->registered||!actor->status){error="Native TakeHit attack counter requires actual source UnitStatus";return false;}
    // Life.TakeHit2b8fxx captures ONLY active status entries, without cleared
    // filtering; every one increments even if ClearOnDamageInflictedInstances is disabled.
    // TakeHit VA1802b8xxx: ++Effect+0x10, threshold EffectData+0x1c8 =
    // EffectDataDef+0x160 mClearOnDamageInflictedInstances (int; >0 enables).
    for(const auto h:actor->status->snapshot().active){auto* effect=instance(h);const auto* data=effectData(h,error);if(!effect||!data)return fail(error.empty()?"Active hit effect storage missing":error,error);
        float threshold=0;if(!number(*data,"ClearOnDamageInflictedInstances",threshold,error))return fail(error,error);
        if(effect->attacksMade==INT32_MAX)return fail("Native effect attack counter outside portable signed range",error);
        ++effect->attacksMade;
        if(threshold>0&&threshold<=effect->attacksMade){
            if(!effect->state.status->cleared&&!clear(*effect,providers,error))return fail(error,error);
            if(!actor->status->enqueueEffectRemoval(h,error))return fail(error,error);
        }
    }error.clear();return true;
}
bool WeaponSelfEffectWorld::clearOnCritted(int32_t victim,const WeaponSelfEffectProviders& providers,std::string& error){
    if(faulted_){error=fault_;return false;}auto* actor=unit(victim,error);if(!actor)return false;
    // Projectile.ApplyImpact captures only active effects; Clear then direct
    // mToDelete append, including an already-cleared selected record.
    for(const auto h:actor->status->snapshot().active){auto* actual=instance(h);const auto* data=effectData(h,error);if(!actual||!data)return fail(error.empty()?"Critical victim effect storage missing":error,error);bool selected=false;if(!flag(*data,"ClearOnCritted",selected,error))return fail(error,error);if(!selected)continue;
        if(!actual->state.status->cleared&&!clear(*actual,providers,error))return fail(error,error);
        if(!actor->status->enqueueEffectRemoval(h,error))return fail(error,error);
    }error.clear();return true;
}
ActorComponents* WeaponSelfEffectWorld::unit(int32_t id,std::string& error){
    auto* p=actors_.actor(id);if(!p||!p->registered||!p->status||!p->arsenal){error="Weapon self-effects require actual registered Unit/status/Arsenal";return nullptr;}return p;
}
SpeedEffectOwner* WeaponSelfEffectWorld::ownerView(int32_t id,std::string& error){
    auto* p=actors_.actor(id);if(!p||!p->registered||!p->status)return nullptr;
    SpeedEffectOwner next;next.id=id;next.elapsedTimeMultiplier=p->timeMultiplier;
    next.speedModifier=p->speedModifier;next.speedAddition=p->speedAddition;
    next.hasPhysics=bool(p->physics);next.hasAttachedAnimation=p->animation.active()||p->primaryEffectAnimation||p->primaryAnimationLifetime;
    if(!actorNumber(actors_,id,"SlowEffectModifier",next.slowModifier,error)||!actorFlag(actors_,id,"ImmuneToStun",next.immuneToStun,error))return nullptr;
    ownerViews_[id]=next;return &ownerViews_.at(id);
}
bool WeaponSelfEffectWorld::commitOwner(int32_t id,std::string& error){
    auto* p=actors_.actor(id);auto it=ownerViews_.find(id);
    if(!p||!p->registered||it==ownerViews_.end()){error="Actual effect owner disappeared before live-field commit";return false;}
    const auto& view=it->second;
    if(!std::isfinite(view.elapsedTimeMultiplier)||!std::isfinite(view.speedModifier)||!std::isfinite(view.speedAddition)){error="Effect owner live fields became nonfinite";return false;}
    p->timeMultiplier=view.elapsedTimeMultiplier;p->speedModifier=view.speedModifier;p->speedAddition=view.speedAddition;return true;
}
const WeaponEffectDefinition* WeaponSelfEffectWorld::definition(const WeaponSelfEffectInstance& instance,std::string& error)const{
    const auto* owner=actors_.actor(instance.sourceOwner);
    // ActorWorld retains component/Arsenal storage when a Unit unregisters;
    // this is the portable lifetime binding, not native RefCount bit emulation.
    if(!owner||!owner->arsenal){error="Actual source weapon storage no longer exists";return nullptr;}
    const auto* weapon=owner->arsenal->weaponState(instance.sourceWeapon,error);if(!weapon)return nullptr;
    if(instance.sourceProjectile){
        const auto* projectile=owner->arsenal->projectileData(instance.sourceWeapon,instance.sourceSpawned,error);if(!projectile)return nullptr;
        if(instance.sourceIndex>=projectile->effects.size()){error="Source projectile EffectData vector changed during live effect lifetime";return nullptr;}
        return &projectile->effects[instance.sourceIndex];
    }
    if(instance.sourceIndex>=weapon->definition.effects.size()){error="Source EffectData vector identity changed during live effect lifetime";return nullptr;}
    return &weapon->definition.effects[instance.sourceIndex];
}
bool WeaponSelfEffectWorld::runtime(WeaponSelfEffectInstance& instance,WeaponEffectRuntimeData& data,std::string& error){
    const auto* source=definition(instance,error);if(!source||!projectWeaponEffectRuntimeData(*source,instance.dataHandle,data,error))return false;
    if(data.nativeType!=instance.factoryType){error="Live effect Type changed after concrete factory allocation";return false;}
    if(instance.state.status){auto& status=*instance.state.status;status.name=data.statusPrototype.name;status.cancelable=data.statusPrototype.cancelable;status.disableAttack=data.statusPrototype.disableAttack;status.disableMove=data.statusPrototype.disableMove;status.disableRotate=data.statusPrototype.disableRotate;}
    return true;
}
bool WeaponSelfEffectWorld::affect(WeaponSelfEffectInstance& instance,int32_t ownerId,const WeaponSelfEffectProviders& providers,std::string& error){
    WeaponEffectRuntimeData data;if(!runtime(instance,data,error))return false;
    auto* owner=ownerView(ownerId,error);if(!owner){if(error.empty())error="AffectUnit owner lookup failed";return false;}
    auto execute=[&](EffectAffectOperation op,EffectAffectState&,EffectAffectOwner&,const EffectAffectData&,std::string& e){
        if(!commitOwner(ownerId,e))return false;
        const auto* source=definition(instance,e);if(!source)return false;
        if(!providers.affect){e="Actual self-effect AffectUnit operation/Lua executor missing";return false;}
        if(!providers.affect(op,instance,*owner,*source,false,e))return false;
        // Reacquire after synchronous original callbacks, including data edits.
        if(!ownerView(ownerId,e)){if(e.empty())e="Effect owner disappeared during AffectUnit";return false;}
        return runtime(instance,data,e);
    };
    auto lookup=[&](int32_t id){return ownerView(id,error);};
    bool ok=false;
    if(data.speed)ok=affectSpeedEffect(instance.state,*owner,*data.speed,execute,lookup,error);
    else if(data.nativeType==11){
        // InvulnerableEffect::AffectUnit5e580: Effect::AffectUnit5cb40 first, then its own body.
        ok=affectBaseEffect(instance.state,*owner,data.base,execute,error);
        if(ok){if(!commitOwner(ownerId,error))return false;const auto* source=definition(instance,error);if(!source)return false;
            if(!providers.invulnerable){error="InvulnerableEffect AffectUnit consumer missing";return false;}
            if(!providers.invulnerable(instance,ownerId,*source,false,error))return false;
            if(!ownerView(ownerId,error)){if(error.empty())error="Effect owner disappeared during InvulnerableEffect";return false;}}
    }
    else ok=affectTagEffect(instance.state,*owner,data.base,execute,error);
    if(!ok)return false;
    return commitOwner(ownerId,error);
}
bool WeaponSelfEffectWorld::clear(WeaponSelfEffectInstance& instance,const WeaponSelfEffectProviders& providers,std::string& error){
    if(!instance.state.status){error="No actual effect status at Clear";return false;}
    if(instance.state.status->cleared){error.clear();return true;}
    WeaponEffectRuntimeData data;if(!runtime(instance,data,error))return false;
    if(auto* actual=actors_.actor(instance.state.status->unitId);actual&&actual->registered&&!ownerView(actual->id,error))return false;
    auto baseClear=[&](SpeedEffectState&,std::string& e){
        const auto* source=definition(instance,e);if(!source)return false;
        const int32_t id=instance.state.status->unitId;
        auto* actual=actors_.actor(id);
        if(!actual||!actual->registered){
            // Native Effect.Clear5d560 still clears persistent sound/FX when
            // UnitManager.Get returns null during Unit's destructor. Every
            // owner-property/animation/ClearSound branch is inside that lookup.
            bool stop=false;float fade=0;if(!flag(*source,"StopSoundOnEnd",stop,e)||!number(*source,"StopSoundOnFinishFade",fade,e))return false;
            const bool needsOwnerIndependent=(instance.state.soundId!=0&&(stop||fade>0))||instance.state.frontFx||instance.state.overheadFx||instance.state.backFx;
            if(needsOwnerIndependent){if(!providers.complexBaseClear){e="Detached-owner Clear requires actual persistent sound/FX retirement";return false;}return providers.complexBaseClear(instance,*source,e);}
            instance.state.status->cleared=true;
            if(!providers.onCleared){e="Detached-owner Clear requires actual source callback admission";return false;}
            if(!providers.onCleared(instance,*source,e))return false;
            return runtime(instance,data,e);
        }
        if(actual&&actual->registered&&ownerViews_.contains(id)&&!commitOwner(id,e))return false;
        bool complex=false,b=false;float value=0;std::string name;
        for(const char* key:{"CancelAnimationOnClear","ClearAnimation","ClearFx","EnableWeaponForDuration","UnitPropertyName"}){if(!text(*source,key,name,e))return false;complex|=!name.empty()&&name!="null";}
        for(const char* key:{"IdleOnClear","StopSoundOnEnd"}){if(!flag(*source,key,b,e))return false;complex|=b;}
        bool haltOnEnd=false;if(!flag(*source,"HaltOnEnd",haltOnEnd,e))return false;
        for(const char* key:{"StopSoundOnFinishFade","BlockDurationOnExpire"}){if(!number(*source,key,value,e))return false;complex|=value>0;}
        auto cue=source->cues.find("ClearSound");if(cue==source->cues.end()){e="Missing real ClearSound normalization";return false;}
        complex|=!cue->second.name.empty();
        complex|=instance.state.frontFx||instance.state.overheadFx||instance.state.backFx;
        if(complex){
            if(!providers.complexBaseClear){e="Effect::Clear has actual FX/audio/property/animation branches requiring parent consumer";return false;}
            if(!providers.complexBaseClear(instance,*source,e))return false;
            if(!instance.state.status->cleared){e="Actual base Clear consumer did not mark effect cleared";return false;}
            return true;
        }
        // Effect::Clear5d560 marks cleared before any owner/callback operations.
        instance.state.status->cleared=true;
        if(actual&&actual->registered&&actual->lifeStatus==0&&instance.state.appliedElapsedTimeMultiplier!=1){
            actual->timeMultiplier=div(actual->timeMultiplier,instance.state.appliedElapsedTimeMultiplier);
            if(!std::isfinite(actual->timeMultiplier)){e="Effect Clear elapsed-time reciprocal became nonfinite";return false;}
        }
        // Effect::Clear5d560 VA18005d7cd: HaltOnEnd with an existing owner whose
        // pPhysics is present calls PhysicsComponent::Halt2bcd20.
        if(haltOnEnd&&actual&&actual->registered&&actual->physics){if(!providers.haltPhysics){e="Effect HaltOnEnd requires actual PhysicsComponent::Halt consumer";return false;}if(!providers.haltPhysics(id,e))return false;}
        if(!providers.onCleared){e="Actual original OnEffectCleared executor missing";return false;}
        if(!providers.onCleared(instance,*source,e))return false;
        if(actual&&actual->registered&&!ownerView(id,e))return false;
        return runtime(instance,data,e);
    };
    if(data.speed){
        auto lookup=[&](int32_t id){return ownerView(id,error);};
        return clearSpeedEffect(instance.state,*data.speed,lookup,baseClear,error);
    }
    if(data.nativeType==11){
        // InvulnerableEffect::Clear5e7c0: UnitManager::Get(+0x14); live owner body, then tail-jump Effect::Clear.
        const int32_t id=instance.state.status->unitId;auto* actual=actors_.actor(id);
        if(actual&&actual->registered){const auto* source=definition(instance,error);if(!source)return false;
            if(!providers.invulnerable){error="InvulnerableEffect Clear consumer missing";return false;}
            if(!providers.invulnerable(instance,id,*source,true,error))return false;
            if(!ownerView(id,error))return false;}
    }
    return baseClear(instance.state,error);
}
bool WeaponSelfEffectWorld::resetUnit(int32_t id,const WeaponSelfEffectProviders& providers,
    const std::function<bool(std::string&)>& setAsAlly,std::string& error){
    auto* actual=actors_.actor(id);if(!actual||!actual->status){error="Unit destructor status allocation unavailable";return false;}
    const auto clear=[&](UnitStatusEffect& effect,std::string& detail){auto* value=instance(effect.handle);if(!value){detail="Reset refers to unsupported effect factory ownership";return false;}return this->clear(*value,providers,detail);};
    const auto dispose=[&](UnitStatusEffect& effect,std::string& detail){return this->dispose(effect,detail);};
    if(!actual->status->reset(setAsAlly,clear,dispose,error))return false;
    ownerViews_.erase(id);collectDisposed();error.clear();return true;
}
bool WeaponSelfEffectWorld::clearNamedEffect(int32_t id,const std::string& name,const WeaponSelfEffectProviders& providers,std::string& error){
    if(faulted_){error=fault_;return false;}
    auto* actual=actors_.actor(id);if(!actual||!actual->status){error="ClearEffect requires actual UnitStatus (Unit+0x460)";return false;}
    const auto clear=[&](UnitStatusEffect& effect,std::string& detail){auto* value=instance(effect.handle);if(!value){detail="ClearEffect refers to unsupported effect factory ownership";return false;}return this->clear(*value,providers,detail);};
    const auto dispose=[&](UnitStatusEffect& effect,std::string& detail){return this->dispose(effect,detail);};
    if(!actual->status->clearEffect(name,clear,dispose,error))return fail(error,error);
    collectDisposed();error.clear();return true;
}
bool WeaponSelfEffectWorld::clearAllEffects(int32_t id,const std::vector<std::string>* exclude,const WeaponSelfEffectProviders& providers,
    const std::function<bool(std::string&)>& setAsAlly,std::string& error){
    if(faulted_){error=fault_;return false;}
    auto* actual=actors_.actor(id);if(!actual||!actual->status){error="ClearEffect All requires actual UnitStatus (Unit+0x460)";return false;}
    if(exclude){
        // VA1802f413b..429c: for each active entry (Unit+0x4b0..0x4b8), keep=true;
        // any ExcludeNames value string == EffectData name -> keep=false (sticky);
        // keep -> UnitStatus::ClearEffect2fd9d0(name). No Reset on this branch.
        for(const auto& name:actual->status->activeEffectNames()){
            if(std::find(exclude->begin(),exclude->end(),name)!=exclude->end())continue;
            if(!clearNamedEffect(id,name,providers,error))return false;
            actual=actors_.actor(id);if(!actual||!actual->status){error="ClearEffect owner lost during Clear callback";return false;}
        }
        error.clear();return true;
    }
    const auto clear=[&](UnitStatusEffect& effect,std::string& detail){auto* value=instance(effect.handle);if(!value){detail="ClearEffect All refers to unsupported effect factory ownership";return false;}return this->clear(*value,providers,detail);};
    if(!actual->status->clearActiveEffects(clear,error))return fail(error,error);
    return resetUnit(id,providers,setAsAlly,error);
}
bool WeaponSelfEffectWorld::reapply(WeaponSelfEffectInstance& instance,const WeaponEffectDefinition& incoming,const WeaponSelfEffectProviders& providers,std::string& error){
    WeaponEffectRuntimeData data;if(!runtime(instance,data,error))return false;
    const auto* old=definition(instance,error);if(!old)return false;
    bool extend=false;if(!flag(*old,"ExtendDurationOnReapply",extend,error))return false;
    if(extend){float duration=0;if(!number(incoming,"Duration",duration,error))return false;instance.state.timeRemaining=duration;}
    auto baseReapply=[&](SpeedEffectState&,std::string& e){
        auto* owner=ownerView(instance.state.status->unitId,e);if(!owner){if(e.empty())return true;return false;}
        const auto* source=definition(instance,e);if(!source)return false;
        bool stacks=false;if(!flag(*source,"Stacks",stacks,e))return false;
        if(data.base.elapsedTimeMultiplier!=1&&stacks){
            if(!providers.complexBaseReapply){e="Elapsed-time stacking Reapply needs actual native consumer";return false;}
            return providers.complexBaseReapply(instance,*source,e);
        }
        auto execute=[&](EffectAffectOperation op){
            if(!commitOwner(owner->id,e))return false;
            if(!providers.affect){e="Actual original Reapply/OnEffectApply executor missing";return false;}
            if(!providers.affect(op,instance,*owner,*source,true,e))return false;
            return ownerView(owner->id,e)!=nullptr;
        };
        if(data.base.disableAttack&&!owner->immuneToStun&&data.base.duration>0&&!execute(EffectAffectOperation::OnStun))return false;
        if(!execute(EffectAffectOperation::OnEffectApply))return false;
        std::string fx;if(!text(*source,"ReapplyFx",fx,e))return false;
        if(!fx.empty()&&fx!="null"){
            if(!providers.complexBaseReapply){e="Nonempty original ReapplyFx requires actual animation consumer";return false;}
            return providers.complexBaseReapply(instance,*source,e);
        }
        return runtime(instance,data,e);
    };
    if(data.speed){
        auto lookup=[&](int32_t id){return ownerView(id,error);};
        auto maxStacks=[&](int32_t& result,std::string& e){const auto* source=definition(instance,e);if(!source)return false;if(!providers.getMaxStacks){e="Actual Effect::GetMaxStacks callback missing";return false;}return providers.getMaxStacks(instance,*source,result,e);};
        if(!reapplySpeedEffect(instance.state,*data.speed,lookup,baseReapply,maxStacks,error))return false;
        if(actors_.actor(instance.state.status->unitId)&&actors_.actor(instance.state.status->unitId)->registered)return commitOwner(instance.state.status->unitId,error);
    }else{
        if(!baseReapply(instance.state,error))return false;
        // Tag::Reapply611a0: captured cooldown refreshed AFTER base callback.
        const auto* source=definition(instance,error);if(!source||!number(*source,"Cooldown",instance.state.tagCooldown,error))return false;
        instance.state.timeRemaining=add(instance.state.timeRemaining,instance.state.tagCooldown);
    }
    error.clear();return true;
}
bool WeaponSelfEffectWorld::dispose(UnitStatusEffect& status,std::string& error){
    auto it=effects_.find(status.handle);if(it==effects_.end()||it->second->state.status.get()!=&status){error="Status Dispose refers to an effect owned by an unsupported factory";return false;}
    // Speed/Tag Dispose only return this concrete allocation to its typed pool.
    // Portable ownership releases after all real status-vector references leave;
    // a bounded scalar history replaces the native typed pool's memory reuse.
    status.disposed=true;error.clear();return true;
}
void WeaponSelfEffectWorld::collectDisposed(){
    for(auto it=effects_.begin();it!=effects_.end();){const auto& value=*it->second;
        if(value.state.status->disposed&&value.state.status.use_count()==1){
            retired_.push_back(snapshotOf(value));if(retired_.size()>64)retired_.pop_front();
            it=effects_.erase(it);
        }else ++it;
    }
}
bool WeaponSelfEffectWorld::applyWeaponEffects(int32_t id,const std::string& weaponName,int32_t trigger,const WeaponSelfEffectProviders& providers,SelfEffectAdmissionResult& result,std::string& error){
    if(faulted_){error=fault_;return false;}auto* actor=unit(id,error);if(!actor)return false;
    const auto* weapon=actor->arsenal->weaponState(weaponName,error);if(!weapon)return false;
    auto chanceIt=weapon->definition.fields.find("AllEffectsProcChance");if(chanceIt==weapon->definition.fields.end()||!std::holds_alternative<double>(chanceIt->second)){error="Actual weapon AllEffectsProcChance missing";return false;}
    const float chance=float(std::get<double>(chanceIt->second));
    std::vector<std::shared_ptr<WeaponSelfEffect>> admission;
    std::map<uint64_t,size_t> indices;
    const size_t count=weapon->definition.effects.size();
    for(size_t i=0;i<count;++i){const auto key=std::make_tuple(id,weaponName,i);auto [it,inserted]=dataIds_.try_emplace(key,nextData_);if(inserted)++nextData_;
        auto value=std::make_shared<WeaponSelfEffect>();if(!projectWeaponEffectAdmission(weapon->definition.effects[i],it->second,*value,error))return false;indices[it->second]=i;admission.push_back(std::move(value));}
    auto refresh=[&](std::string& e){const auto* fresh=actor->arsenal->weaponState(weaponName,e);if(!fresh)return false;if(fresh->definition.effects.size()!=count){e="Captured native EffectData vector size changed during admission";return false;}
        for(size_t i=0;i<count;++i)if(!projectWeaponEffectAdmission(fresh->definition.effects[i],admission[i]->dataHandle,*admission[i],e))return false;return true;};
    SelfEffectCallbacks cb;cb.randomStream0=providers.randomStream0;
    cb.isPerfectCharge=[&](bool& perfect,std::string& e){perfect=actor->arsenal->isPerfectCharge(weaponName,e);return e.empty();};
    cb.findEffect=[&](const std::string& name,std::shared_ptr<UnitStatusEffect>& found,std::string& e){
        for(auto& [handle,owned]:effects_){(void)handle;if(owned->state.status->unitId==id&&!owned->state.status->disposed){WeaponEffectRuntimeData ignored;if(!runtime(*owned,ignored,e))return false;}}
        found=actor->status->getEffect(name);return true;};
    cb.createEffect=[&](const WeaponSelfEffect& incoming,float overrideDuration,std::shared_ptr<UnitStatusEffect>& created,std::string& e){
        if(overrideDuration!=0){e="Unsupported self EffectFactory duration override";return false;}
        if(nextEffect_==std::numeric_limits<uint64_t>::max()){e="Portable effect lifetime identities exhausted";return false;}
        auto record=std::make_unique<WeaponSelfEffectInstance>();record->handle=nextEffect_++;record->dataHandle=incoming.dataHandle;record->sourceOwner=id;record->sourceWeapon=weaponName;record->sourceIndex=indices.at(incoming.dataHandle);
        const auto* source=definition(*record,e);if(!source)return false;WeaponEffectRuntimeData data;if(!projectWeaponEffectRuntimeData(*source,record->dataHandle,data,e))return false;
        record->factoryType=data.nativeType;record->state.status=std::make_shared<UnitStatusEffect>(data.statusPrototype);record->state.status->handle=record->handle;record->state.hasSourceWeapon=true;record->state.tagCooldown=data.tagCooldown;
        if(!data.speed){float rate=0;if(!number(*source,"Rate",rate,e))return false;if(rate>0){e="Tag periodic Rate branch needs actual native event behavior";return false;}}
        created=record->state.status;effects_.emplace(record->handle,std::move(record));return true;};
    cb.affectUnit=[&](const WeaponSelfEffect&,UnitStatusEffect& actual,std::string& e){auto* owned=instance(actual.handle);if(!owned||!affect(*owned,id,providers,e))return false;return refresh(e);};
    cb.reapply=[&](const WeaponSelfEffect& incoming,UnitStatusEffect& actual,float duration,std::string& e){
        if(duration!=-1){e="Unsupported self Reapply override";return false;}auto* owned=instance(actual.handle);if(!owned){e="Existing effect belongs to an unimplemented external factory";return false;}
        const auto* fresh=actor->arsenal->weaponState(weaponName,e);if(!fresh)return false;const auto incomingCopy=fresh->definition.effects[indices.at(incoming.dataHandle)];
        if(!reapply(*owned,incomingCopy,providers,e))return false;return refresh(e);};
    if(!admitWeaponSelfEffects(chance,trigger,admission,*actor->status,cb,result,error))return fail(error,error);
    error.clear();return true;
}
bool WeaponSelfEffectWorld::admitProjectileEffect(int32_t victim,int32_t sourceOwner,const std::string& weaponName,
        bool spawned,size_t index,float impactAngle,const WeaponSelfEffectProviders& providers,std::string& error){
    if(faulted_){error=fault_;return false;}
    auto* actor=unit(victim,error);const auto* source=actors_.actor(sourceOwner);
    if(!actor||!source||!source->arsenal||!std::isfinite(impactAngle)){if(error.empty())error="Projectile effect requires actual source Weapon/victim/status/angle";return false;}
    const auto* projectile=source->arsenal->projectileData(weaponName,spawned,error);if(!projectile)return false;
    if(index>=projectile->effects.size()){error="Actual projectile effect index outside source vector";return false;}
    const auto incoming=projectile->effects[index];
    if(auto existing=actor->status->getEffect(incoming.name)){
        auto* record=instance(existing->handle);if(!record){error="Victim duplicate effect belongs to unsupported external factory";return false;}
        // UnitStatus.AddEffect2fdf3b..4f calls virtual Reapply on the existing
        // name-matched allocation, passing the incoming data/weapon/duration.
        // Effect.Reapply5d910..949 reads EXISTING ExtendDurationOnReapply,
        // optionally copies INCOMING Duration, then virtual noarg Reapply.
        // It never replaces Effect+20 data, source Weapon, angle or factory.
        // Speed/Tag inherit this overload; source identity equality is not a gate.
        const bool crossSource=!record->sourceProjectile||record->sourceOwner!=sourceOwner||record->sourceWeapon!=weaponName||record->sourceIndex!=index||record->sourceSpawned!=spawned;
        if(crossSource&&record->crossSourceReapplyDiagnostics<4){++record->crossSourceReapplyDiagnostics;
            log(Severity::Info,"HadesNative","EFFECT_REAPPLY victim=%d effect=%s retained=%d/%s/%zu incoming=%d/%s/%zu retainedData=%llu",victim,incoming.name.c_str(),record->sourceOwner,record->sourceWeapon.c_str(),record->sourceIndex,sourceOwner,weaponName.c_str(),index,(unsigned long long)record->dataHandle);}
        if(!reapply(*record,incoming,providers,error))return fail(error,error);error.clear();return true;
    }
    if(nextEffect_==UINT64_MAX||nextData_==UINT64_MAX){error="Portable effect lifetime/data identities exhausted";return false;}
    const auto key=std::make_tuple(sourceOwner,weaponName,index,spawned);auto [id,inserted]=projectileDataIds_.try_emplace(key,nextData_);if(inserted)++nextData_;
    auto record=std::make_unique<WeaponSelfEffectInstance>();record->handle=nextEffect_++;record->dataHandle=id->second;
    record->sourceOwner=sourceOwner;record->sourceWeapon=weaponName;record->sourceIndex=index;record->sourceProjectile=true;record->sourceSpawned=spawned;record->sourceAngle=impactAngle;
    WeaponEffectRuntimeData projected;if(!projectWeaponEffectRuntimeData(incoming,record->dataHandle,projected,error))return false;
    record->factoryType=projected.nativeType;record->state.status=std::make_shared<UnitStatusEffect>(projected.statusPrototype);record->state.status->handle=record->handle;record->state.status->selfApplied=false;
    record->state.hasSourceWeapon=true;record->state.tagCooldown=projected.tagCooldown;
    if(!projected.speed){float rate=0;if(!number(incoming,"Rate",rate,error))return false;if(rate>0){error="Victim Tag periodic Rate needs actual native event behavior";return false;}}
    const auto handle=record->handle;effects_.emplace(handle,std::move(record));
    // Native AddEffect creates factory, invokes virtual AffectUnit synchronously,
    // THEN publishes to mToAdd; original callback sees this actual allocation but
    // it is not already a committed/pending status member during AffectUnit.
    auto* actual=instance(handle);if(!actual||!affect(*actual,victim,providers,error))return fail(error.empty()?"Actual victim AffectUnit failed":error,error);
    if(!actor->status->trackAdmittedEffect(actual->state.status,error))return fail(error,error);
    error.clear();return true;
}
bool WeaponSelfEffectWorld::cancelWeaponEffects(int32_t id,const std::string& name,int32_t trigger,const WeaponSelfEffectProviders& providers,std::string& error){
    if(faulted_){error=fault_;return false;}auto* owner=unit(id,error);if(!owner)return false;
    const auto* weapon=owner->arsenal->weaponState(name,error);if(!weapon)return false;const size_t count=weapon->definition.effects.size();
    for(size_t i=0;i<count;++i){weapon=owner->arsenal->weaponState(name,error);if(!weapon)return fail(error,error);if(weapon->definition.effects.size()!=count)return fail("CancelSelfEffects source vector changed across callback",error);
        WeaponSelfEffect source;if(!projectWeaponEffectAdmission(weapon->definition.effects[i],1,source,error))return fail(error,error);if(!source.active||source.trigger!=trigger)continue;
        auto clearOne=[&](UnitStatusEffect& status,std::string& e){auto* actual=instance(status.handle);if(!actual){e="CancelSelfEffects selected unsupported external effect";return false;}return clear(*actual,providers,e);};
        auto disposeOne=[&](UnitStatusEffect& status,std::string& e){return dispose(status,e);};
        if(!owner->status->cancelSelfEffects(source.name,clearOne,disposeOne,error))return fail(error,error);
    }
    collectDisposed();error.clear();return true;
}
bool WeaponSelfEffectWorld::clearOnAttack(int32_t id,const WeaponSelfEffectProviders& providers,std::string& error){
    if(faulted_){error=fault_;return false;}auto* owner=unit(id,error);if(!owner)return false;
    auto selected=[&](const UnitStatusEffect& status,bool& match,std::string& e){auto* actual=instance(status.handle);if(!actual){e="ClearOnAttack selected external effect with unavailable EffectData";return false;}const auto* source=definition(*actual,e);return source&&flag(*source,"ClearOnAttack",match,e);};
    auto clearOne=[&](UnitStatusEffect& status,std::string& e){auto* actual=instance(status.handle);if(!actual){e="ClearOnAttack effect lifetime unavailable";return false;}return clear(*actual,providers,e);};
    if(!owner->status->clearOnAttackEffects(selected,clearOne,error))return fail(error,error);error.clear();return true;
}
bool WeaponSelfEffectWorld::updateUnit(int32_t id,float elapsed,const WeaponSelfEffectProviders& providers,std::string& error){
    if(faulted_){error=fault_;return false;}auto* owner=unit(id,error);if(!owner)return false;
    auto inputs=[&](const UnitStatusEffect& status,float& fraction,float& multiplier,std::string& e){auto* actual=instance(status.handle);if(!actual){e="Unit effect update encountered external factory";return false;}const auto* source=definition(*actual,e);if(!source||!number(*source,"TimeModifierFraction",fraction,e))return false;const auto* unit=actors_.actor(status.unitId);multiplier=unit&&unit->registered?unit->timeMultiplier:1;return true;};
    auto update=[&](UnitStatusEffect& status,float dt,bool& expired,std::string& e){
        auto* actual=instance(status.handle);if(!actual){e="Actual effect lifetime unavailable";return false;}WeaponEffectRuntimeData data;if(!runtime(*actual,data,e))return false;
        EffectUpdateCallbacks cb;
        cb.queryOwner=[&](int32_t ownerId,EffectUpdateOwner& query,std::string& detail){auto* actor=actors_.actor(ownerId);query.exists=actor&&actor->registered;if(!query.exists)return true;return actorNumber(actors_,ownerId,"StunBreakModifier",query.stunBreakModifier,detail)&&actorNumber(actors_,ownerId,"CharmBreakModifier",query.charmBreakModifier,detail);};
        cb.updateFx=[&](EffectAffectState&,EffectUpdateFxState&,std::string& detail){const auto* source=definition(*actual,detail);if(!source)return false;if(!providers.updateFx){detail="Actual effect UpdateFx consumer missing";return false;}return providers.updateFx(*actual,*source,detail);};
        cb.hasRefreshOnExpireEffect=[&](bool& value,std::string& detail){const auto* source=definition(*actual,detail);if(!source)return false;if(providers.hasRefreshOnExpireEffect)return providers.hasRefreshOnExpireEffect(*actual,*source,value,detail);return hasRefreshOnExpire(*actual,*source,value,detail);};
        cb.sourceWeaponOwnerAlive=[&](bool& alive,std::string&){const auto* source=actors_.actor(actual->sourceOwner);alive=source&&source->registered&&source->hasLife&&source->lifeStatus==0;return true;};
        cb.clearVirtual=[&](EffectAffectState&,std::string& detail){return clear(*actual,providers,detail);};
        auto baseUpdate=[&](SpeedEffectState&,std::string& detail){
            if(data.speed&&actors_.actor(status.unitId)&&actors_.actor(status.unitId)->registered&&ownerViews_.contains(status.unitId)&&!commitOwner(status.unitId,detail))return false;
            return updateBaseEffect(actual->state,actual->fx,data.update,dt,cb,expired,detail);};
        if(data.speed){auto lookup=[&](int32_t ownerId){return ownerView(ownerId,e);};if(!updateSpeedEffect(actual->state,*data.speed,dt,lookup,baseUpdate,e))return false;}
        else{
            if(!baseUpdate(actual->state,e))return false;
            if(!expired){actual->state.tagCooldown=sub(actual->state.tagCooldown,dt);if(actual->state.tagCooldown<=0&&!actual->state.tagActive){const auto* current=actors_.actor(status.unitId);if(current&&current->registered)actual->state.tagActive=true;}}
        }
        return true;
    };
    auto disposeOne=[&](UnitStatusEffect& status,std::string& e){return dispose(status,e);};
    if(!owner->status->advanceEffects(elapsed,inputs,update,disposeOne,error))return fail(error,error);
    collectDisposed();error.clear();return true;
}
bool WeaponSelfEffectWorld::clearEffect(uint64_t handle,const WeaponSelfEffectProviders& providers,std::string& error){
    if(faulted_){error=fault_;return false;}auto* actual=instance(handle);if(!actual){error="Unknown self-effect lifetime";return false;}if(!clear(*actual,providers,error))return fail(error,error);error.clear();return true;
}
WeaponSelfEffectInstance* WeaponSelfEffectWorld::instance(uint64_t handle){auto it=effects_.find(handle);return it==effects_.end()?nullptr:it->second.get();}
const WeaponEffectDefinition* WeaponSelfEffectWorld::effectData(uint64_t handle,std::string& error)const{auto it=effects_.find(handle);if(it==effects_.end()){error="No live effect data for this lifetime";return nullptr;}return definition(*it->second,error);}
bool WeaponSelfEffectWorld::hasRefreshOnExpire(const WeaponSelfEffectInstance& effect,const WeaponEffectDefinition& data,bool& result,std::string& error)const{
    result=false;if(!effect.state.status||!data.nativeIdsKnown){error="Actual effect header source IDs unavailable";return false;}
    const auto* owner=actors_.actor(effect.state.status->unitId);if(!owner||!owner->registered){error.clear();return true;}
    if(!owner->status){error="Actual effect owner has no status storage";return false;}
    const auto lists=owner->status->snapshot();
    auto has=[&](uint64_t nameId,bool& found){
        found=false;
        for(const auto* list:{&lists.toDelete,&lists.active,&lists.pending}){
            for(uint64_t handle:*list){const auto* candidate=effectData(handle,error);if(!candidate||!candidate->nativeIdsKnown){if(error.empty())error="HasEffect selected external unknown native ID";return false;}
                if(candidate->nativeNameId==nameId){found=list!=&lists.toDelete;return true;}}
        }
        return true;
    };
    if(!has(data.nativeRefreshId,result))return false;
    if(!result&&!has(data.nativeRefreshAltId,result))return false;
    error.clear();return true;
}
bool WeaponSelfEffectWorld::refreshUnit(int32_t owner,std::string& error){
    if(faulted_){error=fault_;return false;}
    for(auto& [handle,effect]:effects_){(void)handle;if(effect->state.status->unitId==owner&&!effect->state.status->disposed){WeaponEffectRuntimeData data;if(!runtime(*effect,data,error))return fail(error,error);}}
    error.clear();return true;
}
std::vector<WeaponSelfEffectSnapshot> WeaponSelfEffectWorld::snapshot()const{
    std::vector<WeaponSelfEffectSnapshot> out(retired_.begin(),retired_.end());out.reserve(out.size()+effects_.size());for(const auto& [handle,p]:effects_){(void)handle;out.push_back(snapshotOf(*p));}return out;
}
std::string WeaponSelfEffectWorld::dump()const{std::ostringstream out;out<<"WEAPON_SELF_EFFECT_WORLD live="<<effects_.size()<<" retired_history="<<retired_.size()<<" fault="<<faulted_;if(faulted_)out<<" reason="<<fault_;out<<'\n';for(const auto& e:snapshot())out<<"SELF_EFFECT handle="<<e.handle<<" data="<<e.dataHandle<<" owner="<<e.ownerId<<" name="<<e.name<<" source="<<e.sourceWeapon<<" type="<<e.type<<" remaining="<<e.remaining<<" stacks="<<e.numStacks<<" selfApplied="<<e.selfApplied<<" cleared="<<e.cleared<<" disposed="<<e.disposed<<'\n';return out.str();}
}

namespace hades {
bool WeaponSelfEffectWorld::retainsSourceWeapon(int32_t owner,const std::string& name)const{
    for(const auto& [handle,effect]:effects_){(void)handle;if(effect->sourceOwner==owner&&effect->sourceWeapon==name&&effect->state.status&&!effect->state.status->disposed)return true;}
    return false;
}
}
