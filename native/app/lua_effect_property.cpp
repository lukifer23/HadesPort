#include "lua_runtime.hpp"
#include "object_runtime.hpp"
#include "primitive_properties.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

bool ScriptRuntime::setEffectPropertyImpl(lua_State* L,std::string& error){
    if(!lua_istable(L,1)){error="SetEffectProperty requires an actual argument table";return false;}
    const auto text=[&](const char* key,const char* fallback=""){
        lua_getfield(L,1,key);std::string value=fallback;
        if(lua_type(L,-1)==LUA_TSTRING||lua_type(L,-1)==LUA_TNUMBER){size_t bytes=0;const char* data=lua_tolstring(L,-1,&bytes);if(bytes>65536||std::memchr(data,0,bytes)){lua_pop(L,1);throw std::runtime_error(std::string("SetEffectProperty text argument bound: ")+key);}value.assign(data,bytes);}lua_pop(L,1);return value;
    };
    const auto weaponName=text("WeaponName"),effectName=text("EffectName"),property=text("Property");
    const auto change=static_cast<hades::WeaponValueChange>(uint8_t(hades::parsePropertyChange(text("ValueChangeType","Absolute"))));
    std::optional<hades::WeaponScalar> value;lua_getfield(L,1,"Value");
    const int kind=lua_type(L,-1);
    if(kind==LUA_TNUMBER){const double number=lua_tonumber(L,-1);if(!std::isfinite(number)){lua_pop(L,1);error="SetEffectProperty nonfinite Lua value";return false;}value=number;}
    else if(kind==LUA_TBOOLEAN)value=bool(lua_toboolean(L,-1));
    else if(kind==LUA_TSTRING){size_t bytes=0;const char* data=lua_tolstring(L,-1,&bytes);if(bytes>65536||std::memchr(data,0,bytes)){lua_pop(L,1);error="SetEffectProperty value text bound";return false;}value=std::string(data,bytes);}
    else if(kind!=LUA_TNIL){lua_pop(L,1);error="SetEffectProperty complex Lua value requires its native typed handler";return false;}
    lua_pop(L,1);
    std::vector<int32_t> targets;if(!resolveObjectTargets(L,true,targets,error))return false;
    const hades::WeaponEffectCueResolver cue=[this](const std::string& requested,hades::WeaponSoundCue& out,std::string& detail){
        bool valid=false;if(!audio_.resolveCue(requested,valid,detail,false))return false;
        // GetSoundCue545c0 retains the normalized requested name even when
        // invalid. Descriptor lookup does not allocate/play any audio channel.
        std::string normalized=requested;if(!normalized.empty()&&normalized.front()!='/'&&normalized.front()!='{')normalized='/'+normalized;
        out={std::move(normalized),valid,true};return true;
    };
    // ScriptAction2725a0 selects active native Units and their owned arsenal.
    // GetActiveMapThings destination aliases/order come from the shared source
    // selector. Unowned weapons and metadata-only proxies have no write.
    for(const auto id:targets){
        const auto* object=findObjectSnapshot(L,id);auto* actor=actorWorld_.actor(id);
        // Destination resolution already checks actual World membership. The
        // native 0x221 check is a Unit cast, without another Life/alive test.
        if(!object||object->queryProxy||!object->entityHandle||object->backend!=ObjectBackend::Actor||!actor||!actor->registered||actor->object!=object||actor->entityHandle!=object->entityHandle)continue;
        if(!actor->definition){error="SetEffectProperty registered actor has no actual class definition";return false;}
        if(actor->definition->kind!="Unit")continue;
        if(!actor->arsenal){error="SetEffectProperty actual Unit arsenal unavailable";return false;}
        if(!actor->arsenal->hasWeapon(weaponName))continue;
        if(!actor->arsenal->setScriptEffectProperty(weaponName,effectName,property,value,change,error,cue))return false;
    }
    error.clear();return true;
}
int ScriptRuntime::setEffectProperty(lua_State* L){
    char message[2048]{};bool ok=false;
    {std::string error;try{ok=runtime(L)->setEffectPropertyImpl(L,error);}catch(const std::exception& exception){error=exception.what();}if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
