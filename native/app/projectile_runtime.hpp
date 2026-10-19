#pragma once
#include "weapon_runtime.hpp"
#include <string>
#include <functional>
#include <optional>

namespace hades {
enum class ProjectileClass : int32_t { Straight=0,Homing=1,Beam=2,Lob=3,Sky=4,Arc=5,Instant=6 };
enum class ProjectileCollisionCheck : uint8_t { Bounding=0,Polygon=1,CirclePolygon=2,PointInPolygon=3,PolygonContainsPoint=4,CircleCircle=5 };
struct ProjectileCreationDescriptor {
    ProjectileClass concreteClass=ProjectileClass::Straight;
    ProjectileCollisionCheck unitCollisionCheck=ProjectileCollisionCheck::Polygon;
    ProjectileCollisionCheck obstacleCollisionCheck=ProjectileCollisionCheck::Polygon;
    // Native covered base/HOMING/ARC path retains Thing::Defaults mId=-1;
    // lifetime/entity handles belong to the parent, never a new guessed ID range.
    int32_t publicThingId=-1;
    bool usesBaseProjectileInit=false;
    bool requiresHomingDefaults=false;
};
// Exact native manager dispatch descriptor only. This creates NO actor/body,
// allocates no identity, and does not claim Init or queue registration occurred.
// Values are validated from current copied ProjectileData, never guessed from
// weapon names or representative art. Full typed source v6 is required.
bool describeProjectileCreation(const ProjectileDefinition&,ProjectileCreationDescriptor&,std::string& error);
// Covered prefix of Projectile::Init, through ClearOnAttackEffects, before
// native GUN charge/range/speed modifiers. This deliberately does not register
// an active projectile or report completion of the full Init routine.
struct ProjectileInitializationRequest {
    uint64_t lifetimeHandle=0; // parent-owned portable lifetime identity
    int32_t ownerId=-1,fireIndex=0,volleyIndex=0;
    std::string weaponName,groupName;
    float x=0,y=0;
    WeaponFireRequest fire;
    // Native manager takes the actual volley/scatter angle separately from
    // the captured FireRequest. Older callers can omit when both are equal.
    std::optional<float> shotAngle;
};
struct ProjectileInitializationFrame {
    float worldTime=0;
    bool thingInitialized=false,hasPhysics=false,hasBody=false;
    bool ownerExists=false,ownerTraitor=false;
    int32_t ownerSpawnedById=-1;
    // Historical API spelling: native AddOwnerZ uses Thing.GetZLocation ONLY,
    // excluding Unit.GetAirborneZ. Parent must supply that exact value.
    float ownerTotalZ=0;
    // Re-evaluated after synchronous operations by the parent. Exact native
    // IsPerfectCharge and HasAmmo results, not guessed from source defaults.
    bool weaponPerfectCharge=false,weaponHasAmmo=false;
};
struct ProjectileInitialState {
    uint64_t lifetimeHandle=0;int32_t publicThingId=-1;
    int32_t ownerId=-1,ownerSpawnedById=-1,fireIndex=0,volleyIndex=0,waveIndex=0;
    float spawnAngle=0,lastJumpTime=0,pauseTimeRemaining=0,startDelay=0;
    float critBonus=0,additiveDamageMultiplier=0,damageMultiplier=1;
    int32_t damageAddition=0;
    bool hasStarted=false,useStartMarker=false,firedByTraitor=false;
    bool blockSpawns=false,detonated=false,expired=false,blinkWeaponActive=false;
    bool forceOwnerReturn=false,flipAllegiance=false,hitMaxRange=false;
    int32_t detonations=0,numBounces=0,numJumps=0,numPenetrations=0;
    float impactX=0,impactY=0,speed=0,maxSpeed=0,acceleration=0;
    float range=0,rangeSquared=0,blastRadius=0,blastTimeRemaining=0,blastRadiusModifier=1;
    float fuseRemaining=0,lifeRemaining=0,moveIntervalRemaining=0,targetX=0,targetY=0;
};
enum class ProjectileInitializationOperationKind {
    ThingInitialize,SetPhysicsIgnoreGravity,AdjustBodyAlpha,AddOwnerZ,
    RetainFiringWeapon,AddToGroup,SetWeaponLastFirePerfectCharge,
    SetAnimation,CreateAttachedAnimation,SetAttachedAnimationRadius,
    RequestRandomDouble,SetupGeometry,RefreshLocation,SetAngle,ClearOnAttackEffects,
    SetBodyScale,ApplyStartMarker,CreateStartAnimation,AttachToOwner,MatchOwnerBody,
    FinalizeHomingTargetLink,QueuePendingProjectile,InheritElapsedTimeMultiplier,CopyOwnerGroups,SetProjectileNumOpacityFlag
};
struct ProjectileInitializationOperation {
    ProjectileInitializationOperationKind kind=ProjectileInitializationOperationKind::ThingInitialize;
    uint64_t lifetimeHandle=0;int32_t ownerId=-1;
    std::string projectileName,weaponName,name;
    float x=0,y=0,value=0,start=0,duration=0;
    bool flag=false;
};
using ProjectileInitializationSupport=std::function<bool(const ProjectileInitializationOperation&,std::string&)>;
enum class ProjectileInitializationProgress { Yielded,BeforeWeaponModifiers };
struct ProjectileInitializationStep {
    ProjectileInitializationProgress progress=ProjectileInitializationProgress::Yielded;
    ProjectileInitializationOperation operation;
};
class ProjectileInitialization {
public:
    explicit ProjectileInitialization(ProjectileInitializationRequest);
    // A yielded operation must be consumed synchronously before resuming. Pass
    // a fresh per-instance definition/frame after callbacks; no stale data
    // reference is retained. A RandomDouble yield alone requires one [0,1)
    // reply on the next call. Unsupported consumers do not advance this cursor.
    bool advance(const ProjectileDefinition&,const ProjectileInitializationFrame&,
                 std::optional<double> randomReply,const ProjectileInitializationSupport&,
                 ProjectileInitializationStep&,std::string& error);
    const ProjectileInitialState& state() const { return state_; }
private:
    ProjectileInitializationRequest request_;ProjectileInitialState state_;
    std::string definitionName_;unsigned phase_=0;
    bool waitingRandom_=false;float speedBase_=0,speedVariance_=0,fuseMin_=0,fuseMax_=0;
};
struct ProjectileGunOwnerFrame {
    // Parent resolves these from the actual live owner after ClearOnAttackEffects.
    // UnitData +29c,+2a0,+2f8 and live Unit+3ec, respectively.
    bool exists=false,perfectCharge=false;
    float damageOutputMultiplier=0,projectileSpeedMultiplier=0,perfectChargeBonus=0,damageOutputModifier=0;
};
// Exact pure GUN scalar modifier section of Projectile::Init. Caller consumes
// an unconditional SetBodyScale(bodyScale), MarkDirty and SetupGeometry next.
// This is not a full shot, initialization completion or collision activation.
bool applyProjectileGunModifiers(ProjectileInitialState&,const WeaponSnapshot&,
    const ProjectileGunOwnerFrame&,float& bodyScale,std::string& error);
struct ProjectileInitializationTailFrame {
    ProjectileGunOwnerFrame gunOwner;
    // Actual Thing.GetLocation and mSpawnLocation after any marker operation.
    float worldX=0,worldY=0,spawnX=0,spawnY=0;
    float ownerElapsedTimeMultiplier=0;
};
enum class ProjectileInitializationTailProgress { Yielded,PendingQueueSetupFinished };
struct ProjectileInitializationTailStep {
    ProjectileInitializationTailProgress progress=ProjectileInitializationTailProgress::Yielded;
    ProjectileInitializationOperation operation;
};
class ProjectileInitializationTail {
public:
    // Start ONLY after the prefix yielded BeforeWeaponModifiers and the parent
    // consumed all its real operations. No reconstruction live registration is
    // inferred merely from an InitialState DTO passed to this constructor.
    ProjectileInitializationTail(ProjectileInitializationRequest,ProjectileInitialState);
    bool advance(const ProjectileDefinition&,const WeaponSnapshot&,
        const ProjectileInitializationTailFrame&,const ProjectileInitializationSupport&,
        ProjectileInitializationTailStep&,std::string& error);
    const ProjectileInitialState& state()const{return state_;}
private:
    ProjectileInitializationRequest request_;ProjectileInitialState state_;
    unsigned phase_=0;
};
}

namespace hades {
struct ArcProjectileMoveFrame {
    // Manager has already applied mElapsedTimeMultiplier. World time and
    // Thing creation time are actual clocks, not an assumed frame counter.
    float dt=0,worldTime=0,creationTime=0;
    float spawnX=0,spawnY=0,range=0,spawnAngle=0;
    bool ownerExists=false,ownerHasBody=false;
    float ownerX=0,ownerY=0,ownerBodyAngle=0;
};
struct ArcProjectileMovePlan {
    float nextAngleOffset=0,x=0,y=0,requestedAngle=0;
    bool nativeShouldRemove=false,hasTransform=false;
};
// ArcProjectile::Init writes ArcStart AFTER full base Projectile::Init.
// This pure helper performs no initialization, lifetime registration or IO.
bool initialArcProjectileAngle(const ProjectileDefinition&,float&,std::string& error);
// Exact ArcProjectile::Move math (RVA62440). Pure preflight plan; parent must
// consume SetLocation(x,y,false), then SetAngle(requestedAngle), then commit
// nextAngleOffset. This is NOT full UpdateSelf/collision/detonation handling.
bool planArcProjectileMove(const ProjectileDefinition&,float currentAngleOffset,
    const ArcProjectileMoveFrame&,ArcProjectileMovePlan&,std::string& error);
}
