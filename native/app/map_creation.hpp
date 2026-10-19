#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct MapCreationPoint { float x=0,y=0; };
struct MapCreationRecord {
    int32_t id=0;
    std::string name;
    bool sourceActive=false;
    bool doCreatePresence=false; // Serialized payload presence; NEVER an Active flag.
    bool activateAtRange=false;
    float activationRange=0;
    float x=0,y=0;
    int32_t parentId=0;
    std::vector<int32_t> childIds; // Exact original AttachedIDs list order.
    int32_t dataType=0; // Original MapThingDef: 1 obstacle, 2 unit.
    std::optional<bool> playerControlled; // Required resolved definition for unit data.
    bool triggerOnSpawn=false; // Resolved Thing definition gate, not proof callback ran.
};
struct MapCreationContext {
    bool editingMode=false;
    int32_t worldFrame=0;
    // Position used by original top-level range gate: active-unit location of
    // the first non-null PlayerManager player. Missing unit/location -> absent.
    std::optional<MapCreationPoint> firstPlayerLocation;
};
enum class MapCreationKind { Obstacle, PlayerUnit, EnemyUnit };
enum class MapDeferredReason { SourceInactive, RangeDeferred, AncestorDeferred };
struct MapCreationEntry {
    int32_t id=0,parentId=0;
    size_t sourceOrdinal=0; // Index in caller's original source-ordered records.
    uint32_t depth=0;
    MapCreationKind kind=MapCreationKind::Obstacle;
    bool attachedChild=false;
    bool definitionTriggerOnSpawn=false;
};
struct MapDeferredEntry {
    int32_t id=0,parentId=0,deferredRootId=0;
    size_t sourceOrdinal=0;
    MapDeferredReason reason=MapDeferredReason::SourceInactive;
    // Native top-level inactive table receives roots only. Attached descendants
    // wait for parent creation; do not claim they are individually registered.
    bool nativeInactiveRegistration=false;
    bool rangeRegistration=false;
};
struct MapCreationPlan {
    std::vector<MapCreationEntry> creation;
    std::vector<MapDeferredEntry> deferred;
};
// Pure planning only: validates payload identity, unique IDs, complete reciprocal
// attachment references, child uniqueness and cycles before producing output.
// Keeps source root order and native ordered depth-first child traversal.
// Native GroupManager hash traversal order is not reconstructed here.
// No Lua/entity mutation, physics, final transforms or callback dispatch.
bool planMapCreation(const std::vector<MapCreationRecord>&,const MapCreationContext&,
                     MapCreationPlan& result,std::string& error);
std::string dumpMapCreationPlan(const MapCreationPlan&);
