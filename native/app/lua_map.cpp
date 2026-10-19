#include "lua_runtime.hpp"
#include "delayed_actions.hpp"
#include "input_runtime.hpp"
#include "text_runtime.hpp"
#include "platform_log.hpp"
#include <memory>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
template<class T,class... Args>void reconstruct(T& value,Args&&...args){std::destroy_at(&value);std::construct_at(&value,std::forward<Args>(args)...);}
std::vector<uint8_t> readRequired(hades::AssetStore&assets,const char*name,size_t limit){auto file=assets.open(name);if(!file||file->size()>limit)throw std::runtime_error(std::string("Map resource missing/oversized: ")+name);std::vector<uint8_t> bytes(file->size());size_t at=0;while(at<bytes.size()){auto n=file->read(bytes.data()+at,bytes.size()-at);if(!n)throw std::runtime_error(std::string("Truncated map resource: ")+name);at+=n;}return bytes;}
}
void ScriptRuntime::registerMapActions(){lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,loadMapAction,1);lua_setglobal(state_,"LoadMapInternal");}
int ScriptRuntime::loadMapAction(lua_State*L){char message[2048]{};bool ok=false;{std::string error;auto*self=runtime(L);ok=self&&self->loadMapActionImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}if(!ok)return luaL_error(L,"%s",message);return 0;}
bool ScriptRuntime::loadMapActionImpl(lua_State*L,std::string&error){try{
 // ScriptAction::LoadMap 0x265950 -> World::RequestBegin 0x114340.
 // Only request state is mutated here. Main.LoadMap's genuine Lua wait yields
 // until the next World boundary; never replace its coroutine or CurrentRun.
 hades::SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;
 const auto flag=[&](const char*key){const auto*p=args.get(key);return p&&p->boolean();};
 if(flag("ResetApp"))throw std::runtime_error("LoadMap ResetApp requires full application/profile reset");
 const auto*names=args.get("Names");if(!names)names=args.get("Name");if(!names)throw std::runtime_error("LoadMap requires actual map Name/Names");
 std::string name;if(std::holds_alternative<std::string>(names->value))name=names->text();else{const auto&list=names->array();if(list.empty()){error.clear();return true;}name=list.front().text();}
 if(!hades::validMapName(name))throw std::runtime_error("LoadMap has an invalid map identifier");
 MapRequest request;request.name=name;request.resetBinks=flag("ResetBinks");request.resetWeaponBinks=flag("ResetWeaponBinks");
 if(const auto*color=args.get("LoadBackgroundColor")){std::vector<double>channels;for(const auto&v:color->array())channels.push_back(v.number());request.background=hades::textSourceColor(channels);}
 pendingMap_=std::move(request);
 hades::log(hades::Severity::Info,"HadesLua","NATIVE_MAP_REQUEST from=%s to=%s frame=%llu begin_type=%d",mapName_.c_str(),name.c_str(),(unsigned long long)worldFrame_,name==mapName_?2:1);
 error.clear();return true;
 }catch(const std::exception&e){error=e.what();return false;}}
bool ScriptRuntime::beginRequestedMap(bool restoreAssetsOnly){try{
 if(!pendingMap_)return true;const MapRequest request=*pendingMap_;pendingMap_.reset();
 // Validate actual next-room files before retiring the old map. A scoped view
 // never falls back to the opening scene, even for an unimplemented map.
 // Probe the selected persistent view temporarily, restoring it before reset/save.
 if(!roomAssets_->selectMap(request.name))throw std::runtime_error("Map request name rejected");
 for(const auto name:hades::kRoomBoundAssets)if(!assets_->open(name)){roomAssets_->selectMap(mapName_);throw std::runtime_error("Actual room asset unavailable: "+request.name+"/"+std::string(name));}
 roomAssets_->selectMap(mapName_);
 const std::string previous=mapName_;
 hades::log(hades::Severity::Info,"HadesLua","NATIVE_MAP_BEGIN from=%s to=%s frame=%llu lua_retained=true clocks_retained=true",previous.c_str(),request.name.c_str(),(unsigned long long)worldFrame_);
 // World::Begin 0x318897/8a8 resets the requested movie-cache categories.
 if(request.resetWeaponBinks)movies_.reset(hades::MovieCacheKind::Weapon);
 if(request.resetBinks)movies_.reset(hades::MovieCacheKind::Stream);
 // World::Reset(false) 0x315f60 clears actual Things, grids, pending native
 // events and animation/light managers. LuaInterface and the original Lua
 // scheduler survive; Main.LoadMap already queued its tagged-thread kills.
 for(const auto&[index,key]:useTargets_)if(key.id>=0&&findObjectSnapshot(state_,key.id)){
  int32_t user=-1;for(const auto&p:snapshotInputPlayers(state_))if(p.playerIndex==index)user=p.activeUnitId;
  hades::InteractionSnapshot interaction;const bool known=interactions_.snapshot(key,interaction,error_);bool automatic=false;if(known)if(const auto*p=interaction.definition.get("AutoActivate"))automatic=p->boolean();
  if(!dispatchThingCallbacks("OnActiveUseTargetLost",key.id,[key,user,automatic](lua_State*L,std::string&){if(user>=0){lua_pushinteger(L,user);lua_setfield(L,-2,"UserId");}pushObjectLuaTable(L,key.id);lua_setfield(L,-2,"AttachedTable");lua_pushboolean(L,automatic);lua_setfield(L,-2,"AutoActivate");return true;}))return false;
 }
 if(!audio_.stopAllGameSounds(error_))return false;
 // World::Reset 0x31680c..814 clears Player's active Unit/entity and use
 // target while preserving the player slot, Lua and named input blocks.
 for(const auto&player:snapshotInputPlayers(state_))if(!bindRecoveredPlayerUnit(state_,player.playerIndex,0,0,error_))return false;
 delayedSounds_.clear();audioCallbacks_.clear();audioFramePlays_=0;reconstruct(speech_,audio_);initializeSpeechBridge();captionText_={};
 for(const auto&notify:worldNotifies_)if(notify.arguments>0)luaL_unref(state_,LUA_REGISTRYINDEX,notify.arguments);worldNotifies_.clear();
 if(!clearDelayedActions(state_,error_))return false;
 for(const auto&object:snapshotObjects(state_))disposeTextOwner(state_,object.id);
 reconstruct(screenAnimations_);reconstruct(scenePrimaries_);reconstruct(actionEffects_);reconstruct(particles_);reconstruct(effects_);reconstruct(actorLights_,actorWorld_);reconstruct(lifeSystem_,actorWorld_);reconstruct(selfEffects_,actorWorld_);reconstruct(projectileWorld_);
 if(!actorWorld_.resetForMap(error_)||!resetObjectRuntimeForMap(state_,error_))return false;
 actorMotion_={};localMovement_.clear();retainedArsenalOwners_.clear();projectileFxOwners_.clear();projectileFxOwnerIds_.clear();nextProjectileFxOwner_=-0x1000000;
 interactions_={};useTargets_.clear();interactionGridReady_=false;previousUseDown_=false;interactUserPrimaryUnknown_=false;
 presentationBases_.clear();sceneSpriteOwners_.clear();originalSceneGroupNames_.clear();presentationGroupFrame_=UINT64_MAX;presentationGroups_.clear();presentationGroupIndices_.clear();presentationShadowFrame_=UINT64_MAX;presentationShadows_.reset();
 primarySpawnerRecipes_.clear();primarySpawnerGraphics_.clear();sceneCorners_.reset();occlusionBaseAnims_.clear();unitCollisionSets_.clear();polygonAxisCache_.fill(0);lastLocationBlocker_=0;
 inactiveMap_.clear();mapMetadata_.clear();creationPlan_={};stagedUnitArsenals_.clear();roomBooted_=roomActivated_=activationStarted_=mapProbeInstalled_=false;physicsFaulted_=false;physicsFault_.clear();
 const auto viewport=camera_.state();camera_={};if(!camera_.setViewport(viewport.viewportWidth,viewport.viewportHeight,error_))return false;cameraFade_={};if(!bloom_.resetOff(error_))return false;
 // World::Begin 0x318b4f saves before LoadNextMap changes the active map name;
 // SaveInfo retains the preceding map and StartNextMap names the new room.
 // 0x318b4f calls ProgressManager::Save and does not branch on its result.
 // Keep the real save failure log/lastSaveError (including absent host save
 // device), then continue loading just as the native World::Begin does.
 if(!restoreAssetsOnly)(void)performSaveOnLoad(request.name);
 mapName_=request.name;if(!roomAssets_->selectMap(mapName_))throw std::runtime_error("Map view selection failed");++mapRevision_;
 const auto groups=readRequired(*assets_,"groups.bin",16*1024*1024);if(!loadSceneGroupsSGP1(state_,groups,error_))return false;
 for(const auto&group:snapshotSceneGroups(state_))originalSceneGroupNames_.push_back(group.name);
 if(!loadMapObjects()||!loadActorDefinitions()||!loadProjectileDefinitions()||!loadEffectDefinitions()||!loadActorLightDefinitions()||!loadScenePrimaryDefinitions()||!loadActionEffectDefinitions()||!loadScreenAnimationDefinitions())return false;
 lua_gc(state_,LUA_GCCOLLECT,0); // World::Begin 0x318d26 retains the Lua VM.
 // ProgressManager::Restore selects nextMap/currentMap before Lua Load and
 // OnPreThingCreation. Restore resource selection must neither run StartRoom
 // with the old Lua state nor save over the checkpoint being restored.
 if(restoreAssetsOnly){hades::log(hades::Severity::Info,"HadesLua","NATIVE_MAP_RESTORE_ASSETS map=%s revision=%llu callbacks_pending=true",mapName_.c_str(),(unsigned long long)mapRevision_);return true;}
 if(!bootRoom()||!activateRoom())return false;
 hades::log(hades::Severity::Info,"HadesLua","NATIVE_MAP_TRANSITION_COMPLETE from=%s to=%s revision=%llu lua_retained=true world_time=%.6f screen_time=%.6f",previous.c_str(),mapName_.c_str(),(unsigned long long)mapRevision_,worldTime_,screenTime_);
 return true;
 }catch(const std::exception&e){error_=e.what();return false;}}
