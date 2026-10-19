#include "lua_runtime.hpp"
#include "recovered_runtime.hpp"
#include <cmath>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
bool ScriptRuntime::scenePrimaryFrame(uint32_t index,std::optional<hades::SceneFrameSelection>& out,std::string& error)const{return scenePrimaries_.selectSceneFrame(index,out,error);}
bool ScriptRuntime::scenePrimarySubmitted(uint32_t index,std::string& error){return scenePrimaries_.submitted(index,error);}
std::vector<uint64_t> ScriptRuntime::scenePrimaryAuxiliaryHandles()const{return scenePrimaries_.auxiliaryHandles();}
const hades::ScenePrimaryState* ScriptRuntime::scenePrimaryState(uint64_t handle)const{return scenePrimaries_.state(handle);}
const hades::ScenePrimaryState* ScriptRuntime::scenePrimaryStateForSprite(uint32_t index)const{return scenePrimaries_.stateForSprite(index);}
bool ScriptRuntime::sampleScenePrimaryAuxiliary(uint64_t handle,hades::ActorVisual& out,std::string& error){return scenePrimaries_.sampleAuxiliary(handle,out,error);}
bool ScriptRuntime::loadScenePrimaryDefinitions(){
 try{
    auto file=assets_->open("primaries.lua");if(!file||file->size()>4*1024*1024){error_="Original scene primary source catalog unavailable";return false;}
    std::vector<char> bytes(file->size());size_t at=0;while(at<bytes.size()){auto n=file->read(bytes.data()+at,bytes.size()-at);if(!n)throw std::runtime_error("Truncated scene primary catalog");at+=n;}
    const int top=lua_gettop(state_);auto status=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@primaries.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);
    if(status!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    hades::SourceValue catalog;const bool valid=hades::readSourceValue(state_,-1,catalog,error_);lua_settop(state_,top);if(!valid||!scenePrimaries_.load(*assets_,std::move(catalog),effects_.registry(),error_))return false;
    for(const auto& object:mapMetadata_)if(const auto* recipe=scenePrimaries_.recipe(object.id))for(auto sprite:recipe->sceneSprites)if(!sceneSpriteOwners_.emplace(sprite,object.id).second){error_="Duplicate original scene sprite owner";return false;}
    return true;
 }catch(const std::exception& e){error_=e.what();return false;}
}
bool ScriptRuntime::initializeScenePrimary(int32_t id,std::string& error){
    auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object){error="Actual scene primary actor unavailable";return false;}
    const auto* recipe=scenePrimaries_.recipe(id);if(!recipe){error="Original scene primary actor recipe unavailable";return false;}
    if(recipe->externalHaa){if(!actor->primaryAnimationLifetime){error="Actual source HAA primary must initialize before its OnSpawn";return false;}return true;}
    if(hades::ScenePrimaryWorld::externalEffect(recipe->graphic)){
        if(!actor->primaryEffectAnimation){error="Actual source VFX primary must initialize before its OnSpawn";return false;}
        actor->primaryOwnerHasNoCollision=false;return true;
    }
    if(actor->primaryAnimationLifetime){error="Duplicate actual scene primary initialization";return false;}
    hades::EffectOwnerState owner;if(!effectOwner(id,owner,error))return false;
    const hades::EffectAnimationRandom random=[this](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value))return true;detail="Actual scene primary uniform RNG unavailable";return false;};
    const hades::EffectBoundedRandom integers=[this](uint32_t bound,uint32_t& value,std::string& detail){if(nextRecoveredEngineBoundedRandom(state_,bound,value))return true;detail="Actual scene primary bounded PCG unavailable";return false;};
    uint64_t handle=0;if(!scenePrimaries_.create(id,actor->object->animation,owner,random,integers,handle,error))return false;
    const auto* source=scenePrimaries_.state(handle);actor->primaryAnimationLifetime=handle;actor->primaryOwnerHasNoCollision=source->ownerHasNoCollision;actor->primaryOwnerImmobile=source->ownerImmobile;actor->primaryDisableWeapons=source->disableWeapons;actor->primaryAirborne=false;
    actor->object->animationScale=source->ownScale;actor->object->animationOffsetX=source->offsetX;actor->object->animationOffsetY=source->offsetY;
    return true;
}
bool ScriptRuntime::setScenePrimaryAnimation(int32_t id,const std::string& name,std::string& error){
    auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object){error="Actual scene primary SetAnimation actor unavailable";return false;}
    hades::EffectOwnerState owner;if(!effectOwner(id,owner,error))return false;
    const hades::EffectAnimationRandom random=[this](double& value,std::string& detail){if(nextRecoveredEngineRandom(state_,value))return true;detail="Actual source SetAnimation uniform RNG unavailable";return false;};
    const hades::EffectBoundedRandom integers=[this](uint32_t bound,uint32_t& value,std::string& detail){if(nextRecoveredEngineBoundedRandom(state_,bound,value))return true;detail="Actual source SetAnimation bounded PCG unavailable";return false;};
    uint64_t handle=0;
    if(scenePrimaries_.state(actor->primaryAnimationLifetime)){if(!scenePrimaries_.replace(actor->primaryAnimationLifetime,name,owner,random,integers,handle,error))return false;}
    else{
        if(!scenePrimaries_.create(id,name,owner,random,integers,handle,error))return false;
        if(!retirePrimaryAnimation(actor->primaryAnimationLifetime,error))return false;
    }
    if(!retirePrimaryAnimation(actor->primaryEffectAnimation,error))return false;
    actor->animation.reset();actor->primaryEffectAnimation=0;
    const auto* source=scenePrimaries_.state(handle);if(!source){error="New actual scene primary source state unavailable";return false;}
    actor->primaryAnimationLifetime=handle;actor->primaryOwnerHasNoCollision=source->ownerHasNoCollision;actor->primaryOwnerImmobile=source->ownerImmobile;actor->primaryDisableWeapons=source->disableWeapons;actor->primaryAirborne=false;actor->object->animation=source->name;
    actor->object->animationScale=source->ownScale;actor->object->animationOffsetX=source->offsetX;actor->object->animationOffsetY=source->offsetY;actor->object->graphicsDirty=true;
    return true;
}
