#pragma once
#include "object_runtime.hpp"
#include "source_value.hpp"
#include "primitive_properties.hpp"
#include "unit_status.hpp"
#include "actor_animation.hpp"
#include "body_transition.hpp"
#include "physics_runtime.hpp"
#include "weapon_catalog.hpp"
#include "actor_lifecycle.hpp"
#include "unit_lifecycle.hpp"
#include "actor_spatial.hpp"
#include "actor_lighting.hpp"
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>

namespace hades {
struct ActorDefinition {std::string key,name,kind;SourceValue properties;};
struct ActorMapSeed {int32_t id=0;std::string definition;SourceValue map;};
struct ActorPoint {float x=0,y=0;};
struct ActorLife {
    int32_t activeHitFxCount=0,consecutiveHits=0;
    float invulnerableCoverage=0,invulnerableCoverageOffset=0,dodgeChance=0;
    float armorCoverage=6.2831854820251465f,collisionDamageMultiplier=1,damageTakenMultiplier=1;
    float consecutiveHitWindow=0,activeHitFxCooldown=1,reflection=0;
    bool initialized=false,isObstacle=false,isPlayerControlled=false;
};
// ReflectionHelper::ParseEnum<SortMode> VA1800eb0b0 (case-insensitive):
// Isometric0 FromParent1 Id2 Secondary3; unrecognized names yield 0.
uint8_t parseSortMode(const std::string&);
struct ActorComponents {
    uint64_t entityHandle=0;
    int32_t id=0,sourceId=0;
    const ActorDefinition* definition=nullptr;
    ObjectSnapshot* object=nullptr;
    int32_t parentId=0;std::vector<int32_t> children;
    // Generic Thing attachment is independent of map structural ownership.
    int32_t attachedParent=0;uint64_t attachedParentLifetime=0;
    float attachOffsetX=0,attachOffsetY=0,attachOffsetZ=0;
    std::vector<int32_t> attachedChildren;
    std::array<float,4> color{1,1,1,1};
    std::array<uint8_t,4> baseColorBytes{255,255,255,255};
    std::array<float,3> hsv{},tint{1,1,1};
    float squashAngle=0,squashScale=1,ambient=1;
    // Live Thing byte+0x38: InitThing copies ThingData SortMode (VA1802dd5c0, full+0x298);
    // Obstacle::OnParentAdded sets FromParent. Enum per ParseEnum<SortMode> VA1800eb0b0.
    uint8_t sortMode=0;
    float ownParallax=1,mapParallax=1;
    float velocityX=0,velocityY=0,velocityZ=0;
    float speed=0,lowSpeedThreshold=0,lowSpeedFraction=0;
    float speedModifier=1,speedAddition=0;
    float damageOutputModifier=1; // Unit.Defaults +3ec.
    uint32_t ambientSoundId=0; // Thing.Defaults full+224; actual allocated channel, separate from descriptor.
    bool suppressSounds=false; // Live Thing Bool field+62, separate from ThingData.
    int32_t nearbyDangerId=0; // Native source constructor default; danger system owns updates.
    int32_t spawnedById=0; // Unit.Defaults +3c8.
    float timeModifierFraction=1,timeMultiplier=1;
    float lastImpulsedAnimTime=0; // Thing.Defaults VA1802dbd3a, EBP=0.
    bool hasLife=false,invulnerable=false,targetable=true;
    ActorLife life;
    bool canMoveData=false,canAttackData=false,allowAnyFire=false,allowInput=false;
    uint8_t lifeStatus=0; // Native LifeStatus ALIVE=0, DEAD=1; separate from entity registration.
    bool registered=false,spawnDispatched=false,isMarkedDead=false;
    bool stopsUnits=false,stopsProjectiles=false;
    bool stopsUnitsDirty=true,stopsProjectilesDirty=true,drawBoundsChanged=false;
    bool hasText=false,isDisposed=false;
    // Populate from the actual primary when it is owned by the FX/book provider.
    // ActorAnimationPlayback's source flags already cover its own active clips.
    std::optional<bool> primaryOwnerImmobile,primaryDisableWeapons,primaryAirborne;
    std::optional<bool> primaryOwnerHasNoCollision;
    bool geometryAvailable=false,geometryRequired=false;
    // SetAngle's MirrorGeometry mutation history, independent of source Body
    // flip bits. Fresh SetupGeometry does not consult MirrorGeometry.
    bool geometryFacingFlipped=false;
    std::vector<ActorPoint> geometry;
    std::unique_ptr<WeaponArsenal> arsenal;
    std::unique_ptr<UnitStatus> status;
    std::unique_ptr<PhysicsState> physics;
    bool physicsPending=false; // Thing.AddComponent request, reconciled by World.
    bool physicsActive=false; // Actual Physics system membership, separate from allocation.
    ActorAnimationPlayback animation;
    uint64_t primaryEffectAnimation=0,primaryAnimationLifetime=0;
    float animationOwnScale=1,animationShakeX=0,animationShakeY=0;
    SourceValue::Object propertyOverrides;
    std::array<BodyTransition,2> fractions; // Scale, alpha; native initial values1.
    // Body color helpers +0x200/+0x260/+0x2c0 (R,G,B -> tint). Alpha is Body+0x320,
    // the same helper SetAlpha (RunScriptAction case 0x6a) drives: fractions[1].
    std::array<BodyTransition,3> colorChannels;
    BodyTransition angleTransition; // Body+0x80: timed SetAngle / Thing SetGoalAngle.
};
using ActorRandom=std::function<bool(double&,std::string&)>;
// Typed actor/component storage and manager registration. Public source IDs,
// generated Thing IDs and lifetime handles are different domains.
// Primary-animation OwnerHasNoCollision (AnimationDataDef+0x35) as read by obstacle/unit collision.
bool actorOwnerHasNoCollision(const ActorComponents&);
class ActorWorld {
    std::map<std::string,ActorDefinition> definitions_;
    std::map<int32_t,ActorMapSeed> seeds_;
    std::map<int32_t,std::unique_ptr<ActorComponents>> actors_;
    std::vector<int32_t> registrationOrder_;
    std::map<std::string,std::string> unitPropertyTypes_;
    std::map<std::string,std::string> lifePropertyTypes_,thingPropertyTypes_;
    bool initializeActor(int32_t,const ActorDefinition&,const ActorMapSeed*,float,float,const std::string&,
                         ObjectSnapshot&,std::shared_ptr<const WeaponCatalog>,const ActorRandom&,std::string&);
    uint32_t generation_=1,nextEntity_=1;
    std::shared_ptr<ActorAnimationBank> animationBank_;
    std::map<uint64_t,int32_t> sortDirty_; // Pointer-lifetime identity, no native hash iteration claim.
    std::function<bool(int32_t,bool,std::string&)> obstacleLocationChanged_;
    ActorSpatialGrid spatialGrid_;
    std::map<uint64_t,ActorOccluder> occluders_;
    std::map<uint64_t,bool> occluderInitialized_;
    uint64_t nextOccluderRevision_=1;
    std::map<uint64_t,int32_t> drawGridDirty_;
    std::function<bool(int32_t,ActorLocationStage,std::string&)> locationAuxiliary_;
    std::map<uint64_t,int32_t> obstacleDestroyQueue_;
    std::vector<std::pair<uint64_t,int32_t>> unitDestroyQueue_;
    bool disposalInProgress_=false,disposalFaulted_=false;
    std::string disposalFault_;
public:
    bool loadCatalog(SourceValue,std::string& error);
    bool setAnimationBank(std::shared_ptr<ActorAnimationBank>,std::string& error);
    bool initializeMapActor(int32_t sourceId,ObjectSnapshot& initial,
                            std::shared_ptr<const WeaponCatalog>,const ActorRandom&,
                            std::string& error);
    bool initializeDynamicObstacle(int32_t,const std::string&,float x,float y,const std::string& group,
        ObjectSnapshot&,const ActorRandom&,std::string& error);
    bool registerActor(int32_t,ObjectSnapshot&,std::string& error);
    bool linkStructuralChild(int32_t parent,int32_t child,std::string& error);
    bool destroyActor(int32_t,std::string& error);
    // No-Life Obstacle::Destroy admission only; registration and component/Lua
    // ownership survive until the source manager cleanup phase below.
    bool queueObstacleDestroy(int32_t,std::string& error);
    bool advanceObstacleDestruction(const ActorDisposalCallback&,std::string& error);
    // After the actual DEAD Unit's arsenal/movement/status phases. Caller
    // supplies current primary name from its real owning animation registry.
    bool queueReadyUnitRetirement(int32_t,const std::string& primaryName,std::string& error);
    bool advanceUnitDestruction(const ActorDisposalCallback&,const UnitDestructorCallback&,std::string& error);
    // Exact manager cleanup entry, normally called by advance above. Structural
    // descendants follow a FIFO; generic attached Things are not descendants.
    bool disposeObstacleTree(int32_t,const ActorDisposalCallback&,std::string& error);
    bool disposalFaulted()const{return disposalFaulted_;}
    const std::string& disposalFault()const{return disposalFault_;}
    bool attach(int32_t child,int32_t parent,float x,float y,float z,bool dynamicScale,std::string& error);
    bool location(int32_t,float& x,float& y,std::string& error)const;
    bool zLocation(int32_t,float&,std::string& error)const;
    bool refreshGeometry(int32_t,std::string& error);
    bool setAngle(int32_t,float radians,std::string& error);
    bool propertiesDirty(int32_t,std::string& error);
    bool obstacleStops(int32_t,bool projectiles,bool& result,std::string& error);
    // Obstacle::CheckMovementReactionsWithChildren2c37e0: DFS from mHandle over mChildren(+0x648)
    // (children pushed last-to-first, visited set); returns, in visit order, the obstacles whose
    // MapThing AllowMovementReaction(+0xeb) is set, i.e. those that get ScriptManager::OnMovementReaction.
    bool movementReactionTargets(int32_t,std::vector<int32_t>&,std::string& error)const;
    void setObstacleLocationChanged(std::function<bool(int32_t,bool,std::string&)> fn){obstacleLocationChanged_=std::move(fn);}
    std::vector<int32_t> takeSortDirty();
    // Parent auxiliary provider checks actual allocated component/FX registries.
    void setLocationAuxiliary(std::function<bool(int32_t,ActorLocationStage,std::string&)> fn){locationAuxiliary_=std::move(fn);}
    bool obstacleLocationChanged(int32_t,bool force,std::string& error);
    bool registerObstacleCollision(int32_t,std::string& error);
    bool clearObstacleGridRecord(int32_t,std::string& error);
    bool removeObstacleDrawGridRecord(int32_t,std::string& error);
    bool rebuildObstacleGrid(std::string& error);
    bool spatialObstacleCandidates(const SpatialBounds&,ObstacleGridCategory,std::vector<int32_t>&,std::string& error)const;
    std::vector<int32_t> takeDrawGridDirty();
    bool initializeOccluder(int32_t,std::string& error);
    bool updateOccluder(int32_t,std::string& error);
    bool removeOccluder(int32_t,std::string& error);
    std::vector<ActorOccluder> occluderSnapshot()const;
    bool lightSegmentBlocked(ActorOccluderPoint,ActorOccluderPoint,uint64_t excludedOwnerLifetime=0)const;
    bool adjustFraction(int32_t,bool scale,float,const BodyTransitionOptions&,std::string& error);
    bool advanceBody(int32_t,float elapsed,float undoModifier,std::string& error);
    // Channel 0..2 tint, 3 alpha; FUN_1802b7440 with {target,initial,options}.
    bool adjustAngle(int32_t,float targetRadians,const BodyTransitionOptions&,std::string& error);
    bool adjustColorChannel(int32_t,size_t channel,float target,float initial,const BodyTransitionOptions&,std::string& error);
    bool ensurePhysics(int32_t,std::string& error);
    bool applyScriptUpwardForce(int32_t,float speed,float hangTime,std::string& error);
    bool unitCanMove(int32_t,float actualPlayerSpeedMultiplier,bool& result,std::string& error)const;
    bool unitAirborne(int32_t,bool& result,std::string& error)const;
    bool weaponOwnerFrame(int32_t,float worldTime,float actualPlayerSpeedMultiplier,
        bool editingMode,bool actualInputAllowed,WeaponOwnerFrame&,std::string& error)const;
    bool setUnitProperty(int32_t,const std::string&,const PropertyInput&,PropertyChange,std::string& error);
    bool setThingProperty(int32_t,const std::string&,const PropertyInput&,PropertyChange,std::string& error);
    bool setLifeProperty(int32_t,const std::string&,const PropertyInput&,PropertyChange,bool data,std::string& error);
    const SourceValue* currentProperty(int32_t,const std::string&)const;
    ActorComponents* actor(int32_t);
    const ActorComponents* actor(int32_t)const;
    std::span<const int32_t> registrationOrder()const{return registrationOrder_;}
    const ActorMapSeed* seed(int32_t)const;
    const ActorDefinition* definition(const std::string&)const;
    uint64_t allocateEntityLifetime(std::string& error);
    std::string dump()const;
};
}
