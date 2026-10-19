#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hades {
// The effect owner populates these fields from actual admitted Effect/EffectData
// state. A status registration is not an implementation of ApplyEffect.
struct UnitStatusEffect {
    uint64_t handle=0;
    int32_t unitId=-1;
    std::string name;
    bool cancelable=false,disableAttack=false,selfApplied=false;
    bool cleared=false,disposed=false;
    bool disableMove=false,disableRotate=false;
};
struct UnitDisableTimers {
    bool moveCancelable=false,rotateCancelable=false,attackCancelable=false;
    float move=0,attack=0,rotate=0; // Unit constructor constants, before Init Reset.
};
struct UnitStatusSnapshot {
    bool isTraitor=false; // Actual UnitStatus constructor+fresh Reset ally state.
    UnitDisableTimers timers;
    std::vector<uint64_t> pending,active,toDelete;
    std::vector<std::string> canceledNames;
    bool faulted=false;
    std::string fault;
};
using UnitStatusEffectCallback=std::function<bool(UnitStatusEffect&,std::string&)>;
using UnitStatusEffectTickInputs=std::function<bool(const UnitStatusEffect&,
    float& timeModifierFraction,float& currentOwnerTimeMultiplier,std::string&)>;
using UnitStatusEffectUpdate=std::function<bool(UnitStatusEffect&,float elapsed,
    bool& expired,std::string&)>;
using UnitStatusEffectPredicate=std::function<bool(const UnitStatusEffect&,bool&,std::string&)>;
class UnitStatus {
    std::vector<std::shared_ptr<UnitStatusEffect>> pending_,active_,toDelete_;
    std::vector<std::string> canceledNames_;
    bool inMutation_=false,updatingEffects_=false,faulted_=false;
    std::string fault_;
    bool fault(const std::string&,std::string&);
public:
    bool isTraitor=false; // Actual UnitStatus constructor+fresh Reset ally state.
    bool blockAll=false; // UnitStatus.Reset VA1802fd4fe.
    uint64_t blockEffectId=0; // Reset VA1802fd505, native interned EffectData ID.
    std::vector<std::string> immuneEffects; // Reset clears mImmuneEffects+68.
    float allegianceTime=0,blockEffectDuration=0;
    UnitDisableTimers timers;
    // Fresh map Unit::Init calls native Reset after assigning status.pOwner.
    // Reset of an existing effect-bearing unit needs full ClearEffects/allegiance
    // logic and is deliberately a separate contract.
    bool initializeFreshUnit(std::string&);
    bool clearEffects(const UnitStatusEffectCallback& clear,std::string&);
    // Reset2fd4c0: timer reset -> SetAsAlly -> ClearEffects -> CleanupEffects
    // -> block/immunity reset. An existing traitor needs actual allegiance work.
    bool reset(const std::function<bool(std::string&)>& setAsAlly,
        const UnitStatusEffectCallback& clear,const UnitStatusEffectCallback& dispose,std::string&);
    bool trackAdmittedEffect(std::shared_ptr<UnitStatusEffect>,std::string&);
    // Native direct mToDelete append: preserves repeated references. Caller
    // performs any required virtual Clear BEFORE this separate queue operation.
    bool enqueueEffectRemoval(uint64_t,std::string&);
    // Native exact-name lookup: deletion queue masks every matching active or
    // pending record; otherwise first active match, then first pending match.
    std::shared_ptr<UnitStatusEffect> getEffect(const std::string&)const;
    bool canAttack(bool editingMode,bool immuneToStun,bool allowCancellation)const;
    // Timer/effect portion of Unit::CanMove only. Caller separately checks
    // active-animation movement disable and actual effective movement speed.
    bool canMove()const;
    // Only the three native timer lines of Update. Parent supplies actual unit
    // elapsed time and UnitData.StunBreakModifier; no clamp at zero.
    bool advanceDisableTimers(float elapsed,float stunBreakModifier,std::string&);
    // Update's pending-to-active phase, before actual Effect::Update callbacks.
    bool promotePendingEffects(std::string&);
    // Native effect phase of UnitStatus.Update, after timer/block/allegiance
    // updates. Promotes current pending, captures active count, invokes actual
    // virtual updates with per-effect elapsed scaling, queues true returns,
    // clears updating guard, then invokes actual disposal cleanup. Effects
    // admitted synchronously by original callbacks wait pending until next tick.
    bool advanceEffects(float unitElapsed,const UnitStatusEffectTickInputs&,
        const UnitStatusEffectUpdate&,const UnitStatusEffectCallback& dispose,std::string&);
    // Clear callback is the real virtual Effect::Clear barrier; parent retains
    // arguments and performs its behavior. Cleared records remain query-visible
    // until cleanup; this method does not invent effect behavior.
    bool cancelDisables(const std::vector<std::string>& weaponEffectNames,
        bool canCancelOwnDisables,const UnitStatusEffectCallback& clear,std::string&);
    // Native CancelRotateDisables2fca40 scans active effects only. Attack
    // disables are excluded; pending effects and move/attack timers survive.
    bool cancelRotateDisables(const std::vector<std::string>& weaponEffectNames,
        bool canCancelOwnDisables,const UnitStatusEffectCallback& clear,std::string&);
    // Weapon::CancelSelfEffects: one source EffectData name at a time. Pending
    // reverse removal/disposal, active forward Clear+deferred deletion. This
    // deliberately does not apply cancelDisables' cancelability/name policies.
    bool cancelSelfEffects(const std::string& exactName,const UnitStatusEffectCallback& clear,
        const UnitStatusEffectCallback& dispose,std::string&);
    // UnitStatus::ClearEffect2fd9d0 (exact-name; EffectData name at Effect+0x20 -> +8):
    // pending (+0x20..+0x28) reverse: !cleared(+0x2a) -> vfunc+0x48 Clear;
    // !disposed(+0x28) -> unitId(+0x14)=-1, vfunc+0x38 Dispose(true), disposed=1;
    // erase. Active (+0x50..+0x58, captured count) forward: !cleared -> Clear;
    // append to mToDelete (+0x38). Same body as Weapon::CancelSelfEffects' use.
    bool clearEffect(const std::string& exactName,const UnitStatusEffectCallback& clear,
        const UnitStatusEffectCallback& dispose,std::string& error){return cancelSelfEffects(exactName,clear,dispose,error);}
    // Unit::RunScriptAction ClearEffect All (no ExcludeNames) VA1802f42a4..42e9:
    // over captured active entries only, !cleared(+0x2a) -> vfunc+0x48 Clear.
    // No deletion append here; the following Reset2fd4c0 performs ClearEffects.
    bool clearActiveEffects(const UnitStatusEffectCallback& clear,std::string&);
    // Active effect names in vector order (Unit+0x4b0 == UnitStatus+0x50).
    std::vector<std::string> activeEffectNames()const;
    // UnitStatus::ClearOnAttackEffects2fd880 scans ONLY captured active entries.
    // Predicate reads each instance's actual live EffectData.ClearOnAttack.
    bool clearOnAttackEffects(const UnitStatusEffectPredicate&,
        const UnitStatusEffectCallback& clear,std::string&);
    // Caller invokes this only outside Effect::Update iteration. Disposal calls
    // the native virtual Dispose(true) equivalent, after unitId becomes -1.
    bool cleanupEffects(const UnitStatusEffectCallback& dispose,std::string&);
    UnitStatusSnapshot snapshot()const;
    std::string dump()const;
};
}
