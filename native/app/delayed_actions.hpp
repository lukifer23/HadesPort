#pragma once
#include <cstdint>
#include <string>
#include <vector>
struct lua_State;

enum class DelayedActionAdmission { ExecuteNow, Queued };
struct DelayedActionSnapshot {
    uint64_t sequence = 0;
    int actionId = -1;
    float deadline = 0;
};
struct DelayedActionsSnapshot {
    bool initialized = false;
    bool pumping = false;
    bool faulted = false;
    uint64_t executed = 0;
    uint64_t cancelled = 0;
    std::vector<DelayedActionSnapshot> pending;
    std::vector<DelayedActionSnapshot> interrupted;
    std::string error;
};
// Parent supplies only genuine implemented native Void action IDs. Float,
// Bool, String, Object and Table actions have their own admission contracts.
bool initializeDelayedActions(lua_State*, const std::vector<int>& supportedVoidActionIds,
                              std::string& error);
using DelayedActionRandom = bool (*)(void* context, float minimum, float maximum,
                                     float& result, std::string& error);
// Call at a verified Void action's bridge boundary. ExecuteNow means parent
// executes its real immediate handler. Queued means this VM retains the actual
// source args table strongly (native ScriptArgs copies Lua refs, not Lua data).
// The optional RNG seam must draw from the recovered native deterministic stream.
bool admitDelayedAction(lua_State*, int actionId, int argsIndex, float worldTime,
                        DelayedActionAdmission& outcome, std::string& error,
                        DelayedActionRandom random = nullptr, void* randomContext = nullptr);
// Called with a borrowed original args table on the VM stack. Parent must
// execute under a Lua protected call, bypass admission, preserve that stack,
// and return errors rather than longjmp through this C++ bridge.
using DelayedActionExecutor = bool (*)(lua_State*, int actionId, int argsIndex,
                                       void* context, std::string& error);
// Native ScriptManager pumps delayed actions before original Main.update.
// Deadlines and comparisons use float; ready iff worldTime > deadline.
// New admissions during this pump are retained for the next pump.
bool advanceDelayedActions(lua_State*, float worldTime, DelayedActionExecutor,
                           void* context, std::string& error);
// Explicit full-queue cancellation, corresponding to native ScriptManager::Clear.
// Clears the queue's retained fault. Parent owns other script/lifecycle resets.
bool clearDelayedActions(lua_State*, std::string& error);
DelayedActionsSnapshot snapshotDelayedActions(lua_State*);
std::string dumpDelayedActions(lua_State*);
