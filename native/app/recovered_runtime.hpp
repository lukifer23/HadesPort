#pragma once
#include <cstdint>
struct lua_State;

// Register only artifact-proven implementations; call after generic API guards.
// State belongs to this Lua VM and is reclaimed by lua_close.
void registerRecoveredRuntime(lua_State* state);

// Select the reconstructed host UI locale explicitly after registration.
// Currently only English is supported; false means no state/config was changed.
bool configureRecoveredLanguage(lua_State* state, const char* languageCode);

// Source stream0, using the recovered compiled initial state until a boot seed is selected.
bool nextRecoveredEngineRandom(lua_State*,double& value);
// Exact engine stream0 bounded PCG path; bound1 still consumes one PCG draw.
// Zero bound is rejected, matching callers' positive exclusive-bound contract.
bool nextRecoveredEngineBoundedRandom(lua_State*,uint32_t bound,uint32_t& value);
