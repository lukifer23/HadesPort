#pragma once
#include <array>
#include "asset_store.hpp"
#include "room_assets.hpp"
#include "object_runtime.hpp"
#include "screen_animation.hpp"
#include "unit_movement.hpp"
#include "map_creation.hpp"
#include "weapon_runtime.hpp"
#include "actor_runtime.hpp"
#include "life_runtime.hpp"
#include "actor_light_world.hpp"
#include "self_effect_world.hpp"
#include "asset_packages.hpp"
#include "effect_animation.hpp"
#include "effect_particle.hpp"
#include "action_effect.hpp"
#include "scene_primary.hpp"
#include "scene_runtime.hpp"
#include "script_scheduler.hpp"
#include "movie_preload.hpp"
#include "camera_fade.hpp"
#include "color_grading.hpp"
#include "bloom_state.hpp"
#include "native_camera.hpp"
#include "actor_motion.hpp"
#include "interaction_runtime.hpp"
#include "actor_marker.hpp"
#include "projectile_world.hpp"
#include "audio_world.hpp"
#include "speech_world.hpp"
#include "text_layout.hpp"
#include "audio_output.hpp"
#include "save_runtime.hpp"
#include "native_flash.hpp"
#include "native_shake.hpp"
#include <map>
#include <string>
#include <unordered_set>
struct lua_State;
namespace hades {struct WeaponTriggerReply;}

class ScriptRuntime {
    lua_State* state_ = nullptr;
    hades::AssetStore* assets_;
    std::unique_ptr<hades::RoomAssets> roomAssets_;
    std::unordered_set<std::string> loaded_;
    std::string error_;
    std::map<int32_t,hades::NativeFlash> flashes_;
    std::map<int32_t,hades::NativeShake> shakes_;
    std::vector<int32_t> shakeOrder_;
    void registerShakeAction();
    static int shakeAction(lua_State*);
    bool shakeActionImpl(lua_State*,std::string&);
    bool advanceShakes(float,std::string&);
    std::array<float,2> shakeOffsetForOwner(int32_t,uint64_t)const;
    void registerFlashActions();
    static int flashAction(lua_State*);
    bool flashActionImpl(lua_State*,bool,std::string&);
    bool advanceFlashes(float,std::string&);
    std::array<float,4> flashColorForOwner(int32_t,uint64_t)const;
    bool mainLoaded_ = false, gameLoaded_ = false;
    bool roomBooted_ = false;
    std::string mapName_ = "RoomOpening";
    struct MapRequest {std::string name;bool resetBinks=false,resetWeaponBinks=false;std::optional<std::array<float,4>> background;};
    std::optional<MapRequest> pendingMap_;
    uint64_t mapRevision_=0;
    void registerMapActions();
    static int loadMapAction(lua_State*);
    bool loadMapActionImpl(lua_State*,std::string&);
    bool beginRequestedMap(bool restoreAssetsOnly=false);
    std::vector<ObjectSnapshot> mapMetadata_;
    std::map<int32_t,const ObjectSnapshot*> inactiveMap_;
    MapCreationPlan creationPlan_;
    bool mapProbeInstalled_ = false;
    std::vector<hades::WeaponDefinition> weaponDefinitions_;
    std::shared_ptr<const hades::WeaponCatalog> weaponCatalog_;
    hades::ActorWorld actorWorld_;
    hades::LifeSystemRuntime lifeSystem_{actorWorld_};
    hades::ActorLightWorld actorLights_{actorWorld_};
    hades::InteractionWorld interactions_;
    std::map<uint32_t,hades::InteractionKey> useTargets_;
    bool interactionGridReady_=false,previousUseDown_=false,interactUserPrimaryUnknown_=false;
    hades::InteractionProviders interactionProviders();
    bool interactionOwnerFrame(hades::InteractionKey,hades::InteractionOwnerFrame&,std::string&);
    bool interactionUserFrame(hades::InteractionKey,hades::InteractionUserFrame&,std::string&);
    bool initializeInteraction(int32_t,std::string&);
    bool removeInteraction(int32_t,std::string&);
    bool advancePlayerUse(std::string&);
    bool useInteraction(hades::InteractionKey,hades::InteractionKey,std::string&);
    bool consumeInteractionOperation(const hades::InteractionUseOperation&,std::string&);
    bool interactionPrimaryFlags(int32_t,bool&,bool&,bool&,std::string&)const;
    void registerInteractionActions();
    static int interactionAction(lua_State*);
    bool interactionActionImpl(lua_State*,int,std::string&);
    hades::WeaponSelfEffectWorld selfEffects_;
    hades::ProjectileWorld projectileWorld_;
    // Projectile-owned action FX (Projectile::Detonate6d670 DetonateGraphic owner Thing).
    // Projectiles have no public Thing id; FX ownership uses a reserved negative id
    // domain mapped to the projectile lifetime handle (portable representation only).
    std::map<int32_t,uint64_t> projectileFxOwners_;std::map<uint64_t,int32_t> projectileFxOwnerIds_;int32_t nextProjectileFxOwner_=-0x1000000;
    int32_t projectileFxOwner(uint64_t handle);
    bool projectileEffectOwner(int32_t,hades::EffectOwnerState&,std::string&)const;
    bool releaseProjectileFxOwner(uint64_t handle,std::string&);
    void installProjectileGraphicProvider();
    bool setProjectileAnimation(hades::ProjectileActor&,const std::string&,std::string&);
    bool projectileGraphicTransform(uint64_t,std::string&);
    hades::ProjectileDefenseProviders projectileDefenseProviders();
    std::shared_ptr<hades::ActorAnimationBank> animationBank_;
    hades::AssetPackages packages_;
    hades::EffectAnimationWorld effects_;
    hades::EffectParticleWorld particles_;
    hades::ActionEffectWorld actionEffects_;
    std::shared_ptr<hades::ActorAnimationBank> actionEffectBank_;
    bool loadActionEffectDefinitions();
    hades::ActionEffectProviders actionEffectProviders();
    bool prepareActionEffect(const std::string&,int32_t,std::string&);
    bool createActionEffect(const std::string&,int32_t,std::optional<std::array<float,3>>,uint64_t&,std::string&);
    hades::ScenePrimaryWorld scenePrimaries_;
    struct PresentationBase {ObjectSnapshot body;std::array<float,4> color;std::array<float,3> hsv;};
    std::map<int32_t,PresentationBase> presentationBases_;
    std::map<uint32_t,int32_t> sceneSpriteOwners_;
    std::vector<std::string> originalSceneGroupNames_;
    mutable uint64_t presentationGroupFrame_=UINT64_MAX;
    mutable std::vector<SceneGroupDefinition> presentationGroups_;
    mutable std::map<std::string,uint32_t> presentationGroupIndices_;
    uint64_t presentationShadowFrame_=UINT64_MAX;
    std::shared_ptr<hades::LightShadowGeometry> presentationShadows_;
    std::shared_ptr<const hades::LightShadowGeometry> presentationShadowGeometry();
    uint32_t presentationGroup(const std::string&)const;
    uint32_t presentationDrawGroup(const ObjectSnapshot&)const;
    std::shared_ptr<hades::ActorAnimationBank> particleBank_,spawnerBank_,leafBank_;
    std::map<int32_t,std::string> primarySpawnerRecipes_;
    std::map<std::string,hades::EffectTexture> primarySpawnerGraphics_;
    hades::AudioWorld audio_;
    hades::SpeechWorld speech_{audio_};
    void initializeSpeechBridge();
    bool advanceSpeech(float,std::string&);
    bool speechActionImpl(lua_State*,uint32_t&,std::string&);
    static int speechAction(lua_State*);
    bool speechControlImpl(lua_State*,int,std::string&);
    static int speechControlAction(lua_State*);
    hades::AudioOutput audioOutput_;
    struct DelayedSound {uint32_t id;float deadline;};
    std::vector<DelayedSound> delayedSounds_;
    std::unordered_set<uint32_t> audioCallbacks_;
    uint32_t audioFramePlays_=0;
    bool playAudioCue(const std::string&,int32_t,const std::array<float,3>&,uint32_t&,std::string&,int32_t managerCap=0);
    bool audioOwnerDeath(int32_t,std::string&);
    bool stopActorAmbientSound(int32_t,std::string&);
    bool admitOwnerAudio(const std::string&,int32_t,bool&,std::string&,int32_t managerCap=0);
    bool actorAudioSource(int32_t,hades::AudioSource&,std::string&);
    bool playLocationAudioCue(const std::string&,const std::array<float,3>&,int32_t,uint32_t&,std::string&);
    bool loadAudioDefinitions();
    bool advanceAudio(float,std::string&);
    bool audioActionImpl(lua_State*,int,uint32_t&,std::string&);
    static int audioAction(lua_State*);
    hades::MoviePreload movies_;
    hades::CameraFade cameraFade_;
    hades::ColorGrading colorGrading_;
    hades::FullscreenBloom bloom_;
    static int bloomAction(lua_State*);
    bool bloomActionImpl(lua_State*,std::string&);
    void loadBloomDefinitions(hades::AssetStore&);
    bool advanceBloom(std::string&);
    static int colorGradingAction(lua_State*);
    bool colorGradingActionImpl(lua_State*,std::string&);
    hades::NativeCamera camera_;
    hades::NativeCameraResolve cameraTargets();
    static int cameraAction(lua_State*);
    bool cameraActionImpl(lua_State*,int,std::string&);
    static int actorMotionAction(lua_State*);
    static int expireProjectilesAction(lua_State*);
    bool actorMotionActionImpl(lua_State*,int,std::string&);
    bool shiftActorZ(int32_t,float,std::string&);
    // Obstacle::MoveTo 0x2c5bf0 / Obstacle::ShiftLocationWithChildren 0x2c3510.
    bool obstacleMoveImpl(const hades::SourceValue&,const std::vector<int32_t>& all,const std::vector<int32_t>& obstacles,const std::vector<int32_t>& destinations,std::string&);
    bool obstacleMoveTo(int32_t,float x,float y,float duration,float speed,bool smooth,float easeIn,float easeOut,float tmf,std::string&);
    bool shiftObstacleWithChildren(int32_t,float dx,float dy,std::string&);
    hades::ActorMotionWorld actorMotion_;
    hades::ActorMarkerBank markers_;
    bool roomActivated_=false, activationStarted_=false, worldFaulted_=false;
    float worldTime_=0;
    uint64_t worldFrame_=0;
    struct SurveyEntry{uint64_t firstFrame=0;uint32_t count=0;};
    std::map<std::string,SurveyEntry> survey_;
    bool faultContainment_=false;std::string lastContained_;
    bool containStage(const char* stage);
    static int surveyRecord(lua_State*);
    float localMoveX_=0,localMoveY_=0;
    std::vector<std::string> localActionDown_,previousLocalActionDown_;
    struct WorldNotify {
        int arguments=0;
        bool control=false,released=false,playerInput=false;
        bool allDead=false; // ScriptAction::CheckOnAllDead27ce60.
        bool stopped=false; // ScriptAction::CheckStopped 0x2797f0 (NotifyOnStopped, action 244).
        uint8_t unitState=0; // CheckCanAttack27e960 / CheckRotationComplete279a40.
        bool whenSeparated=false,pointOnly=false;
        uint8_t distance=0; // 1 WithinDistanceAverage, 2 WithinDistanceAny, 3 OutsideDistanceAverage.
        float timeout=0;
        std::vector<int32_t> sources,destinations;
        std::vector<std::string> names;
    };
    std::vector<WorldNotify> worldNotifies_;
    static int collisionNotifyAction(lua_State*);
    bool collisionNotifyImpl(lua_State*,bool,std::string&);
    bool advanceNativeNotifies(float,std::string&);
    static int allDeadNotifyAction(lua_State*);
    bool checkAllDeadNotify(const WorldNotify&,bool&,std::string&);
    static int distanceNotifyAction(lua_State*);
    bool checkDistanceNotify(const WorldNotify&,bool&,std::string&);
    static int stoppedNotifyAction(lua_State*);
    bool checkStoppedNotify(const WorldNotify&,bool&,std::string&);
    static int unitAIQueryAction(lua_State*);
    bool unitAIQueryImpl(lua_State*,int,int,bool&,std::string&);
    bool unitAttackAllowed(int32_t,bool,bool,bool&,std::string&);
    static int unitStateNotifyAction(lua_State*);
    bool checkUnitStateNotify(const WorldNotify&,bool&,std::string&);
    static int distanceQueryAction(lua_State*);
    bool distanceQueryImpl(lua_State*,float&,std::string&);
    static int intersectingIdsAction(lua_State*);
    bool intersectingIdsImpl(lua_State*,std::vector<int32_t>&,std::string&);
    static int thingQueryAction(lua_State*);
    bool thingQueryImpl(lua_State*,int mode,double&,std::string&);
    std::vector<std::string> rawControlsDown_,rawControlsPressed_,rawControlsReleased_;
    void snapshotControlFrame();
    static int controlNotifyAction(lua_State*);
    bool controlNotifyImpl(lua_State*,bool,std::string&);
    bool checkControlNotify(const WorldNotify&,bool&,std::string&,std::string&);
    static int spatialQueryAction(lua_State*);
    bool spatialQueryImpl(lua_State*,int mode,double&,std::string&);
    std::shared_ptr<hades::TextFontBank> textFonts_;
    std::function<bool(const std::string&,std::string&,std::string&)> controlLabelProvider_;
    hades::TextCaptionWorld captionText_;
    std::map<uint64_t,hades::UnitMovementState> localMovement_;

    float worldSpeedModifier_=1; // Matching World constructor +0x20.
    float worldSpeedTarget_=1,worldSpeedRate_=1,worldSpeedRemaining_=0; // World+24/+28/+2c.
    static int simulationSpeedAction(lua_State*);
    bool simulationSpeedImpl(lua_State*,std::string&);
    bool advanceWorldSpeed(float,std::string&);
    bool physicsFaulted_=false;
    std::string physicsFault_;
    std::function<bool(int32_t,std::string&)> physicsTouchdownWeapon_;
    std::function<bool(int32_t,float,float,std::string&)> physicsRumble_;
    std::function<bool(int32_t,float&,std::string&)> physicsGrip_;
    double screenTime_=0;
    hades::ScreenAnimationWorld screenAnimations_;
    std::shared_ptr<hades::ActorAnimationBank> screenAnimationBank_;
    std::map<int32_t,hades::WeaponArsenal> stagedUnitArsenals_;
    std::string diagnosticWeaponDump_;
    static ScriptRuntime* runtime(lua_State*);
    static int import(lua_State*);
    static int unsupported(lua_State*);
    static int delayedAction(lua_State*);
    static int equipWeapon(lua_State*);
    static int createAnimation(lua_State*);
    static int playerInputNotifyAction(lua_State*);
    bool rawInputPulse_=false,playerInputThisFrame_=false;
    static int rumbleAction(lua_State*);
    static int setColor(lua_State*);
    static int attach(lua_State*);
    bool attachImpl(lua_State*,std::string&);
    static int switchActiveUnit(lua_State*);
    bool switchActiveUnitImpl(lua_State*,std::string&);
    static int inactiveIds(lua_State*);
    bool inactiveIdsImpl(lua_State*,bool byType,std::string&);
    static int preloadBinks(lua_State*);
    bool preloadBinksImpl(lua_State*,std::string&);
    bool loadMovieDefinitions();
    bool effectMarker(int32_t,const std::string&,std::array<float,3>&,std::string&)const;
    static int fade(lua_State*);

    static int applyUpwardForce(lua_State*);
    bool fadeImpl(lua_State*,bool in,std::string&);
    bool setColorImpl(lua_State*,std::string&);
    bool createAnimationImpl(lua_State*,std::string&);
    static int weaponProperty(lua_State*);
    bool weaponPropertyImpl(lua_State*,int mode,std::string&);
    static int runWeaponMethod(lua_State*);
    bool runWeaponMethodImpl(lua_State*,std::string&);
    static int spawnUnit(lua_State*);
    bool spawnUnitImpl(lua_State*,int32_t&,std::string&);
    static int fireWeaponFromUnitAction(lua_State*);
    bool fireWeaponFromUnitImpl(lua_State*,std::string&);
    static int spawnObstacle(lua_State*);
    bool spawnObstacleImpl(lua_State*,int32_t&,std::string&);
    static int setUnitProperty(lua_State*);
    static int setLifeProperty(lua_State*);
    static int setThingProperty(lua_State*);
    static int setObstacleProperty(lua_State*);
    static int getUnitDataValue(lua_State*);
    bool getUnitDataValueImpl(lua_State*,hades::SourceValue&,std::string&);
    bool setActorPropertyImpl(lua_State*,int kind,std::string&);
    bool setUnitPropertyImpl(lua_State*,std::string&);
    bool equipWeaponImpl(lua_State*,std::string&);
    static bool delayedRandom(void*,float,float,float&,std::string&);
    static bool executeDelayed(lua_State*,int,int,void*,std::string&);
    bool installDelayedActions();
    static int trigger(lua_State*);
    static int print(lua_State*);
    static int debugMessage(lua_State*);
    static int getTime(lua_State*);
    static int getMapName(lua_State*);
    static int getMapThings(lua_State*);
    static int debugAssert(lua_State*);
    static int setRichPresence(lua_State*);
    bool load(const char* name, lua_State* caller = nullptr);
    bool loadMapObjects();
    bool loadWeaponDefinitions();
    bool configurationFloat(const char*,float&,std::string&);
    bool physicsGripValue(int32_t,float&,std::string&);
    bool physicsUnitTouchdown(int32_t,std::string&);
    struct AimLineState {uint64_t handle=0,frame=0,ownerLifetime=0;float x=0,y=0,endX=0,endY=0,width=0;std::string weapon;};
    std::map<int32_t,AimLineState> aimLines_;
    bool weaponAimLine(const hades::WeaponOutput&,bool execute,std::string&);
    bool aimLineIntersection(int32_t,const hades::ProjectileDefinition&,float,float,float&,float&,float,float,std::string&);
    bool weaponCallback(const hades::WeaponOutput&,std::string&);
    bool weaponOperationSupported(const hades::WeaponOutput&,std::string&);
    bool consumeWeaponOperation(const hades::WeaponOutput&,hades::WeaponTriggerReply&,std::string&);
    // BlinkWeapon::Fire teleport segment / BlinkWeapon::Update body (lua_combat.cpp).
    bool blinkTeleport(const hades::WeaponOutput&,bool& fireContinues,std::string&);
    bool blinkUpdate(const hades::WeaponOutput&,std::string&);
    bool unitSetLocation(int32_t,float x,float y,std::string&);
    bool unitLocationValid(int32_t,float x,float y,bool& valid,std::string&);
    bool unitPathNodeBlocked(int32_t,float x,float y,bool preferAvoid,bool& blocked,std::string&);
    bool unitLineOfMovement(int32_t,float x,float y,bool preferAvoid,bool& clear,std::string&);
    bool spawnPointBlocked(float x,float y,bool&,std::string&);
    bool closestNonBlockedLocation(float x,float y,float& outX,float& outY,std::string&);
    bool gridLineBlocked(float x0,float y0,float x1,float y1,bool& blocked,std::string&);
    bool gridLocationBlocked(float x,float y,bool& blocked,std::string&);
    bool thingCalcValidLocationOnVector(int32_t,float tx,float ty,float sx,float sy,float lx,float ly,float step,float& ox,float& oy,std::string&);
    // Unit::ApplyFullShift2f76d0 / Unit::ApplyShift2f7c00 / Unit::OnObstacleCollision2f6ce0 (unit_shift in lua_combat.cpp).
    bool unitApplyFullShift(int32_t,float locX,float locY,float dx,float dy,bool stopOnCollision,bool ignoreNoCollisionAnim,std::string&);
    bool unitApplyShift(int32_t,float nx,float ny,float sx,float sy,bool ignoreNoCollisionAnim,bool& collided,std::string&);
    bool unitOnUnitCollision(int32_t unit,int32_t other,float velocity,std::string&);
    bool unitOnObstacleCollision(int32_t unit,int32_t obstacle,float sx,float sy,float px,float py,float& damage,std::string&);
    struct UnitCollisionSets {std::vector<std::pair<int32_t,uint64_t>> previous,current;float lastTranslationX=0,lastTranslationY=0;};
    std::map<uint64_t,UnitCollisionSets> unitCollisionSets_; // Unit+0x4f8 mPrevColliding, +0x528 mCurrentlyColliding, +0x410 mLastCollisionTranslation
    std::array<uint64_t,1999> polygonAxisCache_{}; // PolygonCollisionOpt2cefd0 DAT_181a095d0 separating-axis cache
    bool lifeSetInvulnerable(int32_t,std::string&);
    bool lifeSetVulnerable(int32_t,std::string&);
    static int lifeInvulnerableAction(lua_State*);
    static int hasEffectAction(lua_State*);
    static int clearEffectAction(lua_State*);
    static int swapWeaponAction(lua_State*);
    void registerLifeActions();
    int32_t lastLocationBlocker_=0; // diagnostic: obstacle that failed the last IsLocationValid
    bool gridCheckMovementReactions(float x0,float y0,float x1,float y1,float distance,std::string&);
    hades::WeaponSelfEffectProviders selfEffectProviders();
    bool effectCallback(hades::WeaponSelfEffectInstance&,const hades::WeaponEffectDefinition&,bool,bool,std::string&);
    bool advanceUnitEffects(float,std::string&);
    bool advanceWeapons(float,std::string&);
    bool advanceLife(float,std::string&);
    bool primaryCombatFlags(int32_t,bool& invulnerable,bool& noArmorCoverage,bool& noInvulnerableCoverage,std::string&)const;
    bool primaryUntargetable(int32_t,bool&,std::string&)const;
    bool retirePrimaryAnimation(uint64_t,std::string&);
    bool applyWeaponPhysics(const hades::WeaponOutput&,bool execute,std::string&);
    bool weaponAnimationRadius(const hades::WeaponOutput&,bool execute,std::string&);
    bool onActorForce(int32_t,bool execute,std::string&);
    bool isImmuneToForce(int32_t,bool&,std::string&);
    bool externalForceOwner(int32_t,hades::PhysicsForceOwner&,std::string&);
    bool createWeaponProjectile(const hades::WeaponOutput&,uint64_t&,std::string&);
    bool advanceProjectiles(float,std::string&);
    bool applyProjectileImpact(uint64_t,int32_t,const hades::ProjectileImpactProviders&,std::string&,bool detonation=false);
    void fillImpactProviders(hades::ProjectileImpactProviders&);
    bool loadProjectileDefinitions();
    std::map<std::string,hades::SourceValue> actorAnimationDefinitions_;
    hades::SourceValue nativeAnimationDefaults_;
    bool loadActorAnimationDefinitions();
    bool loadScreenAnimationDefinitions();
    hades::ScreenAnimationSound screenAnimationSound();
    bool spawnScreenObstacleImpl(lua_State*,int32_t&,std::string&);
    bool setScreenAnimation(int32_t,const std::string&,std::string&);
    bool advanceScreenAnimations(float,std::string&);
    bool screenFraction(int32_t,bool,float,const hades::BodyTransitionOptions&,std::string&);
    bool screenColor(int32_t,const std::array<float,4>&,bool,std::string&);
    bool screenDestroy(int32_t,std::string&);
    // Screen HUD actions: SetAnimationFrameTarget, SetScaleX/Y, FadeObstacleIn,
    // HideObstacle and the screen branch of Move (lua_screen_animation.cpp).
    static int screenAction(lua_State*);
    bool screenActionImpl(lua_State*,int,std::string&);
    bool screenMoveImpl(const hades::SourceValue&,bool& handled,std::string&);
    bool screenAnimationArgs(int32_t,const std::string&,lua_State*,std::string&);
    // CursorManager::Cursors[player] location written by TeleportCursor 0x25e3d0
    // (+0x14/+0x18 current, +0x1c previous x, +0xa ForceUseCheck). Android has
    // no free-form cursor consumer; the state is retained, not drawn.
    struct ScreenCursor{float x=0,y=0,previousX=0;bool forceUseCheck=false;} screenCursor_;

    bool setActorAnimation(int32_t,const std::string&,std::string&);
    float actorAnimationFacing(const hades::ActorComponents&)const;
    bool setActionPrimaryAnimation(int32_t,const std::string&,std::string&,bool copyPrevious=false);
    bool worldAnimationArgs(int32_t,const std::string&,lua_State*,std::string&);
    bool actorSlideEvent(int32_t,const hades::ActorAnimationClip&,const hades::ActorAnimationSlide&,std::string&);
    bool advanceActorAnimations(float,std::string&);
    bool advanceUnitActivations(std::string&);
    bool loadActorDefinitions();
    bool loadActorLightDefinitions();
    bool initializeActorLight(int32_t,std::string&);
    bool advanceActorLights(float,std::string&);
    bool loadPackageDefinitions();
    bool loadEffectDefinitions();
    bool effectOwner(int32_t,hades::EffectOwnerState&,std::string&)const;
    bool markThingDead(int32_t,bool,std::string&);
    bool advanceObstacleDisposal(std::string&);
    // ObstacleManager::Update 0x1802c80c0 UseOcclusion block (lua_occlusion.cpp).
    bool advanceObstacleOcclusion(std::string&);
    bool configOptionBool(const char*,bool&,std::string&);
    std::optional<std::vector<std::array<float,8>>> sceneCorners_;
    std::map<int32_t,uint64_t> occlusionBaseAnims_; // Thing+0x3d0 per obstacle
    bool advanceUnitDisposal(bool collectReady,std::string&);
    std::map<uint64_t,int32_t> retainedArsenalOwners_;
    bool retireActorArsenal(int32_t,uint64_t,std::string&);
    bool collectRetiredArsenals(std::string&);
    bool disposeThingStage(const hades::ActorDisposalEvent&,std::string&);
    bool locationAuxiliary(int32_t,hades::ActorLocationStage,std::string&);
    bool initializePrimarySpawner(int32_t,std::string&);
    bool loadScenePrimaryDefinitions();
    bool initializeScenePrimary(int32_t,std::string&);
    bool setScenePrimaryAnimation(int32_t,const std::string&,std::string&);
    bool stageUnitArsenals();
    using TriggerArguments=std::function<bool(lua_State*,std::string&)>;
    bool dispatchCallbacks(const char* event,const char* sourceName,int32_t actorId=0,const TriggerArguments& extra={},bool filterPlayerIndex=false);
    // Thing-sourced trigger with full Script::isSource RVA0x24ea10 matching
    // (map/Thing ID, group membership, GetName, _UNIT/_OBSTACLE) - lua_interaction.cpp.
    bool dispatchThingCallbacks(const char* event,int32_t actorId,const TriggerArguments& extra={});
    bool sourceTokenMatches(const std::string& token,const char* sourceName,int32_t actorId,std::string& error);
    bool thingSourceMatch_=false;
    void reportError(const char* stage);
    // Original ProgressManager save/load (lua_save.cpp, save_runtime.cpp;
    // native/analysis/save-contract.json).
    std::string saveDirectory_;
    std::string activeProfile_="Profile1";    // ProfileManager::getLastLoaded default
    std::string nextSaveName_,nextDevSaveName_;// ProgressManager::NEXT_SAVE_NAME / NEXT_DEV_SAVE_NAME
    bool saveOnLoad_=false;                    // LaunchScreen::s_SaveOnLoad
    bool validCheckpoint_=false;               // ConfigOptions::ValidCheckpoint
    std::string lastSaveError_,loadedSavePath_;
    void registerSaveActions();
    static int saveCheckpointAction(lua_State*);
    static int validateCheckpointAction(lua_State*);
    bool saveCheckpointImpl(lua_State*,std::string&);
    bool gatherSaveInfo(hades::save::SaveInfo&,std::string&);
    bool gatherLuaState(std::string&,std::string&);
    bool saveProfile(const std::string& name,const std::string& nextMap,std::string&);
    std::string savePath(const std::string& name,const char* extension)const;
public:
    hades::AssetStore& currentRoomAssets(){return *assets_;}
    const std::string& currentMapName()const{return mapName_;}
    uint64_t mapRevision()const{return mapRevision_;}
    bool loadTextDefinitions();
    const std::shared_ptr<hades::TextFontBank>& textFonts()const{return textFonts_;}
    void setControlLabelProvider(std::function<bool(const std::string&,std::string&,std::string&)>);
    bool advanceText(float worldElapsed,float screenElapsed,std::string&);
    bool consumeSpeechText(const std::vector<hades::SpeechPresentation>&,std::string&);
    std::vector<hades::TextVisual> textVisuals()const;
    std::vector<hades::SpeechPresentation> drainSpeechPresentation();
    struct VisualKey {enum class Kind {Actor,Auxiliary,Particle,Action,Screen,Light};Kind kind;int32_t owner=0;uint64_t handle=0;};
    explicit ScriptRuntime(hades::AssetStore*);
    ~ScriptRuntime();
    void initialize();
    void setAnimationBank(std::shared_ptr<hades::ActorAnimationBank> bank){if(!state_)animationBank_=std::move(bank);}
    bool mainLoaded() const { return mainLoaded_; }
    bool gameLoaded() const { return gameLoaded_; }
    bool bootRoom();
    bool activateRoom();
    // Lua-call containment wrappers (blocker_survey.cpp): host survey, and device durability.
    bool enableBlockerSurvey(std::string& error);
    std::string blockerSurveyReport()const;
    void surveyRecordWorldFault(); // records error_, clears the fault so the replay continues
    // Fault containment (blocker_survey.cpp). Off: a failing world stage faults
    // the world (strict, host default). On (device): the stage's error is logged
    // once and recorded, and the remaining stages still run. Missing behaviour
    // simply doesn't happen; nothing is substituted.
    void setFaultContainment(bool on){faultContainment_=on;}
    bool worldFaulted()const{return worldFaulted_;}
    // Host diagnostic: first active player unit's location, body angle and goal angle.
    std::string heroMotionTrace();
    // Host autoplay observation (native/host/autoplay.cpp): read-only view of
    // the original RequiredKillEnemies table and live interaction owners. Not compiled
    // into the Android engine.
    struct AutoplayTarget {int32_t id=0;float x=0,y=0;std::string name;};
    struct AutoplayView {bool hero=false;float x=0,y=0;bool inputAllowed=false;std::vector<AutoplayTarget> enemies,usables;std::string map;};
    AutoplayView autoplayView();
    size_t containedFaults()const{return survey_.size();}
    const std::string& lastContainedFault()const{return lastContained_;}
    bool advanceWorld(float dt,double screenDt);
    bool advanceVfx(float dt,std::string& error);
    std::vector<int32_t> actorLightOwners()const;
    bool sampleActorLight(int32_t,hades::ActorLightVisual&,std::string&);
    bool advancePhysics(float dt,std::string& error);
    bool advanceLocalPlayerInput(float,std::string&);
    bool advanceUnitMovement(float,std::string&);
    bool updateLocalMoveAnimation(int32_t,std::string&);

    void setPhysicsTouchdownWeapon(std::function<bool(int32_t,std::string&)> fn){physicsTouchdownWeapon_=std::move(fn);}
    bool permanentVfxCovered()const;
    hades::AudioOutput::Stats audioStats()const{return audioOutput_.stats();}
    // Android raw input pulse (touch/key down) for Player::HandleInput's any-input flag.
    void notePlayerInput(){rawInputPulse_=true;}
    bool applyConfigOptions(const std::vector<std::pair<std::string,bool>>& options);
    bool setAudioVolumes(float master,float music,float fx,float dialogue){return audio_.setCategoryVolumes(master,music,fx,dialogue,error_);}
    void setPhysicsRumble(std::function<bool(int32_t,float,float,std::string&)> fn){physicsRumble_=std::move(fn);}
    void setPhysicsGrip(std::function<bool(int32_t,float&,std::string&)> fn){physicsGrip_=std::move(fn);}
    bool scenePrimaryFrame(uint32_t,std::optional<hades::SceneFrameSelection>&,std::string&)const;
    bool scenePrimarySubmitted(uint32_t,std::string&);
    std::vector<uint64_t> scenePrimaryAuxiliaryHandles()const;
    const hades::ScenePrimaryState* scenePrimaryState(uint64_t)const;
    const hades::ScenePrimaryState* scenePrimaryStateForSprite(uint32_t)const;
    bool sampleScenePrimaryAuxiliary(uint64_t,hades::ActorVisual&,std::string&);
    bool sourceSceneSprite(uint32_t,hades::SceneSprite&,std::string&)const;
    std::vector<VisualKey> presentationVisuals()const;
    std::vector<int32_t> screenPresentationIds()const;
    bool sampleScreenPresentation(int32_t,hades::ActorVisual&,std::string&);
    bool samplePresentation(const VisualKey&,hades::ActorVisual&,std::string&);
    bool samplePresentationImpl(const VisualKey&,hades::ActorVisual&,std::string&);
    bool applyShakePresentation(const VisualKey&,hades::ActorVisual&,std::string&)const;
    bool serviceAssets(size_t textureBudget=1);
    bool assetsPending()const{return packages_.pending();}
    bool startAudio();
#ifndef __ANDROID__
    // Host integration replay consumes the genuine mixer output, allowing real
    // cue retirement/waiters without hardware playback or a synthetic timer.
    bool renderOfflineAudio(float* stereo,uint32_t frames,std::string& error){return audio_.render(stereo,frames,error);}
#endif
    void stopAudio();
    bool roomActivated()const{return roomActivated_;}
    bool fullscreenPresentation(uint32_t&group,bool&drawColor,bool&drawBloom,std::string&error)const;
    const hades::FullscreenBloom& bloom()const{return bloom_;}
    const hades::ColorGrading& colorGrading()const{return colorGrading_;}
    const std::array<float,4>& cameraFadeColor()const{return cameraFade_.color();}
    void registerCameraActions();
    bool cameraShakeOption(bool& option,std::string& error);
    void registerActorMotionActions();
    static int stopAnimation(lua_State*);
    bool stopAnimationImpl(lua_State*,std::string&);
    void registerProjectileActions();
    bool projectileOwnerMarker(int32_t owner,const std::string& marker,float& x,float& y,std::string&)const;
    bool advanceActorMotion(float elapsed,std::string&);
    bool advanceCamera(float elapsed,std::string& error);
    const hades::NativeCameraState& cameraState()const{return camera_.state();}
    bool setCameraViewport(uint32_t width,uint32_t height,std::string& error){return camera_.setViewport(width,height,error);}
    std::string actorsDump()const;
    bool roomBooted() const { return roomBooted_; }
    // Mac diagnostic replay only. Does not activate map Things, run OnSpawn,
    // mark Map::Begin complete or enable the production update loop.
    bool probeLoadCallbacks();
    // Source-backed component debug only. Does not dispatch EquipWeapon on
    // query proxies, activate units or establish combat execution.
    bool probeStagedWeaponInitialization();
    const std::string& error() const { return error_; }
    std::string sceneGroupsDump() const;
    std::string stateSummary() const;
    std::string objectsDump() const;
    std::string textFormatsDump() const;
    std::string inputDump() const;
    std::string mapCreationDump() const;
    std::string stagedWeaponsDump() const;
    bool inputAllowed(int playerIndex=1) const;
    bool setLocalInput(float dx,float dy,const std::vector<std::string>& actionDownNames,std::string&);
    std::optional<ObjectSnapshot> localPlayerBody()const;
    // Save API. The directory is the app-private save folder (Android:
    // internalDataPath based); without it saves are skipped like the original
    // "No save device found" path. Files: <profile><SaveName>.sav, .sav.bak,
    // <profile>.v.sav.
    void setSaveDirectory(std::string directory){saveDirectory_=std::move(directory);}
    const std::string& saveDirectory()const{return saveDirectory_;}
    // RemoteProfileScreen::ShouldLoadCheckPoint: Profile1.sav or Profile1_Temp.sav exists.
    bool hasSave()const;
    // GetPreferredSave + ProgressManager::Load/Restore/LoadLua. Call after
    // initialize() and before bootRoom(); runs Lua Load(data) and sets
    // _worldTime/_screenTime. Fails without touching Lua if the restore map is
    // not the loadable map.
    bool loadSave();
    const std::string& loadedSavePath()const{return loadedSavePath_;}
    // Invoke the original Lua-registered SaveCheckpoint action with an args
    // table (diagnostics and app code use the same path as scripts).
    bool invokeSaveCheckpoint(const std::string& saveName,const std::string& startNextMap,std::string& error);
    // World::Begin s_SaveOnLoad path: a SaveCheckpoint with StartNextMap is
    // deferred to the next map begin. Room transitions must call this.
    bool saveOnLoadPending()const{return saveOnLoad_;}
    bool performSaveOnLoad(const std::string& nextMap);
    const std::string& lastSaveError()const{return lastSaveError_;}
    // luabins bytes of the current Lua save table (calls the original Save()).
    bool serializeLuaState(std::string& bytes,std::string& error){return gatherLuaState(bytes,error);}
};
