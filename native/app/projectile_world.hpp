#pragma once
#include "actor_runtime.hpp"
#include "projectile_runtime.hpp"
#include "hit_arguments.hpp"
#include <optional>
#include <functional>
#include <deque>

namespace hades {
struct ProjectileActor {
    ProjectileInitializationRequest request;
    ProjectileCreationDescriptor descriptor;
    ProjectileInitialState state;
    ObjectSnapshot body;
    const SourceValue* thing=nullptr;
    std::array<uint8_t,4> baseColor{255,255,255,255};
    std::array<float,4> hsv{};
    float ambient=1,timeModifierFraction=1,timeMultiplier=1;
    std::array<BodyTransition,2> fractions;
    std::unique_ptr<PhysicsState> physics;
    std::vector<ActorPoint> geometry;
    uint64_t primaryAnimation=0,attachedAnimation=0;
    std::vector<uint64_t> attachedAnimations;
    // Actual portable World retains ActorComponents/Arsenal until World teardown.
    // This binding is independent of the owner being registered/alive.
    ActorWorld* firingWorld=nullptr;WeaponArsenal* firingArsenal=nullptr;
    bool spawnedDefinition=false,shotInitialized=false;
    // Parent sets only for a genuine spawned-projectile ownership edge. Plain
    // Unit-fired initialization has no source projectile and leaves this zero.
    uint64_t sourceProjectileLifetime=0;
    float spawnX=0,spawnY=0,spawnTime=0,arcAngleOffset=0;
    bool bodyPending=false,physicsPending=false,thingInitialized=false;
    bool pending=false,active=false,disposed=false;
    uint8_t lifeStatus=0; // Thing::IsAlive reads ALIVE0, independent of mExpired.
    // Native vector stores Entity IDs (Thing base+44), never public map IDs.
    // Parent portable lifetime handles preserve that identity domain.
    std::vector<uint64_t> thingsHit;
    std::vector<uint64_t> hitByImpact;
    float currentDistanceSquared=0;
    std::map<int32_t,int32_t> consecutiveHits; // native keyed by public victim ID.
    // Projectile+0x460 mHitByDetonation: eastl hash_set<int> keyed by victim
    // Thing.mId (+0x6c), cleared by every accepted Detonate6d670.
    std::vector<int32_t> hitByDetonation;
    // Thing+0x140/0x148 mAttachedTo (portable registered ActorWorld id plus
    // its lifetime, EntityManager::IsAlive analogue), +0x200 mAttachOffset,
    // +0x70 mAttachOffsetZ. Set only by the native Thing::AttachTo2e59f0 path.
    int32_t attachedTo=0;uint64_t attachedToLifetime=0;
    float attachOffsetX=0,attachOffsetY=0,attachOffsetZ=0;
    // Projectile+0x508 mPrevLocation, written by Projectile::SetLocation6ee90.
    float previousX=0,previousY=0;
};
struct ProjectileTeamProviders {
    // Portable identities of actual allocated Player objects, not team/group
    // names. Native HasSameTeam compares pPlayer after FriendlyToPlayer remap.
    uint64_t firstPlayerIdentity=0;
    std::function<bool(int32_t,uint64_t&,std::string&)> unitPlayerIdentity;
};
// Detonate6d670 / UpdateDetonation71c10 / IsInBlast760e0. External graphic,
// sound, shake, spawn and victim-impact branches are evaluated exactly and
// fail when actually reached; no absent consumer is acknowledged.
struct ProjectileDetonationProviders {
    float worldTime=0;
    ProjectileTeamProviders teams;
    // Projectile::ApplyImpact76590 on a detonation victim (parent supplies hit consumers).
    std::function<bool(uint64_t projectile,int32_t victim,std::string&)> applyImpact;
};
// Projectile::OnDeath6f5c0 native arguments for ScriptManager::OnProjectileDeath
// 289ed0 (proj, victim, LastProjectileDeath, FirstUnitInVolley, InvulnerableImpact).
struct ProjectileDeathArguments {
    std::string name,weaponName;
    int32_t attackerId=-1,victimId=-1,firstUnitInVolley=-1,fireIndex=0,volley=0,lastHitId=-1;
    bool hasLocation=false,killedVictim=false,lastProjectileDeath=false,invulnerableImpact=false,isDeflected=false,lastHitIsUnit=false;
    float locationX=0,locationY=0,angleDegrees=0;
};
bool projectileUnitsShareTeam(const ActorWorld&,int32_t first,int32_t second,
    const ProjectileTeamProviders&,bool&,std::string&);
// GunWeapon::Fire VA18007fedf..18007ffb2 barrel test through
// GridManager::DetectCollisions309dd0 / cell helper30ae10 obstacle branch:
// obstacles only, StopsProjectiles list, first hit, check type4 PointInPolygon.
// hit=0 when no obstacle contains the point. Contract:
// native/analysis/player-attack-dash-contract.json.
bool barrelObstacleAt(ActorWorld&,float x,float y,int32_t& hit,std::string&);
bool projectileAffectsUnit(const ProjectileActor&,int32_t victim,float actualWorldTime,
    const ProjectileTeamProviders&,bool&,std::string&);
enum class ProjectileDefenseActionKind {Reflect,OnProjectileBlock};
struct ProjectileDefenseAction {
    ProjectileDefenseActionKind kind=ProjectileDefenseActionKind::Reflect;
    uint64_t defender=0,candidate=0;int32_t ownerId=-1;
    float directionX=0,directionY=0;
    std::string weaponName;
};
struct ProjectileDefenseProviders {
    ProjectileTeamProviders teams;
    std::function<bool(const ProjectileDefenseAction&,std::string&)> preflight;
    std::function<bool(const ProjectileDefenseAction&,std::string&)> execute;
    // Required only when actual defender UseDetonationForProjectileDefense.
    std::function<bool(uint64_t defender,uint64_t candidate,bool&,std::string&)> inBlast;
};
enum class ProjectileCollisionOperationKind {
    OnMovementReaction,TryProjectileDefenseEffect,CheckDodge,OnDodge,
    HasLineOfSight,Reflect,Impact
};
struct ProjectileCollisionOperation {
    ProjectileCollisionOperationKind kind=ProjectileCollisionOperationKind::Impact;
    uint64_t projectile=0,effect=0;int32_t victimId=-1;
    float fromX=0,fromY=0,toX=0,toY=0,z=0,backoff=0;
    std::optional<ActorPoint> minimumTranslation; // mandatory actual Reflect input; no guessed zero.
    bool unit=false;
};
struct ProjectileCollisionReply {std::optional<bool> predicate;};
struct ProjectileImpactProviders {
    float worldTime=0;
    ProjectileTeamProviders teams;
    ActorRandom random;
    // Actual admitted effect definition, required when active UnitStatus
    // entries participate in GetReflection/ProjectileDefenseEffect queries.
    std::function<bool(uint64_t,const WeaponEffectDefinition*&,std::string&)> effectData;
    std::function<bool(uint64_t,int32_t&,std::string&)> effectStacks;
    std::function<bool(int32_t,bool&,std::string&)> primaryInvulnerable;
    std::function<bool(const ProjectileCollisionOperation&,std::string&)> preflight;
    std::function<bool(const ProjectileCollisionOperation&,ProjectileCollisionReply&,std::string&)> execute;
    // Projectile::ApplyImpact76590 for a detonation victim (UpdateDetonation71c10).
    std::function<bool(uint64_t,int32_t,std::string&)> detonationImpact;
};
struct ProjectileCalculatedDamage {float amount=0;bool critical=false;};
struct ProjectileImpactGeometry {float x=0,y=0,z=0,angle=0;bool nativeImpactAdmitted=false;};
enum class ProjectileHitOperationKind {
    TakeHitPresentation,LookupTagMultipliers,MarkDirty,SetDamagedAnimation,
    OnHit,CountAttackerEffects,OnHitWeapon,ApplyVictimEffects,SpawnOnImpact,
    ImpactFx,ClearCritEffects,InflictedDamageFx,InflictedDamageSound,
    ApplyImpactForces,Charm
};
struct ProjectileHitOperation {
    ProjectileHitOperationKind kind=ProjectileHitOperationKind::OnHit;
    uint64_t projectile=0;int32_t victim=-1,attacker=-1;
    ProjectileImpactGeometry geometry;
    NativeHitArguments hit{};
};
struct ProjectileHitReply {std::optional<float> tagMultiplierAddition;};
struct ProjectileHitProviders {
    ProjectileImpactProviders collision;
    std::optional<float> damageMultiplier,damageTakenMultiplier;
    std::optional<bool> disableDamage;
    std::function<bool(int32_t,bool& invulnerable,bool& noArmorCoverage,
        bool& noInvulnerableCoverage,std::string&)> primaryFlags;
    // PhysicsComponent::IsImmuneToForce2bd4d0 on the victim's actual Physics.
    std::function<bool(int32_t,bool& immune,std::string&)> immuneToForce;
    std::function<bool(const ProjectileHitOperation&,ProjectileHitReply&,std::string&)> execute;
};
struct ProjectileThingProviders {
    ActorRandom random;
    // Deprecated cross-check only: native InitThing tests AmbientSound.valid
    // (ThingData+71), not ScriptManager registrations. Actual source cue
    // validity is read from the Thing catalog; unknown validity fails.
    std::optional<bool> playerNearbyRequired;
    std::function<bool(ProjectileActor&,std::string&)> createInteract;
    std::function<bool(ProjectileActor&,std::string&)> createMetering;
    std::function<bool(ProjectileActor&,std::string&)> createSpeech;
    std::function<bool(ProjectileActor&,const std::string&,std::string&)> createLight;
    std::function<bool(ProjectileActor&,std::string&)> createPlayerNearby;
    std::function<bool(ProjectileActor&,std::string&)> requestOutline;
    std::function<bool(ProjectileActor&,const std::string&,uint64_t&,std::string&)> createAttachedAnimation;
    std::function<bool(ProjectileActor&,const std::string&,bool primary,std::string&)> setAnimation;
    std::function<bool(ProjectileActor&,const std::string&,std::string&)> createActivateFx;
};
struct ProjectileShotProviders {
    ProjectileThingProviders thing;
    ActorRandom random;
    float worldTime=0;
    // Three actual live fields not derivable from source UnitData. All other
    // owner values are read freshly from ActorWorld/source properties by helper.
    std::function<bool(const ActorComponents&,float& damageOutputModifier,
        int32_t& spawnedBy,bool& statusTraitor,std::string&)> ownerState;
    // Pure preflight and real synchronous consumption of remaining engine
    // operations: ClearOnAttackEffects, StartFx/marker, attachment, animation
    // radius/time, and opacity system. No absent callback becomes success.
    std::function<bool(const ProjectileInitializationOperation&,const ProjectileActor&,std::string&)> preflight;
    std::function<bool(const ProjectileInitializationOperation&,ProjectileActor&,std::string&)> execute;
    std::function<bool(const std::string&,std::string&)> validateGroup;
};
enum class ProjectileUpdateOperationKind {
    CreateStartFx,UnpauseAnimation,CheckProjectileDefense,UpdateAnimation,
    UpdateDetonation,Detonate,OnDeath,CheckForImpact,TransformChanged,DisposeThing
};
enum class ProjectileImpactResult : int32_t {Stop=0,Continue=1};
const char* projectileUpdateOperationName(ProjectileUpdateOperationKind);
struct ProjectileUpdateOperation {
    ProjectileUpdateOperationKind kind=ProjectileUpdateOperationKind::CheckProjectileDefense;
    float elapsed=0;
    int32_t victim=-1; // OnDeath victim Thing; -1 is native null.
};
struct ProjectileUpdateReply {
    // Original Projectile::CheckForImpact return value; Arc Update removes on0.
    // Acknowledge only after real collision/hit callbacks completed.
    std::optional<ProjectileImpactResult> impactResult;
    int32_t impactVictim=-1; // CheckForImpact STOP out-parameter (VA7144f/71519/71721).
};
struct ProjectileUpdateProviders {
    float worldTime=0,worldTimeMultiplier=0; // actual current World fields; no healthy/default clock
    ActorRandom random;
    std::function<bool(const ProjectileUpdateOperation&,const ProjectileActor&,std::string&)> preflight;
    std::function<bool(const ProjectileUpdateOperation&,ProjectileActor&,ProjectileUpdateReply&,std::string&)> execute;
    // Additional real references beyond copied source-weapon delayed shot and
    // cross-projectile ownership. Omit only while no such consumer is admitted.
    std::function<bool(uint64_t,bool&,std::string&)> retained;
};
struct ProjectileRetiredSnapshot {
    uint64_t handle=0;
    int32_t owner=-1;
    std::string name,weapon;
    float x=0,y=0,z=0,arcAngle=0;
    size_t polygonVertices=0;
};
// Actual projectile body/component storage. Public Thing ID stays-1; lifetime
// allocation uses the same parent World allocator as map/dynamic Things.
// Pending and active vectors preserve source manager order, not handle sorting.
class ProjectileWorld {
    std::map<std::string,SourceValue> thingData_;
    std::map<uint64_t,std::unique_ptr<ProjectileActor>> actors_;
    std::vector<uint64_t> pending_,active_,canBeReflected_;
    // ProjectileManager::PROJECTILE_EFFECTORS: admitted at pending promotion
    // when IncreaseAdjustRateOfFriendlyProjectilesInside!=0 (VA180078fc8).
    std::vector<uint64_t> effectors_;
    std::deque<ProjectileRetiredSnapshot> retired_;
    bool faulted_=false;
    bool updating_=false;
    bool numOpacityDirty_=false;
    std::string fault_;
    bool updateArc(uint64_t,float,const ProjectileUpdateProviders&,bool& remove,std::string&);
    // Base sgg::Projectile::UpdateSelf6a720 for classes without a subclass
    // override (STRAIGHT). ARC keeps ArcProjectile::UpdateSelf621b0 above.
    bool updateBase(uint64_t,float,const ProjectileUpdateProviders&,bool& remove,std::string&);
    bool collectDisposed(const ProjectileUpdateProviders&,std::string&);
    // Owner marker resolution for AngleFromMarker/AttachToMarker: owner pAnim
    // (+0x138) vslot0xd8 GetMarkerLocation(name), else owner GetLocation.
    std::function<bool(int32_t owner,const std::string& marker,float& x,float& y,std::string&)> ownerMarker_;
    std::function<bool(int32_t,bool&,bool&,std::string&)> primaryCombat_;
    std::function<bool(uint64_t,const std::string&,float,float,bool,float,std::string&)> detonateGraphic_;
    std::function<bool(const std::string&,float,float,std::string&)> detonateSound_;
    ProjectileDefenseProviders defenseProviders_;
    bool attachedAlive(const ProjectileActor&)const;
    // Thing::GetLocation2e0b70 / GetZLocation2e0bf0 composition.
    bool thingLocation(const ProjectileActor&,float& x,float& y,std::string&)const;
    bool thingZ(const ProjectileActor&,float& z,std::string&)const;
    // Thing::SetAngle2e0ce0 polygon rotation/mirror and Body angle store.
    bool setThingAngle(uint64_t,float radians,std::string&);
    // Projectile::SetLocation6ee90 (vslot0x80).
    bool setLocation(uint64_t,float x,float y,std::string&);
    // Projectile::StepMove75650; died=true after its OnDeath(victim) return1.
    bool stepMove(uint64_t,float fromX,float fromY,float toX,float toY,const ProjectileUpdateProviders&,bool& died,std::string&);
    // Projectile::Move702d0 (vslot0x1a8) for the base class.
    bool moveBase(uint64_t,float dt,const ProjectileUpdateProviders&,bool& died,std::string&);
    bool updateNumOpacity(std::string&);
public:
    bool retainsWeapon(const WeaponArsenal*,const std::string&)const;
    // Attached-aware Thing::GetLocation2e0b70 of a live projectile.
    bool projectileLocation(const ProjectileActor& p,float& x,float& y,std::string& e)const{return thingLocation(p,x,y,e);}
    // Actual primary-animation combat flags (OwnerInvulnerable, OwnerNoInvulnerableCoverage) by Thing id.
    // Projectile::Detonate6d670 DetonateGraphic/DetonateGraphic2: AnimationManager::Create29e410
    // with this projectile as owner Thing at its GetLocation. primary=true for DetonateGraphic,
    // which the parent follows with Animation::SetRadius2974e0(DamageRadius) and
    // PreferUnmodifiedTime29a5c0; DetonateGraphic2 is created only.
    using DetonateGraphicProvider=std::function<bool(uint64_t projectile,const std::string& name,float x,float y,bool primary,float radius,std::string&)>;
    void setDetonateGraphicProvider(DetonateGraphicProvider fn){detonateGraphic_=std::move(fn);}
    // AudioManager::PlayCue3D4a8a0(cue, location): parent resolves validity and plays.
    void setDetonateSoundProvider(std::function<bool(const std::string&,float,float,std::string&)> fn){detonateSound_=std::move(fn);}
    bool projectileZ(const ProjectileActor& p,float& z,std::string& e)const{return thingZ(p,z,e);}
    void setPrimaryCombatProvider(std::function<bool(int32_t,bool& invulnerable,bool& noCoverage,std::string&)> fn){primaryCombat_=std::move(fn);}
    void setOwnerMarkerProvider(std::function<bool(int32_t,const std::string&,float&,float&,std::string&)> fn){ownerMarker_=std::move(fn);}
    // Unit::SetLocation2f2a40 -> Thing::OnAttachedToMoved2e5960 for each
    // projectile attached to the moved Unit, in attachment order.
    bool ownerMoved(int32_t unit,std::string&);
    // ScriptAction::ExpireProjectiles258f10 over the active manager vector.
    // Weapon::PostFire89500 CancelAttachedProjectiles (WeaponData+0x709) scan of the active
    // manager vector: AttachToOwner && !(SkipFireCancelIfFiring && pFiredBy firing) &&
    // live mAttachedTo == owner -> mExpired(+0x4c2)=1.
    bool cancelAttached(int32_t unit,uint64_t unitLifetime,const std::function<bool(const ProjectileActor&,bool&,std::string&)>& firing,std::string&);
    bool expire(const std::vector<std::string>& names,const std::optional<std::string>& weapon,
        const std::optional<std::vector<std::string>>& excludeNames,std::string&);
    bool loadCatalog(SourceValue,std::string&);
    bool allocate(ProjectileInitializationRequest, const ProjectileDefinition&,
        ActorWorld& lifetimeOwner,uint64_t& handle,std::string&);
    bool initializeThing(uint64_t,const ProjectileThingProviders&,std::string&);
    bool initializeShot(ProjectileInitializationRequest,WeaponArsenal&,ActorWorld&,
        const ProjectileShotProviders&,uint64_t& handle,std::string&,bool spawnedDefinition=false);
    bool setupGeometry(uint64_t,std::string&);
    bool queuePending(uint64_t,std::string&);
    // Use at native ProjectileManager.Update, after Unit/Obstacle managers.
    bool flushPending(std::string&);
    // Native ProjectileManager phase: promote pending first, insertion-order
    // virtual updates with per-Thing time scaling, stable survivor compaction.
    // New pending admissions during callbacks remain pending until next frame.
    bool update(float managerElapsed,const ProjectileUpdateProviders&,std::string&);
    // Actual Arc.CheckProjectileDefense: captured native reflected-vector
    // order, real team/coverage/geometry tests and mandatory hit callbacks.
    bool checkArcProjectileDefense(uint64_t,const ProjectileDefenseProviders&,std::string&);
    // Bounded native ARC impact path: actual sparse obstacle broadphase,
    // source Unit filters, convex polygon narrowphase, source penetration and
    // mandatory genuine LOS/dodge/reflection/Impact consumers. Unsupported
    // source jump/bounce/range branches fail, never become a no-hit result.
    bool checkForImpact(uint64_t,const ProjectileImpactProviders&,
        ProjectileImpactResult&,std::string&,int32_t* stopVictim=nullptr);
    bool hasLineOfSight(const ProjectileCollisionOperation&,bool&,std::string&);
    bool detonate(uint64_t,const ProjectileDetonationProviders&,std::string&);
    bool updateDetonation(uint64_t,float elapsed,const ProjectileDetonationProviders&,std::string&);
    bool isInBlast(uint64_t,int32_t target,bool&,std::string&);
    bool blastDone(uint64_t,bool&,std::string&);
    // Native OnDeath6f5c0 up to the ScriptManager dispatch; parent dispatches
    // original Lua OnProjectileDeath with the returned values. victim<0 = null.
    bool prepareDeath(uint64_t,int32_t victim,ProjectileDeathArguments&,std::string&);
    bool createStartFx(uint64_t,std::string&);
    bool checkBaseProjectileDefense(uint64_t,std::string&);
    bool checkProjectileDefense(uint64_t,const ProjectileDefenseProviders&,bool arc,std::string&);
    void setDefenseProviders(ProjectileDefenseProviders p){defenseProviders_=std::move(p);}
    // Native pre-LifeComponent calculation; not health subtraction nor a
    // fabricated DamageResult. Parent supplies actual Config.DisableDamage.
    bool calcDamage(uint64_t,int32_t victim,bool disableDamage,
        const ProjectileImpactProviders&,ProjectileCalculatedDamage&,std::string&);
    bool prepareArcImpact(uint64_t,int32_t victim,ProjectileImpactGeometry&,std::string&);
    // GetImpactLocation72a90 + GetImpactAngle6e7d0 + impact Z, no CalcImpactLocation.
    bool impactGeometry(uint64_t,int32_t victim,ProjectileImpactGeometry&,std::string&);
    bool impactAngle(uint64_t,int32_t victim,float x,float y,float& angle,std::string&);
    bool applyImpact(uint64_t,int32_t victim,const ProjectileImpactGeometry&,const ProjectileHitProviders&,std::string&);
    // Native Impact->ApplyImpact->Life.TakeHit sequencing. Native health stays
    // in original Lua OnHit. Every unreconstructed side-effect branch yields
    // a mandatory operation; failure after an actual mutation faults storage.
    bool applyArcImpact(uint64_t,int32_t victim,const ProjectileHitProviders&,std::string&);
    bool applyDetonationImpact(uint64_t,int32_t victim,const ProjectileHitProviders&,std::string&);
    bool setProjectileNumOpacityFlag(uint64_t,std::string&);
    bool projectileNumOpacityDirty()const{return numOpacityDirty_;}
    // Parent invokes only from this real component's BodySystem phase with its
    // already-scaled native elapsed and actual UndoModifier.
    bool advanceBody(uint64_t,float actualBodyElapsed,float undoModifier,std::string&);
    ProjectileActor* actor(uint64_t);
    const ProjectileActor* actor(uint64_t)const;
    const std::vector<uint64_t>& active()const{return active_;}
    // Allocated actual Body components, including pending projectiles. Parent
    // performs native component reconciliation/scheduling; this is not manager
    // activation. Portable monotonic lifetime allocation preserves creation order.
    std::vector<uint64_t> bodyComponents()const;
    const std::deque<ProjectileRetiredSnapshot>& retired()const{return retired_;}
    std::string dump()const;
};
}
