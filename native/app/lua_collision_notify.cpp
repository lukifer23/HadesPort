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
        if(pending.playerInput){overlap=playerInputThisFrame_;}
        else if(!pending.control&&!pending.distance&&!pending.stopped&&!pending.allDead)for(int32_t sourceId:pending.sources){auto* source=actorWorld_.actor(sourceId);if(!available(source))continue;
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
        const bool satisfied=pending.allDead?allDeadDone:pending.stopped?stoppedDone:pending.distance?distanceDone:pending.control?controlMatched:(pending.whenSeparated?!overlap:overlap);
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
        float dx=0,dy=0;int dcount=0;
        auto addDestination=[&](int32_t id)->bool{
            if(!available(actorWorld_.actor(id)))return true;P p;if(!place(id,p)){inner=error;return false;}
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
        if(tableBool(L,t,"StopsProjectiles",false)||tableBool(L,t,"StopsUnits",false))
            return fail("NotifyWithinDistance StopsProjectiles/StopsUnits requires native GridManager::GetLineOfSightBlocker provider");
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
