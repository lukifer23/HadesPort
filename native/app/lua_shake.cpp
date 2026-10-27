#include "lua_runtime.hpp"
#include "scene_renderer.hpp"
#include "recovered_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
namespace {
uint32_t locationMode(const hades::SourceValue&definition){const auto*p=definition.get("LocationFromOwner");if(!p)return 0;if(std::holds_alternative<std::string>(p->value)){const auto value=p->text();if(value=="Maintain")return 0;if(value=="Take")return 1;if(value=="Ignore")return 2;throw std::runtime_error("Unknown native LocationFromOwner enum");}const double n=p->number();if(n<0||n>2||n!=std::trunc(n))throw std::runtime_error("Invalid native LocationFromOwner enum");return uint32_t(n);}
bool permitsShake(const hades::SourceValue&definition){const auto*p=definition.get("UseAttachedShake");return (!p||p->boolean())&&locationMode(definition)==0;}
}
void ScriptRuntime::registerShakeAction(){lua_pushlightuserdata(state_,this);lua_pushcclosure(state_,shakeAction,1);lua_setglobal(state_,"Shake");}
int ScriptRuntime::shakeAction(lua_State*L){char message[2048]{};bool ok=false;{try{std::string error;auto*self=runtime(L);ok=self&&self->shakeActionImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}catch(const std::exception&e){std::snprintf(message,sizeof(message),"%s",e.what());}}if(!ok)return luaL_error(L,"%s",message);return 0;}
bool ScriptRuntime::shakeActionImpl(lua_State*L,std::string&error){
    std::vector<int32_t>ids;if(!resolveObjectTargets(L,false,ids,error))return false;hades::SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;
    const auto number=[&](const char*key,float fallback=0){const auto*p=args.get(key);const float value=p?float(p->number()):fallback;if(!std::isfinite(value))throw std::runtime_error("Shake requires finite source scalars");return value;};
    const float speed=number("Speed"),distance=number("Distance"),duration=args.get("Duration")?number("Duration"):number("DurationSeconds"),degrees=number("Angle",-1000000.f);
    const auto random=[&](){double value=0;if(!nextRecoveredEngineRandom(state_,value))throw std::runtime_error("Thing Shake source RNG unavailable");return value;};
    for(auto id:ids){const auto*body=findObjectSnapshot(state_,id);if(!body||!body->active||!body->alive||body->queryProxy||!body->entityHandle)continue;
        const auto*actor=actorWorld_.actor(id);const bool obstacle=body->backend==ObjectBackend::Screen||(actor&&actor->definition->kind=="Obstacle");
        // Obstacle override2c57d2 has a separate structural-child traversal;
        // Unit/Thing case7b does not read ApplyToChildren at all.
        if(obstacle)if(const auto*p=args.get("ApplyToChildren");p&&p->boolean())throw std::runtime_error("Obstacle Shake ApplyToChildren requires native location-graph traversal");
        const float angle=obstacle?float(double(degrees)*0.017453292519943295):degrees*0.01745329238474369049072265625f;
        auto&shake=shakes_[id];if(!shake.active&&std::find(shakeOrder_.begin(),shakeOrder_.end(),id)==shakeOrder_.end())shakeOrder_.push_back(id);
        shake.initialize(body->entityHandle,speed,distance,duration,angle,random);
    }error.clear();return true;
}
bool ScriptRuntime::advanceShakes(float elapsed,std::string&error){try{
    const auto random=[&](){double value=0;if(!nextRecoveredEngineRandom(state_,value))throw std::runtime_error("Thing Shake update source RNG unavailable");return value;};
    // ThingSystem forwards World-scaled elapsed, but ShakeSystem2d6263 divides
    // by World+20 before OffsetHelper.Update: this phase consumes real elapsed.
    for(auto it=shakeOrder_.begin();it!=shakeOrder_.end();){auto found=shakes_.find(*it);const auto*body=findObjectSnapshot(state_,*it);if(found==shakes_.end()||!body||!body->active||!body->alive||body->entityHandle!=found->second.lifetime){if(found!=shakes_.end())shakes_.erase(found);it=shakeOrder_.erase(it);continue;}
        found->second.advance(elapsed,random);if(!found->second.active)it=shakeOrder_.erase(it);else ++it;
    }error.clear();return true;
}catch(const std::exception&e){error=e.what();return false;}}
std::array<float,2> ScriptRuntime::shakeOffsetForOwner(int32_t id,uint64_t lifetime)const{const auto it=shakes_.find(id);return it!=shakes_.end()&&it->second.lifetime==lifetime?it->second.offset():std::array<float,2>{};}
bool ScriptRuntime::applyShakePresentation(const VisualKey&key,hades::ActorVisual&out,std::string&error)const{try{
    if(key.kind==VisualKey::Kind::Light)return true;const auto*body=findObjectSnapshot(state_,key.owner);if(!body)return true;const auto offset=shakeOffsetForOwner(key.owner,body->entityHandle);if(offset[0]==0&&offset[1]==0)return true;
    const hades::SourceValue*definition=nullptr;
    if(key.kind==VisualKey::Kind::Actor){const auto*a=actorWorld_.actor(key.owner);const auto use=animationBank_->exactUseAttachedShake(a->animation.clipIndex());const auto mode=animationBank_->exactLocationFromOwner(a->animation.clipIndex());
        if(!use||!mode){error="Actual shaken primary requires hash-bound UseAttachedShake/LocationFromOwner metadata (HAP6)";return false;}if(!*use||*mode!=0)return true;}
    else if(key.kind==VisualKey::Kind::Action){const auto*s=actionEffects_.state(key.handle);if(!s||s->detached)return true;definition=&s->definition;}
    else if(key.kind==VisualKey::Kind::Particle){const auto it=particles_.particles().find(key.handle);if(it==particles_.particles().end())return true;definition=&it->second.sourceDefinition;}
    else if(key.kind==VisualKey::Kind::Auxiliary){const auto*s=scenePrimaries_.state(key.handle);if(!s)return true;definition=&s->definition;}
    else if(key.kind==VisualKey::Kind::Screen){const auto*s=screenAnimations_.thing(key.owner);if(!s)return true;const auto layer=std::find_if(s->layers.begin(),s->layers.end(),[&](const auto&l){return l.handle==key.handle;});if(layer==s->layers.end())return true;definition=screenAnimations_.definition(layer->name);}
    if(definition&&!permitsShake(*definition))return true;
    // GetDrawLocation297dda..297e25 adds raw Thing ShakeComponent offset,
    // independent of body/image scale. Book markers use only animation ECS
    // ShakeOffset2b0f78; do not move the physical marker/damage origin here.
    out.x+=offset[0];out.y+=offset[1];return true;
}catch(const std::exception&e){error=e.what();return false;}}
