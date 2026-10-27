#include "lua_runtime.hpp"
#include "object_runtime.hpp"
#include <cstdio>
#include <exception>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

// Action79: Execute251f5a -> runActionOnThings27f4e0 -> virtual Thing+e8.
// Obstacle.RunScriptAction2c5b29 removes its draw-grid entry and adds its
// existing handle to ObstacleManager screen set3 before the base action.
// Thing.RunScriptAction2e42f2 only sets Thing+220. It does not call
// Animation.SetUseScreenLocation or alter the primary/child own flags.
bool ScriptRuntime::drawScreenRelativeImpl(lua_State*L,std::string&error){
    std::vector<int32_t> ids;if(!resolveObjectTargets(L,false,ids,error))return false;
    // Validate the selected resource backend before changing any owner flag.
    for(auto id:ids){
        const auto*body=findObjectSnapshot(L,id);if(!body||!body->active||!body->alive||body->queryProxy)continue;
        if(body->backend==ObjectBackend::Screen){const auto*s=screenAnimations_.thing(id);if(!s||s->dead||s->body!=body){error="DrawScreenRelative screen Thing lifetime unavailable";return false;}continue;}
        const auto*a=actorWorld_.actor(id);if(!a||!a->registered||a->object!=body){error="DrawScreenRelative actor Thing lifetime unavailable";return false;}
        if(a->animation.active()||scenePrimaries_.state(a->primaryAnimationLifetime)||a->primaryEffectAnimation){error="DrawScreenRelative primary resource requires its native screen projection adapter";return false;}
        if(!body->animation.empty()){
            const auto*primary=actionEffects_.state(a->primaryAnimationLifetime);
            if(!primary||!primary->primary||primary->ownerLifetime!=a->entityHandle){error="DrawScreenRelative primary Animation ownership unavailable";return false;}
        }
    }
    for(auto id:ids){
        auto*body=findObjectSnapshot(L,id);if(!body||!body->active||!body->alive||body->queryProxy)continue;
        if(body->backend==ObjectBackend::Actor){const auto*a=actorWorld_.actor(id);
            if(a->definition&&a->definition->kind=="Obstacle"&&!actorWorld_.removeObstacleDrawGridRecord(id,error))return false;
            // Portable presentation enumerates every registered actor and its
            // actual animations. The owner screen flag selects the screen
            // render route, so no world-cull gate remains after removal.
        }
        body->screenSpace=true;body->graphicsDirty=true;
    }
    error.clear();return true;
}
int ScriptRuntime::drawScreenRelative(lua_State*L){
    char message[2048]{};bool ok=false;
    try{std::string error;auto*self=runtime(L);ok=self&&self->drawScreenRelativeImpl(L,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.empty()?"DrawScreenRelative runtime unavailable":error.c_str());}
    catch(const std::exception&e){std::snprintf(message,sizeof(message),"%s",e.what());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
