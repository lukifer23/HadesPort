#pragma once
#include "actor_runtime.hpp"
#include "weapon_catalog.hpp"
#include <map>
#include <memory>
#include <deque>
#include <tuple>

namespace hades {
// Portable effect lifetime identity is separate from Thing/Unit public IDs.
// Actual status membership remains in the owner's real UnitStatus component.
struct WeaponSelfEffectInstance {
    uint64_t handle=0,dataHandle=0;
    int32_t sourceOwner=-1;
    std::string sourceWeapon;
    size_t sourceIndex=0;
    bool sourceProjectile=false,sourceSpawned=false;
    float sourceAngle=0;
    int32_t factoryType=12;
    int32_t attacksMade=0; // Effect.defaults VA18005ca1d stores Effect+10=0.
    SpeedEffectState state; // base portion also owns Tag state; no fake Speed admission
    EffectUpdateFxState fx;
};
struct WeaponSelfEffectProviders {
    ActorRandom randomStream0;
    // Called at actual source barriers. Reapplied distinguishes original
    // OnEffectApply arguments; other operations use the same typed executor.
    std::function<bool(EffectAffectOperation,WeaponSelfEffectInstance&,
        EffectAffectOwner&,const WeaponEffectDefinition&,bool reapplied,std::string&)> affect;
    std::function<bool(WeaponSelfEffectInstance&,const WeaponEffectDefinition&,std::string&)> onCleared;
    std::function<bool(WeaponSelfEffectInstance&,const WeaponEffectDefinition&,std::string&)> updateFx;
    // Base Clear/Reapply for branches beyond this world's proven first-Sword
    // subset. A missing executor is an error, never an omitted side effect.
    std::function<bool(WeaponSelfEffectInstance&,const WeaponEffectDefinition&,std::string&)> complexBaseClear;
    std::function<bool(WeaponSelfEffectInstance&,const WeaponEffectDefinition&,std::string&)> complexBaseReapply;
    std::function<bool(const WeaponSelfEffectInstance&,const WeaponEffectDefinition&,int32_t&,std::string&)> getMaxStacks;
    // PhysicsComponent::Halt2bcd20 for Effect::Clear5d560 HaltOnEnd (+0x312)
    // on an existing owner with pPhysics.
    std::function<bool(int32_t owner,std::string&)> haltPhysics;
    std::function<bool(const WeaponSelfEffectInstance&,const WeaponEffectDefinition&,bool&,std::string&)> hasRefreshOnExpireEffect;
    // InvulnerableEffect (Type 11) AffectUnit5e580 tail (after base AffectUnit) / Clear5e7c0
    // head (before base Effect::Clear5d560) for the actual owner Unit.
    std::function<bool(WeaponSelfEffectInstance&,int32_t owner,const WeaponEffectDefinition&,bool clear,std::string&)> invulnerable;
};
struct WeaponSelfEffectSnapshot {
    uint64_t handle=0,dataHandle=0;
    int32_t ownerId=-1,sourceOwner=-1,type=12,numStacks=0;
    std::string name,sourceWeapon;
    float remaining=0,modifier=0,appliedModifier=0,tagCooldown=0;
    bool selfApplied=false,cleared=false,disposed=false,tagActive=false;
};
class WeaponSelfEffectWorld {
    ActorWorld& actors_;
    std::map<uint64_t,std::unique_ptr<WeaponSelfEffectInstance>> effects_;
    std::deque<WeaponSelfEffectSnapshot> retired_; // bounded diagnostic history, not live ownership
    std::map<std::tuple<int32_t,std::string,size_t>,uint64_t> dataIds_;
    std::map<std::tuple<int32_t,std::string,size_t,bool>,uint64_t> projectileDataIds_;
    std::map<int32_t,SpeedEffectOwner> ownerViews_;
    uint64_t nextEffect_=1,nextData_=1;
    bool faulted_=false;
    std::string fault_;
    bool fail(const std::string&,std::string&);
    ActorComponents* unit(int32_t,std::string&);
    SpeedEffectOwner* ownerView(int32_t,std::string&);
    bool commitOwner(int32_t,std::string&);
    const WeaponEffectDefinition* definition(const WeaponSelfEffectInstance&,std::string&)const;
    bool runtime(WeaponSelfEffectInstance&,WeaponEffectRuntimeData&,std::string&);
    bool affect(WeaponSelfEffectInstance&,int32_t,const WeaponSelfEffectProviders&,std::string&);
    bool clear(WeaponSelfEffectInstance&,const WeaponSelfEffectProviders&,std::string&);
    bool reapply(WeaponSelfEffectInstance&,const WeaponEffectDefinition&,const WeaponSelfEffectProviders&,std::string&);
    bool dispose(UnitStatusEffect&,std::string&);
    void collectDisposed();
public:
    explicit WeaponSelfEffectWorld(ActorWorld& world):actors_(world){}
    // Owner must be a real registered typed Unit with its actual copied Arsenal.
    // No diagnostic query proxy or general AddEffect admission is fabricated.
    bool applyWeaponEffects(int32_t owner,const std::string& weapon,int32_t trigger,
        const WeaponSelfEffectProviders&,SelfEffectAdmissionResult&,std::string&);
    bool cancelWeaponEffects(int32_t owner,const std::string& weapon,int32_t trigger,
        const WeaponSelfEffectProviders&,std::string&);
    bool clearOnAttack(int32_t owner,const WeaponSelfEffectProviders&,std::string&);
    bool countOwnerAttack(int32_t owner,const WeaponSelfEffectProviders&,std::string&);
    bool clearOnCritted(int32_t victim,const WeaponSelfEffectProviders&,std::string&);
    // Caller has already executed native Projectile.ApplyEffects and
    // UnitStatus.AddEffect admission gates/RNG against the actual victim.
    // This owns genuine Speed/Tag factory/Affect/Reapply/pending publication.
    bool admitProjectileEffect(int32_t victim,int32_t sourceOwner,
        const std::string& weapon,bool spawned,size_t index,float impactAngle,
        const WeaponSelfEffectProviders&,std::string&);
    // Native status effect phase only. Parent advances status disable timers and
    // all earlier Unit phases separately, exactly once in its World schedule.
    bool updateUnit(int32_t owner,float unitElapsed,const WeaponSelfEffectProviders&,std::string&);
    bool clearEffect(uint64_t,const WeaponSelfEffectProviders&,std::string&);
    bool resetUnit(int32_t,const WeaponSelfEffectProviders&,
        const std::function<bool(std::string&)>& setAsAlly,std::string&);
    // Unit::RunScriptAction case ClearEffect (Action 144, VA1802f410a..4320).
    // exclude==nullptr: All without ExcludeNames (active Clear, then Reset2fd4c0).
    bool clearAllEffects(int32_t,const std::vector<std::string>* exclude,const WeaponSelfEffectProviders&,
        const std::function<bool(std::string&)>& setAsAlly,std::string&);
    bool clearNamedEffect(int32_t,const std::string&,const WeaponSelfEffectProviders&,std::string&);
    bool retainsSourceWeapon(int32_t sourceOwner,const std::string&)const;
    // Reflection setters alter live copied EffectData. Refresh projected status
    // query flags after such mutations and before parent CanAttack/CanMove.
    bool refreshUnit(int32_t,std::string&);
    WeaponSelfEffectInstance* instance(uint64_t);
    const WeaponEffectDefinition* effectData(uint64_t,std::string&)const;
    bool hasRefreshOnExpire(const WeaponSelfEffectInstance&,const WeaponEffectDefinition&,bool&,std::string&)const;
    std::vector<WeaponSelfEffectSnapshot> snapshot()const;
    std::string dump()const;
};
}
