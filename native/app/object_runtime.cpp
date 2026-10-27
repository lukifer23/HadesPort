#include "object_runtime.hpp"
#include "scene_runtime.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
constexpr const char* registryKey = "hades.objectRuntime";
constexpr const char* metatableName = "hades.objectRuntime.owner";
constexpr const char* luaTablesKey = "hades.objectLuaTables";
struct ObjectState {
    std::map<int32_t, ObjectSnapshot> objects;
    std::vector<int32_t> insertionOrder;
    std::map<std::string, ObjectDefinition> definitions;
    std::map<std::string, ObjectAnimationDefinition> animations;
    int32_t nextId = 2000001,nextUnitId=1000001;
    bool mapSeeded = false;
    ObjectGeometryChanged geometryChanged;
    ObjectAnimationSet animationSet;
    ObjectAnimationArgs worldAnimationArgs;
    ObjectDestroy destroy;
    ObjectMembership member;
    ObjectLocation location;
    ObjectFractionChanged fractionChanged;
    ObjectScreenHooks screen;
};
ObjectState* lookup(lua_State* state) {
    lua_getfield(state, LUA_REGISTRYINDEX, registryKey);
    auto* result = static_cast<ObjectState*>(lua_touserdata(state, -1)); lua_pop(state, 1); return result;
}
ObjectState& objects(lua_State* state) {
    return *static_cast<ObjectState*>(lua_touserdata(state, lua_upvalueindex(1)));
}
void table(lua_State* state) {
    if (!lua_istable(state, 1)) throw std::runtime_error("Object action requires an argument table");
}
bool has(lua_State* state, const char* name) {
    lua_getfield(state, 1, name); const bool result = !lua_isnil(state, -1); lua_pop(state, 1); return result;
}
std::string string(lua_State* state, const char* name, const char* fallback = "") {
    lua_getfield(state, 1, name);
    if (lua_isnil(state, -1)) { lua_pop(state, 1); return fallback; }
    if (lua_type(state, -1) != LUA_TSTRING) throw std::runtime_error(std::string(name) + " requires string");
    size_t length = 0; const char* data = lua_tolstring(state, -1, &length);
    if (length > 65536 || std::memchr(data, 0, length)) throw std::runtime_error(std::string(name) + " outside supported name bounds");
    std::string result(data, length); lua_pop(state, 1); return result;
}
float number(lua_State* state, const char* name, float fallback = 0) {
    lua_getfield(state, 1, name);
    if (lua_isnil(state, -1)) { lua_pop(state, 1); return fallback; }
    if (lua_type(state, -1) != LUA_TNUMBER) throw std::runtime_error(std::string(name) + " requires number");
    const auto value = lua_tonumber(state, -1); lua_pop(state, 1);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) throw std::runtime_error("Non-finite object argument");
    return static_cast<float>(value);
}
bool boolean(lua_State* state, const char* name, bool fallback = false) {
    lua_getfield(state, 1, name);
    if (lua_isnil(state, -1)) { lua_pop(state, 1); return fallback; }
    if (!lua_isboolean(state, -1)) throw std::runtime_error(std::string(name) + " requires boolean");
    const bool result = lua_toboolean(state, -1); lua_pop(state, 1); return result;
}
int32_t idAt(lua_State* state, int at) {
    if (lua_type(state, at) != LUA_TNUMBER) throw std::runtime_error("Object ID requires integer");
    const auto value = lua_tonumber(state, at);
    if (!std::isfinite(value) || value <= 0 || value > INT32_MAX || value != std::trunc(value)) throw std::runtime_error("Object ID outside positive signed32-bit range");
    return static_cast<int32_t>(value);
}
std::vector<int32_t> ids(lua_State* state, const char* singular, const char* plural) {
    std::vector<int32_t> result;
    lua_getfield(state,1,plural);
    if(!lua_isnil(state,-1)){
        if(!lua_istable(state,-1))throw std::runtime_error(std::string(plural)+" requires array");
        const auto count=lua_rawlen(state,-1);if(count>100000)throw std::runtime_error("Too many object IDs");
        for(size_t i=1;i<=count;++i){lua_rawgeti(state,-1,int(i));result.push_back(idAt(state,-1));lua_pop(state,1);}
        lua_pop(state,1);return result;
    }
    lua_pop(state,1);lua_getfield(state,1,singular);
    if(!lua_isnil(state,-1))result.push_back(idAt(state,-1));lua_pop(state,1);return result;
}
std::vector<std::string> names(lua_State* state,const char* singular,const char* plural){
    std::vector<std::string> result;lua_getfield(state,1,plural);
    // ScriptArgs name lists (see DestinationNames2819d0 in lua_spatial_queries)
    // accept a single string under the plural key as well as an array;
    // original scripts use both (e.g. DestinationNames = "SpawnPoints").
    if(lua_type(state,-1)==LUA_TSTRING){result.emplace_back(lua_tostring(state,-1));lua_pop(state,1);return result;}
    if(!lua_isnil(state,-1)){
        if(!lua_istable(state,-1)||lua_rawlen(state,-1)>10000)throw std::runtime_error("Object names require bounded array");
        for(size_t i=1,n=lua_rawlen(state,-1);i<=n;++i){lua_rawgeti(state,-1,int(i));if(lua_type(state,-1)!=LUA_TSTRING)throw std::runtime_error("Object names require strings");result.emplace_back(lua_tostring(state,-1));lua_pop(state,1);}
        lua_pop(state,1);return result;
    }
    lua_pop(state,1);const auto name=string(state,singular);if(!name.empty())result.push_back(name);return result;
}
std::vector<std::string> scriptNames(lua_State* state) {
    lua_getfield(state,1,"Names");
    if(lua_isnil(state,-1)){lua_pop(state,1);lua_getfield(state,1,"Name");}
    std::vector<std::string> result;
    if(lua_isnil(state,-1)){lua_pop(state,1);return result;}
    if(lua_type(state,-1)==LUA_TSTRING)result.emplace_back(lua_tostring(state,-1));
    else{
        if(!lua_istable(state,-1)||lua_rawlen(state,-1)>10000)throw std::runtime_error("Name/Names requires string or bounded array");
        for(size_t i=1,count=lua_rawlen(state,-1);i<=count;++i){lua_rawgeti(state,-1,static_cast<int>(i));if(lua_type(state,-1)!=LUA_TSTRING)throw std::runtime_error("Group names require strings");result.emplace_back(lua_tostring(state,-1));lua_pop(state,1);}
    }
    lua_pop(state,1);return result;
}
ObjectSnapshot& active(ObjectState& state, int32_t id) {
    const auto at = state.objects.find(id);
    if (at == state.objects.end() || (state.member?!state.member(id):(!at->second.active||!at->second.alive))) throw std::runtime_error("Object ID is not active: " + std::to_string(id));
    return at->second;
}
std::vector<int32_t> targets(lua_State* state, ObjectState& registry, bool destination = false) {
    auto result = ids(state, destination ? "DestinationId" : "Id", destination ? "DestinationIds" : "Ids");
    const auto groups = names(state, destination ? "DestinationName" : "Name", destination ? "DestinationNames" : "Names");
    for (const auto& name : groups) {
        bool known = false;
        for (const auto& group : snapshotSceneGroups(state)) if (group.name == name) known = true;
        if (!known) continue;
        for (const auto& [id, object] : registry.objects) if (object.active && object.alive &&
            (object.groups.empty()?object.group==name:std::find(object.groups.begin(),object.groups.end(),name)!=object.groups.end()))result.push_back(id);
    }
    std::sort(result.begin(), result.end()); result.erase(std::unique(result.begin(), result.end()), result.end());
    // World.GetActiveMapThings314740..3147a1 resolves IDs in the actual
    // manager map and skips absent owners. Retained disposal snapshots are
    // diagnostic state, not action targets (e.g. a finished dialogue arrow).
    std::erase_if(result,[&](int32_t id){const auto at=registry.objects.find(id);return at==registry.objects.end()||(registry.member?!registry.member(id):(!at->second.active||!at->second.alive));});
    return result;
}
void requireSupportedFields(lua_State* state, const std::set<std::string>& allowed) {
    static const std::set<std::string> common{"Delay","DelaySeconds","DelayMin","DelaySecondsMin","DelayMax","DelaySecondsMax"};
    lua_pushnil(state);
    while (lua_next(state, 1)) {
        if(lua_type(state,-2)!=LUA_TSTRING)throw std::runtime_error("Object action key requires string");
        const char* key=lua_tostring(state,-2);
        if(!allowed.contains(key)&&!common.contains(key))throw std::runtime_error(std::string("Unsupported object action argument: ")+key);
        lua_pop(state,1);
    }
}

void ensureGroup(lua_State* state, const std::string& name) {
    for (const auto& group : snapshotSceneGroups(state)) if (group.name == name) return;
    // Native SpawnObstacle calls GroupManager::Add(groupName), auto-creating it.
    lua_getglobal(state, "CreateGroup"); lua_newtable(state);
    lua_pushstring(state, name.c_str()); lua_setfield(state, -2, "Name");
    if (lua_pcall(state, 1, 0, 0) != LUA_OK) {
        const char* detail = lua_tostring(state, -1); const std::string error = detail ? detail : "Group creation failed";
        lua_pop(state, 1); throw std::runtime_error(error);
    }
}
void applyAnimation(ObjectState& registry, ObjectSnapshot& object, const std::string& name) {
    const auto at = registry.animations.find(name);
    if (at == registry.animations.end()) throw std::runtime_error("Original animation metadata has not been loaded: " + name);
    // Only single-frame source animations have complete playback semantics here.
    if (at->second.numFrames != 1) throw std::runtime_error("Native multi-frame object animation is not reconstructed: " + name);
    object.animation = name; object.animationPath = at->second.filePath;
    object.frame = at->second.firstFrame; object.numFrames = at->second.numFrames;
    object.animationScale=at->second.scale;object.animationOffsetX=at->second.offsetX;object.animationOffsetY=at->second.offsetY;object.graphicsDirty=true;
}
int spawn(lua_State* state) {
    table(state); auto& registry = objects(state);
    requireSupportedFields(state, {"Name","Group","OffsetX","OffsetY","OffsetZ","LocationX","LocationY",
        "DestinationId","DestinationIds","UseScreenLocation","SortById","TriggerOnSpawn","ForceToValidLocation",
        "SkipIfBlocked","DestroyIfBlocked","Attach"});
    const auto name = string(state, "Name"); const auto type = registry.definitions.find(name);
    if (type == registry.definitions.end()) throw std::runtime_error("Original obstacle definition not loaded: " + name);
    if (boolean(state,"ForceToValidLocation") || boolean(state,"SkipIfBlocked") || boolean(state,"DestroyIfBlocked") || boolean(state,"Attach"))
        throw std::runtime_error("Requested native obstacle collision/attachment behavior is not reconstructed");
    if (boolean(state,"TriggerOnSpawn",true) && type->second.triggerOnSpawn)
        throw std::runtime_error("Requested obstacle OnSpawn dispatch is not reconstructed");
    ObjectSnapshot object; object.name = name; object.group = string(state,"Group","Default");
    object.groups = {object.group};
    object.x = number(state,"LocationX"); object.y = number(state,"LocationY");
    const auto destinations = ids(state,"DestinationId","DestinationIds");
    if (!destinations.empty()) {
        object.x = 0; object.y = 0;
        for (int32_t id : destinations) { const auto& destination = active(registry,id); object.x += destination.x; object.y += destination.y; }
        object.x /= destinations.size(); object.y /= destinations.size();
    }
    object.x += number(state,"OffsetX"); object.y += number(state,"OffsetY"); object.z = number(state,"OffsetZ");
    object.screenSpace = boolean(state,"UseScreenLocation"); object.sortById = boolean(state,"SortById");
    object.scale = type->second.scale; object.visible = type->second.visible;
    applyAnimation(registry,object,type->second.graphic); ensureGroup(state,object.group);
    while (registry.objects.contains(registry.nextId)) {
        if (registry.nextId == INT32_MAX) throw std::runtime_error("Reconstructed object ID range exhausted");
        ++registry.nextId;
    }
    object.id = registry.nextId; if (registry.nextId < INT32_MAX) ++registry.nextId;
    registry.objects.emplace(object.id,object); registry.insertionOrder.push_back(object.id); lua_pushinteger(state,object.id); return 1;
}
// PDB-bounded native ScriptAction::GetIds RVA0x269db0 (2382 bytes) and
// World::GetActiveMapThingGroupByName RVA0x314a30. See native-object-getids.json.
// Native enumerates pointer-hash buckets. Our metadata insertion order gives
// stable membership but does not claim that allocation-dependent native order.
int getIds(lua_State* state) {
    table(state); auto& registry = objects(state);
    requireSupportedFields(state,{"Name","Names","Type"});
    const auto selectedGroups = names(state,"Name","Names");
    if (!selectedGroups.empty() && !registry.mapSeeded)
        throw std::runtime_error("GetIds requires original map/group registry before querying membership");
    const auto type = string(state,"Type");
    const auto sceneGroups = snapshotSceneGroups(state);
    std::vector<int32_t> result;
    for (const auto& selected : selectedGroups) {
        // GetIndex(name)<0 selects MapThingGroupId::None. RoomOpening has no
        // None members; retaining the empty membership name also models that
        // key without silently treating an unknown name as every object.
        const bool known = std::any_of(sceneGroups.begin(),sceneGroups.end(),
            [&](const SceneGroupDefinition& group){return group.name == selected;});
        const std::string resolved = known ? selected : "";
        for (int32_t id : registry.insertionOrder) {
            const auto& object = registry.objects.at(id);
            if (!object.active || !object.alive || (!type.empty() && object.name != type)) continue;
            const bool member = object.groups.empty() ? object.group == resolved :
                std::find(object.groups.begin(),object.groups.end(),resolved) != object.groups.end();
            if (member) result.push_back(id);
        }
    }
    lua_createtable(state,static_cast<int>(result.size()),0);
    for (size_t i = 0; i < result.size(); ++i) { lua_pushinteger(state,result[i]); lua_rawseti(state,-2,static_cast<int>(i + 1)); }
    lua_pushvalue(state,-1); lua_setglobal(state,"IdsTable");
    return 1;
}
int destroy(lua_State* state) {
    table(state); auto& registry = objects(state);
    requireSupportedFields(state,{"Id","Ids","Name","Names","NoSound"});
    const bool noSound=boolean(state,"NoSound");
    for (int32_t id : targets(state,registry)) {
        auto at = registry.objects.find(id);
        // ScriptAction.Execute251910 Destroy branch selects GetActiveMapThings before virtual Destroy;
        // portable tombstones must not be mistaken for live manager entries.
        if (at != registry.objects.end() && at->second.active && at->second.entityHandle) {
            const auto& handler=at->second.backend==ObjectBackend::Screen?registry.screen.destroy:registry.destroy;
            if(!handler||!at->second.entityHandle)throw std::runtime_error("Destroy requires actual Thing lifecycle provider");
            std::string error;if(!handler(id,noSound,error))throw std::runtime_error(error);
        }
    }
    return 0;
}
// ExecuteBool(IdExists234)2552ee..255381: Int(Id,0), nonnull global
// World map-Thing registry lookup; no LifeStatus/is-alive query.
int idExists(lua_State* state){
    table(state);auto& registry=objects(state);lua_getfield(state,1,"Id");
    const auto id=lua_tointeger(state,-1);lua_pop(state,1);
    const auto it=id>0&&id<=INT32_MAX?registry.objects.find(int32_t(id)):registry.objects.end();
    lua_pushboolean(state,it!=registry.objects.end()&&it->second.active&&it->second.entityHandle!=0&&!it->second.queryProxy);return 1;
}
int getGroupName(lua_State* state){
    table(state);auto& registry=objects(state);requireSupportedFields(state,{"Id","Ids","Name","Names","DrawGroup"});
    const bool draw=boolean(state,"DrawGroup");
    for(const auto id:targets(state,registry)){
        const auto i=registry.objects.find(id);if(i==registry.objects.end()||!i->second.active||!i->second.alive)continue;
        const auto& o=i->second;
        if(o.groups.empty())throw std::runtime_error("Native GetGroupName empty-membership fallback requires Thing virtual name contract");
        const auto& group=draw?o.groups.front():o.groups.back();lua_pushlstring(state,group.data(),group.size());return 1;
    }
    lua_pushliteral(state,"");return 1;
}
int attachLua(lua_State* state) {
    table(state);auto& registry=objects(state);
    requireSupportedFields(state,{"Id","Ids","Name","Names","Table","OnlyNullKeys"});
    const auto selected=targets(state,registry);const bool onlyNull=boolean(state,"OnlyNullKeys");
    lua_getfield(state,1,"Table");const int source=lua_gettop(state);
    if(!lua_istable(state,source)&&!lua_isnil(state,source))throw std::runtime_error("AttachLua Table requires Lua table or nil");
    // Thing::RunScriptAction159 -> mLuaTable +0x150. Replacement retains the
    // original table identity. OnlyNullKeys shallow-copies missing string/int
    // keys into an existing table; false/0 values are present, not null.
    lua_getfield(state,LUA_REGISTRYINDEX,luaTablesKey);const int tables=lua_gettop(state);
    for(const auto id:selected){
        active(registry,id);lua_rawgeti(state,tables,id);const int destination=lua_gettop(state);
        if(onlyNull&&lua_istable(state,destination)){
            if(lua_istable(state,source)){
                lua_pushnil(state);
                while(lua_next(state,source)){
                    const bool supported=lua_type(state,-2)==LUA_TSTRING||
                        (lua_type(state,-2)==LUA_TNUMBER&&lua_tonumber(state,-2)>=INT32_MIN&&lua_tonumber(state,-2)<=INT32_MAX&&std::trunc(lua_tonumber(state,-2))==lua_tonumber(state,-2));
                    if(supported){
                        lua_pushvalue(state,-2);lua_gettable(state,destination);const bool missing=lua_isnil(state,-1);lua_pop(state,1);
                        if(missing){lua_pushvalue(state,-2);lua_pushvalue(state,-2);lua_settable(state,destination);}
                    }
                    lua_pop(state,1);
                }
            }
        }else{lua_pushvalue(state,source);lua_rawseti(state,tables,id);}
        lua_pop(state,1);
    }
    return 0;
}
int addToGroup(lua_State* state) {
    table(state);auto& registry=objects(state);
    requireSupportedFields(state,{"Id","Ids","Name","Names","DrawGroup"});
    const auto selected=ids(state,"Id","Ids");
    // Native ScriptArgs::Names gives Names precedence over Name.
    const auto groups=scriptNames(state);
    const bool drawGroup=boolean(state,"DrawGroup");
    for(const auto& group:groups){
        if(group.empty())throw std::runtime_error("Empty native None-group mutation is not reconstructed");
        ensureGroup(state,group);
        for(const auto id:selected){
            auto& object=active(registry,id);
            if(object.groups.empty()&&!object.group.empty())object.groups.push_back(object.group);
            auto at=std::find(object.groups.begin(),object.groups.end(),group);
            if(at==object.groups.end()){object.groups.push_back(group);at=object.groups.end()-1;}
            // Thing::AddToGroup promotes the selected draw group to index0;
            // adding the first group also makes it the draw group.
            if(drawGroup||object.groups.size()==1){std::iter_swap(object.groups.begin(),at);object.group=group;object.graphicsDirty=true;}
        }
    }
    return 0;
}
int removeFromGroup(lua_State* state) {
    table(state);auto& registry=objects(state);
    requireSupportedFields(state,{"Id","Ids","Name","Names"});
    const auto selected=ids(state,"Id","Ids");
    const auto groups=scriptNames(state);
    // World::RemoveFromGroup(Thing*, group) drops the Thing from the world's
    // active group set; MapThing::RemoveFromGroup RVA0xce2a0 erases the id from
    // the membership vector (eastl::find + move-down). No promotion: DrawGroup
    // remains index0 of what is left, so removing it shifts the next one in.
    for(const auto& group:groups){
        if(group.empty())throw std::runtime_error("Empty native None-group mutation is not reconstructed");
        for(const auto id:selected){
            auto& object=active(registry,id);
            if(object.groups.empty()&&!object.group.empty())object.groups.push_back(object.group);
            const auto at=std::find(object.groups.begin(),object.groups.end(),group);
            if(at==object.groups.end())continue;
            const bool draw=at==object.groups.begin();object.groups.erase(at);
            if(draw){object.group=object.groups.empty()?std::string():object.groups.front();object.graphicsDirty=true;}
        }
    }
    return 0;
}
int flipHorizontal(lua_State* state) {
    table(state);auto& registry=objects(state);
    requireSupportedFields(state,{"Id","Ids","Name","Names"});
    // RunScriptAction117 -> BodyComponent::FlipHorizontally RVA0x2b7780:
    // XOR byte+0x69, invalidate owner's graphics, refresh body geometry.
    // Dirty state is retained; live render/physics consumers remain unfinished.
    for(const auto id:targets(state,registry)){auto& object=active(registry,id);object.flipHorizontal=!object.flipHorizontal;object.graphicsDirty=true;object.geometryDirty=true;
        if(registry.geometryChanged&&object.backend!=ObjectBackend::Screen){std::string detail;if(!registry.geometryChanged(id,detail))throw std::runtime_error(detail);}}
    return 0;
}
int getLocation(lua_State* state) {
    table(state); auto& registry = objects(state);
    requireSupportedFields(state,{"Id","Ids","Name","Names","CheckInactive"});
    const bool inactive = boolean(state,"CheckInactive");
    if (inactive && !registry.mapSeeded) throw std::runtime_error("Inactive object locations require original map records");
    const auto selected = inactive ? ids(state,"Id","Ids") : targets(state,registry);
    float x = 0, y = 0; size_t count = 0;
    for (int32_t id : selected) {
        const auto at = registry.objects.find(id);
        if (at == registry.objects.end()) {
            if (!registry.mapSeeded) throw std::runtime_error("Object location requires missing original map registry");
            continue;
        }
        const auto& object = at->second;
        if (inactive ? !object.fromMap : !object.active) continue;
        float ox=object.x,oy=object.y;
        if((object.backend==ObjectBackend::Screen?registry.screen.location:registry.location)&&object.entityHandle){std::string detail;if(!(object.backend==ObjectBackend::Screen?registry.screen.location:registry.location)(id,ox,oy,detail))throw std::runtime_error(detail);}
        x += ox; y += oy; ++count;
    }
    // Native GetLocation returns a genuine empty table when no targets resolve.
    lua_newtable(state); if (!count) return 1;
    x /= count; y /= count;
    lua_pushnumber(state,x); lua_setfield(state,-2,"X"); lua_pushnumber(state,y); lua_setfield(state,-2,"Y"); return 1;
}
int idsByType(lua_State* state) {
    table(state); auto& registry = objects(state);
    requireSupportedFields(state,{"Name","Names","RequireAlive"});
    const auto requested = names(state,"Name","Names");
    if (!registry.mapSeeded) throw std::runtime_error("GetIdsByType requires original map object registry to be seeded");
    lua_newtable(state); int entry = 1;
    const bool requireAlive = boolean(state,"RequireAlive");
    for (const auto& [id, object] : registry.objects) if (object.active && (!requireAlive || object.alive) && std::find(requested.begin(),requested.end(),object.name) != requested.end()) {
        lua_pushinteger(state,id); lua_rawseti(state,-2,entry++);
    }
    return 1;
}
int setAnimation(lua_State* state) {
    table(state); auto& registry = objects(state);
    // ScriptAction::SetAnimation 0x180261a10 (.local/reports/anim-setup/):
    // Thing::SetAnimation(Name, SuppressSounds, SuppressSoundsIfInvisible), then
    // Duration/MinDuration/MaxDuration, OffsetX/OffsetY, Scale, PlaySpeed,
    // StartFrameFraction, FrameTarget/Instant, Color (Thing SetColor action) and
    // SoundId on the new primary. DestinationName(s) is read but unused.
    // The complete native action queries these keys individually; it does
    // not reject unrelated keys. In particular Group is only consumed by
    // SetupAnimation262c50/CreateAnimation, never SetAnimation261a10.
    const auto animation = string(state,"Name");
    bool extended=false;for(const char* key:{"FrameTarget","Instant","Color","PlaySpeed","StartFrameFraction","Scale","Duration","MinDuration","MaxDuration","OffsetX","OffsetY","SoundId","SuppressSounds","SuppressSoundsIfInvisible"})if(has(state,key))extended=true;
    for (int32_t id : targets(state,registry,true)){
        auto& object=active(registry,id);
        if(extended&&object.backend==ObjectBackend::Screen){
            if(!registry.screen.animationArgs||!object.entityHandle)throw std::runtime_error("SetAnimation optional arguments require the screen animation consumer");
            std::string error;if(!registry.screen.animationArgs(id,animation,state,error))throw std::runtime_error(error);continue;
        }
        const auto& handler=object.backend==ObjectBackend::Screen?registry.screen.animation:registry.animationSet;
        if(handler&&object.entityHandle){std::string error;if(!handler(id,animation,error))throw std::runtime_error(error);
            if(extended){if(!registry.worldAnimationArgs)throw std::runtime_error("SetAnimation optional arguments require the world animation consumer");if(!registry.worldAnimationArgs(id,animation,state,error))throw std::runtime_error(error);}}
        else{if(extended)throw std::runtime_error("SetAnimation optional arguments on metadata-only Thing: "+animation);applyAnimation(registry,object,animation);}
    }
    return 0;
}
template<bool Scale> int setFraction(lua_State* state) {
    table(state); auto& registry = objects(state);
    requireSupportedFields(state,{"Id","Ids","Name","Names","Fraction","Duration","SmoothStep","EaseIn","EaseOut"});
    hades::BodyTransitionOptions options;options.duration=number(state,"Duration");options.smoothStep=boolean(state,"SmoothStep");options.easeIn=number(state,"EaseIn");options.easeOut=number(state,"EaseOut");
    if (!has(state,"Fraction")) throw std::runtime_error("Object transform requires Fraction");
    const float fraction = number(state,"Fraction");
    if (fraction < 0 || (!Scale && fraction > 1)) throw std::runtime_error("Object transform Fraction outside supported range");
    for (int32_t id : targets(state,registry)) { auto& object = active(registry,id);
        const auto& handler=object.backend==ObjectBackend::Screen?registry.screen.fraction:registry.fractionChanged;
        if(handler&&object.entityHandle){std::string detail;if(!handler(id,Scale,fraction,options,detail))throw std::runtime_error(detail);continue;}
        if(options.duration!=0||options.easeIn!=0||options.easeOut!=0)throw std::runtime_error("Timed object transform requires actual Body component");
        if constexpr (Scale){object.scaleFraction=fraction;object.geometryDirty=true;}
        else object.alpha=fraction;object.graphicsDirty=true;
        if constexpr(Scale)if(registry.geometryChanged&&object.backend!=ObjectBackend::Screen){std::string detail;if(!registry.geometryChanged(id,detail))throw std::runtime_error(detail);} }
    return 0;
}
template<int (*Function)(lua_State*)> int guarded(lua_State* state) {
    char message[1024] = {};
    try { return Function(state); }
    catch(const std::exception& error) { std::snprintf(message,sizeof(message),"%s",error.what()); }
    return luaL_error(state,"%s",message);
}
int finalize(lua_State* state) { static_cast<ObjectState*>(lua_touserdata(state,1))->~ObjectState(); return 0; }
void bind(lua_State* state,int owner,const char* name,lua_CFunction function) {
    lua_pushvalue(state,owner); lua_pushcclosure(state,function,1); lua_setglobal(state,name);
}
} // namespace

void registerObjectRuntime(lua_State* state,int32_t firstRuntimeId) {
    const int top = lua_gettop(state);
    auto* registry = new(lua_newuserdata(state,sizeof(ObjectState))) ObjectState();
    registry->nextId = firstRuntimeId > 0 ? firstRuntimeId : 2000001;
    const int owner = lua_gettop(state);
    if (luaL_newmetatable(state,metatableName)) { lua_pushcfunction(state,finalize); lua_setfield(state,-2,"__gc"); }
    lua_setmetatable(state,owner); lua_pushvalue(state,owner); lua_setfield(state,LUA_REGISTRYINDEX,registryKey);
    lua_newtable(state);lua_setfield(state,LUA_REGISTRYINDEX,luaTablesKey);
    // Transcribed original definitions, not generated artwork:
    // 1_DevObstacles.sjson520 ->InvisibleTarget252; NeedsLife=false,
    // StopsUnits/Projectiles=false, Unlit, Graphic=Blank, Invisible=false.
    registry->definitions.emplace("BlankObstacle",ObjectDefinition{"BlankObstacle","Blank",1,true,false});
    // 1_DevAnimations.sjson5 and GUIAnimations.sjson4323, both single frame.
    registry->animations.emplace("Blank",ObjectAnimationDefinition{"Blank","Dev\\blank_invisible",1,1,0,false});
    registry->animations.emplace("VignetteOverlay",ObjectAnimationDefinition{"VignetteOverlay","Fx\\Vignette\\VignetteOverlay",1,1,1,true});
    // Exact inherited ConstantNoUpdate definitions: EnemyAnimations.sjson341-364.
    // Single-frame state only; original urn textures are not yet bound to actors.
    registry->animations.emplace("BreakableIdle1",ObjectAnimationDefinition{"BreakableIdle1","Tilesets\\Temple\\Temple_Urn_01",1,1,0,false,.2f,0,-50});
    registry->animations.emplace("BreakableIdle2",ObjectAnimationDefinition{"BreakableIdle2","Tilesets\\Temple\\Temple_Urn_02",1,1,0,false,.13f,0,-50});
    registry->animations.emplace("BreakableIdle3",ObjectAnimationDefinition{"BreakableIdle3","Tilesets\\Temple\\Temple_Urn_03",1,1,0,false,.15f,0,-33});
    bind(state,owner,"SpawnObstacle",guarded<spawn>); bind(state,owner,"Destroy",guarded<destroy>);
    bind(state,owner,"GetIds",guarded<getIds>);bind(state,owner,"IdExists",guarded<idExists>);
    bind(state,owner,"GetGroupName",guarded<getGroupName>);
    bind(state,owner,"AttachLua",guarded<attachLua>);
    bind(state,owner,"AddToGroup",guarded<addToGroup>);bind(state,owner,"RemoveFromGroup",guarded<removeFromGroup>);bind(state,owner,"FlipHorizontal",guarded<flipHorizontal>);
    bind(state,owner,"GetLocation",guarded<getLocation>); bind(state,owner,"GetIdsByType",guarded<idsByType>);
    bind(state,owner,"SetAnimation",guarded<setAnimation>);
    bind(state,owner,"SetScale",guarded<setFraction<true>>); bind(state,owner,"SetAlpha",guarded<setFraction<false>>);
    lua_settop(state,top);
}
bool resetObjectRuntimeForMap(lua_State* state,std::string& error) {
 auto* registry=lookup(state);if(!registry){error="Map reset requires registered ObjectRuntime";return false;}
 registry->objects.clear();registry->insertionOrder.clear();registry->mapSeeded=false;registry->nextId=2000001;registry->nextUnitId=1000001;
 lua_newtable(state);lua_setfield(state,LUA_REGISTRYINDEX,luaTablesKey);error.clear();return true;
}
bool registerObjectDefinitions(lua_State* state,const std::vector<ObjectDefinition>& definitions,std::string& error) {
    auto* registry = lookup(state); if (!registry) { error="Object runtime not registered"; return false; }
    for(const auto& definition:definitions) {
        if(definition.name.empty() || definition.graphic.empty() || !std::isfinite(definition.scale) || definition.scale<0) { error="Invalid original object definition"; return false; }
    }
    for(const auto& definition:definitions) registry->definitions[definition.name]=definition;
    error.clear(); return true;
}
bool registerObjectAnimations(lua_State* state,const std::vector<ObjectAnimationDefinition>& definitions,std::string& error) {
    auto* registry = lookup(state); if (!registry) { error="Object runtime not registered"; return false; }
    for(const auto& definition:definitions) if(definition.name.empty() || definition.filePath.empty() || !definition.numFrames ||
        !std::isfinite(definition.duration)||definition.duration<0||!std::isfinite(definition.scale)||definition.scale<0||
        !std::isfinite(definition.offsetX)||!std::isfinite(definition.offsetY)) { error="Invalid original animation metadata"; return false; }
    for(const auto& definition:definitions) registry->animations[definition.name]=definition;
    error.clear(); return true;
}
bool seedObjectRuntime(lua_State* state,const std::vector<ObjectSnapshot>& objects,std::string& error) {
    auto* registry = lookup(state); if (!registry) { error="Object runtime not registered"; return false; }
    std::set<int32_t> seen;
    for(const auto& object:objects) if(object.id<=0 || object.name.empty() || !std::isfinite(object.x) || !std::isfinite(object.y) || !seen.insert(object.id).second || registry->objects.contains(object.id)) { error="Invalid or duplicate original map object"; return false; }
    for(auto object:objects) { object.fromMap=true; registry->insertionOrder.push_back(object.id); registry->objects.emplace(object.id,std::move(object)); }
    registry->mapSeeded=true; error.clear(); return true;
}
std::vector<ObjectSnapshot> snapshotObjects(lua_State* state) {
    std::vector<ObjectSnapshot> result;
    if(const auto* registry=lookup(state)) for(const auto& [id,object]:registry->objects) result.push_back(object);
    return result;
}
void markObjectMapMetadataLoaded(lua_State* state) {
    if(auto* registry=lookup(state))registry->mapSeeded=true;
}
void pushObjectLuaTable(lua_State* state,int32_t id) {
    lua_getfield(state,LUA_REGISTRYINDEX,luaTablesKey);
    if(!lua_istable(state,-1)){lua_pop(state,1);lua_pushnil(state);return;}
    lua_rawgeti(state,-1,id);lua_remove(state,-2);
}
std::string dumpObjectRuntime(lua_State* state) {
    std::ostringstream out; const auto* registry=lookup(state); if(!registry) return "OBJECTS unavailable\n";
    out<<"OBJECTS count="<<registry->objects.size()<<" mapMetadataLoaded="<<registry->mapSeeded<<" nextId="<<registry->nextId<<'\n';
    for(const auto& [id,o]:registry->objects){
        pushObjectLuaTable(state,id);const bool luaAttached=lua_istable(state,-1);lua_pop(state,1);
        out<<id<<' '<<o.name<<" group="<<o.group<<" xyz="<<o.x<<','<<o.y<<','<<o.z<<" scale="<<o.scale<<" scaleFraction="<<o.scaleFraction<<" alpha="<<o.alpha<<" screen="<<o.screenSpace<<" active="<<o.active<<" alive="<<o.alive<<" queryProxy="<<o.queryProxy<<" luaAttached="<<luaAttached<<" flipX="<<o.flipHorizontal<<" graphicsDirty="<<o.graphicsDirty<<" geometryDirty="<<o.geometryDirty<<" animation="<<o.animation<<" animationScale="<<o.animationScale<<" animationOffset="<<o.animationOffsetX<<','<<o.animationOffsetY<<" path="<<o.animationPath<<'\n';
    }
    return out.str();
}

ObjectSnapshot* findObjectSnapshot(lua_State* L,int32_t id) {
    auto* r=lookup(L);if(!r)return nullptr;const auto i=r->objects.find(id);return i==r->objects.end()?nullptr:&i->second;
}
bool insertActorSnapshot(lua_State* L,ObjectSnapshot object,std::string& error) {
    auto* r=lookup(L);
    if(!r||object.id<=0||object.entityHandle==0||object.queryProxy||r->objects.contains(object.id)){error="Invalid or duplicate native actor object registration";return false;}
    r->insertionOrder.push_back(object.id);r->objects.emplace(object.id,std::move(object));error.clear();return true;
}

// World::GetActiveMapThingGroupByName RVA0x314a30: GroupManager index for the
// name (unknown -> MapThingGroupId::None, which has no RoomOpening members),
// then that group's active-thing set. Native hash-set order is allocation
// dependent; registry ID order is the stable portable order.
bool activeMapThingGroup(lua_State* L,const std::string& name,std::vector<int32_t>& result,std::string& error){
    result.clear();auto* r=lookup(L);if(!r){error="Object manager unavailable";return false;}
    bool known=false;for(const auto& group:snapshotSceneGroups(L))if(group.name==name){known=true;break;}
    if(known)for(const auto& [id,object]:r->objects)if(object.active&&object.alive&&
        (object.groups.empty()?object.group==name:std::find(object.groups.begin(),object.groups.end(),name)!=object.groups.end()))result.push_back(id);
    error.clear();return true;
}
bool resolveObjectTargets(lua_State* L,bool destination,std::vector<int32_t>& result,std::string& error){
    const int top=lua_gettop(L);try{table(L);auto* r=lookup(L);if(!r)throw std::runtime_error("Object manager unavailable");result=targets(L,*r,destination);lua_settop(L,top);error.clear();return true;}catch(const std::exception& e){lua_settop(L,top);error=e.what();return false;}
}

// World.Reset315fac sets next Unit ID1000000; Unit.Init2f1006 preincrements.
bool allocateUnitObjectId(lua_State* L,int32_t& output,std::string& error){
    auto* r=lookup(L);if(!r){error="Object allocator unavailable";return false;}
    while(r->nextUnitId<2000000&&r->objects.contains(r->nextUnitId))++r->nextUnitId;
    if(r->nextUnitId<1000001||r->nextUnitId>=2000000){error="Native unit ID domain exhausted";return false;}
    output=r->nextUnitId++;error.clear();return true;
}
bool allocateObstacleObjectId(lua_State* L,int32_t& output,std::string& error){
    auto* r=lookup(L);if(!r){error="Object allocator unavailable";return false;}
    while(r->nextId<3000000&&r->objects.contains(r->nextId))++r->nextId;
    if(r->nextId<2000001||r->nextId>=3000000){error="Native obstacle ID domain exhausted";return false;}
    output=r->nextId++;error.clear();return true;
}
void setObjectLuaTable(lua_State* L,int32_t id,int valueIndex){
    valueIndex=lua_absindex(L,valueIndex);lua_getfield(L,LUA_REGISTRYINDEX,luaTablesKey);
    lua_pushvalue(L,valueIndex);lua_rawseti(L,-2,id);lua_pop(L,1);
}

void setObjectGeometryChanged(lua_State* L,ObjectGeometryChanged handler){if(auto* r=lookup(L))r->geometryChanged=std::move(handler);}
void setObjectLocation(lua_State* L,ObjectLocation handler){if(auto* r=lookup(L))r->location=std::move(handler);}
void setObjectFractionChanged(lua_State* L,ObjectFractionChanged handler){if(auto* r=lookup(L))r->fractionChanged=std::move(handler);}

void setObjectAnimationSet(lua_State* L,ObjectAnimationSet handler){if(auto* r=lookup(L))r->animationSet=std::move(handler);}
void setObjectWorldAnimationArgs(lua_State* L,ObjectAnimationArgs handler){if(auto* r=lookup(L))r->worldAnimationArgs=std::move(handler);}

bool setObjectAnimationMetadata(lua_State* L,int32_t id,const std::string& name,std::string& error){
    try{auto* r=lookup(L);if(!r)throw std::runtime_error("Object animation registry unavailable");applyAnimation(*r,active(*r,id),name);error.clear();return true;}
    catch(const std::exception& e){error=e.what();return false;}
}

void setObjectDestroy(lua_State* L,ObjectDestroy handler){if(auto* r=lookup(L))r->destroy=std::move(handler);}

void setObjectScreenHooks(lua_State*L,ObjectScreenHooks hooks){if(auto*r=lookup(L))r->screen=std::move(hooks);}

void setObjectMembership(lua_State* state,ObjectMembership member){auto* registry=lookup(state);if(!registry)throw std::runtime_error("Object membership requires registered runtime");registry->member=std::move(member);}
