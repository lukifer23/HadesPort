#include "scene_runtime.hpp"
#include <algorithm>
#include <bit>
#include <climits>
#include <cstdio>
#include <cstring>
#include <functional>
#include <new>
#include <sstream>
#include <stdexcept>
#include <utility>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
// Exact EngineWin64s.dll/PDB identity and observations: native/analysis/engine-identity.json.
// GroupManager::Init0x30c030, AddGroup0x30c140, Insert0x30c440,
// Reset0x30c250, Add(name)0x30c9d0. Disassembly native-*-groups.json.
constexpr const char* registryKey = "hades.sceneGroups";
constexpr const char* metatableName = "hades.sceneGroups.owner";
struct SceneState {
    std::vector<SceneGroupDefinition> groups;
    int32_t highId = INT32_MIN;
    bool initialized = false;
};
SceneGroupKey key(const SceneGroupDefinition& group) { return {group.id, group.name}; }
SceneGroupKey parent(const SceneGroupDefinition& group) { return {group.parentId, group.parentName}; }
int index(const SceneState& scene, const SceneGroupKey& target) {
    for (size_t i = 0; i < scene.groups.size(); ++i) if (key(scene.groups[i]) == target) return static_cast<int>(i);
    return -1;
}
int index(const SceneState& scene, const std::string& name) {
    for (size_t i = 0; i < scene.groups.size(); ++i) if (scene.groups[i].name == name) return static_cast<int>(i);
    return -1;
}
SceneState* lookup(lua_State* state) {
    lua_getfield(state, LUA_REGISTRYINDEX, registryKey);
    auto* result = static_cast<SceneState*>(lua_touserdata(state, -1));
    lua_pop(state, 1);
    return result;
}
SceneState& scene(lua_State* state) {
    auto* result = static_cast<SceneState*>(lua_touserdata(state, lua_upvalueindex(1)));
    if (!result || !result->initialized) throw std::runtime_error("Scene groups have not been initialized from map metadata");
    return *result;
}
void requireTable(lua_State* state) {
    if (!lua_istable(state, 1)) throw std::runtime_error("Scene group action requires an argument table");
}
std::string field(lua_State* state, const char* name, bool required) {
    lua_getfield(state, 1, name);
    if (lua_isnil(state, -1) && !required) { lua_pop(state, 1); return {}; }
    if (lua_type(state, -1) != LUA_TSTRING) { lua_pop(state, 1); throw std::runtime_error(std::string(name) + " requires a string"); }
    size_t length = 0; const char* value = lua_tolstring(state, -1, &length);
    if (length == 0 || length > 65536 || std::memchr(value, 0, length)) {
        lua_pop(state, 1); throw std::runtime_error(std::string(name) + " requires a nonempty bounded name");
    }
    std::string result(value, length); lua_pop(state, 1); return result;
}
std::vector<std::string> names(lua_State* state) {
    std::vector<std::string> result;
    lua_getfield(state, 1, "Names");
    if (lua_isnil(state, -1)) {
        lua_pop(state, 1); const auto name = field(state, "Name", false);
        if (!name.empty()) result.push_back(name);
        return result;
    }
    if (!lua_istable(state, -1)) { lua_pop(state, 1); throw std::runtime_error("Names requires an array table"); }
    const size_t count = lua_rawlen(state, -1);
    if (count > 10000) throw std::runtime_error("Names exceeds group limit");
    for (size_t i = 1; i <= count; ++i) {
        lua_rawgeti(state, -1, static_cast<int>(i));
        if (lua_type(state, -1) != LUA_TSTRING) throw std::runtime_error("Names entries require strings");
        size_t length = 0; const char* value = lua_tolstring(state, -1, &length);
        if (!length || length > 65536 || std::memchr(value, 0, length)) throw std::runtime_error("Names entry outside supported bounds");
        result.emplace_back(value, length); lua_pop(state, 1);
    }
    lua_pop(state, 1); return result;
}
int32_t blendMode(const std::string& name) {
    // Exact PDB DrawManager::BlendMode enum (TPI57396 ->57395). Native
    // CreateGroup's failed enum parse defaults Alpha0; SetupMap uses Normal.
    const char* modes[] = {"Alpha", "Opaque", "Additive", "Multiplicative2x", "Screen", "AdditiveMerge",
        "Multiplicative2xMerge", "ScreenMerge", "ColorBurn", "Overlay", "DarkerColor", "LighterColor",
        "Displacement", "DisplacementMerge"};
    if (name.empty() || name == "Normal") return 0;
    for (int i = 0; i < 14; ++i) if (name == modes[i]) return i;
    if (name == "Undefined") return 255;
    throw std::runtime_error("Unsupported scene group BlendMode: " + name);
}
void rebuildChildren(SceneState& scene) {
    for (auto& group : scene.groups) group.children.clear();
    // ChildGroups stores front-to-back, flattened array stores back-to-front.
    for (auto it = scene.groups.rbegin(); it != scene.groups.rend(); ++it) {
        const auto p = parent(*it); if (p.name.empty() && p.id == 0) continue;
        const int at = index(scene, p);
        if (at < 0) throw std::runtime_error("Scene group parent missing: " + p.name);
        scene.groups[at].children.push_back(key(*it));
    }
}
bool descendant(const SceneState& scene, size_t child, const SceneGroupKey& ancestor) {
    auto p = parent(scene.groups[child]);
    for (size_t depth = 0; !p.name.empty() || p.id != 0; ++depth) {
        if (p == ancestor) return true;
        const int at = index(scene, p);
        if (at < 0 || depth >= scene.groups.size()) throw std::runtime_error("Invalid scene group parent chain");
        p = parent(scene.groups[at]);
    }
    return false;
}
void move(SceneState& scene, const std::string& name, const std::string& destination, bool behind) {
    // Native Insert detaches source from old parent, moves contiguous subtree,
    // inserts at destination index+(behind?0:1), inherits destination's parent.
    const int from = index(scene, name), to = index(scene, destination);
    if (from < 0 || to < 0) throw std::runtime_error("Scene group relocation requires existing source and destination: " + name + " -> " + destination);
    if (from == to || descendant(scene, static_cast<size_t>(to), key(scene.groups[from])))
        throw std::runtime_error("Unsupported scene group relocation into its own subtree");
    const auto sourceKey = key(scene.groups[from]);
    const auto destinationKey = key(scene.groups[to]);
    const auto newParent = parent(scene.groups[to]);
    size_t begin = static_cast<size_t>(from);
    while (begin && descendant(scene, begin - 1, sourceKey)) --begin;
    std::vector<SceneGroupDefinition> block(scene.groups.begin() + static_cast<ptrdiff_t>(begin), scene.groups.begin() + from + 1);
    block.back().parentId = newParent.id; block.back().parentName = newParent.name;
    scene.groups.erase(scene.groups.begin() + static_cast<ptrdiff_t>(begin), scene.groups.begin() + from + 1);
    const int target = index(scene, destinationKey);
    const size_t insertion = static_cast<size_t>(target + (behind ? 0 : 1));
    scene.groups.insert(scene.groups.begin() + static_cast<ptrdiff_t>(insertion), block.begin(), block.end());
    rebuildChildren(scene);
}
int createImpl(lua_State* state) {
    requireTable(state); auto& s = scene(state);
    const auto requested = names(state); const int32_t mode = blendMode(field(state, "BlendMode", false));
    for (const auto& name : requested) {
        int at = index(s, name);
        if (at < 0) {
            SceneGroupDefinition group; group.name = name; group.id = ++s.highId;
            s.groups.push_back(std::move(group)); at = static_cast<int>(s.groups.size() - 1);
        }
        // Original CreateGroup overwrites blend mode even when group exists.
        s.groups[at].blendMode = mode;
    }
    return 0;
}
int moveImpl(lua_State* state, bool behind) {
    requireTable(state); auto& s = scene(state);
    const auto destination = field(state, "DestinationName", true);
    for (const auto& name : names(state)) move(s, name, destination, behind);
    return 0;
}
int hasImpl(lua_State* state) {
    requireTable(state); auto& s = scene(state); const auto requested = names(state);
    // ExecuteBool HasGroup action85 examines first resolved Name only.
    lua_pushboolean(state, !requested.empty() && index(s, requested.front()) >= 0); return 1;
}
int visibilityImpl(lua_State* state) {
    requireTable(state); auto& s = scene(state);
    lua_getfield(state, 1, "Visible");
    const bool visible = lua_isnil(state, -1) ? true : lua_toboolean(state, -1);
    if (!lua_isnil(state, -1) && !lua_isboolean(state, -1)) throw std::runtime_error("Visible requires boolean");
    lua_pop(state, 1); bool changed = false;
    for (const auto& name : names(state)) {
        const int at = index(s, name);
        if (at < 0) throw std::runtime_error("Unknown scene group visibility target: " + name);
        if (s.groups[at].visible != visible) { s.groups[at].visible = visible; changed = true; }
    }
    lua_pushboolean(state, changed); return 1;
}
template<int (*Function)(lua_State*)> int guarded(lua_State* state) {
    char message[1024] = {};
    try { return Function(state); }
    catch (const std::exception& error) { std::snprintf(message, sizeof(message), "%s", error.what()); }
    // Lua longjmp only after C++ temporary objects/exception have been destroyed.
    return luaL_error(state, "%s", message);
}
int behindImpl(lua_State* state) { return moveImpl(state, true); }
int frontImpl(lua_State* state) { return moveImpl(state, false); }
int finalize(lua_State* state) { static_cast<SceneState*>(lua_touserdata(state, 1))->~SceneState(); return 0; }
void bind(lua_State* state, int owner, const char* name, lua_CFunction function) {
    lua_pushvalue(state, owner); lua_pushcclosure(state, function, 1); lua_setglobal(state, name);
}
} // namespace

void registerSceneRuntime(lua_State* state) {
    const int oldTop = lua_gettop(state);
    auto* memory = lua_newuserdata(state, sizeof(SceneState)); new (memory) SceneState();
    const int owner = lua_gettop(state);
    if (luaL_newmetatable(state, metatableName)) { lua_pushcfunction(state, finalize); lua_setfield(state, -2, "__gc"); }
    lua_setmetatable(state, owner);
    lua_pushvalue(state, owner); lua_setfield(state, LUA_REGISTRYINDEX, registryKey);
    bind(state, owner, "CreateGroup", guarded<createImpl>);
    bind(state, owner, "InsertGroupBehind", guarded<behindImpl>);
    bind(state, owner, "InsertGroupInFront", guarded<frontImpl>);
    bind(state, owner, "HasGroup", guarded<hasImpl>);
    bind(state, owner, "SetGroupVisibility", guarded<visibilityImpl>);
    lua_settop(state, oldTop);
}

bool initializeSceneGroups(lua_State* state, const std::vector<SceneGroupDefinition>& definitions, std::string& error) {
    try {
        auto* target = lookup(state); if (!target) throw std::runtime_error("Scene runtime is not registered");
        if (definitions.size() > 10000) throw std::runtime_error("Scene group limit exceeded");
        SceneState replacement;
        std::vector<uint8_t> visited(definitions.size());
        const auto find = [&](const SceneGroupKey& targetKey) {
            for (size_t i = 0; i < definitions.size(); ++i) if (key(definitions[i]) == targetKey) return i;
            throw std::runtime_error("Missing map child group: " + targetKey.name);
        };
        for (size_t i = 0; i < definitions.size(); ++i) {
            const auto& g = definitions[i];
            if (g.name.empty() || g.name.size() > 65536 || g.name.find('\0') != std::string::npos)
                throw std::runtime_error("Invalid map group name");
            if ((g.blendMode < 0 || g.blendMode > 13) && g.blendMode != 255) throw std::runtime_error("Unknown map group blend enum");
            for (size_t j = 0; j < i; ++j) if (g.name == definitions[j].name) throw std::runtime_error("Duplicate map group name: " + g.name);
        }
        std::function<void(size_t)> append = [&](size_t at) {
            if (visited[at]) throw std::runtime_error("Cyclic or repeated map child group");
            visited[at] = 1;
            const auto& group = definitions[at];
            for (auto child = group.children.rbegin(); child != group.children.rend(); ++child) {
                const size_t childAt = find(*child);
                if (parent(definitions[childAt]) != key(group)) throw std::runtime_error("Map child-parent identity mismatch");
                append(childAt);
            }
            replacement.groups.push_back(group);
        };
        for (size_t i = 0; i < definitions.size(); ++i) {
            const auto& group = definitions[i];
            if ((group.id == -1 && group.name == "Overlay") || (group.id == -2 && group.name == "ScreenOverlay")) { visited[i] = 1; continue; }
            if (group.parentId == 0 && group.parentName.empty()) append(i);
        }
        if (std::find(visited.begin(), visited.end(), 0) != visited.end()) throw std::runtime_error("Orphan map groups outside root hierarchy");
        SceneGroupDefinition overlay; overlay.id = -1; overlay.name = "Overlay"; overlay.overlay = true;
        replacement.groups.push_back(overlay); overlay.id = -2; overlay.name = "ScreenOverlay"; replacement.groups.push_back(overlay);
        replacement.initialized = true; *target = std::move(replacement); error.clear(); return true;
    } catch (const std::exception& detail) { error = detail.what(); return false; }
}

bool loadSceneGroupsSGP1(lua_State* state, const std::vector<uint8_t>& bytes, std::string& error) {
    try {
        size_t cursor = 4;
        if (bytes.size() < 8 || std::memcmp(bytes.data(), "SGP1", 4)) throw std::runtime_error("Invalid SGP1 group metadata header");
        const auto u32 = [&]() {
            if (cursor > bytes.size() || bytes.size() - cursor < 4) throw std::runtime_error("Truncated SGP1 integer");
            uint32_t result = 0; for (unsigned shift = 0; shift < 32; shift += 8) result |= uint32_t(bytes[cursor++]) << shift;
            return result;
        };
        const auto string = [&]() {
            const uint32_t size = u32();
            if (size > 65536 || cursor > bytes.size() || bytes.size() - cursor < size) throw std::runtime_error("Truncated or oversized SGP1 string");
            std::string result(reinterpret_cast<const char*>(bytes.data() + cursor), size); cursor += size;
            if (result.find('\0') != std::string::npos) throw std::runtime_error("SGP1 name contains embedded zero");
            return result;
        };
        const uint32_t count = u32(); if (count > 10000) throw std::runtime_error("SGP1 group limit exceeded");
        std::vector<SceneGroupDefinition> definitions; definitions.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            SceneGroupDefinition group;
            group.id = std::bit_cast<int32_t>(u32()); group.parentId = std::bit_cast<int32_t>(u32());
            group.blendMode = std::bit_cast<int32_t>(u32()); const uint32_t visible = u32();
            if (visible > 1) throw std::runtime_error("SGP1 Visible must be0 or1"); group.visible = visible != 0;
            group.name = string(); group.parentName = string();
            const uint32_t children = u32(); if (children > 10000) throw std::runtime_error("SGP1 child limit exceeded");
            for (uint32_t j = 0; j < children; ++j) { const auto id = std::bit_cast<int32_t>(u32()); group.children.push_back({id, string()}); }
            definitions.push_back(std::move(group));
        }
        if (cursor != bytes.size()) throw std::runtime_error("Unexpected trailing SGP1 metadata");
        return initializeSceneGroups(state, definitions, error);
    } catch (const std::exception& detail) { error = detail.what(); return false; }
}

std::vector<SceneGroupDefinition> snapshotSceneGroups(lua_State* state) {
    const auto* result = lookup(state); return result ? result->groups : std::vector<SceneGroupDefinition>{};
}
std::string dumpSceneGroups(lua_State* state) {
    std::ostringstream out; const auto* result = lookup(state);
    if (!result) return "SCENE_GROUPS unavailable\n";
    out << "SCENE_GROUPS count=" << result->groups.size() << " initialized=" << result->initialized << '\n';
    for (size_t i = 0; i < result->groups.size(); ++i) {
        const auto& g = result->groups[i];
        out << i << ' ' << g.name << " id=" << g.id << " parent=" << g.parentName << " blend=" << g.blendMode
            << " visible=" << g.visible << " overlay=" << g.overlay << " children=" << g.children.size() << '\n';
    }
    return out.str();
}
