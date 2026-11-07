#include "lua_runtime.hpp"
#include "object_runtime.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
std::string useableText(const hades::SourceValue& d,const char* key){const auto* value=d.get(key);return value?value->text():std::string{};}
std::vector<int32_t> useableTargets(lua_State* L){
    // runActionOnThings27f584..27f662 reads singular Id first. Its fallback
    // 27f667..27f7da iterates the actual Lua Ids array in order, retaining
    // duplicates. This is not World.GetActiveMapThings' pointer hash set.
    // Native Names preprocessing and converting/nonsequential Lua forms are
    // kept explicit until their action-specific expansion is recovered.
    for(const char* field:{"Name","Names"}){lua_getfield(L,1,field);const bool present=!lua_isnil(L,-1);lua_pop(L,1);if(present)throw std::runtime_error("UseableOn/Off named selector preprocessing not reconstructed");}
    for(const char* field:{"Interval","MinInterval","MaxInterval"}){
        lua_getfield(L,1,field);if(!lua_isnil(L,-1)&&(lua_type(L,-1)!=LUA_TNUMBER||lua_tonumber(L,-1)!=0))throw std::runtime_error("UseableOn/Off nonzero interval/RNG branch not reconstructed");lua_pop(L,1);
    }
    const auto integer=[&](int index){
        if(lua_type(L,index)!=LUA_TNUMBER)throw std::runtime_error("UseableOn/Off converting selector form not reconstructed");
        const double n=lua_tonumber(L,index);if(!std::isfinite(n)||n<INT32_MIN||n>INT32_MAX||n!=std::trunc(n))throw std::runtime_error("UseableOn/Off ID outside integer signed32 subset");return int32_t(n);
    };
    lua_getfield(L,1,"Id");
    if(!lua_isnil(L,-1)){const int32_t id=integer(-1);lua_pop(L,1);return {id};}
    lua_pop(L,1);lua_getfield(L,1,"Ids");std::vector<int32_t> result;
    if(lua_isnil(L,-1)){lua_pop(L,1);return result;}
    if(!lua_istable(L,-1))throw std::runtime_error("UseableOn/Off Ids requires actual dense Lua array");
    const int table=lua_absindex(L,-1);
    for(int i=1;;++i){if(i>100000)throw std::runtime_error("UseableOn/Off selector count bound");lua_rawgeti(L,table,i);if(lua_isnil(L,-1)){lua_pop(L,1);break;}result.push_back(integer(-1));lua_pop(L,1);}
    lua_pushnil(L);while(lua_next(L,table)){
        const double key=lua_type(L,-2)==LUA_TNUMBER?lua_tonumber(L,-2):0;
        if(key<1||key>double(result.size())||key!=std::trunc(key))throw std::runtime_error("UseableOn/Off sparse/named Ids form not reconstructed");lua_pop(L,1);
    }
    lua_pop(L,1);return result;
}
}

bool ScriptRuntime::useableImpl(lua_State* L,bool enabled,std::string& error){
    const int top=lua_gettop(L);
    try{
        if(!lua_istable(L,1))throw std::runtime_error("UseableOn/Off requires original argument table");
        const auto selected=useableTargets(L);
        for(const int32_t id:selected){
            auto* object=findObjectSnapshot(L,id);if(!object||object->queryProxy||!object->entityHandle)continue;
            const hades::InteractionKey key{id,object->entityHandle};
            const hades::ScreenThing* screen=nullptr;const hades::ActorComponents* actor=nullptr;
            if(object->backend==ObjectBackend::Screen){screen=screenAnimations_.thing(id);if(!screen||screen->body!=object||screen->body->entityHandle!=key.lifetime)continue;}
            else{actor=actorWorld_.actor(id);if(!actor||!actor->registered||actor->object!=object||actor->entityHandle!=key.lifetime)continue;}
            // Missing Interact is a native no-op; these actions never allocate
            // data, reset use counts/cooldowns, or change grid membership.
            if(!interactions_.contains(key))continue;
            hades::InteractionSnapshot state;if(!interactions_.snapshot(key,state,error))throw std::runtime_error(error);
            if(state.enabled==enabled)continue;
            bool force=false;
            if(!enabled){
                // Thing2e3ddd reads ForceHighlightOff only for a live enabled
                // component. Repeated Off must not redispatch OnMouseOff.
                lua_getfield(L,1,"ForceHighlightOff");if(!lua_isnil(L,-1)){if(lua_type(L,-1)!=LUA_TBOOLEAN)throw std::runtime_error("UseableOff ForceHighlightOff requires actual boolean");force=lua_toboolean(L,-1);}lua_pop(L,1);
            }
            if(!interactions_.setEnabled(key,enabled,error))throw std::runtime_error(error);
            const auto unuseable=useableText(state.definition,"UnuseableAnimation");
            std::string animation;
            if(enabled&&!unuseable.empty()){
                // Thing2e3da4 uses immutable ThingData.Graphic (+8), not the
                // current mutable primary name or a UseableAnimation alias.
                if(screen)animation=screen->sourceGraphic;
                else{
                    if(!actor->definition)throw std::runtime_error("UseableOn actual ThingData definition unavailable");
                    const auto* graphic=actor->definition->properties.path("Thing.Graphic");
                    if(!graphic)throw std::runtime_error("UseableOn immutable ThingData.Graphic not represented");
                    animation=graphic->text();
                }
            }else if(!enabled)animation=unuseable;
            // Thing.SetAnimation2ded10 returns immediately only for an empty
            // string. Nonempty names go through the real primary backend.
            if(!animation.empty()){
                if(!(screen?setScreenAnimation(id,animation,error):setActorAnimation(id,animation,error)))throw std::runtime_error(error);
            }
            // Native enabled byte changes before SetAnimation(false,true,false),
            // while Interact+3 (previousUseable) changes after that call.
            if(!interactions_.setPreviousUseable(key,enabled,error))throw std::runtime_error(error);
            if(force){
                if(!cursorHighlight(key,false,error))throw std::runtime_error(error);
                // SetUnuseable2bfca9 calls ScriptManager.OnMouseOff directly:
                // no cursor-target/cache change and no physical-visible gate.
                if(!dispatchThingCallbacks("OnMouseOff",id,[](lua_State* state,std::string&){lua_pushnil(state);lua_setfield(state,-2,"name");return true;}))throw std::runtime_error(error_);
            }
        }
        error.clear();return true;
    }catch(const std::exception& detail){lua_settop(L,top);error=detail.what();return false;}
}
int ScriptRuntime::useableAction(lua_State* L){
    char message[2048]{};bool ok=false;
    try{std::string error;auto* self=runtime(L);ok=self&&self->useableImpl(L,lua_toboolean(L,lua_upvalueindex(2)),error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.empty()?"UseableOn/Off runtime unavailable":error.c_str());}
    catch(const std::exception& detail){std::snprintf(message,sizeof(message),"%s",detail.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
