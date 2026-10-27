#include "lua_runtime.hpp"
#include "object_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
// ScriptArgs::Ids 0x1802818f0 caches a nonempty IntList, preferring Ids
// when that field exists. IntList 0x180248e50 supports additional string and
// named-table forms; only its numeric scalar/plain sequential-array path is
// admitted here. Other forms fail explicitly rather than changing selection.
void cacheInteractIds(lua_State* L,int args,std::vector<int32_t>& result) {
    if(!result.empty())return;
    std::vector<int32_t> parsed;
    for(const auto* key:{"Name","Names"}) {
        lua_getfield(L,args,key);const bool supplied=!lua_isnil(L,-1);lua_pop(L,1);
        if(supplied)throw std::runtime_error("NotifyOnInteract named/group selector cache not reconstructed");
    }
    lua_getfield(L,args,"Ids");
    if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,args,"Id");}
    const auto append=[&](int index) {
        if(lua_type(L,index)!=LUA_TNUMBER)throw std::runtime_error("NotifyOnInteract nonnumeric IntList form not reconstructed");
        const double value=std::trunc(lua_tonumber(L,index));
        if(!std::isfinite(value)||value<INT32_MIN||value>INT32_MAX)throw std::runtime_error("NotifyOnInteract ID outside signed32 domain");
        parsed.push_back(int32_t(value));
    };
    if(lua_istable(L,-1)) {
        const int list=lua_absindex(L,-1);
        for(int i=1;;++i) {
            if(i>100000)throw std::runtime_error("NotifyOnInteract ID count bound");
            lua_pushinteger(L,i);lua_gettable(L,list);
            if(lua_isnil(L,-1)){lua_pop(L,1);break;}
            append(-1);lua_pop(L,1);
        }
        // Native IntList also visits named fields after the sequence. That
        // conversion/iteration branch remains explicit instead of dropping it.
        lua_pushnil(L);
        while(lua_next(L,list)) {
            if(lua_type(L,-2)!=LUA_TNUMBER||lua_tonumber(L,-2)<1||
               lua_tonumber(L,-2)>double(parsed.size())||lua_tonumber(L,-2)!=std::trunc(lua_tonumber(L,-2)))
                throw std::runtime_error("NotifyOnInteract named/sparse IntList form not reconstructed");
            lua_pop(L,1);
        }
    } else if(!lua_isnil(L,-1))append(-1);
    lua_pop(L,1);
    std::sort(parsed.begin(),parsed.end());
    parsed.erase(std::unique(parsed.begin(),parsed.end()),parsed.end());
    result=std::move(parsed);
}
}

int ScriptRuntime::interactNotifyAction(lua_State* L) {
    char message[2048]{};
    try {
        auto* self=runtime(L);
        if(!self||!lua_istable(L,1))throw std::runtime_error("NotifyOnInteract requires source argument table");
        // Execute action257: dispatch 0x1802538b2..0x180253911 copies
        // ScriptArgs and registers CheckInteract 0x18027de30. Shared event
        // path 0x18025344e reads Float(Timeout,FLT_MAX), then World.AddEvent.
        WorldNotify pending;pending.interact=true;pending.timeout=std::numeric_limits<float>::max();
        lua_getfield(L,1,"Timeout");
        if(lua_isnumber(L,-1)) {
            const double value=lua_tonumber(L,-1);
            if(!std::isfinite(value)||std::abs(value)>std::numeric_limits<float>::max())throw std::runtime_error("NotifyOnInteract timeout bound");
            pending.timeout=float(value);
        }
        lua_pop(L,1);
        if(self->worldNotifies_.size()>=10000)throw std::runtime_error("Native notification queue bound");
        lua_pushvalue(L,1);pending.arguments=luaL_ref(L,LUA_REGISTRYINDEX);
        self->worldNotifies_.push_back(std::move(pending));return 0;
    } catch(const std::exception& error) {std::snprintf(message,sizeof(message),"%s",error.what());}
    return luaL_error(L,"%s",message);
}

bool ScriptRuntime::checkInteractNotify(WorldNotify& pending,bool& matched,int32_t& hit,std::string& error) {
    const int top=lua_gettop(state_);
    try {
        matched=false;hit=0;
        // CheckInteract 0x18027de68 returns false while EditingMode is set.
        bool editing=false;if(!configOptionBool("EditingMode",editing,error))return false;
        if(editing){error.clear();return true;}
        lua_rawgeti(state_,LUA_REGISTRYINDEX,pending.arguments);
        cacheInteractIds(state_,lua_absindex(state_,-1),pending.sources);
        lua_settop(state_,top);
        // World.GetActiveMapThings is repeated each check; retained selector
        // IDs do not pin an old entity lifetime. Read only actual live Things
        // and the Interact record belonging to that current entity.
        for(const auto id:pending.sources) {
            const auto* object=findObjectSnapshot(state_,id);
            if(!object||object->queryProxy||!object->entityHandle)continue;
            // Numeric World.GetActiveMapThings314740..3147a1 only tests
            // current ID-map membership, not life/active flags. A just-used
            // owner marked dead can still match before actual disposal.
            if(object->backend==ObjectBackend::Screen) {
                const auto* screen=screenAnimations_.thing(id);
                if(!screen||!screen->body||screen->body->entityHandle!=object->entityHandle)continue;
            } else {
                const auto* actor=actorWorld_.actor(id);
                if(!actor||!actor->registered||actor->entityHandle!=object->entityHandle)continue;
            }
            hades::InteractionSnapshot interaction;std::string missing;
            if(!interactions_.snapshot({id,object->entityHandle},interaction,missing))continue;
            // 0x18027df83..0x18027df92: signed World.Frame <=
            // Interact.mLastUseFrame+1, including the native int32 wrap.
            // Do not substitute hasBeenUsed/OnUsed or fabricate a click.
            const int32_t lastPlusOne=int32_t(uint32_t(interaction.lastUseFrame)+1u);
            if(int32_t(uint32_t(worldFrame_))>lastPlusOne)continue;
            if(matched)throw std::runtime_error("NotifyOnInteract simultaneous-source native hash iteration order not reconstructed");
            matched=true;hit=id;
        }
        error.clear();return true;
    } catch(const std::exception& detail) {lua_settop(state_,top);error=detail.what();return false;}
}
