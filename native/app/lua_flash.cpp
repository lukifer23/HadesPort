#include "lua_runtime.hpp"
#include "scene_renderer.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}
void ScriptRuntime::registerFlashActions(){for(int stop=0;stop<2;++stop){lua_pushlightuserdata(state_,this);lua_pushboolean(state_,stop);lua_pushcclosure(state_,flashAction,2);lua_setglobal(state_,stop?"StopFlashing":"Flash");}}
int ScriptRuntime::flashAction(lua_State*L){char message[2048]{};bool ok=false;{try{std::string error;auto*self=runtime(L);ok=self&&self->flashActionImpl(L,lua_toboolean(L,lua_upvalueindex(2)),error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}catch(const std::exception&e){std::snprintf(message,sizeof(message),"%s",e.what());}}if(!ok)return luaL_error(L,"%s",message);return 0;}
bool ScriptRuntime::flashActionImpl(lua_State*L,bool stop,std::string&error){
    std::vector<int32_t> ids;if(!resolveObjectTargets(L,false,ids,error))return false;
    if(stop){for(auto id:ids)flashes_.erase(id);return true;}
    hades::SourceValue args;if(!hades::readSourceValue(L,1,args,error))return false;
    const auto number=[&](const char*name,float fallback=0){const auto*p=args.get(name);const float value=p?float(p->number()):fallback;if(!std::isfinite(value))throw std::runtime_error("Flash requires finite source scalars");return value;};
    const float duration=args.get("Duration")?number("Duration"):number("DurationSeconds"),speed=number("Speed");
    std::array<uint8_t,4> color{255,255,255,255};
    if(args.get("ColorByName"))throw std::runtime_error("Flash ColorByName lookup requires native color table provider");
    if(const auto*p=args.get("Color")){const auto&v=p->array();if(v.size()!=3&&v.size()!=4)throw std::runtime_error("Flash source Color requires RGB/RGBA bytes");for(size_t i=0;i<v.size();++i){const double n=v[i].number();if(n<0||n>255||n!=std::trunc(n))throw std::runtime_error("Flash source color outside native byte range");color[i]=uint8_t(n);}}
    for(auto id:ids){const auto*body=findObjectSnapshot(state_,id);if(!body||!body->active||!body->alive||body->queryProxy||!body->entityHandle)continue;
        auto&flash=flashes_[id];if(flash.lifetime!=body->entityHandle){flash={};flash.lifetime=body->entityHandle;}
        // Thing.RunScriptAction case79 / FlashForDuration2e1c90: Init resets
        // phase, prior direction and fixed-value controls. Duration0 reads
        // ExpireAfterCycle; positive Duration ignores that script flag.
        if(duration>=0){const auto lifetime=flash.lifetime;flash={};flash.lifetime=lifetime;flash.enabled=true;flash.color=color;flash.speed=speed*3.1415927410125732421875f;
            if(duration>0)flash.remaining=duration;else if(const auto*p=args.get("ExpireAfterCycle"))flash.expireAfterCycle=p->boolean();}
        flash.minimum=number("MinFraction");flash.maximum=number("MaxFraction");
    }
    error.clear();return true;
}
bool ScriptRuntime::advanceFlashes(float elapsed,std::string&error){
    // World.Update scales elapsed by World+20, then ThingSystem.Update2d9d54
    // forwards it unchanged (no per-Thing clock multiplier). FlashSystem
    // UpdateThing2cf560 updates before strict remaining<0 retirement.
    for(auto it=flashes_.begin();it!=flashes_.end();){const auto*body=findObjectSnapshot(state_,it->first);if(!body||!body->active||!body->alive||body->entityHandle!=it->second.lifetime){it=flashes_.erase(it);continue;}
        // Expiry removes system membership, retaining the component itself
        // (ThingSystem.Update2d9d60..2d9d6f / PrepDraw2afc14).
        if(it->second.updating){it->second.advance(elapsed);if(!it->second.enabled||it->second.remaining<0)it->second.updating=false;}++it;
    }error.clear();return true;
}
std::array<float,4> ScriptRuntime::flashColorForOwner(int32_t id,uint64_t lifetime)const{const auto it=flashes_.find(id);return it!=flashes_.end()&&it->second.lifetime==lifetime?it->second.opacityColor():std::array<float,4>{};}
bool ScriptRuntime::samplePresentation(const VisualKey&key,hades::ActorVisual&out,std::string&error){
    if(!samplePresentationImpl(key,out,error))return false;if(key.kind==VisualKey::Kind::Light)return true;
    const auto*body=findObjectSnapshot(state_,key.owner);if(!body)return true;
    const auto color=flashColorForOwner(key.owner,body->entityHandle);if(color[3]==0)return true;
    const hades::SourceValue*definition=nullptr;
    if(key.kind==VisualKey::Kind::Actor){const auto*a=actorWorld_.actor(key.owner);const auto use=animationBank_->exactUseAttachedFlasher(a->animation.clipIndex());if(!use){error="Actual flashed primary requires hash-bound UseAttachedFlasher metadata (HAP5)";return false;}if(!*use)return true;}
    else if(key.kind==VisualKey::Kind::Action){const auto*s=actionEffects_.state(key.handle);if(!s||s->detached)return true;definition=&s->definition;}
    else if(key.kind==VisualKey::Kind::Particle){const auto it=particles_.particles().find(key.handle);if(it==particles_.particles().end())return true;definition=&it->second.sourceDefinition;}
    else if(key.kind==VisualKey::Kind::Auxiliary){const auto*s=scenePrimaries_.state(key.handle);if(!s)return true;definition=&s->definition;}
    else if(key.kind==VisualKey::Kind::Screen){const auto*s=screenAnimations_.thing(key.owner);if(!s)return true;const auto layer=std::find_if(s->layers.begin(),s->layers.end(),[&](const auto&l){return l.handle==key.handle;});if(layer==s->layers.end())return true;definition=screenAnimations_.definition(layer->name);}
    // Animation.GetDrawFlasher293fab..293fd6: source owner inheritance is
    // conditional on Data.UseAttachedFlasher (native ctor defaulttrue).
    if(definition)if(const auto*p=definition->get("UseAttachedFlasher");p&&!p->boolean())return true;
    out.flashColor=color;return true;
}
