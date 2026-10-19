#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include "body_transition.hpp"
struct lua_State;

struct ObjectSnapshot {
    int32_t id = 0;
    std::string name, animation, animationPath, group;
    std::vector<std::string> groups; // All original memberships when staged; group remains primary.
    float x = 0, y = 0, z = 0;
    float scale = 1, scaleFraction = 1, alpha = 1, angle = 0;
    bool screenSpace = false, sortById = false, alive = true, visible = true;
    bool active = true, doCreate = true;
    bool fromMap = false;
    bool queryProxy = false; // Host-only lifecycle probe, never a created native actor.
    uint64_t entityHandle = 0; // Reconstruction lifetime handle, separate from public/map IDs.
    int32_t dataType = 0;
    bool playerControlled = false;
    bool flipHorizontal = false, graphicsDirty = false, geometryDirty = false;
    float animationScale = 1, animationOffsetX = 0, animationOffsetY = 0;
    uint32_t frame = 1, numFrames = 1;
};
struct ObjectDefinition {
    std::string name, graphic;
    float scale = 1;
    bool visible = true, triggerOnSpawn = false;
};
struct ObjectAnimationDefinition {
    std::string name, filePath;
    uint32_t firstFrame = 1, numFrames = 1;
    float duration = 0;
    bool loop = false;
    float scale = 1, offsetX = 0, offsetY = 0;
};
// Registers only implemented object APIs. The explicitly reconstructed allocator
// uses firstRuntimeId, skips seeded map IDs, never reuses destroyed IDs.
void registerObjectRuntime(lua_State*, int32_t firstRuntimeId = 2000001);
bool registerObjectDefinitions(lua_State*, const std::vector<ObjectDefinition>&, std::string& error);
bool registerObjectAnimations(lua_State*, const std::vector<ObjectAnimationDefinition>&, std::string& error);
bool seedObjectRuntime(lua_State*, const std::vector<ObjectSnapshot>&, std::string& error);
void markObjectMapMetadataLoaded(lua_State*);
std::vector<ObjectSnapshot> snapshotObjects(lua_State*);
std::string dumpObjectRuntime(lua_State*);
// Pushes the exact table attached by AttachLua, or nil. No table is fabricated
// for a Thing that has not received Lua state (Script::run uses the same slot).
void pushObjectLuaTable(lua_State*,int32_t id);

// Stable map-backed object storage for actual native component registration.
ObjectSnapshot* findObjectSnapshot(lua_State*,int32_t id);
bool insertActorSnapshot(lua_State*,ObjectSnapshot,std::string& error);

bool resolveObjectTargets(lua_State*,bool destination,std::vector<int32_t>&,std::string& error);
bool activeMapThingGroup(lua_State*,const std::string& name,std::vector<int32_t>&,std::string& error);

bool allocateObstacleObjectId(lua_State*,int32_t&,std::string& error);
void setObjectLuaTable(lua_State*,int32_t id,int valueIndex);

using ObjectGeometryChanged=std::function<bool(int32_t,std::string&)>;
void setObjectGeometryChanged(lua_State*,ObjectGeometryChanged);
using ObjectLocation=std::function<bool(int32_t,float&,float&,std::string&)>;
void setObjectLocation(lua_State*,ObjectLocation);
using ObjectFractionChanged=std::function<bool(int32_t,bool,float,const hades::BodyTransitionOptions&,std::string&)>;
void setObjectFractionChanged(lua_State*,ObjectFractionChanged);

using ObjectAnimationSet=std::function<bool(int32_t,const std::string&,std::string&)>;
void setObjectAnimationSet(lua_State*,ObjectAnimationSet);
bool setObjectAnimationMetadata(lua_State*,int32_t,const std::string&,std::string&);
// World SetAnimation optional arguments, applied after the primary is set;
// reads the action table at index 1.
using ObjectAnimationArgs=std::function<bool(int32_t,const std::string&,lua_State*,std::string&)>;
void setObjectWorldAnimationArgs(lua_State*,ObjectAnimationArgs);

using ObjectDestroy=std::function<bool(int32_t,bool,std::string&)>;
void setObjectDestroy(lua_State*,ObjectDestroy);

// Separate native GameUI body consumers: screen Things never enter world physics.
struct ObjectScreenHooks {
 ObjectAnimationSet animation;
 ObjectFractionChanged fraction;
 ObjectLocation location;
 ObjectDestroy destroy;
 // SetAnimation with native optional arguments; reads the action table at index 1.
 std::function<bool(int32_t,const std::string&,lua_State*,std::string&)> animationArgs;
};
void setObjectScreenHooks(lua_State*,ObjectScreenHooks);
