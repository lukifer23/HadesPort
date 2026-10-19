#include "lua_runtime.hpp"
#include <cstdio>
#include <cmath>
#include <stdexcept>
#include "object_runtime.hpp"
#include "recovered_runtime.hpp"
#include "platform_log.hpp"
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
float ScriptRuntime::actorAnimationFacing(const hades::ActorComponents& actor)const{
    // BookAnimation::GetAngleNum VA1802b0652..666: a Unit owner supplies
    // mCosmeticAngle(+3d0), whereas an ordinary Thing supplies body.angle.
    const auto move=localMovement_.find(actor.entityHandle);
    if(actor.definition&&actor.definition->kind=="Unit"&&move!=localMovement_.end()&&move->second.initialized)return move->second.cosmeticAngle;
    return actor.object?actor.object->angle:0;
}
bool ScriptRuntime::primaryCombatFlags(int32_t id,bool& invulnerable,bool& noArmor,bool& noCoverage,std::string& error)const{
    const auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object){error="Actual primary combat owner unavailable";return false;}
    if(actor->animation.active()){
        const auto clip=actor->animation.clipIndex();const auto a=animationBank_->exactOwnerInvulnerable(clip),b=animationBank_->exactOwnerNoArmorCoverage(clip),c=animationBank_->exactOwnerNoInvulnerableCoverage(clip);
        if(!a||!b||!c){error="Actual HAA combat properties unavailable for "+actor->animation.clip()->name;return false;}invulnerable=*a;noArmor=*b;noCoverage=*c;error.clear();return true;
    }
    if(const auto* primary=scenePrimaries_.state(actor->primaryAnimationLifetime)){
        invulnerable=primary->ownerInvulnerable;noArmor=primary->ownerNoArmorCoverage;noCoverage=primary->ownerNoInvulnerableCoverage;error.clear();return true;
    }
    if(const auto* primary=actionEffects_.state(actor->primaryAnimationLifetime);primary&&primary->primary){
        // Same AnimationData zero template as the effect primary below.
        const auto flag=[&](const char* key){const auto* value=primary->definition.get(key);return value&&value->boolean();};
        invulnerable=flag("OwnerInvulnerable");noArmor=flag("OwnerNoArmorCoverage");noCoverage=flag("OwnerNoInvulnerableCoverage");error.clear();return true;
    }
    if(const auto* primary=effects_.entity(actor->primaryEffectAnimation)){
        // Exact matching AnimationData ctor zero template has no later stores
        // to OwnerInvulnerable/OwnerNoArmorCoverage/OwnerNoInvulnerableCoverage.
        const auto flag=[&](const char* key){const auto* value=primary->definition->source.get(key);return value&&value->boolean();};
        invulnerable=flag("OwnerInvulnerable");noArmor=flag("OwnerNoArmorCoverage");noCoverage=flag("OwnerNoInvulnerableCoverage");error.clear();return true;
    }
    if(actor->object->animation.empty()){invulnerable=noArmor=noCoverage=false;error.clear();return true;}
    error="Actual primary combat resource/lifetime unavailable: "+actor->object->animation;return false;
}
bool ScriptRuntime::retirePrimaryAnimation(uint64_t handle,std::string& error){
    if(!handle)return true;
    if(scenePrimaries_.state(handle))return scenePrimaries_.destroy(handle,error);
    if(effects_.entity(handle))return effects_.destroy(handle,error);
    if(const auto* action=actionEffects_.state(handle);action&&action->primary)return actionEffects_.destroy(handle,error);
    if(actionEffects_.retiredPrimary(handle))return true; // finished on its own (Constant duration)
    const auto* lifetime=effects_.registry()->find(handle);
    if(!lifetime||lifetime->ownership!=hades::AnimationOwnership::ThingPrimary){error="Actual primary animation retirement owner unavailable";return false;}
    return effects_.registry()->cancel(handle,error)&&effects_.registry()->dispose(handle,error);
}
bool ScriptRuntime::loadActorAnimationDefinitions(){
    auto file=assets_->open("actor-animation-events.lua");if(!file||file->size()>4*1024*1024){error_="Missing source actor animation definitions";return false;}
    std::vector<char> bytes(file->size());size_t at=0;while(at<bytes.size()){auto n=file->read(bytes.data()+at,bytes.size()-at);if(!n){error_="Truncated actor animation definitions";return false;}at+=n;}
    const int top=lua_gettop(state_);int status=luaL_loadbufferx(state_,bytes.data(),bytes.size(),"@actor-animation-events.lua","t");if(status==LUA_OK)status=lua_pcall(state_,0,1,0);
    if(status!=LUA_OK){error_=lua_tostring(state_,-1);lua_settop(state_,top);return false;}
    hades::SourceValue data;bool ok=hades::readSourceValue(state_,-1,data,error_);lua_settop(state_,top);if(!ok)return false;
    try{
        if(!data.get("schema")||data.get("schema")->text()!="hades-actor-animation-events-v1")throw std::runtime_error("Actor animation definitions schema");
        auto identity=assets_->open("actor-animations-sha256.bin");std::array<unsigned char,32> hash{};
        if(!identity||identity->size()!=hash.size()||identity->read(hash.data(),hash.size())!=hash.size())throw std::runtime_error("Actor animation identity unavailable");
        std::string hex;for(auto byte:hash){char pair[3];std::snprintf(pair,sizeof(pair),"%02x",unsigned(byte));hex+=pair;}
        if(!data.get("bank_sha256")||data.get("bank_sha256")->text()!=hex)throw std::runtime_error("Actor event definitions differ from active bank");
        const auto* clips=data.get("clips");if(!clips)throw std::runtime_error("Actor event definitions missing clips");
        for(const auto& clip:animationBank_->clips())if(!clips->get(clip.name))throw std::runtime_error("Source animation event definition absent: "+clip.name);
        actorAnimationDefinitions_=clips->object();
        setObjectWorldAnimationArgs(state_,[this](int32_t id,const std::string& name,lua_State* L,std::string& e){return worldAnimationArgs(id,name,L,e);});
        return loadActionEffectDefinitions()&&loadScreenAnimationDefinitions()&&loadTextDefinitions();
    }catch(const std::exception& e){error_=e.what();return false;}
}
bool ScriptRuntime::actorSlideEvent(int32_t id,const hades::ActorAnimationClip& clip,const hades::ActorAnimationSlide& slide,std::string& error){
    const auto definition=actorAnimationDefinitions_.find(clip.name);if(definition==actorAnimationDefinitions_.end()){error="Actor source definition unavailable";return false;}
    const auto* frames=definition->second.get("frames");const auto* events=frames?frames->get(std::to_string(slide.originalFrame)):nullptr;
    if(!events||events->object().empty())return true;
    // Native SlideStart2b40e7 tests RandomDurationFramesMin>0; RandomInt
    //291250 uses the engine PCG stream, upper-exclusive, no draw if equal.
    // These timing fields are source operations, not sound/FX events.
    if(!(clip.flags&hades::ActorBook))if(const auto* minimum=events->get("RandomDurationFramesMin")){
        const double lo=minimum->number();const auto* maximum=events->get("RandomDurationFramesMax");const double hi=maximum?maximum->number():0;
        if(!std::isfinite(lo)||!std::isfinite(hi)||lo<0||hi<lo||hi>100000||lo!=std::trunc(lo)||hi!=std::trunc(hi)){error="Original random slide duration integer bounds";return false;}
        if(lo>0){uint32_t draw=0;if(hi!=lo&&!nextRecoveredEngineBoundedRandom(state_,uint32_t(hi-lo),draw)){error="Original random slide duration RNG unavailable";return false;}auto* actor=actorWorld_.actor(id);if(!actor){error="Random Slide duration owner unavailable";return false;}actor->animation.replaceCurrentSlideTicks(uint32_t(lo)+draw);}
    }
    // Validate every operation before consuming the first event. Native source
    // FX allocation then owns its real marker/time/children, not metadata ack.
    for(const auto&[key,value]:events->object()){
        if(key=="FootstepFxR"||key=="FootstepFxL"||key=="VisualFx"){if(!prepareActionEffect(value.text(),id,error))return false;}
        else if(key!="Sound"&&key!="FootstepSound"&&key!="RandomDurationFramesMin"&&key!="RandomDurationFramesMax"){error="Unsupported actual source slide event: "+key;return false;}
    }
    for(const auto* key:{"FootstepSound","FootstepFxL","FootstepFxR","Sound","VisualFx"}){
        const auto* value=events->get(key);if(!value)continue;const auto& name=value->text();if(name.empty()||name=="null")continue;
        if(std::string_view(key)=="Sound"||std::string_view(key)=="FootstepSound"){
            std::array<float,3> position{};if(!actorWorld_.location(id,position[0],position[1],error)||!actorWorld_.zLocation(id,position[2],error))return false;uint32_t sound=0;if(!playAudioCue(name,id,position,sound,error))return false;
        }else{uint64_t animation=0;if(!createActionEffect(name,id,std::nullopt,animation,error))return false;}
    }
    return true;
}
bool ScriptRuntime::setActorAnimation(int32_t id,const std::string& name,std::string& error){
    try{
        if(auto* screen=findObjectSnapshot(state_,id);screen&&screen->screenSpace)return setScreenAnimation(id,name,error);
        auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object){error="SetAnimation requires actual registered Thing";return false;}
        // Source data spells "no animation" as the literal "null" (e.g. weapon
        // owner animations); it has no AnimationData definition, as with "".
        if(name.empty()||name=="null")return true;
        const auto target=animationBank_->find(name);if(!target){
            if(scenePrimaries_.recipe(id)||scenePrimaries_.admitsDynamic(name))return setScenePrimaryAnimation(id,name,error);
            if(actionEffects_.admits(name))return setActionPrimaryAnimation(id,name,error);
            const auto* why=actionEffects_.rejection(name);
            error="Original animation requires an actual exported primary/resource consumer: "+name+(why?" ("+*why+")":"");return false;
        }
        if(actor->animation.active()){
            const auto block=animationBank_->exactBlockNewOwnerAnims(actor->animation.clipIndex());if(!block){error="SetAnimation requires current primary BlockNewOwnerAnims";return false;}
            if(*block)return true;
            if(actor->animation.clip()->name==name&&actor->primaryAnimationLifetime){
                actor->animation.restartSource([&](const auto& clip,const auto& slide){if(!actorSlideEvent(id,clip,slide,error))throw std::runtime_error(error);});
                if(actor->primaryAnimationLifetime&&!effects_.registry()->restart(actor->primaryAnimationLifetime,error))return false;
                actor->object->graphicsDirty=true;return true;
            }
        }
        const auto record=actorAnimationDefinitions_.find(name);if(record==actorAnimationDefinitions_.end()){error="Source animation definition unavailable: "+name;return false;}
        // Animation.InitData291c70 performs source CreateAnimation and the
        // ordered additive CreateAnimations list before Slide.Init SetupFX.
        // These are independent manager-created animations attached to the
        // actual Thing, not synthetic child images of the hero resource.
        const auto& source=*record->second.get("definition");std::vector<std::string> creates;
        if(const auto* create=source.get("CreateAnimation")){const auto& value=create->text();if(!value.empty()&&value!="null")creates.push_back(value);}
        if(const auto* list=source.get("CreateAnimations"))for(const auto& entry:list->array()){const auto* create=entry.get("Name");if(!create){error="Source HAA CreateAnimations entry has no Name";return false;}const auto& value=create->text();if(!value.empty()&&value!="null")creates.push_back(value);}
        if(creates.size()>64){error="Source HAA init create-list bound";return false;}
        for(const auto& create:creates)if(!prepareActionEffect(create,id,error))return false;
        hades::AnimationAllocation allocation;allocation.name=name;allocation.ownerId=id;allocation.ownerLifetime=actor->entityHandle;
        allocation.ownership=hades::AnimationOwnership::ThingPrimary;allocation.sourceDefinition=*record->second.get("definition");
        const auto& clip=animationBank_->clips().at(*target);allocation.type=(clip.flags&hades::ActorBook)?hades::NativeAnimationType::Book:hades::NativeAnimationType::Slide;allocation.loop=clip.flags&hades::ActorLoop;
        // Matching compiled zero-template defaults and no overrides in the
        // selected29 hero definitions: OnlyWhenVisible=false/CancelOverCap=false.
        allocation.onlyWhenVisible=false;allocation.cancelOverCap=false;
        uint64_t handle=0;if(!effects_.registry()->allocate(allocation,handle,error))return false;
        for(const auto& create:creates){uint64_t created=0;if(!createActionEffect(create,id,std::nullopt,created,error))return false;}
        actor->animation.play(*animationBank_,name,[&](const auto& c,const auto& s){if(!(c.flags&hades::ActorBook)&&!actorSlideEvent(id,c,s,error))throw std::runtime_error(error);});
        if(!effects_.registry()->commitInitialization(handle,false,error))return false;
        if(!retirePrimaryAnimation(actor->primaryAnimationLifetime,error)||!retirePrimaryAnimation(actor->primaryEffectAnimation,error))return false;
        actor->primaryEffectAnimation=0;
        actor->primaryAnimationLifetime=handle;actor->object->animation=name;actor->object->graphicsDirty=true;
        actor->object->frame=actor->animation.originalFrame();actor->object->numFrames=uint32_t(clip.slides.size());
        // Unit inherits the base OnAnimationSet no-op (matched DLL RVA2c730).
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool ScriptRuntime::advanceActorAnimations(float dt,std::string& error){
    try{
        for(auto id:actorWorld_.registrationOrder()){
            auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->animation.active())continue;
            if(actor->timeModifierFraction!=1||actor->timeMultiplier!=1){error="Actor animation requires non-default source time adapter";return false;}
            // Unit::MoveInput VA1802f6220 writes PlaySpeedMultiplier only on
            // accepted movement input. Recomputing it here from zero velocity
            // freezes NativeMoveSpeed stop animations at their first frame.
            const auto chain=actor->animation.advance(dt,actor->animation.playSpeedMultiplier(),[&](const auto& clip,const auto& slide){if(!actorSlideEvent(id,clip,slide,error))throw std::runtime_error(error);});
            if(chain&&!setActorAnimation(id,*chain,error))return false;
            // Unit::UpdateAnimation VA1802f1a80 advances first, then handles
            // completion and movement cancellation on the resulting primary.
            if(!updateLocalMoveAnimation(id,error))return false;
            actor->object->frame=actor->animation.originalFrame();actor->object->graphicsDirty=true;
        }
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}

// Thing::SetAnimation with an exported action-bank definition (e.g. the
// CreateDoorRewardPreview door icons on spawned BlankGeoObstacle Things):
// a ThingPrimary Animation owned by the Thing, replacing its previous primary.
bool ScriptRuntime::setActionPrimaryAnimation(int32_t id,const std::string& name,std::string& error){
    auto* actor=actorWorld_.actor(id);if(!actor||!actor->registered||!actor->object){error="SetAnimation requires actual registered Thing";return false;}
    if(actor->animation.active()){error="Action-bank primary on an HAA-animated Thing not reconstructed: "+name;return false;}
    if(const auto* current=actionEffects_.state(actor->primaryAnimationLifetime);current&&current->primary&&current->name==name)return true;
    uint64_t handle=0;if(!actionEffects_.createPrimary(name,id,actionEffectProviders(),handle,error))return false;
    if(!retirePrimaryAnimation(actor->primaryAnimationLifetime,error)||!retirePrimaryAnimation(actor->primaryEffectAnimation,error))return false;
    actor->primaryEffectAnimation=0;actor->primaryAnimationLifetime=handle;actor->object->animation=name;actor->object->graphicsDirty=true;
    if(const auto* pending=actionEffects_.pendingFields(name)){std::string list;for(const auto& f:*pending)list+=(list.empty()?"":",")+f;hades::log(hades::Severity::Info,"HadesLua","NATIVE_ACTION_FX_PENDING_PRESENTATION name=%s fields=%s",name.c_str(),list.c_str());}
    return true;
}
// ScriptAction::SetAnimation 0x180261a10 optional arguments on a world Thing.
bool ScriptRuntime::worldAnimationArgs(int32_t id,const std::string& name,lua_State* L,std::string& error){
    try{
        const auto number=[&](const char* key,double fallback){lua_getfield(L,1,key);int ok=0;const double v=lua_tonumberx(L,-1,&ok);lua_pop(L,1);if(ok&&!std::isfinite(v))throw std::runtime_error(std::string("SetAnimation scalar: ")+key);return ok?v:fallback;};
        const auto flag=[&](const char* key,bool fallback){lua_getfield(L,1,key);const bool v=lua_isnil(L,-1)?fallback:lua_toboolean(L,-1);lua_pop(L,1);return v;};
        const auto has=[&](const char* key){lua_getfield(L,1,key);const bool v=!lua_isnil(L,-1);lua_pop(L,1);return v;};
        if(flag("SuppressSounds",false)||!flag("SuppressSoundsIfInvisible",true)){error="SetAnimation SuppressSounds options not reconstructed: "+name;return false;}
        auto* actor=actorWorld_.actor(id);if(!actor||!actor->object){error="SetAnimation optional arguments require actual Thing";return false;}
        if(actor->object->animation!=name)return true; // Thing::SetAnimation kept the old primary (blocked): options skipped
        auto* action=actionEffects_.state(actor->primaryAnimationLifetime);if(action&&!action->primary)action=nullptr;
        const auto need=[&](const char* what){if(action)return true;error=std::string("SetAnimation ")+what+" on a non-action primary not reconstructed: "+name;return false;};
        if(number("MinDuration",0)!=0&&number("MaxDuration",0)!=0){error="SetAnimation MinDuration/MaxDuration use thread-local sgg::Random (not reconstructed)";return false;}
        if(const double d=number("Duration",0);d>0&&(!need("Duration")||!actionEffects_.setDuration(actor->primaryAnimationLifetime,float(d),error)))return false;
        const double x=number("OffsetX",0),y=number("OffsetY",0);
        if(x!=0||y!=0){if(!need("OffsetX/OffsetY"))return false;const auto* m=action->definition.get("Mirror");if(m&&m->boolean()){error="SetAnimation OffsetX on a Mirror primary not reconstructed: "+name;return false;}if(!actionEffects_.setAttachOffset(actor->primaryAnimationLifetime,float(x),float(y),error))return false;}
        if(const double scale=number("Scale",1);scale!=1&&(!need("Scale")||!actionEffects_.setScale(actor->primaryAnimationLifetime,float(scale),error)))return false;
        if(number("PlaySpeed",1)!=1||number("StartFrameFraction",0)!=0||number("FrameTarget",-1)>=0||has("Color")||number("SoundId",0)!=0){error="SetAnimation PlaySpeed/StartFrameFraction/FrameTarget/Color/SoundId on a world Thing not reconstructed: "+name;return false;}
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
