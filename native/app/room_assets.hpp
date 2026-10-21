#pragma once
// Map-scoped asset view for multi-room packs.
//
// The staged asset root holds the boot map's room-bound packs (the original
// ConfigOptions::DirectLoad default "RoomOpening", initializer RVA 0xe620; see
// config_file.cpp). Every other map's room-bound packs live under
// rooms/<MapName>/ (native/tools/stage_room_packs.py). For a non-root map the
// room-bound names below are served ONLY from that directory: a missing file
// returns nullptr so the consumer fails loudly. Opening data is never reused
// as a stand-in for another room. Shared packs (scripts, UI, hero, weapons,
// audio, action FX...) are always read from the root.
#include "asset_store.hpp"
#include <array>
#include <string>
#include <string_view>

namespace hades {
inline constexpr std::string_view kRootMapName = "RoomOpening";
inline constexpr std::array<std::string_view, 26> kRoomBoundAssets{{
    "scene.bin", "scene-sha256.bin", "materials.bin", "ambient.bin", "composition.bin",
    "animations.bin", "animations-sha256.bin", "parallax.bin", "hulls.bin",
    "render-options.bin", "groups.bin", "camera.bin", "player-world.bin", "ordering.bin",
    "DecodedMapObjects.lua", "actors.lua", "effects.lua", "spawner-frames.bin",
    "room-leaf-frames.bin", "room-leaf-facing.bin", "room-leaf-sha256.bin",
    "primaries.lua", "primary-child-frames.bin",
    "actor-lights.lua", "actor-light-frames.bin", "actor-light-sha256.bin",
}};
// Room-bound but legitimately absent for maps without map primary spawners:
// effects.lua then has no PrimaryRecipes and lua_runtime opens neither the
// spawner nor the leaf bank. Still never served from the root for another map.
inline constexpr std::array<std::string_view, 4> kOptionalRoomAssets{{
    "spawner-frames.bin", "room-leaf-frames.bin", "room-leaf-facing.bin", "room-leaf-sha256.bin",
}};
inline bool isOptionalRoomAsset(std::string_view name) {
    for (const auto optional : kOptionalRoomAssets) if (optional == name) return true;
    return false;
}
inline bool isRoomBoundAsset(std::string_view name) {
    for (const auto bound : kRoomBoundAssets) if (bound == name) return true;
    return false;
}
// Bounded map-name check: original map names are ASCII identifiers.
inline bool validMapName(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    for (const char c : name) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
class RoomAssets final : public AssetStore {
    AssetStore& base_;
    std::string map_;
public:
    RoomAssets(AssetStore& base, std::string map) : base_(base), map_(std::move(map)) {}
    const std::string& map() const { return map_; }
    bool selectMap(std::string map){if(!validMapName(map))return false;map_=std::move(map);return true;}
    std::unique_ptr<AssetFile> open(std::string_view name) override {
        if (map_ == kRootMapName || !isRoomBoundAsset(name)) return base_.open(name);
        std::string scoped = "rooms/" + map_ + "/";
        scoped.append(name);
        return base_.open(scoped);
    }
};
}
