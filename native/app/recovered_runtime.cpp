#include "recovered_runtime.hpp"
#include "config_file.hpp"
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
struct GaussianStream {uint64_t state=initialState;double spare=0;bool hasSpare=false;};
struct Runtime {
    Generator generators[64];
    // Gaussian initializer RVA0x20950, array RVA0x1ca3640: each of the 64
    // 32-byte native records starts with this same PCG state/increment and
    // zero spare/flag. Seed RVA0x201660 writes Rngs, never this separate array.
    GaussianStream gaussian[64];
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
uint32_t next(uint64_t& state) {
    const uint64_t old = state;
    state = old * multiplier + increment;
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
    const double result = std::ldexp(static_cast<double>(next(generator.state)), -32);
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
        do { offset = next(generator.state); } while (offset < threshold);
        offset %= bound;
    }
    ++generator.sequence;
    lua_pushinteger(state, std::bit_cast<int32_t>(offset + static_cast<uint32_t>(low)));
    return 1;
}
int randomGaussian(lua_State* state){
#if defined(__clang__)
    // The recovered x64 helper uses separate scalar mul/add instructions;
    // Android ARM64 must not contract radius/factor operations into an FMA.
#pragma clang fp contract(off)
#endif
    Runtime& r=*runtime(state);
    const int id=int32Argument(state,1);
    // Native ScriptManager lambda RVA0x287be0: Count gate is shared, but
    // Gaussian[id] has independent PCG state, spare(+0x10) and flag(+0x18).
    // No normal-Rng sequence index changes on either branch.
    if(id<0||id>=r.count){lua_pushnumber(state,0);return 1;}
    auto& stream=r.gaussian[id];
    if(stream.hasSpare){stream.hasSpare=false;lua_pushnumber(state,stream.spare);return 1;}
    double x,y,radius;
    do{
        const double u=std::ldexp(static_cast<double>(next(stream.state)),-32);
        x=(u+u)-1.;
        const double v=std::ldexp(static_cast<double>(next(stream.state)),-32);
        y=(v+v)-1.;
        const double xx=x*x;
        const double yy=y*y;
        radius=yy+xx; // Native RVA0x287d18..2b: y squared, then add x squared.
    }while(radius>=1.||radius==0.);
    // RVA0x287d46..92 calls native log/sqrt in this exact order, returns
    // factor*x and caches factor*y. Last-bit Windows CRT/libm parity is not
    // established by using the platform implementations of these functions.
    const double logged=std::log(radius);
    const double scaled=logged*-2.;
    const double divided=scaled/radius;
    const double factor=std::sqrt(divided);
    stream.spare=factor*y;stream.hasSpare=true;
    lua_pushnumber(state,factor*x);return 1;
}
int mathRandom(lua_State* state){
    // InitLua RVA0x2860de installs an arity/truthiness dispatcher over native
    // random/randomint, defaulting to engine stream0. Reconstruct its behavior
    // here instead of retaining Lua's unrelated libc PRNG or copying the shim.
    int arguments=0;const char* operation="random";
    if(lua_toboolean(state,1)&&lua_toboolean(state,2)){lua_settop(state,2);arguments=2;operation="randomint";}
    else if(lua_toboolean(state,1)){lua_settop(state,1);lua_pushinteger(state,1);lua_insert(state,1);arguments=2;operation="randomint";}
    else lua_settop(state,0);
    // The native-installed Lua function resolves the globals at call time.
    lua_getglobal(state,operation);lua_insert(state,1);lua_call(state,arguments,LUA_MULTRET);
    return lua_gettop(state);
}
const hades::config::OptionSpec* configSpec(lua_State* state) {
    luaL_checktype(state, 1, LUA_TTABLE);
    lua_getfield(state, 1, "Name");
    size_t length=0;
    const char* name=luaL_checklstring(state,-1,&length);
    if(length>=256||std::memchr(name,0,length))luaL_error(state,"Invalid config option name");
    // The registered field map is the sole owner of the 346 registered defaults
    // and type tags (registerField / ConfigOptions dynamic initializers; see
    // config-file-contract.json). Script GetPropertyInfo is case-insensitive.
    if(const auto* spec=hades::config::findOption(std::string_view(name,length)))return spec;
    // These four native GetPropertyInfo fields are absent from the registered
    // persistence map. ConfigOptions::Defaults VA1801058f6/5900/590a explicitly
    // writes float1 to DamageMultiplier/DamageTakenMultiplier/SpeedMultiplier
    // (global RVAs6a7f10/6a7f30/6a7f14). DisableDamage is Bool2 at GetPropertyInfo
    // RVA109233, loader-zero globalRVA6a7eff. Never duplicate registered fields.
    using hades::config::FieldType;
    static constexpr hades::config::OptionSpec nativeOnly[]={
        {"SpeedMultiplier",FieldType::Float,0,false,0,1.f,"",0},
        {"DamageMultiplier",FieldType::Float,0,false,0,1.f,"",0},
        {"DamageTakenMultiplier",FieldType::Float,0,false,0,1.f,"",0},
        {"DisableDamage",FieldType::Bool,0,false,0,0.f,"",0},
    };
    for(const auto& spec:nativeOnly)if(strcasecmp(name,spec.name)==0)return &spec;
    luaL_error(state,"Native config option not recovered yet: %s",name);return nullptr;
}
int getConfigDefault(lua_State* state) {
    const auto* spec=configSpec(state);
    using hades::config::FieldType;
    switch(spec->type){
    case FieldType::Bool:lua_pushboolean(state,spec->defaultBool);return 1;
    case FieldType::Int:case FieldType::Long:lua_pushinteger(state,spec->defaultInt);return 1;
    case FieldType::Float:lua_pushnumber(state,spec->defaultFloat);return 1;
    case FieldType::String:lua_pushstring(state,spec->defaultString);return 1;
    case FieldType::Other:return luaL_error(state,"Native config enum/non-scalar option not recovered yet: %s",spec->name);
    }
    return luaL_error(state,"Native config property type not recovered yet: %s",spec->name);
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
    // GetValue RVA0x2509af returns an empty any for nil/missing Value.
    // SetPropertyValue's string branch RVA0x202d6c..0x202d9c stores "" for
    // that empty any. This is an assignment, not removal/reset of an override.
    if(expectedType==LUA_TSTRING&&lua_isnil(state,-1)){lua_pop(state,1);lua_pushliteral(state,"");}
    if (lua_type(state, -1) != expectedType) return luaL_error(state, "Config Value has wrong type for recovered option");
    if (expectedType == LUA_TNUMBER) {
        const lua_Number value = lua_tonumber(state, -1);
        const auto* spec=configSpec(state);lua_pop(state,1);
        const bool nativeFloat=spec->type==hades::config::FieldType::Float;
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
    bind(state, userdata, "randomgaussian", randomGaussian);
    lua_getglobal(state,"math");
    lua_pushcfunction(state,mathRandom);lua_setfield(state,-2,"random");
    lua_pop(state,1);
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
    auto& g=r->generators[0];value=std::ldexp(static_cast<double>(next(g.state)),-32);++g.sequence;return true;
}
bool nextRecoveredEngineBoundedRandom(lua_State* L,uint32_t bound,uint32_t& value) {
    if(!bound)return false;
    lua_getfield(L,LUA_REGISTRYINDEX,runtimeRegistryKey);
    auto* r=static_cast<Runtime*>(lua_touserdata(L,-1));lua_pop(L,1);if(!r)return false;
    auto& g=r->generators[0];const uint32_t threshold=(uint32_t(0)-bound)%bound;
    uint32_t sample=0;do{sample=next(g.state);}while(sample<threshold);
    value=sample%bound;++g.sequence;return true;
}
