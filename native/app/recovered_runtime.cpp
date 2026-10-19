#include "recovered_runtime.hpp"
#include <bit>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstring>
#include <new>
#include <strings.h>
extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

namespace {
// EngineWin64s.dll SHA256 d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254,
// matching PDB GUID15f83bcc-e740-4d0b-bf67-6ac938c8bdfa age1.
// PDB-bounded disassembly is saved in .local/reports/native-rng-*-annotated.json.
constexpr uint64_t multiplier = UINT64_C(0x5851f42d4c957f2d);
constexpr uint64_t increment = UINT64_C(0xb47c73972972b7b7);
constexpr uint64_t seedSubtract = UINT64_C(0x03d657cc62bc341e);
constexpr uint64_t initialState = UINT64_C(0xea1b84321029ee21);
struct Generator { uint64_t state = initialState; uint32_t references = 0, sequence = 0; };
struct Runtime {
    Generator generators[64];
    int count = 1;
    int32_t lastLuaSeed = 0;
    char language[3] = {};
    Runtime() { generators[0].references = 1; }
};
constexpr const char* runtimeRegistryKey = "hades.recoveredRuntime";
// Native reserves stream0 for engine use (Random.lua:36). Its pre-Lua boot seed
// is not recovered here. Retain the proven static initializer for stream0;
// explicit randomseed(s,id), including the script's stream1, is exact below.
Runtime* runtime(lua_State* state) {
    return static_cast<Runtime*>(lua_touserdata(state, lua_upvalueindex(1)));
}
int32_t int32Argument(lua_State* state, int index, int32_t fallback = 0) {
    if (lua_isnoneornil(state, index)) return fallback;
    const auto value = static_cast<uint32_t>(luaL_checkinteger(state, index));
    return std::bit_cast<int32_t>(value);
}
// Random::RandomDouble RVA0x54f80 and pcg32_boundedrand_r RVA0x327dd0.
uint32_t next(Generator& generator) {
    const uint64_t old = generator.state;
    generator.state = old * multiplier + increment;
    const uint32_t xorshifted = static_cast<uint32_t>((old >> 45) ^ (old >> 27));
    const uint32_t rotation = static_cast<uint32_t>(old >> 59);
    return (xorshifted >> rotation) | (xorshifted << ((-rotation) & 31));
}
int randomSeed(lua_State* state) {
    Runtime& r = *runtime(state);
    const int32_t seed = int32Argument(state, 1);
    int id = int32Argument(state, 2, -1);
    // RandomNumberGenerator::Seed RVA0x201660, 423 bytes.
    if (id == 1) r.lastLuaSeed = seed;
    if (id == -1) {
        if (r.count >= 64) { lua_pushinteger(state, 0); return 1; }
        id = 0;
        while (id < 64 && r.generators[id].references > 0) ++id;
    }
    if (id < 0 || id >= 64) { lua_pushinteger(state, 0); return 1; }
    Generator& generator = r.generators[id];
    generator.state = static_cast<uint64_t>(static_cast<int64_t>(seed)) * multiplier - seedSubtract;
    if (generator.references == 0) ++r.count;
    ++generator.references;
    generator.sequence = 0;
    lua_pushinteger(state, id);
    return 1;
}
int randomNumber(lua_State* state) {
    Runtime& r = *runtime(state);
    const int id = int32Argument(state, 1);
    // Native lambda RVA0x287b10 checks count, returns 0 for invalid IDs.
    if (id < 0 || id >= r.count) { lua_pushnumber(state, 0); return 1; }
    Generator& generator = r.generators[id];
    const double result = std::ldexp(static_cast<double>(next(generator)), -32);
    ++generator.sequence;
    lua_pushnumber(state, result);
    return 1;
}
int randomInteger(lua_State* state) {
    Runtime& r = *runtime(state);
    const int32_t low = int32Argument(state, 1);
    const int32_t high = int32Argument(state, 2);
    const int id = int32Argument(state, 3);
    // Native lambda RVA0x287b60 uses inclusive signed bounds, unsigned
    // subtraction, and rejection sampling through pcg32_boundedrand_r.
    if (id < 0 || id >= r.count) { lua_pushinteger(state, 0); return 1; }
    Generator& generator = r.generators[id];
    const uint32_t bound = static_cast<uint32_t>(high) - static_cast<uint32_t>(low) + 1;
    uint32_t offset = 0;
    if (bound != 0) {
        const uint32_t threshold = (uint32_t(0) - bound) % bound;
        do { offset = next(generator); } while (offset < threshold);
        offset %= bound;
    }
    ++generator.sequence;
    lua_pushinteger(state, std::bit_cast<int32_t>(offset + static_cast<uint32_t>(low)));
    return 1;
}
int getConfigDefault(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "Name");
    const char* name = luaL_checkstring(state, -1);
    // Exact type/value stores in native dynamic initializers, not guessed fallbacks.
    // RVA0x10580 proves DebugRNGSeed is int0. The remaining six are bools;
    // their full initializer disassembly is native-config-<Name>-annotated.json.
    if (strcasecmp(name, "DebugRNGSeed") == 0 || strcasecmp(name, "FreeFormSelecSearchFromId") == 0) {
        // FreeFormSelecSearchFromId RVA0x12e70 also initializes an int to zero.
        lua_pushinteger(state, 0); return 1;
    }
    if (strcasecmp(name, "KioskModeNumLives") == 0) { // RVA0xd9e0
        lua_pushinteger(state, 2); return 1;
    }
    if (strcasecmp(name, "ExclusiveInteractGroup") == 0) { // RVA0xa880, empty string default
        lua_pushliteral(state, ""); return 1;
    }
    if(strcasecmp(name,"FullscreenEffectGroup")==0){ // Exact initializer18000cf40 passes "Vignette" to registerField<string>.
        lua_pushliteral(state,"Vignette");return 1;
    }
    if(strcasecmp(name,"SpeedMultiplier")==0||strcasecmp(name,"DamageMultiplier")==0||strcasecmp(name,"DamageTakenMultiplier")==0){
        // ConfigOptions.Defaults105830, VA18010590a ->globalRVA6a7f14.
        // DamageMultiplier VA1801058f6->6a7f10 and DamageTakenMultiplier
        // VA180105900->6a7f30 both explicitly write float1. GetPropertyInfo
        // exposes both as Float4 (107b09/109768); retained combat-config report.
        lua_pushnumber(state,1.0);return 1;
    }
    struct Option { const char* name; bool value; };
    constexpr Option options[] = {
        {"DisableDamage", false}, // Bool2/GetPropertyInfo109233; loader-zero virtual .data tail atRVA6a7eff.
        {"AllowPlaySpeech", true}, // Exact original source Defaults18000b9a0; mutable VM setting thereafter.
        {"Subtitles", true}, // Exact original source initializer18000cca5; mutable VM setting thereafter.
        {"ZeroMouseTether", false}, // Initializer18000ebef writes byte0 to181a0ed5b.
        {"ShowDamageNumbers", true}, // RVA0xc730
        {"ShowUIAnimations", true}, // RVA0xc6b0
        {"LogCombatMultipliers", false}, // RVA0x9650
        {"ShowSpeechBubble", true}, // RVA0xd1c0
        {"EasyMode", false}, // RVA0xd860
        {"HardMode", false}, // RVA0xd8e0
        {"AllowStripRequirements", false}, // RVA0x10930
        {"DrawColorGrading", true}, // Source initializer18000f670 writes true at181a0f298.
        {"DrawBloom", true}, // RVA0xf4e0
        {"DumpRunStatsOnLoad", false}, // RVA0x9b50
        {"EditingMode", false}, // RVA0xbb30
        {"EnableConsoleMenus", false}, // RVA0x12af0
        {"FastForward", false}, // RVA0x9bd0
        {"ForceFlipMapThings", false}, // RVA0x113b0
        {"KioskMode", false}, // RVA0xd960
        {"ResetRoomData", false}, // RVA0x11430
        {"UseMouse", false}, // RVA0xa0e0
        {"GamepadCursorControl", false}, // RVA0xa560
        {"FlipMapThings", false}, // RVA0x11330
        {"UseOcclusion", true}, // initializer RVA0xba30: movb $1 at 0x18000baa0 -> 0x181a0ed01
        {"CameraShake", true}, // initializer RVA0xd040 movb $1 -> 0x181a0f2d9 (.local/reports/config-defaults)
    };
    for (const auto& option : options) {
        if (strcasecmp(name, option.name) == 0) { lua_pushboolean(state, option.value); return 1; }
    }
    return luaL_error(state, "Native config option not recovered yet: %s", name);
}
constexpr const char* configOverridesKey = "hades.recoveredConfigOverrides";
void configKey(lua_State* state, char (&key)[256]) {
    lua_getfield(state, 1, "Name");
    size_t length = 0;
    const char* name = luaL_checklstring(state, -1, &length);
    if (length >= sizeof(key)) luaL_error(state, "Config option name too long");
    for (size_t i = 0; i < length; ++i) key[i] = name[i] >= 'A' && name[i] <= 'Z' ? name[i] + ('a' - 'A') : name[i];
    key[length] = '\0';
    lua_pop(state, 1);
}
int getConfig(lua_State* state) {
    getConfigDefault(state); // Validate and obtain the proven typed compiled default.
    char key[256]; configKey(state, key);
    lua_getfield(state, LUA_REGISTRYINDEX, configOverridesKey);
    lua_getfield(state, -1, key);
    if (lua_isnil(state, -1)) { lua_pop(state, 2); return 1; }
    return 1;
}
int setConfig(lua_State* state) {
    // ScriptAction::SetConfigOption RVA0x265080 resolves reflected property by
    // Name and stores Value, converting native int/float properties explicitly.
    // Supported properties retain their recovered type; unknown fields fail.
    getConfigDefault(state);
    const int expectedType = lua_type(state, -1);
    lua_getfield(state, 1, "Value");
    if (lua_type(state, -1) != expectedType) return luaL_error(state, "Config Value has wrong type for recovered option");
    if (expectedType == LUA_TNUMBER) {
        const lua_Number value = lua_tonumber(state, -1);
        char option[256];configKey(state,option);
        const bool nativeFloat=std::strcmp(option,"speedmultiplier")==0||std::strcmp(option,"damagemultiplier")==0||std::strcmp(option,"damagetakenmultiplier")==0;
        if(!std::isfinite(value))return luaL_error(state,"Recovered config requires finite Value");
        if(nativeFloat){if(std::abs(value)>std::numeric_limits<float>::max())return luaL_error(state,"Recovered float config outside float32 range");lua_pop(state,1);lua_pushnumber(state,float(value));}
        else if(value<INT32_MIN||value>INT32_MAX||std::trunc(value)!=value)return luaL_error(state,"Recovered integer config requires a signed32-bit integer Value");
    }
    char key[256]; configKey(state, key);
    lua_getfield(state, LUA_REGISTRYINDEX, configOverridesKey);
    lua_pushvalue(state, -2);
    lua_setfield(state, -2, key);
    return 0;
}
int getLanguage(lua_State* state) {
    Runtime& r = *runtime(state);
    // GetLanguage=Action281. ExecuteString RVA0x2556a0 uses switch tables
    // RVA0x255a18/0x2559f0 to reach0x25584f, copying Localization::UiLang's
    // language-code string at globalRVA0x1ca33c0. Native Language option's
    // compiled default is empty (initializer0x12f80); Localization::Init
    // RVA0x1fd220 selects an OS locale or nonempty language override.
    if (r.language[0] == '\0') return luaL_error(state, "Native UI language has not been configured");
    lua_pushstring(state, r.language);
    return 1;
}
void bind(lua_State* state, int userdata, const char* name, lua_CFunction function) {
    lua_pushvalue(state, userdata);
    lua_pushcclosure(state, function, 1);
    lua_setglobal(state, name);
}
} // namespace

void registerRecoveredRuntime(lua_State* state) {
    const int oldTop = lua_gettop(state);
    // Trivially destructible POD ownership: Lua userdata itself is the allocation.
    auto* memory = lua_newuserdata(state, sizeof(Runtime));
    new (memory) Runtime();
    const int userdata = lua_gettop(state);
    lua_pushvalue(state, userdata);
    lua_setfield(state, LUA_REGISTRYINDEX, runtimeRegistryKey);
    lua_newtable(state);
    lua_setfield(state, LUA_REGISTRYINDEX, configOverridesKey);
    bind(state, userdata, "GetConfigOptionValue", getConfig);
    bind(state, userdata, "SetConfigOption", setConfig);
    bind(state, userdata, "GetLanguage", getLanguage);
    bind(state, userdata, "randomseed", randomSeed);
    bind(state, userdata, "random", randomNumber);
    bind(state, userdata, "randomint", randomInteger);
    // randomgaussian remains the fail-fast generic binding: its separate native
    // Gaussian stream lifetime/seeding is not reconstructed by these routines.
    lua_settop(state, oldTop);
}

bool configureRecoveredLanguage(lua_State* state, const char* languageCode) {
    if (!languageCode || std::strcmp(languageCode, "en") != 0) return false;
    lua_getfield(state, LUA_REGISTRYINDEX, runtimeRegistryKey);
    auto* r = static_cast<Runtime*>(lua_touserdata(state, -1));
    if (r) std::memcpy(r->language, "en", 3);
    lua_pop(state, 1);
    return r != nullptr;
}

bool nextRecoveredEngineRandom(lua_State* L,double& value) {
    lua_getfield(L,LUA_REGISTRYINDEX,runtimeRegistryKey);
    auto* r=static_cast<Runtime*>(lua_touserdata(L,-1));lua_pop(L,1);if(!r)return false;
    auto& g=r->generators[0];value=std::ldexp(static_cast<double>(next(g)),-32);++g.sequence;return true;
}
bool nextRecoveredEngineBoundedRandom(lua_State* L,uint32_t bound,uint32_t& value) {
    if(!bound)return false;
    lua_getfield(L,LUA_REGISTRYINDEX,runtimeRegistryKey);
    auto* r=static_cast<Runtime*>(lua_touserdata(L,-1));lua_pop(L,1);if(!r)return false;
    auto& g=r->generators[0];const uint32_t threshold=(uint32_t(0)-bound)%bound;
    uint32_t sample=0;do{sample=next(g);}while(sample<threshold);
    value=sample%bound;++g.sequence;return true;
}
