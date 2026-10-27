// Host-only autoplay driver. A debugging input source like --input-replay:
// it observes the original script state (RequiredKillEnemies, live Interact owners)
// and produces stick/control input through the same setLocalInput path the
// Android touch controls use. It never changes game state directly.
#include "autoplay.hpp"
#include "../app/object_runtime.hpp"
#include "../app/unit_path.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
extern "C" {
#include <lua.h>
}

ScriptRuntime::AutoplayView ScriptRuntime::autoplayView(){
    AutoplayView view;view.map=mapName_;view.inputAllowed=inputAllowed();view.pendingControls=pendingControlPresses();
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
bool ScriptRuntime::autoplayWaypoint(float goalX,float goalY,float& x,float& y,std::string& error){
    // Read-only query: planUnitPath and the node/line validators only refresh
    // cached geometry and draw no engine RNG, so replays stay deterministic.
    const auto hero=localPlayerBody();if(!hero){error="Autoplay hero unavailable";return false;}
    const int32_t id=hero->id;float hx=0,hy=0;if(!actorWorld_.location(id,hx,hy,error))return false;
    const hades::UnitPathConfig config;std::vector<hades::UnitPathNode> nodes;
    const auto validate=[&](float nx,float ny,bool& blocked,std::string& detail){return unitPathNodeBlocked(id,nx,ny,false,blocked,detail);};
    if(!hades::planUnitPath(hx,hy,goalX,goalY,config,validate,nodes,error))return false;
    if(nodes.empty()){error="Autoplay path search exhausted";return false;}
    // nodes run goal..start; take the node nearest the goal that is directly reachable.
    for(const auto& node:nodes){bool clear=false;if(!unitLineOfMovement(id,float(node.x),float(node.y),false,clear,error))return false;
        if(clear){x=float(node.x);y=float(node.y);error.clear();return true;}}
    x=float(nodes.back().x);y=float(nodes.back().y);error.clear();return true;
}

namespace hades {
// Policy: fight the nearest live enemy (approach, then release/repress
// Attack2 so the original require-release trigger policy fires each swing);
// with no enemies, walk to the nearest unused interaction and pulse Use.
// Stalled progress triggers a sideways Rush. Tried usables are skipped for a
// while so doors that refuse (locked) do not trap the driver.
AutoplayInput Autoplay::step(int frame,const ScriptRuntime::AutoplayView& view,const Waypoint& waypoint){
    AutoplayInput input;
    // A real queued control notification requests dialogue input even while
    // PlayerUnit gameplay input is blocked. Supply release/repress pulses;
    // the original waiters, callbacks and scripts own all progression.
    if(std::binary_search(view.pendingControls.begin(),view.pendingControls.end(),"ContinueText")) {
        if(frame%60==0)input.controls.push_back("ContinueText");
        input.summary="queued ContinueText";stall_=0;return input;
    }
    if(!view.hero||!view.inputAllowed){stall_=0;return input;}
    if(view.map!=map_){map_=view.map;tried_.clear();}
    const ScriptRuntime::AutoplayTarget* target=nullptr;float best=1e30f;bool enemy=false;
    for(const auto& e:view.enemies){const auto t=tried_.find(e.id);if(t!=tried_.end()&&frame-t->second<1800)continue;const float d=std::hypot(e.x-view.x,e.y-view.y);if(d<best){best=d;target=&e;enemy=true;}}
    if(!target)for(const auto& u:view.usables){const auto t=tried_.find(u.id);if(t!=tried_.end()&&frame-t->second<900)continue;const float d=std::hypot(u.x-view.x,u.y-view.y);if(d<best){best=d;target=&u;}}
    if(!target){stall_=0;return input;}
    if(target->id!=targetId_){targetId_=target->id;targetSince_=frame;lastDistance_=best;stall_=0;waypointFrame_=-1000;}
    // Steer along the engine's own A* path; straight lines walk into concave walls.
    float goalX=target->x,goalY=target->y;
    if(frame-waypointFrame_>=15){waypointFrame_=frame;std::string error;
        if(waypoint(target->x,target->y,waypointX_,waypointY_,error)){waypointValid_=true;}
        else{waypointValid_=false;if(error!=waypointError_){waypointError_=error;std::cout<<"AUTOPLAY_PATH frame="<<frame<<" direct: "<<error<<'\n';}}}
    if(waypointValid_){goalX=waypointX_;goalY=waypointY_;}
    float dx=goalX-view.x,dy=goalY-view.y;
    if(std::hypot(dx,dy)<8){dx=target->x-view.x;dy=target->y-view.y;}
    const float length=std::max(1.f,std::hypot(dx,dy));dx/=length;dy/=length;
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
