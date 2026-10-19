#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <memory>
#include <map>
#include <span>
#include <functional>
#include <variant>
#include <optional>
#include <vector>

namespace hades {
struct WeaponCatalog;
class WeaponExecution;
class WeaponFirePrefix;
class WeaponGunExecution;
class WeaponFireEffects;
class WeaponArsenalExecution;
class WeaponCancelCharge;
class WeaponUpdateExecution;
class WeaponPostFire;
class WeaponReloadExecution;
using WeaponScalar = std::variant<bool,double,std::string>;
enum class WeaponValueChange : uint8_t { Absolute=0,Multiply=1,MultiplyBase=2,Add=3,ReplaceIfNull=4 };
struct WeaponSoundCue { std::string name;bool valid=false,validityKnown=false; };
struct WeaponEffectDefinition {
    std::string name; int32_t type=12, trigger=1;
    std::string sourceJson; // normalized source/default record; no effect execution
    std::string effectiveFieldsJson;
    std::map<std::string,WeaponScalar> fields;
    std::map<std::string,std::string> fieldTypes;
    std::map<std::string,WeaponSoundCue> cues;
    uint32_t flashColorRgba=0xffffffff;
    // Native EffectData header IDs: omitted source fields retain ctor0;
    // explicitly read strings (including empty) use exact shipped XXH3.
    uint64_t nativeNameId=0,nativeRefreshId=0,nativeRefreshAltId=0;
    bool nativeIdsKnown=false;
};
struct ProjectileDefinition {
    std::string name; int32_t spawnType=0;
    std::array<std::string,3> spawned{}; // detonate, impact, death; native copy order
    std::string effectiveJson; // copied source/default DTO, not native memory/pointers
    std::string effectsJson; // source AddReplace inheritance, Trigger0 default; not weapon Fire1
    std::map<std::string,WeaponScalar> fields; // complex Thing defaults remain unresolved
    std::map<std::string,std::string> fieldTypes;
    std::vector<WeaponEffectDefinition> effects; // v14 normalized original victim effects; old packets keep explicit empty fields.
    std::map<std::string,WeaponSoundCue> cues; // v15 original compiled/read cue semantics; unknown validity retained.
    bool removeEffectsKnownEmpty=false; // v16 ctor/source proof; older packets remain unknown.
};
// A source-catalog DTO. Native complex fields not represented here remain in
// unresolvedFields; an instance is not a complete native WeaponData image.
struct WeaponDefinition {
    std::string name, projectile;
    int32_t type = 0, startingAmmo = -1, maxAmmo = -1, clipSize = 0;
    float chargeTime = 0, clipRegenInterval = 0, initialCooldown = 0;
    bool reloadOnCreation = false, reloadOnEquip = false, defaultControl = true;
    std::array<std::string, 4> controls{};
    std::array<std::string, 7> bundled{};
    std::string effectName;
    int32_t effectType = 12, effectTrigger = 1;
    bool effectContainerResolved = false;
    std::vector<std::string> unresolvedFields;
    float controlWindow=0,cooldown=0,cooldownMin=0,cooldownMax=0;
    float reloadTime=0,reloadTimeMin=0,reloadTimeMax=0;
    bool ignoreForceCooldown=false;
    std::string reloadFinishGraphic,swapOnReloadFinish,shareAmmoWeapon;
    std::string effectiveFieldsJson;
    std::map<std::string,WeaponScalar> fields; // current per-instance scalar values; unknowns absent
    std::map<std::string,std::string> fieldTypes;
    std::vector<WeaponEffectDefinition> effects;
    std::map<std::string,WeaponSoundCue> cues;
};
struct WeaponFireRequest {
    // Portable Thing ID plus actual entity generation replace native pointer
    // and EntityLinkedObject ID; the parent resolves both after callbacks.
    int32_t targetId=-1;
    uint64_t targetLifetime=0; // Actual parent entity-generation link, validated by the producer/resolver.
    float fromX=0,fromY=0,targetX=0,targetY=0,angle=0;
    bool applySelfEffects=true;
};
struct WeaponSimulationState {
    int32_t ammo=-1,clipShotsRemaining=0;
    float critBonusChance=0,additiveDamageMultiplier=0,blastRadiusModifier=1;
    int32_t critBonusRemaining=0,additiveDamageBonusRemaining=0;
    uint32_t chargeSoundId=0,chargeFullSoundId=0; // native ctor qword+170=0
    uint32_t impactCueId=0,fireSoundId=0;float impactCueStopTime=0;
    bool reDisableAfterFire=false,lockedPerfectCharge=false,wasLastFirePerfectCharge=false;
    uint64_t delayedProjectileHandle=0; // parent lifetime link; native pDelayedProjectile
    uint64_t fireFxLifetime=0; // Native pFireFx; zero until an actual writer supplies it.
    std::array<std::vector<float>,4> volleyHitTimes;
    std::array<int32_t,4> volleyFirstHitId{};
    std::array<bool,4> volleySlotInitialized{};
    float chargeTimeRemaining=0,cooldownTimeRemaining=0,reloadTimeRemaining=0,nextClipRegenTime=0,lastFireTime=0,scatter=0;
    // Constructor qword VA1800887e8 writes +178=1, +17c=0.
    float perfectChargeWindowRemaining=0;
    float controlTime=0;
    bool requireControlRelease=false;
    std::optional<WeaponFireRequest> currentFireRequest,previousFireRequest;
    std::string gainedControlFrom;
    bool charging=false,validProjectileData=false,validSpawnedProjectileData=false,controllable=false,disposed=false;
    bool bundled=false,reloadPaused=false;int32_t volley=0;
    std::array<int32_t,4> controlIndices{{-1,-1,-1,-1}};
    // BlinkWeapon members +0x198..+0x1c4, all zeroed by BlinkWeapon::defaults7d170
    // (.local/reports/native-combat4-blink-defaults-static.json).
    struct Blink {
        bool endpointDetonated=false;      // +0x198
        float lastAngle=0,lastRadius=0;    // +0x19c/+0x1a0 (OnBlinkFinished; read by PostFire)
        float angle=0,remaining=0,interval=0; // +0x1a4/+0x1a8/+0x1ac
        float startX=0,startY=0,targetX=0,targetY=0; // +0x1b0..+0x1bc
    } blink;
};
struct WeaponSnapshot : WeaponSimulationState {
    int32_t ownerId=0;std::string ownerName;WeaponDefinition definition;
    ProjectileDefinition projectileData,spawnedProjectileData;
    // Immutable initial templates keep repeated shot plans free of provenance
    // copies. A future per-instance projectile property setter must replace the
    // affected template (copy on write), rather than mutate catalog storage.
    std::shared_ptr<const ProjectileDefinition> projectileSource,spawnedProjectileSource;
};
enum class WeaponOutputKind { SetOwnerAnimation, RemoveWeaponControl, ScriptCallback, ApplySelfEffects, ProjectileSpawn, CheckInvulnerableBarrelImpact, ApplyOwnerPhysics, SetOwnerAngle, SetOwnerCompleteAngle, CancelOwnerMovement, CancelOwnerDisables, CancelOwnerRotateDisables, CreateAttachedAnimation, ConsumeRandomChance, CreateWeaponWorldAnimation, RequestRandomDouble, FinalizeGunProjectile, SetWeaponWorldAnimationRadius, CancelWeaponSelfEffects, RootOwnerMovement, ExtendOwnerDisables, BlinkTeleport, BlinkUpdate, CancelAttachedProjectiles, QueryNearbyDanger, ShowAimLine, PlayFireSound };
struct WeaponOutput {
    WeaponOutputKind kind=WeaponOutputKind::ScriptCallback;
    int32_t ownerId=0;std::string weaponName,name;
    float x=0,y=0,z=0,angle=0,value=0;
    std::shared_ptr<const ProjectileDefinition> projectile;
    int32_t waveIndex=0,projectileIndex=0,volleyIndex=0;
    uint64_t projectileLifetimeHandle=0,animationLifetimeHandle=0;
    bool addCritBonus=false,setAdditiveDamageMultiplier=false;
    float critBonus=0,additiveDamageMultiplier=0,blastRadiusMultiplier=1;
    bool hasTargetLocation=false;
    float disableAttackFor=0,disableOwnerFor=0; // ExtendOwnerDisables (PostFire)
    std::optional<WeaponFireRequest> fireRequest; // actual captured GUN request, separate from scatter angle
    int32_t chargeFxRole=0; // Weapon::Charge: 1 ChargeStartFx(+0x548) PlaySpeedMultiplier, 2 ChargeFx(+0x530) SetDuration
    bool controllable=false,freshInput=false; // OnWeaponFailedToFire28d0d0 caller booleans
    bool clearUseStartMarker=false; // ProjectileSpawn from BlinkWeapon::FireProjectile: Projectile+0x3b2 = false after Create
    bool delayedProjectile=false; // FinalizeGunProjectile: ProjectileData StartDelay>0 (GunWeapon::Fire VA1800807xx)
};
struct WeaponVolleyPlan {std::vector<WeaponOutput> outputs;size_t randomValuesUsed=0;int32_t nextVolley=0;};
struct WeaponOwnerFrame {
    int32_t ownerId=0;
    // enabled is the historical API spelling for live Unit.mAllowAnyFire;
    // it is independent of the WeaponData.Enabled/UnitData.Enabled fields.
    bool exists=false,alive=false,enabled=false,canAttack=false,canAttackAllowCancellation=false,canMove=false;
    bool inputAllowed=false,animationBlocksAttack=false;
    float x=0,y=0,z=0,totalZ=0,angle=0,worldTime=0;
};
struct WeaponPoint {float x=0,y=0;};
struct WeaponTrigger { int32_t nativeControlIndex=-1;bool down=false;float angle=0; };
// Pure preflight: parent must not mutate World during this callback. Once step
// succeeds it consumes the ordered outputs. Unsupported outputs fail before
// Arsenal state changes, so firing cannot silently omit effects/callbacks.
using WeaponOutputSupport=std::function<bool(const WeaponOutput&,std::string&)>;
struct WeaponRetirementProviders {
    // Queries real allocated projectile/effect ownership; not a timer or an
    // invented Windows reference count. True retains the actual copied data.
    std::function<bool(const std::string&,bool&,std::string&)> retained;
    // Native Arsenal destructor stops mImpactCue (FMOD mode0). It drops the
    // pFireFx reference; that operation is not forced animation destruction.
    std::function<bool(uint32_t,std::string&)> stopImpactCue;
    std::function<bool(uint64_t,std::string&)> dropFireFx;
};
struct WeaponRetirementSnapshot {std::string name;int32_t ownerId=-1;bool disposed=true;};
// This is a staged typed-unit COMPONENT owner. Constructing it does not create
// a Thing, Unit/Entity handle, active World entry, Lua query proxy, or OnSpawn.
// Normal Lua EquipWeapon must retain native live-unit selection requirements.
class WeaponArsenal {
public:
    WeaponArsenal(int32_t ownerId, std::string ownerName);
    // ownerId is actual generated Unit.mId, never a source-map metadata ID.
    WeaponArsenal(int32_t ownerId, std::string ownerName, std::shared_ptr<const WeaponCatalog>);
    bool equip(const std::string& name, std::string& error);
    bool equip(const WeaponDefinition&, std::string& error);
    bool setProperty(const std::string& weapon,const std::string& property,const WeaponScalar&,std::string& error);
    // Source ScriptAction conversion boundary. Missing optional is empty any;
    // dataValue=true also normalizes literal string "null" to empty any.
    // Selector/live-Unit validation remains the parent's ScriptAction duty.
    bool setScriptProperty(const std::string& weapon,const std::string& property,
                           std::optional<WeaponScalar>,WeaponValueChange,bool dataValue,std::string& error);
    // Current native per-owner copied projectile data. Reacquire after every
    // callback/equip/mutation; vector storage and COW snapshots can change.
    const WeaponSnapshot* weaponState(const std::string& weapon,std::string& error) const;
    // BlinkWeapon member state, mutated only by the parent's native Blink consumers.
    WeaponSnapshot::Blink* blinkState(const std::string& weapon,std::string& error);
    // ScriptAction::SwapWeapon25bfe0 post-swap stores (request clear, pGainedControlFrom, ControlTime).
    WeaponSnapshot* scriptMutableWeapon(const std::string& weapon);
    // Life.TakeHit's actual GUN volley bookkeeping. Native GetVolleyHitTimes
    // materializes copies; only the first-victim ID store mutates the weapon.
    bool recordProjectileHit(const std::string&,int32_t volley,int32_t fireIndex,
        int32_t victim,bool victimIsUnit,bool invulnerable,float worldTime,
        bool& firstInVolley,std::string& error);
    const ProjectileDefinition* projectileData(const std::string& weapon,bool spawned,std::string& error) const;
    // Explicit transactional editing seam for recovered parent setters. Mutator
    // must not touch World/Lua. Updates future-shot COW template atomically and
    // preserves already-issued projectile snapshots. Identity changes require
    // separate native Copy/GetProjectileData handling, not this callback.
    bool editProjectileData(const std::string& weapon,bool spawned,
        const std::function<bool(ProjectileDefinition&,std::string&)>&,std::string& error);
    // Unfiltered Weapon::SetProjectileProperty scalar branch. Applies primary
    // and native-valid spawned copy when spawn names exist, transactionally.
    // Parent owns live Unit/weapon selection. Type/vector/spawn-name and Thing
    // fallback fields remain explicit errors, rather than partial fake support.
    bool setScriptProjectileProperty(const std::string& weapon,const std::string& property,
        std::optional<WeaponScalar>,WeaponValueChange,std::string& error);
    bool dataProperty(const std::string& weapon,const std::string& property,WeaponScalar&,std::string& error)const;
    bool stateProperty(const std::string& weapon,const std::string& property,WeaponScalar&,std::string& error)const;
    // Return false with empty error for a recovered native request gate.
    bool requestFire(const std::string&,WeaponFireRequest,const WeaponOwnerFrame&,std::string& error);
    // Unit::OnTouchdown's exact RequestFire83160 overload: angle-1, two
    // zero Vector2 arguments, null target Thing, applySelfEffects=true.
    // Caller already ran real Thing.OnTouchdown and Unit/status/editing gates.
    bool requestTouchdownFire(const std::string&,const WeaponOwnerFrame&,std::string& error);
    // Input/control producer boundary: clears current request only; previous
    // remains for native HandleTriggerRelease during Weapon::Update.
    bool clearFireRequest(const std::string&,std::string& error);
    bool recordFireSound(const std::string&,uint32_t,std::string&);
    // UnitArsenal.RemoveWeaponControl2ff510 removes every membership and clears
    // the current request before its optional, separately executed CancelCharge.
    bool removeWeaponControl(const std::string&,bool& cancelCharge,std::string& error);
    bool swapWeaponControl(const std::string& from,const std::string& to,const WeaponOwnerFrame&,std::string& error);
    // WeaponArsenal::AddWeaponControl2ff480: append if absent.
    bool addWeaponControl(const std::string&,std::string& error);
    // PlayerUnitInput::CheckWeaponControl2ef100 selection: controllable
    // weapons to remove (in order, duplicates kept) and the release-flag
    // clears. Parent performs each RemoveWeaponControl2ff510 (and its charge
    // cancel) and then AddWeaponControl(pGainedControlFrom) via
    // finishLostControl. down(name) is the raw control-held bit (&2).
    bool checkWeaponControl(const std::function<bool(const std::string&)>& down,std::vector<std::string>& lose,std::string& error);
    bool finishLostControl(const std::string&,std::string& error);
    void clearReleasedControls(const std::function<bool(const std::string&)>& down);
    // Weapon+0x78 pDelayedProjectile: set by GunWeapon::Fire for a Unit with
    // pPlayer when StartDelay>0; Weapon::Update183e80 clears it once the
    // projectile has started, detonated, or its Thing is DEAD.
    bool setDelayedProjectile(const std::string&,uint64_t,std::string& error);
    bool clearDelayedProjectiles(const std::function<bool(uint64_t,bool&,std::string&)>& release,std::string& error);
    std::vector<std::string> controllableWeapons()const;
    bool retireOwner(const WeaponRetirementProviders&,bool& released,std::string& error);
    bool retirementRequested()const{return retirementRequested_;}
    const std::vector<WeaponRetirementSnapshot>& retiredWeapons()const{return retiredWeapons_;}
    bool finishFireRequestFrame(const std::string&,bool fired,bool failedCooldownAnimation,std::string& error);
    bool isStrafing(const std::string& ignoredWeapon,std::string& error)const;
    bool hasWeapon(const std::string& name) const;
    bool hasAmmo(const std::string& name, std::string& error) const;
    bool getAmmo(const std::string& name,int32_t& result,std::string& error) const;
    bool addAmmo(const std::string& name,int32_t amount,std::string& error);
    bool isPerfectCharge(const std::string& name,std::string& error) const;
    bool recordProjectilePerfectCharge(const std::string& name,bool,std::string& error);
    bool canFire(const std::string& name,const WeaponOwnerFrame&,std::string& error) const;
    // Default owner-relative FireRequest origin. Explicit FromLocation and
    // obstacle-source branches remain the caller's distinct native paths.
    bool fireOrigin(const std::string& name,const WeaponOwnerFrame&,float angle,WeaponPoint&,std::string& error) const;
    // Weapon::GetFireOffset86010: owner-relative barrel offset for an angle.
    bool fireOffset(const std::string& name,float angle,WeaponPoint& output,std::string& error)const;
    // Pure Gun::Fire layout after common Fire preconditions/side effects. It
    // neither fires nor creates actors. Random tape/cursor remains caller-owned
    // and is consumed only when caller commits accepted native operations.
    bool planGunProjectiles(const std::string& name,const WeaponOwnerFrame&,float angle,
                            std::span<const double> randomTape,WeaponVolleyPlan&,std::string& error) const;
    // Finite depletion is transactional and requires callback support when the
    // native OnOutOfAmmo branch is reached; -1 remains nondepleting.
    bool spendAmmo(const std::string& name, std::string& error);
    bool spendAmmo(const std::string& name,std::vector<WeaponOutput>&,const WeaponOutputSupport&,std::string& error);
    bool step(float dt,const WeaponOwnerFrame&,std::span<const WeaponTrigger>,
              std::vector<WeaponOutput>& outputs,const WeaponOutputSupport&,std::string& error);
    bool empty()const{return weapons_.empty();}
    std::vector<WeaponSnapshot> snapshot() const;
    std::string dump() const;
private:
    friend class WeaponExecution;
    friend class WeaponFirePrefix;
    friend class WeaponGunExecution;
    friend class WeaponFireEffects;
    friend class WeaponArsenalExecution;
    friend class WeaponCancelCharge;
    friend class WeaponUpdateExecution;
    friend class WeaponPostFire;
    friend class WeaponReloadExecution;
    int32_t ownerId_;
    std::string ownerName_;
    std::vector<WeaponSnapshot> weapons_;
    std::vector<std::string> controllableOrder_;
    std::shared_ptr<const WeaponCatalog> catalog_;
    bool retirementRequested_=false;
    std::vector<WeaponRetirementSnapshot> retiredWeapons_; // bounded scalar diagnostics
};
} // namespace hades
