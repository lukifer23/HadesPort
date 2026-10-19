#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hades {
struct SpatialBounds {float minX=0,minY=0,maxX=0,maxY=0;};
enum class ObstacleGridCategory {All,StopsProjectiles,StopsUnits,MovementReaction};
struct SpatialObstacle {
    int32_t id=0;uint64_t lifetime=0;SpatialBounds bounds;
    bool stopsProjectiles=false,stopsUnits=false,movementReaction=false;
};
// Android broadphase adaptation: unbounded sparse512-unit cells, conservative
// transformed float AABBs. Windows clamped map-grid indices/hash order are not
// exposed. Real narrowphase must still test each returned source polygon.
class ActorSpatialGrid {
    using Cell=std::pair<int32_t,int32_t>;
    struct Record {SpatialObstacle obstacle;std::vector<Cell> cells;};
    std::map<Cell,std::vector<uint64_t>> cells_;
    std::map<uint64_t,Record> records_;
    static bool coverage(const SpatialBounds&,std::vector<Cell>&,std::string&);
public:
    bool update(const SpatialObstacle&,std::string&);
    void remove(uint64_t lifetime);
    bool query(const SpatialBounds&,ObstacleGridCategory,std::vector<SpatialObstacle>&,std::string&,bool filterBounds=true)const;
    bool contains(uint64_t lifetime)const{return records_.contains(lifetime);}
    size_t size()const{return records_.size();}
};
enum class ActorLocationStage {Interaction,Light,Occluder,AttachedAnimations};
}
