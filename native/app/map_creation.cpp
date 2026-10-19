#include "map_creation.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {
// Original EngineWin64s.dll d98546ea54371910ea4499216453994d78e9b4e23b3381e457f8a1648a32e254,
// PDB GUID15f83bcc-e740-4d0b-bf67-6ac938c8bdfa age1.
// World::CreateMapThings RVA0x316d10: top-level Active/EditingMode,
// AttachToID and ActivateAtRange gates; native-lifecycle-CreateMapThings.json.
// World::CreateChildren RVA0x313200, REA
// ev_92816af134504b5a00a1094e6395c1c35572d1cf3e4dcebe90ba048fc3b9c823:
// pushes child IDs reversed, pops back, creates obstacle child immediately,
// removes inactive record, attaches to parent, then pushes grandchildren.
// It does not test child Active/ActivateAtRange or choose UnitManager.
using Index=std::map<int32_t,size_t>;
void require(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}
std::string label(const MapCreationRecord& record){return std::to_string(record.id)+" ("+record.name+")";}
MapCreationKind kind(const MapCreationRecord& record) {
    if(record.dataType==1)return MapCreationKind::Obstacle;
    return *record.playerControlled?MapCreationKind::PlayerUnit:MapCreationKind::EnemyUnit;
}
bool inRange(const MapCreationRecord& record,const MapCreationContext& context) {
    if(!context.firstPlayerLocation || context.worldFrame<=1)return false;
    // Preserve original float squared-distance comparison, including the
    // native squared treatment of a negative finite ActivationRange.
    const float dx=record.x-context.firstPlayerLocation->x;
    const float dy=record.y-context.firstPlayerLocation->y;
    const float distanceSquared=dx*dx+dy*dy;
    const float rangeSquared=record.activationRange*record.activationRange;
    return distanceSquared<=rangeSquared;
}
void validate(const std::vector<MapCreationRecord>& records,const MapCreationContext& context,Index& index) {
    require(records.size()<=100000,"Map creation record count exceeds reconstruction limit100000");
    if(context.firstPlayerLocation)require(std::isfinite(context.firstPlayerLocation->x)&&std::isfinite(context.firstPlayerLocation->y),"Map creation player location must be finite");
    for(size_t i=0;i<records.size();++i) {
        const auto& record=records[i];
        require(record.doCreatePresence,"Map creation record ordinal"+std::to_string(i)+" has absent serialized payload identity (DoCreate=false); native default-record allocation is not reconstructed");
        require(record.id>0&&!record.name.empty(),"Map creation record has invalid payload ID/name at ordinal"+std::to_string(i));
        require(record.name.size()<=65536 && record.name.find('\0')==std::string::npos,"Map creation name outside supported bounds");
        require(index.emplace(record.id,i).second,"Duplicate map creation ID: "+std::to_string(record.id));
        require(record.dataType==1||record.dataType==2,"Unknown map DataType for "+label(record));
        require(record.dataType!=2||record.playerControlled.has_value(),"Missing resolved unit PlayerControlled for "+label(record));
        require(record.dataType!=1||!record.playerControlled.value_or(false),"Obstacle identity incorrectly marked PlayerControlled: "+label(record));
        require(std::isfinite(record.x)&&std::isfinite(record.y)&&std::isfinite(record.activationRange),"Non-finite map creation location/range: "+label(record));
        require(record.parentId>=0&&record.parentId!=record.id,"Invalid self/negative map parent: "+label(record));
        require(record.childIds.size()<=records.size(),"Map child list exceeds record count: "+label(record));
    }
    for(const auto& record:records) {
        std::set<int32_t> children;
        for(int32_t id:record.childIds) {
            const auto child=index.find(id);
            require(id>0&&id!=record.id&&child!=index.end(),"Missing/self child reference "+std::to_string(id)+" of "+label(record));
            require(children.insert(id).second,"Duplicate child reference "+std::to_string(id)+" of "+label(record));
            require(records[child->second].parentId==record.id,"Child parent/list mismatch for "+std::to_string(id)+" under "+label(record));
        }
        if(record.parentId) {
            const auto parent=index.find(record.parentId);
            require(parent!=index.end(),"Missing parent "+std::to_string(record.parentId)+" of "+label(record));
            const auto& ids=records[parent->second].childIds;
            require(std::find(ids.begin(),ids.end(),record.id)!=ids.end(),"Parent omits child "+label(record));
            require(record.dataType==1,"Attached child requires unreconstructed native unit-as-obstacle path: "+label(record));
        }
    }
    // Each node has at most one parent; iterative parent-chain coloring avoids
    // recursion limits while detecting cycles anywhere, including inactive roots.
    std::vector<uint8_t> color(records.size(),0);
    for(size_t initial=0;initial<records.size();++initial) {
        if(color[initial])continue;
        std::vector<size_t> path;size_t current=initial;
        while(!color[current]) {
            color[current]=1;path.push_back(current);
            const int32_t parent=records[current].parentId;
            if(!parent)break;
            current=index.at(parent);
            require(color[current]!=1,"Attachment cycle includes "+label(records[current]));
        }
        for(size_t at:path)color[at]=2;
    }
}
const char* kindName(MapCreationKind kind) {
    switch(kind){case MapCreationKind::Obstacle:return "obstacle";case MapCreationKind::PlayerUnit:return "player_unit";case MapCreationKind::EnemyUnit:return "enemy_unit";}return "invalid";
}
const char* reasonName(MapDeferredReason reason) {
    switch(reason){case MapDeferredReason::SourceInactive:return "source_inactive";case MapDeferredReason::RangeDeferred:return "range_deferred";case MapDeferredReason::AncestorDeferred:return "ancestor_deferred";}return "invalid";
}
}
bool planMapCreation(const std::vector<MapCreationRecord>& records,const MapCreationContext& context,MapCreationPlan& result,std::string& error) {
    try {
        Index index;validate(records,context,index);MapCreationPlan planned;
        struct Pending {size_t ordinal;uint32_t depth;};
        for(size_t root=0;root<records.size();++root) {
            if(records[root].parentId)continue;
            const auto& top=records[root];
            const bool sourceInactive=!top.sourceActive&&!context.editingMode;
            const bool rangeDeferred=!sourceInactive&&top.activateAtRange&&!inRange(top,context);
            const bool deferred=sourceInactive||rangeDeferred;
            std::vector<Pending> pending={{root,0}};
            while(!pending.empty()) {
                const auto at=pending.back();pending.pop_back();const auto& record=records[at.ordinal];
                if(deferred) {
                    MapDeferredEntry entry;entry.id=record.id;entry.parentId=record.parentId;entry.deferredRootId=top.id;entry.sourceOrdinal=at.ordinal;
                    entry.reason=at.depth?MapDeferredReason::AncestorDeferred:(sourceInactive?MapDeferredReason::SourceInactive:MapDeferredReason::RangeDeferred);
                    entry.nativeInactiveRegistration=at.depth==0;entry.rangeRegistration=at.depth==0&&top.activateAtRange;
                    planned.deferred.push_back(entry);
                } else {
                    MapCreationEntry entry;entry.id=record.id;entry.parentId=record.parentId;entry.sourceOrdinal=at.ordinal;entry.depth=at.depth;
                    entry.kind=at.depth?MapCreationKind::Obstacle:kind(record);entry.attachedChild=at.depth!=0;entry.definitionTriggerOnSpawn=record.triggerOnSpawn;
                    planned.creation.push_back(entry);
                }
                // Even deferred descendants are enumerated only for explicit
                // accounting. Native does not create/register them at this point.
                for(auto child=record.childIds.rbegin();child!=record.childIds.rend();++child)pending.push_back({index.at(*child),at.depth+1});
            }
        }
        require(planned.creation.size()+planned.deferred.size()==records.size(),"Internal map plan lost source records");
        result=std::move(planned);error.clear();return true;
    }catch(const std::exception& failure){error=failure.what();return false;}
}
std::string dumpMapCreationPlan(const MapCreationPlan& plan) {
    std::ostringstream out;out<<"MAP_CREATION planned="<<plan.creation.size()<<" deferred="<<plan.deferred.size()<<" order=source_roots_ordered_depth_first_children activation=not_executed\n";
    for(const auto& entry:plan.creation)out<<"create id="<<entry.id<<" parent="<<entry.parentId<<" ordinal="<<entry.sourceOrdinal<<" depth="<<entry.depth<<" kind="<<kindName(entry.kind)<<" attached="<<entry.attachedChild<<" triggerDefinition="<<entry.definitionTriggerOnSpawn<<'\n';
    for(const auto& entry:plan.deferred)out<<"defer id="<<entry.id<<" parent="<<entry.parentId<<" ordinal="<<entry.sourceOrdinal<<" root="<<entry.deferredRootId<<" reason="<<reasonName(entry.reason)<<" inactiveRegistration="<<entry.nativeInactiveRegistration<<" rangeRegistration="<<entry.rangeRegistration<<'\n';
    return out.str();
}
