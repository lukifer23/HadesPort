#include "lua_runtime.hpp"
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
bool live(const hades::ActorComponents* a){return a&&a->registered&&a->object&&a->object->active&&a->object->alive&&!a->object->queryProxy;}
bool flag(lua_State* L,const char* key){lua_getfield(L,1,key);const bool result=lua_toboolean(L,-1);lua_pop(L,1);return result;}
float number(lua_State* L,const char* key,float fallback=0){lua_getfield(L,1,key);if(lua_isnil(L,-1)){lua_pop(L,1);return fallback;}int numeric=0;const double n=lua_tonumberx(L,-1,&numeric);lua_pop(L,1);if(!numeric||!std::isfinite(n)||std::abs(n)>std::numeric_limits<float>::max())throw std::runtime_error(std::string("Invalid spatial query scalar: ")+key);return float(n);}
}
bool ScriptRuntime::spatialQueryImpl(lua_State* L,int mode,double& result,std::string& error){try{
    const bool angle=mode==1;
    if(!lua_istable(L,1))throw std::runtime_error("Spatial query requires source argument table");result=0;
    if(mode==2){
        // ScriptAction::GetClosestUnitOfType VA180278c10: singular Id wins;
        // otherwise average extant World Things from contiguous Ids entries.
        // Names/DestinationId arguments are checked for diagnostics only.
        float x=0,y=0;size_t count=0;
        const auto add=[&](int32_t id)->bool{const auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object)return true;float ax=0,ay=0;if(!actorWorld_.location(id,ax,ay,error))return false;x+=ax;y+=ay;++count;return true;};
        lua_getfield(L,1,"Id");int hasId=0;const auto source=lua_tointegerx(L,-1,&hasId);lua_pop(L,1);
        if(hasId){if(source>=INT32_MIN&&source<=INT32_MAX&&!add(int32_t(source)))return false;}
        else{lua_getfield(L,1,"Ids");if(lua_istable(L,-1))for(int i=1;;++i){if(i>100000)throw std::runtime_error("GetClosestUnitOfType source list bound");lua_rawgeti(L,-1,i);if(lua_isnil(L,-1)){lua_pop(L,1);break;}int numeric=0;const auto id=lua_tointegerx(L,-1,&numeric);lua_pop(L,1);if(numeric&&id>=INT32_MIN&&id<=INT32_MAX&&!add(int32_t(id))){lua_pop(L,1);return false;}}lua_pop(L,1);}
        if(!count)return true;x/=float(count);y/=float(count);
        // ScriptArgs::DestinationNames2819d0 gives plural-key presence priority.
        std::vector<std::string> names;lua_getfield(L,1,"DestinationNames");if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,1,"DestinationName");}
        const auto name=[&](int at){if(lua_isstring(L,at))names.emplace_back(lua_tostring(L,at));};
        if(lua_istable(L,-1))for(int i=1;;++i){if(i>100000)throw std::runtime_error("GetClosestUnitOfType name list bound");lua_rawgeti(L,-1,i);if(lua_isnil(L,-1)){lua_pop(L,1);break;}name(-1);lua_pop(L,1);}else name(-1);lua_pop(L,1);
        const float maximum=number(L,"Distance"),minimum=number(L,"MinDistance"),max2=maximum*maximum,min2=minimum*minimum;
        float best=std::numeric_limits<float>::max();
        // Native scans UnitManager::ALL_UNITS, exact UnitData.Name equality,
        // inclusive distance limits; no alive, allegiance, or IgnoreSelf gate.
        // Portable manager registration order preserves the first distance tie.
        for(const auto id:actorWorld_.registrationOrder()){const auto* a=actorWorld_.actor(id);if(!a||!a->registered||!a->object||!a->definition||a->definition->kind!="Unit"||std::find(names.begin(),names.end(),a->definition->name)==names.end())continue;
            float ax=0,ay=0;if(!actorWorld_.location(id,ax,ay,error))return false;const float dx=x-ax,dy=y-ay,d2=dy*dy+dx*dx;
            if((max2>0&&d2>max2)||(min2>0&&d2<min2))continue;
            if(d2<best){best=d2;result=float(id);}}
        return true;
    }
    std::vector<int32_t> sources,destinations;
    if(angle){
        // Selected source path reads singular IDs before the separate array
        // path. Array missing-ID denominator and native debug-name semantics
        // remain explicit rather than silently inventing a generalized query.
        for(const char* key:{"Ids","DestinationIds","Name","Names","DestinationName","DestinationNames"}){lua_getfield(L,1,key);const bool supplied=!lua_isnil(L,-1);lua_pop(L,1);if(supplied)throw std::runtime_error("GetAngleBetween array/group path requires its native query adapter");}
        for(auto entry:{std::pair{"Id",&sources},std::pair{"DestinationId",&destinations}}){const float id=number(L,entry.first);if(id>0&&double(id)<=INT32_MAX&&id==std::trunc(id))entry.second->push_back(int32_t(id));}
    }else{
        if(!resolveObjectTargets(L,false,sources,error)||!resolveObjectTargets(L,true,destinations,error))return false;
        for(const char* key:{"IgnoreHomingIneligible","IgnoreInvulnerable","RequiredLocationUnblocked"})if(flag(L,key))throw std::runtime_error(std::string("GetClosest native filter requires its actual provider: ")+key);
    }
    float x=0,y=0;int32_t lastSource=0;size_t count=0;
    for(auto id:sources){if(!live(actorWorld_.actor(id)))continue;float px=0,py=0;if(!actorWorld_.location(id,px,py,error))return false;x+=px;y+=py;++count;lastSource=id;}
    if(!count)return true;x/=float(count);y/=float(count);
    if(angle){
        if(destinations.empty()||!live(actorWorld_.actor(destinations.front())))return true;
        float dx=0,dy=0;if(!actorWorld_.location(destinations.front(),dx,dy,error))return false;
        if(x==dx&&y==dy)return true;
        constexpr float tau=6.2831854820251465f;float radians=std::atan2(-(dy-y),dx-x);radians-=std::floor(radians/tau)*tau;
        result=double(radians)*57.29577951308232;return true;
    }
    const bool ignoreSelf=flag(L,"IgnoreSelf");if(ignoreSelf&&count>1)throw std::runtime_error("GetClosest multi-source IgnoreSelf depends on native pointer hash iteration");
    const float distance=number(L,"Distance"),limit=distance*distance;float best=std::numeric_limits<float>::max();
    // ScriptAction::GetClosest VA180278180 (.local/reports/native-script-get-closest-pseudocode-v1.txt):
    // StopsUnits / StopsProjectiles admit only Obstacle-typed destinations (type
    // mask 0x121) whose Obstacle::StopsUnits2c55b0 / StopsProjectiles2c5640 holds.
    const bool stopsUnits=flag(L,"StopsUnits"),stopsProjectiles=flag(L,"StopsProjectiles");
    for(auto id:destinations){if(!live(actorWorld_.actor(id))||(ignoreSelf&&id==lastSource))continue;
        if(stopsUnits||stopsProjectiles){const auto* d=actorWorld_.actor(id);if(!d->definition||d->definition->kind!="Obstacle")continue;bool stops=false;
            if(stopsUnits){if(!actorWorld_.obstacleStops(id,false,stops,error))return false;if(!stops)continue;}
            if(stopsProjectiles){if(!actorWorld_.obstacleStops(id,true,stops,error))return false;if(!stops)continue;}}
        float px=0,py=0;if(!actorWorld_.location(id,px,py,error))return false;const float dx=x-px,dy=y-py,d2=dy*dy+dx*dx;if(limit>0&&d2>limit)continue;if(d2<best){best=d2;result=float(id);}}
    // Distinct equal-distance candidates retain portable sorted-ID order; the
    // source's pointer hash order cannot be inferred from Windows addresses.
    return true;
}catch(const std::exception& e){error=e.what();return false;}}
int ScriptRuntime::spatialQueryAction(lua_State* L){
    char message[2048]{};bool ok=false;double result=0;{std::string error;ok=runtime(L)->spatialQueryImpl(L,int(lua_tointeger(L,lua_upvalueindex(2))),result,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    if(!ok)return luaL_error(L,"%s",message);lua_pushnumber(L,result);return 1;
}
namespace {
// ScriptArgs::Ids RVA0x2818f0: IntList(Ids) when Ids is present, else IntList(Id).
std::vector<int32_t> scriptArgIds(lua_State* L){
    std::vector<int32_t> out;lua_getfield(L,1,"Ids");if(lua_isnil(L,-1)){lua_pop(L,1);lua_getfield(L,1,"Id");}
    auto add=[&](int at){if(!lua_isnumber(L,at))return;const double v=lua_tonumber(L,at);if(std::isfinite(v)&&v>=INT32_MIN&&v<=INT32_MAX)out.push_back(int32_t(v));};
    if(lua_istable(L,-1)){const size_t n=lua_rawlen(L,-1);if(n>100000)throw std::runtime_error("Script ID list bound");for(size_t i=1;i<=n;++i){lua_rawgeti(L,-1,int(i));add(-1);lua_pop(L,1);}}
    else add(-1);
    lua_pop(L,1);return out;
}
}
// mode0 ExecuteBool case0xdd IsAlive: any ScriptArgs::Ids entry present in
//   World+0x398 whose Thing::IsAlive (vtable+0xd0, VA18002c720: LifeStatus
//   byte+0x238 == ALIVE) holds.
// mode1 ExecuteBool case0xe2 IsHorizontallyFlipped: any GetActiveMapThings(
//   Names, Ids) Thing whose body (+0xc8) flag byte+0x69 bit0 is set; that is
//   the same bit BodyComponent::FlipHorizontally RVA0x2b7780 toggles.
// mode2 ScriptAction::GetAngle RVA0x26cfd0: GetActiveMapThings(Names, Ids);
//   sum body angle (+0xc8 -> +0x54 radians, missing body counts 0), divide by
//   the set size and scale by 57.29578f; empty selection returns 0.
bool ScriptRuntime::thingQueryImpl(lua_State* L,int mode,double& result,std::string& error){try{
    if(!lua_istable(L,1))throw std::runtime_error("Thing query requires source argument table");result=0;
    if(mode==0){
        for(const auto id:scriptArgIds(L)){const auto* a=actorWorld_.actor(id);if(live(a)&&a->lifeStatus==0){result=1;break;}}
        return true;
    }
    std::vector<int32_t> selected;if(!resolveObjectTargets(L,false,selected,error))return false;
    float sum=0;size_t count=0;
    for(const auto id:selected){
        const auto* a=actorWorld_.actor(id);if(!live(a))continue;
        if(mode==1&&a->object->flipHorizontal){result=1;return true;}
        sum+=a->object->angle;++count;
    }
    if(mode==2&&count)result=double((sum/float(count))*57.29578f);
    return true;
}catch(const std::exception& e){error=e.what();return false;}}
int ScriptRuntime::thingQueryAction(lua_State* L){
    char message[2048]{};bool ok=false;double result=0;const int mode=int(lua_tointeger(L,lua_upvalueindex(2)));
    {std::string error;ok=runtime(L)->thingQueryImpl(L,mode,result,error);if(!ok)std::snprintf(message,sizeof(message),"%s",error.c_str());}
    if(!ok)return luaL_error(L,"%s",message);
    if(mode==2)lua_pushnumber(L,result);else lua_pushboolean(L,result!=0);return 1;
}
