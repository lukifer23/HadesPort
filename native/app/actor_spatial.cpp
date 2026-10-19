#include "actor_spatial.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace hades {
bool ActorSpatialGrid::coverage(const SpatialBounds& b,std::vector<Cell>& cells,std::string& error){
    if(!std::isfinite(b.minX)||!std::isfinite(b.minY)||!std::isfinite(b.maxX)||!std::isfinite(b.maxY)||b.minX>b.maxX||b.minY>b.maxY){error="Invalid actual spatial bounds";return false;}
    const double x0=std::floor(double(b.minX)/512),x1=std::floor(double(b.maxX)/512);
    const double y0=std::floor(double(b.minY)/512),y1=std::floor(double(b.maxY)/512);
    if(x0<INT32_MIN||y0<INT32_MIN||x1>INT32_MAX||y1>INT32_MAX||x1-x0>4096||y1-y0>4096||(x1-x0+1)*(y1-y0+1)>65536){error="Spatial coverage exceeds bounded source geometry limits";return false;}
    cells.clear();cells.reserve(size_t((x1-x0+1)*(y1-y0+1)));
    for(int64_t x=int64_t(x0);x<=int64_t(x1);++x)for(int64_t y=int64_t(y0);y<=int64_t(y1);++y)cells.emplace_back(int32_t(x),int32_t(y));
    error.clear();return true;
}
void ActorSpatialGrid::remove(uint64_t lifetime){
    const auto record=records_.find(lifetime);if(record==records_.end())return;
    for(const auto& cell:record->second.cells){const auto i=cells_.find(cell);if(i==cells_.end())continue;auto& values=i->second;values.erase(std::remove(values.begin(),values.end(),lifetime),values.end());if(values.empty())cells_.erase(i);}
    records_.erase(record);
}
bool ActorSpatialGrid::update(const SpatialObstacle& obstacle,std::string& error){
    if(!obstacle.lifetime){error="Spatial obstacle requires actual lifetime identity";return false;}
    std::vector<Cell> cells;if(!coverage(obstacle.bounds,cells,error))return false;
    remove(obstacle.lifetime);
    for(const auto& cell:cells)cells_[cell].push_back(obstacle.lifetime);
    records_.emplace(obstacle.lifetime,Record{obstacle,std::move(cells)});error.clear();return true;
}
bool ActorSpatialGrid::query(const SpatialBounds& bounds,ObstacleGridCategory category,std::vector<SpatialObstacle>& out,std::string& error)const{
    std::vector<Cell> cells;if(!coverage(bounds,cells,error))return false;
    std::set<uint64_t> seen;out.clear();
    for(const auto& cell:cells){const auto i=cells_.find(cell);if(i==cells_.end())continue;for(const auto lifetime:i->second){if(!seen.insert(lifetime).second)continue;const auto record=records_.find(lifetime);if(record==records_.end())continue;const auto& o=record->second.obstacle;
        if(category==ObstacleGridCategory::StopsProjectiles&&!o.stopsProjectiles)continue;
        if(category==ObstacleGridCategory::StopsUnits&&!o.stopsUnits)continue;
        if(category==ObstacleGridCategory::MovementReaction&&!o.movementReaction)continue;
        if(o.bounds.maxX<bounds.minX||o.bounds.minX>bounds.maxX||o.bounds.maxY<bounds.minY||o.bounds.minY>bounds.maxY)continue;
        out.push_back(o);
    }}
    // Portable stable lifetime order, not Windows pointer/hash bucket order.
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.lifetime<b.lifetime;});error.clear();return true;
}
}
