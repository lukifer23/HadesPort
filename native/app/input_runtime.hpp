#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct lua_State;

struct InputPlayerSnapshot {
    uint32_t playerIndex = 1; // Lua-facing one-based selector into PlayerManager slots.
    bool allowInput = true;
    std::vector<std::string> namedBlocks; // Unique names, sorted for diagnostics only.
    bool inputAllowed = true; // Exact IsInputAllowed: allowInput && namedBlocks.empty().
    int32_t activeUnitId=0;
    uint64_t activeUnitLifetime=0;
};
// Registers an empty input-state registry and the real AddInputBlock,
// RemoveInputBlock, IsInputAllowed APIs. No player is silently created.
// No GetInputBlock Lua API exists in this engine build.
void registerInputRuntime(lua_State*);
// Explicit integration policy creates local slots sequentially (1,2,...),
// using recovered native Player ctor defaults; it creates no active unit.
bool createRecoveredInputPlayer(lua_State*, uint32_t playerIndex, std::string& error);
std::vector<InputPlayerSnapshot> snapshotInputPlayers(lua_State*);
std::string dumpInputRuntime(lua_State*);
bool isRecoveredInputAllowed(lua_State*, uint32_t playerIndex = 1);
bool hasRecoveredInputBlock(lua_State*, const std::string& name, uint32_t playerIndex = 1);
// C++ integration seam for Player.mAllowInput; this is not an invented Lua action.
bool setRecoveredPlayerInputPermission(lua_State*, uint32_t playerIndex, bool allowInput,
                                       std::string& error);
bool bindRecoveredPlayerUnit(lua_State*,uint32_t playerIndex,int32_t unitId,uint64_t lifetime,std::string& error);
// Membership is independent of ToggleControl enablement and the weapon catalog.
bool isRecoveredControlKnown(lua_State*,const std::string& name);
bool isRecoveredControlEnabled(lua_State*,const std::string& name);
// ToggleMove state of the PlayerUnit with this entity lifetime (PlayerUnitInput+0x8; default enabled).
bool isRecoveredUnitMoveEnabled(lua_State*,uint64_t unitLifetime);
