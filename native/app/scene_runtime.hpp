#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct lua_State;

struct SceneGroupKey {
    int32_t id = 0;
    std::string name;
    bool operator==(const SceneGroupKey&) const = default;
};
struct SceneGroupDefinition {
    int32_t id = 0;
    std::string name;
    int32_t parentId = 0;
    std::string parentName;
    std::vector<SceneGroupKey> children;
    int32_t blendMode = 0;
    bool visible = true;
    bool overlay = false;
};

void registerSceneRuntime(lua_State* state);
bool initializeSceneGroups(lua_State* state, const std::vector<SceneGroupDefinition>& definitions,
                           std::string& error);
// SGP1,u32count; each:i32id,i32parentId,i32blend,u32visible,stringname,
// stringparent,u32children,{i32id,stringname}[]; strings:u32length+UTF8bytes.
// All integer fields little-endian. Limits:10000groups/children,64KiB/string.
bool loadSceneGroupsSGP1(lua_State* state, const std::vector<uint8_t>& bytes, std::string& error);
// Back-to-front flattened native group order, including descendants before parent.
std::vector<SceneGroupDefinition> snapshotSceneGroups(lua_State* state);
std::string dumpSceneGroups(lua_State* state);
