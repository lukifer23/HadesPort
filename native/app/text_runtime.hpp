#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include "text_layout.hpp"
struct lua_State;

// Explicit values copied from the original format table. Missing properties
// remain absent: this interface does not invent engine/font defaults.
struct TextFormatProperty {
    enum class Kind { Number, Boolean, String, NumericArray } kind = Kind::Number;
    double number = 0;
    bool boolean = false;
    std::string string;
    std::vector<double> array;
};
struct TextFormatSnapshot {
    uint64_t sequence = 0; // Reconstruction-local component identity, not a native handle.
    int32_t ownerId = 0;
    std::string name;
    std::map<std::string, TextFormatProperty> properties;
    std::optional<int32_t> fontSize; // Native CreateTextBox explicitly reads this as int.
    bool ownerActive = false;
};
// Retains empty formatting components and consumes admitted original English
// nonempty text through source glyphs. Unknown fonts/macros/character fades
// remain explicit errors. Presentation is sampled from actual live owners.
void registerTextRuntime(lua_State*);
void configureTextRuntime(lua_State*,std::shared_ptr<const hades::TextFontBank>);
void disposeTextOwner(lua_State*,int32_t ownerId);
bool advanceTextRuntime(lua_State*,float worldElapsed,float screenElapsed,std::string&);
std::vector<hades::TextVisual> snapshotTextVisuals(lua_State*);
std::vector<TextFormatSnapshot> snapshotTextFormats(lua_State*);
std::string dumpTextRuntime(lua_State*);
