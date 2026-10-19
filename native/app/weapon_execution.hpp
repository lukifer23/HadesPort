#pragma once
#include "weapon_runtime.hpp"
#include <memory>
#include <optional>

namespace hades {
// An explicitly requested native charge invocation, after native control/trigger
// selection. It is NOT a replacement for Weapon::Update input arbitration.
struct WeaponExecutionRequest {
    std::string weaponName;
    float dt=0,angle=0;
    bool hasCompleteAngle=false;
};
enum class WeaponExecutionProgress { Yielded,ChargeFinished,OwnerMissing,Blocked };
struct WeaponExecutionStep {
    WeaponExecutionProgress progress=WeaponExecutionProgress::Blocked;
    WeaponOutput operation;
    bool chargeReady=false;
};
// One synchronous external operation per advance. A yielded operation has passed
// pure preflight and must be consumed before the next advance. Script callbacks
// may mutate Arsenal data; no definition/state pointer survives across calls.
// Native local charge fractions survive those callbacks, matching Charge's stack.
// ChargeFinished reports the return of native Charge only, NEVER a fired shot.
class WeaponExecution {
public:
    WeaponExecution(WeaponArsenal&,WeaponExecutionRequest);
    bool advance(const WeaponOwnerFrame&,const WeaponOutputSupport&,
                 WeaponExecutionStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;
    WeaponExecutionRequest request_;
    unsigned phase_=0;int32_t chargeFxRole_=0;
    float oldFraction_=0,newFraction_=0,pendingRemaining_=0;
    bool startedPartial_=false,finished_=false,chargeTimerCommitted_=false;
};
enum class WeaponFirePrefixProgress { Yielded,CommonFireFinished,OwnerMissing };
struct WeaponFirePrefixStep {
    WeaponFirePrefixProgress progress=WeaponFirePrefixProgress::Yielded;
    WeaponOutput operation;
    bool expectsRandomReply=false,expectsProjectileReply=false,expectsBlinkReply=false;
};
// Replies consumed by the BlinkWeapon::Fire pre-step (RVA7d7d0) only.
struct WeaponBlinkReply {
    std::optional<double> randomDouble;
    std::optional<uint64_t> createdProjectile;
    // BlinkTeleport consumer result: false = native FireResult 0 return BEFORE
    // Weapon::Fire88f00 (VA18007dafd); true = continue into common Weapon::Fire.
    std::optional<bool> fireContinues;
};
// BlinkWeapon::FireProjectile RVA7e4f0 (.local/reports/native-combat2-blink-fire-projectile-static.json):
// one RNG/result or actual projectile construction per yielded operation.
class WeaponBlinkProjectile {
public:
    WeaponBlinkProjectile(WeaponArsenal&,std::string weaponName,float x,float y,float angle,WeaponFireRequest);
    // Finished when returned true with finished=true.
    bool advance(const WeaponOwnerFrame&,const WeaponBlinkReply&,const WeaponOutputSupport&,
        WeaponOutput& operation,bool& finished,bool& expectsRandom,bool& expectsProjectile,std::string& error);
private:
    WeaponArsenal* arsenal_;std::string name_;float x_=0,y_=0,angle_=0;WeaponFireRequest request_;
    unsigned phase_=0;int32_t count_=0,index_=0;
    enum class Await { None,Bonus,Offset,Projectile } await_=Await::None;
    float k_=0,sign_=0,shotAngle_=0,shotX_=0,shotY_=0;
};
// HandleTriggerDown's ready GUN branch through common Weapon::Fire. This
// prepares the genuine Gun::Fire continuation; completion is NOT a full shot,
// does not create a projectile and does not substitute for mandatory PostFire.
class WeaponFirePrefix {
public:
    WeaponFirePrefix(WeaponArsenal&,std::string weaponName,WeaponFireRequest);
    bool advance(const WeaponOwnerFrame&,const WeaponOutputSupport&,
                 WeaponFirePrefixStep&,std::string& error);
    bool advance(const WeaponOwnerFrame&,const WeaponBlinkReply&,const WeaponOutputSupport&,
                 WeaponFirePrefixStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;
    std::string name_;
    WeaponFireRequest request_;
    WeaponPoint origin_;
    unsigned phase_=0;
    bool finished_=false,lastFireCommitted_=false,blinkAwait_=false;
    std::unique_ptr<WeaponBlinkProjectile> blinkProjectile_;
};
struct WeaponGunReply {
    std::optional<double> randomDouble;
    std::optional<uint64_t> createdProjectile;
};
enum class WeaponGunProgress { Yielded,GunBodyFinished,OwnerMissing };
struct WeaponGunStep {
    WeaponGunProgress progress=WeaponGunProgress::Yielded;
    WeaponOutput operation;
    bool nativeVirtualFireResult=false;
};
// Continuation ONLY after common Weapon::Fire succeeded for this GUN. It
// consumes one RNG/result or actual projectile construction at a time, allowing
// the caller to run original callbacks before the next projectile/wave. Its
// completion still requires native HandleFireFx and PostFire, even if the
// virtual Fire result is false. It is not an Arsenal frame polling substitute.
class WeaponGunExecution {
public:
    WeaponGunExecution(WeaponArsenal&,std::string weaponName,WeaponFireRequest);
    bool advance(const WeaponOwnerFrame&,const WeaponGunReply&,const WeaponOutputSupport&,
                 WeaponGunStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;std::string name_;WeaponFireRequest request_;
    WeaponPoint origin_;unsigned phase_=0;bool finished_=false;
    enum class Await { None,SpreadRandom,BonusRandom,Projectile } await_=Await::None;
    float waveAngle_=0,capturedAngle_=0,spreadHalf_=0,bonusChance_=0;
    int32_t wave_=0,index_=0,projectileCount_=0,baseCount_=0;
    uint64_t createdProjectile_=0;int32_t pendingVolley_=0;bool volleyCommitted_=false;
};
enum class WeaponFireEffectsProgress { Yielded,BeforePostFire,OwnerMissing };
struct WeaponFireEffectsStep {
    WeaponFireEffectsProgress progress=WeaponFireEffectsProgress::Yielded;
    WeaponOutput operation;
};
// HandleTriggerDown's inline FireFx/FireFx2 continuation, AFTER the virtual
// GUN fire returned (including false return), BEFORE virtual PostFire.
class WeaponFireEffects {
public:
    WeaponFireEffects(WeaponArsenal&,std::string weaponName,WeaponFireRequest);
    // Creation replies are required, including explicit zero for a genuine
    // absent original Animation lookup. SetRadius receives a real nonzero ID.
    bool advance(const WeaponOwnerFrame&,std::optional<uint64_t> animationReply,
        const WeaponOutputSupport&,WeaponFireEffectsStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;std::string name_;WeaponFireRequest request_;
    WeaponPoint origin_;unsigned phase_=0;bool waiting_=false,finished_=false;
    uint64_t animation_=0;
};
struct WeaponArsenalFrame {
    WeaponOwnerFrame owner;
    // Fresh original UnitData strings at the corresponding native read.
    std::string continuousWeapon,priorityWeapon;
    bool editingMode=false;
};
enum class WeaponArsenalProgress { WeaponUpdateRequired,FrameFinished,OwnerMissing };
struct WeaponArsenalStep {
    WeaponArsenalProgress progress=WeaponArsenalProgress::WeaponUpdateRequired;
    std::string weaponName;float dt=0;bool priority=false;
};
// Exact outer Arsenal::Update order. Each WeaponUpdateRequired must run that
// weapon's COMPLETE native update cursor (trigger/release + timers + request
// finalization), then acknowledge on the next call. Merely running Gun/Charge
// or independent timers is not completion. New ordinary weapons added during
// the captured creation-vector iteration are not added to this frame's count.
class WeaponArsenalExecution {
public:
    WeaponArsenalExecution(WeaponArsenal&,float dt);
    bool advance(const WeaponArsenalFrame&,bool previousWeaponUpdateCompleted,
        WeaponArsenalStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;float dt_=0;unsigned phase_=0;size_t index_=0;
    bool waiting_=false,finished_=false;std::string priority_;
    std::vector<std::string> capturedOrder_;
};
enum class WeaponCancelChargeProgress { Yielded,Finished };
struct WeaponCancelChargeStep {
    WeaponCancelChargeProgress progress=WeaponCancelChargeProgress::Yielded;
    WeaponOutput operation;
};
// Weapon::cancelChargeOnUnit (the native CancelCharge body). The initial bounded
// charge path has no live audio handles; nonzero channel IDs remain errors
// until actual stop/fade consumption is reconstructed. Self-effect cancellation is
// a required real operation, NOT UnitStatus attack-disables cancellation.
class WeaponCancelCharge {
public:
    WeaponCancelCharge(WeaponArsenal&,std::string weaponName);
    bool advance(const WeaponOwnerFrame&,const WeaponOutputSupport&,
        WeaponCancelChargeStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;std::string name_;unsigned phase_=0;
    bool resetCommitted_=false,wasCharging_=false,clearRequest_=false,finished_=false;
    float fraction_=0,resetChargeTime_=0;
};
enum class WeaponUpdateChild { HandleTriggerDown,HandleTriggerRelease,Reload,ReloadFinished };
enum class WeaponUpdateProgress { Yielded,ChildRequired,FrameFinished,OwnerMissing };
struct WeaponUpdateStep {
    WeaponUpdateProgress progress=WeaponUpdateProgress::Yielded;
    WeaponOutput operation;
    WeaponUpdateChild child=WeaponUpdateChild::HandleTriggerDown;
    WeaponFireRequest request;
    float dt=0;
};
// Source Weapon::Update order, retaining real child operation boundaries. A
// child result means its FULL native routine finished; false is a valid native
// return only for trigger down/release. Never acknowledge after charge alone
// when that charge became ready and required firing/PostFire.
class WeaponUpdateExecution {
public:
    WeaponUpdateExecution(WeaponArsenal&,std::string weaponName,float dt);
    bool advance(const WeaponOwnerFrame&,std::optional<bool> childResult,
        const WeaponOutputSupport&,WeaponUpdateStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;std::string name_;float dt_=0;unsigned phase_=0;
    bool waiting_=false,finished_=false,fired_=false,failedCooldownAnimation_=false;
    WeaponUpdateChild child_=WeaponUpdateChild::HandleTriggerDown;
};
enum class WeaponPostFireProgress { Yielded,Finished };
struct WeaponPostFireStep {
    WeaponPostFireProgress progress=WeaponPostFireProgress::Yielded;
    WeaponOutput operation;
};
// Native base PostFire continuation after actual trigger FireFx. The bounded
// first-sword/cast paths include real cooldown/clip/reload/control state and
// original callbacks. Positive status durations and complex control-removal/
// projectile-cancel/RAM/danger branches stay explicit until real consumers.
class WeaponPostFire {
public:
    WeaponPostFire(WeaponArsenal&,std::string weaponName);
    bool advance(const WeaponOwnerFrame&,std::optional<double> randomReply,
        const WeaponOutputSupport&,WeaponPostFireStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;std::string name_;unsigned phase_=0,controlKey_=0;
    bool finished_=false,hasTarget_=false,clipEmpty_=false,cancelAttachedDone_=false,dangerQueried_=false;
    float targetX_=0,targetY_=0,randomMin_=0,randomMax_=0,pendingRandom_=0;
    enum class Await { None,Cooldown,Reload } await_=Await::None;
    std::unique_ptr<WeaponCancelCharge> cancel_;
};
}

namespace hades {
struct WeaponTriggerReply {
    std::optional<double> randomDouble;
    std::optional<uint64_t> createdProjectile,createdAnimation;
    std::optional<bool> blinkFireContinues; // BlinkTeleport consumer result
};
enum class WeaponTriggerProgress { Yielded,Finished,OwnerMissing };
struct WeaponTriggerStep {
    WeaponTriggerProgress progress=WeaponTriggerProgress::Yielded;
    WeaponOutput operation;
    bool nativeResult=false;
    bool expectsRandomReply=false,expectsProjectileReply=false,expectsAnimationReply=false,expectsBlinkReply=false;
};
// Complete bounded HandleTriggerDown: Charge -> actual virtual GUN fire ->
// inline FX -> PostFire. Every yielded real operation must finish before resume.
// RNG/creation replies are accepted only by the child that requested them.
// A false return after incomplete charge is the native result, not a fake shot.
class WeaponTriggerExecution {
public:
    WeaponTriggerExecution(WeaponArsenal&,std::string,float dt,WeaponFireRequest);
    bool advance(const WeaponOwnerFrame&,const WeaponTriggerReply&,
        const WeaponOutputSupport&,WeaponTriggerStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;std::string name_;float dt_=0;WeaponFireRequest request_;
    unsigned phase_=0;bool finished_=false,rootApplied_=false;
    std::unique_ptr<WeaponExecution> charge_;
    std::unique_ptr<WeaponFirePrefix> prefix_;
    std::unique_ptr<WeaponGunExecution> gun_;
    std::unique_ptr<WeaponFireEffects> fx_;
    std::unique_ptr<WeaponPostFire> post_;
};
// Non-FireOnRelease branch of HandleTriggerRelease. Includes actual swap,
// optional charge cancellation and original OnWeaponTriggerRelease callback.
// FireOnRelease virtual firing and positive status durations stay explicit.
class WeaponTriggerRelease {
public:
    WeaponTriggerRelease(WeaponArsenal&,std::string,float dt,WeaponFireRequest);
    bool advance(const WeaponOwnerFrame&,const WeaponOutputSupport&,
        WeaponTriggerStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;std::string name_;float dt_=0;WeaponFireRequest request_;
    unsigned phase_=0;bool finished_=false;
    std::unique_ptr<WeaponCancelCharge> cancel_;
};
}

namespace hades {
enum class WeaponReloadProgress { Yielded,Finished };
struct WeaponReloadStep { WeaponReloadProgress progress=WeaponReloadProgress::Yielded;WeaponOutput operation;bool expectsRandomReply=false; };
// Exact native reload/onReloadFinished. finishedOnly selects the timer-expiry
// child; ordinary reload samples only when ReloadTimeMin>0, then calls finish
// immediately when the chosen delay<=0. Real finish animation may be empty.
class WeaponReloadExecution {
public:
    WeaponReloadExecution(WeaponArsenal&,std::string,bool finishedOnly=false);
    bool advance(const WeaponOwnerFrame&,std::optional<double>,const WeaponOutputSupport&,
        WeaponReloadStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;std::string name_;unsigned phase_=0;
    bool finished_=false,awaitingRandom_=false;float minimum_=0,maximum_=0;
};
}

namespace hades {
enum class WeaponArsenalFrameProgress { Yielded,FrameFinished,OwnerMissing };
struct WeaponArsenalFrameStep {
    WeaponArsenalFrameProgress progress=WeaponArsenalFrameProgress::Yielded;
    WeaponOutput operation;
    bool expectsRandomReply=false,expectsProjectileReply=false,expectsAnimationReply=false,expectsBlinkReply=false;
};
// Complete native outer Arsenal frame composition. The parent never fabricates
// child-complete acknowledgments: this cursor owns WeaponUpdate/Trigger cursors.
// A fresh frame is supplied after each real yielded operation. All replies obey
// explicit expects*Reply flags. Native reload children run through their own resumable original routines.
class WeaponArsenalFrameExecution {
public:
    WeaponArsenalFrameExecution(WeaponArsenal&,float dt);
    bool advance(const WeaponArsenalFrame&,const WeaponTriggerReply&,
        const WeaponOutputSupport&,WeaponArsenalFrameStep&,std::string& error);
private:
    WeaponArsenal* arsenal_;float dt_=0;unsigned phase_=0;bool finished_=false;
    bool completedWeapon_=false;std::string current_;
    std::optional<bool> childResult_;
    std::unique_ptr<WeaponArsenalExecution> outer_;
    std::unique_ptr<WeaponUpdateExecution> update_;
    std::unique_ptr<WeaponTriggerExecution> down_;
    std::unique_ptr<WeaponTriggerRelease> release_;
    std::unique_ptr<WeaponReloadExecution> reload_;
};
}
