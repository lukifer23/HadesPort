// Focused read-only debugger against decoded original RoomOpening metadata.
#include "../app/map_creation.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
void require(bool ok,const std::string& detail){if(!ok)throw std::runtime_error(detail);}
int main(int argc,char** argv) {
    if(argc!=2)return 64;
    try {
        std::ifstream input(argv[1]);size_t count=0;input>>count;
        std::vector<MapCreationRecord> records;
        for(size_t n=0;n<count;++n) {
            MapCreationRecord record;int player=-1;size_t children=0;
            input>>record.id>>std::quoted(record.name)>>record.sourceActive>>record.doCreatePresence>>record.activateAtRange>>record.activationRange>>record.x>>record.y>>record.parentId>>record.dataType>>player>>record.triggerOnSpawn>>children;
            if(player>=0)record.playerControlled=player!=0;
            for(size_t i=0;i<children;++i){int32_t id=0;input>>id;record.childIds.push_back(id);}
            require(static_cast<bool>(input),"Truncated source debug records");records.push_back(std::move(record));
        }
        MapCreationPlan plan;std::string error;MapCreationContext context;
        require(planMapCreation(records,context,plan,error),error);
        require(records.size()==1037&&plan.creation.size()==1033&&plan.deferred.size()==4,"Unexpected source partition");
        std::set<int32_t> created;size_t attached=0,players=0;
        for(const auto& entry:plan.creation) {
            if(entry.attachedChild){require(created.contains(entry.parentId),"Child before parent");++attached;}
            if(entry.kind==MapCreationKind::PlayerUnit)++players;
            require(created.insert(entry.id).second,"Repeated planned ID");
        }
        require(attached==182&&players==1,"Unexpected original child/player count");
        for(const auto& entry:plan.deferred)require(entry.reason==MapDeferredReason::SourceInactive&&entry.nativeInactiveRegistration,"Unexpected original deferred reason");
        // Verify the recovered child rule, with all root inputs unchanged.
        auto changed=records;const auto child=std::find_if(changed.begin(),changed.end(),[](const auto& r){return r.parentId!=0;});
        require(child!=changed.end(),"Missing original child");child->sourceActive=false;child->activateAtRange=true;
        MapCreationPlan altered;require(planMapCreation(changed,context,altered,error),error);require(altered.creation.size()==1033,"Child incorrectly used top-level activation gate");
        // Transactional failure diagnostics for a broken real attachment and
        // absent payload are useful before any live entity reconstruction.
        changed=records;auto parent=std::find_if(changed.begin(),changed.end(),[](const auto& r){return !r.childIds.empty();});parent->childIds.pop_back();
        require(!planMapCreation(changed,context,altered,error)&&error.find("omits child")!=std::string::npos,"Broken attachment accepted");
        changed=records;changed.front().doCreatePresence=false;
        require(!planMapCreation(changed,context,altered,error)&&error.find("absent serialized payload")!=std::string::npos,"Absent payload treated as inactivity");
        // Reuse a real unattached obstacle with no children to isolate exact range
        // frame/location boundary without modifying the production source data.
        auto ranged=*std::find_if(records.begin(),records.end(),[](const auto& r){return r.dataType==1&&r.parentId==0&&r.childIds.empty()&&r.sourceActive;});
        ranged.activateAtRange=true;ranged.activationRange=25;
        require(planMapCreation({ranged},context,altered,error)&&altered.creation.empty()&&altered.deferred[0].reason==MapDeferredReason::RangeDeferred,"Range gate bypassed missing native player/frame");
        MapCreationContext within;within.worldFrame=2;within.firstPlayerLocation=MapCreationPoint{ranged.x+25,ranged.y};
        require(planMapCreation({ranged},within,altered,error)&&altered.creation.size()==1,"Inclusive native range boundary failed");
        // Close a real parent-child edge into a reciprocal cycle. Validation
        // must diagnose the cycle before any partial output is committed.
        changed=records;auto cycleParent=std::find_if(changed.begin(),changed.end(),[](const auto& r){return !r.parentId&&!r.childIds.empty();});
        const int32_t childId=cycleParent->childIds.front();const int32_t parentId=cycleParent->id;
        auto cycleChild=std::find_if(changed.begin(),changed.end(),[&](const auto& r){return r.id==childId;});
        cycleParent->parentId=childId;cycleChild->childIds.push_back(parentId);
        require(!planMapCreation(changed,context,altered,error)&&error.find("cycle")!=std::string::npos,"Reciprocal cycle accepted");
        std::cout<<"ORIGINAL_MAP_CREATION_PLAN_OK records=1037 create=1033 attached=182 player=1 inactive=4\n"<<dumpMapCreationPlan(plan);
        return 0;
    }catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 2;}
}
