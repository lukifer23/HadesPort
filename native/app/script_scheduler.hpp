#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
struct lua_State;

struct ScriptSchedulerSnapshot {
    bool initialized = false;
    bool faulted = false;
    uint64_t updateCalls = 0;
    uint64_t successfulUpdates = 0;
    float worldTime = 0;
    double screenTime = 0;
    size_t timedThreads = 0;
    size_t workingThreads = 0;
    size_t eventNames = 0;
    size_t eventWaiters = 0;
    size_t pendingKillTags = 0;
    std::string error;
};

// Call after the unmodified Main.lua has defined its scheduler. No waits,
// coroutines, timers or frame functions are replaced. Repeated initialization
// preserves the original cached functions and any recorded fault.
bool initializeScriptScheduler(lua_State*, std::string& error);
// ScriptManager::Update supplies World.mElapsedTime (float) first, then
// ScreenManager.s_ElapsedTime (double). Parent owns clock advancement/pause and
// native component/action update order. Main.update also calls Main.draw.
bool advanceScriptScheduler(lua_State*, float worldTime, double screenTime,
                            std::string& error);
// Invoke the original Lua event function. existingOnly selects
// notifyExistingWaiters; otherwise notify retains an event with no listeners.
bool notifyScriptEvent(lua_State*, const std::string& event, bool existingOnly,
                       bool wasTimeout, std::string& error,bool omitTimeoutArgument=false);
ScriptSchedulerSnapshot snapshotScriptScheduler(lua_State*);
std::string dumpScriptScheduler(lua_State*);
