#include "lua_runtime.hpp"
#include "unit_movement.hpp"
#include "platform_log.hpp"
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
std::vector<int32_t> ids(lua_State* L,const char* one,const char* many){
    std::vector<int32_t> out;lua_getfield(L,1,many);
    auto add=[&](){int numeric=0;const double v=lua_tonumberx(L,-1,&numeric);if(!numeric||!std::isfinite(v)||v!=std::floor(v)||v<INT32_MIN||v>INT32_MAX)throw std::runtime_error("Collision notification requires signed32 object IDs");out.push_back(int32_t(v));};
    if(!lua_isnil(L,-1)){if(!lua_istable(L,-1)||lua_rawlen(L,-1)>100000)throw std::runtime_error("Collision notification ID list bound");for(size_t i=1,n=lua_rawlen(L,-1);i<=n;++i){lua_rawgeti(L,-1,int(i));add();lua_pop(L,1);}lua_pop(L,1);}
    else{lua_pop(L,1);lua_getfield(L,1,one);if(!lua_isnil(L,-1))add();lua_pop(L,1);}return out;
}
bool contains(const std::vector<hades::ActorPoint>& polygon,float x,float y){
    if(polygon.size()<3)return false;bool in=false;
    for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++){
        const auto a=polygon[i],b=polygon[j];
        if((a.y>y)!=(b.y>y)&&x<(b.x-a.x)*(y-a.y)/(b.y-a.y)+a.x)in=!in;
    }return in;
}
bool available(const hades::ActorComponents* a){return a&&a->registered&&a->object&&a->object->active&&a->object->alive&&!a->object->queryProxy;}
}
bool ScriptRuntime::collisionNotifyImpl(lua_State* L,bool separated,std::string& error){try{
    if(!lua_istable(L,1))throw std::runtime_error("Collision notification requires argument table");
    WorldNotify pending;pending.sources=ids(L,"Id","Ids");pending.destinations=ids(L,"DestinationId","DestinationIds");
    // Execute's two early vector tests precede event creation even for an empty
    // DestinationIds supplied by genuine GetIds. No synthetic completion.
    if(pending.sources.empty()||pending.destinations.empty())return true;
    for(const char* key:{"Name","Names","DestinationName","DestinationNames"}){lua_getfield(L,1,key);bool named=!lua_isnil(L,-1);lua_pop(L,1);if(named)throw std::runtime_error("Collision notification named-group cache adapter unavailable");}
    pending.whenSeparated=separated;lua_getfield(L,1,"PointOnly");pending.pointOnly=lua_toboolean(L,-1);lua_pop(L,1);
    pending.timeout=std::numeric_limits<float>::max();lua_getfield(L,1,"Timeout");if(!lua_isnil(L,-1)){int numeric=0;const double v=lua_tonumberx(L,-1,&numeric);if(!numeric||!std::isfinite(v)||std::abs(v)>std::numeric_limits<float>::max())throw std::runtime_error("Collision notification timeout bound");pending.timeout=float(v);}lua_pop(L,1);
    if(worldNotifies_.size()>=10000)throw std::runtime_error("Collision notification queue bound");
    lua_pushvalue(L,1);pending.arguments=luaL_ref(L,LUA_REGISTRYINDEX);worldNotifies_.push_back(std::move(pending));return true;
}catch(const std::exception& e){error=e.what();return false;}}
int ScriptRuntime::collisionNotifyAction(lua_State* L){
    char message[2048]{};bool ok=false;{std::string error;ok=runtime(L)->collisionNotifyImpl(L,lua_toboolean(L,lua_upvalueindex(2)),error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    if(!ok)return luaL_error(L,"%s",message);return 0;
}
bool ScriptRuntime::advanceNativeNotifies(float dt,std::string& error){
    // Native World moves newly admitted events into its active vector before
    // checking them. Registrations during callbacks are for the next frame.
    const size_t count=worldNotifies_.size();
    for(size_t i=0;i<count;++i){
        auto& pending=worldNotifies_[i];if(!pending.arguments)continue;
        bool overlap=false,controlMatched=false;int32_t hit=0;std::string controlName;
        if(pending.control&&!checkControlNotify(pending,controlMatched,controlName,error))return false;
        bool allDeadDone=false;if(pending.allDead&&!checkAllDeadNotify(pending,allDeadDone,error))return false;
        bool distanceDone=false;if(pending.distance&&!checkDistanceNotify(pending,distanceDone,error))return false;
        bool stoppedDone=false;if(pending.stopped&&!checkStoppedNotify(pending,stoppedDone,error))return false;
        bool unitStateDone=false;if(pending.unitState&&!checkUnitStateNotify(pending,unitStateDone,error))return false;
        if(pending.playerInput){overlap=playerInputThisFrame_;}
        else if(!pending.control&&!pending.distance&&!pending.stopped&&!pending.allDead&&!pending.unitState)for(int32_t sourceId:pending.sources){auto* source=actorWorld_.actor(sourceId);if(!available(source))continue;
            if(!actorWorld_.refreshGeometry(sourceId,error))return false;
            for(int32_t destinationId:pending.destinations){auto* destination=actorWorld_.actor(destinationId);if(!available(destination))continue;
                bool collided=false;
                if(pending.pointOnly&&!pending.whenSeparated){float x=0,y=0;if(!actorWorld_.location(destinationId,x,y,error))return false;collided=contains(source->geometry,x,y);}
                else{
                    if(!actorWorld_.refreshGeometry(destinationId,error))return false;
                    // Portable polygon narrowphase shared with movement. Native
                    // CheckOnCollide/NotColliding use geometry without Z gates.
                    std::vector<hades::MovementPoint> a,b;a.reserve(source->geometry.size());b.reserve(destination->geometry.size());
                    for(auto p:source->geometry)a.push_back({p.x,p.y});for(auto p:destination->geometry)b.push_back({p.x,p.y});
                    collided=hades::movementPolygonsOverlap(a,b);
                }
                if(collided){overlap=true;hit=destinationId;break;}
            }if(overlap)break;
        }
        const bool satisfied=pending.unitState?unitStateDone:pending.allDead?allDeadDone:pending.stopped?stoppedDone:pending.distance?distanceDone:pending.control?controlMatched:(pending.whenSeparated?!overlap:overlap);
        if(!satisfied)pending.timeout-=dt;
        const bool timeout=!satisfied&&pending.timeout<0;if(!satisfied&&!timeout)continue;
        const int top=lua_gettop(state_);lua_rawgeti(state_,LUA_REGISTRYINDEX,pending.arguments);lua_getfield(state_,-1,"Notify");
        const char* value=lua_tostring(state_,-1);const std::string name=value?value:"";lua_settop(state_,top);
        if(satisfied&&!pending.playerInput&&pending.control){lua_getglobal(state_,"NotifyResultsTable");if(lua_istable(state_,-1)){lua_pushlstring(state_,controlName.data(),controlName.size());lua_setfield(state_,-2,name.c_str());}lua_settop(state_,top);}
        // CheckOnCollide calls Lua::NewTable("NotifyResultsTable") like the
        // distance checks: a fresh global table holding only this result.
        else if(satisfied&&!pending.playerInput&&overlap&&!pending.whenSeparated){lua_newtable(state_);lua_pushvalue(state_,-1);lua_setglobal(state_,"NotifyResultsTable");lua_pushinteger(state_,hit);lua_setfield(state_,-2,name.c_str());lua_settop(state_,top);}
        const int ref=pending.arguments;pending.arguments=0;luaL_unref(state_,LUA_REGISTRYINDEX,ref);
        if(!name.empty()&&!notifyScriptEvent(state_,name,true,timeout,error,!timeout))return false;
    }
    std::erase_if(worldNotifies_,[](const auto& p){return !p.arguments;});return true;
}

namespace {
// sgg::LuaTableExtensions::Float/Bool read the retained original table on
// every check: number (or numeric string) else default; boolean else default.
float tableFloat(lua_State* L,int t,const char* key,float fallback){lua_getfield(L,t,key);float v=fallback;if(lua_isnumber(L,-1))v=float(lua_tonumber(L,-1));lua_pop(L,1);return v;}
bool tableBool(lua_State* L,int t,const char* key,bool fallback){lua_getfield(L,t,key);const bool v=lua_isboolean(L,-1)?lua_toboolean(L,-1)!=0:fallback;lua_pop(L,1);return v;}
// LuaTable::GetByRef<LuaTable> then t[i] for i=1.. until nil. Non-number IDs
// (lua_isnumber false) and non-string names are skipped, not terminating.
template<class F> bool eachId(lua_State* L,int t,const char* key,F&& f){
    lua_getfield(L,t,key);if(!lua_istable(L,-1)){lua_pop(L,1);return true;}
    for(lua_Integer i=1;i<=100000;++i){lua_pushinteger(L,i);lua_gettable(L,-2);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
        const bool numeric=lua_isnumber(L,-1)!=0;const lua_Integer id=numeric?lua_tointeger(L,-1):0;lua_pop(L,1);
        if(numeric&&id>=INT32_MIN&&id<=INT32_MAX&&!f(int32_t(id))){lua_pop(L,1);return false;}}
    lua_pop(L,1);return true;
}
template<class F> bool eachName(lua_State* L,int t,const char* key,F&& f){
    lua_getfield(L,t,key);if(!lua_istable(L,-1)){lua_pop(L,1);return true;}
    for(lua_Integer i=1;i<=100000;++i){lua_pushinteger(L,i);lua_gettable(L,-2);if(lua_isnil(L,-1)){lua_pop(L,1);break;}
        std::string name;const bool text=lua_isstring(L,-1)!=0;if(text){size_t n=0;const char* b=lua_tolstring(L,-1,&n);name.assign(b,std::min<size_t>(n,4096));}lua_pop(L,1);
        if(text&&!f(name)){lua_pop(L,1);return false;}}
    lua_pop(L,1);return true;
}
}
// ScriptAction::Execute (VA180253381/180253443 for the two actions): copy the
// original args into ScriptArgs and World::AddScriptUpdateGameEvent with the
// check procedure, LuaTableExtensions::Float(Timeout, FLT_MAX@VA180611134).
// No early empty-selection test precedes admission (unlike OnCollide).
int ScriptRuntime::distanceNotifyAction(lua_State* L){
    auto* self=runtime(L);const int mode=int(lua_tointeger(L,lua_upvalueindex(2)));
    if(!self||!lua_istable(L,1))return luaL_error(L,"Distance notification requires source argument table");
    WorldNotify pending;pending.distance=uint8_t(mode);
    pending.timeout=tableFloat(L,1,"Timeout",std::numeric_limits<float>::max());
    if(!std::isfinite(pending.timeout))return luaL_error(L,"Distance notification Timeout must be finite");
    if(self->worldNotifies_.size()>=10000)return luaL_error(L,"Native notification queue bounds");
    lua_pushvalue(L,1);pending.arguments=luaL_ref(L,LUA_REGISTRYINDEX);self->worldNotifies_.push_back(std::move(pending));return 0;
}
// CheckWithinDistanceAverage RVA0x279c70 / CheckWithinDistanceAny RVA0x27a680.
// Thing lookup is World+0x398 (ID -> live Thing); location is Thing::GetLocation
// (attached: parent location + attach offset), Z is GetZLocation. UseLocationZ
// subtracts Z*0.8660254 from the SOURCE Y (Average: from destinations as well).
bool ScriptRuntime::checkDistanceNotify(const WorldNotify& pending,bool& done,std::string& error){
    done=false;lua_State* L=state_;const int top=lua_gettop(L);
    lua_rawgeti(L,LUA_REGISTRYINDEX,pending.arguments);const int t=lua_absindex(L,-1);
    auto fail=[&](const std::string& m){lua_settop(L,top);error=m;return false;};
    if(!lua_istable(L,t))return fail("Distance notification lost its original argument table");
    const bool useZ=tableBool(L,t,"UseLocationZ",false);
    struct P{float x=0,y=0,z=0;};
    auto place=[&](int32_t id,P& p)->bool{return actorWorld_.location(id,p.x,p.y,error)&&actorWorld_.zLocation(id,p.z,error);};
    float sx=0,sy=0;int count=0;std::string inner;
    auto addSource=[&](int32_t id)->bool{
        if(!available(actorWorld_.actor(id)))return true;P p;if(!place(id,p)){inner=error;return false;}
        sx+=p.x;sy+=p.y;if(useZ)sy-=p.z*0.8660254f;++count;return true;};
    if(pending.distance==1||pending.distance==3){
        // CheckOutsideDistanceAverage VA18027b8a0 uses the same source and
        // destination means, OnlyX/OnlyY and projected-Z arithmetic, but
        // singular IDs take precedence over arrays. Empty selection finishes;
        // nonempty selections finish only at strictly greater distance.
        const bool outside=pending.distance==3;
        lua_getfield(L,t,"Id");int numeric=0;const lua_Integer id=lua_tointegerx(L,-1,&numeric);lua_pop(L,1);
        if(numeric&&id>=INT32_MIN&&id<=INT32_MAX&&!addSource(int32_t(id)))return fail(inner);
        if((!outside||!numeric)&&!eachId(L,t,"Ids",addSource))return fail(inner);
        if(count<=0){lua_settop(L,top);done=true;error.clear();return true;}
        const float cx=sx/float(count),cy=sy/float(count);
        float dx=0,dy=0;int dcount=0;int32_t firstDestination=0;
        auto addDestination=[&](int32_t id)->bool{
            if(!available(actorWorld_.actor(id)))return true;P p;if(!place(id,p)){inner=error;return false;}
            if(!firstDestination)firstDestination=id;
            dx+=p.x;dy+=p.y;if(useZ)dy-=p.z*0.8660254f;++dcount;return true;};
        lua_getfield(L,t,"DestinationId");numeric=0;const lua_Integer did=lua_tointegerx(L,-1,&numeric);lua_pop(L,1);
        if(numeric&&did>=INT32_MIN&&did<=INT32_MAX&&!addDestination(int32_t(did)))return fail(inner);
        if((!outside||!numeric)&&!eachId(L,t,"DestinationIds",addDestination))return fail(inner);
        // No destination resolves: the native check also returns finished.
        if(dcount<=0){lua_settop(L,top);done=true;error.clear();return true;}
        const float ax=dx/float(dcount),ay=dy/float(dcount);
        float d2=0;
        if(tableBool(L,t,"OnlyX",false))d2=(cx-ax)*(cx-ax);
        else{d2=(cy-ay)*(cy-ay);if(!tableBool(L,t,"OnlyY",false))d2=d2+(cx-ax)*(cx-ax);}
        const float distance=tableFloat(L,t,"Distance",0);
        if(outside){done=distance*distance<d2;lua_settop(L,top);error.clear();return true;}
        if(distance*distance<d2){lua_settop(L,top);error.clear();return true;}
        // CheckWithinDistanceAverage VA18027a4d0..a603: LOS goes from source
        // mean to destination mean, ignores the first actual destination and
        // uses Z=0. An end buffer strictly beyond the separation completes;
        // equality shortens to the source and the grid returns clear.
        const bool stopsProjectiles=tableBool(L,t,"StopsProjectiles",false),stopsUnits=tableBool(L,t,"StopsUnits",false);
        if(stopsProjectiles||stopsUnits){
            const float width=tableFloat(L,t,"LineOfSightBuffer",0),endBuffer=tableFloat(L,t,"LineOfSightEndBuffer",0);
            if(!std::isfinite(width)||!std::isfinite(endBuffer))return fail("Nonfinite native notification LOS buffer");
            float tx=ax,ty=ay;bool clear=false;
            if(endBuffer>0&&(ax!=cx||ay!=cy)){
                const float ex=ax-cx,ey=ay-cy,length2=ex*ex+ey*ey;
                if(length2<endBuffer*endBuffer)clear=true;
                else{const float inverse=1.f/std::sqrt(length2);tx=ax-(inverse*ex)*endBuffer;ty=ay-(inverse*ey)*endBuffer;}
            }
            if(!clear){int32_t blocker=0;
                if(!lineOfSightBlocker(cx,cy,tx,ty,firstDestination,0,width,stopsProjectiles,stopsUnits,blocker,error)){lua_settop(L,top);return false;}
                if(blocker){lua_settop(L,top);error.clear();return true;}
            }
        }
        lua_settop(L,top);done=true;error.clear();return true;
    }
    if(!eachId(L,t,"Ids",addSource))return fail(inner);
    std::vector<int32_t> members;
    if(!eachName(L,t,"Names",[&](const std::string& name){if(!activeMapThingGroup(L,name,members,inner))return false;for(auto id:members)if(!addSource(id))return false;return true;}))return fail(inner);
    if(count<1){lua_settop(L,top);done=true;error.clear();return true;}
    const float cx=sx/float(count),cy=sy/float(count);
    const float maxZ=tableFloat(L,t,"MaxZ",std::numeric_limits<float>::max());
    int32_t hit=0;bool found=false;
    auto test=[&](int32_t id)->bool{
        if(found)return true;if(!available(actorWorld_.actor(id)))return true;P p;if(!place(id,p)){inner=error;return false;}
        if(maxZ<p.z)return true;
        const float scaleY=tableFloat(L,t,"ScaleY",0);bool within=false;
        if(scaleY<=0){
            float d2=0;
            if(tableBool(L,t,"OnlyX",false))d2=(cx-p.x)*(cx-p.x);
            else if(tableBool(L,t,"OnlyY",false))d2=(cy-p.y)*(cy-p.y);
            else d2=(cy-p.y)*(cy-p.y)+(cx-p.x)*(cx-p.x);
            const float distance=tableFloat(L,t,"Distance",0);within=d2<distance*distance;
        }else{
            const float distance=tableFloat(L,t,"Distance",0);
            const float f=((p.y-cy)*(p.y-cy))/(distance*scaleY*distance*scaleY)+((p.x-cx)*(p.x-cx))/(distance*distance);
            within=f<=1.0f;
        }
        if(within){found=true;hit=id;}return true;};
    if(!eachId(L,t,"DestinationIds",test))return fail(inner);
    if(!found&&!eachName(L,t,"DestinationNames",[&](const std::string& name){if(found)return true;if(!activeMapThingGroup(L,name,members,inner))return false;for(auto id:members)if(!test(id))return false;return true;}))return fail(inner);
    if(!found){lua_settop(L,top);error.clear();return true;}
    // NotifyResultsTable[Notify] = matched destination Thing ID (+0x6c).
    lua_getfield(L,t,"Notify");std::string name;if(lua_type(L,-1)==LUA_TSTRING||lua_type(L,-1)==LUA_TNUMBER){size_t n=0;const char* b=lua_tolstring(L,-1,&n);name.assign(b,n);}lua_pop(L,1);
    // Lua::NewTable RVA0x200360: lua_createtable(0,0), globals[name]=table
    // (replacing any previous NotifyResultsTable), then table[Notify]=ID.
    lua_newtable(L);lua_pushvalue(L,-1);lua_setglobal(L,"NotifyResultsTable");lua_pushinteger(L,hit);lua_setfield(L,-2,name.c_str());
    lua_settop(L,top);done=true;error.clear();return true;
}

// NotifyOnStopped (Action 244 -> Execute jump index 0x95, VA0x1802531a9): the
// same World notify registration as the distance checks (Timeout, retained
// argument table) with check ScriptAction::CheckStopped 0x2797f0
// (.local/reports/motion-CheckStopped.json). Each check re-reads
// GetActiveMapThings(Ids,Names): any Thing with a Physics component (+0xd8)
// whose velocity (+0x70/+0x74) is nonzero, or any obstacle (type mask 0x121)
// whose X/Y move helper is active with duration!=0 and remaining>=0, means
// "still moving". Otherwise (including an empty set) it is stopped. No
// NotifyResultsTable write.
int ScriptRuntime::stoppedNotifyAction(lua_State* L){
    auto* self=runtime(L);
    if(!self||!lua_istable(L,1))return luaL_error(L,"NotifyOnStopped requires source argument table");
    WorldNotify pending;pending.stopped=true;
    pending.timeout=tableFloat(L,1,"Timeout",std::numeric_limits<float>::max());
    if(!std::isfinite(pending.timeout))return luaL_error(L,"NotifyOnStopped Timeout must be finite");
    if(self->worldNotifies_.size()>=10000)return luaL_error(L,"Native notification queue bounds");
    lua_pushvalue(L,1);pending.arguments=luaL_ref(L,LUA_REGISTRYINDEX);self->worldNotifies_.push_back(std::move(pending));return 0;
}
bool ScriptRuntime::checkStoppedNotify(const WorldNotify& pending,bool& done,std::string& error){
    done=false;lua_State* L=state_;const int top=lua_gettop(L);
    lua_rawgeti(L,LUA_REGISTRYINDEX,pending.arguments);const int t=lua_absindex(L,-1);
    auto fail=[&](const std::string& m){lua_settop(L,top);error=m;return false;};
    if(!lua_istable(L,t))return fail("NotifyOnStopped lost its original argument table");
    lua_getfield(L,t,"Names");const bool named=!lua_isnil(L,-1);lua_pop(L,1);
    if(named)return fail("NotifyOnStopped name selection requires actual name-query provider");
    std::vector<int32_t> selected;
    auto add=[&](int index)->bool{int numeric=0;const double v=lua_tonumberx(L,index,&numeric);if(!numeric||!std::isfinite(v)||v!=std::floor(v)||v<INT32_MIN||v>INT32_MAX)return false;selected.push_back(int32_t(v));return true;};
    // ScriptArgs::Ids: integer Id, else the integers of the Ids table.
    lua_getfield(L,t,"Id");
    if(lua_isnumber(L,-1)){if(!add(-1))return fail("NotifyOnStopped requires signed32 object IDs");lua_pop(L,1);}
    else{lua_pop(L,1);lua_getfield(L,t,"Ids");if(lua_istable(L,-1)){const size_t n=lua_rawlen(L,-1);if(n>100000)return fail("NotifyOnStopped ID list bound");for(size_t i=1;i<=n;++i){lua_rawgeti(L,-1,int(i));const bool ok=add(-1);lua_pop(L,1);if(!ok)return fail("NotifyOnStopped requires signed32 object IDs");}}lua_pop(L,1);}
    lua_settop(L,top);
    bool moving=false;
    for(const auto id:selected){const auto* a=actorWorld_.actor(id);if(!available(a))continue;
        if(a->physics&&(a->physics->velocity.x!=0||a->physics->velocity.y!=0)){moving=true;break;}
        if(a->definition&&a->definition->kind=="Obstacle"){
            const auto m=actorMotion_.obstacles.find(a->entityHandle);
            if(m!=actorMotion_.obstacles.end()){
                const auto busy=[](const hades::ObstacleMoveAxis& x){return x.active&&x.duration!=0&&!(x.remaining<0);};
                if(busy(m->second.x)||busy(m->second.y)){moving=true;break;}
            }
        }
    }
    done=!moving;error.clear();return true;
}

// ScriptAction.Execute case0x9d registers CheckOnAllDead VA18027ce60 with
// retained ScriptArgs and standard Timeout. Singular Id/Name are diagnostics
// only: each check rereads contiguous Ids and Names arrays. Missing Things
// count as dead; actual Thing::IsAlive reads LifeStatus+0x238==ALIVE0.
int ScriptRuntime::allDeadNotifyAction(lua_State* L){
    auto* self=runtime(L);if(!self||!lua_istable(L,1))return luaL_error(L,"NotifyOnAllDead requires source argument table");
    WorldNotify pending;pending.allDead=true;pending.timeout=tableFloat(L,1,"Timeout",std::numeric_limits<float>::max());
    if(!std::isfinite(pending.timeout))return luaL_error(L,"NotifyOnAllDead Timeout must be finite");
    if(self->worldNotifies_.size()>=10000)return luaL_error(L,"Native notification queue bounds");
    lua_pushvalue(L,1);pending.arguments=luaL_ref(L,LUA_REGISTRYINDEX);self->worldNotifies_.push_back(std::move(pending));return 0;
}
bool ScriptRuntime::checkAllDeadNotify(const WorldNotify& pending,bool& done,std::string& error){
    done=false;lua_State* L=state_;const int top=lua_gettop(L);lua_rawgeti(L,LUA_REGISTRYINDEX,pending.arguments);const int t=lua_absindex(L,-1);
    auto fail=[&](const std::string& detail){lua_settop(L,top);error=detail;return false;};
    if(!lua_istable(L,t))return fail("NotifyOnAllDead lost its original argument table");
    bool alive=false;std::string inner;
    const auto check=[&](int32_t id){const auto* a=actorWorld_.actor(id);if(a&&a->registered&&a->object&&a->lifeStatus==0)alive=true;return true;};
    if(!eachId(L,t,"Ids",check))return fail(inner);
    if(alive){lua_settop(L,top);error.clear();return true;}
    std::vector<int32_t> members;
    if(!eachName(L,t,"Names",[&](const std::string& name){
        if(alive)return true;if(!activeMapThingGroup(L,name,members,inner))return false;
        for(auto id:members)check(id);if(alive)return true;
        // CheckOnAllDead VA18027d2f1 calls CarryingSpawnWithGroup7ad30 even
        // for an empty group. That method scans active AND pending manager
        // lists: SpawnOnDetonate nonempty, SpawnType!=PROJECTILE3, then living
        // Unit owner membership or SpawnsInheritGroups projectile membership.
        for(auto handle:projectileWorld_.bodyComponents()){
            const auto* p=projectileWorld_.actor(handle);if(!p||(!p->active&&!p->pending))continue;
            if(!p->firingArsenal){inner="AllDead carrying-spawn query lost actual firing Arsenal";return false;}
            const auto* data=p->firingArsenal->projectileData(p->request.weaponName,p->spawnedDefinition,inner);if(!data)return false;
            const auto text=data->fields.find("SpawnOnDetonate"),type=data->fields.find("SpawnType"),inherit=data->fields.find("SpawnsInheritGroups");
            if(text==data->fields.end()||!std::holds_alternative<std::string>(text->second)||type==data->fields.end()||!std::holds_alternative<double>(type->second)||inherit==data->fields.end()||!std::holds_alternative<bool>(inherit->second)){inner="AllDead native carrying-spawn scalar data unavailable";return false;}
            if(std::get<std::string>(text->second).empty()||std::get<double>(type->second)==3)continue;
            const auto* owner=actorWorld_.actor(p->state.ownerId);
            if(owner&&owner->registered&&owner->object&&owner->definition&&owner->definition->kind=="Unit"&&owner->lifeStatus==0&&std::find(owner->object->groups.begin(),owner->object->groups.end(),name)!=owner->object->groups.end()){alive=true;break;}
            if(std::get<bool>(inherit->second)&&std::find(p->body.groups.begin(),p->body.groups.end(),name)!=p->body.groups.end()){alive=true;break;}
        }
        return true;
    }))return fail(inner);
    lua_settop(L,top);done=!alive;error.clear();return true;
}

bool ScriptRuntime::unitAttackAllowed(int32_t id,bool editing,bool primaryGate,bool& allowed,std::string& error){
    allowed=false;const auto* actor=actorWorld_.actor(id);
    if(!actor||!actor->registered||!actor->object||!actor->definition||actor->definition->kind!="Unit")return true;
    if(!actor->status){error="Actual UnitStatus unavailable for CanAttack";return false;}
    const auto* immune=actorWorld_.currentProperty(id,"ImmuneToStun");
    if(!immune){error="Actual CanAttack ImmuneToStun unavailable";return false;}
    // ScriptAction.CanAttack276f51..27702f is UnitStatus.CanAttack(false),
    // without UnitData.CanAttack or the animation gate. CheckCanAttack27ea8d
    // additionally rejects current primary DisableOwnerWeapons(+84).
    if(primaryGate){
        if(actor->animation.active()){if(actor->animation.clip()->flags&hades::ActorDisableWeapons)return true;}
        else if(actor->primaryAnimationLifetime||actor->primaryEffectAnimation){if(!actor->primaryDisableWeapons){error="NotifyOnCanAttack actual primary weapon flag unavailable";return false;}if(*actor->primaryDisableWeapons)return true;}
    }
    allowed=actor->status->canAttack(editing,immune->boolean(),false);return true;
}
bool ScriptRuntime::unitAIQueryImpl(lua_State* L,int table,int mode,bool& result,std::string& error){try{
    table=lua_absindex(L,table);if(!lua_istable(L,table))throw std::runtime_error("Native Unit query requires argument table");result=false;
    bool editing=false;if(mode==0){const int top=lua_gettop(L);lua_getglobal(L,"GetConfigOptionValue");lua_newtable(L);lua_pushliteral(L,"EditingMode");lua_setfield(L,-2,"Name");if(lua_pcall(L,1,1,0)!=LUA_OK){error=lua_tostring(L,-1);lua_settop(L,top);return false;}if(!lua_isboolean(L,-1)){lua_settop(L,top);throw std::runtime_error("Actual EditingMode boolean unavailable");}editing=lua_toboolean(L,-1);lua_settop(L,top);}
    const auto check=[&](int32_t id){if(result)return true;const auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->definition||a->definition->kind!="Unit")return true;
        // ScriptAction.IsCharmed2778c0 reads UnitStatus.mIsTraitor(+90),
        // live Unit+4f0. Neither query tests life status or weapon cooldown.
        if(mode==1){if(!a->status){error="Actual UnitStatus unavailable for IsCharmed";return false;}result=a->status->isTraitor;return true;}
        return unitAttackAllowed(id,editing,false,result,error);};
    lua_getfield(L,table,"Id");const bool numeric=lua_isnumber(L,-1);const auto id=numeric?lua_tointeger(L,-1):0;lua_pop(L,1);
    if(numeric){if(id>=INT32_MIN&&id<=INT32_MAX&&!check(int32_t(id)))return false;}
    else if(!eachId(L,table,"Ids",check))return false;
    return true;
}catch(const std::exception& e){error=e.what();return false;}}
int ScriptRuntime::unitAIQueryAction(lua_State* L){char message[2048]{};bool ok=false,result=false;{std::string error;ok=runtime(L)->unitAIQueryImpl(L,1,int(lua_tointeger(L,lua_upvalueindex(2))),result,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}if(!ok)return luaL_error(L,"%s",message);lua_pushboolean(L,result);return 1;}
int ScriptRuntime::unitStateNotifyAction(lua_State* L){
    auto* self=runtime(L);if(!self||!lua_istable(L,1))return luaL_error(L,"Native Unit notification requires argument table");
    WorldNotify pending;pending.unitState=uint8_t(lua_tointeger(L,lua_upvalueindex(2)));pending.timeout=tableFloat(L,1,"Timeout",std::numeric_limits<float>::max());
    if(!std::isfinite(pending.timeout)||self->worldNotifies_.size()>=10000)return luaL_error(L,"Native Unit notification timeout/queue bounds");
    lua_pushvalue(L,1);pending.arguments=luaL_ref(L,LUA_REGISTRYINDEX);self->worldNotifies_.push_back(std::move(pending));return 0;
}
bool ScriptRuntime::checkUnitStateNotify(const WorldNotify& pending,bool& done,std::string& error){
    const int top=lua_gettop(state_);lua_rawgeti(state_,LUA_REGISTRYINDEX,pending.arguments);const int table=lua_absindex(state_,-1);
    const auto fail=[&](const std::string& detail){error=detail;lua_settop(state_,top);return false;};
    if(!lua_istable(state_,table))return fail("Native Unit notification argument table unavailable");
    if(pending.unitState==2){
        // CheckRotationComplete279a40: singular Id only. A missing source
        // completes. Unit requires exact body==goal; Cosmetic additionally
        // waits for actual cosmetic angle unless CosmeticallyTargetPlayerUnit.
        lua_getfield(state_,table,"Id");const auto id=lua_tointeger(state_,-1);lua_pop(state_,1);const auto* a=id>=INT32_MIN&&id<=INT32_MAX?actorWorld_.actor(int32_t(id)):nullptr;done=true;
        if(a&&a->registered&&a->object){
            if(!a->definition||a->definition->kind!="Unit")return fail("NotifyOnRotationComplete non-Unit Body angular transition check not reconstructed");
            const auto movement=localMovement_.find(a->entityHandle);const float goal=movement==localMovement_.end()?a->object->angle:movement->second.goalAngle;
            done=a->object->angle==goal;
            const auto* targetsPlayer=actorWorld_.currentProperty(a->id,"CosmeticallyTargetPlayerUnit");if(!targetsPlayer)return fail("Actual cosmetic targeting flag unavailable");
            if(done&&!targetsPlayer->boolean()&&tableBool(state_,table,"Cosmetic",false))done=(movement==localMovement_.end()?a->object->angle:movement->second.cosmeticAngle)==a->object->angle;
        }
        lua_settop(state_,top);return true;
    }
    // CheckCanAttack27e960 selects actual active MapThings by IDs/names,
    // ignores non-Units, and finishes only when EVERY selected Unit permits
    // attacking. An empty selection completes, unlike CanAttack's any query.
    done=true;std::vector<int32_t> selected,members;std::string detail;
    // ScriptArgs.Ids2818f0 / Names2817f0 select the plural key when present,
    // otherwise the singular key. They do not union both spellings.
    lua_getfield(state_,table,"Ids");const bool pluralIds=!lua_isnil(state_,-1);lua_pop(state_,1);const char* idKey=pluralIds?"Ids":"Id";
    lua_getfield(state_,table,idKey);const bool idList=lua_istable(state_,-1);if(lua_isnumber(state_,-1)){const auto id=lua_tointeger(state_,-1);if(id>=INT32_MIN&&id<=INT32_MAX)selected.push_back(int32_t(id));}else if(!lua_isnil(state_,-1)&&!idList){lua_pop(state_,1);return fail("Native Unit notification IntList nonnumeric-string selector not reconstructed");}lua_pop(state_,1);
    if(idList&&!eachId(state_,table,idKey,[&](int32_t id){selected.push_back(id);return true;}))return fail("Unit notification ID selection failed");
    const auto names=[&](const std::string& name){if(!activeMapThingGroup(state_,name,members,detail))return false;selected.insert(selected.end(),members.begin(),members.end());return true;};
    lua_getfield(state_,table,"Names");const bool pluralNames=!lua_isnil(state_,-1);lua_pop(state_,1);const char* nameKey=pluralNames?"Names":"Name";
    lua_getfield(state_,table,nameKey);const bool nameList=lua_istable(state_,-1);if(lua_isstring(state_,-1)){const std::string name=lua_tostring(state_,-1);if(!names(name)){lua_pop(state_,1);return fail(detail);}}lua_pop(state_,1);
    if(nameList&&!eachName(state_,table,nameKey,names))return fail(detail);
    lua_getglobal(state_,"GetConfigOptionValue");lua_newtable(state_);lua_pushliteral(state_,"EditingMode");lua_setfield(state_,-2,"Name");if(lua_pcall(state_,1,1,0)!=LUA_OK)return fail(lua_tostring(state_,-1));if(!lua_isboolean(state_,-1))return fail("Actual EditingMode boolean unavailable");const bool editing=lua_toboolean(state_,-1);lua_pop(state_,1);
    for(const auto id:selected){const auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object||!a->object->active||!a->definition||a->definition->kind!="Unit")continue;bool allowed=false;if(!unitAttackAllowed(id,editing,true,allowed,detail))return fail(detail);if(!allowed){done=false;break;}}
    lua_settop(state_,top);return true;
}

bool ScriptRuntime::distanceQueryImpl(lua_State* L,float& result,std::string& error){try{
    if(!lua_istable(L,1))throw std::runtime_error("GetDistance requires argument table");
    // ScriptAction.GetDistance279380: arithmetic means of existing source
    // and destination Things, projected Y-=Z*0.8660254 on BOTH sets when
    // UseLocationZ; OnlyX precedes OnlyY; missing either set returns zero.
    for(const char* key:{"Name","Names","DestinationName","DestinationNames"}){lua_getfield(L,1,key);const bool named=!lua_isnil(L,-1);lua_pop(L,1);if(named)throw std::runtime_error("GetDistance named selector diagnostic branch not reconstructed");}
    const bool useZ=tableBool(L,1,"UseLocationZ",false);float sx=0,sy=0,dx=0,dy=0;int sn=0,dn=0;
    const auto mean=[&](const char* one,const char* many,float& x,float& y,int& count){
        const auto add=[&](int32_t id){const auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object)return true;float px=0,py=0,z=0;if(!actorWorld_.location(id,px,py,error)||!actorWorld_.zLocation(id,z,error))return false;x+=px;y+=py;if(useZ)y-=z*0.8660254f;++count;return true;};
        lua_getfield(L,1,many);const bool plural=!lua_isnil(L,-1);lua_pop(L,1);const char* key=plural?many:one;
        lua_getfield(L,1,key);const bool list=lua_istable(L,-1);if(lua_isnumber(L,-1)){const auto id=lua_tointeger(L,-1);lua_pop(L,1);if(id>=INT32_MIN&&id<=INT32_MAX)return add(int32_t(id));return true;}if(!lua_isnil(L,-1)&&!list){lua_pop(L,1);error="GetDistance native IntList nonnumeric-string selector not reconstructed";return false;}lua_pop(L,1);return !list||eachId(L,1,key,add);
    };
    result=0;if(!mean("Id","Ids",sx,sy,sn))return false;if(!sn)return true;if(!mean("DestinationId","DestinationIds",dx,dy,dn))return false;if(!dn)return true;
    sx/=float(sn);sy/=float(sn);dx/=float(dn);dy/=float(dn);
    if(tableBool(L,1,"OnlyX",false))result=std::abs(sx-dx);else if(tableBool(L,1,"OnlyY",false))result=std::abs(sy-dy);else{const float x=sx-dx,y=sy-dy;result=std::sqrt(x*x+y*y);}return true;
}catch(const std::exception& e){error=e.what();return false;}}
int ScriptRuntime::distanceQueryAction(lua_State* L){char message[2048]{};bool ok=false;float result=0;{std::string error;ok=runtime(L)->distanceQueryImpl(L,result,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}if(!ok)return luaL_error(L,"%s",message);lua_pushnumber(L,result);return 1;}

bool ScriptRuntime::intersectingIdsImpl(lua_State* L,std::vector<int32_t>& result,std::string& error){try{
    if(!lua_istable(L,1))throw std::runtime_error("GetIntersectingIds requires argument table");
    const auto selected=[&](const char* one,const char* many,const char* name,const char* names,std::vector<int32_t>& out){
        const auto add=[&](int32_t id){const auto* a=actorWorld_.actor(id);if(a&&a->registered&&a->object&&a->object->active&&!a->object->queryProxy&&std::find(out.begin(),out.end(),id)==out.end())out.push_back(id);return true;};
        lua_getfield(L,1,many);const bool plural=!lua_isnil(L,-1);lua_pop(L,1);const char* idKey=plural?many:one;
        lua_getfield(L,1,idKey);const bool list=lua_istable(L,-1);if(lua_isnumber(L,-1)){const auto id=lua_tointeger(L,-1);if(id>=INT32_MIN&&id<=INT32_MAX)add(int32_t(id));}else if(!lua_isnil(L,-1)&&!list){lua_pop(L,1);error="GetIntersectingIds IntList nonnumeric-string selector not reconstructed";return false;}lua_pop(L,1);if(list&&!eachId(L,1,idKey,add))return false;
        const auto group=[&](const std::string& n){std::vector<int32_t> members;if(!activeMapThingGroup(L,n,members,error))return false;for(auto id:members)add(id);return true;};
        lua_getfield(L,1,names);const bool pluralNames=!lua_isnil(L,-1);lua_pop(L,1);const char* nameKey=pluralNames?names:name;
        lua_getfield(L,1,nameKey);const bool nameList=lua_istable(L,-1);if(lua_isstring(L,-1)){const std::string n=lua_tostring(L,-1);if(!group(n)){lua_pop(L,1);return false;}}lua_pop(L,1);return !nameList||eachName(L,1,nameKey,group);
    };
    std::vector<int32_t> sources,destinations;if(!selected("Id","Ids","Name","Names",sources)||!selected("DestinationId","DestinationIds","DestinationName","DestinationNames",destinations))return false;
    result.clear();
    // GetIntersectingIds26af00 copies each SOURCE Thing polygon and tests
    // each DESTINATION Thing.GetLocation as a point; it is not polygon overlap.
    // Native PointInPolygon2ceaf0 uses these exact >= half-open crossings.
    for(auto source:sources){if(!actorWorld_.refreshGeometry(source,error))return false;const auto* a=actorWorld_.actor(source);if(!a->geometryAvailable){error="GetIntersectingIds actual source polygon unavailable";return false;}const auto& p=a->geometry;if(p.empty())continue;
        for(auto destination:destinations){float x=0,y=0;if(!actorWorld_.location(destination,x,y,error))return false;bool inside=false;auto previous=p.back();bool previousAbove=previous.y>=y;
            for(const auto& current:p){const bool above=current.y>=y;if(previousAbove!=above){const float lhs=(previous.x-current.x)*(current.y-y),rhs=(previous.y-current.y)*(current.x-x);if((lhs>=rhs)==above)inside=!inside;}previous=current;previousAbove=above;}
            if(inside)result.push_back(destination);
        }
    }
    // Native nested source/destination pointer-hash traversal appends matches,
    // including repeats across sources. Preserve that uncertainty explicitly
    // when return ordering can affect downstream Destroy callbacks/RNG.
    if(result.size()>1){error="GetIntersectingIds multiple matches require original pointer-hash traversal order";return false;}
    return true;
}catch(const std::exception& e){error=e.what();return false;}}
int ScriptRuntime::intersectingIdsAction(lua_State* L){char message[2048]{};bool ok=false;{std::string error;std::vector<int32_t> ids;ok=runtime(L)->intersectingIdsImpl(L,ids,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());else{lua_newtable(L);lua_pushvalue(L,-1);lua_setglobal(L,"IdsTable");for(size_t i=0;i<ids.size();++i){lua_pushinteger(L,ids[i]);lua_rawseti(L,-2,lua_Integer(i+1));}}}if(!ok)return luaL_error(L,"%s",message);return 1;}

// GridManager::GetLineOfSightBlocker VA1803076a0 (matching DLL/PDB evidence
// combat-GetLineOfSightBlocker-static.json). Equal endpoints return none;
// FromThickLine62e10 supplies the ±width/2 normal. StopsProjectiles selects
// that grid list and skips ProjectilesAlwaysPenetrate; StopsUnits additionally
// tests virtual Obstacle::StopsUnits and excludes Life.DieOnCollision.
// APPROXIMATIONS (existing portable collision policy): conservative sparse-grid
// swept AABB replaces the native integer segment-rectangle/cell/hash traversal.
// It must include thick-line width because this grid also filters float AABBs.
// Narrowphase uses the existing portable segment/polygon and polygon-overlap
// predicates, not native floating instruction equivalence. Data Tallness is
// the existing portable representation of the live Thing+0x78 height query.
bool ScriptRuntime::lineOfSightBlocker(float x0,float y0,float x1,float y1,int32_t ignore,float z,float width,bool stopsProjectiles,bool stopsUnits,int32_t& blocker,std::string& error){
    try{
        blocker=0;
        for(float value:{x0,y0,x1,y1,z,width})if(!std::isfinite(value))throw std::runtime_error("Nonfinite native LOS input");
        if(x0==x1&&y0==y1){error.clear();return true;}
        std::vector<hades::MovementPoint> thick;
        hades::SpatialBounds bounds{std::min(x0,x1),std::min(y0,y1),std::max(x0,x1),std::max(y0,y1)};
        if(width!=0){
            float dx=x1-x0,dy=y1-y0;const float length=std::sqrt(dy*dy+dx*dx);
            if(!(length>0)||!std::isfinite(length))throw std::runtime_error("Native LOS line length outside finite portable bounds");
            dx/=length;dy/=length;const float nx=dy*(width*-.5f),ny=dx*(width*.5f);
            thick={{x0+nx,y0+ny},{x1+nx,y1+ny},{x1-nx,y1-ny},{x0-nx,y0-ny}};
            for(const auto p:thick){bounds.minX=std::min(bounds.minX,p.x);bounds.minY=std::min(bounds.minY,p.y);bounds.maxX=std::max(bounds.maxX,p.x);bounds.maxY=std::max(bounds.maxY,p.y);}
        }
        std::vector<int32_t> candidates;
        if(!actorWorld_.spatialObstacleCandidates(bounds,stopsProjectiles?hades::ObstacleGridCategory::StopsProjectiles:hades::ObstacleGridCategory::All,candidates,error))return false;
        const auto property=[&](int32_t id,const char* key)->const hades::SourceValue&{const auto* value=actorWorld_.currentProperty(id,key);if(!value)throw std::runtime_error(std::string("Native LOS source property unavailable: ")+key);return *value;};
        const auto cross=[](hades::ActorPoint a,hades::ActorPoint b,hades::ActorPoint c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
        const auto onSegment=[&](hades::ActorPoint a,hades::ActorPoint b,hades::ActorPoint p){return cross(a,b,p)==0&&p.x>=std::min(a.x,b.x)&&p.x<=std::max(a.x,b.x)&&p.y>=std::min(a.y,b.y)&&p.y<=std::max(a.y,b.y);};
        for(const auto id:candidates){
            if(id==ignore)continue;const auto* obstacle=actorWorld_.actor(id);
            if(!obstacle||!obstacle->registered||!obstacle->object||!obstacle->definition||obstacle->definition->kind!="Obstacle")continue;
            // VA18030785c..78ab: positive Z skips only strictly shorter
            // obstacles; equality blocks. Both stopping flags can be required.
            if(z>0&&z>float(property(id,"Thing.Tallness").number()))continue;
            if(stopsProjectiles&&property(id,"Life.ProjectilesAlwaysPenetrate").boolean())continue;
            if(stopsUnits){bool stops=false;if(!actorWorld_.obstacleStops(id,false,stops,error))return false;if(!stops||property(id,"Life.DieOnCollision").boolean())continue;}
            if(!obstacle->geometryAvailable){if(obstacle->geometryRequired)throw std::runtime_error("Native LOS blocker polygon unavailable");continue;}
            bool hit=false;
            if(width==0){
                // Same portable thin-line predicate as projectile collision:
                // endpoint containment plus proper/collinear/touching edges.
                const hades::ActorPoint p{x0,y0},q{x1,y1};
                hit=contains(obstacle->geometry,x0,y0)||contains(obstacle->geometry,x1,y1);
                if(!obstacle->geometry.empty()){auto a=obstacle->geometry.back();
                    for(const auto b:obstacle->geometry){const float pa=cross(p,q,a),pb=cross(p,q,b),ap=cross(a,b,p),aq=cross(a,b,q);
                        if((((pa>0&&pb<0)||(pa<0&&pb>0))&&((ap>0&&aq<0)||(ap<0&&aq>0)))||onSegment(p,q,a)||onSegment(p,q,b)||onSegment(a,b,p)||onSegment(a,b,q)){hit=true;break;}a=b;}
                }
            }else{
                std::vector<hades::MovementPoint> shape;for(const auto point:obstacle->geometry)shape.push_back({point.x,point.y});
                hit=hades::movementPolygonsOverlap(shape,thick);
            }
            if(hit){blocker=id;break;}
        }
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
