// Host-only autoplay driver. A debugging input source like --input-replay:
// it observes the original script state (RequiredKillEnemies, live Interact owners)
// and produces stick/control input through the same setLocalInput path the
// Android touch controls use. It never changes game state directly.
#include "autoplay.hpp"
#include "../app/object_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
extern "C" {
#include <lua.h>
}

ScriptRuntime::AutoplayView ScriptRuntime::autoplayView(){
    AutoplayView view;view.map=mapName_;view.inputAllowed=inputAllowed();
    if(const auto hero=localPlayerBody()){view.hero=true;view.x=hero->x;view.y=hero->y;}
    if(!state_)return view;
    lua_getglobal(state_,"RequiredKillEnemies"); // original encounter kill set (excludes breakables)
    if(lua_istable(state_,-1)){lua_pushnil(state_);while(lua_next(state_,-2)){
        if(lua_istable(state_,-1)){lua_getfield(state_,-1,"ObjectId");const int32_t id=lua_isnumber(state_,-1)?int32_t(lua_tointeger(state_,-1)):-1;lua_pop(state_,1);
            lua_getfield(state_,-1,"Name");std::string name=lua_isstring(state_,-1)?lua_tostring(state_,-1):"";lua_pop(state_,1);
            const auto* actor=actorWorld_.actor(id);std::string error;AutoplayTarget t{id,0,0,name};
            if(actor&&actor->registered&&actor->object&&actor->object->alive&&actor->lifeStatus==0&&actorWorld_.location(id,t.x,t.y,error))view.enemies.push_back(t);}
        lua_pop(state_,1);}}
    lua_pop(state_,1);
    for(const auto& s:interactions_.snapshots()){
        if(!s.enabled||s.key.id<0)continue;const auto* object=findObjectSnapshot(state_,s.key.id);if(!object||!object->alive)continue;
        AutoplayTarget t{s.key.id,object->x,object->y,object->name};std::string error;actorWorld_.location(s.key.id,t.x,t.y,error);view.usables.push_back(t);
    }
    return view;
}

namespace hades {
// Policy: fight the nearest live enemy (approach, then release/repress
// Attack2 so the original require-release trigger policy fires each swing);
// with no enemies, walk to the nearest unused interaction and pulse Use.
// Stalled progress triggers a sideways Rush. Tried usables are skipped for a
// while so doors that refuse (locked) do not trap the driver.
AutoplayInput Autoplay::step(int frame,const ScriptRuntime::AutoplayView& view){
    AutoplayInput input;if(!view.hero||!view.inputAllowed){stall_=0;return input;}
    if(view.map!=map_){map_=view.map;tried_.clear();}
    const ScriptRuntime::AutoplayTarget* target=nullptr;float best=1e30f;bool enemy=false;
    for(const auto& e:view.enemies){const auto t=tried_.find(e.id);if(t!=tried_.end()&&frame-t->second<1800)continue;const float d=std::hypot(e.x-view.x,e.y-view.y);if(d<best){best=d;target=&e;enemy=true;}}
    if(!target)for(const auto& u:view.usables){const auto t=tried_.find(u.id);if(t!=tried_.end()&&frame-t->second<900)continue;const float d=std::hypot(u.x-view.x,u.y-view.y);if(d<best){best=d;target=&u;}}
    if(!target){stall_=0;return input;}
    float dx=target->x-view.x,dy=target->y-view.y;const float length=std::max(1.f,std::hypot(dx,dy));dx/=length;dy/=length;
    if(target->id!=targetId_){targetId_=target->id;targetSince_=frame;lastDistance_=best;stall_=0;}
    if(enemy&&frame-targetSince_>900){tried_[target->id]=frame;targetId_=-1;}
    if(best<lastDistance_-2){lastDistance_=best;stall_=0;}else ++stall_;
    if(enemy){
        input.x=dx;input.y=dy;
        if(best<260&&(frame/8)%2==0)input.controls.push_back("Attack2");
        if(best>=260&&stall_>90){input.x=-dy;input.y=dx;input.controls.push_back("Rush");stall_=0;lastDistance_=1e30f;}
        if(best<260)stall_=0;
    }else{
        if(best>90){input.x=dx;input.y=dy;}
        if(best<220&&(frame/10)%2==0)input.controls.push_back("Use");
        if(best<220&&stall_>240){tried_[target->id]=frame;stall_=0;}
        if(best>=220&&stall_>120){input.x=-dy;input.y=dx;input.controls.push_back("Rush");stall_=0;lastDistance_=1e30f;}
    }
    std::ostringstream line;line<<"target="<<target->name<<'#'<<target->id<<(enemy?" enemy":" use")<<" dist="<<int(best)<<" enemies="<<view.enemies.size()<<" usables="<<view.usables.size()<<" map="<<view.map;input.summary=line.str();
    return input;
}
}
